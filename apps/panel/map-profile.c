//go:build linux && cgo && panel

#include "map-profile.h"
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
	PangoFontDescription *font=pango_font_description_new();
	pango_font_description_set_family(font,"Sans");
	pango_font_description_set_absolute_size(font,size*PANGO_SCALE);
	pango_font_description_set_weight(font,strong?PANGO_WEIGHT_SEMIBOLD:PANGO_WEIGHT_NORMAL);
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
		g_auto(GStrv) fields=g_strsplit(*line,"\t",6);
		if(g_strv_length(fields)!=6) continue;
		struct profile_data *d=g_new0(struct profile_data,1);
		d->created_at=g_ascii_strtoll(fields[1],NULL,10);
		d->last_active=g_ascii_strtoll(fields[2],NULL,10);
		d->empty=g_str_equal(fields[3],"true");
		d->description=g_strdup(fields[4]);
		d->app_ids=g_strsplit(fields[5],",",-1);
		g_hash_table_replace(out,g_strdup(fields[0]),d);
	}
	return out;
}

double profile_height(const struct profile_data *d) {
	return 95+(d && *d->description?24:0)+(d && d->created_at>0?22:0)+
		(d && d->last_active>0?22:0)+(d && (d->empty || d->app_ids[0])?27:0);
}

static char *elapsed(gint64 from,gint64 now) {
	gint64 min=MAX((gint64)0,now-from)/60;
	if(min==0) return g_strdup("<1m");
	if(min<60) return g_strdup_printf("%" G_GINT64_FORMAT "m",min);
	if(min<1440) return g_strdup_printf("%" G_GINT64_FORMAT "h %" G_GINT64_FORMAT "m",min/60,min%60);
	return g_strdup_printf("%" G_GINT64_FORMAT "d %" G_GINT64_FORMAT "h",min/1440,(min%1440)/60);
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

static void draw_profile(GtkSnapshot *snap,GtkWidget *widget,struct profile_layout layout,const char *name,struct profile_data *d) {
	struct profile_rect r=layout.card,b=layout.fly;
	double extent=PROFILE_RIM_GAP+2;
	graphene_rect_t bounds=GRAPHENE_RECT_INIT(r.x-extent,r.y-extent,r.w+2*extent,r.h+2*extent);
	cairo_t *cr=gtk_snapshot_append_cairo(snap,&bounds);
	cutout(cr,r,PROFILE_CORNER);
	cairo_pattern_t *p=cairo_pattern_create_linear(r.x,r.y,r.x,r.y+r.h);
	cairo_pattern_add_color_stop_rgb(p,0,.085,.16,.21);
	cairo_pattern_add_color_stop_rgb(p,1,.055,.103,.151);
	cairo_set_source(cr,p);cairo_fill_preserve(cr);cairo_pattern_destroy(p);
	cairo_set_source_rgb(cr,.50,.64,.71);cairo_set_line_width(cr,1.3);cairo_stroke(cr);
	rim(cr,r);
	double x=r.x+15,y=r.y+12,w=r.w-30;
	label(cr,name,x,y,w,26,TRUE);y+=36;
	if(d && *d->description) { label(cr,d->description,x,y,w,15,FALSE);y+=24; }
	gint64 now=g_get_real_time()/G_USEC_PER_SEC;
	if(d && d->created_at>0) {
		g_autofree char *t=elapsed(d->created_at,now);
		g_autofree char *s=g_strdup_printf("Running time   %s",t);
		label(cr,s,x,y,MAX(1,w-44),12,FALSE);y+=22;
	}
	if(d && d->last_active>0) {
		g_autofree char *t=elapsed(d->last_active,now);
		g_autofree char *s=g_strdup_printf("Last active   %s ago",t);
		label(cr,s,x,y,MAX(1,w-44),12,FALSE);y+=22;
	}
	double row=r.y+r.h-46;
	if(d && d->empty) label(cr,"No windows open",x,row+8,MAX(1,w-53),12,FALSE);
	double available=b.x-x-8;
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
		cairo_move_to(cr,x,row-8);cairo_line_to(cr,r.x+r.w-15,row-8);cairo_stroke(cr);
	}
	for(int i=0;i<shown;i++) {
		cutout(cr,(struct profile_rect){x+i*33,row,28,29},4);
		cairo_set_source_rgb(cr,.085,.163,.217);cairo_fill_preserve(cr);
		cairo_set_source_rgb(cr,.25,.40,.49);cairo_stroke(cr);
	}
	cutout(cr,b,PROFILE_FLY_CORNER);
	cairo_set_source_rgb(cr,.31,.81,.81);cairo_fill_preserve(cr);
	cairo_set_source_rgb(cr,.61,.97,.95);cairo_set_line_width(cr,1.3);cairo_stroke(cr);
	cairo_save(cr);cairo_translate(cr,b.x+b.w*.5,b.y+b.h*.5);cairo_rotate(cr,.60);
	cairo_set_source_rgb(cr,.05,.15,.20);
	cairo_move_to(cr,0,-13);cairo_curve_to(cr,7,-8,6,-1,4,4);
	cairo_line_to(cr,7,8);cairo_line_to(cr,7,12);cairo_line_to(cr,2,10);
	cairo_line_to(cr,-2,10);cairo_line_to(cr,-7,12);cairo_line_to(cr,-7,8);cairo_line_to(cr,-4,4);
	cairo_curve_to(cr,-6,-1,-7,-8,0,-13);cairo_close_path(cr);cairo_fill(cr);
	cairo_arc(cr,0,-4,2.3,0,2*G_PI);cairo_set_source_rgb(cr,.68,.98,.97);cairo_fill(cr);
	cairo_restore(cr);
	cairo_destroy(cr);
	for(int i=0;i<shown;i++) {
		graphene_point_t pos=GRAPHENE_POINT_INIT(x+i*33+4,row+4.5);
		gtk_snapshot_save(snap);gtk_snapshot_translate(snap,&pos);
		gdk_paintable_snapshot(GDK_PAINTABLE(g_ptr_array_index(d->icons,i)),snap,20,20);
		gtk_snapshot_restore(snap);
	}
}


void profile_snapshot(GtkSnapshot *snap,GtkWidget *widget,struct profile_layout layout,const char *name,struct profile_data *d) {
	if(!d) { draw_profile(snap,widget,layout,name,NULL); return; }
	gint64 now=g_get_real_time()/G_USEC_PER_SEC;
	gint64 age=d->created_at>0?MAX((gint64)0,now-d->created_at)/60:-1;
	gint64 active=d->last_active>0?MAX((gint64)0,now-d->last_active)/60:-1;
	int scale=widget?gtk_widget_get_scale_factor(widget):1;
	if(d->paint_scale!=scale) g_clear_pointer(&d->icons,g_ptr_array_unref);
	struct profile_rect r=layout.card;
	if(!d->paint || d->paint_width!=r.w || d->paint_height!=r.h ||
			d->paint_scale!=scale || d->age_min!=age || d->activity_min!=active) {
		g_clear_pointer(&d->paint,gsk_render_node_unref);
		layout.card.x=layout.card.y=0;
		layout.fly.x-=r.x;layout.fly.y-=r.y;
		GtkSnapshot *cache=gtk_snapshot_new();
		draw_profile(cache,widget,layout,name,d);
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
