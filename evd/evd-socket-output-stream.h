/*
 * evd-socket-output-stream.h
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

#ifndef __EVD_SOCKET_OUTPUT_STREAM_H__
#define __EVD_SOCKET_OUTPUT_STREAM_H__

#include <glib-object.h>
#include <gio/gio.h>

#include "evd-socket.h"

G_BEGIN_DECLS

typedef struct _EvdSocketOutputStream EvdSocketOutputStream;
typedef struct _EvdSocketOutputStreamClass EvdSocketOutputStreamClass;
typedef struct _EvdSocketOutputStreamPrivate EvdSocketOutputStreamPrivate;

struct _EvdSocketOutputStream
{
  GOutputStream parent;

  EvdSocketOutputStreamPrivate *priv;
};

struct _EvdSocketOutputStreamClass
{
  GOutputStreamClass parent_class;

  /* signal prototypes */
  void (* filled) (EvdSocketOutputStream *self);
};

#define EVD_TYPE_SOCKET_OUTPUT_STREAM           (evd_socket_output_stream_get_type ())
#define EVD_SOCKET_OUTPUT_STREAM(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_SOCKET_OUTPUT_STREAM, EvdSocketOutputStream))
#define EVD_SOCKET_OUTPUT_STREAM_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_SOCKET_OUTPUT_STREAM, EvdSocketOutputStreamClass))
#define EVD_IS_SOCKET_OUTPUT_STREAM(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_SOCKET_OUTPUT_STREAM))
#define EVD_IS_SOCKET_OUTPUT_STREAM_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_SOCKET_OUTPUT_STREAM))
#define EVD_SOCKET_OUTPUT_STREAM_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_SOCKET_OUTPUT_STREAM, EvdSocketOutputStreamClass))


GType                 evd_socket_output_stream_get_type                     (void) G_GNUC_CONST;

EvdSocketOutputStream *evd_socket_output_stream_new                         (EvdSocket *socket);

void                   evd_socket_output_stream_set_socket                  (EvdSocketOutputStream *self,
                                                                             EvdSocket             *socket);
EvdSocket             *evd_socket_output_stream_get_socket                  (EvdSocketOutputStream *self);

G_END_DECLS

#endif /* __EVD_SOCKET_OUTPUT_STREAM_H__ */
