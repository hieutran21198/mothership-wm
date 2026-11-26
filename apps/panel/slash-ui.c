//go:build linux && cgo && panel

#include "slash-ui.h"
#include "analog-glass.h"
#include "app-grid.h"
#include "home-panel.h"
#include "map-fleet.h"
#include "map-guide.h"

enum slash_kind {
	MODULE_APPS,MODULE_NODES,MODULE_MAPS,MODULE_NOTIFICATIONS,MODULE_SETTINGS,
	COMMAND_OPEN_APP,COMMAND_CONNECT,COMMAND_NEW,COMMAND_DELETE,COMMAND_FORCE,
	COMMAND_OPEN_MAP,COMMAND_OPEN_NOTIFICATIONS,COMMAND_OPEN_SETTINGS,COMMAND_DISPLAY,COMMAND_SHORTCUTS,
	RESULT_APP,RESULT_NODE,RESULT_DELETE,RESULT_FORCE
};
static const struct {
	enum slash_kind module,kind;
	const char *token,*complete;
} commands[]={
	{MODULE_APPS,COMMAND_OPEN_APP,"open","apps open "},
	{MODULE_NODES,COMMAND_CONNECT,"connect","nodes connect "},
	{MODULE_NODES,COMMAND_NEW,"new","nodes new "},
	{MODULE_NODES,COMMAND_DELETE,"delete","nodes delete "},
	{MODULE_NODES,COMMAND_FORCE,"force-delete","nodes force-delete "},
	{MODULE_MAPS,COMMAND_OPEN_MAP,"open","maps open"},
	{MODULE_NOTIFICATIONS,COMMAND_OPEN_NOTIFICATIONS,"open","notifications open"},
	{MODULE_SETTINGS,COMMAND_OPEN_SETTINGS,"open","settings open"},
	{MODULE_SETTINGS,COMMAND_DISPLAY,"display","settings display"},
	{MODULE_SETTINGS,COMMAND_SHORTCUTS,"shortcuts","settings shortcuts"}
};
static const struct {
	enum slash_kind kind;
	const char *name,*complete;
} modules[]={
	{MODULE_APPS,"apps","apps "},
	{MODULE_NODES,"nodes","nodes "},
	{MODULE_MAPS,"maps","maps "},
	{MODULE_NOTIFICATIONS,"notifications","notifications "},
	{MODULE_SETTINGS,"settings","settings "}
};
enum { ROW_COUNT=5, ROW_HEIGHT=48, WIDTH_CAP=480 };
struct suggestion { enum slash_kind kind; char *name; GDesktopAppInfo *app; GtkWidget *item; };
static GtkWidget *root,*entry,*list,*empty;
static GPtrArray *catalog,*results;
static char **node_names;
static void (*close_slash)(void);
static void (*open_node)(const char *,gboolean,guint64);
static void (*show_view)(enum slash_view);
static void (*delete_node)(const char *,int);
static void (*notify_region)(void);
static int chosen,first,limit=ROW_COUNT;
static gboolean composing,tab_held,enter_held,filling;
static gboolean new_node,new_name_ready,node_scope;
static char *new_name,*node_error;
static guint64 node_serial,node_request;
static char *filled_app,*delete_confirmation,*delete_status;
static gboolean delete_force,delete_pending;
static void refresh(void);

static void suggestion_free(gpointer p) {
	struct suggestion *s=p;
	g_free(s->name);g_free(s);
}

static void add(enum slash_kind kind,const char *name,GDesktopAppInfo *app) {
	struct suggestion *s=g_new0(struct suggestion,1);
	*s=(struct suggestion){.kind=kind,.name=g_strdup(name),.app=app};
	g_ptr_array_add(results,s);
}

static gboolean match(const char *name,const char *term,gboolean prefix) {
	g_autofree char *folded=g_utf8_casefold(name,-1);
	return prefix?g_str_has_prefix(folded,term):strstr(folded,term)!=NULL;
}
static gboolean matches_app(GDesktopAppInfo *info,const char *term) {
	GAppInfo *app=G_APP_INFO(info);
	return match(g_app_info_get_display_name(app),term,FALSE) ||
		match(g_app_info_get_id(app),term,FALSE);
}
static gboolean take_token(char **text,const char *token) {
	size_t len=strlen(token);
	if(g_ascii_strncasecmp(*text,token,len)!=0 || !g_ascii_isspace((*text)[len])) return FALSE;
	*text+=len;
	while(g_ascii_isspace(**text)) (*text)++;
	return TRUE;
}

static gboolean module_available(enum slash_kind module) {
	return !node_scope || module==MODULE_APPS || module==MODULE_NOTIFICATIONS || module==MODULE_SETTINGS;
}

static void add_commands(enum slash_kind module,const char *term) {
	for(guint i=0;i<G_N_ELEMENTS(commands);i++)
		if(commands[i].module==module &&
			(!node_scope || (commands[i].kind!=COMMAND_DISPLAY && commands[i].kind!=COMMAND_SHORTCUTS)) &&
			match(commands[i].token,term,TRUE))
			add(commands[i].kind,commands[i].token,NULL);
}

static gboolean valid_new_name(const char *name) {
	if(!*name || strlen(name)>32) return FALSE;
	for(const char *c=name;*c;c++)
		if(!g_ascii_isalnum(*c) && *c!='_' && *c!='-') return FALSE;
	return TRUE;
}

static void glyph(GtkDrawingArea *area,cairo_t *cr,int w,int h,gpointer data) {
	(void)area;
	enum slash_kind kind=GPOINTER_TO_INT(data);
	cairo_translate(cr,w*.5,h*.5);
	cairo_set_source_rgb(cr,.65,.86,.92);
	cairo_set_line_width(cr,1.45);
	cairo_set_line_join(cr,CAIRO_LINE_JOIN_ROUND);
	cairo_set_line_cap(cr,CAIRO_LINE_CAP_ROUND);
	if(kind==MODULE_APPS || kind==COMMAND_OPEN_APP) {
		cairo_translate(cr,-12,-12);
		home_apps_glyph(cr);
	} else if(kind==MODULE_MAPS || kind==COMMAND_OPEN_MAP) {
		cairo_translate(cr,-12,-12);
		home_map_glyph(cr);
	} else if(kind==MODULE_NODES || kind==RESULT_NODE || kind==RESULT_DELETE || kind==RESULT_FORCE) {
		cairo_scale(cr,.79,.79);
		cairo_move_to(cr,fleet_node[0].x,fleet_node[0].y);
		for(unsigned i=1;i<FLEET_COUNT(fleet_node);i++) cairo_line_to(cr,fleet_node[i].x,fleet_node[i].y);
		cairo_close_path(cr);cairo_move_to(cr,0,-4);cairo_line_to(cr,0,3);
	} else if(kind==MODULE_NOTIFICATIONS || kind==COMMAND_OPEN_NOTIFICATIONS) {
		cairo_move_to(cr,-7,4);cairo_curve_to(cr,-4,2,-5,-1,-4,-4);
		cairo_curve_to(cr,-3,-8,3,-8,4,-4);cairo_curve_to(cr,5,-1,4,2,7,4);
		cairo_close_path(cr);cairo_new_sub_path(cr);cairo_arc(cr,0,4,2,0,G_PI);
	} else if(kind==MODULE_SETTINGS || kind==COMMAND_OPEN_SETTINGS ||
			kind==COMMAND_DISPLAY || kind==COMMAND_SHORTCUTS) {
		for(int i=-5;i<=5;i+=5) {
			int x=i?-2:2;
			cairo_move_to(cr,-7,i);cairo_line_to(cr,x-2,i);
			cairo_move_to(cr,x+2,i);cairo_line_to(cr,7,i);
			cairo_new_sub_path(cr);cairo_arc(cr,x,i,1.8,0,2*G_PI);
		}
	} else if(kind==COMMAND_NEW) {
		cairo_move_to(cr,-6,0);cairo_line_to(cr,6,0);
		cairo_move_to(cr,0,-6);cairo_line_to(cr,0,6);
	} else {
		cairo_move_to(cr,-7,0);cairo_line_to(cr,7,0);
		cairo_line_to(cr,2,-5);cairo_move_to(cr,7,0);cairo_line_to(cr,2,5);
	}
	cairo_stroke(cr);
}

static void fill_text(const char *text) {
	filling=TRUE;
	gtk_editable_set_text(GTK_EDITABLE(entry),text);
	gtk_entry_grab_focus_without_selecting(GTK_ENTRY(entry));
	gtk_editable_select_region(GTK_EDITABLE(entry),-1,-1);
	filling=FALSE;
	refresh();
}
static void complete(void) {
	if(chosen<0 || (guint)chosen>=results->len) return;
	struct suggestion *s=g_ptr_array_index(results,chosen);
	g_autofree char *value=NULL;
	g_clear_pointer(&filled_app,g_free);
	for(guint i=0;i<G_N_ELEMENTS(modules);i++)
		if(s->kind==modules[i].kind) {fill_text(modules[i].complete);return;}
	for(guint i=0;i<G_N_ELEMENTS(commands);i++)
		if(s->kind==commands[i].kind) {fill_text(commands[i].complete);return;}
	switch(s->kind) {
	case RESULT_DELETE: value=g_strdup_printf("nodes delete %s",s->name);break;
	case RESULT_FORCE: value=g_strdup_printf("nodes force-delete %s",s->name);break;
	case RESULT_APP:
		value=g_strdup_printf("apps open %s",s->name);
		filled_app=g_strdup(g_app_info_get_id(G_APP_INFO(s->app)));
		break;
	case RESULT_NODE:
		// Keep names that start with "new" or "connect" unambiguous.
		value=g_strdup_printf("nodes connect %s",s->name);
		break;
	default: return;
	}
	fill_text(value);
}
static void submit_node(const char *name,gboolean create) {
	g_autofree char *target=g_strdup(name);
	g_clear_pointer(&node_error,g_free);
	node_request=++node_serial;
	gtk_editable_set_editable(GTK_EDITABLE(entry),FALSE);
	refresh();
	open_node(target,create,node_request);
}
static void activation(GtkEntry *input,gpointer data) {
	(void)input;(void)data;
	if(composing || delete_pending || node_request) return;
	if(delete_confirmation) {
		g_autofree char *name=g_steal_pointer(&delete_confirmation);
		delete_pending=TRUE;
		refresh();
		delete_node(name,delete_force);
		return;
	}
	if(new_node) {
		if(!new_name_ready) return;
		submit_node(new_name,TRUE);
		return;
	}
	if(chosen<0 || (guint)chosen>=results->len) return;
	struct suggestion *s=g_ptr_array_index(results,chosen);
	switch(s->kind) {
	case COMMAND_OPEN_MAP: case COMMAND_OPEN_NOTIFICATIONS: case COMMAND_OPEN_SETTINGS:
	case COMMAND_DISPLAY: case COMMAND_SHORTCUTS: {
		enum slash_view view=s->kind==COMMAND_OPEN_MAP?SLASH_MAP:
			s->kind==COMMAND_OPEN_NOTIFICATIONS?SLASH_NOTIFICATIONS:
			s->kind==COMMAND_DISPLAY?SLASH_DISPLAY:s->kind==COMMAND_SHORTCUTS?SLASH_SHORTCUTS:SLASH_SETTINGS;
		close_slash();show_view(view);break;
	}
	case RESULT_APP:
		if(app_catalog_launch(s->app)) close_slash();
		break;
	case RESULT_NODE:
		submit_node(s->name,FALSE);break;
	case RESULT_DELETE: case RESULT_FORCE: {
		const gboolean force=s->kind==RESULT_FORCE;
		g_autofree char *name=g_strdup(s->name);
		complete();
		delete_confirmation=g_strdup(name);
		delete_force=force;
		refresh();break;
	}
	default: complete();break;
	}
}

static void clicked(GtkButton *w,gpointer data) {
	(void)w;
	chosen=GPOINTER_TO_INT(data);
	complete();
}

static void draw_rows(void) {
	GtkWidget *child;
	while((child=gtk_widget_get_first_child(list))) gtk_box_remove(GTK_BOX(list),child);
	for(guint i=0;i<results->len;i++) ((struct suggestion *)g_ptr_array_index(results,i))->item=NULL;
	for(int i=first;i<(int)results->len && i<first+limit;i++) {
		struct suggestion *s=g_ptr_array_index(results,i);
		GtkWidget *button=gtk_button_new();
		gtk_widget_add_css_class(button,"slash-row");
		gtk_widget_set_focusable(button,FALSE);
		gtk_widget_set_size_request(button,-1,ROW_HEIGHT);
		GtkWidget *line=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,12);
		gtk_widget_add_css_class(line,"slash-row-line");
		gtk_widget_set_can_target(line,FALSE);
		GtkWidget *icon;
		if(s->kind==RESULT_APP) {
			GIcon *art=g_app_info_get_icon(G_APP_INFO(s->app));
			GtkIconTheme *theme=gtk_icon_theme_get_for_display(gtk_widget_get_display(root));
			if(art && G_IS_THEMED_ICON(art) && gtk_icon_theme_has_gicon(theme,art))
				icon=gtk_image_new_from_gicon(art);
			else if(art && G_IS_FILE_ICON(art) && g_file_is_native(g_file_icon_get_file(G_FILE_ICON(art))))
				icon=gtk_image_new_from_gicon(art);
			else icon=gtk_image_new_from_icon_name("application-x-executable-symbolic");
			gtk_image_set_pixel_size(GTK_IMAGE(icon),20);
		} else {
			icon=gtk_drawing_area_new();
			gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(icon),glyph,GINT_TO_POINTER(s->kind),NULL);
		}
		gtk_widget_set_size_request(icon,24,24);
		gtk_widget_set_valign(icon,GTK_ALIGN_CENTER);
		gtk_box_append(GTK_BOX(line),icon);
		GtkWidget *name=gtk_label_new(s->name);
		gtk_label_set_single_line_mode(GTK_LABEL(name),TRUE);
		gtk_label_set_ellipsize(GTK_LABEL(name),PANGO_ELLIPSIZE_END);
		gtk_label_set_xalign(GTK_LABEL(name),0);
		gtk_widget_set_hexpand(name,TRUE);
		gtk_box_append(GTK_BOX(line),name);
		s->item=analog_item_new(line);
		analog_item_select(s->item,i==chosen);
		gtk_button_set_child(GTK_BUTTON(button),s->item);
		gtk_accessible_update_property(GTK_ACCESSIBLE(button),GTK_ACCESSIBLE_PROPERTY_LABEL,s->name,-1);
		gtk_accessible_update_state(GTK_ACCESSIBLE(button),GTK_ACCESSIBLE_STATE_SELECTED,i==chosen,-1);
		g_signal_connect(button,"clicked",G_CALLBACK(clicked),GINT_TO_POINTER(i));
		gtk_box_append(GTK_BOX(list),button);
	}
	notify_region();
}

static void select_row(int delta) {
	if(!results->len) return;
	int next=CLAMP(chosen+delta,0,(int)results->len-1);
	if(next==chosen) return;
	int previous=chosen;chosen=next;
	if(chosen<first || chosen>=first+limit) {
		first=chosen<first?chosen:chosen-limit+1;draw_rows();return;
	}
	for(int i=first;i<(int)results->len && i<first+limit;i++) if(i==previous || i==chosen) {
		struct suggestion *s=g_ptr_array_index(results,i);
		analog_item_select(s->item,i==chosen);
		GtkWidget *button=gtk_widget_get_parent(s->item);
		gtk_accessible_update_state(GTK_ACCESSIBLE(button),GTK_ACCESSIBLE_STATE_SELECTED,i==chosen,-1);
	}
}
static gboolean scroll(GtkEventControllerScroll *controller,double dx,double dy,gpointer data) {
	(void)controller;(void)dx;(void)data;
	if(dy!=0) select_row(dy>0?1:-1);
	return dy!=0;
}

static void refresh(void) {
	g_autofree char *copy=g_strdup(gtk_editable_get_text(GTK_EDITABLE(entry)));
	char *query=g_strchug(copy);
	if(*query=='/') query=g_strchug(query+1);
	g_ptr_array_set_size(results,0);
	g_clear_pointer(&new_name,g_free);
	new_node=FALSE;
	new_name_ready=FALSE;
	chosen=0;first=0;
	const char *hint="No matching command.";
	g_autofree char *prompt=NULL;
	if(node_request) {
		hint="Opening Node…";
	} else if(node_error) {
		hint=node_error;
	} else if(delete_pending) {
		hint="Deleting Node…";
	} else if(delete_status) {
		hint=delete_status;
	} else if(delete_confirmation) {
		prompt=g_strdup_printf(delete_force?
			"Force delete %s? Its windows move to Default Node and stay open. Press Enter to confirm or Esc to cancel.":
			"Delete %s? Press Enter to confirm or Esc to cancel.",delete_confirmation);
		hint=prompt;
	} else if(take_token(&query,"apps")) {
		hint="No matching apps.";
		gboolean explicit_open=take_token(&query,"open");
		g_autofree char *term=g_utf8_casefold(g_strchomp(query),-1);
		if(!explicit_open) add_commands(MODULE_APPS,term);
		for(guint i=0;(explicit_open || *term) && i<catalog->len;i++) {
			GDesktopAppInfo *a=g_ptr_array_index(catalog,i);
			if(!matches_app(a,term)) continue;
			const char *name=g_app_info_get_display_name(G_APP_INFO(a));
			if(filled_app && g_strcmp0(filled_app,g_app_info_get_id(G_APP_INFO(a)))==0) chosen=(int)results->len;
			add(RESULT_APP,name,a);
		}
	} else if(module_available(MODULE_NODES) && take_token(&query,"nodes")) {
		hint="No matching Nodes.";
		gboolean explicit_connect=take_token(&query,"connect");
		gboolean ordinary_delete=!explicit_connect && take_token(&query,"delete");
		gboolean force_delete=!explicit_connect && !ordinary_delete && take_token(&query,"force-delete");
		if(!explicit_connect && !ordinary_delete && !force_delete && take_token(&query,"new")) {
			new_node=TRUE;
			new_name=g_strdup(g_strchomp(query));
			new_name_ready=valid_new_name(new_name);
			hint=!*new_name?"Type a name for the new Node.":new_name_ready
				?"Press Enter to create and enter this Node.":"Use 1–32 letters, digits, hyphens or underscores.";
			for(int i=0;new_name_ready && node_names && node_names[i];i++)
				if(g_str_equal(new_name,node_names[i])) {
					new_name_ready=FALSE;
					hint="That Node already exists.";
				}
		} else {
			g_autofree char *term=g_utf8_casefold(g_strchomp(query),-1);
			gboolean explicit_node=explicit_connect || ordinary_delete || force_delete;
			if(!explicit_node) add_commands(MODULE_NODES,term);
			for(int i=0;(explicit_node || *term) && node_names && node_names[i];i++) {
				if(!*node_names[i] || !match(node_names[i],term,FALSE)) continue;
				if(g_str_equal(node_names[i],query) && *term) chosen=(int)results->len;
				add(force_delete?RESULT_FORCE:ordinary_delete?RESULT_DELETE:RESULT_NODE,node_names[i],NULL);
			}
		}
	} else if(module_available(MODULE_MAPS) && take_token(&query,"maps")) {
		g_autofree char *term=g_utf8_casefold(g_strchomp(query),-1);
		add_commands(MODULE_MAPS,term);
	} else if(take_token(&query,"notifications")) {
		g_autofree char *term=g_utf8_casefold(g_strchomp(query),-1);
		add_commands(MODULE_NOTIFICATIONS,term);
	} else if(take_token(&query,"settings")) {
		g_autofree char *term=g_utf8_casefold(g_strchomp(query),-1);
		add_commands(MODULE_SETTINGS,term);
	} else {
		g_autofree char *term=g_utf8_casefold(g_strchomp(query),-1);
		for(guint i=0;i<G_N_ELEMENTS(modules);i++)
			if(module_available(modules[i].kind) && match(modules[i].name,term,TRUE))
				add(modules[i].kind,modules[i].name,NULL);
	}
	if(results->len==0) chosen=-1;
	if(chosen>=limit) first=chosen-limit+1;
	gtk_label_set_text(GTK_LABEL(empty),hint);
	gtk_widget_set_visible(empty,results->len==0);
	draw_rows();
}
static void edited(GtkEditable *editable,gpointer data) {
	(void)editable;(void)data;
	if(filling) return;
	g_clear_pointer(&delete_confirmation,g_free);
	g_clear_pointer(&delete_status,g_free);
	g_clear_pointer(&node_error,g_free);
	g_clear_pointer(&filled_app,g_free);
	refresh();
}
static void preedit(GtkText *text,const char *value,gpointer data) {
	(void)text;(void)data;composing=value && *value;
}
static gboolean keyboard(GtkEventControllerKey *keys,guint key,guint code,GdkModifierType mods,gpointer data) {
	(void)keys;(void)code;(void)data;
	if(mods&(GDK_SHIFT_MASK|GDK_CONTROL_MASK|GDK_ALT_MASK|GDK_SUPER_MASK|GDK_META_MASK|GDK_HYPER_MASK)) return FALSE;
	if(key==GDK_KEY_Return || key==GDK_KEY_KP_Enter) {
		// Let GTK/IME handle the first press. A repeat must never run a
		// command at the next stage after the same press selected a module.
		if(enter_held && !composing) return TRUE;
		enter_held=TRUE;
		return FALSE;
	}
	if(composing) return FALSE;
	switch(key) {
	case GDK_KEY_Tab:
		if(!tab_held) {tab_held=TRUE;complete();}
		return TRUE;
	case GDK_KEY_Down: select_row(1);return TRUE;
	case GDK_KEY_Up: select_row(-1);return TRUE;
	default: return FALSE;
	}
}
static void release(GtkEventControllerKey *keys,guint key,guint code,GdkModifierType mods,gpointer data) {
	(void)keys;(void)code;(void)mods;(void)data;
	if(key==GDK_KEY_Tab || key==GDK_KEY_ISO_Left_Tab) tab_held=FALSE;
	if(key==GDK_KEY_Return || key==GDK_KEY_KP_Enter) enter_held=FALSE;
}
static GtkWidget *pane(GtkWidget *content) {
	GtkWidget *w=gtk_overlay_new();
	gtk_overlay_set_child(GTK_OVERLAY(w),analog_glass_new());
	gtk_overlay_add_overlay(GTK_OVERLAY(w),content);
	gtk_overlay_set_measure_overlay(GTK_OVERLAY(w),content,TRUE);
	gtk_widget_set_hexpand(w,TRUE);
	return w;
}
static void guide_draw(GtkDrawingArea *area,cairo_t *cr,int w,int h,gpointer data) {
	(void)area;(void)h;(void)data;slash_guide_draw(cr,w);
}

GtkWidget *slash_ui_new(void (*close)(void),void (*enter_node)(const char *,gboolean,guint64),
		void (*open_view)(enum slash_view),void (*layout_changed)(void),void (*delete)(const char *,int)) {
	close_slash=close;open_node=enter_node;show_view=open_view;notify_region=layout_changed;delete_node=delete;
	results=g_ptr_array_new_with_free_func(suggestion_free);
	root=gtk_box_new(GTK_ORIENTATION_VERTICAL,9);
	gtk_widget_set_halign(root,GTK_ALIGN_CENTER);
	gtk_widget_set_valign(root,GTK_ALIGN_END);
	gtk_widget_set_margin_bottom(root,HOME_BOTTOM+HOME_HEIGHT+9);
	GtkWidget *body=gtk_box_new(GTK_ORIENTATION_VERTICAL,10);
	gtk_widget_add_css_class(body,"slash-suggestions");
	list=gtk_box_new(GTK_ORIENTATION_VERTICAL,6);
	gtk_box_append(GTK_BOX(body),list);
	empty=gtk_label_new("");
	gtk_widget_add_css_class(empty,"slash-empty");
	gtk_label_set_wrap(GTK_LABEL(empty),TRUE);
	gtk_label_set_max_width_chars(GTK_LABEL(empty),40);
	gtk_label_set_wrap_mode(GTK_LABEL(empty),PANGO_WRAP_WORD_CHAR);
	gtk_label_set_xalign(GTK_LABEL(empty),0);
	gtk_box_append(GTK_BOX(body),empty);
	GtkWidget *guide=gtk_drawing_area_new();
	gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(guide),19);
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(guide),guide_draw,NULL,NULL);
	gtk_widget_set_can_target(guide,FALSE);
	gtk_box_append(GTK_BOX(body),guide);
	gtk_box_append(GTK_BOX(root),pane(body));
	GtkEventController *wheel=gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL|GTK_EVENT_CONTROLLER_SCROLL_DISCRETE);
	g_signal_connect(wheel,"scroll",G_CALLBACK(scroll),NULL);
	gtk_widget_add_controller(body,wheel);
	GtkWidget *prompt=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
	gtk_widget_add_css_class(prompt,"home-slash");
	gtk_box_append(GTK_BOX(prompt),gtk_label_new("/"));
	entry=gtk_entry_new();
	gtk_entry_set_max_length(GTK_ENTRY(entry),160);
	gtk_editable_set_width_chars(GTK_EDITABLE(entry),1);
	gtk_editable_set_max_width_chars(GTK_EDITABLE(entry),1);
	gtk_widget_set_hexpand(entry,TRUE);
	gtk_accessible_update_property(GTK_ACCESSIBLE(entry),GTK_ACCESSIBLE_PROPERTY_LABEL,"Slash command",
		GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,"Use Up and Down to select, Tab to complete, Enter to run, or Escape to close.",-1);
	gtk_box_append(GTK_BOX(prompt),entry);
	gtk_box_append(GTK_BOX(root),pane(prompt));
	GtkEventController *keys=gtk_event_controller_key_new();
	gtk_event_controller_set_propagation_phase(keys,GTK_PHASE_CAPTURE);
	g_signal_connect(keys,"key-pressed",G_CALLBACK(keyboard),NULL);
	g_signal_connect(keys,"key-released",G_CALLBACK(release),NULL);
	gtk_widget_add_controller(entry,keys);
	GtkEditable *delegate=gtk_editable_get_delegate(GTK_EDITABLE(entry));
	if(GTK_IS_TEXT(delegate)) g_signal_connect(delegate,"preedit-changed",G_CALLBACK(preedit),NULL);
	g_signal_connect(entry,"changed",G_CALLBACK(edited),NULL);
	g_signal_connect(entry,"activate",G_CALLBACK(activation),NULL);
	gtk_widget_set_visible(root,FALSE);
	return root;
}
void slash_ui_show(gboolean in_node) {
	node_scope=in_node;
	g_clear_pointer(&catalog,g_ptr_array_unref);
	catalog=app_catalog_snapshot();
	g_clear_pointer(&filled_app,g_free);
	composing=tab_held=enter_held=FALSE;
	gtk_widget_set_visible(root,TRUE);
	fill_text("");
}
void slash_ui_hide(void) {
	composing=tab_held=enter_held=FALSE;
	g_clear_pointer(&delete_confirmation,g_free);
	g_clear_pointer(&delete_status,g_free);
	g_clear_pointer(&node_error,g_free);
	node_request=0;
	gtk_editable_set_editable(GTK_EDITABLE(entry),TRUE);
	gtk_widget_set_visible(root,FALSE);
}
struct node_result { guint64 request; char *message; };
static void free_node_result(gpointer data) {
	struct node_result *result=data;
	g_free(result->message);
	g_free(result);
}
static gboolean show_node_error(gpointer data) {
	struct node_result *result=data;
	if(result->request!=node_request || !gtk_widget_get_visible(root)) return G_SOURCE_REMOVE;
	node_request=0;
	node_error=g_strdup(result->message);
	gtk_editable_set_editable(GTK_EDITABLE(entry),TRUE);
	refresh();
	return G_SOURCE_REMOVE;
}
void slash_ui_node_error(guint64 request,const char *message) {
	struct node_result *result=g_new(struct node_result,1);
	*result=(struct node_result){request,g_strdup(message)};
	g_idle_add_full(G_PRIORITY_DEFAULT,show_node_error,result,free_node_result);
}
static gboolean show_delete_result(gpointer data) {
	delete_pending=FALSE;
	if(gtk_widget_get_visible(root)) {
		g_free(delete_status);
		delete_status=g_strdup(data);
		refresh();
	}
	return G_SOURCE_REMOVE;
}
void slash_ui_delete_result(const char *message) {
	g_idle_add_full(G_PRIORITY_DEFAULT,show_delete_result,g_strdup(message),g_free);
}
gboolean slash_ui_composing(void) {return composing;}
void slash_ui_fit(int width,int height) {
	gtk_widget_set_size_request(root,MIN(WIDTH_CAP,MAX(1,width-24)),-1);
	int next=CLAMP((height-HOME_BOTTOM-HOME_HEIGHT-140)/(ROW_HEIGHT+6),1,ROW_COUNT);
	if(next!=limit) {
		limit=next;
		if(gtk_widget_get_visible(root)) {
			first=MAX(0,chosen-limit+1);
			draw_rows();
		}
	}
}
void slash_ui_nodes(const char *list_text) {
	// A changed catalog invalidates any unsubmitted confirmation.
	g_clear_pointer(&delete_confirmation,g_free);
	g_clear_pointer(&node_names,g_strfreev);
	node_names=g_strsplit(list_text?list_text:"","\n",-1);
	if(root && gtk_widget_get_visible(root)) refresh();
}
