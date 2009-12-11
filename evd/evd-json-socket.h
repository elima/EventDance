/*
 * evd-json-socket.h
 *
 * EventDance project - An event distribution framework (http://eventdance.org)
 *
 * Copyright (C) 2009, Igalia S.L.
 *
 * Authors:
 *   Eduardo Lima Mitev <elima@igalia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 3 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#ifndef __EVD_JSON_SOCKET_H__
#define __EVD_JSON_SOCKET_H__

#include "evd-socket.h"

G_BEGIN_DECLS

typedef struct _EvdJsonSocket EvdJsonSocket;
typedef struct _EvdJsonSocketClass EvdJsonSocketClass;
typedef struct _EvdJsonSocketPrivate EvdJsonSocketPrivate;

struct _EvdJsonSocket
{
  EvdSocket parent;

  EvdJsonSocketPrivate *priv;
};

struct _EvdJsonSocketClass
{
  EvdSocketClass parent_class;
};

/* error codes */
typedef enum
{
  EVD_JSON_SOCKET_ERROR_FIRST = EVD_SOCKET_ERROR_LAST,
  EVD_JSON_SOCKET_ERROR_RESOLVE
} EvdJsonSocketError;

#define EVD_TYPE_JSON_SOCKET           (evd_json_socket_get_type ())
#define EVD_JSON_SOCKET(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_JSON_SOCKET, EvdJsonSocket))
#define EVD_JSON_SOCKET_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_JSON_SOCKET, EvdJsonSocketClass))
#define EVD_IS_JSON_SOCKET(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_JSON_SOCKET))
#define EVD_IS_JSON_SOCKET_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_JSON_SOCKET))
#define EVD_JSON_SOCKET_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_JSON_SOCKET, EvdJsonSocketClass))


GType             evd_json_socket_get_type         (void) G_GNUC_CONST;

EvdJsonSocket    *evd_json_socket_new              (void);

G_END_DECLS

#endif /* __EVD_JSON_SOCKET_H__ */
