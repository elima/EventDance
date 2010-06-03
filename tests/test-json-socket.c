/*
 * test-json-socket.c
 *
 * EventDance project - An event distribution framework (http://eventdance.org)
 *
 * Copyright (C) 2009, Igalia S.L.
 *
 * Authors:
 *   Eduardo Lima Mitev <elima@igalia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 3 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <string.h>

#include <evd.h>
#include <evd-socket-manager.h>

static const gchar *evd_json_socket_chunks[] =
{
  " \ \  [\"hell",
  "o world!\"\, 1, 4\, fal",
  "se,    456, 4,   ",
  "null]      {\"foo\":1234} ",
  "[\"this should throw an error\"}"
};

static const gchar *evd_json_socket_packets[] =
{
  "[\"hello world!\"\, 1, 4\, false,    456, 4,   null]",
  "{\"foo\":1234}"
};

typedef struct
{
  GMainLoop *main_loop;
  EvdJsonSocket *socket;
  EvdJsonSocket *socket1;
  EvdJsonSocket *socket2;
  GSocketAddress *socket_addr;

  gint break_src_id;

  gint packet_index;
  gint total_packets;

  gboolean completed;
} EvdJsonSocketFixture;

static void
evd_json_socket_fixture_setup (EvdJsonSocketFixture *f,
                               gconstpointer         test_data)
{
  gint port;
  GInetAddress *inet_addr;

  f->main_loop = g_main_loop_new (NULL, FALSE);
  f->break_src_id = 0;

  f->socket = evd_json_socket_new ();
  f->socket1 = evd_json_socket_new ();
  f->socket2 = NULL;

  inet_addr = g_inet_address_new_from_string ("127.0.0.1");
  port = g_random_int_range (1024, 0xFFFF-1);
  f->socket_addr = g_inet_socket_address_new (inet_addr, 5453);
  g_object_unref (inet_addr);

  f->packet_index = 0;
  f->total_packets = 0;

  f->completed = FALSE;
}

static gboolean
evd_json_socket_test_break (gpointer user_data)
{
  EvdJsonSocketFixture *f = (EvdJsonSocketFixture *) user_data;

  if (f->main_loop != NULL)
    {
      if (f->break_src_id != 0)
        g_source_remove (f->break_src_id);

      g_main_context_wakeup (g_main_loop_get_context (f->main_loop));
      g_main_loop_quit (f->main_loop);
      g_main_loop_unref (f->main_loop);
      f->main_loop = NULL;
    }

  return FALSE;
}

static void
evd_json_socket_fixture_teardown (EvdJsonSocketFixture *f,
                                  gconstpointer         test_data)
{
  evd_json_socket_test_break ((gpointer) f);

  g_object_unref (f->socket);
  g_object_unref (f->socket1);

  if (f->socket2 != NULL)
    g_object_unref (f->socket2);

  if (f->socket_addr != NULL)
    g_object_unref (f->socket_addr);

  g_assert (evd_socket_manager_get () == NULL);
}

static void
evd_json_socket_test_on_error (EvdSocket *self,
                               gint       code,
                               gchar     *message,
                               gpointer   user_data)
{
  g_assert_cmpint (code, ==, EVD_ERROR_INVALID_DATA);
}

static void
evd_json_socket_test_on_close (EvdSocket *self, gpointer user_data)
{
  EvdJsonSocketFixture *f = (EvdJsonSocketFixture *) user_data;

  f->completed = TRUE;

  evd_json_socket_test_break (user_data);
}

static void
evd_json_socket_test_on_packet (EvdJsonSocket *self,
                                const gchar   *buffer,
                                gsize          size,
                                gpointer       user_data)
{
  EvdJsonSocketFixture *f = (EvdJsonSocketFixture *) user_data;

  g_assert_cmpint (g_strcmp0 (buffer,
                              evd_json_socket_packets[f->packet_index]),
                   ==,
                   0);
  f->packet_index ++;

  if (f->packet_index == sizeof (evd_json_socket_packets) / sizeof (gchar *))
    {
      GError *error = NULL;

      g_assert (evd_socket_close (EVD_SOCKET (self), &error));
      g_assert_no_error (error);
    }
}

static void
evd_json_socket_test_on_read (EvdSocket *self,
                              gpointer   user_data)
{
  g_assert_not_reached ();
}

static void
evd_json_socket_test_on_write (EvdSocket *self, gpointer user_data)
{
  GError *error = NULL;
  gint i;

  for (i=0; i<sizeof (evd_json_socket_chunks)/sizeof (gchar *); i++)
    {
      evd_socket_write (EVD_SOCKET (self),
                        evd_json_socket_chunks[i],
                        strlen (evd_json_socket_chunks[i]),
                        &error);
      g_assert_no_error (error);
    }
}

static void
evd_json_socket_test_on_new_conn (EvdJsonSocket *self,
                                  EvdJsonSocket *client,
                                  gpointer       user_data)
{
  EvdJsonSocketFixture *f = (EvdJsonSocketFixture *) user_data;

  g_assert (EVD_IS_JSON_SOCKET (self));
  g_assert (EVD_IS_JSON_SOCKET (client));

  g_signal_connect (client,
                    "error",
                    G_CALLBACK (evd_json_socket_test_on_error),
                    (gpointer) f);

  evd_socket_base_set_write_handler (EVD_SOCKET_BASE (client),
                                     G_CALLBACK (evd_json_socket_test_on_write),
                                     f);
  g_assert (evd_socket_base_get_on_write (EVD_SOCKET_BASE (client)) != NULL);

  evd_json_socket_set_packet_handler (client,
                                      evd_json_socket_test_on_packet,
                                      f);

  f->socket2 = client;
  g_object_ref (f->socket2);
}

static void
evd_json_socket_test_on_state_changed (EvdSocket      *self,
                                       EvdSocketState  new_state,
                                       EvdSocketState  old_state,
                                       gpointer        user_data)
{
  EvdJsonSocketFixture *f = (EvdJsonSocketFixture *) user_data;
  GError *error = NULL;

  if (new_state == EVD_SOCKET_STATE_LISTENING)
    {
      /* connect */
      evd_socket_connect_addr (EVD_SOCKET (f->socket1), f->socket_addr, &error);
      g_assert_no_error (error);
    }
}

static gboolean
evd_json_socket_launch_test (gpointer user_data)
{
  EvdJsonSocketFixture *f = (EvdJsonSocketFixture *) user_data;
  GError *error = NULL;

  g_signal_connect (f->socket,
                    "error",
                    G_CALLBACK (evd_json_socket_test_on_error),
                    (gpointer) f);
  g_signal_connect (f->socket,
                    "state-changed",
                    G_CALLBACK (evd_json_socket_test_on_state_changed),
                    (gpointer) f);
  g_signal_connect (f->socket,
                    "new-connection",
                    G_CALLBACK (evd_json_socket_test_on_new_conn),
                    (gpointer) f);

  g_signal_connect (f->socket1,
                    "error",
                    G_CALLBACK (evd_json_socket_test_on_error),
                    (gpointer) f);

  g_signal_connect (f->socket1,
                    "close",
                    G_CALLBACK (evd_json_socket_test_on_close),
                    (gpointer) f);

  evd_socket_base_set_read_handler (EVD_SOCKET_BASE (f->socket1),
                                    G_CALLBACK (evd_json_socket_test_on_read),
                                    f);
  g_assert (evd_socket_base_get_on_read (EVD_SOCKET_BASE (f->socket1)) != NULL);

  evd_json_socket_set_packet_handler (f->socket1,
                                      evd_json_socket_test_on_packet,
                                      f);
  g_assert (evd_json_socket_get_on_packet (f->socket1) != NULL);

  /* listen */
  evd_socket_listen (EVD_SOCKET (f->socket), "127.0.0.1:5453", &error);
  g_assert_no_error (error);

  return FALSE;
}

static void
evd_json_socket_test (EvdJsonSocketFixture *f,
                      gconstpointer         test_data)
{
  f->break_src_id = g_timeout_add (1000,
                                   (GSourceFunc) evd_json_socket_test_break,
                                   (gpointer) f);

  g_idle_add ((GSourceFunc) evd_json_socket_launch_test,
              (gpointer) f);

  g_main_loop_run (f->main_loop);

  g_assert_cmpint (f->packet_index,
                   ==,
                   sizeof (evd_json_socket_packets) / sizeof (gchar *));

  g_assert (f->completed);
}

gint
main (gint argc, gchar *argv[])
{
  g_type_init ();
  g_test_init (&argc, &argv, NULL);

  g_test_add ("/evd/json/socket/basic",
              EvdJsonSocketFixture,
              NULL,
              evd_json_socket_fixture_setup,
              evd_json_socket_test,
              evd_json_socket_fixture_teardown);

  g_test_run ();

  return 0;
}
