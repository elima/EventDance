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

G_DEFINE_TYPE (EvdJsonSocket, evd_json_socket, EVD_TYPE_SOCKET)

#define EVD_JSON_SOCKET_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                          EVD_TYPE_JSON_SOCKET, \
                                          EvdJsonSocketPrivate))

/* private data */
struct _EvdJsonSocketPrivate
{
  EvdJsonFilter *json_filter;
};

static void     evd_json_socket_class_init         (EvdJsonSocketClass *class);
static void     evd_json_socket_init               (EvdJsonSocket *self);

static void     evd_json_socket_finalize           (GObject *obj);
static void     evd_json_socket_dispose            (GObject *obj);

static void
evd_json_socket_class_init (EvdJsonSocketClass *class)
{
  GObjectClass *obj_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_json_socket_dispose;
  obj_class->finalize = evd_json_socket_finalize;

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
}

static void
evd_json_socket_dispose (GObject *obj)
{
  G_OBJECT_CLASS (evd_json_socket_parent_class)->dispose (obj);
}

static void
evd_json_socket_finalize (GObject *obj)
{
  EvdJsonSocket *self = EVD_JSON_SOCKET (obj);

  g_object_unref (self->priv->json_filter);

  G_OBJECT_CLASS (evd_json_socket_parent_class)->finalize (obj);
}

/* public methods */

EvdJsonSocket *
evd_json_socket_new (void)
{
  EvdJsonSocket *self;

  self = g_object_new (EVD_TYPE_JSON_SOCKET, NULL);

  return self;
}
