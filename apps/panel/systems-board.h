#ifndef MOTHERSHIP_SYSTEMS_BOARD_H
#define MOTHERSHIP_SYSTEMS_BOARD_H
#include <gtk/gtk.h>

// The Systems board: Sound and Bluetooth device control, projected above
// the Systems dock button (Home) or the Volume button (Node).
enum systems_module { SYSTEMS_LAST=-1, SYSTEMS_SOUND=0, SYSTEMS_BLUETOOTH=1 };

void systems_board_init(GtkOverlay *root,void (*region)(void));
GtkWidget *systems_board_widget(void);
// Open and owning input; false while the close animation runs.
gboolean systems_board_visible(void);
void systems_board_open(enum systems_module module);
gboolean systems_board_close(void);
void systems_board_layout(int width,int height,int anchor_x,int bottom);
void systems_board_scan(void);
void systems_board_toggle_mute(void);
// Thread-safe: sound state from the Panel's pactl watcher.
void systems_board_sound_post(const char *);
#endif
