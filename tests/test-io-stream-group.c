/*
 * test-io-stream-group.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2012, Igalia S.L.
 *
 * Authors:
 *   Eduardo Lima Mitev <elima@igalia.com>
 */

#include <evd.h>

#define LISTEN_PORT 54321

typedef struct
{
  EvdIoStreamGroup *group0;
  EvdIoStreamGroup *group1;
  EvdSocket *socket0;
  EvdSocket *socket1;
  GMainLoop *main_loop;

  guint count_group_changes;
  EvdIoStreamGroup *expected_old_group;
  EvdIoStreamGroup *expected_new_group;
} Fixture;

static void
fixture_setup (Fixture *f, gconstpointer test_data)
{
  f->group0 = evd_io_stream_group_new ();
  f->group1 = evd_io_stream_group_new ();
  f->socket0 = evd_socket_new ();
  f->socket1 = evd_socket_new ();

  f->count_group_changes = 0;

  f->expected_old_group = NULL;
  f->expected_new_group = NULL;

  f->main_loop = g_main_loop_new (NULL, FALSE);
}

static void
fixture_teardown (Fixture *f, gconstpointer test_data)
{
  g_object_unref (f->group0);
  f->group0 = NULL;

  g_object_unref (f->group1);
  f->group1 = NULL;

  g_object_unref (f->socket0);
  f->socket0 = NULL;

  g_object_unref (f->socket1);
  f->socket1 = NULL;

  g_main_loop_unref (f->main_loop);
  f->main_loop = NULL;
}

static void
check_io_stream_is_in_group (EvdIoStream *io_stream, EvdIoStreamGroup *group)
{
  EvdIoStreamGroup *_group;

  g_assert (evd_io_stream_get_group (io_stream) == group);
  g_object_get (io_stream, "group", &_group, NULL);
  g_assert (_group == group);

  if (_group != NULL)
    g_object_unref (_group);
}

static void
connection_on_group_changed (EvdIoStream      *io_stream,
                             EvdIoStreamGroup *new_group,
                             EvdIoStreamGroup *old_group,
                             gpointer          user_data)
{
  Fixture *f = user_data;

  g_assert (EVD_IS_IO_STREAM (io_stream));
  g_assert (new_group == f->expected_new_group);
  g_assert (old_group == f->expected_old_group);
}

static void
socket_on_new_connection (EvdSocket     *socket,
                          EvdConnection *conn,
                          gpointer       user_data)
{
  Fixture *f = user_data;
  EvdIoStreamGroup *group;

  g_assert (conn != NULL);
  g_assert (EVD_IS_CONNECTION (conn));
  g_assert (EVD_IS_IO_STREAM (conn));
  g_assert (G_IS_IO_STREAM (conn));

  g_signal_connect (conn,
                    "group-changed",
                    G_CALLBACK (connection_on_group_changed),
                    f);

  /* here starts the actual assertions for this test */

  /* initially, the connection has no group */
  check_io_stream_is_in_group (EVD_IO_STREAM (conn), NULL);

  /* put connection into one group using method */
  f->expected_old_group = NULL;
  f->expected_new_group = f->group0;
  evd_io_stream_set_group (EVD_IO_STREAM (conn), f->group0);
  check_io_stream_is_in_group (EVD_IO_STREAM (conn), f->group0);

  /* remove connection from group using method */
  f->expected_new_group = NULL;
  f->expected_old_group = f->group0;
  evd_io_stream_set_group (EVD_IO_STREAM (conn), NULL);
  check_io_stream_is_in_group (EVD_IO_STREAM (conn), NULL);

  /* put connection into another group using property */
  f->expected_old_group = NULL;
  f->expected_new_group = f->group1;
  g_object_set (conn, "group", f->group1, NULL);
  check_io_stream_is_in_group (EVD_IO_STREAM (conn), f->group1);

  /* remove connection from group using property */
  f->expected_new_group = NULL;
  f->expected_old_group = f->group1;
  g_object_set (conn, "group", NULL, NULL);
  check_io_stream_is_in_group (EVD_IO_STREAM (conn), NULL);

  /* put connection into another group using API from group */
  f->expected_old_group = NULL;
  f->expected_new_group = f->group0;
  evd_io_stream_group_add (f->group0, G_IO_STREAM (conn));
  check_io_stream_is_in_group (EVD_IO_STREAM (conn), f->group0);

  /* remove connection from group using API from group */
  f->expected_new_group = NULL;
  f->expected_old_group = f->group0;
  evd_io_stream_group_remove (f->group0, G_IO_STREAM (conn));
  check_io_stream_is_in_group (EVD_IO_STREAM (conn), NULL);

  g_main_loop_quit (f->main_loop);
}

static void
test_func (Fixture *f, gconstpointer test_data)
{
  gchar *addr;

  addr = g_strdup_printf ("0.0.0.0:%d", LISTEN_PORT);

  evd_socket_listen (f->socket0,
                     addr,
                     NULL,
                     NULL,
                     f);
  g_signal_connect (f->socket0,
                    "new-connection",
                    G_CALLBACK (socket_on_new_connection),
                    f);

  evd_socket_connect_to (f->socket1,
                         addr,
                         NULL,
                         NULL,
                         NULL);

  g_free (addr);

  g_main_loop_run (f->main_loop);
}

gint
main (gint argc, gchar *argv[])
{
  g_type_init ();
  g_test_init (&argc, &argv, NULL);

  g_test_add ("/evd/io-stream-group/all",
              Fixture,
              NULL,
              fixture_setup,
              test_func,
              fixture_teardown);

  return g_test_run ();
}
