/*
 * evd-tls-common.h
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

#ifndef __EVD_TLS_COMMON_H__
#define __EVD_TLS_COMMON_H__

#include <glib.h>
#include <gio/gio.h>
#include <gnutls/gnutls.h>

G_BEGIN_DECLS

/* TLS mode (client vs. server) */
typedef enum
{
  EVD_TLS_MODE_CLIENT = GNUTLS_CLIENT,
  EVD_TLS_MODE_SERVER = GNUTLS_SERVER
} EvdTlsMode;

typedef enum
{
  EVD_TLS_CERTIFICATE_TYPE_UNKNOWN = GNUTLS_CRT_UNKNOWN,
  EVD_TLS_CERTIFICATE_TYPE_X509    = GNUTLS_CRT_X509,
  EVD_TLS_CERTIFICATE_TYPE_OPENPGP = GNUTLS_CRT_OPENPGP
} EvdTlsCertificateType;

typedef enum
{
  EVD_TLS_VERIFY_STATE_OK               = 0,
  EVD_TLS_VERIFY_STATE_NO_CERT          = 1 << 0,
  EVD_TLS_VERIFY_STATE_INVALID          = 1 << 1,
  EVD_TLS_VERIFY_STATE_REVOKED          = 1 << 2,
  EVD_TLS_VERIFY_STATE_SIGNER_NOT_FOUND = 1 << 3,
  EVD_TLS_VERIFY_STATE_SIGNER_NOT_CA    = 1 << 4,
  EVD_TLS_VERIFY_STATE_INSECURE_ALG     = 1 << 5,
  EVD_TLS_VERIFY_STATE_EXPIRED          = 1 << 6,
  EVD_TLS_VERIFY_STATE_NOT_ACTIVE       = 1 << 7
} EvdTlsVerifyState;

gboolean evd_tls_init                      (GError **error);
void     evd_tls_deinit                    (void);

void     evd_tls_free_certificates         (GList *certificates);

void     evd_tls_generate_dh_params        (guint                bit_length,
                                            gboolean             regenerate,
                                            GAsyncReadyCallback  callback,
                                            GCancellable        *cancellable,
                                            gpointer             user_data);
gpointer evd_tls_generate_dh_params_finish (GAsyncResult  *result,
                                            GError       **error);

G_END_DECLS

#endif /* __EVD_TLS_COMMON_H__ */
