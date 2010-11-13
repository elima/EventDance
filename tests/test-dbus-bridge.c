/*
 * test-dbus-bridge.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2009/2010, Igalia S.L.
 *
 * Authors:
 *   Eduardo Lima Mitev <elima@igalia.com>
 */

#include <string.h>
#include <json-glib/json-glib.h>

#include <evd.h>

static const gchar *session_bus_addr;
static const gchar *addr_alias = "alias:abstract=/org/eventdance/lib/demo/dbus-bridge";

typedef struct
{
  gchar *test_name;
  gchar *phrases[20];
} TestCase;

struct Fixture
{
  EvdDBusBridge *bridge;
  GObject *obj1;
  GObject *obj2;
  gint i;
  TestCase *test_case;
  GMainLoop *main_loop;
};

static const TestCase test_cases[] =
  {
    { "error/invalid-message",
      {
        "",
        "[1,0,0,\"[1]\"]",
        "[]",
        "[1,0,0,\"[1]\"]",
        "[0,0,0,0]",
        "[1,0,0,\"[1]\"]",
        "[3,1,0,0]",
        "[1,0,0,\"[1]\"]"
      }
    },

    { "error/invalid-command",
      {
        "[0,1,0,\"\"]",
        "[1,1,0,\"[2]\"]",
        "[100,16,0,\"\"]",
        "[1,16,0,\"[2]\"]",
      }
    },

    { "error/invalid-arguments",
      {
        "[3,1,0,\"\"]",
        "[1,1,0,\"[4]\"]"
      }
    },

    { "new-connection/error",
      {
        "[3,1,0,'[\"invalid:address=error\"]']",
        "[1,1,0,\"[5,\\\"Unknown or unsupported transport `invalid' for address `invalid:address=error'\\\"]\"]"
      }
    },

    { "new-connection/success",
      {
        "[3,1,0,'[\"alias:abstract=/org/eventdance/lib/demo/dbus-bridge\"]']",
        "[2,1,0,\"[1]\"]",
        "[3,2,0,'[\"alias:abstract=/org/eventdance/lib/demo/dbus-bridge\"]']",
        "[2,2,0,\"[2]\"]"
      }
    },

    { "close-connection/error",
      {
        "[4,2,1,'[]']",
        "[1,2,1,\"[3,\\\"Object doesn't hold specified connection\\\"]\"]"
      }
    },

    { "close-connection/success",
      {
        "[3,1,0,'[\"alias:abstract=/org/eventdance/lib/demo/dbus-bridge\"]']",
        "[2,1,0,\"[1]\"]",
        "[4,2,1,'[]']",
        "[2,2,1,\"[]\"]"
      }
    }


  };

static void
test_fixture_setup (struct Fixture *f,
                    gconstpointer   test_data)
{
  f->bridge = evd_dbus_bridge_new ();
  f->obj1 = g_object_new (G_TYPE_OBJECT, NULL);
  f->obj2 = g_object_new (G_TYPE_OBJECT, NULL);

  evd_dbus_agent_create_address_alias (f->obj1, session_bus_addr, addr_alias);
  evd_dbus_agent_create_address_alias (f->obj2, session_bus_addr, addr_alias);

  f->main_loop = g_main_loop_new (NULL, FALSE);

  f->i = 0;
}

static void
test_fixture_teardown (struct Fixture *f,
                       gconstpointer   test_data)
{
  g_object_unref (f->bridge);
  g_object_unref (f->obj2);
  g_object_unref (f->obj1);

  g_main_loop_unref (f->main_loop);
}

static void
on_bridge_send_msg (EvdDBusBridge *self,
                    GObject       *object,
                    const gchar   *json,
                    gpointer       user_data)
{
  struct Fixture *f = (struct Fixture *) user_data;
  const gchar *expected_json;
  JsonParser *parser;
  GError *error = NULL;

  expected_json = f->test_case->phrases[f->i];

  //  g_debug ("%s", json);
  g_assert_cmpstr (expected_json, ==, json);

  parser = json_parser_new ();
  json_parser_load_from_data (parser,
                              json,
                              -1,
                              &error);
  g_assert_no_error (error);
  g_object_unref (parser);

  f->i++;
  if (f->test_case->phrases[f->i] != NULL)
    {
      f->i++;
      evd_dbus_bridge_process_msg (f->bridge,
                                   f->obj1,
                                   f->test_case->phrases[f->i - 1],
                                   -1);
    }
  else
    {
      g_main_loop_quit (f->main_loop);
    }
}

static void
test_func (struct Fixture *f,
           gconstpointer   test_data)
{
  TestCase *test_case = (TestCase *) test_data;

  f->test_case = test_case;
  evd_dbus_bridge_set_send_msg_callback (f->bridge,
                                         on_bridge_send_msg,
                                         f);

  f->i++;
  evd_dbus_bridge_process_msg (f->bridge,
                               f->obj1,
                               test_case->phrases[f->i-1],
                               -1);

  g_main_loop_run (f->main_loop);
}

gint
main (gint argc, gchar *argv[])
{
  GDBusConnection *dbus_conn;

  g_type_init ();
  g_test_init (&argc, &argv, NULL);

  /* check D-Bus session bus is active */
  session_bus_addr = g_dbus_address_get_for_bus_sync (G_BUS_TYPE_SESSION,
                                                      NULL,
                                                      NULL);

  if ( (dbus_conn = g_dbus_connection_new_for_address_sync (session_bus_addr,
                                G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION |
                                G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,
                                NULL,
                                NULL,
                                NULL)) != NULL)
    {
      gint i;

      g_dbus_connection_close_sync (dbus_conn, NULL, NULL);
      g_object_unref (dbus_conn);

      for (i=0; i<sizeof (test_cases) / sizeof (TestCase); i++)
        {
          gchar *test_name;

          test_name =
            g_strdup_printf ("/evd/dbus/bridge/%s", test_cases[i].test_name);

          g_test_add (test_name,
                      struct Fixture,
                      &test_cases[i],
                      test_fixture_setup,
                      test_func,
                      test_fixture_teardown);

          g_free (test_name);
        }
    }

  return g_test_run ();
}
