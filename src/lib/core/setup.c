/* $Id: setup.c,v 1.104 2006-11-25 14:08:00 ensonic Exp $
 *
 * Buzztard
 * Copyright (C) 2006 Buzztard team <buzztard-devel@lists.sf.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
/**
 * SECTION:btsetup
 * @short_description: class with all machines and wires (#BtMachine and #BtWire) 
 * for a #BtSong instance
 *
 * The setup manages virtual gear. That is used #BtMachines and the #BtWires
 * that connect them.
 */ 
/* @idea add a methods for dumping the setup as a dot-graph
 * machines and wires should be dumped with details (as subgraphs)!
 */
 
#define BT_CORE
#define BT_SETUP_C

#include <libbtcore/core.h>

//-- signal ids

enum {
  MACHINE_ADDED_EVENT,
  MACHINE_REMOVED_EVENT,
  WIRE_ADDED_EVENT,
  WIRE_REMOVED_EVENT,
  LAST_SIGNAL
};

//-- property ids

enum {
  SETUP_PROPERTIES=1,
  SETUP_SONG,
  SETUP_MACHINES,
  SETUP_WIRES,
  SETUP_MISSING_MACHINES
};

struct _BtSetupPrivate {
  /* used to validate if dispose has run */
  gboolean dispose_has_run;
  
  /* the song the setup belongs to */
  union {
    BtSong *song;
    gconstpointer song_ptr;
  };
  
  GList *machines;         // each entry points to BtMachine
  GList *wires;            // each entry points to BtWire
  GList *missing_machines; // each entry points to a gchar*
  
  /* (ui) properties accociated with this song
     zoom. scroll-position
   */
  GHashTable *properties;
};

static GObjectClass *parent_class=NULL;

static guint signals[LAST_SIGNAL]={0,};

//-- constructor methods

/**
 * bt_setup_new:
 * @song: the song the new instance belongs to
 *
 * Create a new instance
 *
 * Returns: the new instance or %NULL in case of an error
 */
BtSetup *bt_setup_new(const BtSong * const song) {
  g_return_val_if_fail(BT_IS_SONG(song),NULL);

  BtSetup * const self=BT_SETUP(g_object_new(BT_TYPE_SETUP,"song",song,NULL));
  return(self);
}

//-- private methods

/**
 * bt_setup_get_wire_by_machine_type:
 * @self: the setup to search for the wire
 * @machine: the machine that is at the src or dst of the wire
 * @type: the type name (src or dst) that the given machine should have in the wire
 *
 * Searches for the first wire in setup that uses the given #BtMachine as the given
 * type.
 * In other words - it returns the first wire that has the the given #BtMachine as
 * the given type.
 *
 * Returns: the #BtWire or %NULL 
 */
static BtWire *bt_setup_get_wire_by_machine_type(const BtSetup * const self, const BtMachine * const machine, const gchar * const type) {
  gboolean found=FALSE;
  BtMachine * const search_machine;
  GList *node;
  
  for(node=self->priv->wires;node;node=g_list_next(node)) {
    BtWire * const wire=BT_WIRE(node->data);
    g_object_get(G_OBJECT(wire),type,&search_machine,NULL);
    if(search_machine==machine) found=TRUE;
    g_object_try_unref(search_machine);
    if(found) return(g_object_ref(wire));
  }
  GST_DEBUG("no wire found for %s-machine %p",type,machine);
  return(NULL);
}

/**
 * bt_setup_get_wires_by_machine_type:
 * @self: the setup to search for the wires
 * @machine: the machine that is at the src or dst of the wire
 * @type: the type name (src or dst) that the given machine should have in the wire
 *
 * Searches for all wires in setup that uses the given #BtMachine as the given
 * type.
 *
 * Returns: a #GList with the #BtWires or %NULL 
 */
static GList *bt_setup_get_wires_by_machine_type(const BtSetup * const self,const BtMachine * const machine, const gchar * const type) {
  GList *wires=NULL,*node;
  BtMachine * const search_machine;

  for(node=self->priv->wires;node;node=g_list_next(node)) {
    BtWire * const wire=BT_WIRE(node->data);
    g_object_get(G_OBJECT(wire),type,&search_machine,NULL);
    if(search_machine==machine) {
      wires=g_list_prepend(wires,g_object_ref(wire));
    }
    g_object_try_unref(search_machine);
  }
  return(wires);
}

//-- public methods

/**
 * bt_setup_add_machine:
 * @self: the setup to add the machine to
 * @machine: the new machine instance
 *
 * Let the setup know that the suplied machine is now part of the song.
 *
 * Returns: return true, if the machine can be added. Returns false if the
 * machine is currently added to the setup.
 */
gboolean bt_setup_add_machine(const BtSetup * const self, const BtMachine * const machine) {
  gboolean ret=FALSE;

  // @todo add g_error stuff, to give the programmer more information, whats going wrong.
  
  g_return_val_if_fail(BT_IS_SETUP(self),FALSE);
  g_return_val_if_fail(BT_IS_MACHINE(machine),FALSE);

  if(!g_list_find(self->priv->machines,machine)) {
    ret=TRUE;
    self->priv->machines=g_list_append(self->priv->machines,g_object_ref(G_OBJECT(machine)));
    g_signal_emit(G_OBJECT(self),signals[MACHINE_ADDED_EVENT], 0, machine);
    bt_song_set_unsaved(self->priv->song,TRUE);
  }
  else {
    GST_WARNING("trying to add machine again"); 
  }
  return ret;
}

/**
 * bt_setup_add_wire:
 * @self: the setup to add the wire to
 * @wire: the new wire instance
 *
 * Let the setup know that the suplied wire is now part of the song.
 *
 * Returns: returns true, if the wire is added. Returns false, if the setup
 * contains a wire witch is link between the same src and dst machines (cycle
 * check).

 */
gboolean bt_setup_add_wire(const BtSetup * const self, const BtWire * const wire) {
  gboolean ret=FALSE;

  // @todo add g_error stuff, to give the programmer more information, whats going wrong.
  
  g_return_val_if_fail(BT_IS_SETUP(self),FALSE);
  g_return_val_if_fail(BT_IS_WIRE(wire),FALSE);

  if(!g_list_find(self->priv->wires,wire)) {
    BtMachine *src,*dst;
    BtWire *other_wire1,*other_wire2;
    
    // check for wires with equal src and dst machines 
    g_object_get(G_OBJECT(wire),"src",&src,"dst",&dst,NULL);
    other_wire1=bt_setup_get_wire_by_machines(self,src,dst);
    other_wire2=bt_setup_get_wire_by_machines(self,dst,src);
    if((!other_wire1) && (!other_wire2)) {
      ret=TRUE;
      self->priv->wires=g_list_append(self->priv->wires,g_object_ref(G_OBJECT(wire)));
      g_signal_emit(G_OBJECT(self),signals[WIRE_ADDED_EVENT], 0, wire);
      bt_song_set_unsaved(self->priv->song,TRUE);
    }
    g_object_try_unref(other_wire1);
    g_object_try_unref(other_wire2);
    g_object_try_unref(src);
    g_object_try_unref(dst);
  }
  else {
    GST_WARNING("trying to add wire again"); 
  }
  return ret;
}

/**
 * bt_setup_remove_machine:
 * @self: the setup to remove the machine from
 * @machine: the machine instance to remove
 *
 * Let the setup know that the suplied machine is removed from the song.
 */
void bt_setup_remove_machine(const BtSetup * const self, const BtMachine * const machine) {
  g_return_if_fail(BT_IS_SETUP(self));
  g_return_if_fail(BT_IS_MACHINE(machine));

  if(g_list_find(self->priv->machines,machine)) {
    self->priv->machines=g_list_remove(self->priv->machines,machine);
    g_signal_emit(G_OBJECT(self),signals[MACHINE_REMOVED_EVENT], 0, machine);
    GST_DEBUG("removing machine: ref_count=%d",G_OBJECT(machine)->ref_count);
    g_object_unref(G_OBJECT(machine));
    bt_song_set_unsaved(self->priv->song,TRUE);
  }
  else {
    GST_WARNING("trying to remove machine that is not in setup"); 
  }
}

/**
 * bt_setup_remove_wire:
 * @self: the setup to remove the wire from
 * @wire: the wire instance to remove
 *
 * Let the setup know that the suplied wire is removed from the song.
 */
void bt_setup_remove_wire(const BtSetup * const self, const BtWire * const wire) {
  g_return_if_fail(BT_IS_SETUP(self));
  g_return_if_fail(BT_IS_WIRE(wire));

  if(g_list_find(self->priv->wires,wire)) {
    self->priv->wires=g_list_remove(self->priv->wires,wire);
    g_signal_emit(G_OBJECT(self),signals[WIRE_REMOVED_EVENT], 0, wire);
    GST_DEBUG("removing wire: ref_count=%d",G_OBJECT(wire)->ref_count);
    g_object_unref(G_OBJECT(wire));
    bt_song_set_unsaved(self->priv->song,TRUE);
  }
  else {
    GST_WARNING("trying to remove wire that is not in setup"); 
  }
}

/**
 * bt_setup_get_machine_by_id:
 * @self: the setup to search for the machine
 * @id: the identifier of the machine
 *
 * Search the setup for a machine by the supplied id.
 * The machine must have been added previously to this setup with bt_setup_add_machine().
 * Unref the machine, when done with it.
 *
 * Returns: #BtMachine instance or %NULL if not found
 */
BtMachine *bt_setup_get_machine_by_id(const BtSetup * const self, const gchar * const id) {
  gboolean found=FALSE;
  gchar * const machine_id;
  const GList* node;

  g_return_val_if_fail(BT_IS_SETUP(self),NULL);
  g_return_val_if_fail(BT_IS_STRING(id),NULL);
  
  /*
    find_by_property(gpointer item,gpointer data) {
      GValue value;
      g_object_get_property(item,data.key,&value);
      //a) compare via strcmp(find_by_property(value),find_by_property(data.compare_value));
      //b) switch(g_value_get_type(value)) {
      //     G_TYPE_STRING: strcmp(value,data.compare_value);
      //     default: value==data.compare_value;
      //   }
      //-> what about: gst_value_compare()
      // @todo method puts key-key into a gvalue and gets the param-spec by name, then calls generalized search
      //-> what about: g_param_values_cmp()
    }  
   
    struct {
     gchar *key;
     GValue compare_value;
    } data;
    g_value_init(data.compare_value, G_TYPE_STRING);
    g_value_set_string(data.compare_value,id);
    data.key="id";
   
    node = g_list_find_custom(self->priv->machines, data, find_by_property);
    if(node) return(BT_MACHINE(node->data);
  */
  
  for(node=self->priv->machines;node;node=g_list_next(node)) {
    BtMachine * const machine=BT_MACHINE(node->data);
    g_object_get(G_OBJECT(machine),"id",&machine_id,NULL);
    if(!strcmp(machine_id,id)) found=TRUE;
    g_free(machine_id);
    if(found) return(g_object_ref(machine));
  }
  GST_DEBUG("no machine found for id \"%s\"",id);
  return(NULL);
}

/**
 * bt_setup_get_machine_by_index:
 * @self: the setup to search for the machine
 * @index: the list-position of the machine
 *
 * search the setup for a machine by the supplied index position.
 * The machine must have been added previously to this setup with bt_setup_add_machine().
 * Unref the machine, when done with it.
 *
 * Returns: #BtMachine instance or %NULL if not found
 */
BtMachine *bt_setup_get_machine_by_index(const BtSetup * const self, const gulong index) {
  g_return_val_if_fail(BT_IS_SETUP(self),NULL);
  g_return_val_if_fail(index<g_list_length(self->priv->machines),NULL);
  
  return(g_object_ref(BT_MACHINE(g_list_nth_data(self->priv->machines,(guint)index))));
}

/**
 * bt_setup_get_machine_by_type:
 * @self: the setup to search for the machine
 * @type: the gobject type (sink,processor,source)
 *
 * Search the setup for the first machine with the given type.
 * The machine must have been added previously to this setup with bt_setup_add_machine().
 * Unref the machine, when done with it.
 *
 * Returns: #BtMachine instance or %NULL if not found
 */
BtMachine *bt_setup_get_machine_by_type(const BtSetup * const self, const GType type) {
  const GList *node;

  g_return_val_if_fail(BT_IS_SETUP(self),NULL);

  for(node=self->priv->machines;node;node=g_list_next(node)) {
    BtMachine * const machine=BT_MACHINE(node->data);
    if(G_OBJECT_TYPE(machine)==type) {
      return(g_object_ref(machine));
    }
  }
  GST_DEBUG("no machine found for this type");
  return(NULL);
}

/**
 * bt_setup_get_machines_by_type:
 * @self: the setup to search for the machine
 * @type: the gobject type (sink,processor,source)
 *
 * Gathers all machines of the given type from the setup.
 * Free the list (and unref the machine), when done with it.
 *
 * Returns: the list instance or %NULL if not found
 */
GList *bt_setup_get_machines_by_type(const BtSetup * const self, const GType type) {
  GList *machines=NULL;
  const GList * node;

  g_return_val_if_fail(BT_IS_SETUP(self),NULL);

  for(node=self->priv->machines;node;node=g_list_next(node)) {
    BtMachine * const machine=BT_MACHINE(node->data);
    if(G_OBJECT_TYPE(machine)==type) {
      machines=g_list_prepend(machines,g_object_ref(machine));
    }
  }
  return(machines);
}

 
/**
 * bt_setup_get_wire_by_src_machine:
 * @self: the setup to search for the wire
 * @src: the machine that is at the src end of the wire
 *
 * Searches for the first wire in setup that uses the given #BtMachine as a source.
 * In other words - it returns the first wire that starts at the given #BtMachine.
 * Unref the wire, when done with it.
 *
 * Returns: the #BtWire or %NULL 
 */
BtWire *bt_setup_get_wire_by_src_machine(const BtSetup * const self, const BtMachine * const src) {
  g_return_val_if_fail(BT_IS_SETUP(self),NULL);
  g_return_val_if_fail(BT_IS_MACHINE(src),NULL);
  return(bt_setup_get_wire_by_machine_type(self,src,"src"));
}

/**
 * bt_setup_get_wire_by_dst_machine:
 * @self: the setup to search for the wire
 * @dst: the machine that is at the dst end of the wire
 *
 * Searches for the first wire in setup that uses the given #BtMachine as a target.
 * In other words - it returns the first wire that ends at the given #BtMachine.
 * Unref the wire, when done with it.
 *
 * Returns: the #BtWire or %NULL 
 */
BtWire *bt_setup_get_wire_by_dst_machine(const BtSetup *const self, const BtMachine * const dst) {
  g_return_val_if_fail(BT_IS_SETUP(self),NULL);
  g_return_val_if_fail(BT_IS_MACHINE(dst),NULL);
  return(bt_setup_get_wire_by_machine_type(self,dst,"dst"));
}

/**
 * bt_setup_get_wire_by_machines:
 * @self: the setup to search for the wire
 * @src: the machine that is at the src end of the wire
 * @dst: the machine that is at the dst end of the wire
 *
 * Searches for a wire in setup that uses the given #BtMachine instances as a
 * source and dest.
 * Unref the wire, when done with it.
 *
 * Returns: the #BtWire or NULL
 */
BtWire *bt_setup_get_wire_by_machines(const BtSetup * const self, const BtMachine * const src, const BtMachine * const dst) {
  gboolean found=FALSE;
  BtMachine * const src_machine, * const dst_machine;
  const GList *node;

  g_return_val_if_fail(BT_IS_SETUP(self),NULL);
  g_return_val_if_fail(BT_IS_MACHINE(src),NULL);
  g_return_val_if_fail(BT_IS_MACHINE(dst),NULL);
   
  for(node=self->priv->wires;node;node=g_list_next(node)) {
    BtWire * const wire=BT_WIRE(node->data);
    g_object_get(G_OBJECT(wire),"src",&src_machine,"dst",&dst_machine,NULL);
    if((src_machine==src) && (dst_machine==dst))found=TRUE;
    g_object_try_unref(src_machine);
    g_object_try_unref(dst_machine);
    if(found) return(g_object_ref(wire));
  }
  GST_DEBUG("no wire found for machines %p %p",src,dst);
  return(NULL);
}

/**
 * bt_setup_get_wires_by_src_machine:
 * @self: the setup to search for the wire
 * @src: the machine that is at the src end of the wire
 *
 * Searches for all wires in setup that use the given #BtMachine as a source.
 * Free the list (and unref the wires), when done with it.
 *
 * Returns: a #GList with the #BtWires or %NULL 
 */
GList *bt_setup_get_wires_by_src_machine(const BtSetup * const self, const BtMachine * const src) {
  g_return_val_if_fail(BT_IS_SETUP(self),NULL);
  g_return_val_if_fail(BT_IS_MACHINE(src),NULL);
  return(bt_setup_get_wires_by_machine_type(self,src,"src"));
}

/**
 * bt_setup_get_wires_by_dst_machine:
 * @self: the setup to search for the wire
 * @dst: the machine that is at the dst end of the wire
 *
 * Searches for all wires in setup that use the given #BtMachine as a target.
 * Free the list (and unref the wires), when done with it.
 *
 * Returns: a #GList with the #BtWires or %NULL 
 */
GList *bt_setup_get_wires_by_dst_machine(const BtSetup * const self, const BtMachine * const dst) {
  g_return_val_if_fail(BT_IS_SETUP(self),NULL);
  g_return_val_if_fail(BT_IS_MACHINE(dst),NULL);
  return(bt_setup_get_wires_by_machine_type(self,dst,"dst"));
}

/**
 * bt_setup_get_unique_machine_id:
 * @self: the setup for which the name should be unique
 * @base_name: the leading name part
 *
 * The function makes the supplied base_name unique in this setup by eventually
 * adding a number postfix. This method should be used when adding new machines.
 *
 * Returns: the newly allocated unique name
 */
gchar *bt_setup_get_unique_machine_id(const BtSetup * const self, gchar * const base_name) {
  BtMachine *machine;
  guint8 i=0;
  
  g_return_val_if_fail(BT_IS_SETUP(self),NULL);
  g_return_val_if_fail(BT_IS_STRING(base_name),NULL);
  
  if(!(machine=bt_setup_get_machine_by_id(self,base_name))) {
    return(g_strdup(base_name));
  }
  else {
    g_object_unref(machine);
    machine=NULL;
  }

  gchar * const id=g_strdup_printf("%s 00",base_name);
  gchar * const ptr=&id[strlen(base_name)+1];
  do {
    (void)g_sprintf(ptr,"%02u",i++);
    g_object_try_unref(machine);
  } while((machine=bt_setup_get_machine_by_id(self,id)) && (i<100));
  g_object_try_unref(machine);
  return(id);
}

/**
 * bt_setup_remember_missing_machine:
 * @self: the setup
 * @str: human readable description of the missing machine
 *
 * Loaders can use this function to collect information about machines that
 * failed to load.
 * The front-end can access this later by reading BtSetup::missing-machines
 * property.
 */
void bt_setup_remember_missing_machine(const BtSetup * const self, const gchar * const str) {
  GST_INFO("missing machine %s",str);
  self->priv->missing_machines=g_list_prepend(self->priv->missing_machines,(gpointer)str);
}

//-- io interface

static xmlNodePtr bt_setup_persistence_save(const BtPersistence * const persistence, xmlNodePtr const parent_node, const BtPersistenceSelection * const selection) {
  BtSetup * const self = BT_SETUP(persistence);
  xmlNodePtr node=NULL;
  xmlNodePtr child_node;
  
  GST_DEBUG("PERSISTENCE::setup");

  if((node=xmlNewChild(parent_node,NULL,XML_CHAR_PTR("setup"),NULL))) {
    if((child_node=xmlNewChild(node,NULL,XML_CHAR_PTR("properties"),NULL))) {
      if(!bt_persistence_save_hashtable(self->priv->properties, child_node)) goto Error;
    }
    else goto Error;
    if((child_node=xmlNewChild(node,NULL,XML_CHAR_PTR("machines"),NULL))) {
      bt_persistence_save_list(self->priv->machines,child_node);
    }
    else goto Error;
    if((child_node=xmlNewChild(node,NULL,XML_CHAR_PTR("wires"),NULL))) {
      bt_persistence_save_list(self->priv->wires,child_node);
    }
    else goto Error;
  }
Error:
  return(node);
}

static gboolean bt_setup_persistence_load(const BtPersistence * const persistence, xmlNodePtr node, const BtPersistenceLocation * const location) {
  BtSetup * const self = BT_SETUP(persistence);
  xmlNodePtr child_node;
  
  GST_DEBUG("PERSISTENCE::setup");
  g_assert(node);
  
  for(node=node->children;node;node=node->next) {
    if(!xmlNodeIsText(node)) {
      if(!strncmp((gchar *)node->name,"machines\0",9)) {
        GType type=0;
        
        //bt_song_io_native_load_setup_machines(self,song,node->children);
        for(child_node=node->children;child_node;child_node=child_node->next) {
          if(!xmlNodeIsText(child_node)) {
            if(!strncmp((gchar *)child_node->name,"machine\0",8)) {
              xmlChar * const type_str=xmlGetProp(child_node,XML_CHAR_PTR("type"));
              if(!strncmp((gchar *)type_str,"processor\0",10)) {
                type=BT_TYPE_PROCESSOR_MACHINE;
              }
              else if(!strncmp((gchar *)type_str,"sink\0",5)) {
                type=BT_TYPE_SINK_MACHINE;
              }
              else if(!strncmp((gchar *)type_str,"source\0",7)) {
                type=BT_TYPE_SOURCE_MACHINE;
              }
              else {
                GST_WARNING("machine node has no type");
              }
              if(type) {
                BtMachine * const machine=BT_MACHINE(g_object_new(type,"song",self->priv->song,NULL));
                if(bt_persistence_load(BT_PERSISTENCE(machine),child_node,NULL)) {
                  bt_setup_add_machine(self,machine);
                }
                else {
                  // collect failed machines
                  gchar * const id,* const plugin_name;
                  
                  g_object_get(machine,"id",&id,"plugin-name",&plugin_name,NULL);        
                  const gchar * const str=g_strdup_printf("%s: %s",id, plugin_name);  // str is freed elsewhere
                  bt_setup_remember_missing_machine(self,str);
                  g_free(id);
		              g_free(plugin_name);
                }
                g_object_unref(machine);
              }
              xmlFree(type_str);
            }
          }
        }
      }
      else if(!strncmp((gchar *)node->name,"wires\0",6)) {
        for(child_node=node->children;child_node;child_node=child_node->next) {
          if(!xmlNodeIsText(child_node)) {
            BtWire * const wire=BT_WIRE(g_object_new(BT_TYPE_WIRE,"song",self->priv->song,NULL));
            if(bt_persistence_load(BT_PERSISTENCE(wire),child_node,NULL)) {
              bt_setup_add_wire(self,wire);
            }
            g_object_unref(wire);
          }
        }
      }
      else if(!strncmp((gchar *)node->name,"properties\0",11)) {
        bt_persistence_load_hashtable(self->priv->properties,node);
      }
    }
  }
  return(TRUE);
}

static void bt_setup_persistence_interface_init(gpointer const g_iface, gpointer const iface_data) {
  BtPersistenceInterface * const iface = g_iface;
  
  iface->load = bt_setup_persistence_load;
  iface->save = bt_setup_persistence_save;
}

//-- wrapper

//-- class internals

/* returns a property for the given property_id for this object */
static void bt_setup_get_property(GObject      * const object,
                               const guint         property_id,
                               GValue       * const value,
                               GParamSpec   * const pspec)
{
  const BtSetup * const self = BT_SETUP(object);
  return_if_disposed();
  switch (property_id) {
    case SETUP_PROPERTIES: {
      g_value_set_pointer(value, self->priv->properties);
    } break;
    case SETUP_SONG: {
      g_value_set_object(value, self->priv->song);
    } break;
    case SETUP_MACHINES: {
      /* @todo: this is not good, lists returned by bt_setup_get_xxx_by_yyy
       * returns a new list where one has to unref the elements
       */
      g_value_set_pointer(value,g_list_copy(self->priv->machines));
    } break;
    case SETUP_WIRES: {
      /* @todo: this is not good, lists returned by bt_setup_get_xxx_by_yyy
       * returns a new list where one has to unref the elements
       */
      g_value_set_pointer(value,g_list_copy(self->priv->wires));
    } break;
    case SETUP_MISSING_MACHINES: {
      g_value_set_pointer(value,self->priv->missing_machines);
    } break;
    default: {
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object,property_id,pspec);
    } break;
  }
}

/* sets the given properties for this object */
static void bt_setup_set_property(GObject      * const object,
                              const guint         property_id,
                              const GValue * const value,
                              GParamSpec   * const pspec)
{
  const BtSetup * const self = BT_SETUP(object);
  return_if_disposed();
  switch (property_id) {
    case SETUP_SONG: {
      g_object_try_weak_unref(self->priv->song);
      self->priv->song = BT_SONG(g_value_get_object(value));
      g_object_try_weak_ref(self->priv->song);
      //GST_DEBUG("set the song for setup: %p",self->priv->song);
    } break;
    default: {
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object,property_id,pspec);
    } break;
  }
}

static void bt_setup_dispose(GObject * const object) {
  BtSetup * const self = BT_SETUP(object);
  GList* node;

  return_if_disposed();
  self->priv->dispose_has_run = TRUE;

  GST_DEBUG("!!!! self=%p",self);

  g_object_try_weak_unref(self->priv->song);
  // unref list of wires
  if(self->priv->wires) {
    for(node=self->priv->wires;node;node=g_list_next(node)) {
      if(node->data) {
        const GObject * const obj=node->data;
        GST_DEBUG("  free wire : %p (%d)",obj,obj->ref_count);
      }
      g_object_try_unref(node->data);
      node->data=NULL;
    }
  }
  // unref list of machines
  if(self->priv->machines) {
    for(node=self->priv->machines;node;node=g_list_next(node)) {
      if(node->data) {
        const GObject * const obj=node->data;
        GST_DEBUG("  free machine : %p (%d)",obj,obj->ref_count);
      }
      g_object_try_unref(node->data);
      node->data=NULL;
    }
  }

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void bt_setup_finalize(GObject * const object) {
  BtSetup * const self = BT_SETUP(object);

  GST_DEBUG("!!!! self=%p",self);

  // free list of wires
  if(self->priv->wires) {
    g_list_free(self->priv->wires);
    self->priv->wires=NULL;
  }
  // free list of machines
  if(self->priv->machines) {
    g_list_free(self->priv->machines);
    self->priv->machines=NULL;
  }
  // free list of missing_machines
  if(self->priv->missing_machines) {
    const GList* node;
    for(node=self->priv->missing_machines;node;node=g_list_next(node)) {
      g_free(node->data);
    }
    g_list_free(self->priv->missing_machines);
    self->priv->missing_machines=NULL;
  }

  g_hash_table_destroy(self->priv->properties);
  
  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void bt_setup_init(const GTypeInstance * const instance, gconstpointer g_class) {
  BtSetup * const self = BT_SETUP(instance);
  
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self, BT_TYPE_SETUP, BtSetupPrivate);
  
  self->priv->properties=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
}

static void bt_setup_class_init(BtSetupClass * const klass) {
  GObjectClass * const gobject_class = G_OBJECT_CLASS(klass);

  parent_class=g_type_class_peek_parent(klass);
  g_type_class_add_private(klass,sizeof(BtSetupPrivate));

  gobject_class->set_property = bt_setup_set_property;
  gobject_class->get_property = bt_setup_get_property;
  gobject_class->dispose      = bt_setup_dispose;
  gobject_class->finalize     = bt_setup_finalize;

  klass->machine_added_event = NULL;
  klass->wire_added_event = NULL;
  klass->machine_removed_event = NULL;
  klass->wire_removed_event = NULL;
  
  /** 
   * BtSetup::machine-added:
   * @self: the setup object that emitted the signal
   * @machine: the new machine
   *
   * A new machine item has been added to the setup
   */
  signals[MACHINE_ADDED_EVENT] = g_signal_new("machine-added",
                                        G_TYPE_FROM_CLASS(klass),
                                        G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
                                        G_ABS_STRUCT_OFFSET(BtSetupClass,machine_added_event),
                                        NULL, // accumulator
                                        NULL, // acc data
                                        g_cclosure_marshal_VOID__POINTER,
                                        G_TYPE_NONE, // return type
                                        1, // n_params
                                        BT_TYPE_MACHINE // param data
                                        );

  /** 
   * BtSetup::wire-added:
   * @self: the setup object that emitted the signal
   * @wire: the new wire
   *
   * A new wire item has been added to the setup
   */
  signals[WIRE_ADDED_EVENT] = g_signal_new("wire-added",
                                        G_TYPE_FROM_CLASS(klass),
                                        G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
                                        G_ABS_STRUCT_OFFSET(BtSetupClass,wire_added_event),
                                        NULL, // accumulator
                                        NULL, // acc data
                                        g_cclosure_marshal_VOID__POINTER,
                                        G_TYPE_NONE, // return type
                                        1, // n_params
                                        BT_TYPE_WIRE // param data
                                        );

  /**
   * BtSetup::machine-removed:
   * @self: the setup object that emitted the signal
   * @machine: the old machine
   *
   * A machine item has been removed from the setup
   */
  signals[MACHINE_REMOVED_EVENT] = g_signal_new("machine-removed",
                                        G_TYPE_FROM_CLASS(klass),
                                        G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
                                        G_ABS_STRUCT_OFFSET(BtSetupClass,machine_removed_event),
                                        NULL, // accumulator
                                        NULL, // acc data
                                        g_cclosure_marshal_VOID__POINTER,
                                        G_TYPE_NONE, // return type
                                        1, // n_params
                                        BT_TYPE_MACHINE // param data
                                        );
  
  /** 
   * BtSetup::wire-removed:
   * @self: the setup object that emitted the signal
   * @wire: the old wire
   *
   * A wire item has been removed from the setup
   */
  signals[WIRE_REMOVED_EVENT] = g_signal_new("wire-removed",
                                        G_TYPE_FROM_CLASS(klass),
                                        G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
                                        G_ABS_STRUCT_OFFSET(BtSetupClass,wire_removed_event),
                                        NULL, // accumulator
                                        NULL, // acc data
                                        g_cclosure_marshal_VOID__POINTER,
                                        G_TYPE_NONE, // return type
                                        1, // n_params
                                        BT_TYPE_WIRE // param data
                                        );

  g_object_class_install_property(gobject_class,SETUP_PROPERTIES,
                                  g_param_spec_pointer("properties",
                                     "properties prop",
                                     "list of setup properties",
                                     G_PARAM_READABLE));

  g_object_class_install_property(gobject_class,SETUP_SONG,
                                  g_param_spec_object("song",
                                     "song contruct prop",
                                     "Set song object, the setup belongs to",
                                     BT_TYPE_SONG, /* object type */
                                     G_PARAM_CONSTRUCT_ONLY |G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class,SETUP_MACHINES,
                                  g_param_spec_pointer("machines",
                                     "machine list prop",
                                     "A copy of the list of machines",
                                     G_PARAM_READABLE));

  g_object_class_install_property(gobject_class,SETUP_WIRES,
                                  g_param_spec_pointer("wires",
                                     "wire list prop",
                                     "A copy of the list of wires",
                                     G_PARAM_READABLE));

  g_object_class_install_property(gobject_class,SETUP_MISSING_MACHINES,
                                  g_param_spec_pointer("missing-machines",
                                     "missing-machines list prop",
                                     "The list of missing machines, don't change",
                                     G_PARAM_READABLE));
}

GType bt_setup_get_type(void) {
  static GType type = 0;
  if (G_UNLIKELY(type == 0)) {
    static const GTypeInfo info = {
      G_STRUCT_SIZE(BtSetupClass),
      NULL, // base_init
      NULL, // base_finalize
      (GClassInitFunc)bt_setup_class_init, // class_init
      NULL, // class_finalize
      NULL, // class_data
      G_STRUCT_SIZE(BtSetup),
      0,   // n_preallocs
      (GInstanceInitFunc)bt_setup_init, // instance_init
      NULL // value_table
    };
    static const GInterfaceInfo persistence_interface_info = {
      (GInterfaceInitFunc) bt_setup_persistence_interface_init,  // interface_init
      NULL, // interface_finalize
      NULL  // interface_data
    };
    type = g_type_register_static(G_TYPE_OBJECT,"BtSetup",&info,0);
    g_type_add_interface_static(type, BT_TYPE_PERSISTENCE, &persistence_interface_info);
  }
  return type;
}
