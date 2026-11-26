#pragma once
#include <glib.h>
#include <math.h>
#include <string.h>

enum { FIELD_W=1000, FIELD_H=550, ENEMIES=12, SHOTS=96, LOBS=28, PATCHES=12, PLASMA_CELLS=10 };
enum { UP=1, DOWN=2, LEFT=4, RIGHT=8, FIRE=16, PLASMA=32 };
enum animal { CHICKEN, PIG, DOG, ANIMALS };
struct vec { double x,y; };
struct bounds { double l,t,r,b; };
struct shot { struct vec at,v; gboolean live; };
struct impact { struct vec at; double remaining; };
struct enemy {
	struct vec at;
	double warning,next;
	unsigned edge;
	enum animal kind;
	gboolean live;
};
struct lob {
	struct vec at,v;
	double age,travel,spin;
	unsigned owner;
	enum animal kind;
	gboolean live,landed;
};
struct patch { struct vec at;double left; };
struct game {
	struct vec player,velocity,mouse;
	struct bounds bounds;
	double aim, next_fire,plasma,beam,invulnerable,inter_wave,damage,bank,slip,mud;
	unsigned held,mouse_held,score,wave,hull;
	gboolean mouse_valid;
	struct enemy enemies[ENEMIES];
	struct impact impacts[ENEMIES];
	struct shot shots[SHOTS];
	struct lob lobs[LOBS];
	struct patch patches[PATCHES];
};

static const double plasma_cooldown=3,plasma_duration=.28,impact_duration=.18;
static const double damage_duration=.46,bank_limit=10,bank_rate=120;
enum { PLASMA_HALF_WIDTH=32, ENEMY_HIT_RADIUS=18, FLEET_GAP=82 };

static inline struct vec vec_add(struct vec a,struct vec b) {return (struct vec){a.x+b.x,a.y+b.y};}
static inline struct vec vec_sub(struct vec a,struct vec b) {return (struct vec){a.x-b.x,a.y-b.y};}
static inline struct vec vec_mul(struct vec a,double k) {return (struct vec){a.x*k,a.y*k};}
static inline double vec_dot(struct vec a,struct vec b) {return a.x*b.x+a.y*b.y;}
static inline double vec_len(struct vec a) {return hypot(a.x,a.y);}
static inline struct vec vec_unit(struct vec a) {double n=vec_len(a);return n>1e-8?vec_mul(a,1/n):(struct vec){0,-1};}
static inline struct vec vec_rotate(struct vec a,double angle) {
	double c=cos(angle),s=sin(angle);return (struct vec){a.x*c-a.y*s,a.x*s+a.y*c};
}
static inline struct vec vec_clamp(struct vec p,struct bounds b,double padding) {
	double x=MIN(padding,(b.r-b.l)*.5),y=MIN(padding,(b.b-b.t)*.5);
	return (struct vec){CLAMP(p.x,b.l+x,b.r-x),CLAMP(p.y,b.t+y,b.b-y)};
}
static inline struct vec heading(const struct game *g) {return (struct vec){cos(g->aim),sin(g->aim)};}
static inline struct vec mount(const struct game *g,double x,double y) {
	return vec_add(g->player,vec_rotate((struct vec){x,y},g->aim+G_PI_2));
}
static inline void update_aim(struct game *g) {
	struct vec d=vec_sub(g->mouse,g->player);
	if(g->mouse_valid && vec_dot(d,d)>36)g->aim=atan2(d.y,d.x);
}
static inline gboolean outside(struct vec p,struct bounds b) {
	return p.x<b.l || p.x>b.r || p.y<b.t || p.y>b.b;
}
static inline struct vec entry(const struct game *g,unsigned i) {
	const struct enemy *e=&g->enemies[i];struct bounds b=g->bounds;
	double u=.24+.25*(i/4)+.04*(e->edge%2);
	switch(e->edge) {
	case 0:return (struct vec){b.l+(b.r-b.l)*u,b.t+18};
	case 1:return (struct vec){b.r-18,b.t+(b.b-b.t)*u};
	case 2:return (struct vec){b.l+(b.r-b.l)*(1-u),b.b-18};
	default:return (struct vec){b.l+18,b.t+(b.b-b.t)*(1-u)};
	}
}
static inline void set_wave(struct game *g) {
	for(unsigned i=0;i<ENEMIES;++i)
		g->enemies[i]=(struct enemy){.warning=.45+.59*(i/4)+.12*(i%4),
			.next=1.72+.19*i,.edge=i%4,.kind=(i+g->wave-1)%ANIMALS,.live=TRUE};
	memset(g->lobs,0,sizeof(g->lobs));memset(g->patches,0,sizeof(g->patches));
	g->inter_wave=0;
}
static inline void round_reset(struct game *g,struct bounds b) {
	*g=(struct game){.player={FIELD_W*.5,FIELD_H*.63},.bounds=b,.hull=3,.wave=1,.aim=-G_PI_2};
	set_wave(g);
}
static inline void fire(struct game *g) {
	struct vec v=vec_mul(heading(g),660);
	for(int ship=-1;ship<=1;++ship) for(int wing=-1;wing<=1;wing+=2) {
		double x=ship*FLEET_GAP+wing*(ship?14:36),y=ship?19:-3;
		for(unsigned i=0;i<SHOTS;++i)if(!g->shots[i].live) {
			g->shots[i]=(struct shot){mount(g,x,y),v,TRUE};break;
		}
	}
	g->next_fire=.17;
}
static inline void hit(struct game *g) {
	if(!g->invulnerable && g->hull) {g->hull--;g->invulnerable=1;g->damage=damage_duration;}
}
static inline double beam_length(const struct game *g) {
	// All visual quads and impacts stay safely away from the 45° corners.
	struct bounds b=g->bounds;struct vec p=g->player,d=heading(g);
	double m=PLASMA_HALF_WIDTH+3;
	double sx=d.x>1e-8?(b.r-m-p.x)/d.x:d.x< -1e-8?(b.l+m-p.x)/d.x:INFINITY;
	double sy=d.y>1e-8?(b.b-m-p.y)/d.y:d.y< -1e-8?(b.t+m-p.y)/d.y:INFINITY;
	return MAX(20,MIN(sx,sy));
}
static inline void defeat(struct game *g,unsigned i,unsigned score,gboolean flash) {
	struct enemy *e=&g->enemies[i];
	e->live=FALSE;g->score+=score;
	if(flash)g->impacts[i]=(struct impact){e->at,impact_duration};
}
static inline void plasma_sweep(struct game *g) {
	struct vec d=heading(g);double range=beam_length(g);
	for(unsigned i=0;i<ENEMIES;++i) {
		struct enemy *e=&g->enemies[i];
		if(!e->live || e->warning>0) continue;
		struct vec p=vec_sub(e->at,g->player);
		double along=vec_dot(d,p),normal=fabs(d.x*p.y-d.y*p.x);
		if(along+ENEMY_HIT_RADIUS>=20 && along-ENEMY_HIT_RADIUS<=range &&
		   normal<=PLASMA_HALF_WIDTH+ENEMY_HIT_RADIUS)defeat(g,i,20,TRUE);
	}
}
static inline gboolean plasma_begin(struct game *g) {
	if(g->plasma>0)return FALSE;
	g->plasma=plasma_cooldown;g->beam=plasma_duration;
	plasma_sweep(g);return TRUE;
}
static inline void bank_step(struct game *g,double motion,double dt) {
	double target=motion>0?bank_limit:motion<0?-bank_limit:0;
	double bound=bank_rate*dt;g->bank+=CLAMP(target-g->bank,-bound,bound);
}
static inline double damage_opacity(double remaining) {
	if(remaining<=0) return 0;
	double elapsed=damage_duration-remaining;
	double first=1-fabs(elapsed-.075)/.075,second=1-fabs(elapsed-.27)/.10;
	return .52*fmax(0,fmax(first,second*.82));
}
static inline void new_lob(struct game *g,unsigned index) {
	struct enemy *e=&g->enemies[index];struct vec dv=vec_sub(g->player,e->at);
	double dist=vec_len(dv);
	if(e->kind==DOG) {
		for(unsigned j=0;j<LOBS;++j)
			if(g->lobs[j].live && g->lobs[j].owner==index)return;
	}
	for(unsigned j=0;j<LOBS;++j) if(!g->lobs[j].live) {
		double speed=e->kind==PIG?145:e->kind==DOG?380:250;
		double travel=e->kind==DOG?CLAMP(dist/speed+.16,.52,.92):CLAMP(dist/speed,.28,1.65);
		g->lobs[j]=(struct lob){.at=e->at,.v=vec_mul(vec_unit(dv),speed),
			.travel=travel,.owner=index,.kind=e->kind,.live=TRUE};
		break;
	}
}
static inline void new_patch(struct game *g,struct vec p) {
	for(unsigned i=0;i<PATCHES;++i) if(g->patches[i].left<=0) {
		g->patches[i]=(struct patch){vec_clamp(p,g->bounds,23),2.4};return;
	}
}
static inline double segment_distance(struct vec a,struct vec b,struct vec p) {
	struct vec delta=vec_sub(b,a),rel=vec_sub(p,a);
	double sq=vec_dot(delta,delta),t=sq>0?CLAMP(vec_dot(delta,rel)/sq,0,1):0;
	return vec_len(vec_sub(rel,vec_mul(delta,t)));
}
static inline void step_lobs(struct game *g,double dt) {
	for(unsigned j=0;j<LOBS;++j) {
		struct lob *p=&g->lobs[j];if(!p->live)continue;
		struct vec old=p->at;p->age+=dt;p->spin+=dt*(p->kind==DOG?10:4);
		if(p->kind==DOG && p->age>=p->travel) {
			struct enemy *e=&g->enemies[p->owner];
			if(!e->live || p->age>p->travel+2 || vec_len(vec_sub(e->at,p->at))<23) {p->live=FALSE;continue;}
			p->v=vec_mul(vec_unit(vec_sub(e->at,p->at)),420);
		}
		p->at=vec_add(p->at,vec_mul(p->v,dt));
		gboolean contact=segment_distance(old,p->at,g->player)<24;
		if(p->kind==CHICKEN && (contact || p->age>=p->travel || outside(p->at,g->bounds))) {
			new_patch(g,p->at);p->live=FALSE;
		} else if(p->kind==PIG && contact) {
			hit(g);g->mud=.86;p->live=FALSE;
		} else if(p->kind==DOG && contact && !p->landed) {
			hit(g);p->landed=TRUE;
		}
		if((p->kind==PIG && (p->age>3.5 || outside(p->at,g->bounds))) ||
		   (p->kind==DOG && p->age<p->travel && outside(p->at,g->bounds))) {
			if(p->kind==DOG) {p->at=vec_clamp(p->at,g->bounds,2);p->age=p->travel;}
			else p->live=FALSE;
		}
	}
}
static inline void step_enemies(struct game *g,double dt) {
	unsigned alive=0;
	for(unsigned i=0;i<ENEMIES;++i) {
		struct enemy *e=&g->enemies[i];if(!e->live)continue;
		++alive;
		if(e->warning>0) {
			e->warning-=dt;
			if(e->warning<=0)e->at=entry(g,i);
			else continue;
		}
		struct vec to=vec_sub(g->player,e->at);double dist=vec_len(to);
		struct vec d=vec_unit(to);
		double drive=dist>220?.94:dist>120?.3:-.58;
		double turn=(i%2?1:-1)*(1-fmin(.73,dist/600));
		struct vec velocity=vec_add(vec_mul(d,drive),(struct vec){-d.y*turn,d.x*turn});
		double speed=e->kind==PIG?53:e->kind==DOG?99:75;
		e->at=vec_clamp(vec_add(e->at,vec_mul(velocity,dt*(speed+MIN(28,g->wave*2)))),g->bounds,17);
		if((e->next-=dt)<=0) {
			new_lob(g,i);
			e->next=(e->kind==PIG?3.4:e->kind==DOG?2.85:2.6)+.20*(i%4)-MIN(.7,(g->wave-1)*.05);
		}
	}
	if(!alive && (g->inter_wave+=dt)>.8) {++g->wave;set_wave(g);}
}
static inline void game_step(struct game *g,double dt) {
	g->next_fire=fmax(0,g->next_fire-dt);g->plasma=fmax(0,g->plasma-dt);
	g->invulnerable=fmax(0,g->invulnerable-dt);
	g->slip=fmax(0,g->slip-dt);g->mud=fmax(0,g->mud-dt);
	for(unsigned i=0;i<ENEMIES;++i)g->impacts[i].remaining=fmax(0,g->impacts[i].remaining-dt);
	for(unsigned i=0;i<PATCHES;++i)if(g->patches[i].left>0) {
		g->patches[i].left=fmax(0,g->patches[i].left-dt);
		if(vec_len(vec_sub(g->patches[i].at,g->player))<34)g->slip=.55;
	}
	struct vec input={!!(g->held&RIGHT)-!!(g->held&LEFT),!!(g->held&DOWN)-!!(g->held&UP)};
	struct vec dst=vec_mul(input,310*(g->mud>0?.52:1)/(input.x && input.y?G_SQRT2:1));
	double mix=g->slip>0?MIN(1,dt*4):1;
	g->velocity=vec_add(vec_mul(g->velocity,1-mix),vec_mul(dst,mix));
	struct vec before=g->player;
	g->player=vec_clamp(vec_add(g->player,vec_mul(g->velocity,dt)),g->bounds,FLEET_GAP+33);
	bank_step(g,g->player.x-before.x,dt);update_aim(g);
	if(((g->held|g->mouse_held)&FIRE) && !g->next_fire) fire(g);
	step_enemies(g,dt);
	g->beam=fmax(0,g->beam-dt);if(g->beam>0)plasma_sweep(g);
	for(unsigned j=0;j<SHOTS;++j) {
		struct shot *p=&g->shots[j];if(!p->live)continue;
		struct vec old=p->at;p->at=vec_add(p->at,vec_mul(p->v,dt));
		if(outside(p->at,g->bounds)) {p->live=FALSE;continue;}
		for(unsigned i=0;i<ENEMIES;++i) {
			struct enemy *e=&g->enemies[i];
			if(e->live && e->warning<=0 && segment_distance(old,p->at,e->at)<ENEMY_HIT_RADIUS) {
				p->live=FALSE;defeat(g,i,10,FALSE);break;
			}
		}
	}
	step_lobs(g,dt);
}
