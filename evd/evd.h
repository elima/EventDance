/*
 * evd.h
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

#ifndef __EVD_H__
#define __EVD_H__

#define __EVD_H_INSIDE__

#include "evd-error.h"
#include "evd-utils.h"
#include "evd-socket.h"
#include "evd-stream-throttle.h"
#include "evd-buffered-input-stream.h"
#include "evd-buffered-output-stream.h"
#include "evd-throttled-input-stream.h"
#include "evd-throttled-output-stream.h"
#include "evd-service.h"
#include "evd-tls-session.h"
#include "evd-tls-credentials.h"
#include "evd-tls-certificate.h"
#include "evd-tls-privkey.h"
#include "evd-connection.h"
#include "evd-io-stream-group.h"
#include "evd-http-connection.h"
#include "evd-peer.h"
#include "evd-peer-manager.h"
#include "evd-http-request.h"
#include "evd-longpolling-server.h"
#include "evd-websocket-server.h"
#include "evd-websocket-client.h"
#include "evd-connection-pool.h"
#include "evd-reproxy.h"
#include "evd-web-selector.h"
#include "evd-web-transport-server.h"
#include "evd-web-dir.h"
#include "evd-ipc-mechanism.h"
#include "evd-dbus-bridge.h"
#include "evd-dbus-daemon.h"
#include "evd-jsonrpc.h"
#include "evd-tls-cipher.h"
#include "evd-pki.h"
#include "evd-pki-privkey.h"
#include "evd-pki-pubkey.h"
#include "evd-daemon.h"
#include "evd-jsonrpc-http-client.h"
#include "evd-jsonrpc-http-server.h"

#endif /* __EVD_H__ */
