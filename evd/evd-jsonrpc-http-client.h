/*
 * evd-jsonrpc-http-client.h
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

#ifndef __EVD_JSONRPC_HTTP_CLIENT_H__
#define __EVD_JSONRPC_HTTP_CLIENT_H__

#if !defined (__EVD_H_INSIDE__) && !defined (EVD_COMPILATION)
#error "Only <evd.h> can be included directly."
#endif

#include <glib-object.h>
#include <json-glib/json-glib.h>

#include "evd-connection-pool.h"
#include "evd-http-request.h"

G_BEGIN_DECLS

typedef struct _EvdJsonrpcHttpClient EvdJsonrpcHttpClient;
typedef struct _EvdJsonrpcHttpClientClass EvdJsonrpcHttpClientClass;
typedef struct _EvdJsonrpcHttpClientPrivate EvdJsonrpcHttpClientPrivate;

struct _EvdJsonrpcHttpClient
{
  EvdConnectionPool parent;

  EvdJsonrpcHttpClientPrivate *priv;
};

struct _EvdJsonrpcHttpClientClass
{
  EvdConnectionPoolClass parent_class;

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

#define EVD_TYPE_JSONRPC_HTTP_CLIENT           (evd_jsonrpc_http_client_get_type ())
#define EVD_JSONRPC_HTTP_CLIENT(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_JSONRPC_HTTP_CLIENT, EvdJsonrpcHttpClient))
#define EVD_JSONRPC_HTTP_CLIENT_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_JSONRPC_HTTP_CLIENT, EvdJsonrpcHttpClientClass))
#define EVD_IS_JSONRPC_HTTP_CLIENT(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_JSONRPC_HTTP_CLIENT))
#define EVD_IS_JSONRPC_HTTP_CLIENT_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_JSONRPC_HTTP_CLIENT))
#define EVD_JSONRPC_HTTP_CLIENT_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_JSONRPC_HTTP_CLIENT, EvdJsonrpcHttpClientClass))


GType                  evd_jsonrpc_http_client_get_type                (void) G_GNUC_CONST;

EvdJsonrpcHttpClient * evd_jsonrpc_http_client_new                     (const gchar *url);

void                   evd_jsonrpc_http_client_call_method             (EvdJsonrpcHttpClient *self,
                                                                        const gchar          *method,
                                                                        JsonNode             *params,
                                                                        GCancellable         *cancellable,
                                                                        GAsyncReadyCallback   callback,
                                                                        gpointer              user_data);
gboolean               evd_jsonrpc_http_client_call_method_finish      (EvdJsonrpcHttpClient  *self,
                                                                        GAsyncResult          *result,
                                                                        JsonNode             **json_result,
                                                                        JsonNode             **json_error,
                                                                        GError               **error);

G_END_DECLS

#endif /* __EVD_JSONRPC_HTTP_CLIENT_H__ */
