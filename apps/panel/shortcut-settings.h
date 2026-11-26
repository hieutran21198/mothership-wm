#ifndef MOTHERSHIP_SHORTCUT_SETTINGS_H
#define MOTHERSHIP_SHORTCUT_SETTINGS_H
#include <gtk/gtk.h>
GtkWidget *shortcut_settings_new(void (*close)(void));
void shortcut_settings_open(void);
void shortcut_settings_result(int save,int close,int grid,int settings,int function_row,const char *error);
void shortcut_guide(char *grid,char *settings);
#endif
