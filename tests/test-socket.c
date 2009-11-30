/*
 * test-socket.c
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

#include <evd.h>
#include <evd-socket-manager.h>

typedef struct
{
  GMainLoop *main_loop;
  EvdSocket *socket;
} EvdSocketFixture;

static void
evd_socket_fixture_setup (EvdSocketFixture *fixture)
{
  fixture->main_loop = g_main_loop_new (NULL, FALSE);
  fixture->socket = evd_socket_new ();

  g_assert (evd_socket_manager_get () != NULL);
}

static void
evd_socket_fixture_teardown (EvdSocketFixture *fixture)
{
  g_object_unref (fixture->socket);
  g_main_loop_quit (fixture->main_loop);
  g_main_loop_unref (fixture->main_loop);

  g_assert (evd_socket_manager_get () == NULL);
}

/* common test functions */
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

/* test initial state */
static void
evd_socket_test_initial_state (EvdSocketFixture *f)
{
  /* EvdStream */
  g_assert (EVD_IS_STREAM (f->socket));
  g_assert (EVD_IS_SOCKET (f->socket));

  g_assert (evd_stream_get_on_read (EVD_STREAM (f->socket)) == NULL);
  g_assert (evd_stream_get_on_write (EVD_STREAM (f->socket)) == NULL);

  g_assert_cmpuint (evd_stream_get_total_read (EVD_STREAM (f->socket)), ==, 0);
  g_assert_cmpuint (evd_stream_get_total_written (EVD_STREAM (f->socket)), ==, 0);

  /* EvdSocket */
  g_assert (evd_socket_get_socket (f->socket) == NULL);
  g_assert (evd_socket_get_context (f->socket) == NULL);
  g_assert (evd_socket_get_group (f->socket) == NULL);

  g_assert_cmpint (evd_socket_get_status (f->socket), ==, EVD_SOCKET_CLOSED);
  g_assert_cmpint (evd_socket_get_priority (f->socket), ==, G_PRIORITY_DEFAULT);

  evd_socket_test_config (f->socket,
                          G_SOCKET_FAMILY_INVALID,
                          G_SOCKET_TYPE_INVALID,
                          G_SOCKET_PROTOCOL_UNKNOWN);

  g_assert (evd_socket_can_read (f->socket) == FALSE);
  g_assert (evd_socket_can_write (f->socket) == FALSE);
  g_assert (evd_socket_has_write_data_pending (f->socket) == FALSE);
}

/* test bind and listen */

static void
evd_socket_test_bind_on_bound (EvdSocket *self,
                               gpointer   user_data)
{
  g_assert (EVD_IS_SOCKET (self));
  g_assert_cmpint (evd_socket_get_status (self), ==, EVD_SOCKET_BOUND);
  g_assert (evd_socket_get_socket (self) != NULL);

  evd_socket_test_config (self,
                          G_SOCKET_FAMILY_IPV4,
                          G_SOCKET_TYPE_STREAM,
                          G_SOCKET_PROTOCOL_DEFAULT);
}

static void
evd_socket_test_bind_and_listen (EvdSocketFixture *f)
{
  GError *error = NULL;
  GInetAddress *inet_addr;
  GSocketAddress *socket_addr;

  inet_addr = g_inet_address_new_from_string ("127.0.0.1");

  /* privilaged port, should fail */
  socket_addr = g_inet_socket_address_new (inet_addr,
                                           g_random_int_range (1, 1023));

  evd_socket_bind (f->socket, socket_addr, TRUE, &error);

  g_assert_error (error,
                  g_quark_from_string ("g-io-error-quark"),
                  G_IO_ERROR_PERMISSION_DENIED);
  g_error_free (error);
  error = NULL;
  g_object_unref (socket_addr);

  /* non-privileged port, should bind OK */
  g_signal_connect (f->socket,
                    "bind",
                    G_CALLBACK (evd_socket_test_bind_on_bound),
                    NULL);
  socket_addr = g_inet_socket_address_new (inet_addr,
                                           g_random_int_range (1024, 0xFFFF-1));

  evd_socket_bind (f->socket, socket_addr, TRUE, &error);

  g_assert_no_error (error);
  g_object_unref (socket_addr);
}


static void
test_socket (void)
{
  g_test_add ("/evd/socket/initial-state",
              EvdSocketFixture,
              NULL,
              evd_socket_fixture_setup,
              evd_socket_test_initial_state,
              evd_socket_fixture_teardown);

  g_test_add ("/evd/socket/bind&listen",
              EvdSocketFixture,
              NULL,
              evd_socket_fixture_setup,
              evd_socket_test_bind_and_listen,
              evd_socket_fixture_teardown);
}
