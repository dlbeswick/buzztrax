/* $Id: main-menu.c,v 1.21 2004-11-18 17:58:16 ensonic Exp $
 * class for the editor main menu
 */

#define BT_EDIT
#define BT_MAIN_MENU_C

#include "bt-edit.h"

enum {
  MAIN_MENU_APP=1,
};


struct _BtMainMenuPrivate {
  /* used to validate if dispose has run */
  gboolean dispose_has_run;

  /* the application */
  BtEditApplication *app;
};

static GtkMenuBarClass *parent_class=NULL;

//-- event handler

static void on_menu_quit_activate(GtkMenuItem *menuitem,gpointer user_data) {
  gboolean quit;
  BtMainMenu *self=BT_MAIN_MENU(user_data);
  BtMainWindow *main_window;

  g_assert(user_data);

  GST_INFO("menu quit event occurred");
  g_object_get(G_OBJECT(self->priv->app),"main-window",&main_window,NULL);
  quit=bt_main_window_check_quit(main_window);
  g_object_try_unref(main_window);
  if(quit) gtk_main_quit();
}

static void on_menu_new_activate(GtkMenuItem *menuitem,gpointer user_data) {
  BtMainMenu *self=BT_MAIN_MENU(user_data);
  BtMainWindow *main_window;

  g_assert(user_data);

  GST_INFO("menu new event occurred");
  g_object_get(G_OBJECT(self->priv->app),"main-window",&main_window,NULL);
  bt_main_window_new_song(main_window);
  g_object_try_unref(main_window);
}

static void on_menu_open_activate(GtkMenuItem *menuitem,gpointer user_data) {
  BtMainMenu *self=BT_MAIN_MENU(user_data);
  BtMainWindow *main_window;

  g_assert(user_data);

  GST_INFO("menu open event occurred");
  g_object_get(G_OBJECT(self->priv->app),"main-window",&main_window,NULL);
  bt_main_window_open_song(main_window);
  g_object_try_unref(main_window);
}

static void on_menu_save_activate(GtkMenuItem *menuitem,gpointer user_data) {
  BtMainMenu *self=BT_MAIN_MENU(user_data);
  BtMainWindow *main_window;

  g_assert(user_data);

  GST_INFO("menu save event occurred");
  g_object_get(G_OBJECT(self->priv->app),"main-window",&main_window,NULL);
  bt_main_window_save_song(main_window);
  g_object_try_unref(main_window);
}

static void on_menu_saveas_activate(GtkMenuItem *menuitem,gpointer user_data) {
  BtMainMenu *self=BT_MAIN_MENU(user_data);
  BtMainWindow *main_window;

  g_assert(user_data);

  GST_INFO("menu saveas event occurred");
  g_object_get(G_OBJECT(self->priv->app),"main-window",&main_window,NULL);
  bt_main_window_save_song_as(main_window);
  g_object_try_unref(main_window);
}

static void on_menu_settings_activate(GtkMenuItem *menuitem,gpointer user_data) {
  BtMainMenu *self=BT_MAIN_MENU(user_data);
  BtMainWindow *main_window;
  GtkWidget *dialog;
  
  g_assert(user_data);

  GST_INFO("menu settings event occurred");
  g_object_get(G_OBJECT(self->priv->app),"main-window",&main_window,NULL);
  
  dialog=GTK_WIDGET(bt_settings_dialog_new(self->priv->app/*,main_window*/));
  
  gtk_widget_show_all(dialog);
                                                  
  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
  g_object_try_unref(main_window);
}

static void on_menu_about_activate(GtkMenuItem *menuitem,gpointer user_data) {
  BtMainMenu *self=BT_MAIN_MENU(user_data);
  BtMainWindow *main_window;
  GtkWidget *label,*icon,*box;
  GtkWidget *dialog;
  gchar *str;
  
  g_assert(user_data);

  GST_INFO("menu about event occurred");
  g_object_get(G_OBJECT(self->priv->app),"main-window",&main_window,NULL);
  dialog = gtk_dialog_new_with_buttons(_("About ..."),
                                        GTK_WINDOW(main_window),
                                        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                        GTK_STOCK_OK,
                                        GTK_RESPONSE_ACCEPT,
                                        NULL);

  box=gtk_hbox_new(FALSE,12);
  gtk_container_set_border_width(GTK_CONTAINER(box),6);

  icon=gtk_image_new_from_stock(GTK_STOCK_DIALOG_INFO,GTK_ICON_SIZE_DIALOG);
  gtk_container_add(GTK_CONTAINER(box),icon);
  
  label=gtk_label_new(NULL);
  str=g_strdup_printf(
    "<big><b>"PACKAGE_STRING"</b></big>\n\n%s\n\nhttp://www.buzztard.org",
    _("brought to you by\n\nStefan 'ensonic' Kost\nThomas 'waffel' Wabner")
  );
  gtk_label_set_markup(GTK_LABEL(label),str);
  g_free(str);
  gtk_container_add(GTK_CONTAINER(box),label);
  gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox),box);
  gtk_widget_show_all(dialog);

  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
  g_object_try_unref(main_window);
}


//-- helper methods

static gboolean bt_main_menu_init_ui(const BtMainMenu *self,GtkAccelGroup *accel_group) {
  GtkWidget *item,*menu,*subitem,*image;
  
  gtk_widget_set_name(GTK_WIDGET(self),_("main menu"));

  //-- file menu
  item=gtk_menu_item_new_with_mnemonic(_("_File"));
  gtk_widget_set_name(item,_("file menu"));
  gtk_container_add(GTK_CONTAINER(self),item);

  menu=gtk_menu_new();
  gtk_widget_set_name(menu,_("file menu"));
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),menu);

  subitem=gtk_image_menu_item_new_from_stock(GTK_STOCK_NEW,accel_group);
  gtk_widget_set_name(subitem,_("New"));
  gtk_container_add(GTK_CONTAINER(menu),subitem);
  g_signal_connect(G_OBJECT(subitem),"activate",G_CALLBACK(on_menu_new_activate),(gpointer)self);

  subitem=gtk_image_menu_item_new_from_stock(GTK_STOCK_OPEN,accel_group);
  gtk_widget_set_name(subitem,_("Open"));
  gtk_container_add(GTK_CONTAINER(menu),subitem);
  g_signal_connect(G_OBJECT(subitem),"activate",G_CALLBACK(on_menu_open_activate),(gpointer)self);

  subitem=gtk_separator_menu_item_new();
  gtk_widget_set_name(subitem,_("separator"));
  gtk_container_add(GTK_CONTAINER(menu),subitem);
  gtk_widget_set_sensitive(subitem,FALSE);

  subitem=gtk_image_menu_item_new_from_stock(GTK_STOCK_SAVE,accel_group);
  gtk_widget_set_name(subitem,_("Save"));
  gtk_container_add(GTK_CONTAINER(menu),subitem);
	g_signal_connect(G_OBJECT(subitem),"activate",G_CALLBACK(on_menu_save_activate),(gpointer)self);

  subitem=gtk_image_menu_item_new_from_stock(GTK_STOCK_SAVE_AS,accel_group);
  gtk_widget_set_name(subitem,_("Save as"));
  gtk_container_add(GTK_CONTAINER(menu),subitem);
	g_signal_connect(G_OBJECT(subitem),"activate",G_CALLBACK(on_menu_saveas_activate),(gpointer)self);

  subitem=gtk_separator_menu_item_new();
  gtk_widget_set_name(subitem,_("separator"));
  gtk_container_add(GTK_CONTAINER(menu),subitem);
  gtk_widget_set_sensitive(subitem,FALSE);

  subitem=gtk_image_menu_item_new_from_stock(GTK_STOCK_QUIT,accel_group);
  gtk_widget_set_name(subitem,_("Quit"));
  gtk_container_add(GTK_CONTAINER(menu),subitem);
  g_signal_connect(G_OBJECT(subitem),"activate",G_CALLBACK(on_menu_quit_activate),(gpointer)self);

  // edit menu
  item=gtk_menu_item_new_with_mnemonic(_("_Edit"));
  gtk_widget_set_name(item,_("edit menu"));
  gtk_container_add(GTK_CONTAINER(self),item);

  menu=gtk_menu_new();
  gtk_widget_set_name(menu,_("edit menu"));
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),menu);

  subitem=gtk_image_menu_item_new_from_stock(GTK_STOCK_CUT,accel_group);
  gtk_widget_set_name(subitem,_("cut"));
  gtk_container_add(GTK_CONTAINER(menu),subitem);

  subitem=gtk_image_menu_item_new_from_stock(GTK_STOCK_COPY,accel_group);
  gtk_widget_set_name(subitem,_("copy"));
  gtk_container_add(GTK_CONTAINER(menu),subitem);

  subitem=gtk_image_menu_item_new_from_stock(GTK_STOCK_PASTE,accel_group);
  gtk_widget_set_name(subitem,_("paste"));
  gtk_container_add(GTK_CONTAINER(menu),subitem);

  subitem=gtk_image_menu_item_new_from_stock(GTK_STOCK_DELETE,accel_group);
  gtk_widget_set_name(subitem,_("delete"));
  gtk_container_add(GTK_CONTAINER(menu),subitem);

  subitem=gtk_separator_menu_item_new();
  gtk_widget_set_name(subitem,_("separator"));
  gtk_container_add(GTK_CONTAINER(menu),subitem);
  gtk_widget_set_sensitive(subitem,FALSE);

  subitem=gtk_image_menu_item_new_from_stock(GTK_STOCK_PREFERENCES,accel_group);
  gtk_widget_set_name(subitem,_("settings"));
  gtk_container_add(GTK_CONTAINER(menu),subitem);
  g_signal_connect(G_OBJECT(subitem),"activate",G_CALLBACK(on_menu_settings_activate),(gpointer)self);
  
  // view menu
  item=gtk_menu_item_new_with_mnemonic(_("_View"));
  gtk_widget_set_name(item,_("view menu"));
  gtk_container_add(GTK_CONTAINER(self),item);

  menu=gtk_menu_new();
  gtk_widget_set_name(menu,_("view menu"));
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),menu);

  subitem=gtk_check_menu_item_new_with_mnemonic(_("Toolbar"));
  gtk_widget_set_name(subitem,_("Toolbar"));
  gtk_container_add(GTK_CONTAINER(menu),subitem);

  // help menu
  item=gtk_menu_item_new_with_mnemonic(_("_Help"));
  gtk_widget_set_name(item,_("help menu"));
  gtk_menu_item_right_justify(GTK_MENU_ITEM(item));
  gtk_container_add(GTK_CONTAINER(self),item);

  menu=gtk_menu_new();
  gtk_widget_set_name(menu,_("help menu"));
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),menu);

  subitem=gtk_image_menu_item_new_from_stock(GTK_STOCK_HELP,accel_group);
  gtk_widget_set_name(subitem,_("Content"));
  gtk_container_add(GTK_CONTAINER(menu),subitem);

  subitem=gtk_image_menu_item_new_with_mnemonic(_("About"));
  gtk_widget_set_name(subitem,_("About"));
  gtk_container_add(GTK_CONTAINER(menu),subitem);
  image=create_pixmap("stock_about.png");
  gtk_widget_set_name(image,_("About"));
  gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(subitem),image);
  g_signal_connect(G_OBJECT(subitem),"activate",G_CALLBACK(on_menu_about_activate),(gpointer)self);

  return(TRUE);
}

//-- constructor methods

/**
 * bt_main_menu_new:
 * @app: the application the menu belongs to
 * @accel_group: the main-windows accelerator group
 *
 * Create a new instance
 *
 * Returns: the new instance or NULL in case of an error
 */
BtMainMenu *bt_main_menu_new(const BtEditApplication *app,GtkAccelGroup *accel_group) {
  BtMainMenu *self;

  if(!(self=BT_MAIN_MENU(g_object_new(BT_TYPE_MAIN_MENU,"app",app,NULL)))) {
    goto Error;
  }
  // generate UI
  if(!bt_main_menu_init_ui(self,accel_group)) {
    goto Error;
  }
  return(self);
Error:
  g_object_try_unref(self);
  return(NULL);
}

//-- methods


//-- class internals

/* returns a property for the given property_id for this object */
static void bt_main_menu_get_property(GObject      *object,
                               guint         property_id,
                               GValue       *value,
                               GParamSpec   *pspec)
{
  BtMainMenu *self = BT_MAIN_MENU(object);
  return_if_disposed();
  switch (property_id) {
    case MAIN_MENU_APP: {
      g_value_set_object(value, self->priv->app);
    } break;
    default: {
 			G_OBJECT_WARN_INVALID_PROPERTY_ID(object,property_id,pspec);
    } break;
  }
}

/* sets the given properties for this object */
static void bt_main_menu_set_property(GObject      *object,
                              guint         property_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  BtMainMenu *self = BT_MAIN_MENU(object);
  return_if_disposed();
  switch (property_id) {
    case MAIN_MENU_APP: {
      g_object_try_unref(self->priv->app);
      self->priv->app = g_object_try_ref(g_value_get_object(value));
      //GST_DEBUG("set the app for main_menu: %p",self->priv->app);
    } break;
    default: {
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object,property_id,pspec);
    } break;
  }
}

static void bt_main_menu_dispose(GObject *object) {
  BtMainMenu *self = BT_MAIN_MENU(object);
	return_if_disposed();
  self->priv->dispose_has_run = TRUE;

  g_object_try_unref(self->priv->app);

  if(G_OBJECT_CLASS(parent_class)->dispose) {
    (G_OBJECT_CLASS(parent_class)->dispose)(object);
  }
}

static void bt_main_menu_finalize(GObject *object) {
  BtMainMenu *self = BT_MAIN_MENU(object);
  
  g_free(self->priv);

  if(G_OBJECT_CLASS(parent_class)->finalize) {
    (G_OBJECT_CLASS(parent_class)->finalize)(object);
  }
}

static void bt_main_menu_init(GTypeInstance *instance, gpointer g_class) {
  BtMainMenu *self = BT_MAIN_MENU(instance);
  self->priv = g_new0(BtMainMenuPrivate,1);
  self->priv->dispose_has_run = FALSE;
}

static void bt_main_menu_class_init(BtMainMenuClass *klass) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

  parent_class=g_type_class_ref(GTK_TYPE_MENU_BAR);
  
  gobject_class->set_property = bt_main_menu_set_property;
  gobject_class->get_property = bt_main_menu_get_property;
  gobject_class->dispose      = bt_main_menu_dispose;
  gobject_class->finalize     = bt_main_menu_finalize;

  g_object_class_install_property(gobject_class,MAIN_MENU_APP,
                                  g_param_spec_object("app",
                                     "app contruct prop",
                                     "Set application object, the menu belongs to",
                                     BT_TYPE_EDIT_APPLICATION, /* object type */
                                     G_PARAM_CONSTRUCT_ONLY |G_PARAM_READWRITE));
}

GType bt_main_menu_get_type(void) {
  static GType type = 0;
  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (BtMainMenuClass),
      NULL, // base_init
      NULL, // base_finalize
      (GClassInitFunc)bt_main_menu_class_init, // class_init
      NULL, // class_finalize
      NULL, // class_data
      sizeof (BtMainMenu),
      0,   // n_preallocs
	    (GInstanceInitFunc)bt_main_menu_init, // instance_init
    };
		type = g_type_register_static(GTK_TYPE_MENU_BAR,"BtMainMenu",&info,0);
  }
  return type;
}
