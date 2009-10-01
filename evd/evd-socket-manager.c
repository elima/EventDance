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

  GSourceFunc callback;
  gboolean started;
};

/* signals */
enum
{
  SIGNAL_EXAMPLE,
  LAST_SIGNAL
};

static guint evd_socket_manager_signals [LAST_SIGNAL] = { 0 };

/* properties */
enum
{
  PROP_0,
  PROP_EXAMPLE
};

/* we are a singleton object */
static EvdSocketManager *evd_socket_manager_singleton = NULL;

static void evd_socket_manager_class_init (EvdSocketManagerClass *class);
static void evd_socket_manager_init (EvdSocketManager *self);
static void evd_socket_manager_finalize (GObject *obj);
static void evd_socket_manager_dispose (GObject *obj);
static void evd_socket_manager_set_property (GObject      *obj,
					     guint         prop_id,
					     const GValue *value,
					     GParamSpec   *pspec);
static void evd_socket_manager_get_property (GObject     *obj,
					      guint       prop_id,
					      GValue     *value,
					      GParamSpec *pspec);

static gpointer evd_socket_manager_idle (gpointer data);


static void
evd_socket_manager_class_init (EvdSocketManagerClass *class)
{
  GObjectClass *obj_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_socket_manager_dispose;
  obj_class->finalize = evd_socket_manager_finalize;
  obj_class->get_property = evd_socket_manager_get_property;
  obj_class->set_property = evd_socket_manager_set_property;

  /* install signals */
  evd_socket_manager_signals[SIGNAL_EXAMPLE] =
    g_signal_new ("signal-example",
          G_TYPE_FROM_CLASS (obj_class),
          G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
          G_STRUCT_OFFSET (EvdSocketManagerClass, signal_example),
          NULL, NULL,
          g_cclosure_marshal_VOID__BOXED,
          G_TYPE_NONE, 1,
          G_TYPE_POINTER);

  /* install properties */
  g_object_class_install_property (obj_class,
                                   PROP_EXAMPLE,
                                   g_param_spec_int ("example",
                                   "An example property",
                                   "An example property to gobject boilerplate",
                                   0,
                                   256,
                                   0,
                                   G_PARAM_READWRITE));

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
  G_OBJECT_CLASS (evd_socket_manager_parent_class)->dispose (obj);
}

static void
evd_socket_manager_finalize (GObject *obj)
{
  g_debug ("Socket-manager destroyed!");

  G_OBJECT_CLASS (evd_socket_manager_parent_class)->finalize (obj);
}

static void
evd_socket_manager_set_property (GObject      *obj,
				 guint         prop_id,
				 const GValue *value,
				 GParamSpec   *pspec)
{
  EvdSocketManager *self;

  self = EVD_SOCKET_MANAGER (obj);

  switch (prop_id)
    {
    case PROP_EXAMPLE:
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_socket_manager_get_property (GObject    *obj,
				 guint       prop_id,
				 GValue     *value,
				 GParamSpec *pspec)
{
  EvdSocketManager *self;

  self = EVD_SOCKET_MANAGER (obj);

  switch (prop_id)
    {
    case PROP_EXAMPLE:
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
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
  GList *list;

  nfds = epoll_wait (self->priv->epoll_fd, events, DEFAULT_MAX_SOCKETS, 0);
  if (nfds == -1)
    {
      /* TODO: Handle error */
      g_error ("ERROR: epoll_pwait");
    }

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
      list = g_hash_table_lookup (contexts, context);
      if (list == NULL)
	{
	  list = g_list_prepend (list, (gpointer) msg);

	  g_hash_table_insert (contexts,
			       (gpointer) context,
			       (gpointer) list);
	}
    }

  /* dispatch the lot of events to each context */
  g_hash_table_iter_init (&iter, contexts);
  while (g_hash_table_iter_next (&iter,
				 (gpointer *) &context,
				 (gpointer *) &list))
    {
      GSource *src;

      src = g_idle_source_new ();
      g_source_set_callback (src,
			     self->priv->callback,
			     (gpointer) list,
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

/* public methods */

EvdSocketManager *
evd_socket_manager_get (void)
{
  if (evd_socket_manager_singleton == NULL)
    evd_socket_manager_singleton = evd_socket_manager_new ();

  return evd_socket_manager_singleton;
}

void
evd_socket_manager_set_callback (GSourceFunc callback)
{
  EvdSocketManager *self;

  self = evd_socket_manager_get ();
  self->priv->callback = callback;
}

gboolean
evd_socket_manager_add_socket (EvdSocket  *socket,
			       GError    **error)
{
  EvdSocketManager *self;
  struct epoll_event ev;
  gint fd;

  g_assert (EVD_IS_SOCKET (socket));

  self = evd_socket_manager_get ();

  if (! self->priv->started)
    evd_socket_manager_start (self, error);

  fd = g_socket_get_fd (G_SOCKET (socket));

  ev.events = EPOLLET | EPOLLIN | EPOLLOUT | EPOLLRDHUP;
  ev.data.fd = fd;
  ev.data.ptr = (void *) socket;
  if (epoll_ctl (self->priv->epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1)
    {
      *error = g_error_new (g_quark_from_string (DOMAIN_QUARK_STRING),
			    EVD_SOCKET_ERR_EPOLL_ADD,
			    "Failed to add socket file descriptor to epoll set");
      return FALSE;
    }

  g_object_ref (self);

  return TRUE;
}

gboolean
evd_socket_manager_del_socket (EvdSocket  *socket,
			       GError    **error)
{
  EvdSocketManager *self;
  gint fd;

  g_assert (G_IS_SOCKET (socket));

  self = evd_socket_manager_get ();

  fd = g_socket_get_fd (G_SOCKET (socket));

  if (epoll_ctl (self->priv->epoll_fd, EPOLL_CTL_DEL, fd, NULL) == -1)
    {
      *error = g_error_new (g_quark_from_string (DOMAIN_QUARK_STRING),
			    EVD_SOCKET_ERR_EPOLL_ADD,
			    "Failed to remove socket file descriptor from epoll set");
      return FALSE;
    }

  g_object_unref (self);

  return TRUE;
}

void
evd_socket_manager_free_event_list (GList *list)
{
  GList *node = list;
  while (node != NULL)
    {
      g_free (node->data);
      node = node->next;
    }
  g_list_free (list);
}
