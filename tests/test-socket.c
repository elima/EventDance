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

#include <evd.h>

#define BLOCK_SIZE 1024
#define INET_PORT 6666

EvdSocket *socket1, *socket2;

static void
on_socket_close (EvdSocket *socket, gpointer user_data)
{
  g_debug ("Socket closed (%X)", (guint) socket);
}

static void
on_socket_connected (EvdSocket *socket, gpointer user_data)
{
  g_debug ("Socket connected (%X)", (guint) socket);
}

static void
on_socket_new_connection (EvdSocket *socket,
			  EvdSocket *client,
			  gpointer   user_data)
{
  g_debug ("New connection on socket (%X) by socket (%X)",
	   (guint) socket,
	   (guint) client);
}

static gboolean
test_tcp_sockets (gpointer data)
{
  GSocketAddress *addr;
  GError *error = NULL;

  /* TCP socket test */
  /* =============== */

  g_print ("\nTest 1/3: TCP sockets\n");
  g_print ("=======================\n");

  /* create server socket */
  if (! (socket1 = evd_socket_new (G_SOCKET_FAMILY_IPV4,
				   G_SOCKET_TYPE_STREAM,
				   G_SOCKET_PROTOCOL_TCP,
				   &error)))
    {
      g_error ("TCP server socket create error: %s", error->message);
      return -1;
    }
  g_signal_connect (socket1,
		    "new-connection",
		    G_CALLBACK (on_socket_new_connection),
		    NULL);
  g_signal_connect (socket1,
		    "close",
		    G_CALLBACK (on_socket_close),
		    NULL);

  /* bind server socket */
  addr = g_inet_socket_address_new (g_inet_address_new_any (G_SOCKET_FAMILY_IPV4),
				    INET_PORT);
  if (! g_socket_bind (G_SOCKET (socket1), addr, TRUE, &error))
    {
      g_error ("TCP server socket bind error: %s", error->message);
      return -1;
    }
  g_object_unref (addr);

  /* start listening */
  if (! evd_socket_listen (socket1, &error))
    {
      g_error ("TCP server socket listen error: %s", error->message);
      return -1;
    }

  /* create client socket */
  if (! (socket2 = evd_socket_new (G_SOCKET_FAMILY_IPV4,
				   G_SOCKET_TYPE_STREAM,
				   G_SOCKET_PROTOCOL_TCP,
				   &error)))
    {
      g_error ("TCP client socket create error: %s", error->message);
      return -1;
    }
  g_signal_connect (socket2,
		    "close",
		    G_CALLBACK (on_socket_close),
		    NULL);
  g_signal_connect (socket2,
		    "connected",
		    G_CALLBACK (on_socket_connected),
		    NULL);

  /* connect client socket */
  addr = g_inet_socket_address_new (g_inet_address_new_from_string ("127.0.0.1"),
				    INET_PORT);
  if (! evd_socket_connect (socket2, addr, NULL, &error))
    {
      g_error ("TCP client socket connect error: %s", error->message);
      return -1;
    }
  g_object_unref (addr);

  return FALSE;
}

static gboolean
test_connection (GSourceFunc test_func)
{
  GMainLoop *main_loop;

  main_loop = g_main_loop_new (g_main_context_default (), FALSE);

  g_idle_add (test_func, NULL);

  g_main_loop_run (main_loop);

  g_main_loop_unref (main_loop);

  return FALSE;
}

gint
main (gint argc, gchar **argv)
{
  g_type_init ();

  test_connection (test_tcp_sockets);

  return 0;
}
