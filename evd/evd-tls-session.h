/*
 * evd-tls-session.h
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

#ifndef __EVD_TLS_SESSION_H__
#define __EVD_TLS_SESSION_H__

#include <glib-object.h>
#include <gio/gio.h>
#include <gnutls/gnutls.h>

#include "evd-tls-common.h"

G_BEGIN_DECLS

typedef struct _EvdTlsSession EvdTlsSession;
typedef struct _EvdTlsSessionClass EvdTlsSessionClass;
typedef struct _EvdTlsSessionPrivate EvdTlsSessionPrivate;

typedef gssize (* EvdTlsSessionPullFunc) (EvdTlsSession  *self,
                                          gchar          *buf,
                                          gsize           size,
                                          gpointer        user_data,
                                          GError        **error);
typedef gssize (* EvdTlsSessionPushFunc) (EvdTlsSession  *self,
                                          const gchar    *buf,
                                          gsize           size,
                                          gpointer        user_data,
                                          GError        **error);

struct _EvdTlsSession
{
  GObject parent;

  EvdTlsSessionPrivate *priv;
};

struct _EvdTlsSessionClass
{
  GObjectClass parent_class;

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

#define EVD_TYPE_TLS_SESSION           (evd_tls_session_get_type ())
#define EVD_TLS_SESSION(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_TLS_SESSION, EvdTlsSession))
#define EVD_TLS_SESSION_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_TLS_SESSION, EvdTlsSessionClass))
#define EVD_IS_TLS_SESSION(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_TLS_SESSION))
#define EVD_IS_TLS_SESSION_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_TLS_SESSION))
#define EVD_TLS_SESSION_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_TLS_SESSION, EvdTlsSessionClass))


GType              evd_tls_session_get_type                (void) G_GNUC_CONST;

EvdTlsSession     *evd_tls_session_new                     (void);

void               evd_tls_session_set_transport_pull_func (EvdTlsSession         *self,
                                                            EvdTlsSessionPullFunc  func,
                                                            gpointer               user_data,
                                                            GDestroyNotify         user_data_free_func);
void               evd_tls_session_set_transport_push_func (EvdTlsSession         *self,
                                                            EvdTlsSessionPushFunc  func,
                                                            gpointer               user_data,
                                                            GDestroyNotify         user_data_free_func);

gint               evd_tls_session_handshake               (EvdTlsSession   *self,
                                                            GError         **error);

gssize             evd_tls_session_read                    (EvdTlsSession  *self,
                                                            gchar          *buffer,
                                                            gsize           size,
                                                            GError        **error);
gssize             evd_tls_session_write                   (EvdTlsSession  *self,
                                                            const gchar    *buffer,
                                                            gsize           size,
                                                            GError        **error);

GIOCondition       evd_tls_session_get_direction           (EvdTlsSession *self);

gboolean           evd_tls_session_close                   (EvdTlsSession  *self,
                                                            GError        **error);
gboolean           evd_tls_session_shutdown_write          (EvdTlsSession  *self,
                                                            GError         **error);

void               evd_tls_session_copy_properties         (EvdTlsSession *self,
                                                            EvdTlsSession *target);

GList             *evd_tls_session_get_peer_certificates   (EvdTlsSession  *self,
                                                            GError        **error);

gint               evd_tls_session_verify_peer             (EvdTlsSession  *self,
                                                            guint           flags,
                                                            GError        **error);

void               evd_tls_session_reset                   (EvdTlsSession *self);

gboolean           evd_tls_session_set_server_name         (EvdTlsSession  *self,
                                                            const gchar    *server_name,
                                                            GError        **error);
const gchar       *evd_tls_session_get_server_name         (EvdTlsSession  *self);

G_END_DECLS

#endif /* __EVD_TLS_SESSION_H__ */
