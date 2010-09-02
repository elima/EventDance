/*
 * evd-peer-manager.c
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

#include "evd-peer-manager.h"

#include "evd-utils.h"

G_DEFINE_TYPE (EvdPeerManager, evd_peer_manager, G_TYPE_OBJECT)

#define EVD_PEER_MANAGER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                           EVD_TYPE_PEER_MANAGER, \
                                           EvdPeerManagerPrivate))

#define DEFAULT_PEER_CLEANUP_INTERVAL 10 /* seconds */

/* private data */
struct _EvdPeerManagerPrivate
{
  GHashTable *peers;

  GTimer *peer_cleanup_timer;
  guint peer_cleanup_interval;
};

/* signals */
enum
{
  SIGNAL_LAST
};

//static guint evd_peer_manager_signals[SIGNAL_LAST] = { 0 };

static EvdPeerManager *evd_peer_manager_default = NULL;

static void     evd_peer_manager_class_init         (EvdPeerManagerClass *class);
static void     evd_peer_manager_init               (EvdPeerManager *self);

static void     evd_peer_manager_finalize           (GObject *obj);
static void     evd_peer_manager_dispose            (GObject *obj);

static void
evd_peer_manager_class_init (EvdPeerManagerClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_peer_manager_dispose;
  obj_class->finalize = evd_peer_manager_finalize;

  /* add private structure */
  g_type_class_add_private (obj_class, sizeof (EvdPeerManagerPrivate));
}

static void
evd_peer_manager_init (EvdPeerManager *self)
{
  EvdPeerManagerPrivate *priv;

  priv = EVD_PEER_MANAGER_GET_PRIVATE (self);
  self->priv = priv;

  priv->peers = g_hash_table_new_full (g_str_hash,
                                       g_str_equal,
                                       g_free,
                                       g_object_unref);

  priv->peer_cleanup_timer = g_timer_new ();
  priv->peer_cleanup_interval = DEFAULT_PEER_CLEANUP_INTERVAL;
}

static void
evd_peer_manager_dispose (GObject *obj)
{
  G_OBJECT_CLASS (evd_peer_manager_parent_class)->dispose (obj);
}

static void
evd_peer_manager_finalize (GObject *obj)
{
  EvdPeerManager *self = EVD_PEER_MANAGER (obj);

  g_hash_table_destroy (self->priv->peers);

  g_timer_destroy (self->priv->peer_cleanup_timer);

  G_OBJECT_CLASS (evd_peer_manager_parent_class)->finalize (obj);

  if (self == evd_peer_manager_default)
    evd_peer_manager_default = NULL;
}

static void
evd_peer_manager_destroy_peer (EvdPeerManager *self,
                               EvdPeer        *peer)
{
  g_hash_table_remove (self->priv->peers, evd_peer_get_id (peer));

  g_debug ("peer destroyed");

  /* @TODO: emit signal to notify the world */

  g_object_unref (peer);
}

static gboolean
evd_peer_manager_check_peer (gpointer key,
                             gpointer value,
                             gpointer user_data)
{
  EvdPeerManager *self = EVD_PEER_MANAGER (user_data);
  EvdPeer *peer = EVD_PEER (value);

  if (! evd_peer_is_alive (peer))
    {
      evd_peer_manager_destroy_peer (self, peer);

      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

static void
evd_peer_manager_cleanup_peers (EvdPeerManager *self)
{
  if (g_timer_elapsed (self->priv->peer_cleanup_timer, NULL) <=
      self->priv->peer_cleanup_interval)
    return;

  g_timer_start (self->priv->peer_cleanup_timer);

  g_hash_table_foreach_remove (self->priv->peers,
                               evd_peer_manager_check_peer,
                               self);
}

/* public methods */

EvdPeerManager *
evd_peer_manager_get_default (void)
{
  if (evd_peer_manager_default == NULL)
    return evd_peer_manager_new ();
  else
    return g_object_ref (evd_peer_manager_default);
}

EvdPeerManager *
evd_peer_manager_new (void)
{
  EvdPeerManager *self;

  self = g_object_new (EVD_TYPE_PEER_MANAGER, NULL);

  return self;
}

EvdPeer *
evd_peer_manager_create_new_peer (EvdPeerManager *self,
                                  EvdTransport   *transport)
{
  EvdPeer *peer;
  gchar *id;

  g_return_val_if_fail (EVD_IS_PEER_MANAGER (self), NULL);
  g_return_val_if_fail (EVD_IS_TRANSPORT (transport), NULL);

  id = evd_uuid_new ();

  peer = g_object_new (EVD_TYPE_PEER,
                       "id", id,
                       "transport", transport,
                       NULL);

  g_hash_table_insert (self->priv->peers, id, peer);

  return peer;
}

/**
 * evd_peer_manager_lookup_peer:
 *
 * Returns: (transfer none): The #EvdPeer, or NULL if not found.
 **/
EvdPeer *
evd_peer_manager_lookup_peer (EvdPeerManager *self, const gchar *id)
{
  EvdPeer *peer;

  g_return_val_if_fail (EVD_IS_PEER_MANAGER (self), NULL);
  g_return_val_if_fail (id != NULL, NULL);

  peer = EVD_PEER (g_hash_table_lookup (self->priv->peers,
                                        (gconstpointer) id));

  if (peer != NULL && ! evd_peer_is_alive (peer))
    {
      evd_peer_manager_destroy_peer (self, peer);

      peer = NULL;
    }

  return peer;
}

/**
 * evd_peer_manager_get_all_peers:
 *
 * Returns: (transfer container) (element-type Evd.Peer):
 **/
GList *
evd_peer_manager_get_all_peers (EvdPeerManager *self)
{
  g_return_val_if_fail (EVD_IS_PEER_MANAGER (self), NULL);

  evd_peer_manager_cleanup_peers (self);

  return g_hash_table_get_values (self->priv->peers);
}
