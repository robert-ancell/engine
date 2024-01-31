// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/fl_key_event.h"

static void dispose_origin_from_gdk_event(gpointer origin) {
  g_return_if_fail(origin != nullptr);
  g_object_unref(reinterpret_cast<GObject*>(origin));
}

FlKeyEvent* fl_key_event_new_from_gdk_event(GdkEvent* event) {
  g_return_val_if_fail(event != nullptr, nullptr);
  GdkEventType type = gdk_event_get_event_type(event);
  g_return_val_if_fail(type == GDK_KEY_PRESS || type == GDK_KEY_RELEASE,
                       nullptr);
  FlKeyEvent* result = g_new(FlKeyEvent, 1);

  guint16 keycode = 0;
  // gdk_event_get_keycode(event, &keycode);
  guint keyval = 0;
  // gdk_event_get_keyval(event, &keyval);
  GdkModifierType state = static_cast<GdkModifierType>(0);
  // gdk_event_get_state(event, &state);
  guint group = 0;
  // gdk_event_get_key_group(event, &group);

  result->time = gdk_event_get_time(event);
  result->is_press = type == GDK_KEY_PRESS;
  result->keycode = keycode;
  result->keyval = keyval;
  result->state = state;
  result->group = group;
  result->origin = event;
  result->dispose_origin = dispose_origin_from_gdk_event;

  return result;
}

void fl_key_event_dispose(FlKeyEvent* event) {
  if (event->dispose_origin != nullptr) {
    event->dispose_origin(event->origin);
  }
  g_free(event);
}

FlKeyEvent* fl_key_event_clone(const FlKeyEvent* event) {
  FlKeyEvent* new_event = g_new(FlKeyEvent, 1);
  *new_event = *event;
  return new_event;
}
