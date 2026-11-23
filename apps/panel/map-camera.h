#ifndef MOTHERSHIP_MAP_CAMERA_H
#define MOTHERSHIP_MAP_CAMERA_H

#include <math.h>
#include <stdbool.h>

enum map_animation { MAP_ANIMATION_OFF, MAP_ANIMATION_LINEAR, MAP_ANIMATION_EASE, MAP_ANIMATION_COUNT };

enum { MAP_PAN_MIN=50, MAP_PAN_DEFAULT=100, MAP_PAN_MAX=200, MAP_PAN_STEP=10 };
struct map_camera {double x, y;};
struct map_wallpaper_fit {double x, y, width, height;};
struct map_zoom {double scale,grid_x,grid_y;bool anchored;};

#define MAP_ZOOM_MIN .65
#define MAP_ZOOM_MAX 2.25
#define MAP_ZOOM_DURATION_US 90000

enum {
	MAP_REFERENCE_W=2560, MAP_REFERENCE_H=1440,
	MAP_TABLE_W=4*MAP_REFERENCE_W, MAP_TABLE_H=4*MAP_REFERENCE_H,
	MAP_TABLE_MIN_X=-(MAP_TABLE_W-MAP_REFERENCE_W)/2,
	MAP_TABLE_MIN_Y=-(MAP_TABLE_H-MAP_REFERENCE_H)/2,
};

static inline double map_camera_clamp(double v, double lo, double hi) {
	return fmin(hi,fmax(lo,v));
}

static inline struct map_camera map_tabletop_clamp(struct map_camera c,double w,double h) {
	double max_x=MAP_TABLE_MIN_X+fmax(0,MAP_TABLE_W-w);
	double max_y=MAP_TABLE_MIN_Y+fmax(0,MAP_TABLE_H-h);
	return (struct map_camera){
		map_camera_clamp(c.x,MAP_TABLE_MIN_X,max_x),
		map_camera_clamp(c.y,MAP_TABLE_MIN_Y,max_y),
	};
}

static inline double map_wheel_target(double from,double delta) {
	return map_camera_clamp(from*pow(1.10,-map_camera_clamp(delta,-60,60)/10.),
		MAP_ZOOM_MIN,MAP_ZOOM_MAX);
}

static inline double map_zoom_sample(double from,double to,double progress,int animation) {
	if (progress<=0) return from;
	if (progress>=1) return to;
	if (animation==MAP_ANIMATION_EASE) progress=progress*progress*(3-2*progress);
	return from*exp(log(to/from)*progress);
}

static inline bool map_zoom_to(struct map_camera *camera,struct map_zoom *z,
		double w,double h,double x,double y,double next) {
	if (next==z->scale) return false;
	double ratio=next/z->scale;
	struct map_camera anchor={camera->x+x/z->scale-x/next,camera->y+y/z->scale-y/next};
	*camera=map_tabletop_clamp(anchor,w/next,h/next);
	z->grid_x=x+(z->grid_x-x)*ratio;
	z->grid_y=y+(z->grid_y-y)*ratio;
	z->scale=next;z->anchored=true;
	return true;
}

static inline struct map_wallpaper_fit map_screen_wallpaper(double w,double h,
		double iw,double ih,bool contain) {
	double scale=contain?fmin(w/iw,h/ih):fmax(w/iw,h/ih);
	double ww=iw*scale,hh=ih*scale;
	return (struct map_wallpaper_fit){(w-ww)*.5,(h-hh)*.5,ww,hh};
}

struct map_velocity {
	double from_x, from_y, to_x, to_y, elapsed, duration;
};

static inline double ease(double x) { return x*x*(3.-2.*x); }
static inline double ease_integral(double x) { return x*x*x*(1.-.5*x); }

static inline void map_velocity_target(struct map_velocity *v,int x,int y,enum map_animation mode,int percent) {
	double norm=hypot(x,y);
	const double pan_speed=530.4*percent/100.; // logical px per second
	double to_x=norm?pan_speed*x/norm:0, to_y=norm?pan_speed*y/norm:0;
	if (mode!=MAP_ANIMATION_EASE) {
		*v=(struct map_velocity){.to_x=to_x,.to_y=to_y};
		return;
	}
	if(to_x==v->to_x && to_y==v->to_y) return;
	double t=v->duration?ease(v->elapsed/v->duration):1.;
	v->from_x+=(v->to_x-v->from_x)*t;
	v->from_y+=(v->to_y-v->from_y)*t;
	v->to_x=to_x;v->to_y=to_y;
	v->elapsed=0;v->duration=norm?.140:.180;
}

static inline bool map_velocity_move(struct map_velocity *v,double dt,double *x,double *y) {
	*x=*y=0;
	// A long system suspension should not appear as a position jump.
	if(dt<0 || dt>.25) return true;
	if(v->elapsed<v->duration) {
		double transition=fmin(dt,v->duration-v->elapsed);
		double a=v->elapsed/v->duration, b=(v->elapsed+transition)/v->duration;
		double weight=v->duration*(ease_integral(b)-ease_integral(a));
		*x+=v->from_x*transition+(v->to_x-v->from_x)*weight;
		*y+=v->from_y*transition+(v->to_y-v->from_y)*weight;
		v->elapsed+=transition;dt-=transition;
	}
	*x+=v->to_x*dt; *y+=v->to_y*dt;
	return v->to_x!=0 || v->to_y!=0 || v->elapsed<v->duration;
}

#endif
