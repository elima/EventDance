/*
 * evd-pki-privkey.h
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

#ifndef __EVD_PKI_PRIVKEY_H__
#define __EVD_PKI_PRIVKEY_H__

#include <glib-object.h>
#include <gio/gio.h>
#include <gnutls/abstract.h>

#include <evd-pki-common.h>

G_BEGIN_DECLS

typedef struct _EvdPkiPrivkey EvdPkiPrivkey;
typedef struct _EvdPkiPrivkeyClass EvdPkiPrivkeyClass;
typedef struct _EvdPkiPrivkeyPrivate EvdPkiPrivkeyPrivate;

struct _EvdPkiPrivkey
{
  GObject parent;

  EvdPkiPrivkeyPrivate *priv;
};

struct _EvdPkiPrivkeyClass
{
  GObjectClass parent_class;
};

#define EVD_TYPE_PKI_PRIVKEY           (evd_pki_privkey_get_type ())
#define EVD_PKI_PRIVKEY(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_PKI_PRIVKEY, EvdPkiPrivkey))
#define EVD_PKI_PRIVKEY_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_PKI_PRIVKEY, EvdPkiPrivkeyClass))
#define EVD_IS_PKI_PRIVKEY(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_PKI_PRIVKEY))
#define EVD_IS_PKI_PRIVKEY_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_PKI_PRIVKEY))
#define EVD_PKI_PRIVKEY_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_PKI_PRIVKEY, EvdPkiPrivkeyClass))


GType              evd_pki_privkey_get_type                (void) G_GNUC_CONST;

EvdPkiPrivkey *    evd_pki_privkey_new                     (void);

EvdPkiKeyType      evd_pki_privkey_get_key_type            (EvdPkiPrivkey  *self);

gboolean           evd_pki_privkey_import_native           (EvdPkiPrivkey     *self,
                                                            gnutls_privkey_t   privkey,
                                                            GError           **error);

void               evd_pki_privkey_decrypt                 (EvdPkiPrivkey       *self,
                                                            const gchar         *data,
                                                            gsize                size,
                                                            GCancellable        *cancellable,
                                                            GAsyncReadyCallback  callback,
                                                            gpointer             user_data);
gchar *            evd_pki_privkey_decrypt_finish          (EvdPkiPrivkey  *self,
                                                            GAsyncResult   *result,
                                                            gsize          *size,
                                                            GError        **error);

void               evd_pki_privkey_sign_data               (EvdPkiPrivkey       *self,
                                                            const gchar         *data,
                                                            gsize                data_size,
                                                            GCancellable        *cancellable,
                                                            GAsyncReadyCallback  callback,
                                                            gpointer             user_data);
gchar *            evd_pki_privkey_sign_data_finish        (EvdPkiPrivkey  *self,
                                                            GAsyncResult   *result,
                                                            gsize          *size,
                                                            GError        **error);

void               evd_pki_privkey_generate                (EvdPkiPrivkey        *self,
                                                            EvdPkiKeyType         key_type,
                                                            guint                 bits,
                                                            GCancellable         *cancellable,
                                                            GAsyncReadyCallback   callback,
                                                            gpointer              user_data);
gboolean           evd_pki_privkey_generate_finish         (EvdPkiPrivkey  *self,
                                                            GAsyncResult   *result,
                                                            GError        **error);

G_END_DECLS

#endif /* __EVD_PKI_PRIVKEY_H__ */
