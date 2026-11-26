//go:build linux && cgo && panel

#include "display-settings.h"
#include "home-panel.h"
#include "map-pointer.h"

struct mode { int w,h,hz; };
static GtkWidget *display_entry,*monitor,*resolution,*refresh,*scale,*apply,*notice,*controls;
static GArray *modes,*sizes,*rates;
static struct mode current;
static int current_scale;
static gboolean available,updating,pending;
static void (*close_settings)(void);
static const int scales[]={100,125,150,175,200,225,250,275,300};

static void report(const char *message) {
	gtk_label_set_text(GTK_LABEL(notice),message);
	gtk_widget_set_visible(notice,*message!=0);
}
static guint chosen(GtkWidget *w) {return gtk_drop_down_get_selected(GTK_DROP_DOWN(w));}
static void choose(GtkWidget *w,guint i) {gtk_drop_down_set_selected(GTK_DROP_DOWN(w),i);}
static void model(GtkWidget *w,GtkStringList *rows) {
	gtk_drop_down_set_model(GTK_DROP_DOWN(w),G_LIST_MODEL(rows));
}
static void sync_apply(void) {
	guint m=chosen(refresh),s=chosen(scale);
	gboolean valid=!pending && m<rates->len && s<G_N_ELEMENTS(scales);
	if(valid) {
		struct mode r=g_array_index(rates,struct mode,m);
		valid=(gint64)r.w*100>=640*scales[s] && (gint64)r.h*100>=360*scales[s];
	}
	gtk_widget_set_sensitive(apply,valid);
}
static void update_rates(void) {
	guint n=chosen(resolution);
	g_autoptr(GtkStringList) rows=gtk_string_list_new(NULL);
	g_array_set_size(rates,0);
	guint selected=0;
	if(n<sizes->len) {
		struct mode size=g_array_index(sizes,struct mode,n);
		for(guint i=0;i<modes->len;i++){
			struct mode m=g_array_index(modes,struct mode,i);
			if(m.w!=size.w || m.h!=size.h)continue;
			gboolean duplicate=FALSE;
			for(guint j=0;j<rates->len;j++)
				if(g_array_index(rates,struct mode,j).hz==m.hz){duplicate=TRUE;break;}
			if(duplicate)continue;
			if(m.hz==current.hz)selected=rates->len;
			g_array_append_val(rates,m);
			g_autofree char *label=m.hz%1000
				?g_strdup_printf("%.3f Hz",m.hz/1000.)
				:g_strdup_printf("%d Hz",m.hz/1000);
			gtk_string_list_append(rows,label);
		}
	}
	model(refresh,rows);
	if(rates->len)choose(refresh,selected);
}
static void selection(GObject *w,GParamSpec *p,gpointer data) {
	(void)p;(void)data;
	if(updating || pending)return;
	if((GtkWidget *)w==resolution) {updating=TRUE;update_rates();updating=FALSE;}
	report("");
	sync_apply();
}
static void cancel_clicked(GtkButton *b,gpointer data) {
	(void)b;(void)data;
	if(close_settings)close_settings();
}
static void apply_clicked(GtkButton *b,gpointer data) {
	(void)b;(void)data;
	guint m=chosen(refresh),s=chosen(scale);
	if(pending || m>=rates->len || s>=G_N_ELEMENTS(scales))return;
	struct mode r=g_array_index(rates,struct mode,m);
	if(!panel_display_apply(r.w,r.h,r.hz,scales[s])) {
		report("The display is unavailable.");
		return;
	}
	pending=TRUE;
	report("");
	gtk_widget_set_sensitive(controls,FALSE);
	sync_apply();
}
static GtkWidget *key_button(const char *label,gboolean primary,GCallback action) {
	GtkWidget *w=gtk_button_new_with_label(label);
	home_mechanical_button(w,primary);
	g_signal_connect(w,"clicked",action,NULL);
	return w;
}
static GtkWidget *field(GtkGrid *grid,int row,const char *label) {
	GtkWidget *text=gtk_label_new(label);
	gtk_label_set_xalign(GTK_LABEL(text),0);
	gtk_widget_set_hexpand(text,TRUE);
	gtk_grid_attach(grid,text,0,row,1,1);
	GtkWidget *w=gtk_drop_down_new(NULL,NULL);
	gtk_widget_add_css_class(w,"display-choice");
	gtk_widget_set_size_request(w,130,-1);
	gtk_accessible_update_property(GTK_ACCESSIBLE(w),GTK_ACCESSIBLE_PROPERTY_LABEL,label,-1);
	gtk_grid_attach(grid,w,1,row,1,1);
	g_signal_connect(w,"notify::selected",G_CALLBACK(selection),NULL);
	return w;
}
GtkWidget *display_settings_new(void (*close)(void)) {
	close_settings=close;
	modes=g_array_new(FALSE,FALSE,sizeof(struct mode));
	sizes=g_array_new(FALSE,FALSE,sizeof(struct mode));
	rates=g_array_new(FALSE,FALSE,sizeof(struct mode));
	GtkWidget *body=gtk_box_new(GTK_ORIENTATION_VERTICAL,9);
	gtk_widget_add_css_class(body,"shell-settings-content");
	GtkWidget *head=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);
	GtkWidget *title=gtk_label_new("DISPLAY");
	gtk_widget_add_css_class(title,"shell-settings-title");
	gtk_widget_set_halign(title,GTK_ALIGN_START);
	gtk_widget_set_hexpand(title,TRUE);
	gtk_box_append(GTK_BOX(head),title);
	gtk_box_append(GTK_BOX(head),key_button("Esc",FALSE,G_CALLBACK(cancel_clicked)));
	gtk_box_append(GTK_BOX(body),head);
	gtk_box_append(GTK_BOX(body),gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

	GtkWidget *scroll=gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll),TRUE);
	gtk_widget_set_vexpand(scroll,TRUE);
	gtk_box_append(GTK_BOX(body),scroll);
	GtkWidget *details=gtk_box_new(GTK_ORIENTATION_VERTICAL,9);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),details);
	GtkWidget *identity=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
	gtk_box_append(GTK_BOX(identity),gtk_image_new_from_icon_name("video-display-symbolic"));
	monitor=gtk_label_new("");
	gtk_label_set_ellipsize(GTK_LABEL(monitor),PANGO_ELLIPSIZE_END);
	gtk_box_append(GTK_BOX(identity),monitor);
	gtk_box_append(GTK_BOX(details),identity);
	GtkGrid *grid=GTK_GRID(gtk_grid_new());
	gtk_grid_set_column_spacing(grid,12);
	gtk_grid_set_row_spacing(grid,8);
	controls=GTK_WIDGET(grid);
	gtk_box_append(GTK_BOX(details),controls);
	resolution=field(grid,0,"Resolution");
	refresh=field(grid,1,"Refresh rate");
	scale=field(grid,2,"Scale");
	g_autoptr(GtkStringList) options=gtk_string_list_new(NULL);
	for(guint i=0;i<G_N_ELEMENTS(scales);i++) {
		g_autofree char *label=g_strdup_printf("%d%%",scales[i]);
		gtk_string_list_append(options,label);
	}
	updating=TRUE;
	model(scale,options);
	updating=FALSE;

	notice=gtk_label_new("");
	gtk_widget_add_css_class(notice,"shell-settings-hint");
	gtk_label_set_wrap(GTK_LABEL(notice),TRUE);
	gtk_label_set_xalign(GTK_LABEL(notice),0);
	gtk_box_append(GTK_BOX(details),notice);
	gtk_box_append(GTK_BOX(body),gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

	GtkWidget *actions=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);
	gtk_widget_set_halign(actions,GTK_ALIGN_END);
	gtk_box_append(GTK_BOX(actions),key_button("Cancel",FALSE,G_CALLBACK(cancel_clicked)));
	apply=key_button("Apply",TRUE,G_CALLBACK(apply_clicked));
	gtk_box_append(GTK_BOX(actions),apply);
	gtk_box_append(GTK_BOX(body),actions);
	return body;
}

void display_settings_bind_link(GtkWidget *w) {display_entry=w;gtk_widget_set_visible(display_entry,available);}
void display_settings_available(gboolean on) {
	available=on;
	if(display_entry)gtk_widget_set_visible(display_entry,on);
	if(!on && pending && notice)display_settings_result(0,"The display is unavailable.");
}
void display_settings_open(void) {
	if(pending)return;
	gtk_widget_set_sensitive(controls,FALSE);
	gtk_widget_set_sensitive(apply,FALSE);
	if(!available) {
		report("Display settings are available only in a native session.");
		return;
	}
	report("");
	panel_display_query();
}

void display_settings_current(const char *name,const char *connector,int w,int h,int hz,int s) {
	current=(struct mode){w,h,hz};
	current_scale=s;
	g_array_set_size(modes,0);
	g_autofree char *label=g_strcmp0(name,connector)==0?g_strdup(name):g_strdup_printf("%s · %s",name,connector);
	gtk_label_set_text(GTK_LABEL(monitor),label);
}
void display_settings_mode(int w,int h,int hz) {
	struct mode m={w,h,hz};
	g_array_append_val(modes,m);
}
void display_settings_done(void) {
	updating=TRUE;
	g_autoptr(GtkStringList) rows=gtk_string_list_new(NULL);
	g_array_set_size(sizes,0);
	guint selected=0;
	for(guint i=0;i<modes->len;i++){
		struct mode m=g_array_index(modes,struct mode,i);
		gboolean duplicate=FALSE;
		for(guint n=0;n<sizes->len;n++){
			struct mode p=g_array_index(sizes,struct mode,n);
			if(p.w==m.w && p.h==m.h){duplicate=TRUE;break;}
		}
		if(duplicate)continue;
		if(m.w==current.w && m.h==current.h)selected=sizes->len;
		g_array_append_val(sizes,m);
		g_autofree char *label=g_strdup_printf("%d × %d",m.w,m.h);
		gtk_string_list_append(rows,label);
	}
	model(resolution,rows);
	choose(resolution,selected);
	update_rates();
	guint best=0;
	for(guint i=1;i<G_N_ELEMENTS(scales);i++)
		if(abs(scales[i]-current_scale)<abs(scales[best]-current_scale))best=i;
	choose(scale,best);
	updating=FALSE;
	gtk_widget_set_sensitive(controls,TRUE);
	if(!modes->len)report("No supported display modes.");
	sync_apply();
}

void display_settings_result(unsigned status,const char *message) {
	pending=FALSE;
	gtk_widget_set_sensitive(controls,TRUE);
	sync_apply();
	if(status==1) {if(close_settings)close_settings();return;}
	if(status==2) {
		g_autofree char *text=g_strdup_printf("Applied, but could not save: %s",message);
		report(text);
	} else report(message);
}
