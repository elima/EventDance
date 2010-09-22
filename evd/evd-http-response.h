/*
 * evd-http-response.h
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2013, Igalia S.L.
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
 * Lesser General Public License at http://www.gnu.org/licenses/lgpl-3.0.txt
 * for more details.
 */

#ifndef __EVD_HTTP_RESPONSE_H__
#define __EVD_HTTP_RESPONSE_H__

#if !defined (__EVD_H_INSIDE__) && !defined (EVD_COMPILATION)
#error "Only <evd.h> can be included directly."
#endif

#include "evd-http-message.h"

G_BEGIN_DECLS

typedef struct _EvdHttpResponse EvdHttpResponse;
typedef struct _EvdHttpResponseClass EvdHttpResponseClass;
typedef struct _EvdHttpResponsePrivate EvdHttpResponsePrivate;

struct _EvdHttpResponse
{
  EvdHttpMessage parent;

  EvdHttpResponsePrivate *priv;
};

struct _EvdHttpResponseClass
{
  EvdHttpMessageClass parent_class;
};

#define EVD_TYPE_HTTP_RESPONSE           (evd_http_response_get_type ())
#define EVD_HTTP_RESPONSE(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_HTTP_RESPONSE, EvdHttpResponse))
#define EVD_HTTP_RESPONSE_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_HTTP_RESPONSE, EvdHttpResponseClass))
#define EVD_IS_HTTP_RESPONSE(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_HTTP_RESPONSE))
#define EVD_IS_HTTP_RESPONSE_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_HTTP_RESPONSE))
#define EVD_HTTP_RESPONSE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_HTTP_RESPONSE, EvdHttpResponseClass))


GType                    evd_http_response_get_type          (void) G_GNUC_CONST;

EvdHttpResponse *        evd_http_response_new               (void);

void                     evd_http_response_set_reason_phrase (EvdHttpResponse *self,
                                                              const gchar     *reason_phrase);
const gchar *            evd_http_response_get_reason_phrase (EvdHttpResponse *self);

void                     evd_http_response_set_status_code   (EvdHttpResponse *self,
                                                              guint            status_code);
guint                    evd_http_response_get_status_code   (EvdHttpResponse *self);

G_END_DECLS

#endif /* __EVD_HTTP_RESPONSE_H__ */
