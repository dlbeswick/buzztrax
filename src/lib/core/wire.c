/* $Id: wire.c,v 1.15 2004-07-02 13:44:50 ensonic Exp $
 * class for a machine to machine connection
 */
 
#define BT_CORE
#define BT_WIRE_C

#include <libbtcore/core.h>

enum {
  WIRE_SONG=1,
	WIRE_SRC,
	WIRE_DST
};

struct _BtWirePrivate {
  /* used to validate if dispose has run */
  gboolean dispose_has_run;
	
	/* the song the wire belongs to */
	BtSong *song;
	/* which machines are linked */
	BtMachine *src,*dst;
	// wire type adapter elements
	GstElement *convert,*scale;
	// convinience pointer
	GstElement *dst_elem,*src_elem;
};

//-- helper methods

/**
 * bt_wire_link_machines:
 * @self: the wire that should be used for this connection
 *
 * Links the gst-element of this wire and tries a couple for conversion elements
 * if necessary.
 *
 * Returns: true for success
 */
static gboolean bt_wire_link_machines(const BtWire *self) {
  BtMachine *src, *dst;
  BtSong *song=self->private->song;
  GstBin *bin;
  GValue val={0,};
	g_value_init(&val,G_TYPE_OBJECT);

  src=self->private->src;
  dst=self->private->dst;

  g_object_get_property(G_OBJECT(song),"bin", &val);
  bin=GST_BIN(g_value_get_object(&val));

	GST_INFO("trying to link machines");
	// try link src to dst {directly, with convert, with scale, with ...}
	if(!gst_element_link(src->src_elem, dst->dst_elem)) {
		if(!self->private->convert) {
			self->private->convert=gst_element_factory_make("audioconvert",g_strdup_printf("audioconvert_%p",self));
			g_assert(self->private->convert!=NULL);
		}
		gst_bin_add(bin, self->private->convert);
		if(!gst_element_link_many(src->src_elem, self->private->convert, dst->dst_elem, NULL)) {
			if(!self->private->scale) {
				self->private->scale=gst_element_factory_make("audioscale",g_strdup_printf("audioscale_%p",self));
				g_assert(self->private->scale!=NULL);
			}
			gst_bin_add(bin, self->private->scale);
			if(!gst_element_link_many(src->src_elem, self->private->scale, dst->dst_elem, NULL)) {
				if(!gst_element_link_many(src->src_elem, self->private->convert, self->private->scale, dst->dst_elem, NULL)) {
					// try harder (scale, convert)
					GST_DEBUG("failed to link the machines");return(FALSE);
				}
				else {
					self->private->src_elem=self->private->scale;
					self->private->dst_elem=self->private->convert;
					GST_INFO("  wire okay with convert and scale");
				}
			}
			else {
				self->private->src_elem=self->private->scale;
				self->private->dst_elem=self->private->scale;
        GST_INFO("  wire okay with scale");
			}
		}
		else {
			self->private->src_elem=self->private->convert;
			self->private->dst_elem=self->private->convert;
			GST_INFO("  wire okay with convert");
		}
	}
	else {
		self->private->src_elem=src->src_elem;
		self->private->dst_elem=dst->dst_elem;
		GST_INFO("  wire okay");
	}
	return(TRUE);
}

/**
 * bt_wire_unlink_machines:
 * @self: the wire that should be used for this connection
 *
 * Unlinks gst-element of this wire and removes the conversion elemnts from the
 * song.
 *
 * Returns: true for success
 */
static gboolean bt_wire_unlink_machines(const BtWire *self) {
  BtSong *song=self->private->song;
  GstBin *bin;
  GValue val={0,};
	g_value_init(&val,G_TYPE_OBJECT);

  g_object_get_property(G_OBJECT(song),"bin", &val);
  bin=GST_BIN(g_value_get_object(&val));

	GST_INFO("trying to unlink machines");
	gst_element_unlink(self->private->src->src_elem, self->private->dst->dst_elem);
	if(self->private->convert) {
		gst_element_unlink_many(self->private->src->src_elem, self->private->convert, self->private->dst->dst_elem, NULL);
	}
	if(self->private->scale) {
		gst_element_unlink_many(self->private->src->src_elem, self->private->scale, self->private->dst->dst_elem, NULL);
	}
	if(self->private->convert && self->private->scale) {
		gst_element_unlink_many(self->private->src->src_elem, self->private->convert, self->private->scale, self->private->dst->dst_elem, NULL);
	}
	if(self->private->convert) {
		gst_bin_remove(bin, self->private->convert);
	}
	if(self->private->scale) {
		gst_bin_remove(bin, self->private->scale);
	}
}

/**
 * bt_wire_connect:
 * @self: the wire that should be used for this connection
 *
 * Connect two machines with a wire. The source and dsetination machine must be
 * passed upon construction. 
 * Each machine can be involved in multiple connections. The method
 * transparently add spreader or adder elements in such cases. It further
 * inserts elemnts for data-type conversion if neccesary.
 *
 * Returns: true for success
 */
static gboolean bt_wire_connect(BtWire *self) {
  BtSong *song=self->private->song;
  BtSetup *setup=bt_song_get_setup(song);
  BtWire *other_wire;
  BtMachine *src, *dst;
  GstBin *bin;
  GValue val={0,};
	g_value_init(&val,G_TYPE_OBJECT);

  if(!self->private->src || !self->private->dst) return(FALSE);
  src=self->private->src;
  dst=self->private->dst;

  g_object_get_property(G_OBJECT(song),"bin", &val);
  bin=GST_BIN(g_value_get_object(&val));

	GST_INFO("trying to link machines : %p -> %p",src,dst);

	// if there is already a connection from src && src has not yet an spreader (in use)
	if((other_wire=bt_setup_get_wire_by_src_machine(setup,src)) && (src->src_elem!=src->spreader)) {
		GST_INFO("  other wire from src found");
		// unlink the elements from the other wire
		bt_wire_unlink_machines(other_wire);
		// create spreader (if needed)
		if(!src->spreader) {
			src->spreader=gst_element_factory_make("oneton",g_strdup_printf("oneton_%p",src));
			g_assert(src->spreader!=NULL);
			gst_bin_add(bin, src->spreader);
			if(!gst_element_link(src->machine, src->spreader)) {
				GST_ERROR("failed to link the machines internal spreader");return(FALSE);
			}
		}
		if(other_wire->private->src_elem==src->src_elem) {
			// we need to fix the src_elem of the other connect, as we have inserted the spreader
			other_wire->private->src_elem=src->spreader;
		}
		// activate spreader
		src->src_elem=src->spreader;
		GST_INFO("  spreader activated for \"%s\"",gst_element_get_name(src->machine));
		// correct the link for the other wire
		if(!bt_wire_link_machines(other_wire)) {
		//if(!gst_element_link(other_wire->src->src_elem, other_wire->dst_elem)) {
			GST_ERROR("failed to re-link the machines after inserting internal spreaker");return(FALSE);
		}
	}
	
	// if there is already a wire to dst and dst has not yet an adder (in use)
	if((other_wire=bt_setup_get_wire_by_src_machine(setup,dst)) && (dst->dst_elem!=dst->adder)) {
		GST_INFO("  other wire to dst found");
		// unlink the elements from the other wire
		bt_wire_unlink_machines(other_wire);
		//gst_element_unlink(other_wire->src_elem, other_wire->dst->dst_elem);
		// create adder (if needed)
		if(!dst->adder) {
			GstElement *convert;
			
			dst->adder=gst_element_factory_make("adder",g_strdup_printf("adder_%p",dst));
			g_assert(dst->adder!=NULL);
			gst_bin_add(bin, dst->adder);
			/** @todo this is a workaround for gst not making full caps-nego */
			convert=gst_element_factory_make("audioconvert",g_strdup_printf("audioconvert_%p",dst));
			g_assert(convert!=NULL);
			gst_bin_add(bin, convert);
			if(!gst_element_link_many(dst->adder, convert, dst->machine, NULL)) {
				GST_ERROR("failed to link the machines internal adder");return(FALSE);
			}
		}
		if(other_wire->private->dst_elem==dst->dst_elem) {
			// we need to fix the dst_elem of the other connect, as we have inserted the adder
			other_wire->private->dst_elem=dst->adder;
		}
		// activate adder
		dst->dst_elem=dst->adder;
		GST_INFO("  adder activated for \"%s\"",gst_element_get_name(dst->machine)); 
		// correct the link for the other wire
		if(!bt_wire_link_machines(other_wire)) {
		//if(!gst_element_link(other_wire->src_elem, other_wire->dst->dst_elem)) {
			GST_ERROR("failed to re-link the machines after inserting internal adder");return(FALSE);
		}
	}
	
	if(!bt_wire_link_machines(self)) {
		g_object_unref(G_OBJECT(self));
    return(FALSE);
	}
  bt_setup_add_wire(setup,self);
  GST_INFO("linking machines succeded");
	return(TRUE);
}

//-- wrapper

//-- class internals

/* returns a property for the given property_id for this object */
static void bt_wire_get_property(GObject      *object,
                               guint         property_id,
                               GValue       *value,
                               GParamSpec   *pspec)
{
  BtWire *self = BT_WIRE(object);
  return_if_disposed();
  switch (property_id) {
    case WIRE_SONG: {
      g_value_set_object(value, G_OBJECT(self->private->song));
    } break;
    case WIRE_SRC: {
      g_value_set_object(value, G_OBJECT(self->private->src));
    } break;
    case WIRE_DST: {
      g_value_set_object(value, G_OBJECT(self->private->dst));
    } break;
    default: {
      g_assert(FALSE);
      break;
    }
  }
}

/* sets the given properties for this object */
static void bt_wire_set_property(GObject      *object,
                              guint         property_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  BtWire *self = BT_WIRE(object);
  return_if_disposed();
  switch (property_id) {
    case WIRE_SONG: {
      self->private->song = g_object_ref(G_OBJECT(g_value_get_object(value)));
      //GST_INFO("set the song for wire: %p",self->private->song);
    } break;
		case WIRE_SRC: {
			self->private->src = g_object_ref(G_OBJECT(g_value_get_object(value)));
      //GST_INFO("set the source element for the wire: %p",self->private->src);
      bt_wire_connect(self);
		} break;
		case WIRE_DST: {
			self->private->dst = g_object_ref(G_OBJECT(g_value_get_object(value)));
      //GST_INFO("set the target element for the wire: %p",self->private->dst);
      bt_wire_connect(self);
		} break;
    default: {
      g_assert(FALSE);
      break;
    }
  }
}

static void bt_wire_dispose(GObject *object) {
  BtWire *self = BT_WIRE(object);
	return_if_disposed();
  self->private->dispose_has_run = TRUE;
}

static void bt_wire_finalize(GObject *object) {
  BtWire *self = BT_WIRE(object);
	g_object_unref(G_OBJECT(self->private->dst));
	g_object_unref(G_OBJECT(self->private->src));
	g_object_unref(G_OBJECT(self->private->song));
  g_free(self->private);
}

static void bt_wire_init(GTypeInstance *instance, gpointer g_class) {
  BtWire *self = BT_WIRE(instance);
  self->private = g_new0(BtWirePrivate,1);
  self->private->dispose_has_run = FALSE;
}

static void bt_wire_class_init(BtWireClass *klass) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  
  gobject_class->set_property = bt_wire_set_property;
  gobject_class->get_property = bt_wire_get_property;
  gobject_class->dispose      = bt_wire_dispose;
  gobject_class->finalize     = bt_wire_finalize;

  g_object_class_install_property(gobject_class,WIRE_SONG,
																	g_param_spec_object("song",
                                     "song construct prop",
                                     "the song object, the wire belongs to",
                                     BT_TYPE_SONG, /* object type */
                                     G_PARAM_CONSTRUCT_ONLY|G_PARAM_READWRITE));
  g_object_class_install_property(gobject_class,WIRE_SRC,
																	g_param_spec_object("src",
                                     "src ro prop",
                                     "src machine object, the wire links to",
                                     BT_TYPE_MACHINE, /* object type */
                                     G_PARAM_CONSTRUCT_ONLY|G_PARAM_READABLE));
  g_object_class_install_property(gobject_class,WIRE_DST,
																	g_param_spec_object("dst",
                                     "dst ro prop",
                                     "dst machine object, the wire links to",
                                     BT_TYPE_MACHINE, /* object type */
                                     G_PARAM_CONSTRUCT_ONLY|G_PARAM_READABLE));
}

GType bt_wire_get_type(void) {
  static GType type = 0;
  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (BtWireClass),
      NULL, // base_init
      NULL, // base_finalize
      (GClassInitFunc)bt_wire_class_init, // class_init
      NULL, // class_finalize
      NULL, // class_data
      sizeof (BtWire),
      0,   // n_preallocs
	    (GInstanceInitFunc)bt_wire_init, // instance_init
    };
		type = g_type_register_static(G_TYPE_OBJECT,"BtWire",&info,0);
  }
  return type;
}

