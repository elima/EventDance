/*
 * evd-stream.c
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

#include <math.h>
#include <string.h>

#include "evd-stream.h"

#define DOMAIN_QUARK_STRING "org.eventdance.glib.stream"

G_DEFINE_ABSTRACT_TYPE (EvdStream, evd_stream, G_TYPE_OBJECT)

#define EVD_STREAM_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                     EVD_TYPE_STREAM, \
                                     EvdStreamPrivate))

/* private data */
struct _EvdStreamPrivate
{
  GClosure *read_closure;
  GClosure *write_closure;

  gsize  bandwidth_in;
  gsize  bandwidth_out;
  gulong latency_in;
  gulong latency_out;

  GTimeVal current_time;
  gsize    bytes_in;
  gsize    bytes_out;
  GTimeVal last_in;
  GTimeVal last_out;

  gsize total_in;
  gsize total_out;

  GMutex *mutex;

  gboolean       tls_enabled;
  EvdTlsSession *tls_session;
};


/* properties */
enum
{
  PROP_0,
  PROP_READ_CLOSURE,
  PROP_WRITE_CLOSURE,
  PROP_BANDWIDTH_IN,
  PROP_BANDWIDTH_OUT,
  PROP_LATENCY_IN,
  PROP_LATENCY_OUT,
  PROP_TLS_ENABLED,
  PROP_TLS_SESSION
};

static void     evd_stream_class_init         (EvdStreamClass *class);
static void     evd_stream_init               (EvdStream *self);

static void     evd_stream_finalize           (GObject *obj);
static void     evd_stream_dispose            (GObject *obj);

static void     evd_stream_set_property       (GObject      *obj,
                                               guint         prop_id,
                                               const GValue *value,
                                               GParamSpec   *pspec);
static void     evd_stream_get_property       (GObject    *obj,
                                               guint       prop_id,
                                               GValue     *value,
                                               GParamSpec *pspec);

static void
evd_stream_class_init (EvdStreamClass *class)
{
  GObjectClass *obj_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_stream_dispose;
  obj_class->finalize = evd_stream_finalize;
  obj_class->get_property = evd_stream_get_property;
  obj_class->set_property = evd_stream_set_property;

  /* install properties */
  g_object_class_install_property (obj_class, PROP_READ_CLOSURE,
                                   g_param_spec_boxed ("read-closure",
                                                       "Read closure",
                                                       "The callback closure that will be invoked when data is ready to be read",
                                                       G_TYPE_CLOSURE,
                                                       G_PARAM_READWRITE));

  g_object_class_install_property (obj_class, PROP_WRITE_CLOSURE,
                                   g_param_spec_boxed ("write-closure",
                                                       "Write closure",
                                                       "The callback closure that will be invoked when data is ready to be written",
                                                       G_TYPE_CLOSURE,
                                                       G_PARAM_READWRITE));

  g_object_class_install_property (obj_class, PROP_BANDWIDTH_IN,
                                   g_param_spec_float ("bandwidth-in",
                                                       "Inbound bandwidth limit",
                                                       "The maximum bandwidth for reading, in kilobytes",
                                                       0.0,
                                                       G_MAXFLOAT,
                                                       0.0,
                                                       G_PARAM_READWRITE |
                                                       G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_BANDWIDTH_OUT,
                                   g_param_spec_float ("bandwidth-out",
                                                       "Outbound bandwidth limit",
                                                       "The maximum bandwidth for writing, in kilobytes",
                                                       0.0,
                                                       G_MAXFLOAT,
                                                       0.0,
                                                       G_PARAM_READWRITE |
                                                       G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_LATENCY_IN,
                                   g_param_spec_float ("latency-in",
                                                       "Inbound minimum latency",
                                                       "The minimum time between two reads, in miliseconds",
                                                       0.0,
                                                       G_MAXFLOAT,
                                                       0.0,
                                                       G_PARAM_READWRITE |
                                                       G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_LATENCY_OUT,
                                   g_param_spec_float ("latency-out",
                                                       "Outbound minimum latency",
                                                       "The minimum time between two writes, in miliseconds",
                                                       0.0,
                                                       G_MAXFLOAT,
                                                       0.0,
                                                       G_PARAM_READWRITE |
                                                       G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_TLS_ENABLED,
                                   g_param_spec_boolean ("tls-enabled",
                                                         "Whether SSL/TLS should be enabled or disabled",
                                                         "Used to enable/disabled SSL/TLS on socket",
                                                         FALSE,
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_TLS_SESSION,
                                   g_param_spec_object ("tls",
                                                        "The SSL/TLS session",
                                                        "The underlaying SSL/TLS session object",
                                                        EVD_TYPE_TLS_SESSION,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  /* add private structure */
  g_type_class_add_private (obj_class, sizeof (EvdStreamPrivate));
}

static void
evd_stream_init (EvdStream *self)
{
  EvdStreamPrivate *priv;

  priv = EVD_STREAM_GET_PRIVATE (self);
  self->priv = priv;

  /* initialize private members */
  priv->read_closure = NULL;

  priv->bandwidth_in = 0;
  priv->bandwidth_out = 0;
  priv->latency_in = 0;
  priv->latency_out = 0;

  priv->current_time.tv_sec = 0;
  priv->current_time.tv_usec = 0;

  priv->bytes_in = 0;
  priv->bytes_out = 0;

  if (! g_thread_get_initialized ())
    g_thread_init (NULL);
  priv->mutex = g_mutex_new ();

  priv->tls_enabled = FALSE;
  priv->tls_session = NULL;
}

static void
evd_stream_dispose (GObject *obj)
{
  EvdStream *self = EVD_STREAM (obj);

  evd_stream_set_on_read (self, NULL);

  G_OBJECT_CLASS (evd_stream_parent_class)->dispose (obj);
}

static void
evd_stream_finalize (GObject *obj)
{
  EvdStream *self = EVD_STREAM (obj);

  g_mutex_free (self->priv->mutex);

  if (self->priv->read_closure != NULL)
    g_closure_unref (self->priv->read_closure);

  if (self->priv->write_closure != NULL)
    g_closure_unref (self->priv->write_closure);

  if (self->priv->tls_session != NULL)
    g_object_unref (self->priv->tls_session);

  G_OBJECT_CLASS (evd_stream_parent_class)->finalize (obj);
}

static void
evd_stream_set_property (GObject      *obj,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  EvdStream *self;

  self = EVD_STREAM (obj);

  switch (prop_id)
    {
    case PROP_READ_CLOSURE:
      evd_stream_set_on_read (self, g_value_get_boxed (value));
      break;

    case PROP_WRITE_CLOSURE:
      evd_stream_set_on_write (self, g_value_get_boxed (value));
      break;

    case PROP_BANDWIDTH_IN:
      self->priv->bandwidth_in = (gsize) (g_value_get_float (value) * 1024.0);
      break;

    case PROP_BANDWIDTH_OUT:
      self->priv->bandwidth_out = (gsize) (g_value_get_float (value) * 1024.0);
      break;

      /* Latency properties are in miliseconds, but we store the value
         internally  in microseconds, to allow up to 1/1000 fraction of a
         milisecond */
    case PROP_LATENCY_IN:
      self->priv->latency_in = (gulong) (g_value_get_float (value) * 1000.0);
      break;

    case PROP_LATENCY_OUT:
      self->priv->latency_out = (gulong) (g_value_get_float (value) * 1000.0);
      break;

    case PROP_TLS_ENABLED:
      evd_stream_set_tls_enabled (self, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_stream_get_property (GObject    *obj,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  EvdStream *self;

  self = EVD_STREAM (obj);

  switch (prop_id)
    {
    case PROP_READ_CLOSURE:
      g_value_set_boxed (value, self->priv->read_closure);
      break;

    case PROP_WRITE_CLOSURE:
      g_value_set_boxed (value, self->priv->write_closure);
      break;

    case PROP_BANDWIDTH_IN:
      g_value_set_float (value, self->priv->bandwidth_in / 1024.0);
      break;

    case PROP_BANDWIDTH_OUT:
      g_value_set_float (value, self->priv->bandwidth_out / 1024.0);
      break;

      /* Latency values are stored in microseconds internally */
    case PROP_LATENCY_IN:
      g_value_set_float (value, self->priv->latency_in / 1000.0);
      break;

    case PROP_LATENCY_OUT:
      g_value_set_float (value, self->priv->latency_out / 1000.0);
      break;

    case PROP_TLS_ENABLED:
      g_value_set_boolean (value, evd_stream_get_tls_enabled (self));
      break;

    case PROP_TLS_SESSION:
      g_value_set_object (value, self->priv->tls_session);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_stream_update_current_time (EvdStream *self)
{
  GTimeVal time_val;

  g_get_current_time (&time_val);

  if (time_val.tv_sec != self->priv->current_time.tv_sec)
    {
      g_mutex_lock (self->priv->mutex);

      self->priv->bytes_in = 0;
      self->priv->bytes_out = 0;

      g_mutex_unlock (self->priv->mutex);
    }

  g_memmove (&self->priv->current_time, &time_val, sizeof (GTimeVal));
}

static gulong
g_timeval_get_diff_micro (GTimeVal *time1, GTimeVal *time2)
{
  gulong result;

  result = ABS (time2->tv_sec - time1->tv_sec) * G_USEC_PER_SEC;
  result += ABS (time2->tv_usec - time1->tv_usec) / 1000;

  return result;
}

static gsize
evd_stream_request (EvdStream *self,
                    gsize      bandwidth,
                    gulong     latency,
                    gsize      bytes,
                    GTimeVal  *last,
                    gsize      size,
                    guint     *wait)
{
  gsize actual_size = size;

  if ( (wait != NULL) && (*wait < 0) )
    *wait = 0;

  g_mutex_lock (self->priv->mutex);

  /*  latency check */
  if (latency > 0)
    {
      gulong elapsed;

      elapsed = g_timeval_get_diff_micro (&self->priv->current_time,
                                          last);
      if (elapsed < latency)
        {
          actual_size = 0;

          if (wait != NULL)
            *wait = MAX ((guint) ((latency - elapsed) / 1000),
                         *wait);
        }
    }

  /* bandwidth check */
  if ( (bandwidth > 0) && (actual_size > 0) )
    {
      actual_size = MAX ((gssize) (bandwidth - bytes), 0);
      actual_size = MIN (actual_size, size);

      if (wait != NULL)
        if (actual_size < size)
          *wait = MAX ((guint) (((1000001 - self->priv->current_time.tv_usec) / 1000)) + 1,
                       *wait);
    }

  g_mutex_unlock (self->priv->mutex);

  return actual_size;
}

/* protected methods */

void
evd_stream_report_read (EvdStream *self,
                        gsize     size)
{
  evd_stream_update_current_time (self);

  g_mutex_lock (self->priv->mutex);

  self->priv->bytes_in += size;
  self->priv->total_in += size;

  g_memmove (&self->priv->last_in,
             &self->priv->current_time,
             sizeof (GTimeVal));

  g_mutex_unlock (self->priv->mutex);
}

void
evd_stream_report_write (EvdStream *self,
                         gsize     size)
{
  evd_stream_update_current_time (self);

  g_mutex_lock (self->priv->mutex);

  self->priv->bytes_out += size;
  self->priv->total_out += size;

  g_memmove (&self->priv->last_out,
             &self->priv->current_time,
             sizeof (GTimeVal));

  g_mutex_unlock (self->priv->mutex);
}

/* public methods */

EvdStream *
evd_stream_new (void)
{
  EvdStream *self;

  self = g_object_new (EVD_TYPE_STREAM, NULL);

  return self;
}

void
evd_stream_set_on_read (EvdStream *self,
                        GClosure  *closure)
{
  g_return_if_fail (EVD_IS_STREAM (self));

  if (self->priv->read_closure != NULL)
    g_closure_unref (self->priv->read_closure);

  if (closure != NULL)
    {
      g_closure_ref (closure);
      g_closure_sink (closure);
    }

  self->priv->read_closure = closure;
}

GClosure *
evd_stream_get_on_read (EvdStream *self)
{
  g_return_val_if_fail (EVD_IS_STREAM (self), NULL);

  return self->priv->read_closure;
}

void
evd_stream_set_on_write (EvdStream *self,
                         GClosure  *closure)
{
  g_return_if_fail (EVD_IS_STREAM (self));

  if (self->priv->write_closure != NULL)
    g_closure_unref (self->priv->write_closure);

  if (closure != NULL)
    {
      g_closure_ref (closure);
      g_closure_sink (closure);
    }

  self->priv->write_closure = closure;
}

GClosure *
evd_stream_get_on_write (EvdStream *self)
{
  g_return_val_if_fail (EVD_IS_STREAM (self), NULL);

  return self->priv->write_closure;
}

gsize
evd_stream_request_read  (EvdStream *self,
                          gsize      size,
                          guint     *wait)
{
  evd_stream_update_current_time (self);

  return evd_stream_request (self,
                             self->priv->bandwidth_in,
                             self->priv->latency_in,
                             self->priv->bytes_in,
                             &self->priv->last_in,
                             size,
                             wait);
}

gsize
evd_stream_request_write (EvdStream *self,
                          gsize      size,
                          guint     *wait)
{
  evd_stream_update_current_time (self);

  return evd_stream_request (self,
                             self->priv->bandwidth_out,
                             self->priv->latency_out,
                             self->priv->bytes_out,
                             &self->priv->last_out,
                             size,
                             wait);
}

gulong
evd_stream_get_total_read (EvdStream *self)
{
  return self->priv->total_in;
}

gulong
evd_stream_get_total_written (EvdStream *self)
{
  return self->priv->total_out;
}

gfloat
evd_stream_get_actual_bandwidth_in (EvdStream *self)
{
  return self->priv->bytes_in / 1024.0;
}

gfloat
evd_stream_get_actual_bandwidth_out (EvdStream *self)
{
  return self->priv->bytes_out / 1024.0;
}

void
evd_stream_set_tls_enabled (EvdStream *self, gboolean enabled)
{
  g_return_if_fail (EVD_IS_STREAM (self));

  self->priv->tls_enabled = enabled;

  if (self->priv->tls_enabled)
    {
      if (self->priv->tls_session == NULL)
        self->priv->tls_session = evd_tls_session_new ();
    }
  else
    {
      if (self->priv->tls_session != NULL)
        {
          g_object_unref (self->priv->tls_session);
          self->priv->tls_session = NULL;
        }
    }
}

gboolean
evd_stream_get_tls_enabled (EvdStream *self)
{
  g_return_val_if_fail (EVD_IS_STREAM (self), FALSE);

  return self->priv->tls_enabled;
}

EvdTlsSession *
evd_stream_get_tls_session (EvdStream *self)
{
  g_return_val_if_fail (EVD_IS_STREAM (self), NULL);

  return self->priv->tls_session;
}
