/*
 * evd-tls-certificate.h
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

#ifndef __EVD_TLS_CERTIFICATE_H__
#define __EVD_TLS_CERTIFICATE_H__

#include <glib-object.h>

#include "evd-pki-pubkey.h"

G_BEGIN_DECLS

typedef struct _EvdTlsCertificate EvdTlsCertificate;
typedef struct _EvdTlsCertificateClass EvdTlsCertificateClass;
typedef struct _EvdTlsCertificatePrivate EvdTlsCertificatePrivate;

struct _EvdTlsCertificate
{
  GObject parent;

  EvdTlsCertificatePrivate *priv;
};

struct _EvdTlsCertificateClass
{
  GObjectClass parent_class;
};

#define EVD_TYPE_TLS_CERTIFICATE           (evd_tls_certificate_get_type ())
#define EVD_TLS_CERTIFICATE(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_TLS_CERTIFICATE, EvdTlsCertificate))
#define EVD_TLS_CERTIFICATE_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_TLS_CERTIFICATE, EvdTlsCertificateClass))
#define EVD_IS_TLS_CERTIFICATE(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_TLS_CERTIFICATE))
#define EVD_IS_TLS_CERTIFICATE_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_TLS_CERTIFICATE))
#define EVD_TLS_CERTIFICATE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_TLS_CERTIFICATE, EvdTlsCertificateClass))


GType              evd_tls_certificate_get_type                (void) G_GNUC_CONST;

EvdTlsCertificate *evd_tls_certificate_new                     (void);

gboolean           evd_tls_certificate_import                  (EvdTlsCertificate  *self,
                                                                const gchar        *raw_data,
                                                                gsize               len,
                                                                GError            **error);

gpointer           evd_tls_certificate_get_native              (EvdTlsCertificate *self);
gpointer           evd_tls_certificate_steal_native            (EvdTlsCertificate *self);

gchar             *evd_tls_certificate_get_dn                  (EvdTlsCertificate  *self,
                                                                GError            **error);

time_t             evd_tls_certificate_get_expiration_time     (EvdTlsCertificate  *self,
                                                                GError            **error);

time_t             evd_tls_certificate_get_activation_time     (EvdTlsCertificate  *self,
                                                                GError            **error);

gint               evd_tls_certificate_verify_validity         (EvdTlsCertificate  *self,
                                                                GError            **error);

void               evd_tls_certificate_import_from_file        (EvdTlsCertificate   *self,
                                                                const gchar         *filename,
                                                                GCancellable        *cancellable,
                                                                GAsyncReadyCallback  callback,
                                                                gpointer             user_data);
gboolean           evd_tls_certificate_import_from_file_finish (EvdTlsCertificate  *self,
                                                                GAsyncResult       *result,
                                                                GError            **error);

EvdPkiPubkey *     evd_tls_certificate_get_pki_key             (EvdTlsCertificate  *self,
                                                                GError            **error);

G_END_DECLS

#endif /* __EVD_TLS_CERTIFICATE_H__ */
