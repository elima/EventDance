/*
 * evd-output-stream.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2014, Igalia S.L.
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

#include <string.h>

#include "evd-output-stream.h"

enum
{
  SIGNAL_CLOSE,
  SIGNAL_LAST
};

static guint evd_output_stream_signals[SIGNAL_LAST] = { 0 };

static void
evd_output_stream_base_init (gpointer g_class)
{
  static gboolean is_initialized = FALSE;
  EvdOutputStreamInterface *iface = (EvdOutputStreamInterface *) g_class;

  if (! is_initialized)
    {
      evd_output_stream_signals[SIGNAL_CLOSE] =
        g_signal_new ("close",
                      G_TYPE_FROM_CLASS (g_class),
                      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                      G_STRUCT_OFFSET (EvdOutputStreamInterface, signal_close),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);

      is_initialized = TRUE;
    }

  iface->is_closed = FALSE;
  iface->has_pending = FALSE;
}

static void
evd_output_stream_base_finalize (gpointer g_class)
{
}

static void
close_on_flush (GObject      *obj,
                GAsyncResult *res,
                gpointer      user_data)
{
  EvdOutputStream *self = EVD_OUTPUT_STREAM (obj);
  GError *error = NULL;
  EvdOutputStreamInterface *iface = EVD_OUTPUT_STREAM_GET_INTERFACE (self);

  if (! evd_output_stream_flush_finish (self, res, &error))
    {
      g_printerr ("Failed to flush output stream before closing: %s\n", error->message);
      g_error_free (error);
    }

  iface->has_pending = FALSE;

  iface->close_fn (EVD_OUTPUT_STREAM (obj));

  g_signal_emit (self, SIGNAL_CLOSE, 0, NULL);
}

/* public methods */

GType
evd_output_stream_get_type (void)
{
  static GType iface_type = 0;

  if (iface_type == 0)
    {
      static const GTypeInfo info = {
        sizeof (EvdOutputStreamInterface),
        evd_output_stream_base_init,
        evd_output_stream_base_finalize,
        NULL,
      };

      iface_type = g_type_register_static (G_TYPE_INTERFACE,
                                           "EvdOutputStream",
                                           &info,
                                           0);
    }

  return iface_type;
}

gssize
evd_output_stream_write (EvdOutputStream  *self,
                         const void       *buffer,
                         gsize             size,
                         GError          **error)
{
  EvdOutputStreamInterface *iface = EVD_OUTPUT_STREAM_GET_INTERFACE (self);

  g_return_val_if_fail (EVD_IS_OUTPUT_STREAM (self), -1);

  g_assert (iface->write_fn != NULL);

  return iface->write_fn (self, buffer, size, error);
}

void
evd_output_stream_close (EvdOutputStream *self)
{
  EvdOutputStreamInterface *iface = EVD_OUTPUT_STREAM_GET_INTERFACE (self);

  g_return_if_fail (EVD_IS_OUTPUT_STREAM (self));

  g_assert (iface->close_fn != NULL);

  if (iface->is_closed)
    return;

  if (! iface->has_pending)
    evd_output_stream_flush (self, NULL, close_on_flush, NULL);

  iface->is_closed = TRUE;
}

void
evd_output_stream_flush (EvdOutputStream     *self,
                         GCancellable        *cancellable,
                         GAsyncReadyCallback  callback,
                         gpointer             user_data)
{
  EvdOutputStreamInterface *iface = EVD_OUTPUT_STREAM_GET_INTERFACE (self);
  GSimpleAsyncResult *res;

  g_return_if_fail (EVD_IS_OUTPUT_STREAM (self));
  g_assert (iface->flush_fn != NULL);

  res = g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   evd_output_stream_flush);

  if (iface->has_pending)
    {
      g_simple_async_result_set_error (res,
                                       G_IO_ERROR,
                                       G_IO_ERROR_PENDING,
                                       "Output stream has a pending operation");
      g_simple_async_result_complete_in_idle (res);
      g_object_unref (res);
      return;
    }

  if (iface->is_closed)
    {
      g_simple_async_result_set_error (res,
                                       G_IO_ERROR,
                                       G_IO_ERROR_CLOSED,
                                       "Output stream is closed");
      g_simple_async_result_complete_in_idle (res);
      g_object_unref (res);
      return;
    }

  iface->flush_fn (self, G_ASYNC_RESULT (res), cancellable);
}

gboolean
evd_output_stream_flush_finish (EvdOutputStream  *self,
                                GAsyncResult     *result,
                                GError          **error)
{
  g_return_val_if_fail (EVD_IS_OUTPUT_STREAM (self), FALSE);
  g_return_val_if_fail (g_simple_async_result_is_valid (result,
                                                        G_OBJECT (self),
                                                        evd_output_stream_flush),
                        FALSE);

  return
    ! g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                             error);
}

gsize
evd_output_stream_get_max_writable (EvdOutputStream *self)
{
  EvdOutputStreamInterface *iface = EVD_OUTPUT_STREAM_GET_INTERFACE (self);

  g_return_val_if_fail (EVD_IS_OUTPUT_STREAM (self), 0);

  if (iface->is_closed || iface->has_pending)
    return 0;

  if (iface->get_max_writable != NULL)
    return iface->get_max_writable (self);
  else
    return G_MAXUINT32;
}
