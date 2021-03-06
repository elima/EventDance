/*
 * evd-transport.h
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2009-2014, Igalia S.L.
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

#ifndef __EVD_TRANSPORT_H__
#define __EVD_TRANSPORT_H__

#if !defined (__EVD_H_INSIDE__) && !defined (EVD_COMPILATION)
#error "Only <evd.h> can be included directly."
#endif

#include <glib-object.h>
#include <gio/gio.h>

#include "evd-utils.h"
#include "evd-peer-manager.h"
#include "evd-peer.h"

G_BEGIN_DECLS

typedef struct _EvdTransport               EvdTransport;
typedef struct _EvdTransportInterface      EvdTransportInterface;

struct _EvdTransportInterface
{
  GTypeInterface parent_iface;

  /* virtual methods */
  gboolean  (* send)                 (EvdTransport    *self,
                                      EvdPeer         *peer,
                                      const gchar     *buffer,
                                      gsize            size,
                                      EvdMessageType   type,
                                      GError         **error);
  void      (* notify_receive)       (EvdTransport *self,
                                      EvdPeer      *peer);
  void      (* receive)              (EvdTransport *self,
                                      EvdPeer      *peer,
                                      const gchar  *buffer,
                                      gsize         size);

  void      (* notify_new_peer)      (EvdTransport *self, EvdPeer *peer);
  EvdPeer * (* create_new_peer)      (EvdTransport *self);

  void      (* notify_peer_closed)   (EvdTransport *self,
                                      EvdPeer      *peer,
                                      gboolean      gracefully);
  void      (* peer_closed)          (EvdTransport *self,
                                      EvdPeer      *peer,
                                      gboolean      gracefully);

  guint     (* notify_validate_peer) (EvdTransport *self, EvdPeer *peer);

  gboolean  (* peer_is_connected)    (EvdTransport *self, EvdPeer *peer);

  gboolean  (* accept_peer)          (EvdTransport *self, EvdPeer *peer);
  gboolean  (* reject_peer)          (EvdTransport *self, EvdPeer *peer);

  void      (* open)                 (EvdTransport       *self,
                                      const gchar        *address,
                                      GSimpleAsyncResult *async_result,
                                      GCancellable       *cancellable);

  /* signals */
  void (* signal_receive)        (EvdTransport *self,
                                  EvdPeer      *peer,
                                  gpointer      user_data);

  void  (* signal_new_peer)      (EvdTransport *self,
                                  EvdPeer      *peer,
                                  gpointer      user_data);
  void  (* signal_peer_closed)   (EvdTransport *self,
                                  EvdPeer      *peer,
                                  gboolean      gracefully,
                                  gpointer      user_data);

  guint (* signal_validate_peer) (EvdTransport *self,
                                  EvdPeer      *peer,
                                  gpointer      user_data);

  /* members */
  EvdPeerManager *peer_manager;

  /* padding for future expansion */
  void (* _padding_0_) (void);
  void (* _padding_1_) (void);
  void (* _padding_2_) (void);
  void (* _padding_3_) (void);
  void (* _padding_4_) (void);
  void (* _padding_5_) (void);
  void (* _padding_6_) (void);
  void (* _padding_7_) (void);
};

#define EVD_TYPE_TRANSPORT                 (evd_transport_get_type ())
#define EVD_TRANSPORT(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_TRANSPORT, EvdTransport))
#define EVD_IS_TRANSPORT(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_TRANSPORT))
#define EVD_TRANSPORT_GET_INTERFACE(inst)  (G_TYPE_INSTANCE_GET_INTERFACE ((inst), EVD_TYPE_TRANSPORT, EvdTransportInterface))

GType           evd_transport_get_type                      (void);

gboolean        evd_transport_send                          (EvdTransport  *self,
                                                             EvdPeer       *peer,
                                                             const gchar   *buffer,
                                                             gsize          size,
                                                             GError       **error);
gboolean        evd_transport_send_text                     (EvdTransport  *self,
                                                             EvdPeer       *peer,
                                                             const gchar   *text,
                                                             GError       **error);
const gchar    *evd_transport_receive                       (EvdTransport *self,
                                                             EvdPeer      *peer,
                                                             gsize        *size);
const gchar    *evd_transport_receive_text                  (EvdTransport *self,
                                                             EvdPeer      *peer);

gboolean        evd_transport_peer_is_connected             (EvdTransport *self,
                                                             EvdPeer       *peer);

void            evd_transport_close_peer                    (EvdTransport  *self,
                                                             EvdPeer       *peer,
                                                             gboolean       gracefully,
                                                             GError       **error);

EvdPeer        *evd_transport_create_new_peer               (EvdTransport *self);
EvdPeer        *evd_transport_lookup_peer                   (EvdTransport *self,
                                                             const gchar  *peer_id);

gboolean        evd_transport_accept_peer                   (EvdTransport *self,
                                                             EvdPeer      *peer);
gboolean        evd_transport_reject_peer                   (EvdTransport *self,
                                                             EvdPeer      *peer);

EvdPeerManager *evd_transport_get_peer_manager              (EvdTransport *self);
void            evd_transport_set_peer_manager              (EvdTransport   *self,
                                                             EvdPeerManager *peer_manager);

void            evd_transport_open                          (EvdTransport        *self,
                                                             const gchar         *address,
                                                             GCancellable        *cancellable,
                                                             GAsyncReadyCallback  callback,
                                                             gpointer             user_data);
gboolean        evd_transport_open_finish                   (EvdTransport  *self,
                                                             GAsyncResult  *result,
                                                             GError       **error);

/* defines here get_transport() method of EvdPeer to avoid cyclic dependency */
EvdTransport *  evd_peer_get_transport                      (EvdPeer *self);

G_END_DECLS

#endif /* __EVD_TRANSPORT_H__ */
