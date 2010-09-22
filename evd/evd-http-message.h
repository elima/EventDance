/*
 * evd-http-message.h
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

#ifndef __EVD_HTTP_MESSAGE_H__
#define __EVD_HTTP_MESSAGE_H__

#include <glib-object.h>
#include <libsoup/soup-headers.h>
#include <libsoup/soup-enum-types.h>

G_BEGIN_DECLS

typedef struct _EvdHttpMessage EvdHttpMessage;
typedef struct _EvdHttpMessageClass EvdHttpMessageClass;
typedef struct _EvdHttpMessagePrivate EvdHttpMessagePrivate;

struct _EvdHttpMessage
{
  GObject parent;

  EvdHttpMessagePrivate *priv;
};

struct _EvdHttpMessageClass
{
  GObjectClass parent_class;

  SoupMessageHeadersType type;
};

#define EVD_TYPE_HTTP_MESSAGE           (evd_http_message_get_type ())
#define EVD_HTTP_MESSAGE(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_HTTP_MESSAGE, EvdHttpMessage))
#define EVD_HTTP_MESSAGE_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_HTTP_MESSAGE, EvdHttpMessageClass))
#define EVD_IS_HTTP_MESSAGE(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_HTTP_MESSAGE))
#define EVD_IS_HTTP_MESSAGE_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_HTTP_MESSAGE))
#define EVD_HTTP_MESSAGE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_HTTP_MESSAGE, EvdHttpMessageClass))


GType                    evd_http_message_get_type          (void) G_GNUC_CONST;

EvdHttpMessage          *evd_http_message_new               (void);


SoupHTTPVersion          evd_http_message_get_version       (EvdHttpMessage *self);

SoupMessageHeaders      *evd_http_message_get_headers       (EvdHttpMessage *self);

gchar                   *evd_http_message_headers_to_string (EvdHttpMessage *self,
                                                             gsize          *size);

G_END_DECLS

#endif /* __EVD_HTTP_MESSAGE_H__ */
