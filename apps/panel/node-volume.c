//go:build linux && cgo && panel

#include "node-volume.h"
#include "analog-glass.h"
#include "analog-button.h"
#include <stdlib.h>

extern void panelNodeVolumeRequest(int,char *,int,int);
enum { WATCH, HIDE, SET_VOLUME, TOGGLE_APP, TOGGLE_NODE };
static GtkWidget *pane,*heading,*all,*notice,*list,*scroll,*tail;
static char *current,*last_rows;
static gboolean showing,master;
static int active_sliders,tail_x;
static void (*on_region)(void);

struct row {
	int pid,percent;
	guint delay;
	gboolean dragging;
	GtkWidget *readout;
};
static void send(int action,int pid,int percent) {
	if(current) panelNodeVolumeRequest(action,current,pid,percent);
}
static gboolean commit_slider(gpointer ptr) {
	struct row *r=ptr;
	r->delay=0;
	if(showing)send(SET_VOLUME,r->pid,r->percent);
	return G_SOURCE_REMOVE;
}
static void row_free(gpointer ptr) {
	struct row *r=ptr;
	if(r->delay)g_source_remove(r->delay);
	if(r->dragging)active_sliders--;
	g_free(r);
}
static void slider_press(GtkGestureClick *g,int count,double x,double y,gpointer ptr) {
	(void)g;(void)count;(void)x;(void)y;
	struct row *r=ptr;
	if(!r->dragging){r->dragging=TRUE;active_sliders++;}
}
static void slider_release(GtkGestureClick *g,int count,double x,double y,gpointer ptr) {
	(void)g;(void)count;(void)x;(void)y;
	struct row *r=ptr;
	if(r->dragging){r->dragging=FALSE;active_sliders--;}
}
static void slider_stopped(GtkGesture *g,GdkEventSequence *seq,gpointer ptr) {
	(void)g;(void)seq;
	struct row *r=ptr;
	if(r->dragging){r->dragging=FALSE;active_sliders--;}
}
static void value_changed(GtkRange *w,gpointer ptr) {
	struct row *r=ptr;
	r->percent=(int)(gtk_range_get_value(w)+.5);
	char value[24];g_snprintf(value,sizeof value,"%d%%",r->percent);
	gtk_label_set_text(GTK_LABEL(r->readout),value);
	if(r->delay)g_source_remove(r->delay);
	r->delay=g_timeout_add(130,commit_slider,r);
}
static void mute_app(GtkButton *w,gpointer ptr) {
	(void)w;
	send(TOGGLE_APP,GPOINTER_TO_INT(ptr),0);
}
static void mute_node(GtkButton *w,gpointer ptr) {
	(void)w;(void)ptr;
	send(TOGGLE_NODE,0,0);
}
static void dismiss(GtkButton *w,gpointer ptr) {
	(void)w;(void)ptr;node_volume_close();
}
static GtkWidget *icon_button(const char *icon,const char *name,GCallback on_click,gpointer data) {
	GtkWidget *b=gtk_button_new_from_icon_name(icon);
	analog_button_style(b,8,5);
	gtk_accessible_update_property(GTK_ACCESSIBLE(b),GTK_ACCESSIBLE_PROPERTY_LABEL,name,-1);
	gtk_widget_set_focus_on_click(b,FALSE);
	g_signal_connect(b,"clicked",on_click,data);
	return b;
}

static void add_row(int pid,int percent,gboolean muted,gboolean writable,const char *name,const char *icon) {
	struct row *state=g_new0(struct row,1);
	state->pid=pid;state->percent=percent;
	GtkWidget *line=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,12);
	gtk_widget_add_css_class(line,"node-volume-row");
	GtkWidget *app_icon=gtk_image_new_from_icon_name(*icon?icon:"audio-x-generic-symbolic");
	gtk_widget_add_css_class(app_icon,"node-volume-app-icon");
	gtk_widget_set_size_request(app_icon,30,30);
	gtk_widget_set_valign(app_icon,GTK_ALIGN_CENTER);
	gtk_box_append(GTK_BOX(line),app_icon);
	GtkWidget *detail=gtk_box_new(GTK_ORIENTATION_VERTICAL,2);
	gtk_widget_set_hexpand(detail,TRUE);
	gtk_box_append(GTK_BOX(line),detail);
	GtkWidget *labels=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);
	GtkWidget *app=gtk_label_new(name);
	gtk_label_set_ellipsize(GTK_LABEL(app),PANGO_ELLIPSIZE_END);
	gtk_label_set_xalign(GTK_LABEL(app),0);
	gtk_widget_set_hexpand(app,TRUE);
	gtk_box_append(GTK_BOX(labels),app);
	g_autofree char *v=g_strdup_printf("%d%%",percent);
	state->readout=gtk_label_new(v);
	gtk_box_append(GTK_BOX(labels),state->readout);
	gtk_box_append(GTK_BOX(detail),labels);
	GtkWidget *slider=gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,0,100,1);
	gtk_scale_set_draw_value(GTK_SCALE(slider),FALSE);
	gtk_range_set_value(GTK_RANGE(slider),CLAMP(percent,0,100));
	gtk_widget_set_sensitive(slider,writable);
	gtk_accessible_update_property(GTK_ACCESSIBLE(slider),GTK_ACCESSIBLE_PROPERTY_LABEL,name,-1);
	g_object_set_data_full(G_OBJECT(slider),"node-volume-state",state,row_free);
	g_signal_connect(slider,"value-changed",G_CALLBACK(value_changed),state);
	GtkGesture *track=gtk_gesture_click_new();
	gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(track),GTK_PHASE_CAPTURE);
	g_signal_connect(track,"pressed",G_CALLBACK(slider_press),state);
	g_signal_connect(track,"released",G_CALLBACK(slider_release),state);
	g_signal_connect(track,"cancel",G_CALLBACK(slider_stopped),state);
	gtk_widget_add_controller(slider,GTK_EVENT_CONTROLLER(track));
	gtk_box_append(GTK_BOX(detail),slider);
	GtkWidget *mute=icon_button(muted?"audio-volume-muted-symbolic":"audio-volume-high-symbolic",
		muted?"Unmute app":"Mute app",G_CALLBACK(mute_app),GINT_TO_POINTER(pid));
	analog_button_on(mute,muted);
	gtk_widget_set_sensitive(mute,!master);
	gtk_widget_set_valign(mute,GTK_ALIGN_CENTER);
	gtk_box_append(GTK_BOX(line),mute);
	gtk_box_append(GTK_BOX(list),line);
	gtk_box_append(GTK_BOX(list),gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
}

static void draw_tail(GtkDrawingArea *area,cairo_t *cr,int w,int h,gpointer data) {
	(void)area;(void)w;(void)data;
	cairo_move_to(cr,tail_x-9,.5);
	cairo_line_to(cr,tail_x,h-.5);
	cairo_line_to(cr,tail_x+9,.5);
	cairo_close_path(cr);
	cairo_set_source_rgba(cr,.035,.081,.122,.90);
	cairo_fill(cr);
	cairo_move_to(cr,tail_x-9,.5);
	cairo_line_to(cr,tail_x,h-.5);
	cairo_line_to(cr,tail_x+9,.5);
	cairo_set_source_rgba(cr,.49,.82,.90,.50);
	cairo_set_line_width(cr,1);
	cairo_stroke(cr);
}

void node_volume_init(GtkOverlay *root,void (*region)(void)) {
	on_region=region;
	pane=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);
	GtkWidget *board=gtk_overlay_new();
	gtk_overlay_set_child(GTK_OVERLAY(board),analog_glass_new());
	gtk_box_append(GTK_BOX(pane),board);
	tail=gtk_drawing_area_new();
	gtk_widget_set_size_request(tail,-1,9);
	gtk_widget_set_can_target(tail,FALSE);
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(tail),draw_tail,NULL,NULL);
	gtk_box_append(GTK_BOX(pane),tail);
	gtk_widget_set_halign(pane,GTK_ALIGN_START);
	gtk_widget_set_valign(pane,GTK_ALIGN_END);
	gtk_widget_set_visible(pane,FALSE);
	gtk_overlay_add_overlay(root,pane);
	GtkWidget *body=gtk_box_new(GTK_ORIENTATION_VERTICAL,9);
	gtk_widget_add_css_class(body,"notification-content");
	gtk_widget_add_css_class(body,"notification-log-content");
	gtk_widget_add_css_class(body,"node-volume-content");
	gtk_overlay_add_overlay(GTK_OVERLAY(board),body);
	gtk_overlay_set_measure_overlay(GTK_OVERLAY(board),body,TRUE);
	GtkWidget *bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
	GtkWidget *title=gtk_label_new("Volume");
	gtk_widget_add_css_class(title,"notification-title");
	gtk_widget_add_css_class(title,"shell-system-title");
	gtk_label_set_xalign(GTK_LABEL(title),0);
	gtk_widget_set_hexpand(title,TRUE);
	gtk_box_append(GTK_BOX(bar),title);
	gtk_box_append(GTK_BOX(bar),icon_button("window-close-symbolic","Close Volume",G_CALLBACK(dismiss),NULL));
	gtk_box_append(GTK_BOX(body),bar);
	GtkWidget *scope=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,10);
	heading=gtk_label_new("");
	gtk_widget_add_css_class(heading,"notification-group");
	gtk_label_set_ellipsize(GTK_LABEL(heading),PANGO_ELLIPSIZE_END);
	gtk_label_set_max_width_chars(GTK_LABEL(heading),24);
	gtk_box_append(GTK_BOX(scope),heading);
	GtkWidget *rule=gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_widget_set_hexpand(rule,TRUE);
	gtk_widget_set_valign(rule,GTK_ALIGN_CENTER);
	gtk_box_append(GTK_BOX(scope),rule);
	all=icon_button("audio-volume-high-symbolic","Mute Node",G_CALLBACK(mute_node),NULL);
	gtk_box_append(GTK_BOX(scope),all);
	gtk_box_append(GTK_BOX(body),scope);
	scroll=gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll),TRUE);
	gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll),240);
	gtk_box_append(GTK_BOX(body),scroll);
	list=gtk_box_new(GTK_ORIENTATION_VERTICAL,4);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),list);
	notice=gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(notice),0);
	gtk_label_set_wrap(GTK_LABEL(notice),TRUE);
	gtk_widget_add_css_class(notice,"notification-meta");
	gtk_box_append(GTK_BOX(body),notice);
	GtkCssProvider *skin=gtk_css_provider_new();
	gtk_css_provider_load_from_string(skin,
		".node-volume-row { padding: 8px 2px; }"
		".node-dock-action.node-volume-trigger, .node-dock-action.node-volume-trigger image { color: #c1e3ef; -gtk-icon-style: symbolic; }"
		".node-volume-app-icon { background: #172b3a; border: 1px solid #365160; border-radius: 3px; }"
		".node-volume-content scale { padding: 2px 0; }"
		".node-volume-content scale trough { background: #364957; min-height: 3px; border: none; box-shadow: none; }"
		".node-volume-content scale highlight { background: #82cedd; border: none; box-shadow: none; }"
		".node-volume-content scale slider { background: #c3dce7; min-width: 10px; min-height: 10px; border: none; box-shadow: none; }");
	gtk_style_context_add_provider_for_display(gdk_display_get_default(),GTK_STYLE_PROVIDER(skin),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(skin);
}

GtkWidget *node_volume_widget(void) {return pane;}
gboolean node_volume_close(void) {
	if(!showing)return FALSE;
	showing=FALSE;
	send(HIDE,0,0);
	gtk_widget_set_visible(pane,FALSE);
	on_region();
	return TRUE;
}
static void clear_rows(void) {
	GtkWidget *row;
	while((row=gtk_widget_get_first_child(list)))gtk_box_remove(GTK_BOX(list),row);
	g_clear_pointer(&last_rows,g_free);
}
void node_volume_open(const char *node,const char *label) {
	if(!pane || !node || !*node)return;
	g_free(current);current=g_strdup(node);
	clear_rows();
	gtk_label_set_text(GTK_LABEL(heading),label);
	gtk_label_set_text(GTK_LABEL(notice),"Loading audio…");
	gtk_widget_set_visible(notice,TRUE);
	gtk_widget_set_sensitive(all,FALSE);
	showing=TRUE;
	gtk_widget_set_visible(pane,TRUE);
	send(WATCH,0,0);
	on_region();
}
void node_volume_context(const char *node) {
	if(showing && g_strcmp0(node,current)!=0)node_volume_close();
}
void node_volume_layout(int w,int h,int anchor,int bottom) {
	if(!pane || !showing)return;
	int width=MIN(406,MAX(1,w-24));
	gtk_widget_set_size_request(pane,width,-1);
	int left=CLAMP(anchor-width/2,12,MAX(12,w-width-12));
	gtk_widget_set_margin_start(pane,left);
	int next=CLAMP(anchor-left,18,MAX(18,width-18));
	if(next!=tail_x){tail_x=next;gtk_widget_queue_draw(tail);}
	gtk_widget_set_margin_bottom(pane,bottom);
	gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll),CLAMP(h-bottom-122,48,240));
}

struct update {char *node,*rows,*message;gboolean muted;};
static gboolean update_rows(gpointer ptr) {
	struct update *u=ptr;
	if(showing && g_strcmp0(current,u->node)==0 && active_sliders==0) {
		gboolean changed=master!=u->muted || g_strcmp0(last_rows,u->rows)!=0;
		master=u->muted;
		if(changed) {
			analog_button_icon(all,master?"audio-volume-muted-symbolic":"audio-volume-high-symbolic");
			gtk_accessible_update_property(GTK_ACCESSIBLE(all),GTK_ACCESSIBLE_PROPERTY_LABEL,master?"Unmute Node":"Mute Node",-1);
			analog_button_on(all,master);
			gtk_widget_set_sensitive(all,master || *u->rows);
		}
		const char *status=*u->message?u->message:*u->rows?"":"No audio apps in this Node.";
		gboolean status_changed=g_strcmp0(status,gtk_label_get_text(GTK_LABEL(notice)))!=0;
		if(status_changed)gtk_label_set_text(GTK_LABEL(notice),status);
		gtk_widget_set_visible(notice,*status!=0);
		if(changed) {
			clear_rows();
			last_rows=g_strdup(u->rows);
			g_auto(GStrv) lines=g_strsplit(u->rows,"\n",-1);
			for(int i=0;lines[i] && *lines[i];i++) {
				g_auto(GStrv) fields=g_strsplit(lines[i],"\t",6);
				if(g_strv_length(fields)==6)
					add_row(atoi(fields[0]),atoi(fields[1]),atoi(fields[2])!=0,atoi(fields[3])!=0,fields[4],fields[5]);
			}
		}
		if(changed || status_changed)on_region();
	}
	g_free(u->node);g_free(u->rows);g_free(u->message);g_free(u);
	return G_SOURCE_REMOVE;
}
void node_volume_post(const char *node,const char *rows,int mute,const char *message) {
	struct update *u=g_new(struct update,1);
	*u=(struct update){g_strdup(node),g_strdup(rows),g_strdup(message),mute!=0};
	g_idle_add(update_rows,u);
}
