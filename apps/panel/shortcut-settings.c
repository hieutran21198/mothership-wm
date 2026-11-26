//go:build linux && cgo && panel

#include "shortcut-settings.h"
#include "home-panel.h"
#include <stdlib.h>

extern void panelShortcutRequest(int save,int close,int grid,int settings,int function_row);

static GtkWidget *controls,*save_button,*notice,*choices[4],*board;
static char keys[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ,";
static char app_grid='A',node_settings=',';
static gboolean pending;
static void (*close_board)(void);

static guint get(GtkWidget *w){return gtk_drop_down_get_selected(GTK_DROP_DOWN(w));}
static void choose(GtkWidget *w,guint i){gtk_drop_down_set_selected(GTK_DROP_DOWN(w),i);}
static void status(const char *s) {
	gtk_label_set_text(GTK_LABEL(notice),s);
	gtk_widget_set_visible(notice,*s!=0);
}
static void enable(gboolean ready) {
	gtk_widget_set_sensitive(controls,ready);
	gtk_widget_set_sensitive(save_button,ready);
}
static void cancel(GtkButton *unused,gpointer data) {
	(void)unused;(void)data;
	close_board();
}
static void reset(GtkButton *unused,gpointer data) {
	(void)unused;(void)data;
	choose(choices[0],(guint)('Q'-'A'));
	choose(choices[1],0);
	choose(choices[2],26);
	choose(choices[3],0);
	status("");
}
static void save(GtkButton *unused,gpointer data) {
	(void)unused;(void)data;
	if(pending)return;
	guint a=get(choices[0]),b=get(choices[1]),c=get(choices[2]),d=get(choices[3]);
	if(a>=sizeof(keys)-1 || b>=sizeof(keys)-1 || c>=sizeof(keys)-1 || d>1)return;
	pending=TRUE;status("");enable(FALSE);
	panelShortcutRequest(1,keys[a],keys[b],keys[c],d!=0);
}
static GtkWidget *button(const char *s,gboolean primary,GCallback callback) {
	GtkWidget *w=gtk_button_new_with_label(s);
	home_mechanical_button(w,primary);
	g_signal_connect(w,"clicked",callback,NULL);
	return w;
}
GtkWidget *shortcut_settings_new(void (*close)(void)) {
	close_board=close;
	board=gtk_box_new(GTK_ORIENTATION_VERTICAL,9);
	gtk_widget_add_css_class(board,"shell-settings-content");
	GtkWidget *header=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6),*title=gtk_label_new("KEYBOARD SHORTCUTS");
	gtk_widget_add_css_class(title,"shell-settings-title");
	gtk_widget_set_halign(title,GTK_ALIGN_START);gtk_widget_set_hexpand(title,TRUE);
	gtk_box_append(GTK_BOX(header),title);
	gtk_box_append(GTK_BOX(header),button("Esc",FALSE,G_CALLBACK(cancel)));
	gtk_box_append(GTK_BOX(board),header);
	gtk_box_append(GTK_BOX(board),gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
	GtkWidget *scroll=gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
	gtk_widget_set_vexpand(scroll,TRUE);
	gtk_box_append(GTK_BOX(board),scroll);
	GtkGrid *grid=GTK_GRID(gtk_grid_new());controls=GTK_WIDGET(grid);
	gtk_grid_set_column_spacing(grid,12);gtk_grid_set_row_spacing(grid,8);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),controls);
	const char *labels[]={"Close focused window","Open Node App Grid","Open Node Settings","Switch Nodes"};
	for(guint i=0;i<4;i++) {
		GtkStringList *options=gtk_string_list_new(NULL);
		if(i==3) {
			gtk_string_list_append(options,"Win + 1–9");
			gtk_string_list_append(options,"Win + F1–F9");
		} else for(guint j=0;j<sizeof(keys)-1;j++) {
			char label[8];g_snprintf(label,sizeof(label),"Win + %c",keys[j]);
			gtk_string_list_append(options,label);
		}
		GtkWidget *label=gtk_label_new(labels[i]);
		gtk_label_set_xalign(GTK_LABEL(label),0);gtk_widget_set_hexpand(label,TRUE);
		gtk_grid_attach(grid,label,0,i,1,1);
		choices[i]=gtk_drop_down_new(G_LIST_MODEL(options),NULL);
		gtk_widget_add_css_class(choices[i],"display-choice");
		gtk_accessible_update_property(GTK_ACCESSIBLE(choices[i]),GTK_ACCESSIBLE_PROPERTY_LABEL,labels[i],-1);
		gtk_grid_attach(grid,choices[i],1,i,1,1);
	}
	notice=gtk_label_new("");
	gtk_widget_add_css_class(notice,"shell-settings-hint");
	gtk_label_set_xalign(GTK_LABEL(notice),0);gtk_label_set_wrap(GTK_LABEL(notice),TRUE);
	gtk_grid_attach(grid,notice,0,4,2,1);
	gtk_widget_set_visible(notice,FALSE);
	gtk_box_append(GTK_BOX(board),gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
	GtkWidget *footer=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6),*space=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
	gtk_widget_set_hexpand(space,TRUE);
	gtk_box_append(GTK_BOX(footer),button("Defaults",FALSE,G_CALLBACK(reset)));
	gtk_box_append(GTK_BOX(footer),space);
	gtk_box_append(GTK_BOX(footer),button("Cancel",FALSE,G_CALLBACK(cancel)));
	save_button=button("Save",TRUE,G_CALLBACK(save));gtk_box_append(GTK_BOX(footer),save_button);
	gtk_box_append(GTK_BOX(board),footer);
	enable(FALSE);
	return board;
}
void shortcut_settings_open(void) {
	if(pending)return;
	pending=TRUE;status("");enable(FALSE);
	panelShortcutRequest(0,0,0,0,0);
}
void shortcut_guide(char *grid,char *settings) {
	*grid=app_grid;*settings=node_settings;
}
struct reply {int save,close,grid,settings,function_row;char *error;};
static gboolean apply_reply(gpointer ptr) {
	struct reply *r=ptr;
	gboolean startup=r->save<0;
	if(!startup)pending=FALSE;
	if(*r->error) {
		if(!startup){enable(r->save!=0);status(r->error);}
	} else {
		app_grid=r->grid;node_settings=r->settings;
		if(!startup) {
			choose(choices[0],r->close==','?26:r->close-'A');
			choose(choices[1],r->grid==','?26:r->grid-'A');
			choose(choices[2],r->settings==','?26:r->settings-'A');
			choose(choices[3],r->function_row?1:0);
			enable(TRUE);status("");
			if(r->save && gtk_widget_get_visible(board))close_board();
		}
	}
	g_free(r->error);g_free(r);
	return G_SOURCE_REMOVE;
}
void shortcut_settings_result(int save,int close,int grid,int settings,int function_row,const char *error) {
	struct reply *r=g_new(struct reply,1);
	*r=(struct reply){save,close,grid,settings,function_row,g_strdup(error)};
	g_idle_add(apply_reply,r);
}
