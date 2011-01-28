/*
 * evd-tls-cipher.h
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2011, Igalia S.L.
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

#ifndef __EVD_TLS_CIPHER_H__
#define __EVD_TLS_CIPHER_H__

#include <glib-object.h>
#include <gio/gio.h>
#include <gcrypt.h>

G_BEGIN_DECLS

typedef struct _EvdTlsCipher EvdTlsCipher;
typedef struct _EvdTlsCipherClass EvdTlsCipherClass;
typedef struct _EvdTlsCipherPrivate EvdTlsCipherPrivate;

struct _EvdTlsCipher
{
  GObject parent;

  EvdTlsCipherPrivate *priv;
};

struct _EvdTlsCipherClass
{
  GObjectClass parent_class;
};

typedef enum
{
  EVD_TLS_CIPHER_ALGO_NONE    = 0,
  EVD_TLS_CIPHER_ALGO_AES128  = GCRY_CIPHER_AES128,
  EVD_TLS_CIPHER_ALGO_AES192  = GCRY_CIPHER_AES192,
  EVD_TLS_CIPHER_ALGO_AES256  = GCRY_CIPHER_AES256,
  EVD_TLS_CIPHER_ALGO_ARCFOUR = GCRY_CIPHER_ARCFOUR,
  EVD_TLS_CIPHER_ALGO_LAST,
} EvdTlsCipherAlgo;

typedef enum
{
  EVD_TLS_CIPHER_MODE_NONE    = 0,
  EVD_TLS_CIPHER_MODE_ECB     = GCRY_CIPHER_MODE_ECB,
  EVD_TLS_CIPHER_MODE_CBC     = GCRY_CIPHER_MODE_CBC,
  EVD_TLS_CIPHER_MODE_STREAM  = GCRY_CIPHER_MODE_STREAM,
  EVD_TLS_CIPHER_MODE_LAST,
} EvdTlsCipherMode;

#define EVD_TYPE_TLS_CIPHER           (evd_tls_cipher_get_type ())
#define EVD_TLS_CIPHER(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_TLS_CIPHER, EvdTlsCipher))
#define EVD_TLS_CIPHER_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_TLS_CIPHER, EvdTlsCipherClass))
#define EVD_IS_TLS_CIPHER(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_TLS_CIPHER))
#define EVD_IS_TLS_CIPHER_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_TLS_CIPHER))
#define EVD_TLS_CIPHER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_TLS_CIPHER, EvdTlsCipherClass))


GType             evd_tls_cipher_get_type                (void) G_GNUC_CONST;

EvdTlsCipher *    evd_tls_cipher_new                     (void);
EvdTlsCipher *    evd_tls_cipher_new_full                (EvdTlsCipherAlgo algorithm,
                                                          EvdTlsCipherMode mode);

void              evd_tls_cipher_encrypt                 (EvdTlsCipher        *self,
                                                          const gchar         *data,
                                                          gsize                data_size,
                                                          const gchar         *key,
                                                          gsize                key_size,
                                                          GCancellable        *cancellable,
                                                          GAsyncReadyCallback  callback,
                                                          gpointer             user_data);
gchar *            evd_tls_cipher_encrypt_finish         (EvdTlsCipher  *self,
                                                          GAsyncResult  *result,
                                                          gsize         *size,
                                                          GError       **error);

void               evd_tls_cipher_decrypt                (EvdTlsCipher        *self,
                                                          const gchar         *data,
                                                          gsize                data_size,
                                                          const gchar         *key,
                                                          gsize                key_size,
                                                          GCancellable        *cancellable,
                                                          GAsyncReadyCallback  callback,
                                                          gpointer             user_data);
gchar *            evd_tls_cipher_decrypt_finish         (EvdTlsCipher  *self,
                                                          GAsyncResult  *result,
                                                          gsize         *size,
                                                          GError       **error);

G_END_DECLS

#endif /* __EVD_TLS_CIPHER_H__ */
