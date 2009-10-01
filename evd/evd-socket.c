/*
 * evd-socket.c
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

#include <sys/types.h>
#include <sys/socket.h>

#include "evd-socket-manager.h"
#include "evd-socket.h"

#define DOMAIN_QUARK_STRING "org.eventdance.glib.socket"

G_DEFINE_TYPE (EvdSocket, evd_socket, G_TYPE_SOCKET)

#define EVD_SOCKET_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
	                             EVD_TYPE_SOCKET, \
                                     EvdSocketPrivate))

/* private data */
struct _EvdSocketPrivate
{
  EvdSocketState status;
  GMainContext *context;
};

/* signals */
enum
{
  CLOSE,
  CONNECTED,
  LAST_SIGNAL
};

static guint evd_socket_signals [LAST_SIGNAL] = { 0 };

/* properties */
enum
{
  PROP_0,
  PROP_EXAMPLE
};

static void evd_socket_class_init (EvdSocketClass *class);
static void evd_socket_init (EvdSocket *self);
static void evd_socket_finalize (GObject *obj);
static void evd_socket_dispose (GObject *obj);
static void evd_socket_set_property (GObject      *obj,
				     guint         prop_id,
				     const GValue *value,
				     GParamSpec   *pspec);
static void evd_socket_get_property (GObject    *obj,
				     guint       prop_id,
				     GValue     *value,
				     GParamSpec *pspec);
static gboolean evd_socket_events_handler (gpointer data);

static gboolean evd_socket_watch (EvdSocket *self, GError **error);
static gboolean evd_socket_unwatch (EvdSocket *self, GError **error);

static void
evd_socket_class_init (EvdSocketClass *class)
{
  GObjectClass *obj_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_socket_dispose;
  obj_class->finalize = evd_socket_finalize;
  obj_class->get_property = evd_socket_get_property;
  obj_class->set_property = evd_socket_set_property;

  class->event_handler = NULL;

  /* install signals */
  evd_socket_signals[CLOSE] =
    g_signal_new ("close",
          G_TYPE_FROM_CLASS (obj_class),
          G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
          G_STRUCT_OFFSET (EvdSocketClass, close),
          NULL, NULL,
          g_cclosure_marshal_VOID__BOXED,
          G_TYPE_NONE, 1,
          G_TYPE_POINTER);

  evd_socket_signals[CONNECTED] =
    g_signal_new ("connected",
          G_TYPE_FROM_CLASS (obj_class),
          G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
          G_STRUCT_OFFSET (EvdSocketClass, connected),
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
  g_type_class_add_private (obj_class, sizeof (EvdSocketPrivate));

  evd_socket_manager_set_callback (evd_socket_events_handler);
}

static void
evd_socket_init (EvdSocket *self)
{
  EvdSocketPrivate *priv;

  priv = EVD_SOCKET_GET_PRIVATE (self);
  self->priv = priv;

  /* initialize private members */
  priv->status = EVD_SOCKET_CLOSED;
  priv->context = g_main_context_get_thread_default ();
}

static void
evd_socket_dispose (GObject *obj)
{
  G_OBJECT_CLASS (evd_socket_parent_class)->dispose (obj);
}

static void
evd_socket_finalize (GObject *obj)
{
  G_OBJECT_CLASS (evd_socket_parent_class)->finalize (obj);
}

static void
evd_socket_set_property (GObject      *obj,
			 guint         prop_id,
			 const GValue *value,
			 GParamSpec   *pspec)
{
  EvdSocket *self;

  self = EVD_SOCKET (obj);

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
evd_socket_get_property (GObject    *obj,
			 guint       prop_id,
			 GValue     *value,
			 GParamSpec *pspec)
{
  EvdSocket *self;

  self = EVD_SOCKET (obj);

  switch (prop_id)
    {
    case PROP_EXAMPLE:
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static gboolean
evd_socket_events_handler (gpointer data)
{
  EvdSocketClass *class;
  GList *list = data;
  GList *node;
  GError *error = NULL;

  node = list;
  while (node != NULL)
    {
      EvdSocketEvent *event = (EvdSocketEvent *) node->data;
      EvdSocket *socket;
      GIOCondition condition;

      socket = event->socket;
      condition = event->condition;

      class = EVD_SOCKET_GET_CLASS (socket);
      if (class->event_handler != NULL)
	class->event_handler (socket, condition);
      else
	{
	  if (socket->priv->status == EVD_SOCKET_LISTENING)
	    {
	      EvdSocket *client;

	      if ((client = evd_socket_accept (socket, &error)) != NULL)
		{
		  /* TODO: allow external function to decide whether to
		           accept/refuse the new connection */

		  /* TODO: fire 'new-connection' signal */

		  g_debug ("Incoming connection accepted");
		}
	    }
	  else
	    {
	      //	      g_debug ("event!");

	      if (condition & G_IO_HUP)
		{
		  evd_socket_close (socket, &error);
		  continue;
		}

	      if (condition & G_IO_OUT)
		{
		  if (socket->priv->status == EVD_SOCKET_CONNECTING)
		    {
		      socket->priv->status = EVD_SOCKET_CONNECTED;

		      /* emit 'connected' signal */
		      g_signal_emit (socket, evd_socket_signals[CONNECTED], 0);
		    }
		}
	    }
	}

      node = node->next;
    }

  evd_socket_manager_free_event_list (list);

  return FALSE;
}

static gboolean
evd_socket_watch (EvdSocket *self, GError **error)
{
  return evd_socket_manager_add_socket (self, error);
}

static gboolean
evd_socket_unwatch (EvdSocket *self, GError **error)
{
  return evd_socket_manager_del_socket (self, error);
}

static gboolean
evd_socket_initable_init (EvdSocket *self, GError **error)
{
  if (g_initable_init (G_INITABLE (self), NULL, error))
    {
      g_socket_set_blocking (G_SOCKET (self), FALSE);
      g_socket_set_keepalive (G_SOCKET (self), TRUE);

      return TRUE;
    }

  return FALSE;
}

/* public methods */

EvdSocket *
evd_socket_new (GSocketFamily     family,
		GSocketType       type,
		GSocketProtocol   protocol,
		GError          **error)
{
  EvdSocket *self;

  self = g_object_new (EVD_TYPE_SOCKET,
		       "family", family,
		       "type", type,
		       "protocol", protocol,
		       NULL);

  if (evd_socket_initable_init (self, error))
    return self;
  else
    return NULL;
}

EvdSocket *
evd_socket_new_from_fd (gint     fd,
			GError **error)
{
  EvdSocket *self;

  g_return_val_if_fail (fd > 0, NULL);

  self = g_object_new (EVD_TYPE_SOCKET,
		       "fd", fd,
		       NULL);

  if (evd_socket_initable_init (self, error))
    return self;
  else
    return NULL;
}

GMainContext *
evd_socket_get_context (EvdSocket *self)
{
  g_return_val_if_fail (EVD_IS_SOCKET (self), NULL);

  return self->priv->context;
}

gboolean
evd_socket_close (EvdSocket *self, GError **error)
{
  gboolean result = TRUE;

  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);

  if (! evd_socket_unwatch (self, error))
    result = FALSE;

  self->priv->status = EVD_SOCKET_CLOSED;

  if (! g_socket_is_closed (G_SOCKET (self)))
    if (! g_socket_close (G_SOCKET (self), error))
      result = FALSE;

  /* fire 'close' signal */
  g_signal_emit (self, evd_socket_signals[CLOSE], 0);

  return result;
}

gboolean
evd_socket_listen (EvdSocket *self, GError **error)
{
  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);

  if (g_socket_listen (G_SOCKET (self), error))
    if (evd_socket_watch (self, error))
      {
	self->priv->status = EVD_SOCKET_LISTENING;
	return TRUE;
      }

  return FALSE;
}

EvdSocket *
evd_socket_accept (EvdSocket *self, GError **error)
{
  gint fd;
  gint client_fd;
  EvdSocket *client = NULL;

  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);

  fd = g_socket_get_fd (G_SOCKET (self));

  client_fd = accept (fd, NULL, 0);

  if ( (client = evd_socket_new_from_fd (client_fd, error)) != NULL)
    if (evd_socket_watch (client, error))
      {
	client->priv->status = EVD_SOCKET_CONNECTED;
	return client;
      }

  return NULL;
}

gboolean
evd_socket_connect (EvdSocket       *self,
		    GSocketAddress  *address,
		    GCancellable    *cancellable,
		    GError         **error)
{
  g_socket_connect (G_SOCKET (self),
		    address,
		    cancellable,
		    error);

  if (evd_socket_watch (self, error))
    {
      self->priv->status = EVD_SOCKET_CONNECTING;
      return TRUE;
    }

  return FALSE;
}
