/*
 * evd-tls-session.h
 *
 * EventDance project - An event distribution framework (http://eventdance.org)
 *
 * Copyright (C) 2009, Igalia S.L.
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
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#ifndef __EVD_TLS_SESSION_H__
#define __EVD_TLS_SESSION_H__

#include <glib-object.h>
#include <gnutls/gnutls.h>

#include "evd-tls-common.h"
#include "evd-tls-credentials.h"

G_BEGIN_DECLS

typedef struct _EvdTlsSession EvdTlsSession;
typedef struct _EvdTlsSessionClass EvdTlsSessionClass;
typedef struct _EvdTlsSessionPrivate EvdTlsSessionPrivate;

typedef gssize (* EvdTlsSessionPullFunc) (EvdTlsSession *self,
                                          gchar         *buf,
                                          gsize          size,
                                          gpointer       user_data);
typedef gssize (* EvdTlsSessionPushFunc) (EvdTlsSession *self,
                                          const gchar   *buf,
                                          gsize          size,
                                          gpointer       user_data);

struct _EvdTlsSession
{
  GObject parent;

  /* private structure */
  EvdTlsSessionPrivate *priv;
};

struct _EvdTlsSessionClass
{
  GObjectClass parent_class;
};

#define EVD_TYPE_TLS_SESSION           (evd_tls_session_get_type ())
#define EVD_TLS_SESSION(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_TLS_SESSION, EvdTlsSession))
#define EVD_TLS_SESSION_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_TLS_SESSION, EvdTlsSessionClass))
#define EVD_IS_TLS_SESSION(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_TLS_SESSION))
#define EVD_IS_TLS_SESSION_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_TLS_SESSION))
#define EVD_TLS_SESSION_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_TLS_SESSION, EvdTlsSessionClass))


GType              evd_tls_session_get_type            (void) G_GNUC_CONST;

EvdTlsSession     *evd_tls_session_new                 (void);

void               evd_tls_session_set_credentials     (EvdTlsSession     *self,
                                                        EvdTlsCredentials *credentials);

/**
 * evd_tls_session_get_credentials:
 *
 * Returns: (transfer none): The #EvdTlsCredentials object of this session
 */
EvdTlsCredentials *evd_tls_session_get_credentials     (EvdTlsSession *self);

void               evd_tls_session_set_transport_funcs (EvdTlsSession         *self,
                                                        EvdTlsSessionPullFunc  pull_func,
                                                        EvdTlsSessionPushFunc  push_func,
                                                        gpointer               user_data);

gboolean           evd_tls_session_handshake           (EvdTlsSession   *self,
                                                        GError         **error);

gssize             evd_tls_session_read                (EvdTlsSession  *self,
                                                        gchar          *buffer,
                                                        gsize           size,
                                                        GError        **error);
gssize             evd_tls_session_write               (EvdTlsSession  *self,
                                                        const gchar    *buffer,
                                                        gsize           size,
                                                        GError        **error);

GIOCondition       evd_tls_session_get_direction       (EvdTlsSession *self);

gboolean           evd_tls_session_close               (EvdTlsSession  *self,
                                                        GError        **error);
gboolean           evd_tls_session_shutdown_write      (EvdTlsSession  *self,
                                                        GError         **error);

void               evd_tls_session_copy_properties     (EvdTlsSession *self,
                                                        EvdTlsSession *target);

/**
 * evd_tls_session_get_peer_certificates:
 * @self:
 * @error:
 *
 * Returns: (transfer full) (element-type Evd.TlsCertificate): The list of certificates
 *          as sent by the peer.
 */
GList             *evd_tls_session_get_peer_certificates (EvdTlsSession  *self,
                                                          GError        **error);

G_END_DECLS

#endif /* __EVD_TLS_SESSION_H__ */
