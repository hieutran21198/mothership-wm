//go:build linux && cgo && panel

#include <gio/gio.h>
#include <unistd.h>

extern void panelAppNotification(unsigned int,int,char *,char *,char *);
extern void panelAppNotificationEnded(unsigned int);

static const char interface[]="org.freedesktop.Notifications";
static const char object[]="/org/freedesktop/Notifications";
static GDBusConnection *bus;
static GDBusNodeInfo *definition;
static guint registration;
static guint32 next_id;
static GHashTable *messages;

static const char xml[]="<node><interface name='org.freedesktop.Notifications'>"
	"<method name='GetCapabilities'><arg type='as' direction='out'/></method>"
	"<method name='GetServerInformation'><arg type='s' direction='out'/><arg type='s' direction='out'/>"
	"<arg type='s' direction='out'/><arg type='s' direction='out'/></method>"
	"<method name='Notify'><arg type='s' direction='in'/><arg type='u' direction='in'/>"
	"<arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/>"
	"<arg type='as' direction='in'/><arg type='a{sv}' direction='in'/><arg type='i' direction='in'/>"
	"<arg type='u' direction='out'/></method>"
	"<method name='CloseNotification'><arg type='u' direction='in'/></method>"
	"<signal name='NotificationClosed'><arg type='u'/><arg type='u'/></signal>"
	"</interface></node>";

struct live_notice { char *sender; guint timer; guint32 id; };
static void forget_notice(gpointer data) {
	struct live_notice *item=data;
	g_clear_handle_id(&item->timer,g_source_remove);
	g_free(item->sender);g_free(item);
}
static void end_notice(struct live_notice *item,guint32 reason) {
	g_dbus_connection_emit_signal(bus,item->sender,object,interface,"NotificationClosed",
		g_variant_new("(uu)",item->id,reason),NULL);
	panelAppNotificationEnded(item->id);
	g_hash_table_remove(messages,GUINT_TO_POINTER(item->id));
}
static gboolean expiration(gpointer data) {
	struct live_notice *item=data;
	item->timer=0;end_notice(item,1);
	return G_SOURCE_REMOVE;
}
static void route_pid(GDBusConnection *connection,const char *who,int *pid) {
	// A short-lived sender can disconnect before credentials are read. The
	// correct fallback is an unattributed entry, never the focused Node.
	g_autoptr(GVariant) result=g_dbus_connection_call_sync(connection,"org.freedesktop.DBus",
		"/org/freedesktop/DBus","org.freedesktop.DBus","GetConnectionCredentials",
		g_variant_new("(s)",who),G_VARIANT_TYPE("(a{sv})"),G_DBUS_CALL_FLAGS_NONE,300,NULL,NULL);
	if(!result) return;
	g_autoptr(GVariant) p=g_variant_get_child_value(result,0);
	guint32 uid=0,process=0;
	if(g_variant_lookup(p,"UnixUserID","u",&uid) && uid==getuid() &&
		g_variant_lookup(p,"ProcessID","u",&process) && process<=G_MAXINT) *pid=(int)process;
}
static void method(GDBusConnection *connection,const char *sender,const char *path,const char *iface,
	const char *name,GVariant *args,GDBusMethodInvocation *call,gpointer data) {
	(void)path;(void)iface;(void)data;
	if(g_str_equal(name,"GetCapabilities")) {
		const char *caps[]={"body",NULL};
		g_dbus_method_invocation_return_value(call,g_variant_new("(^as)",caps));
	} else if(g_str_equal(name,"GetServerInformation")) {
		g_dbus_method_invocation_return_value(call,g_variant_new("(ssss)","Mothership","Mothership","1","1.3"));
	} else if(g_str_equal(name,"CloseNotification")) {
		guint32 id;g_variant_get(args,"(u)",&id);
		struct live_notice *item=g_hash_table_lookup(messages,GUINT_TO_POINTER(id));
		if(item && g_str_equal(item->sender,sender)) end_notice(item,3);
		g_dbus_method_invocation_return_value(call,NULL);
	} else if(g_str_equal(name,"Notify")) {
		const char *app,*icon,*title,*body;
		g_autoptr(GVariant) actions=NULL,hints=NULL;
		guint32 id;int ms,pid=0;
		g_variant_get(args,"(&su&s&s&s@as@a{sv}i)",&app,&id,&icon,&title,&body,&actions,&hints,&ms);
		struct live_notice *item=g_hash_table_lookup(messages,GUINT_TO_POINTER(id));
		gboolean replace=item && g_str_equal(item->sender,sender);
		if(!replace) {
			if(g_hash_table_size(messages)>=256) {
				g_dbus_method_invocation_return_dbus_error(call,"org.freedesktop.Notifications.Error.LimitExceeded","Too many active notifications");
				return;
			}
			do { id=++next_id; } while(!id || g_hash_table_contains(messages,GUINT_TO_POINTER(id)));
			item=g_new0(struct live_notice,1);item->id=id;item->sender=g_strdup(sender);
			g_hash_table_insert(messages,GUINT_TO_POINTER(id),item);
		}
		g_clear_handle_id(&item->timer,g_source_remove);
		// The shell displays a passive preview. Bound live IDs independently
		// of the retained history; a client can always send a newer update.
		item->timer=g_timeout_add(CLAMP(ms>0?ms:5000,100,60000),expiration,item);
		route_pid(connection,sender,&pid);
		panelAppNotification(id,pid,(char *)app,(char *)title,(char *)body);
		g_dbus_method_invocation_return_value(call,g_variant_new("(u)",id));
	}
}
static const GDBusInterfaceVTable callbacks={.method_call=method};

void notice_disconnect_bus(void) {
	g_clear_pointer(&messages,g_hash_table_unref);
	if(bus && registration) g_dbus_connection_unregister_object(bus,registration);
	registration=0;
	g_clear_pointer(&definition,g_dbus_node_info_unref);
	if(bus) g_dbus_connection_close_sync(bus,NULL,NULL);
	g_clear_object(&bus);
}

int notice_connect_bus(const char *address) {
	g_autoptr(GVariant) name=NULL;
	bus=g_dbus_connection_new_for_address_sync(address,G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT|
		G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,NULL,NULL,NULL);
	if(!bus) return 0;
	g_dbus_connection_set_exit_on_close(bus,FALSE);
	definition=g_dbus_node_info_new_for_xml(xml,NULL);
	if(!definition) goto fail;
	registration=g_dbus_connection_register_object(bus,object,definition->interfaces[0],&callbacks,NULL,NULL,NULL);
	if(!registration) goto fail;
	name=g_dbus_connection_call_sync(bus,"org.freedesktop.DBus",
		"/org/freedesktop/DBus","org.freedesktop.DBus","RequestName",g_variant_new("(su)",interface,4U),
		G_VARIANT_TYPE("(u)"),G_DBUS_CALL_FLAGS_NONE,1000,NULL,NULL);
	guint32 result=0;
	if(name) g_variant_get(name,"(u)",&result);
	if(result!=1) goto fail;
	messages=g_hash_table_new_full(g_direct_hash,g_direct_equal,NULL,forget_notice);
	return 1;
fail:
	notice_disconnect_bus();
	return 0;
}
