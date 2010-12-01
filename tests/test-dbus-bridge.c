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

#define BASE_NAME "org.eventdance.lib.test"
#define BASE_OBJ_PATH "/org/eventdance/lib/test"
#define BASE_IFACE_NAME BASE_NAME
#define DBUS_ADDR "alias:abstract=" BASE_OBJ_PATH "/dbus-bridge"
#define IFACE_XML \
  "<interface name=\\\"" BASE_IFACE_NAME ".TestIface\\\">" \
  "  <method name=\\\"HelloWorld\\\">" \
  "    <arg type=\\\"s\\\" name=\\\"greeting\\\" direction=\\\"in\\\"/>" \
  "    <arg type=\\\"s\\\" name=\\\"response\\\" direction=\\\"out\\\"/>" \
  "  </method>" \
  "  <signal name=\\\"WorldGreets\\\">" \
  "    <arg type=\\\"s\\\" name=\\\"message\\\"/>" \
  "  </signal>" \
  "</interface>"

static const gchar *session_bus_addr;
static const gchar *addr_alias = DBUS_ADDR;

static gint test_index = -1;
static const gchar *self_name;

static GOptionEntry entries[] =
{
  { "run-test", 'r', 0, G_OPTION_ARG_INT, &test_index, "Only run specified test case", NULL }
};

typedef struct
{
  gchar *test_name;
  gchar *send[15];
  gchar *expect[15];
} TestCase;

struct Fixture
{
  EvdDBusBridge *bridge;
  GObject *obj;
  gint i;
  gint j;
  TestCase *test_case;
  GMainLoop *main_loop;
};

static const TestCase test_cases[] =
  {
    { "error/invalid-message",
      {
        "",
        "[]",
        "[0,0,0,\"\"]",
        "[0,0,0,0,0]",
        "[3,1,0,0]",
      },
      {
        "[1,0,0,0,\"[1]\"]",
        "[1,0,0,0,\"[1]\"]",
        "[1,0,0,0,\"[1]\"]",
        "[1,0,0,0,\"[1]\"]"
      }
    },

    { "error/invalid-command",
      {
        "[0,1,0,0,\"\"]",
        "[100,16,0,0,\"\"]",
      },
      {
        "[1,1,0,0,\"[2]\"]",
        "[1,16,0,0,\"[2]\"]",
      }
    },

    { "error/invalid-arguments",
      {
        "[3,1,1,0,\"\"]",
      },
      {
        "[1,1,1,0,\"[4]\"]"
      }
    },

    { "new-connection/error",
      {
        "[3,1,0,0,'[\"invalid:address=error\",true]']",
      },
      {
        "[1,1,0,0,\"[5,\\\"Unknown or unsupported transport `invalid' for address `invalid:address=error'\\\"]\"]"
      }
    },

    { "new-connection/success",
      {
        "[3,1,0,0,'[\"" DBUS_ADDR "\",true]']",
        "[3,2,0,0,'[\"" DBUS_ADDR "\",false]']",
      },
      {
        "[2,1,0,0,\"[1]\"]",
        "[2,2,0,0,\"[2]\"]"
      }
    },

    { "close-connection/error",
      {
        "[4,2,1,0,'[]']"
      },
      {
        "[1,2,1,0,\"[3,\\\"Object doesn't hold specified connection\\\"]\"]"
      }
    },

    { "close-connection/success",
      {
        "[3,1,0,0,'[\"" DBUS_ADDR "\",true]']",
        "[4,2,1,0,'[]']"
      },
      {
        "[2,1,0,0,\"[1]\"]",
        "[2,2,1,0,\"[]\"]"
      }
    },

    { "own-name",
      {
        "[3,1,0,0,'[\"" DBUS_ADDR "\",true]']", /* new-connection */
        "[5,2,1,0,'[\"org.eventdance.lib.tests\", 0]']", /* own-name */
        NULL,
      },
      {
        "[2,1,0,0,\"[1]\"]", /* new-connection response */
        "[2,2,1,0,\"[1]\"]", /* own-name response */
        "[7,0,1,1,\"[]\"]", /* name-acquired signal */
      }
    },

    { "own-name/twice",
      {
        "[3,1,0,0,'[\"" DBUS_ADDR "\",true]']", /* new-connection */
        "[5,2,1,0,'[\"org.eventdance.lib.tests\", 0]']", /* own-name */
        NULL,
        "[6,3,1,1,'[]']", /* unown-name */
        "[5,4,1,0,'[\"org.eventdance.lib.tests1\", 0]']", /* own-name again */
        NULL,
        "[6,5,1,2,'[]']", /* unown-name */
        "[5,6,1,0,'[\"org.eventdance.lib.tests1\", 0]']", /* own-name again */
        NULL,
        "[6,7,1,3,'[]']", /* unown-name */
      },
      {
        "[2,1,0,0,\"[1]\"]", /* new-connection response */
        "[2,2,1,0,\"[1]\"]", /* own-name response */
        "[7,0,1,1,\"[]\"]", /* name-acquired signal */
        "[2,3,1,1,\"[]\"]", /* unown-name response */
        "[2,4,1,0,\"[2]\"]", /* own-name response */
        "[7,0,1,2,\"[]\"]", /* name-acquired signal again */
        "[2,5,1,2,\"[]\"]", /* unown-name response */
        "[2,6,1,0,\"[3]\"]", /* own-name response */
        "[7,0,1,3,\"[]\"]", /* name-acquired signal again */
        "[2,7,1,3,\"[]\"]", /* unown-name response */
      }
    },

    { "own-name/replace",
      {
        "[3,1,0,0,'[\"" DBUS_ADDR "\",true]']", /* new-connection 1*/
        "[5,2,1,0,'[\"org.eventdance.lib.tests\",3]']", /* own-name */
        NULL,
        "[3,3,0,0,'[\"" DBUS_ADDR "\",false]']", /* new-connection 2 */
        "[5,4,2,0,'[\"org.eventdance.lib.tests\",3]']", /* own-name */
        NULL,
        NULL,
      },
      {
        "[2,1,0,0,\"[1]\"]", /* new-connection 1 response */
        "[2,2,1,0,\"[1]\"]", /* own-name response */
        "[7,0,1,1,\"[]\"]", /* name-acquired signal */
        "[2,3,0,0,\"[2]\"]", /* new-connection 2 response */
        "[2,4,2,0,\"[2]\"]", /* own-name response */
        "[8,0,1,1,\"[]\"]", /* name-lost signal on connection 1 */
        "[7,0,2,2,\"[]\"]", /* name-acquired signal */
      }
    },

    { "own-name/queue",
      {
        "[3,1,0,0,'[\"" DBUS_ADDR "\",true]']", /* new-connection 1*/
        "[3,2,0,0,'[\"" DBUS_ADDR "\",false]']", /* new-connection 2 */
        "[5,3,1,0,'[\"org.eventdance.lib.tests\", 0]']", /* own-name, connection 1 */
        NULL,
        "[5,4,2,0,'[\"org.eventdance.lib.tests\", 0]']", /* own-name, connection 2 */
        NULL,
        "[6,5,1,1,'[]']", /* unown-name, connection 1 */
        NULL,
        "[6,6,2,2,'[]']", /* unown-name, connection 2 */
      },
      {
        "[2,1,0,0,\"[1]\"]", /* new-connection 1 response */
        "[2,2,0,0,\"[2]\"]", /* new-connection 2 response */
        "[2,3,1,0,\"[1]\"]", /* own-name response, connection 1 */
        "[7,0,1,1,\"[]\"]", /* name-acquired signal, connection 1 */
        "[2,4,2,0,\"[2]\"]", /* own-name response, connection 2 */
        "[8,0,2,2,\"[]\"]", /* name-lost signal, connection 2 */
        "[2,5,1,1,\"[]\"]", /* unown-name response, connection 1 */
        "[7,0,2,2,\"[]\"]", /* name-acquired signal, connection 2 */
        "[2,6,2,2,\"[]\"]", /* unown-name response, connection 2 */
      }
    },

    { "own-name/close-connection",
      {
        "[3,1,0,0,'[\"" DBUS_ADDR "\",true]']", /* new-connection 1*/
        "[3,1,0,0,'[\"" DBUS_ADDR "\",false]']", /* new-connection 2 */
        "[5,2,1,0,'[\"org.eventdance.lib.tests\", 0]']", /* own-name, connection 1 */
        NULL,
        "[5,2,2,0,'[\"org.eventdance.lib.tests\", 0]']", /* own-name, connection 2 */
        NULL,
        "[4,3,1,0,'[]']", /* close connection 1 */
        NULL,
        "[6,3,2,2,'[]']", /* unown-name, connection 2 */
      },
      {
        "[2,1,0,0,\"[1]\"]", /* new-connection 1 response */
        "[2,1,0,0,\"[2]\"]", /* new-connection 2 response */
        "[2,2,1,0,\"[1]\"]", /* own-name response, connection 1 */
        "[7,0,1,1,\"[]\"]", /* name-acquired signal, connection 1 */
        "[2,2,2,0,\"[2]\"]", /* own-name response, connection 2 */
        "[8,0,2,2,\"[]\"]", /* name-lost signal, connection 2 */
        "[7,0,2,2,\"[]\"]", /* name-acquired signal, connection 2 */
        "[2,3,1,0,\"[]\"]", /* close-connection 1 response */
        "[2,3,2,2,\"[]\"]", /* unown-name response, connection 2 */
      }
    },

    { "register-object",
      {
        "[3,1,0,0,'[\"" DBUS_ADDR "\",true]']", /* new-connection */
        "[5,2,1,0,'[\"org.eventdance.lib.tests.RegisterObject\", 0]']", /* own-name */
        NULL,
        "[9,3,1,0,'[\"/org/eventdance/lib/test/RegisterObject/Object\",\"" IFACE_XML "\"]']", /* register-object */
        "[10,4,1,1,'[]']", /* unregister-object */
        "[6,5,1,1,'[]']", /* unown-name */
      },
      {
        "[2,1,0,0,\"[1]\"]", /* new-connection response */
        "[2,2,1,0,\"[1]\"]", /* own-name response */
        "[7,0,1,1,\"[]\"]", /* name-acquired signal */
        "[2,3,1,0,\"[1]\"]", /* register-object response */
        "[2,4,1,1,\"[]\"]", /* unregister-object response */
        "[2,5,1,1,\"[]\"]", /* unown-name response */
      }
    },

    { "register-object/already-registered",
      {
        "[3,1,0,0,'[\"" DBUS_ADDR "\",true]']", /* new-connection */
        "[5,2,1,0,'[\"org.eventdance.lib.tests.RegisterObject\", 0]']", /* own-name */
        NULL,
        "[9,3,1,0,'[\"/org/eventdance/lib/test/RegisterObject/Object\",\"" IFACE_XML "\"]']", /* register-object */
        "[9,4,1,0,'[\"/org/eventdance/lib/test/RegisterObject/Object\",\"" IFACE_XML "\"]']", /* register-object */
        "[10,5,1,1,'[]']", /* unregister-object */
        "[6,6,1,1,'[]']", /* unown-name */
      },
      {
        "[2,1,0,0,\"[1]\"]", /* new-connection response */
        "[2,2,1,0,\"[1]\"]", /* own-name response */
        "[7,0,1,1,\"[]\"]", /* name-acquired signal */
        "[2,3,1,0,\"[1]\"]", /* register-object response */
        "[1,4,1,0,\"[6]\"]", /* register-object again - error, already registered */
        "[2,5,1,1,\"[]\"]", /* unregister-object response */
        "[2,6,1,1,\"[]\"]", /* unown-name response */
      }
    },

    { "register-object/two-connections",
      {
        "[3,1,0,0,'[\"" DBUS_ADDR "\",true]']", /* new-connection 1 */
        "[3,2,0,0,'[\"" DBUS_ADDR "\",false]']", /* new-connection 2 */
        "[9,1,1,0,'[\"/org/eventdance/lib/test/RegisterObject/Object\",\"" IFACE_XML "\"]']", /* register-object, connection 1 */
        "[9,1,2,0,'[\"/org/eventdance/lib/test/RegisterObject/Object\",\"" IFACE_XML "\"]']", /* register-object, connection 2 */
        "[10,2,1,1,'[]']", /* unregister-object, connection 1 */
        "[10,2,2,2,'[]']", /* unregister-object, connection 2 */
      },
      {
        "[2,1,0,0,\"[1]\"]", /* new-connection 1 response */
        "[2,2,0,0,\"[2]\"]", /* new-connection 2 response */
        "[2,1,1,0,\"[1]\"]", /* register-object response, connection 1 */
        "[2,1,2,0,\"[2]\"]", /* register-object response, connection 2 */
        "[2,2,1,1,\"[]\"]", /* unregister-object response, connection 1 */
        "[2,2,2,2,\"[]\"]", /* unregister-object response, connection 2 */
      }
    },

    { "register-object/close-connection",
      {
        "[3,1,0,0,'[\"" DBUS_ADDR "\",true]']", /* new-connection */
        "[5,2,1,0,'[\"org.eventdance.lib.tests.RegisterObject\", 0]']", /* own-name */
        NULL,
        "[9,3,1,0,'[\"/org/eventdance/lib/test/RegisterObject/Object\",\"" IFACE_XML "\"]']", /* register-object */
        "[4,4,1,0,'[]']", /* close connection (should unregister object, and lost name) */
        "[10,5,1,1,'[]']", /* unregister-object (should return error, invalid subject) */
      },
      {
        "[2,1,0,0,\"[1]\"]", /* new-connection response */
        "[2,2,1,0,\"[1]\"]", /* own-name response */
        "[7,0,1,1,\"[]\"]", /* name-acquired signal */
        "[2,3,1,0,\"[1]\"]", /* register-object response */
        "[2,4,1,0,\"[]\"]", /* close-connection response */
        "[1,5,1,1,\"[3]\"]", /* error in unregister-object, invalid registered object */
      }
    },

    { "new-proxy",
      {
        "[3,1,0,0,'[\"" DBUS_ADDR "\",true]']", /* new-connection */
        "[11,1,1,0,'[\"" BASE_NAME "\",\"" BASE_OBJ_PATH "/NewProxy\",\"" BASE_IFACE_NAME ".TestIface\",0]']", /* new-proxy */
        "[12,2,1,1,'[]']", /* close-proxy */
      },
      {
        "[2,1,0,0,\"[1]\"]", /* new-connection response */
        "[2,1,1,0,\"[1]\"]", /* new-proxy response */
        "[2,2,1,1,\"[]\"]", /* close-proxy response */
      }
    },

    { "new-proxy/close-connection",
      {
        "[3,1,0,0,'[\"" DBUS_ADDR "\",true]']", /* new-connection */
        "[11,1,1,0,'[\"" BASE_NAME "\",\"" BASE_OBJ_PATH "/NewProxy\",\"" BASE_IFACE_NAME ".TestIface\",0]']", /* new-proxy */
        "[4,2,1,0,'[]']", /* close connection (should invalidate proxy) */
        "[12,3,1,1,'[]']", /* close-proxy */
      },
      {
        "[2,1,0,0,\"[1]\"]", /* new-connection response */
        "[2,1,1,0,\"[1]\"]", /* new-proxy response */
        "[2,2,1,0,\"[]\"]", /* close-connection response */
        "[1,3,1,1,\"[3]\"]", /* error in close-proxy, invalid proxy */
      }
    },

    { "proxy/call-method",
      {
        "[3,1,0,0,'[\"" DBUS_ADDR "\",true]']", /* new-connection */
        "[5,1,1,0,'[\"" BASE_NAME ".CallProxyMethod\", 0]']", /* own-name */
        NULL,
        "[9,2,1,0,'[\"" BASE_OBJ_PATH "/CallProxyMethod\",\"" IFACE_XML "\"]']", /* register-object */
        "[11,3,1,0,'[\"" BASE_NAME ".CallProxyMethod\",\"" BASE_OBJ_PATH "/CallProxyMethod\",\"" BASE_IFACE_NAME ".TestIface\",0]']", /* new-proxy */
        "[13,4,1,1,'[\"HelloWorld\",\"[\\\"Hi there\\\"]\",\"(s)\",0,-1]']", /* call-method on proxy */
        "[14,1,1,1,'[\"[\\\"hello world!\\\"]\",\"(s)\"]']", /* call-method-return from registered object */
      },
      {
        "[2,1,0,0,\"[1]\"]", /* new-connection response */
        "[2,1,1,0,\"[1]\"]", /* own-name response */
        "[7,0,1,1,\"[]\"]", /* name-acquired signal */
        "[2,2,1,0,\"[1]\"]", /* register-object response */
        "[2,3,1,0,\"[1]\"]", /* new-proxy response */
        "[13,1,1,1,\"[\\\"HelloWorld\\\",\\\"[ \\\\\\\"Hi there\\\\\\\" ]\\\",\\\"(s)\\\",0,0]\"]", /* call-method to registered object */
        "[14,4,1,1,\"[\\\"[ \\\\\\\"hello world!\\\\\\\" ]\\\",\\\"(s)\\\"]\"]", /* call-method-return to proxy */
      }
    },

    { "proxy/signal",
      {
        "[3,1,0,0,'[\"" DBUS_ADDR "\",true]']", /* new-connection */
        "[5,1,1,0,'[\"" BASE_NAME ".ProxySignal\", 0]']", /* own-name */
        NULL,
        "[9,2,1,0,'[\"" BASE_OBJ_PATH "/ProxySignal\",\"" IFACE_XML "\"]']", /* register-object */
        "[11,3,1,0,'[\"" BASE_NAME ".ProxySignal\",\"" BASE_OBJ_PATH "/ProxySignal\",\"" BASE_IFACE_NAME ".TestIface\",0]']", /* new-proxy */
        "[15,4,1,1,'[\"WorldGreets\",\"[\\\"hello world!\\\"]\",\"(s)\"]']", /* emit-signal from registered object */
      },
      {
        "[2,1,0,0,\"[1]\"]", /* new-connection response */
        "[2,1,1,0,\"[1]\"]", /* own-name response */
        "[7,0,1,1,\"[]\"]", /* name-acquired signal */
        "[2,2,1,0,\"[1]\"]", /* register-object response */
        "[2,3,1,0,\"[1]\"]", /* new-proxy response */
        "[15,0,1,1,\"[\\\"WorldGreets\\\",\\\"[ \\\\\\\"hello world!\\\\\\\" ]\\\",\\\"(s)\\\"]\"]", /* emit-signal received on proxy */
      }
    },
  };

static void
test_fixture_setup (struct Fixture *f,
                    gconstpointer   test_data)
{
  f->bridge = evd_dbus_bridge_new ();
  f->obj = g_object_new (G_TYPE_OBJECT, NULL);

  evd_dbus_agent_create_address_alias (f->obj, session_bus_addr, addr_alias);

  evd_dbus_bridge_track_object (f->bridge, f->obj);

  f->main_loop = g_main_loop_new (NULL, FALSE);

  f->i = 0;
  f->j = 0;
}

static void
test_fixture_teardown (struct Fixture *f,
                       gconstpointer   test_data)
{
  g_object_unref (f->bridge);
  g_object_unref (f->obj);

  g_main_loop_unref (f->main_loop);
}

static gboolean
on_send_in_idle (gpointer user_data)
{
  struct Fixture *f = (struct Fixture *) user_data;

  evd_dbus_bridge_process_msg (f->bridge,
                               f->obj,
                               f->test_case->send[f->i-1],
                               -1);

  return FALSE;
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

  expected_json = f->test_case->expect[f->j];
  f->j++;
  f->i++;

  //  g_debug ("%s", json);
  g_assert_cmpstr (expected_json, ==, json);

  parser = json_parser_new ();
  json_parser_load_from_data (parser,
                              json,
                              -1,
                              &error);
  g_assert_no_error (error);
  g_object_unref (parser);

  if (f->test_case->send[f->i-1] != NULL)
    g_idle_add (on_send_in_idle, f);

  if (f->test_case->send[f->i] == NULL
      && f->test_case->expect[f->j] == NULL)
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

  evd_dbus_bridge_process_msg (f->bridge,
                               f->obj,
                               test_case->send[f->i],
                               -1);
  f->i++;

  g_main_loop_run (f->main_loop);
}

static void
spawn_test (gconstpointer test_data)
{
  gint *test_index = (gint *) test_data;
  gint exit_status;
  GError *error = NULL;
  gchar *cmdline;

  cmdline = g_strdup_printf ("%s -r %d", self_name, *test_index);

  g_spawn_command_line_sync (cmdline,
                             NULL,
                             NULL,
                             &exit_status,
                             &error);

  g_assert_cmpint (exit_status, ==, 0);

  g_free (cmdline);
  g_free (test_index);
}

gint
main (gint argc, gchar *argv[])
{
  GDBusConnection *dbus_conn;
  GError *error = NULL;
  GOptionContext *context;

  self_name = argv[0];

  g_type_init ();
  g_test_init (&argc, &argv, NULL);

  context = g_option_context_new ("- test tree model performance");
  g_option_context_add_main_entries (context, entries, NULL);
  if (! g_option_context_parse (context, &argc, &argv, &error))
    {
      g_print ("Error in arguments: %s\n", error->message);
      return 1;
    }
  g_option_context_free (context);

  session_bus_addr = g_dbus_address_get_for_bus_sync (G_BUS_TYPE_SESSION,
                                                      NULL,
                                                      NULL);

  if (test_index >= 0 && test_index < sizeof (test_cases) / sizeof (TestCase))
    {
      struct Fixture *f;

      f = g_slice_new (struct Fixture);
      test_fixture_setup (f, &test_cases[test_index]);
      test_func (f, &test_cases[test_index]);
      test_fixture_teardown (f, &test_cases[test_index]);

      g_slice_free (struct Fixture, f);
    }
  else
    {
      /* check D-Bus session bus is active */
      if ( (dbus_conn =
            g_dbus_connection_new_for_address_sync (session_bus_addr,
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
              gint *index;

              index = g_new0 (gint, 1);
              *index = i;

              test_name =
                g_strdup_printf ("/evd/dbus/bridge/%s", test_cases[i].test_name);

              g_test_add_data_func (test_name,
                                    index,
                                    spawn_test);

              g_free (test_name);
            }
        }
    }

  return g_test_run ();
}
