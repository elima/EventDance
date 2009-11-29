/*
 * evd-service.c
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

#include "evd-service.h"
#include "evd-service-protected.h"
#include "evd-inet-socket.h"
#include "evd-socket-group-protected.h"

G_DEFINE_TYPE (EvdService, evd_service, EVD_TYPE_SOCKET_GROUP)

#define EVD_SERVICE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                           EVD_TYPE_SERVICE, \
                                           EvdServicePrivate))

/* private data */
struct _EvdServicePrivate
{
  GHashTable *listeners;
};

/* signals */
enum
{
  SIGNAL_NEW_CONNECTION,
  SIGNAL_CLOSE,
  SIGNAL_LAST
};

static guint evd_service_signals[SIGNAL_LAST] = { 0 };

static void     evd_service_class_init         (EvdServiceClass *class);
static void     evd_service_init               (EvdService *self);

static void     evd_service_finalize           (GObject *obj);
static void     evd_service_dispose            (GObject *obj);

static void
evd_service_class_init (EvdServiceClass *class)
{
  GObjectClass *obj_class;
  EvdSocketGroupClass *socket_group_class;

  class->socket_on_close = evd_service_socket_on_close;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_service_dispose;
  obj_class->finalize = evd_service_finalize;

  socket_group_class = EVD_SOCKET_GROUP_CLASS (class);
  socket_group_class->add = evd_service_add_internal;
  socket_group_class->remove = evd_service_remove_internal;

  evd_service_signals[SIGNAL_NEW_CONNECTION] =
    g_signal_new ("new-connection",
                  G_TYPE_FROM_CLASS (obj_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (EvdServiceClass, new_connection),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1,
                  EVD_TYPE_SOCKET);

  evd_service_signals[SIGNAL_CLOSE] =
    g_signal_new ("close",
                  G_TYPE_FROM_CLASS (obj_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (EvdServiceClass, close),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1,
                  EVD_TYPE_SOCKET);

  /* add private structure */
  g_type_class_add_private (obj_class, sizeof (EvdServicePrivate));

}

static void
evd_service_init (EvdService *self)
{
  EvdServicePrivate *priv;

  priv = EVD_SERVICE_GET_PRIVATE (self);
  self->priv = priv;

  /* initialize private members */
  priv->listeners = g_hash_table_new (g_direct_hash, g_direct_equal);
}

static void
evd_service_dispose (GObject *obj)
{
  G_OBJECT_CLASS (evd_service_parent_class)->dispose (obj);
}

static void
evd_service_finalize (GObject *obj)
{
  EvdService *self = EVD_SERVICE (obj);

  while (g_hash_table_size (self->priv->listeners) > 0)
    {
      GHashTableIter iter;
      gpointer key, value;
      EvdSocket *socket;

      g_hash_table_iter_init (&iter, self->priv->listeners);
      g_hash_table_iter_next (&iter, &key, &value);

      socket = EVD_SOCKET (value);

      evd_service_remove_listener (self, socket);
    }

  g_hash_table_unref (self->priv->listeners);

  G_OBJECT_CLASS (evd_service_parent_class)->finalize (obj);
}

static void
evd_service_on_client_close (EvdSocket *socket,
                             gpointer   user_data)
{
  EvdService *self = EVD_SERVICE (user_data);
  EvdServiceClass *class = EVD_SERVICE_GET_CLASS (self);

  if (class->socket_on_close != NULL)
    class->socket_on_close (self, socket);
}

static void
evd_service_on_new_connection (EvdSocket *listener,
                               EvdSocket *client,
                               gpointer   user_data)
{
  EvdService *self = EVD_SERVICE (user_data);

  evd_socket_group_add (EVD_SOCKET_GROUP (self), client);

  g_object_ref (client);
  g_signal_emit (self,
                 evd_service_signals[SIGNAL_NEW_CONNECTION],
                 0,
                 client, NULL);
  g_object_unref (client);
}

static void
evd_service_on_listener_close (EvdSocket *listener,
                               gpointer   user_data)
{
  EvdService *self = EVD_SERVICE (user_data);

  evd_service_remove_listener (self, listener);
}

/* protected methods */

void
evd_service_add_internal (EvdSocketGroup *self, EvdSocket *socket)
{
  g_return_if_fail (EVD_IS_SERVICE (self));
  g_return_if_fail (EVD_IS_SOCKET (socket));

  evd_socket_group_add_internal (self, socket);

  /* connect to the 'close' signal of the socket */
  g_signal_connect (socket,
                    "close",
                    G_CALLBACK (evd_service_on_client_close),
                    (gpointer) self);

  evd_socket_group_socket_on_read_internal (self, socket);
}

gboolean
evd_service_remove_internal (EvdSocketGroup *self, EvdSocket *socket)
{
  g_return_val_if_fail (EVD_IS_SERVICE (self), FALSE);
  g_return_val_if_fail (EVD_IS_SOCKET (socket), FALSE);

  /* disconnect from the 'close' signal of the socket */
  g_signal_handlers_disconnect_by_func (self,
                                        G_CALLBACK (evd_service_on_client_close),
                                        (gpointer) self);

  return evd_socket_group_remove_internal (self, socket);
}

void
evd_service_socket_on_close (EvdService *self,
                             EvdSocket  *socket)
{
  g_signal_emit (self,
                 evd_service_signals[SIGNAL_CLOSE],
                 0,
                 socket, NULL);
}

/* public methods */

EvdService *
evd_service_new (void)
{
  EvdService *self;

  self = g_object_new (EVD_TYPE_SERVICE, NULL);

  return self;
}

void
evd_service_add_listener (EvdService  *self,
                          EvdSocket   *socket)
{
  g_return_if_fail (EVD_IS_SERVICE (self));
  g_return_if_fail (EVD_IS_SOCKET (socket));

  g_object_ref (socket);

  g_hash_table_insert (self->priv->listeners,
                       (gpointer) socket,
                       (gpointer) socket);

  if (evd_socket_get_status (socket) == EVD_SOCKET_LISTENING)
    g_signal_connect (socket,
                      "new-connection",
                      G_CALLBACK (evd_service_on_new_connection),
                      self);
  else
    evd_socket_group_add (EVD_SOCKET_GROUP (self), socket);

  g_signal_connect (socket,
                    "close",
                    G_CALLBACK (evd_service_on_listener_close),
                    self);
}

EvdSocket *
evd_service_listen_inet (EvdService   *self,
                         const gchar  *address,
                         guint         port,
                         GError      **error)
{
  EvdInetSocket *socket = NULL;

  g_return_val_if_fail (EVD_IS_SERVICE (self), NULL);

  socket = evd_inet_socket_new ();

  if (! evd_inet_socket_listen (socket, address, port, error))
    {
      g_object_unref (socket);
      return NULL;
    }
  else
    {
      evd_service_add_listener (self, EVD_SOCKET (socket));

      return EVD_SOCKET (socket);
    }
}

gboolean
evd_service_remove_listener (EvdService *self,
                             EvdSocket  *socket)
{
  g_return_val_if_fail (EVD_IS_SERVICE (self), FALSE);
  g_return_val_if_fail (EVD_IS_SOCKET (socket), FALSE);

  if (g_hash_table_remove (self->priv->listeners,
                           (gconstpointer) socket))
    {
      EvdSocketGroup *group = NULL;

      g_object_get (socket, "group", &group, NULL);
      if (group == EVD_SOCKET_GROUP (self))
        evd_socket_group_remove (EVD_SOCKET_GROUP (self), socket);

      g_object_unref (socket);

      return TRUE;
    }

  return FALSE;
}
