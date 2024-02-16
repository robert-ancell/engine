// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_FL_RENDERER_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_FL_RENDERER_H_

#include "flutter/shell/platform/linux/public/flutter_linux/fl_dart_project.h"
#include "flutter/shell/platform/linux/public/flutter_linux/fl_view.h"

#include "flutter/shell/platform/embedder/embedder.h"

G_BEGIN_DECLS

/**
 * FlRendererError:
 * Errors for #FlRenderer objects to set on failures.
 */

typedef enum {
  // NOLINTBEGIN(readability-identifier-naming)
  FL_RENDERER_ERROR_FAILED,
  // NOLINTEND(readability-identifier-naming)
} FlRendererError;

GQuark fl_renderer_error_quark(void) G_GNUC_CONST;

G_DECLARE_DERIVABLE_TYPE(FlRenderer, fl_renderer, FL, RENDERER, GObject)

/**
 * FlRenderer:
 *
 * #FlRenderer is an abstract class that allows Flutter to draw pixels.
 */

struct _FlRendererClass {
  GObjectClass parent_class;

  /**
   * Virtual method called when Flutter starts the renderer.
   * @renderer: an #FlRenderer.
   * @error: (allow-none): #GError location to store the error occurring, or
   * %NULL
   *
   * Returns: %TRUE if renderer successfully started.
   */
  gboolean (*start)(FlRenderer* renderer, GError** error);

  /**
   * Virtual method called when Flutter needs a backing store for a specific
   * #FlutterLayer.
   * @renderer: an #FlRenderer.
   * @config: backing store config.
   * @backing_store_out: saves created backing store.
   *
   * Returns %TRUE if successful.
   */
  gboolean (*create_backing_store)(FlRenderer* renderer,
                                   const FlutterBackingStoreConfig* config,
                                   FlutterBackingStore* backing_store_out);

  /**
   * Virtual method called when Flutter wants to release the backing store.
   * @renderer: an #FlRenderer.
   * @backing_store: backing store to be released.
   *
   * Returns %TRUE if successful.
   */
  gboolean (*collect_backing_store)(FlRenderer* renderer,
                                    const FlutterBackingStore* backing_store);

  /**
   * Virtual method called when Flutter wants to composite layers onto the
   * screen.
   * @renderer: an #FlRenderer.
   * @layers: layers to be composited.
   * @layers_count: number of layers.
   *
   * Returns %TRUE if successful.
   */
  gboolean (*present_layers)(FlRenderer* renderer,
                             const FlutterLayer** layers,
                             size_t layers_count);
};

/**
 * fl_renderer_start:
 * @renderer: an #FlRenderer.
 * @view: the view Flutter is renderering to.
 * @error: (allow-none): #GError location to store the error occurring, or %NULL
 * to ignore.
 *
 * Start the renderer.
 *
 * Returns: %TRUE if successfully started.
 */
gboolean fl_renderer_start(FlRenderer* renderer, FlView* view, GError** error);

/**
 * fl_renderer_get_view:
 * @renderer: an #FlRenderer.
 *
 * Returns: targeted #FlView or %NULL if headless.
 */
FlView* fl_renderer_get_view(FlRenderer* renderer);

/**
 * fl_renderer_create_backing_store:
 * @renderer: an #FlRenderer.
 * @config: backing store config.
 * @backing_store_out: saves created backing store.
 *
 * Obtain a backing store for a specific #FlutterLayer.
 *
 * Returns %TRUE if successful.
 */
gboolean fl_renderer_create_backing_store(
    FlRenderer* renderer,
    const FlutterBackingStoreConfig* config,
    FlutterBackingStore* backing_store_out);

/**
 * fl_renderer_collect_backing_store:
 * @renderer: an #FlRenderer.
 * @backing_store: backing store to be released.
 *
 * A callback invoked by the engine to release the backing store. The
 * embedder may collect any resources associated with the backing store.
 *
 * Returns %TRUE if successful.
 */
gboolean fl_renderer_collect_backing_store(
    FlRenderer* renderer,
    const FlutterBackingStore* backing_store);

/**
 * fl_renderer_present_layers:
 * @renderer: an #FlRenderer.
 * @layers: layers to be composited.
 * @layers_count: number of layers.
 *
 * Callback invoked by the engine to composite the contents of each layer
 * onto the screen.
 *
 * Returns %TRUE if successful.
 */
gboolean fl_renderer_present_layers(FlRenderer* renderer,
                                    const FlutterLayer** layers,
                                    size_t layers_count);

/**
 * fl_renderer_wait_for_frame:
 * @renderer: an #FlRenderer.
 * @target_width: width of frame being waited for
 * @target_height: height of frame being waited for
 *
 * Holds the thread until frame with requested dimensions is presented.
 * While waiting for frame Flutter platform and raster tasks are being
 * processed.
 */
void fl_renderer_wait_for_frame(FlRenderer* renderer,
                                int target_width,
                                int target_height);

G_END_DECLS

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_FL_RENDERER_H_
