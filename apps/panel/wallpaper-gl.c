//go:build linux && cgo && panel

#include <epoxy/gl.h>
#include <gtk/gtk.h>
#include <math.h>
#include <string.h>

struct blur_cache {
	gboolean blur_attempted;
	GLuint source, blurred, blur, vao;
};

static const char vertex[] =
	"#version 300 es\n"
	"out vec2 uv;\n"
	"void main() { vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));"
	"uv=p; gl_Position=vec4(p*2.0-1.0,0,1); }\n";

static const char blur_fragment[] =
	"#version 300 es\n"
	"precision highp float; in vec2 uv; out vec4 pixel;"
	"uniform sampler2D image; uniform vec2 step_uv;\n"
	"void main(){ vec4 sum=vec4(0);"
	"for(int i=-3;i<=3;++i) sum+=texture(image,uv+float(i)*step_uv);"
	"pixel=sum/7.0; }\n";

static GLuint shader(GLenum kind, const char *text) {
	GLuint id = glCreateShader(kind);
	glShaderSource(id, 1, &text, NULL);
	glCompileShader(id);
	GLint ok;
	glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
	if (ok) return id;
	char info[512];
	glGetShaderInfoLog(id, sizeof(info), NULL, info);
	g_warning("Mothership GLES shader: %s", info);
	glDeleteShader(id);
	return 0;
}

static GLuint program(const char *fragment) {
	GLuint a = shader(GL_VERTEX_SHADER, vertex), b = shader(GL_FRAGMENT_SHADER, fragment);
	GLuint id = 0;
	if (a && b) {
		id = glCreateProgram();
		glAttachShader(id, a);
		glAttachShader(id, b);
		glLinkProgram(id);
		GLint ok;
		glGetProgramiv(id, GL_LINK_STATUS, &ok);
		if (!ok) { glDeleteProgram(id); id = 0; }
	}
	if (a) glDeleteShader(a);
	if (b) glDeleteShader(b);
	return id;
}

static GLuint texture(int w, int h, const void *data) {
	GLuint id;
	glGenTextures(1, &id);
	glBindTexture(GL_TEXTURE_2D, id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	return id;
}

static void cleanup(struct blur_cache *g) {
	glDeleteTextures(1, &g->blurred);
	glDeleteTextures(1, &g->source);
	glDeleteVertexArrays(1, &g->vao);
	glDeleteProgram(g->blur);
	g->source = g->blurred = g->blur = g->vao = 0;
	g->blur_attempted = FALSE;
}

static gboolean software(const char *renderer) {
	char *lower = g_ascii_strdown(renderer, -1);
	gboolean found = strstr(lower,"llvmpipe") || strstr(lower,"softpipe") ||
		strstr(lower,"software") || strstr(lower,"swrast") || strstr(lower,"swiftshader");
	g_free(lower);
	return found;
}

static void cache_blur(struct blur_cache *g, int iw, int ih, GLint frame, const GLint *vp) {
	if (g->blur_attempted || !g->blur) return;
	g->blur_attempted = TRUE;
	double fit = MIN(1.,768./MAX(iw,ih));
	int w=MAX(1,(int)(iw*fit+.5)), h=MAX(1,(int)(ih*fit+.5));
	GLuint fbo, scratch, result;
	glGenFramebuffers(1,&fbo);
	glActiveTexture(GL_TEXTURE0);
	scratch=texture(w,h,NULL); result=texture(w,h,NULL);
	glBindFramebuffer(GL_FRAMEBUFFER,fbo);
	glViewport(0,0,w,h);
	glUseProgram(g->blur);
	glUniform1i(glGetUniformLocation(g->blur,"image"),0);
	GLint step=glGetUniformLocation(g->blur,"step_uv");
	GLuint inputs[]={g->source,scratch}, outputs[]={scratch,result};
	gboolean ok=TRUE;
	for(int pass=0;pass<2;pass++) {
		glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,outputs[pass],0);
		if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){ok=FALSE;break;}
		glBindTexture(GL_TEXTURE_2D,inputs[pass]);
		glUniform2f(step,pass==0?1.f/w:0,pass==1?1.f/h:0);
		glDrawArrays(GL_TRIANGLES,0,3);
	}
	glBindFramebuffer(GL_FRAMEBUFFER,frame);
	glViewport(vp[0],vp[1],vp[2],vp[3]);
	glDeleteFramebuffers(1,&fbo);
	glDeleteTextures(1,&scratch);
	if(ok && glGetError()==GL_NO_ERROR) {
		g->blurred=result;
		g_message("Mothership GLES Frosted cache: %dx%d",w,h);
	} else glDeleteTextures(1,&result);
}

GdkGLContext *map_gpu_context(GtkWidget *widget,const char *part) {
	GtkNative *native=gtk_widget_get_native(widget);
	GdkSurface *surface=native?gtk_native_get_surface(native):NULL;
	if(!surface) return NULL;
	GError *error=NULL;
	GdkGLContext *ctx=gdk_surface_create_gl_context(surface,&error);
	if(ctx) {
		gdk_gl_context_set_allowed_apis(ctx,GDK_GL_API_GLES);
		gdk_gl_context_set_required_version(ctx,3,0);
		if(gdk_gl_context_realize(ctx,&error)) {
			gdk_gl_context_make_current(ctx);
			const char *renderer=(const char *)glGetString(GL_RENDERER);
			gboolean ok=gdk_gl_context_get_api(ctx)==GDK_GL_API_GLES&&renderer&&!software(renderer);
			if(ok)g_message("Mothership GLES %s renderer: %s",part,renderer);
			gdk_gl_context_clear_current();
			if(ok)return ctx;
		}
		g_object_unref(ctx);
	}
	g_clear_error(&error);
	return NULL;
}

struct release_texture { GdkGLContext *context; GLuint id; };

static gboolean release_gpu_texture(gpointer data) {
	struct release_texture *item=data;
	GdkGLContext *old=gdk_gl_context_get_current();
	if(old)g_object_ref(old);
	gdk_gl_context_make_current(item->context);
	glDeleteTextures(1,&item->id);
	if(old) {gdk_gl_context_make_current(old);g_object_unref(old);}
	else gdk_gl_context_clear_current();
	g_object_unref(item->context);g_free(item);
	return G_SOURCE_REMOVE;
}

static void destroy_gpu_texture(gpointer data) {
	g_main_context_invoke(NULL,release_gpu_texture,data);
}

GdkTexture *map_gl_frost_texture(GdkGLContext *context,cairo_surface_t *image) {
	GdkGLContext *old=gdk_gl_context_get_current();
	if(old)g_object_ref(old);
	gdk_gl_context_make_current(context);
	struct blur_cache g={0};
	g.blur=program(blur_fragment);
	glGenVertexArrays(1,&g.vao);
	glBindVertexArray(g.vao);
	glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_SCISSOR_TEST);
	cairo_surface_flush(image);
	int iw=cairo_image_surface_get_width(image),ih=cairo_image_surface_get_height(image);
	glActiveTexture(GL_TEXTURE0);
	glPixelStorei(GL_UNPACK_ROW_LENGTH,cairo_image_surface_get_stride(image)/4);
	g.source=texture(iw,ih,cairo_image_surface_get_data(image));
	glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_SWIZZLE_R,GL_BLUE);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_SWIZZLE_B,GL_RED);
#else
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_SWIZZLE_R,GL_GREEN);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_SWIZZLE_G,GL_BLUE);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_SWIZZLE_B,GL_ALPHA);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_SWIZZLE_A,GL_RED);
#endif
	if(cairo_image_surface_get_format(image)==CAIRO_FORMAT_RGB24)glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_SWIZZLE_A,GL_ONE);
	GLint vp[4];glGetIntegerv(GL_VIEWPORT,vp);
	cache_blur(&g,iw,ih,0,vp);
	GdkTexture *result=NULL;
	if(g.blurred) {
		double fit=MIN(1.,768./MAX(iw,ih));
		int w=MAX(1,(int)(iw*fit+.5)), h=MAX(1,(int)(ih*fit+.5));
		glFinish(); /* One-time texture handoff to the shared GDK renderer. */
		GdkGLTextureBuilder *builder=gdk_gl_texture_builder_new();
		gdk_gl_texture_builder_set_context(builder,context);
		gdk_gl_texture_builder_set_id(builder,g.blurred);
		gdk_gl_texture_builder_set_width(builder,w);
		gdk_gl_texture_builder_set_height(builder,h);
		gdk_gl_texture_builder_set_format(builder,GDK_MEMORY_R8G8B8A8_PREMULTIPLIED);
		struct release_texture *item=g_new(struct release_texture,1);
		*item=(struct release_texture){g_object_ref(context),g.blurred};
		result=gdk_gl_texture_builder_build(builder,destroy_gpu_texture,item);
		if(result)g.blurred=0;
		else {g_object_unref(item->context);g_free(item);}
		g_object_unref(builder);
	}
	cleanup(&g);
	if(old){gdk_gl_context_make_current(old);g_object_unref(old);}
	else gdk_gl_context_clear_current();
	return result;
}
