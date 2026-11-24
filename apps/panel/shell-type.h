#ifndef MOTHERSHIP_SHELL_TYPE_H
#define MOTHERSHIP_SHELL_TYPE_H
#include <pango/pangocairo.h>

const char *shell_font_family(void);
PangoFontDescription *shell_font_description(const char *family,double size,PangoWeight weight);
#endif
