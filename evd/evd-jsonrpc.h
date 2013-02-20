/*
 * evd-jsonrpc.h
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

#ifndef __EVD_JSONRPC_H__
#define __EVD_JSONRPC_H__

#include <glib-object.h>
#include <gio/gio.h>
#include <json-glib/json-glib.h>

#include "evd-transport.h"

G_BEGIN_DECLS

typedef struct _EvdJsonrpc EvdJsonrpc;
typedef struct _EvdJsonrpcClass EvdJsonrpcClass;
typedef struct _EvdJsonrpcPrivate EvdJsonrpcPrivate;

typedef gboolean (* EvdJsonrpcTransportWriteCb) (EvdJsonrpc  *self,
                                                 const gchar *buffer,
                                                 gsize        size,
                                                 gpointer     context,
                                                 gpointer     user_data);

typedef void (* EvdJsonrpcTransportSendCb) (EvdJsonrpc  *self,
                                            const gchar *message,
                                            gpointer     context,
                                            guint        invocation_id,
                                            gpointer     user_data);

/**
 * EvdJsonrpcMethodCallCb:
 * @context: (type GObject):
 **/
typedef void (* EvdJsonrpcMethodCallCb) (EvdJsonrpc  *self,
                                         const gchar *method_name,
                                         JsonNode    *params,
                                         guint        invocation_id,
                                         gpointer     context,
                                         gpointer     user_data);

/**
 * EvdJsonrpcNotificationCb:
 * @context: (type GObject):
 **/
typedef void (* EvdJsonrpcNotificationCb) (EvdJsonrpc  *self,
                                           const gchar *notification_name,
                                           JsonNode    *params,
                                           gpointer     context,
                                           gpointer     user_data);

struct _EvdJsonrpc
{
  GObject parent;

  EvdJsonrpcPrivate *priv;
};

struct _EvdJsonrpcClass
{
  GObjectClass parent_class;
};

#define EVD_TYPE_JSONRPC           (evd_jsonrpc_get_type ())
#define EVD_JSONRPC(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_JSONRPC, EvdJsonrpc))
#define EVD_JSONRPC_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_JSONRPC, EvdJsonrpcClass))
#define EVD_IS_JSONRPC(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_JSONRPC))
#define EVD_IS_JSONRPC_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_JSONRPC))
#define EVD_JSONRPC_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_JSONRPC, EvdJsonrpcClass))


GType                evd_jsonrpc_get_type                     (void) G_GNUC_CONST;

EvdJsonrpc *         evd_jsonrpc_new                          (void);

void                 evd_jsonrpc_transport_set_send_callback  (EvdJsonrpc                *self,
                                                               EvdJsonrpcTransportSendCb  callback,
                                                               gpointer                   user_data,
                                                               GDestroyNotify             user_data_free_func);

void                 evd_jsonrpc_transport_set_write_callback (EvdJsonrpc                 *self,
                                                               EvdJsonrpcTransportWriteCb  callback,
                                                               gpointer                    user_data) G_GNUC_DEPRECATED_FOR (evd_jsonrpc_transport_set_send_callback);
gboolean             evd_jsonrpc_transport_read               (EvdJsonrpc   *self,
                                                               const gchar  *buffer,
                                                               gsize         size,
                                                               gpointer      context,
                                                               GError      **error) G_GNUC_DEPRECATED_FOR (evd_jsonrpc_transport_receive);

gboolean             evd_jsonrpc_transport_receive            (EvdJsonrpc    *self,
                                                               const gchar   *message,
                                                               gpointer       context,
                                                               guint          invocation_id,
                                                               GError       **error);

void                 evd_jsonrpc_transport_error              (EvdJsonrpc *self,
                                                               guint       invocation_id,
                                                               GError     *error);

void                 evd_jsonrpc_call_method                  (EvdJsonrpc          *self,
                                                               const gchar         *method_name,
                                                               JsonNode            *params,
                                                               gpointer             context,
                                                               GCancellable        *cancellable,
                                                               GAsyncReadyCallback  callback,
                                                               gpointer             user_data);
gboolean             evd_jsonrpc_call_method_finish           (EvdJsonrpc    *self,
                                                               GAsyncResult  *result,
                                                               JsonNode     **result_json,
                                                               JsonNode     **error_json,
                                                               GError       **error);

void                 evd_jsonrpc_set_callbacks                (EvdJsonrpc               *self,
                                                               EvdJsonrpcMethodCallCb    method_call_cb,
                                                               EvdJsonrpcNotificationCb  notification_cb,
                                                               gpointer                  user_data,
                                                               GDestroyNotify            user_data_free_func);

gboolean             evd_jsonrpc_respond                      (EvdJsonrpc  *self,
                                                               guint        invocation_id,
                                                               JsonNode    *result,
                                                               gpointer     context,
                                                               GError     **error);
gboolean             evd_jsonrpc_respond_error                (EvdJsonrpc  *self,
                                                               guint        invocation_id,
                                                               JsonNode    *json_error,
                                                               gpointer     context,
                                                               GError     **error);
gboolean             evd_jsonrpc_respond_from_error           (EvdJsonrpc  *self,
                                                               guint        invocation_id,
                                                               GError      *result_error,
                                                               gpointer     context,
                                                               GError     **error);

void                 evd_jsonrpc_use_transport                (EvdJsonrpc   *self,
                                                               EvdTransport *transport);
void                 evd_jsonrpc_unuse_transport              (EvdJsonrpc   *self,
                                                               EvdTransport *transport);

gboolean             evd_jsonrpc_send_notification            (EvdJsonrpc   *self,
                                                               const gchar  *notification_name,
                                                               JsonNode     *params,
                                                               gpointer      context,
                                                               GError      **error);

G_END_DECLS

#endif /* __EVD_JSONRPC_H__ */
