/* Buzztard
 * Copyright (C) 2012 Buzztard team <buzztard-devel@lists.sf.net>
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

#include "m-bt-core.h"

//-- globals

static BtApplication *app;
static BtSong *song;
static BtSetup *setup;
static BtSettings *settings;
static gfloat minv, maxv;

static gchar *media_types[] = {
  "application/ogg",
  "application/x-id3",
  "audio/x-wav",
  "application/ogg",
  "video/quicktime",
  NULL
};

//-- fixtures

static void
case_setup (void)
{
  BT_CASE_START;
}

static void
test_setup (void)
{
  app = bt_test_application_new ();
  settings = bt_settings_make ();
  song = bt_song_new (app);
  setup = BT_SETUP (check_gobject_get_object_property (song, "setup"));
  minv = G_MAXFLOAT;
  maxv = -G_MAXFLOAT;
}

static void
test_teardown (void)
{
  g_object_unref (setup);
  g_object_checked_unref (song);
  g_object_unref (settings);
  g_object_checked_unref (app);
}

static void
case_teardown (void)
{
}

//-- helper

static void
make_new_song (gint wave)
{
  BtSequence *sequence =
      (BtSequence *) check_gobject_get_object_property (song, "sequence");
  BtMachine *sink = BT_MACHINE (bt_sink_machine_new (song, "master", NULL));
  BtMachine *gen =
      BT_MACHINE (bt_source_machine_new (song, "gen", "audiotestsrc", 0L,
          NULL));
  BtParameterGroup *pg = bt_machine_get_global_param_group (gen);
  bt_wire_new (song, gen, sink, NULL);
  BtPattern *pattern =
      bt_pattern_new (song, "pattern-id", "pattern-name", 8L, BT_MACHINE (gen));
  GstElement *element =
      (GstElement *) check_gobject_get_object_property (gen, "machine");

  g_object_set (sequence, "length", 8L, "loop", FALSE, NULL);
  bt_sequence_add_track (sequence, gen, -1);
  bt_sequence_set_pattern (sequence, 0, 0, (BtCmdPattern *) pattern);
  g_object_set (element, "wave", wave, "volume", 1.0, NULL);
  bt_parameter_group_set_param_default (pg,
      bt_parameter_group_get_param_index (pg, "wave"));
  bt_parameter_group_set_param_default (pg,
      bt_parameter_group_get_param_index (pg, "volume"));

  gst_object_unref (element);
  g_object_unref (pattern);
  g_object_unref (sequence);
  GST_INFO ("  song created");
}

static GstElement *
get_sink_bin (void)
{
  BtMachine *machine =
      bt_setup_get_machine_by_type (setup, BT_TYPE_SINK_MACHINE);
  GstElement *sink_bin =
      GST_ELEMENT (check_gobject_get_object_property (machine, "machine"));

  g_object_try_unref (machine);

  return sink_bin;
}

static GstElement *
get_sink_element (GstBin * bin)
{
  GstElement *e;
  GList *node;

  GST_INFO_OBJECT (bin, "looking for audio_sink");
  for (node = GST_BIN_CHILDREN (bin); node; node = g_list_next (node)) {
    e = (GstElement *) node->data;
    if (GST_IS_BIN (e) && GST_OBJECT_FLAG_IS_SET (e, GST_ELEMENT_IS_SINK)) {
      return get_sink_element ((GstBin *) e);
    }
    if (GST_OBJECT_FLAG_IS_SET (e, GST_ELEMENT_IS_SINK)) {
      return e;
    }
  }
  return NULL;
}

static gchar *
make_tmp_song_path (const gchar * name, const gchar * ext)
{
  gchar *song_name = g_strconcat (name, ext, NULL);
  gchar *song_path = g_build_filename (g_get_tmp_dir (), song_name, NULL);
  g_free (song_name);
  GST_INFO ("testing '%s'", song_path);
  return song_path;
}

static void
handoff_buffer_cb (GstElement * fakesink, GstBuffer * buffer, GstPad * pad,
    gpointer user_data)
{
  gint i, num_samples;
  gfloat *data = (gfloat *) GST_BUFFER_DATA (buffer);
  GST_DEBUG_OBJECT (fakesink, "got buffer %p, caps=%" GST_PTR_FORMAT, buffer,
      GST_BUFFER_CAPS (buffer));
  num_samples = GST_BUFFER_SIZE (buffer) / sizeof (gfloat);
  for (i = 0; i < num_samples; i++) {
    if (data[i] < minv)
      minv = data[i];
    else if (data[i] > maxv)
      maxv = data[i];
  }
}

static gchar *typefind_media_type = NULL;
static GMainLoop *typefind_main_loop = NULL;

static void
have_type_cb (GstElement * typefind, guint probability, GstCaps * caps,
    gpointer user_data)
{
  GST_DEBUG ("have type with %u, %" GST_PTR_FORMAT, probability, caps);
  typefind_media_type =
      g_strdup (gst_structure_get_name (gst_caps_get_structure (caps, 0)));
  g_main_loop_quit (typefind_main_loop);
}

static void
typefind_message_received (GstBus * bus, GstMessage * message,
    gpointer user_data)
{
  if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ERROR) {
    GST_WARNING_OBJECT (GST_MESSAGE_SRC (message),
        "error for '%s': %" GST_PTR_FORMAT, user_data, message);
  } else {
    GST_DEBUG_OBJECT (GST_MESSAGE_SRC (message),
        "eos for '%s': %" GST_PTR_FORMAT, user_data, message);
  }
  g_main_loop_quit (typefind_main_loop);
}

static gchar *
get_media_type (const gchar * filename)
{
  GstElement *pipeline = gst_pipeline_new ("typefind");
  GstElement *filesrc = gst_element_factory_make ("filesrc", NULL);
  GstElement *typefind = gst_element_factory_make ("typefind", NULL);
  GstElement *fakesink = gst_element_factory_make ("fakesink", NULL);

  gst_bin_add_many (GST_BIN (pipeline), filesrc, typefind, fakesink, NULL);
  gst_element_link_many (filesrc, typefind, fakesink, NULL);

  g_object_set (filesrc, "location", filename, NULL);
  g_signal_connect (typefind, "have-type", (GCallback) have_type_cb, NULL);

  GstBus *bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_signal_watch_full (bus, G_PRIORITY_HIGH);
  g_signal_connect (bus, "message::error",
      G_CALLBACK (typefind_message_received), (gpointer) filename);
  g_signal_connect (bus, "message::eos", G_CALLBACK (typefind_message_received),
      (gpointer) filename);
  gst_object_unref (bus);

  typefind_media_type = NULL;
  typefind_main_loop = g_main_loop_new (NULL, FALSE);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  // run main loop until EOS or type found
  g_main_loop_run (typefind_main_loop);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  g_main_loop_unref (typefind_main_loop);
  gst_object_unref (pipeline);
  return typefind_media_type;
}

static void
message_received (GstBus * bus, GstMessage * message, gpointer user_data)
{
  if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ERROR) {
    GST_WARNING_OBJECT (GST_MESSAGE_SRC (message), "error: %" GST_PTR_FORMAT,
        message);
  } else {
    GST_DEBUG_OBJECT (GST_MESSAGE_SRC (message), "eos: %" GST_PTR_FORMAT,
        message);
  }
  g_main_loop_quit (user_data);
}

static void
run_main_loop_until_eos (void)
{
  GstElement *bin =
      (GstElement *) check_gobject_get_object_property (song, "bin");
  GMainLoop *main_loop = g_main_loop_new (NULL, FALSE);
  GstBus *bus = gst_element_get_bus (bin);
  gst_bus_add_signal_watch_full (bus, G_PRIORITY_HIGH);
  g_signal_connect (bus, "message::error", G_CALLBACK (message_received),
      (gpointer) main_loop);
  g_signal_connect (bus, "message::eos", G_CALLBACK (message_received),
      (gpointer) main_loop);
  gst_object_unref (bus);
  gst_object_unref (bin);

  g_main_loop_run (main_loop);
}

//-- tests

static void
test_bt_sink_bin_new (BT_TEST_ARGS)
{
  BT_TEST_START;
  /* arrange */

  /* act */
  GstElement *bin = gst_element_factory_make ("bt-sink-bin", NULL);

  /* assert */
  fail_unless (bin != NULL, NULL);

  /* cleanup */
  gst_object_unref (bin);
  BT_TEST_END;
}

/* test playback (we test this elsewhere too) */
/* test recording (loop test over BtSinkBinRecordFormat */
static void
test_bt_sink_bin_record (BT_TEST_ARGS)
{
  BT_TEST_START;
  /* arrange */
  make_new_song ( /*silence */ 4);
  GstElement *sink_bin = get_sink_bin ();
  GEnumClass *enum_class =
      g_type_class_peek_static (BT_TYPE_SINK_BIN_RECORD_FORMAT);
  GEnumValue *enum_value = g_enum_get_value (enum_class, _i);
  gchar *filename = make_tmp_song_path ("record", enum_value->value_name);
  g_object_set (sink_bin,
      "mode", BT_SINK_BIN_MODE_RECORD,
      "record-format", _i, "record-file-name", filename, NULL);

  /* act */
  GST_INFO ("act: == %s ==", filename);
  bt_song_play (song);
  run_main_loop_until_eos ();
  bt_song_stop (song);

  /* assert */
  GST_INFO ("assert: == %s ==", filename);
  fail_unless (g_file_test (filename, G_FILE_TEST_IS_REGULAR));
  ck_assert_str_eq_and_free (get_media_type (filename), media_types[_i]);

  /* cleanup */
  g_free (filename);
  gst_object_unref (sink_bin);
  BT_TEST_END;
}

/* test playback + recording, same as above */
static void
test_bt_sink_bin_record_and_play (BT_TEST_ARGS)
{
  BT_TEST_START;
  /* arrange */
  make_new_song ( /*silence */ 4);
  GstElement *sink_bin = get_sink_bin ();
  GEnumClass *enum_class =
      g_type_class_peek_static (BT_TYPE_SINK_BIN_RECORD_FORMAT);
  GEnumValue *enum_value = g_enum_get_value (enum_class, _i);
  gchar *filename = make_tmp_song_path ("record", enum_value->value_name);
  g_object_set (sink_bin,
      "mode", BT_SINK_BIN_MODE_PLAY_AND_RECORD,
      "record-format", _i, "record-file-name", filename, NULL);

  /* act */
  GST_INFO ("act: == %s ==", filename);
  bt_song_play (song);
  run_main_loop_until_eos ();
  bt_song_stop (song);

  /* assert */
  GST_INFO ("assert: == %s ==", filename);
  fail_unless (g_file_test (filename, G_FILE_TEST_IS_REGULAR));
  ck_assert_str_eq_and_free (get_media_type (filename), media_types[_i]);

  /* cleanup */
  g_free (filename);
  gst_object_unref (sink_bin);
  BT_TEST_END;
}

/* test master volume, using appsink? */
static void
test_bt_sink_bin_master_volume (BT_TEST_ARGS)
{
  BT_TEST_START;
  /* arrange */
  gdouble volume = 1.0 / (gdouble) _i;
  g_object_set (settings, "audiosink", "fakesink", NULL);
  make_new_song ( /*square */ 1);
  BtMachine *machine =
      bt_setup_get_machine_by_type (setup, BT_TYPE_SINK_MACHINE);
  GstElement *sink_bin =
      GST_ELEMENT (check_gobject_get_object_property (machine, "machine"));
  BtParameterGroup *pg = bt_machine_get_global_param_group (machine);
  gst_element_set_state (sink_bin, GST_STATE_READY);
  GstElement *fakesink = get_sink_element ((GstBin *) sink_bin);
  g_object_set (fakesink, "signal-handoffs", TRUE, NULL);
  g_signal_connect (fakesink, "handoff", (GCallback) handoff_buffer_cb, NULL);

  /* act */
  g_object_set (sink_bin, "master-volume", volume, NULL);
  bt_parameter_group_set_param_default (pg,
      bt_parameter_group_get_param_index (pg, "master-volume"));
  bt_song_play (song);
  check_run_main_loop_for_usec (G_USEC_PER_SEC / 10);

  /* assert */
  GST_INFO ("minv=%7.4lf, maxv=%7.4lf", minv, maxv);
  ck_assert_float_eq (maxv, +volume);
  ck_assert_float_eq (minv, -volume);

  /* cleanup */
  bt_song_stop (song);
  gst_object_unref (sink_bin);
  g_object_try_unref (machine);
  BT_TEST_END;
}

/* insert analyzers */
static void
test_bt_sink_bin_analyzers (BT_TEST_ARGS)
{
  BT_TEST_START;
  /* arrange */
  make_new_song ( /*square */ 1);
  GstElement *sink_bin = get_sink_bin ();
  GstElement *fakesink = gst_element_factory_make ("fakesink", NULL);
  GstElement *queue = gst_element_factory_make ("queue", NULL);
  GList *analyzers_list =
      g_list_prepend (g_list_prepend (NULL, fakesink), queue);
  g_object_set (sink_bin, "analyzers", analyzers_list, NULL);
  g_object_set (fakesink, "signal-handoffs", TRUE, NULL);
  g_signal_connect (fakesink, "handoff", (GCallback) handoff_buffer_cb, NULL);

  /* act */
  bt_song_play (song);
  check_run_main_loop_for_usec (G_USEC_PER_SEC / 10);
  bt_song_write_to_lowlevel_dot_file (song);

  /* assert */
  GST_INFO ("minv=%7.4lf, maxv=%7.4lf", minv, maxv);
  ck_assert_float_eq (maxv, +1.0);
  ck_assert_float_eq (minv, -1.0);

  /* cleanup */
  bt_song_stop (song);
  gst_object_unref (sink_bin);
  BT_TEST_END;
}

TCase *
bt_sink_bin_example_case (void)
{
  TCase *tc = tcase_create ("BtSinkBinExamples");

  tcase_add_test (tc, test_bt_sink_bin_new);
  tcase_add_loop_test (tc, test_bt_sink_bin_record, 0,
      BT_SINK_BIN_RECORD_FORMAT_COUNT);
  tcase_add_loop_test (tc, test_bt_sink_bin_record_and_play, 0,
      BT_SINK_BIN_RECORD_FORMAT_COUNT);
  tcase_add_loop_test (tc, test_bt_sink_bin_master_volume, 1, 3);
  tcase_add_test (tc, test_bt_sink_bin_analyzers);
  tcase_add_checked_fixture (tc, test_setup, test_teardown);
  tcase_add_unchecked_fixture (tc, case_setup, case_teardown);
  return (tc);
}
