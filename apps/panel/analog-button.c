//go:build linux && cgo && panel

#include "analog-button.h"

struct tone { unsigned fill,edge; };
static void source(cairo_t *cr,unsigned rgb,double alpha) {
	cairo_set_source_rgba(cr,(rgb>>16)/255.,((rgb>>8)&255)/255.,(rgb&255)/255.,alpha);
}

static void contour(cairo_t *cr,double x,double y,double w,double h,double cut) {
	cairo_move_to(cr,x+cut,y);
	cairo_line_to(cr,x+w-cut,y);cairo_line_to(cr,x+w,y+cut);
	cairo_line_to(cr,x+w,y+h-cut);cairo_line_to(cr,x+w-cut,y+h);
	cairo_line_to(cr,x+cut,y+h);cairo_line_to(cr,x,y+h-cut);
	cairo_line_to(cr,x,y+cut);cairo_close_path(cr);
}

static void edge_corner(cairo_t *cr,double x,double y,int sx,int sy,double cut,double arm) {
	cairo_move_to(cr,x+sx*arm,y);cairo_line_to(cr,x+sx*cut,y);
	cairo_line_to(cr,x,y+sy*cut);cairo_line_to(cr,x,y+sy*arm);
}

void analog_button_paint(cairo_t *cr,double x,double y,double w,double h,GtkStateFlags state) {
	if(w<10 || h<10) return;
	gboolean off=state&GTK_STATE_FLAG_INSENSITIVE;
	gboolean lit=!off && (state&GTK_STATE_FLAG_FOCUS_VISIBLE);
	gboolean hover=!off && (state&GTK_STATE_FLAG_PRELIGHT);
	gboolean on=!off && state&GTK_STATE_FLAG_CHECKED;
	// Selection is persistent; active and hover are transient.
	struct tone palette=off?(struct tone){0x10202b,0x304554}:
		state&GTK_STATE_FLAG_ACTIVE?(struct tone){0x244f62,0xacf0f5}:
		on?(struct tone){0x1c404c,0x8ce7e7}:
		lit?(struct tone){0x1a3646,0x88cbd9}:
		state&GTK_STATE_FLAG_PRELIGHT?(struct tone){0x193746,0x7cc3d2}:
		(struct tone){0x102331,0x527383};
	double cut=CLAMP(MIN(w,h)*.13,2.,5.),inset=2.5;
	double bw=w-2*inset,bh=h-2*inset;
	double arm=MIN(.28*MIN(bw,bh),cut+7);
	if(arm<=cut) return;
	cairo_save(cr);
	x+=inset;y+=inset;
	contour(cr,x,y,bw,bh,cut);
	source(cr,palette.fill,.94);cairo_fill(cr);
	if(hover || lit) {
		edge_corner(cr,x,y,1,1,cut,arm);
		edge_corner(cr,x+bw,y,-1,1,cut,arm);
		edge_corner(cr,x+bw,y+bh,-1,-1,cut,arm);
		edge_corner(cr,x,y+bh,1,-1,cut,arm);
		if(lit) {
			source(cr,0x9cedf5,.12);cairo_set_line_width(cr,4.0);cairo_stroke_preserve(cr);
			source(cr,0x9cedf5,.19);cairo_set_line_width(cr,2.5);cairo_stroke_preserve(cr);
		}
		source(cr,lit?0xb3e8ee:palette.edge,1.);
		cairo_set_line_width(cr,lit?1.4:1.15);cairo_stroke(cr);
	}
	cairo_restore(cr);
}

static void paint(GtkDrawingArea *area,cairo_t *cr,int w,int h,gpointer data) {
	(void)area;
	analog_button_paint(cr,0,0,w,h,gtk_widget_get_state_flags(data));
}
static void paint_changed(GtkWidget *button,GtkStateFlags old,gpointer area) {
	(void)button;(void)old;
	gtk_widget_queue_draw(area);
}

void analog_button_style(GtkWidget *button,int pad_x,int pad_y) {
	static gboolean installed;
	if(!installed) {
		GtkCssProvider *skin=gtk_css_provider_new();
		gtk_css_provider_load_from_string(skin,
			"button.analog-button { background: transparent; background-image: none; "
			"border: 0; border-radius: 0; padding: 0; margin: 0; min-width: 0; min-height: 0; "
			"box-shadow: none; outline: none; text-shadow: none; transform: none; color: #c4dae5; }"
			"button.analog-button:hover, button.analog-button:active, "
			"button.analog-button:focus-visible { color: #effcff; }"
			"button.analog-button:checked { color: #a3f2f1; }"
			"button.analog-button:disabled { color: #728693; }");
		gtk_style_context_add_provider_for_display(gtk_widget_get_display(button),GTK_STYLE_PROVIDER(skin),
			GTK_STYLE_PROVIDER_PRIORITY_APPLICATION+1);
		g_object_unref(skin);
		installed=TRUE;
	}
	GtkWidget *content=gtk_button_get_child(GTK_BUTTON(button));
	g_return_if_fail(content && !gtk_widget_has_css_class(button,"analog-button"));
	g_object_ref(content);
	gtk_button_set_child(GTK_BUTTON(button),NULL);
	gtk_widget_add_css_class(button,"analog-button");
	GtkWidget *overlay=gtk_overlay_new(),*area=gtk_drawing_area_new();
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area),paint,button,NULL);
	gtk_widget_set_can_target(area,FALSE);
	gtk_overlay_set_child(GTK_OVERLAY(overlay),area);
	gtk_widget_set_margin_start(content,pad_x);gtk_widget_set_margin_end(content,pad_x);
	gtk_widget_set_margin_top(content,pad_y);gtk_widget_set_margin_bottom(content,pad_y);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay),content);
	gtk_overlay_set_measure_overlay(GTK_OVERLAY(overlay),content,TRUE);
	g_object_set_data(G_OBJECT(button),"analog-button-content",content);
	g_object_unref(content);
	gtk_button_set_child(GTK_BUTTON(button),overlay);
	g_signal_connect_object(button,"state-flags-changed",G_CALLBACK(paint_changed),area,0);
}

void analog_button_on(GtkWidget *button,gboolean on) {
	if(on) gtk_widget_set_state_flags(button,GTK_STATE_FLAG_CHECKED,FALSE);
	else gtk_widget_unset_state_flags(button,GTK_STATE_FLAG_CHECKED);
	gtk_accessible_update_state(GTK_ACCESSIBLE(button),GTK_ACCESSIBLE_STATE_PRESSED,
		on?GTK_ACCESSIBLE_TRISTATE_TRUE:GTK_ACCESSIBLE_TRISTATE_FALSE,-1);
}
void analog_button_label(GtkWidget *button,const char *label) {
	GtkWidget *content=g_object_get_data(G_OBJECT(button),"analog-button-content");
	if(GTK_IS_LABEL(content)) gtk_label_set_text(GTK_LABEL(content),label);
}
void analog_button_icon(GtkWidget *button,const char *name) {
	GtkWidget *content=g_object_get_data(G_OBJECT(button),"analog-button-content");
	if(GTK_IS_IMAGE(content)) gtk_image_set_from_icon_name(GTK_IMAGE(content),name);
}
