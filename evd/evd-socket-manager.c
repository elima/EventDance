/*
 * evd-socket-manager.c
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

#include <sys/epoll.h>
#include <time.h>

#include "evd-socket-manager.h"
#include "evd-socket-protected.h"

#define DEFAULT_MAX_SOCKETS 50000 /* maximum number of sockets to handle */
#define DEFAULT_MIN_LATENCY   100 /* nanoseconds between dispatch calls */

#define DOMAIN_QUARK_STRING "org.eventdance.socket.manager"

G_DEFINE_TYPE (EvdSocketManager, evd_socket_manager, G_TYPE_OBJECT)

#define EVD_SOCKET_MANAGER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
		                             EVD_TYPE_SOCKET_MANAGER, \
                                             EvdSocketManagerPrivate))

/* private data */
struct _EvdSocketManagerPrivate
{
  gulong min_latency;

  gint epoll_fd;
  GThread *thread;

  gboolean started;
};

/* we are a singleton object */
static EvdSocketManager *evd_socket_manager_singleton = NULL;

static void     evd_socket_manager_class_init   (EvdSocketManagerClass *class);
static void     evd_socket_manager_init         (EvdSocketManager *self);
static void     evd_socket_manager_finalize     (GObject *obj);
static void     evd_socket_manager_dispose      (GObject *obj);
static gpointer evd_socket_manager_idle         (gpointer data);


static void
evd_socket_manager_class_init (EvdSocketManagerClass *class)
{
  GObjectClass *obj_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_socket_manager_dispose;
  obj_class->finalize = evd_socket_manager_finalize;

  /* add private structure */
  g_type_class_add_private (obj_class, sizeof (EvdSocketManagerPrivate));
}

static void
evd_socket_manager_init (EvdSocketManager *self)
{
  EvdSocketManagerPrivate *priv;

  priv = EVD_SOCKET_MANAGER_GET_PRIVATE (self);
  self->priv = priv;

  /* initialize private members */
  priv->min_latency = DEFAULT_MIN_LATENCY;
  priv->started = FALSE;
}

static void
evd_socket_manager_dispose (GObject *obj)
{
  EvdSocketManager *self = EVD_SOCKET_MANAGER (obj);

  if (self->priv->started)
    if (self->priv->thread != NULL)
      {
	self->priv->started = FALSE;
	g_thread_join (self->priv->thread);
	self->priv->thread = NULL;
      }

  G_OBJECT_CLASS (evd_socket_manager_parent_class)->dispose (obj);
}

static void
evd_socket_manager_finalize (GObject *obj)
{
  evd_socket_manager_singleton = NULL;

  G_OBJECT_CLASS (evd_socket_manager_parent_class)->finalize (obj);
}

static EvdSocketManager *
evd_socket_manager_new (void)
{
  EvdSocketManager *self;

  self = g_object_new (EVD_TYPE_SOCKET_MANAGER, NULL);

  return self;
}

static void
evd_socket_manager_start (EvdSocketManager  *self,
			  GError           **error)
{
  if (! g_thread_get_initialized ())
    g_thread_init (NULL);

  self->priv->started = TRUE;

  self->priv->epoll_fd = epoll_create (DEFAULT_MAX_SOCKETS);

  self->priv->thread = g_thread_create (evd_socket_manager_idle,
					(gpointer) self,
					TRUE,
					error);
}

static void
evd_socket_manager_dispatch (EvdSocketManager *self)
{
  static struct epoll_event events[DEFAULT_MAX_SOCKETS];
  gint i;
  gint nfds;
  GHashTable *contexts;
  GHashTableIter iter;
  GMainContext *context;
  GQueue *queue;

  nfds = epoll_wait (self->priv->epoll_fd, events, DEFAULT_MAX_SOCKETS, 0);
  if (nfds == -1)
    {
      /* TODO: Handle error */
      g_error ("ERROR: epoll_pwait");
    }

  /*
  if (nfds > 0)
    g_debug ("events read: %d", nfds);
  */

  /* group event by context */
  contexts = g_hash_table_new (g_direct_hash, g_direct_equal);
  for (i=0; i < nfds; i++)
    {
      EvdSocketEvent *msg;
      EvdSocket *socket;

      socket = (EvdSocket *) events[i].data.ptr;

      /* create the event message */
      msg = g_new0 (EvdSocketEvent, 1);
      msg->socket = socket;
      if (events[i].events & EPOLLIN)
	msg->condition |= G_IO_IN;
      if (events[i].events & EPOLLOUT)
	msg->condition |= G_IO_OUT;
      if (events[i].events & EPOLLRDHUP)
	msg->condition |= G_IO_HUP;
      if (events[i].events & EPOLLERR)
	msg->condition |= G_IO_ERR;

      /* obtain the context for this event */
      context = evd_socket_get_context (socket);
      if (context == NULL)
	context = g_main_context_default ();

      /* get the list of events for this context */
      queue = g_hash_table_lookup (contexts, context);
      if (queue == NULL)
	{
	  queue = g_queue_new ();
	  g_hash_table_insert (contexts,
			       (gpointer) context,
			       (gpointer) queue);
	}
      g_queue_push_tail (queue, (gpointer) msg);
    }

  /* dispatch the lot of events to each context */
  g_hash_table_iter_init (&iter, contexts);
  while (g_hash_table_iter_next (&iter,
				 (gpointer *) &context,
				 (gpointer *) &queue))
    {
      GSource *src;

      src = g_idle_source_new ();
      g_source_set_callback (src,
			     evd_socket_event_list_handler,
			     (gpointer) queue,
			     NULL);
      g_source_attach (src, context);
      g_source_unref (src);
    }
  g_hash_table_unref (contexts);
}

/* TODO: eventuallly remove this from here */
static void
g_nanosleep (gulong nanoseconds)
{
  struct timespec delay;

  delay.tv_sec = 0;
  delay.tv_nsec = nanoseconds;

  nanosleep (&delay, NULL);
}

static gpointer
evd_socket_manager_idle (gpointer data)
{
  EvdSocketManager *self = data;

  while (self->priv->started)
    {
      evd_socket_manager_dispatch (self);

      g_nanosleep (self->priv->min_latency);
    }

  return NULL;
}

static EvdSocketManager *
evd_socket_manager_get (void)
{
  if (evd_socket_manager_singleton == NULL)
    evd_socket_manager_singleton = evd_socket_manager_new ();

  return evd_socket_manager_singleton;
}

/* public methods */

void
evd_socket_manager_ref (void)
{
  if (evd_socket_manager_singleton == NULL)
    evd_socket_manager_singleton = evd_socket_manager_new ();
  else
    g_object_ref (evd_socket_manager_singleton);
}

void
evd_socket_manager_unref (void)
{
  if (evd_socket_manager_singleton != NULL)
    g_object_unref (evd_socket_manager_singleton);
}

gboolean
evd_socket_manager_add_socket (EvdSocket  *socket,
			       GError    **error)
{
  EvdSocketManager *self;
  struct epoll_event ev;
  gint fd;
  gboolean result = TRUE;

  self = evd_socket_manager_get ();

  if (! self->priv->started)
    evd_socket_manager_start (self, error);

  fd = g_socket_get_fd (evd_socket_get_socket (socket));

  ev.events = EPOLLET | EPOLLIN | EPOLLOUT | EPOLLRDHUP;
  ev.data.fd = fd;
  ev.data.ptr = (void *) socket;

  if (epoll_ctl (self->priv->epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1)
    {
      *error = g_error_new (g_quark_from_string (DOMAIN_QUARK_STRING),
			    EVD_SOCKET_ERR_EPOLL_ADD,
			    "Failed to add socket file descriptor to epoll set");
      result = FALSE;
    }

  return result;
}

gboolean
evd_socket_manager_del_socket (EvdSocket  *socket,
			       GError    **error)
{
  EvdSocketManager *self;
  gint fd;
  gboolean result = TRUE;

  self = evd_socket_manager_get ();

  fd = g_socket_get_fd (evd_socket_get_socket (socket));

  if (epoll_ctl (self->priv->epoll_fd, EPOLL_CTL_DEL, fd, NULL) == -1)
    {
      *error = g_error_new (g_quark_from_string (DOMAIN_QUARK_STRING),
			    EVD_SOCKET_ERR_EPOLL_ADD,
			    "Failed to remove socket file descriptor from epoll set");
      result = FALSE;
    }

  return result;
}
