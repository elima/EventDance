/*
 * evd-poll.c
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
 * Lesser General Public License at http://www.gnu.org/licenses/lgpl-3.0.txt
 * for more details.
 */

#include <unistd.h>
#include <sys/epoll.h>

#include "evd-poll.h"

#include "evd-error.h"
#include "evd-utils.h"

#define DEFAULT_MAX_FDS 10000 /* maximum number of file descriptors to poll */

G_DEFINE_TYPE (EvdPoll, evd_poll, G_TYPE_OBJECT)

#define EVD_POLL_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                   EVD_TYPE_POLL, \
                                   EvdPollPrivate))

/* private data */
struct _EvdPollPrivate
{
  gint num_fds;
  gint epoll_fd;
  GThread *thread;
  gboolean started;
  guint max_fds;

  GMainLoop *main_loop;
};

struct _EvdPollSession
{
  EvdPoll *self;
  gint fd;
  GIOCondition cond_in;
  GIOCondition cond_out;
  GMainContext *main_context;
  guint priority;
  EvdPollCallback callback;
  gpointer user_data;
  guint src_id;
};

G_LOCK_DEFINE_STATIC (num_fds);

static EvdPoll *evd_poll_default = NULL;

static void     evd_poll_class_init   (EvdPollClass *class);
static void     evd_poll_init         (EvdPoll *self);
static void     evd_poll_finalize     (GObject *obj);

static void     evd_poll_stop         (EvdPoll *self);

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
  priv->num_fds = 0;

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

  if (self == evd_poll_default)
    evd_poll_default = NULL;
}

static gboolean
evd_poll_callback_wrapper (gpointer user_data)
{
  EvdPollSession *session;

  session = (EvdPollSession *) user_data;

  session->src_id = 0;

  if (session->callback != NULL)
    session->callback (session->self, session->cond_out, session->user_data);

  session->cond_out = 0;

  return FALSE;
}

static gboolean
evd_poll_dispatch (gpointer user_data)
{
  EvdPoll *self = EVD_POLL (user_data);
  static struct epoll_event events[DEFAULT_MAX_FDS];
  gint i;
  gint nfds;

  nfds = epoll_wait (self->priv->epoll_fd,
                     events,
                     self->priv->max_fds,
                     -1);

  G_LOCK (num_fds);
  if (self->priv->num_fds == 0)
    {
      G_UNLOCK (num_fds);
      return TRUE;
    }
  G_UNLOCK (num_fds);

  if (nfds == -1)
    {
      /* TODO: handle error */
      g_warning ("epoll error ocurred");

      return TRUE;
    }

  for (i=0; i < nfds; i++)
    {
      EvdPollSession *session;
      GIOCondition cond = 0;

      session = (EvdPollSession *) events[i].data.ptr;

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
      session->src_id = evd_timeout_add (session->main_context,
                                         0,
                                         session->priority,
                                         evd_poll_callback_wrapper,
                                         session);
    }

  return TRUE;
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

  G_LOCK (num_fds);

  if (op == EPOLL_CTL_DEL)
    {
      result = epoll_ctl (self->priv->epoll_fd, EPOLL_CTL_DEL, fd, NULL) != -1;
      self->priv->num_fds--;
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

      if (result && op == EPOLL_CTL_ADD)
        self->priv->num_fds++;
    }

  G_UNLOCK (num_fds);

  return result;
}

static void
evd_poll_stop (EvdPoll *self)
{
  G_UNLOCK (num_fds);

  self->priv->started = FALSE;

  if (self->priv->main_loop != NULL)
    g_main_loop_quit (self->priv->main_loop);

  /* the only purpose of this is to interrupt the 'epoll_wait'.
     FIXME: Adding fd '0' is just a nasty hack that happens to work.
     Have to figure out a better way to interrupt it. */
  evd_poll_epoll_ctl (self, 0, EPOLL_CTL_ADD, G_IO_OUT, NULL);

  g_thread_join (self->priv->thread);
  self->priv->thread = NULL;

  close (self->priv->epoll_fd);
  self->priv->epoll_fd = 0;

  G_LOCK (num_fds);
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
  if (evd_poll_default == NULL)
    evd_poll_default = evd_poll_new ();
  else
    g_object_ref (evd_poll_default);

  return evd_poll_default;
}

EvdPollSession *
evd_poll_add (EvdPoll          *self,
              gint              fd,
              GIOCondition      condition,
              GMainContext     *main_context,
              guint             priority,
              EvdPollCallback   callback,
              gpointer          user_data,
              GError          **error)
{
  EvdPollSession *session;

  g_return_val_if_fail (EVD_IS_POLL (self), NULL);
  g_return_val_if_fail (fd > 0, NULL);
  g_return_val_if_fail (callback != NULL, NULL);

  if (! self->priv->started)
    if (! evd_poll_start (self, error))
      return NULL;

  session = g_slice_new0 (EvdPollSession);
  session->self = self;
  session->fd = fd;
  session->cond_in = condition;
  session->cond_out = 0;
  session->main_context = main_context;
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

      g_slice_free (EvdPollSession, session);
      session = NULL;
    }

  return session;
}

gboolean
evd_poll_mod (EvdPoll         *self,
              EvdPollSession  *session,
              GIOCondition     condition,
              GError         **error)
{
  g_return_val_if_fail (EVD_IS_POLL (self), FALSE);
  g_return_val_if_fail (session != NULL, FALSE);

  if (condition == session->cond_in)
    return TRUE;

  session->cond_in = condition;

  if (evd_poll_epoll_ctl (self, session->fd, EPOLL_CTL_MOD, condition, session))
    {
      return TRUE;
    }
  else
    {
      g_set_error_literal (error,
                           EVD_ERROR,
                           EVD_ERROR_EPOLL,
                           "Failed to modify watched conditions in epoll set");

      return FALSE;
    }
}

gboolean
evd_poll_del (EvdPoll         *self,
              EvdPollSession  *session,
              GError         **error)
{
  g_return_val_if_fail (EVD_IS_POLL (self), FALSE);
  g_return_val_if_fail (session != NULL, FALSE);

  if (session->src_id != 0)
    {
      g_source_remove (session->src_id);
      session->src_id = 0;
    }

  session->callback = NULL;

  if (evd_poll_epoll_ctl (self, session->fd, EPOLL_CTL_DEL, 0, NULL))
    {
      return TRUE;
    }
  else
    {
      g_set_error_literal (error,
                           EVD_ERROR,
                           EVD_ERROR_EPOLL,
                           "Failed to delete file descriptor from epoll set");

      return FALSE;
    }
}

void
evd_poll_free_session (EvdPollSession *session)
{
  g_return_if_fail (session != NULL);

  g_slice_free (EvdPollSession, session);
}
