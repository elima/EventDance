/*
 * test-inet-socket.c
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

typedef struct
{
  GMainLoop *main_loop;
  EvdInetSocket *socket;
} EvdInetSocketFixture;

static void
evd_inet_socket_fixture_setup (EvdInetSocketFixture *fixture)
{
  fixture->main_loop = g_main_loop_new (NULL, FALSE);
  fixture->socket = evd_inet_socket_new ();
}

static void
evd_inet_socket_fixture_teardown (EvdInetSocketFixture *fixture)
{
  g_object_unref (fixture->socket);

  g_main_loop_quit (fixture->main_loop);
  g_main_loop_unref (fixture->main_loop);
}

static void
evd_inet_socket_on_resolve_error (EvdSocket *self,
                                  gint code,
                                  gchar *msg,
                                  gpointer user_data)
{
  g_assert_cmpint (code, ==, EVD_INET_SOCKET_ERROR_RESOLVE);
  g_assert_cmpint (evd_socket_get_status (EVD_SOCKET (self)), ==, EVD_SOCKET_CLOSED);
  g_assert_cmpint ((guintptr) evd_socket_get_socket (EVD_SOCKET (self)), ==, (guintptr) NULL);

  g_main_loop_quit ((GMainLoop *) user_data);
}

static void
evd_inet_socket_test_basic (EvdInetSocketFixture *fixture)
{
  g_assert (EVD_IS_STREAM (fixture->socket));
  g_assert (EVD_IS_SOCKET (fixture->socket));
  g_assert (EVD_IS_INET_SOCKET (fixture->socket));
  g_assert_cmpint (evd_socket_get_status (EVD_SOCKET (fixture->socket)), ==, EVD_SOCKET_CLOSED);
  g_assert_cmpint ((guintptr) evd_socket_get_socket (EVD_SOCKET (fixture->socket)), ==, (guintptr) NULL);
}

static void
evd_inet_socket_test_resolver (EvdInetSocketFixture *fixture)
{
  GError *error = NULL;

  g_signal_connect (fixture->socket,
                    "error",
                    G_CALLBACK (evd_inet_socket_on_resolve_error),
                    (gpointer) fixture->main_loop);

  evd_inet_socket_bind (fixture->socket,
                        "non-existant-domain",
                        g_test_rand_int_range (1024, G_MAXUINT16-1),
                        TRUE,
                        &error);
  g_main_loop_run (fixture->main_loop);

  evd_inet_socket_listen (fixture->socket,
                          "non-existant-domain",
                          g_test_rand_int_range (1024, G_MAXUINT16-1),
                          &error);
  g_main_loop_run (fixture->main_loop);

  evd_inet_socket_connect_to (fixture->socket,
                              "non-existant-domain",
                              g_test_rand_int_range (1024, G_MAXUINT16-1),
                              &error);
  g_main_loop_run (fixture->main_loop);
}

static void
evd_inet_socket_test_ports (EvdInetSocketFixture *fixture)
{
  GError *error = NULL;

  evd_inet_socket_bind (fixture->socket,
                        "127.0.0.1",
                        g_test_rand_int_range (1, 1024),
                        TRUE,
                        &error);

  g_assert_error (error, g_quark_from_string ("g-io-error-quark"), 14);

  g_error_free (error);
  error = NULL;

  evd_inet_socket_bind (fixture->socket,
                        "127.0.0.1",
                        g_test_rand_int_range (1024, G_MAXUINT16-1),
                        TRUE,
                        &error);
  g_assert_no_error (error);
}

static void
evd_inet_socket_on_listen (EvdSocket *self, gpointer user_data)
{
  EvdInetSocketFixture *fixture = (EvdInetSocketFixture *) user_data;

  g_assert_cmpint (evd_socket_get_status (self), ==, EVD_SOCKET_LISTENING);
  g_assert_cmpint ((guintptr) evd_socket_get_socket (self), !=, (guintptr) NULL);
}

static void
evd_inet_socket_test_listen (EvdInetSocketFixture *fixture)
{
  GError *error = NULL;

  g_signal_connect (fixture->socket,
                    "listen",
                    G_CALLBACK (evd_inet_socket_on_listen),
                    (gpointer) fixture->main_loop);

  evd_inet_socket_listen (fixture->socket,
                          "127.0.0.1",
                          g_test_rand_int_range (1024, G_MAXUINT16-1),
                          &error);

  g_assert_no_error (error);
}

static void
test_inet_socket (void)
{
  g_test_add ("/evd/inet-socket/basic",
              EvdInetSocketFixture,
              NULL,
              evd_inet_socket_fixture_setup,
              evd_inet_socket_test_basic,
              evd_inet_socket_fixture_teardown);

  g_test_add ("/evd/inet-socket/resolver",
              EvdInetSocketFixture,
              NULL,
              evd_inet_socket_fixture_setup,
              evd_inet_socket_test_resolver,
              evd_inet_socket_fixture_teardown);

  g_test_add ("/evd/inet-socket/ports",
              EvdInetSocketFixture,
              NULL,
              evd_inet_socket_fixture_setup,
              evd_inet_socket_test_ports,
              evd_inet_socket_fixture_teardown);

  g_test_add ("/evd/inet-socket/listen",
              EvdInetSocketFixture,
              NULL,
              evd_inet_socket_fixture_setup,
              evd_inet_socket_test_listen,
              evd_inet_socket_fixture_teardown);
}
