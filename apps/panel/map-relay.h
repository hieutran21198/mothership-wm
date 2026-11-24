#ifndef MOTHERSHIP_MAP_RELAY_H
#define MOTHERSHIP_MAP_RELAY_H

#include <gtk/gtk.h>

enum map_relay_phase { RELAY_IDLE, RELAY_OUT, RELAY_WAIT, RELAY_IN, RELAY_DONE };
struct map_relay_motion {
	enum map_relay_phase phase;
	gint64 start;
	double progress;
	gboolean acknowledged;
};

enum { MAP_RELAY_OUT_US=150000, MAP_RELAY_IN_US=180000 };

static inline void map_relay_accept(struct map_relay_motion *m) {
	m->acknowledged=TRUE;
	if(m->phase==RELAY_WAIT) {
		m->phase=RELAY_IN;
		m->start=0;
		m->progress=0;
	}
}

static inline void map_relay_advance(struct map_relay_motion *m,gint64 now) {
	if(m->phase!=RELAY_OUT && m->phase!=RELAY_IN) return;
	if(!m->start) m->start=now;
	double duration=m->phase==RELAY_OUT?MAP_RELAY_OUT_US:MAP_RELAY_IN_US;
	m->progress=CLAMP((now-m->start)/duration,0,1);
	if(m->progress<1) return;
	if(m->phase==RELAY_IN) m->phase=RELAY_DONE;
	else if(!m->acknowledged) m->phase=RELAY_WAIT;
	else {
		m->phase=RELAY_IN;
		m->start=now;
		m->progress=0;
	}
}

void map_relay_draw(GtkSnapshot *,double hx,double hy,double tx,double ty,double first,double last,
		const struct map_relay_motion *);
#endif
