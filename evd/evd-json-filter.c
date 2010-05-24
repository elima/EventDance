/*
 * evd-json-filter.c
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

#include <string.h>

#include "evd-error.h"
#include "evd-json-filter.h"
#include "evd-marshal.h"

G_DEFINE_TYPE (EvdJsonFilter, evd_json_filter, G_TYPE_OBJECT)

#define EVD_JSON_FILTER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                          EVD_TYPE_JSON_FILTER, \
                                          EvdJsonFilterPrivate))

#define MAX_DEPTH 128

/*
 *  Code pieces taken from http://www.json.org/JSON_checker/.
 */

#define __         -1     /* the universal error code */

/*
    Characters are mapped into these 31 character classes. This allows for
    a significant reduction in the size of the state transition table.
*/

enum classes {
    C_SPACE,  /* space */
    C_WHITE,  /* other whitespace */
    C_LCURB,  /* {  */
    C_RCURB,  /* } */
    C_LSQRB,  /* [ */
    C_RSQRB,  /* ] */
    C_COLON,  /* : */
    C_COMMA,  /* , */
    C_QUOTE,  /* " */
    C_BACKS,  /* \ */
    C_SLASH,  /* / */
    C_PLUS,   /* + */
    C_MINUS,  /* - */
    C_POINT,  /* . */
    C_ZERO ,  /* 0 */
    C_DIGIT,  /* 123456789 */
    C_LOW_A,  /* a */
    C_LOW_B,  /* b */
    C_LOW_C,  /* c */
    C_LOW_D,  /* d */
    C_LOW_E,  /* e */
    C_LOW_F,  /* f */
    C_LOW_L,  /* l */
    C_LOW_N,  /* n */
    C_LOW_R,  /* r */
    C_LOW_S,  /* s */
    C_LOW_T,  /* t */
    C_LOW_U,  /* u */
    C_ABCDF,  /* ABCDF */
    C_E,      /* E */
    C_ETC,    /* everything else */
    NR_CLASSES
};

static gint ascii_class[128] = {
/*
    This array maps the 128 ASCII characters into character classes.
    The remaining Unicode characters should be mapped to C_ETC.
    Non-whitespace control characters are errors.
*/
    __,      __,      __,      __,      __,      __,      __,      __,
    __,      C_WHITE, C_WHITE, __,      __,      C_WHITE, __,      __,
    __,      __,      __,      __,      __,      __,      __,      __,
    __,      __,      __,      __,      __,      __,      __,      __,

    C_SPACE, C_ETC,   C_QUOTE, C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,
    C_ETC,   C_ETC,   C_ETC,   C_PLUS,  C_COMMA, C_MINUS, C_POINT, C_SLASH,
    C_ZERO,  C_DIGIT, C_DIGIT, C_DIGIT, C_DIGIT, C_DIGIT, C_DIGIT, C_DIGIT,
    C_DIGIT, C_DIGIT, C_COLON, C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,

    C_ETC,   C_ABCDF, C_ABCDF, C_ABCDF, C_ABCDF, C_E,     C_ABCDF, C_ETC,
    C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,
    C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,
    C_ETC,   C_ETC,   C_ETC,   C_LSQRB, C_BACKS, C_RSQRB, C_ETC,   C_ETC,

    C_ETC,   C_LOW_A, C_LOW_B, C_LOW_C, C_LOW_D, C_LOW_E, C_LOW_F, C_ETC,
    C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_LOW_L, C_ETC,   C_LOW_N, C_ETC,
    C_ETC,   C_ETC,   C_LOW_R, C_LOW_S, C_LOW_T, C_LOW_U, C_ETC,   C_ETC,
    C_ETC,   C_ETC,   C_ETC,   C_LCURB, C_ETC,   C_RCURB, C_ETC,   C_ETC
};

/*
    The state codes.
*/
enum states {
    GO,  /* start    */
    OK,  /* ok       */
    OB,  /* object   */
    KE,  /* key      */
    CO,  /* colon    */
    VA,  /* value    */
    AR,  /* array    */
    ST,  /* string   */
    ES,  /* escape   */
    U1,  /* u1       */
    U2,  /* u2       */
    U3,  /* u3       */
    U4,  /* u4       */
    MI,  /* minus    */
    ZE,  /* zero     */
    IN,  /* integer  */
    FR,  /* fraction */
    E1,  /* e        */
    E2,  /* ex       */
    E3,  /* exp      */
    T1,  /* tr       */
    T2,  /* tru      */
    T3,  /* true     */
    F1,  /* fa       */
    F2,  /* fal      */
    F3,  /* fals     */
    F4,  /* false    */
    N1,  /* nu       */
    N2,  /* nul      */
    N3,  /* null     */
    NR_STATES
};


static int state_transition_table[NR_STATES][NR_CLASSES] = {
/*
    The state transition table takes the current state and the current symbol,
    and returns either a new state or an action. An action is represented as a
    negative number. A JSON text is accepted if at the end of the text the
    state is OK and if the mode is MODE_DONE.

                 white                                      1-9                                   ABCDF  etc
             space |  {  }  [  ]  :  ,  "  \  /  +  -  .  0  |  a  b  c  d  e  f  l  n  r  s  t  u  |  E  |*/
/*start  GO*/ {GO,GO,-6,__,-5,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*ok     OK*/ {OK,OK,__,-8,__,-7,__,-3,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*object OB*/ {OB,OB,__,-9,__,__,__,__,ST,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*key    KE*/ {KE,KE,__,__,__,__,__,__,ST,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*colon  CO*/ {CO,CO,__,__,__,__,-2,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*value  VA*/ {VA,VA,-6,__,-5,__,__,__,ST,__,__,__,MI,__,ZE,IN,__,__,__,__,__,F1,__,N1,__,__,T1,__,__,__,__},
/*array  AR*/ {AR,AR,-6,__,-5,-7,__,__,ST,__,__,__,MI,__,ZE,IN,__,__,__,__,__,F1,__,N1,__,__,T1,__,__,__,__},
/*string ST*/ {ST,__,ST,ST,ST,ST,ST,ST,-4,ES,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST},
/*escape ES*/ {__,__,__,__,__,__,__,__,ST,ST,ST,__,__,__,__,__,__,ST,__,__,__,ST,__,ST,ST,__,ST,U1,__,__,__},
/*u1     U1*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,U2,U2,U2,U2,U2,U2,U2,U2,__,__,__,__,__,__,U2,U2,__},
/*u2     U2*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,U3,U3,U3,U3,U3,U3,U3,U3,__,__,__,__,__,__,U3,U3,__},
/*u3     U3*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,U4,U4,U4,U4,U4,U4,U4,U4,__,__,__,__,__,__,U4,U4,__},
/*u4     U4*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,ST,ST,ST,ST,ST,ST,ST,ST,__,__,__,__,__,__,ST,ST,__},
/*minus  MI*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,ZE,IN,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*zero   ZE*/ {OK,OK,__,-8,__,-7,__,-3,__,__,__,__,__,FR,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*int    IN*/ {OK,OK,__,-8,__,-7,__,-3,__,__,__,__,__,FR,IN,IN,__,__,__,__,E1,__,__,__,__,__,__,__,__,E1,__},
/*frac   FR*/ {OK,OK,__,-8,__,-7,__,-3,__,__,__,__,__,__,FR,FR,__,__,__,__,E1,__,__,__,__,__,__,__,__,E1,__},
/*e      E1*/ {__,__,__,__,__,__,__,__,__,__,__,E2,E2,__,E3,E3,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*ex     E2*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,E3,E3,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*exp    E3*/ {OK,OK,__,-8,__,-7,__,-3,__,__,__,__,__,__,E3,E3,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*tr     T1*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,T2,__,__,__,__,__,__},
/*tru    T2*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,T3,__,__,__},
/*true   T3*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,OK,__,__,__,__,__,__,__,__,__,__},
/*fa     F1*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,F2,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*fal    F2*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,F3,__,__,__,__,__,__,__,__},
/*fals   F3*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,F4,__,__,__,__,__},
/*false  F4*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,OK,__,__,__,__,__,__,__,__,__,__},
/*nu     N1*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,N2,__,__,__},
/*nul    N2*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,N3,__,__,__,__,__,__,__,__},
/*null   N3*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,OK,__,__,__,__,__,__,__,__},
};


/*
    These modes can be pushed on the stack.
*/
enum modes {
    MODE_ARRAY,
    MODE_DONE,
    MODE_KEY,
    MODE_OBJECT,
};

/* private data */
struct _EvdJsonFilterPrivate
{
  gint  state;
  gint  depth;
  gint  top;
  gint* stack;

  gint     content_start;
  GString *cache;

  GClosure *on_packet;
};

static void     evd_json_filter_class_init         (EvdJsonFilterClass *class);
static void     evd_json_filter_init               (EvdJsonFilter *self);

static void     evd_json_filter_finalize           (GObject *obj);
static void     evd_json_filter_dispose            (GObject *obj);

static void
evd_json_filter_class_init (EvdJsonFilterClass *class)
{
  GObjectClass *obj_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_json_filter_dispose;
  obj_class->finalize = evd_json_filter_finalize;

  /* add private structure */
  g_type_class_add_private (obj_class, sizeof (EvdJsonFilterPrivate));
}

static void
evd_json_filter_init (EvdJsonFilter *self)
{
  EvdJsonFilterPrivate *priv;

  priv = EVD_JSON_FILTER_GET_PRIVATE (self);
  self->priv = priv;

  /* initialize private members */
  priv->stack = g_new0 (gint, MAX_DEPTH);
  priv->cache = g_string_new ("");

  evd_json_filter_reset (self);

  priv->on_packet = NULL;
}

static void
evd_json_filter_dispose (GObject *obj)
{
  G_OBJECT_CLASS (evd_json_filter_parent_class)->dispose (obj);
}

static void
evd_json_filter_finalize (GObject *obj)
{
  EvdJsonFilter *self = EVD_JSON_FILTER (obj);

  g_free (self->priv->stack);

  g_string_free (self->priv->cache, TRUE);

  if (self->priv->on_packet != NULL)
    g_closure_unref (self->priv->on_packet);

  G_OBJECT_CLASS (evd_json_filter_parent_class)->finalize (obj);
}

static gboolean
evd_json_filter_push (EvdJsonFilter *self, gint mode)
{
  /* Push a mode onto the stack. Return false if there is overflow. */
  self->priv->top += 1;

  if (self->priv->top >= self->priv->depth)
    {
      return FALSE;
    }

  self->priv->stack[self->priv->top] = mode;

  return TRUE;
}

static gboolean
evd_json_filter_pop (EvdJsonFilter *self, gint mode)
{
  /* Pop the stack, assuring that the current mode matches the expectation.
     Return false if there is underflow or if the modes mismatch. */
  if ( (self->priv->top < 0) || (self->priv->stack[self->priv->top] != mode) )
    {
      return FALSE;
    }

  self->priv->top -= 1;

  return TRUE;
}

static gboolean
evd_json_filter_error (EvdJsonFilter *self)
{
  evd_json_filter_reset (self);

  return FALSE;
}

static gboolean
evd_json_filter_process (EvdJsonFilter *self, gint next_char, gsize offset)
{
  /*
   * After calling new_JSON_checker, call this function for each character (or
   * partial character) in your JSON text. It can accept UTF-8, UTF-16, or
   * UTF-32. It returns true if things are looking ok so far. If it rejects the
   * text, it deletes the JSON_checker object and returns false.
   */

  gint next_class, next_state;

  /* Determine the character's class. */
  if (next_char < 0)
    return evd_json_filter_error (self);

  if (next_char >= 128)
    {
      next_class = C_ETC;
    }
  else
    {
      next_class = ascii_class[next_char];
      if (next_class <= __)
        return evd_json_filter_error (self);
    }

  /* Get the next state from the state transition table. */
    next_state = state_transition_table[self->priv->state][next_class];
    if (next_state >= 0)
      {
        /* Change the state. */
        self->priv->state = next_state;
      }
    else
      {
        if (self->priv->content_start == -1)
          self->priv->content_start = offset;

        /* Or perform one of the actions. */
        switch (next_state)
          {
            /* empty } */
          case -9:
            if (! evd_json_filter_pop (self, MODE_KEY))
              return evd_json_filter_error (self);
            else
              self->priv->state = OK;

            break;

            /* } */
          case -8:
            if (! evd_json_filter_pop (self, MODE_OBJECT))
              return evd_json_filter_error (self);
            else
              self->priv->state = OK;

            break;

            /* ] */
          case -7:
            if (! evd_json_filter_pop (self, MODE_ARRAY))
              return evd_json_filter_error (self);
            else
              self->priv->state = OK;

            break;

            /* { */
          case -6:
            if (! evd_json_filter_push (self, MODE_KEY))
              return evd_json_filter_error(self);
            else
              self->priv->state = OB;

            break;

            /* [ */
          case -5:
            if (! evd_json_filter_push (self, MODE_ARRAY))
              return evd_json_filter_error (self);
            else
              self->priv->state = AR;

            break;

            /* " */
          case -4:
            switch (self->priv->stack[self->priv->top])
              {
              case MODE_KEY:
                self->priv->state = CO;
                break;
              case MODE_ARRAY:
              case MODE_OBJECT:
                self->priv->state = OK;
                break;
              default:
                return evd_json_filter_error (self);
              }
            break;

            /* , */
          case -3:
            switch (self->priv->stack[self->priv->top])
              {
              case MODE_OBJECT:
                /* A comma causes a flip from object mode to key mode. */
                if (! evd_json_filter_pop (self, MODE_OBJECT) ||
                    ! evd_json_filter_push (self, MODE_KEY))
                  {
                    return evd_json_filter_error (self);
                  }
                else
                  {
                    self->priv->state = KE;
                  }

                break;

              case MODE_ARRAY:
                self->priv->state = VA;
                break;

              default:
                return evd_json_filter_error (self);
              }

            break;

            /* : */
          case -2:
            /* A colon causes a flip from key mode to object mode. */
            if (! evd_json_filter_pop (self, MODE_KEY) ||
                ! evd_json_filter_push (self, MODE_OBJECT))
              {
                return evd_json_filter_error (self);
              }
            else
              {
                self->priv->state = VA;
              }

            break;

            /* Bad action. */
          default:
            return evd_json_filter_error (self);
          }
      }

    return TRUE;
}

static void
evd_json_filter_notify_packet (EvdJsonFilter *self,
                               const gchar   *buffer,
                               gsize          size)
{
  if (self->priv->on_packet != NULL)
    {
      GValue params[3] = { {0, } };

      g_value_init (&params[0], EVD_TYPE_JSON_FILTER);
      g_value_set_object (&params[0], self);

      g_value_init (&params[1], G_TYPE_STRING);
      g_value_set_static_string (&params[1], buffer);

      g_value_init (&params[2], G_TYPE_ULONG);
      g_value_set_ulong (&params[2], size);

      g_object_ref (self);
      g_closure_invoke (self->priv->on_packet, NULL, 3, params, NULL);
      g_object_unref (self);

      g_value_unset (&params[0]);
      g_value_unset (&params[1]);
      g_value_unset (&params[2]);
    }
}

/* public methods */

EvdJsonFilter *
evd_json_filter_new (void)
{
  EvdJsonFilter *self;

  self = g_object_new (EVD_TYPE_JSON_FILTER, NULL);

  return self;
}

void
evd_json_filter_reset (EvdJsonFilter *self)
{
  g_return_if_fail (EVD_IS_JSON_FILTER (self));

  self->priv->state = GO;
  self->priv->depth = MAX_DEPTH;
  self->priv->top = -1;

  self->priv->content_start = -1;

  evd_json_filter_push (self, MODE_DONE);
}

gboolean
evd_json_filter_feed_len (EvdJsonFilter  *self,
                          const gchar    *buffer,
                          gsize           size,
                          GError        **error)
{
  gint i;

  g_return_val_if_fail (EVD_IS_JSON_FILTER (self), FALSE);
  g_return_val_if_fail (buffer != NULL, FALSE);

  i = 0;
  while (i < size)
    {
      if (! evd_json_filter_process (self, (gint) buffer[i], i))
        {
          if (error != NULL)
            {
              *error = g_error_new (EVD_ERROR,
                                    EVD_ERROR_INVALID_DATA,
                                    "Malformed JSON sequence at offset %d", i);
            }

          return FALSE;
        }
      else
        {
          if ( (self->priv->content_start >= 0) &&
              self->priv->stack[self->priv->top] == MODE_DONE)
            {
              if (self->priv->cache->len > 0)
                {
                  g_string_append_len (self->priv->cache, buffer, i+1);

                  evd_json_filter_notify_packet (self,
                                                 self->priv->cache->str,
                                                 self->priv->cache->len);

                  g_string_free (self->priv->cache, TRUE);
                  self->priv->cache = g_string_new ("");
                }
              else
                {
                  evd_json_filter_notify_packet (self,
                      (gchar *) ( (void *) buffer + self->priv->content_start),
                      i - self->priv->content_start + 1);
                }

              evd_json_filter_reset (self);
            }

          i++;
        }
    }

  if (self->priv->content_start >= 0)
    {
      g_string_append_len (self->priv->cache,
                     (gchar *) ( (void *) (buffer) + self->priv->content_start),
                     size - self->priv->content_start);

      self->priv->content_start = 0;
    }

  return TRUE;
}

gboolean
evd_json_filter_feed (EvdJsonFilter  *self,
                      const gchar    *buffer,
                      GError        **error)
{
  return evd_json_filter_feed_len (self, buffer, strlen (buffer), error);
}

void
evd_json_filter_set_packet_handler (EvdJsonFilter                *self,
                                    EvdJsonFilterOnPacketHandler  handler,
                                    gpointer                      user_data)
{
  GClosure *closure;

  g_return_if_fail (EVD_IS_JSON_FILTER (self));

  if (handler == NULL)
    {
      evd_json_filter_set_on_packet (self, NULL);

      return;
    }

  closure = g_cclosure_new (G_CALLBACK (handler),
			    user_data,
			    NULL);

  if (G_CLOSURE_NEEDS_MARSHAL (closure))
    {
      GClosureMarshal marshal = evd_marshal_VOID__STRING_ULONG;

      g_closure_set_marshal (closure, marshal);
    }

  evd_json_filter_set_on_packet (self, closure);
}

void
evd_json_filter_set_on_packet (EvdJsonFilter *self,
                               GClosure      *closure)
{
  g_return_if_fail (EVD_IS_JSON_FILTER (self));

  if (closure == NULL)
    {
      if (self->priv->on_packet != NULL)
        {
          g_closure_unref (self->priv->on_packet);
          self->priv->on_packet = NULL;
        }

      return;
    }

  g_closure_ref (closure);
  g_closure_sink (closure);

  self->priv->on_packet = closure;
}
