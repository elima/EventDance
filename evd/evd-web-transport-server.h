/*
 * evd-web-transport-server.h
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

#ifndef __EVD_WEB_TRANSPORT_SERVER_H__
#define __EVD_WEB_TRANSPORT_SERVER_H__

#if !defined (__EVD_H_INSIDE__) && !defined (EVD_COMPILATION)
#error "Only <evd.h> can be included directly."
#endif

#include "evd-web-dir.h"
#include "evd-web-selector.h"
#include "evd-peer.h"

G_BEGIN_DECLS

typedef struct _EvdWebTransportServer EvdWebTransportServer;
typedef struct _EvdWebTransportServerClass EvdWebTransportServerClass;
typedef struct _EvdWebTransportServerPrivate EvdWebTransportServerPrivate;

struct _EvdWebTransportServer
{
  EvdWebDir parent;

  EvdWebTransportServerPrivate *priv;
};

struct _EvdWebTransportServerClass
{
  EvdWebDirClass parent_class;

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

#define EVD_TYPE_WEB_TRANSPORT_SERVER           (evd_web_transport_server_get_type ())
#define EVD_WEB_TRANSPORT_SERVER(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_WEB_TRANSPORT_SERVER, EvdWebTransportServer))
#define EVD_WEB_TRANSPORT_SERVER_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_WEB_TRANSPORT_SERVER, EvdWebTransportServerClass))
#define EVD_IS_WEB_TRANSPORT_SERVER(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_WEB_TRANSPORT_SERVER))
#define EVD_IS_WEB_TRANSPORT_SERVER_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_WEB_TRANSPORT_SERVER))
#define EVD_WEB_TRANSPORT_SERVER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_WEB_TRANSPORT_SERVER, EvdWebTransportServerClass))


GType                   evd_web_transport_server_get_type                    (void) G_GNUC_CONST;

EvdWebTransportServer * evd_web_transport_server_new                         (const gchar *base_path);

void                    evd_web_transport_server_set_selector                (EvdWebTransportServer *self,
                                                                              EvdWebSelector        *selector) G_GNUC_DEPRECATED_FOR (evd_web_transport_server_use_selector);
EvdWebSelector *        evd_web_transport_server_get_selector                (EvdWebTransportServer *self) G_GNUC_DEPRECATED;

void                    evd_web_transport_server_use_selector                (EvdWebTransportServer *self,
                                                                              EvdWebSelector        *selector);
void                    evd_web_transport_server_unuse_selector              (EvdWebTransportServer *self,
                                                                              EvdWebSelector        *selector);

const gchar *           evd_web_transport_server_get_base_path               (EvdWebTransportServer *self);

void                    evd_web_transport_server_set_enable_websocket        (EvdWebTransportServer *self,
                                                                              gboolean               enabled);

void                    evd_web_transport_server_get_validate_peer_arguments (EvdWebTransportServer  *self,
                                                                              EvdPeer                *peer,
                                                                              EvdHttpConnection     **conn,
                                                                              EvdHttpRequest        **request);

G_END_DECLS

#endif /* __EVD_WEB_TRANSPORT_SERVER_H__ */
