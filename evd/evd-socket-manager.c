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

#include "evd-socket-manager.h"
#include "evd-socket-protected.h"

#define DEFAULT_MAX_SOCKETS      1000 /* maximum number of sockets to poll */
#define DEFAULT_MIN_LATENCY      1000 /* nanoseconds between dispatch calls */
#define DEFAULT_DISPATCH_LOT    FALSE /* whether events whall be dispatched by lot or not */

#define DOMAIN_QUARK_STRING "org.eventdance.socket.manager"

G_DEFINE_TYPE (EvdSocketManager, evd_socket_manager, G_TYPE_OBJECT)

#define EVD_SOCKET_MANAGER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                             EVD_TYPE_SOCKET_MANAGER, \
                                             EvdSocketManagerPrivate))

/* private data */
struct _EvdSocketManagerPrivate
{
  gint num_fds;
  gulong min_latency;
  gint epoll_fd;
  GThread *thread;
  gboolean started;
  gboolean dispatch_lot;
  guint max_sockets;
  gint epoll_timeout;
};

G_LOCK_DEFINE_STATIC (num_fds);

/* we are a singleton object */
static EvdSocketManager *evd_socket_manager_singleton = NULL;

static void     evd_socket_manager_class_init   (EvdSocketManagerClass *class);
static void     evd_socket_manager_init         (EvdSocketManager *self);
static void     evd_socket_manager_finalize     (GObject *obj);

static void     evd_socket_manager_stop         (EvdSocketManager *self);

static void
evd_socket_manager_class_init (EvdSocketManagerClass *class)
{
  GObjectClass *obj_class;

  obj_class = G_OBJECT_CLASS (class);

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
  priv->num_fds = 0;

  priv->max_sockets = 1;
  priv->epoll_timeout = -1;

  priv->dispatch_lot = DEFAULT_DISPATCH_LOT;
}

static void
evd_socket_manager_finalize (GObject *obj)
{
  EvdSocketManager *self = EVD_SOCKET_MANAGER (obj);

  evd_socket_manager_stop (self);

  //  g_debug ("[EvdSocketManager 0x%X] Socket manager finalized", (guintptr) obj);

  G_OBJECT_CLASS (evd_socket_manager_parent_class)->finalize (obj);

  evd_socket_manager_singleton = NULL;
}

static EvdSocketManager *
evd_socket_manager_new (void)
{
  EvdSocketManager *self;

  self = g_object_new (EVD_TYPE_SOCKET_MANAGER, NULL);

  evd_socket_manager_singleton = self;

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

  G_LOCK (num_fds);
  if (self->priv->num_fds == 0)
    {
      G_UNLOCK (num_fds);
      return;
    }
  G_UNLOCK (num_fds);

  if (nfds == -1)
    {
      /* TODO: handle error */
      g_warning ("epoll error ocurred");

      return;
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

  if (self->priv->dispatch_lot)
    contexts = g_hash_table_new (g_direct_hash, g_direct_equal);

  for (i=0; i < nfds; i++)
    {
      EvdSocketEvent *msg;
      EvdSocket *socket;
      gint priority;

      socket = (EvdSocket *) events[i].data.ptr;
      if (! EVD_IS_SOCKET (socket))
        continue;

      priority = evd_socket_get_actual_priority (socket);

      /* create the event message */
      msg = g_new0 (EvdSocketEvent, 1);
      msg->socket = socket;
      if (events[i].events & EPOLLIN)
        msg->condition |= G_IO_IN;
      if (events[i].events & EPOLLOUT)
        msg->condition |= G_IO_OUT;
      if (events[i].events & EPOLLRDHUP)
        msg->condition |= G_IO_HUP;
      if (events[i].events & EPOLLHUP)
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
          g_source_set_priority (src, priority);
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

static gpointer
evd_socket_manager_thread_loop (gpointer data)
{
  EvdSocketManager *self = data;

  while (self->priv->started)
    {
      evd_nanosleep (self->priv->min_latency);
      evd_socket_manager_dispatch (self);
    }

  return NULL;
}

static void
evd_socket_manager_start (EvdSocketManager  *self,
                          GError           **error)
{
  self->priv->started = TRUE;

  self->priv->epoll_fd = epoll_create (DEFAULT_MAX_SOCKETS);

  if (! g_thread_get_initialized ())
    g_thread_init (NULL);

  self->priv->thread = g_thread_create (evd_socket_manager_thread_loop,
                                        (gpointer) self,
                                        TRUE,
                                        error);
}

static gboolean
evd_socket_manager_add_fd_into_epoll (EvdSocketManager *self,
                                      gint              fd,
                                      gpointer          data)
{
  struct epoll_event ev = { 0 };

  ev.events = EPOLLET | EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLHUP;
  ev.data.fd = fd;
  ev.data.ptr = (void *) data;

  return (epoll_ctl (self->priv->epoll_fd, EPOLL_CTL_ADD, fd, &ev) != -1);
}

static void
evd_socket_manager_stop (EvdSocketManager *self)
{
  G_UNLOCK (num_fds);

  self->priv->started = FALSE;

  /* the only purpose of this is to interrupt the 'epoll_wait'.
     FIXME: Adding fd '1' is just a nasty hack that happens to work.
     Have to figure out a better way to interrupt it. */
  evd_socket_manager_add_fd_into_epoll (self, 1, NULL);

  g_thread_join (self->priv->thread);
  self->priv->thread = NULL;

  close (self->priv->epoll_fd);
  self->priv->epoll_fd = 0;

  G_LOCK (num_fds);
}

/* public methods */

EvdSocketManager *
evd_socket_manager_get (void)
{
  return evd_socket_manager_singleton;
}

gboolean
evd_socket_manager_add_socket (EvdSocket  *socket,
                               GError    **error)
{
  EvdSocketManager *self;
  gint fd;
  gboolean result = TRUE;

  self = evd_socket_manager_get ();
  if (self == NULL)
    self = evd_socket_manager_new ();

  if (! self->priv->started)
    evd_socket_manager_start (self, error);

  fd = g_socket_get_fd (evd_socket_get_socket (socket));

  G_LOCK (num_fds);

  if (! evd_socket_manager_add_fd_into_epoll (self, fd, socket))
    {
      if (error != NULL)
        *error = g_error_new (g_quark_from_string (DOMAIN_QUARK_STRING),
                              EVD_SOCKET_ERR_EPOLL_ADD,
                              "Failed to add socket file descriptor to epoll set");

      result = FALSE;
    }
  else
    self->priv->num_fds++;

  G_UNLOCK (num_fds);

  return result;
}

gboolean
evd_socket_manager_del_socket (EvdSocket  *socket,
                               GError    **error)
{
  EvdSocketManager *self;
  gboolean result = TRUE;

  self = evd_socket_manager_get ();

  G_LOCK (num_fds);

  if ( (self != NULL) && (self->priv->num_fds > 0) )
    {
      gint fd;

      fd = g_socket_get_fd (evd_socket_get_socket (socket));

      if (epoll_ctl (self->priv->epoll_fd, EPOLL_CTL_DEL, fd, NULL) == -1)
        {
          if (error != NULL)
            *error = g_error_new (g_quark_from_string (DOMAIN_QUARK_STRING),
                                  EVD_SOCKET_ERR_EPOLL_ADD,
                                  "Failed to remove socket file descriptor from epoll set");
          result = FALSE;
        }
      else
        {
          self->priv->num_fds--;
          if (self->priv->num_fds == 0)
            g_object_unref (self);
        }
    }

  G_UNLOCK (num_fds);

  return result;
}
