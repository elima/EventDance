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

EvdSocket *server;
GSocketAddress *addr;
GMainLoop *main_loop;

gboolean
close_timeout (gpointer data)
{
  evd_socket_close (server, NULL);

  g_main_loop_quit (main_loop);

  return FALSE;
}

void
on_socket_close (EvdSocket *socket, gpointer user_data)
{
  g_debug ("socket closed (%X)", (guint) socket);
}

gint
main (gint argc, gchar **argv)
{
  GError *error = NULL;

  /* a main loop */
  main_loop = g_main_loop_new (g_main_context_default (), FALSE);

  g_type_init ();
  g_thread_init (NULL);

  /* create server socket */
  if (! (server = evd_socket_new (G_SOCKET_FAMILY_IPV4,
				  G_SOCKET_TYPE_STREAM,
				  G_SOCKET_PROTOCOL_TCP,
				  &error)))
    {
      g_error ("socket create error: %s", error->message);
      return -1;
    }
  g_signal_connect (server, "close", G_CALLBACK (on_socket_close), NULL);

  /* bind socket */
  addr = g_inet_socket_address_new (g_inet_address_new_any (G_SOCKET_FAMILY_IPV4),
				    6666);
  if (! g_socket_bind (G_SOCKET (server), addr, TRUE, &error))
    {
      g_error ("socket bind error: %s", error->message);
      return -1;
    }

  /* start listening */
  if (! evd_socket_listen (server, &error))
    {
      g_error ("socket listening error: %s", error->message);
      return -1;
    }

  /* let the show begin */
  //  g_timeout_add (3000, (GSourceFunc) close_timeout, NULL);
  g_main_loop_run (main_loop);

  /* free stuff */
  g_object_unref (server);

  return 0;
}
