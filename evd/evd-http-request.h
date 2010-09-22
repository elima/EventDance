/*
 * evd-http-request.h
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

#ifndef __EVD_HTTP_REQUEST_H__
#define __EVD_HTTP_REQUEST_H__

#if !defined (__EVD_H_INSIDE__) && !defined (EVD_COMPILATION)
#error "Only <evd.h> can be included directly."
#endif

#include <libsoup/soup-uri.h>

#include "evd-http-message.h"
#include "evd-http-response.h"

G_BEGIN_DECLS

typedef struct _EvdHttpRequest EvdHttpRequest;
typedef struct _EvdHttpRequestClass EvdHttpRequestClass;
typedef struct _EvdHttpRequestPrivate EvdHttpRequestPrivate;

struct _EvdHttpRequest
{
  EvdHttpMessage parent;

  EvdHttpRequestPrivate *priv;
};

struct _EvdHttpRequestClass
{
  EvdHttpMessageClass parent_class;
};

#define EVD_TYPE_HTTP_REQUEST           (evd_http_request_get_type ())
#define EVD_HTTP_REQUEST(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_HTTP_REQUEST, EvdHttpRequest))
#define EVD_HTTP_REQUEST_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_HTTP_REQUEST, EvdHttpRequestClass))
#define EVD_IS_HTTP_REQUEST(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_HTTP_REQUEST))
#define EVD_IS_HTTP_REQUEST_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_HTTP_REQUEST))
#define EVD_HTTP_REQUEST_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_HTTP_REQUEST, EvdHttpRequestClass))


GType                    evd_http_request_get_type          (void) G_GNUC_CONST;

EvdHttpRequest          *evd_http_request_new               (const gchar *method,
                                                             const gchar *url);

const gchar             *evd_http_request_get_method        (EvdHttpRequest *self);

 gchar                  *evd_http_request_get_path          (EvdHttpRequest *self);

SoupURI                 *evd_http_request_get_uri           (EvdHttpRequest *self);

EvdHttpResponse         *evd_http_request_get_response      (EvdHttpRequest *self);

gchar                   *evd_http_request_to_string         (EvdHttpRequest *self,
                                                             gsize          *size);

void             evd_http_request_set_basic_auth_credentials (EvdHttpRequest *self,
                                                              const gchar    *user,
                                                              const gchar    *passw);
gboolean         evd_http_request_get_basic_auth_credentials (EvdHttpRequest  *self,
                                                              gchar          **user,
                                                              gchar          **password);

gchar           *evd_http_request_get_cookie_value           (EvdHttpRequest *self,
                                                              const gchar    *cookie_name);

const gchar     *evd_http_request_get_origin                 (EvdHttpRequest *self);

gboolean         evd_http_request_is_cross_origin            (EvdHttpRequest *self);
gboolean         evd_http_request_is_cors_preflight          (EvdHttpRequest *self);

G_END_DECLS

#endif /* __EVD_HTTP_REQUEST_H__ */
