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

#include <unistd.h>
#include <sys/epoll.h>
#include <time.h>

#include "evd-socket-manager.h"
#include "evd-socket-protected.h"

#define DEFAULT_MAX_SOCKETS      1000 /* maximum number of sockets to handle */
#define DEFAULT_MIN_LATENCY    100000 /* nanoseconds between dispatch calls */
#define DEFAULT_DISPATCH_LOT     TRUE /* whether events whall be dispatched by lot or not */

#define DOMAIN_QUARK_STRING "org.eventdance.socket.manager"

G_DEFINE_TYPE (EvdSocketManager, evd_socket_manager, G_TYPE_OBJECT)

#define EVD_SOCKET_MANAGER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
		                             EVD_TYPE_SOCKET_MANAGER, \
                                             EvdSocketManagerPrivate))

/* private data */
struct _EvdSocketManagerPrivate
{
  gint ref_count;
  gulong min_latency;
  gint epoll_fd;
  GThread *thread;
  gboolean started;
  gboolean dispatch_lot;
  guint max_sockets;
  gint epoll_timeout;
};

G_LOCK_DEFINE_STATIC (ref_count);

/* we are a singleton object */
static EvdSocketManager *evd_socket_manager_singleton = NULL;

static void     evd_socket_manager_class_init   (EvdSocketManagerClass *class);
static void     evd_socket_manager_init         (EvdSocketManager *self);
static void     evd_socket_manager_finalize     (GObject *obj);
static void     evd_socket_manager_dispose      (GObject *obj);
static EvdSocketManager *evd_socket_manager_get (void);

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
  priv->ref_count = 1;

  priv->max_sockets = 1;
  priv->epoll_timeout = -1;

  priv->dispatch_lot = DEFAULT_DISPATCH_LOT;
}

static void
evd_socket_manager_dispose (GObject *obj)
{
  EvdSocketManager *self = EVD_SOCKET_MANAGER (obj);

  if (self->priv->epoll_fd != 0)
    {
      close (self->priv->epoll_fd);
      self->priv->epoll_fd--;
    }

  G_OBJECT_CLASS (evd_socket_manager_parent_class)->dispose (obj);
}

static void
evd_socket_manager_finalize (GObject *obj)
{
  evd_socket_manager_singleton = NULL;

  //  g_debug ("Socket manager finalized!");

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
evd_socket_manager_dispatch (EvdSocketManager *self)
{
  static struct epoll_event events[DEFAULT_MAX_SOCKETS];
  gint i;
  gint nfds;
  GHashTable *contexts = NULL;
  GMainContext *context;
  GSource *src;
  GQueue *queue;
  GHashTableIter iter;

  nfds = epoll_wait (self->priv->epoll_fd,
                     events,
                     self->priv->max_sockets,
                     self->priv->epoll_timeout);
  if (nfds == -1)
    {
      /* TODO: Handle error */
      g_error ("ERROR: epoll_pwait");
    }

  if (nfds > 0)
    {
      self->priv->max_sockets = DEFAULT_MAX_SOCKETS;
      self->priv->epoll_timeout = 0;
    }
  else
    {
      self->priv->max_sockets = 1;
      self->priv->epoll_timeout = -1;
    }

  /*
  if (nfds > 0)
    g_debug ("events read: %d", nfds);
  */

  if (self->priv->dispatch_lot)
    contexts = g_hash_table_new (g_direct_hash, g_direct_equal);

  for (i=0; i < nfds; i++)
    {
      EvdSocketEvent *msg;
      EvdSocket *socket;

      socket = (EvdSocket *) events[i].data.ptr;
      if (! EVD_IS_SOCKET (socket))
	continue;

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

      if (! self->priv->dispatch_lot)
	{
	  /* dispatch a single event */
	  src = g_idle_source_new ();
	  g_source_set_callback (src,
				 evd_socket_event_handler,
				 (gpointer) msg,
				 NULL);
	  g_source_attach (src, context);
	  g_source_unref (src);
	}
      else
	{
	  /* group events by context */
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
    }

  if (self->priv->dispatch_lot)
    {
      /* dispatch the lot of events to each context */
      g_hash_table_iter_init (&iter, contexts);
      while (g_hash_table_iter_next (&iter,
				     (gpointer *) &context,
				     (gpointer *) &queue))
	{
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
evd_socket_manager_thread_loop (gpointer data)
{
  EvdSocketManager *self = data;

  while (self->priv->started)
    {
      g_nanosleep (self->priv->min_latency);
      evd_socket_manager_dispatch (self);
    }

  return NULL;
}

static void
evd_socket_manager_start (EvdSocketManager  *self,
			  GError           **error)
{
  self->priv->started = TRUE;
  self->priv->ref_count = 0;

  self->priv->epoll_fd = epoll_create (DEFAULT_MAX_SOCKETS);

  if (! g_thread_get_initialized ())
    g_thread_init (NULL);

  self->priv->thread = g_thread_create (evd_socket_manager_thread_loop,
					(gpointer) self,
					TRUE,
					error);
}

static EvdSocketManager *
evd_socket_manager_get (void)
{
  return evd_socket_manager_singleton;
}

static gboolean
evd_socket_manager_add_fd_into_epoll (EvdSocketManager *self,
                                      gint              fd,
                                      gpointer          data)
{
  struct epoll_event ev = { 0 };

  ev.events = EPOLLET | EPOLLIN | EPOLLOUT | EPOLLRDHUP;
  ev.data.fd = fd;
  ev.data.ptr = (void *) data;

  return (epoll_ctl (self->priv->epoll_fd, EPOLL_CTL_ADD, fd, &ev) != -1);
}

static void
evd_socket_manager_stop (EvdSocketManager *self)
{
  GSocket *socket;
  GError *error = NULL;

  if ( (socket = g_socket_new (G_SOCKET_FAMILY_IPV4,
                               G_SOCKET_TYPE_DATAGRAM,
                               G_SOCKET_PROTOCOL_UDP,
                               &error)) == NULL)
    {
      g_warning ("error interrupting epoll_wait: %s", error->message);
      g_error_free (error);
    }
  else
    evd_socket_manager_add_fd_into_epoll (self,
                                          g_socket_get_fd (socket),
                                          NULL);

  g_thread_join (self->priv->thread);
  self->priv->thread = NULL;

  g_object_unref (socket);
}

/* public methods */

void
evd_socket_manager_ref (void)
{
  EvdSocketManager *self = evd_socket_manager_singleton;

  G_LOCK (ref_count);

  if (self == NULL)
    evd_socket_manager_singleton = evd_socket_manager_new ();
  else
    self->priv->ref_count++;

  G_UNLOCK (ref_count);
}

void
evd_socket_manager_unref (void)
{
  EvdSocketManager *self = evd_socket_manager_singleton;

  G_LOCK (ref_count);

  if (self != NULL)
    {
      self->priv->ref_count--;

      if (self->priv->ref_count < 0)
	{
	  self->priv->started = FALSE;

          evd_socket_manager_stop (self);

	  g_object_unref (self);
	}
    }

  G_UNLOCK (ref_count);
}

gboolean
evd_socket_manager_add_socket (EvdSocket  *socket,
			       GError    **error)
{
  EvdSocketManager *self;
  gint fd;
  gboolean result = TRUE;

  G_LOCK (ref_count);

  self = evd_socket_manager_get ();

  if (! self->priv->started)
    evd_socket_manager_start (self, error);

  fd = g_socket_get_fd (evd_socket_get_socket (socket));

  if (! evd_socket_manager_add_fd_into_epoll (self, fd, socket))
    {
      if (error != NULL)
        *error = g_error_new (g_quark_from_string (DOMAIN_QUARK_STRING),
                              EVD_SOCKET_ERR_EPOLL_ADD,
                              "Failed to add socket file descriptor to epoll set");
      result = FALSE;
    }

  G_UNLOCK (ref_count);

  return result;
}

gboolean
evd_socket_manager_del_socket (EvdSocket  *socket,
			       GError    **error)
{
  EvdSocketManager *self;
  gint fd;
  gboolean result = TRUE;

  G_LOCK (ref_count);

  self = evd_socket_manager_get ();

  fd = g_socket_get_fd (evd_socket_get_socket (socket));

  if (epoll_ctl (self->priv->epoll_fd, EPOLL_CTL_DEL, fd, NULL) == -1)
    {
      *error = g_error_new (g_quark_from_string (DOMAIN_QUARK_STRING),
			    EVD_SOCKET_ERR_EPOLL_ADD,
			    "Failed to remove socket file descriptor from epoll set");
      result = FALSE;
    }

  G_UNLOCK (ref_count);

  return result;
}
