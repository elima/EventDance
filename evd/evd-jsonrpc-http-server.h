/*
 * evd-jsonrpc-http-server.h
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2012, Igalia S.L.
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

#ifndef __EVD_JSONRPC_HTTP_SERVER_H__
#define __EVD_JSONRPC_HTTP_SERVER_H__

#include <glib-object.h>

#include <evd-web-service.h>
#include <json-glib/json-glib.h>

G_BEGIN_DECLS

typedef struct _EvdJsonrpcHttpServer EvdJsonrpcHttpServer;
typedef struct _EvdJsonrpcHttpServerClass EvdJsonrpcHttpServerClass;
typedef struct _EvdJsonrpcHttpServerPrivate EvdJsonrpcHttpServerPrivate;

typedef void (* EvdJsonrpcHttpServerMethodCallCb) (EvdJsonrpcHttpServer *self,
                                                   const gchar          *method_name,
                                                   JsonNode             *params,
                                                   guint                 invocation_id,
                                                   EvdHttpConnection    *connection,
                                                   EvdHttpRequest       *request,
                                                   gpointer              user_data);

struct _EvdJsonrpcHttpServer
{
  EvdWebService parent;

  EvdJsonrpcHttpServerPrivate *priv;
};

struct _EvdJsonrpcHttpServerClass
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

#define EVD_TYPE_JSONRPC_HTTP_SERVER           (evd_jsonrpc_http_server_get_type ())
#define EVD_JSONRPC_HTTP_SERVER(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_JSONRPC_HTTP_SERVER, EvdJsonrpcHttpServer))
#define EVD_JSONRPC_HTTP_SERVER_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_JSONRPC_HTTP_SERVER, EvdJsonrpcHttpServerClass))
#define EVD_IS_JSONRPC_HTTP_SERVER(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_JSONRPC_HTTP_SERVER))
#define EVD_IS_JSONRPC_HTTP_SERVER_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_JSONRPC_HTTP_SERVER))
#define EVD_JSONRPC_HTTP_SERVER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_JSONRPC_HTTP_SERVER, EvdJsonrpcHttpServerClass))


GType                  evd_jsonrpc_http_server_get_type                 (void) G_GNUC_CONST;

EvdJsonrpcHttpServer * evd_jsonrpc_http_server_new                      (void);

SoupMessageHeaders *   evd_jsonrpc_http_server_get_response_headers     (EvdJsonrpcHttpServer *self);

void                   evd_jsonrpc_http_server_set_method_call_callback (EvdJsonrpcHttpServer             *self,
                                                                         EvdJsonrpcHttpServerMethodCallCb  callback,
                                                                         gpointer                          user_data,
                                                                         GDestroyNotify                    user_data_free_func);

gboolean               evd_jsonrpc_http_server_respond                  (EvdJsonrpcHttpServer  *self,
                                                                         guint                  invocation_id,
                                                                         JsonNode              *result,
                                                                         GError               **error);
gboolean               evd_jsonrpc_http_server_respond_error            (EvdJsonrpcHttpServer  *self,
                                                                         guint                  invocation_id,
                                                                         JsonNode              *json_error,
                                                                         GError               **error);

G_END_DECLS

#endif /* __EVD_JSONRPC_HTTP_SERVER_H__ */
