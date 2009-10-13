/*
 * test-socket-context.c
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
#include <evd.h>

#define RUNS                  1

#define THREADS             350
#define SOCKETS_PER_THREAD    1
#define INET_PORT          6666

#define DATA_SIZE         10000
#define BLOCK_SIZE         1500
#define TOTAL_DATA_SIZE    DATA_SIZE * THREADS * SOCKETS_PER_THREAD

#define SOCKET_BANDWIDTH      0
#define GROUP_BANDWIDTH       0

GMainLoop      *main_loop_server;
EvdInetSocket  *server;
EvdSocketGroup *group;

static guint conns = 0;
static gchar data[DATA_SIZE];
static gulong total_read = 0;
static guint clients_done = 0;

G_LOCK_DEFINE_STATIC (clients_done);

static guint
timeout_add (guint timeout,
	     GMainContext *context,
	     GSourceFunc func,
	     gpointer user_data)
{
  GSource *src;
  guint src_id;

  if (timeout == 0)
    src = g_idle_source_new ();
  else
    src = g_timeout_source_new (timeout);

  g_source_set_callback (src,
			 func,
			 user_data,
			 NULL);
  src_id = g_source_attach (src, context);
  g_source_unref (src);

  return src_id;
}

static gboolean
client_send_data (gpointer user_data)
{
  gssize size;
  EvdSocket *client = EVD_SOCKET (user_data);
  GError *error = NULL;
  guint retry_wait;

  size = DATA_SIZE - evd_stream_get_total_written (EVD_STREAM (client));
  if (size > BLOCK_SIZE)
    size = BLOCK_SIZE;

  if ( (size = evd_socket_write (client, data, size, &retry_wait, &error)) < 0)
    g_debug ("ERROR sending data: %s", error->message);
  else
    {
      //      g_debug ("%d bytes sent from client socket 0x%X", size, (guintptr) client);

      if (evd_stream_get_total_written (EVD_STREAM (client)) < DATA_SIZE)
	timeout_add (0, g_main_context_get_thread_default (),
		     (GSourceFunc) client_send_data,
		     (gpointer) client);
    }

  return FALSE;
}

static void
client_on_connect (EvdSocket *socket,
		   GSocketAddress *addr,
		   gpointer user_data)
{
  conns++;
  //  g_debug ("client connected (%d)", conns);

  /* send data to server */
  timeout_add (0,
	       g_main_context_get_thread_default (),
	       client_send_data,
	       socket);
}

static void
client_on_connect_timeout (EvdSocket *socket, gpointer user_data)
{
  g_debug ("client connection timed-out");
}

static void
client_on_close (EvdSocket *socket, gpointer user_data)
{
  //  g_debug ("client 0x%X socket closed", (guintptr) socket);

  g_object_unref (socket);
}

static gpointer
thread_handler (gpointer user_data)
{
  GMainContext *main_context;
  GMainLoop *main_loop;
  EvdInetSocket *sockets[SOCKETS_PER_THREAD];
  gint i;

  main_context = g_main_context_new ();
  g_main_context_push_thread_default (main_context);

  main_loop = g_main_loop_new (main_context, FALSE);

  /* create client sockets for this context */
  for (i=0; i<SOCKETS_PER_THREAD; i++)
    {
      EvdInetSocket *client;
      GError *error = NULL;

      client = evd_inet_socket_new ();
      g_object_set (client,
		    "connect-timeout", 3000,
		    "group", group,
		    NULL);

      g_signal_connect (client, "connect",
			G_CALLBACK (client_on_connect),
			NULL);

      g_signal_connect (client, "connect-timeout",
			G_CALLBACK (client_on_connect_timeout),
			NULL);

      g_object_set_data (G_OBJECT (client),
			 "main_loop", (gpointer) main_loop);

      if (! evd_inet_socket_connect_to (client, "127.0.0.1", INET_PORT, &error))
	{
	  g_error ("ERROR connecting client socket: %s", error->message);
	  break;
	}

      g_object_ref_sink (client);
      sockets[i] = client;
    }

  g_main_loop_run (main_loop);

  g_main_loop_unref (main_loop);
  g_main_context_unref (main_context);

  G_LOCK (clients_done);
  clients_done ++;
  //  g_debug ("threads finished: %d", clients_done);

  if (clients_done == THREADS * SOCKETS_PER_THREAD)
    {
      while (g_main_context_pending (g_main_context_default ()))
	g_main_context_dispatch (NULL);
      g_main_loop_quit (main_loop_server);
    }
  G_UNLOCK (clients_done);

  return NULL;
}

static void
server_on_new_connection (EvdSocket *self, EvdSocket *client, gpointer user_data)
{
  //  g_debug ("incoming connection on server socket (0x%X)", (guintptr) client);

  g_object_ref_sink (client);

  /* send data to client */
  g_idle_add ((GSourceFunc) client_send_data,
	      (gpointer) client);

  g_signal_connect (client, "close",
		    G_CALLBACK (client_on_close),
		    NULL);
}

static gboolean
client_read_data (gpointer user_data)
{
  EvdSocket *client = EVD_SOCKET (user_data);
  gssize size;
  GError *error = NULL;
  gchar buf[DATA_SIZE+1] = { 0 };
  guint retry_wait;

  if (evd_socket_get_status (client) != EVD_SOCKET_CONNECTED)
    return FALSE;

  if ( (size = evd_socket_read_buffer (client,
				       buf,
				       BLOCK_SIZE,
				       &retry_wait,
				       &error)) < 0)
    {
      g_debug ("ERROR reading data: %s", error->message);
    }
  else
    {
      total_read += size;

      //      g_debug ("%d bytes read in socket 0x%X", size, (guintptr) client);
      if (evd_stream_get_total_read (EVD_STREAM (client)) < DATA_SIZE)
	{
	  timeout_add (retry_wait,
		       g_main_context_get_thread_default (),
		       (GSourceFunc) client_read_data,
		       (gpointer) client);
	}
      else
	{
	  GMainLoop *main_loop;

	  /* finish */
	  evd_socket_close (client, NULL);

	  main_loop = (GMainLoop *) g_object_get_data (G_OBJECT (client),
						       "main_loop");
	  g_object_unref (client);

	  g_main_loop_quit (main_loop);
	}
    }

  return FALSE;
}

static void
group_socket_on_read (EvdSocketGroup *self, EvdSocket *socket, gpointer user_data)
{
  g_idle_add ((GSourceFunc) client_read_data, (gpointer) socket);
}

gint
main (gint argc, gchar **argv)
{
  gint i, j;
  GThread *threads[THREADS];

  g_type_init ();
  g_thread_init (NULL);

  main_loop_server = g_main_loop_new (NULL, FALSE);

  /* server socket */
  server = evd_inet_socket_new ();
  evd_inet_socket_listen (server, "0.0.0.0", INET_PORT, NULL);
  g_signal_connect (server,
		    "new-connection",
		    G_CALLBACK (server_on_new_connection),
		    NULL);

  /* socket group */
  group = evd_socket_group_new ();
  evd_socket_group_set_read_handler (group,
		     (EvdSocketGroupReadHandler) group_socket_on_read,
		     NULL);

  /* fill data with random bytes */
  for (i=0; i<DATA_SIZE; i++)
    data[i] = g_random_int_range (32, 128);

  for (j=0; j<RUNS; j++)
    {
      total_read = 0;
      clients_done = 0;
      conns = 0;

      /* create thread for each context */
      for (i=0; i<THREADS; i++)
	threads[i] = g_thread_create (thread_handler, NULL, TRUE, NULL);

      g_main_loop_run (main_loop_server);

      /* wait for the whorms */
      for (i=0; i<THREADS; i++)
	g_thread_join (threads[i]);
    }

  /* free stuff */
  g_object_unref (server);
  g_object_unref (group);

  g_main_loop_unref (main_loop_server);

  return 0;
}
