/*
 * evd-connection.h
 *
 * EventDance - An event distribution framework (http://eventdance.org)
 *
 * Copyright (C) 2009/2010, Igalia S.L.
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
 *
 */

#ifndef __EVD_CONNECTION_H__
#define __EVD_CONNECTION_H__

#include <glib-object.h>
#include <gio/gio.h>

#include <evd-socket.h>
#include <evd-tls-session.h>
#include <evd-connection-group.h>

G_BEGIN_DECLS

typedef struct _EvdConnection EvdConnection;
typedef struct _EvdConnectionClass EvdConnectionClass;
typedef struct _EvdConnectionPrivate EvdConnectionPrivate;

struct _EvdConnection
{
  GIOStream parent;

  EvdConnectionPrivate *priv;
};

struct _EvdConnectionClass
{
  GIOStreamClass parent_class;

  /* signal prototypes */
  void (* close) (EvdConnection *self);
};

#define EVD_TYPE_CONNECTION           (evd_connection_get_type ())
#define EVD_CONNECTION(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_CONNECTION, EvdConnection))
#define EVD_CONNECTION_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_CONNECTION, EvdConnectionClass))
#define EVD_IS_CONNECTION(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_CONNECTION))
#define EVD_IS_CONNECTION_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_CONNECTION))
#define EVD_CONNECTION_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_CONNECTION, EvdConnectionClass))


GType              evd_connection_get_type             (void) G_GNUC_CONST;

EvdConnection     *evd_connection_new                  (EvdSocket *socket);

void               evd_connection_set_socket           (EvdConnection *self,
                                                        EvdSocket     *socket);
EvdSocket         *evd_connection_get_socket           (EvdConnection *self);

EvdTlsSession     *evd_connection_get_tls_session      (EvdConnection *self);

void               evd_connection_starttls_async       (EvdConnection       *self,
                                                        EvdTlsMode           mode,
                                                        GCancellable        *cancellable,
                                                        GAsyncReadyCallback  callback,
                                                        gpointer             user_data);
gboolean           evd_connection_starttls_finish      (EvdConnection  *self,
                                                        GAsyncResult   *result,
                                                        GError        **error);

gboolean           evd_connection_get_tls_active       (EvdConnection *self);

gboolean           evd_connection_is_connected         (EvdConnection *self);

gint               evd_connection_get_priority         (EvdConnection *self);

gboolean           evd_connection_close_protected      (EvdConnection  *self,
                                                        GCancellable   *cancellable,
                                                        GError        **error);
gboolean           evd_connection_set_group            (EvdConnection      *self,
                                                        EvdConnectionGroup *group);

G_END_DECLS

#endif /* __EVD_CONNECTION_H__ */
