//go:build linux && cgo && panel

#include "map-arcade.h"
#include "shell-type.h"
#include "../../internal/wayland/map-frame.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "map-arcade-play.h"

enum { ARCADE_GRID_PIXEL_LIMIT=16*1024*1024 };
enum sprite { MOTHER, ESCORT, CHICKEN_SUIT, PIG_SUIT, DOG_SUIT, EGG_SPRITE, MUD_SPRITE, BONE_SPRITE, WARNING, RETICLE, SPRITES };
typedef struct {
	GtkWidget parent;
	map_arcade_ship_painter ship;
	struct game g;
	graphene_rect_t frame;
	GskRenderNode *scene,*grid,*sprite[SPRITES],*damaged_ship;
	double grid_density,aim_x,aim_y;
	PangoLayout *wave,*score,*hull;
	guint tick;
	gint64 time;
	gboolean playing,focused;
} MapArcade;
typedef GtkWidgetClass MapArcadeClass;
G_DEFINE_TYPE(MapArcade,map_arcade,GTK_TYPE_WIDGET)

static void color(cairo_t *cr,unsigned rgb) {
	cairo_set_source_rgb(cr,((rgb>>16)&255)/255.,((rgb>>8)&255)/255.,(rgb&255)/255.);
}
static void face(cairo_t *cr,double x,double y,double w,double h,double t) {
	cairo_move_to(cr,x+t,y);cairo_line_to(cr,x+w-t,y);
	cairo_line_to(cr,x+w,y+t);cairo_line_to(cr,x+w,y+h-t);
	cairo_line_to(cr,x+w-t,y+h);cairo_line_to(cr,x+t,y+h);
	cairo_line_to(cr,x,y+h-t);cairo_line_to(cr,x,y+t);cairo_close_path(cr);
}
static void text_layout(PangoLayout *layout,const char *s) {pango_layout_set_text(layout,s,-1);}
static PangoLayout *layout(GtkWidget *w,const char *s,double size,PangoWeight weight) {
	PangoLayout *p=gtk_widget_create_pango_layout(w,s);
	PangoFontDescription *desc=shell_font_description(shell_font_family(),size,weight);
	pango_layout_set_font_description(p,desc);pango_font_description_free(desc);
	return p;
}
static void draw_text(GtkSnapshot *s,PangoLayout *p,int x,int y,unsigned rgb) {
	GdkRGBA ink={((rgb>>16)&255)/255.,((rgb>>8)&255)/255.,(rgb&255)/255.,1};
	graphene_point_t at=GRAPHENE_POINT_INIT(x,y);
	gtk_snapshot_save(s);gtk_snapshot_translate(s,&at);
	gtk_snapshot_append_layout(s,p,&ink);gtk_snapshot_restore(s);
}
static void caption(GtkWidget *w,GtkSnapshot *s,const char *line,int x,int y,int size,unsigned rgb) {
	PangoLayout *p=layout(w,line,size,PANGO_WEIGHT_SEMIBOLD);
	draw_text(s,p,x,y,rgb);g_object_unref(p);
}
static void stats(MapArcade *a) {
	char t[40];
	g_snprintf(t,sizeof(t),"%02u",a->g.wave);text_layout(a->wave,t);
	g_snprintf(t,sizeof(t),"%u",a->g.score);text_layout(a->score,t);
	g_snprintf(t,sizeof(t),"%u",a->g.hull);text_layout(a->hull,t);
}
static void reset(MapArcade *a) {
	round_reset(&a->g,a->g.bounds);stats(a);
}

static gboolean tick(GtkWidget *w,GdkFrameClock *clock,gpointer data) {
	(void)data;
	MapArcade *a=(MapArcade *)w;
	gint64 now=gdk_frame_clock_get_frame_time(clock);
	if(a->time) {
		struct game *g=&a->g;
		unsigned wave=g->wave,score=g->score,hull=g->hull;
		double dt=CLAMP((now-a->time)/1000000.,0,.035);
		g->damage=fmax(0,g->damage-dt);
		if(g->hull) game_step(g,dt);
		else {
			bank_step(g,0,dt);
			g->beam=fmax(0,g->beam-dt);
			for(unsigned i=0;i<ENEMIES;++i)
				g->impacts[i].remaining=fmax(0,g->impacts[i].remaining-dt);
		}
		if(wave!=g->wave || score!=g->score || hull!=g->hull) stats(a);
		gtk_widget_queue_draw(w);
	}
	a->time=now;
	if(a->g.hull || a->g.damage>0) return G_SOURCE_CONTINUE;
	a->tick=0;a->time=0;return G_SOURCE_REMOVE;
}
static void sync_tick(MapArcade *a) {
	if(a->playing && a->focused && (a->g.hull || a->g.damage>0)) {
		if(!a->tick) a->tick=gtk_widget_add_tick_callback(GTK_WIDGET(a),tick,NULL,NULL);
	} else if(a->tick) {
		gtk_widget_remove_tick_callback(GTK_WIDGET(a),a->tick);
		a->tick=0;a->time=0;
	}
}

static void fill(GtkSnapshot *s,double x,double y,double w,double h,unsigned rgb) {
	GdkRGBA c={((rgb>>16)&255)/255.,((rgb>>8)&255)/255.,(rgb&255)/255.,1};
	graphene_rect_t r=GRAPHENE_RECT_INIT(x,y,w,h);gtk_snapshot_append_color(s,&c,&r);
}
static void draw_game_grid(cairo_t *cr,graphene_rect_t box) {
	double x=box.origin.x,y=box.origin.y,width=box.size.width,height=box.size.height;
	double fit=MIN((width-16)/FIELD_W,(height-12)/FIELD_H);
	double step=MAX(16,36*fit);
	face(cr,x,y,width,height,MAP_FRAME_CUT-MAP_ARCADE_FRAME_INSET*(2-G_SQRT2));
	cairo_clip(cr);
	color(cr,0x0d1b28);cairo_paint(cr);
	color(cr,0x213a4a);cairo_set_line_width(cr,.7);
	for(double px=x+12;px<x+width;px+=step) {
		cairo_move_to(cr,px+.5,y);cairo_line_to(cr,px+.5,y+height);
	}
	for(double py=y+12;py<y+height;py+=step) {
		cairo_move_to(cr,x,py+.5);cairo_line_to(cr,x+width,py+.5);
	}
	cairo_stroke(cr);color(cr,0x37586a);
	for(double px=x+12;px<x+width;px+=step)
		for(double py=y+12;py<y+height;py+=step)
			cairo_rectangle(cr,px,py,1.3,1.3);
	cairo_fill(cr);
	for(unsigned i=1;i<=65;++i)
		cairo_rectangle(cr,x+fmod(i*1913u+77,width),y+fmod(i*2149u+19,height),1,1);
	cairo_fill(cr);
}
static void release_grid_image(gpointer data) {cairo_surface_destroy(data);}

static GskRenderNode *game_grid(graphene_rect_t box,double density) {
	// Rasterize the unchanging field at the actual output density. Keeping
	// the chamfer in this texture avoids re-compositing a full-window path
	// mask (and hence the entire static layer) on each action frame. The
	// immutable GBytes owns the Cairo buffer until GTK releases the texture.
	double physical_w=ceil(box.size.width*density),physical_h=ceil(box.size.height*density);
	if(physical_w*physical_h<=ARCADE_GRID_PIXEL_LIMIT) {
		int w=physical_w,h=physical_h;
		cairo_surface_t *image=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,w,h);
		if(cairo_surface_status(image)==CAIRO_STATUS_SUCCESS) {
			cairo_surface_set_device_scale(image,density,density);
			cairo_t *cr=cairo_create(image);
			cairo_translate(cr,-box.origin.x,-box.origin.y);
			draw_game_grid(cr,box);
			cairo_status_t status=cairo_status(cr);
			cairo_destroy(cr);
			if(status==CAIRO_STATUS_SUCCESS) {
				cairo_surface_flush(image);
				int stride=cairo_image_surface_get_stride(image);
				GBytes *bytes=g_bytes_new_with_free_func(cairo_image_surface_get_data(image),
					(gsize)stride*h,release_grid_image,image);
				GdkTexture *texture=gdk_memory_texture_new(w,h,GDK_MEMORY_DEFAULT,bytes,stride);
				graphene_rect_t bounds=GRAPHENE_RECT_INIT(box.origin.x,box.origin.y,w/density,h/density);
				GskRenderNode *node=gsk_texture_node_new(texture,&bounds);
				g_object_unref(texture);g_bytes_unref(bytes);
				return node;
			}
		}
		cairo_surface_destroy(image);
	}
	// Oversize or allocation pressure: same artwork using the normal
	// retained Cairo node. The moving layer still gets a cheap rect clip.
	GtkSnapshot *snap=gtk_snapshot_new();
	cairo_t *cr=gtk_snapshot_append_cairo(snap,&box);
	draw_game_grid(cr,box);
	cairo_destroy(cr);
	return gtk_snapshot_free_to_node(snap);
}

static GskRenderNode *background(MapArcade *a) {
	GtkSnapshot *s=gtk_snapshot_new();
	graphene_rect_t r=GRAPHENE_RECT_INIT(0,0,FIELD_W,FIELD_H);
	cairo_t *cr=gtk_snapshot_append_cairo(s,&r);
	face(cr,26,FIELD_H-55,555,38,7);color(cr,0x142835);cairo_fill_preserve(cr);
	color(cr,0x416171);cairo_set_line_width(cr,1);cairo_stroke(cr);
	face(cr,FIELD_W-236,FIELD_H-55,210,38,7);color(cr,0x191e36);cairo_fill_preserve(cr);
	color(cr,0x615480);cairo_stroke(cr);cairo_destroy(cr);
	GtkWidget *w=GTK_WIDGET(a);
	caption(w,s,"OPS  //  CẢNH BÁO BỐN HƯỚNG",26,18,14,0x9dbecf);
	caption(w,s,"ĐỢT",735,17,13,0x9dbecf);
	caption(w,s,"ĐIỂM",813,17,13,0x9dbecf);
	caption(w,s,"THÂN TÀU",897,17,13,0x9dbecf);
	caption(w,s,"WASD · Lướt    Chuột · Ngắm    Trái / Q · Bắn    Phải / E · Plasma    Esc · Thoát",39,FIELD_H-45,13,0xbae0e9);
	caption(w,s,"E  ·  PLASMA",FIELD_W-221,FIELD_H-53,12,0xcfb7ff);
	return gtk_snapshot_free_to_node(s);
}
static void disc(cairo_t *cr,double x,double y,double r,unsigned rgb) {
	cairo_new_sub_path(cr);cairo_arc(cr,x,y,r,0,2*G_PI);color(cr,rgb);cairo_fill(cr);
}
static void oval(cairo_t *cr,double x,double y,double rx,double ry,unsigned rgb) {
	cairo_save(cr);cairo_translate(cr,x,y);cairo_scale(cr,rx,ry);
	disc(cr,0,0,1,rgb);cairo_restore(cr);
}
static void draw_suit(cairo_t *cr,enum sprite k) {
	unsigned trim=k==CHICKEN_SUIT?0xe7a26c:k==PIG_SUIT?0xcb92ad:0xc4a27c;
	unsigned fur=k==CHICKEN_SUIT?0xffca86:k==PIG_SUIT?0xe49ca7:0xeac49b;
	for(int sign=-1;sign<=1;sign+=2) {
		face(cr,sign*17-3,0,6,12,2);color(cr,0x383238);cairo_fill_preserve(cr);
		color(cr,trim);cairo_set_line_width(cr,1);cairo_stroke(cr);
		face(cr,sign*6-3,14,6,6,1);color(cr,trim);cairo_fill(cr);
	}
	cairo_arc(cr,0,0,17,0,2*G_PI);color(cr,0x173645);cairo_fill_preserve(cr);
	color(cr,0x8eaeb9);cairo_set_line_width(cr,1.05);cairo_stroke(cr);
	disc(cr,0,0,12,fur);
	if(k==CHICKEN_SUIT) {
		cairo_move_to(cr,-8,-10);cairo_line_to(cr,-8,-15);
		cairo_line_to(cr,-4,-13);cairo_line_to(cr,-1,-19);cairo_line_to(cr,3,-13);
		cairo_line_to(cr,7,-15);cairo_line_to(cr,7,-10);cairo_close_path(cr);
		color(cr,0xf18353);cairo_fill_preserve(cr);color(cr,trim);cairo_stroke(cr);
		disc(cr,-8,4,2,0xd48d70);disc(cr,8,4,2,0xd48d70);
		cairo_move_to(cr,-3,3);cairo_line_to(cr,3,3);cairo_line_to(cr,0,7);cairo_close_path(cr);
		color(cr,0xf39743);cairo_fill(cr);
	} else if(k==PIG_SUIT) {
		for(int sign=-1;sign<=1;sign+=2) {
			cairo_move_to(cr,sign*5,-8);cairo_line_to(cr,sign*10,-15);
			cairo_line_to(cr,sign*12,-7);cairo_close_path(cr);
			color(cr,0xd7899b);cairo_fill(cr);
		}
		oval(cr,0,5,6,4,0xf3b2c0);
		disc(cr,-2,5,1.05,0x8b5064);disc(cr,2,5,1.05,0x8b5064);
	} else {
		oval(cr,-10,-2,3.3,8,0x7d533a);oval(cr,10,-2,3.3,8,0x7d533a);
		oval(cr,-4,-2,3.2,4.2,0xb17d54);
		oval(cr,0,5,5.8,3.9,0xffe0ae);oval(cr,0,3.5,2.8,1.8,0x292e31);
		cairo_move_to(cr,-2,7);cairo_curve_to(cr,-1,8,1,8,2,7);
		color(cr,0x665148);cairo_set_line_width(cr,.85);cairo_stroke(cr);
	}
	oval(cr,-4,-3,1.35,1.8,0x17242a);oval(cr,4,-3,1.35,1.8,0x17242a);
	cairo_move_to(cr,-10,9);cairo_curve_to(cr,-8,13,-4,14,0,14);
	color(cr,0x5d8795);cairo_set_line_width(cr,1);cairo_stroke(cr);
}

static void draw_small_asset(cairo_t *cr,enum sprite k) {
	if(k==EGG_SPRITE) {
		oval(cr,0,0,6,8,0xffedbe);oval(cr,-1,-2,2,3,0xfffae2);
	} else if(k==MUD_SPRITE) {
		oval(cr,0,0,10,7,0x9b6558);
		disc(cr,-6,-4,4,0xbd8a6e);disc(cr,6,2,4,0x9b6558);disc(cr,2,-3,3,0xca9275);
	} else if(k==BONE_SPRITE) {
		color(cr,0xf1d3a7);cairo_rectangle(cr,-8,-2,16,4);cairo_fill(cr);
		for(int x=-1;x<=1;x+=2) for(int y=-1;y<=1;y+=2)
			disc(cr,x*9,y*2.6,3.25,0xf1d3a7);
	} else if(k==WARNING) {
		for(int i=0;i<3;++i) {
			cairo_move_to(cr,-6,-9+i*6);cairo_line_to(cr,0,-5+i*6);
			cairo_line_to(cr,6,-9+i*6);
		}
		color(cr,0xe8a16e);cairo_set_line_width(cr,1.15);cairo_stroke(cr);
	} else {
		color(cr,0xd8c4f0);cairo_set_line_width(cr,1);
		cairo_arc(cr,0,0,9,0,2*G_PI);cairo_stroke(cr);
		cairo_move_to(cr,-14,0);cairo_line_to(cr,-7,0);cairo_move_to(cr,7,0);cairo_line_to(cr,14,0);
		cairo_move_to(cr,0,-14);cairo_line_to(cr,0,-7);cairo_move_to(cr,0,7);cairo_line_to(cr,0,14);
		cairo_stroke(cr);
	}
}

static void build_sprite(MapArcade *a,enum sprite k) {
	GtkSnapshot *s=gtk_snapshot_new();
	graphene_rect_t r=k<=ESCORT?GRAPHENE_RECT_INIT(-49,-42,98,84):
		k<=DOG_SUIT?GRAPHENE_RECT_INIT(-23,-22,46,44):GRAPHENE_RECT_INIT(-16,-16,32,32);
	cairo_t *cr=gtk_snapshot_append_cairo(s,&r);
	if(k<=ESCORT) {
		if(k==MOTHER) cairo_push_group(cr);
		a->ship(cr,0,0,k==MOTHER?2.7:1.62,k==MOTHER,FALSE,FALSE,FALSE);
		// Two wing cannon housings, including on escort ships.
		double offset=k==MOTHER?36:14;
		for(int dir=-1;dir<=1;dir+=2) {
			cairo_rectangle(cr,dir*offset-2.1,k==MOTHER?-1:1,4.2,9);
			color(cr,0x264657);cairo_fill_preserve(cr);color(cr,0x87d4de);
			cairo_set_line_width(cr,1);cairo_stroke(cr);
		}
		if(k==MOTHER) {
			face(cr,-4,-23,8,16,3);color(cr,0x4a376e);cairo_fill_preserve(cr);
			color(cr,0xa694de);cairo_set_line_width(cr,1);cairo_stroke(cr);
		}
		if(k==MOTHER) {
			cairo_pattern_t *shape=cairo_pop_group(cr);
			cairo_set_source(cr,shape);cairo_paint(cr);
			GtkSnapshot *tint=gtk_snapshot_new();
			cairo_t *mask=gtk_snapshot_append_cairo(tint,&r);
			color(mask,0xff927b);cairo_mask(mask,shape);
			cairo_destroy(mask);cairo_pattern_destroy(shape);
			a->damaged_ship=gtk_snapshot_free_to_node(tint);
		}
	} else if(k<=DOG_SUIT) draw_suit(cr,k);
	else draw_small_asset(cr,k);
	cairo_destroy(cr);
	a->sprite[k]=gtk_snapshot_free_to_node(s);
}
static void place(GtkSnapshot *s,struct vec p,double angle) {
	graphene_point_t at=GRAPHENE_POINT_INIT(p.x,p.y);
	gtk_snapshot_save(s);gtk_snapshot_translate(s,&at);
	if(angle!=0)gtk_snapshot_rotate(s,angle);
}

static void sprite(GtkSnapshot *s,GskRenderNode *n,struct vec p,double angle) {
	place(s,p,angle);gtk_snapshot_append_node(s,n);gtk_snapshot_restore(s);
}

static void banked_ship(GtkSnapshot *s,GskRenderNode *n,GskRenderNode *damage,
	double opacity,struct vec p,double angle,double bank) {
	place(s,p,angle);
	// The bank is a small roll, not a second yaw: wing pitch changes
	// visually without steering gun rays away from the reticle.
	if(bank!=0) {
		graphene_matrix_t roll;
		graphene_matrix_init_from_2d(&roll,1-.085*fabs(bank)/bank_limit,bank*.011,0,1,0,0);
		gtk_snapshot_transform_matrix(s,&roll);
	}
	gtk_snapshot_append_node(s,n);
	if(damage && opacity>0) {
		gtk_snapshot_push_opacity(s,opacity);
		gtk_snapshot_append_node(s,damage);gtk_snapshot_pop(s);
	}
	gtk_snapshot_restore(s);
}

struct game_view {double x,y,scale;};
static struct game_view field_view(graphene_rect_t frame) {
	double scale=MIN((frame.size.width-16)/FIELD_W,(frame.size.height-12)/FIELD_H);
	return (struct game_view){
		frame.origin.x+(frame.size.width-FIELD_W*scale)*.5,
		frame.origin.y+(frame.size.height-FIELD_H*scale)*.5,scale};
}

static void patch_draw(GtkSnapshot *s,const struct patch *p) {
	double a=MIN(1,p->left/.40);
	graphene_rect_t rect=GRAPHENE_RECT_INIT(p->at.x-23,p->at.y-15,46,30);
	GskRoundedRect round;
	gsk_rounded_rect_init_from_rect(&round,&rect,12);
	gtk_snapshot_push_opacity(s,.68*a);
	gtk_snapshot_push_rounded_clip(s,&round);
	fill(s,rect.origin.x,rect.origin.y,46,30,0x5d5439);
	fill(s,p->at.x-12,p->at.y-7,24,14,0xbe9a47);
	fill(s,p->at.x-8,p->at.y-5,13,10,0xefd094);
	gtk_snapshot_pop(s);gtk_snapshot_pop(s);
}

static void snapshot(GtkWidget *w,GtkSnapshot *s) {
	MapArcade *a=(MapArcade *)w;
	struct game *g=&a->g;
	if(!a->playing || a->frame.size.width<24 || a->frame.size.height<24) return;
	if(!a->scene) {
		a->scene=background(a);
		for(enum sprite k=0;k<SPRITES;++k)build_sprite(a,k);
	}
	// Retained scene and sprites; this tick builds only transforms, simple
	// shot quads and text already shaped when a counter changes.
	struct game_view view=field_view(a->frame);
	GtkNative *native=gtk_widget_get_native(w);
	GdkSurface *surface=native?gtk_native_get_surface(native):NULL;
	double density=surface?MAX(1,gdk_surface_get_scale(surface)):1;
	if(!a->grid || a->grid_density!=density) {
		g_clear_pointer(&a->grid,gsk_render_node_unref);
		a->grid=game_grid(a->frame,density);
		a->grid_density=density;
	}
	gtk_snapshot_append_node(s,a->grid);
	// Playfield actors and the HUD plaques are well inside the four corner
	// cuts. Limit their damage by bounds without a full-frame alpha mask.
	gtk_snapshot_push_clip(s,&a->frame);
	graphene_point_t origin=GRAPHENE_POINT_INIT(view.x,view.y);
	gtk_snapshot_save(s);gtk_snapshot_translate(s,&origin);gtk_snapshot_scale(s,view.scale,view.scale);
	for(unsigned i=0;i<PATCHES;++i)if(g->patches[i].left>0)patch_draw(s,&g->patches[i]);
	for(unsigned i=0;i<ENEMIES;++i) {
		struct enemy *e=&g->enemies[i];if(!e->live)continue;
		if(e->warning>0) {
			if(e->warning<.54) sprite(s,a->sprite[WARNING],entry(g,i),e->edge*90);
		} else sprite(s,a->sprite[CHICKEN_SUIT+e->kind],e->at,0);
	}
	for(unsigned i=0;i<SHOTS;++i) if(g->shots[i].live) {
		struct shot *p=&g->shots[i];
		place(s,p->at,atan2(p->v.y,p->v.x)*180/G_PI);
		fill(s,0,-1,10,2,0x99e4ed);gtk_snapshot_restore(s);
	}
	for(unsigned i=0;i<LOBS;++i) if(g->lobs[i].live) {
		struct lob *p=&g->lobs[i];
		sprite(s,a->sprite[EGG_SPRITE+p->kind],p->at,p->spin*180/G_PI);
	}
	if(g->beam>0) {
		double length=beam_length(g)-20;
		double flash=MIN(1,g->beam/.075);
		place(s,g->player,g->aim*180/G_PI);
		gtk_snapshot_push_opacity(s,flash);
		fill(s,20,-PLASMA_HALF_WIDTH,length,2*PLASMA_HALF_WIDTH,0x49366d);
		fill(s,20,-23,length,46,0x7554ad);
		fill(s,20,-10,length,20,0xac82ef);
		fill(s,20,-4,length,8,0xf0dbff);
		fill(s,18,-14,4,28,0xd5aeff);
		gtk_snapshot_pop(s);gtk_snapshot_restore(s);
	}
	for(unsigned i=0;i<ENEMIES;++i) if(g->impacts[i].remaining>0) {
		const struct impact *hit=&g->impacts[i];
		double opacity=hit->remaining/impact_duration,radius=7+(1-opacity)*10;
		gtk_snapshot_push_opacity(s,opacity);
		fill(s,hit->at.x-radius,hit->at.y-1,2*radius,2,0xffd5f4);
		fill(s,hit->at.x-1,hit->at.y-radius,2,2*radius,0xffd5f4);
		fill(s,hit->at.x-4,hit->at.y-4,8,8,0xd59bff);
		gtk_snapshot_pop(s);
	}
	double angle=g->aim*180/G_PI+90;
	banked_ship(s,a->sprite[MOTHER],a->damaged_ship,damage_opacity(g->damage),g->player,angle,g->bank);
	banked_ship(s,a->sprite[ESCORT],NULL,0,mount(g,-FLEET_GAP,24),angle,g->bank);
	banked_ship(s,a->sprite[ESCORT],NULL,0,mount(g,FLEET_GAP,24),angle,g->bank);
	gtk_snapshot_append_node(s,a->scene);
	draw_text(s,a->wave,735,34,0xd5edf5);
	draw_text(s,a->score,813,34,0xd5edf5);
	draw_text(s,a->hull,897,34,0xd5edf5);
	int charged=(int)ceil(CLAMP(1-g->plasma/plasma_cooldown,0,1)*PLASMA_CELLS);
	for(int i=0;i<PLASMA_CELLS;++i)
		fill(s,FIELD_W-221+i*19,FIELD_H-30,15,5,i<charged?0xb4a0f6:0x413b5d);
	if(g->mouse_valid) sprite(s,a->sprite[RETICLE],vec_clamp(g->mouse,g->bounds,4),0);

	if(!g->hull || !a->focused) {
		fill(s,FIELD_W/2-112,FIELD_H/2-22,224,46,0x112431);
		caption(w,s,g->hull?"TẠM DỪNG":"HẾT TÀU   ·   Q CHƠI LẠI",FIELD_W/2-91,FIELD_H/2-10,16,0xd5edf5);
	}
	gtk_snapshot_restore(s);gtk_snapshot_pop(s);
}

static void drop_layouts(GObject *obj) {
	MapArcade *a=(MapArcade *)obj;
	a->playing=FALSE;sync_tick(a);
	g_clear_object(&a->wave);g_clear_object(&a->score);g_clear_object(&a->hull);
	g_clear_pointer(&a->scene,gsk_render_node_unref);
	g_clear_pointer(&a->damaged_ship,gsk_render_node_unref);
	g_clear_pointer(&a->grid,gsk_render_node_unref);
	for(unsigned i=0;i<SPRITES;++i)g_clear_pointer(&a->sprite[i],gsk_render_node_unref);
	G_OBJECT_CLASS(map_arcade_parent_class)->dispose(obj);
}
static void map_arcade_class_init(MapArcadeClass *c) {
	GTK_WIDGET_CLASS(c)->snapshot=snapshot;
	G_OBJECT_CLASS(c)->dispose=drop_layouts;
}
static void map_arcade_init(MapArcade *a) {
	GtkWidget *w=GTK_WIDGET(a);
	gtk_widget_set_can_target(w,FALSE);gtk_widget_set_focusable(w,FALSE);
	gtk_widget_set_hexpand(w,TRUE);gtk_widget_set_vexpand(w,TRUE);
}
GtkWidget *map_arcade_new(map_arcade_ship_painter paint) {
	MapArcade *a=g_object_new(map_arcade_get_type(),NULL);
	a->ship=paint;return GTK_WIDGET(a);
}
void map_arcade_layout(GtkWidget *w,double x,double y,double width,double height) {
	MapArcade *a=(MapArcade *)w;
	graphene_rect_t r=GRAPHENE_RECT_INIT(x,y,width,height);
	if(graphene_rect_equal(&a->frame,&r)) return;
	a->frame=r;
	if(width>=24 && height>=24) {
		struct game_view f=field_view(r);
		double pad=12/f.scale;
		a->g.bounds=(struct bounds){
			(x-f.x)/f.scale+pad,(y-f.y)/f.scale+pad,
			(x+width-f.x)/f.scale-pad,(y+height-f.y)/f.scale-pad};
		if(a->playing)a->g.player=vec_clamp(a->g.player,a->g.bounds,FLEET_GAP+33);
	}
	if(a->g.mouse_valid)map_arcade_aim(w,
		graphene_rect_contains_point(&r,&GRAPHENE_POINT_INIT(a->aim_x,a->aim_y)),a->aim_x,a->aim_y);
	g_clear_pointer(&a->grid,gsk_render_node_unref);
	gtk_widget_queue_draw(w);
}
void map_arcade_start(GtkWidget *w) {
	MapArcade *a=(MapArcade *)w;
	if(!a->wave) {
		a->wave=layout(w,"",24,PANGO_WEIGHT_BOLD);
		a->score=layout(w,"",24,PANGO_WEIGHT_BOLD);
		a->hull=layout(w,"",24,PANGO_WEIGHT_BOLD);
	}
	a->playing=a->focused=TRUE;reset(a);sync_tick(a);
	gtk_widget_queue_draw(w);
}
void map_arcade_stop(GtkWidget *w) {
	MapArcade *a=(MapArcade *)w;
	a->playing=FALSE;a->g.held=a->g.mouse_held=0;sync_tick(a);
	g_clear_pointer(&a->grid,gsk_render_node_unref);
}
void map_arcade_focus(GtkWidget *w,gboolean focus) {
	MapArcade *a=(MapArcade *)w;
	if(!a->playing || a->focused==focus) return;
	a->focused=focus;a->g.held=a->g.mouse_held=0;a->g.velocity=(struct vec){0};
	a->g.mouse_valid=FALSE;sync_tick(a);gtk_widget_queue_draw(w);
}
void map_arcade_aim(GtkWidget *w,gboolean valid,double x,double y) {
	MapArcade *a=(MapArcade *)w;
	if(!a->playing || !a->focused) return;
	struct game_view f=field_view(a->frame);
	valid=valid && f.scale>0;
	a->g.mouse_valid=valid;
	if(!valid)a->g.mouse_held=0;
	if(valid) {
		a->aim_x=x;a->aim_y=y;
		a->g.mouse=(struct vec){(x-f.x)/f.scale,(y-f.y)/f.scale};
		update_aim(&a->g);
	}
	// The existing clock consumes the most recent position; do not turn
	// mouse event frequency into extra frames.
	if(!a->tick)gtk_widget_queue_draw(w);
}
unsigned map_arcade_key_bit(guint key) {
	switch(gdk_keyval_to_lower(key)) {
	case GDK_KEY_w: return UP;
	case GDK_KEY_s: return DOWN;
	case GDK_KEY_a: return LEFT;
	case GDK_KEY_d: return RIGHT;
	case GDK_KEY_q: return FIRE;
	case GDK_KEY_e: return PLASMA;
	default: return 0;
	}
}
static void attack_press(MapArcade *a,unsigned bit,unsigned *held) {
	*held|=bit;
	if(!a->g.hull && bit==FIRE) {
		struct vec aim=a->g.mouse;
		gboolean have_aim=a->g.mouse_valid;
		reset(a);
		a->g.mouse=aim;a->g.mouse_valid=have_aim;update_aim(&a->g);
		*held|=FIRE;
		sync_tick(a);
	}
	if(a->g.hull && bit==PLASMA && plasma_begin(&a->g)) {
		stats(a);gtk_widget_queue_draw(GTK_WIDGET(a));
	}
	if(a->g.hull && bit==FIRE && !a->g.next_fire) {
		fire(&a->g);gtk_widget_queue_draw(GTK_WIDGET(a));
	}
}
void map_arcade_weapon(GtkWidget *w,gboolean plasma,gboolean pressed) {
	MapArcade *a=(MapArcade *)w;
	unsigned bit=plasma?PLASMA:FIRE;
	if(!pressed) {a->g.mouse_held&=~bit;return;}
	if(a->playing && a->focused && a->g.mouse_valid && !(a->g.mouse_held&bit))
		attack_press(a,bit,&a->g.mouse_held);
}
gboolean map_arcade_key(GtkWidget *w,guint key,gboolean pressed) {
	MapArcade *a=(MapArcade *)w;
	unsigned bit=map_arcade_key_bit(key);
	if(!a->playing || !a->focused || !bit) return FALSE;
	if(!pressed) {a->g.held&=~bit;return TRUE;}
	if(!(a->g.held&bit))attack_press(a,bit,&a->g.held);
	return TRUE;
}
