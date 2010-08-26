/*
 * evd-web-service.h
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
 * Lesser General Public License at http://www.gnu.org/licenses/lgpl-3.0.txt
 * for more details.
 */

#ifndef __EVD_WEB_SERVICE_H__
#define __EVD_WEB_SERVICE_H__

#include <glib-object.h>
#include <libsoup/soup-headers.h>

#include <evd-service.h>
#include <evd-http-connection.h>

G_BEGIN_DECLS

typedef struct _EvdWebService EvdWebService;
typedef struct _EvdWebServiceClass EvdWebServiceClass;

struct _EvdWebService
{
  EvdService parent;
};

struct _EvdWebServiceClass
{
  EvdServiceClass parent_class;

  /* virtual methods */
  void (* headers_read) (EvdWebService      *self,
                         EvdHttpConnection  *conn,
                         SoupHTTPVersion     ver,
                         gchar              *method,
                         gchar              *path,
                         SoupMessageHeaders *headers);

  /* signal prototypes */
};

#define EVD_TYPE_WEB_SERVICE           (evd_web_service_get_type ())
#define EVD_WEB_SERVICE(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_WEB_SERVICE, EvdWebService))
#define EVD_WEB_SERVICE_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_WEB_SERVICE, EvdWebServiceClass))
#define EVD_IS_WEB_SERVICE(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_WEB_SERVICE))
#define EVD_IS_WEB_SERVICE_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_WEB_SERVICE))
#define EVD_WEB_SERVICE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_WEB_SERVICE, EvdWebServiceClass))


GType             evd_web_service_get_type                (void) G_GNUC_CONST;

EvdWebService *   evd_web_service_new                     (void);

G_END_DECLS

#endif /* __EVD_WEB_SERVICE_H__ */
