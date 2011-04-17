/*
 * evd-pki.h
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

#ifndef __EVD_PKI_H__
#define __EVD_PKI_H__

#include <gio/gio.h>

#include <evd-pki-common.h>
#include <evd-pki-privkey.h>
#include <evd-pki-pubkey.h>

G_BEGIN_DECLS

void     evd_pki_generate_key_pair        (EvdPkiKeyType        key_type,
                                           guint                bit_length,
                                           gboolean             fast_but_insecure,
                                           GCancellable        *cancellable,
                                           GAsyncReadyCallback  callback,
                                           gpointer             user_data);
gboolean evd_pki_generate_key_pair_finish (GAsyncResult   *result,
                                           EvdPkiPrivkey **privkey,
                                           EvdPkiPubkey  **pubkey,
                                           GError        **error);

G_END_DECLS

#endif /* __EVD_PKI_H__ */
