/*
 * evd-peer-manager.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2009-2013, Igalia S.L.
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

#include <string.h>

#include "evd-peer-manager.h"

#include "evd-marshal.h"
#include "evd-utils.h"

G_DEFINE_TYPE (EvdPeerManager, evd_peer_manager, G_TYPE_OBJECT)

#define EVD_PEER_MANAGER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                           EVD_TYPE_PEER_MANAGER, \
                                           EvdPeerManagerPrivate))

#define DEFAULT_PEER_CLEANUP_INTERVAL 5 /* seconds */

#define PEER_DATA_KEY "org.eventdance.lib.PeerManager.PEER_DATA"

/* private data */
struct _EvdPeerManagerPrivate
{
  GHashTable *peers;

  GTimer *peer_cleanup_timer;
  guint peer_cleanup_interval;
  guint peer_cleanup_src_id;

  GQueue *removal_list;
};

/* signals */
enum
{
  SIGNAL_NEW_PEER,
  SIGNAL_PEER_CLOSED,
  SIGNAL_LAST
};

static guint evd_peer_manager_signals[SIGNAL_LAST] = { 0 };

static EvdPeerManager *evd_peer_manager_default = NULL;

static void     evd_peer_manager_class_init          (EvdPeerManagerClass *class);
static void     evd_peer_manager_init                (EvdPeerManager *self);

static void     evd_peer_manager_finalize            (GObject *obj);
static void     evd_peer_manager_dispose             (GObject *obj);

static void     evd_peer_manager_close_peer_internal (EvdPeerManager *self,
                                                      EvdPeer        *peer,
                                                      gboolean        gracefully);

static void
evd_peer_manager_class_init (EvdPeerManagerClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_peer_manager_dispose;
  obj_class->finalize = evd_peer_manager_finalize;

  evd_peer_manager_signals[SIGNAL_NEW_PEER] =
    g_signal_new ("new-peer",
                  G_TYPE_FROM_CLASS (obj_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (EvdPeerManagerClass, new_peer),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE,
                  1, EVD_TYPE_PEER);

  evd_peer_manager_signals[SIGNAL_PEER_CLOSED] =
    g_signal_new ("peer-closed",
                  G_TYPE_FROM_CLASS (obj_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (EvdPeerManagerClass, peer_closed),
                  NULL, NULL,
                  evd_marshal_VOID__OBJECT_BOOLEAN,
                  G_TYPE_NONE,
                  2, EVD_TYPE_PEER,
                  G_TYPE_BOOLEAN);

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

  priv->removal_list = g_queue_new ();
}

static void
evd_peer_manager_dispose (GObject *obj)
{
  EvdPeerManager *self = EVD_PEER_MANAGER (obj);

  if (self->priv->peers != NULL)
    {
      while (g_queue_get_length (self->priv->removal_list) > 0)
        {
          EvdPeer *peer;

          peer = EVD_PEER (g_queue_pop_head (self->priv->removal_list));
          evd_peer_manager_close_peer_internal (self, peer, FALSE);
          g_object_unref (peer);
        }
      g_queue_free (self->priv->removal_list);

      g_hash_table_unref (self->priv->peers);
      self->priv->peers = NULL;
    }

  G_OBJECT_CLASS (evd_peer_manager_parent_class)->dispose (obj);
}

static void
evd_peer_manager_finalize (GObject *obj)
{
  EvdPeerManager *self = EVD_PEER_MANAGER (obj);

  g_timer_destroy (self->priv->peer_cleanup_timer);

  if (self->priv->peer_cleanup_src_id != 0)
    g_source_remove (self->priv->peer_cleanup_src_id);

  G_OBJECT_CLASS (evd_peer_manager_parent_class)->finalize (obj);

  if (self == evd_peer_manager_default)
    evd_peer_manager_default = NULL;
}

static void
evd_peer_manager_close_peer_internal (EvdPeerManager *self,
                                      EvdPeer        *peer,
                                      gboolean        gracefully)
{
  evd_peer_close (peer, gracefully);

  g_signal_emit (self,
                 evd_peer_manager_signals[SIGNAL_PEER_CLOSED],
                 0,
                 peer,
                 gracefully,
                 NULL);
}

static gboolean
evd_peer_manager_check_peer (gpointer key,
                             gpointer value,
                             gpointer user_data)
{
  EvdPeerManager *self = EVD_PEER_MANAGER (user_data);
  EvdPeer *peer = EVD_PEER (value);

  if (evd_peer_is_alive (peer))
    {
      return FALSE;
    }
  else
    {
      g_queue_push_tail (self->priv->removal_list, g_object_ref (peer));

      return TRUE;
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

  while (g_queue_get_length (self->priv->removal_list) > 0)
    {
      EvdPeer *peer;

      peer = EVD_PEER (g_queue_pop_head (self->priv->removal_list));

      evd_peer_manager_close_peer_internal (self, peer, FALSE);
      g_object_unref (peer);
    }
}

static gboolean
evd_peer_manager_notify_new_peer (gpointer user_data)
{
  EvdPeerManager *self;
  EvdPeer *peer;

  peer = EVD_PEER (user_data);
  self = EVD_PEER_MANAGER (g_object_get_data (G_OBJECT (peer), PEER_DATA_KEY));

  g_signal_emit (self, evd_peer_manager_signals[SIGNAL_NEW_PEER], 0, peer, NULL);

  g_object_set_data (G_OBJECT (peer), PEER_DATA_KEY, NULL);
  g_object_unref (self);

  return FALSE;
}

static gboolean
evd_peer_manager_cleanup_peers_cb (gpointer user_data)
{
  EvdPeerManager *self = EVD_PEER_MANAGER (user_data);

  self->priv->peer_cleanup_src_id = 0;

  evd_peer_manager_cleanup_peers (self);

  return FALSE;
}

/* public methods */

/**
 * evd_peer_manager_get_default:
 *
 * Returns: (transfer full):
 **/
EvdPeerManager *
evd_peer_manager_get_default (void)
{
  if (evd_peer_manager_default == NULL)
    evd_peer_manager_default = evd_peer_manager_new ();
  else
    g_object_ref (evd_peer_manager_default);

  return evd_peer_manager_default;
}

EvdPeerManager *
evd_peer_manager_new (void)
{
  EvdPeerManager *self;

  self = g_object_new (EVD_TYPE_PEER_MANAGER, NULL);

  return self;
}

void
evd_peer_manager_add_peer (EvdPeerManager *self, EvdPeer *peer)
{
  g_return_if_fail (EVD_IS_PEER_MANAGER (self));
  g_return_if_fail (EVD_IS_PEER (peer));

  g_object_ref (peer);
  g_hash_table_insert (self->priv->peers,
                       g_strdup (evd_peer_get_id (peer)),
                       peer);

  g_object_set_data (G_OBJECT (peer), PEER_DATA_KEY, self);
  g_object_ref (self);

  evd_timeout_add (g_main_context_get_thread_default (),
                   0,
                   G_PRIORITY_DEFAULT,
                   evd_peer_manager_notify_new_peer,
                   peer);

  evd_peer_manager_cleanup_peers (self);
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

  if (id == NULL)
    return NULL;

  peer = EVD_PEER (g_hash_table_lookup (self->priv->peers,
                                        (gconstpointer) id));

  /* trigger a peer cleanup in idle */
  self->priv->peer_cleanup_src_id =
    evd_timeout_add (NULL,
                     0,
                     G_PRIORITY_DEFAULT,
                     evd_peer_manager_cleanup_peers_cb,
                     self);

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

void
evd_peer_manager_close_peer (EvdPeerManager *self,
                             EvdPeer        *peer,
                             gboolean        gracefully)
{
  g_return_if_fail (EVD_IS_PEER_MANAGER (self));
  g_return_if_fail (EVD_IS_PEER (peer));

  if (g_hash_table_remove (self->priv->peers, evd_peer_get_id (peer)))
    evd_peer_manager_close_peer_internal (self, peer, gracefully);
}
