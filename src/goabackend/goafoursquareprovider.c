/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2014 Red Hat, Inc.
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
 * Public License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"
#include <glib/gi18n-lib.h>

#include <rest/rest-proxy.h>
#include <json-glib/json-glib.h>

#include "goaprovider.h"
#include "goaprovider-priv.h"
#include "goaoauth2provider.h"
#include "goafoursquareprovider.h"

/**
 * GoaFoursquareProvider:
 *
 * The #GoaFoursquareProvider structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _GoaFoursquareProvider
{
  /*< private >*/
  GoaOAuth2Provider parent_instance;
};

typedef struct _GoaFoursquareProviderClass GoaFoursquareProviderClass;

struct _GoaFoursquareProviderClass
{
  GoaOAuth2ProviderClass parent_class;
};

/**
 * SECTION:goafoursquareprovider
 * @title: GoaFoursquareProvider
 * @short_description: A provider for Foursquare
 *
 * #GoaFoursquareProvider is used for handling Foursquare accounts.
 */

G_DEFINE_TYPE_WITH_CODE (GoaFoursquareProvider, goa_foursquare_provider, GOA_TYPE_OAUTH2_PROVIDER,
                         goa_provider_ensure_extension_points_registered ();
                         g_io_extension_point_implement (GOA_PROVIDER_EXTENSION_POINT_NAME,
                             g_define_type_id,
                             "foursquare",
                             0));

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
get_provider_type (GoaProvider *_provider)
{
  return "foursquare";
}

static gchar *
get_provider_name (GoaProvider *_provider,
                   GoaObject   *object)
{
  return g_strdup (_("Foursquare"));
}

static GoaProviderGroup
get_provider_group (GoaProvider *_provider)
{
  return GOA_PROVIDER_GROUP_BRANDED;
}

static GoaProviderFeatures
get_provider_features (GoaProvider *_provider)
{
  return GOA_PROVIDER_FEATURE_BRANDED |
         GOA_PROVIDER_FEATURE_MAPS;
}

static gchar *
build_authorization_uri (GoaOAuth2Provider  *provider,
                         const gchar        *authorization_uri,
                         const gchar        *escaped_redirect_uri,
                         const gchar        *escaped_client_id,
                         const gchar        *escaped_scope)
{
  gchar *uri;

  uri = g_strdup_printf ("%s"
                          "?response_type=token"
                          "&redirect_uri=%s"
                          "&client_id=%s",
                          authorization_uri,
                          escaped_redirect_uri,
                          escaped_client_id);
  return uri;
}

static const gchar *
get_authorization_uri (GoaOAuth2Provider *provider)
{
  return "https://foursquare.com/oauth2/authenticate";
}

static const gchar *
get_redirect_uri (GoaOAuth2Provider *provider)
{
  return "https://localhost/";
}

static guint
get_credentials_generation (GoaProvider *provider)
{
  return 1;
}

static const gchar *
get_client_id (GoaOAuth2Provider *provider)
{
  return GOA_FOURSQUARE_CLIENT_ID;
}

static const gchar *
get_client_secret (GoaOAuth2Provider *provider)
{
  /* The client secret is not used in the Foursquare Token Flow
   * that is the recommended flow for serverless apps. */
  return NULL;
}

gboolean
get_use_mobile_browser (GoaOAuth2Provider *provider)
{
  /* Foursquare needs to identify the browser as mobile to
   * provide a better interface for GOA */
  return TRUE;
}

static const gchar *
get_authentication_cookie (GoaOAuth2Provider *provider)
{
  return "bbhive";
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
get_identity_sync (GoaOAuth2Provider  *provider,
                   const gchar        *access_token,
                   gchar             **out_presentation_identity,
                   GCancellable       *cancellable,
                   GError            **error)
{
  GError *identity_error;
  RestProxy *proxy;
  RestProxyCall *call;
  JsonParser *parser;
  JsonObject *json_object;
  JsonObject *json_contact_object;
  gchar *ret;
  gchar *id;
  gchar *presentation_identity;

  ret = NULL;

  identity_error = NULL;
  proxy = NULL;
  call = NULL;
  parser = NULL;
  id = NULL;
  presentation_identity = NULL;

  /* TODO: cancellable */

  proxy = rest_proxy_new ("https://api.foursquare.com/v2/users/self", FALSE);
  call = rest_proxy_new_call (proxy);
  rest_proxy_call_set_method (call, "GET");
  rest_proxy_call_add_param (call, "oauth_token", access_token);

  /* See https://developer.foursquare.com/overview/versioning */
  rest_proxy_call_add_param (call, "v", "20140226");

  if (!rest_proxy_call_sync (call, error))
  {
    printf("1\n%d\n%s\n", rest_proxy_call_get_status_code (call), rest_proxy_call_get_status_message (call));
    goto out;
  }
  if (rest_proxy_call_get_status_code (call) != 200)
    {
      printf("2\n");
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Expected status 200 when requesting your identity, instead got status %d (%s)"),
                   rest_proxy_call_get_status_code (call),
                   rest_proxy_call_get_status_message (call));
      goto out;
    }

  parser = json_parser_new ();
  if (!json_parser_load_from_data (parser,
                                   rest_proxy_call_get_payload (call),
                                   rest_proxy_call_get_payload_length (call),
                                   &identity_error))
    {
      printf("3\n");
      g_warning ("json_parser_load_from_data() failed: %s (%s, %d)",
                 identity_error->message,
                 g_quark_to_string (identity_error->domain),
                 identity_error->code);
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Could not parse response"));
      goto out;
    }

  /* Get response.user member */
  json_object = json_node_get_object (json_parser_get_root (parser));
  json_object = json_object_get_object_member (json_object, "response");
  if (json_object == NULL)
    {
      printf("4\n");
      g_warning ("Did not find response object in JSON data");
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Could not parse response"));
      goto out;
    }
  json_object = json_object_get_object_member (json_object, "user");
  if (json_object == NULL)
    {
      printf("5\n");
      g_warning ("Did not find user object in JSON data");
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Could not parse response"));
      goto out;
    }

  /* Get response.user.id for the user identity */
  id = g_strdup (json_object_get_string_member (json_object, "id"));
  if (id == NULL)
    {
      printf("6\n");
      g_warning ("Did not find id in JSON data");
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Could not parse response"));
      goto out;
    }

  /* Get response.user.contact.email for the presentation identity */
  json_object = json_object_get_object_member (json_object, "contact");
  if (json_object == NULL)
    {
      printf("7\n");
      g_warning ("Did not find contact object in JSON data");
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Could not parse response"));
      goto out;
    }
  presentation_identity = g_strdup (json_object_get_string_member (json_object, "email"));
  if (presentation_identity == NULL)
    {
      printf("8\n");
      g_warning ("Did not find email in JSON data");
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Could not parse response"));
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
  g_clear_error (&identity_error);
  g_free (id);
  g_free (presentation_identity);
  if (parser != NULL)
    g_object_unref (parser);
  if (call != NULL)
    g_object_unref (call);
  if (proxy != NULL)
    g_object_unref (proxy);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
is_deny_node (GoaOAuth2Provider *provider, WebKitDOMNode *node)
{
  WebKitDOMElement *element;
  gboolean ret;
  gchar *id;

  id = NULL;
  ret = FALSE;

  if (!WEBKIT_DOM_IS_ELEMENT (node))
    goto out;

  element = WEBKIT_DOM_ELEMENT (node);

  id = webkit_dom_element_get_id (element);
  if (g_strcmp0 (id, "denyButton") != 0)
    goto out;

  ret = TRUE;

 out:
  g_free (id);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
is_identity_node (GoaOAuth2Provider *provider, WebKitDOMHTMLInputElement *element)
{
  gboolean ret;
  gchar *element_type;
  gchar *name;

  element_type = NULL;
  name = NULL;

  ret = FALSE;

  g_object_get (element, "type", &element_type, NULL);
  if (g_strcmp0 (element_type, "text") != 0)
    goto out;

  name = webkit_dom_html_input_element_get_name (element);
  if (g_strcmp0 (name, "emailOrPhone") != 0)
    goto out;

  ret = TRUE;

 out:
  g_free (element_type);
  g_free (name);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
build_object (GoaProvider         *provider,
              GoaObjectSkeleton   *object,
              GKeyFile            *key_file,
              const gchar         *group,
              GDBusConnection     *connection,
              gboolean             just_added,
              GError             **error)
{
  GoaAccount *account;
  GoaMaps *maps = NULL;
  gboolean maps_enabled;
  gboolean ret = FALSE;

  account = NULL;

  /* Chain up */
  if (!GOA_PROVIDER_CLASS (goa_foursquare_provider_parent_class)->build_object (provider,
                                                                              object,
                                                                              key_file,
                                                                              group,
                                                                              connection,
                                                                              just_added,
                                                                              error))
    goto out;

  account = goa_object_get_account (GOA_OBJECT (object));

  /* Check In */
  maps = goa_object_get_maps (GOA_OBJECT (object));
  maps_enabled = g_key_file_get_boolean (key_file, group, "MapsEnabled", NULL);

  if (maps_enabled)
    {
      if (maps == NULL)
        {
          maps = goa_maps_skeleton_new ();
          goa_object_skeleton_set_maps (object, maps);
        }
    }
  else
    {
      if (maps != NULL)
        goa_object_skeleton_set_maps (object, NULL);
    }

  if (just_added)
    {
      goa_account_set_maps_disabled (account, !maps_enabled);
      g_signal_connect (account,
                        "notify::maps-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        "MapsEnabled");
    }

  ret = TRUE;

 out:
  g_clear_object (&account);
  g_clear_object (&maps);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
show_account (GoaProvider         *provider,
              GoaClient           *client,
              GoaObject           *object,
              GtkBox              *vbox,
              GtkGrid             *grid,
              G_GNUC_UNUSED GtkGrid *dummy)
{
  gint row;

  row = 0;

  goa_util_add_account_info (grid, row++, object);

  goa_util_add_row_switch_from_keyfile_with_blurb (grid, row++, object,
                                                   /* Translators: This is a label for a series of
                                                    * options switches. For example: “Use for Mail”. */
                                                   _("Use for"),
                                                   "maps-disabled",
                                                   _("_Maps"));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_account_key_values (GoaOAuth2Provider *provider,
                        GVariantBuilder   *builder)
{
  g_variant_builder_add (builder, "{ss}", "MapsEnabled", "true");
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_foursquare_provider_init (GoaFoursquareProvider *client)
{
}

static void
goa_foursquare_provider_class_init (GoaFoursquareProviderClass *klass)
{
  GoaProviderClass *provider_class;
  GoaOAuth2ProviderClass *oauth2_class;

  provider_class = GOA_PROVIDER_CLASS (klass);
  provider_class->get_provider_type          = get_provider_type;
  provider_class->get_provider_name          = get_provider_name;
  provider_class->get_provider_group         = get_provider_group;
  provider_class->get_provider_features      = get_provider_features;
  provider_class->build_object               = build_object;
  provider_class->show_account               = show_account;
  provider_class->get_credentials_generation = get_credentials_generation;

  oauth2_class = GOA_OAUTH2_PROVIDER_CLASS (klass);
  oauth2_class->get_authorization_uri    = get_authorization_uri;
  oauth2_class->build_authorization_uri  = build_authorization_uri;
  oauth2_class->get_redirect_uri         = get_redirect_uri;
  oauth2_class->get_client_id            = get_client_id;
  oauth2_class->get_client_secret        = get_client_secret;
  oauth2_class->get_use_mobile_browser   = get_use_mobile_browser;
  oauth2_class->get_authentication_cookie = get_authentication_cookie;
  oauth2_class->get_identity_sync        = get_identity_sync;
  oauth2_class->is_deny_node             = is_deny_node;
  oauth2_class->is_identity_node         = is_identity_node;
  oauth2_class->add_account_key_values   = add_account_key_values;
}

