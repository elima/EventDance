/*
 * evd-transport.h
 *
 * EventDance project - An event distribution framework (http://eventdance.org)
 *
 * Copyright (C) 2009, 2010 Igalia S.L.
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

#ifndef __EVD_TRANSPORT_H__
#define __EVD_TRANSPORT_H__

#include <glib-object.h>

#include <evd-peer.h>

G_BEGIN_DECLS

typedef struct _EvdTransport               EvdTransport;
typedef struct _EvdTransportInterface      EvdTransportInterface;

struct _EvdTransportInterface
{
  GTypeInterface parent_iface;

  /* virtual methods */
  gssize   (* send)              (EvdTransport *self,
                                  EvdPeer       *peer,
                                  const gchar   *buffer,
                                  gsize          size,
                                  GError        **error);
  void     (* notify_receive)    (EvdTransport *self,
                                  EvdPeer       *peer);
  void     (* receive)           (EvdTransport *self,
                                  EvdPeer       *peer,
                                  const gchar   *buffer,
                                  gsize          size);

  gboolean (* peer_is_connected) (EvdTransport *self, EvdPeer *peer);

  /* signals */
  void (* signal_receive) (EvdTransport *self, gpointer user_data);
};

#define EVD_TYPE_TRANSPORT                 (evd_transport_get_type ())
#define EVD_TRANSPORT(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_TRANSPORT, EvdTransport))
#define EVD_IS_TRANSPORT(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_TRANSPORT))
#define EVD_TRANSPORT_GET_INTERFACE(inst)  (G_TYPE_INSTANCE_GET_INTERFACE ((inst), EVD_TYPE_TRANSPORT, EvdTransportInterface))

GType           evd_transport_get_type                      (void);

gboolean        evd_transport_send                          (EvdTransport *self,
                                                             EvdPeer       *peer,
                                                             const gchar   *buffer,
                                                             gsize          size,
                                                             GError        **error);
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

G_END_DECLS

#endif /* __EVD_TRANSPORT_H__ */
