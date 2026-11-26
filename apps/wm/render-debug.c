//go:build linux && cgo && wlroots

#define _GNU_SOURCE
#include "render-debug.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_compositor.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum { CAPTURE_MS=10000, MAX_ROWS=16384, MAX_REGIONS=8, MANUAL_LIMIT_MS=60000,
	GPU_SAMPLE_STRIDE=4, GPU_POLL_MS=50, GPU_POLL_LIMIT=3, GPU_PROBE_LIMIT_NS=2000000 };
enum buffer_origin { BUFFER_NONE,BUFFER_SHM,BUFFER_DMABUF,BUFFER_UNKNOWN };
enum probe_state { PROBE_NONE,PROBE_READY,PROBE_UNAVAILABLE,PROBE_NOT_READY,
	PROBE_INVALID_CLOCK,PROBE_OVER_BUDGET };
struct sample {
	int64_t ns, prepare_ns, submit_ns, damage_px, declared_surface_px, declared_buffer_px;
	int64_t scene_pre_render_ns,render_pass_ns,gpu_probe_ns;
	uint64_t window;
	uint32_t key_events,focus_changes,configures;
	enum buffer_origin buffer;
	enum probe_state gpu_status;
	uint32_t seq, flags, sources;
	pixman_box32_t regions[MAX_REGIONS];
	int kind, frost, width,height, x1,y1,x2,y2,rects, refresh_ns;
};
struct render_debug {
	struct wl_event_source *watch,*timer,*probe;
	struct wl_listener precommit,commit,present,output_destroy;
	struct wlr_output *out;
	struct sample *rows;
	const char *backend;
	char *fifo;
	int fd;
	unsigned len,dropped,sources;
	bool manual,gpu,probe_gles2,probe_armed,probe_submitting,probe_disabled;
	uint32_t key_events,focus_changes,configures;
	uint32_t gpu_seq,gpu_sources;
	unsigned gpu_frames,gpu_attempts,gpu_unready_spans;
	int64_t gpu_probe_ns,gpu_started;
	struct wlr_scene_timer scene_timer;
	struct wlr_scene_output_state_options scene_options;
	int64_t started;
	float scale;
};

static int64_t ns(struct timespec t) {return t.tv_sec*1000000000LL+t.tv_nsec;}
static int64_t monotonic(void) {
	struct timespec t={0};
	clock_gettime(CLOCK_MONOTONIC,&t);
	return ns(t);
}

bool render_debug_active(struct render_debug *d) { return d && d->rows; }

void render_debug_source(struct render_debug *d,unsigned source) {
	if(render_debug_active(d)) d->sources|=source;
}

void render_debug_activity(struct render_debug *d,enum render_activity activity) {
	if (!render_debug_active(d)) return;
	switch(activity) {
	case RENDER_KEY: d->key_events++; break;
	case RENDER_FOCUS: d->focus_changes++; break;
	case RENDER_CONFIGURE: d->configures++; break;
	}
}

static struct sample *record(struct render_debug *d,int kind,int64_t t,uint32_t seq) {
	if(!render_debug_active(d))return NULL;
	if(d->len==MAX_ROWS){d->dropped++;return NULL;}
	struct sample *s=&d->rows[d->len++];
	*s=(struct sample){.kind=kind,.ns=t,.seq=seq,.damage_px=-1,.sources=d->sources,
		.scene_pre_render_ns=-1,.render_pass_ns=-1};
	return s;
}

static int64_t region_area(const pixman_region32_t *region) {
	int count=0;
	const pixman_box32_t *rects=pixman_region32_rectangles(region,&count);
	int64_t pixels=0;
	for(int i=0;i<count;i++)
		pixels+=(int64_t)(rects[i].x2-rects[i].x1)*(rects[i].y2-rects[i].y1);
	return pixels;
}

static void fill_damage(struct sample *s,const pixman_region32_t *region) {
	const pixman_box32_t *bounds=pixman_region32_extents(region);
	s->x1=bounds->x1;s->y1=bounds->y1;s->x2=bounds->x2;s->y2=bounds->y2;
	const pixman_box32_t *rects=pixman_region32_rectangles(region,&s->rects);
	for(int i=0;i<s->rects && i<MAX_REGIONS;i++)s->regions[i]=rects[i];
	s->damage_px=region_area(region);
}

// Inspect protocol buffer metadata only. Never map or read its storage.
static enum buffer_origin buffer_origin(struct wlr_buffer *buffer) {
	if (!buffer) return BUFFER_NONE;
	struct wlr_dmabuf_attributes dma;
	if (wlr_buffer_get_dmabuf(buffer,&dma)) return BUFFER_DMABUF;
	struct wlr_shm_attributes shm;
	if (wlr_buffer_get_shm(buffer,&shm)) return BUFFER_SHM;
	return BUFFER_UNKNOWN;
}

void render_debug_surface(struct render_debug *d,unsigned source,uint64_t window,
		struct wlr_surface *surface) {
	if(!render_debug_active(d))return;
	render_debug_source(d,source);
	struct sample *s=record(d,4,monotonic(),0);
	if(!s)return;
	s->sources=source;s->window=window;
	s->buffer=surface->buffer?buffer_origin(surface->buffer->source):BUFFER_NONE;
	if (surface->buffer && !surface->buffer->source) s->buffer=BUFFER_UNKNOWN;
	s->width=surface->current.width;s->height=surface->current.height;
	s->declared_surface_px=region_area(&surface->current.surface_damage);
	s->declared_buffer_px=region_area(&surface->current.buffer_damage);
	pixman_region32_t effective;
	pixman_region32_init(&effective);
	wlr_surface_get_effective_damage(surface,&effective);
	fill_damage(s,&effective);
	pixman_region32_fini(&effective);
}

static void on_commit(struct wl_listener *l,void *ptr) {
	struct render_debug *d=wl_container_of(l,d,commit);
	struct wlr_output_event_commit *e=ptr;
	if(!render_debug_active(d) || !(e->state->committed&WLR_OUTPUT_STATE_BUFFER) || !e->state->buffer)return;
	struct sample *s=record(d,1,monotonic(),d->out->commit_seq);
	if(!s)return;
	s->buffer=buffer_origin(e->state->buffer);
	s->width=e->state->buffer->width;s->height=e->state->buffer->height;
	if(!(e->state->committed&WLR_OUTPUT_STATE_DAMAGE))return;
	fill_damage(s,&e->state->damage);
}

static void on_precommit(struct wl_listener *l,void *ptr) {
	struct render_debug *d=wl_container_of(l,d,precommit);
	struct wlr_output_event_precommit *e=ptr;
	if(!render_debug_active(d) || !(e->state->committed&WLR_OUTPUT_STATE_BUFFER) || !e->state->buffer)return;
	record(d,5,monotonic(),d->out->commit_seq+1);
}

static void on_present(struct wl_listener *l,void *ptr) {
	struct render_debug *d=wl_container_of(l,d,present);
	struct wlr_output_event_present *e=ptr;
	if(!render_debug_active(d))return;
	struct sample *s=record(d,e->presented?2:3,e->presented?ns(e->when):0,e->commit_seq);
	if(s){s->flags=e->flags;s->refresh_ns=e->refresh;s->sources=0;}
}

void render_debug_frame(struct render_debug *d,struct timespec begin,struct timespec submit,unsigned frost) {
	if(!render_debug_active(d))return;
	struct sample *s=record(d,0,ns(begin),d->out->commit_seq);
	if(s){
		s->prepare_ns=ns(submit)-ns(begin);s->submit_ns=monotonic()-ns(submit);s->frost=frost;
		s->key_events=d->key_events;s->focus_changes=d->focus_changes;s->configures=d->configures;
	}
	d->sources=0;
	d->key_events=d->focus_changes=d->configures=0;
}

static void stop_probe(struct render_debug *d) {
	if (d->probe) wl_event_source_timer_update(d->probe,0);
	wlr_scene_timer_finish(&d->scene_timer);
	d->scene_timer=(struct wlr_scene_timer){0};
	d->probe_armed=d->probe_submitting=false;
}

static void record_probe(struct render_debug *d,int64_t pass_ns,enum probe_state status) {
	struct sample *s=record(d,6,monotonic(),d->gpu_seq);
	if (s) {
		s->sources=d->gpu_sources;
		if (d->probe_gles2) s->scene_pre_render_ns=d->scene_timer.pre_render_duration;
		s->render_pass_ns=pass_ns;
		s->gpu_probe_ns=d->gpu_probe_ns;
		s->gpu_status=status;
	}
	stop_probe(d);
}

static int poll_probe(void *ptr) {
	struct render_debug *d=ptr;
	if (!d->probe_armed || !render_debug_active(d)) return 0;
	// Pinned GLES2 implementation checks disjoint and result availability
	// before fetching. Wait via the event loop, never glFinish or a fence wait.
	int64_t before=monotonic();
	int64_t pass=wlr_render_timer_get_duration_ns(d->scene_timer.render_timer);
	int64_t after=monotonic();
	d->gpu_probe_ns+=after-before;
	// Some drivers expose the extension but return incomparable clocks.
	// A completed pass cannot be longer than submit to result availability.
	if (pass>after-d->gpu_started) {
		d->probe_disabled=true;
		record_probe(d,-1,PROBE_INVALID_CLOCK);
	} else if (d->gpu_probe_ns>GPU_PROBE_LIMIT_NS) {
		d->probe_disabled=true;
		record_probe(d,-1,PROBE_OVER_BUDGET);
	} else if (pass>=0) {
		d->gpu_unready_spans=0;
		record_probe(d,pass,PROBE_READY);
	} else if (++d->gpu_attempts>=GPU_POLL_LIMIT ||
			wl_event_source_timer_update(d->probe,GPU_POLL_MS)<0) {
		if (++d->gpu_unready_spans>=GPU_POLL_LIMIT) d->probe_disabled=true;
		record_probe(d,-1,PROBE_NOT_READY);
	}
	return 0;
}

const struct wlr_scene_output_state_options *render_debug_scene_begin(struct render_debug *d,
		struct wlr_scene_output *output) {
	if (!render_debug_active(d) || !d->gpu || d->probe_disabled || d->probe_armed ||
			!wlr_scene_output_needs_frame(output) || d->gpu_frames++%GPU_SAMPLE_STRIDE)
		return NULL;
	d->probe_armed=d->probe_submitting=true;
	d->gpu_sources=d->sources;
	d->gpu_attempts=0;
	d->gpu_probe_ns=0;
	d->gpu_started=monotonic();
	return d->probe_gles2?&d->scene_options:NULL;
}

void render_debug_scene_end(struct render_debug *d) {
	if (!render_debug_active(d) || !d->probe_submitting) return;
	d->probe_submitting=false;
	d->gpu_seq=d->out->commit_seq;
	if (!d->scene_timer.render_timer) {
		if (!d->probe_gles2 || ++d->gpu_unready_spans>=GPU_POLL_LIMIT) d->probe_disabled=true;
		record_probe(d,-1,PROBE_UNAVAILABLE);
	} else if (wl_event_source_timer_update(d->probe,GPU_POLL_MS)<0) {
		record_probe(d,-1,PROBE_NOT_READY);
	}
}

static bool make_dirs(char *path) {
	for(char *p=path+1;*p;p++) {
		if(*p!='/')continue;
		*p='\0';
		int result=mkdir(path,0700);
		*p='/';
		if(result<0 && errno!=EEXIST)return false;
	}
	return mkdir(path,0700)==0 || errno==EEXIST;
}

static char *manual_dir(void) {
	const char *state=getenv("XDG_STATE_HOME"),*home=getenv("HOME");
	char *path=NULL;
	if(state && state[0]=='/') {
		if(asprintf(&path,"%s/mothership-wm/render-debug",state)<0)return NULL;
	} else if(home && home[0]=='/') {
		if(asprintf(&path,"%s/.local/state/mothership-wm/render-debug",home)<0)return NULL;
	}
	if(path && !make_dirs(path)){free(path);return NULL;}
	return path;
}

static int finish(void *ptr) {
	struct render_debug *d=ptr;
	if(!render_debug_active(d))return 0;
	wl_event_source_timer_update(d->timer,0);
	if (d->probe_armed && !d->probe_submitting) record_probe(d,-1,PROBE_NOT_READY);
	else stop_probe(d);
	char *owned=d->manual?manual_dir():NULL;
	const char *run=d->manual?owned:getenv("XDG_RUNTIME_DIR");
	char *part=NULL,*path=NULL;
	FILE *file=NULL;
	bool saved=false;
	if(run && asprintf(&part,"%s/render-debug-XXXXXX",run)>=0) {
		int fd=mkstemp(part);
		if(fd>=0) {
			file=fdopen(fd,"w");
			if(!file)close(fd);
			if(file) {
				static const char *kinds[]={"frame","commit","present","discard","surface","precommit","gpu"};
				fprintf(file,"# backend=%s scale=%.3f capture_ms=%" PRId64 " dropped=%u gpu=%u\n",
					d->backend,d->scale,(monotonic()-d->started)/1000000,d->dropped,(unsigned)d->gpu);
				fputs("event,ns,seq,flags,sources,frost,prepare_ns,submit_ns,damage_px,rects,x1,y1,x2,y2,width,height,refresh_ns,window,declared_surface_px,declared_buffer_px,damage_regions,buffer_origin,key_events,focus_changes,configures,scene_pre_render_ns,render_pass_ns,gpu_probe_ns,gpu_status\n",file);
				for(unsigned i=0;i<d->len;i++) {
					struct sample s=d->rows[i];
					fprintf(file,"%s,%" PRId64 ",%u,%u,%u,%d,%" PRId64 ",%" PRId64 ",%" PRId64 ",%d,%d,%d,%d,%d,%d,%d,%d,%" PRIu64 ",%" PRId64 ",%" PRId64 ",",
						kinds[s.kind],s.ns,s.seq,s.flags,s.sources,s.frost,
						s.prepare_ns,s.submit_ns,s.damage_px,s.rects,s.x1,s.y1,s.x2,s.y2,s.width,s.height,s.refresh_ns,s.window,s.declared_surface_px,s.declared_buffer_px);
					for(int j=0;j<s.rects && j<MAX_REGIONS;j++) {
						pixman_box32_t b=s.regions[j];
						fprintf(file,"%s%d:%d:%d:%d",j?"|":"",b.x1,b.y1,b.x2,b.y2);
					}
					fprintf(file,",%u,%u,%u,%u,%" PRId64 ",%" PRId64 ",%" PRId64 ",%u\n",
						(unsigned)s.buffer,s.key_events,s.focus_changes,s.configures,
						s.scene_pre_render_ns,s.render_pass_ns,s.gpu_probe_ns,(unsigned)s.gpu_status);
				}
				bool ok=!ferror(file);
				if(fclose(file)!=0)ok=false;
				saved=ok && asprintf(&path,"%s.csv",part)>=0 && rename(part,path)==0;
			}
			if(!saved)unlink(part);
		}
	}
	if(saved)fprintf(stderr,"mothership render debug: %s\n",path);
	else fputs("mothership render debug: could not save capture\n",stderr);
	free(owned);free(part);free(path);
	free(d->rows);d->rows=NULL;
	return 0;
}

static bool start(struct render_debug *d,bool manual,bool gpu) {
	if(!d || !d->out || d->rows)return false;
	d->manual=manual;
	d->gpu=gpu && d->probe;
	d->rows=calloc(MAX_ROWS,sizeof(*d->rows));
	if(!d->rows)return false;
	d->len=d->dropped=d->sources=d->gpu_frames=d->gpu_unready_spans=0;
	d->probe_disabled=false;
	d->key_events=d->focus_changes=d->configures=0;
	d->started=monotonic();
	d->scale=d->out->scale;
	if(wl_event_source_timer_update(d->timer,manual?MANUAL_LIMIT_MS:CAPTURE_MS)<0) {
		free(d->rows);d->rows=NULL;
		return false;
	}
	return true;
}

bool render_debug_toggle(struct render_debug *d) {
	if(!d || !d->out)return false;
	if(d->rows) {
		if(d->manual)finish(d);
		// Do not interrupt an independently running terminal capture.
	} else if(!start(d,true,false)) {
		fputs("mothership render debug: could not start capture\n",stderr);
	}
	return true;
}

static int on_signal_pipe(int fd,uint32_t mask,void *ptr) {
	(void)mask;
	char bytes[64];
	ssize_t len=read(fd,bytes,sizeof(bytes));
	if (len>0)
		start(ptr,false,len==12 && memcmp(bytes,"capture gpu\n",12)==0);
	return 0;
}

static void on_output_destroy(struct wl_listener *l,void *ptr) {
	(void)ptr;
	struct render_debug *d=wl_container_of(l,d,output_destroy);
	if (d->probe_armed) stop_probe(d);
	wl_list_remove(&d->precommit.link);
	wl_list_remove(&d->commit.link);
	wl_list_remove(&d->present.link);
	wl_list_remove(&d->output_destroy.link);
	d->out=NULL;
}

struct render_debug *render_debug_init(struct wl_event_loop *loop,
		struct wlr_output *out,const char *backend,bool probe_gles2) {
	struct render_debug *d=calloc(1,sizeof(*d));
	if(!d)return NULL;
	d->backend=backend;
	d->probe_gles2=probe_gles2;
	d->fd=-1;
	const char *run=getenv("XDG_RUNTIME_DIR");
	struct stat st;
	if(!run || run[0]!='/' || stat(run,&st)<0 || !S_ISDIR(st.st_mode) ||
			st.st_uid!=geteuid() || (st.st_mode&0077) ||
			asprintf(&d->fifo,"%s/mothership-render-debug-%ld.fifo",run,(long)getpid())<0) {
		free(d);return NULL;
	}
	if(mkfifo(d->fifo,0600)<0) {free(d->fifo);free(d);return NULL;}
	d->fd=open(d->fifo,O_RDWR|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);
	if(d->fd<0){render_debug_destroy(d);return NULL;}
	d->timer=wl_event_loop_add_timer(loop,finish,d);
	d->watch=wl_event_loop_add_fd(loop,d->fd,WL_EVENT_READABLE,on_signal_pipe,d);
	d->probe=wl_event_loop_add_timer(loop,poll_probe,d);
	d->scene_options.timer=&d->scene_timer;
	if(!d->timer || !d->watch){render_debug_destroy(d);return NULL;}
	d->out=out;
	d->precommit.notify=on_precommit;
	d->commit.notify=on_commit;
	d->present.notify=on_present;
	d->output_destroy.notify=on_output_destroy;
	wl_signal_add(&out->events.precommit,&d->precommit);
	wl_signal_add(&out->events.commit,&d->commit);
	wl_signal_add(&out->events.present,&d->present);
	wl_signal_add(&out->events.destroy,&d->output_destroy);
	return d;
}

void render_debug_destroy(struct render_debug *d) {
	if(!d)return;
	if(d->manual && d->rows)finish(d);
	if(d->out)on_output_destroy(&d->output_destroy,NULL);
	if(d->watch)wl_event_source_remove(d->watch);
	if(d->timer)wl_event_source_remove(d->timer);
	if(d->probe)wl_event_source_remove(d->probe);
	if(d->fd>=0)close(d->fd);
	if(d->fifo){unlink(d->fifo);free(d->fifo);}
	free(d->rows);
	free(d);
}
