/*
 * evd-websocket-server.h
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2011, Igalia S.L.
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

#ifndef __EVD_WEBSOCKET_SERVER_H__
#define __EVD_WEBSOCKET_SERVER_H__

#include <evd-web-service.h>
#include <evd-http-connection.h>
#include <evd-http-request.h>
#include <evd-peer.h>

G_BEGIN_DECLS

typedef struct _EvdWebsocketServer EvdWebsocketServer;
typedef struct _EvdWebsocketServerClass EvdWebsocketServerClass;
typedef struct _EvdWebsocketServerPrivate EvdWebsocketServerPrivate;

struct _EvdWebsocketServer
{
  EvdWebService parent;

  EvdWebsocketServerPrivate *priv;
};

struct _EvdWebsocketServerClass
{
  EvdWebServiceClass parent_class;

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

#define EVD_TYPE_WEBSOCKET_SERVER           (evd_websocket_server_get_type ())
#define EVD_WEBSOCKET_SERVER(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_WEBSOCKET_SERVER, EvdWebsocketServer))
#define EVD_WEBSOCKET_SERVER_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_WEBSOCKET_SERVER, EvdWebsocketServerClass))
#define EVD_IS_WEBSOCKET_SERVER(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_WEBSOCKET_SERVER))
#define EVD_IS_WEBSOCKET_SERVER_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_WEBSOCKET_SERVER))
#define EVD_WEBSOCKET_SERVER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_WEBSOCKET_SERVER, EvdWebsocketServerClass))


GType                   evd_websocket_server_get_type          (void) G_GNUC_CONST;

EvdWebsocketServer     *evd_websocket_server_new               (void);

void                    evd_websocket_server_set_standalone    (EvdWebsocketServer *self,
                                                                gboolean            standalone);
gboolean                evd_websocket_server_get_standalone    (EvdWebsocketServer *self);

void                    evd_websocket_server_get_validate_peer_arguments (EvdWebsocketServer  *self,
                                                                          EvdPeer             *peer,
                                                                          EvdHttpConnection  **conn,
                                                                          EvdHttpRequest     **request);

G_END_DECLS

#endif /* __EVD_WEBSOCKET_SERVER_H__ */
