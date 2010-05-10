/*
 * evd-resolver-request.c
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

#include <stdio.h>
#include <string.h>

#ifdef HAVE_GIO_UNIX
#include <gio/gunixsocketaddress.h>
#endif

#include "evd-utils.h"
#include "evd-resolver.h"
#include "evd-resolver-request.h"

G_DEFINE_TYPE (EvdResolverRequest, evd_resolver_request, G_TYPE_OBJECT)

#define EVD_RESOLVER_REQUEST_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                               EVD_TYPE_RESOLVER_REQUEST, \
                                               EvdResolverRequestPrivate))

struct _EvdResolverRequestPrivate
{
  gchar       *address;
  GClosure    *closure;
  EvdResolver *resolver;

  gchar  *host;
  guint   port;
  GList  *socket_addresses;
  GError *error;

  GCancellable *cancellable;
  guint         src_id;

  gboolean active;
};

/* properties */
enum
{
  PROP_0,
  PROP_ADDRESS,
  PROP_CLOSURE,
  PROP_PORT
};

static void     evd_resolver_request_class_init         (EvdResolverRequestClass *class);
static void     evd_resolver_request_init               (EvdResolverRequest *self);

static void     evd_resolver_request_finalize           (GObject *obj);
static void     evd_resolver_request_dispose            (GObject *obj);

static void     evd_resolver_request_set_property       (GObject      *obj,
                                                         guint         prop_id,
                                                         const GValue *value,
                                                         GParamSpec   *pspec);
static void     evd_resolver_request_get_property       (GObject    *obj,
                                                         guint       prop_id,
                                                         GValue     *value,
                                                         GParamSpec *pspec);

static void
evd_resolver_request_class_init (EvdResolverRequestClass *class)
{
  GObjectClass *obj_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_resolver_request_dispose;
  obj_class->finalize = evd_resolver_request_finalize;
  obj_class->get_property = evd_resolver_request_get_property;
  obj_class->set_property = evd_resolver_request_set_property;

  /* install properties */
  g_object_class_install_property (obj_class, PROP_ADDRESS,
                                   g_param_spec_string ("address",
                                                        "The address to resolve",
                                                        "A string that represent the address to resolve",
                                                        "",
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_CLOSURE,
                                   g_param_spec_boxed ("closure",
                                                       "The callback to call upon completion",
                                                       "A closure to the user function and data to call when request completes",
                                                       G_TYPE_CLOSURE,
                                                       G_PARAM_READWRITE |
                                                       G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_PORT,
                                   g_param_spec_uint ("port",
                                                      "The port for inet socket addresses",
                                                      "An internet protocol port",
                                                      0,
                                                      G_MAXUINT16,
                                                      0,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (obj_class, sizeof (EvdResolverRequestPrivate));
}

static void
evd_resolver_request_init (EvdResolverRequest *self)
{
  EvdResolverRequestPrivate *priv;

  priv = EVD_RESOLVER_REQUEST_GET_PRIVATE (self);
  self->priv = priv;

  priv->address = NULL;
  priv->closure = NULL;
  priv->resolver = NULL;

  priv->port = 0;
  priv->socket_addresses = NULL;
  priv->error = NULL;

  priv->cancellable = NULL;
  priv->src_id = 0;
  priv->active = FALSE;
}

static void
evd_resolver_request_dispose (GObject *obj)
{
  EvdResolverRequest *self = EVD_RESOLVER_REQUEST (obj);

  if (self->priv->resolver != NULL)
    {
      g_object_unref (self->priv->resolver);
      self->priv->resolver = NULL;
    }

  G_OBJECT_CLASS (evd_resolver_request_parent_class)->dispose (obj);
}

static void
evd_resolver_request_finalize (GObject *obj)
{
  EvdResolverRequest *self = EVD_RESOLVER_REQUEST (obj);

  evd_resolver_request_cancel (self);

  if (self->priv->address != NULL)
    g_free (self->priv->address);

  if (self->priv->host != NULL)
    g_free (self->priv->host);

  if (self->priv->closure != NULL)
    g_closure_unref (self->priv->closure);

  G_OBJECT_CLASS (evd_resolver_request_parent_class)->finalize (obj);
}

static void
evd_resolver_request_set_property (GObject      *obj,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  EvdResolverRequest *self;

  self = EVD_RESOLVER_REQUEST (obj);

  switch (prop_id)
    {
    case PROP_ADDRESS:
      if (self->priv->address != NULL)
        g_free (self->priv->address);
      self->priv->address = g_strdup (g_value_get_string (value));
      break;

    case PROP_CLOSURE:
      if (self->priv->closure != NULL)
        g_closure_unref (self->priv->closure);
      self->priv->closure = g_value_get_boxed (value);
      if (self->priv->closure != NULL)
        {
          g_closure_ref (self->priv->closure);
          g_closure_sink (self->priv->closure);
        }
      break;

    case PROP_PORT:
      self->priv->port = g_value_get_uint (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_resolver_request_get_property (GObject    *obj,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  EvdResolverRequest *self;

  self = EVD_RESOLVER_REQUEST (obj);

  switch (prop_id)
    {
    case PROP_ADDRESS:
      g_value_set_string (value, self->priv->address);
      break;

    case PROP_CLOSURE:
      g_value_set_boxed (value, self->priv->closure);
      break;

    case PROP_PORT:
      g_value_set_uint (value, self->priv->port);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_resolver_request_invoke_closure (EvdResolverRequest *self)
{
  GValue params[2] = { {0, } };

  self->priv->active = FALSE;

  g_value_init (&params[0], EVD_TYPE_RESOLVER);
  g_value_set_object (&params[0], self->priv->resolver);

  g_value_init (&params[1], EVD_TYPE_RESOLVER_REQUEST);
  g_value_set_object (&params[1], self);

  g_object_ref (self);
  g_closure_invoke (self->priv->closure, NULL, 2, params, NULL);
  g_object_unref (self);

  g_value_unset (&params[0]);
  g_value_unset (&params[1]);
}

static void
evd_resolver_request_on_resolver_result (GResolver    *resolver,
                                         GAsyncResult *res,
                                         gpointer      user_data)
{
  EvdResolverRequest *self;
  GList *result = NULL;
  GError *error = NULL;

  self = EVD_RESOLVER_REQUEST (user_data);
  g_object_unref (self);

  if (self->priv->cancellable == NULL)
    return;

  self->priv->active = FALSE;

  if ((result = g_resolver_lookup_by_name_finish (resolver,
						  res,
						  &error)) != NULL)
    {
      GList *node = result;
      GInetAddress *inet_addr;
      GSocketAddress *addr;

      while (node != NULL)
        {
          inet_addr = G_INET_ADDRESS (node->data);

          addr = g_inet_socket_address_new (inet_addr, self->priv->port);
          g_object_ref_sink (addr);
          self->priv->socket_addresses = g_list_append (self->priv->socket_addresses,
                                                        (gpointer) addr);

          node = node->next;
        }

      g_resolver_free_addresses (result);
    }
  else if (error->code == G_IO_ERROR_CANCELLED)
    {
      return;
    }
  else
    {
      self->priv->error = error;
    }

  evd_resolver_request_invoke_closure (self);
}

/* public methods */

void
evd_resolver_request_set_resolver (EvdResolverRequest *self,
                                   gpointer           *resolver)
{
  g_return_if_fail (EVD_IS_RESOLVER_REQUEST (self));
  g_return_if_fail (EVD_IS_RESOLVER (resolver) || resolver == NULL);

  if (self->priv->resolver != NULL)
    g_object_unref (self->priv->resolver);

  self->priv->resolver = EVD_RESOLVER (resolver);

  if (resolver != NULL)
    g_object_ref (self->priv->resolver);
}

static gboolean
evd_resolver_request_perform (gpointer user_data)
{
  EvdResolverRequest *self = EVD_RESOLVER_REQUEST (user_data);
  gchar *address;
  GInetAddress *inet_addr;
  GSocketAddress *addr;

  address = self->priv->address;

  if (address[0] == '/')
    {
#ifdef HAVE_GIO_UNIX
      /* assume unix address */
      /* TODO: improve this detection, seems very naive */
      addr = (GSocketAddress *) g_unix_socket_address_new (address);
      self->priv->socket_addresses = g_list_append (NULL, (gpointer) addr);
#else
      /* TODO: build an error. Unix sockets are not supported */
#endif
      evd_resolver_request_invoke_closure (self);

      return FALSE;
    }

  /* at this point we have a valid port, and a host to validate,
     so let's build the request */
  inet_addr = g_inet_address_new_from_string (self->priv->host);
  if (inet_addr != NULL)
    {
      addr = g_inet_socket_address_new (inet_addr, self->priv->port);
      self->priv->socket_addresses = g_list_append (NULL, (gpointer) addr);

      evd_resolver_request_invoke_closure (self);
    }
  else
    {
      GCancellable *cancellable;

      cancellable = g_cancellable_new ();
      self->priv->cancellable = cancellable;

      g_object_ref (self);

      g_resolver_lookup_by_name_async (g_resolver_get_default (),
                 self->priv->host,
                 cancellable,
                 (GAsyncReadyCallback) evd_resolver_request_on_resolver_result,
                 (gpointer) self);
    }

  return FALSE;
}

void
evd_resolver_request_resolve (EvdResolverRequest  *self)
{
  gchar *address;
  gchar *host;
  gchar *split;
  gchar *port_str;
  gint   port;

  g_return_if_fail (EVD_IS_RESOLVER_REQUEST (self));
  g_return_if_fail (self->priv->address != NULL);

  /* clean-up any previous resolution data  */
  evd_resolver_request_cancel (self);

  self->priv->active = TRUE;

  address = self->priv->address;
  host = g_strdup (address);

  if (address[0] != '/')
    {
      /* at this point address is expected to be of type inet
         and in the form host:port, where host can be also a domain name */
      split = g_strrstr (host, ":");
      if (split != NULL)
        {
          split[0] = '\0';
          port_str = (char *) ((void *) split) + 1;

          if (strlen (port_str) == 0 || (sscanf (port_str, "%d", &port) == 0) )
            {
              g_free (host);
              host = g_strdup (address);
            }
          else
            {
              self->priv->port = port;
            }
        }
    }

  self->priv->host = host;

  self->priv->src_id =
    evd_timeout_add (g_main_context_get_thread_default (),
                     0,
                     G_PRIORITY_DEFAULT,
                     (GSourceFunc) evd_resolver_request_perform,
                     (gpointer) self);
}

GList *
evd_resolver_request_get_result (EvdResolverRequest  *self,
                                 GError             **error)
{
  if (self->priv->error != NULL)
    {
      if (error != NULL)
        *error = g_error_copy (self->priv->error);

      return NULL;
    }
  else
    {
      GList *list = NULL;
      GList *node;

      g_return_val_if_fail (EVD_IS_RESOLVER_REQUEST (self), NULL);

      node = self->priv->socket_addresses;
      while (node != NULL)
        {
          list = g_list_append (list, node->data);
          g_object_ref (G_OBJECT (node->data));

          node = node->next;
        }

      return list;
    }
}

void
evd_resolver_request_cancel (EvdResolverRequest *self)
{
  g_return_if_fail (EVD_IS_RESOLVER_REQUEST (self));

  if (self->priv->cancellable != NULL)
    {
      g_cancellable_cancel (self->priv->cancellable);
      g_object_unref (self->priv->cancellable);
      self->priv->cancellable = NULL;
    }
  else if (self->priv->src_id != 0)
    {
      g_source_remove (self->priv->src_id);
      self->priv->src_id = 0;
    }

  self->priv->active = FALSE;

  evd_resolver_free_addresses (self->priv->socket_addresses);
  self->priv->socket_addresses = NULL;

  if (self->priv->error != NULL)
    {
      g_error_free (self->priv->error);
      self->priv->error = NULL;
    }
}

gboolean
evd_resolver_request_is_active (EvdResolverRequest *self)
{
  return self->priv->active;
}
