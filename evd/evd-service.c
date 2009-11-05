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
#include "evd-inet-socket.h"

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

  obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_service_dispose;
  obj_class->finalize = evd_service_finalize;

  evd_service_signals[SIGNAL_NEW_CONNECTION] =
    g_signal_new ("new-connection",
                  G_TYPE_FROM_CLASS (obj_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (EvdServiceClass, new_connection),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOXED,
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
  G_OBJECT_CLASS (evd_service_parent_class)->finalize (obj);
}

static void
evd_service_on_new_connection (EvdSocket *listener,
                               EvdSocket *client,
                               gpointer   user_data)
{
  EvdService *self = EVD_SERVICE (user_data);

  g_signal_emit (self,
                 evd_service_signals[SIGNAL_NEW_CONNECTION],
                 0,
                 client, NULL);
}

/* protected methods */

/* public methods */

EvdService *
evd_service_new (void)
{
  EvdService *self;

  self = g_object_new (EVD_TYPE_SERVICE, NULL);

  return self;
}

EvdSocket *
evd_service_add_listener_inet (EvdService   *self,
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
      g_hash_table_insert (self->priv->listeners,
                           (gpointer) socket,
                           (gpointer) socket);

      g_signal_connect (socket,
                        "new-connection",
                        G_CALLBACK (evd_service_on_new_connection),
                        self);

      return EVD_SOCKET (socket);
    }
}
