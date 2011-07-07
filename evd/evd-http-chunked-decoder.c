/*
 * evd-http-chunked-decoder.c
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

#include <stdio.h>
#include <string.h>
#include <gio/gio.h>

#include "evd-http-chunked-decoder.h"

#define EVD_HTTP_CHUNKED_DECODER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                                   EVD_TYPE_HTTP_CHUNKED_DECODER, \
                                                   EvdHttpChunkedDecoderPrivate))

/* private data */
struct _EvdHttpChunkedDecoderPrivate
{
  gsize chunk_left;

  guint status;

  gchar hdr_buf[10];
  gsize hdr_buf_len;

  guint crlf_pos;
};

enum
{
  READING_CHUNK_HEADER,
  READING_CONTENT,
  READING_CRLF_1,
  READING_CRLF_2
};

static void             evd_http_chunked_decoder_class_init (EvdHttpChunkedDecoderClass *class);
static void             evd_http_chunked_decoder_init       (EvdHttpChunkedDecoder *self);

static void             converter_iface_init                (GConverterIface *iface);

static GConverterResult convert                             (GConverter       *converter,
                                                             const void       *inbuf,
                                                             gsize             inbuf_size,
                                                             void             *outbuf,
                                                             gsize             outbuf_size,
                                                             GConverterFlags   flags,
                                                             gsize            *bytes_read,
                                                             gsize            *bytes_written,
                                                             GError          **error);
static void             reset                               (GConverter *converter);

G_DEFINE_TYPE_WITH_CODE (EvdHttpChunkedDecoder, evd_http_chunked_decoder, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_CONVERTER,
                                                converter_iface_init))

static void
evd_http_chunked_decoder_class_init (EvdHttpChunkedDecoderClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);

  g_type_class_add_private (obj_class, sizeof (EvdHttpChunkedDecoderPrivate));
}

static void
converter_iface_init (GConverterIface *iface)
{
  iface->reset = reset;
  iface->convert = convert;
}

static void
evd_http_chunked_decoder_init (EvdHttpChunkedDecoder *self)
{
  EvdHttpChunkedDecoderPrivate *priv;

  priv = EVD_HTTP_CHUNKED_DECODER_GET_PRIVATE (self);
  self->priv = priv;

  reset (G_CONVERTER (self));
}

static GConverterResult
convert (GConverter       *converter,
         const void       *inbuf,
         gsize             inbuf_size,
         void             *outbuf,
         gsize             outbuf_size,
         GConverterFlags   flags,
         gsize            *bytes_read,
         gsize            *bytes_written,
         GError          **error)
{
  EvdHttpChunkedDecoder *self = EVD_HTTP_CHUNKED_DECODER (converter);
  GConverterResult result = G_CONVERTER_CONVERTED;
  gsize bw = 0;
  const gchar *_inbuf = (const gchar *) inbuf;
  char c;
  gboolean done = FALSE;
  gsize pos = 0;

  while (! done && pos < inbuf_size && bw < outbuf_size)
    {
      c = _inbuf[pos];

      switch (self->priv->status)
        {
        case READING_CHUNK_HEADER:
          {
            if ( (c >= '0' && c <= '9') ||
                 (c >= 'a' && c <= 'f') ||
                 (c >= 'A' && c <= 'F') ||
                 (c == 32) )
              {
                pos++;

                self->priv->hdr_buf[ self->priv->hdr_buf_len ] = c;
                self->priv->hdr_buf_len++;
              }
            else if (self->priv->hdr_buf_len > 0)
              {
                self->priv->hdr_buf[ self->priv->hdr_buf_len ] = '\0';
                sscanf (self->priv->hdr_buf,
                        "%x",
                        (guint *) &self->priv->chunk_left);

                self->priv->hdr_buf_len = 0;

                self->priv->status = READING_CRLF_1;
              }
            else
              {
                g_set_error (error,
                             G_IO_ERROR,
                             G_IO_ERROR_INVALID_DATA,
                             "Failed to parse chunk-size of chunked encoded content");

                result = G_CONVERTER_ERROR;
                done = TRUE;
              }

            break;
          }

        case READING_CRLF_1:
        case READING_CRLF_2:
          {
            gboolean err = FALSE;

            if (self->priv->crlf_pos == 0)
              {
                if (c == '\r')
                  self->priv->crlf_pos++;
                else
                  {
                    g_debug ("kaka: %d", c);
                    err = TRUE;
                  }
              }
            else
              {
                if (c == '\n')
                  {
                    self->priv->crlf_pos = 0;

                    if (self->priv->status == READING_CRLF_1)
                      {
                        if (self->priv->chunk_left == 0)
                          {
                            result = G_CONVERTER_FINISHED;
                            done = TRUE;
                          }
                        else
                          self->priv->status = READING_CONTENT;
                      }
                    else
                      self->priv->status = READING_CHUNK_HEADER;
                  }
                else
                  err = TRUE;
              }

            if (err)
              {
                g_set_error (error,
                             G_IO_ERROR,
                             G_IO_ERROR_INVALID_DATA,
                             "Failed to parse chunked encoded content");

                result = G_CONVERTER_ERROR;
                done = TRUE;
              }
            else
              {
                pos++;
              }

            break;
          }

        case READING_CONTENT:
          {
            gsize move_size;

            move_size = MIN (self->priv->chunk_left,
                             inbuf_size - pos);
            move_size = MIN (move_size, outbuf_size - bw);

            g_memmove (outbuf + bw, inbuf + pos, move_size);

            pos += move_size;
            bw += move_size;
            self->priv->chunk_left -= move_size;

            if (self->priv->chunk_left == 0)
              self->priv->status = READING_CRLF_2;

            break;
          }
        }
    }

  if (bytes_read != NULL)
    *bytes_read = pos;
  if (bytes_written != NULL)
    *bytes_written = bw;

  if (result != G_CONVERTER_ERROR && flags & G_CONVERTER_FLUSH)
    result = G_CONVERTER_FLUSHED;

  return result;
}

static void
reset (GConverter *converter)
{
  EvdHttpChunkedDecoder *self = EVD_HTTP_CHUNKED_DECODER (converter);

  self->priv->chunk_left = 0;

  self->priv->status = READING_CHUNK_HEADER;

  self->priv->hdr_buf_len = 0;

  self->priv->crlf_pos = 0;
}

/* public methods */

EvdHttpChunkedDecoder *
evd_http_chunked_decoder_new (void)
{
  return g_object_new (EVD_TYPE_HTTP_CHUNKED_DECODER, NULL);
}
