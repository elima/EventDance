/*
 * evd-ipc-mechanism.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2013, Igalia S.L.
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

#include "evd-ipc-mechanism.h"

G_DEFINE_ABSTRACT_TYPE (EvdIpcMechanism, evd_ipc_mechanism, G_TYPE_OBJECT)

#define EVD_IPC_MECHANISM_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                            EVD_TYPE_IPC_MECHANISM, \
                                            EvdIpcMechanismPrivate))

struct _EvdIpcMechanismPrivate
{
  GList *transports;
};

static void     evd_ipc_mechanism_class_init      (EvdIpcMechanismClass *class);
static void     evd_ipc_mechanism_init            (EvdIpcMechanism *self);

static void     evd_ipc_mechanism_finalize        (GObject *obj);

static void     transport_on_new_peer             (EvdTransport *transport,
                                                   EvdPeer      *peer,
                                                   gpointer      user_data);
static void     transport_on_receive              (EvdTransport *transport,
                                                   EvdPeer      *peer,
                                                   gpointer      user_data);

static void     transport_on_destroyed            (gpointer  user_data,
                                                   GObject  *where_the_object_was);

static void
evd_ipc_mechanism_class_init (EvdIpcMechanismClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);

  obj_class->finalize = evd_ipc_mechanism_finalize;

  g_type_class_add_private (obj_class, sizeof (EvdIpcMechanismPrivate));
}

static void
evd_ipc_mechanism_init (EvdIpcMechanism *self)
{
  EvdIpcMechanismPrivate *priv;

  priv = EVD_IPC_MECHANISM_GET_PRIVATE (self);
  self->priv = priv;

  priv->transports = NULL;
}

static void
evd_ipc_mechanism_finalize (GObject *obj)
{
  EvdIpcMechanism *self = EVD_IPC_MECHANISM (obj);
  GList *node;

  node = self->priv->transports;
  while (node != NULL)
    {
      if (EVD_IS_TRANSPORT (node->data))
        {
          EvdTransport *transport;

          transport = EVD_TRANSPORT (node->data);

          g_signal_handlers_disconnect_by_func (transport,
                                                transport_on_new_peer,
                                                self);
          g_signal_handlers_disconnect_by_func (transport,
                                                transport_on_receive,
                                                self);

          g_object_weak_unref (G_OBJECT (transport),
                               transport_on_destroyed,
                               self);
        }

      node = node->next;
    }
  g_list_free (self->priv->transports);

  G_OBJECT_CLASS (evd_ipc_mechanism_parent_class)->finalize (obj);
}

static void
transport_on_new_peer (EvdTransport *transport,
                       EvdPeer      *peer,
                       gpointer      user_data)
{
  EvdIpcMechanism *self = EVD_IPC_MECHANISM (user_data);
  EvdIpcMechanismClass *class;

  class = EVD_IPC_MECHANISM_GET_CLASS (self);
  if (class->transport_new_peer != NULL)
    class->transport_new_peer (self, transport, peer);
}

static void
transport_on_receive (EvdTransport *transport,
                      EvdPeer      *peer,
                      gpointer      user_data)
{
  EvdIpcMechanism *self = EVD_IPC_MECHANISM (user_data);
  EvdIpcMechanismClass *class;

  class = EVD_IPC_MECHANISM_GET_CLASS (self);
  if (class->transport_receive != NULL)
    {
      const gchar *data;
      gsize size;

      data = evd_transport_receive (transport, peer, &size);

      class->transport_receive (self,
                                transport,
                                peer,
                                (const guchar *) data,
                                size);
    }
}

static void
transport_on_destroyed (gpointer user_data, GObject *where_the_object_was)
{
  EvdIpcMechanism *self = EVD_IPC_MECHANISM (user_data);

  self->priv->transports = g_list_remove (self->priv->transports,
                                          where_the_object_was);
}

/* public methods */

void
evd_ipc_mechanism_use_transport (EvdIpcMechanism *self, EvdTransport *transport)
{
  g_return_if_fail (EVD_IS_IPC_MECHANISM (self));
  g_return_if_fail (EVD_IS_TRANSPORT (transport));

  if (g_list_find (self->priv->transports, transport) != NULL)
    return;

  self->priv->transports = g_list_append (self->priv->transports, transport);

  g_signal_connect (transport,
                    "new-peer",
                    G_CALLBACK (transport_on_new_peer),
                    self);
  g_signal_connect (transport,
                    "receive",
                    G_CALLBACK (transport_on_receive),
                    self);

  g_object_weak_ref (G_OBJECT (transport), transport_on_destroyed, self);
}

void
evd_ipc_mechanism_unuse_transport (EvdIpcMechanism *self,
                                   EvdTransport    *transport)
{
  g_return_if_fail (EVD_IS_IPC_MECHANISM (self));
  g_return_if_fail (EVD_IS_TRANSPORT (transport));

  if (g_list_find (self->priv->transports, transport) == NULL)
    return;

  g_signal_handlers_disconnect_by_func (transport,
                                        transport_on_new_peer,
                                        self);
  g_signal_handlers_disconnect_by_func (transport,
                                        transport_on_receive,
                                        self);

  self->priv->transports = g_list_remove (self->priv->transports, transport);

  g_object_weak_unref (G_OBJECT (transport), transport_on_destroyed, self);
}
