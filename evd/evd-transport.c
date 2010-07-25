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

#include <string.h>

#include "evd-transport.h"

#include "evd-utils.h"
#include "evd-error.h"

G_DEFINE_ABSTRACT_TYPE (EvdTransport, evd_transport, EVD_TYPE_SERVICE)

#define EVD_TRANSPORT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                        EVD_TYPE_TRANSPORT, \
                                        EvdTransportPrivate))

#define DEFAULT_PEER_TIMEOUT_INTERVAL  5 /* seconds */
#define DEFAULT_PEER_CLEANUP_INTERVAL 10 /* seconds */

/* private data */
struct _EvdTransportPrivate
{
  GHashTable *peers;

  guint peer_timeout_interval;

  GTimer *peer_cleanup_timer;
  guint peer_cleanup_interval;

  EvdPeer *rcv_peer;
  gchar *rcv_buf;
  gsize rcv_size;
};

/* signals */
enum
{
  SIGNAL_RECEIVE,
  SIGNAL_LAST
};

static guint evd_transport_signals[SIGNAL_LAST] = { 0 };

static void     evd_transport_class_init         (EvdTransportClass *class);
static void     evd_transport_init               (EvdTransport *self);

static void     evd_transport_finalize           (GObject *obj);
static void     evd_transport_dispose            (GObject *obj);

static EvdPeer *evd_transport_create_new_peer    (EvdTransport *self);

static void     evd_transport_receive_internal   (EvdTransport *self,
                                                  EvdPeer      *peer,
                                                  gchar        *buffer,
                                                  gsize         size);

static void
evd_transport_class_init (EvdTransportClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_transport_dispose;
  obj_class->finalize = evd_transport_finalize;

  class->create_new_peer = evd_transport_create_new_peer;
  class->receive = evd_transport_receive_internal;

  evd_transport_signals[SIGNAL_RECEIVE] =
    g_signal_new ("receive",
                  G_TYPE_FROM_CLASS (obj_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (EvdTransportClass, signal_receive),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

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

  priv->peer_timeout_interval = DEFAULT_PEER_TIMEOUT_INTERVAL;

  priv->peer_cleanup_timer = g_timer_new ();
  priv->peer_cleanup_interval = DEFAULT_PEER_CLEANUP_INTERVAL;

  priv->rcv_peer = NULL;
  priv->rcv_buf = NULL;
  priv->rcv_size = 0;
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

  g_timer_destroy (self->priv->peer_cleanup_timer);

  G_OBJECT_CLASS (evd_transport_parent_class)->finalize (obj);
}

static void
evd_transport_destroy_peer (EvdTransport *self,
                            EvdPeer      *peer)
{
  /* @TODO: emit signal to notify the world */
}

static gboolean
evd_transport_check_peer (gpointer key,
                          gpointer value,
                          gpointer user_data)
{
  EvdTransport *self = EVD_TRANSPORT (user_data);
  EvdPeer *peer = EVD_PEER (value);

  if (evd_transport_peer_is_dead (self, peer))
    {
      evd_transport_destroy_peer (self, peer);
      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

static void
evd_transport_cleanup_peers (EvdTransport *self)
{
  if (g_timer_elapsed (self->priv->peer_cleanup_timer, NULL) <=
      self->priv->peer_cleanup_interval)
    return;

  g_timer_start (self->priv->peer_cleanup_timer);

  g_hash_table_foreach_remove (self->priv->peers,
                               evd_transport_check_peer,
                               self);
}

static EvdPeer *
evd_transport_create_new_peer (EvdTransport *self)
{
  EvdPeer *peer;
  gchar *id;

  g_return_val_if_fail (EVD_IS_TRANSPORT (self), NULL);

  id = evd_uuid_new ();

  peer = g_object_new (EVD_TYPE_PEER,
                       "id", id,
                       "transport", self,
                       NULL);

  g_hash_table_insert (self->priv->peers, id, peer);

  return peer;
}

static void
evd_transport_receive_internal (EvdTransport *self,
                                EvdPeer      *peer,
                                gchar        *buffer,
                                gsize         size)
{
  self->priv->rcv_peer = peer;
  self->priv->rcv_buf = buffer;
  self->priv->rcv_size = size;

  g_signal_emit (self, evd_transport_signals[SIGNAL_RECEIVE], 0, NULL);

  self->priv->rcv_peer = NULL;
  self->priv->rcv_buf = NULL;
  self->priv->rcv_size = 0;

  g_free (buffer);
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

  if (peer != NULL && evd_transport_peer_is_dead (self, peer))
    {
      evd_transport_destroy_peer (self, peer);
      g_hash_table_remove (self->priv->peers, id);

      peer = NULL;
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

  evd_transport_cleanup_peers (self);

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

  evd_transport_cleanup_peers (self);

  /* check the peer is one of our peers */
  if (g_hash_table_lookup (self->priv->peers, evd_peer_get_id (peer)) != peer)
    {
      g_set_error_literal (error,
                           EVD_ERROR,
                           EVD_ERROR_REFUSED,
                           "Peer doesn't belong to transport");

      return -1;
    }

  /* check peer is not dead */
  if (evd_transport_peer_is_dead (self, peer))
    {
      evd_transport_destroy_peer (self, peer);

      g_set_error_literal (error,
                           EVD_ERROR,
                           EVD_ERROR_CLOSED,
                           "Peer has been invalidated due to inactivity");

      return -1;
    }

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

gboolean
evd_transport_peer_is_dead (EvdTransport *self,
                            EvdPeer      *peer)
{
  EvdTransportClass *class;

  g_return_val_if_fail (EVD_IS_TRANSPORT (self), FALSE);
  g_return_val_if_fail (EVD_IS_PEER (peer), FALSE);

  if (evd_peer_get_idle_time (peer) <= self->priv->peer_timeout_interval)
    return FALSE;

  class = EVD_TRANSPORT_GET_CLASS (self);
  if (class->peer_is_connected == NULL ||
      class->peer_is_connected (self, peer))
    return FALSE;
  else
    return TRUE;
}

/**
 * evd_transport_receive:
 * @peer: (out) (transfer none):
 * @size: (out):
 *
 * Returns: (transfer none):
 **/
const gchar *
evd_transport_receive (EvdTransport  *self,
                       EvdPeer      **peer,
                       gsize         *size)
{
  g_return_val_if_fail (EVD_IS_TRANSPORT (self), NULL);

  if (peer != NULL)
    *peer = self->priv->rcv_peer;

  if (size != NULL)
    *size = self->priv->rcv_size;

  return self->priv->rcv_buf;
}
