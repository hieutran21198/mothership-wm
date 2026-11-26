//go:build linux && cgo && panel

#include "analog-glass.h"
#include "map-pointer.h"
#include <math.h>

typedef struct {
	GtkWidget parent;
	GdkFrameClock *clock;
	gulong after_paint;
	int slot;
	gboolean detail,inherit_backdrop;
} AnalogGlass;
typedef struct { GtkWidgetClass parent; } AnalogGlassClass;
G_DEFINE_TYPE(AnalogGlass,analog_glass,GTK_TYPE_WIDGET)

enum { CORNER_CUT=9, CORNER_ARM=21, DETAIL_CUT=6, DETAIL_ARM=13 };
static GList *panes;

static GskPath *outline(int w,int h,int c) {
	GskPathBuilder *b=gsk_path_builder_new();
	gsk_path_builder_move_to(b,c,0);
	gsk_path_builder_line_to(b,w-c,0);
	gsk_path_builder_line_to(b,w,c);
	gsk_path_builder_line_to(b,w,h-c);
	gsk_path_builder_line_to(b,w-c,h);
	gsk_path_builder_line_to(b,c,h);
	gsk_path_builder_line_to(b,0,h-c);
	gsk_path_builder_line_to(b,0,c);
	gsk_path_builder_close(b);
	return gsk_path_builder_free_to_path(b);
}

static void corner(cairo_t *cr,int x,int y,int sx,int sy,int arm,int cut) {
	cairo_move_to(cr,x+sx*arm,y);
	cairo_line_to(cr,x+sx*cut,y);
	cairo_line_to(cr,x,y+sy*cut);
	cairo_line_to(cr,x,y+sy*arm);
}

static void analog_glass_snapshot(GtkWidget *self,GtkSnapshot *s) {
	int w=gtk_widget_get_width(self),h=gtk_widget_get_height(self);
	gboolean detail=((AnalogGlass *)self)->detail;
	int arm=MIN(detail?DETAIL_ARM:CORNER_ARM,(MIN(w,h)-4)/2);
	int chamfer=detail?DETAIL_CUT:CORNER_CUT;
	if(arm<=chamfer) return;
	graphene_rect_t bounds=GRAPHENE_RECT_INIT(0,0,w,h),pos;
	GskPath *cut=outline(w,h,chamfer);
	GtkRoot *root=gtk_widget_get_root(self);
	if(((AnalogGlass *)self)->slot<0 && !((AnalogGlass *)self)->inherit_backdrop && root && gtk_widget_compute_bounds(self,GTK_WIDGET(root),&pos))
		wallpaper_analog_backdrop(s,gtk_widget_get_width(GTK_WIDGET(root)),
			gtk_widget_get_height(GTK_WIDGET(root)),pos.origin.x,pos.origin.y,w,h,cut);
	const GdkRGBA tint=detail?(GdkRGBA){.024,.059,.09,.92}:(GdkRGBA){.035,.081,.122,.78};
	gtk_snapshot_append_fill(s,cut,GSK_FILL_RULE_WINDING,&tint);
	gsk_path_unref(cut);
	cairo_t *cr=gtk_snapshot_append_cairo(s,&bounds);
	corner(cr,1,1,1,1,arm,chamfer);corner(cr,w-1,1,-1,1,arm,chamfer);
	corner(cr,w-1,h-1,-1,-1,arm,chamfer);corner(cr,1,h-1,1,-1,arm,chamfer);
	cairo_set_source_rgba(cr,.49,.82,.90,.88);
	cairo_set_line_width(cr,detail?.85:1);cairo_stroke(cr);
	cairo_destroy(cr);
}

static void glass_after_paint(GdkFrameClock *clock,gpointer data) {
	(void)clock;
	AnalogGlass *g=data;
	GtkWidget *widget=GTK_WIDGET(g);
	GtkRoot *root=gtk_widget_get_root(widget);
	graphene_rect_t bounds;
	if(!root || !gtk_widget_compute_bounds(widget,GTK_WIDGET(root),&bounds)) return;
	double alpha=1.;
	for(GtkWidget *p=widget;p && p!=GTK_WIDGET(root);p=gtk_widget_get_parent(p))
		alpha*=gtk_widget_get_opacity(p);
	map_pointer_scene_glass(g->slot,lround(bounds.origin.x),lround(bounds.origin.y),
		lround(bounds.size.width),lround(bounds.size.height),lround(alpha*1000));
}

static void glass_end(AnalogGlass *g) {
	if(!g->clock) return;
	g_signal_handler_disconnect(g->clock,g->after_paint);
	g_clear_object(&g->clock);
	map_pointer_scene_glass(g->slot,0,0,0,0,0);
}

static void analog_glass_dispose(GObject *object) {
	glass_end((AnalogGlass *)object);
	panes=g_list_remove(panes,object);
	G_OBJECT_CLASS(analog_glass_parent_class)->dispose(object);
}

static void glass_start(AnalogGlass *g) {
	if(g->slot<0 || g->clock) return;
	GdkFrameClock *clock=gtk_widget_get_frame_clock(GTK_WIDGET(g));
	if(!clock) return;
	g->clock=g_object_ref(clock);
	g->after_paint=g_signal_connect(clock,"after-paint",G_CALLBACK(glass_after_paint),g);
}
static void analog_glass_map(GtkWidget *w) {
	GTK_WIDGET_CLASS(analog_glass_parent_class)->map(w);
	glass_start((AnalogGlass *)w);
}
static void analog_glass_unmap(GtkWidget *w) {
	glass_end((AnalogGlass *)w);
	GTK_WIDGET_CLASS(analog_glass_parent_class)->unmap(w);
}
static void analog_glass_class_init(AnalogGlassClass *c) {
	GTK_WIDGET_CLASS(c)->map=analog_glass_map;
	GTK_WIDGET_CLASS(c)->unmap=analog_glass_unmap;
	GTK_WIDGET_CLASS(c)->snapshot=analog_glass_snapshot;
	G_OBJECT_CLASS(c)->dispose=analog_glass_dispose;
}

static void analog_glass_init(AnalogGlass *g) {
	g->slot=-1;
	gtk_widget_set_can_target(GTK_WIDGET(g),FALSE);
	panes=g_list_prepend(panes,g);
}

GtkWidget *analog_glass_new(void) {
	return g_object_new(analog_glass_get_type(),NULL);
}

void analog_glass_set_scene_backdrop(GtkWidget *self,int slot) {
	AnalogGlass *g=(AnalogGlass *)self;
	if(slot>=0 && g_strcmp0(g_getenv("MOTHERSHIP_EXPERIMENT_GRID_FROST"),"0")==0) slot=-1;
	if(g->slot==slot) return;
	glass_end(g);
	g->slot=slot;
	if(gtk_widget_get_mapped(self)) glass_start(g);
	gtk_widget_queue_draw(self);
}

struct item_motion {
	GtkWidget *glass;
	gboolean hovered,selected;
	guint tick;
	gint64 started;
	double from,to;
};

static void apply_item_opacity(struct item_motion *state,double opacity) {
	gtk_widget_set_opacity(state->glass,opacity);
	gtk_widget_set_visible(state->glass,opacity>0);
}
static gboolean item_frame(GtkWidget *item,GdkFrameClock *clock,gpointer data) {
	(void)item;
	struct item_motion *state=data;
	const double duration=state->to>state->from?150000.:180000.;
	double t=CLAMP((gdk_frame_clock_get_frame_time(clock)-state->started)/duration,0.,1.);
	double eased=t*t*(3.-2.*t);
	apply_item_opacity(state,state->from+(state->to-state->from)*eased);
	if(t<1) return G_SOURCE_CONTINUE;
	state->tick=0;
	return G_SOURCE_REMOVE;
}
static void sync_item(GtkWidget *item,struct item_motion *state) {
	double target=state->selected || state->hovered;
	gboolean animations=TRUE;
	g_object_get(gtk_widget_get_settings(item),"gtk-enable-animations",&animations,NULL);
	if(!gtk_widget_get_mapped(item) || !animations) {
		if(state->tick) gtk_widget_remove_tick_callback(item,state->tick);
		state->tick=0;
		state->to=target;
		apply_item_opacity(state,target);
		return;
	}
	if(state->tick && state->to==target) return;
	state->from=gtk_widget_get_opacity(state->glass);
	state->to=target;
	if(state->from==target) {
		if(state->tick) gtk_widget_remove_tick_callback(item,state->tick);
		state->tick=0;
		apply_item_opacity(state,target);
		return;
	}
	state->started=g_get_monotonic_time();
	if(!state->tick) state->tick=gtk_widget_add_tick_callback(item,item_frame,state,NULL);
}
static void item_unmap(GtkWidget *item,gpointer data) {
	struct item_motion *state=data;
	state->hovered=FALSE;
	if(state->tick) gtk_widget_remove_tick_callback(item,state->tick);
	state->tick=0;
	state->to=state->selected;
	apply_item_opacity(state,state->to);
}
static void item_enter(GtkEventControllerMotion *motion,double x,double y,gpointer data) {
	(void)x;(void)y;
	GtkWidget *item=gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(motion));
	if(!item) return;
	struct item_motion *state=data;
	state->hovered=TRUE;
	sync_item(item,state);
}
static void item_leave(GtkEventControllerMotion *motion,gpointer data) {
	GtkWidget *item=gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(motion));
	if(!item) return;
	struct item_motion *state=data;
	state->hovered=FALSE;
	sync_item(item,state);
}

GtkWidget *analog_item_new(GtkWidget *content) {
	GtkWidget *item=gtk_overlay_new();
	AnalogGlass *glass=g_object_new(analog_glass_get_type(),NULL);
	glass->detail=TRUE;
	gtk_overlay_set_child(GTK_OVERLAY(item),GTK_WIDGET(glass));
	struct item_motion *state=g_new0(struct item_motion,1);
	state->glass=GTK_WIDGET(glass);
	g_object_set_data_full(G_OBJECT(item),"analog-item-motion",state,g_free);
	apply_item_opacity(state,0);
	gtk_overlay_add_overlay(GTK_OVERLAY(item),content);
	gtk_overlay_set_measure_overlay(GTK_OVERLAY(item),content,TRUE);
	GtkEventController *motion=gtk_event_controller_motion_new();
	g_signal_connect(motion,"enter",G_CALLBACK(item_enter),state);
	g_signal_connect(motion,"leave",G_CALLBACK(item_leave),state);
	gtk_widget_add_controller(item,motion);
	g_signal_connect(item,"unmap",G_CALLBACK(item_unmap),state);
	return item;
}

void analog_item_set_inherit_backdrop(GtkWidget *item) {
	if(g_strcmp0(g_getenv("MOTHERSHIP_EXPERIMENT_GRID_FROST"),"0")==0) return;
	struct item_motion *state=g_object_get_data(G_OBJECT(item),"analog-item-motion");
	((AnalogGlass *)state->glass)->inherit_backdrop=TRUE;
}

void analog_item_select(GtkWidget *item,gboolean selected) {
	struct item_motion *state=g_object_get_data(G_OBJECT(item),"analog-item-motion");
	state->selected=selected;
	sync_item(item,state);
}

void analog_glass_wallpaper_changed(void) {
	for(GList *l=panes;l;l=l->next) {
		AnalogGlass *g=l->data;
		// The compositor updates scene-backed panes on its own. A video frame
		// must not invalidate or repaint the foreground shell to drive blur.
		if(g->slot<0 && !g->inherit_backdrop && gtk_widget_get_mapped(GTK_WIDGET(g)))
			gtk_widget_queue_draw(GTK_WIDGET(g));
	}
}
