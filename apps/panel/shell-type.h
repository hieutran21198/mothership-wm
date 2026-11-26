#ifndef MOTHERSHIP_SHELL_TYPE_H
#define MOTHERSHIP_SHELL_TYPE_H
#include <pango/pangocairo.h>

#define SHELL_DISPLAY_FONT "Black Ops One"
#define SHELL_DISPLAY_FALLBACK SHELL_DISPLAY_FONT ", Chakra Petch, Sans"

const char *shell_font_family(void);
PangoFontDescription *shell_font_description(const char *family,double size,PangoWeight weight);
#endif
