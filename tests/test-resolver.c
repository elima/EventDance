/*
 * test-resolver.c
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

#ifdef HAVE_GIO_UNIX
#include <gio/gunixsocketaddress.h>
#endif

#include <evd-resolver.h>

#define UNIX_ADDR "/this-is-any-unix-addr"
#define IPV4_OK_1 "192.168.0.1:1234"

typedef struct
{
  GMainLoop *main_loop;
  EvdResolver *resolver;
} Fixture;

static void
fixture_setup (Fixture       *f,
               gconstpointer  test_data)
{
  f->resolver = evd_resolver_get_default ();
  f->main_loop = g_main_loop_new (NULL, FALSE);
}

static void
fixture_teardown (Fixture       *f,
                  gconstpointer  test_data)
{
  g_object_unref (f->resolver);
  g_main_loop_unref (f->main_loop);
}

static void
get_default (Fixture       *f,
             gconstpointer  test_data)
{
  EvdResolver *other_resolver;

  g_assert (EVD_IS_RESOLVER (f->resolver));
  g_assert_cmpint (G_OBJECT (f->resolver)->ref_count, ==, 1);

  other_resolver = evd_resolver_get_default ();
  g_assert (f->resolver == other_resolver);
  g_assert_cmpint (G_OBJECT (f->resolver)->ref_count, ==, 2);

  g_object_unref (other_resolver);
}

#ifdef HAVE_GIO_UNIX

/* unix-addr */

static void
unix_addr_on_resolve (EvdResolver *self,
                      GList       *addresses,
                      GError      *error,
                      gpointer     user_data)
{
  Fixture *f = (Fixture *) user_data;
  GSocketAddress *addr;

  g_assert_no_error (error);

  g_assert (EVD_IS_RESOLVER (self));
  g_assert (f->resolver == self);

  g_assert (addresses != NULL);
  g_assert_cmpint (g_list_length (addresses), ==, 1);

  addr = G_SOCKET_ADDRESS (addresses->data);
  g_assert (G_IS_SOCKET_ADDRESS (addr));
  g_assert_cmpint (g_socket_address_get_family (addr),
                   ==,
                   G_SOCKET_FAMILY_UNIX);
  g_assert_cmpstr (g_unix_socket_address_get_path (G_UNIX_SOCKET_ADDRESS (addr)),
                   ==,
                   UNIX_ADDR);

  evd_resolver_free_addresses (addresses);

  g_main_loop_quit (f->main_loop);
}

static void
unix_addr (Fixture       *f,
           gconstpointer  test_data)
{
  gboolean result = FALSE;

  result = evd_resolver_resolve (f->resolver,
                                 UNIX_ADDR,
                                 unix_addr_on_resolve,
                                 f);

  g_assert (result);

  g_main_loop_run (f->main_loop);
}

#endif

/* ipv4-ok-no-resolve 1 */

static void
ipv4_ok_1_on_resolve (EvdResolver *self,
                      GList       *addresses,
                      GError      *error,
                      gpointer     user_data)
{
  Fixture *f = (Fixture *) user_data;
  GSocketAddress *addr;
  GInetAddress *inet_addr;

  g_assert_no_error (error);

  g_assert (EVD_IS_RESOLVER (self));
  g_assert (f->resolver == self);

  g_assert (addresses != NULL);
  g_assert_cmpint (g_list_length (addresses), ==, 1);

  addr = G_SOCKET_ADDRESS (addresses->data);
  g_assert (G_IS_SOCKET_ADDRESS (addr));
  g_assert_cmpint (g_socket_address_get_family (addr),
                   ==,
                   G_SOCKET_FAMILY_IPV4);
  g_assert_cmpint (g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (addr)),
                   ==,
                   1234);

  inet_addr = g_inet_socket_address_get_address (G_INET_SOCKET_ADDRESS (addr));
  g_assert_cmpstr (g_inet_address_to_string (inet_addr),
                   ==,
                   "192.168.0.1");
  g_object_unref (inet_addr);

  evd_resolver_free_addresses (addresses);

  g_main_loop_quit (f->main_loop);
}

static void
ipv4_ok_1 (Fixture       *f,
           gconstpointer  test_data)
{
  gboolean result = FALSE;

  result = evd_resolver_resolve (f->resolver,
                                 IPV4_OK_1,
                                 ipv4_ok_1_on_resolve,
                                 f);

  g_assert (result);

  g_main_loop_run (f->main_loop);
}

gint
main (gint argc, gchar **argv)
{
  g_type_init ();
  g_test_init (&argc, &argv, NULL);

  g_test_add ("/evd/resolver/get-default",
              Fixture,
              NULL,
              fixture_setup,
              get_default,
              fixture_teardown);

#ifdef HAVE_GIO_UNIX
  g_test_add ("/evd/resolver/unix-addr",
              Fixture,
              NULL,
              fixture_setup,
              unix_addr,
              fixture_teardown);
#endif

  g_test_add ("/evd/resolver/ipv4-addr/ok-1",
              Fixture,
              NULL,
              fixture_setup,
              ipv4_ok_1,
              fixture_teardown);

  g_test_run ();

  return 0;
}
