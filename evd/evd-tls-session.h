/*
 * evd-tls-session.h
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

#ifndef __EVD_TLS_SESSION_H__
#define __EVD_TLS_SESSION_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _EvdTlsSession EvdTlsSession;
typedef struct _EvdTlsSessionClass EvdTlsSessionClass;
typedef struct _EvdTlsSessionPrivate EvdTlsSessionPrivate;

struct _EvdTlsSession
{
  GObject parent;

  /* private structure */
  EvdTlsSessionPrivate *priv;
};

struct _EvdTlsSessionClass
{
  GObjectClass parent_class;
};

#define EVD_TYPE_TLS_SESSION           (evd_tls_session_get_type ())
#define EVD_TLS_SESSION(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_TLS_SESSION, EvdTlsSession))
#define EVD_TLS_SESSION_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_TLS_SESSION, EvdTlsSessionClass))
#define EVD_IS_TLS_SESSION(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_TLS_SESSION))
#define EVD_IS_TLS_SESSION_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_TLS_SESSION))
#define EVD_TLS_SESSION_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_TLS_SESSION, EvdTlsSessionClass))


GType             evd_tls_session_get_type       (void) G_GNUC_CONST;

EvdTlsSession    *evd_tls_session_new            (void);


G_END_DECLS

#endif /* __EVD_TLS_SESSION_H__ */
