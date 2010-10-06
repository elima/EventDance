/*
 * evd-tls-credentials.h
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

#ifndef __EVD_TLS_CREDENTIALS_H__
#define __EVD_TLS_CREDENTIALS_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _EvdTlsCredentials EvdTlsCredentials;
typedef struct _EvdTlsCredentialsClass EvdTlsCredentialsClass;
typedef struct _EvdTlsCredentialsPrivate EvdTlsCredentialsPrivate;

struct _EvdTlsCredentials
{
  GObject parent;

  EvdTlsCredentialsPrivate *priv;
};

struct _EvdTlsCredentialsClass
{
  GObjectClass parent_class;

  /* signal prototypes */
  void (* ready) (EvdTlsCredentials *self);
};

#define EVD_TYPE_TLS_CREDENTIALS           (evd_tls_credentials_get_type ())
#define EVD_TLS_CREDENTIALS(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_TLS_CREDENTIALS, EvdTlsCredentials))
#define EVD_TLS_CREDENTIALS_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_TLS_CREDENTIALS, EvdTlsCredentialsClass))
#define EVD_IS_TLS_CREDENTIALS(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_TLS_CREDENTIALS))
#define EVD_IS_TLS_CREDENTIALS_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_TLS_CREDENTIALS))
#define EVD_TLS_CREDENTIALS_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_TLS_CREDENTIALS, EvdTlsCredentialsClass))


GType              evd_tls_credentials_get_type        (void) G_GNUC_CONST;

EvdTlsCredentials *evd_tls_credentials_new             (void);


void               evd_tls_credentials_set_cert_file   (EvdTlsCredentials *self,
                                                        const gchar       *cert_file);

void               evd_tls_credentials_set_key_file    (EvdTlsCredentials *self,
                                                        const gchar       *key_file);

void               evd_tls_credentials_set_trust_file  (EvdTlsCredentials *self,
                                                        const gchar       *trust_file);

gboolean           evd_tls_credentials_ready           (EvdTlsCredentials *self);
gboolean           evd_tls_credentials_prepare         (EvdTlsCredentials  *self,
                                                        EvdTlsMode          mode,
                                                        GError            **error);

gboolean           evd_tls_credentials_get_anonymous   (EvdTlsCredentials *self);
gpointer           evd_tls_credentials_get_credentials (EvdTlsCredentials *self);


G_END_DECLS

#endif /* __EVD_TLS_CREDENTIALS_H__ */
