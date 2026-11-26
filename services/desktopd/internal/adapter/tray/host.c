//go:build linux && cgo && tray

#include <gio/gio.h>
#include <string.h>

#define WATCH_PATH "/StatusNotifierWatcher"
#define KDE_WATCH "org.kde.StatusNotifierWatcher"
#define FDO_WATCH "org.freedesktop.StatusNotifierWatcher"
#define KDE_ITEM "org.kde.StatusNotifierItem"
#define FDO_ITEM "org.freedesktop.StatusNotifierItem"
#define KDE_HOST "org.kde.StatusNotifierHost-Mothership"
#define DESK "org.mothership.SystemTray1"
#define DESK_PATH "/org/mothership/SystemTray"
#define MENU "com.canonical.dbusmenu"
#define DBUS "org.freedesktop.DBus"
#define DBUS_PATH "/org/freedesktop/DBus"
#define MAX_ITEMS 48
#define MAX_ICON 128
#define TIMEOUT 800

#define WATCH_XML(NAME) "<interface name='" NAME "'>" \
	"<method name='RegisterStatusNotifierItem'><arg type='s' direction='in'/></method>" \
	"<method name='RegisterStatusNotifierHost'><arg type='s' direction='in'/></method>" \
	"<property name='RegisteredStatusNotifierItems' type='as' access='read'/>" \
	"<property name='IsStatusNotifierHostRegistered' type='b' access='read'/>" \
	"<property name='ProtocolVersion' type='i' access='read'/>" \
	"<signal name='StatusNotifierItemRegistered'><arg type='s'/></signal>" \
	"<signal name='StatusNotifierItemUnregistered'><arg type='s'/></signal>" \
	"<signal name='StatusNotifierHostRegistered'/>" \
	"<signal name='StatusNotifierHostUnregistered'/></interface>"

static const char xml[]="<node>" WATCH_XML(KDE_WATCH) WATCH_XML(FDO_WATCH)
	"<interface name='" DESK "'>"
	"<method name='GetItems'><arg type='a(ssssbiiay)' direction='out'/></method>"
	"<method name='Open'><arg type='s' direction='in'/><arg type='b' direction='in'/>"
	"<arg type='i' direction='in'/><arg type='i' direction='in'/><arg type='b' direction='out'/></method>"
	"<method name='GetMenu'><arg type='s' direction='in'/><arg type='i' direction='in'/>"
	"<arg type='b' direction='in'/><arg type='a(isbbsi)' direction='out'/></method>"
	"<method name='Choose'><arg type='s' direction='in'/><arg type='i' direction='in'/>"
	"<arg type='u' direction='in'/></method>"
	"<signal name='Changed'/><signal name='MenuChanged'><arg type='s'/></signal>"
	"</interface></node>";

struct item {
	char *id,*owner,*service,*path,*iface,*title,*icon,*status,*menu;
	gboolean menu_only,ready,reading,changed;
	int w,h;
	GVariant *pixels;
};

static GDBusConnection *bus;
static GMainLoop *loop;
static GThread *thread;
static GDBusNodeInfo *definition;
static GPtrArray *items;
static guint registration[3],owners,updates;
static gboolean watcher_owned,freedesktop_owned;

static void free_item(gpointer p) {
	struct item *i=p;
	g_free(i->id);g_free(i->owner);g_free(i->service);g_free(i->path);
	g_free(i->iface);g_free(i->title);g_free(i->icon);g_free(i->status);g_free(i->menu);
	g_clear_pointer(&i->pixels,g_variant_unref);g_free(i);
}

static struct item *find(const char *id) {
	for(guint n=0;n<items->len;n++){
		struct item *i=g_ptr_array_index(items,n);
		if(g_str_equal(i->id,id))return i;
	}
	return NULL;
}

static void publish(void) {
	g_dbus_connection_emit_signal(bus,NULL,DESK_PATH,DESK,"Changed",NULL,NULL);
}

static void watcher_signal(const char *name,const char *id) {
	if(watcher_owned) g_dbus_connection_emit_signal(bus,NULL,WATCH_PATH,KDE_WATCH,name,g_variant_new("(s)",id),NULL);
	if(freedesktop_owned) g_dbus_connection_emit_signal(bus,NULL,WATCH_PATH,FDO_WATCH,name,g_variant_new("(s)",id),NULL);
}

static GVariant *watch_get(GDBusConnection *b,const char *sender,const char *path,const char *iface,
	const char *name,GError **error,gpointer data) {
	(void)b;(void)sender;(void)path;(void)iface;(void)error;(void)data;
	if(g_str_equal(name,"IsStatusNotifierHostRegistered"))return g_variant_new_boolean(TRUE);
	if(g_str_equal(name,"ProtocolVersion"))return g_variant_new_int32(0);
	GVariantBuilder v;
	g_variant_builder_init(&v,G_VARIANT_TYPE("as"));
	for(guint n=0;n<items->len;n++){
		struct item *i=g_ptr_array_index(items,n);
		g_autofree char *registered=g_strconcat(i->service,i->path,NULL);
		g_variant_builder_add(&v,"s",registered);
	}
	return g_variant_builder_end(&v);
}

static gboolean set_string(char **s,const char *next) {
	if(g_strcmp0(*s,next)==0)return FALSE;
	g_free(*s);*s=g_strdup(next);return TRUE;
}

static const char *str(GVariant *props,const char *key,const char *fallback) {
	const char *value;
	if(g_variant_lookup(props,key,"&s",&value) && *value && strlen(value)<=512)return value;
	return fallback;
}

// The SNI wire format is straight alpha bytes in network ARGB order.
static GVariant *icon_pixels(GVariant *props,const char *name,int *width,int *height) {
	*width=*height=0;
	g_autoptr(GVariant) images=g_variant_lookup_value(props,name,G_VARIANT_TYPE("a(iiay)"));
	if(!images)return NULL;
	g_autoptr(GVariant) best=NULL;
	int score=G_MAXINT;
	for(gsize n=0;n<g_variant_n_children(images) && n<64;n++){
		g_autoptr(GVariant) image=g_variant_get_child_value(images,n);
		g_autoptr(GVariant) pixels=NULL;
		int w,h;g_variant_get(image,"(ii@ay)",&w,&h,&pixels);
		gsize size;g_variant_get_fixed_array(pixels,&size,1);
		if(w<1 || h<1 || w>MAX_ICON || h>MAX_ICON || size!=(gsize)(4*w*h))continue;
		int rank=abs(MAX(w,h)-40);
		if(rank>=score)continue;
		score=rank;*width=w;*height=h;
		g_clear_pointer(&best,g_variant_unref);best=g_steal_pointer(&pixels);
	}
	if(!best)return NULL;
	gsize size;
	const guint8 *src=g_variant_get_fixed_array(best,&size,1);
	g_autofree guint8 *rgba=g_malloc(size);
	for(gsize n=0;n<size;n+=4){
		rgba[n]=src[n+1];rgba[n+1]=src[n+2];rgba[n+2]=src[n+3];rgba[n+3]=src[n];
	}
	return g_variant_ref_sink(g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,rgba,size,1));
}

struct refresh {char *id;gboolean fallback;};
static void fetch(struct item *,gboolean);
static void item_read(GObject *sender,GAsyncResult *result,gpointer data) {
	struct refresh *query=data;
	g_autofree char *id=query->id;
	gboolean fallback=query->fallback;
	g_free(query);
	g_autoptr(GVariant) reply=g_dbus_connection_call_finish(G_DBUS_CONNECTION(sender),result,NULL);
	struct item *i=find(id);
	if(!i)return;
	if(!reply && !i->iface && !fallback) {fetch(i,TRUE);return;}
	i->reading=FALSE;
	if(reply) {
		g_autoptr(GVariant) props=g_variant_get_child_value(reply,0);
		set_string(&i->iface,fallback?FDO_ITEM:i->iface?i->iface:KDE_ITEM);
		const char *status=str(props,"Status","Active");
		gboolean attention=g_str_equal(status,"NeedsAttention");
		const char *normal_icon=str(props,"IconName","");
		const char *icon=attention?str(props,"AttentionIconName",normal_icon):normal_icon;
		int w,h;
		g_autoptr(GVariant) pixels=icon_pixels(props,attention?"AttentionIconPixmap":"IconPixmap",&w,&h);
		if(attention && !pixels)pixels=icon_pixels(props,"IconPixmap",&w,&h);
		gboolean changed=!i->ready;
		changed |= set_string(&i->title,str(props,"Title",str(props,"Id","Application")));
		changed |= set_string(&i->status,status);
		changed |= set_string(&i->icon,icon);
		const char *menu="";
		g_variant_lookup(props,"Menu","&o",&menu);
		if(g_str_equal(menu,"/") || g_str_equal(menu,"/NO_DBUSMENU"))menu="";
		changed |= set_string(&i->menu,menu);
		gboolean only=FALSE;g_variant_lookup(props,"ItemIsMenu","b",&only);
		changed |= i->menu_only!=only;i->menu_only=only;
		changed |= i->w!=w || i->h!=h || ((i->pixels==NULL)!=(pixels==NULL)) ||
			(i->pixels && pixels && !g_variant_equal(i->pixels,pixels));
		g_clear_pointer(&i->pixels,g_variant_unref);
		i->pixels=g_steal_pointer(&pixels);i->w=w;i->h=h;
		i->ready=TRUE;
		if(changed)publish();
	}
	if(i->changed){i->changed=FALSE;fetch(i,FALSE);}
}

static void fetch(struct item *i,gboolean fallback) {
	i->reading=TRUE;
	struct refresh *query=g_new0(struct refresh,1);
	query->id=g_strdup(i->id);query->fallback=fallback;
	g_dbus_connection_call(bus,i->owner,i->path,"org.freedesktop.DBus.Properties","GetAll",
		g_variant_new("(s)",fallback?FDO_ITEM:i->iface?i->iface:KDE_ITEM),G_VARIANT_TYPE("(a{sv})"),
		G_DBUS_CALL_FLAGS_NO_AUTO_START,TIMEOUT,NULL,item_read,query);
}

static void register_item(const char *sender,const char *arg,GDBusMethodInvocation *call) {
	const char *service=sender,*path=arg;
	g_autofree char *id=NULL,*registered=NULL;
	if(*arg!='/'){service=arg;path="/StatusNotifierItem";}
	if(!g_dbus_is_name(service) || !g_variant_is_object_path(path) || strlen(path)>256)goto invalid;
	if(!g_str_equal(service,sender)){
		g_autoptr(GVariant) owner=g_dbus_connection_call_sync(bus,DBUS,DBUS_PATH,DBUS,"GetNameOwner",
			g_variant_new("(s)",service),G_VARIANT_TYPE("(s)"),G_DBUS_CALL_FLAGS_NONE,TIMEOUT,NULL,NULL);
		const char *unique;
		if(!owner)goto invalid;
		g_variant_get(owner,"(&s)",&unique);
		if(!g_str_equal(unique,sender))goto invalid;
	}
	id=g_strconcat(sender,path,NULL);
	struct item *i=find(id);
	if(i) {
		if(!i->reading)fetch(i,FALSE);
		g_dbus_method_invocation_return_value(call,NULL);
		return;
	}
	if(items->len>=MAX_ITEMS) {
		g_dbus_method_invocation_return_dbus_error(call,DESK ".Limit","Too many active tray items");
		return;
	}
	i=g_new0(struct item,1);i->id=g_steal_pointer(&id);i->owner=g_strdup(sender);
	i->service=g_strdup(service);i->path=g_strdup(path);
	g_ptr_array_add(items,i);
	registered=g_strconcat(i->service,i->path,NULL);
	watcher_signal("StatusNotifierItemRegistered",registered);
	fetch(i,FALSE);g_dbus_method_invocation_return_value(call,NULL);return;
invalid:
	g_dbus_method_invocation_return_dbus_error(call,DESK ".InvalidItem","Register an object owned by the calling application");
}

static void owners_changed(GDBusConnection *b,const char *sender,const char *path,const char *iface,
	const char *name,GVariant *value,gpointer data) {
	(void)b;(void)sender;(void)path;(void)iface;(void)name;(void)data;
	const char *service,*old,*next;
	g_variant_get(value,"(&s&s&s)",&service,&old,&next);
	if(!*old || g_str_equal(old,next))return;
	gboolean dirty=FALSE;
	for(guint n=items->len;n>0;n--){
		struct item *i=g_ptr_array_index(items,n-1);
		if(g_str_equal(service,i->service) || g_str_equal(service,i->owner)){
			g_autofree char *registered=g_strconcat(i->service,i->path,NULL);
			dirty|=i->ready;
			g_ptr_array_remove_index(items,n-1);
			watcher_signal("StatusNotifierItemUnregistered",registered);
		}
	}
	if(dirty)publish();
}

static void item_signal(GDBusConnection *b,const char *sender,const char *path,const char *iface,
	const char *name,GVariant *value,gpointer data) {
	(void)b;(void)value;(void)data;
	gboolean menus=g_str_equal(iface,MENU);
	if(menus && !g_str_equal(name,"LayoutUpdated") && !g_str_equal(name,"ItemsPropertiesUpdated"))return;
	if(!menus && !g_str_equal(iface,KDE_ITEM) && !g_str_equal(iface,FDO_ITEM) &&
		!(g_str_equal(iface,"org.freedesktop.DBus.Properties") && g_str_equal(name,"PropertiesChanged")))return;
	for(guint n=0;n<items->len;n++){
		struct item *i=g_ptr_array_index(items,n);
		if(!g_str_equal(sender,i->owner))continue;
		if(menus && i->menu && g_str_equal(path,i->menu))
			g_dbus_connection_emit_signal(bus,NULL,DESK_PATH,DESK,"MenuChanged",g_variant_new("(s)",i->id),NULL);
		if(menus || !g_str_equal(path,i->path))continue;
		if(i->reading)i->changed=TRUE;
		else fetch(i,FALSE);
	}
}

struct menu_request {
	char *owner,*path;
	int parent;
	GDBusMethodInvocation *call;
};

static void menu_free(gpointer data) {
	struct menu_request *m=data;
	g_free(m->owner);g_free(m->path);g_object_unref(m->call);g_free(m);
}

static void menu_layout(GObject *,GAsyncResult *,gpointer);

static void get_menu(GDBusConnection *b,struct menu_request *m) {
	const char *names[]={"label","enabled","visible","type","children-display","toggle-state","toggle-type",NULL};
	g_dbus_connection_call(b,m->owner,m->path,MENU,"GetLayout",g_variant_new("(ii^as)",m->parent,1,names),
		G_VARIANT_TYPE("(u(ia{sv}av))"),G_DBUS_CALL_FLAGS_NO_AUTO_START,TIMEOUT,NULL,
		menu_layout,m);
}

static void menu_about(GObject *b,GAsyncResult *result,gpointer data) {
	g_autoptr(GVariant) about=g_dbus_connection_call_finish(G_DBUS_CONNECTION(b),result,NULL);
	get_menu(G_DBUS_CONNECTION(b),data);
}

static void menu_layout(GObject *b,GAsyncResult *result,gpointer data) {
	struct menu_request *m=data;
	g_autoptr(GError) error=NULL;
	g_autoptr(GVariant) reply=g_dbus_connection_call_finish(G_DBUS_CONNECTION(b),result,&error);
	if(!reply)g_dbus_method_invocation_return_gerror(m->call,error);
	else {
		g_autoptr(GVariant) root=g_variant_get_child_value(reply,1);
		g_autoptr(GVariant) children=g_variant_get_child_value(root,2);
		GVariantBuilder rows;
		g_variant_builder_init(&rows,G_VARIANT_TYPE("a(isbbsi)"));
		guint count=0;
		for(gsize row=0;row<g_variant_n_children(children) && row<128 && count<64;row++){
			g_autoptr(GVariant) boxed=g_variant_get_child_value(children,row);
			g_autoptr(GVariant) entry=g_variant_get_variant(boxed);
			if(!g_variant_is_of_type(entry,G_VARIANT_TYPE("(ia{sv}av)")))continue;
			g_autoptr(GVariant) props=NULL,sub=NULL;
			int id,state=-1;gboolean visible=TRUE,enabled=TRUE;
			g_variant_get(entry,"(i@a{sv}@av)",&id,&props,&sub);
			g_variant_lookup(props,"visible","b",&visible);
			if(!visible)continue;
			g_variant_lookup(props,"enabled","b",&enabled);
			g_variant_lookup(props,"toggle-state","i",&state);
			const char *kind=str(props,"type","");
			gboolean child=g_variant_n_children(sub)>0 || g_str_equal(str(props,"children-display",""),"submenu");
			g_variant_builder_add(&rows,"(isbbsi)",id,str(props,"label",""),enabled,child,
				g_str_equal(kind,"separator")?"separator":str(props,"toggle-type",""),state);
			count++;
		}
		g_dbus_method_invocation_return_value(m->call,g_variant_new("(a(isbbsi))",&rows));
	}
	menu_free(m);
}

static void action_completed(GObject *conn,GAsyncResult *result,gpointer data) {
	GDBusMethodInvocation *call=data;
	g_autoptr(GError) error=NULL;
	g_autoptr(GVariant) r=g_dbus_connection_call_finish(G_DBUS_CONNECTION(conn),result,&error);
	if(r)g_dbus_method_invocation_return_value(call,g_str_equal(g_dbus_method_invocation_get_method_name(call),"Open")?g_variant_new("(b)",FALSE):NULL);
	else g_dbus_method_invocation_return_gerror(call,error);
	g_object_unref(call);
}

static void call_method(GDBusConnection *b,const char *sender,const char *path,const char *iface,
	const char *method,GVariant *args,GDBusMethodInvocation *call,gpointer data) {
	(void)path;(void)data;
	if(!g_str_equal(iface,DESK)){
		const char *arg;g_variant_get(args,"(&s)",&arg);
		if(g_str_equal(method,"RegisterStatusNotifierItem"))register_item(sender,arg,call);
		else g_dbus_method_invocation_return_value(call,NULL); // Our host is always registered.
		return;
	}
	if(g_str_equal(method,"GetItems")){
		GVariantBuilder rows;
		g_variant_builder_init(&rows,G_VARIANT_TYPE("a(ssssbiiay)"));
		for(guint n=0;n<items->len;n++){
			struct item *i=g_ptr_array_index(items,n);
			if(i->ready)g_variant_builder_add(&rows,"(ssssbii@ay)",i->id,i->title,i->icon,i->status,i->menu_only,
				i->w,i->h,i->pixels?g_variant_ref(i->pixels):g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,NULL,0,1));
		}
		g_dbus_method_invocation_return_value(call,g_variant_new("(a(ssssbiiay))",&rows));
		return;
	}
	const char *id;
	g_variant_get_child(args,0,"&s",&id);
	struct item *i=find(id);
	if(!i || !i->ready)goto stale;
	if(g_str_equal(method,"Open")){
		gboolean menu;int x,y;
		g_variant_get(args,"(&sbii)",&id,&menu,&x,&y);
		menu|=i->menu_only;
		if(menu && *i->menu){
			g_dbus_method_invocation_return_value(call,g_variant_new("(b)",TRUE));
		} else g_dbus_connection_call(b,i->owner,i->path,i->iface,menu?"ContextMenu":"Activate",
			g_variant_new("(ii)",x,y),G_VARIANT_TYPE_UNIT,G_DBUS_CALL_FLAGS_NO_AUTO_START,TIMEOUT,NULL,action_completed,g_object_ref(call));
		return;
	}
	if(!*i->menu)goto stale;
	if(g_str_equal(method,"GetMenu")){
		struct menu_request *m=g_new0(struct menu_request,1);
		gboolean prepare;g_variant_get(args,"(&sib)",&id,&m->parent,&prepare);
		m->owner=g_strdup(i->owner);m->path=g_strdup(i->menu);m->call=g_object_ref(call);
		if(!prepare)get_menu(b,m);
		else g_dbus_connection_call(b,m->owner,m->path,MENU,"AboutToShow",g_variant_new("(i)",m->parent),
			G_VARIANT_TYPE("(b)"),G_DBUS_CALL_FLAGS_NO_AUTO_START,TIMEOUT,NULL,menu_about,m);
		return;
	}
	int entry;guint stamp;g_variant_get(args,"(&siu)",&id,&entry,&stamp);
	g_dbus_connection_call(b,i->owner,i->menu,MENU,"Event",
		g_variant_new("(isvu)",entry,"clicked",g_variant_new_int32(0),stamp),G_VARIANT_TYPE_UNIT,
		G_DBUS_CALL_FLAGS_NO_AUTO_START,TIMEOUT,NULL,action_completed,g_object_ref(call));
	return;
stale:
	g_dbus_method_invocation_return_dbus_error(call,DESK ".StaleItem","This tray item or menu is no longer available");
}

static const GDBusInterfaceVTable vtable={.method_call=call_method,.get_property=watch_get};

static gboolean claim(const char *name) {
	g_autoptr(GVariant) reply=g_dbus_connection_call_sync(bus,DBUS,DBUS_PATH,DBUS,"RequestName",g_variant_new("(su)",name,4U),
		G_VARIANT_TYPE("(u)"),G_DBUS_CALL_FLAGS_NONE,TIMEOUT,NULL,NULL);
	guint code=0;if(reply)g_variant_get(reply,"(u)",&code);
	return code==1;
}

static gboolean run(const char *address) {
	bus=g_dbus_connection_new_for_address_sync(address,G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT|
		G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,NULL,NULL,NULL);
	if(!bus)return FALSE;
	g_dbus_connection_set_exit_on_close(bus,FALSE);
	if(!claim(DESK))return FALSE;
	// On buses with an existing watcher, never replace it (or create a second
	// active watcher via its compatibility name). The session app bus is private.
	watcher_owned=claim(KDE_WATCH);
	if(!watcher_owned)return FALSE;
	freedesktop_owned=claim(FDO_WATCH);
	if(!freedesktop_owned)return FALSE;
	claim(KDE_HOST);
	definition=g_dbus_node_info_new_for_xml(xml,NULL);
	if(!definition)return FALSE;
	for(int n=0;n<3;n++){
		if(n==1 && !freedesktop_owned)continue;
		registration[n]=g_dbus_connection_register_object(bus,n==2?DESK_PATH:WATCH_PATH,definition->interfaces[n],&vtable,NULL,NULL,NULL);
		if(!registration[n])return FALSE;
	}
	items=g_ptr_array_new_with_free_func(free_item);
	owners=g_dbus_connection_signal_subscribe(bus,DBUS,DBUS,"NameOwnerChanged",DBUS_PATH,NULL,G_DBUS_SIGNAL_FLAGS_NONE,owners_changed,NULL,NULL);
	updates=g_dbus_connection_signal_subscribe(bus,NULL,NULL,NULL,NULL,NULL,G_DBUS_SIGNAL_FLAGS_NONE,item_signal,NULL,NULL);
	return TRUE;
}

struct startup {const char *address;GAsyncQueue *ready;};
static gpointer event_thread(gpointer data) {
	struct startup *start=data;
	GMainContext *context=g_main_context_new();
	g_main_context_push_thread_default(context);
	loop=g_main_loop_new(context,FALSE);
	gboolean ok=run(start->address);
	g_async_queue_push(start->ready,GINT_TO_POINTER(ok?1:2));
	if(ok)g_main_loop_run(loop);
	if(bus){
		if(owners)g_dbus_connection_signal_unsubscribe(bus,owners);
		if(updates)g_dbus_connection_signal_unsubscribe(bus,updates);
		for(int n=0;n<3;n++)if(registration[n])g_dbus_connection_unregister_object(bus,registration[n]);
		g_dbus_connection_close_sync(bus,NULL,NULL);
	}
	g_clear_pointer(&items,g_ptr_array_unref);
	g_clear_pointer(&definition,g_dbus_node_info_unref);g_clear_object(&bus);
	g_main_loop_unref(loop);loop=NULL;
	g_main_context_pop_thread_default(context);g_main_context_unref(context);
	return NULL;
}

int tray_start(const char *address) {
	GAsyncQueue *ready=g_async_queue_new();
	struct startup start={address,ready};
	thread=g_thread_new("system-tray",event_thread,&start);
	gboolean ok=GPOINTER_TO_INT(g_async_queue_pop(ready))==1;
	g_async_queue_unref(ready);
	if(!ok){g_thread_join(thread);thread=NULL;}
	return ok;
}

static gboolean stop_loop(gpointer data) {
	g_main_loop_quit(data);return G_SOURCE_REMOVE;
}
void tray_stop(void) {
	if(!thread)return;
	GSource *source=g_idle_source_new();
	g_source_set_callback(source,stop_loop,loop,NULL);
	g_source_attach(source,g_main_loop_get_context(loop));
	g_source_unref(source);
	g_thread_join(thread);thread=NULL;
}
