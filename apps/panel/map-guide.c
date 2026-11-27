//go:build linux && cgo && panel

#include "map-guide.h"
#include "shell-type.h"
#include <math.h>

static void ink(cairo_t *cr,unsigned rgb) {
	cairo_set_source_rgb(cr,((rgb>>16)&255)/255.,((rgb>>8)&255)/255.,(rgb&255)/255.);
}
static void face(cairo_t *cr,double w,double h,double cut,double inset) {
	double l=inset,r=w-inset,t=inset,b=h-inset;
	cut-=inset*(2.-G_SQRT2);
	cairo_new_path(cr);
	cairo_move_to(cr,l+cut,t);cairo_line_to(cr,r-cut,t);
	cairo_line_to(cr,r,t+cut);cairo_line_to(cr,r,b-cut);
	cairo_line_to(cr,r-cut,b);cairo_line_to(cr,l+cut,b);
	cairo_line_to(cr,l,b-cut);cairo_line_to(cr,l,t+cut);
	cairo_close_path(cr);
}
static void keycap(cairo_t *cr,double w,double h,double cut,gboolean active) {
	face(cr,w,h,cut,.5);
	ink(cr,active?0x245360:0x162934);
	cairo_fill_preserve(cr);
	ink(cr,active?0x93eaf1:0x425d69);
	cairo_set_line_width(cr,1);
	cairo_stroke(cr);
}
static void key(cairo_t *cr,PangoLayout *layout,int x,int y,int width,const char *text,gboolean active) {
	cairo_save(cr);cairo_translate(cr,x,y);
	keycap(cr,width,17,4,active);
	pango_layout_set_text(layout,text,-1);
	int w,h;pango_layout_get_pixel_size(layout,&w,&h);
	ink(cr,active?0xecffff:0xc0d3de);
	cairo_move_to(cr,(width-w)*.5,(17-h)*.5);
	pango_cairo_show_layout(cr,layout);
	cairo_restore(cr);
}
static void mouse(cairo_t *cr,int x,gboolean scroll,gboolean active) {
	cairo_save(cr);cairo_translate(cr,x,10);
	keycap(cr,26,32,5,active);
	cairo_translate(cr,6,6);
	cairo_move_to(cr,7,0);cairo_curve_to(cr,2,0,0,3,0,7);
	cairo_line_to(cr,0,13);cairo_curve_to(cr,0,17,3,20,7,20);
	cairo_curve_to(cr,11,20,14,17,14,13);
	cairo_line_to(cr,14,7);cairo_curve_to(cr,14,3,12,0,7,0);cairo_close_path(cr);
	cairo_set_line_width(cr,1);
	ink(cr,active?0xecffff:0xa9c6d0);
	cairo_stroke(cr);
	cairo_move_to(cr,.15,8.5);cairo_line_to(cr,13.85,8.5);
	cairo_stroke(cr);
	if(scroll) {
		cairo_set_line_width(cr,2);cairo_set_line_cap(cr,CAIRO_LINE_CAP_ROUND);
		cairo_move_to(cr,7,3);cairo_line_to(cr,7,6);
		cairo_stroke(cr);
	} else {
		// Identify the primary button even when the keycap is idle.
		cairo_move_to(cr,1.5,7);cairo_curve_to(cr,1.6,4,3,2,5.7,1.9);
		cairo_line_to(cr,5.7,7);cairo_close_path(cr);
		ink(cr,active?0xd7ffff:0x7fa9b7);cairo_fill(cr);
		cairo_move_to(cr,7,1);cairo_line_to(cr,7,8.5);
		ink(cr,active?0xecffff:0xa9c6d0);cairo_stroke(cr);
	}
	cairo_restore(cr);
}
void map_guide_draw(cairo_t *cr,int width,unsigned active) {
	face(cr,width,MAP_GUIDE_HEIGHT,9,.5);
	ink(cr,0x0e1c25);cairo_fill_preserve(cr);
	ink(cr,0x637f8c);cairo_set_line_width(cr,1);cairo_stroke(cr);
	face(cr,width,MAP_GUIDE_HEIGHT,9,3.6);
	ink(cr,0x324e5b);cairo_stroke(cr);
	PangoLayout *layout=pango_cairo_create_layout(cr);
	PangoFontDescription *font=shell_font_description(NULL,10,PANGO_WEIGHT_SEMIBOLD);
	pango_layout_set_font_description(layout,font);pango_font_description_free(font);
	key(cr,layout,32,7,20,"W",active&MAP_GUIDE_W);
	key(cr,layout,10,27,20,"A",active&MAP_GUIDE_A);
	key(cr,layout,32,27,20,"S",active&MAP_GUIDE_S);
	key(cr,layout,54,27,20,"D",active&MAP_GUIDE_D);

	cairo_move_to(cr,78.5,12);cairo_line_to(cr,78.5,40);ink(cr,0x324e5b);cairo_stroke(cr);
	mouse(cr,83,FALSE,active&MAP_GUIDE_GRAB);
	mouse(cr,112,TRUE,active&MAP_GUIDE_SCROLL);
	if(width==MAP_GUIDE_WIDTH) {
		cairo_move_to(cr,144.5,12);cairo_line_to(cr,144.5,40);ink(cr,0x324e5b);cairo_stroke(cr);
		key(cr,layout,152,7,20,"Q",FALSE);
		key(cr,layout,176,7,32,"Esc",FALSE);
		key(cr,layout,212,7,45,"Space",active&MAP_GUIDE_SPACE);
		key(cr,layout,164,27,20,"M",FALSE);
		key(cr,layout,188,27,57,"Meta",FALSE);
	}
	g_object_unref(layout);
}

static void legend(cairo_t *cr,PangoLayout *layout,int x,int y,const char *label) {
	ink(cr,0xc0d3de);
	pango_layout_set_text(layout,label,-1);
	cairo_move_to(cr,x,y+2);
	pango_cairo_show_layout(cr,layout);
}

void app_grid_guide_draw(cairo_t *cr,int width,gboolean results_focused) {
	PangoLayout *layout=pango_cairo_create_layout(cr);
	PangoFontDescription *font=shell_font_description(NULL,10,PANGO_WEIGHT_SEMIBOLD);
	pango_layout_set_font_description(layout,font);pango_font_description_free(font);
	const int actions=80;
	const int nav=MAX(0,width-actions-12);
	if(width>=actions) {
		int x=width-actions;
		key(cr,layout,x,1,42,"Enter",FALSE);
		key(cr,layout,x+48,1,32,"Esc",FALSE);
	}
	if (!results_focused && nav>=23) {
		key(cr,layout,0,1,23,"↓",FALSE);
		if(nav>=68) legend(cr,layout,29,1,"Results");
	} else if (results_focused && nav>=148) {
		const char *letters[]={"W","A","S","D"};
		for (int i=0;i<4;i++) key(cr,layout,i*22,1,19,letters[i],FALSE);
		key(cr,layout,91,1,57,"Backspace",FALSE);
	} else if (results_focused && nav>=74) {
		key(cr,layout,0,1,51,"W A S D",FALSE);
		key(cr,layout,55,1,19,"⌫",FALSE);
	}
	g_object_unref(layout);
}

void slash_guide_draw(cairo_t *cr,int width) {
	const int span=165;
	if(width<span) return;
	PangoLayout *layout=pango_cairo_create_layout(cr);
	PangoFontDescription *font=shell_font_description(NULL,10,PANGO_WEIGHT_SEMIBOLD);
	pango_layout_set_font_description(layout,font);pango_font_description_free(font);
	int x=width-span;
	key(cr,layout,x,1,23,"↑",FALSE);
	key(cr,layout,x+27,1,23,"↓",FALSE);
	key(cr,layout,x+54,1,33,"Tab",FALSE);
	key(cr,layout,x+91,1,38,"Enter",FALSE);
	key(cr,layout,x+133,1,32,"Esc",FALSE);
	g_object_unref(layout);
}

// Mission Log: A/D change day; L, Q or Esc close.
void mission_log_guide_draw(cairo_t *cr,int width) {
	if(width<160) return;
	PangoLayout *layout=pango_cairo_create_layout(cr);
	PangoFontDescription *font=shell_font_description(NULL,10,PANGO_WEIGHT_SEMIBOLD);
	pango_layout_set_font_description(layout,font);pango_font_description_free(font);
	key(cr,layout,0,1,19,"A",FALSE);
	key(cr,layout,22,1,19,"D",FALSE);
	key(cr,layout,width-80,1,20,"L",FALSE);
	key(cr,layout,width-56,1,20,"Q",FALSE);
	key(cr,layout,width-32,1,32,"Esc",FALSE);
	g_object_unref(layout);
}

void notification_log_guide_draw(cairo_t *cr,int width) {
	if(width<180) return;
	PangoLayout *layout=pango_cairo_create_layout(cr);
	PangoFontDescription *font=shell_font_description(NULL,10,PANGO_WEIGHT_SEMIBOLD);
	pango_layout_set_font_description(layout,font);pango_font_description_free(font);
	const char *directions[]={"W","A","S","D"};
	for(int i=0;i<4;i++) key(cr,layout,i*22,1,19,directions[i],FALSE);
	int close_y=21;
	if(width>=445) {
		legend(cr,layout,92,1,"Move focus");
		key(cr,layout,158,1,62,"Backspace",FALSE);
		legend(cr,layout,226,1,"Delete");
		key(cr,layout,278,1,104,"Shift + Backspace",FALSE);
		legend(cr,layout,388,1,"Delete all");
		key(cr,layout,0,21,44,"Space",FALSE);
		legend(cr,layout,50,21,"Details");
		key(cr,layout,99,21,43,"Enter",FALSE);
		legend(cr,layout,148,21,"Mark read");
		key(cr,layout,207,21,81,"Shift + Enter",FALSE);
		legend(cr,layout,294,21,"Mark all read");
	} else if(width>=272) {
		key(cr,layout,94,1,62,"Backspace",FALSE);
		key(cr,layout,162,1,104,"Shift + Backspace",FALSE);
		key(cr,layout,0,21,44,"Space",FALSE);
		key(cr,layout,50,21,43,"Enter",FALSE);
		key(cr,layout,99,21,81,"Shift + Enter",FALSE);
	} else {
		key(cr,layout,87,1,44,"Space",FALSE);
		key(cr,layout,137,1,43,"Enter",FALSE);
		key(cr,layout,0,21,62,"Backspace",FALSE);
		key(cr,layout,68,21,104,"Shift + Backspace",FALSE);
		key(cr,layout,0,41,81,"Shift + Enter",FALSE);
		close_y=41;
	}
	key(cr,layout,width-80,close_y,20,"Q",FALSE);
	key(cr,layout,width-56,close_y,20,"N",FALSE);
	key(cr,layout,width-32,close_y,32,"Esc",FALSE);
	g_object_unref(layout);
}

static int home_guide_columns(int width) {
	return MAX(1,(width-2*HOME_GUIDE_INSET)/HOME_GUIDE_CELL_WIDTH);
}

int home_guide_height(int width,gboolean default_node) {
	int cols=home_guide_columns(width),items=home_guide_items(default_node);
	return 2*HOME_GUIDE_INSET+HOME_GUIDE_ROW_HEIGHT*((items+cols-1)/cols);
}

void home_guide_draw(cairo_t *cr,int width,gboolean default_node) {
	int height=home_guide_height(width,default_node);
	face(cr,width,height,8,.5);
	ink(cr,0x0e1c25);cairo_fill_preserve(cr);
	ink(cr,0x637f8c);cairo_set_line_width(cr,1);cairo_stroke(cr);
	face(cr,width,height,8,3.6);
	ink(cr,0x324e5b);cairo_stroke(cr);
	static const char *keys[]={"A","M","W","/",",","R","N","L"};
	PangoLayout *layout=pango_cairo_create_layout(cr);
	PangoFontDescription *font=shell_font_description(NULL,10,PANGO_WEIGHT_SEMIBOLD);
	pango_layout_set_font_description(layout,font);pango_font_description_free(font);
	int cols=home_guide_columns(width),i=0;
	for(int n=0;n<(int)G_N_ELEMENTS(keys);n++) {
		if(n==2 && !default_node) continue;
		int x=HOME_GUIDE_INSET+(i%cols)*HOME_GUIDE_CELL_WIDTH;
		int y=HOME_GUIDE_INSET+(i/cols)*HOME_GUIDE_ROW_HEIGHT+2;
		key(cr,layout,x+1,y,20,keys[n],FALSE);
		i++;
	}
	g_object_unref(layout);
}
