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

#include <string.h>

#include <glib.h>
#include <gio/gio.h>

#include <evd.h>

#define BLOCK_SIZE 1024
#define INET_PORT  6666
#define TIMEOUT    3000

EvdSocket *socket1, *socket2;
GMainLoop *main_loop;

static const gchar *greeting = "Hello world!";
guint bytes_read;
guint bytes_expected;

guint sockets_closed;
guint expected_sockets_closed;

static gboolean
terminate (gpointer user_data)
{
  g_main_loop_quit (main_loop);

  return FALSE;
}

static void
on_socket_read (EvdSocket *socket, gpointer user_data)
{
  GError *error = NULL;
  static gchar buf[BLOCK_SIZE];
  static gssize size;

  if ((size = evd_socket_read_to_buffer (socket,
					 buf,
					 BLOCK_SIZE,
					 &error)) == -1)
    {
      g_debug ("ERROR: Failed to read data from socket: %s", error->message);
      return;
    }

  if (size > 0)
    {
      g_debug ("%d bytes read from socket (%X): %s",
	       (guint) size,
	       (guint) (guintptr) socket,
	       buf);

      bytes_read += size;
    }

  if (bytes_read == bytes_expected)
    {
      evd_socket_close (socket1, NULL);
      evd_socket_close (socket2, NULL);
    }
}

static void
on_socket_close (EvdSocket *socket, gpointer user_data)
{
  g_debug ("Socket closed (%X)", (guint) (guintptr) socket);

  g_object_unref (socket);

  sockets_closed ++;

  if (sockets_closed == expected_sockets_closed)
    g_idle_add ((GSourceFunc) terminate, NULL);
}

static void
on_socket_connected (EvdSocket *socket, gpointer user_data)
{
  GError *error = NULL;

  g_debug ("Socket connected (%X)", (guint) (guintptr) socket);

  evd_socket_set_read_handler (socket,
			       on_socket_read,
			       NULL);

  if (g_socket_send (evd_socket_get_socket (socket),
		     greeting,
		     strlen (greeting), NULL, &error) < 0)
    {
      g_debug ("ERROR sending greeting: %s", error->message);
    }
}

static void
on_socket_new_connection (EvdSocket *socket,
			  EvdSocket *client,
			  gpointer   user_data)
{
  g_debug ("Incoming connection (%X) on socket (%X)",
	   (guint) (guintptr) client,
	   (guint) (guintptr) socket);

  g_signal_connect (client,
		    "close",
		    G_CALLBACK (on_socket_close),
		    NULL);

  g_socket_send (evd_socket_get_socket (client),
		 greeting,
		 strlen (greeting), NULL, NULL);

  evd_socket_set_read_handler (client,
			       on_socket_read,
			       NULL);
}

static void
on_socket_listen (EvdSocket *socket, gpointer user_data)
{
  g_debug ("Socket (%X) listening", (guint) (guintptr) socket);
}

static gboolean
test_connection (GSourceFunc test_func)
{
  bytes_read = 0;
  sockets_closed = 0;

  main_loop = g_main_loop_new (g_main_context_default (), FALSE);

  g_idle_add (test_func, NULL);

  g_timeout_add (TIMEOUT,
		 (GSourceFunc) terminate,
		 NULL);

  g_main_loop_run (main_loop);

  g_main_loop_unref (main_loop);

  g_print ("Test result: ");
  if (sockets_closed != expected_sockets_closed)
    g_print ("FAILED\n");
  else
    g_print ("PASSED\n");

  return FALSE;
}

static gboolean
test_tcp_sockets (gpointer data)
{
  GSocketAddress *addr;
  GError *error = NULL;

  /* TCP socket test */
  /* =============== */

  bytes_expected = strlen (greeting) * 2;
  expected_sockets_closed = 3;

  g_print ("\nTest 1/3: TCP sockets\n");
  g_print ("=======================\n");

  /* create server socket */
  socket1 = evd_socket_new ();

  g_signal_connect (socket1,
		    "new-connection",
		    G_CALLBACK (on_socket_new_connection),
		    NULL);
  g_signal_connect (socket1,
		    "close",
		    G_CALLBACK (on_socket_close),
		    NULL);
  g_signal_connect (socket1,
		    "listen",
		    G_CALLBACK (on_socket_listen),
		    NULL);

  /* bind server socket */
  addr = g_inet_socket_address_new (g_inet_address_new_any (G_SOCKET_FAMILY_IPV4),
				    INET_PORT);
  if (! evd_socket_bind (socket1, addr, TRUE, &error))
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
  socket2 = evd_socket_new ();

  g_signal_connect (socket2,
		    "close",
		    G_CALLBACK (on_socket_close),
		    NULL);
  g_signal_connect (socket2,
		    "connect",
		    G_CALLBACK (on_socket_connected),
		    NULL);

  /* connect client socket */
  addr = g_inet_socket_address_new (g_inet_address_new_from_string ("127.0.0.1"),
				    INET_PORT);
  if (! evd_socket_connect_to (socket2, addr, &error))
    {
      g_error ("TCP client socket connect error: %s", error->message);
      return -1;
    }
  g_object_unref (addr);

  return FALSE;
}

static gboolean
test_udp_sockets (gpointer data)
{
  GSocketAddress *addr1, *addr2;
  GError *error = NULL;

  /* UDP socket test */
  /* =============== */

  bytes_expected = strlen (greeting) * 2;
  expected_sockets_closed = 2;

  g_print ("\nTest 2/3: UDP sockets\n");
  g_print ("=======================\n");

  addr1 = g_inet_socket_address_new (g_inet_address_new_from_string ("127.0.0.1"),
				     6666);
  addr2 = g_inet_socket_address_new (g_inet_address_new_from_string ("127.0.0.1"),
				     6667);

  /* create socket1 */
  socket1 = evd_socket_new ();
  g_object_set (socket1,
		"type", G_SOCKET_TYPE_DATAGRAM,
		"protocol", G_SOCKET_PROTOCOL_UDP,
		NULL);

  g_signal_connect (socket1,
		    "close",
		    G_CALLBACK (on_socket_close),
		    NULL);
  g_signal_connect (socket1,
		    "connect",
		    G_CALLBACK (on_socket_connected),
		    NULL);

  /* bind socket1 */
  if (! evd_socket_bind (socket1, addr2, TRUE, &error))
    {
      g_debug ("UDP socket1 bind error: %s", error->message);
      return FALSE;
    }

  /* connect socket1 */
  if (! evd_socket_connect_to (socket1, addr1, &error))
    {
      g_error ("UDP socket1 connect error: %s", error->message);
      return FALSE;
    }

  /* create socket2 */
  socket2 = evd_socket_new ();
  g_object_set (socket2,
		"type", G_SOCKET_TYPE_DATAGRAM,
		"protocol", G_SOCKET_PROTOCOL_UDP,
		NULL);

  g_signal_connect (socket2,
		    "close",
		    G_CALLBACK (on_socket_close),
		    NULL);
  g_signal_connect (socket2,
		    "connect",
		    G_CALLBACK (on_socket_connected),
		    NULL);

  if (! evd_socket_bind (socket2, addr1, TRUE, &error))
    {
      g_debug ("UDP socket2 bind error: %s", error->message);
      return FALSE;
    }
  g_object_set_data (G_OBJECT (socket2), "dest-addr", addr2);
  g_object_set_data (G_OBJECT (socket2), "peer", socket1);

  /* connect socket2 */
  if (! evd_socket_connect_to (socket2, addr2, &error))
    {
      g_error ("UDP socket2 connect error: %s", error->message);
      return FALSE;
    }

  g_object_unref (addr1);
  g_object_unref (addr2);

  return FALSE;
}

gint
main (gint argc, gchar **argv)
{
  g_type_init ();

  test_connection (test_tcp_sockets);
  test_connection (test_udp_sockets);

  return 0;
}
