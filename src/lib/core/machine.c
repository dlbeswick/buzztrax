/* Buzztard
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
/**
 * SECTION:btmachine
 * @short_description: base class for signal processing machines
 *
 * Machines are pieces in a #BtSong that generate, process or play media.
 *
 * The machine class takes care of inserting additional low-level elemnts to do
 * signal conversion etc.. Further it provides general facillities like
 * input/output level monitoring. The resulting machine instance is a box
 * containing several processing elements.
 *
 * A machine can have several #GstElements:
 * <variablelist>
 *  <varlistentry>
 *    <term>adder:</term>
 *    <listitem><simpara>mixes all incoming signals</simpara></listitem>
 *  </varlistentry>
 *  <varlistentry>
 *    <term>input volume:</term>
 *    <listitem><simpara>gain for incoming signals</simpara></listitem>
 *  </varlistentry>
 *  <varlistentry>
 *    <term>input pre/post-gain level:</term>
 *    <listitem><simpara>level meter for incoming signal</simpara></listitem>
 *  </varlistentry>
 *  <varlistentry>
 *    <term>machine:</term>
 *    <listitem><simpara>the real machine</simpara></listitem>
 *  </varlistentry>
 *  <varlistentry>
 *    <term>output volume:</term>
 *    <listitem><simpara>gain for outgoing signal</simpara></listitem>
 *  </varlistentry>
 *  <varlistentry>
 *    <term>output pre/post-gain level:</term>
 *    <listitem><simpara>level meter for outgoing signal</simpara></listitem>
 *  </varlistentry>
 *  <varlistentry>
 *    <term>spreader:</term>
 *    <listitem><simpara>distibutes signal to outgoing connections</simpara></listitem>
 *  </varlistentry>
 * </variablelist>
 * The adder and spreader elements are activated depending on element type.
 * The volume controls and level meters are activated as requested via the API.
 * It is recommended to only activate them, when needed. The instances are cached
 * after deactivation (so that they can be easily reactivated) and destroyed with
 * the #BtMachine object.
 *
 * Furthermore the machine handles a list of #BtCmdPattern instances. These
 * contain event patterns that form a #BtSequence.
 */

/* TODO(ensonic): machine part creation api
 * - need single api to create machine parts, instead of individual functions
 *   - export the enum ?
 *   - one function that takes flags, or vararg of enum values
 */
/* TODO(ensonic): cache the GstControlSource objects?
 * - we look them up a lot, its a linear search in a list, locking and ref/unref
 * - one for each param and again each voice
 */
/* TODO(ensonic): undo/redo needs to handle controlbindings */

#define BT_CORE
#define BT_MACHINE_C

#include "core_private.h"
#include "ic.h"

// do sanity check for pattern lifecycle
//#define CHECK_PATTERN_OWNERSHIP 1

//-- signal ids

enum
{
  PATTERN_ADDED_EVENT,
  PATTERN_REMOVED_EVENT,
  LAST_SIGNAL
};

//-- property ids

enum
{
  MACHINE_CONSTRUCTION_ERROR = 1,
  MACHINE_PROPERTIES,
  MACHINE_SONG,
  MACHINE_ID,
  MACHINE_PLUGIN_NAME,
  MACHINE_VOICES,
  MACHINE_PREFS_PARAMS,
  MACHINE_GLOBAL_PARAMS,
  MACHINE_VOICE_PARAMS,
  MACHINE_MACHINE,
  MACHINE_ADDER_CONVERT,
  MACHINE_INPUT_PRE_LEVEL,
  MACHINE_INPUT_GAIN,
  MACHINE_INPUT_POST_LEVEL,
  MACHINE_OUTPUT_PRE_LEVEL,
  MACHINE_OUTPUT_GAIN,
  MACHINE_OUTPUT_POST_LEVEL,
  MACHINE_PATTERNS,
  MACHINE_STATE
};

// adder, capsfiter, level, volume are gap-aware
typedef enum
{
  /* utillity element to allow multiple inputs */
  PART_ADDER = 0,
  /* helper to enforce common format for adder inputs */
  PART_CAPS_FILTER,
  /* helper to make adder link to next element */
  PART_ADDER_CONVERT,
  /* the elements to control and analyse the current input signal */
  PART_INPUT_PRE_LEVEL,
  PART_INPUT_GAIN,
  PART_INPUT_POST_LEVEL,
  /* the gstreamer element that produces/processes the signal */
  PART_MACHINE,
  /* the elements to control and analyse the current output signal */
  PART_OUTPUT_PRE_LEVEL,
  PART_OUTPUT_GAIN,
  PART_OUTPUT_POST_LEVEL,
  /* utillity element to allow multiple outputs */
  PART_SPREADER,
  /* how many elements are used */
  PART_COUNT
} BtMachinePart;

struct _BtMachinePrivate
{
  /* used to validate if dispose has run */
  gboolean dispose_has_run;
  /* used to signal failed instance creation */
  GError **constrution_error;

  /* (ui) properties accociated with this machine */
  GHashTable *properties;

  /* the song the machine belongs to */
  BtSong *song;

  /* status in songs pipeline */
  gboolean is_added, is_connected;

  /* the id, we can use to lookup the machine */
  gchar *id;
  /* the name of the gst-plugin the machine is using */
  gchar *plugin_name;

  /* the number of voices the machine provides */
  gulong voices;
  /* the number of static params the machine provides per instance */
  gulong prefs_params;
  /* the number of dynamic params the machine provides per instance */
  gulong global_params;
  /* the number of dynamic params the machine provides per instance and voice */
  gulong voice_params;

  /* the current state of the machine */
  BtMachineState state;

  /* parameter groups */
  BtParameterGroup *prefs_param_group;
  BtParameterGroup *global_param_group, **voice_param_groups;

  /* event patterns */
  GList *patterns;              // each entry points to BtCmdPattern
  guint private_patterns;

  /* the gstreamer elements that are used */
  GstElement *machines[PART_COUNT];
  GstPad *src_pads[PART_COUNT], *sink_pads[PART_COUNT];

  /* caps filter format */
  gint format;                  /* 0=int/1=float */
  gint channels;
  gint width;
  gint depth;

  /* realtime control (bt-ic) */
  GHashTable *control_data;     // each entry is <GParamSpec,BtControlData>

  /* src/sink ghost-pad counters for the machine */
  gint src_pad_counter, sink_pad_counter;
};

typedef enum
{
  BT_MONO_CONTROL_DATA = 0,
  BT_POLY_CONTROL_DATA
} BtControlDataType;

typedef struct
{
  BtControlDataType type;
  BtIcControl *control;
  GParamSpec *pspec;
  BtParameterGroup *pg;
  gulong handler_id;
  gint pi;
} BtControlData;

typedef struct
{
  BtControlData common;
  GstObject *object;
} BtMonoControlData;

typedef struct
{
  BtControlData common;
  BtMachinePrivate *machine_priv;
  gint voice_ct;
} BtPolyControlData;

static GQuark error_domain = 0;
GQuark bt_machine_machine = 0;
GQuark bt_machine_property_name = 0;
static GQuark tee_last_seq_num = 0;

static guint signals[LAST_SIGNAL] = { 0, };

static gchar *src_pn[] = {
  "src",                        /* adder */
  "src",                        /* caps filter */
  "src",                        /* audioconvert */
  "src",                        /* input pre level */
  "src",                        /* input gain */
  "src",                        /* input post level */
  "src",                        /* machine */
  "src",                        /* output pre level */
  "src",                        /* output gain */
  "src",                        /* output post level */
  NULL                          /* tee */
};

static gchar *sink_pn[] = {
  NULL,                         /* adder */
  "sink",                       /* caps filter */
  "sink",                       /* audioconvert */
  "sink",                       /* input pre level */
  "sink",                       /* input gain */
  "sink",                       /* input post level */
  "sink",                       /* machine */
  "sink",                       /* output pre level */
  "sink",                       /* output gain */
  "sink",                       /* output post level */
  "sink"                        /* tee */
};

static GstElementFactory *factories[PART_COUNT];
// for tee and adder
static GstPadTemplate *src_pt;
static GstPadTemplate *sink_pt;


//-- the class

static void bt_machine_persistence_interface_init (gpointer const g_iface,
    gpointer const iface_data);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (BtMachine, bt_machine, GST_TYPE_BIN,
    G_IMPLEMENT_INTERFACE (BT_TYPE_PERSISTENCE,
        bt_machine_persistence_interface_init));

//-- macros

#define GLOBAL_PARAM_NAME(ix) self->priv->global_props[ix]->name
#define GLOBAL_PARAM_TYPE(ix) self->priv->global_props[ix]->value_type
#define VOICE_PARAM_NAME(ix) self->priv->voice_props[ix]->name
#define VOICE_PARAM_TYPE(ix) self->priv->voice_props[ix]->value_type

//-- enums

GType
bt_machine_state_get_type (void)
{
  static GType type = 0;
  if (G_UNLIKELY (type == 0)) {
    static const GEnumValue values[] = {
      {BT_MACHINE_STATE_NORMAL, "BT_MACHINE_STATE_NORMAL", "normal"},
      {BT_MACHINE_STATE_MUTE, "BT_MACHINE_STATE_MUTE", "mute"},
      {BT_MACHINE_STATE_SOLO, "BT_MACHINE_STATE_SOLO", "solo"},
      {BT_MACHINE_STATE_BYPASS, "BT_MACHINE_STATE_BYPASS", "bypass"},
      {0, NULL, NULL},
    };
    type = g_enum_register_static ("BtMachineState", values);
  }
  return type;
}

//-- signal handler

/* IDEA(ensonic): dynamically handle stpb (subticks per beat)
 * - we'd like to set stpb=1 for non interactive playback and recording
 * - we'd like to set stpb>1 for:
 *   - idle-loop play
 *   - open machine windows (with controllers assigned)
 *
 * (GST_SECOND*60)
 * ------------------- = 30
 * (bpm*tpb*stpb*1000)
 *
 * (GST_SECOND*60)
 * ------------------- = stpb
 * (bpm*tpb*30*1000000)
 *
 * - maybe we get set target-latency=-1 for no subticks (=1) e.g. when recording
 * - make this a property on the song that merges latency setting + playback mode?
 */
static glong
update_subticks (gulong bpm, gulong tpb, guint latency)
{
  glong st = 0;

  st = (glong) ((GST_SECOND * 60) / (bpm * tpb * latency *
          G_GINT64_CONSTANT (1000000)));
  st = MAX (1, st);
  GST_DEBUG ("chosing subticks=%ld from bpm=%lu,tpb=%lu,latency=%u", st, bpm,
      tpb, latency);
  return (st);
}

static void
bt_machine_on_bpm_changed (BtSongInfo * const song_info,
    const GParamSpec * const arg, gconstpointer const user_data)
{
  const BtMachine *const self = BT_MACHINE (user_data);
  BtSettings *settings = bt_settings_make ();
  gulong bpm, tpb;
  guint latency;

  GST_DEBUG_OBJECT (self, "bpm changed");
  g_object_get (song_info, "bpm", &bpm, "tpb", &tpb, NULL);
  g_object_get (settings, "latency", &latency, NULL);
  g_object_unref (settings);
  gstbt_tempo_change_tempo (GSTBT_TEMPO (self->priv->machines[PART_MACHINE]),
      (glong) bpm, -1, update_subticks (bpm, tpb, latency));
}

static void
bt_machine_on_tpb_changed (BtSongInfo * const song_info,
    const GParamSpec * const arg, gconstpointer const user_data)
{
  const BtMachine *const self = BT_MACHINE (user_data);
  BtSettings *settings = bt_settings_make ();
  gulong bpm, tpb;
  guint latency;

  GST_DEBUG_OBJECT (self, "tpb changed");
  g_object_get (song_info, "bpm", &bpm, "tpb", &tpb, NULL);
  g_object_get (settings, "latency", &latency, NULL);
  g_object_unref (settings);
  gstbt_tempo_change_tempo (GSTBT_TEMPO (self->priv->machines[PART_MACHINE]),
      -1, (glong) tpb, update_subticks (bpm, tpb, latency));
}

static void
bt_machine_on_latency_changed (BtSettings * const settings,
    const GParamSpec * const arg, gconstpointer const user_data)
{
  const BtMachine *const self = BT_MACHINE (user_data);
  BtSongInfo *song_info;
  gulong bpm, tpb;
  guint latency;

  GST_DEBUG_OBJECT (self, "latency changed");
  g_object_get (settings, "latency", &latency, NULL);
  g_object_get ((gpointer) (self->priv->song), "song-info", &song_info, NULL);
  g_object_get (song_info, "bpm", &bpm, "tpb", &tpb, NULL);
  g_object_unref (song_info);
  gstbt_tempo_change_tempo (GSTBT_TEMPO (self->priv->machines[PART_MACHINE]),
      -1, -1, update_subticks (bpm, tpb, latency));
}

static void
bt_machine_on_duration_changed (BtSequence * const sequence,
    const GParamSpec * const arg, gconstpointer const user_data)
{
  const BtMachine *const self = BT_MACHINE (user_data);
  GstClockTime duration, tick_duration;
  glong length;

  GST_DEBUG_OBJECT (self, "length changed");

  g_object_get (sequence, "length", &length, NULL);
  bt_child_proxy_get (self->priv->song, "song-info::tick-duration",
      &tick_duration, NULL);
  duration = tick_duration * length;
  GST_BASE_SRC (self->priv->machines[PART_MACHINE])->segment.duration =
      duration;
  GST_INFO ("  duration: %" GST_TIME_FORMAT, GST_TIME_ARGS (duration));
}

//-- helper methods

/*
 * mute the machine output
 */
static gboolean
bt_machine_set_mute (const BtMachine * const self)
{
  const BtMachinePart part =
      BT_IS_SINK_MACHINE (self) ? PART_INPUT_GAIN : PART_OUTPUT_GAIN;

  //if(self->priv->state==BT_MACHINE_STATE_MUTE) return(TRUE);

  if (self->priv->machines[part]) {
    g_object_set (self->priv->machines[part], "mute", TRUE, NULL);
    return (TRUE);
  }
  GST_WARNING_OBJECT (self, "can't mute element '%s'", self->priv->id);
  return (FALSE);
}

/*
 * unmute the machine output
 */
static gboolean
bt_machine_unset_mute (const BtMachine * const self)
{
  const BtMachinePart part =
      BT_IS_SINK_MACHINE (self) ? PART_INPUT_GAIN : PART_OUTPUT_GAIN;

  //if(self->priv->state!=BT_MACHINE_STATE_MUTE) return(TRUE);

  if (self->priv->machines[part]) {
    g_object_set (self->priv->machines[part], "mute", FALSE, NULL);
    return (TRUE);
  }
  GST_WARNING_OBJECT (self, "can't unmute element '%s'", self->priv->id);
  return (FALSE);
}

/*
 * bt_machine_change_state:
 *
 * Reset old state and go to new state. Do sanity checking of allowed states for
 * given machine.
 *
 * Returns: %TRUE for success
 */
static gboolean
bt_machine_change_state (const BtMachine * const self,
    const BtMachineState new_state)
{
  gboolean res = TRUE;
  BtSetup *setup;

  // reject a few nonsense changes
  if ((new_state == BT_MACHINE_STATE_BYPASS)
      && (!BT_IS_PROCESSOR_MACHINE (self)))
    return (FALSE);
  if ((new_state == BT_MACHINE_STATE_SOLO) && (BT_IS_SINK_MACHINE (self)))
    return (FALSE);
  if (self->priv->state == new_state)
    return (TRUE);

  g_object_get (self->priv->song, "setup", &setup, NULL);

  GST_INFO ("state change for element '%s'", self->priv->id);

  // return to normal state
  switch (self->priv->state) {
    case BT_MACHINE_STATE_MUTE:
      // source, processor, sink
      if (!bt_machine_unset_mute (self))
        res = FALSE;
      break;
    case BT_MACHINE_STATE_SOLO:{
      // source
      GList *node, *machines =
          bt_setup_get_machines_by_type (setup, BT_TYPE_SOURCE_MACHINE);
      BtMachine *machine;
      // set all but this machine to playing again
      for (node = machines; node; node = g_list_next (node)) {
        machine = BT_MACHINE (node->data);
        if (machine != self) {
          if (!bt_machine_unset_mute (machine))
            res = FALSE;
        }
        g_object_unref (machine);
      }
      GST_INFO ("unmuted %d elements with result = %d",
          g_list_length (machines), res);
      g_list_free (machines);
      break;
    }
    case BT_MACHINE_STATE_BYPASS:{
      // processor
      const GstElement *const element = self->priv->machines[PART_MACHINE];
      if (GST_IS_BASE_TRANSFORM (element)) {
        gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (element),
            FALSE);
      } else {
        // TODO(ensonic): disconnect its source and sink + set this machine to playing
        GST_INFO ("element does not support passthrough");
      }
      break;
    }
    case BT_MACHINE_STATE_NORMAL:
      //g_return_val_if_reached(FALSE);
      break;
    default:
      GST_WARNING_OBJECT (self, "invalid old machine state: %d",
          self->priv->state);
      break;
  }
  // set to new state
  switch (new_state) {
    case BT_MACHINE_STATE_MUTE:{
      // source, processor, sink
      if (!bt_machine_set_mute (self))
        res = FALSE;
    }
      break;
    case BT_MACHINE_STATE_SOLO:{
      // source
      GList *node, *machines =
          bt_setup_get_machines_by_type (setup, BT_TYPE_SOURCE_MACHINE);
      BtMachine *machine;
      // set all but this machine to paused
      for (node = machines; node; node = g_list_next (node)) {
        machine = BT_MACHINE (node->data);
        if (machine != self) {
          // if a different machine is solo, set it to normal and unmute the current source
          if (machine->priv->state == BT_MACHINE_STATE_SOLO) {
            machine->priv->state = BT_MACHINE_STATE_NORMAL;
            g_object_notify (G_OBJECT (machine), "state");
            if (!bt_machine_unset_mute (self))
              res = FALSE;
          }
          if (!bt_machine_set_mute (machine))
            res = FALSE;
        }
        g_object_unref (machine);
      }
      GST_INFO ("muted %d elements with result = %d", g_list_length (machines),
          res);
      g_list_free (machines);
    }
      break;
    case BT_MACHINE_STATE_BYPASS:{
      // processor
      const GstElement *element = self->priv->machines[PART_MACHINE];
      if (GST_IS_BASE_TRANSFORM (element)) {
        gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (element), TRUE);
      } else {
        // TODO(ensonic): set this machine to paused + connect its source and sink
        GST_INFO ("element does not support passthrough");
      }
    }
      break;
    case BT_MACHINE_STATE_NORMAL:
      //g_return_val_if_reached(FALSE);
      break;
    default:
      GST_WARNING_OBJECT (self, "invalid new machine state: %d", new_state);
      break;
  }
  self->priv->state = new_state;

  g_object_unref (setup);
  return (res);
}

/*
 * bt_machine_link_elements:
 * @self: the machine in which we link
 * @src,@sink: the pads
 *
 * Link two pads.
 *
 * Returns: %TRUE for sucess
 */
static gboolean
bt_machine_link_elements (const BtMachine * const self, GstPad * src,
    GstPad * sink)
{
  GstPadLinkReturn plr;

  if ((plr = gst_pad_link (src, sink)) != GST_PAD_LINK_OK) {
    GST_WARNING_OBJECT (self, "can't link %s:%s with %s:%s: %d",
        GST_DEBUG_PAD_NAME (src), GST_DEBUG_PAD_NAME (sink), plr);
    return (FALSE);
  }
  return (TRUE);
}

/*
 * bt_machine_insert_element:
 * @self: the machine for which the element should be inserted
 * @peer: the peer pad element
 * @pos: the element at this position should be inserted (activated)
 *
 * Searches surrounding elements of the new element for active peers
 * and connects them. The new elemnt needs to be created before calling this method.
 *
 * Returns: %TRUE for sucess
 */
static gboolean
bt_machine_insert_element (BtMachine * const self, GstPad * const peer,
    const BtMachinePart pos)
{
  gboolean res = FALSE;
  gint i, pre, post;
  BtWire *wire;
  GstElement **const machines = self->priv->machines;
  GstPad **const src_pads = self->priv->src_pads;
  GstPad **const sink_pads = self->priv->sink_pads;

  // look for elements before and after pos
  pre = post = -1;
  for (i = pos - 1; i > -1; i--) {
    if (machines[i]) {
      pre = i;
      break;
    }
  }
  for (i = pos + 1; i < PART_COUNT; i++) {
    if (machines[i]) {
      post = i;
      break;
    }
  }
  GST_INFO ("positions: %d ... %d(%s) ... %d", pre, pos,
      GST_OBJECT_NAME (machines[pos]), post);
  // get pads
  if ((pre != -1) && (post != -1)) {
    // unlink old connection
    gst_pad_unlink (src_pads[pre], sink_pads[post]);
    // link new connection
    res = bt_machine_link_elements (self, src_pads[pre], sink_pads[pos]);
    res &= bt_machine_link_elements (self, src_pads[pos], sink_pads[post]);
    if (!res) {
      gst_pad_unlink (src_pads[pre], sink_pads[pos]);
      gst_pad_unlink (src_pads[pos], sink_pads[post]);
      GST_WARNING_OBJECT (self,
          "failed to link part '%s' inbetween '%s' and '%s'",
          GST_OBJECT_NAME (machines[pos]), GST_OBJECT_NAME (machines[pre]),
          GST_OBJECT_NAME (machines[post]));
      // relink previous connection
      bt_machine_link_elements (self, src_pads[pre], sink_pads[post]);
    }
  } else if (pre == -1) {
    // unlink old connection
    gst_pad_unlink (peer, sink_pads[post]);
    // link new connection
    res = bt_machine_link_elements (self, peer, sink_pads[pos]);
    res &= bt_machine_link_elements (self, src_pads[pos], sink_pads[post]);
    if (!res) {
      gst_pad_unlink (peer, sink_pads[pos]);
      gst_pad_unlink (src_pads[pos], sink_pads[post]);
      GST_WARNING_OBJECT (self, "failed to link part '%s' before '%s'",
          GST_OBJECT_NAME (machines[pos]), GST_OBJECT_NAME (machines[post]));
      // try to re-wire
      if ((res =
              bt_machine_link_elements (self, src_pads[pos],
                  sink_pads[post]))) {
        if ((wire =
                (self->dst_wires ? (BtWire *) (self->
                        dst_wires->data) : NULL))) {
          if (!(res = bt_wire_reconnect (wire))) {
            GST_WARNING_OBJECT (self,
                "failed to reconnect wire after linking '%s' before '%s'",
                GST_OBJECT_NAME (machines[pos]),
                GST_OBJECT_NAME (machines[post]));
          }
        }
      } else {
        GST_WARNING_OBJECT (self, "failed to link part '%s' before '%s' again",
            GST_OBJECT_NAME (machines[pos]), GST_OBJECT_NAME (machines[post]));
      }
    }
  } else if (post == -1) {
    // unlink old connection
    gst_pad_unlink (src_pads[pre], peer);
    // link new connection
    res = bt_machine_link_elements (self, src_pads[pre], sink_pads[pos]);
    res &= bt_machine_link_elements (self, src_pads[pos], peer);
    if (!res) {
      gst_pad_unlink (src_pads[pre], sink_pads[pos]);
      gst_pad_unlink (src_pads[pos], peer);
      GST_WARNING_OBJECT (self, "failed to link part '%s' after '%s'",
          GST_OBJECT_NAME (machines[pos]), GST_OBJECT_NAME (machines[pre]));
      // try to re-wire
      if ((res =
              bt_machine_link_elements (self, src_pads[pre], sink_pads[pos]))) {
        if ((wire =
                (self->src_wires ? (BtWire *) (self->
                        src_wires->data) : NULL))) {
          if (!(res = bt_wire_reconnect (wire))) {
            GST_WARNING_OBJECT (self,
                "failed to reconnect wire after linking '%s' after '%s'",
                GST_OBJECT_NAME (machines[pos]),
                GST_OBJECT_NAME (machines[pre]));
          }
        }
      } else {
        GST_WARNING_OBJECT (self, "failed to link part '%s' after '%s' again",
            GST_OBJECT_NAME (machines[pos]), GST_OBJECT_NAME (machines[pre]));
      }
    }
  } else {
    GST_ERROR_OBJECT (self, "failed to link part '%s' in broken machine",
        GST_OBJECT_NAME (machines[pos]));
  }
  return (res);
}

static void
bt_machine_clamp_voices (const BtMachine * const self)
{
  GstElement *machine = self->priv->machines[PART_MACHINE];
  GParamSpecULong *ps;

  // clamp to property bounds
  if ((ps = (GParamSpecULong *)
          g_object_class_find_property (G_OBJECT_GET_CLASS (machine),
              "children"))) {
    self->priv->voices = CLAMP (self->priv->voices, ps->minimum, ps->maximum);
  }
  g_object_set (machine, "children", self->priv->voices, NULL);
  g_object_get (machine, "children", &self->priv->voices, NULL);
}

/*
 * bt_machine_resize_voices:
 * @self: the machine which has changed its number of voices
 *
 * Adjust the private data structure after a change in the number of voices.
 */
static void
bt_machine_resize_voices (const BtMachine * const self, const gulong old_voices)
{
  gulong new_voices;
  const gulong voice_params = self->priv->voice_params;
  GstElement *machine = self->priv->machines[PART_MACHINE];

  // clamp to property and run-time bounds
  bt_machine_clamp_voices (self);
  if (old_voices == self->priv->voices) {
    GST_INFO_OBJECT (self, "not changing changing machine voices");
    return;
  }
  new_voices = self->priv->voices;

  GST_INFO_OBJECT (self, "changing machine %" G_OBJECT_REF_COUNT_FMT
      ", voices from %ld to %ld",
      G_OBJECT_LOG_REF_COUNT (machine), old_voices, new_voices);

  if (old_voices > new_voices) {
    gulong v;

    // release params for old voices
    for (v = new_voices; v < old_voices; v++) {
      g_object_try_unref (self->priv->voice_param_groups[v]);
    }
  }

  self->priv->voice_param_groups =
      (BtParameterGroup **) g_renew (gpointer, self->priv->voice_param_groups,
      new_voices);
  if (old_voices < new_voices) {
    GstObject *voice_child;
    gulong v;

    for (v = old_voices; v < new_voices; v++) {
      // get child for voice
      if ((voice_child =
              gst_child_proxy_get_child_by_index (GST_CHILD_PROXY (machine),
                  v))) {
        GParamSpec **properties;
        guint number_of_properties;

        if ((properties =
                g_object_class_list_properties (G_OBJECT_CLASS
                    (GST_OBJECT_GET_CLASS (voice_child)),
                    &number_of_properties))) {
          GParamSpec **params = (GParamSpec **) g_new (gpointer, voice_params);
          GObject **parents = (GObject **) g_new (gpointer, voice_params);
          guint i, j;

          for (i = j = 0; i < number_of_properties; i++) {
            if (properties[i]->flags & GST_PARAM_CONTROLLABLE) {
              params[j] = properties[i];
              parents[j] = (GObject *) voice_child;
              j++;
            }
          }
          self->priv->voice_param_groups[v] =
              bt_parameter_group_new (voice_params, parents, params,
              self->priv->song, self);
        }
        g_free (properties);
      } else {
        self->priv->voice_param_groups[v] = NULL;
        GST_WARNING_OBJECT (self, "  can't get voice child %lu!", v);
      }
    }
  }
}

/*
 * bt_machine_make_internal_element:
 * @self: the machine
 * @part: which internal element to create
 * @factory_name: the element-factories name
 * @element_name: the name of the new #GstElement instance
 *
 * This helper is used by the family of bt_machine_enable_xxx() functions.
 */
static gboolean
bt_machine_make_internal_element (const BtMachine * const self,
    const BtMachinePart part, const gchar * const factory_name,
    const gchar * const element_name)
{
  gboolean res = FALSE;
  GstElement *m;
  GstElementFactory *f;
  const gchar *const parent_name = GST_OBJECT_NAME (self);
  gchar *const name =
      g_alloca (strlen (parent_name) + 2 + strlen (element_name));
  gpointer item;

  g_return_val_if_fail ((self->priv->machines[part] == NULL), TRUE);

  // for the core machine we don't cache the factory
  if (part == PART_MACHINE) {
    if (!(f = gst_element_factory_find (factory_name))) {
      GST_WARNING_OBJECT (self, "failed to lookup factory %s", factory_name);
      goto Error;
    }
  } else {
    if (!factories[part]) {
      // we never unref them, instead we keep them until the end
      if (!(factories[part] = gst_element_factory_find (factory_name))) {
        GST_WARNING_OBJECT (self, "failed to lookup factory %s", factory_name);
        goto Error;
      }
    }
    f = factories[part];
  }

  // create internal element
  //strcat(name,parent_name);strcat(name,":");strcat(name,element_name);
  g_sprintf (name, "%s:%s", parent_name, element_name);
  if (!(self->priv->machines[part] = gst_element_factory_create (f, name))) {
    GST_WARNING_OBJECT (self, "failed to create %s from factory %s",
        element_name, factory_name);
    goto Error;
  }
  m = self->priv->machines[part];
  // disable deep notify
  {
    GObjectClass *gobject_class = G_OBJECT_GET_CLASS (m);
    GObjectClass *parent_class = g_type_class_peek_static (G_TYPE_OBJECT);
    gobject_class->dispatch_properties_changed =
        parent_class->dispatch_properties_changed;
  }

  // get the pads
  if (src_pn[part]) {
    if (!(self->priv->src_pads[part] =
            gst_element_get_static_pad (m, src_pn[part]))) {
      GstIterator *it = gst_element_iterate_src_pads (m);
      gboolean done = FALSE;

      while (!done) {
        switch (gst_iterator_next (it, &item)) {
          case GST_ITERATOR_RESYNC:
            gst_iterator_resync (it);
            break;
          case GST_ITERATOR_OK:
            self->priv->src_pads[part] = GST_PAD (item);
            /* fall through */
          case GST_ITERATOR_ERROR:
          case GST_ITERATOR_DONE:
            done = TRUE;
            break;
        }
      }
      gst_iterator_free (it);
      if (!self->priv->src_pads[part]) {
        GST_DEBUG_OBJECT (m, "element has no src pads");
      }
    }
  }
  if (sink_pn[part]) {
    if (!(self->priv->sink_pads[part] =
            gst_element_get_static_pad (m, sink_pn[part]))) {
      GstIterator *it = gst_element_iterate_sink_pads (m);
      gboolean done = FALSE;

      while (!done) {
        switch (gst_iterator_next (it, &item)) {
          case GST_ITERATOR_RESYNC:
            gst_iterator_resync (it);
            break;
          case GST_ITERATOR_OK:
            self->priv->sink_pads[part] = GST_PAD (item);
            /* fall through */
          case GST_ITERATOR_ERROR:
          case GST_ITERATOR_DONE:
            done = TRUE;
            break;
        }
      }
      gst_iterator_free (it);
      if (!self->priv->sink_pads[part]) {
        GST_DEBUG_OBJECT (m, "element has no sink pads");
      }
    }
  }

  gst_bin_add (GST_BIN (self), m);
  gst_element_sync_state_with_parent (m);
  if (part == PART_MACHINE) {
    gst_object_unref (f);
  }
  res = TRUE;
Error:
  return (res);
}

/*
 * bt_machine_add_input_element:
 * @self: the machine
 * @part: which internal element to link
 *
 * This helper is used by the family of bt_machine_enable_input_xxx() functions.
 */
static gboolean
bt_machine_add_input_element (BtMachine * const self, const BtMachinePart part)
{
  gboolean res = FALSE;
  GstElement **const machines = self->priv->machines;
  GstPad **const src_pads = self->priv->src_pads;
  GstPad **const sink_pads = self->priv->sink_pads;
  GstPad *peer;
  guint i, tix = PART_MACHINE;

  // get next element on the source side
  for (i = part + 1; i <= PART_MACHINE; i++) {
    if (machines[i]) {
      tix = i;
      GST_DEBUG ("src side target at %d: %s:%s", i,
          GST_DEBUG_PAD_NAME (sink_pads[tix]));
      break;
    }
  }

  // is the machine connected towards the input side (its sink)?
  if (!(peer = gst_pad_get_peer (sink_pads[tix]))) {
    GST_DEBUG ("target '%s:%s' is not yet connected on the input side",
        GST_DEBUG_PAD_NAME (sink_pads[tix]));
    if (!bt_machine_link_elements (self, src_pads[part], sink_pads[tix])) {
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (self),
          GST_DEBUG_GRAPH_SHOW_ALL, PACKAGE_NAME "-machine");
      GST_ERROR_OBJECT (self, "failed to link the element '%s' for '%s'",
          GST_OBJECT_NAME (machines[part]),
          GST_OBJECT_NAME (machines[PART_MACHINE]));
      goto Error;
    }
    GST_INFO ("sucessfully prepended element '%s' for '%s'",
        GST_OBJECT_NAME (machines[part]),
        GST_OBJECT_NAME (machines[PART_MACHINE]));
  } else {
    GST_DEBUG ("target '%s:%s' has peer pad '%s:%s', need to insert",
        GST_DEBUG_PAD_NAME (sink_pads[tix]), GST_DEBUG_PAD_NAME (peer));
    if (!bt_machine_insert_element (self, peer, part)) {
      gst_object_unref (peer);
      GST_ERROR_OBJECT (self, "failed to link the element '%s' for '%s'",
          GST_OBJECT_NAME (machines[part]),
          GST_OBJECT_NAME (machines[PART_MACHINE]));
      goto Error;
    }
    gst_object_unref (peer);
    GST_INFO ("sucessfully inserted element'%s' for '%s'",
        GST_OBJECT_NAME (machines[part]),
        GST_OBJECT_NAME (machines[PART_MACHINE]));
  }
  res = TRUE;
Error:
  return (res);
}

/*
 * bt_machine_add_output_element:
 * @self: the machine
 * @part: which internal element to link
 *
 * This helper is used by the family of bt_machine_enable_output_xxx() functions.
 */
static gboolean
bt_machine_add_output_element (BtMachine * const self, const BtMachinePart part)
{
  gboolean res = FALSE;
  GstElement **const machines = self->priv->machines;
  GstPad **const src_pads = self->priv->src_pads;
  GstPad **const sink_pads = self->priv->sink_pads;
  GstPad *peer;
  guint i, tix = PART_MACHINE;

  // get next element on the sink side
  for (i = part - 1; i >= PART_MACHINE; i--) {
    if (machines[i]) {
      tix = i;
      GST_DEBUG_OBJECT (self, "sink side target at %d: %s:%s", i,
          GST_DEBUG_PAD_NAME (src_pads[tix]));
      break;
    }
  }

  // is the machine unconnected towards the output side (its source)?
  if (!(peer = gst_pad_get_peer (src_pads[tix]))) {
    GST_DEBUG ("target '%s:%s' is not yet connected on the output side",
        GST_DEBUG_PAD_NAME (src_pads[tix]));
    if (!bt_machine_link_elements (self, src_pads[tix], sink_pads[part])) {
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (self),
          GST_DEBUG_GRAPH_SHOW_ALL, PACKAGE_NAME "-machine");
      GST_ERROR_OBJECT (self, "failed to link the element '%s' for '%s'",
          GST_OBJECT_NAME (machines[part]),
          GST_OBJECT_NAME (machines[PART_MACHINE]));
      goto Error;
    }
    GST_INFO ("sucessfully appended element '%s' for '%s'",
        GST_OBJECT_NAME (machines[part]),
        GST_OBJECT_NAME (machines[PART_MACHINE]));
  } else {
    GST_DEBUG ("target '%s:%s' has peer pad '%s:%s', need to insert",
        GST_DEBUG_PAD_NAME (src_pads[tix]), GST_DEBUG_PAD_NAME (peer));
    if (!bt_machine_insert_element (self, peer, part)) {
      gst_object_unref (peer);
      GST_ERROR_OBJECT (self, "failed to link the element '%s' for '%s'",
          GST_OBJECT_NAME (machines[part]),
          GST_OBJECT_NAME (machines[PART_MACHINE]));
      goto Error;
    }
    gst_object_unref (peer);
    GST_INFO ("sucessfully inserted element'%s' for '%s'",
        GST_OBJECT_NAME (machines[part]),
        GST_OBJECT_NAME (machines[PART_MACHINE]));
  }
  res = TRUE;
Error:
  return (res);
}

/* bt_machine_enable_part:
 * @part: which internal element to create
 * @factory_name: the element-factories name
 * @element_name: the name of the new #GstElement instance
 *
 * can replace _enable_{in,out}put_{level,gain}
 * this is not good enough for adder, ev. okay for spreader
 */
static gboolean
bt_machine_enable_part (BtMachine * const self, const BtMachinePart part,
    const gchar * const factory_name, const gchar * const element_name)
{
  gboolean res = FALSE;

  if (self->priv->machines[part])
    return (TRUE);

  if (!bt_machine_make_internal_element (self, part, factory_name,
          element_name))
    goto Error;
  // configure part
  switch (part) {
    case PART_INPUT_PRE_LEVEL:
    case PART_INPUT_POST_LEVEL:
    case PART_OUTPUT_PRE_LEVEL:
    case PART_OUTPUT_POST_LEVEL:
      g_object_set (self->priv->machines[part],
          "interval", (GstClockTime) (0.1 * GST_SECOND), "message", TRUE,
          "peak-ttl", (GstClockTime) (0.2 * GST_SECOND), "peak-falloff", 50.0,
          NULL);
      break;
    default:
      break;
  }
  if (part < PART_MACHINE) {
    if (!bt_machine_add_input_element (self, part))
      goto Error;
  } else {
    if (!bt_machine_add_output_element (self, part))
      goto Error;
  }
  res = TRUE;
Error:
  return (res);
}


//-- init helpers

static gboolean
bt_machine_init_core_machine (BtMachine * const self)
{
  gboolean res = FALSE;

  if (!bt_machine_make_internal_element (self, PART_MACHINE,
          self->priv->plugin_name, self->priv->id))
    goto Error;
  GST_INFO ("  instantiated machine \"%s\", %" G_OBJECT_REF_COUNT_FMT,
      self->priv->plugin_name,
      G_OBJECT_LOG_REF_COUNT (self->priv->machines[PART_MACHINE]));

  res = TRUE;
Error:
  return (res);
}

static void
bt_machine_init_interfaces (const BtMachine * const self)
{
  GstElement *machine = self->priv->machines[PART_MACHINE];
  GObjectClass *klass = G_OBJECT_GET_CLASS (machine);
  GParamSpec *pspec;

  /* initialize buzz-host-callbacks (structure with callbacks)
   * buzzmachines can then call c function of the host
   * would be good to set this as early as possible
   */
  pspec = g_object_class_find_property (klass, "host-callbacks");
  if (pspec && G_IS_PARAM_SPEC_POINTER (pspec)) {
    extern gpointer bt_buzz_callbacks_get (BtSong * song);

    g_object_set (machine, "host-callbacks",
        bt_buzz_callbacks_get (self->priv->song), NULL);
    GST_INFO ("  host-callbacks iface initialized");
  }
  pspec = g_object_class_find_property (klass, "wave-callbacks");
  if (pspec && G_IS_PARAM_SPEC_POINTER (pspec)) {
    extern gpointer bt_wavetable_callbacks_get (BtWavetable * self);
    BtWavetable *wavetable;

    g_object_get (self->priv->song, "wavetable", &wavetable, NULL);
    g_object_set (machine, "wave-callbacks",
        bt_wavetable_callbacks_get (wavetable), NULL);
    g_object_unref (wavetable);
    GST_INFO ("  wave-table bridge initialized");
  }
  // initialize child-proxy iface properties
  if (GSTBT_IS_CHILD_BIN (machine)) {
    if (!self->priv->voices) {
      GST_WARNING_OBJECT (self, "voices==0");
      //g_object_get(machine,"children",&self->priv->voices,NULL);
    } else {
      bt_machine_clamp_voices (self);
    }
    GST_INFO ("  child proxy iface initialized");
  }
  // initialize tempo iface properties
  if (GSTBT_IS_TEMPO (machine)) {
    BtSongInfo *song_info;
    BtSettings *settings = bt_settings_make ();
    gulong bpm, tpb;
    guint latency;

    g_object_get ((gpointer) (self->priv->song), "song-info", &song_info, NULL);
    g_object_get (song_info, "bpm", &bpm, "tpb", &tpb, NULL);
    g_object_get (settings, "latency", &latency, NULL);
    gstbt_tempo_change_tempo (GSTBT_TEMPO (machine),
        (glong) bpm, (glong) tpb, update_subticks (bpm, tpb, latency));

    g_signal_connect (song_info, "notify::bpm",
        G_CALLBACK (bt_machine_on_bpm_changed), (gpointer) self);
    g_signal_connect (song_info, "notify::tpb",
        G_CALLBACK (bt_machine_on_tpb_changed), (gpointer) self);
    g_object_unref (song_info);
    g_signal_connect (settings, "notify::latency",
        G_CALLBACK (bt_machine_on_latency_changed), (gpointer) self);
    g_object_unref (settings);
    GST_INFO ("  tempo iface initialized");
  }
  // sync duration with song
  if (GST_IS_BASE_SRC (machine)) {
    BtSequence *sequence;

    g_object_get ((gpointer) (self->priv->song), "sequence", &sequence, NULL);
    bt_machine_on_duration_changed (sequence, NULL, (gpointer) self);
    g_signal_connect (sequence, "notify::length",
        G_CALLBACK (bt_machine_on_duration_changed), (gpointer) self);

    g_object_unref (sequence);
    GST_INFO ("  duration sync initialized");
  }
  GST_INFO ("machine element instantiated and interfaces initialized");
}

/*
 * bt_machine_check_type:
 *
 * Sanity check if the machine is of the right type. It counts the source,
 * sink pads and check with the machine class-type.
 *
 * Returns: %TRUE if type and pads match
 */
static gboolean
bt_machine_check_type (const BtMachine * const self)
{
  BtMachineClass *klass = BT_MACHINE_GET_CLASS (self);
  GstIterator *it;
  GstPad *pad;
  gulong pad_src_ct = 0, pad_sink_ct = 0;
  gboolean done;

  if (!klass->check_type) {
    GST_WARNING_OBJECT (self, "no BtMachine::check_type() implemented");
    return (TRUE);
  }
  // get pad counts per type
  it = gst_element_iterate_pads (self->priv->machines[PART_MACHINE]);
  done = FALSE;
  while (!done) {
    switch (gst_iterator_next (it, (gpointer) & pad)) {
      case GST_ITERATOR_OK:
        switch (gst_pad_get_direction (pad)) {
          case GST_PAD_SRC:
            pad_src_ct++;
            break;
          case GST_PAD_SINK:
            pad_sink_ct++;
            break;
          default:
            GST_INFO ("unhandled pad type discovered");
            break;
        }
        gst_object_unref (pad);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (it);
        break;
      case GST_ITERATOR_ERROR:
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (it);

  // test pad counts and element type
  if (!((klass->check_type) (self, pad_src_ct, pad_sink_ct))) {
    return (FALSE);
  }
  return (TRUE);
}

static void
bt_machine_init_prefs_params (const BtMachine * const self)
{
  GParamSpec **properties;
  guint number_of_properties;
  GstElement *machine = self->priv->machines[PART_MACHINE];

  if ((properties =
          g_object_class_list_properties (G_OBJECT_CLASS (GST_ELEMENT_GET_CLASS
                  (machine)), &number_of_properties))) {
    guint i, j;
    gboolean skip;

    for (i = 0; i < number_of_properties; i++) {
      skip = FALSE;
      if (properties[i]->flags & GST_PARAM_CONTROLLABLE)
        skip = TRUE;
      // skip uneditable gobject propertes properties
      else if (G_TYPE_IS_OBJECT (properties[i]->value_type))
        skip = TRUE;
      else if (G_TYPE_IS_INTERFACE (properties[i]->value_type))
        skip = TRUE;
      else if (properties[i]->value_type == G_TYPE_POINTER)
        skip = TRUE;
      // skip baseclass properties
      else if (!strncmp (properties[i]->name, "name\0", 5))
        skip = TRUE;
      else if (properties[i]->owner_type == GST_TYPE_BASE_SRC)
        skip = TRUE;
      else if (properties[i]->owner_type == GST_TYPE_BASE_TRANSFORM)
        skip = TRUE;
      else if (properties[i]->owner_type == GST_TYPE_BASE_SINK)
        skip = TRUE;
      else if (properties[i]->owner_type == GST_TYPE_BIN)
        skip = TRUE;
      // skip know interface properties (tempo, childbin)
      else if (properties[i]->owner_type == GSTBT_TYPE_CHILD_BIN)
        skip = TRUE;
      else if (properties[i]->owner_type == GSTBT_TYPE_TEMPO)
        skip = TRUE;

      if (skip) {
        properties[i] = NULL;
      } else {
        self->priv->prefs_params++;
      }
    }
    GST_INFO ("found %lu property params", self->priv->prefs_params);
    GParamSpec **params =
        (GParamSpec **) g_new (gpointer, self->priv->prefs_params);
    GObject **parents = (GObject **) g_new (gpointer, self->priv->prefs_params);

    for (i = j = 0; i < number_of_properties; i++) {
      if (properties[i]) {
        params[j] = properties[i];
        parents[j] = (GObject *) machine;
        j++;
      }
    }
    self->priv->prefs_param_group =
        bt_parameter_group_new (self->priv->prefs_params, parents, params,
        self->priv->song, self);

    g_free (properties);
  }
}

static void
bt_machine_init_global_params (const BtMachine * const self)
{
  GParamSpec **properties;
  guint number_of_properties;
  GstElement *machine = self->priv->machines[PART_MACHINE];

  if ((properties =
          g_object_class_list_properties (G_OBJECT_CLASS (GST_ELEMENT_GET_CLASS
                  (machine)), &number_of_properties))) {
    GParamSpec **child_properties = NULL;
    //GstController *ctrl;
    guint number_of_child_properties = 0;
    guint i, j;
    gboolean skip;

    // check if the elemnt implements the GstBtChildBin interface (implies GstChildProxy)
    if (GSTBT_IS_CHILD_BIN (self->priv->machines[PART_MACHINE])) {
      GstObject *voice_child;

      //g_assert(gst_child_proxy_get_children_count(GST_CHILD_PROXY(self->priv->machines[PART_MACHINE])));
      // get child for voice 0
      if ((voice_child =
              gst_child_proxy_get_child_by_index (GST_CHILD_PROXY (self->
                      priv->machines[PART_MACHINE]), 0))) {
        child_properties =
            g_object_class_list_properties (G_OBJECT_CLASS (GST_OBJECT_GET_CLASS
                (voice_child)), &number_of_child_properties);
        g_object_unref (voice_child);
      }
    }
    // count number of controlable params
    for (i = 0; i < number_of_properties; i++) {
      skip = FALSE;
      if (!(properties[i]->flags & GST_PARAM_CONTROLLABLE)) {
        skip = TRUE;
      } else if (child_properties) {
        // check if this param is also registered as child param, if so skip
        for (j = 0; j < number_of_child_properties; j++) {
          if (!strcmp (properties[i]->name, child_properties[j]->name)) {
            GST_DEBUG
                ("    skipping global_param [%d] \"%s\", due to voice_param[%d]",
                i, properties[i]->name, j);
            skip = TRUE;
            break;
          }
        }
      }
      if (skip) {
        properties[i] = NULL;
      } else {
        self->priv->global_params++;
      }
    }
    GST_INFO ("found %lu global params", self->priv->global_params);
    GParamSpec **params =
        (GParamSpec **) g_new (gpointer, self->priv->global_params);
    GObject **parents =
        (GObject **) g_new (gpointer, self->priv->global_params);

    for (i = j = 0; i < number_of_properties; i++) {
      if (properties[i]) {
        params[j] = properties[i];
        parents[j] = (GObject *) machine;
        j++;
      }
    }
    self->priv->global_param_group =
        bt_parameter_group_new (self->priv->global_params, parents, params,
        self->priv->song, self);

    g_free (properties);
    g_free (child_properties);
  }
}

static void
bt_machine_init_voice_params (const BtMachine * const self)
{
  // check if the elemnt implements the GstChildProxy interface
  if (GSTBT_IS_CHILD_BIN (self->priv->machines[PART_MACHINE])) {
    GstObject *voice_child;

    // register voice params
    // get child for voice 0
    if ((voice_child =
            gst_child_proxy_get_child_by_index (GST_CHILD_PROXY (self->
                    priv->machines[PART_MACHINE]), 0))) {
      GParamSpec **properties;
      guint number_of_properties;

      if ((properties =
              g_object_class_list_properties (G_OBJECT_CLASS
                  (GST_OBJECT_GET_CLASS (voice_child)),
                  &number_of_properties))) {
        guint i;

        // count number of controlable params
        for (i = 0; i < number_of_properties; i++) {
          if (properties[i]->flags & GST_PARAM_CONTROLLABLE)
            self->priv->voice_params++;
        }
        GST_INFO ("found %lu voice params", self->priv->voice_params);
      }
      g_free (properties);

      // bind params to machines voice controller
      bt_machine_resize_voices (self, 0);
      g_object_unref (voice_child);
    } else {
      GST_WARNING_OBJECT (self, "  can't get first voice child!");
    }
  } else {
    GST_INFO ("  instance is monophonic!");
    self->priv->voices = 0;
  }
}

//-- methods

/**
 * bt_machine_enable_input_pre_level:
 * @self: the machine to enable the pre-gain input-level analyser in
 *
 * Creates the pre-gain input-level analyser of the machine and activates it.
 *
 * Returns: %TRUE for success, %FALSE otherwise
 */
gboolean
bt_machine_enable_input_pre_level (BtMachine * const self)
{
  g_return_val_if_fail (BT_IS_MACHINE (self), FALSE);
  g_return_val_if_fail (!BT_IS_SOURCE_MACHINE (self), FALSE);

  return (bt_machine_enable_part (self, PART_INPUT_PRE_LEVEL, "level",
          "input_pre_level"));
}

/**
 * bt_machine_enable_input_post_level:
 * @self: the machine to enable the post-gain input-level analyser in
 *
 * Creates the post-gain input-level analyser of the machine and activates it.
 *
 * Returns: %TRUE for success, %FALSE otherwise
 */
gboolean
bt_machine_enable_input_post_level (BtMachine * const self)
{
  g_return_val_if_fail (BT_IS_MACHINE (self), FALSE);
  g_return_val_if_fail (!BT_IS_SOURCE_MACHINE (self), FALSE);

  return (bt_machine_enable_part (self, PART_INPUT_POST_LEVEL, "level",
          "input_post_level"));
}

/**
 * bt_machine_enable_output_pre_level:
 * @self: the machine to enable the pre-gain output-level analyser in
 *
 * Creates the pre-gain output-level analyser of the machine and activates it.
 *
 * Returns: %TRUE for success, %FALSE otherwise
 */
gboolean
bt_machine_enable_output_pre_level (BtMachine * const self)
{
  g_return_val_if_fail (BT_IS_MACHINE (self), FALSE);
  g_return_val_if_fail (!BT_IS_SINK_MACHINE (self), FALSE);

  return (bt_machine_enable_part (self, PART_OUTPUT_PRE_LEVEL, "level",
          "output_pre_level"));
}

/**
 * bt_machine_enable_output_post_level:
 * @self: the machine to enable the post-gain output-level analyser in
 *
 * Creates the post-gain output-level analyser of the machine and activates it.
 *
 * Returns: %TRUE for success, %FALSE otherwise
 */
gboolean
bt_machine_enable_output_post_level (BtMachine * const self)
{
  g_return_val_if_fail (BT_IS_MACHINE (self), FALSE);
  g_return_val_if_fail (!BT_IS_SINK_MACHINE (self), FALSE);

  return (bt_machine_enable_part (self, PART_OUTPUT_POST_LEVEL, "level",
          "output_post_level"));
}

/**
 * bt_machine_enable_input_gain:
 * @self: the machine to enable the input-gain element in
 *
 * Creates the input-gain element of the machine and activates it.
 *
 * Returns: %TRUE for success, %FALSE otherwise
 */
gboolean
bt_machine_enable_input_gain (BtMachine * const self)
{
  g_return_val_if_fail (BT_IS_MACHINE (self), FALSE);
  g_return_val_if_fail (!BT_IS_SOURCE_MACHINE (self), FALSE);
  return (bt_machine_enable_part (self, PART_INPUT_GAIN, "volume",
          "input_gain"));
}

/**
 * bt_machine_enable_output_gain:
 * @self: the machine to enable the output-gain element in
 *
 * Creates the output-gain element of the machine and activates it.
 *
 * Returns: %TRUE for success, %FALSE otherwise
 */
gboolean
bt_machine_enable_output_gain (BtMachine * const self)
{
  g_return_val_if_fail (BT_IS_MACHINE (self), FALSE);
  g_return_val_if_fail (!BT_IS_SINK_MACHINE (self), FALSE);
  return (bt_machine_enable_part (self, PART_OUTPUT_GAIN, "volume",
          "output_gain"));
}

/**
 * bt_machine_activate_adder:
 * @self: the machine to activate the adder in
 *
 * Machines use an adder to allow multiple incoming wires.
 * This method is used by the #BtWire class to activate the adder when needed.
 *
 * Returns: %TRUE for success
 */
gboolean
bt_machine_activate_adder (BtMachine * const self)
{
  gboolean res = FALSE;

  g_return_val_if_fail (BT_IS_MACHINE (self), FALSE);
  g_return_val_if_fail (!BT_IS_SOURCE_MACHINE (self), FALSE);

  GstElement **const machines = self->priv->machines;

  if (!machines[PART_ADDER]) {
    gboolean skip_convert = FALSE;
    GstPad **const src_pads = self->priv->src_pads;
    GstPad **const sink_pads = self->priv->sink_pads;
    guint i, tix = PART_MACHINE;

    // get first element on the source side
    for (i = PART_INPUT_PRE_LEVEL; i <= PART_MACHINE; i++) {
      if (machines[i]) {
        tix = i;
        GST_DEBUG_OBJECT (self, "src side target at %d: %s:%s", i,
            GST_DEBUG_PAD_NAME (sink_pads[tix]));
        break;
      }
    }

    // create the adder
    if (!(bt_machine_make_internal_element (self, PART_ADDER, "adder",
                "adder")))
      goto Error;
    /* live adder mixes by timestamps and does a timeout if an input is late */
    //if(!(bt_machine_make_internal_element(self,PART_ADDER,"liveadder","adder"))) goto Error;

    // try without capsfilter (>= 0.10.24)
    if (!g_object_class_find_property (G_OBJECT_CLASS (BT_MACHINE_GET_CLASS
                (machines[PART_ADDER])), "caps")) {
      if (!(bt_machine_make_internal_element (self, PART_CAPS_FILTER,
                  "capsfilter", "capsfilter")))
        goto Error;
      g_object_set (machines[PART_CAPS_FILTER], "caps", bt_default_caps, NULL);
    } else {
      g_object_set (machines[PART_ADDER], "caps", bt_default_caps, NULL);
    }

    if (!BT_IS_SINK_MACHINE (self)) {
      // try without converters in effects
      skip_convert =
          gst_caps_can_intersect (bt_default_caps,
          gst_pad_get_pad_template_caps (sink_pads[PART_MACHINE]));
    }
    if (skip_convert) {
      GST_DEBUG_OBJECT (self, "  about to link adder -> dst_elem");
      if (!machines[PART_CAPS_FILTER]) {
        if (!bt_machine_link_elements (self, src_pads[PART_ADDER],
                sink_pads[tix])) {
          GST_ERROR_OBJECT (self,
              "failed to link the internal adder of machine");
          goto Error;
        }
      } else {
        res =
            bt_machine_link_elements (self, src_pads[PART_ADDER],
            sink_pads[PART_CAPS_FILTER]);
        res &=
            bt_machine_link_elements (self, src_pads[PART_CAPS_FILTER],
            sink_pads[tix]);
        if (!res) {
          gst_pad_unlink (src_pads[PART_ADDER], sink_pads[PART_CAPS_FILTER]);
          gst_pad_unlink (src_pads[PART_CAPS_FILTER], sink_pads[tix]);
          GST_ERROR_OBJECT (self,
              "failed to link the internal adder of machine");
          goto Error;
        }
      }
    } else {
      GST_INFO_OBJECT (self, "adding converter");
      if (!(bt_machine_make_internal_element (self, PART_ADDER_CONVERT,
                  "audioconvert", "audioconvert")))
        goto Error;
      if (!BT_IS_SINK_MACHINE (self)) {
        // only do this for the final mix, if at all
        g_object_set (machines[PART_ADDER_CONVERT], "dithering", 0,
            "noise-shaping", 0, NULL);
      }
      GST_DEBUG_OBJECT (self, "  about to link adder -> convert -> dst_elem");
      if (!machines[PART_CAPS_FILTER]) {
        res =
            bt_machine_link_elements (self, src_pads[PART_ADDER],
            sink_pads[PART_ADDER_CONVERT]);
        res &=
            bt_machine_link_elements (self, src_pads[PART_ADDER_CONVERT],
            sink_pads[tix]);
        if (!res) {
          gst_pad_unlink (src_pads[PART_ADDER], sink_pads[PART_ADDER_CONVERT]);
          gst_pad_unlink (src_pads[PART_ADDER_CONVERT], sink_pads[tix]);
          GST_ERROR_OBJECT (self,
              "failed to link the internal adder of machine");
          goto Error;
        }
      } else {
        res =
            bt_machine_link_elements (self, src_pads[PART_ADDER],
            sink_pads[PART_CAPS_FILTER]);
        res &=
            bt_machine_link_elements (self, src_pads[PART_CAPS_FILTER],
            sink_pads[PART_ADDER_CONVERT]);
        res &=
            bt_machine_link_elements (self, src_pads[PART_ADDER_CONVERT],
            sink_pads[tix]);
        if (!res) {
          gst_pad_unlink (src_pads[PART_ADDER], sink_pads[PART_CAPS_FILTER]);
          gst_pad_unlink (src_pads[PART_CAPS_FILTER],
              sink_pads[PART_ADDER_CONVERT]);
          gst_pad_unlink (src_pads[PART_ADDER_CONVERT], sink_pads[tix]);
          GST_ERROR_OBJECT (self,
              "failed to link the internal adder of machine");
          goto Error;
        }
      }
    }
    GST_DEBUG_OBJECT (self, "  adder activated");
  }
  res = TRUE;
Error:
  bt_machine_dbg_print_parts (self);
  bt_song_write_to_lowlevel_dot_file (self->priv->song);
  return (res);
}

/**
 * bt_machine_has_active_adder:
 * @self: the machine to check
 *
 * Checks if the machine currently uses an adder.
 * This method is used by the #BtWire class to activate the adder when needed.
 *
 * Returns: %TRUE for success
 */
gboolean
bt_machine_has_active_adder (const BtMachine * const self)
{
  g_return_val_if_fail (BT_IS_MACHINE (self), FALSE);

  return (self->priv->machines[PART_ADDER] != NULL);
}

// workaround for tee in not handling events, see design/gst/looplock.c
static gboolean
gst_tee_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:{
      GstElement *tee = (GstElement *) gst_pad_get_parent (pad);
      guint last_seq_num =
          GPOINTER_TO_UINT (g_object_get_qdata ((GObject *) tee,
              tee_last_seq_num));
      guint32 seq_num = gst_event_get_seqnum (event);
      if (seq_num != last_seq_num) {
        last_seq_num = seq_num;
        g_object_set_qdata ((GObject *) tee, tee_last_seq_num,
            GUINT_TO_POINTER (last_seq_num));
        GST_LOG_OBJECT (pad, "new seek: %u", seq_num);
        res = gst_pad_event_default (pad, event);
      } else {
        GstSeekFlags flags;
        gst_event_parse_seek (event, NULL, NULL, &flags, NULL, NULL, NULL,
            NULL);
        if (flags & GST_SEEK_FLAG_FLUSH) {
          GST_LOG_OBJECT (pad, "forwarding dup seek: %u", seq_num);
          res = gst_pad_event_default (pad, event);
        } else {
          /* drop duplicated event */
          GST_LOG_OBJECT (pad, "dropping dup seek: %u", seq_num);
        }
      }
      gst_object_unref (tee);
      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }
  return res;
}

static void
tee_on_pad_added (GstElement * gstelement, GstPad * new_pad, gpointer user_data)
{
  gst_pad_set_event_function (new_pad, gst_tee_src_event);
}

/**
 * bt_machine_activate_spreader:
 * @self: the machine to activate the spreader in
 *
 * Machines use a spreader to allow multiple outgoing wires.
 * This method is used by the #BtWire class to activate the spreader when needed.
 *
 * Returns: %TRUE for success
 */
gboolean
bt_machine_activate_spreader (BtMachine * const self)
{
  gboolean res = FALSE;

  g_return_val_if_fail (BT_IS_MACHINE (self), FALSE);
  g_return_val_if_fail (!BT_IS_SINK_MACHINE (self), FALSE);

  GstElement **const machines = self->priv->machines;

  if (!machines[PART_SPREADER]) {
    GstPad **const src_pads = self->priv->src_pads;
    GstPad **const sink_pads = self->priv->sink_pads;
    guint i, tix = PART_MACHINE;

    // get next element on the sink side
    for (i = PART_OUTPUT_POST_LEVEL; i >= PART_MACHINE; i--) {
      if (machines[i]) {
        tix = i;
        GST_DEBUG_OBJECT (self, "sink side target at %d: %s:%s", i,
            GST_DEBUG_PAD_NAME (src_pads[tix]));
        break;
      }
    }

    // create the spreader (tee)
    if (!(bt_machine_make_internal_element (self, PART_SPREADER, "tee", "tee")))
      goto Error;
    // workaround for tee in not handling events, see design/gst/looplock.c
    g_signal_connect (machines[PART_SPREADER], "pad-added",
        (GCallback) tee_on_pad_added, NULL);

    if (!bt_machine_link_elements (self, src_pads[tix],
            sink_pads[PART_SPREADER])) {
      GST_ERROR_OBJECT (self,
          "failed to link the internal spreader of machine");
      goto Error;
    }

    GST_DEBUG_OBJECT (self, "  spreader activated");
  }
  res = TRUE;
Error:
  bt_machine_dbg_print_parts (self);
  bt_song_write_to_lowlevel_dot_file (self->priv->song);
  return (res);
}

/**
 * bt_machine_has_active_spreader:
 * @self: the machine to check
 *
 * Checks if the machine currently uses an spreader.
 * This method is used by the #BtWire class to activate the spreader when needed.
 *
 * Returns: %TRUE for success
 */
gboolean
bt_machine_has_active_spreader (const BtMachine * const self)
{
  g_return_val_if_fail (BT_IS_MACHINE (self), FALSE);

  return (self->priv->machines[PART_SPREADER] != NULL);
}

//-- pattern handling

// DEBUG
#ifdef CHECK_PATTERN_OWNERSHIP
static void
check_pattern_ownership (gpointer data, GObject * obj)
{
  BtMachine *self = BT_MACHINE (data);

  GST_DEBUG ("destroyed pattern: %p", obj);
  if (g_list_find (self->priv->patterns, obj)) {
    GST_WARNING_OBJECT (self,
        "pattern %p destroyed, but still in machines pattern list", obj);
    //g_assert_not_reached();
  }
}
#endif
// DEBUG

/**
 * bt_machine_add_pattern:
 * @self: the machine to add the pattern to
 * @pattern: the new pattern instance
 *
 * Add the supplied pattern to the machine. This is automatically done by
 * bt_pattern_new().
 */
void
bt_machine_add_pattern (const BtMachine * const self,
    const BtCmdPattern * const pattern)
{
  g_return_if_fail (BT_IS_MACHINE (self));
  g_return_if_fail (BT_IS_CMD_PATTERN (pattern));

  // TODO(ensonic): check unique name? we could also have a hashtable to speed up
  // the lookups
  if (!g_list_find (self->priv->patterns, pattern)) {
    self->priv->patterns =
        g_list_append (self->priv->patterns, g_object_ref ((gpointer) pattern));

    // DEBUG
#ifdef CHECK_PATTERN_OWNERSHIP
    g_object_weak_ref ((GObject *) pattern, check_pattern_ownership,
        (gpointer) self);
#endif
    // DEBUG

    // check if its a internal pattern and if it is update count of those
    if (!BT_IS_PATTERN (pattern)) {
      self->priv->private_patterns++;
      GST_DEBUG ("adding internal pattern %" G_OBJECT_REF_COUNT_FMT ", nr=%u",
          G_OBJECT_LOG_REF_COUNT (pattern), self->priv->private_patterns);
    } else {
      GST_DEBUG ("adding pattern %" G_OBJECT_REF_COUNT_FMT,
          G_OBJECT_LOG_REF_COUNT (pattern));
      g_signal_emit ((gpointer) self, signals[PATTERN_ADDED_EVENT], 0, pattern);
    }
  } else {
    GST_WARNING_OBJECT (self, "trying to add pattern again");
  }
}

/**
 * bt_machine_remove_pattern:
 * @self: the machine to remove the pattern from
 * @pattern: the existing pattern instance
 *
 * Remove the given pattern from the machine.
 */
void
bt_machine_remove_pattern (const BtMachine * const self,
    const BtCmdPattern * const pattern)
{
  GList *node;
  g_return_if_fail (BT_IS_MACHINE (self));
  g_return_if_fail (BT_IS_CMD_PATTERN (pattern));

  if ((node = g_list_find (self->priv->patterns, pattern))) {
    // DEBUG
#ifdef CHECK_PATTERN_OWNERSHIP
    g_object_weak_unref ((GObject *) pattern, check_pattern_ownership,
        (gpointer) self);
#endif
    // DEBUG

    self->priv->patterns = g_list_delete_link (self->priv->patterns, node);

    GST_DEBUG ("removing pattern: %" G_OBJECT_REF_COUNT_FMT,
        G_OBJECT_LOG_REF_COUNT (pattern));
    g_signal_emit ((gpointer) self, signals[PATTERN_REMOVED_EVENT], 0, pattern);
    GST_DEBUG ("removed pattern: %" G_OBJECT_REF_COUNT_FMT,
        G_OBJECT_LOG_REF_COUNT (pattern));
    g_object_unref ((gpointer) pattern);
  } else {
    GST_WARNING_OBJECT (self,
        "trying to remove pattern that is not in machine");
  }
}

/**
 * bt_machine_get_pattern_by_name:
 * @self: the machine to search for the pattern
 * @name: the name of the pattern
 *
 * Search the machine for a pattern by the supplied name.
 * The pattern must have been added previously to this setup with #bt_machine_add_pattern().
 * Unref the pattern, when done with it.
 *
 * Returns: #BtCmdPattern instance or %NULL if not found
 */
BtCmdPattern *
bt_machine_get_pattern_by_name (const BtMachine * const self,
    const gchar * const name)
{
  gboolean found = FALSE;
  BtCmdPattern *pattern;
  gchar *pattern_name;
  GList *node;

  g_return_val_if_fail (BT_IS_MACHINE (self), NULL);
  g_return_val_if_fail (BT_IS_STRING (name), NULL);

  //GST_DEBUG("pattern-list.length=%d",g_list_length(self->priv->patterns));

  for (node = self->priv->patterns; node; node = g_list_next (node)) {
    pattern = BT_CMD_PATTERN (node->data);
    g_object_get (pattern, "name", &pattern_name, NULL);
    if (!strcmp (pattern_name, name))
      found = TRUE;
    g_free (pattern_name);
    if (found)
      return (g_object_ref (pattern));
  }
  GST_DEBUG ("no pattern found for name \"%s\"", name);
  return NULL;
}

/**
 * bt_machine_get_pattern_by_index:
 * @self: the machine to search for the pattern
 * @index: the index of the pattern in the machines pattern list
 *
 * Fetches the pattern from the given position of the machines pattern list.
 * The pattern must have been added previously to this setup with #bt_machine_add_pattern().
 * Unref the pattern, when done with it.
 *
 * Returns: #BtCmdPattern instance or %NULL if not found
 */
BtCmdPattern *
bt_machine_get_pattern_by_index (const BtMachine * const self,
    const gulong index)
{
  BtCmdPattern *pattern;
  g_return_val_if_fail (BT_IS_MACHINE (self), NULL);

  if ((pattern = g_list_nth_data (self->priv->patterns, (guint) index))) {
    return g_object_ref (pattern);
  }
  return NULL;
}

// legacy helper for pre-0.7 songs
BtCmdPattern *
bt_machine_get_pattern_by_id (const BtMachine * const self,
    const gchar * const id)
{
  GObject *pattern;
  GList *node;

  for (node = self->priv->patterns; node; node = g_list_next (node)) {
    pattern = (GObject *) node->data;
    if (!g_strcmp0 (g_object_get_data (pattern, "BtPattern::id"), id)) {
      GST_INFO ("legacy pattern lookup for '%s' = %p", id, pattern);
      return g_object_ref (pattern);
    }
  }
  return NULL;
}

/**
 * bt_machine_get_unique_pattern_name:
 * @self: the machine for which the name should be unique
 *
 * The function generates a unique pattern name for this machine by eventually
 * adding a number postfix. This method should be used when adding new patterns.
 *
 * Returns: the newly allocated unique name
 */
gchar *
bt_machine_get_unique_pattern_name (const BtMachine * const self)
{
  BtCmdPattern *pattern = NULL;
  gchar name[3];
  guint i = 0;

  g_return_val_if_fail (BT_IS_MACHINE (self), NULL);

  do {
    (void) g_sprintf (name, "%02u", i++);
    g_object_try_unref (pattern);
  } while ((pattern = bt_machine_get_pattern_by_name (self, name))
      && (i < 100));
  g_object_try_unref (pattern);

  return g_strdup (name);
}

/**
 * bt_machine_has_patterns:
 * @self: the machine for which to check the patterns
 *
 * Check if the machine has #BtPattern entries appart from the standart private
 * ones.
 *
 * Returns: %TRUE if it has patterns
 */
gboolean
bt_machine_has_patterns (const BtMachine * const self)
{
  return (g_list_length (self->priv->patterns) > self->priv->private_patterns);
}

//-- global and voice param handling

/**
 * bt_machine_is_polyphonic:
 * @self: the machine to check
 *
 * Tells if the machine can produce (multiple) voices. Monophonic machines have
 * their (one) voice params as part of the global params.
 *
 * Returns: %TRUE for polyphic machines, %FALSE for monophonic ones
 */
gboolean
bt_machine_is_polyphonic (const BtMachine * const self)
{
  gboolean res;
  g_return_val_if_fail (BT_IS_MACHINE (self), FALSE);

  res = GSTBT_IS_CHILD_BIN (self->priv->machines[PART_MACHINE]);
  GST_INFO (" is machine \"%s\" polyphonic ? %d", self->priv->id, res);
  return (res);
}

/**
 * bt_machine_handles_waves:
 * @self: the machine to check
 *
 * Tells if the machine is using the wave-table.
 *
 * Since: 0.7
 * Returns: %TRUE for wavetable machines, %FALSE otherwise
 */
gboolean
bt_machine_handles_waves (const BtMachine * const self)
{
  BtParameterGroup *pg;
  gboolean handles_waves = FALSE;

  if ((pg = self->priv->global_param_group) &&
      (bt_parameter_group_get_wave_param_index (pg) != -1)) {
    handles_waves = TRUE;
  } else if (bt_machine_is_polyphonic (self) &&
      (self->priv->voices > 0) && (pg = self->priv->voice_param_groups[0]) &&
      (bt_parameter_group_get_wave_param_index (pg) != -1)) {
    handles_waves = TRUE;
  }
  return handles_waves;
}

/**
 * bt_machine_get_prefs_param_group:
 * @self: the machine
 *
 * Get the parameter group of machine properties. Properties are settings that
 * cannot be changed during playback.
 *
 * Returns: the #BtParameterGroup or %NULL
 */
BtParameterGroup *
bt_machine_get_prefs_param_group (const BtMachine * const self)
{
  g_return_val_if_fail (BT_IS_MACHINE (self), NULL);
  return self->priv->prefs_param_group;
}

/**
 * bt_machine_get_global_param_group:
 * @self: the machine
 *
 * Get the parameter group of global parameters.
 *
 * Returns: the #BtParameterGroup or %NULL
 */
BtParameterGroup *
bt_machine_get_global_param_group (const BtMachine * const self)
{
  g_return_val_if_fail (BT_IS_MACHINE (self), NULL);
  return self->priv->global_param_group;
}

/**
 * bt_machine_get_voice_param_group:
 * @self: the machine
 * @voice: the voice number
 *
 * Get the parameter group of voice parameters for the given @voice.
 *
 * Returns: the #BtParameterGroup or %NULL
 */
BtParameterGroup *
bt_machine_get_voice_param_group (const BtMachine * const self,
    const gulong voice)
{
  g_return_val_if_fail (BT_IS_MACHINE (self), NULL);

  return (voice <
      self->priv->voices) ? self->priv->voice_param_groups[voice] : NULL;
}

/**
 * bt_machine_set_param_defaults:
 * @self: the machine
 *
 * Sets default values that should be used before the first control-point.
 * Should be called, if all parameters are changed (like after switching
 * presets).
 */
void
bt_machine_set_param_defaults (const BtMachine * const self)
{
  const gulong voices = self->priv->voices;
  gulong j;

  bt_parameter_group_set_param_defaults (self->priv->global_param_group);
  for (j = 0; j < voices; j++) {
    bt_parameter_group_set_param_defaults (self->priv->voice_param_groups[j]);
  }
}

//-- interaction control

static void
free_control_data (BtControlData * data)
{
  BtIcDevice *device;

  // stop the device
  g_object_get ((gpointer) (data->control), "device", &device, NULL);
  if (device) {
    btic_device_stop (device);
    g_object_unref (device);
  }
  // disconnect the handler
  g_signal_handler_disconnect ((gpointer) data->control, data->handler_id);
  g_object_set (data->control, "bound", FALSE, NULL);
  g_object_set_qdata ((GObject *) data->control, bt_machine_machine, NULL);
  g_object_set_qdata ((GObject *) data->control, bt_machine_property_name,
      NULL);
  g_object_unref (data->control);

  g_free (data);
}

static void
on_boolean_control_notify (const BtIcControl * control, GParamSpec * arg,
    gpointer user_data)
{
  BtControlData *data = (BtControlData *) (user_data);
  BtMonoControlData *mdata = (BtMonoControlData *) (user_data);
  gboolean value;

  g_object_get ((gpointer) (data->control), "value", &value, NULL);
  g_object_set (mdata->object, data->pspec->name, value, NULL);
  bt_parameter_group_set_param_default (data->pg, data->pi);
}

#define ON_CONTROL_NOTIFY(t,T) \
static void on_## t ##_control_notify(const BtIcControl *control,GParamSpec *arg,gpointer user_data) { \
  BtControlData *data=(BtControlData *)(user_data);                                                    \
  BtMonoControlData *mdata = (BtMonoControlData *) (user_data);                                        \
  GParamSpec ## T *p=(GParamSpec ## T *)data->pspec;                                                   \
  glong svalue,min,max;                                                                                \
  g ## t dvalue;                                                                                       \
                                                                                                       \
  g_object_get((gpointer)(data->control),"value",&svalue,"min",&min,"max",&max,NULL);                  \
  dvalue=p->minimum+(g ## t)((svalue-min)*((gdouble)(p->maximum-p->minimum)/(gdouble)(max-min)));      \
  dvalue=CLAMP(dvalue,p->minimum,p->maximum);                                                          \
  g_object_set(mdata->object,((GParamSpec *)p)->name,dvalue,NULL);                                     \
  bt_parameter_group_set_param_default (data->pg, data->pi);                                           \
}

ON_CONTROL_NOTIFY (int, Int);
ON_CONTROL_NOTIFY (uint, UInt);
ON_CONTROL_NOTIFY (int64, Int64);
ON_CONTROL_NOTIFY (uint64, UInt64);
ON_CONTROL_NOTIFY (float, Float);
ON_CONTROL_NOTIFY (double, Double);

static void
on_enum_control_notify (const BtIcControl * control,
    GParamSpec * arg, gpointer user_data)
{
  BtControlData *data = (BtControlData *) (user_data);
  BtMonoControlData *mdata = (BtMonoControlData *) (user_data);
  GParamSpecEnum *p = (GParamSpecEnum *) data->pspec;
  GEnumClass *e = p->enum_class;
  glong svalue, min, max;
  gint dvalue;

  g_object_get ((gpointer) (data->control), "value", &svalue, "min", &min,
      "max", &max, NULL);
  dvalue =
      (gint) ((svalue - min) * ((gdouble) (e->n_values) / (gdouble) (max -
              min)));
  g_object_set (mdata->object, ((GParamSpec *) p)->name,
      e->values[dvalue].value, NULL);
  bt_parameter_group_set_param_default (data->pg, data->pi);
}

/**
 * bt_machine_bind_parameter_control:
 * @self: machine
 * @object: child object (global or voice child)
 * @property_name: name of the parameter
 * @control: interaction control object
 * @pg: the parameter group of the property
 *
 * Connect the interaction control object to the give parameter. Changes of the
 * control-value are mapped into a change of the parameter.
 */
void
bt_machine_bind_parameter_control (const BtMachine * const self,
    GstObject * object, const gchar * property_name, BtIcControl * control,
    BtParameterGroup * pg)
{
  BtControlData *data;
  GParamSpec *pspec;
  BtIcDevice *device;
  gboolean new_data = FALSE;

  pspec =
      g_object_class_find_property (G_OBJECT_GET_CLASS (object), property_name);

  data =
      (BtControlData *) g_hash_table_lookup (self->priv->control_data,
      (gpointer) pspec);
  if (!data) {
    new_data = TRUE;
    data = (BtControlData *) g_new (BtMonoControlData, 1);
    data->type = BT_MONO_CONTROL_DATA;
    data->pspec = pspec;
    data->pg = pg;
    data->pi = bt_parameter_group_get_param_index (pg, property_name);
    ((BtMonoControlData *) data)->object = object;
  } else {
    // stop the old device
    g_object_get ((gpointer) (data->control), "device", &device, NULL);
    btic_device_stop (device);
    g_object_unref (device);
    // disconnect old signal handler
    g_signal_handler_disconnect ((gpointer) data->control, data->handler_id);
    g_object_unref ((gpointer) (data->control));
  }
  data->control = g_object_ref (control);
  // start the new device
  g_object_get ((gpointer) (data->control), "device", &device, NULL);
  btic_device_start (device);
  g_object_unref (device);
  /* TODO(ensonic): controls need flags to indicate whether they are absolute or
   * relative, we connect a different handler for relative ones that add/sub
   * values to current value
   */
  // connect signal handler
  switch (bt_g_type_get_base_type (pspec->value_type)) {
    case G_TYPE_BOOLEAN:
      data->handler_id =
          g_signal_connect (control, "notify::value",
          G_CALLBACK (on_boolean_control_notify), (gpointer) data);
      break;
    case G_TYPE_INT:
      data->handler_id =
          g_signal_connect (control, "notify::value",
          G_CALLBACK (on_int_control_notify), (gpointer) data);
      break;
    case G_TYPE_UINT:
      data->handler_id =
          g_signal_connect (control, "notify::value",
          G_CALLBACK (on_uint_control_notify), (gpointer) data);
      break;
    case G_TYPE_INT64:
      data->handler_id =
          g_signal_connect (control, "notify::value",
          G_CALLBACK (on_int64_control_notify), (gpointer) data);
      break;
    case G_TYPE_UINT64:
      data->handler_id =
          g_signal_connect (control, "notify::value",
          G_CALLBACK (on_uint64_control_notify), (gpointer) data);
      break;
    case G_TYPE_FLOAT:
      data->handler_id =
          g_signal_connect (control, "notify::value",
          G_CALLBACK (on_float_control_notify), (gpointer) data);
      break;
    case G_TYPE_DOUBLE:
      data->handler_id =
          g_signal_connect (control, "notify::value",
          G_CALLBACK (on_double_control_notify), (gpointer) data);
      break;
    case G_TYPE_ENUM:
      data->handler_id =
          g_signal_connect (control, "notify::value",
          G_CALLBACK (on_enum_control_notify), (gpointer) data);
      break;
    default:
      GST_WARNING_OBJECT (self, "unhandled type \"%s\"",
          G_PARAM_SPEC_TYPE_NAME (pspec));
      break;
  }

  g_object_set_qdata ((GObject *) data->control, bt_machine_machine,
      (gpointer) self);
  g_object_set_qdata ((GObject *) data->control, bt_machine_property_name,
      (gpointer) pspec->name);
  if (new_data) {
    g_hash_table_insert (self->priv->control_data, (gpointer) pspec,
        (gpointer) data);
    g_object_set (data->control, "bound", TRUE, NULL);
  }
}

static void
on_boolean_poly_control_notify (const BtIcControl * control, GParamSpec * arg,
    gpointer user_data)
{
  BtControlData *data = (BtControlData *) (user_data);
  BtPolyControlData *pdata = (BtPolyControlData *) (user_data);
  GstElement *machine;
  GstObject *object;
  gboolean value;

  g_object_get ((gpointer) (data->control), "value", &value, NULL);
  machine = pdata->machine_priv->machines[PART_MACHINE];
  object =
      gst_child_proxy_get_child_by_index (GST_CHILD_PROXY (machine),
      pdata->voice_ct);
  g_object_set (object, data->pspec->name, value, NULL);
  bt_parameter_group_set_param_default (data->pg, data->pi);
  pdata->voice_ct = (pdata->voice_ct + 1) % pdata->machine_priv->voices;
}

#define ON_POLY_CONTROL_NOTIFY(t,T) \
static void on_## t ##_poly_control_notify(const BtIcControl *control,GParamSpec *arg,gpointer user_data) { \
  BtControlData *data = (BtControlData *) (user_data);                                                 \
  BtPolyControlData *pdata=(BtPolyControlData *)(user_data);                                           \
  GstElement *machine;                                                                                 \
  GstObject *object;                                                                                   \
  GParamSpec ## T *p=(GParamSpec ## T *)data->pspec;                                                   \
  glong svalue,min,max;                                                                                \
  g ## t dvalue;                                                                                       \
                                                                                                       \
  g_object_get((gpointer)(data->control),"value",&svalue,"min",&min,"max",&max,NULL);                  \
  dvalue=p->minimum+(g ## t)((svalue-min)*((gdouble)(p->maximum-p->minimum)/(gdouble)(max-min)));      \
  dvalue=CLAMP(dvalue,p->minimum,p->maximum);                                                          \
  machine = pdata->machine_priv->machines[PART_MACHINE];                                               \
  object = gst_child_proxy_get_child_by_index (GST_CHILD_PROXY (machine), pdata->voice_ct);            \
  g_object_set(object,((GParamSpec *)p)->name,dvalue,NULL);                                            \
  bt_parameter_group_set_param_default (data->pg, data->pi);                                           \
  pdata->voice_ct = (pdata->voice_ct + 1) % pdata->machine_priv->voices;                               \
}

ON_POLY_CONTROL_NOTIFY (int, Int);
ON_POLY_CONTROL_NOTIFY (uint, UInt);
ON_POLY_CONTROL_NOTIFY (int64, Int64);
ON_POLY_CONTROL_NOTIFY (uint64, UInt64);
ON_POLY_CONTROL_NOTIFY (float, Float);
ON_POLY_CONTROL_NOTIFY (double, Double);

static void
on_enum_poly_control_notify (const BtIcControl * control, GParamSpec * arg,
    gpointer user_data)
{
  BtControlData *data = (BtControlData *) (user_data);
  BtPolyControlData *pdata = (BtPolyControlData *) (user_data);
  GstElement *machine;
  GstObject *object;
  GParamSpecEnum *p = (GParamSpecEnum *) data->pspec;
  GEnumClass *e = p->enum_class;
  glong svalue, min, max;
  gint dvalue;

  g_object_get ((gpointer) (data->control), "value", &svalue, "min", &min,
      "max", &max, NULL);
  dvalue =
      (gint) ((svalue - min) * ((gdouble) (e->n_values) / (gdouble) (max -
              min)));

  machine = pdata->machine_priv->machines[PART_MACHINE];
  object =
      gst_child_proxy_get_child_by_index (GST_CHILD_PROXY (machine),
      pdata->voice_ct);
  g_object_set (object, ((GParamSpec *) p)->name, e->values[dvalue].value,
      NULL);
  bt_parameter_group_set_param_default (data->pg, data->pi);
  pdata->voice_ct = (pdata->voice_ct + 1) % pdata->machine_priv->voices;
}

/**
 * bt_machine_bind_poly_parameter_control:
 * @self: machine
 * @property_name: name of the parameter
 * @control: interaction control object
 * @pg: the parameter group of the property
 *
 * Connect the interaction control object to the give parameter. Changes of the
 * control-value are mapped into a change of the parameter.
 */
void
bt_machine_bind_poly_parameter_control (const BtMachine * const self,
    const gchar * property_name, BtIcControl * control, BtParameterGroup * pg)
{
  BtControlData *data;
  GstElement *machine;
  GstObject *object;
  GParamSpec *pspec;
  BtIcDevice *device;
  gboolean new_data = FALSE;

  machine = self->priv->machines[PART_MACHINE];
  object = gst_child_proxy_get_child_by_index (GST_CHILD_PROXY (machine), 0);
  pspec =
      g_object_class_find_property (G_OBJECT_GET_CLASS (object), property_name);

  data =
      (BtControlData *) g_hash_table_lookup (self->priv->control_data,
      (gpointer) pspec);
  if (!data) {
    new_data = TRUE;
    data = (BtControlData *) g_new (BtPolyControlData, 1);
    data->type = BT_POLY_CONTROL_DATA;
    data->pspec = pspec;
    data->pg = pg;
    data->pi = bt_parameter_group_get_param_index (pg, property_name);
    ((BtPolyControlData *) data)->voice_ct = 0;
    ((BtPolyControlData *) data)->machine_priv = self->priv;
  } else {
    // stop the old device
    g_object_get ((gpointer) (data->control), "device", &device, NULL);
    btic_device_stop (device);
    g_object_unref (device);
    // disconnect old signal handler
    g_signal_handler_disconnect ((gpointer) data->control, data->handler_id);
    g_object_unref ((gpointer) (data->control));
  }
  data->control = g_object_ref (control);
  // start the new device
  g_object_get ((gpointer) (data->control), "device", &device, NULL);
  btic_device_start (device);
  g_object_unref (device);
  /* TODO(ensonic): controls need flags to indicate whether they are absolute or relative
   * we conect a different handler for relative ones that add/sub values to current value
   */
  // connect signal handler
  switch (bt_g_type_get_base_type (pspec->value_type)) {
    case G_TYPE_BOOLEAN:
      data->handler_id =
          g_signal_connect (control, "notify::value",
          G_CALLBACK (on_boolean_poly_control_notify), (gpointer) data);
      break;
    case G_TYPE_INT:
      data->handler_id =
          g_signal_connect (control, "notify::value",
          G_CALLBACK (on_int_poly_control_notify), (gpointer) data);
      break;
    case G_TYPE_UINT:
      data->handler_id =
          g_signal_connect (control, "notify::value",
          G_CALLBACK (on_uint_poly_control_notify), (gpointer) data);
      break;
    case G_TYPE_INT64:
      data->handler_id =
          g_signal_connect (control, "notify::value",
          G_CALLBACK (on_int64_poly_control_notify), (gpointer) data);
      break;
    case G_TYPE_UINT64:
      data->handler_id =
          g_signal_connect (control, "notify::value",
          G_CALLBACK (on_uint64_poly_control_notify), (gpointer) data);
      break;
    case G_TYPE_FLOAT:
      data->handler_id =
          g_signal_connect (control, "notify::value",
          G_CALLBACK (on_float_poly_control_notify), (gpointer) data);
      break;
    case G_TYPE_DOUBLE:
      data->handler_id =
          g_signal_connect (control, "notify::value",
          G_CALLBACK (on_double_poly_control_notify), (gpointer) data);
      break;
    case G_TYPE_ENUM:
      data->handler_id =
          g_signal_connect (control, "notify::value",
          G_CALLBACK (on_enum_poly_control_notify), (gpointer) data);
      break;
    default:
      GST_WARNING_OBJECT (self, "unhandled type \"%s\"",
          G_PARAM_SPEC_TYPE_NAME (pspec));
      break;
  }

  g_object_set_qdata ((GObject *) data->control, bt_machine_machine,
      (gpointer) self);
  g_object_set_qdata ((GObject *) data->control, bt_machine_property_name,
      (gpointer) pspec->name);
  if (new_data) {
    g_hash_table_insert (self->priv->control_data, (gpointer) pspec,
        (gpointer) data);
    g_object_set (data->control, "bound", TRUE, NULL);
  }
}

/**
 * bt_machine_unbind_parameter_control:
 * @self: machine
 * @object: child object (global or voice child)
 * @property_name: name of the parameter
 *
 * Disconnect the interaction control object from the give parameter.
 */
void
bt_machine_unbind_parameter_control (const BtMachine * const self,
    GstObject * object, const gchar * property_name)
{
  GParamSpec *pspec;

  pspec =
      g_object_class_find_property (G_OBJECT_GET_CLASS (object), property_name);
  g_hash_table_remove (self->priv->control_data, (gpointer) pspec);
}

/**
 * bt_machine_unbind_parameter_controls:
 * @self: machine
 *
 * Disconnect all interaction controls.
 */
void
bt_machine_unbind_parameter_controls (const BtMachine * const self)
{
  g_hash_table_remove_all (self->priv->control_data);
}

//-- settings

/**
 * bt_machine_randomize_parameters:
 * @self: machine
 *
 * Randomizes machine parameters.
 */
void
bt_machine_randomize_parameters (const BtMachine * const self)
{
  const gulong voices = self->priv->voices;
  gulong j;

  bt_parameter_group_randomize_values (self->priv->global_param_group);
  for (j = 0; j < voices; j++) {
    bt_parameter_group_randomize_values (self->priv->voice_param_groups[j]);
  }
  bt_machine_set_param_defaults (self);
}

/**
 * bt_machine_reset_parameters:
 * @self: machine
 *
 * Resets machine parameters back to defaults.
 */
void
bt_machine_reset_parameters (const BtMachine * const self)
{
  const gulong voices = self->priv->voices;
  gulong j;

  bt_parameter_group_reset_values (self->priv->global_param_group);
  for (j = 0; j < voices; j++) {
    bt_parameter_group_reset_values (self->priv->voice_param_groups[j]);
  }
}

//-- linking

/**
 * bt_machine_get_wire_by_dst_machine:
 * @self: the machine that is at src of a wire
 * @dst: the machine that is at the dst end of the wire
 *
 * Searches for a wire in the wires originating from this machine that uses the
 * given #BtMachine instances as a target. Unref the wire, when done with it.
 *
 * Returns: the #BtWire or NULL
 *
 * Since: 0.6
 */
BtWire *
bt_machine_get_wire_by_dst_machine (const BtMachine * const self,
    const BtMachine * const dst)
{
  gboolean found = FALSE;
  BtMachine *const machine;
  const GList *node;

  g_return_val_if_fail (BT_IS_MACHINE (self), NULL);
  g_return_val_if_fail (BT_IS_MACHINE (dst), NULL);

  // either src or dst has no wires
  if (!self->src_wires || !dst->dst_wires)
    return (NULL);

  // check if self links to dst
  // ideally we would search the shorter of the lists
  for (node = self->src_wires; node; node = g_list_next (node)) {
    BtWire *const wire = BT_WIRE (node->data);
    g_object_get (wire, "dst", &machine, NULL);
    if (machine == dst)
      found = TRUE;
    g_object_unref (machine);
    if (found)
      return (g_object_ref (wire));
  }
  GST_DEBUG ("no wire found for machines %p:%s %p:%s", self,
      GST_OBJECT_NAME (self), dst, GST_OBJECT_NAME (dst));
  return (NULL);
}


//-- debug helper

// used in bt_song_write_to_highlevel_dot_file
GList *
bt_machine_get_element_list (const BtMachine * const self)
{
  GList *list = NULL;
  gulong i;

  for (i = 0; i < PART_COUNT; i++) {
    if (self->priv->machines[i]) {
      list = g_list_append (list, self->priv->machines[i]);
    }
  }

  return (list);
}

void
bt_machine_dbg_print_parts (const BtMachine * const self)
{
  /* [A AC I<L IG I>L M O<L OG O>L S] */
  GST_INFO ("%s [%s %s %s %s %s %s %s %s %s %s]",
      self->priv->id,
      self->priv->machines[PART_ADDER] ? "A" : "a",
      self->priv->machines[PART_ADDER_CONVERT] ? "AC" : "ac",
      self->priv->machines[PART_INPUT_PRE_LEVEL] ? "I<L" : "i<l",
      self->priv->machines[PART_INPUT_GAIN] ? "IG" : "ig",
      self->priv->machines[PART_INPUT_POST_LEVEL] ? "I>L" : "i>l",
      self->priv->machines[PART_MACHINE] ? "M" : "m",
      self->priv->machines[PART_OUTPUT_PRE_LEVEL] ? "O<L" : "o<l",
      self->priv->machines[PART_OUTPUT_GAIN] ? "OG" : "og",
      self->priv->machines[PART_OUTPUT_POST_LEVEL] ? "O>L" : "o>l",
      self->priv->machines[PART_SPREADER] ? "S" : "s");
}

//-- io interface

static xmlNodePtr
bt_machine_persistence_save (const BtPersistence * const persistence,
    const xmlNodePtr const parent_node)
{
  const BtMachine *const self = BT_MACHINE (persistence);
  GstObject *machine, *machine_voice;
  xmlNodePtr node = NULL;
  xmlNodePtr child_node;
  gulong i, j;
  GValue value = { 0, };

  GST_DEBUG ("PERSISTENCE::machine");

  if ((node = xmlNewChild (parent_node, NULL, XML_CHAR_PTR ("machine"), NULL))) {
    const gulong voices = self->priv->voices;
    const gulong prefs_params = self->priv->prefs_params;
    const gulong global_params = self->priv->global_params;
    const gulong voice_params = self->priv->voice_params;
    const gchar *pname;
    BtParameterGroup *prefs_param_group = self->priv->prefs_param_group;
    BtParameterGroup *global_param_group = self->priv->global_param_group;
    BtParameterGroup **voice_param_groups =
        self->priv->voice_param_groups, *voice_param_group;

    xmlNewProp (node, XML_CHAR_PTR ("id"), XML_CHAR_PTR (self->priv->id));
    xmlNewProp (node, XML_CHAR_PTR ("state"),
        XML_CHAR_PTR (bt_persistence_strfmt_enum (BT_TYPE_MACHINE_STATE,
                self->priv->state)));

    machine = GST_OBJECT (self->priv->machines[PART_MACHINE]);
    for (i = 0; i < prefs_params; i++) {
      if ((child_node =
              xmlNewChild (node, NULL, XML_CHAR_PTR ("prefsdata"), NULL))) {
        pname = bt_parameter_group_get_param_name (prefs_param_group, i);
        g_value_init (&value,
            bt_parameter_group_get_param_type (prefs_param_group, i));
        g_object_get_property (G_OBJECT (machine), pname, &value);
        gchar *const str = bt_persistence_get_value (&value);
        xmlNewProp (child_node, XML_CHAR_PTR ("name"), XML_CHAR_PTR (pname));
        xmlNewProp (child_node, XML_CHAR_PTR ("value"), XML_CHAR_PTR (str));
        g_free (str);
        g_value_unset (&value);
      }
    }
    for (i = 0; i < global_params; i++) {
      // skip trigger parameters
      if (bt_parameter_group_is_param_trigger (global_param_group, i))
        continue;
      if ((child_node =
              xmlNewChild (node, NULL, XML_CHAR_PTR ("globaldata"), NULL))) {
        pname = bt_parameter_group_get_param_name (global_param_group, i);
        g_value_init (&value,
            bt_parameter_group_get_param_type (global_param_group, i));
        g_object_get_property (G_OBJECT (machine), pname, &value);
        gchar *const str = bt_persistence_get_value (&value);
        xmlNewProp (child_node, XML_CHAR_PTR ("name"), XML_CHAR_PTR (pname));
        xmlNewProp (child_node, XML_CHAR_PTR ("value"), XML_CHAR_PTR (str));
        g_free (str);
        g_value_unset (&value);
      }
    }
    for (j = 0; j < voices; j++) {
      machine_voice =
          gst_child_proxy_get_child_by_index (GST_CHILD_PROXY (machine), j);
      voice_param_group = voice_param_groups[j];

      for (i = 0; i < voice_params; i++) {
        // skip trigger parameters
        if (bt_parameter_group_is_param_trigger (voice_param_group, i))
          continue;
        if ((child_node =
                xmlNewChild (node, NULL, XML_CHAR_PTR ("voicedata"), NULL))) {
          pname = bt_parameter_group_get_param_name (voice_param_group, i);
          g_value_init (&value,
              bt_parameter_group_get_param_type (voice_param_group, i));
          g_object_get_property (G_OBJECT (machine_voice), pname, &value);
          gchar *const str = bt_persistence_get_value (&value);
          xmlNewProp (child_node, XML_CHAR_PTR ("voice"),
              XML_CHAR_PTR (bt_persistence_strfmt_ulong (j)));
          xmlNewProp (child_node, XML_CHAR_PTR ("name"), XML_CHAR_PTR (pname));
          xmlNewProp (child_node, XML_CHAR_PTR ("value"), XML_CHAR_PTR (str));
          g_free (str);
          g_value_unset (&value);
        }
      }
      g_object_unref (machine_voice);
    }

    if (g_hash_table_size (self->priv->properties)) {
      if ((child_node =
              xmlNewChild (node, NULL, XML_CHAR_PTR ("properties"), NULL))) {
        if (!bt_persistence_save_hashtable (self->priv->properties, child_node))
          goto Error;
      } else
        goto Error;
    }
    if (bt_machine_has_patterns (self)) {
      if ((child_node =
              xmlNewChild (node, NULL, XML_CHAR_PTR ("patterns"), NULL))) {
        bt_persistence_save_list (self->priv->patterns, child_node);
      } else
        goto Error;
    }
    if (g_hash_table_size (self->priv->control_data)) {
      if ((child_node =
              xmlNewChild (node, NULL, XML_CHAR_PTR ("interaction-controllers"),
                  NULL))) {
        GList *list = NULL, *lnode;
        BtControlData *data;
        BtIcDevice *device;
        gchar *device_name, *control_name;
        xmlNodePtr sub_node;

        g_hash_table_foreach (self->priv->control_data,
            bt_persistence_collect_hashtable_entries, (gpointer) & list);

        for (lnode = list; lnode; lnode = g_list_next (lnode)) {
          data = (BtControlData *) lnode->data;

          g_object_get ((gpointer) (data->control), "device", &device, "name",
              &control_name, NULL);
          g_object_get (device, "name", &device_name, NULL);
          g_object_unref (device);

          sub_node =
              xmlNewChild (child_node, NULL,
              XML_CHAR_PTR ("interaction-controller"), NULL);
          // we need global or voiceXX here
          if (data->type == BT_MONO_CONTROL_DATA) {
            xmlNewProp (sub_node, XML_CHAR_PTR ("global"), XML_CHAR_PTR ("0"));
          } else {
            if (GSTBT_IS_CHILD_BIN (self->priv->machines[PART_MACHINE])) {
              xmlNewProp (sub_node, XML_CHAR_PTR ("voice"), XML_CHAR_PTR ("0"));
            }
          }
          xmlNewProp (sub_node, XML_CHAR_PTR ("parameter"),
              XML_CHAR_PTR (data->pspec->name));
          xmlNewProp (sub_node, XML_CHAR_PTR ("device"),
              XML_CHAR_PTR (device_name));
          xmlNewProp (sub_node, XML_CHAR_PTR ("control"),
              XML_CHAR_PTR (control_name));

          g_free (device_name);
          g_free (control_name);
        }
        g_list_free (list);
      } else
        goto Error;
    }
  }
Error:
  return (node);
}

static BtPersistence *
bt_machine_persistence_load (const GType type,
    const BtPersistence * const persistence, xmlNodePtr node, GError ** err,
    va_list var_args)
{
  BtMachine *const self = BT_MACHINE (persistence);
  xmlChar *name, *global_str, *voice_str, *value_str;
  xmlNodePtr child_node;
  GValue value = { 0, };
  glong param, voice;
  GstObject *machine;
  BtParameterGroup *pg;

  GST_DEBUG ("PERSISTENCE::machine");
  g_assert (node);

  if ((machine = GST_OBJECT (self->priv->machines[PART_MACHINE]))) {
    if ((value_str = xmlGetProp (node, XML_CHAR_PTR ("state")))) {
      self->priv->state =
          bt_persistence_parse_enum (BT_TYPE_MACHINE_STATE,
          (gchar *) value_str);
      xmlFree (value_str);
    }

    for (node = node->children; node; node = node->next) {
      if (!xmlNodeIsText (node)) {
        if (!strncmp ((gchar *) node->name, "prefsdata\0", 10)) {
          name = xmlGetProp (node, XML_CHAR_PTR ("name"));
          value_str = xmlGetProp (node, XML_CHAR_PTR ("value"));
          pg = self->priv->prefs_param_group;
          param = bt_parameter_group_get_param_index (pg, (gchar *) name);
          if ((param != -1) && value_str) {
            g_value_init (&value, bt_parameter_group_get_param_type (pg,
                    param));
            bt_persistence_set_value (&value, (gchar *) value_str);
            g_object_set_property (bt_parameter_group_get_param_parent (pg,
                    param), (gchar *) name, &value);
            g_value_unset (&value);
          }
          xmlFree (name);
          xmlFree (value_str);
        } else if (!strncmp ((gchar *) node->name, "globaldata\0", 11)) {
          name = xmlGetProp (node, XML_CHAR_PTR ("name"));
          value_str = xmlGetProp (node, XML_CHAR_PTR ("value"));
          pg = self->priv->global_param_group;
          param = bt_parameter_group_get_param_index (pg, (gchar *) name);
          if ((param != -1) && value_str) {
            g_value_init (&value, bt_parameter_group_get_param_type (pg,
                    param));
            bt_persistence_set_value (&value, (gchar *) value_str);
            g_object_set_property (bt_parameter_group_get_param_parent (pg,
                    param), (gchar *) name, &value);
            g_value_unset (&value);
            bt_parameter_group_set_param_default (pg, param);
          }
          xmlFree (name);
          xmlFree (value_str);
        } else if (!strncmp ((gchar *) node->name, "voicedata\0", 10)) {
          voice_str = xmlGetProp (node, XML_CHAR_PTR ("voice"));
          voice = atol ((char *) voice_str);
          name = xmlGetProp (node, XML_CHAR_PTR ("name"));
          value_str = xmlGetProp (node, XML_CHAR_PTR ("value"));
          pg = self->priv->voice_param_groups[voice];
          param = bt_parameter_group_get_param_index (pg, (gchar *) name);
          if ((param != -1) && value_str) {
            g_value_init (&value, bt_parameter_group_get_param_type (pg,
                    param));
            bt_persistence_set_value (&value, (gchar *) value_str);
            g_object_set_property (bt_parameter_group_get_param_parent (pg,
                    param), (gchar *) name, &value);
            g_value_unset (&value);
            bt_parameter_group_set_param_default (pg, param);
          }
          xmlFree (name);
          xmlFree (value_str);
          xmlFree (voice_str);
        } else if (!strncmp ((gchar *) node->name, "properties\0", 11)) {
          bt_persistence_load_hashtable (self->priv->properties, node);
        } else if (!strncmp ((gchar *) node->name, "patterns\0", 9)) {
          BtPattern *pattern;

          for (child_node = node->children; child_node;
              child_node = child_node->next) {
            if ((!xmlNodeIsText (child_node))
                && (!strncmp ((char *) child_node->name, "pattern\0", 8))) {
              GError *err = NULL;
              pattern =
                  BT_PATTERN (bt_persistence_load (BT_TYPE_PATTERN, NULL,
                      child_node, &err, "song", self->priv->song, "machine",
                      self, NULL));
              if (err != NULL) {
                GST_WARNING_OBJECT (self, "Can't create pattern: %s",
                    err->message);
                g_error_free (err);
              }
              g_object_unref (pattern);
            }
          }
        } else if (!strncmp ((gchar *) node->name, "interaction-controllers\0",
                24)) {
          BtIcRegistry *registry;
          BtIcDevice *device;
          BtIcControl *control;
          GList *lnode, *devices, *controls;
          gchar *name;
          xmlChar *device_str, *control_str, *property_name;
          gboolean found;

          registry = btic_registry_new ();
          g_object_get (registry, "devices", &devices, NULL);

          for (child_node = node->children; child_node;
              child_node = child_node->next) {
            if ((!xmlNodeIsText (child_node))
                && (!strncmp ((char *) child_node->name,
                        "interaction-controller\0", 23))) {
              control = NULL;

              if ((device_str =
                      xmlGetProp (child_node, XML_CHAR_PTR ("device")))) {
                found = FALSE;
                for (lnode = devices; lnode; lnode = g_list_next (lnode)) {
                  device = BTIC_DEVICE (lnode->data);
                  g_object_get (device, "name", &name, NULL);
                  if (!strcmp (name, (gchar *) device_str))
                    found = TRUE;
                  g_free (name);
                  if (found)
                    break;
                }
                if (found) {
                  if ((control_str =
                          xmlGetProp (child_node, XML_CHAR_PTR ("control")))) {
                    found = FALSE;
                    g_object_get (device, "controls", &controls, NULL);
                    for (lnode = controls; lnode; lnode = g_list_next (lnode)) {
                      control = BTIC_CONTROL (lnode->data);
                      g_object_get (control, "name", &name, NULL);
                      if (!strcmp (name, (gchar *) control_str))
                        found = TRUE;
                      g_free (name);
                      if (found)
                        break;
                    }
                    g_list_free (controls);
                    if (found) {
                      if ((property_name =
                              xmlGetProp (child_node,
                                  XML_CHAR_PTR ("parameter")))) {
                        if ((global_str =
                                xmlGetProp (child_node,
                                    XML_CHAR_PTR ("global")))) {
                          bt_machine_bind_parameter_control (self, machine,
                              (gchar *) property_name, control,
                              self->priv->global_param_group);
                          xmlFree (global_str);
                        } else {
                          if ((voice_str =
                                  xmlGetProp (child_node,
                                      XML_CHAR_PTR ("voice")))) {
                            bt_machine_bind_poly_parameter_control (self,
                                (gchar *) property_name,
                                control, self->priv->voice_param_groups[0]);
                            xmlFree (voice_str);
                          }
                        }
                        xmlFree (property_name);
                      }
                    }
                    xmlFree (control_str);
                  }
                }
                xmlFree (device_str);
              }
            }
          }
          g_list_free (devices);
        }
      }
    }
  }
  return (BT_PERSISTENCE (persistence));
}

static void
bt_machine_persistence_interface_init (gpointer const g_iface,
    gpointer const iface_data)
{
  BtPersistenceInterface *const iface = g_iface;

  iface->load = bt_machine_persistence_load;
  iface->save = bt_machine_persistence_save;
}

//-- wrapper

//-- gstelement overrides

static GstPad *
bt_machine_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * _name)
{
  BtMachine *const self = BT_MACHINE (element);
  gchar *name;
  GstPad *pad, *target;

  // check direction
  if (GST_PAD_TEMPLATE_DIRECTION (templ) == GST_PAD_SRC) {
    if (!src_pt) {
      src_pt =
          bt_gst_element_factory_get_pad_template (factories[PART_SPREADER],
          "src%d");
    }

    if (!(target = gst_element_request_pad (self->priv->machines[PART_SPREADER],
                src_pt, NULL, NULL))) {
      GST_WARNING_OBJECT (element, "failed to request pad 'src%d'");
      return NULL;
    }
    name = g_strdup_printf ("src%d", self->priv->src_pad_counter++);
    GST_INFO_OBJECT (element, "request src pad: %s", name);
  } else {
    if (!sink_pt) {
      sink_pt =
          bt_gst_element_factory_get_pad_template (factories[PART_ADDER],
          "sink%d");
    }

    if (!(target = gst_element_request_pad (self->priv->machines[PART_ADDER],
                sink_pt, NULL, NULL))) {
      GST_WARNING_OBJECT (element, "failed to request pad 'sink%d'");
      return NULL;
    }
    name = g_strdup_printf ("sink%d", self->priv->sink_pad_counter++);
    GST_INFO_OBJECT (element, "request sink pad: %s", name);
  }
  if ((pad = gst_ghost_pad_new (name, target))) {
    GST_INFO ("%s:%s: %s%s%s", GST_DEBUG_PAD_NAME (target),
        GST_OBJECT (target)->flags & GST_PAD_BLOCKED ? "blocked, " : "",
        GST_OBJECT (target)->flags & GST_PAD_FLUSHING ? "flushing, " : "",
        GST_OBJECT (target)->flags & GST_PAD_BLOCKING ? "blocking, " : "");
    GST_INFO ("%s:%s: %s%s%s", GST_DEBUG_PAD_NAME (pad),
        GST_OBJECT (pad)->flags & GST_PAD_BLOCKED ? "blocked, " : "",
        GST_OBJECT (pad)->flags & GST_PAD_FLUSHING ? "flushing, " : "",
        GST_OBJECT (pad)->flags & GST_PAD_BLOCKING ? "blocking, " : "");

    if (GST_STATE (element) != GST_STATE_NULL) {
      GST_DEBUG_OBJECT (element, "activating pad");
      gst_pad_set_active (pad, TRUE);
    }
    gst_element_add_pad (element, pad);
  } else {
    GST_WARNING_OBJECT (element, "failed to create ghostpad %s to target %s:%s",
        name, GST_DEBUG_PAD_NAME (target));
  }
  gst_object_unref (target);
  g_free (name);

  return (pad);
}

static void
bt_machine_release_pad (GstElement * element, GstPad * pad)
{
  BtMachine *const self = BT_MACHINE (element);
  GstPad *target;

  if (GST_STATE (element) == GST_STATE_PLAYING) {
    GST_DEBUG_OBJECT (element, "deactivating pad");
    gst_pad_set_active (pad, FALSE);
  }

  target = gst_ghost_pad_get_target (GST_GHOST_PAD (pad));
  gst_element_remove_pad (element, pad);

  if (gst_pad_get_direction (pad) == GST_PAD_SRC) {
    GST_INFO_OBJECT (element, "release src pad: %s:%s",
        GST_DEBUG_PAD_NAME (target));
    gst_element_release_request_pad (self->priv->machines[PART_SPREADER],
        target);
  } else {
    GST_INFO_OBJECT (element, "release sink pad: %s:%s",
        GST_DEBUG_PAD_NAME (target));
    gst_element_release_request_pad (self->priv->machines[PART_ADDER], target);
  }
  gst_object_unref (target);
}


//-- gobject overrides

static void
bt_machine_constructed (GObject * object)
{
  BtMachine *const self = BT_MACHINE (object);

  GST_INFO ("machine constructed ...");

  if (G_OBJECT_CLASS (bt_machine_parent_class)->constructed)
    G_OBJECT_CLASS (bt_machine_parent_class)->constructed (object);

  g_return_if_fail (BT_IS_SONG (self->priv->song));
  g_return_if_fail (BT_IS_STRING (self->priv->id));
  g_return_if_fail (BT_IS_STRING (self->priv->plugin_name));

  GST_INFO ("initializing machine");

  gst_object_set_name (GST_OBJECT (self), self->priv->id);
  GST_INFO ("naming machine : %s", self->priv->id);

  // name the machine and try to instantiate it
  if (!bt_machine_init_core_machine (self)) {
    goto Error;
  }
  // initialize iface properties
  bt_machine_init_interfaces (self);
  // we need to make sure the machine is from the right class
  if (!bt_machine_check_type (self)) {
    goto Error;
  }

  GST_DEBUG_OBJECT (self, "machine: %" G_OBJECT_REF_COUNT_FMT,
      G_OBJECT_LOG_REF_COUNT (self));

  // register params
  bt_machine_init_prefs_params (self);
  bt_machine_init_global_params (self);
  bt_machine_init_voice_params (self);

  GST_DEBUG_OBJECT (self, "machine: %" G_OBJECT_REF_COUNT_FMT,
      G_OBJECT_LOG_REF_COUNT (self));

  // post sanity checks
  GST_INFO ("  added machine to bin, %" G_OBJECT_REF_COUNT_FMT,
      G_OBJECT_LOG_REF_COUNT (self->priv->machines[PART_MACHINE]));
  g_assert (self->priv->machines[PART_MACHINE] != NULL);
  if (!(self->priv->global_params + self->priv->voice_params)) {
    GST_WARNING_OBJECT (self, "  machine %s has no params", self->priv->id);
  }
  // prepare common internal patterns for the machine
  g_object_unref (bt_cmd_pattern_new (self->priv->song, self,
          BT_PATTERN_CMD_BREAK));
  g_object_unref (bt_cmd_pattern_new (self->priv->song, self,
          BT_PATTERN_CMD_MUTE));

  GST_INFO_OBJECT (self, "construction done: %" G_OBJECT_REF_COUNT_FMT,
      G_OBJECT_LOG_REF_COUNT (self));
  return;
Error:
  GST_WARNING_OBJECT (self, "failed to create machine: %s",
      self->priv->plugin_name);
  if (self->priv->constrution_error) {
    g_set_error (self->priv->constrution_error, error_domain,   /* errorcode= */
        0, "failed to setup the machine.");
  }
}

static void
bt_machine_get_property (GObject * const object, const guint property_id,
    GValue * const value, GParamSpec * const pspec)
{
  const BtMachine *const self = BT_MACHINE (object);

  return_if_disposed ();
  switch (property_id) {
    case MACHINE_CONSTRUCTION_ERROR:
      g_value_set_pointer (value, self->priv->constrution_error);
      break;
    case MACHINE_PROPERTIES:
      g_value_set_pointer (value, self->priv->properties);
      break;
    case MACHINE_SONG:
      g_value_set_object (value, self->priv->song);
      break;
    case MACHINE_ID:
      g_value_set_string (value, self->priv->id);
      break;
    case MACHINE_PLUGIN_NAME:
      g_value_set_string (value, self->priv->plugin_name);
      break;
    case MACHINE_VOICES:
      g_value_set_ulong (value, self->priv->voices);
      break;
    case MACHINE_PREFS_PARAMS:
      g_value_set_ulong (value, self->priv->prefs_params);
      break;
    case MACHINE_GLOBAL_PARAMS:
      g_value_set_ulong (value, self->priv->global_params);
      break;
    case MACHINE_VOICE_PARAMS:
      g_value_set_ulong (value, self->priv->voice_params);
      break;
    case MACHINE_MACHINE:
      g_value_set_object (value, self->priv->machines[PART_MACHINE]);
      break;
    case MACHINE_ADDER_CONVERT:
      g_value_set_object (value, self->priv->machines[PART_ADDER_CONVERT]);
      break;
    case MACHINE_INPUT_PRE_LEVEL:
      g_value_set_object (value, self->priv->machines[PART_INPUT_PRE_LEVEL]);
      break;
    case MACHINE_INPUT_GAIN:
      g_value_set_object (value, self->priv->machines[PART_INPUT_GAIN]);
      break;
    case MACHINE_INPUT_POST_LEVEL:
      g_value_set_object (value, self->priv->machines[PART_INPUT_POST_LEVEL]);
      break;
    case MACHINE_OUTPUT_PRE_LEVEL:
      g_value_set_object (value, self->priv->machines[PART_OUTPUT_PRE_LEVEL]);
      break;
    case MACHINE_OUTPUT_GAIN:
      g_value_set_object (value, self->priv->machines[PART_OUTPUT_GAIN]);
      break;
    case MACHINE_OUTPUT_POST_LEVEL:
      g_value_set_object (value, self->priv->machines[PART_OUTPUT_POST_LEVEL]);
      break;
    case MACHINE_PATTERNS:
      if (self->priv->patterns) {
        GList *list = g_list_copy (self->priv->patterns);
        g_list_foreach (list, (GFunc) g_object_ref, NULL);
        g_value_set_pointer (value, list);
      } else {
        g_value_set_pointer (value, NULL);
      }
      break;
    case MACHINE_STATE:
      g_value_set_enum (value, self->priv->state);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
bt_machine_set_property (GObject * const object, const guint property_id,
    const GValue * const value, GParamSpec * const pspec)
{
  const BtMachine *const self = BT_MACHINE (object);

  return_if_disposed ();
  switch (property_id) {
    case MACHINE_CONSTRUCTION_ERROR:
      self->priv->constrution_error = (GError **) g_value_get_pointer (value);
      break;
    case MACHINE_SONG:
      self->priv->song = BT_SONG (g_value_get_object (value));
      g_object_try_weak_ref (self->priv->song);
      GST_DEBUG ("set the song: %p", self->priv->song);
      break;
    case MACHINE_ID:
      g_free (self->priv->id);
      self->priv->id = g_value_dup_string (value);
      GST_INFO_OBJECT (self, "set the id for machine: %s", self->priv->id);
      if (self->priv->machines[PART_MACHINE]) {
        GstObject *parent = gst_object_get_parent (GST_OBJECT (self));
        if (!parent) {
          gst_element_set_name (self, self->priv->id);
        } else {
          gst_object_unref (parent);
        }
      }
      break;
    case MACHINE_PLUGIN_NAME:
      g_free (self->priv->plugin_name);
      self->priv->plugin_name = g_value_dup_string (value);
      GST_DEBUG_OBJECT (self, "set the plugin_name for machine: %s",
          self->priv->plugin_name);
      break;
    case MACHINE_VOICES:{
      const gulong voices = self->priv->voices;
      self->priv->voices = g_value_get_ulong (value);
      if (GSTBT_IS_CHILD_BIN (self->priv->machines[PART_MACHINE])) {
        GST_DEBUG_OBJECT (self, "set the voices for machine: %lu -> %lu",
            voices, self->priv->voices);
        bt_machine_resize_voices (self, voices);
      } else if (self->priv->voices > 1) {
        GST_WARNING_OBJECT (self,
            "ignoring change in voices  %lu -> %lu for monophonic machine",
            voices, self->priv->voices);
      }
      break;
    }
    case MACHINE_STATE:
      if (bt_machine_change_state (self, g_value_get_enum (value))) {
        GST_DEBUG_OBJECT (self, "set the state for machine: %d",
            self->priv->state);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
bt_machine_dispose (GObject * const object)
{
  const BtMachine *const self = BT_MACHINE (object);
  BtSettings *settings = bt_settings_make ();
  const gulong voices = self->priv->voices;
  gulong i;

  return_if_disposed ();
  self->priv->dispose_has_run = TRUE;

  GST_DEBUG_OBJECT (self, "!!!! self=%p,%s, song=%p", self, self->priv->id,
      self->priv->song);

  // shut down interaction control setup
  g_hash_table_destroy (self->priv->control_data);

  // disconnect notify handlers
  if (self->priv->song) {
    BtSequence *sequence;
    BtSongInfo *song_info;
    g_object_get ((gpointer) (self->priv->song), "sequence", &sequence,
        "song-info", &song_info, NULL);
    if (sequence) {
      GST_DEBUG ("  disconnecting sequence handlers");
      g_signal_handlers_disconnect_matched (sequence,
          G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA, 0, 0, NULL,
          bt_machine_on_duration_changed, (gpointer) self);
      g_object_unref (sequence);
    }
    if (song_info) {
      GST_DEBUG ("  disconnecting song-info handlers");
      g_signal_handlers_disconnect_matched (song_info,
          G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA, 0, 0, NULL,
          bt_machine_on_bpm_changed, (gpointer) self);
      g_signal_handlers_disconnect_matched (song_info,
          G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA, 0, 0, NULL,
          bt_machine_on_tpb_changed, (gpointer) self);
      g_object_unref (song_info);
    }
  }
  g_signal_handlers_disconnect_matched (settings,
      G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA, 0, 0, NULL,
      bt_machine_on_latency_changed, (gpointer) self);
  g_object_unref (settings);

  // unref the pads
  for (i = 0; i < PART_COUNT; i++) {
    if (self->priv->src_pads[i])
      gst_object_unref (self->priv->src_pads[i]);
    if (self->priv->sink_pads[i])
      gst_object_unref (self->priv->sink_pads[i]);
  }

  // gstreamer uses floating references, therefore elements are destroyed, when removed from the bin
  GST_DEBUG ("  releasing song: %p", self->priv->song);
  g_object_try_weak_unref (self->priv->song);

  GST_DEBUG ("  releasing patterns");
  // unref list of patterns
  if (self->priv->patterns) {
    GList *node;
    for (node = self->priv->patterns; node; node = g_list_next (node)) {
      GST_DEBUG ("removing pattern: %" G_OBJECT_REF_COUNT_FMT,
          G_OBJECT_LOG_REF_COUNT (node->data));
      // DEBUG
#ifdef CHECK_PATTERN_OWNERSHIP
      g_object_weak_unref ((GObject *) node->data, check_pattern_ownership,
          (gpointer) self);
#endif
      // DEBUG
      g_object_unref (node->data);
      node->data = NULL;
    }
  }
  // unref param groups
  g_object_try_unref (self->priv->prefs_param_group);
  g_object_try_unref (self->priv->global_param_group);
  if (self->priv->voice_param_groups) {
    for (i = 0; i < voices; i++) {
      g_object_try_unref (self->priv->voice_param_groups[i]);
    }
  }

  GST_DEBUG ("  chaining up");
  G_OBJECT_CLASS (bt_machine_parent_class)->dispose (object);
  GST_DEBUG ("  done");
}

static void
bt_machine_finalize (GObject * const object)
{
  const BtMachine *const self = BT_MACHINE (object);

  GST_DEBUG_OBJECT (self, "!!!! self=%p", self);

  g_hash_table_destroy (self->priv->properties);
  g_free (self->priv->id);
  g_free (self->priv->plugin_name);
  g_free (self->priv->voice_param_groups);

  // free list of patterns
  if (self->priv->patterns) {
    g_list_free (self->priv->patterns);
    self->priv->patterns = NULL;
  }

  if (self->src_wires) {
    g_list_free (self->src_wires);
  }
  if (self->dst_wires) {
    g_list_free (self->dst_wires);
  }

  GST_DEBUG ("  chaining up");
  G_OBJECT_CLASS (bt_machine_parent_class)->finalize (object);
  GST_DEBUG ("  done");
}

//-- class internals

static void
bt_machine_init (BtMachine * self)
{
  self->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (self, BT_TYPE_MACHINE, BtMachinePrivate);
  // default is no voice, only global params
  //self->priv->voices=1;
  self->priv->properties =
      g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

  self->priv->control_data =
      g_hash_table_new_full (NULL, NULL, NULL,
      (GDestroyNotify) free_control_data);

  GST_DEBUG ("!!!! self=%p", self);
}

static void
bt_machine_class_init (BtMachineClass * const klass)
{
  GObjectClass *const gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *const gstelement_class = GST_ELEMENT_CLASS (klass);

  error_domain = g_type_qname (BT_TYPE_MACHINE);
  g_type_class_add_private (klass, sizeof (BtMachinePrivate));

  bt_machine_machine = g_quark_from_static_string ("BtMachine::machine");
  bt_machine_property_name =
      g_quark_from_static_string ("BtMachine::property-name");
  tee_last_seq_num = g_quark_from_static_string ("GstTee::last_seq_num");

  gobject_class->constructed = bt_machine_constructed;
  gobject_class->set_property = bt_machine_set_property;
  gobject_class->get_property = bt_machine_get_property;
  gobject_class->dispose = bt_machine_dispose;
  gobject_class->finalize = bt_machine_finalize;

  // disable deep notify
  {
    GObjectClass *parent_class = g_type_class_peek_static (G_TYPE_OBJECT);
    gobject_class->dispatch_properties_changed =
        parent_class->dispatch_properties_changed;
  }

  gstelement_class->request_new_pad = bt_machine_request_new_pad;
  gstelement_class->release_pad = bt_machine_release_pad;

  /**
   * BtMachine::pattern-added:
   * @self: the machine object that emitted the signal
   * @pattern: the new pattern
   *
   * A new pattern item has been added to the machine
   */
  signals[PATTERN_ADDED_EVENT] = g_signal_new ("pattern-added", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS, 0, NULL,        // accumulator
      NULL,                     // acc data
      g_cclosure_marshal_VOID__OBJECT, G_TYPE_NONE,     // return type
      1,                        // n_params
      BT_TYPE_PATTERN           // param data
      );

  /**
   * BtMachine::pattern-removed:
   * @self: the machine object that emitted the signal
   * @pattern: the old pattern
   *
   * A pattern item has been removed from the machine
   */
  signals[PATTERN_REMOVED_EVENT] = g_signal_new ("pattern-removed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS, 0, NULL,    // accumulator
      NULL,                     // acc data
      g_cclosure_marshal_VOID__OBJECT, G_TYPE_NONE,     // return type
      1,                        // n_params
      BT_TYPE_PATTERN           // param data
      );

  g_object_class_install_property (gobject_class, MACHINE_CONSTRUCTION_ERROR,
      g_param_spec_pointer ("construction-error",
          "construction error prop",
          "signal failed instance creation",
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, MACHINE_PROPERTIES,
      g_param_spec_pointer ("properties",
          "properties prop",
          "hashtable of machine properties",
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, MACHINE_SONG,
      g_param_spec_object ("song", "song contruct prop",
          "song object, the machine belongs to", BT_TYPE_SONG,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, MACHINE_ID,
      g_param_spec_string ("id", "id contruct prop", "machine identifier",
          "unamed machine",
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, MACHINE_PLUGIN_NAME,
      g_param_spec_string ("plugin-name", "plugin-name construct prop",
          "the name of the gst plugin for the machine", "unamed machine",
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, MACHINE_VOICES,
      g_param_spec_ulong ("voices",
          "voices prop",
          "number of voices in the machine",
          0,
          G_MAXULONG,
          0, G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, MACHINE_PREFS_PARAMS,
      g_param_spec_ulong ("prefs-params",
          "property-params prop",
          "number of static params for the machine",
          0, G_MAXULONG, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, MACHINE_GLOBAL_PARAMS,
      g_param_spec_ulong ("global-params",
          "global-params prop",
          "number of dynamic params for the machine",
          0, G_MAXULONG, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, MACHINE_VOICE_PARAMS,
      g_param_spec_ulong ("voice-params",
          "voice-params prop",
          "number of dynamic params for each machine voice",
          0, G_MAXULONG, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, MACHINE_MACHINE,
      g_param_spec_object ("machine", "machine element prop",
          "the machine element, if any", GST_TYPE_ELEMENT,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, MACHINE_ADDER_CONVERT,
      g_param_spec_object ("adder-convert", "adder-convert prop",
          "the after mixing format converter element, if any", GST_TYPE_ELEMENT,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, MACHINE_INPUT_PRE_LEVEL,
      g_param_spec_object ("input-pre-level", "input-pre-level prop",
          "the pre-gain input-level element, if any", GST_TYPE_ELEMENT,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, MACHINE_INPUT_GAIN,
      g_param_spec_object ("input-gain", "input-gain prop",
          "the input-gain element, if any", GST_TYPE_ELEMENT,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, MACHINE_INPUT_POST_LEVEL,
      g_param_spec_object ("input-post-level", "input-post-level prop",
          "the post-gain input-level element, if any", GST_TYPE_ELEMENT,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, MACHINE_OUTPUT_PRE_LEVEL,
      g_param_spec_object ("output-pre-level", "output-pre-level prop",
          "the pre-gain output-level element, if any", GST_TYPE_ELEMENT,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, MACHINE_OUTPUT_GAIN,
      g_param_spec_object ("output-gain", "output-gain prop",
          "the output-gain element, if any", GST_TYPE_ELEMENT,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, MACHINE_OUTPUT_POST_LEVEL,
      g_param_spec_object ("output-post-level", "output-post-level prop",
          "the post-gain output-level element, if any", GST_TYPE_ELEMENT,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, MACHINE_PATTERNS,
      g_param_spec_pointer ("patterns",
          "pattern list prop",
          "a copy of the list of patterns",
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, MACHINE_STATE, g_param_spec_enum ("state", "state prop", "the current state of this machine", BT_TYPE_MACHINE_STATE,  /* enum type */
          BT_MACHINE_STATE_NORMAL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}
