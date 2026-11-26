//go:build linux && cgo && panel

#include "wallpaper-video.h"
#include <gmodule.h>

// Optional GStreamer 1.x runtime. Keep the PNG path independent of GStreamer
// and only resolve this stable C ABI when an MP4 is requested.
typedef struct _GstElement GstElement;
typedef struct _GstBus GstBus;
typedef struct _GstMessage GstMessage;
enum { GST_NULL=1, GST_PAUSED=3, GST_PLAYING=4, GST_FAILURE=0, GST_SUCCESS=1 };
enum { GST_TIME=3, GST_SEEK_FLUSH=1, GST_SEEK_KEY_UNIT=4 };
static struct {
	gboolean (*gst_init_check)(int *,char ***,GError **);
	GstElement *(*gst_parse_launch)(const char *,GError **);
	GstElement *(*gst_bin_get_by_name)(gpointer,const char *);
	int (*gst_element_set_state)(GstElement *,int);
	int (*gst_element_get_state)(GstElement *,int *,int *,guint64);
	GstBus *(*gst_element_get_bus)(GstElement *);
	gboolean (*gst_element_seek_simple)(GstElement *,int,int,gint64);
	void (*gst_bus_add_signal_watch)(GstBus *);
	void (*gst_bus_remove_signal_watch)(GstBus *);
	void (*gst_message_parse_error)(GstMessage *,GError **,char **);
} gst;
static GModule *runtime;

static gboolean load_runtime(void) {
	if (runtime) return TRUE;
	const char *path=g_getenv("MOTHERSHIP_GST_LIBRARY");
	GModule *m=g_module_open(path && *path?path:"libgstreamer-1.0.so.0",G_MODULE_BIND_LAZY);
	if (!m) return FALSE;
#define LOAD(symbol) if (!g_module_symbol(m,#symbol,(gpointer *)&gst.symbol)) goto unavailable
	LOAD(gst_init_check);
	LOAD(gst_parse_launch);
	LOAD(gst_bin_get_by_name);
	LOAD(gst_element_set_state);
	LOAD(gst_element_get_state);
	LOAD(gst_element_get_bus);
	LOAD(gst_element_seek_simple);
	LOAD(gst_bus_add_signal_watch);
	LOAD(gst_bus_remove_signal_watch);
	LOAD(gst_message_parse_error);
#undef LOAD
	g_module_make_resident(m);
	if (!gst.gst_init_check(NULL,NULL,NULL)) goto unavailable;
	runtime=m;
	return TRUE;
unavailable:
	g_module_close(m);
	return FALSE;
}

struct wallpaper_video {
	GstElement *pipeline;
	GdkPaintable *paintable;
	GstBus *bus;
	void (*failed)(void);
};

void wallpaper_video_free(struct wallpaper_video *v) {
	if (!v) return;
	if (v->bus) {
		g_signal_handlers_disconnect_by_data(v->bus,v);
		gst.gst_bus_remove_signal_watch(v->bus);
		g_object_unref(v->bus);
	}
	gst.gst_element_set_state(v->pipeline,GST_NULL);
	g_object_unref(v->pipeline);
	g_clear_object(&v->paintable);
	g_free(v);
}

static struct wallpaper_video *prepare(const char *path,gboolean gpu) {
	// Only a local H.264 track reaches the sink. No audio or network input.
	const char *decoder=gpu?"nvh264dec":"avdec_h264 max-threads=2";
	g_autofree char *pipeline=g_strdup_printf(
		"filesrc name=file ! qtdemux ! h264parse ! "
		"video/x-h264,width=(int)[1,4096],height=(int)[1,2160] ! "
		"%s ! videoconvert ! gtk4paintablesink name=video sync=true qos=true "
		"max-lateness=33000000",decoder);
	g_autoptr(GError) error=NULL;
	GstElement *main=gst.gst_parse_launch(pipeline,&error);
	if (error || !main) {
		if (main) g_object_unref(main);
		return NULL;
	}
	struct wallpaper_video *v=g_new0(struct wallpaper_video,1);
	v->pipeline=main;
	GstElement *src=gst.gst_bin_get_by_name(main,"file");
	GstElement *sink=gst.gst_bin_get_by_name(main,"video");
	g_object_set(src,"location",path,NULL);
	g_object_get(sink,"paintable",&v->paintable,NULL);
	g_object_unref(src);
	g_object_unref(sink);
	if (!v->paintable || gst.gst_element_set_state(main,GST_PAUSED)==GST_FAILURE ||
		gst.gst_element_get_state(main,NULL,NULL,1200*G_GUINT64_CONSTANT(1000000))!=GST_SUCCESS) {
		wallpaper_video_free(v);
		return NULL;
	}
	g_message("Mothership wallpaper: MP4 using %s",gpu?"NVDEC":"software H.264");
	return v;
}

struct wallpaper_video *wallpaper_video_prepare(const char *path) {
	if (!load_runtime()) return NULL;
	struct wallpaper_video *v=prepare(path,TRUE);
	return v?v:prepare(path,FALSE);
}

static void eos(GstBus *bus,GstMessage *message,gpointer data) {
	(void)bus;(void)message;
	struct wallpaper_video *v=data;
	if (!gst.gst_element_seek_simple(v->pipeline,GST_TIME,GST_SEEK_FLUSH|GST_SEEK_KEY_UNIT,0)) {
		g_warning("Mothership wallpaper: could not loop MP4; restoring PNG");
		v->failed();
	}
}

static void playback_error(GstBus *bus,GstMessage *message,gpointer data) {
	(void)bus;
	struct wallpaper_video *v=data;
	g_autoptr(GError) error=NULL;
	gst.gst_message_parse_error(message,&error,NULL);
	g_warning("Mothership wallpaper: %s; restoring PNG",error?error->message:"MP4 playback error");
	v->failed();
}

void wallpaper_video_attach(struct wallpaper_video *v,GtkPicture *picture,void (*failed)(void)) {
	gtk_picture_set_paintable(picture,v->paintable);
	v->failed=failed;
	v->bus=gst.gst_element_get_bus(v->pipeline);
	gst.gst_bus_add_signal_watch(v->bus);
	g_signal_connect(v->bus,"message::eos",G_CALLBACK(eos),v);
	g_signal_connect(v->bus,"message::error",G_CALLBACK(playback_error),v);
	if (gst.gst_element_set_state(v->pipeline,GST_PLAYING)==GST_FAILURE) failed();
}
