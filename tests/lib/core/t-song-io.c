/* $Id: t-song-io.c,v 1.15 2006-01-16 21:39:26 ensonic Exp $
 */

#include "m-bt-core.h"

//-- globals

//-- fixtures

static void test_setup(void) {
  bt_core_setup();
  GST_INFO("================================================================================");
}

static void test_teardown(void) {
  bt_core_teardown();
  //puts(__FILE__":teardown");
}

//-- tests

// try to create a SongIO object with NULL pointer
BT_START_TEST(test_btsong_io_obj1) {
  BtSongIO *song_io;
  
  song_io=bt_song_io_new(NULL);
  fail_unless(song_io == NULL, NULL);
}
BT_END_TEST

// try to create a SongIO object with empty string
BT_START_TEST(test_btsong_io_obj2) {
  BtSongIO *song_io;

  song_io=bt_song_io_new("");
  fail_unless(song_io==NULL, NULL);
}
BT_END_TEST

// try to create a SongIO object from song name without extension
BT_START_TEST(test_btsong_io_obj3) {
  BtSongIO *song_io;

  song_io=bt_song_io_new("test");
  fail_unless(song_io==NULL, NULL);
}
BT_END_TEST

// try to create a SongIO object from song name with unknown extension
BT_START_TEST(test_btsong_io_obj4) {
  BtSongIO *song_io;

  song_io=bt_song_io_new("test.unk");
  fail_unless(song_io==NULL, NULL);
}
BT_END_TEST

// try to create a SongIO object with a native file name
// this test is a positiv test
BT_START_TEST(test_btsong_io_obj5) {
  BtSongIO *song_io;
  
  song_io=bt_song_io_new(check_get_test_song_path("example.xml"));
  // check if the type of songIO is native
  fail_unless(BT_IS_SONG_IO_NATIVE(song_io), NULL);
  fail_unless(song_io!=NULL, NULL);

  g_object_checked_unref(song_io);
}
BT_END_TEST


TCase *bt_song_io_test_case(void) {
  TCase *tc = tcase_create("BtSongIOTests");

  tcase_add_test(tc,test_btsong_io_obj1);
  tcase_add_test(tc,test_btsong_io_obj2);
  tcase_add_test(tc,test_btsong_io_obj3);
  tcase_add_test(tc,test_btsong_io_obj4);
  tcase_add_test(tc,test_btsong_io_obj5);
  tcase_add_unchecked_fixture(tc, test_setup, test_teardown);
  return(tc);
}
