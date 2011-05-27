/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: David Zeuthen <davidz@redhat.com>
 */

#include "config.h"
#include <glib/gi18n-lib.h>

#include <rest/rest-proxy.h>
#include <json-glib/json-glib.h>

#include "goaprovider.h"
#include "goaoauth2provider.h"
#include "goafacebookprovider.h"

/**
 * GoaFacebookProvider:
 *
 * The #GoaFacebookProvider structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _GoaFacebookProvider
{
  /*< private >*/
  GoaOAuth2Provider parent_instance;
};

typedef struct _GoaFacebookProviderClass GoaFacebookProviderClass;

struct _GoaFacebookProviderClass
{
  GoaOAuth2ProviderClass parent_class;
};

/**
 * SECTION:goafacebookprovider
 * @title: GoaFacebookProvider
 * @short_description: A provider for Facebook
 *
 * #GoaFacebookProvider is used for handling Facebook accounts.
 */

G_DEFINE_TYPE_WITH_CODE (GoaFacebookProvider, goa_facebook_provider, GOA_TYPE_OAUTH2_PROVIDER,
                         g_io_extension_point_implement (GOA_PROVIDER_EXTENSION_POINT_NAME,
							 g_define_type_id,
							 "facebook",
							 0));

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
get_provider_type (GoaProvider *_provider)
{
  return "facebook";
}

static const gchar *
get_name (GoaProvider *_provider)
{
  return _("Facebook Account");
}

static const gchar *
get_authorization_uri (GoaOAuth2Provider *provider)
{
  return "https://www.facebook.com/dialog/oauth";
}


static const gchar *
get_token_uri (GoaOAuth2Provider *provider)
{
  return "https://graph.facebook.com/oauth/access_token";
}


static const gchar *
get_redirect_uri (GoaOAuth2Provider *provider)
{
  return "https://www.gnome.org/goa-1.0/oauth2?callback=1";
}

static const gchar *
get_scope (GoaOAuth2Provider *provider)
{
  /* see https://developers.facebook.com/docs/authentication/permissions/ */
  return
    "user_events,"
    "read_mailbox,"
    "offline_access";
}

static const gchar *
get_client_id (GoaOAuth2Provider *provider)
{
  return "103995033022129";
}

static const gchar *
get_client_secret (GoaOAuth2Provider *provider)
{
  return "c3a9f8d49188a6dd8b596df48a90b94a";
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
get_identity_sync (GoaOAuth2Provider  *provider,
                   const gchar        *access_token,
                   gchar             **out_presentation_identity,
                   GCancellable       *cancellable,
                   GError            **error)
{
  RestProxy *proxy;
  RestProxyCall *call;
  JsonParser *parser;
  JsonObject *json_object;
  gchar *ret;
  gchar *id;
  gchar *presentation_identity;

  ret = NULL;
  proxy = NULL;
  call = NULL;
  parser = NULL;
  id = NULL;
  presentation_identity = NULL;

  /* TODO: cancellable */

  proxy = rest_proxy_new ("https://graph.facebook.com/me", FALSE);
  call = rest_proxy_new_call (proxy);
  rest_proxy_call_set_method (call, "GET");
  rest_proxy_call_add_param (call, "access_token", access_token);

  if (!rest_proxy_call_sync (call, error))
    goto out;
  if (rest_proxy_call_get_status_code (call) != 200)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Expected status 200 when requesting guid, instead got status %d (%s)"),
                   rest_proxy_call_get_status_code (call),
                   rest_proxy_call_get_status_message (call));
      goto out;
    }

  parser = json_parser_new ();
  if (!json_parser_load_from_data (parser,
                                   rest_proxy_call_get_payload (call),
                                   rest_proxy_call_get_payload_length (call),
                                   error))
    {
      g_prefix_error (error, _("Error parsing response as JSON: "));
      goto out;
    }

  json_object = json_node_get_object (json_parser_get_root (parser));
  id = g_strdup (json_object_get_string_member (json_object, "id"));
  if (id == NULL)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Didn't find username member in JSON data"));
      goto out;
    }
  presentation_identity = g_strdup (json_object_get_string_member (json_object, "username"));
  if (presentation_identity == NULL)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Didn't find name member in JSON data"));
      goto out;
    }

  ret = id;
  id = NULL;
  if (out_presentation_identity != NULL)
    {
      *out_presentation_identity = presentation_identity;
      presentation_identity = NULL;
    }

 out:
  g_free (id);
  g_free (presentation_identity);
  if (call != NULL)
    g_object_unref (call);
  if (proxy != NULL)
    g_object_unref (proxy);
  return ret;
}


/* ---------------------------------------------------------------------------------------------------- */

static gboolean
build_object (GoaProvider         *provider,
              GoaObjectSkeleton   *object,
              GKeyFile            *key_file,
              const gchar         *group,
              GError             **error)
{
  gboolean ret;

  /* Chain up */
  if (!GOA_PROVIDER_CLASS (goa_facebook_provider_parent_class)->build_object (provider,
                                                                              object,
                                                                              key_file,
                                                                              group,
                                                                              error))
    goto out;

  ret = TRUE;

 out:
  return ret;
}

static gboolean
get_use_external_browser (GoaOAuth2Provider *provider)
{
  return FALSE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
show_account (GoaProvider         *provider,
              GoaClient           *client,
              GoaObject           *object,
              GtkBox              *vbox,
              GtkTable            *table)
{
  /* Chain up */
  GOA_PROVIDER_CLASS (goa_facebook_provider_parent_class)->show_account (provider, client, object, vbox, table);

  goa_util_add_row_editable_label_from_keyfile (table, object, _("User Name"), "PresentationIdentity", FALSE);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_facebook_provider_init (GoaFacebookProvider *client)
{
}

static void
goa_facebook_provider_class_init (GoaFacebookProviderClass *klass)
{
  GoaProviderClass *provider_class;
  GoaOAuth2ProviderClass *oauth2_class;

  provider_class = GOA_PROVIDER_CLASS (klass);
  provider_class->get_provider_type          = get_provider_type;
  provider_class->get_name                   = get_name;
  provider_class->build_object               = build_object;
  provider_class->show_account               = show_account;

  oauth2_class = GOA_OAUTH2_PROVIDER_CLASS (klass);
  oauth2_class->get_authorization_uri    = get_authorization_uri;
  oauth2_class->get_token_uri            = get_token_uri;
  oauth2_class->get_redirect_uri         = get_redirect_uri;
  oauth2_class->get_scope                = get_scope;
  oauth2_class->get_client_id            = get_client_id;
  oauth2_class->get_client_secret        = get_client_secret;
  oauth2_class->get_identity_sync        = get_identity_sync;
  oauth2_class->get_use_external_browser = get_use_external_browser;
}
