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

#include <glib.h>
#include <gio/gio.h>
#include <glib/gstdio.h>

#include <evd.h>
#include <evd-socket-manager.h>

#include "test-socket-common.c"

/* test initial state */
static void
evd_socket_test_initial_state (EvdSocketFixture *f,
                               gconstpointer     test_data)
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

  g_assert_cmpint (evd_socket_get_status (f->socket),
                   ==,
                   EVD_SOCKET_STATE_CLOSED);
  g_assert_cmpint (evd_socket_get_priority (f->socket),
                   ==,
                   G_PRIORITY_DEFAULT);

  evd_socket_test_config (f->socket,
                          G_SOCKET_FAMILY_INVALID,
                          G_SOCKET_TYPE_INVALID,
                          G_SOCKET_PROTOCOL_UNKNOWN);

  g_assert (evd_socket_can_read (f->socket) == FALSE);
  g_assert (evd_socket_can_write (f->socket) == FALSE);
  g_assert (evd_socket_has_write_data_pending (f->socket) == FALSE);
}

/* test inet socket */

static void
evd_socket_inet_ipv4_fixture_setup (EvdSocketFixture *fixture,
                                    gconstpointer     test_data)
{
  gint port;
  GInetAddress *inet_addr;

  evd_socket_fixture_setup (fixture, test_data);

  inet_addr = g_inet_address_new_from_string ("127.0.0.1");
  port = g_random_int_range (1024, 0xFFFF-1);

  fixture->socket_addr = g_inet_socket_address_new (inet_addr, port);
  g_object_unref (inet_addr);
}

/* test inet socket */

static void
evd_socket_inet_ipv6_fixture_setup (EvdSocketFixture *fixture,
                                    gconstpointer     test_data)
{
  gint port;
  GInetAddress *inet_addr;

  evd_socket_fixture_setup (fixture, test_data);

  inet_addr = g_inet_address_new_from_string ("::1");
  port = g_random_int_range (1024, 0xFFFF-1);

  fixture->socket_addr = g_inet_socket_address_new (inet_addr, port);
  g_object_unref (inet_addr);
}

/* test unix socket */

static void
evd_socket_unix_fixture_setup (EvdSocketFixture *fixture,
                               gconstpointer     test_data)
{
  const gchar *UNIX_FILENAME = "/tmp/evd-test-socket-unix";

  evd_socket_fixture_setup (fixture, test_data);

  g_unlink (UNIX_FILENAME);
  fixture->socket_addr =
    G_SOCKET_ADDRESS (g_unix_socket_address_new (UNIX_FILENAME));
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

  g_test_add ("/evd/socket/unix",
              EvdSocketFixture,
              NULL,
              evd_socket_unix_fixture_setup,
              evd_socket_test,
              evd_socket_fixture_teardown);

  g_test_add ("/evd/socket/inet/ipv4",
              EvdSocketFixture,
              NULL,
              evd_socket_inet_ipv4_fixture_setup,
              evd_socket_test,
              evd_socket_fixture_teardown);

  g_test_add ("/evd/socket/inet/ipv6",
              EvdSocketFixture,
              NULL,
              evd_socket_inet_ipv6_fixture_setup,
              evd_socket_test,
              evd_socket_fixture_teardown);
}
