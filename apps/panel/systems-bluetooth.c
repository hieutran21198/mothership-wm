//go:build linux && cgo && panel

#include "systems-bluetooth.h"
#include <stdlib.h>
#include <string.h>

#define BLUEZ "org.bluez"
#define ADAPTER "org.bluez.Adapter1"
#define DEVICE "org.bluez.Device1"
#define AGENT_PATH "/org/mothership/systems/agent"
enum { SCAN_SECONDS=30, PROMPT_SECONDS=30, CONNECT_TIMEOUT=30000, PAIR_TIMEOUT=60000 };

static GDBusObjectManager *manager;
static GCancellable *cancel;
static void (*on_changed)(void);
static gboolean active,powering,discovering,scan_ours;
static char *adapter_path,*adapter_label;
static enum bt_state state=BT_LOADING;
static GPtrArray *paired_list,*nearby_list;
static int unnamed_count;
static GHashTable *busy,*errors,*seen;
static guint scan_timer,changed_timer,prompt_timer,agent_id;
static gint64 scan_end;
static struct bt_pairing pairing;
static GDBusMethodInvocation *prompt_call;

static void device_free(gpointer p) {
	struct bt_device *d=p;
	g_free(d->path);g_free(d->name);g_free(d->icon);g_free(d->status);g_free(d->error);g_free(d);
}
static GDBusConnection *bus(void) {
	return manager?g_dbus_object_manager_client_get_connection(G_DBUS_OBJECT_MANAGER_CLIENT(manager)):NULL;
}
static GVariant *cached(GDBusInterface *i,const char *name) {
	return i?g_dbus_proxy_get_cached_property(G_DBUS_PROXY(i),name):NULL;
}
static char *prop_text(GDBusInterface *i,const char *name) {
	g_autoptr(GVariant) v=cached(i,name);
	return v && (g_variant_is_of_type(v,G_VARIANT_TYPE_STRING) || g_variant_is_of_type(v,G_VARIANT_TYPE_OBJECT_PATH))?
		g_variant_dup_string(v,NULL):NULL;
}
static gboolean prop_flag(GDBusInterface *i,const char *name) {
	g_autoptr(GVariant) v=cached(i,name);
	return v && g_variant_is_of_type(v,G_VARIANT_TYPE_BOOLEAN) && g_variant_get_boolean(v);
}

// A soft or hard rfkill block keeps BlueZ from powering the adapter.
static gboolean rfkill_blocked(void) {
	GDir *dir=g_dir_open("/sys/class/rfkill",0,NULL);
	if(!dir) return FALSE;
	gboolean blocked=FALSE;
	for(const char *n;(n=g_dir_read_name(dir));) {
		g_autofree char *type_path=g_build_filename("/sys/class/rfkill",n,"type",NULL),*type=NULL;
		if(!g_file_get_contents(type_path,&type,NULL,NULL) || !g_str_has_prefix(type,"bluetooth")) continue;
		for(int k=0;k<2;k++) {
			g_autofree char *p=g_build_filename("/sys/class/rfkill",n,k?"hard":"soft",NULL),*v=NULL;
			if(g_file_get_contents(p,&v,NULL,NULL) && atoi(v)==1) blocked=TRUE;
		}
	}
	g_dir_close(dir);
	return blocked;
}

static int by_paired(gconstpointer a,gconstpointer b) {
	const struct bt_device *x=*(struct bt_device **)a,*y=*(struct bt_device **)b;
	if(x->connected!=y->connected) return x->connected?-1:1;
	return g_utf8_collate(x->name,y->name);
}
static int by_signal(gconstpointer a,gconstpointer b) {
	const struct bt_device *x=*(struct bt_device **)a,*y=*(struct bt_device **)b;
	return (y->has_rssi?y->rssi:-127)-(x->has_rssi?x->rssi:-127);
}

static void rebuild(void) {
	if(!paired_list) {
		paired_list=g_ptr_array_new_with_free_func(device_free);
		nearby_list=g_ptr_array_new_with_free_func(device_free);
	}
	g_ptr_array_set_size(paired_list,0);
	g_ptr_array_set_size(nearby_list,0);
	unnamed_count=0;
	g_clear_pointer(&adapter_path,g_free);
	g_clear_pointer(&adapter_label,g_free);
	if(!manager) return;
	g_autofree char *owner=g_dbus_object_manager_client_get_name_owner(G_DBUS_OBJECT_MANAGER_CLIENT(manager));
	if(!owner) {state=BT_NO_ADAPTER;return;}
	GList *objects=g_dbus_object_manager_get_objects(manager);
	gboolean powered=FALSE;
	g_autofree char *power_state=NULL;
	for(GList *l=objects;l && !adapter_path;l=l->next) {
		g_autoptr(GDBusInterface) a=g_dbus_object_get_interface(l->data,ADAPTER);
		if(!a) continue;
		adapter_path=g_strdup(g_dbus_object_get_object_path(l->data));
		adapter_label=prop_text(a,"Alias");
		if(!adapter_label) adapter_label=prop_text(a,"Name");
		powered=prop_flag(a,"Powered");
		discovering=prop_flag(a,"Discovering");
		power_state=prop_text(a,"PowerState");
	}
	if(!adapter_path) state=BT_NO_ADAPTER;
	else if(g_strcmp0(power_state,"off-blocked")==0 || (!powered && rfkill_blocked())) state=BT_BLOCKED;
	else state=powered?BT_ON:BT_OFF;
	for(GList *l=objects;l && adapter_path;l=l->next) {
		g_autoptr(GDBusInterface) i=g_dbus_object_get_interface(l->data,DEVICE);
		if(!i) continue;
		g_autofree char *owner_adapter=prop_text(i,"Adapter");
		if(g_strcmp0(owner_adapter,adapter_path)!=0) continue;
		struct bt_device *d=g_new0(struct bt_device,1);
		d->path=g_strdup(g_dbus_object_get_object_path(l->data));
		g_autofree char *real=prop_text(i,"Name");
		d->named=real!=NULL;
		d->name=prop_text(i,"Alias");
		if(!d->name) d->name=prop_text(i,"Address");
		if(!d->name) d->name=g_strdup("Device");
		d->icon=prop_text(i,"Icon");
		d->paired=prop_flag(i,"Paired");
		d->connected=prop_flag(i,"Connected");
		g_autoptr(GVariant) rssi=cached(i,"RSSI");
		d->has_rssi=rssi && g_variant_is_of_type(rssi,G_VARIANT_TYPE_INT16);
		d->rssi=d->has_rssi?g_variant_get_int16(rssi):-127;
		d->battery=-1;
		g_autoptr(GDBusInterface) b=g_dbus_object_get_interface(l->data,"org.bluez.Battery1");
		g_autoptr(GVariant) level=cached(b,"Percentage");
		if(level && g_variant_is_of_type(level,G_VARIANT_TYPE_BYTE)) d->battery=g_variant_get_byte(level);
		const char *work=g_hash_table_lookup(busy,d->path);
		d->busy=work!=NULL;
		d->error=g_strdup(g_hash_table_lookup(errors,d->path));
		d->status=work?g_strdup(work):d->connected?(d->battery>=0?g_strdup_printf("Connected · %d%%",d->battery):
			g_strdup("Connected")):g_strdup(d->paired?"Not connected":"");
		if(d->paired) {g_ptr_array_add(paired_list,d);continue;}
		if(d->has_rssi && scan_ours) g_hash_table_add(seen,g_strdup(d->path));
		if(!d->has_rssi && !g_hash_table_contains(seen,d->path)) {device_free(d);continue;}
		if(d->named) g_ptr_array_add(nearby_list,d);
		else {unnamed_count++;device_free(d);}
	}
	g_ptr_array_sort(paired_list,by_paired);
	g_ptr_array_sort(nearby_list,by_signal);
	g_list_free_full(objects,g_object_unref);
}

static gboolean emit_changed(gpointer unused) {
	(void)unused;
	changed_timer=0;
	rebuild();
	if(on_changed) on_changed();
	return G_SOURCE_REMOVE;
}
// Discovery sends bursts of property changes; coalesce them.
static void changed_soon(void) {
	if(!changed_timer) changed_timer=g_timeout_add(60,emit_changed,NULL);
}
static void any_signal(void) {changed_soon();}

static char *error_text(const char *verb,GError *e) {
	g_autofree char *remote=g_dbus_error_get_remote_error(e);
	g_dbus_error_strip_remote_error(e);
	const char *m=e->message,*why=m;
	if(strstr(m,"page-timeout") || strstr(m,"Page Timeout") || strstr(m,"Host is down") ||
		strstr(m,"not available") || strstr(m,"profile-unavailable")) why="device not reachable";
	else if(remote && g_str_has_suffix(remote,"AuthenticationRejected")) why="rejected on the device";
	else if(remote && g_str_has_suffix(remote,"AuthenticationCanceled")) why="cancelled";
	else if(remote && g_str_has_suffix(remote,"AuthenticationFailed")) why="authentication failed";
	else if(remote && g_str_has_suffix(remote,"NoReply")) why="no response";
	else if(g_error_matches(e,G_IO_ERROR,G_IO_ERROR_TIMED_OUT)) why="no response";
	return g_strdup_printf("Could not %s: %s",verb,why);
}
static gboolean cancelled(GError *e) {return g_error_matches(e,G_IO_ERROR,G_IO_ERROR_CANCELLED);}

static void call(const char *path,const char *iface,const char *method,GVariant *params,int timeout,
		GAsyncReadyCallback done,gpointer data) {
	GDBusConnection *c=bus();
	if(!c) {
		if(params) g_variant_unref(g_variant_ref_sink(params));
		if(data) g_free(data);
		return;
	}
	g_dbus_connection_call(c,BLUEZ,path,iface,method,params,NULL,G_DBUS_CALL_FLAGS_NO_AUTO_START,timeout,
		done?cancel:NULL,done,data);
}
static void set_property(const char *path,const char *iface,const char *name,GVariant *value,
		GAsyncReadyCallback done,gpointer data) {
	GDBusConnection *c=bus();
	if(!c) {g_variant_unref(g_variant_ref_sink(value));g_free(data);return;}
	g_dbus_connection_call(c,BLUEZ,path,"org.freedesktop.DBus.Properties","Set",
		g_variant_new("(ssv)",iface,name,value),NULL,G_DBUS_CALL_FLAGS_NO_AUTO_START,5000,cancel,done,data);
}

// Finishing a request: data is the device path; verb names the error.
struct request { char *path; const char *verb; int next; };
enum { NEXT_NONE, NEXT_CONNECT };
static void request_done(GObject *source,GAsyncResult *result,gpointer data) {
	struct request *r=data;
	g_autoptr(GError) e=NULL;
	g_autoptr(GVariant) reply=g_dbus_connection_call_finish(G_DBUS_CONNECTION(source),result,&e);
	if(e && cancelled(e)) {g_free(r->path);g_free(r);return;}
	g_hash_table_remove(busy,r->path);
	if(e) g_hash_table_replace(errors,g_strdup(r->path),error_text(r->verb,e));
	else g_hash_table_remove(errors,r->path);
	if(!e && r->next==NEXT_CONNECT) {
		g_hash_table_replace(busy,g_strdup(r->path),g_strdup("Connecting…"));
		struct request *n=g_new0(struct request,1);
		*n=(struct request){g_strdup(r->path),"connect",NEXT_NONE};
		call(r->path,DEVICE,"Connect",NULL,CONNECT_TIMEOUT,request_done,n);
	}
	g_free(r->path);g_free(r);
	changed_soon();
}

void bt_connect(const char *path,gboolean on) {
	if(!path || !bus() || g_hash_table_contains(busy,path)) return;
	g_hash_table_remove(errors,path);
	g_hash_table_replace(busy,g_strdup(path),g_strdup(on?"Connecting…":"Disconnecting…"));
	struct request *r=g_new0(struct request,1);
	*r=(struct request){g_strdup(path),on?"connect":"disconnect",NEXT_NONE};
	call(path,DEVICE,on?"Connect":"Disconnect",NULL,CONNECT_TIMEOUT,request_done,r);
	changed_soon();
}

static void power_done(GObject *source,GAsyncResult *result,gpointer data) {
	g_autoptr(GError) e=NULL;
	g_autoptr(GVariant) reply=g_dbus_connection_call_finish(G_DBUS_CONNECTION(source),result,&e);
	gboolean on=GPOINTER_TO_INT(data);
	if(e && cancelled(e)) return;
	powering=FALSE;
	if(e && adapter_path) g_hash_table_replace(errors,g_strdup(adapter_path),error_text(on?"turn Bluetooth on":"turn Bluetooth off",e));
	else if(adapter_path) g_hash_table_remove(errors,adapter_path);
	changed_soon();
}
void bt_set_power(gboolean on) {
	if(!adapter_path || powering || state==BT_BLOCKED) return;
	if(!on) bt_scan(FALSE);
	powering=TRUE;
	GDBusConnection *c=bus();
	if(!c) {powering=FALSE;return;}
	g_dbus_connection_call(c,BLUEZ,adapter_path,"org.freedesktop.DBus.Properties","Set",
		g_variant_new("(ssv)",ADAPTER,"Powered",g_variant_new_boolean(on)),NULL,G_DBUS_CALL_FLAGS_NO_AUTO_START,
		5000,cancel,power_done,GINT_TO_POINTER(on));
	changed_soon();
}

static gboolean scan_expired(gpointer unused) {(void)unused;scan_timer=0;bt_scan(FALSE);return G_SOURCE_REMOVE;}
static void scan_done(GObject *source,GAsyncResult *result,gpointer data) {
	(void)data;
	g_autoptr(GError) e=NULL;
	g_autoptr(GVariant) reply=g_dbus_connection_call_finish(G_DBUS_CONNECTION(source),result,&e);
	if(e && cancelled(e)) return;
	if(e && adapter_path) {
		scan_ours=FALSE;
		g_clear_handle_id(&scan_timer,g_source_remove);
		g_hash_table_replace(errors,g_strdup(adapter_path),error_text("scan",e));
	}
	changed_soon();
}
void bt_scan(gboolean on) {
	if(!adapter_path || !bus()) return;
	if(on) {
		if(scan_ours || state!=BT_ON) return;
		scan_ours=TRUE;
		g_hash_table_remove_all(seen);
		g_hash_table_remove(errors,adapter_path);
		scan_end=g_get_monotonic_time()+SCAN_SECONDS*G_USEC_PER_SEC;
		scan_timer=g_timeout_add_seconds(SCAN_SECONDS,scan_expired,NULL);
		call(adapter_path,ADAPTER,"StartDiscovery",NULL,5000,scan_done,NULL);
	} else if(scan_ours) {
		scan_ours=FALSE;
		g_clear_handle_id(&scan_timer,g_source_remove);
		call(adapter_path,ADAPTER,"StopDiscovery",NULL,5000,NULL,NULL);
	}
	changed_soon();
}
gboolean bt_scanning(void) {return scan_ours;}
int bt_scan_seconds_left(void) {
	if(!scan_ours) return 0;
	gint64 left=scan_end-g_get_monotonic_time();
	return left>0?(int)((left+G_USEC_PER_SEC-1)/G_USEC_PER_SEC):0;
}

void bt_forget(const char *path) {
	if(!adapter_path || !path) return;
	g_hash_table_replace(busy,g_strdup(path),g_strdup("Forgetting…"));
	struct request *r=g_new0(struct request,1);
	*r=(struct request){g_strdup(path),"forget",NEXT_NONE};
	call(adapter_path,ADAPTER,"RemoveDevice",g_variant_new("(o)",path),5000,request_done,r);
	changed_soon();
}
void bt_clear_error(const char *path) {if(path && errors && g_hash_table_remove(errors,path)) changed_soon();}

/* ---- pairing agent ---- */
static const char agent_xml[]=
	"<node><interface name='org.bluez.Agent1'>"
	"<method name='Release'/>"
	"<method name='RequestPinCode'><arg type='o' direction='in'/><arg type='s' direction='out'/></method>"
	"<method name='DisplayPinCode'><arg type='o' direction='in'/><arg type='s' direction='in'/></method>"
	"<method name='RequestPasskey'><arg type='o' direction='in'/><arg type='u' direction='out'/></method>"
	"<method name='DisplayPasskey'><arg type='o' direction='in'/><arg type='u' direction='in'/><arg type='q' direction='in'/></method>"
	"<method name='RequestConfirmation'><arg type='o' direction='in'/><arg type='u' direction='in'/></method>"
	"<method name='RequestAuthorization'><arg type='o' direction='in'/></method>"
	"<method name='AuthorizeService'><arg type='o' direction='in'/><arg type='s' direction='in'/></method>"
	"<method name='Cancel'/>"
	"</interface></node>";

static void reject(GDBusMethodInvocation *inv) {
	g_dbus_method_invocation_return_dbus_error(inv,"org.bluez.Error.Rejected","Rejected by the user");
}
static void prompt_clear(gboolean answer_reject) {
	g_clear_handle_id(&prompt_timer,g_source_remove);
	if(prompt_call) {
		if(answer_reject) reject(prompt_call);
		prompt_call=NULL;
	}
	pairing.deadline=0;
	memset(pairing.code,0,sizeof pairing.code);
}
static void pairing_end(void) {
	prompt_clear(TRUE);
	g_clear_pointer(&pairing.path,g_free);
	g_clear_pointer(&pairing.name,g_free);
	g_clear_pointer(&pairing.error,g_free);
	pairing.prompt=BT_PROMPT_NONE;
	GDBusConnection *c=bus();
	if(agent_id && c) {
		g_dbus_connection_call(c,BLUEZ,"/org/bluez","org.bluez.AgentManager1","UnregisterAgent",
			g_variant_new("(o)",AGENT_PATH),NULL,G_DBUS_CALL_FLAGS_NO_AUTO_START,2000,NULL,NULL,NULL);
		g_dbus_connection_unregister_object(c,agent_id);
	}
	agent_id=0;
}
static gboolean prompt_expired(gpointer unused) {
	(void)unused;
	prompt_timer=0;
	bt_pair_answer(FALSE,NULL);
	return G_SOURCE_REMOVE;
}
static void prompt_wait(GDBusMethodInvocation *inv,enum bt_prompt kind) {
	prompt_clear(TRUE);
	prompt_call=inv;
	pairing.prompt=kind;
	pairing.deadline=g_get_monotonic_time()+PROMPT_SECONDS*G_USEC_PER_SEC;
	prompt_timer=g_timeout_add_seconds(PROMPT_SECONDS,prompt_expired,NULL);
}
static void agent_call(GDBusConnection *c,const char *sender,const char *path,const char *iface,const char *method,
		GVariant *params,GDBusMethodInvocation *inv,gpointer data) {
	(void)c;(void)sender;(void)path;(void)iface;(void)data;
	if(g_str_equal(method,"Release")) {g_dbus_method_invocation_return_value(inv,NULL);return;}
	if(g_str_equal(method,"Cancel")) {
		prompt_clear(FALSE);
		if(pairing.path) pairing.prompt=BT_PROMPT_WORKING;
		g_dbus_method_invocation_return_value(inv,NULL);
		changed_soon();
		return;
	}
	const char *device=NULL;
	g_variant_get_child(params,0,"&o",&device);
	// Only the device the user asked to pair gets an answer.
	if(!pairing.path || g_strcmp0(device,pairing.path)!=0) {reject(inv);return;}
	if(g_str_equal(method,"RequestAuthorization") || g_str_equal(method,"AuthorizeService")) {
		g_dbus_method_invocation_return_value(inv,NULL);
	} else if(g_str_equal(method,"DisplayPinCode")) {
		const char *pin=NULL;
		g_variant_get(params,"(&o&s)",NULL,&pin);
		prompt_clear(TRUE);
		g_strlcpy(pairing.code,pin?pin:"",sizeof pairing.code);
		pairing.prompt=BT_PROMPT_DISPLAY;
		g_dbus_method_invocation_return_value(inv,NULL);
	} else if(g_str_equal(method,"DisplayPasskey")) {
		guint32 key=0;guint16 typed=0;
		g_variant_get(params,"(&ouq)",NULL,&key,&typed);
		prompt_clear(TRUE);
		g_snprintf(pairing.code,sizeof pairing.code,"%06u",key);
		pairing.prompt=BT_PROMPT_DISPLAY;
		g_dbus_method_invocation_return_value(inv,NULL);
	} else if(g_str_equal(method,"RequestConfirmation")) {
		guint32 key=0;
		g_variant_get(params,"(&ou)",NULL,&key);
		prompt_wait(inv,BT_PROMPT_CONFIRM);
		g_snprintf(pairing.code,sizeof pairing.code,"%06u",key);
	} else if(g_str_equal(method,"RequestPinCode")) {
		prompt_wait(inv,BT_PROMPT_PIN);
	} else if(g_str_equal(method,"RequestPasskey")) {
		prompt_wait(inv,BT_PROMPT_PASSKEY);
	} else reject(inv);
	changed_soon();
}
static const GDBusInterfaceVTable agent_vtable={agent_call,NULL,NULL,{0}};

void bt_pair_answer(gboolean accept,const char *entry) {
	if(!pairing.path) return;
	if(!prompt_call) {
		// A displayed code needs no answer; rejecting cancels the pairing.
		if(!accept) call(pairing.path,DEVICE,"CancelPairing",NULL,5000,NULL,NULL);
		return;
	}
	GDBusMethodInvocation *inv=prompt_call;
	prompt_call=NULL;
	enum bt_prompt kind=pairing.prompt;
	prompt_clear(FALSE);
	pairing.prompt=BT_PROMPT_WORKING;
	if(!accept) {reject(inv);changed_soon();return;}
	if(kind==BT_PROMPT_PIN) g_dbus_method_invocation_return_value(inv,g_variant_new("(s)",entry?entry:""));
	else if(kind==BT_PROMPT_PASSKEY) g_dbus_method_invocation_return_value(inv,g_variant_new("(u)",(guint32)strtoul(entry?entry:"0",NULL,10)));
	else g_dbus_method_invocation_return_value(inv,NULL);
	changed_soon();
}
const struct bt_pairing *bt_pairing(void) {return &pairing;}

static void pair_done(GObject *source,GAsyncResult *result,gpointer data) {
	char *path=data;
	g_autoptr(GError) e=NULL;
	g_autoptr(GVariant) reply=g_dbus_connection_call_finish(G_DBUS_CONNECTION(source),result,&e);
	if(e && cancelled(e)) {g_free(path);return;}
	g_autofree char *remote=e?g_dbus_error_get_remote_error(e):NULL;
	gboolean ok=!e || (remote && g_str_has_suffix(remote,"AlreadyExists"));
	g_hash_table_remove(busy,path);
	if(ok) {
		// Trust the new device so it can reconnect, then connect it.
		set_property(path,DEVICE,"Trusted",g_variant_new_boolean(TRUE),NULL,NULL);
		bt_connect(path,TRUE);
	} else g_hash_table_replace(errors,g_strdup(path),error_text("pair",e));
	if(g_strcmp0(pairing.path,path)==0) pairing_end();
	g_free(path);
	changed_soon();
}
static void agent_ready(GObject *source,GAsyncResult *result,gpointer data) {
	char *path=data;
	g_autoptr(GError) e=NULL;
	g_autoptr(GVariant) reply=g_dbus_connection_call_finish(G_DBUS_CONNECTION(source),result,&e);
	if(e && cancelled(e)) {g_free(path);return;}
	g_autofree char *remote=e?g_dbus_error_get_remote_error(e):NULL;
	if(e && !(remote && g_str_has_suffix(remote,"AlreadyExists"))) {
		g_hash_table_remove(busy,path);
		g_hash_table_replace(errors,g_strdup(path),error_text("pair",e));
		pairing_end();
		g_free(path);
		changed_soon();
		return;
	}
	g_dbus_connection_call(G_DBUS_CONNECTION(source),BLUEZ,path,DEVICE,"Pair",NULL,NULL,G_DBUS_CALL_FLAGS_NO_AUTO_START,
		PAIR_TIMEOUT,cancel,pair_done,path);
}
void bt_pair(const char *path) {
	GDBusConnection *c=bus();
	if(!c || !path || pairing.path || g_hash_table_contains(busy,path)) return;
	bt_scan(FALSE);
	g_autoptr(GError) e=NULL;
	g_autoptr(GDBusNodeInfo) info=g_dbus_node_info_new_for_xml(agent_xml,&e);
	if(info) agent_id=g_dbus_connection_register_object(c,AGENT_PATH,info->interfaces[0],&agent_vtable,NULL,NULL,&e);
	if(!agent_id) {
		g_hash_table_replace(errors,g_strdup(path),g_strdup("Could not pair: no pairing agent"));
		changed_soon();
		return;
	}
	pairing.path=g_strdup(path);
	for(guint i=0;i<nearby_list->len;i++) {
		struct bt_device *d=g_ptr_array_index(nearby_list,i);
		if(g_str_equal(d->path,path)) pairing.name=g_strdup(d->name);
	}
	if(!pairing.name) pairing.name=g_strdup("device");
	pairing.prompt=BT_PROMPT_WORKING;
	g_hash_table_remove(errors,path);
	g_hash_table_replace(busy,g_strdup(path),g_strdup("Pairing…"));
	g_dbus_connection_call(c,BLUEZ,"/org/bluez","org.bluez.AgentManager1","RegisterAgent",
		g_variant_new("(os)",AGENT_PATH,"DisplayYesNo"),NULL,G_DBUS_CALL_FLAGS_NO_AUTO_START,5000,cancel,agent_ready,g_strdup(path));
	changed_soon();
}
void bt_cancel(const char *path) {
	if(!path) return;
	if(g_strcmp0(pairing.path,path)==0) {call(path,DEVICE,"CancelPairing",NULL,5000,NULL,NULL);return;}
	const char *work=busy?g_hash_table_lookup(busy,path):NULL;
	if(work && g_str_has_prefix(work,"Connecting")) call(path,DEVICE,"Disconnect",NULL,5000,NULL,NULL);
}

/* ---- lifetime ---- */
static void manager_ready(GObject *source,GAsyncResult *result,gpointer data) {
	(void)source;(void)data;
	g_autoptr(GError) e=NULL;
	GDBusObjectManager *m=g_dbus_object_manager_client_new_for_bus_finish(result,&e);
	if(e && cancelled(e)) return;
	if(!active) {g_clear_object(&m);return;}
	manager=m;
	if(!manager) {state=BT_NO_ADAPTER;changed_soon();return;}
	g_signal_connect_swapped(manager,"object-added",G_CALLBACK(any_signal),NULL);
	g_signal_connect_swapped(manager,"object-removed",G_CALLBACK(any_signal),NULL);
	g_signal_connect_swapped(manager,"interface-added",G_CALLBACK(any_signal),NULL);
	g_signal_connect_swapped(manager,"interface-removed",G_CALLBACK(any_signal),NULL);
	g_signal_connect_swapped(manager,"interface-proxy-properties-changed",G_CALLBACK(any_signal),NULL);
	g_signal_connect_swapped(manager,"notify::name-owner",G_CALLBACK(any_signal),NULL);
	changed_soon();
}

void bt_start(void (*changed)(void)) {
	on_changed=changed;
	if(active) return;
	active=TRUE;
	if(!busy) {
		busy=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
		errors=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
		seen=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
	}
	state=BT_LOADING;
	cancel=g_cancellable_new();
	g_dbus_object_manager_client_new_for_bus(G_BUS_TYPE_SYSTEM,G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_DO_NOT_AUTO_START,
		BLUEZ,"/",NULL,NULL,NULL,cancel,manager_ready,NULL);
}

void bt_stop(void) {
	if(!active) return;
	active=FALSE;
	if(pairing.path) {
		call(pairing.path,DEVICE,"CancelPairing",NULL,2000,NULL,NULL);
		pairing_end();
	}
	bt_scan(FALSE);
	g_cancellable_cancel(cancel);
	g_clear_object(&cancel);
	g_clear_handle_id(&changed_timer,g_source_remove);
	if(manager) g_signal_handlers_disconnect_by_func(manager,G_CALLBACK(any_signal),NULL);
	g_clear_object(&manager);
	powering=FALSE;
	discovering=FALSE;
	g_hash_table_remove_all(busy);
	g_hash_table_remove_all(errors);
	g_hash_table_remove_all(seen);
	rebuild();
	state=BT_LOADING;
}

enum bt_state bt_state(const char **adapter) {
	if(adapter) *adapter=adapter_label;
	return state;
}
gboolean bt_powering(void) {return powering;}
const char *bt_adapter_error(void) {return adapter_path && errors?g_hash_table_lookup(errors,adapter_path):NULL;}
GPtrArray *bt_paired(void) {if(!paired_list) rebuild();return paired_list;}
GPtrArray *bt_nearby(int *unnamed) {
	if(!nearby_list) rebuild();
	if(unnamed) *unnamed=unnamed_count;
	return nearby_list;
}
