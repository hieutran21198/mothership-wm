//go:build linux && cgo && panel

#include "systems-board.h"
#include "systems-bluetooth.h"
#include "analog-button.h"
#include "analog-glass.h"
#include "home-panel.h"
#include "shell-type.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

extern void panelSystemsWatch(int);
extern void panelSystemsSound(int,char *,int);

enum { OP_SINK, OP_SINK_VOLUME, OP_SINK_MUTE, OP_SOURCE, OP_SOURCE_VOLUME, OP_SOURCE_MUTE };
enum { BOARD_WIDTH=560, BOARD_MIN_HEIGHT=480, HEADER=50, FOOTER=50, GUT=30, ROW=38, BT_ROW=44,
	MAX_SINKS=6, MAX_SOURCES=5, MAX_PAIRED=5, RADAR_HEIGHT=200, MAX_ITEMS=96 };
#define OPEN_MS 120.
#define CLOSE_MS 140.
#define FADE_IN_MS 150.
#define FADE_OUT_MS 180.
#define HEAD_MS 200.
#define SWITCH_TIMEOUT_S 3

/* ---------- unfold container ---------- */
#define SYSTEMS_TYPE_FRAME (systems_frame_get_type())
G_DECLARE_FINAL_TYPE(SystemsFrame,systems_frame,SYSTEMS,FRAME,GtkWidget)
struct _SystemsFrame { GtkWidget parent; GtkWidget *child; };
G_DEFINE_TYPE(SystemsFrame,systems_frame,GTK_TYPE_WIDGET)

static double unfold,unfold_target,unfold_from; // 0 closed, 1 open
static gint64 unfold_start; // frame time of the first frame of this run
static int unfold_anchor_x;

static void frame_dispose(GObject *o) {
	SystemsFrame *f=SYSTEMS_FRAME(o);
	g_clear_pointer(&f->child,gtk_widget_unparent);
	G_OBJECT_CLASS(systems_frame_parent_class)->dispose(o);
}
static void frame_measure(GtkWidget *w,GtkOrientation o,int for_size,int *min,int *nat,int *minb,int *natb) {
	SystemsFrame *f=SYSTEMS_FRAME(w);
	if(f->child) gtk_widget_measure(f->child,o,for_size,min,nat,minb,natb);
}
static void frame_allocate(GtkWidget *w,int width,int height,int baseline) {
	SystemsFrame *f=SYSTEMS_FRAME(w);
	if(f->child) gtk_widget_allocate(f->child,width,height,baseline,NULL);
}
static double ease(double t) {t=CLAMP(t,0,1);return t<.5?4*t*t*t:1-pow(-2*t+2,3)/2;}
static double ease_out(double t) {t=CLAMP(t,0,1);return t*(2-t);}
static void frame_snapshot(GtkWidget *w,GtkSnapshot *s) {
	SystemsFrame *f=SYSTEMS_FRAME(w);
	if(!f->child) return;
	double W=gtk_widget_get_width(w),H=gtk_widget_get_height(w),u=unfold;
	if(u>=1) {gtk_widget_snapshot_child(w,f->child,s);return;}
	if(W<=0 || H<=0) return;
	double anchor=CLAMP(unfold_anchor_x,0,W),t=ease_out(u);
	double left=anchor*(1-t);
	gtk_snapshot_push_clip(s,&GRAPHENE_RECT_INIT(left,0,W*t,H));
	gtk_widget_snapshot_child(w,f->child,s);
	gtk_snapshot_pop(s);
}
static void systems_frame_class_init(SystemsFrameClass *k) {
	G_OBJECT_CLASS(k)->dispose=frame_dispose;
	GtkWidgetClass *wk=GTK_WIDGET_CLASS(k);
	wk->measure=frame_measure;
	wk->size_allocate=frame_allocate;
	wk->snapshot=frame_snapshot;
}
static void systems_frame_init(SystemsFrame *f) {(void)f;}

/* ---------- state ---------- */
struct snd { char *name,*desc,*kind,*icon; int percent; gboolean muted,def; };
enum item_kind { IT_CLOSE=1, IT_MODULE, IT_SINK, IT_SOURCE, IT_OUT_MUTE, IT_OUT_LEVEL, IT_IN_MUTE, IT_IN_LEVEL,
	IT_POWER, IT_PAIRED, IT_PAIRED_ACT, IT_SCAN, IT_NEAR, IT_CONFIRM, IT_REJECT };
struct item { int kind,index; double x,y,w,h; gboolean focusable; };
struct glow { int key; double a; };

static GtkWidget *pane,*canvas;
static void (*on_region)(void);
static gboolean open,motion=TRUE;
static int module=SYSTEMS_SOUND,width=BOARD_WIDTH,height=BOARD_MIN_HEIGHT,avail_height=900;
static GArray *sinks,*sources;
static char *sound_error,*row_error_name,*row_error;
static gboolean sound_known;
static char *pending[2];
static guint pending_timer[2];
static int local_level[2]={-1,-1};
static double shown_level[2]={-1,-1};
static int drag_level=-1;
static guint level_commit[2];
static gint64 level_hold[2];
static double head_t=1;
static int head_level=-1;
static double content_fade=1;
static double card_alpha;
static struct item items[MAX_ITEMS];
static int n_items;
static int focus_kind,focus_index,hover_key=-1;
static struct glow glows[MAX_ITEMS];
static int n_glows;
static guint tick_id;
static gint64 last_frame;
static double sweep;
static char *forget_armed;
static guint forget_timer;
static char entry[8];
static GHashTable *dot_radius;
static gboolean scan_on_ready;

static int key_of(int kind,int index) {return kind*256+index;}
static void snd_clear(GArray *a) {
	for(guint i=0;i<a->len;i++) {
		struct snd *s=&g_array_index(a,struct snd,i);
		g_free(s->name);g_free(s->desc);g_free(s->kind);g_free(s->icon);
	}
	g_array_set_size(a,0);
}
static struct snd *snd_default(GArray *a) {
	for(guint i=0;i<a->len;i++) if(g_array_index(a,struct snd,i).def) return &g_array_index(a,struct snd,i);
	return NULL;
}
static int snd_index(GArray *a,const char *name) {
	for(guint i=0;i<a->len;i++) if(g_strcmp0(g_array_index(a,struct snd,i).name,name)==0) return (int)i;
	return -1;
}
static void motion_refresh(void) {
	gboolean allowed=TRUE;
	if(pane) g_object_get(gtk_widget_get_settings(pane),"gtk-enable-animations",&allowed,NULL);
	motion=allowed;
}
static void ensure_tick(void);
static void redraw(void) {if(canvas) gtk_widget_queue_draw(canvas);ensure_tick();}

/* ---------- drawing helpers ---------- */
static void rgba(cairo_t *cr,unsigned rgb,double a) {
	cairo_set_source_rgba(cr,(rgb>>16)/255.,((rgb>>8)&255)/255.,(rgb&255)/255.,a);
}
static void chamfer(cairo_t *cr,double x,double y,double w,double h,double c) {
	cairo_move_to(cr,x+c,y);cairo_line_to(cr,x+w-c,y);cairo_line_to(cr,x+w,y+c);
	cairo_line_to(cr,x+w,y+h-c);cairo_line_to(cr,x+w-c,y+h);cairo_line_to(cr,x+c,y+h);
	cairo_line_to(cr,x,y+h-c);cairo_line_to(cr,x,y+c);cairo_close_path(cr);
}
// Text centered on y; align 0 left, 1 right, 2 center. Returns the width.
static double text(cairo_t *cr,const char *s,double x,double y,double max,double size,PangoWeight weight,
		unsigned rgb,double a,int align,gboolean title) {
	if(!s || !*s) return 0;
	// One layout and a few font descriptions serve every label; creating
	// them per call dominated the redraw cost.
	static PangoLayout *l;
	static struct {gboolean title;int size,weight;PangoFontDescription *f;} fonts[16];
	if(!l) l=pango_cairo_create_layout(cr); else pango_cairo_update_layout(cr,l);
	PangoFontDescription *f=NULL,*spare=NULL;
	int key=(int)lround(size*10);
	for(int i=0;i<16 && fonts[i].f;i++)
		if(fonts[i].title==title && fonts[i].size==key && fonts[i].weight==(int)weight) {f=fonts[i].f;break;}
	if(!f) {
		f=shell_font_description(title?SHELL_DISPLAY_FALLBACK:NULL,size,weight);
		spare=f;
		for(int i=0;i<16;i++) if(!fonts[i].f) {fonts[i].title=title;fonts[i].size=key;fonts[i].weight=weight;fonts[i].f=f;spare=NULL;break;}
	}
	pango_layout_set_font_description(l,f);
	pango_layout_set_text(l,s,-1);
	pango_layout_set_single_paragraph_mode(l,TRUE);
	pango_layout_set_alignment(l,PANGO_ALIGN_LEFT);
	if(max>0) {pango_layout_set_width(l,(int)(max*PANGO_SCALE));pango_layout_set_ellipsize(l,PANGO_ELLIPSIZE_END);}
	else {pango_layout_set_width(l,-1);pango_layout_set_ellipsize(l,PANGO_ELLIPSIZE_NONE);}
	int tw,th;pango_layout_get_pixel_size(l,&tw,&th);
	double left=align==1?x-tw:align==2?x-tw/2.:x;
	if(max>0 && align==1) {pango_layout_set_alignment(l,PANGO_ALIGN_RIGHT);left=x-max;}
	rgba(cr,rgb,a);
	cairo_move_to(cr,left,y-th/2.);
	pango_cairo_show_layout(cr,l);
	if(spare) pango_font_description_free(spare);
	return tw;
}
static void corners(cairo_t *cr,double x,double y,double w,double h,double len,double k,unsigned rgb,double a,double lw) {
	double pts[4][4][2]={{{x,y+len},{x,y+k},{x+k,y},{x+len,y}},{{x+w-len,y},{x+w-k,y},{x+w,y+k},{x+w,y+len}},
		{{x+w,y+h-len},{x+w,y+h-k},{x+w-k,y+h},{x+w-len,y+h}},{{x+len,y+h},{x+k,y+h},{x,y+h-k},{x,y+h-len}}};
	for(int i=0;i<4;i++) {
		cairo_move_to(cr,pts[i][0][0],pts[i][0][1]);
		for(int j=1;j<4;j++) cairo_line_to(cr,pts[i][j][0],pts[i][j][1]);
	}
	rgba(cr,rgb,a);cairo_set_line_width(cr,lw);cairo_set_line_cap(cr,CAIRO_LINE_CAP_SQUARE);cairo_stroke(cr);
}
// Shared Slash / Notification Log item treatment, faded by alpha.
static void item_highlight(cairo_t *cr,double x,double y,double w,double h,double a) {
	if(a<=.01) return;
	cairo_rectangle(cr,x,y,w,h);rgba(cr,0x0a141c,.65*a);cairo_fill(cr);
	corners(cr,x,y,w,h,9,3,0x56d4dd,a,1.2);
}
static void glyph_path(cairo_t *cr,const char *g) {
	if(!g) return;
	if(!strcmp(g,"speaker")||!strcmp(g,"speaker-muted")) {
		cairo_move_to(cr,3.5,8);cairo_line_to(cr,6.5,8);cairo_line_to(cr,10.5,4.5);cairo_line_to(cr,10.5,15.5);
		cairo_line_to(cr,6.5,12);cairo_line_to(cr,3.5,12);cairo_close_path(cr);
		if(g[7]) {cairo_move_to(cr,13,7.5);cairo_line_to(cr,17.5,12.5);cairo_move_to(cr,17.5,7.5);cairo_line_to(cr,13,12.5);}
		else {cairo_new_sub_path(cr);cairo_arc(cr,10.5,10,3.5,-.9,.9);cairo_new_sub_path(cr);cairo_arc(cr,10.5,10,6.5,-.8,.8);}
	} else if(!strcmp(g,"mic")||!strcmp(g,"mic-muted")) {
		cairo_move_to(cr,7.5,5);cairo_arc(cr,10,5,2.5,G_PI,0);cairo_line_to(cr,12.5,9);cairo_arc(cr,10,9,2.5,0,G_PI);cairo_close_path(cr);
		cairo_move_to(cr,4.5,9.5);cairo_arc_negative(cr,10,9.5,5.5,G_PI,0);cairo_move_to(cr,10,15);cairo_line_to(cr,10,17.5);
		if(g[3]) {cairo_move_to(cr,4,16.5);cairo_line_to(cr,16,3.5);}
	} else if(!strcmp(g,"headphones")) {
		cairo_move_to(cr,3.5,13);cairo_line_to(cr,3.5,10.5);cairo_arc(cr,10,10.5,6.5,G_PI,2*G_PI);cairo_line_to(cr,16.5,13);
		cairo_rectangle(cr,3,12,3.5,5);cairo_rectangle(cr,13.5,12,3.5,5);
	} else if(!strcmp(g,"monitor")) {
		cairo_rectangle(cr,2.5,4,15,10);cairo_move_to(cr,7,17);cairo_line_to(cr,13,17);cairo_move_to(cr,10,14);cairo_line_to(cr,10,17);
	} else if(!strcmp(g,"speaker-box")) {
		cairo_rectangle(cr,5,2.5,10,15);cairo_new_sub_path(cr);cairo_arc(cr,10,12,3,0,2*G_PI);cairo_new_sub_path(cr);cairo_arc(cr,10,6,1,0,2*G_PI);
	} else if(!strcmp(g,"mouse")) {
		cairo_move_to(cr,6,7);cairo_arc(cr,10,7,4,G_PI,0);cairo_line_to(cr,14,13);cairo_arc(cr,10,13,4,0,G_PI);cairo_close_path(cr);
		cairo_move_to(cr,10,3);cairo_line_to(cr,10,8);
	} else if(!strcmp(g,"keyboard")) {
		cairo_rectangle(cr,2.5,6,15,9);
		for(int i=0;i<4;i++) {cairo_move_to(cr,5+3*i,9);cairo_line_to(cr,6+3*i,9);}
		cairo_move_to(cr,6,12);cairo_line_to(cr,14,12);
	} else if(!strcmp(g,"phone")) {
		cairo_rectangle(cr,6,2.5,8,15);cairo_move_to(cr,9,15);cairo_line_to(cr,11,15);
	} else if(!strcmp(g,"bt")) {
		cairo_move_to(cr,6,6.5);cairo_line_to(cr,14,13.5);cairo_line_to(cr,10,17);cairo_line_to(cr,10,3);
		cairo_line_to(cr,14,6.5);cairo_line_to(cr,6,13.5);
	} else if(!strcmp(g,"close")) {
		cairo_move_to(cr,5.5,5.5);cairo_line_to(cr,14.5,14.5);cairo_move_to(cr,14.5,5.5);cairo_line_to(cr,5.5,14.5);
	} else {
		cairo_new_sub_path(cr);cairo_arc(cr,10,10,6,0,2*G_PI);cairo_move_to(cr,10,7);cairo_line_to(cr,10,13);
	}
}
static void glyph(cairo_t *cr,const char *g,double x,double y,double k,unsigned rgb,double a) {
	cairo_save(cr);
	cairo_translate(cr,x,y);cairo_scale(cr,k,k);
	glyph_path(cr,g);
	cairo_restore(cr);
	rgba(cr,rgb,a);cairo_set_line_width(cr,1.45);
	cairo_set_line_cap(cr,CAIRO_LINE_CAP_ROUND);cairo_set_line_join(cr,CAIRO_LINE_JOIN_ROUND);cairo_stroke(cr);
}
static const char *bt_glyph(const char *icon) {
	if(!icon) return "bt";
	if(g_str_has_prefix(icon,"audio-head")) return "headphones";
	if(g_str_has_prefix(icon,"audio")) return "speaker-box";
	if(g_str_has_prefix(icon,"input-mouse")) return "mouse";
	if(g_str_has_prefix(icon,"input-keyboard")) return "keyboard";
	if(g_str_has_prefix(icon,"phone")) return "phone";
	if(g_str_has_prefix(icon,"computer")||g_str_has_prefix(icon,"video-display")) return "monitor";
	return "bt";
}
// Top-down Mothership mark, 140 × 110 units.
static void ship(cairo_t *cr,double cx,double cy,double k) {
	static const double hull[][2]={{70,0},{88,26},{92,54},{136,78},{136,96},{104,92},{98,104},{84,104},{80,94},
		{60,94},{56,104},{42,104},{36,92},{4,96},{4,78},{48,54},{52,26}};
	cairo_save(cr);
	cairo_translate(cr,cx-70*k,cy-55*k);cairo_scale(cr,k,k);
	cairo_move_to(cr,hull[0][0],hull[0][1]);
	for(size_t i=1;i<G_N_ELEMENTS(hull);i++) cairo_line_to(cr,hull[i][0],hull[i][1]);
	cairo_close_path(cr);
	cairo_restore(cr);
	rgba(cr,0x1d2e39,1);cairo_fill_preserve(cr);
	rgba(cr,0x8fd3e0,1);cairo_set_line_width(cr,1.2);cairo_set_line_join(cr,CAIRO_LINE_JOIN_MITER);cairo_stroke(cr);
}
static void radio(cairo_t *cr,double x,double y,gboolean on,gboolean pending) {
	chamfer(cr,x,y,12,12,3);
	rgba(cr,on||pending?0x56d4dd:0x5d7a88,1);cairo_set_line_width(cr,1.2);cairo_stroke(cr);
	if(on) {chamfer(cr,x+3,y+3,6,6,1.5);rgba(cr,0x56d4dd,1);cairo_fill(cr);}
}
static void button(cairo_t *cr,double x,double y,double w,double h,const char *label,const char *g,
		gboolean on,gboolean focused,gboolean hovered,gboolean disabled) {
	GtkStateFlags f=(on?GTK_STATE_FLAG_CHECKED:0)|(focused?GTK_STATE_FLAG_FOCUS_VISIBLE:0)|
		(hovered?GTK_STATE_FLAG_PRELIGHT:0)|(disabled?GTK_STATE_FLAG_INSENSITIVE:0);
	analog_button_paint(cr,x,y,w,h,f);
	unsigned ink=disabled?0x5d7180:on?0xa8f1f6:0xe6f1f6;
	if(g) glyph(cr,g,x+w/2-8,y+h/2-8,.8,ink,1);
	else text(cr,label,x+w/2,y+h/2,0,11,PANGO_WEIGHT_SEMIBOLD,ink,1,2,FALSE);
}
static void keycap(cairo_t *cr,double x,double y,const char *label,double *w) {
	double kw=MAX(20,strlen(label)*6.4+12);
	chamfer(cr,x,y,kw,20,3);rgba(cr,0x111c25,1);cairo_fill_preserve(cr);
	rgba(cr,0x4b6371,1);cairo_set_line_width(cr,1);cairo_stroke(cr);
	text(cr,label,x+kw/2,y+10,0,10,PANGO_WEIGHT_SEMIBOLD,0x9fb3c0,1,2,FALSE);
	*w=kw;
}
static void slider(cairo_t *cr,double x,double y,double w,double v,gboolean muted) {
	cairo_rectangle(cr,x,y,w,4);rgba(cr,0x1c2c36,1);cairo_fill(cr);
	cairo_rectangle(cr,x,y,w*v,4);rgba(cr,muted?0x3d5260:0x6cc3cc,1);cairo_fill(cr);
	for(int i=1;i<4;i++) {cairo_rectangle(cr,floor(x+w*i/4.),y+7,1,3);rgba(cr,0x2f4552,1);cairo_fill(cr);}
	cairo_rectangle(cr,x+w*v-1.5,y-6,3,16);rgba(cr,muted?0x9fb3c0:0xa8f1f6,1);cairo_fill(cr);
}

/* ---------- layout ---------- */
static int rail_width(void) {return width<500?118:140;}
static double CX(void) {return 12+rail_width()+28;}
static double CW(void) {return width-CX()-20;}
static void add_item(int kind,int index,double x,double y,double w,double h,gboolean focusable) {
	if(n_items>=MAX_ITEMS) return;
	items[n_items++]=(struct item){kind,index,x,y,w,h,focusable};
}
struct sound_rows { double out_label,out_rows,out_level,in_label,in_rows,in_level,bottom; int ns,nr; };
static struct sound_rows sound_rows(void) {
	struct sound_rows r={0};
	double y0=HEADER+12;
	r.ns=MIN((int)sinks->len,MAX_SINKS);r.nr=MIN((int)sources->len,MAX_SOURCES);
	r.out_label=y0+8;r.out_rows=y0+22;
	r.out_level=r.out_rows+MAX(1,r.ns)*ROW+6;
	r.in_label=r.out_level+50;r.in_rows=r.in_label+14;
	r.in_level=r.in_rows+MAX(1,r.nr)*ROW+6;
	r.bottom=r.in_level+36;
	return r;
}
struct bt_rows { double adapter,paired_label,paired,near_label,near,bottom; int np; };
static struct bt_rows bt_rows(void) {
	struct bt_rows r={0};
	double y0=HEADER+12;
	GPtrArray *p=bt_paired();
	r.np=MIN((int)p->len,MAX_PAIRED);
	r.adapter=y0;
	r.paired_label=y0+(bt_adapter_error()?78:62);
	r.paired=r.paired_label+12;
	r.near_label=r.paired+MAX(1,r.np)*BT_ROW+24;
	r.near=r.near_label+22;
	r.bottom=r.near+RADAR_HEIGHT;
	return r;
}
static void radar_geometry(double ny,double *ax,double *ay,double *R) {
	*R=MIN(150,CW()/2-24);*ax=CX()+CW()/2;*ay=ny+*R+30;
}
static void radar_point(int index,const struct bt_device *d,double ax,double ay,double R,double *x,double *y) {
	// Distance follows the signal; the angle is only a stable spot per device.
	double target=d->has_rssi?CLAMP((-40.-d->rssi)/55.,0,1)*.72+.25:1;
	double *r=dot_radius?g_hash_table_lookup(dot_radius,d->path):NULL;
	double t=r?*r:target;
	guint h=g_str_hash(d->path);
	double deg=-158+(h%137)*1.0+(index%2)*3;
	*x=ax+R*t*cos(deg*G_PI/180);*y=ay+R*t*sin(deg*G_PI/180);
}
static gboolean card_shown(void) {
	const struct bt_pairing *p=bt_pairing();
	return p->path!=NULL;
}
static void layout_items(void) {
	n_items=0;
	add_item(IT_CLOSE,0,width-44,12,28,26,FALSE);
	for(int i=0;i<2;i++) add_item(IT_MODULE,i,12,HEADER+12+i*60,rail_width(),52,FALSE);
	double cx=CX(),cw=CW();
	if(module==SYSTEMS_SOUND) {
		if(sound_error || !sound_known) return;
		struct sound_rows r=sound_rows();
		for(int i=0;i<r.ns;i++) add_item(IT_SINK,i,cx-6,r.out_rows+i*ROW-2,cw+12,36,TRUE);
		add_item(IT_OUT_MUTE,0,cx+GUT,r.out_level,30,26,FALSE);
		add_item(IT_OUT_LEVEL,0,cx-6,r.out_level-4,cw+12,34,TRUE);
		for(int i=0;i<r.nr;i++) add_item(IT_SOURCE,i,cx-6,r.in_rows+i*ROW-2,cw+12,36,TRUE);
		add_item(IT_IN_MUTE,0,cx+GUT,r.in_level,30,26,FALSE);
		add_item(IT_IN_LEVEL,0,cx-6,r.in_level-4,cw+12,34,TRUE);
		return;
	}
	enum bt_state st=bt_state(NULL);
	if(st==BT_LOADING || st==BT_NO_ADAPTER) return;
	struct bt_rows r=bt_rows();
	if(st!=BT_BLOCKED) {
		add_item(IT_POWER,0,cx+cw-104,r.adapter+6,48,26,FALSE);
		add_item(IT_POWER,1,cx+cw-50,r.adapter+6,48,26,FALSE);
		add_item(IT_POWER,2,cx-6,r.adapter-2,cw+12,40,TRUE);
	}
	if(st!=BT_ON) return;
	GPtrArray *p=bt_paired();
	for(int i=0;i<r.np;i++) {
		add_item(IT_PAIRED_ACT,i,cx+cw-100,r.paired+i*BT_ROW+4,100,26,FALSE);
		add_item(IT_PAIRED,i,cx-6,r.paired+i*BT_ROW-3,cw+12,40,TRUE);
	}
	(void)p;
	add_item(IT_SCAN,0,cx+cw-64,r.near_label-13,64,26,TRUE);
	if(card_shown()) {
		const struct bt_pairing *pp=bt_pairing();
		double py=r.near+4;
		if(pp->prompt==BT_PROMPT_CONFIRM || pp->prompt==BT_PROMPT_PIN || pp->prompt==BT_PROMPT_PASSKEY)
			add_item(IT_CONFIRM,0,cx+cw-86,py+70,80,26,TRUE);
		add_item(IT_REJECT,0,cx+cw-170,py+70,76,26,TRUE);
		return;
	}
	int unnamed=0;
	GPtrArray *near=bt_nearby(&unnamed);
	double ax,ay,R;radar_geometry(r.near,&ax,&ay,&R);
	for(guint i=0;i<near->len && i<12;i++) {
		double x,y;radar_point(i,g_ptr_array_index(near,i),ax,ay,R,&x,&y);
		add_item(IT_NEAR,i,x-10,y-14,96,46,TRUE);
	}
}
static int needed_height(void) {
	int h=BOARD_MIN_HEIGHT;
	h=MAX(h,(int)sound_rows().bottom+FOOTER);
	h=MAX(h,(int)bt_rows().bottom+FOOTER);
	return MIN(h,avail_height);
}
static void resize(void) {
	if(!canvas) return;
	int h=needed_height();
	int cur=-1;gtk_widget_get_size_request(canvas,NULL,&cur);
	if(cur!=h) {gtk_widget_set_size_request(canvas,width,h);if(on_region && open) on_region();}
}
static struct item *find_item(int kind,int index) {
	for(int i=0;i<n_items;i++) if(items[i].kind==kind && items[i].index==index) return &items[i];
	return NULL;
}
static int focus_position(void) {
	int pos=-1,n=0;
	for(int i=0;i<n_items;i++) if(items[i].focusable) {
		if(items[i].kind==focus_kind && items[i].index==focus_index) pos=n;
		n++;
	}
	return pos;
}
static void focus_nth(int n) {
	int k=0;
	for(int i=0;i<n_items;i++) if(items[i].focusable) {
		if(k==n) {focus_kind=items[i].kind;focus_index=items[i].index;return;}
		k++;
	}
}
static int focus_count(void) {int n=0;for(int i=0;i<n_items;i++) n+=items[i].focusable;return n;}
// Keep focus on the same item across updates, else the nearest one.
static void focus_fix(void) {
	if(focus_position()>=0) return;
	int count=focus_count();
	if(!count) {focus_kind=0;return;}
	// Prefer the default device or the paired list on a fresh module.
	if(module==SYSTEMS_SOUND) {
		struct snd *d=snd_default(sinks);
		int idx=d?snd_index(sinks,d->name):0;
		if(find_item(IT_SINK,idx)) {focus_kind=IT_SINK;focus_index=idx;return;}
	}
	if(card_shown() && find_item(IT_CONFIRM,0)) {focus_kind=IT_CONFIRM;focus_index=0;return;}
	focus_nth(0);
}

/* ---------- glow (highlight) animation ---------- */
static double *glow_slot(int key) {
	for(int i=0;i<n_glows;i++) if(glows[i].key==key) return &glows[i].a;
	if(n_glows>=MAX_ITEMS) return NULL;
	glows[n_glows]=(struct glow){key,0};
	return &glows[n_glows++].a;
}
static double glow_of(int kind,int index) {
	for(int i=0;i<n_glows;i++) if(glows[i].key==key_of(kind,index)) return glows[i].a;
	return 0;
}
static void glows_snap(void) {
	for(int i=0;i<n_items;i++) if(items[i].focusable) glow_slot(key_of(items[i].kind,items[i].index));
	for(int i=0;i<n_glows;i++) {
		int kind=glows[i].key/256,index=glows[i].key%256;
		glows[i].a=open && kind==focus_kind && index==focus_index && find_item(kind,index)?1:0;
	}
}
static gboolean glows_step(double dt) {
	gboolean moving=FALSE;
	for(int i=0;i<n_items;i++) if(items[i].focusable) glow_slot(key_of(items[i].kind,items[i].index));
	for(int i=0;i<n_glows;i++) {
		int kind=glows[i].key/256,index=glows[i].key%256;
		gboolean on=open && ((kind==focus_kind && index==focus_index) || glows[i].key==hover_key) && find_item(kind,index);
		double target=on?1:0,a=glows[i].a;
		if(!motion) a=target;
		else if(a<target) a=MIN(target,a+dt/FADE_IN_MS);
		else if(a>target) a=MAX(target,a-dt/FADE_OUT_MS);
		glows[i].a=a;
		if(a!=target) moving=TRUE;
	}
	return moving;
}

/* ---------- drawing ---------- */
static void draw_header(cairo_t *cr) {
	text(cr,"SYSTEMS",20,28,0,15,PANGO_WEIGHT_NORMAL,0xe6f1f6,1,0,TRUE);
	struct item *c=find_item(IT_CLOSE,0);
	if(c) button(cr,c->x,c->y,c->w,c->h,NULL,"close",FALSE,FALSE,hover_key==key_of(IT_CLOSE,0),FALSE);
	cairo_rectangle(cr,14,HEADER,width-28,1);rgba(cr,0x2a3f4c,1);cairo_fill(cr);
	const char *names[]={"Sound","Bluetooth"};
	for(int i=0;i<2;i++) {
		double y=HEADER+12+i*60;
		gboolean sel=module==i;
		if(sel) item_highlight(cr,12,y,rail_width(),52,1);
		else if(hover_key==key_of(IT_MODULE,i)) {cairo_rectangle(cr,12,y,rail_width(),52);rgba(cr,0x0a141c,.4);cairo_fill(cr);}
		g_autofree char *status=NULL;
		const char *g="bt";
		if(i==0) {
			struct snd *d=snd_default(sinks);
			g=d && d->muted?"speaker-muted":"speaker";
			status=sound_error?g_strdup("Unavailable"):!d?g_strdup(sound_known?"No output":"Loading…"):
				d->muted?g_strdup_printf("Muted · %s",d->desc):g_strdup_printf("%d%% · %s",d->percent,d->desc);
		} else {
			enum bt_state st=bt_state(NULL);
			int on=0;GPtrArray *p=bt_paired();
			for(guint k=0;k<p->len;k++) on+=((struct bt_device *)g_ptr_array_index(p,k))->connected;
			status=g_strdup(st==BT_LOADING?"Loading…":st==BT_NO_ADAPTER?"No adapter":st==BT_BLOCKED?"Blocked":
				st==BT_OFF?"Off":NULL);
			if(!status) status=on?g_strdup_printf("On · %d connected",on):g_strdup("On");
		}
		glyph(cr,g,24,y+9,.9,sel?0xa8f1f6:0xb2cad5,1);
		text(cr,names[i],50,y+18,rail_width()-44,13,PANGO_WEIGHT_SEMIBOLD,sel?0xe6f1f6:0xc9dde6,1,0,FALSE);
		text(cr,status,50,y+36,rail_width()-44,10,PANGO_WEIGHT_NORMAL,0x9fb3c0,1,0,FALSE);
	}
	cairo_rectangle(cr,12+rail_width()+12,HEADER+12,1,height-HEADER-12-FOOTER);rgba(cr,0x22323d,1);cairo_fill(cr);
}
static void draw_footer(cairo_t *cr) {
	double fy=height-36;
	cairo_rectangle(cr,14,fy-8,width-28,1);rgba(cr,0x2a3f4c,1);cairo_fill(cr);
	struct {const char *keys[2];const char *cap;} groups[]={{{"Tab",NULL},"module"},{{"W","S"},"move"},
		{{"A","D"},module==SYSTEMS_SOUND?"level":"power"},{{"Enter",NULL},"choose"},
		{{"Space",NULL},module==SYSTEMS_SOUND?"mute":"toggle"},{{"Esc",NULL},NULL}};
	double x=16;
	for(size_t g=0;g<G_N_ELEMENTS(groups);g++) {
		for(int k=0;k<2 && groups[g].keys[k];k++) {double w;keycap(cr,x,fy,groups[g].keys[k],&w);x+=w+4;}
		if(groups[g].cap) x+=text(cr,groups[g].cap,x+2,fy+10,0,10,PANGO_WEIGHT_NORMAL,0x5d7180,1,0,FALSE)+14;
		else x+=6;
		if(x>width-40) break;
	}
}
static void beam(cairo_t *cr,double sy,double ty,int style,double alpha,double head) {
	double bx=CX()+10,ex=CX()+GUT-3;
	cairo_move_to(cr,bx,sy);cairo_line_to(cr,bx,ty-10);cairo_line_to(cr,bx+10,ty);cairo_line_to(cr,ex,ty);
	if(style==1) {
		double dash[]={4,4};
		cairo_set_dash(cr,dash,2,0);rgba(cr,0x56d4dd,alpha);cairo_set_line_width(cr,1.4);cairo_stroke(cr);
		cairo_set_dash(cr,NULL,0,0);
		return;
	}
	cairo_path_t *p=cairo_copy_path(cr);
	rgba(cr,0x56d4dd,.16*alpha);cairo_set_line_width(cr,5);cairo_stroke(cr);
	cairo_append_path(cr,p);cairo_path_destroy(p);
	rgba(cr,0xa8f1f6,alpha);cairo_set_line_width(cr,1.4);cairo_stroke(cr);
	if(head>=0 && head<1) {
		// One bright head runs down the beam when a switch is confirmed.
		double len=(ty-10-sy)+14+(ex-bx-10),d=len*head,x=bx,y=sy+d;
		if(d>ty-10-sy) {double over=d-(ty-10-sy);if(over<14){x=bx+over*.707;y=ty-10+over*.707;}else{x=bx+10+over-14;y=ty;}}
		cairo_arc(cr,x,y,3.2,0,2*G_PI);rgba(cr,0xa8f1f6,1-head*.4);cairo_fill(cr);
		cairo_arc(cr,x,y,7,0,2*G_PI);rgba(cr,0x56d4dd,.25*(1-head));cairo_fill(cr);
	}
}
static void draw_device_row(cairo_t *cr,struct snd *s,int kind,int i,double y,int set) {
	double cx=CX(),cw=CW(),X=cx+GUT;
	item_highlight(cr,cx-6,y-2,cw+12,36,glow_of(kind,i));
	gboolean is_pending=pending[set] && !g_strcmp0(pending[set],s->name);
	radio(cr,X,y+10,s->def,is_pending);
	const char *g=!strcmp(s->icon,"speaker")?"speaker":s->icon;
	glyph(cr,g,X+20,y+6,.9,s->def?0xa8f1f6:0xb2cad5,1);
	gboolean err=row_error_name && !g_strcmp0(row_error_name,s->name);
	double right=err?MIN(200,cw*.55):is_pending?80:MIN(130,cw*.35);
	text(cr,s->desc,X+46,y+16,cw-GUT-46-right-8,13,s->def?PANGO_WEIGHT_SEMIBOLD:PANGO_WEIGHT_NORMAL,s->def?0xe6f1f6:0xc9dde6,1,0,FALSE);
	if(err) text(cr,row_error,cx+cw-4,y+16,right,10,PANGO_WEIGHT_NORMAL,0xf2c14e,1,1,FALSE);
	else if(is_pending) text(cr,"Switching…",cx+cw-4,y+16,0,10,PANGO_WEIGHT_SEMIBOLD,0xa8f1f6,1,1,FALSE);
	else text(cr,s->kind,cx+cw-4,y+16,right,10,PANGO_WEIGHT_NORMAL,0x9fb3c0,1,1,FALSE);
}
static void draw_level(cairo_t *cr,int set,double y) {
	double cx=CX(),cw=CW(),X=cx+GUT;
	int level_kind=set?IT_IN_LEVEL:IT_OUT_LEVEL,mute_kind=set?IT_IN_MUTE:IT_OUT_MUTE;
	item_highlight(cr,cx-6,y-4,cw+12,34,glow_of(level_kind,0));
	struct snd *d=snd_default(set?sources:sinks);
	gboolean muted=d && d->muted;
	const char *g=set?(muted?"mic-muted":"mic"):(muted?"speaker-muted":"speaker");
	button(cr,X,y,30,26,NULL,g,muted,FALSE,hover_key==key_of(mute_kind,0),!d);
	double v=shown_level[set]>=0?shown_level[set]:d?d->percent:0;
	slider(cr,X+44,y+11,cw-GUT-104,CLAMP(v/100.,0,1),muted || !d);
	g_autofree char *pct=!d?g_strdup("—"):muted?g_strdup("Muted"):g_strdup_printf("%d%%",(int)lround(v));
	text(cr,pct,cx+cw-4,y+13,0,12,PANGO_WEIGHT_SEMIBOLD,muted||!d?0x9fb3c0:0xe6f1f6,1,1,FALSE);
}
static void draw_sound(cairo_t *cr) {
	double cx=CX(),cw=CW();
	if(sound_error || !sound_known) {
		glyph(cr,"speaker",cx,HEADER+22,.9,0x9fb3c0,1);
		text(cr,sound_error?"Sound unavailable":"Loading sound devices…",cx+28,HEADER+32,cw-28,13,PANGO_WEIGHT_SEMIBOLD,0xe6f1f6,1,0,FALSE);
		if(sound_error) text(cr,sound_error,cx+28,HEADER+50,cw-28,10,PANGO_WEIGHT_NORMAL,0x9fb3c0,1,0,FALSE);
		return;
	}
	struct sound_rows r=sound_rows();
	for(int set=0;set<2;set++) {
		GArray *a=set?sources:sinks;
		int n=set?r.nr:r.ns;
		double label=set?r.in_label:r.out_label,rows=set?r.in_rows:r.out_rows,mark=label;
		ship(cr,cx+10,mark,.14);
		text(cr,set?"INPUT":"OUTPUT",cx+GUT,label,0,10,PANGO_WEIGHT_SEMIBOLD,0x9fb3c0,1,0,FALSE);
		struct snd *d=snd_default(a);
		int di=d?snd_index(a,d->name):-1;
		gboolean waiting=pending[set]!=NULL;
		if(di>=0 && di<n) {
			double head=head_level==set?head_t:-1;
			beam(cr,mark+9,rows+di*ROW+16,0,waiting||d->muted?.3:1,head);
		}
		int pi=waiting?snd_index(a,pending[set]):-1;
		if(pi>=0 && pi<n) beam(cr,mark+9,rows+pi*ROW+16,1,1,-1);
		if(!n) text(cr,set?"No microphones":"No outputs",cx+GUT,rows+16,0,12,PANGO_WEIGHT_NORMAL,0x9fb3c0,1,0,FALSE);
		for(int i=0;i<n;i++) draw_device_row(cr,&g_array_index(a,struct snd,i),set?IT_SOURCE:IT_SINK,i,rows+i*ROW,set);
		draw_level(cr,set,set?r.in_level:r.out_level);
	}
}
static void draw_message(cairo_t *cr,const char *title,const char *body) {
	double cx=CX(),cw=CW();
	glyph(cr,"bt",cx,HEADER+22,.9,0x9fb3c0,1);
	text(cr,title,cx+28,HEADER+32,cw-28,13,PANGO_WEIGHT_SEMIBOLD,0xe6f1f6,1,0,FALSE);
	if(body) text(cr,body,cx+28,HEADER+50,cw-28,10,PANGO_WEIGHT_NORMAL,0x9fb3c0,1,0,FALSE);
}
static void draw_radar(cairo_t *cr,double ny,GPtrArray *near,int unnamed) {
	double ax,ay,R;radar_geometry(ny,&ax,&ay,&R);
	double a0=-165*G_PI/180,a1=-15*G_PI/180;
	cairo_move_to(cr,ax,ay);cairo_arc(cr,ax,ay,R,a0,a1);cairo_close_path(cr);
	rgba(cr,0x285a6e,.12);cairo_fill_preserve(cr);rgba(cr,0x3b6170,1);cairo_set_line_width(cr,1);cairo_stroke(cr);
	for(int i=1;i<3;i++) {cairo_new_sub_path(cr);cairo_arc(cr,ax,ay,R*(i==1?.36:.68),a0,a1);}
	rgba(cr,0x2f4f5e,1);cairo_stroke(cr);
	for(int d=-130;d<=-50;d+=40) {
		cairo_move_to(cr,ax,ay);cairo_line_to(cr,ax+R*cos(d*G_PI/180),ay+R*sin(d*G_PI/180));
	}
	rgba(cr,0x22394a,1);cairo_stroke(cr);
	text(cr,"Near",ax+R*.36*cos(a0)-4,ay+R*.36*sin(a0)+10,0,9,PANGO_WEIGHT_NORMAL,0x5d7180,1,1,FALSE);
	text(cr,"Far",ax+R*cos(a0)-4,ay+R*sin(a0)+10,0,9,PANGO_WEIGHT_NORMAL,0x5d7180,1,1,FALSE);
	if(bt_scanning()) {
		// The sweep turns only while discovery runs.
		double s=motion?sweep:.55,a=a0+(a1-a0)*s;
		for(int k=0;k<3;k++) {
			double from=a-(k+1)*8*G_PI/180,to=a-k*8*G_PI/180;
			if(to<a0) continue;
			cairo_move_to(cr,ax,ay);cairo_arc(cr,ax,ay,R,MAX(a0,from),to);cairo_close_path(cr);
			rgba(cr,0x56d4dd,(double[]){.15,.08,.04}[k]);cairo_fill(cr);
		}
		cairo_move_to(cr,ax,ay);cairo_line_to(cr,ax+R*cos(a),ay+R*sin(a));
		rgba(cr,0xa8f1f6,.85);cairo_set_line_width(cr,1.3);cairo_stroke(cr);
	}
	for(int i=0;i<unnamed && i<8;i++) {
		double deg=-150+i*17,r=R*(.82+(i%3)*.06);
		cairo_arc(cr,ax+r*cos(deg*G_PI/180),ay+r*sin(deg*G_PI/180),2,0,2*G_PI);
		rgba(cr,0x9fb3c0,.45);cairo_fill(cr);
	}
	for(guint i=0;i<near->len && i<12;i++) {
		struct bt_device *d=g_ptr_array_index(near,i);
		double x,y;radar_point(i,d,ax,ay,R,&x,&y);
		item_highlight(cr,x-10,y-14,96,46,glow_of(IT_NEAR,i));
		cairo_arc(cr,x,y,6,0,2*G_PI);rgba(cr,0x56d4dd,.5);cairo_set_line_width(cr,1);cairo_stroke(cr);
		cairo_arc(cr,x,y,3.2,0,2*G_PI);rgba(cr,0xa8f1f6,1);cairo_fill(cr);
		text(cr,d->name,x+12,y-2,80,11,PANGO_WEIGHT_SEMIBOLD,0xe6f1f6,1,0,FALSE);
		if(d->error) text(cr,d->error,x+12,y+11,180,9,PANGO_WEIGHT_NORMAL,0xf2c14e,1,0,FALSE);
		else if(d->busy) text(cr,d->status,x+12,y+11,80,9,PANGO_WEIGHT_NORMAL,0xa8f1f6,1,0,FALSE);
		else if(glow_of(IT_NEAR,i)>.5) {
			double w;keycap(cr,x+10,y+8,"Enter",&w);
			text(cr,"Pair",x+14+w,y+18,0,10,PANGO_WEIGHT_SEMIBOLD,0xa8f1f6,1,0,FALSE);
		}
	}
	ship(cr,ax,ay-8,.16);
	if(unnamed) {
		g_autofree char *count=g_strdup_printf("%d unnamed",unnamed);
		text(cr,count,CX()+CW(),ay-4,0,10,PANGO_WEIGHT_NORMAL,0x5d7180,1,1,FALSE);
	}
}
static void draw_card(cairo_t *cr,double py) {
	const struct bt_pairing *p=bt_pairing();
	double cx=CX(),cw=CW(),a=motion?card_alpha:1;
	cairo_push_group(cr);
	chamfer(cr,cx-6,py,cw+12,108,6);
	rgba(cr,0x0a1820,.85);cairo_fill_preserve(cr);rgba(cr,0x56d4dd,.7);cairo_set_line_width(cr,1);cairo_stroke(cr);
	glyph(cr,"speaker-box",cx+4,py+12,.9,0xa8f1f6,1);
	g_autofree char *head=g_strdup_printf("Pair with %s?",p->name?p->name:"device");
	text(cr,head,cx+30,py+22,cw-80,13,PANGO_WEIGHT_SEMIBOLD,0xe6f1f6,1,0,FALSE);
	const char *line=p->prompt==BT_PROMPT_CONFIRM?"Confirm the code shows on the device.":
		p->prompt==BT_PROMPT_DISPLAY?"Type this code on the device.":
		p->prompt==BT_PROMPT_PIN?"Type the device's PIN, then Enter.":
		p->prompt==BT_PROMPT_PASSKEY?"Type the passkey shown on the device, then Enter.":"Waiting for the device…";
	text(cr,line,cx+30,py+40,cw-40,11,PANGO_WEIGHT_NORMAL,0x9fb3c0,1,0,FALSE);
	const char *code=p->prompt==BT_PROMPT_PIN || p->prompt==BT_PROMPT_PASSKEY?entry:p->code;
	if(code && *code) {
		char spaced[24]={0};
		size_t n=strlen(code),k=0;
		for(size_t i=0;i<n && k<sizeof spaced-2;i++) {if(n==6 && i==3) spaced[k++]=' ';spaced[k++]=code[i];}
		text(cr,spaced,cx+30,py+80,0,24,PANGO_WEIGHT_SEMIBOLD,0xa8f1f6,1,0,FALSE);
	} else if(p->prompt==BT_PROMPT_PIN || p->prompt==BT_PROMPT_PASSKEY) {
		cairo_rectangle(cr,cx+30,py+90,2,-20);rgba(cr,0xa8f1f6,1);cairo_fill(cr);
	}
	if(p->deadline) {
		gint64 left=(p->deadline-g_get_monotonic_time())/G_USEC_PER_SEC;
		g_autofree char *s=g_strdup_printf("%d s",(int)MAX(0,left));
		text(cr,s,cx+cw-6,py+18,0,10,PANGO_WEIGHT_NORMAL,0x5d7180,1,1,FALSE);
	}
	struct item *c=find_item(IT_CONFIRM,0),*r=find_item(IT_REJECT,0);
	if(r) button(cr,r->x,r->y,r->w,r->h,c?"Reject":"Cancel",NULL,FALSE,focus_kind==IT_REJECT,hover_key==key_of(IT_REJECT,0),FALSE);
	if(c) button(cr,c->x,c->y,c->w,c->h,"Confirm",NULL,FALSE,focus_kind==IT_CONFIRM,hover_key==key_of(IT_CONFIRM,0),FALSE);
	cairo_pop_group_to_source(cr);
	cairo_paint_with_alpha(cr,a);
}
static void draw_bluetooth(cairo_t *cr) {
	const char *adapter=NULL;
	enum bt_state st=bt_state(&adapter);
	double cx=CX(),cw=CW();
	if(st==BT_LOADING) {draw_message(cr,"Looking for Bluetooth…",NULL);return;}
	if(st==BT_NO_ADAPTER) {draw_message(cr,"No Bluetooth adapter","bluetoothd is not running or no adapter is present.");return;}
	struct bt_rows r=bt_rows();
	item_highlight(cr,cx-6,r.adapter-2,cw+12,40,glow_of(IT_POWER,2));
	text(cr,"Adapter",cx,r.adapter+12,0,13,PANGO_WEIGHT_SEMIBOLD,0xe6f1f6,1,0,FALSE);
	text(cr,adapter?adapter:"Bluetooth",cx,r.adapter+28,cw-180,10,PANGO_WEIGHT_NORMAL,0x9fb3c0,1,0,FALSE);
	if(st==BT_BLOCKED) {
		text(cr,"Bluetooth is blocked by the system",cx,r.adapter+50,cw,12,PANGO_WEIGHT_SEMIBOLD,0xf2c14e,1,0,FALSE);
		text(cr,"Airplane mode or a hardware switch.",cx,r.adapter+66,cw,10,PANGO_WEIGHT_NORMAL,0x9fb3c0,1,0,FALSE);
		return;
	}
	text(cr,"Power",cx+cw-112,r.adapter+19,0,10,PANGO_WEIGHT_SEMIBOLD,0x9fb3c0,1,1,FALSE);
	gboolean busy=bt_powering();
	for(int i=0;i<2;i++) {
		struct item *b=find_item(IT_POWER,i);
		if(b) button(cr,b->x,b->y,b->w,b->h,i?"On":"Off",NULL,(st==BT_ON)==i,FALSE,hover_key==key_of(IT_POWER,i),busy);
	}
	if(bt_adapter_error()) text(cr,bt_adapter_error(),cx,r.adapter+50,cw,10,PANGO_WEIGHT_NORMAL,0xf2c14e,1,0,FALSE);
	if(st!=BT_ON) {
		text(cr,"Turn Bluetooth on to see paired devices.",cx,r.paired_label+4,cw,11,PANGO_WEIGHT_NORMAL,0x9fb3c0,1,0,FALSE);
		return;
	}
	text(cr,"PAIRED",cx,r.paired_label,0,10,PANGO_WEIGHT_SEMIBOLD,0x9fb3c0,1,0,FALSE);
	GPtrArray *p=bt_paired();
	if(!r.np) text(cr,"No paired devices yet.",cx,r.paired+16,0,11,PANGO_WEIGHT_NORMAL,0x9fb3c0,1,0,FALSE);
	for(int i=0;i<r.np;i++) {
		struct bt_device *d=g_ptr_array_index(p,i);
		double y=r.paired+i*BT_ROW;
		item_highlight(cr,cx-6,y-3,cw+12,40,glow_of(IT_PAIRED,i));
		glyph(cr,bt_glyph(d->icon),cx+2,y+7,.9,d->connected?0xa8f1f6:0xb2cad5,1);
		text(cr,d->name,cx+30,y+11,cw-150,13,PANGO_WEIGHT_SEMIBOLD,d->connected?0xe6f1f6:0xc9dde6,1,0,FALSE);
		const char *status=forget_armed && !g_strcmp0(forget_armed,d->path)?"Press Backspace again to forget":d->error?d->error:d->status;
		unsigned ink=d->error || (forget_armed && !g_strcmp0(forget_armed,d->path))?0xf2c14e:d->connected?0x8fd9df:0x9fb3c0;
		double sw=text(cr,status,cx+30,y+26,cw-150,10,PANGO_WEIGHT_NORMAL,ink,1,0,FALSE);
		if(d->connected && d->battery>=0 && !d->error && !d->busy) {
			double bx=cx+30+sw+8;
			cairo_rectangle(cr,bx+.5,y+22.5,18,8);rgba(cr,0x5d7a88,1);cairo_set_line_width(cr,1);cairo_stroke(cr);
			cairo_rectangle(cr,bx+18.5,y+24.5,2,4);cairo_fill(cr);
			cairo_rectangle(cr,bx+2,y+24,15*d->battery/100.,5);rgba(cr,d->battery<20?0xf2c14e:0x6cc3cc,1);cairo_fill(cr);
		}
		struct item *b=find_item(IT_PAIRED_ACT,i);
		const char *act=d->busy?"Cancel":d->connected?"Disconnect":"Connect";
		double bw=strlen(act)*7.2+22;
		if(b) {b->x=cx+cw-bw;b->w=bw;button(cr,b->x,b->y,bw,b->h,act,NULL,FALSE,glow_of(IT_PAIRED,i)>.5,hover_key==key_of(IT_PAIRED_ACT,i),FALSE);}
	}
	text(cr,"NEARBY",cx,r.near_label,0,10,PANGO_WEIGHT_SEMIBOLD,0x9fb3c0,1,0,FALSE);
	struct item *scan=find_item(IT_SCAN,0);
	gboolean scanning=bt_scanning();
	if(scanning) {
		double sx=cx+76,sy=r.near_label;
		double spin=motion?sweep*2*G_PI:0;
		double dash[]={6,4};
		cairo_set_dash(cr,dash,2,spin*4);
		cairo_arc(cr,sx,sy,4,0,2*G_PI);rgba(cr,0x56d4dd,1);cairo_set_line_width(cr,1.4);cairo_stroke(cr);
		cairo_set_dash(cr,NULL,0,0);
		g_autofree char *left=g_strdup_printf("Scanning · %d s",bt_scan_seconds_left());
		text(cr,left,sx+10,sy,0,10,PANGO_WEIGHT_NORMAL,0x9fb3c0,1,0,FALSE);
	}
	if(scan) button(cr,scan->x,scan->y,scan->w,scan->h,scanning?"Stop":"Scan",NULL,FALSE,glow_of(IT_SCAN,0)>.5,hover_key==key_of(IT_SCAN,0),FALSE);
	if(card_shown()) {draw_card(cr,r.near+4);return;}
	int unnamed=0;
	GPtrArray *near=bt_nearby(&unnamed);
	if(!scanning && !near->len) {
		text(cr,"Scan to find nearby devices.",cx,r.near+16,0,11,PANGO_WEIGHT_NORMAL,0x9fb3c0,1,0,FALSE);
		return;
	}
	draw_radar(cr,r.near,near,unnamed);
}
static void draw_board(GtkDrawingArea *area,cairo_t *cr,int w,int h,gpointer data) {
	(void)area;(void)data;
	width=w;height=h;
	layout_items();
	focus_fix();
	draw_header(cr);
	if(content_fade<1) cairo_push_group(cr);
	if(module==SYSTEMS_SOUND) draw_sound(cr); else draw_bluetooth(cr);
	if(content_fade<1) {cairo_pop_group_to_source(cr);cairo_paint_with_alpha(cr,ease(content_fade));}
	draw_footer(cr);
}

/* ---------- animation ---------- */
static gboolean tick(GtkWidget *w,GdkFrameClock *clock,gpointer data) {
	(void)w;(void)data;
	gint64 now=gdk_frame_clock_get_frame_time(clock);
	double dt=last_frame?MIN(50.,(now-last_frame)/1000.):16;
	last_frame=now;
	gboolean busy=FALSE;
	gboolean unfolding=unfold!=unfold_target;
	if(unfolding) {
		// Driven by elapsed time, so a slow first frame skips ahead
		// instead of stretching the whole projection.
		if(!unfold_start) unfold_start=now;
		double span=fabs(unfold_target-unfold_from)*(unfold_target>unfold_from?OPEN_MS:CLOSE_MS);
		double p=!motion || span<=0?1:(now-unfold_start)/1000./span;
		unfold=p>=1?unfold_target:unfold_from+(unfold_target-unfold_from)*p;
		gtk_widget_queue_draw(pane);
		if(unfold==0 && !open) gtk_widget_set_visible(pane,FALSE);
		busy|=unfold!=unfold_target;
		// Content animations wait: the board's pixels stay cached while
		// only the projection clip changes.
		if(unfold!=unfold_target) return G_SOURCE_CONTINUE;
	}
	if(!open) {tick_id=0;last_frame=0;return G_SOURCE_REMOVE;}
	gboolean draw=FALSE;
	if(content_fade<1) {content_fade=motion?MIN(1,content_fade+dt/FADE_IN_MS):1;draw=TRUE;busy|=content_fade<1;}
	if(head_t<1) {head_t=motion?MIN(1,head_t+dt/HEAD_MS):1;draw=TRUE;busy|=head_t<1;}
	if(card_shown() && card_alpha<1) {card_alpha=motion?MIN(1,card_alpha+dt/160.):1;draw=TRUE;busy|=card_alpha<1;}
	if(!card_shown()) card_alpha=0;
	for(int s=0;s<2;s++) {
		struct snd *d=sinks && sources?snd_default(s?sources:sinks):NULL;
		double target=local_level[s]>=0?local_level[s]:d?d->percent:-1;
		if(target<0) {shown_level[s]=-1;continue;}
		if(shown_level[s]<0 || !motion) shown_level[s]=target;
		else if(fabs(shown_level[s]-target)>.3) {shown_level[s]+=(target-shown_level[s])*MIN(1,dt/70);draw=TRUE;busy=TRUE;}
		else if(shown_level[s]!=target) {shown_level[s]=target;draw=TRUE;}
	}
	if(open && module==SYSTEMS_BLUETOOTH && dot_radius) {
		GPtrArray *near=bt_nearby(NULL);
		for(guint i=0;i<near->len;i++) {
			struct bt_device *d=g_ptr_array_index(near,i);
			double target=d->has_rssi?CLAMP((-40.-d->rssi)/55.,0,1)*.72+.25:1;
			double *r=g_hash_table_lookup(dot_radius,d->path);
			if(!r) {r=g_new(double,1);*r=target;g_hash_table_insert(dot_radius,g_strdup(d->path),r);}
			if(fabs(*r-target)>.002 && motion) {*r+=(target-*r)*MIN(1,dt/300);draw=TRUE;busy=TRUE;}
			else *r=target;
		}
	}
	if(open && module==SYSTEMS_BLUETOOTH && (bt_scanning() || card_shown())) {
		// Sweep left to right every 2 s while scanning; also tick the
		// countdowns once per frame. Nothing runs after the scan ends.
		if(motion) sweep=fmod(sweep+dt/2000,1);
		draw=TRUE;busy=TRUE;
	}
	if(glows_step(dt)) {draw=TRUE;busy=TRUE;}
	if(draw) gtk_widget_queue_draw(canvas);
	if(!busy) {tick_id=0;last_frame=0;return G_SOURCE_REMOVE;}
	return G_SOURCE_CONTINUE;
}
static void ensure_tick(void) {
	if(!tick_id && pane && gtk_widget_get_visible(pane)) {
		last_frame=0;
		tick_id=gtk_widget_add_tick_callback(pane,tick,NULL,NULL);
	}
}

/* ---------- sound actions ---------- */
static void row_error_set(const char *name,const char *message) {
	g_free(row_error_name);g_free(row_error);
	row_error_name=g_strdup(name);row_error=g_strdup(message);
}
static gboolean pending_expired(gpointer data) {
	int set=GPOINTER_TO_INT(data);
	pending_timer[set]=0;
	if(pending[set]) {
		row_error_set(pending[set],set?"Could not switch input":"Could not switch output");
		g_clear_pointer(&pending[set],g_free);
		redraw();
	}
	return G_SOURCE_REMOVE;
}
static void choose_device(int set,int i) {
	GArray *a=set?sources:sinks;
	if(i<0 || i>=(int)a->len) return;
	struct snd *s=&g_array_index(a,struct snd,i);
	g_clear_pointer(&row_error_name,g_free);
	if(s->def) return;
	g_free(pending[set]);pending[set]=g_strdup(s->name);
	g_clear_handle_id(&pending_timer[set],g_source_remove);
	pending_timer[set]=g_timeout_add_seconds(SWITCH_TIMEOUT_S,pending_expired,GINT_TO_POINTER(set));
	panelSystemsSound(set?OP_SOURCE:OP_SINK,s->name,0);
	redraw();
}
static void toggle_mute(int set) {
	struct snd *d=snd_default(set?sources:sinks);
	if(!d) return;
	panelSystemsSound(set?OP_SOURCE_MUTE:OP_SINK_MUTE,d->name,!d->muted);
}
static gboolean commit_level(gpointer data) {
	int set=GPOINTER_TO_INT(data);
	level_commit[set]=0;
	struct snd *d=snd_default(set?sources:sinks);
	if(d && local_level[set]>=0) panelSystemsSound(set?OP_SOURCE_VOLUME:OP_SINK_VOLUME,d->name,local_level[set]);
	return G_SOURCE_REMOVE;
}
static void set_level(int set,int value) {
	struct snd *d=snd_default(set?sources:sinks);
	if(!d) return;
	local_level[set]=CLAMP(value,0,100);
	level_hold[set]=g_get_monotonic_time()+600*1000;
	if(!level_commit[set]) level_commit[set]=g_timeout_add(120,commit_level,GINT_TO_POINTER(set));
	redraw();
}

/* ---------- sound post ---------- */
static void dock_sync(void) {
	struct snd *o=snd_default(sinks),*i=snd_default(sources);
	home_dock_systems_mute(o && o->muted,i && i->muted);
}
static gboolean apply_sound(gpointer data) {
	const char *text_in=data;
	// The watcher can report before the board is built; keep the state.
	if(!sinks) {
		sinks=g_array_new(FALSE,TRUE,sizeof(struct snd));
		sources=g_array_new(FALSE,TRUE,sizeof(struct snd));
	}
	g_auto(GStrv) lines=g_strsplit(text_in,"\n",-1);
	if(lines[0] && g_str_has_prefix(lines[0],"fail\t")) {
		g_auto(GStrv) f=g_strsplit(lines[0],"\t",4);
		if(g_strv_length(f)==4) {
			int op=atoi(f[1]),set=op>=OP_SOURCE;
			row_error_set(f[2],f[3]);
			if((op==OP_SINK || op==OP_SOURCE) && !g_strcmp0(pending[set],f[2])) {
				g_clear_pointer(&pending[set],g_free);
				g_clear_handle_id(&pending_timer[set],g_source_remove);
			}
			redraw();
		}
		return G_SOURCE_REMOVE;
	}
	char *old_default[2]={NULL,NULL};
	for(int s=0;s<2;s++) {struct snd *d=snd_default(s?sources:sinks);old_default[s]=d?g_strdup(d->name):NULL;}
	snd_clear(sinks);snd_clear(sources);
	g_clear_pointer(&sound_error,g_free);
	for(int i=0;lines[i];i++) {
		g_auto(GStrv) f=g_strsplit(lines[i],"\t",-1);
		guint n=g_strv_length(f);
		if(n==2 && !strcmp(f[0],"error")) sound_error=g_strdup(f[1]);
		else if(n==8 && (!strcmp(f[0],"sink") || !strcmp(f[0],"source"))) {
			struct snd s={g_strdup(f[1]),g_strdup(f[2]),g_strdup(f[3]),g_strdup(f[4]),atoi(f[5]),atoi(f[6])!=0,atoi(f[7])!=0};
			if(!strcmp(f[0],"sink")) g_array_append_val(sinks,s);
			else g_array_append_val(sources,s);
		}
	}
	sound_known=TRUE;
	gint64 now=g_get_monotonic_time();
	for(int s=0;s<2;s++) {
		struct snd *d=snd_default(s?sources:sinks);
		if(pending[s] && d && !g_strcmp0(d->name,pending[s])) {
			// Confirmed by the server: solid beam and one head run.
			g_clear_pointer(&pending[s],g_free);
			g_clear_handle_id(&pending_timer[s],g_source_remove);
			head_level=s;head_t=motion?0:1;
		} else if(!pending[s] && d && old_default[s] && g_strcmp0(old_default[s],d->name)) {
			head_level=s;head_t=motion?0:1;
		}
		if(drag_level!=s && !level_commit[s] && now>level_hold[s]) local_level[s]=-1;
		g_free(old_default[s]);
	}
	dock_sync();
	resize();
	redraw();
	return G_SOURCE_REMOVE;
}
void systems_board_sound_post(const char *t) {
	g_idle_add_full(G_PRIORITY_DEFAULT,apply_sound,g_strdup(t?t:""),g_free);
}

/* ---------- bluetooth ---------- */
static void bt_changed(void) {
	if(scan_on_ready) {
		enum bt_state st=bt_state(NULL);
		if(st==BT_ON) {scan_on_ready=FALSE;bt_scan(TRUE);}
		else if(st!=BT_LOADING) scan_on_ready=FALSE;
	}
	if(pane && open) {resize();layout_items();redraw();}
}
static void forget_reset(void) {g_clear_pointer(&forget_armed,g_free);g_clear_handle_id(&forget_timer,g_source_remove);}
static gboolean forget_expired(gpointer unused) {(void)unused;forget_timer=0;g_clear_pointer(&forget_armed,g_free);redraw();return G_SOURCE_REMOVE;}
static struct bt_device *paired_at(int i) {
	GPtrArray *p=bt_paired();
	return i>=0 && i<(int)p->len?g_ptr_array_index(p,i):NULL;
}
static void paired_action(int i) {
	struct bt_device *d=paired_at(i);
	if(!d) return;
	bt_clear_error(d->path);
	if(d->busy) bt_cancel(d->path);
	else bt_connect(d->path,!d->connected);
}

/* ---------- activation ---------- */
static void set_module(int m) {
	if(m==module) return;
	module=m;
	focus_kind=0;
	forget_reset();
	content_fade=motion?0:1;
	if(module==SYSTEMS_BLUETOOTH) bt_start(bt_changed);
	layout_items();
	focus_fix();
	redraw();
	gtk_accessible_update_property(GTK_ACCESSIBLE(canvas),GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,
		module==SYSTEMS_SOUND?"Sound devices":"Bluetooth devices",-1);
}
static void activate(int kind,int index) {
	switch(kind) {
	case IT_CLOSE: systems_board_close();break;
	case IT_MODULE: set_module(index);break;
	case IT_SINK: choose_device(0,index);break;
	case IT_SOURCE: choose_device(1,index);break;
	case IT_OUT_MUTE: case IT_OUT_LEVEL: toggle_mute(0);break;
	case IT_IN_MUTE: case IT_IN_LEVEL: toggle_mute(1);break;
	case IT_POWER: {
		enum bt_state st=bt_state(NULL);
		bt_set_power(index==2?st!=BT_ON:index==1);
		break;
	}
	case IT_PAIRED: case IT_PAIRED_ACT: paired_action(index);break;
	case IT_SCAN: bt_scan(!bt_scanning());break;
	case IT_NEAR: {
		GPtrArray *near=bt_nearby(NULL);
		if(index<(int)near->len) {
			struct bt_device *d=g_ptr_array_index(near,index);
			bt_clear_error(d->path);
			memset(entry,0,sizeof entry);
			card_alpha=0;
			bt_pair(d->path);
		}
		break;
	}
	case IT_CONFIRM: bt_pair_answer(TRUE,entry);memset(entry,0,sizeof entry);break;
	case IT_REJECT: bt_pair_answer(FALSE,NULL);memset(entry,0,sizeof entry);break;
	default: break;
	}
	redraw();
}
static struct item *hit(double x,double y) {
	// Buttons inside a row win over the row.
	struct item *row=NULL;
	for(int i=0;i<n_items;i++) {
		struct item *it=&items[i];
		if(x<it->x || y<it->y || x>it->x+it->w || y>it->y+it->h) continue;
		if(!it->focusable || it->kind==IT_SCAN || it->kind==IT_CONFIRM || it->kind==IT_REJECT) return it;
		if(!row) row=it;
	}
	return row;
}
static double level_value(double x) {
	double x0=CX()+GUT+44,w=CW()-GUT-104;
	return CLAMP((x-x0)/w,0,1)*100;
}
static gboolean on_track(double x) {return x>=CX()+GUT+38;}

static void pressed(GtkGestureClick *g,int n,double x,double y,gpointer data) {
	(void)n;(void)data;
	if(!open) return;
	gtk_widget_grab_focus(canvas);
	layout_items();
	struct item *it=hit(x,y);
	if(!it) return;
	if((it->kind==IT_OUT_LEVEL || it->kind==IT_IN_LEVEL) && on_track(x)) {
		gtk_gesture_set_state(GTK_GESTURE(g),GTK_EVENT_SEQUENCE_CLAIMED);
		return;
	}
	if(it->focusable) {focus_kind=it->kind;focus_index=it->index;}
	// Rows only take focus on a click; their buttons act.
	if(it->kind==IT_OUT_LEVEL || it->kind==IT_IN_LEVEL || it->kind==IT_PAIRED || (it->kind==IT_POWER && it->index==2)) {
		forget_reset();
		redraw();
		return;
	}
	activate(it->kind,it->index);
}
static double drag_x0;
static void drag_begin(GtkGestureDrag *g,double x,double y,gpointer data) {
	(void)data;
	layout_items();
	struct item *it=hit(x,y);
	if(!open || !it || (it->kind!=IT_OUT_LEVEL && it->kind!=IT_IN_LEVEL) || !on_track(x)) {
		gtk_gesture_set_state(GTK_GESTURE(g),GTK_EVENT_SEQUENCE_DENIED);
		return;
	}
	drag_level=it->kind==IT_IN_LEVEL;
	focus_kind=it->kind;focus_index=0;
	drag_x0=x;
	set_level(drag_level,(int)lround(level_value(x)));
}
static void drag_update(GtkGestureDrag *g,double dx,double dy,gpointer data) {
	(void)g;(void)dy;(void)data;
	if(drag_level>=0) set_level(drag_level,(int)lround(level_value(drag_x0+dx)));
}
static void drag_end(GtkGestureDrag *g,double dx,double dy,gpointer data) {
	(void)g;(void)dx;(void)dy;(void)data;
	drag_level=-1;
}
static void motion_at(GtkEventControllerMotion *m,double x,double y,gpointer data) {
	(void)m;(void)data;
	struct item *it=hit(x,y);
	int key=it?key_of(it->kind,it->index):-1;
	if(key!=hover_key) {hover_key=key;redraw();}
}
static void motion_leave(GtkEventControllerMotion *m,gpointer data) {
	(void)m;(void)data;
	if(hover_key!=-1) {hover_key=-1;redraw();}
}
static void move_focus(int step) {
	int n=focus_count();
	if(!n) return;
	int p=focus_position();
	p=p<0?0:CLAMP(p+step,0,n-1);
	focus_nth(p);
	forget_reset();
	redraw();
}
static gboolean key(GtkEventControllerKey *k,guint keyval,guint code,GdkModifierType mods,gpointer data) {
	(void)k;(void)code;(void)data;
	if(!open) return FALSE;
	if(mods&(GDK_CONTROL_MASK|GDK_ALT_MASK|GDK_SUPER_MASK|GDK_META_MASK)) return FALSE;
	layout_items();
	focus_fix();
	const struct bt_pairing *p=bt_pairing();
	gboolean typing=module==SYSTEMS_BLUETOOTH && p->path && (p->prompt==BT_PROMPT_PIN || p->prompt==BT_PROMPT_PASSKEY);
	if(typing && keyval<128 && g_ascii_isdigit((char)keyval)) {
		size_t n=strlen(entry);
		if(n<6) {entry[n]=(char)keyval;entry[n+1]=0;}
		redraw();
		return TRUE;
	}
	if(typing && keyval==GDK_KEY_BackSpace) {size_t n=strlen(entry);if(n) entry[n-1]=0;redraw();return TRUE;}
	guint lower=gdk_keyval_to_lower(keyval);
	switch(lower) {
	case GDK_KEY_Escape: systems_board_close();return TRUE;
	case GDK_KEY_Tab: case GDK_KEY_ISO_Left_Tab: set_module(!module);return TRUE;
	case GDK_KEY_w: case GDK_KEY_Up: move_focus(-1);return TRUE;
	case GDK_KEY_s: case GDK_KEY_Down: move_focus(1);return TRUE;
	case GDK_KEY_a: case GDK_KEY_Left: case GDK_KEY_d: case GDK_KEY_Right: {
		int dir=lower==GDK_KEY_a || lower==GDK_KEY_Left?-1:1;
		if(focus_kind==IT_OUT_LEVEL || focus_kind==IT_IN_LEVEL) {
			int set=focus_kind==IT_IN_LEVEL;
			struct snd *d=snd_default(set?sources:sinks);
			int base=local_level[set]>=0?local_level[set]:d?d->percent:0;
			set_level(set,(base/5+(dir>0?1:(base%5?0:-1)))*5);
		} else if(focus_kind==IT_POWER) bt_set_power(dir>0);
		return TRUE;
	}
	case GDK_KEY_Return: case GDK_KEY_KP_Enter:
		if(focus_kind) activate(focus_kind,focus_index);
		return TRUE;
	case GDK_KEY_space:
		if(module==SYSTEMS_SOUND) toggle_mute(focus_kind==IT_SOURCE || focus_kind==IT_IN_LEVEL);
		else if(focus_kind) activate(focus_kind,focus_index);
		return TRUE;
	case GDK_KEY_BackSpace:
		if(focus_kind==IT_PAIRED) {
			struct bt_device *d=paired_at(focus_index);
			if(!d) return TRUE;
			if(forget_armed && !strcmp(forget_armed,d->path)) {bt_forget(d->path);forget_reset();}
			else {
				forget_reset();
				forget_armed=g_strdup(d->path);
				forget_timer=g_timeout_add_seconds(4,forget_expired,NULL);
			}
			redraw();
		}
		return TRUE;
	default:
		// Own the keyboard while open so Home shortcuts do not fire behind it.
		return TRUE;
	}
}

/* ---------- public ---------- */
void systems_board_init(GtkOverlay *root,void (*region)(void)) {
	on_region=region;
	if(!sinks) {
		sinks=g_array_new(FALSE,TRUE,sizeof(struct snd));
		sources=g_array_new(FALSE,TRUE,sizeof(struct snd));
	}
	dot_radius=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
	pane=g_object_new(SYSTEMS_TYPE_FRAME,NULL);
	GtkWidget *stack=gtk_overlay_new();
	gtk_overlay_set_child(GTK_OVERLAY(stack),analog_glass_new());
	canvas=gtk_drawing_area_new();
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(canvas),draw_board,NULL,NULL);
	gtk_widget_set_focusable(canvas,TRUE);
	gtk_accessible_update_property(GTK_ACCESSIBLE(canvas),GTK_ACCESSIBLE_PROPERTY_LABEL,"Systems",
		GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,"Sound devices",-1);
	gtk_overlay_add_overlay(GTK_OVERLAY(stack),canvas);
	gtk_overlay_set_measure_overlay(GTK_OVERLAY(stack),canvas,TRUE);
	SYSTEMS_FRAME(pane)->child=stack;
	gtk_widget_set_parent(stack,pane);
	gtk_widget_set_halign(pane,GTK_ALIGN_START);
	gtk_widget_set_valign(pane,GTK_ALIGN_END);
	GtkGesture *click=gtk_gesture_click_new();
	g_signal_connect(click,"pressed",G_CALLBACK(pressed),NULL);
	gtk_widget_add_controller(canvas,GTK_EVENT_CONTROLLER(click));
	GtkGesture *drag=gtk_gesture_drag_new();
	g_signal_connect(drag,"drag-begin",G_CALLBACK(drag_begin),NULL);
	g_signal_connect(drag,"drag-update",G_CALLBACK(drag_update),NULL);
	g_signal_connect(drag,"drag-end",G_CALLBACK(drag_end),NULL);
	gtk_widget_add_controller(canvas,GTK_EVENT_CONTROLLER(drag));
	GtkEventController *hover=gtk_event_controller_motion_new();
	g_signal_connect(hover,"motion",G_CALLBACK(motion_at),NULL);
	g_signal_connect(hover,"leave",G_CALLBACK(motion_leave),NULL);
	gtk_widget_add_controller(canvas,hover);
	GtkEventController *keys=gtk_event_controller_key_new();
	gtk_event_controller_set_propagation_phase(keys,GTK_PHASE_CAPTURE);
	g_signal_connect(keys,"key-pressed",G_CALLBACK(key),NULL);
	gtk_widget_add_controller(pane,keys);
	gtk_widget_set_visible(pane,FALSE);
	gtk_overlay_add_overlay(root,pane);
}
GtkWidget *systems_board_widget(void) {return pane;}
gboolean systems_board_visible(void) {return pane && open;}

void systems_board_open(enum systems_module m) {
	if(!pane) return;
	motion_refresh();
	if(m!=SYSTEMS_LAST) module=m;
	gboolean was_open=open;
	open=TRUE;
	focus_kind=0;
	forget_reset();
	content_fade=1;
	gtk_widget_set_can_target(pane,TRUE);
	gtk_widget_set_size_request(canvas,width,needed_height());
	gtk_widget_set_visible(pane,TRUE);
	if(!was_open) {
		unfold_from=unfold;unfold_target=1;unfold_start=0;
		if(!motion) unfold=1;
		panelSystemsWatch(1);
	}
	if(module==SYSTEMS_BLUETOOTH) bt_start(bt_changed);
	layout_items();
	focus_fix();
	glows_snap();
	gtk_widget_grab_focus(canvas);
	redraw();
	gtk_widget_queue_draw(pane);
	if(on_region) on_region();
}
gboolean systems_board_close(void) {
	if(!pane || !open) return FALSE;
	open=FALSE;
	hover_key=-1;
	forget_reset();
	bt_stop();
	panelSystemsWatch(0);
	g_clear_pointer(&row_error_name,g_free);
	// Input is released at once; the fold-back plays without it.
	gtk_widget_set_can_target(pane,FALSE);
	unfold_from=unfold;unfold_target=0;unfold_start=0;
	if(!motion || unfold==0) {
		unfold=0;
		if(tick_id) gtk_widget_remove_tick_callback(pane,tick_id);
		tick_id=0;
		last_frame=0;
		gtk_widget_set_visible(pane,FALSE);
	}
	ensure_tick();
	if(on_region) on_region();
	return TRUE;
}
void systems_board_layout(int w,int h,int anchor,int bottom) {
	if(!pane) return;
	int board=MIN(BOARD_WIDTH,MAX(1,w-24));
	avail_height=MAX(240,h-bottom-24);
	int left=CLAMP(anchor-board/2,12,MAX(12,w-board-12));
	unfold_anchor_x=anchor-left;
	width=board;
	gtk_widget_set_size_request(canvas,board,needed_height());
	gtk_widget_set_margin_start(pane,left);
	gtk_widget_set_margin_bottom(pane,bottom);
}
void systems_board_scan(void) {
	systems_board_open(SYSTEMS_BLUETOOTH);
	if(bt_state(NULL)==BT_ON) bt_scan(TRUE);
	else scan_on_ready=TRUE;
	redraw();
}
void systems_board_toggle_mute(void) {toggle_mute(0);}
