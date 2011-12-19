/*
 * evd-http-request.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2009/2010, Igalia S.L.
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

#include <libsoup/soup-method.h>

#include "evd-http-request.h"

#define EVD_HTTP_REQUEST_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                           EVD_TYPE_HTTP_REQUEST, \
                                           EvdHttpRequestPrivate))

G_DEFINE_TYPE (EvdHttpRequest, evd_http_request, EVD_TYPE_HTTP_MESSAGE)

/* private data */
struct _EvdHttpRequestPrivate
{
  gchar *method;
  SoupURI *uri;
};

/* properties */
enum
{
  PROP_0,
  PROP_METHOD,
  PROP_PATH,
  PROP_URI
};

static void     evd_http_request_class_init           (EvdHttpRequestClass *class);
static void     evd_http_request_init                 (EvdHttpRequest *self);

static void     evd_http_request_finalize             (GObject *obj);
static void     evd_http_request_dispose              (GObject *obj);

static void     evd_http_request_set_property         (GObject      *obj,
                                                       guint         prop_id,
                                                       const GValue *value,
                                                       GParamSpec   *pspec);
static void     evd_http_request_get_property         (GObject    *obj,
                                                       guint       prop_id,
                                                       GValue     *value,
                                                       GParamSpec *pspec);

static void
evd_http_request_class_init (EvdHttpRequestClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_http_request_dispose;
  obj_class->finalize = evd_http_request_finalize;
  obj_class->get_property = evd_http_request_get_property;
  obj_class->set_property = evd_http_request_set_property;

  g_object_class_install_property (obj_class, PROP_METHOD,
                                   g_param_spec_string ("method",
                                                        "Method",
                                                        "The HTTP method of the request (GET, POST, HEAD, etc)",
                                                        SOUP_METHOD_GET,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_PATH,
                                   g_param_spec_string ("path",
                                                        "URI path",
                                                        "The full path portion of the requested URL",
                                                        "",
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_URI,
                                   g_param_spec_boxed ("uri",
                                                       "Request URI",
                                                       "The URI of the requested resource",
                                                       SOUP_TYPE_URI,
                                                       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                                                       G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (obj_class, sizeof (EvdHttpRequestPrivate));
}

static void
evd_http_request_init (EvdHttpRequest *self)
{
  EvdHttpRequestPrivate *priv;

  priv = EVD_HTTP_REQUEST_GET_PRIVATE (self);
  self->priv = priv;
}

static void
evd_http_request_dispose (GObject *obj)
{
  G_OBJECT_CLASS (evd_http_request_parent_class)->dispose (obj);
}

static void
evd_http_request_finalize (GObject *obj)
{
  EvdHttpRequest *self = EVD_HTTP_REQUEST (obj);

  g_free (self->priv->method);

  if (self->priv->uri != NULL)
    soup_uri_free (self->priv->uri);

  G_OBJECT_CLASS (evd_http_request_parent_class)->finalize (obj);
}

static void
evd_http_request_set_property (GObject      *obj,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  EvdHttpRequest *self;

  self = EVD_HTTP_REQUEST (obj);

  switch (prop_id)
    {
    case PROP_METHOD:
      self->priv->method = g_value_dup_string (value);
      break;

    case PROP_URI:
      self->priv->uri = g_value_dup_boxed (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_http_request_get_property (GObject    *obj,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  EvdHttpRequest *self;

  self = EVD_HTTP_REQUEST (obj);

  switch (prop_id)
    {
    case PROP_METHOD:
      g_value_set_string (value, self->priv->method);
      break;

    case PROP_PATH:
      g_value_take_string (value, evd_http_request_get_path (self));
      break;

    case PROP_URI:
      g_value_set_boxed (value, self->priv->uri);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

/* public methods */

EvdHttpRequest *
evd_http_request_new (const gchar *method, const gchar *url)
{
  EvdHttpRequest *self;
  SoupURI *uri;

  uri = soup_uri_new (url);

  self = g_object_new (EVD_TYPE_HTTP_REQUEST,
                       "method", method,
                       "uri", uri,
                       NULL);
  soup_uri_free (uri);

  return self;
}

const gchar *
evd_http_request_get_method (EvdHttpRequest *self)
{
  g_return_val_if_fail (EVD_IS_HTTP_REQUEST (self), NULL);

  return self->priv->method;
}

gchar *
evd_http_request_get_path (EvdHttpRequest *self)
{
  g_return_val_if_fail (EVD_IS_HTTP_REQUEST (self), NULL);

  return soup_uri_to_string (self->priv->uri, TRUE);
}

/**
 * evd_http_request_get_uri:
 *
 * Returns: (transfer none):
 **/
SoupURI *
evd_http_request_get_uri (EvdHttpRequest *self)
{
  g_return_val_if_fail (EVD_IS_HTTP_REQUEST (self), NULL);

  return self->priv->uri;
}

/**
 * evd_http_request_to_string:
 * @size: (out):
 *
 * Returns: (transfer full):
 **/
gchar *
evd_http_request_to_string (EvdHttpRequest *self,
                            gsize          *size)
{
  SoupHTTPVersion version;

  gchar *headers_st;
  gsize headers_size;

  gchar *st;
  GString *buf;
  gchar *result;
  SoupMessageHeaders *headers;
  gchar *path;

  g_return_val_if_fail (EVD_IS_HTTP_REQUEST (self), NULL);

  version = evd_http_message_get_version (EVD_HTTP_MESSAGE (self));

  buf = g_string_new ("");

  /* send status line */
  path = evd_http_request_get_path (self);
  st = g_strdup_printf ("%s %s HTTP/1.%d\r\n",
                        self->priv->method,
                        path,
                        version);
  g_free (path);

  g_string_append_len (buf, st, strlen (st));
  g_free (st);

  headers = evd_http_message_get_headers (EVD_HTTP_MESSAGE (self));

  /* determine 'Host' header */
  if (soup_message_headers_get_one (headers, "Host") == NULL)
    {
      if (self->priv->uri->port == 80)
        st = g_strdup_printf ("%s", self->priv->uri->host);
      else
        st = g_strdup_printf ("%s:%d",
                              self->priv->uri->host,
                              self->priv->uri->port);
      soup_message_headers_replace (headers, "Host", st);
      g_free (st);
    }

  /* set 'User-Agent' header if not present.
     (some server won't answer the request if this is not set */
  if (soup_message_headers_get_one (headers, "User-Agent") == NULL)
    soup_message_headers_replace (headers, "User-Agent", "evd");

  headers_st = evd_http_message_headers_to_string (EVD_HTTP_MESSAGE (self),
                                                   &headers_size);
  g_string_append_len (buf, headers_st, headers_size);
  g_free (headers_st);

  g_string_append_len (buf, "\r\n", 2);

  if (size != NULL)
    *size = buf->len;

  result = buf->str;
  g_string_free (buf, FALSE);

  return result;
}

void
evd_http_request_set_basic_auth_credentials (EvdHttpRequest *self,
                                             const gchar    *user,
                                             const gchar    *passw)
{
  gchar *st;
  gchar *b64_st;
  SoupMessageHeaders *headers;

  g_return_if_fail (EVD_IS_HTTP_REQUEST (self));

  if (user == NULL)
    user = "";
  if (passw == NULL)
    passw = "";

  st = g_strdup_printf ("%s:%s", user, passw);
  b64_st = g_base64_encode ((guchar *) st, strlen (st));
  g_free (st);

  st = g_strdup_printf ("Basic %s", b64_st);
  g_free (b64_st);

  headers = evd_http_message_get_headers (EVD_HTTP_MESSAGE (self));
  soup_message_headers_replace (headers, "Authorization", st);
  g_free (st);
}

gboolean
evd_http_request_get_basic_auth_credentials (EvdHttpRequest  *self,
                                             gchar          **user,
                                             gchar          **password)
{
  SoupMessageHeaders *headers;
  const gchar *auth_st;
  gchar *st;
  gsize len;
  gchar **tokens = NULL;
  guint tokens_len;

  g_return_val_if_fail (EVD_IS_HTTP_REQUEST (self), FALSE);

  headers = evd_http_message_get_headers (EVD_HTTP_MESSAGE (self));

  auth_st = soup_message_headers_get_one (headers, "Authorization");
  if (auth_st == NULL || strlen (auth_st) < 7)
    return FALSE;

  st = (gchar *) g_base64_decode (auth_st + 6, &len);
  tokens = g_strsplit (st, ":", 2);
  g_free (st);

  tokens_len = g_strv_length (tokens);

  if (tokens_len > 0 && user != NULL)
    *user = g_strdup (tokens[0]);

  if (tokens_len > 1 && password != NULL)
    *password = g_strdup (tokens[1]);

  g_strfreev (tokens);

  return TRUE;
}

gchar *
evd_http_request_get_cookie_value (EvdHttpRequest *self,
                                   const gchar    *cookie_name)
{
  gchar *value = NULL;
  SoupMessageHeaders *headers;
  const gchar *cookie_str;
  gchar **cookies;
  gint i;

  g_return_val_if_fail (EVD_IS_HTTP_REQUEST (self), NULL);
  g_return_val_if_fail (cookie_name != NULL, NULL);

  headers = evd_http_message_get_headers (EVD_HTTP_MESSAGE (self));

  cookie_str = soup_message_headers_get_one (headers, "Cookie");
  if (cookie_str == NULL)
    return NULL;

  cookies = g_strsplit (cookie_str, ";", -1);

  i = 0;
  while (cookies[i] != NULL)
    {
      gchar *cookie;

      cookie = g_strstr_len (cookies[i], strlen (cookie_name) + 1, cookie_name);
      if (cookie == cookies[i] ||
          (cookie == cookies[i] + 1 && cookies[i][0] == ' '))
        {
          gchar **key_value;

          key_value = g_strsplit (cookie, "=", 2);
          value = g_strdup (key_value[1]);

          g_strfreev (key_value);

          break;
        }

      i++;
    }

  g_strfreev (cookies);

  return value;
}
