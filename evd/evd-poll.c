/*
 * evd-poll.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2009/2010/2011, Igalia S.L.
 *
 * Authors:
 *   Eduardo Lima Mitev <elima@igalia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 3, or (at your option) any later version as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License at http://www.gnu.org/licenses/lgpl-3.0.txt
 * for more details.
 */

#include <errno.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <gio/gio.h>

#include "evd-poll.h"

#include "evd-error.h"
#include "evd-utils.h"

#define DEFAULT_MAX_FDS 1000 /* maximum number of file descriptors to poll */

G_DEFINE_TYPE (EvdPoll, evd_poll, G_TYPE_OBJECT)

#define EVD_POLL_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                   EVD_TYPE_POLL, \
                                   EvdPollPrivate))

/* private data */
struct _EvdPollPrivate
{
  gint epoll_fd;
  GThread *thread;
  gboolean started;
  guint max_fds;

  GMainLoop *main_loop;

  struct epoll_event events[DEFAULT_MAX_FDS];
  gint nr_events;

  gint interrupt_fds[2];
};

struct _EvdPollSession
{
  gint ref_count;

  EvdPoll *self;
  gint fd;
  GIOCondition cond_in;
  GIOCondition cond_out;
  GMainContext *main_context;
  guint priority;
  EvdPollCallback callback;
  gpointer user_data;
  gint src_id;
};

G_LOCK_DEFINE_STATIC (epoll_mutex);
G_LOCK_DEFINE_STATIC (interrupt_mutex);

static EvdPoll *evd_poll_default = NULL;

static void     evd_poll_class_init   (EvdPollClass *class);
static void     evd_poll_init         (EvdPoll *self);
static void     evd_poll_finalize     (GObject *obj);

static void     evd_poll_stop         (EvdPoll *self);

static gboolean evd_poll_epoll_ctl    (EvdPoll      *self,
                                       gint          fd,
                                       gint          op,
                                       GIOCondition  cond,
                                       gpointer      data);

static void
evd_poll_class_init (EvdPollClass *class)
{
  GObjectClass *obj_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->finalize = evd_poll_finalize;

  g_type_class_add_private (obj_class, sizeof (EvdPollPrivate));
}

static void
evd_poll_init (EvdPoll *self)
{
  EvdPollPrivate *priv;

  priv = EVD_POLL_GET_PRIVATE (self);
  self->priv = priv;

  priv->started = FALSE;

  priv->max_fds = DEFAULT_MAX_FDS;

  priv->main_loop = NULL;
}

static void
evd_poll_finalize (GObject *obj)
{
  EvdPoll *self = EVD_POLL (obj);

  evd_poll_stop (self);

  if (self->priv->main_loop != NULL)
    g_main_loop_unref (self->priv->main_loop);

  G_OBJECT_CLASS (evd_poll_parent_class)->finalize (obj);

  G_LOCK (epoll_mutex);
  if (self == evd_poll_default)
    evd_poll_default = NULL;
  G_UNLOCK (epoll_mutex);
}

static void
evd_poll_session_ref (EvdPollSession *session)
{
  g_atomic_int_exchange_and_add (&session->ref_count, 1);
}

static gboolean
evd_poll_session_unref (EvdPollSession *session)
{
  gint old_ref;

  old_ref = g_atomic_int_get (&session->ref_count);
  if (old_ref > 1)
    {
      g_atomic_int_compare_and_exchange (&session->ref_count, old_ref, old_ref - 1);
      return TRUE;
    }
  else
    {
      if (session->src_id != 0)
        g_source_remove (session->src_id);

      g_main_context_unref (session->main_context);

      g_slice_free (EvdPollSession, session);

      return FALSE;
    }
}

static gboolean
evd_poll_callback_wrapper (gpointer user_data)
{
  EvdPollSession *session;
  GIOCondition cond_out = 0;
  EvdPollCallback callback = NULL;

  G_LOCK (epoll_mutex);

  session = (EvdPollSession *) user_data;

  session->src_id = 0;

  if (evd_poll_session_unref (session))
    {
      callback = session->callback;

      cond_out = session->cond_out;

      session->cond_out = 0;
    }

  G_UNLOCK (epoll_mutex);

  if (callback != NULL)
    callback (session->self, cond_out, session->user_data);

  return FALSE;
}

static gboolean
evd_poll_check_and_consume_interrupt_event (EvdPoll            *self,
                                            struct epoll_event  event)
{
  if (* (gint *) event.data.ptr == self->priv->interrupt_fds[0])
    {
      static gchar buf[1024];

      /* self-pipe trick, here we just read long enough to guarantee that pipe
         is empty, and next call to write will edge-trigger a read event again */
      read (self->priv->interrupt_fds[0], buf, 1024);
      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

static gboolean
evd_poll_dispatch (gpointer user_data)
{
  EvdPoll *self = EVD_POLL (user_data);
  gint i;
  gint nfds;
  gboolean started;
  struct epoll_event *events;

  G_LOCK (interrupt_mutex);
  self->priv->nr_events = epoll_wait (self->priv->epoll_fd,
                                      self->priv->events,
                                      self->priv->max_fds,
                                      -1);
  G_UNLOCK (interrupt_mutex);

  G_LOCK (epoll_mutex);

  events = self->priv->events;
  nfds = self->priv->nr_events;

  started = self->priv->started;

  if (started && nfds > 0)
    for (i=0; i < nfds; i++)
      {
        EvdPollSession *session;
        GIOCondition cond = 0;

        session = (EvdPollSession *) events[i].data.ptr;

        if (session == NULL ||
            evd_poll_check_and_consume_interrupt_event (self, events[i]) ||
            ! evd_poll_session_unref (session))
          {
            continue;
          }
        else
          {
            evd_poll_session_ref (session);
          }

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

        session->cond_out |= cond;

        if (session->src_id == 0)
          {
            evd_poll_session_ref (session);
            session->src_id = evd_timeout_add (session->main_context,
                                               0,
                                               session->priority,
                                               evd_poll_callback_wrapper,
                                               session);
          }
      }

  self->priv->nr_events = 0;

  G_UNLOCK (epoll_mutex);

  return started;
}

static gpointer
evd_poll_thread_loop (gpointer data)
{
  EvdPoll *self = data;
  GMainContext *main_context;

  main_context = g_main_context_new ();
  g_main_context_push_thread_default (main_context);

  self->priv->main_loop = g_main_loop_new (main_context, FALSE);
  g_main_context_unref (main_context);

  evd_timeout_add (main_context,
                   0,
                   G_PRIORITY_HIGH,
                   evd_poll_dispatch,
                   self);

  g_main_loop_run (self->priv->main_loop);

  g_main_context_pop_thread_default (main_context);

  g_main_loop_unref (self->priv->main_loop);
  self->priv->main_loop = NULL;

  return NULL;
}

static gboolean
evd_poll_start (EvdPoll *self, GError **error)
{
  self->priv->started = TRUE;

  if ( (self->priv->epoll_fd = epoll_create (DEFAULT_MAX_FDS)) == -1)
    {
      g_set_error_literal (error,
                           EVD_ERROR,
                           EVD_ERROR_EPOLL,
                           "Failed to create epoll set");

      return FALSE;
    }

  errno = 0;
  if (pipe (self->priv->interrupt_fds) != 0 ||
      ! evd_poll_epoll_ctl (self,
                            self->priv->interrupt_fds[0],
                            EPOLL_CTL_ADD, G_IO_IN | G_IO_OUT,
                            &self->priv->interrupt_fds[0]))
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           g_io_error_from_errno (errno),
                           "Failed to setup epoll's interrupt pipe");
      return FALSE;
    }

  if (! g_thread_get_initialized ())
    g_thread_init (NULL);

  self->priv->thread = g_thread_create (evd_poll_thread_loop,
                                        (gpointer) self,
                                        TRUE,
                                        error);

  return self->priv->thread != NULL;
}

static gboolean
evd_poll_epoll_ctl (EvdPoll      *self,
                    gint          fd,
                    gint          op,
                    GIOCondition  cond,
                    gpointer      data)
{
  gboolean result;

  if (op == EPOLL_CTL_DEL)
    {
      result = epoll_ctl (self->priv->epoll_fd, EPOLL_CTL_DEL, fd, NULL) != -1;
    }
  else
    {
      struct epoll_event ev = { 0 };

      ev.events = EPOLLET | EPOLLRDHUP;

      if (cond & G_IO_IN)
        ev.events |= EPOLLIN | EPOLLPRI;
      if (cond & G_IO_OUT)
        ev.events |= EPOLLOUT;

      ev.data.fd = fd;
      ev.data.ptr = (void *) data;

      result = (epoll_ctl (self->priv->epoll_fd, op, fd, &ev) == 0);
    }

  return result;
}

static void
evd_poll_interrupt_epoll_wait (EvdPoll *self)
{
  const gchar *buf = " ";

  /* self-pipe trick, here we write to pipe to wake up epoll_wait */
  write (self->priv->interrupt_fds[1], buf, 1);
}

static void
evd_poll_stop (EvdPoll *self)
{
  G_LOCK (epoll_mutex);

  self->priv->started = FALSE;

  evd_poll_interrupt_epoll_wait (self);

  close (self->priv->interrupt_fds[0]);
  close (self->priv->interrupt_fds[1]);

  if (self->priv->main_loop != NULL)
    g_main_loop_quit (self->priv->main_loop);

  G_UNLOCK (epoll_mutex);

  g_thread_join (self->priv->thread);

  self->priv->thread = NULL;

  close (self->priv->epoll_fd);
  self->priv->epoll_fd = 0;
}

/* public methods */

EvdPoll *
evd_poll_new (void)
{
  EvdPoll *self;

  self = g_object_new (EVD_TYPE_POLL, NULL);

  return self;
}

EvdPoll *
evd_poll_get_default (void)
{
  G_LOCK (epoll_mutex);

  if (evd_poll_default == NULL)
    evd_poll_default = evd_poll_new ();
  else
    g_object_ref (evd_poll_default);

  G_UNLOCK (epoll_mutex);

  return evd_poll_default;
}

EvdPollSession *
evd_poll_add (EvdPoll          *self,
              gint              fd,
              GIOCondition      condition,
              guint             priority,
              EvdPollCallback   callback,
              gpointer          user_data,
              GError          **error)
{
  EvdPollSession *session;

  g_return_val_if_fail (EVD_IS_POLL (self), NULL);
  g_return_val_if_fail (fd > 0, NULL);
  g_return_val_if_fail (callback != NULL, NULL);

  G_LOCK (epoll_mutex);

  if (! self->priv->started)
    if (! evd_poll_start (self, error))
      return NULL;

  session = g_slice_new0 (EvdPollSession);
  session->ref_count = 1;

  session->self = self;
  session->fd = fd;
  session->cond_in = condition;
  session->cond_out = 0;

  session->main_context = g_main_context_get_thread_default ();
  if (session->main_context == NULL)
    session->main_context = g_main_context_default ();
  g_main_context_ref (session->main_context);

  session->priority = priority;
  session->callback = callback;
  session->user_data = user_data;
  session->src_id = 0;

  if (! evd_poll_epoll_ctl (self,
                            fd,
                            EPOLL_CTL_ADD,
                            condition,
                            session))
    {
      g_set_error_literal (error,
                           EVD_ERROR,
                           EVD_ERROR_EPOLL,
                           "Failed to add file descriptor to epoll set");

      evd_poll_session_unref (session);
      session = NULL;
    }
  else
    {
      evd_poll_session_ref (session);
    }

  G_UNLOCK (epoll_mutex);

  return session;
}

gboolean
evd_poll_mod (EvdPoll         *self,
              EvdPollSession  *session,
              GIOCondition     condition,
              guint            priority,
              GError         **error)
{
  gboolean result = TRUE;

  g_return_val_if_fail (EVD_IS_POLL (self), FALSE);
  g_return_val_if_fail (session != NULL, FALSE);

  G_LOCK (epoll_mutex);

  session->priority = priority;

  if (session->cond_in != condition)
    {
      session->cond_in = condition;

      if (! evd_poll_epoll_ctl (self, session->fd, EPOLL_CTL_MOD, condition, session))
        {
          g_set_error_literal (error,
                               EVD_ERROR,
                               EVD_ERROR_EPOLL,
                               "Failed to modify watched conditions in epoll set");

          result = FALSE;
        }
    }

  G_UNLOCK (epoll_mutex);

  return result;
}

gboolean
evd_poll_del (EvdPoll         *self,
              EvdPollSession  *session,
              GError         **error)
{
  gboolean result;
  gint i;

  g_return_val_if_fail (EVD_IS_POLL (self), FALSE);
  g_return_val_if_fail (session != NULL, FALSE);

  G_LOCK (epoll_mutex);

  if (! G_TRYLOCK (interrupt_mutex))
    {
      evd_poll_interrupt_epoll_wait (self);

      G_LOCK (interrupt_mutex);
    }

  for (i = 0; i < self->priv->nr_events; i++)
    if (self->priv->events[i].data.ptr == session)
      self->priv->events[i].data.ptr = NULL;

  if (session->src_id != 0)
    {
      g_source_remove (session->src_id);
      session->src_id = -1;
      evd_poll_session_unref (session);
    }
  session->callback = NULL;

  if (evd_poll_epoll_ctl (self, session->fd, EPOLL_CTL_DEL, 0, NULL))
    {
      evd_poll_session_unref (session);
      result = TRUE;
    }
  else
    {
      g_set_error_literal (error,
                           EVD_ERROR,
                           EVD_ERROR_EPOLL,
                           "Failed to delete file descriptor from epoll set");

      result = FALSE;
    }

  evd_poll_session_unref (session);

  G_UNLOCK (interrupt_mutex);
  G_UNLOCK (epoll_mutex);

  return result;
}
