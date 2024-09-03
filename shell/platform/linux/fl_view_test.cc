// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/public/flutter_linux/fl_view.h"
#include "flutter/shell/platform/linux/fl_view_private.h"
#include "flutter/shell/platform/linux/testing/fl_test_gtk_logs.h"

#include "gtest/gtest.h"

TEST(FlViewTest, GetEngine) {
  flutter::testing::fl_ensure_gtk_init();
  g_autoptr(FlDartProject) project = fl_dart_project_new();
  g_autoptr(FlEngine) engine = fl_engine_new(project);
  g_autoptr(FlView) view = fl_view_new_implicit(engine);

  // Check the engine is immediately available (i.e. before the widget is
  // realized).
  FlEngine* engine2 = fl_view_get_engine(view);
  EXPECT_NE(engine2, nullptr);

  g_object_ref_sink(view);
}

TEST(FlViewTest, StateUpdateDoesNotHappenInInit) {
  flutter::testing::fl_ensure_gtk_init();
  g_autoptr(FlDartProject) project = fl_dart_project_new();
  g_autoptr(FlEngine) engine = fl_engine_new(project);
  g_autoptr(FlView) view = fl_view_new_implicit(engine);
  // Check that creating a view doesn't try to query the window state in
  // initialization, causing a critical log to be issued.
  EXPECT_EQ(
      flutter::testing::fl_get_received_gtk_log_levels() & G_LOG_LEVEL_CRITICAL,
      (GLogLevelFlags)0x0);
  g_object_ref_sink(view);
}
