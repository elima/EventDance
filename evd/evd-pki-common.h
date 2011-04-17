/*
 * evd-pki-common.h
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

#ifndef __EVD_PKI_COMMON_H__
#define __EVD_PKI_COMMON_H__

#include <gnutls/gnutls.h>

G_BEGIN_DECLS

/* PKI key algorithms */
typedef enum
{
  EVD_PKI_KEY_TYPE_UNKNOWN = GNUTLS_PK_UNKNOWN,
  EVD_PKI_KEY_TYPE_RSA     = GNUTLS_PK_RSA,
  EVD_PKI_KEY_TYPE_DSA     = GNUTLS_PK_DSA
} EvdPkiKeyType;

G_END_DECLS

#endif /* __EVD_PKI_COMMON_H__ */
