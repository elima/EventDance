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

#define THREADS               25
#define SOCKETS_PER_THREAD     5

#define DATA_SIZE          65535
#define BLOCK_SIZE         32752
#define TOTAL_DATA_SIZE    DATA_SIZE * THREADS * SOCKETS_PER_THREAD

#define INET_PORT 5555


GMainLoop      *main_loop_server;
EvdSocket      *server;
EvdSocketGroup *group_senders;
EvdSocketGroup *group_receivers;

static gchar data[DATA_SIZE];
static gsize total_read = 0;
static guint clients_done = 0;
static guint sockets_closed = 0;

static GMainLoop *main_loops[THREADS];
static GThread *threads[THREADS];

G_LOCK_DEFINE_STATIC (sockets_closed);
G_LOCK_DEFINE_STATIC (total_read);

static void
client_on_state_changed (EvdSocket      *socket,
                         EvdSocketState  new_state,
                         EvdSocketState  old_state,
                         gpointer        user_data)
{
  g_assert (EVD_IS_SOCKET (socket));
  g_assert_cmpint (new_state, !=, old_state);
}

static void
client_on_close (EvdSocket *socket, gpointer user_data)
{
  g_assert (EVD_IS_SOCKET (socket));
  g_assert_cmpint (evd_socket_get_status (socket),
                   ==,
                   EVD_SOCKET_STATE_CLOSED);

  G_LOCK (sockets_closed);
  sockets_closed ++;

  if (sockets_closed == THREADS * SOCKETS_PER_THREAD * 2)
    {
      gint i;
      GMainContext *context;

      for (i=0; i<THREADS; i++)
	{
	  context = g_main_loop_get_context (main_loops[i]);
	  while (g_main_context_pending (context))
	    g_main_context_iteration (context, FALSE);

	  g_main_loop_quit (main_loops[i]);
	}

      g_main_loop_quit (main_loop_server);
      g_main_context_wakeup (g_main_loop_get_context (main_loop_server));
    }
  G_UNLOCK (sockets_closed);

  g_object_unref (socket);
  socket = NULL;
}

static void
server_on_new_connection (EvdSocket *self,
                          EvdSocket *client,
                          gpointer   user_data)
{
  g_assert (EVD_IS_SOCKET (self));
  g_assert (self == server);
  g_assert_cmpint (evd_socket_get_status (self),
                   ==,
                   EVD_SOCKET_STATE_LISTENING);

  g_assert (EVD_IS_SOCKET (client));
  g_assert_cmpint (evd_socket_get_status (client),
                   ==,
                   EVD_SOCKET_STATE_CONNECTED);

  g_signal_connect (client, "close",
		    G_CALLBACK (client_on_close),
		    NULL);

  g_object_set (client,
		"group", group_senders,
		NULL);

  g_object_ref_sink (client);
}

static gboolean
socket_do_read (gpointer user_data)
{
  EvdSocket *socket = EVD_SOCKET (user_data);
  gssize size;
  GError *error = NULL;
  gchar buf[DATA_SIZE+1] = { 0 };

  if (evd_socket_get_status (socket) != EVD_SOCKET_STATE_CONNECTED)
    return FALSE;

  size = evd_socket_read (socket,
                          buf,
                          BLOCK_SIZE,
                          &error);
  g_assert_no_error (error);
  g_assert_cmpint (size, >=, 0);

  G_LOCK (total_read);
  total_read += size;
  G_UNLOCK (total_read);

  if (evd_socket_base_get_total_read (EVD_SOCKET_BASE (socket)) == DATA_SIZE)
    {
      g_assert (evd_socket_close (socket, &error));
      g_assert_no_error (error);
      g_assert_cmpint (evd_socket_get_status (socket),
                       ==,
                       EVD_SOCKET_STATE_CLOSING);
    }

  if (size == BLOCK_SIZE)
    return TRUE;
  else
    return FALSE;
}

static void
group_socket_on_read (EvdSocketGroup *self,
                      EvdSocket      *socket,
                      gpointer        user_data)
{
  g_assert (EVD_IS_SOCKET_GROUP (self));
  g_assert (self == group_receivers);
  g_assert (EVD_IS_SOCKET (socket));

  g_assert_cmpint (evd_socket_get_status (socket), ==,
                   EVD_SOCKET_STATE_CONNECTED);

  evd_timeout_add (g_main_context_get_thread_default (),
                   0,
                   G_PRIORITY_DEFAULT,
                   socket_do_read,
                   socket);
}

static void
group_socket_on_write (EvdSocketGroup *self,
                       EvdSocket      *socket,
                       gpointer        user_data)
{
  gulong total_sent;

  g_assert (EVD_IS_SOCKET_GROUP (self));
  g_assert (self == group_senders);
  g_assert (EVD_IS_SOCKET (socket));

  total_sent = evd_socket_base_get_total_written (EVD_SOCKET_BASE (socket));
  if (total_sent < DATA_SIZE)
    {
      GError *error = NULL;
      g_assert_cmpint (evd_socket_write (socket,
                                    (gchar *) (((guintptr) data) + total_sent),
                                    DATA_SIZE - total_sent,
                                    &error),
                       >=, 0);
    }
}

static gpointer
thread_handler (gpointer user_data)
{
  GMainContext *main_context;
  GMainLoop *main_loop;
  EvdSocket *sockets[SOCKETS_PER_THREAD];
  gint *thread_id = (gint *) user_data;
  gint i;
  gchar *client_addr;

  main_context = g_main_context_new ();
  g_main_context_push_thread_default (main_context);

  main_loop = g_main_loop_new (main_context, FALSE);
  G_LOCK (sockets_closed);
  main_loops[*thread_id] = main_loop;
  G_UNLOCK (sockets_closed);

  client_addr = g_strdup_printf ("%s:%d", "127.0.0.1", INET_PORT);

  /* create client sockets for this context */
  for (i=0; i<SOCKETS_PER_THREAD; i++)
    {
      EvdSocket *client;
      GError *error = NULL;

      client = evd_socket_new ();
      g_assert (EVD_IS_SOCKET (client));

      g_object_set (client,
		    "group", group_receivers,
		    NULL);

      g_signal_connect (client, "state-changed",
			G_CALLBACK (client_on_state_changed),
			NULL);

      g_signal_connect (client, "close",
			G_CALLBACK (client_on_close),
			NULL);

      g_object_set_data (G_OBJECT (client),
			 "main_loop", (gpointer) main_loop);

      g_assert (evd_socket_connect_to (client, client_addr, &error));

      g_object_ref_sink (client);
      g_object_ref (client);

      sockets[i] = client;
    }

  g_main_loop_run (main_loop);

  g_free (client_addr);

  g_main_loop_unref (main_loop);
  g_main_context_unref (main_context);

  g_free (thread_id);

  for (i=0; i<SOCKETS_PER_THREAD; i++)
    g_object_unref (sockets[i]);

  return NULL;
}

static void
server_on_state_changed (EvdSocket      *socket,
                         EvdSocketState  new_state,
                         EvdSocketState  old_state,
                         gpointer        user_data)
{
  g_assert (EVD_IS_SOCKET (socket));
  g_assert_cmpint (new_state, !=, old_state);

  if (new_state == EVD_SOCKET_STATE_LISTENING)
    {
      gint i;

      g_assert_cmpint (evd_socket_get_status (server),
                       ==,
                       EVD_SOCKET_STATE_LISTENING);

      total_read = 0;
      clients_done = 0;
      sockets_closed = 0;

      /* create thread for each context */
      for (i=0; i<THREADS; i++)
        {
          gint *thread_id = g_new0 (gint, 1);
          *thread_id = i;
          threads[i] = g_thread_create (thread_handler, thread_id, TRUE, NULL);
        }
    }
}

static void
test_socket_context ()
{
  gint i;
  gchar *server_addr;

  main_loop_server = g_main_loop_new (NULL, FALSE);

  /* server socket */
  server = evd_socket_new ();
  g_assert (EVD_IS_SOCKET (server));

  server_addr = g_strdup_printf ("%s:%d", "0.0.0.0", INET_PORT);

  evd_socket_listen (server, server_addr, NULL);
  g_assert_cmpint (evd_socket_get_status (server),
                   ==,
                   EVD_SOCKET_STATE_RESOLVING);

  g_signal_connect (server,
		    "new-connection",
		    G_CALLBACK (server_on_new_connection),
		    NULL);
  g_signal_connect (server,
		    "state-changed",
		    G_CALLBACK (server_on_state_changed),
		    NULL);

  /* socket group */
  group_senders = evd_socket_group_new ();
  group_receivers = evd_socket_group_new ();
  evd_socket_base_set_read_handler (EVD_SOCKET_BASE (group_receivers),
                                    G_CALLBACK (group_socket_on_read),
                                    NULL);

  evd_socket_base_set_write_handler (EVD_SOCKET_BASE (group_senders),
                                     G_CALLBACK (group_socket_on_write),
                                     NULL);

  /* fill data with random bytes */
  for (i=0; i<DATA_SIZE; i++)
    data[i] = g_random_int_range (32, 128);

  g_main_loop_run (main_loop_server);

  /* wait for the whorms */
  for (i=0; i<THREADS; i++)
    g_thread_join (threads[i]);

  /* free stuff */
  g_free (server_addr);
  g_object_unref (server);
  g_object_unref (group_senders);
  g_object_unref (group_receivers);

  g_main_loop_unref (main_loop_server);
}

gint
main (gint argc, gchar **argv)
{
  g_type_init ();
  g_test_init (&argc, &argv, NULL);

  g_thread_init (NULL);

  g_test_add_func ("/evd/socket/multi-threaded",
                   test_socket_context);

  g_test_run ();

  return 0;
}
