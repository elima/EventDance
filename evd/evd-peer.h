/*
 * evd-peer.h
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

#ifndef __EVD_PEER_H__
#define __EVD_PEER_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _EvdPeer EvdPeer;
typedef struct _EvdPeerClass EvdPeerClass;
typedef struct _EvdPeerPrivate EvdPeerPrivate;

struct _EvdPeer
{
  GObject parent;

  EvdPeerPrivate *priv;
};

struct _EvdPeerClass
{
  GObjectClass parent_class;

  /* virtual methods */

  /* signal prototypes */
};

#define EVD_TYPE_PEER           (evd_peer_get_type ())
#define EVD_PEER(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_PEER, EvdPeer))
#define EVD_PEER_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_PEER, EvdPeerClass))
#define EVD_IS_PEER(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_PEER))
#define EVD_IS_PEER_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_PEER))
#define EVD_PEER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_PEER, EvdPeerClass))


GType             evd_peer_get_type                (void) G_GNUC_CONST;

EvdPeer *         evd_peer_new                     (gchar *id);

const gchar *     evd_peer_get_id                  (EvdPeer *self);

gboolean          evd_peer_backlog_push_frame      (EvdPeer      *self,
                                                    const gchar  *frame,
                                                    gsize         size,
                                                    GError      **error);
gboolean          evd_peer_backlog_unshift_frame   (EvdPeer      *self,
                                                    const gchar  *frame,
                                                    gsize         size,
                                                    GError      **error);
gchar *           evd_peer_backlog_pop_frame       (EvdPeer *self,
                                                    gsize   *size);
guint             evd_peer_backlog_get_length      (EvdPeer *self);

guint             evd_peer_get_idle_time           (EvdPeer *self);
void              evd_peer_touch                   (EvdPeer *self);

G_END_DECLS

#endif /* __EVD_PEER_H__ */
