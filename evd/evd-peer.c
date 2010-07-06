/*
 * evd-peer.c
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

#include "evd-peer.h"

G_DEFINE_TYPE (EvdPeer, evd_peer, G_TYPE_OBJECT)

#define EVD_PEER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                   EVD_TYPE_PEER, \
                                   EvdPeerPrivate))

/* private data */
struct _EvdPeerPrivate
{
  gchar *id;

  GQueue *backlog;
};

/* signals */
enum
{
  SIGNAL_LAST
};

//static guint evd_peer_signals[SIGNAL_LAST] = { 0 };

/* properties */
enum
{
  PROP_0,
  PROP_ID
};

static void     evd_peer_class_init         (EvdPeerClass *class);
static void     evd_peer_init               (EvdPeer *self);

static void     evd_peer_finalize           (GObject *obj);
static void     evd_peer_dispose            (GObject *obj);

static void     evd_peer_set_property       (GObject      *obj,
                                             guint         prop_id,
                                             const GValue *value,
                                             GParamSpec   *pspec);
static void     evd_peer_get_property       (GObject    *obj,
                                             guint       prop_id,
                                             GValue     *value,
                                             GParamSpec *pspec);

static void     evd_peer_backlog_free_frame (gpointer data, gpointer user_data);

static void
evd_peer_class_init (EvdPeerClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_peer_dispose;
  obj_class->finalize = evd_peer_finalize;
  obj_class->get_property = evd_peer_get_property;
  obj_class->set_property = evd_peer_set_property;

  g_object_class_install_property (obj_class, PROP_ID,
                                   g_param_spec_string ("id",
                                                        "Peer's UUID",
                                                        "A string representing the UUID of the peer",
                                                        NULL,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  /* add private structure */
  g_type_class_add_private (obj_class, sizeof (EvdPeerPrivate));
}

static void
evd_peer_init (EvdPeer *self)
{
  EvdPeerPrivate *priv;

  priv = EVD_PEER_GET_PRIVATE (self);
  self->priv = priv;

  priv->backlog = g_queue_new ();
}

static void
evd_peer_dispose (GObject *obj)
{
  G_OBJECT_CLASS (evd_peer_parent_class)->dispose (obj);
}

static void
evd_peer_finalize (GObject *obj)
{
  EvdPeer *self = EVD_PEER (obj);

  g_queue_foreach (self->priv->backlog,
                   evd_peer_backlog_free_frame,
                   NULL);
  g_queue_free (self->priv->backlog);

  g_free (self->priv->id);

  G_OBJECT_CLASS (evd_peer_parent_class)->finalize (obj);
}

static void
evd_peer_set_property (GObject      *obj,
                       guint         prop_id,
                       const GValue *value,
                       GParamSpec   *pspec)
{
  EvdPeer *self;

  self = EVD_PEER (obj);

  switch (prop_id)
    {
    case PROP_ID:
      self->priv->id = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_peer_get_property (GObject    *obj,
                       guint       prop_id,
                       GValue     *value,
                       GParamSpec *pspec)
{
  EvdPeer *self;

  self = EVD_PEER (obj);

  switch (prop_id)
    {
    case PROP_ID:
      g_value_set_string (value, evd_peer_get_id (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_peer_backlog_free_frame (gpointer data, gpointer user_data)
{
  g_string_free ((GString *) data, TRUE);
}

/* public methods */

EvdPeer *
evd_peer_new (gchar *id)
{
  EvdPeer *self;

  g_return_val_if_fail (id != NULL, NULL);

  self = g_object_new (EVD_TYPE_PEER,
                       "id", id,
                       NULL);

  return self;
}

const gchar *
evd_peer_get_id (EvdPeer *self)
{
  g_return_val_if_fail (EVD_IS_PEER (self), NULL);

  return self->priv->id;
}

gboolean
evd_peer_backlog_push_frame (EvdPeer      *self,
                             const gchar  *frame,
                             gsize         size,
                             GError      **error)
{
  g_return_val_if_fail (EVD_IS_PEER (self), FALSE);
  g_return_val_if_fail (frame != NULL, FALSE);

  /* TODO: check backlog limits here */

  g_queue_push_tail (self->priv->backlog, g_string_new_len (frame, size));

  return TRUE;
}

gboolean
evd_peer_backlog_unshift_frame (EvdPeer      *self,
                                const gchar  *frame,
                                gsize         size,
                                GError      **error)
{
  g_return_val_if_fail (EVD_IS_PEER (self), FALSE);
  g_return_val_if_fail (frame != NULL, FALSE);

  /* TODO: check backlog limits here */

  g_queue_push_head (self->priv->backlog, g_string_new_len (frame, size));

  return TRUE;
}

gchar *
evd_peer_backlog_pop_frame (EvdPeer *self,
                            gsize   *size)
{
  GString *frame;

  g_return_val_if_fail (EVD_IS_PEER (self), NULL);

  frame = (GString *) g_queue_pop_head (self->priv->backlog);

  if (frame != NULL)
    {
      gchar *str;

      if (size != NULL)
        *size = frame->len;

      str = frame->str;

      g_string_free (frame, FALSE);

      return str;
    }
  else
    {
      return NULL;
    }
}

guint
evd_peer_backlog_get_length (EvdPeer *self)
{
  g_return_val_if_fail (EVD_IS_PEER (self), 0);

  return g_queue_get_length (self->priv->backlog);
}
