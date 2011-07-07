/*
 * evd-http-chunked-decoder.h
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

#ifndef __EVD_HTTP_CHUNKED_DECODER_H__
#define __EVD_HTTP_CHUNKED_DECODER_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _EvdHttpChunkedDecoder EvdHttpChunkedDecoder;
typedef struct _EvdHttpChunkedDecoderClass EvdHttpChunkedDecoderClass;
typedef struct _EvdHttpChunkedDecoderPrivate EvdHttpChunkedDecoderPrivate;

struct _EvdHttpChunkedDecoder
{
  GObject parent;

  EvdHttpChunkedDecoderPrivate *priv;
};

struct _EvdHttpChunkedDecoderClass
{
  GObjectClass parent_class;
};

#define EVD_TYPE_HTTP_CHUNKED_DECODER           (evd_http_chunked_decoder_get_type ())
#define EVD_HTTP_CHUNKED_DECODER(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_HTTP_CHUNKED_DECODER, EvdHttpChunkedDecoder))
#define EVD_HTTP_CHUNKED_DECODER_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_HTTP_CHUNKED_DECODER, EvdHttpChunkedDecoderClass))
#define EVD_IS_HTTP_CHUNKED_DECODER(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_HTTP_CHUNKED_DECODER))
#define EVD_IS_HTTP_CHUNKED_DECODER_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_HTTP_CHUNKED_DECODER))
#define EVD_HTTP_CHUNKED_DECODER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_HTTP_CHUNKED_DECODER, EvdHttpChunkedDecoderClass))


GType                   evd_http_chunked_decoder_get_type        (void) G_GNUC_CONST;
EvdHttpChunkedDecoder * evd_http_chunked_decoder_new             (void);

G_END_DECLS

#endif /* __EVD_HTTP_CHUNKED_DECODER_H__ */
