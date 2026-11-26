//go:build linux && cgo && panel

#include "map-profile.h"
#include "analog-button.h"
#include "shell-type.h"
#include <gio/gdesktopappinfo.h>
#include <math.h>

static void cutout(cairo_t *cr,struct profile_rect r,double c) {
	double x=r.x,y=r.y,w=r.w,h=r.h;
	cairo_move_to(cr,x+c,y);
	cairo_line_to(cr,x+w-c,y);
	cairo_line_to(cr,x+w,y+c);
	cairo_line_to(cr,x+w,y+h-c);
	cairo_line_to(cr,x+w-c,y+h);
	cairo_line_to(cr,x+c,y+h);
	cairo_line_to(cr,x,y+h-c);
	cairo_line_to(cr,x,y+c);
	cairo_close_path(cr);
}

// The parallel outer polygon comes from a perpendicular offset of the
// inner edges. A 45-degree cut therefore changes by g*(2-sqrt(2)).
static void rim(cairo_t *cr,struct profile_rect inner) {
	const double g=PROFILE_RIM_GAP,c=PROFILE_CORNER+g*(2-G_SQRT2);
	const double x=inner.x-g,y=inner.y-g,w=inner.w+2*g,h=inner.h+2*g;
	const double p[8][2]={{x+c,y},{x+w-c,y},{x+w,y+c},{x+w,y+h-c},
		{x+w-c,y+h},{x+c,y+h},{x,y+h-c},{x,y+c}};
	const double leg=7, dash[]={5,5};
	cairo_set_line_width(cr,1.15);
	cairo_set_source_rgba(cr,.43,.83,.86,.85);
	// Join every diagonal and its straight corner runs as one contour.
	cairo_new_path(cr);
	for(int i=0;i<8;i+=2) {
		const double *a=p[(i+6)%8], *b=p[(i+7)%8],*d=p[i],*e=p[(i+1)%8];
		double n=hypot(a[0]-b[0],a[1]-b[1]),m=hypot(e[0]-d[0],e[1]-d[1]);
		cairo_move_to(cr,b[0]+(a[0]-b[0])*leg/n,b[1]+(a[1]-b[1])*leg/n);
		cairo_line_to(cr,b[0],b[1]);cairo_line_to(cr,d[0],d[1]);
		cairo_line_to(cr,d[0]+(e[0]-d[0])*leg/m,d[1]+(e[1]-d[1])*leg/m);
	}
	cairo_stroke(cr);
	cairo_set_source_rgba(cr,.43,.83,.86,.56);
	cairo_set_dash(cr,dash,2,0);
	for(int i=0;i<8;i+=2) {
		const double *a=p[i],*b=p[i+1];
		double n=hypot(b[0]-a[0],b[1]-a[1]);
		if(n<2*(leg+4)) continue;
		double dx=(b[0]-a[0])*(leg+4)/n,dy=(b[1]-a[1])*(leg+4)/n;
		cairo_move_to(cr,a[0]+dx,a[1]+dy);cairo_line_to(cr,b[0]-dx,b[1]-dy);
	}
	cairo_stroke(cr);
	cairo_set_dash(cr,NULL,0,0);
}

static void label(cairo_t *cr,const char *value,double x,double y,double w,double size,gboolean strong) {
	PangoLayout *layout=pango_cairo_create_layout(cr);
	PangoFontDescription *font=shell_font_description(strong?SHELL_DISPLAY_FALLBACK:NULL,size,
		strong?PANGO_WEIGHT_NORMAL:PANGO_WEIGHT_MEDIUM);
	pango_layout_set_font_description(layout,font);
	pango_layout_set_text(layout,value,-1);
	pango_layout_set_single_paragraph_mode(layout,TRUE);
	pango_layout_set_width(layout,w*PANGO_SCALE);
	pango_layout_set_ellipsize(layout,PANGO_ELLIPSIZE_END);
	cairo_move_to(cr,x,y);
	cairo_set_source_rgb(cr,strong?.91:.61,strong?.96:.71,strong?.99:.79);
	pango_cairo_show_layout(cr,layout);
	pango_font_description_free(font);
	g_object_unref(layout);
}

void profile_data_free(gpointer p) {
	struct profile_data *d=p;
	g_free(d->description);
	g_strfreev(d->app_ids);
	g_clear_pointer(&d->icons,g_ptr_array_unref);
	g_clear_pointer(&d->paint,gsk_render_node_unref);
	g_free(d);
}

GHashTable *profile_parse(const char *rows) {
	GHashTable *out=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,profile_data_free);
	g_auto(GStrv) lines=g_strsplit(rows,"\n",-1);
	for(char **line=lines; *line; ++line) {
		g_auto(GStrv) fields=g_strsplit(*line,"\t",7);
		int count=g_strv_length(fields);
		if(count!=6 && count!=7) continue;
		struct profile_data *d=g_new0(struct profile_data,1);
		d->created_at=g_ascii_strtoll(fields[1],NULL,10);
		d->last_active=g_ascii_strtoll(fields[2],NULL,10);
		d->empty=g_str_equal(fields[3],"true");
		d->description=g_strdup(fields[4]);
		d->app_ids=g_strsplit(fields[5],",",-1);
		d->shortcut=count==7?CLAMP((int)g_ascii_strtoll(fields[6],NULL,10),0,9):0;
		g_hash_table_replace(out,g_strdup(fields[0]),d);
	}
	return out;
}

double profile_height(const struct profile_data *d) {
	return 108+(d && *d->description?24:0)+(d && (d->created_at>0 || d->last_active>0)?22:0)+(d && (d->empty || d->app_ids[0])?27:0);
}

static char *elapsed(gint64 from,gint64 now) {
	gint64 min=MAX((gint64)0,now-from)/60;
	if(min==0) return g_strdup("<1m");
	if(min<60) return g_strdup_printf("%" G_GINT64_FORMAT "m",min);
	if(min<1440) return g_strdup_printf("%" G_GINT64_FORMAT "h %" G_GINT64_FORMAT "m",min/60,min%60);
	return g_strdup_printf("%" G_GINT64_FORMAT "d %" G_GINT64_FORMAT "h",min/1440,(min%1440)/60);
}

static PangoLayout *stat_layout(cairo_t *cr,const char *caption,const char *value) {
	PangoLayout *layout=pango_cairo_create_layout(cr);
	PangoFontDescription *font=shell_font_description(NULL,12,PANGO_WEIGHT_MEDIUM);
	pango_layout_set_font_description(layout,font);
	pango_font_description_free(font);
	pango_layout_set_single_paragraph_mode(layout,TRUE);
	g_autofree char *line=g_markup_printf_escaped(
		"<span foreground=\"#9eb6c9\">%s</span>  <span foreground=\"#e8f2fa\" weight=\"semibold\">%s</span>",
		caption,value);
	pango_layout_set_markup(layout,line,-1);
	return layout;
}

static void stat_icon(cairo_t *cr,double x,double y,gboolean activity) {
	cairo_new_path(cr);
	cairo_set_source_rgb(cr,.56,.72,.81);
	cairo_set_line_width(cr,1);
	cairo_set_line_join(cr,CAIRO_LINE_JOIN_ROUND);
	cairo_set_line_cap(cr,CAIRO_LINE_CAP_ROUND);
	if(!activity) {
		cairo_arc(cr,x+5,y,4.5,0,2*G_PI);
		cairo_move_to(cr,x+5,y-2.5);
		cairo_line_to(cr,x+5,y);
		cairo_line_to(cr,x+7,y+1.5);
	} else {
		cairo_move_to(cr,x,y);cairo_line_to(cr,x+3,y);
		cairo_line_to(cr,x+4.6,y-3.2);cairo_line_to(cr,x+6.1,y+3.1);
		cairo_line_to(cr,x+7.8,y-1.0);cairo_line_to(cr,x+9,y);
		cairo_line_to(cr,x+11,y);
	}
	cairo_stroke(cr);
}

static void profile_stats(cairo_t *cr,const struct profile_data *d,double x,double y,double w,gint64 now) {
	g_autofree char *age=d->created_at>0?elapsed(d->created_at,now):NULL;
	g_autofree char *idle=d->last_active>0?elapsed(d->last_active,now):NULL;
	g_autofree char *active=idle?g_strdup_printf("%s ago",idle):NULL;
	const char *values[]={age,active};
	const char *captions[]={"Running time","Last active"};
	PangoLayout *layouts[2]={NULL,NULL};
	int natural[2]={0,0};
	const double icon_gap=16,seam_gap=19;
	gboolean paired=age && active;
	double available=MAX(0,w-icon_gap*(paired?2:1)-(paired?seam_gap:0));
	for(int n=0;n<2;n++) if(values[n]) {
		layouts[n]=stat_layout(cr,captions[n],values[n]);
		pango_layout_get_pixel_size(layouts[n],&natural[n],NULL);
	}
	if(paired && natural[0]+natural[1]>available) {
		// At narrow windows keep both groups on the same row and prefer
		// both real values to squeezing each of the long captions.
		const char *compact[]={"Run","Active"};
		for(int n=0;n<2;n++) {
			g_object_unref(layouts[n]);
			layouts[n]=stat_layout(cr,compact[n],values[n]);
			pango_layout_get_pixel_size(layouts[n],&natural[n],NULL);
		}
	}
	double widths[2]={MIN(natural[0],available),MIN(natural[1],available)};
	if(paired && natural[0]+natural[1]>available) {
		widths[0]=MIN(natural[0],available-MIN(natural[1],available*.5));
		widths[1]=available-widths[0];
	}
	for(int n=0;n<2;n++) if(layouts[n]) {
		int h;pango_layout_get_pixel_size(layouts[n],NULL,&h);
		stat_icon(cr,x,y+h*.5,n==1);
		pango_layout_set_width(layouts[n],MAX(1,widths[n])*PANGO_SCALE);
		pango_layout_set_ellipsize(layouts[n],PANGO_ELLIPSIZE_END);
		cairo_move_to(cr,x+icon_gap,y);
		pango_cairo_show_layout(cr,layouts[n]);
		x+=icon_gap+widths[n];
		if(n==0 && paired) {
			cairo_set_source_rgba(cr,.56,.72,.81,.42);
			cairo_set_line_width(cr,1);
			cairo_move_to(cr,x+9.5,y+3);cairo_line_to(cr,x+9.5,y+h-3);
			cairo_stroke(cr);
			x+=seam_gap;
		}
		g_object_unref(layouts[n]);
	}
}

static void icons_resolve(GtkWidget *widget,struct profile_data *d) {
	if(!d || d->icons) return;
	d->icons=g_ptr_array_new_with_free_func(g_object_unref);
	GtkIconTheme *theme=gtk_icon_theme_get_for_display(gtk_widget_get_display(widget));
	for(char **id=d->app_ids; *id; ++id) {
		g_autofree char *entry=g_strconcat(*id,".desktop",NULL);
		GDesktopAppInfo *info=g_desktop_app_info_new(entry);
		GIcon *icon=info?g_app_info_get_icon(G_APP_INFO(info)):NULL;
		GtkIconPaintable *paint=NULL;
		if(icon && gtk_icon_theme_has_gicon(theme,icon)) paint=gtk_icon_theme_lookup_by_gicon(theme,icon,20,gtk_widget_get_scale_factor(widget),GTK_TEXT_DIR_NONE,0);
		if(!paint) {
			g_clear_object(&paint);
			const char *fallback[]={"application-x-executable",NULL};
			paint=gtk_icon_theme_lookup_icon(theme,*id,fallback,20,gtk_widget_get_scale_factor(widget),GTK_TEXT_DIR_NONE,0);
		}
		if(paint) g_ptr_array_add(d->icons,paint);
		g_clear_object(&info);
	}
}

void profile_draw_chrome(cairo_t *cr,struct profile_rect r,gboolean flat) {
	cutout(cr,r,PROFILE_CORNER);
	if(flat) {
		cairo_set_source_rgb(cr,.055,.105,.15);
		cairo_fill_preserve(cr);
	} else {
		cairo_pattern_t *p=cairo_pattern_create_linear(r.x,r.y,r.x,r.y+r.h);
		cairo_pattern_add_color_stop_rgb(p,0,.085,.16,.21);
		cairo_pattern_add_color_stop_rgb(p,1,.055,.103,.151);
		cairo_set_source(cr,p);cairo_fill_preserve(cr);cairo_pattern_destroy(p);
	}
	cairo_set_source_rgb(cr,.50,.64,.71);cairo_set_line_width(cr,1.3);cairo_stroke(cr);
	rim(cr,r);
}

static void draw_profile(GtkSnapshot *snap,GtkWidget *widget,struct profile_layout layout,const char *name,struct profile_data *d,GtkStateFlags button_state) {
	struct profile_rect r=layout.card,b=layout.fly;
	double extent=PROFILE_RIM_GAP+2;
	graphene_rect_t bounds=GRAPHENE_RECT_INIT(r.x-extent,r.y-extent,r.w+2*extent,r.h+2*extent);
	cairo_t *cr=gtk_snapshot_append_cairo(snap,&bounds);
	profile_draw_chrome(cr,r,FALSE);
	double x=r.x+PROFILE_PADDING,y=r.y+PROFILE_PADDING,w=r.w-2*PROFILE_PADDING;
	label(cr,name,x,y,w,26,TRUE);y+=36;
	if(d && *d->description) { label(cr,d->description,x,y,w,15,FALSE);y+=24; }
	gint64 now=g_get_real_time()/G_USEC_PER_SEC;
	if(d && (d->created_at>0 || d->last_active>0)) profile_stats(cr,d,x,y,w,now);
	double row=b.y+(b.h-29)*.5;
	double available=b.x-x-12;
	if(d && d->empty) label(cr,"No windows open",x,row+6,MAX(1,available),12,FALSE);
	int shown=0;
	if(widget) icons_resolve(widget,d);
	int count=d && d->icons ? (int)d->icons->len : 0;
	int fits=MAX(0,(int)(available/33));
	shown=MIN(count,fits);
	if(count>shown && shown>0) {
		shown--;
		g_autofree char *more=g_strdup_printf("+%d",count-shown);
		label(cr,more,x+shown*33,row+7,32,12,FALSE);
	}
	if(d && (d->empty || count)) {
		cairo_set_source_rgba(cr,.32,.48,.56,.75);cairo_set_line_width(cr,1);
		cairo_move_to(cr,x,b.y-8);cairo_line_to(cr,r.x+r.w-PROFILE_PADDING,b.y-8);cairo_stroke(cr);
	}
	for(int i=0;i<shown;i++) {
		cutout(cr,(struct profile_rect){x+i*33,row,28,29},4);
		cairo_set_source_rgb(cr,.085,.163,.217);cairo_fill_preserve(cr);
		cairo_set_source_rgb(cr,.25,.40,.49);cairo_stroke(cr);
	}
	analog_button_paint(cr,b.x,b.y,b.w,b.h,button_state);
	cairo_save(cr);cairo_translate(cr,b.x+b.w*.5,b.y+b.h*.5);cairo_rotate(cr,.60);
	cairo_set_source_rgb(cr,.69,.90,.94);
	cairo_move_to(cr,0,-13);cairo_curve_to(cr,7,-8,6,-1,4,4);
	cairo_line_to(cr,7,8);cairo_line_to(cr,7,12);cairo_line_to(cr,2,10);
	cairo_line_to(cr,-2,10);cairo_line_to(cr,-7,12);cairo_line_to(cr,-7,8);cairo_line_to(cr,-4,4);
	cairo_curve_to(cr,-6,-1,-7,-8,0,-13);cairo_close_path(cr);cairo_fill(cr);
	cairo_arc(cr,0,-4,2.3,0,2*G_PI);cairo_set_source_rgb(cr,.09,.21,.28);cairo_fill(cr);
	cairo_restore(cr);
	cairo_destroy(cr);
	for(int i=0;i<shown;i++) {
		graphene_point_t pos=GRAPHENE_POINT_INIT(x+i*33+4,row+4.5);
		gtk_snapshot_save(snap);gtk_snapshot_translate(snap,&pos);
		gdk_paintable_snapshot(GDK_PAINTABLE(g_ptr_array_index(d->icons,i)),snap,20,20);
		gtk_snapshot_restore(snap);
	}
}


void profile_snapshot(GtkSnapshot *snap,GtkWidget *widget,struct profile_layout layout,const char *name,struct profile_data *d,GtkStateFlags button_state) {
	if(!d) { draw_profile(snap,widget,layout,name,NULL,button_state); return; }
	gint64 now=g_get_real_time()/G_USEC_PER_SEC;
	gint64 age=d->created_at>0?MAX((gint64)0,now-d->created_at)/60:-1;
	gint64 active=d->last_active>0?MAX((gint64)0,now-d->last_active)/60:-1;
	int scale=widget?gtk_widget_get_scale_factor(widget):1;
	if(d->paint_scale!=scale) g_clear_pointer(&d->icons,g_ptr_array_unref);
	struct profile_rect r=layout.card;
	if(!d->paint || d->paint_width!=r.w || d->paint_height!=r.h ||
			d->paint_scale!=scale || d->button_state!=button_state || d->age_min!=age || d->activity_min!=active) {
		g_clear_pointer(&d->paint,gsk_render_node_unref);
		layout.card.x=layout.card.y=0;
		layout.fly.x-=r.x;layout.fly.y-=r.y;
		GtkSnapshot *cache=gtk_snapshot_new();
		draw_profile(cache,widget,layout,name,d,button_state);
		d->button_state=button_state;
		d->paint=gtk_snapshot_free_to_node(cache);
		d->paint_width=r.w;d->paint_height=r.h;d->paint_scale=scale;
		d->age_min=age;d->activity_min=active;
	}
	if(d->paint) {
		graphene_point_t at=GRAPHENE_POINT_INIT(r.x,r.y);
		gtk_snapshot_save(snap);gtk_snapshot_translate(snap,&at);
		gtk_snapshot_append_node(snap,d->paint);gtk_snapshot_restore(snap);
	}
}
