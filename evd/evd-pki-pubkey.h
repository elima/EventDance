/*
 * evd-pki-pubkey.h
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2011-2013, Igalia S.L.
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

#ifndef __EVD_PKI_PUBKEY_H__
#define __EVD_PKI_PUBKEY_H__

#include <glib-object.h>
#include <gio/gio.h>
#include <gnutls/gnutls.h>

#include <evd-pki-common.h>

G_BEGIN_DECLS

typedef struct _EvdPkiPubkey EvdPkiPubkey;
typedef struct _EvdPkiPubkeyClass EvdPkiPubkeyClass;
typedef struct _EvdPkiPubkeyPrivate EvdPkiPubkeyPrivate;

struct _EvdPkiPubkey
{
  GObject parent;

  EvdPkiPubkeyPrivate *priv;
};

struct _EvdPkiPubkeyClass
{
  GObjectClass parent_class;
};

#define EVD_TYPE_PKI_PUBKEY           (evd_pki_pubkey_get_type ())
#define EVD_PKI_PUBKEY(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_PKI_PUBKEY, EvdPkiPubkey))
#define EVD_PKI_PUBKEY_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_PKI_PUBKEY, EvdPkiPubkeyClass))
#define EVD_IS_PKI_PUBKEY(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_PKI_PUBKEY))
#define EVD_IS_PKI_PUBKEY_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_PKI_PUBKEY))
#define EVD_PKI_PUBKEY_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_PKI_PUBKEY, EvdPkiPubkeyClass))


GType              evd_pki_pubkey_get_type                (void) G_GNUC_CONST;

EvdPkiPubkey *     evd_pki_pubkey_new                     (void);

EvdPkiKeyType      evd_pki_pubkey_get_key_type            (EvdPkiPubkey  *self);

gboolean           evd_pki_pubkey_import_native           (EvdPkiPubkey  *self,
                                                           gpointer        pubkey_st,
                                                           GError        **error);

gboolean           evd_pki_pubkey_import                  (EvdPkiPubkey  *self,
                                                           const gchar        *raw_data,
                                                           gsize               len,
                                                           GError            **error);

void               evd_pki_pubkey_encrypt                 (EvdPkiPubkey       *self,
                                                           const gchar         *data,
                                                           gsize                size,
                                                           GCancellable        *cancellable,
                                                           GAsyncReadyCallback  callback,
                                                           gpointer             user_data);
gchar *            evd_pki_pubkey_encrypt_finish          (EvdPkiPubkey  *self,
                                                           GAsyncResult   *result,
                                                           gsize          *size,
                                                           GError        **error);

G_END_DECLS

#endif /* __EVD_PKI_PUBKEY_H__ */
