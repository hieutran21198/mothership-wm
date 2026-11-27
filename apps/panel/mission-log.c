//go:build linux && cgo && panel

#include "mission-log.h"
#include "analog-button.h"
#include "analog-glass.h"
#include "map-guide.h"
#include "shell-type.h"
#include <math.h>
#include <stdlib.h>

extern void panelMissionLoad(int);
extern void panelMissionClear(void);

enum { LANE_HEIGHT=28, AXIS_HEIGHT=24, LABEL_WIDTH=136, TOTAL_WIDTH=66, MAX_SCROLL=336 };

struct lane { char *label; gboolean deleted,default_node; gint64 total; };
struct seg { int lane; gint64 start,end; gboolean live; };

static GtkWidget *pane,*day_label,*timeline,*status,*clear_button,*keys_area;
static void (*on_region)(void);
static gboolean open;
static int offset;
static gboolean confirm_clear;
static guint confirm_timer,live_timer;
static GArray *lanes,*segs;
static gint64 day_limit=86400,posted_at;
static char *error_text;
static gboolean loading;

static void lanes_clear(void) {
	if(!lanes) {
		lanes=g_array_new(FALSE,TRUE,sizeof(struct lane));
		segs=g_array_new(FALSE,TRUE,sizeof(struct seg));
	}
	for(guint i=0;i<lanes->len;i++) g_free(g_array_index(lanes,struct lane,i).label);
	g_array_set_size(lanes,0);
	g_array_set_size(segs,0);
	g_clear_pointer(&error_text,g_free);
}

static char *duration(gint64 s) {
	if(s<60) return g_strdup("<1m");
	gint64 m=s/60;
	if(m<60) return g_strdup_printf("%" G_GINT64_FORMAT "m",m);
	return g_strdup_printf("%" G_GINT64_FORMAT "h %02" G_GINT64_FORMAT "m",m/60,m%60);
}

// Live segments grow locally between posts; desktopd is not polled.
static gint64 live_extra(void) {
	return posted_at?MAX((gint64)0,g_get_real_time()/G_USEC_PER_SEC-posted_at):0;
}

static void text_at(cairo_t *cr,const char *text,double x,double y,double w,double size,PangoWeight weight,
		double r,double g,double b,gboolean right) {
	PangoLayout *l=pango_cairo_create_layout(cr);
	PangoFontDescription *f=shell_font_description(NULL,size,weight);
	pango_layout_set_font_description(l,f);pango_font_description_free(f);
	pango_layout_set_text(l,text,-1);
	pango_layout_set_single_paragraph_mode(l,TRUE);
	pango_layout_set_width(l,MAX(1,(int)w)*PANGO_SCALE);
	pango_layout_set_ellipsize(l,PANGO_ELLIPSIZE_END);
	if(right) pango_layout_set_alignment(l,PANGO_ALIGN_RIGHT);
	int th;pango_layout_get_pixel_size(l,NULL,&th);
	cairo_set_source_rgb(cr,r,g,b);
	cairo_move_to(cr,x,y-th*.5);
	pango_cairo_show_layout(cr,l);
	g_object_unref(l);
}

static void draw_timeline(GtkDrawingArea *area,cairo_t *cr,int w,int h,gpointer unused) {
	(void)area;(void)unused;
	if(error_text || !lanes || !lanes->len) {
		text_at(cr,error_text?error_text:loading?"Loading…":"No missions logged.",0,h*.5,w,13,PANGO_WEIGHT_MEDIUM,.64,.75,.81,FALSE);
		return;
	}
	double x0=LABEL_WIDTH,x1=MAX(x0+40,w-TOTAL_WIDTH-8),span=x1-x0;
	// Hour axis every three hours, with faint guides through the lanes.
	for(int hour=0;hour<=24;hour+=3) {
		double x=x0+span*hour*3600./day_limit;
		cairo_set_source_rgba(cr,.36,.52,.60,hour%12?.22:.38);
		cairo_set_line_width(cr,1);
		cairo_move_to(cr,floor(x)+.5,AXIS_HEIGHT-4);cairo_line_to(cr,floor(x)+.5,h);cairo_stroke(cr);
		g_autofree char *tick=g_strdup_printf("%02d",hour);
		text_at(cr,tick,x-14,AXIS_HEIGHT*.42,28,10,PANGO_WEIGHT_SEMIBOLD,.55,.68,.75,FALSE);
	}
	gint64 extra=live_extra();
	for(guint i=0;i<lanes->len;i++) {
		struct lane *l=&g_array_index(lanes,struct lane,i);
		double y=AXIS_HEIGHT+i*LANE_HEIGHT+LANE_HEIGHT*.5;
		// A small diamond stands for the Node; Default Node is hollow.
		cairo_move_to(cr,6,y);cairo_line_to(cr,10,y-4);cairo_line_to(cr,14,y);cairo_line_to(cr,10,y+4);cairo_close_path(cr);
		cairo_set_source_rgba(cr,.56,.85,.89,l->deleted?.4:.9);
		if(l->default_node) {cairo_set_line_width(cr,1);cairo_stroke(cr);} else cairo_fill(cr);
		g_autofree char *name=l->deleted?g_strdup_printf("%s (deleted)",l->label):g_strdup(l->label);
		double ink=l->deleted?.52:.86;
		text_at(cr,name,22,y,LABEL_WIDTH-30,12,PANGO_WEIGHT_MEDIUM,ink,ink+.06,ink+.1,FALSE);
		cairo_set_source_rgba(cr,.20,.31,.37,.8);
		cairo_rectangle(cr,x0,floor(y)-.5,span,1);cairo_fill(cr);
		gint64 total=l->total;
		for(guint j=0;j<segs->len;j++) {
			struct seg *s=&g_array_index(segs,struct seg,j);
			if(s->lane!=(int)i) continue;
			gint64 end=s->live?MIN(day_limit,s->end+extra):s->end;
			if(s->live) total+=end-s->end;
			double a=x0+span*s->start/day_limit,b=MAX(a+1.5,x0+span*end/day_limit);
			cairo_set_source_rgba(cr,.43,.83,.86,l->deleted?.5:.86);
			cairo_rectangle(cr,a,y-5,b-a,10);cairo_fill(cr);
			if(s->live) {
				cairo_set_source_rgb(cr,.82,.98,.99);
				cairo_rectangle(cr,b-2.5,y-7,2.5,14);cairo_fill(cr);
			}
		}
		g_autofree char *sum=duration(total);
		text_at(cr,sum,x1+8,y,TOTAL_WIDTH,12,PANGO_WEIGHT_SEMIBOLD,.91,.95,.98,TRUE);
	}
}

static void draw_keys(GtkDrawingArea *area,cairo_t *cr,int w,int h,gpointer unused) {
	(void)area;(void)h;(void)unused;mission_log_guide_draw(cr,w);
}

static void resize_timeline(void) {
	int rows=lanes?(int)lanes->len:0;
	gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(timeline),rows?AXIS_HEIGHT+rows*LANE_HEIGHT+4:72);
	gtk_widget_queue_draw(timeline);
}

static void request_day(int next) {
	offset=CLAMP(next,-29,0);
	loading=TRUE;
	gtk_widget_queue_draw(timeline);
	panelMissionLoad(offset);
}

static gboolean apply_post(gpointer data) {
	char *text=data;
	lanes_clear();
	loading=FALSE;
	posted_at=g_get_real_time()/G_USEC_PER_SEC;
	gboolean today=FALSE;
	g_auto(GStrv) lines=g_strsplit(text,"\n",-1);
	for(int i=0;lines[i];i++) {
		g_auto(GStrv) f=g_strsplit(lines[i],"\t",-1);
		guint n=g_strv_length(f);
		if(n==2 && g_str_equal(f[0],"error")) {
			error_text=g_strdup_printf("Mission Log unavailable: %s",f[1]);
		} else if(n==6 && g_str_equal(f[0],"day")) {
			gtk_label_set_text(GTK_LABEL(day_label),f[1]);
			today=atoi(f[2])!=0;
			day_limit=MAX(1,g_ascii_strtoll(f[5],NULL,10));
		} else if(n==5 && g_str_equal(f[0],"lane")) {
			struct lane l={g_strdup(f[1]),atoi(f[2])!=0,atoi(f[3])!=0,g_ascii_strtoll(f[4],NULL,10)};
			g_array_append_val(lanes,l);
		} else if(n==5 && g_str_equal(f[0],"seg")) {
			struct seg s={atoi(f[1]),g_ascii_strtoll(f[2],NULL,10),g_ascii_strtoll(f[3],NULL,10),atoi(f[4])!=0};
			if(s.lane>=0 && s.lane<(int)lanes->len) g_array_append_val(segs,s);
		}
	}
	if(!today) posted_at=0;
	resize_timeline();
	return G_SOURCE_REMOVE;
}
void mission_log_post(const char *text) {
	g_idle_add_full(G_PRIORITY_DEFAULT,apply_post,g_strdup(text?text:""),g_free);
}
static gboolean apply_status(gpointer data) {
	gtk_label_set_text(GTK_LABEL(status),data);
	gtk_widget_set_visible(status,TRUE);
	return G_SOURCE_REMOVE;
}
void mission_log_status(const char *text) {
	g_idle_add_full(G_PRIORITY_DEFAULT,apply_status,g_strdup(text?text:""),g_free);
}

static void reset_confirm(void) {
	confirm_clear=FALSE;
	g_clear_handle_id(&confirm_timer,g_source_remove);
	analog_button_label(clear_button,"Clear history");
}
static gboolean confirm_expired(gpointer unused) {(void)unused;confirm_timer=0;reset_confirm();return G_SOURCE_REMOVE;}
// Clearing asks once: the first press arms it for four seconds.
static void clear_clicked(GtkButton *b,gpointer unused) {
	(void)b;(void)unused;
	if(!confirm_clear) {
		confirm_clear=TRUE;
		analog_button_label(clear_button,"Press again to clear");
		confirm_timer=g_timeout_add_seconds(4,confirm_expired,NULL);
		return;
	}
	reset_confirm();
	panelMissionClear();
}

static gboolean live_tick(gpointer unused) {
	(void)unused;
	if(posted_at) gtk_widget_queue_draw(timeline);
	return G_SOURCE_CONTINUE;
}

static gboolean pane_key(GtkEventControllerKey *keys,guint key,guint code,GdkModifierType mods,gpointer unused) {
	(void)keys;(void)code;(void)unused;
	if(mods&(GDK_CONTROL_MASK|GDK_ALT_MASK|GDK_SUPER_MASK|GDK_META_MASK)) return FALSE;
	switch(gdk_keyval_to_lower(key)) {
	case GDK_KEY_a: case GDK_KEY_Left: request_day(offset-1);return TRUE;
	case GDK_KEY_d: case GDK_KEY_Right: request_day(offset+1);return TRUE;
	case GDK_KEY_Escape: case GDK_KEY_q: case GDK_KEY_l: mission_log_dismiss();return TRUE;
	default: return FALSE;
	}
}

void mission_log_init(GtkOverlay *root,void (*region)(void)) {
	on_region=region;
	pane=gtk_overlay_new();
	gtk_overlay_set_child(GTK_OVERLAY(pane),analog_glass_new());
	GtkWidget *body=gtk_box_new(GTK_ORIENTATION_VERTICAL,10);
	gtk_widget_add_css_class(body,"notification-content");
	gtk_widget_add_css_class(body,"notification-log-content");
	gtk_overlay_add_overlay(GTK_OVERLAY(pane),body);
	gtk_overlay_set_measure_overlay(GTK_OVERLAY(pane),body,TRUE);
	gtk_widget_set_focusable(pane,TRUE);
	gtk_widget_set_halign(pane,GTK_ALIGN_START);
	gtk_widget_set_valign(pane,GTK_ALIGN_END);
	GtkWidget *head=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
	GtkWidget *title=gtk_label_new("Mission Log");
	gtk_widget_add_css_class(title,"notification-title");
	gtk_widget_add_css_class(title,"shell-system-title");
	gtk_label_set_xalign(GTK_LABEL(title),0);
	gtk_widget_set_hexpand(title,TRUE);
	gtk_box_append(GTK_BOX(head),title);
	day_label=gtk_label_new("");
	gtk_widget_add_css_class(day_label,"notification-meta");
	gtk_box_append(GTK_BOX(head),day_label);
	gtk_box_append(GTK_BOX(body),head);
	GtkWidget *scroll=gtk_scrolled_window_new();
	gtk_widget_add_css_class(scroll,"notification-log-scroll");
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll),TRUE);
	gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll),MAX_SCROLL);
	timeline=gtk_drawing_area_new();
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(timeline),draw_timeline,NULL,NULL);
	gtk_accessible_update_property(GTK_ACCESSIBLE(timeline),GTK_ACCESSIBLE_PROPERTY_LABEL,"Time per Node for the selected day",-1);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),timeline);
	gtk_box_append(GTK_BOX(body),scroll);
	status=gtk_label_new("");
	gtk_widget_add_css_class(status,"notification-meta");
	gtk_label_set_xalign(GTK_LABEL(status),0);
	gtk_widget_set_visible(status,FALSE);
	gtk_box_append(GTK_BOX(body),status);
	GtkWidget *foot=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,10);
	keys_area=gtk_drawing_area_new();
	gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(keys_area),20);
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(keys_area),draw_keys,NULL,NULL);
	gtk_widget_set_hexpand(keys_area,TRUE);
	gtk_widget_set_can_target(keys_area,FALSE);
	gtk_box_append(GTK_BOX(foot),keys_area);
	clear_button=gtk_button_new_with_label("Clear history");
	analog_button_style(clear_button,10,5);
	gtk_widget_set_focus_on_click(clear_button,FALSE);
	g_signal_connect(clear_button,"clicked",G_CALLBACK(clear_clicked),NULL);
	gtk_box_append(GTK_BOX(foot),clear_button);
	gtk_box_append(GTK_BOX(body),foot);
	GtkEventController *keys=gtk_event_controller_key_new();
	gtk_event_controller_set_propagation_phase(keys,GTK_PHASE_CAPTURE);
	g_signal_connect(keys,"key-pressed",G_CALLBACK(pane_key),NULL);
	gtk_widget_add_controller(pane,keys);
	gtk_widget_set_visible(pane,FALSE);
	gtk_overlay_add_overlay(root,pane);
	lanes_clear();
}

GtkWidget *mission_log_widget(void) { return pane; }
gboolean mission_log_visible(void) { return pane && open; }

void mission_log_toggle(void) {
	if(mission_log_dismiss()) return;
	open=TRUE;
	reset_confirm();
	gtk_widget_set_visible(status,FALSE);
	gtk_widget_set_visible(pane,TRUE);
	gtk_widget_grab_focus(pane);
	if(!live_timer) live_timer=g_timeout_add_seconds(30,live_tick,NULL);
	request_day(0);
	if(on_region) on_region();
}

gboolean mission_log_dismiss(void) {
	if(!pane || !open) return FALSE;
	open=FALSE;
	reset_confirm();
	g_clear_handle_id(&live_timer,g_source_remove);
	gtk_widget_set_visible(pane,FALSE);
	if(on_region) on_region();
	return TRUE;
}

void mission_log_layout(int w,int h,int center,int gap) {
	if(!pane) return;
	int width=MIN(640,MAX(1,w-48));
	gtk_widget_set_size_request(pane,width,-1);
	gtk_widget_set_margin_start(pane,CLAMP(center-width/2,24,MAX(24,w-24-width)));
	gtk_widget_set_margin_bottom(pane,gap);
	(void)h;
}
