/*
 * evd-connection-pool.h
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

#ifndef __EVD_CONNECTION_POOL_H__
#define __EVD_CONNECTION_POOL_H__

#include <evd-io-stream-group.h>

#include <evd-connection.h>

G_BEGIN_DECLS

typedef struct _EvdConnectionPool EvdConnectionPool;
typedef struct _EvdConnectionPoolClass EvdConnectionPoolClass;
typedef struct _EvdConnectionPoolPrivate EvdConnectionPoolPrivate;

struct _EvdConnectionPool
{
  EvdIoStreamGroup parent;

  EvdConnectionPoolPrivate *priv;
};

struct _EvdConnectionPoolClass
{
  EvdIoStreamGroupClass parent_class;
};

#define EVD_TYPE_CONNECTION_POOL           (evd_connection_pool_get_type ())
#define EVD_CONNECTION_POOL(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_CONNECTION_POOL, EvdConnectionPool))
#define EVD_CONNECTION_POOL_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_CONNECTION_POOL, EvdConnectionPoolClass))
#define EVD_IS_CONNECTION_POOL(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_CONNECTION_POOL))
#define EVD_IS_CONNECTION_POOL_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_CONNECTION_POOL))
#define EVD_CONNECTION_POOL_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_CONNECTION_POOL, EvdConnectionPoolClass))


GType                  evd_connection_pool_get_type                 (void) G_GNUC_CONST;

EvdConnectionPool     *evd_connection_pool_new                      (const gchar *address,
                                                                     GType        connection_type);

gboolean               evd_connection_pool_has_free_connections     (EvdConnectionPool *self);

void                   evd_connection_pool_get_connection           (EvdConnectionPool   *self,
                                                                     GCancellable        *cancellable,
                                                                     GAsyncReadyCallback  callback,
                                                                     gpointer             user_data);
EvdConnection         *evd_connection_pool_get_connection_finish    (EvdConnectionPool  *self,
                                                                     GAsyncResult       *result,
                                                                     GError            **error);

gboolean               evd_connection_pool_recycle                  (EvdConnectionPool *self,
                                                                     EvdConnection     *conn);

G_END_DECLS

#endif /* __EVD_CONNECTION_POOL_H__ */
