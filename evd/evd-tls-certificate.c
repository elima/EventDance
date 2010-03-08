/*
 * evd-tls-certificate.c
 *
 * EventDance - An event distribution framework (http://eventdance.org)
 *
 * Copyright (C) 2009/2010, Igalia S.L.
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
 * Lesser General Public License at http://www.gnu.org/licenses/lgpl-3.0.txt
 * for more details.
 *
 */

#include <gnutls/x509.h>
#include <gnutls/openpgp.h>

#include "evd-tls-common.h"
#include "evd-tls-certificate.h"

#define DOMAIN_QUARK_STRING "org.eventdance.lib.tls-certificate"

G_DEFINE_TYPE (EvdTlsCertificate, evd_tls_certificate, G_TYPE_OBJECT)

#define EVD_TLS_CERTIFICATE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                              EVD_TYPE_TLS_CERTIFICATE, \
                                              EvdTlsCertificatePrivate))

/* private data */
struct _EvdTlsCertificatePrivate
{
  gnutls_x509_crt_t    x509_cert;
  gnutls_openpgp_crt_t openpgp_cert;
};


/* properties */
enum
{
  PROP_0,
};

static void     evd_tls_certificate_class_init         (EvdTlsCertificateClass *class);
static void     evd_tls_certificate_init               (EvdTlsCertificate *self);

static void     evd_tls_certificate_finalize           (GObject *obj);
static void     evd_tls_certificate_dispose            (GObject *obj);

static void     evd_tls_certificate_set_property       (GObject      *obj,
                                                        guint         prop_id,
                                                        const GValue *value,
                                                        GParamSpec   *pspec);
static void     evd_tls_certificate_get_property       (GObject    *obj,
                                                        guint       prop_id,
                                                        GValue     *value,
                                                        GParamSpec *pspec);

static void
evd_tls_certificate_class_init (EvdTlsCertificateClass *class)
{
  GObjectClass *obj_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_tls_certificate_dispose;
  obj_class->finalize = evd_tls_certificate_finalize;
  obj_class->get_property = evd_tls_certificate_get_property;
  obj_class->set_property = evd_tls_certificate_set_property;

  /* install properties */

  /* add private structure */
  g_type_class_add_private (obj_class, sizeof (EvdTlsCertificatePrivate));
}

static void
evd_tls_certificate_init (EvdTlsCertificate *self)
{
  EvdTlsCertificatePrivate *priv;

  priv = EVD_TLS_CERTIFICATE_GET_PRIVATE (self);
  self->priv = priv;

  /* initialize private members */
  priv->x509_cert    = NULL;
  priv->openpgp_cert = NULL;
}

static void
evd_tls_certificate_dispose (GObject *obj)
{
  G_OBJECT_CLASS (evd_tls_certificate_parent_class)->dispose (obj);
}

static void
evd_tls_certificate_finalize (GObject *obj)
{
  EvdTlsCertificate *self = EVD_TLS_CERTIFICATE (obj);

  if (self->priv->x509_cert != NULL)
    gnutls_x509_crt_deinit (self->priv->x509_cert);

  if (self->priv->openpgp_cert != NULL)
    gnutls_openpgp_crt_deinit (self->priv->openpgp_cert);

  G_OBJECT_CLASS (evd_tls_certificate_parent_class)->finalize (obj);
}

static void
evd_tls_certificate_set_property (GObject      *obj,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  EvdTlsCertificate *self;

  self = EVD_TLS_CERTIFICATE (obj);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_tls_certificate_get_property (GObject    *obj,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  EvdTlsCertificate *self;

  self = EVD_TLS_CERTIFICATE (obj);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

/* public methods */

EvdTlsCertificate *
evd_tls_certificate_new (void)
{
  EvdTlsCertificate *self;

  self = g_object_new (EVD_TYPE_TLS_CERTIFICATE, NULL);

  return self;
}
