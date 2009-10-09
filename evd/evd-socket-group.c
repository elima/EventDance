/*
 * evd-socket-group.c
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

#include "evd-socket-group.h"

G_DEFINE_TYPE (EvdSocketGroup, evd_socket_group, G_TYPE_OBJECT)

typedef struct
{
  gpointer data;
} AsyncActionData;

static void     evd_socket_group_class_init         (EvdSocketGroupClass *class);
static void     evd_socket_group_init               (EvdSocketGroup *self);

static void     evd_socket_group_finalize           (GObject *obj);
static void     evd_socket_group_dispose            (GObject *obj);

static void
evd_socket_group_class_init (EvdSocketGroupClass *class)
{
  GObjectClass *obj_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_socket_group_dispose;
  obj_class->finalize = evd_socket_group_finalize;
}

static void
evd_socket_group_init (EvdSocketGroup *self)
{
}

static void
evd_socket_group_dispose (GObject *obj)
{
  G_OBJECT_CLASS (evd_socket_group_parent_class)->dispose (obj);
}

static void
evd_socket_group_finalize (GObject *obj)
{
  G_OBJECT_CLASS (evd_socket_group_parent_class)->finalize (obj);
}

/* public methods */

EvdSocketGroup *
evd_socket_group_new (void)
{
  EvdSocketGroup *self;

  self = g_object_new (EVD_TYPE_SOCKET_GROUP, NULL);

  return self;
}

