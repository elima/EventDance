/*
 * evd-inet-socket.c
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

#include "evd-inet-socket.h"
#include "evd-socket-protected.h"

#define DOMAIN_QUARK_STRING "org.eventdance.glib.inet-socket"

G_DEFINE_TYPE (EvdInetSocket, evd_inet_socket, EVD_TYPE_SOCKET)

typedef enum
{
  ACTION_BIND,
  ACTION_CONNECT
} SocketAction;

typedef struct
{
  EvdInetSocket *socket;
  guint          port;
  SocketAction   action;
  gboolean       allow_reuse;
} AsyncActionData;

static void     evd_inet_socket_class_init         (EvdInetSocketClass *class);
static void     evd_inet_socket_init               (EvdInetSocket *self);

static void     evd_inet_socket_finalize           (GObject *obj);
static void     evd_inet_socket_dispose            (GObject *obj);

static void     evd_inet_socket_set_property       (GObject      *obj,
						    guint         prop_id,
						    const GValue *value,
						    GParamSpec   *pspec);
static void     evd_inet_socket_get_property       (GObject    *obj,
						    guint       prop_id,
						    GValue     *value,
						    GParamSpec *pspec);

static void
evd_inet_socket_class_init (EvdInetSocketClass *class)
{
  GObjectClass *obj_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_inet_socket_dispose;
  obj_class->finalize = evd_inet_socket_finalize;
  obj_class->get_property = evd_inet_socket_get_property;
  obj_class->set_property = evd_inet_socket_set_property;
}

static void
evd_inet_socket_init (EvdInetSocket *self)
{
}

static void
evd_inet_socket_dispose (GObject *obj)
{
  G_OBJECT_CLASS (evd_inet_socket_parent_class)->dispose (obj);
}

static void
evd_inet_socket_finalize (GObject *obj)
{
  G_OBJECT_CLASS (evd_inet_socket_parent_class)->finalize (obj);
}

static void
evd_inet_socket_set_property (GObject      *obj,
			     guint         prop_id,
			     const GValue *value,
			     GParamSpec   *pspec)
{
  EvdInetSocket *self;

  self = EVD_INET_SOCKET (obj);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_inet_socket_get_property (GObject    *obj,
			     guint       prop_id,
			     GValue     *value,
			     GParamSpec *pspec)
{
  EvdInetSocket *self;

  self = EVD_INET_SOCKET (obj);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_inet_socket_on_resolver_result (GResolver    *resolver,
				    GAsyncResult *res,
				    gpointer      user_data)
{
  AsyncActionData *data = (AsyncActionData *) user_data;
  EvdInetSocket *self;
  EvdSocketState status;
  GList *result;
  GError *error = NULL;

  self = data->socket;
  status = evd_socket_get_status (EVD_SOCKET (self));

  if (status == EVD_SOCKET_CLOSED)
    return;

  if ((result = g_resolver_lookup_by_name_finish (resolver,
						  res,
						  &error)) != NULL)
    {
      GList *node = result;
      GSocketFamily family;
      gboolean done = FALSE;

      g_object_get (self, "family", &family, NULL);

      while ( (node != NULL) && (! done) )
	{
	  GInetAddress *addr;

	  addr = G_INET_ADDRESS (node->data);
	  if ( (family == G_SOCKET_FAMILY_INVALID) ||
	       (family == g_inet_address_get_family (addr)) )
	    {
	      GSocketAddress *sock_addr;
	      GError *error = NULL;

	      done = TRUE;

	      sock_addr = g_inet_socket_address_new (addr, data->port);

	      if ( (data->action == ACTION_CONNECT) &&
		   (status == EVD_SOCKET_CONNECTING) )
		{
		  if (! evd_socket_connect_to (EVD_SOCKET (self),
					       sock_addr,
					       &error))
		    {
		      /* TODO: emit 'error' signal */
		      g_debug ("ERROR: failed to connect to address");
		    }
		}
	      else
		if ( (data->action == ACTION_BIND) &&
		     (status == EVD_SOCKET_BINDING) )
		{
		  if (! evd_socket_bind (EVD_SOCKET (self),
					 sock_addr,
					 data->allow_reuse,
					 &error))
		    {
		      /* TODO: emit 'error' signal */
		      g_debug ("ERROR: failed to connect to address");
		    }
		}

	      g_object_unref (sock_addr);
	    }

	  node = node->next;
	}

      g_resolver_free_addresses (result);
      g_free (data);
    }
  else
    {
      /* TODO: emit 'error' signal */
    }
}

static gboolean
evd_inet_socket_resolve_and_do (EvdInetSocket    *self,
				const gchar      *address,
				guint             port,
				gboolean          allow_reuse,
				SocketAction      action,
				GError          **error)
{
  GInetAddress *addr;
  gchar *_addr;

  if (g_strcmp0 (address, "*") == 0)
    {
      if (evd_socket_get_family (EVD_SOCKET (self)) == G_SOCKET_FAMILY_IPV4)
	_addr = g_strdup ("0.0.0.0");
      else
	_addr = g_strdup ("::0");
    }
  else
    _addr = g_strdup (address);

  addr = g_inet_address_new_from_string (_addr);
  g_free (_addr);
  if (addr != NULL)
    {
      GSocketAddress *sock_addr;
      gboolean result = FALSE;

      sock_addr = g_inet_socket_address_new (addr, port);

      if (action == ACTION_CONNECT)
	result = evd_socket_connect_to (EVD_SOCKET (self),
					sock_addr,
					error);
      else
	if (action == ACTION_BIND)
	result = evd_socket_bind (EVD_SOCKET (self),
				  sock_addr,
				  allow_reuse,
				  error);

      g_object_unref (sock_addr);
      g_object_unref (addr);

      return result;
    }
  else
    {
      AsyncActionData *data;
      GResolver *resolver;

      data = g_new0 (AsyncActionData, 1);
      data->socket = self;
      data->port = port;
      data->action = action;
      data->allow_reuse = allow_reuse;

      if (action == ACTION_CONNECT)
	evd_socket_set_status (EVD_SOCKET (self), EVD_SOCKET_CONNECTING);
      else
	evd_socket_set_status (EVD_SOCKET (self), EVD_SOCKET_BINDING);

      resolver = g_resolver_get_default ();
      g_resolver_lookup_by_name_async (resolver,
		       address,
		       NULL,
		       (GAsyncReadyCallback) evd_inet_socket_on_resolver_result,
		       (gpointer) data);

      return TRUE;
    }
}

static void
evd_inet_socket_on_bind (EvdInetSocket *self, gpointer user_data)
{
  GError *error;

  g_signal_handlers_disconnect_by_func (self,
					G_CALLBACK (evd_inet_socket_on_bind),
					user_data);

  if (! evd_socket_listen (EVD_SOCKET (self), &error))
    {
      /* TODO: emit 'error' signal */
      g_debug ("ERROR: failed to listen: %s", error->message);
    }
}

/* public methods */

EvdInetSocket *
evd_inet_socket_new (void)
{
  EvdInetSocket *self;

  self = g_object_new (EVD_TYPE_INET_SOCKET, NULL);

  return self;
}

gboolean
evd_inet_socket_connect_to (EvdInetSocket  *self,
			    const gchar    *address,
			    guint           port,
			    GError        **error)
{
  g_return_val_if_fail (EVD_IS_INET_SOCKET (self), FALSE);

  return evd_inet_socket_resolve_and_do (self,
					 address,
					 port,
					 FALSE,
					 ACTION_CONNECT,
					 error);
}

gboolean
evd_inet_socket_bind (EvdInetSocket  *self,
		      const gchar    *address,
		      guint           port,
		      gboolean        allow_reuse,
		      GError        **error)
{
  g_return_val_if_fail (EVD_IS_INET_SOCKET (self), FALSE);

  return evd_inet_socket_resolve_and_do (self,
					 address,
					 port,
					 allow_reuse,
					 ACTION_BIND,
					 error);
}

gboolean
evd_inet_socket_listen (EvdInetSocket  *self,
			const gchar    *address,
			guint           port,
			GError        **error)
{
  g_signal_connect (self, "bind", G_CALLBACK (evd_inet_socket_on_bind), self);

  return evd_inet_socket_bind (self, address, port, TRUE, error);
}
