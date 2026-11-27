//go:build linux && cgo && panel

#include "home-panel.h"
#include "map-fleet.h"
#include "shell-type.h"
#include <math.h>
#include <string.h>

struct point { double x,y; };
static GtkWidget *dock,*rail,*telemetry,*health_lamp,*node_readout,*buttons[HOME_ACTIONS];
static void (*on_action)(enum home_action);
static void (*on_connect)(const char *);
static void (*on_region)(void);
enum { HANGAR_COLUMNS=3, HANGAR_SLOTS=9 };
static char **hangar_nodes;
static int hangar_count,hangar_anchor;
static GtkWidget *hangar,*hangar_buttons[HANGAR_SLOTS];
static GtkWidget *hangar_ink[HANGAR_SLOTS],*hangar_labels[HANGAR_SLOTS],*hangar_pads[HANGAR_SLOTS];
static guint hangar_launch_timer;
static int hangar_launch_slot=-1;
static char *hangar_launch_name;
static gboolean hangar_open;
static int width,wing,tx,tw,center_width,bw,button_gap;
static gboolean tray_available,tray_open;
static int tray_center;
static gboolean map_on;
static int cpu=-1, cores[256], ncpu;
static double ram_used,ram_total,down,up;
static double rx_history[20],tx_history[20];
static int nnet;
static gboolean network_known;
enum health_level { HEALTH_UNKNOWN, HEALTH_NORMAL, HEALTH_WARNING, HEALTH_CRITICAL };
enum { HEALTH_MIN_WIDTH=156, HEALTH_SPAN=37, HEALTH_LAMP_SIZE=28 };
static enum health_level health;
static guint health_tick_id;
static gint64 health_started,health_last_frame;
static double health_phase;
static char clock_label[8]="--:--",date_label[8]="--/--";

static void ink(cairo_t *cr,unsigned rgb) {
	cairo_set_source_rgb(cr,((rgb>>16)&255)/255.,((rgb>>8)&255)/255.,(rgb&255)/255.);
}

// Polygon is clockwise in screen coordinates. Each offset vertex is the
// exact intersection of the two displaced edge lines, including shoulders.
static void contour(cairo_t *cr,const struct point *p,int n,double inset) {
	cairo_new_path(cr);
	for(int i=0;i<n;i++) {
		struct point a=p[(i+n-1)%n],b=p[i],c=p[(i+1)%n];
		double al=hypot(b.x-a.x,b.y-a.y),bl=hypot(c.x-b.x,c.y-b.y);
		struct point u={-(b.y-a.y)/al,(b.x-a.x)/al};
		struct point v={-(c.y-b.y)/bl,(c.x-b.x)/bl};
		double d=1+u.x*v.x+u.y*v.y;
		double x=b.x+inset*(u.x+v.x)/d,y=b.y+inset*(u.y+v.y)/d;
		if (!i) cairo_move_to(cr,x,y); else cairo_line_to(cr,x,y);
	}
	cairo_close_path(cr);
}

#define HOUSING_SEAM_INSET 3.6
#define ANALOG_BAY_FILL 0x0b161d

static void housing_strokes(cairo_t *cr,const struct point *p,int n,unsigned rim,unsigned seam) {
	contour(cr,p,n,.5);cairo_set_line_width(cr,1);ink(cr,rim);cairo_stroke(cr);
	contour(cr,p,n,HOUSING_SEAM_INSET);ink(cr,seam);cairo_stroke(cr);
}
static void housing(cairo_t *cr,const struct point *p,int n,unsigned fill,unsigned rim,unsigned seam) {
	contour(cr,p,n,.5);ink(cr,fill);cairo_fill(cr);
	housing_strokes(cr,p,n,rim,seam);
}

static void box(cairo_t *cr,double x,double y,double w,double h,int cut,unsigned fill,unsigned rim,unsigned seam) {
	if(w<=2*cut || h<=2*cut) return;
	struct point p[]={{x+cut,y},{x+w-cut,y},{x+w,y+cut},{x+w,y+h-cut},
		{x+w-cut,y+h},{x+cut,y+h},{x,y+h-cut},{x,y+cut}};
	housing(cr,p,G_N_ELEMENTS(p),fill,rim,seam);
}

static void paint_housing(cairo_t *cr,int w,int bay) {
	struct point p[]={{13,10},{bay-10,10},{bay+1,21},{w-bay-1,21},{w-bay+10,10},
		{w-13,10},{w,23},{w,49},{w-13,62},{13,62},{0,49},{0,23}};
	housing(cr,p,G_N_ELEMENTS(p),0x182832,0x5c7c8c,0x314d5a);
}
void home_node_dock_face(cairo_t *cr,int w,int tray_x,int tray_width) {
	if(tray_x<25 || tray_width<30) return;
	int left=tray_x,right=tray_x+tray_width;
	// The wings climb straight into the preview bay. One continuous
	// outline supplies both outside strokes; the bay is not a second box.
	struct point p[]={{13,10},{left-10,10},{left,0},{right,0},{right+10,10},
		{w-13,10},{w,23},{w,49},{w-13,62},{13,62},{0,49},{0,23}};
	cairo_save(cr);
	contour(cr,p,G_N_ELEMENTS(p),.5);cairo_clip(cr);
	ink(cr,0x182832);cairo_paint(cr);
	// This opening belongs to the analog display, not the metal outline.
	// Keep its paint inside the chamfers and the existing housing seam.
	const double floor=HOME_HEIGHT-HOUSING_SEAM_INSET;
	const double join=HOUSING_SEAM_INSET*(sqrt(2.)-1.);
	const double top=HOUSING_SEAM_INSET+1.;
	const double l=left-10+join,r=right+10-join;
	const struct point recess[]={{l+10,top},{r-10,top},{r,top+10},{r,floor-9},
		{r-9,floor},{l+9,floor},{l,floor-9},{l,top+10}};
	cairo_save(cr);
	contour(cr,recess,G_N_ELEMENTS(recess),0.);
	cairo_clip(cr);
	ink(cr,ANALOG_BAY_FILL);cairo_paint(cr);

	// Paint a soft inward falloff along the actual contour, including
	// its diagonal cuts. This is shadow, not another border stroke.
	const double strength[]={.22,.15,.12,.12,.13,.13,.16,.18};
	for(size_t i=0;i<G_N_ELEMENTS(recess);i++) {
		struct point a=recess[i],b=recess[(i+1)%G_N_ELEMENTS(recess)];
		double dx=b.x-a.x,dy=b.y-a.y,length=hypot(dx,dy);
		double fade=i==0?4.:3.5;
		cairo_pattern_t *shade=cairo_pattern_create_linear(a.x,a.y,
			a.x-dy/length*fade,a.y+dx/length*fade);
		cairo_pattern_add_color_stop_rgba(shade,0,0,0,0,strength[i]);
		cairo_pattern_add_color_stop_rgba(shade,.45,0,0,0,strength[i]*.34);
		cairo_pattern_add_color_stop_rgba(shade,1,0,0,0,0);
		cairo_set_source(cr,shade);cairo_paint(cr);cairo_pattern_destroy(shade);
	}

	// A faint floor reflection stops before reaching the corners.
	cairo_translate(cr,(l+r)*.5,floor+1.);
	cairo_scale(cr,(r-l)*.37,5.);
	cairo_pattern_t *reflection=cairo_pattern_create_radial(0,0,0,0,0,1);
	cairo_pattern_add_color_stop_rgba(reflection,0,.32,.49,.57,.08);
	cairo_pattern_add_color_stop_rgba(reflection,1,.32,.49,.57,0);
	cairo_set_source(cr,reflection);cairo_paint(cr);cairo_pattern_destroy(reflection);
	cairo_restore(cr);
	cairo_restore(cr);
	housing_strokes(cr,p,G_N_ELEMENTS(p),0x5c7c8c,0x314d5a);
}
static void paint_rail(GtkDrawingArea *a,cairo_t *cr,int w,int h,gpointer user) {
	(void)a;(void)h;(void)user;
	if(w<100 || wing<30) return;
	int bay=MAX(wing,tx-12);

	paint_housing(cr,w,bay);
}

static void text_at(cairo_t *cr,PangoLayout *l,double x,double y,const char *s,
		int size,PangoWeight weight,unsigned color) {
	PangoFontDescription *d=shell_font_description(NULL,size,weight);
	pango_layout_set_font_description(l,d);pango_font_description_free(d);
	pango_layout_set_text(l,s,-1);
	ink(cr,color);cairo_move_to(cr,x,y);pango_cairo_show_layout(cr,l);
}
static void text_aligned(cairo_t *cr,PangoLayout *l,double x,double y,const char *s,
		int size,PangoWeight weight,unsigned color,double anchor) {
	PangoFontDescription *d=shell_font_description(NULL,size,weight);
	pango_layout_set_font_description(l,d);pango_font_description_free(d);
	pango_layout_set_text(l,s,-1);
	int width=0;pango_layout_get_pixel_size(l,&width,NULL);
	ink(cr,color);cairo_move_to(cr,x-width*anchor,y);pango_cairo_show_layout(cr,l);
}
static void text_right(cairo_t *cr,PangoLayout *l,double x,double y,const char *s,
		int size,PangoWeight weight,unsigned color) {
	text_aligned(cr,l,x,y,s,size,weight,color,1);
}
static void text_center(cairo_t *cr,PangoLayout *l,double x,double y,const char *s,
		int size,PangoWeight weight,unsigned color) {
	text_aligned(cr,l,x,y,s,size,weight,color,.5);
}
static void fill(cairo_t *cr,double x,double y,double w,double h,unsigned rgb) {
	if(w<=0||h<=0) return;
	ink(cr,rgb);cairo_rectangle(cr,x,y,w,h);cairo_fill(cr);
}
static void path(cairo_t *cr,double x,double y,double x2,double y2) {
	cairo_move_to(cr,x,y);cairo_line_to(cr,x2,y2);
}
static void history(cairo_t *cr,const double *v,int n,double x,double y,double w,double h,double peak,unsigned color) {
	if(n<2) return;
	ink(cr,color);cairo_set_line_width(cr,1);
	for(int i=0;i<n;i++) {
		double px=x+i*w/(n-1),py=y+h*(1-v[i]/peak);
		if(i) cairo_line_to(cr,px,py);else cairo_move_to(cr,px,py);
	}
	cairo_stroke(cr);
}
static gboolean health_can_animate(GtkWidget *w) {
	gboolean allowed=TRUE;
	g_object_get(gtk_widget_get_settings(w),"gtk-enable-animations",&allowed,NULL);
	return gtk_widget_get_mapped(w) && health!=HEALTH_UNKNOWN && allowed;
}
static void paint_health(GtkDrawingArea *a,cairo_t *cr,int w,int h,gpointer user) {
	(void)a;(void)user;
	static const unsigned colors[]={0xa1afbd,0x76e0ae,0xf7be69,0xff343e};
	double x=w*.5,y=h*.5,r=13;
	unsigned rgb=colors[health];
	double red=((rgb>>16)&255)/255.,green=((rgb>>8)&255)/255.,blue=(rgb&255)/255.;
	cairo_arc(cr,x,y,r,0,2*G_PI);
	ink(cr,0x0b1822);cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr,red,green,blue,.82);cairo_set_line_width(cr,1);
	cairo_stroke(cr);
	// The state changes the tempo, not the outline or the apparent size.
	double u=(health_phase-.16)/.055,v=(health_phase-.33)/.066;
	double pulse=MAX(exp(-u*u),.68*exp(-v*v));
	double glow=r*(.80+.17*pulse);
	double strength=health==HEALTH_UNKNOWN?.55:.82+.15*pulse;
	cairo_pattern_t *p=cairo_pattern_create_radial(x,y,0,x,y,glow);
	cairo_pattern_add_color_stop_rgba(p,0,red,green,blue,strength);
	cairo_pattern_add_color_stop_rgba(p,.24,red,green,blue,strength*.79);
	cairo_pattern_add_color_stop_rgba(p,.55,red,green,blue,strength*.24);
	cairo_pattern_add_color_stop_rgba(p,1,red,green,blue,0);
	cairo_arc(cr,x,y,glow,0,2*G_PI);
	cairo_set_source(cr,p);cairo_fill(cr);cairo_pattern_destroy(p);
}
static gboolean health_tick(GtkWidget *w,GdkFrameClock *clock,gpointer data) {
	(void)data;
	if(!health_can_animate(w)) {
		health_tick_id=0;health_phase=0;
		gtk_widget_queue_draw(w);
		return G_SOURCE_REMOVE;
	}
	gint64 now=gdk_frame_clock_get_frame_time(clock);
	if(!health_started) health_started=now;
	if(now-health_last_frame>=33333) {
		static const gint64 period[]={1,2100000,1500000,1050000};
		health_last_frame=now;
		health_phase=(double)((now-health_started)%period[health])/period[health];
		gtk_widget_queue_draw(w);
	}
	return G_SOURCE_CONTINUE;
}
static void update_health_tick(GtkWidget *w,gpointer unused) {
	(void)unused;
	if(health_can_animate(w)) {
		if(!health_tick_id) {
			health_started=0;health_last_frame=0;
			health_tick_id=gtk_widget_add_tick_callback(w,health_tick,NULL,NULL);
		}
	} else {
		if(health_tick_id) gtk_widget_remove_tick_callback(w,health_tick_id);
		health_tick_id=0;health_phase=0;
		gtk_widget_queue_draw(w);
	}
}

static void paint_telemetry(GtkDrawingArea *a,cairo_t *cr,int w,int h,gpointer user) {
	(void)a;
	if(!user) box(cr,0,0,w,h,10,ANALOG_BAY_FILL,0x637f8c,0x324e5b);
	PangoLayout *l=pango_cairo_create_layout(cr);
	char buf[80];
	unsigned label=0xaac1ce,value=0xdae8ef,signal=0x79b6c2;
	if(w<106) {
		if(w>=53) text_at(cr,l,(w-34)/2.,10,clock_label,12,PANGO_WEIGHT_BOLD,value);
		if(w>=46) text_at(cr,l,(w-29)/2.,29,date_label,9,PANGO_WEIGHT_MEDIUM,label);
		g_object_unref(l);return;
	}
	int health_span=w>=HEALTH_MIN_WIDTH?HEALTH_SPAN:0;
	if(w<304) {
		if(cpu>=0) g_snprintf(buf,sizeof buf,"CPU %d%%",cpu); else g_strlcpy(buf,"CPU —",sizeof buf);
		text_at(cr,l,11+health_span,6,buf,9,PANGO_WEIGHT_SEMIBOLD,value);
		if(ram_total>0) g_snprintf(buf,sizeof buf,"RAM %.0f%%",100*ram_used/ram_total);else g_strlcpy(buf,"RAM —",sizeof buf);
		text_at(cr,l,11+health_span,20,buf,9,PANGO_WEIGHT_MEDIUM,label);
		if(w>=165) text_at(cr,l,11+health_span,34,network_known?"NET active":"NET —",9,PANGO_WEIGHT_MEDIUM,label);
		text_at(cr,l,w-43,9,clock_label,12,PANGO_WEIGHT_BOLD,value);
		text_at(cr,l,w-40,28,date_label,9,PANGO_WEIGHT_MEDIUM,label);
		g_object_unref(l);return;
	}
	// Every column keeps the same gutter on both sides of its divider and
	// fills its content width, so the readouts sit on one even rhythm.
	const double gutter=12,edge=14,clock=58,y=6;
	double lead=health_span?edge+health_span-9:0;
	if(health_span) fill(cr,lead,9,1,32,0x2b4150);
	double x=health_span?lead+1+gutter:edge,right=w-edge;
	double space=MAX(30.,right-x-clock-3*(2*gutter+1));
	double cw=floor(space*.29),rw=floor(space*.30),nw=space-cw-rw;
	text_at(cr,l,x,y,"CPU",9,PANGO_WEIGHT_SEMIBOLD,label);
	if(cpu>=0) g_snprintf(buf,sizeof buf,"%d%%",cpu);else g_strlcpy(buf,"—",sizeof buf);
	text_right(cr,l,x+cw,y,buf,9,PANGO_WEIGHT_MEDIUM,value);
	double pitch=ncpu?(cw+2)/ncpu:0;
	if(pitch>=3.) for(int i=0;i<ncpu;i++) {
		double bx=x+i*pitch,bw=pitch-2,by=23;
		fill(cr,bx,by,bw,16,0x29414c);
		if(cores[i]>=0) {
			double amount=16*cores[i]/100.;
			fill(cr,bx,by+16-amount,bw,amount,signal);
		}
	}
	x+=cw+gutter;fill(cr,x,9,1,32,0x2b4150);x+=1+gutter;
	text_at(cr,l,x,y,"RAM",9,PANGO_WEIGHT_SEMIBOLD,label);
	if(ram_total>0) g_snprintf(buf,sizeof buf,"%.1f / %.0f GiB",ram_used,ram_total);else g_strlcpy(buf,"—",sizeof buf);
	text_at(cr,l,x,18,buf,10,PANGO_WEIGHT_SEMIBOLD,value);
	fill(cr,x,35,rw,4,0x29414c);
	if(ram_total>0) fill(cr,x,35,rw*ram_used/ram_total,4,signal);
	x+=rw+gutter;fill(cr,x,9,1,32,0x2b4150);x+=1+gutter;
	text_at(cr,l,x,y,"NET",9,PANGO_WEIGHT_SEMIBOLD,label);
	if(network_known) {
		double peak=.1;
		for(int i=0;i<nnet;i++) peak=MAX(peak,MAX(rx_history[i],tx_history[i]));
		history(cr,rx_history,nnet,x,19,nw,7,peak,signal);
		history(cr,tx_history,nnet,x,19,nw,7,peak,0xa59375);
		g_snprintf(buf,sizeof buf,"↓%.1f  ↑%.1f Mb/s",down,up);
	} else g_strlcpy(buf,"—",sizeof buf);
	text_at(cr,l,x,29,buf,9,PANGO_WEIGHT_MEDIUM,value);
	x+=nw+gutter;fill(cr,x,9,1,32,0x2b4150);x+=1+gutter;
	double mid=(x+right)/2;
	text_center(cr,l,mid,7,clock_label,14,PANGO_WEIGHT_BOLD,value);
	text_center(cr,l,mid,26,date_label,9,PANGO_WEIGHT_MEDIUM,label);
	g_object_unref(l);
}
void home_slash_face(GtkDrawingArea *a,cairo_t *cr,int w,int h,gpointer unused) {
	(void)a;(void)unused;
	box(cr,0,0,w,h,10,0x101e28,0x607f91,0x304a56);
}
static gboolean notice_open;
static gboolean apps_on;
static int notice_unread,bell_center,systems_center;
static gboolean systems_open,systems_out_muted,systems_mic_muted;

// Match the approved 24-unit icon references at dock size.
static void draw_map_mark(cairo_t *cr) {
	cairo_move_to(cr,5,4.5);cairo_line_to(cr,19,4.5);cairo_line_to(cr,21,6.5);
	cairo_line_to(cr,21,17.5);cairo_line_to(cr,19,19.5);cairo_line_to(cr,5,19.5);
	cairo_line_to(cr,3,17.5);cairo_line_to(cr,3,6.5);cairo_close_path(cr);
	cairo_stroke(cr);
	cairo_push_group(cr);
	cairo_set_line_width(cr,.75);
	path(cr,8.5,5.5,8.5,18.5);path(cr,15.5,5.5,15.5,18.5);
	path(cr,4,12,20,12);cairo_stroke(cr);
	cairo_pop_group_to_source(cr);cairo_paint_with_alpha(cr,.32);
}

void home_apps_glyph(cairo_t *cr) {
	for(int x=3;x<=17;x+=7) for(double y=6.5;y<=13.5;y+=7) {
		const double r=.55;
		cairo_new_sub_path(cr);
		cairo_arc(cr,x+4-r,y+r,r,-G_PI_2,0);
		cairo_arc(cr,x+4-r,y+4-r,r,0,G_PI_2);
		cairo_arc(cr,x+r,y+4-r,r,G_PI_2,G_PI);
		cairo_arc(cr,x+r,y+r,r,G_PI,3*G_PI_2);
		cairo_close_path(cr);
	}
}

void home_map_glyph(cairo_t *cr) {
	cairo_save(cr);
	draw_map_mark(cr);
	cairo_restore(cr);
	cairo_move_to(cr,7,15.5);cairo_line_to(cr,12,15.5);
	cairo_line_to(cr,12,8.5);cairo_line_to(cr,17,8.5);cairo_stroke(cr);
	cairo_arc(cr,7,15.5,1.32,0,2*G_PI);
	cairo_new_sub_path(cr);cairo_arc(cr,17,8.5,1.32,0,2*G_PI);cairo_fill(cr);
}

struct button_travel {
	double lift,from,to;
	gint64 start;
	guint tick;
};

static void button_refresh(GtkWidget *button) {
	GtkWidget *child=gtk_button_get_child(GTK_BUTTON(button));
	if(GTK_IS_OVERLAY(child)) child=gtk_overlay_get_child(GTK_OVERLAY(child));
	if(child) gtk_widget_queue_draw(child);
}

static double mix(double a,double b,double x) {return a+(b-a)*x;}

static unsigned mix_ink(unsigned a,unsigned b,double x) {
	unsigned result=0;
	for(unsigned shift=0;shift<24;shift+=8)
		result|=(unsigned)lround(mix((a>>shift)&255,(b>>shift)&255,x))<<shift;
	return result;
}

static gboolean advance_button_travel(GtkWidget *button,GdkFrameClock *clock,gpointer data) {
	struct button_travel *travel=data;
	double duration=travel->to?60000.:105000.;
	double t=CLAMP((gdk_frame_clock_get_frame_time(clock)-travel->start)/duration,0.,1.);
	double u=1-pow(1-t,3);
	travel->lift=mix(travel->from,travel->to,u);
	button_refresh(button);
	if(t<1) return G_SOURCE_CONTINUE;
	travel->tick=0;
	return G_SOURCE_REMOVE;
}

static double button_depression(GtkWidget *button) {
	struct button_travel *travel=g_object_get_data(G_OBJECT(button),"mht-button-travel");
	return travel?travel->lift:0;
}

static void button_travel_reset(GtkWidget *button,gpointer unused) {
	(void)unused;
	struct button_travel *travel=g_object_get_data(G_OBJECT(button),"mht-button-travel");
	if(!travel) return;
	if(travel->tick) gtk_widget_remove_tick_callback(button,travel->tick);
	*travel=(struct button_travel){0};
}

static void update_button_travel(GtkWidget *button) {
	struct button_travel *travel=g_object_get_data(G_OBJECT(button),"mht-button-travel");
	if(!travel) {
		travel=g_new0(struct button_travel,1);
		g_object_set_data_full(G_OBJECT(button),"mht-button-travel",travel,g_free);
		g_signal_connect(button,"unmap",G_CALLBACK(button_travel_reset),NULL);
	}
	double to=gtk_widget_is_sensitive(button) &&
		(gtk_widget_get_state_flags(button)&GTK_STATE_FLAG_ACTIVE)?1:0;
	if(travel->to==to) return;
	gboolean animations=TRUE;
	g_object_get(gtk_widget_get_settings(button),"gtk-enable-animations",&animations,NULL);
	if(travel->tick) {
		gtk_widget_remove_tick_callback(button,travel->tick);
		travel->tick=0;
	}
	travel->to=to;
	if (!animations || !gtk_widget_get_mapped(button)) {
		travel->lift=to;
		return;
	}
	// Make even a sub-frame tap visible without delaying the action.
	travel->from=travel->lift=to?MAX(.32,travel->lift):travel->lift;
	travel->start=g_get_monotonic_time();
	travel->tick=gtk_widget_add_tick_callback(button,advance_button_travel,travel,NULL);
}

// One machined face, independent of the glyph/text and the widget hit box.
static void mechanical_face(cairo_t *cr,int w,int h,GtkWidget *button,
		gboolean toggled,gboolean primary) {
	if(w<5 || h<5) return;
	GtkStateFlags state=gtk_widget_get_state_flags(button);
	gboolean safe=gtk_widget_is_sensitive(button);
	double travel=safe?button_depression(button):0;
	gboolean hover=safe && (state&GTK_STATE_FLAG_PRELIGHT);
	gboolean bright=safe && (toggled || primary);
	double cut=MIN(5,(MIN(w,h)-2)*.5);
	struct point p[]={{cut,0},{w-cut,0},{w,cut},{w,h-cut},{w-cut,h},{cut,h},{0,h-cut},{0,cut}};
	contour(cr,p,G_N_ELEMENTS(p),.6);
	cairo_save(cr);
	cairo_clip_preserve(cr);
	unsigned raised=bright?0x244553:hover?0x233d4b:0x182933;
	unsigned sunk=bright?0x132c37:0x0b1923;
	ink(cr,safe?mix_ink(raised,sunk,travel):0x14222c);
	cairo_paint(cr);
	if (safe) {
		double lip=1-travel;
		cairo_set_source_rgba(cr,.033,.075,.11,.72*lip);
		cairo_rectangle(cr,0,h-3,w,3);cairo_fill(cr);
		ink(cr,mix_ink(hover?0x80a8b8:bright?0x547989:0x3d5665,sunk,travel));
		cairo_rectangle(cr,cut,.92,w-2*cut,1);cairo_fill(cr);
		// As the raised lip recedes, a narrow inner shadow makes the
		// depression readable even at fractional output scales.
		cairo_set_source_rgba(cr,.015,.044,.068,.7*travel);
		cairo_rectangle(cr,1,1.9+travel,w-2,2);cairo_fill(cr);
	}
	cairo_restore(cr);
	contour(cr,p,G_N_ELEMENTS(p),.6);
	ink(cr,bright?0x83c7d2:hover?0x658795:safe?0x3c5968:0x314552);
	cairo_set_line_width(cr,1);cairo_stroke(cr);
	if (safe && gtk_widget_has_visible_focus(button) && MIN(w,h)>14) {
		contour(cr,p,G_N_ELEMENTS(p),3.2);
		ink(cr,0xb5cdd8);
		cairo_set_line_width(cr,.85);cairo_stroke(cr);
	}
}

void home_tray_glyph(cairo_t *cr) {
	cairo_move_to(cr,-7,0);cairo_line_to(cr,-7,6);
	cairo_line_to(cr,7,6);cairo_line_to(cr,7,0);
	cairo_move_to(cr,-3,-2);cairo_line_to(cr,0,-5);cairo_line_to(cr,3,-2);
}

static void draw_icon(GtkDrawingArea *a,cairo_t *cr,int w,int h,gpointer data) {
	int act=GPOINTER_TO_INT(data);
	GtkWidget *widget=gtk_widget_get_parent(GTK_WIDGET(a));
	GtkStateFlags state=gtk_widget_get_state_flags(widget);
	gboolean selected=(act==HOME_MAP&&map_on)||(act==HOME_APPS&&apps_on)||(act==HOME_HANGAR&&hangar_open)||(act==HOME_NOTIFICATIONS&&notice_open)||(act==HOME_TRAY&&tray_open)||(act==HOME_AUDIO&&systems_open);
	gboolean safe=gtk_widget_is_sensitive(widget);
	gboolean active=selected || (state & (GTK_STATE_FLAG_PRELIGHT|GTK_STATE_FLAG_ACTIVE));
	mechanical_face(cr,w,h,widget,selected,FALSE);
	cairo_save(cr);
	cairo_translate(cr,w*.5,h*.5+(safe?button_depression(widget):0));
	if(w<30) {double scale=CLAMP((w-3)/17.,.1,.85);cairo_scale(cr,scale,scale);}
	ink(cr,!safe?0x62818f:active?0xb0f0f3:0xb2cad5);
	cairo_set_line_width(cr,1.25);cairo_set_line_cap(cr,CAIRO_LINE_CAP_ROUND);cairo_set_line_join(cr,CAIRO_LINE_JOIN_ROUND);
	switch(act) {
	case HOME_MAP:
		cairo_scale(cr,.875,.875);cairo_translate(cr,-12,-12);
		cairo_set_line_width(cr,1.45);
		home_map_glyph(cr);
		break;
	case HOME_APPS:
		cairo_scale(cr,.875,.875);cairo_translate(cr,-12,-12);
		cairo_set_line_width(cr,1.45);
		home_apps_glyph(cr);
		break;
	case HOME_CENTER:
		cairo_move_to(cr,0,-8);cairo_line_to(cr,4,-4);cairo_line_to(cr,8,6);cairo_line_to(cr,3,4);
		cairo_line_to(cr,0,8);cairo_line_to(cr,-3,4);cairo_line_to(cr,-8,6);cairo_line_to(cr,-4,-4);cairo_close_path(cr);
		path(cr,0,-3,0,3);break;
	case HOME_RELOAD:
		cairo_arc(cr,0,0,6,-2.70,-.24);cairo_new_sub_path(cr);
		cairo_arc(cr,0,0,6,.44,2.9);
		cairo_new_sub_path(cr);cairo_move_to(cr,3,-1);cairo_line_to(cr,6,-1);cairo_line_to(cr,7,-4);
		cairo_new_sub_path(cr);cairo_move_to(cr,-3,1);cairo_line_to(cr,-6,1);cairo_line_to(cr,-7,4);break;
	case HOME_HANGAR:
		cairo_move_to(cr,-8,7);cairo_line_to(cr,-8,-3);cairo_line_to(cr,-4,-7);
		cairo_line_to(cr,4,-7);cairo_line_to(cr,8,-3);cairo_line_to(cr,8,7);cairo_close_path(cr);
		cairo_move_to(cr,-5,7);cairo_line_to(cr,-5,-1);cairo_line_to(cr,5,-1);cairo_line_to(cr,5,7);
		path(cr,-2,3,2,3);break;
	case HOME_AUDIO:
		// Systems: three control tracks with their knobs at different levels.
		for(int k=0;k<3;k++) {
			double x=-5+5*k,knob=(double[]){2,-3,4}[k];
			path(cr,x,-7,x,knob-1.6);path(cr,x,knob+1.6,x,7);
			cairo_rectangle(cr,x-2,knob-1.6,4,3.2);
		}
		break;
	case HOME_TRAY: home_tray_glyph(cr);break;
	case HOME_NOTIFICATIONS:
		cairo_move_to(cr,-7,4);cairo_curve_to(cr,-4,2,-5,-1,-4,-4);
		cairo_curve_to(cr,-3,-8,3,-8,4,-4);cairo_curve_to(cr,5,-1,4,2,7,4);
		cairo_close_path(cr);cairo_new_sub_path(cr);cairo_arc(cr,0,4,2,0,G_PI);break;
	case HOME_LOCK:
		cairo_rectangle(cr,-7,-1,14,9);
		cairo_new_sub_path(cr);cairo_move_to(cr,-4,-1);cairo_line_to(cr,-4,-4);
		cairo_arc(cr,0,-4,4,G_PI,2*G_PI);cairo_line_to(cr,4,-1);path(cr,0,3,0,5);break;
	default: break;
	}
	cairo_stroke(cr);
	if(act==HOME_AUDIO && (systems_out_muted || systems_mic_muted)) {
		// Small struck marks: speaker bottom-right, microphone bottom-left.
		cairo_set_line_width(cr,1);
		if(systems_out_muted) {
			cairo_move_to(cr,10,4);cairo_line_to(cr,11.5,4);cairo_line_to(cr,14,2);cairo_line_to(cr,14,9);
			cairo_line_to(cr,11.5,7);cairo_line_to(cr,10,7);cairo_close_path(cr);
			path(cr,9,10,16,1);
		}
		if(systems_mic_muted) {
			cairo_rectangle(cr,-14,1.5,3,5);
			cairo_move_to(cr,-15.5,5.5);cairo_curve_to(cr,-15.5,9,-9.5,9,-9.5,5.5);path(cr,-12.5,8.5,-12.5,10);
			path(cr,-16,10,-9,1);
		}
		cairo_stroke(cr);
	}
	if(act==HOME_NOTIFICATIONS && notice_unread>0) {
		ink(cr,0x77dce8);cairo_arc(cr,9,-9,2.4,0,2*G_PI);cairo_fill(cr);
	}
	cairo_restore(cr);
}

int home_wing_width(int faces) {
	return 2*HOME_BUTTON_INSET+faces*HOME_BUTTON_WIDTH+MAX(0,faces-1)*HOME_BUTTON_GAP;
}

struct home_wing_fit home_wing_fit(int wing,int faces) {
	if(faces<1 || wing>=home_wing_width(faces))
		return (struct home_wing_fit){HOME_BUTTON_WIDTH,HOME_BUTTON_GAP,HOME_BUTTON_INSET};
	// Narrow outputs only: compact faces and gaps equally so every action stays reachable.
	struct home_wing_fit fit={0,wing>=150?8:wing>=100?5:wing>=66?3:1,wing>=150?13:wing>=100?9:wing>=66?6:3};
	fit.face=MAX(4,MIN(HOME_BUTTON_WIDTH,(wing-2*fit.inset-(faces-1)*fit.gap)/faces));
	return fit;
}

static void place(GtkWidget *child,int x,int y,int w,int h) {
	gtk_widget_set_size_request(child,MAX(0,w),MAX(0,h));
	gtk_fixed_move(GTK_FIXED(dock),child,x,y);
}
static void activate(GtkButton *b,gpointer d) {(void)b;on_action(GPOINTER_TO_INT(d));}
static void button_state(GtkWidget *w,GtkStateFlags previous,gpointer d) {
	(void)previous;(void)d;button_refresh(w);
}
static void mechanical_button_state(GtkWidget *w,GtkStateFlags previous,gpointer d) {
	(void)previous;(void)d;
	update_button_travel(w);
	button_refresh(w);
}

static void draw_settings_button(GtkDrawingArea *a,cairo_t *cr,int w,int h,gpointer data) {
	GtkWidget *button=gtk_widget_get_parent(gtk_widget_get_parent(GTK_WIDGET(a)));
	gboolean toggled=GTK_IS_TOGGLE_BUTTON(button) &&
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button));
	mechanical_face(cr,w,h,button,toggled,GPOINTER_TO_INT(data));
}

void home_mechanical_button(GtkWidget *button,gboolean primary) {
	// Wrap existing content without replacing its label, mnemonic or glyph.
	GtkWidget *label=gtk_button_get_child(GTK_BUTTON(button));
	if(GTK_IS_LABEL(label))
		gtk_accessible_update_property(GTK_ACCESSIBLE(button),GTK_ACCESSIBLE_PROPERTY_LABEL,
			gtk_label_get_text(GTK_LABEL(label)),-1);
	g_object_ref(label);
	gtk_button_set_child(GTK_BUTTON(button),NULL);
	GtkWidget *surface=gtk_overlay_new(),*face=gtk_drawing_area_new();
	gtk_widget_set_can_target(face,FALSE);
	gtk_widget_set_can_target(label,FALSE);
	gtk_widget_add_css_class(button,"home-mechanical-button");
	gtk_widget_add_css_class(label,"home-mechanical-label");
	if(primary) gtk_widget_add_css_class(button,"home-mechanical-primary");
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(face),draw_settings_button,GINT_TO_POINTER(primary),NULL);
	gtk_overlay_set_child(GTK_OVERLAY(surface),face);
	gtk_overlay_add_overlay(GTK_OVERLAY(surface),label);
	gtk_overlay_set_measure_overlay(GTK_OVERLAY(surface),label,TRUE);
	gtk_button_set_child(GTK_BUTTON(button),surface);
	g_signal_connect(button,"state-flags-changed",G_CALLBACK(mechanical_button_state),NULL);
	g_object_unref(label);
}

GtkWidget *home_dock_hangar(void) {return hangar;}

static void hangar_cancel_launch(void) {
	g_clear_handle_id(&hangar_launch_timer,g_source_remove);
	if(hangar_launch_slot>=0) {
		gtk_widget_remove_css_class(hangar_ink[hangar_launch_slot],"hangar-takeoff");
		hangar_launch_slot=-1;
	}
	g_clear_pointer(&hangar_launch_name,g_free);
}

gboolean home_dock_hangar_close(void) {
	if(!hangar_open) return FALSE;
	hangar_open=FALSE;
	hangar_cancel_launch();
	gtk_revealer_set_reveal_child(GTK_REVEALER(hangar),FALSE);
	gtk_widget_set_can_target(hangar,FALSE);
	gtk_widget_queue_draw(gtk_button_get_child(GTK_BUTTON(buttons[HOME_HANGAR])));
	on_region();
	return TRUE;
}
void home_dock_hangar_toggle(void) {
	if(home_dock_hangar_close() || !gtk_widget_is_sensitive(buttons[HOME_HANGAR])) return;
	hangar_open=TRUE;
	gtk_widget_set_can_target(hangar,TRUE);
	gtk_revealer_set_reveal_child(GTK_REVEALER(hangar),TRUE);
	gtk_widget_queue_draw(gtk_button_get_child(GTK_BUTTON(buttons[HOME_HANGAR])));
	on_region();
}
static gboolean hangar_connect(gpointer data) {
	(void)data;
	hangar_launch_timer=0;
	g_autofree char *name=g_steal_pointer(&hangar_launch_name);
	home_dock_hangar_close();
	if(name) on_connect(name);
	return G_SOURCE_REMOVE;
}
static void hangar_ship_clicked(GtkButton *button,gpointer data) {
	int i=GPOINTER_TO_INT(data);
	if(!hangar_open || i>=hangar_count || hangar_launch_name) return;
	hangar_launch_slot=i;
	hangar_launch_name=g_strdup(hangar_nodes[i]);
	gboolean motion=TRUE;
	g_object_get(gtk_widget_get_settings(GTK_WIDGET(button)),"gtk-enable-animations",&motion,NULL);
	if(!motion) {hangar_connect(NULL);return;}
	gtk_widget_add_css_class(hangar_ink[i],"hangar-takeoff");
	hangar_launch_timer=g_timeout_add(205,hangar_connect,NULL);
}
static void hangar_back(GtkDrawingArea *a,cairo_t *cr,int w,int h,gpointer data) {
	(void)a;(void)data;
	box(cr,0,0,w,h,12,0x15242d,0x657f8b,0x314854);
}
static gboolean hangar_ship_active(int slot) {
	GtkWidget *button=hangar_buttons[slot];
	return gtk_widget_is_sensitive(button) && (gtk_widget_has_visible_focus(button) ||
		(gtk_widget_get_state_flags(button)&GTK_STATE_FLAG_PRELIGHT));
}
static void hangar_pad(GtkDrawingArea *a,cairo_t *cr,int w,int h,gpointer data) {
	(void)a;
	gboolean occupied=GPOINTER_TO_INT(data)<hangar_count;
	gboolean active=occupied && hangar_ship_active(GPOINTER_TO_INT(data));
	if(!occupied) {
		box(cr,1,2,w-2,h-3,6,0x13212a,0x263b46,0x13212a);
		double l=w*.23,r=w*.77,t=h-13,b=h-8;
		cairo_save(cr);
		cairo_rectangle(cr,l,t,r-l,b-t);cairo_clip(cr);
		ink(cr,0x0a1217);cairo_paint(cr);
		ink(cr,0x806a3e);
		for(double x=l-5;x<r;x+=8) {
			cairo_move_to(cr,x,b);cairo_line_to(cr,x+4,b);
			cairo_line_to(cr,x+9,t);cairo_line_to(cr,x+5,t);cairo_close_path(cr);
		}
		cairo_fill(cr);
		cairo_restore(cr);
		return;
	}
	box(cr,1,2,w-2,h-3,6,0x0f1c25,active?0x719ca8:0x3b5664,0x263d48);
	ink(cr,active?0x9ce9ee:0x648996);cairo_set_line_width(cr,1);
	path(cr,w*.23,h-10,w*.77,h-10);cairo_stroke(cr);
}
static void hangar_ship(GtkDrawingArea *a,cairo_t *cr,int w,int h,gpointer data) {
	(void)a;
	double scale=MIN(w*.04,1.85);
	cairo_translate(cr,w*.5,h*.49);
	cairo_scale(cr,scale,scale);
	ink(cr,hangar_ship_active(GPOINTER_TO_INT(data))?0xc3f5f4:0x9dc4d0);
	cairo_set_line_width(cr,.9);
	cairo_move_to(cr,fleet_node[0].x,fleet_node[0].y);
	for(unsigned j=1;j<FLEET_COUNT(fleet_node);j++) cairo_line_to(cr,fleet_node[j].x,fleet_node[j].y);
	cairo_close_path(cr);cairo_move_to(cr,0,-4);cairo_line_to(cr,0,3);cairo_stroke(cr);
}
static void hangar_state(GtkWidget *w,GtkStateFlags previous,gpointer data) {
	(void)w;(void)previous;gtk_widget_queue_draw(hangar_ink[GPOINTER_TO_INT(data)]);
	gtk_widget_queue_draw(hangar_pads[GPOINTER_TO_INT(data)]);
}
static void hangar_revealed(GObject *widget,GParamSpec *prop,gpointer data) {
	(void)widget;(void)prop;(void)data;on_region();
}
static void create_hangar(void) {
	hangar=gtk_revealer_new();
	gtk_revealer_set_transition_type(GTK_REVEALER(hangar),GTK_REVEALER_TRANSITION_TYPE_SLIDE_UP);
	gtk_revealer_set_transition_duration(GTK_REVEALER(hangar),190);
	g_signal_connect(hangar,"notify::child-revealed",G_CALLBACK(hangar_revealed),NULL);
	gtk_widget_set_halign(hangar,GTK_ALIGN_START);gtk_widget_set_valign(hangar,GTK_ALIGN_END);
	gtk_widget_set_margin_bottom(hangar,HOME_BOTTOM+HOME_HEIGHT+8);
	gtk_widget_set_can_target(hangar,FALSE);
	GtkWidget *overlay=gtk_overlay_new();
	gtk_revealer_set_child(GTK_REVEALER(hangar),overlay);
	GtkWidget *hangar_face=gtk_drawing_area_new();
	gtk_widget_set_can_target(hangar_face,FALSE);
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(hangar_face),hangar_back,NULL,NULL);
	gtk_overlay_set_child(GTK_OVERLAY(overlay),hangar_face);
	GtkWidget *grid=gtk_grid_new();
	gtk_widget_add_css_class(grid,"home-hangar");
	gtk_grid_set_column_homogeneous(GTK_GRID(grid),TRUE);
	gtk_grid_set_row_spacing(GTK_GRID(grid),8);
	gtk_grid_set_column_spacing(GTK_GRID(grid),8);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay),grid);
	gtk_overlay_set_measure_overlay(GTK_OVERLAY(overlay),grid,TRUE);
	for(int i=0;i<HANGAR_SLOTS;i++) {
		GtkWidget *button=hangar_buttons[i]=gtk_button_new();
		gtk_widget_set_sensitive(button,FALSE);
		gtk_widget_set_hexpand(button,TRUE);
		gtk_widget_add_css_class(button,"home-dock-button");
		GtkWidget *col=gtk_box_new(GTK_ORIENTATION_VERTICAL,3);
		gtk_button_set_child(GTK_BUTTON(button),col);
		GtkWidget *bay=gtk_overlay_new();
		gtk_box_append(GTK_BOX(col),bay);
		hangar_pads[i]=gtk_drawing_area_new();
		gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(hangar_pads[i]),62);
		gtk_widget_set_can_target(hangar_pads[i],FALSE);
		gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(hangar_pads[i]),hangar_pad,GINT_TO_POINTER(i),NULL);
		gtk_overlay_set_child(GTK_OVERLAY(bay),hangar_pads[i]);
		hangar_ink[i]=gtk_drawing_area_new();
		gtk_widget_set_visible(hangar_ink[i],FALSE);
		gtk_widget_set_can_target(hangar_ink[i],FALSE);
		gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(hangar_ink[i]),hangar_ship,GINT_TO_POINTER(i),NULL);
		gtk_overlay_add_overlay(GTK_OVERLAY(bay),hangar_ink[i]);
		hangar_labels[i]=gtk_label_new(" ");
		gtk_label_set_ellipsize(GTK_LABEL(hangar_labels[i]),PANGO_ELLIPSIZE_END);
		gtk_box_append(GTK_BOX(col),hangar_labels[i]);
		g_signal_connect(button,"clicked",G_CALLBACK(hangar_ship_clicked),GINT_TO_POINTER(i));
		g_signal_connect(button,"state-flags-changed",G_CALLBACK(hangar_state),GINT_TO_POINTER(i));
		gtk_grid_attach(GTK_GRID(grid),button,i%HANGAR_COLUMNS,i/HANGAR_COLUMNS,1,1);
	}
}
static void fit_hangar(int output_width) {
	const int size=306;
	int anchor=(output_width-width)/2+hangar_anchor;
	gtk_widget_set_size_request(hangar,size,-1);
	gtk_widget_set_margin_start(hangar,CLAMP(anchor-size/2,12,MAX(12,output_width-12-size)));
}

void home_dock_set_nodes(const char *names) {
	g_clear_pointer(&hangar_nodes,g_strfreev);
	hangar_nodes=g_strsplit(names?names:"","\n",-1);
	hangar_cancel_launch();
	hangar_count=MIN(HANGAR_SLOTS,(int)g_strv_length(hangar_nodes));
	if(!hangar_nodes[0] || !*hangar_nodes[0]) hangar_count=0;
	for(int i=0;i<HANGAR_SLOTS;i++) {
		gboolean occupied=i<hangar_count;
		const char *name=occupied?hangar_nodes[i]:" ";
		g_autofree char *label=occupied?g_strdup_printf("Connect to Node %s",name):g_strdup("Empty berth");

		gtk_label_set_text(GTK_LABEL(hangar_labels[i]),name);
		gtk_widget_set_visible(hangar_ink[i],occupied);
		gtk_widget_set_sensitive(hangar_buttons[i],occupied);
		gtk_accessible_update_property(GTK_ACCESSIBLE(hangar_buttons[i]),GTK_ACCESSIBLE_PROPERTY_LABEL,label,-1);
	}
}

GtkWidget *home_dock_new(void (*action)(enum home_action),void (*connect)(const char *),void (*region_changed)(void)) {
	on_action=action;
	on_connect=connect;
	on_region=region_changed;
	dock=gtk_fixed_new();gtk_widget_set_halign(dock,GTK_ALIGN_CENTER);gtk_widget_set_valign(dock,GTK_ALIGN_END);
	gtk_widget_set_margin_bottom(dock,HOME_BOTTOM);
	rail=gtk_drawing_area_new();telemetry=gtk_drawing_area_new();
	GtkWidget *areas[]={rail,telemetry};
	GtkDrawingAreaDrawFunc draw[]={paint_rail,paint_telemetry};
	for(size_t i=0;i<G_N_ELEMENTS(areas);i++){
		gtk_widget_set_can_target(areas[i],FALSE);
		gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(areas[i]),draw[i],NULL,NULL);
		gtk_fixed_put(GTK_FIXED(dock),areas[i],0,0);
	}
	health_lamp=gtk_drawing_area_new();
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(health_lamp),paint_health,NULL,NULL);
	gtk_widget_set_can_target(health_lamp,FALSE);
	gtk_accessible_update_property(GTK_ACCESSIBLE(health_lamp),GTK_ACCESSIBLE_PROPERTY_LABEL,"System health",
		GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,"CPU/RAM load: no data",-1);
	g_signal_connect(health_lamp,"map",G_CALLBACK(update_health_tick),NULL);
	g_signal_connect(health_lamp,"unmap",G_CALLBACK(update_health_tick),NULL);
	gtk_fixed_put(GTK_FIXED(dock),health_lamp,0,0);
	gtk_widget_set_visible(health_lamp,FALSE);
	const char *labels[]={"Map View","App Grid","Command Center","Reload configuration (Home only)",
		"Node Hangar","Systems","Lock host session","System Tray","Notification Log"};
	for(int i=0;i<HOME_ACTIONS;i++){
		if(i==HOME_CENTER)continue;
		GtkWidget *ink_widget=gtk_drawing_area_new();
		gtk_widget_set_can_target(ink_widget,FALSE);
		gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(ink_widget),draw_icon,GINT_TO_POINTER(i),NULL);
		buttons[i]=gtk_button_new();
		gtk_widget_add_css_class(buttons[i],"home-dock-button");
		gtk_widget_set_focus_on_click(buttons[i],FALSE);
		gtk_accessible_update_property(GTK_ACCESSIBLE(buttons[i]),GTK_ACCESSIBLE_PROPERTY_LABEL,labels[i],-1);
		gtk_button_set_child(GTK_BUTTON(buttons[i]),ink_widget);
		g_signal_connect(buttons[i],"clicked",G_CALLBACK(activate),GINT_TO_POINTER(i));
		g_signal_connect(buttons[i],"state-flags-changed",G_CALLBACK(mechanical_button_state),NULL);
		gtk_fixed_put(GTK_FIXED(dock),buttons[i],0,0);
	}
	gtk_accessible_update_state(GTK_ACCESSIBLE(buttons[HOME_TRAY]),GTK_ACCESSIBLE_STATE_EXPANDED,FALSE,-1);
	gtk_widget_set_visible(buttons[HOME_TRAY],FALSE);
	create_hangar();
	return dock;
}

void home_dock_layout(int output_width,int output_height,int wanted,gboolean map,gboolean apps,gboolean apps_open,gboolean reload,gboolean tray) {
	int w=MAX(1,MIN(wanted,output_width-24));
	gboolean hangar_available=reload && output_height>=380 && output_width>=330;
	gtk_widget_set_sensitive(buttons[HOME_HANGAR],hangar_available);
	if(!hangar_available) home_dock_hangar_close();
	gtk_widget_set_sensitive(buttons[HOME_RELOAD],reload);
	// No compositor lock surface exists in the native developer session yet.
	gtk_widget_set_sensitive(buttons[HOME_LOCK],g_strcmp0(g_getenv("MOTHERSHIP_NATIVE_SESSION"),"1")!=0);
	gtk_widget_set_sensitive(buttons[HOME_APPS],apps);
	if (map_on!=map) {map_on=map;gtk_widget_queue_draw(gtk_button_get_child(GTK_BUTTON(buttons[HOME_MAP])));}
	if (apps_on!=apps_open) {apps_on=apps_open;gtk_widget_queue_draw(gtk_button_get_child(GTK_BUTTON(buttons[HOME_APPS])));}
	gtk_widget_set_sensitive(buttons[HOME_TRAY],reload);
	if(w==width && tray_available==tray) {fit_hangar(output_width);return;}
	tray_available=tray;
	gtk_widget_set_visible(buttons[HOME_TRAY],tray);
	width=w;
	// Both wings fit the fuller side at the fixed face size. Anchor each
	// outer face to the same inset; spare space belongs to the telemetry.
	int left_count=0;
	for(int i=0;i<HOME_HANGAR;i++) left_count+=buttons[i]!=NULL;
	int faces=MAX(left_count,tray?5:4);
	wing=MAX(28,MIN(home_wing_width(faces),(w-48)/2));
	struct home_wing_fit fit=home_wing_fit(wing,faces);
	button_gap=fit.gap;bw=fit.face;
	int end_pad=fit.inset,right_width=bw;
	center_width=MAX(0,w-2*(wing+12));
	tw=MIN(center_width,HOME_TELEMETRY_WIDTH_MAX);
	tx=tw>0?(w-tw)/2:wing+12;
	place(rail,0,0,w,HOME_HEIGHT);
	place(telemetry,tx,0,tw,51);
	gtk_widget_set_visible(telemetry,tw>=32);
	place(health_lamp,tx+10,12,HEALTH_LAMP_SIZE,HEALTH_LAMP_SIZE);
	gtk_widget_set_visible(health_lamp,tw>=HEALTH_MIN_WIDTH);
	for(int i=0,slot=0;i<HOME_HANGAR;i++) {
		if(buttons[i]) place(buttons[i],end_pad+slot++*(bw+button_gap),18,bw,HOME_BUTTON_HEIGHT);
	}
	for(int i=HOME_NOTIFICATIONS,slot=0;i>=HOME_HANGAR;i--) {
		if(i==HOME_TRAY && !tray)continue;
		int x=w-end_pad-right_width-slot++*(right_width+button_gap);
		place(buttons[i],x,18,right_width,HOME_BUTTON_HEIGHT);
		if(i==HOME_NOTIFICATIONS)bell_center=x+right_width/2;
		if(i==HOME_HANGAR)hangar_anchor=x+right_width/2;
		if(i==HOME_TRAY)tray_center=x+right_width/2;
		if(i==HOME_AUDIO)systems_center=x+right_width/2;
	}
	fit_hangar(output_width);
	gtk_widget_queue_draw(rail);gtk_widget_queue_draw(telemetry);
}

int home_dock_notice_center(void) {return bell_center;}
GtkWidget *home_dock_notice_button(void) {return buttons[HOME_NOTIFICATIONS];}
GtkWidget *home_dock_tray_button(void) {return buttons[HOME_TRAY];}
int home_dock_tray_center(void) {return tray_center;}
int home_dock_systems_center(void) {return systems_center;}
GtkWidget *home_dock_systems_button(void) {return buttons[HOME_AUDIO];}
static void systems_redraw(void) {
	if(buttons[HOME_AUDIO]) gtk_widget_queue_draw(gtk_button_get_child(GTK_BUTTON(buttons[HOME_AUDIO])));
}
void home_dock_systems_open(gboolean open) {
	if(systems_open==open) return;
	systems_open=open;
	if(buttons[HOME_AUDIO]) gtk_accessible_update_state(GTK_ACCESSIBLE(buttons[HOME_AUDIO]),GTK_ACCESSIBLE_STATE_EXPANDED,open,-1);
	systems_redraw();
}
void home_dock_systems_mute(gboolean output,gboolean input) {
	if(systems_out_muted==output && systems_mic_muted==input) return;
	systems_out_muted=output;systems_mic_muted=input;
	if(buttons[HOME_AUDIO]) gtk_accessible_update_property(GTK_ACCESSIBLE(buttons[HOME_AUDIO]),GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,
		output && input?"Output and microphone muted":output?"Output muted":input?"Microphone muted":"",-1);
	systems_redraw();
}
void home_dock_tray_open(gboolean open) {
	if(tray_open==open)return;
	tray_open=open;
	gtk_widget_queue_draw(gtk_button_get_child(GTK_BUTTON(buttons[HOME_TRAY])));
}
void home_dock_notice_open(gboolean open) {
	notice_open=open;
	if(buttons[HOME_NOTIFICATIONS]) gtk_widget_queue_draw(gtk_button_get_child(GTK_BUTTON(buttons[HOME_NOTIFICATIONS])));
}
void home_dock_notice_count(int unread) {
	notice_unread=unread;
	if(buttons[HOME_NOTIFICATIONS]) gtk_widget_queue_draw(gtk_button_get_child(GTK_BUTTON(buttons[HOME_NOTIFICATIONS])));
}
int home_dock_center_width(void) {return center_width;}
int home_dock_telemetry_center(void) {return tx+tw/2-width/2;}
int home_dock_telemetry_width(void) {return tw;}

static void paint_node_telemetry(GtkDrawingArea *a,cairo_t *cr,int w,int h,gpointer data) {
	(void)data;
	const int left=11+(w>=HEALTH_MIN_WIDTH?HEALTH_SPAN:0);
	if(w>=NODE_TELEMETRY_FULL_WIDTH) {
		// Center the readouts themselves, without reserving a headline.
		cairo_save(cr);
		cairo_translate(cr,0,(h-47)*.5);
		paint_telemetry(a,cr,w,h,GINT_TO_POINTER(1));
		cairo_restore(cr);
	} else {
		PangoLayout *layout=pango_cairo_create_layout(cr);
		char buf[32];
		double y=(h-51)*.5;
		if(cpu>=0) g_snprintf(buf,sizeof buf,"CPU %d%%",cpu); else g_strlcpy(buf,"CPU —",sizeof buf);
		text_at(cr,layout,left,y+7,buf,9,PANGO_WEIGHT_SEMIBOLD,0xdae8ef);
		if(ram_total>0) g_snprintf(buf,sizeof buf,"RAM %.0f%%",100*ram_used/ram_total);
		else g_strlcpy(buf,"RAM —",sizeof buf);
		text_at(cr,layout,left,y+20,buf,9,PANGO_WEIGHT_MEDIUM,0xaac1ce);
		if(w>=211) text_at(cr,layout,left,y+33,network_known?"NET active":"NET —",9,PANGO_WEIGHT_MEDIUM,0xaac1ce);
		text_at(cr,layout,w-43,y+9,clock_label,12,PANGO_WEIGHT_BOLD,0xdae8ef);
		text_at(cr,layout,w-40,y+28,date_label,9,PANGO_WEIGHT_MEDIUM,0xaac1ce);
		g_object_unref(layout);
	}
	if(w>=HEALTH_MIN_WIDTH) {
		cairo_save(cr);
		cairo_translate(cr,10,(h-HEALTH_LAMP_SIZE)*.5);
		cairo_new_path(cr);
		paint_health(a,cr,HEALTH_LAMP_SIZE,HEALTH_LAMP_SIZE,NULL);
		cairo_restore(cr);
	}
}
GtkWidget *home_node_telemetry_new(void) {
	node_readout=gtk_drawing_area_new();
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(node_readout),paint_node_telemetry,NULL,NULL);
	gtk_widget_set_can_target(node_readout,FALSE);
	gtk_accessible_update_property(GTK_ACCESSIBLE(node_readout),GTK_ACCESSIBLE_PROPERTY_LABEL,"System telemetry",-1);
	return node_readout;
}

void home_dock_sample(int busy,const int *values,int n,double used,double total,double rx,double tx_,
		gboolean known,int level,int health_cpu,const char *clock,const char *date) {
	cpu=busy;ncpu=CLAMP(n,0,(int)G_N_ELEMENTS(cores));
	if(ncpu) memcpy(cores,values,ncpu*sizeof(int));
	ram_used=used;ram_total=total;down=rx;up=tx_;network_known=known;
	if(!health_lamp) return;
	level=CLAMP(level,HEALTH_UNKNOWN,HEALTH_CRITICAL);
	if((int)health!=level) {
		health=level;health_started=0;health_phase=0;
		gtk_widget_queue_draw(health_lamp);
	}
	update_health_tick(health_lamp,NULL);
	static const char *status[]={"No data","Normal","High","Very high"};
	char cpu_text[16]="—",ram_text[16]="—",hint[96];
	if(health_cpu>=0) g_snprintf(cpu_text,sizeof cpu_text,"%d%%",health_cpu);
	if(total>0) g_snprintf(ram_text,sizeof ram_text,"%.1f%%",100*used/total);
	g_snprintf(hint,sizeof hint,"%s · CPU %s (10 s) · RAM %s",status[health],cpu_text,ram_text);
	gtk_accessible_update_property(GTK_ACCESSIBLE(health_lamp),GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,hint,-1);
	g_strlcpy(clock_label,clock,sizeof clock_label);g_strlcpy(date_label,date,sizeof date_label);
	if(known){
		if(nnet==(int)G_N_ELEMENTS(rx_history)) {
			memmove(rx_history,rx_history+1,(nnet-1)*sizeof(double));
			memmove(tx_history,tx_history+1,(nnet-1)*sizeof(double));
			nnet--;
		}
		rx_history[nnet]=rx;tx_history[nnet++]=tx_;
	} else nnet=0;
	if(gtk_widget_get_visible(dock)) gtk_widget_queue_draw(telemetry);
	if(node_readout && gtk_widget_get_mapped(node_readout)) gtk_widget_queue_draw(node_readout);
}
