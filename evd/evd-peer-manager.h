/*
 * evd-peer-manager.h
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2009/2010, Igalia S.L.
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

#ifndef __EVD_PEER_MANAGER_H__
#define __EVD_PEER_MANAGER_H__

#include <glib-object.h>

#include <evd-peer.h>
#include <evd-transport.h>

G_BEGIN_DECLS

typedef struct _EvdPeerManager EvdPeerManager;
typedef struct _EvdPeerManagerClass EvdPeerManagerClass;
typedef struct _EvdPeerManagerPrivate EvdPeerManagerPrivate;

struct _EvdPeerManager
{
  GObject parent;

  EvdPeerManagerPrivate *priv;
};

struct _EvdPeerManagerClass
{
  GObjectClass parent_class;

  /* signal prototypes */
  void (* new_peer)    (EvdPeerManager *self,
                        EvdPeer        *peer,
                        gpointer        user_data);
  void (* peer_closed) (EvdPeerManager *self,
                        EvdPeer        *peer,
                        gpointer        user_data);
};

#define EVD_TYPE_PEER_MANAGER           (evd_peer_manager_get_type ())
#define EVD_PEER_MANAGER(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_PEER_MANAGER, EvdPeerManager))
#define EVD_PEER_MANAGER_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_PEER_MANAGER, EvdPeerManagerClass))
#define EVD_IS_PEER_MANAGER(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_PEER_MANAGER))
#define EVD_IS_PEER_MANAGER_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_PEER_MANAGER))
#define EVD_PEER_MANAGER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_PEER_MANAGER, EvdPeerManagerClass))


GType               evd_peer_manager_get_type                 (void) G_GNUC_CONST;

EvdPeerManager     *evd_peer_manager_get_default              (void);

EvdPeerManager     *evd_peer_manager_new                      (void);

EvdPeer            *evd_peer_manager_create_new_peer          (EvdPeerManager *self,
                                                               EvdTransport   *transport);

EvdPeer            *evd_peer_manager_lookup_peer              (EvdPeerManager *self,
                                                               const gchar    *id);

GList              *evd_peer_manager_get_all_peers            (EvdPeerManager *self);

G_END_DECLS

#endif /* __EVD_PEER_MANAGER_H__ */
