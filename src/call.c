/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-call"

#include "call.h"
#include "util.h"

#include <cui-call.h>


enum {
  PROP_0,
  PROP_DBUS_PROXY,
  PROP_DISPLAY_NAME,
  PROP_AVATAR_ICON,
  PROP_ID,
  PROP_STATE,
  PROP_ENCRYPTED,
  PROP_CAN_DTMF,
  PROP_LAST_PROP = PROP_DISPLAY_NAME,
};
static GParamSpec *props[PROP_LAST_PROP];


typedef struct _PhoshCall {
  GObject                  parent;

  PhoshCallsDBusCallsCall *proxy; /* DBus proxy to a single call on for gnome-calls' DBus service */
  GCancellable            *cancel;

  GLoadableIcon           *avatar_icon;
  gboolean                 can_dtmf;
} PhoshCall;


static void phosh_call_cui_call_interface_init (CuiCallInterface *iface);
G_DEFINE_TYPE_WITH_CODE (PhoshCall, phosh_call, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (CUI_TYPE_CALL,
                                                phosh_call_cui_call_interface_init))


static GLoadableIcon *
phosh_call_get_avatar_icon (CuiCall *call)
{
  g_return_val_if_fail (PHOSH_IS_CALL (call), NULL);

  return PHOSH_CALL (call)->avatar_icon;
}


static const char *
phosh_call_get_id (CuiCall *call)
{
  PhoshCall *self;

  g_return_val_if_fail (PHOSH_IS_CALL (call), NULL);
  self = PHOSH_CALL (call);

  return phosh_calls_dbus_calls_call_get_id (self->proxy);
}


static const char *
phosh_call_get_display_name (CuiCall *call)
{
  PhoshCall *self;

  g_return_val_if_fail (PHOSH_IS_CALL (call), NULL);
  self = PHOSH_CALL (call);

  return phosh_calls_dbus_calls_call_get_display_name (self->proxy);
}


static CuiCallState
phosh_call_get_state (CuiCall *call)
{
  PhoshCall *self;

  g_return_val_if_fail (PHOSH_IS_CALL (call), CUI_CALL_STATE_UNKNOWN);
  self = PHOSH_CALL (call);

  return phosh_calls_dbus_calls_call_get_state (self->proxy);
}


static gboolean
phosh_call_get_encrypted (CuiCall *call)
{
  PhoshCall *self;

  g_return_val_if_fail (PHOSH_IS_CALL (call), CUI_CALL_STATE_UNKNOWN);
  self = PHOSH_CALL (call);

  return phosh_calls_dbus_calls_call_get_encrypted (self->proxy);
}


static gboolean
phosh_call_get_can_dtmf (CuiCall *call)
{
  PhoshCall *self;

  g_return_val_if_fail (PHOSH_IS_CALL (call), CUI_CALL_STATE_UNKNOWN);
  self = PHOSH_CALL (call);

  return phosh_calls_dbus_calls_call_get_can_dtmf (self->proxy);
}


static void
on_prop_changed (PhoshCall *self, GParamSpec *pspec)
{
  const char *name = g_param_spec_get_name (pspec);

  /* Just forward any property changes, we fetch them from the DBus proxy anyway */
  if (g_strcmp0 (name, "state") == 0 ||
      g_strcmp0 (name, "encrypted") == 0 ||
      g_strcmp0 (name, "id") == 0 ||
      g_strcmp0 (name, "display-name") == 0 ||
      g_strcmp0 (name, "can-dtmf")) {
    g_object_notify (G_OBJECT (self), name);
  }
}


static void
phosh_call_set_dbus_proxy (PhoshCall *self, PhoshCallsDBusCallsCall *proxy)
{
  self->proxy = g_object_ref (proxy);

  g_object_connect (self->proxy,
                    "swapped-signal::notify::state", G_CALLBACK (on_prop_changed), self,
                    "swapped-signal::notify::encrypted", G_CALLBACK (on_prop_changed), self,
                    "swapped-signal::notify::id", G_CALLBACK (on_prop_changed), self,
                    "swapped-signal::notify::display-name", G_CALLBACK (on_prop_changed), self,
                    "swapped-signal::notify::can-dtmf", G_CALLBACK (on_prop_changed), self,
                    NULL);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_DBUS_PROXY]);
}


static void
phosh_call_set_property (GObject      *object,
                         guint         property_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  PhoshCall *self = PHOSH_CALL (object);

  switch (property_id) {
  case PROP_DBUS_PROXY:
    /* construct only */
    phosh_call_set_dbus_proxy (self, g_value_get_object (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_call_get_property (GObject    *object,
                         guint       property_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  PhoshCall *self = PHOSH_CALL (object);
  CuiCall *iface = CUI_CALL (object);

  switch (property_id) {
  case PROP_DBUS_PROXY:
    g_value_set_object (value, self->proxy);
    break;
  case PROP_AVATAR_ICON:
    g_value_set_object (value, self->avatar_icon);
    break;
  case PROP_ID:
    g_value_set_string (value, phosh_call_get_id (iface));
    break;
  case PROP_DISPLAY_NAME:
    g_value_set_string (value, phosh_call_get_display_name (iface));
    break;
  case PROP_STATE:
    g_value_set_enum (value, phosh_call_get_state (iface));
    break;
  case PROP_ENCRYPTED:
    g_value_set_boolean (value, phosh_call_get_encrypted (iface));
    break;
  case PROP_CAN_DTMF:
    g_value_set_boolean (value, phosh_call_get_can_dtmf (iface));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_call_constructed (GObject *object)
{
  PhoshCall *self = PHOSH_CALL (object);
  g_autoptr (GFile) file = NULL;
  const char *path = NULL;

  G_OBJECT_CLASS (phosh_call_parent_class)->constructed (object);

  path = phosh_calls_dbus_calls_call_get_image_path (self->proxy);
  if (path) {
    file = g_file_new_for_path (path);
    if (file) {
      self->avatar_icon = G_LOADABLE_ICON (g_file_icon_new (file));
    }
  }
}


static void
phosh_call_dispose (GObject *object)
{
  PhoshCall *self = PHOSH_CALL (object);

  g_cancellable_cancel (self->cancel);
  g_clear_object (&self->cancel);
  g_signal_handlers_disconnect_by_data (self->proxy, self);
  g_clear_object (&self->proxy);
  g_clear_object (&self->avatar_icon);

  G_OBJECT_CLASS (phosh_call_parent_class)->dispose (object);
}


static void
phosh_call_class_init (PhoshCallClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = phosh_call_constructed;
  object_class->dispose = phosh_call_dispose;
  object_class->set_property = phosh_call_set_property;
  object_class->get_property = phosh_call_get_property;

  /**
   * PhoshCall:dbus-proxy:
   *
   * The DBus proxy object to a call on gnome-calls DBus interface
   */
  props[PROP_DBUS_PROXY] = g_param_spec_object ("dbus-proxy",
                                                "",
                                                "",
                                                PHOSH_CALLS_DBUS_TYPE_CALLS_CALL,
                                                G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                                G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  g_object_class_override_property (object_class,
                                    PROP_AVATAR_ICON,
                                    "avatar-icon");

  g_object_class_override_property (object_class,
                                    PROP_DISPLAY_NAME,
                                    "id");

  g_object_class_override_property (object_class,
                                    PROP_ID,
                                    "display-name");

  g_object_class_override_property (object_class,
                                    PROP_STATE,
                                    "state");

  g_object_class_override_property (object_class,
                                    PROP_ENCRYPTED,
                                    "encrypted");

  g_object_class_override_property (object_class,
                                    PROP_CAN_DTMF,
                                    "can-dtmf");
}


static void
on_call_accept_finish (PhoshCallsDBusCallsCall *proxy,
                       GAsyncResult            *res,
                       gpointer                 unused)
{
  g_autoptr (GError) err = NULL;

  g_return_if_fail (PHOSH_CALLS_DBUS_IS_CALLS_CALL_PROXY (proxy));

  if (!phosh_calls_dbus_calls_call_call_accept_finish (proxy, res, &err))
    phosh_async_error_warn (err, "Failed to accept call %p", proxy);
}


static void
phosh_call_accept (CuiCall *call)
{
  PhoshCall *self;

  g_return_if_fail (PHOSH_IS_CALL (call));
  self = PHOSH_CALL (call);

  phosh_calls_dbus_calls_call_call_accept (self->proxy,
                                           self->cancel,
                                           (GAsyncReadyCallback) on_call_accept_finish,
                                           NULL);
}

static void
on_call_hangup_finish (PhoshCallsDBusCallsCall *proxy,
                       GAsyncResult            *res,
                       gpointer                 unused)
{
  g_autoptr (GError) err = NULL;

  g_return_if_fail (PHOSH_CALLS_DBUS_IS_CALLS_CALL_PROXY (proxy));

  if (!phosh_calls_dbus_calls_call_call_hangup_finish (proxy, res, &err))
    phosh_async_error_warn (err, "Failed to hangup call %p", proxy);
}


static void
phosh_call_hang_up (CuiCall *call)
{
  PhoshCall *self;

  g_return_if_fail (PHOSH_IS_CALL (call));
  self = PHOSH_CALL (call);

  phosh_calls_dbus_calls_call_call_hangup (self->proxy,
                                           self->cancel,
                                           (GAsyncReadyCallback) on_call_hangup_finish,
                                           NULL);
}


static void
on_call_send_dtmf_finish (PhoshCallsDBusCallsCall *proxy,
                          GAsyncResult            *res,
                          gpointer                 dtmf_key)
{
  g_autoptr (GError) err = NULL;
  char key = (char) GPOINTER_TO_INT (dtmf_key);

  g_return_if_fail (PHOSH_CALLS_DBUS_IS_CALLS_CALL_PROXY (proxy));

  if (!phosh_calls_dbus_calls_call_call_send_dtmf_finish (proxy, res, &err))
    phosh_async_error_warn(err, "Failed to send DTMF `%c' %p", key, proxy);
}


static void
phosh_call_send_dtmf (CuiCall *call, const char *dtmf)
{
  PhoshCall *self;

  g_return_if_fail (PHOSH_IS_CALL (call));

  self = PHOSH_CALL (call);

  phosh_calls_dbus_calls_call_call_send_dtmf (self->proxy,
                                              dtmf,
                                              self->cancel,
                                              (GAsyncReadyCallback) on_call_send_dtmf_finish,
                                              GINT_TO_POINTER (dtmf));
}


static void
phosh_call_cui_call_interface_init (CuiCallInterface *iface)
{
  iface->get_avatar_icon = phosh_call_get_avatar_icon;
  iface->get_id = phosh_call_get_id;
  iface->get_display_name = phosh_call_get_display_name;
  iface->get_state = phosh_call_get_state;
  iface->get_encrypted = phosh_call_get_encrypted;
  iface->get_can_dtmf = phosh_call_get_can_dtmf;

  iface->accept = phosh_call_accept;
  iface->hang_up = phosh_call_hang_up;
  iface->send_dtmf = phosh_call_send_dtmf;
}


static void
phosh_call_init (PhoshCall *self)
{
  self->cancel = g_cancellable_new ();

  /* TODO: once DBus handles it */
  self->can_dtmf = FALSE;
}


PhoshCall *
phosh_call_new (PhoshCallsDBusCallsCall *proxy)
{
  return g_object_new (PHOSH_TYPE_CALL,
                       "dbus-proxy", proxy,
                       NULL);
}
