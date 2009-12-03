/*
 * test-socket-common.c
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

#ifndef __TEST_SOCKET_COMMON_H__
#define __TEST_SOCKET_COMMON_H__

#include <gio/gio.h>
#include <string.h>

#include <evd.h>
#include <evd-socket-manager.h>

#include <evd-socket-protected.h>

#define EVD_SOCKET_TEST_UNREAD_TEXT "Once upon a time "
#define EVD_SOCKET_TEST_TEXT1       "in a very remote land... "
#define EVD_SOCKET_TEST_TEXT2       "and they lived in joy forever."

typedef struct
{
  GMainLoop *main_loop;
  EvdSocket *socket;
  EvdSocket *socket1;
  EvdSocket *socket2;
  GSocketAddress *socket_addr;

  gint break_src_id;

  gboolean bind;
  gboolean listen;
  gboolean connect;
  gboolean new_conn;

  gsize total_read;
} EvdSocketFixture;

static void
evd_socket_fixture_setup (EvdSocketFixture *fixture)
{
  fixture->main_loop = g_main_loop_new (NULL, FALSE);
  fixture->break_src_id = 0;

  fixture->socket = evd_socket_new ();
  fixture->socket1 = evd_socket_new ();
  fixture->socket2 = NULL;
  fixture->socket_addr = NULL;

  fixture->bind = FALSE;
  fixture->listen = FALSE;
  fixture->connect = FALSE;
  fixture->new_conn = FALSE;

  fixture->total_read = 0;

  g_assert (evd_socket_manager_get () != NULL);
}

static gboolean
evd_socket_test_break (gpointer user_data)
{
  EvdSocketFixture *f = (EvdSocketFixture *) user_data;

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
evd_socket_fixture_teardown (EvdSocketFixture *fixture)
{
  evd_socket_test_break ((gpointer) fixture);

  g_object_unref (fixture->socket);
  g_object_unref (fixture->socket1);
  if (fixture->socket2 != NULL)
    g_object_unref (fixture->socket2);

  if (fixture->socket_addr != NULL)
    g_object_unref (fixture->socket_addr);

  g_assert (evd_socket_manager_get () == NULL);
}

static void
evd_socket_test_config (EvdSocket      *socket,
                        GSocketFamily   family,
                        GSocketType     type,
                        GSocketProtocol protocol)
{
  GSocketFamily _family;
  GSocketType _type;
  GSocketProtocol _protocol;

  g_object_get (socket,
                "family", &_family,
                "protocol", &_protocol,
                "type", &_type,
                NULL);
  g_assert (family == _family);
  g_assert (type == _type);
  g_assert (protocol == _protocol);
}

static void
evd_socket_test_on_error (EvdSocket *self,
                          gint       code,
                          gchar     *message,
                          gpointer   user_data)
{
  g_error (message);
  g_assert_not_reached ();
}

static void
evd_socket_test_on_bound (EvdSocket      *self,
                          GSocketAddress *address,
                          gpointer        user_data)
{
  EvdSocketFixture *f = (EvdSocketFixture *) user_data;

  f->bind = TRUE;

  g_assert (EVD_IS_SOCKET (f->socket));
  g_assert_cmpint (evd_socket_get_status (self), ==, EVD_SOCKET_BOUND);
  g_assert (evd_socket_get_socket (self) != NULL);

  g_assert (G_IS_SOCKET_ADDRESS (address));
  g_assert (address == f->socket_addr);

  evd_socket_test_config (self,
                          g_socket_address_get_family (G_SOCKET_ADDRESS (f->socket_addr)),
                          G_SOCKET_TYPE_STREAM,
                          G_SOCKET_PROTOCOL_DEFAULT);
}

static void
evd_socket_test_on_listen (EvdSocket *self,
                           gpointer   user_data)
{
  EvdSocketFixture *f = (EvdSocketFixture *) user_data;

  f->listen = TRUE;

  g_assert (EVD_IS_SOCKET (self));
  g_assert_cmpint (evd_socket_get_status (self), ==, EVD_SOCKET_LISTENING);
  g_assert (evd_socket_get_socket (self) != NULL);
}

static void
evd_socket_test_on_read (EvdSocket *self, gpointer user_data)
{
  EvdSocketFixture *f = (EvdSocketFixture *) user_data;
  GError *error = NULL;
  gchar buf[1024] = { 0, };
  gssize size;
  gchar *expected_st;
  gssize expected_size;

  g_assert (evd_socket_can_read (self));

  size = evd_socket_read_buffer (self, buf, 1023, &error);
  g_assert_no_error (error);

  if (size == 0)
    return;

  /* validate text read */
  expected_size = strlen (EVD_SOCKET_TEST_UNREAD_TEXT) +
    strlen (EVD_SOCKET_TEST_TEXT1) +
    strlen (EVD_SOCKET_TEST_TEXT2);

  g_assert_cmpint (size, ==, expected_size);

  expected_st = g_strconcat (EVD_SOCKET_TEST_UNREAD_TEXT,
                             EVD_SOCKET_TEST_TEXT1,
                             EVD_SOCKET_TEST_TEXT2,
                             NULL);
  g_assert_cmpstr (buf, ==, expected_st);
  g_free (expected_st);

  /* break mainloop if finished reading */
  f->total_read += size;
  if (f->total_read ==
      (strlen (EVD_SOCKET_TEST_UNREAD_TEXT) +
       strlen (EVD_SOCKET_TEST_TEXT1) +
       strlen (EVD_SOCKET_TEST_TEXT2)) * 2)
    {
      evd_socket_test_break ((gpointer) f);
    }
}

static void
evd_socket_test_on_write (EvdSocket *self, gpointer user_data)
{
  EvdSocketFixture *f = (EvdSocketFixture *) user_data;
  GError *error = NULL;

  g_assert (evd_socket_can_write (self));

  evd_socket_unread (self, EVD_SOCKET_TEST_UNREAD_TEXT, &error);
  g_assert_no_error (error);
  g_assert (evd_socket_can_read (self) == TRUE);

  evd_socket_write_buffer (self,
                           EVD_SOCKET_TEST_TEXT1,
                           strlen (EVD_SOCKET_TEST_TEXT1),
                           &error);
  g_assert_no_error (error);

  evd_socket_write (self,
                    EVD_SOCKET_TEST_TEXT2,
                    &error);
  g_assert_no_error (error);
}

static void
evd_socket_test_on_new_conn (EvdSocket *self,
                             EvdSocket *client,
                             gpointer   user_data)
{
  EvdSocketFixture *f = (EvdSocketFixture *) user_data;

  f->new_conn = TRUE;

  g_assert (EVD_IS_SOCKET (self));
  g_assert (EVD_IS_SOCKET (client));
  g_assert_cmpint (evd_socket_get_status (client), ==, EVD_SOCKET_CONNECTED);
  g_assert (evd_socket_get_socket (client) != NULL);

  evd_socket_set_read_handler (client, evd_socket_test_on_read, f);
  g_assert (evd_stream_get_on_read (EVD_STREAM (client)) != NULL);

  evd_socket_set_write_handler (client, evd_socket_test_on_write, f);
  g_assert (evd_stream_get_on_write (EVD_STREAM (client)) != NULL);

  f->socket2 = client;
  g_object_ref (f->socket2);
}

static void
evd_socket_test_on_connect (EvdSocket *self,
                           gpointer   user_data)
{
  EvdSocketFixture *f = (EvdSocketFixture *) user_data;

  f->connect = TRUE;

  g_assert (EVD_IS_SOCKET (self));
  g_assert_cmpint (evd_socket_get_status (self), ==, EVD_SOCKET_CONNECTED);
  g_assert (evd_socket_get_socket (self) != NULL);
}

static gboolean
evd_socket_launch_test (gpointer user_data)
{
  EvdSocketFixture *f = (EvdSocketFixture *) user_data;
  GError *error = NULL;

  g_signal_connect (f->socket,
                    "error",
                    G_CALLBACK (evd_socket_test_on_error),
                    (gpointer) f);
  g_signal_connect (f->socket1,
                    "error",
                    G_CALLBACK (evd_socket_test_on_error),
                    (gpointer) f);

  evd_socket_set_read_handler (f->socket1, evd_socket_test_on_read, f);
  g_assert (evd_stream_get_on_read (EVD_STREAM (f->socket1)) != NULL);

  evd_socket_set_write_handler (f->socket1, evd_socket_test_on_write, f);
  g_assert (evd_stream_get_on_write (EVD_STREAM (f->socket1)) != NULL);

  /* bind */
  g_signal_connect (f->socket,
                    "bind",
                    G_CALLBACK (evd_socket_test_on_bound),
                    (gpointer) f);
  evd_socket_bind (f->socket, f->socket_addr, TRUE, &error);
  g_assert_no_error (error);

  /* listen */
  g_signal_connect (f->socket,
                    "listen",
                    G_CALLBACK (evd_socket_test_on_listen),
                    (gpointer) f);
  evd_socket_listen (f->socket, &error);
  g_assert_no_error (error);

  /* connect */
  g_signal_connect (f->socket,
                    "new-connection",
                    G_CALLBACK (evd_socket_test_on_new_conn),
                    (gpointer) f);
  g_signal_connect (f->socket1,
                    "connect",
                    G_CALLBACK (evd_socket_test_on_connect),
                    (gpointer) f);
  evd_socket_connect_to (f->socket1, f->socket_addr, &error);
  g_assert_no_error (error);
  g_assert_cmpint (evd_socket_get_status (f->socket1), ==, EVD_SOCKET_CONNECTING);

  return FALSE;
}

static void
evd_socket_test (EvdSocketFixture *f)
{
  f->break_src_id = g_timeout_add (1000,
                                   (GSourceFunc) evd_socket_test_break,
                                   (gpointer) f);

  g_idle_add ((GSourceFunc) evd_socket_launch_test,
              (gpointer) f);

  g_main_loop_run (f->main_loop);

  g_assert (f->bind);
  g_assert (f->listen);
  g_assert (f->connect);
  g_assert (f->new_conn);
}

#endif /* __TEST_SOCKET_COMMON_H__ */
