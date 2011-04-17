/*
 * evd-tls-privkey.h
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

#ifndef __EVD_TLS_PRIVKEY_H__
#define __EVD_TLS_PRIVKEY_H__

#include <glib-object.h>

#include <evd-pki-privkey.h>

G_BEGIN_DECLS

typedef struct _EvdTlsPrivkey EvdTlsPrivkey;
typedef struct _EvdTlsPrivkeyClass EvdTlsPrivkeyClass;
typedef struct _EvdTlsPrivkeyPrivate EvdTlsPrivkeyPrivate;

struct _EvdTlsPrivkey
{
  GObject parent;

  EvdTlsPrivkeyPrivate *priv;
};

struct _EvdTlsPrivkeyClass
{
  GObjectClass parent_class;
};

#define EVD_TYPE_TLS_PRIVKEY           (evd_tls_privkey_get_type ())
#define EVD_TLS_PRIVKEY(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_TLS_PRIVKEY, EvdTlsPrivkey))
#define EVD_TLS_PRIVKEY_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_TLS_PRIVKEY, EvdTlsPrivkeyClass))
#define EVD_IS_TLS_PRIVKEY(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_TLS_PRIVKEY))
#define EVD_IS_TLS_PRIVKEY_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_TLS_PRIVKEY))
#define EVD_TLS_PRIVKEY_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_TLS_PRIVKEY, EvdTlsPrivkeyClass))


GType              evd_tls_privkey_get_type                (void) G_GNUC_CONST;

EvdTlsPrivkey *    evd_tls_privkey_new                     (void);

gboolean           evd_tls_privkey_import                  (EvdTlsPrivkey  *self,
                                                            const gchar        *raw_data,
                                                            gsize               len,
                                                            GError            **error);

void               evd_tls_privkey_import_from_file        (EvdTlsPrivkey       *self,
                                                            const gchar         *filename,
                                                            GCancellable        *cancellable,
                                                            GAsyncReadyCallback  callback,
                                                            gpointer             user_data);
gboolean           evd_tls_privkey_import_from_file_finish (EvdTlsPrivkey  *self,
                                                            GAsyncResult   *result,
                                                            GError        **error);

gpointer           evd_tls_privkey_get_native              (EvdTlsPrivkey *self);

EvdPkiPrivkey *    evd_tls_privkey_get_pki_key             (EvdTlsPrivkey  *self,
                                                            GError        **error);

G_END_DECLS

#endif /* __EVD_TLS_PRIVKEY_H__ */
