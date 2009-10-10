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

#include "evd-stream.h"

#define DOMAIN_QUARK_STRING "org.eventdance.glib.stream"

G_DEFINE_ABSTRACT_TYPE (EvdStream, evd_stream, G_TYPE_OBJECT)

#define EVD_STREAM_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
	                             EVD_TYPE_STREAM, \
                                     EvdStreamPrivate))

/* private data */
struct _EvdStreamPrivate
{
  GClosure *receive_closure;
};

/* signals */
/*
enum
{
  SIGNAL_LAST
};
*/

 /*static guint evd_stream_signals[SIGNAL_LAST] = { 0 };*/

/* properties */
enum
{
  PROP_0,
  PROP_RECEIVE_CLOSURE
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

  /* install signals */

  /* install properties */
  g_object_class_install_property (obj_class, PROP_RECEIVE_CLOSURE,
                                   g_param_spec_boxed ("receive",
						       "Receive closure",
						       "The callback closure that will be invoked when data is ready to be read",
						       G_TYPE_CLOSURE,
						       G_PARAM_READWRITE));

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
  priv->receive_closure = NULL;
}

static void
evd_stream_dispose (GObject *obj)
{
  EvdStream *self = EVD_STREAM (obj);

  evd_stream_set_on_receive (self, NULL);

  G_OBJECT_CLASS (evd_stream_parent_class)->dispose (obj);
}

static void
evd_stream_finalize (GObject *obj)
{
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
    case PROP_RECEIVE_CLOSURE:
      evd_stream_set_on_receive (self, g_value_get_boxed (value));
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
    case PROP_RECEIVE_CLOSURE:
      g_value_set_boxed (value, self->priv->receive_closure);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

/* protected methods */


/* public methods */

EvdStream *
evd_stream_new (void)
{
  EvdStream *self;

  self = g_object_new (EVD_TYPE_STREAM, NULL);

  return self;
}

void
evd_stream_set_on_receive (EvdStream *self,
			   GClosure  *closure)
{
  g_return_if_fail (EVD_IS_STREAM (self));

  if (self->priv->receive_closure != NULL)
    g_closure_unref (self->priv->receive_closure);

  if (closure != NULL)
    g_closure_ref (closure);

  self->priv->receive_closure = closure;
}

GClosure *
evd_stream_get_on_receive (EvdStream *self)
{
  g_return_val_if_fail (EVD_IS_STREAM (self), NULL);

  return self->priv->receive_closure;
}
