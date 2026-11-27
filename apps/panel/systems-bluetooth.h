#ifndef MOTHERSHIP_SYSTEMS_BLUETOOTH_H
#define MOTHERSHIP_SYSTEMS_BLUETOOTH_H
#include <gio/gio.h>

// BlueZ for the Systems board. The client exists only while the board is
// open or a pairing is in progress; bt_stop() drops discovery, the agent and
// the bus watch.
enum bt_state { BT_LOADING, BT_NO_ADAPTER, BT_BLOCKED, BT_OFF, BT_ON };
enum bt_prompt { BT_PROMPT_NONE, BT_PROMPT_WORKING, BT_PROMPT_CONFIRM, BT_PROMPT_DISPLAY, BT_PROMPT_PIN, BT_PROMPT_PASSKEY };

struct bt_device {
	char *path,*name,*icon,*status,*error;
	gboolean paired,connected,named,busy,has_rssi;
	int battery,rssi; // battery -1 when not reported
};

struct bt_pairing {
	enum bt_prompt prompt;
	char *path,*name,*error;
	char code[16];
	gint64 deadline; // monotonic µs, 0 without a timeout
};

void bt_start(void (*changed)(void));
void bt_stop(void);
enum bt_state bt_state(const char **adapter);
gboolean bt_powering(void);
const char *bt_adapter_error(void);
// Paired devices, connected first, then nearby unpaired devices with a
// signal reading, strongest first. Valid until the next change callback.
GPtrArray *bt_paired(void);
GPtrArray *bt_nearby(int *unnamed);
gboolean bt_scanning(void);
int bt_scan_seconds_left(void);
void bt_set_power(gboolean on);
void bt_scan(gboolean on);
void bt_connect(const char *path,gboolean on);
void bt_cancel(const char *path);
void bt_pair(const char *path);
void bt_forget(const char *path);
void bt_clear_error(const char *path);
const struct bt_pairing *bt_pairing(void);
// Accept or reject the current prompt; entry carries a typed PIN or passkey.
void bt_pair_answer(gboolean accept,const char *entry);
#endif
