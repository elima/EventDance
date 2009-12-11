/*
 * evd-json-socket.c
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

#include "evd-json-socket.h"
#include "evd-socket-protected.h"
#include "evd-json-filter.h"
#include "evd-marshal.h"

G_DEFINE_TYPE (EvdJsonSocket, evd_json_socket, EVD_TYPE_SOCKET)

#define EVD_JSON_SOCKET_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                          EVD_TYPE_JSON_SOCKET, \
                                          EvdJsonSocketPrivate))

#define MAX_BLOCK_SIZE 0xFFFF

/* private data */
struct _EvdJsonSocketPrivate
{
  EvdJsonFilter *json_filter;

  gchar *buffer;

  GClosure *on_packet;
};

static void     evd_json_socket_class_init         (EvdJsonSocketClass *class);
static void     evd_json_socket_init               (EvdJsonSocket *self);

static void     evd_json_socket_finalize           (GObject *obj);

static void     evd_json_socket_on_read            (EvdSocket *socket);

static void     evd_json_socket_on_filter_packet   (EvdJsonFilter *filter,
                                                    const gchar   *buffer,
                                                    gsize          size,
                                                    gpointer       user_data);

static void
evd_json_socket_class_init (EvdJsonSocketClass *class)
{
  GObjectClass *obj_class;
  EvdSocketClass *socket_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->finalize = evd_json_socket_finalize;

  socket_class = EVD_SOCKET_CLASS (class);

  socket_class->invoke_on_read = evd_json_socket_on_read;

  /* add private structure */
  g_type_class_add_private (obj_class, sizeof (EvdJsonSocketPrivate));
}

static void
evd_json_socket_init (EvdJsonSocket *self)
{
  EvdJsonSocketPrivate *priv;

  priv = EVD_JSON_SOCKET_GET_PRIVATE (self);
  self->priv = priv;

  priv->json_filter = evd_json_filter_new ();
  evd_json_filter_set_packet_handler (priv->json_filter,
                                      evd_json_socket_on_filter_packet,
                                      (gpointer) self);
}

static void
evd_json_socket_finalize (GObject *obj)
{
  EvdJsonSocket *self = EVD_JSON_SOCKET (obj);

  g_object_unref (self->priv->json_filter);

  if (self->priv->on_packet != NULL)
    g_closure_unref (self->priv->on_packet);

  G_OBJECT_CLASS (evd_json_socket_parent_class)->finalize (obj);
}

static void
evd_json_socket_on_filter_packet (EvdJsonFilter *filter,
                                  const gchar   *buffer,
                                  gsize          size,
                                  gpointer       user_data)
{
  EvdJsonSocket *self = EVD_JSON_SOCKET (user_data);

  if (self->priv->on_packet != NULL)
    {
      GValue params[3] = { {0, } };

      guintptr offset = (guintptr) buffer;
      guintptr start  = (guintptr) self->priv->buffer;
      gsize absolute_size = 0;
      gchar tmp;

      if ( (offset >= start) && (offset <= start + MAX_BLOCK_SIZE) )
        {
          absolute_size = size + (offset - start);

          tmp = self->priv->buffer[absolute_size];
          self->priv->buffer[absolute_size] = 0;
        }

      g_value_init (&params[0], EVD_TYPE_JSON_SOCKET);
      g_value_set_object (&params[0], self);

      g_value_init (&params[1], G_TYPE_STRING);
      g_value_set_static_string (&params[1], buffer);

      g_value_init (&params[2], G_TYPE_ULONG);
      g_value_set_ulong (&params[2], size);

      g_object_ref (self);
      g_closure_invoke (self->priv->on_packet, NULL, 3, params, NULL);
      g_object_unref (self);

      if (absolute_size > 0)
        self->priv->buffer[absolute_size] = tmp;

      g_value_unset (&params[0]);
      g_value_unset (&params[1]);
      g_value_unset (&params[2]);
    }
}

static void
evd_json_socket_on_read (EvdSocket *socket)
{
  EvdJsonSocket *self = EVD_JSON_SOCKET (socket);

  gchar buffer[MAX_BLOCK_SIZE+1];
  gssize size;
  GError *error = NULL;

  self->priv->buffer = buffer;

  if ( (size = evd_socket_read_buffer (socket,
                                       self->priv->buffer,
                                       MAX_BLOCK_SIZE,
                                       &error)) < 0)
    {
      evd_socket_throw_error (EVD_SOCKET (self), error);
    }
  else
    {
      if (! evd_json_filter_feed_len (self->priv->json_filter,
                                      self->priv->buffer,
                                      size,
                                      &error))
        {
          evd_socket_throw_error (EVD_SOCKET (self), error);
        }
    }
}

/* public methods */

EvdJsonSocket *
evd_json_socket_new (void)
{
  EvdJsonSocket *self;

  self = g_object_new (EVD_TYPE_JSON_SOCKET, NULL);

  return self;
}

void
evd_json_socket_set_packet_handler (EvdJsonSocket                *self,
                                    EvdJsonSocketOnPacketHandler  handler,
                                    gpointer                      user_data)
{
  GClosure *closure;

  g_return_if_fail (EVD_IS_JSON_SOCKET (self));

  if (handler == NULL)
    {
      evd_json_socket_set_on_packet (self, NULL);

      return;
    }

  closure = g_cclosure_new (G_CALLBACK (handler),
			    user_data,
			    NULL);

  if (G_CLOSURE_NEEDS_MARSHAL (closure))
    {
      GClosureMarshal marshal = evd_marshal_VOID__STRING_ULONG;

      g_closure_set_marshal (closure, marshal);
    }

  evd_json_socket_set_on_packet (self, closure);
}

void
evd_json_socket_set_on_packet (EvdJsonSocket *self,
                               GClosure      *closure)
{
  g_return_if_fail (EVD_IS_JSON_SOCKET (self));

  if (closure == NULL)
    {
      if (self->priv->on_packet != NULL)
        {
          g_closure_unref (self->priv->on_packet);
          self->priv->on_packet = NULL;
        }

      return;
    }

  g_closure_ref (closure);
  g_closure_sink (closure);

  self->priv->on_packet = closure;
}

GClosure *
evd_json_socket_get_on_packet (EvdJsonSocket *self)
{
  return self->priv->on_packet;
}
