/*
 * evd-tls-output-stream.h
 *
 * EventDance - An event distribution framework (http://eventdance.org)
 *
 * Copyright (C) 2009/2010, Igalia S.L.
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
 *
 */

#ifndef __EVD_TLS_OUTPUT_STREAM_H__
#define __EVD_TLS_OUTPUT_STREAM_H__

#include <glib-object.h>
#include <gio/gio.h>

#include "evd-tls-session.h"

G_BEGIN_DECLS

typedef struct _EvdTlsOutputStream EvdTlsOutputStream;
typedef struct _EvdTlsOutputStreamClass EvdTlsOutputStreamClass;
typedef struct _EvdTlsOutputStreamPrivate EvdTlsOutputStreamPrivate;

struct _EvdTlsOutputStream
{
  GFilterOutputStream parent;

  EvdTlsOutputStreamPrivate *priv;
};

struct _EvdTlsOutputStreamClass
{
  GFilterOutputStreamClass parent_class;
};

#define EVD_TYPE_TLS_OUTPUT_STREAM           (evd_tls_output_stream_get_type ())
#define EVD_TLS_OUTPUT_STREAM(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_TLS_OUTPUT_STREAM, EvdTlsOutputStream))
#define EVD_TLS_OUTPUT_STREAM_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_TLS_OUTPUT_STREAM, EvdTlsOutputStreamClass))
#define EVD_IS_TLS_OUTPUT_STREAM(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_TLS_OUTPUT_STREAM))
#define EVD_IS_TLS_OUTPUT_STREAM_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_TLS_OUTPUT_STREAM))
#define EVD_TLS_OUTPUT_STREAM_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_TLS_OUTPUT_STREAM, EvdTlsOutputStreamClass))


GType              evd_tls_output_stream_get_type                     (void) G_GNUC_CONST;

EvdTlsOutputStream *evd_tls_output_stream_new                         (EvdTlsSession *session,
                                                                       GOutputStream *base_stream);

G_END_DECLS

#endif /* __EVD_TLS_OUTPUT_STREAM_H__ */
