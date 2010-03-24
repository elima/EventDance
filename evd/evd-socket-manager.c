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

  for (i=0; i < nfds; i++)
    {
      EvdSocket *socket;
      GIOCondition cond = 0;

      socket = (EvdSocket *) events[i].data.ptr;
      if (! EVD_IS_SOCKET (socket))
        continue;

      if ( (events[i].events & EPOLLIN) > 0 ||
           (events[i].events & EPOLLPRI) > 0)
        cond |= G_IO_IN;

      if (events[i].events & EPOLLOUT)
        cond |= G_IO_OUT;

      if ( (events[i].events & EPOLLHUP) > 0 ||
           (events[i].events & EPOLLRDHUP) > 0)
        cond |= G_IO_HUP;

      if (events[i].events & EPOLLERR)
        cond |= G_IO_ERR;

      evd_socket_notify_condition (socket, cond);
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
                                      GIOCondition      cond,
                                      gpointer          data)
{
  struct epoll_event ev = { 0 };

  ev.events = EPOLLET | EPOLLRDHUP;

  if (cond & G_IO_IN)
    ev.events |= EPOLLIN | EPOLLPRI;
  if (cond & G_IO_OUT)
    ev.events |= EPOLLOUT;

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
  evd_socket_manager_add_fd_into_epoll (self, 1, G_IO_OUT, NULL);

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
evd_socket_manager_add_socket (EvdSocket     *socket,
                               GIOCondition   condition,
                               GError       **error)
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

  if (! evd_socket_manager_add_fd_into_epoll (self,
                                              fd,
                                              condition,
                                              socket))
    {
      if (error != NULL)
        *error = g_error_new (g_quark_from_string (DOMAIN_QUARK_STRING),
                              EVD_SOCKET_ERROR_EPOLL_ADD,
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
                                  EVD_SOCKET_ERROR_EPOLL_DEL,
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

gboolean
evd_socket_manager_mod_socket (EvdSocket     *socket,
                               GIOCondition   condition,
                               GError       **error)
{
  EvdSocketManager *self;
  gboolean result = TRUE;

  self = evd_socket_manager_get ();

  G_LOCK (num_fds);

  if ( (self != NULL) && (self->priv->num_fds > 0) )
    {
      gint fd;
      struct epoll_event ev = { 0 };

      fd = g_socket_get_fd (evd_socket_get_socket (socket));

      ev.events = EPOLLET | EPOLLRDHUP;

      if (condition & G_IO_IN)
        ev.events |= EPOLLIN | EPOLLPRI;
      if (condition & G_IO_OUT)
        ev.events |= EPOLLOUT;

      ev.data.fd = fd;
      ev.data.ptr = (void *) socket;

      if (epoll_ctl (self->priv->epoll_fd, EPOLL_CTL_MOD, fd, &ev) == -1)
        {
          if (error != NULL)
            *error = g_error_new (g_quark_from_string (DOMAIN_QUARK_STRING),
                                  EVD_SOCKET_ERROR_EPOLL_MOD,
                                  "Failed to modify socket conditions in epoll set");
          result = FALSE;
        }
    }

  G_UNLOCK (num_fds);

  return result;
}
