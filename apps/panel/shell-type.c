//go:build linux && cgo && panel

#include "shell-type.h"

static char *shell_family;

int panel_add_shell_font(const char *path) {
	GError *error=NULL;
	gboolean ok=pango_font_map_add_font_file(pango_cairo_font_map_get_default(),path,&error);
	if(!ok) {
		g_warning("Mothership shell typeface could not load; using Sans: %s",error?error->message:"unknown error");
		g_clear_error(&error);
	}
	return ok;
}

void panel_use_shell_family(const char *family) {
	g_free(shell_family);
	shell_family=g_strdup(family);
}

const char *shell_font_family(void) { return shell_family?shell_family:"Sans"; }

PangoFontDescription *shell_font_description(const char *family,double size,PangoWeight weight) {
	PangoFontDescription *font=pango_font_description_new();
	pango_font_description_set_family(font,family?family:shell_font_family());
	pango_font_description_set_absolute_size(font,size*PANGO_SCALE);
	pango_font_description_set_weight(font,weight);
	return font;
}
