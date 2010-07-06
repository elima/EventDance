/*
 * evd-transport.c
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

#include "evd-transport.h"

#include "evd-utils.h"
#include "evd-error.h"

G_DEFINE_ABSTRACT_TYPE (EvdTransport, evd_transport, EVD_TYPE_SERVICE)

#define EVD_TRANSPORT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                        EVD_TYPE_TRANSPORT, \
                                        EvdTransportPrivate))

/* private data */
struct _EvdTransportPrivate
{
  GHashTable *peers;
};

/* signals */
enum
{
  SIGNAL_LAST
};

//static guint evd_transport_signals[SIGNAL_LAST] = { 0 };

static void     evd_transport_class_init         (EvdTransportClass *class);
static void     evd_transport_init               (EvdTransport *self);

static void     evd_transport_finalize           (GObject *obj);
static void     evd_transport_dispose            (GObject *obj);

static void
evd_transport_class_init (EvdTransportClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_transport_dispose;
  obj_class->finalize = evd_transport_finalize;

  /* add private structure */
  g_type_class_add_private (obj_class, sizeof (EvdTransportPrivate));
}

static void
evd_transport_init (EvdTransport *self)
{
  EvdTransportPrivate *priv;

  priv = EVD_TRANSPORT_GET_PRIVATE (self);
  self->priv = priv;

  priv->peers = g_hash_table_new_full (g_str_hash,
                                       g_str_equal,
                                       g_free,
                                       g_object_unref);
}

static void
evd_transport_dispose (GObject *obj)
{
  G_OBJECT_CLASS (evd_transport_parent_class)->dispose (obj);
}

static void
evd_transport_finalize (GObject *obj)
{
  EvdTransport *self = EVD_TRANSPORT (obj);

  g_hash_table_destroy (self->priv->peers);

  G_OBJECT_CLASS (evd_transport_parent_class)->finalize (obj);
}

/* protected methods */

EvdPeer *
evd_transport_get_new_peer_protected (EvdTransport *self)
{
  EvdPeer *peer;
  gchar *id;

  g_return_val_if_fail (EVD_IS_TRANSPORT (self), NULL);

  id = evd_uuid_new ();

  peer = g_object_new (EVD_TYPE_PEER, "id", id, NULL);

  g_hash_table_insert (self->priv->peers, id, peer);

  return peer;
}

/* public methods */

EvdTransport *
evd_transport_new (void)
{
  EvdTransport *self;

  self = g_object_new (EVD_TYPE_TRANSPORT, NULL);

  return self;
}

/**
 * evd_transport_lookup_peer:
 *
 * Returns: (transfer none): The #EvdPeer, or NULL if not found.
 **/
EvdPeer *
evd_transport_lookup_peer (EvdTransport *self, const gchar *id)
{
  EvdPeer *peer;

  g_return_val_if_fail (EVD_IS_TRANSPORT (self), NULL);
  g_return_val_if_fail (id != NULL, NULL);

  peer = EVD_PEER (g_hash_table_lookup (self->priv->peers,
                                        (gconstpointer) id));

  if (peer != NULL)
    {
      /* @TODO: check the peer has timed-out */
    }

  return peer;
}

/**
 * evd_transport_get_all_peers:
 *
 * Returns: (transfer container) (element-type Evd.Peer):
 **/
GList *
evd_transport_get_all_peers (EvdTransport *self)
{
  g_return_val_if_fail (EVD_IS_TRANSPORT (self), NULL);

  return g_hash_table_get_values (self->priv->peers);
}

gssize
evd_transport_send (EvdTransport  *self,
                    EvdPeer       *peer,
                    const gchar   *buffer,
                    gsize          size,
                    GError       **error)
{
  EvdTransportClass *class;

  g_return_val_if_fail (EVD_IS_TRANSPORT (self), -1);
  g_return_val_if_fail (EVD_IS_PEER (peer), -1);

  /* check the peer is one of our peers */
  if (g_hash_table_lookup (self->priv->peers, evd_peer_get_id (peer)) != peer)
    {
      g_set_error_literal (error,
                           EVD_ERROR,
                           EVD_ERROR_REFUSED,
                           "Peer doesn't belong to transport");

      return -1;
    }

  /* @TODO: check peer has not timed-out */

  class = EVD_TRANSPORT_GET_CLASS (self);

  /* add frame to peer's backlog if peer cannot send it just right now */
  if (evd_peer_backlog_get_length (peer) > 0 ||
      class->peer_is_connected == NULL ||
      ! class->peer_is_connected (self, peer))
    {
      if (! evd_peer_backlog_push_frame (peer, buffer, size, error))
        return -1;
      else
        return size;
    }

  if (class->send != NULL)
    {
      if (! class->send (self, peer, buffer, size, NULL) &&
          ! evd_peer_backlog_push_frame (peer, buffer, size, error))
        return -1;
      else
        return size;
    }
  else
    {
      g_set_error_literal (error,
                           EVD_ERROR,
                           EVD_ERROR_ABSTRACT,
                           "Cannot call abstract method 'send'");

      return -1;
    }
}
