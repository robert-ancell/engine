// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_IMPELLER_RENDERER_BACKEND_VULKAN_SURFACE_VK_H_
#define FLUTTER_IMPELLER_RENDERER_BACKEND_VULKAN_SURFACE_VK_H_

#include <memory>

#include "flutter/fml/macros.h"
#include "impeller/renderer/backend/vulkan/context_vk.h"
#include "impeller/renderer/backend/vulkan/swapchain_image_vk.h"
#include "impeller/renderer/surface.h"

namespace impeller {

class SurfaceVK final : public Surface {
 public:
  using SwapCallback = std::function<bool(void)>;

  static std::unique_ptr<SurfaceVK> WrapSwapchainImage(
      const std::shared_ptr<Context>& context,
      std::shared_ptr<SwapchainImageVK>& swapchain_image,
      SwapCallback swap_callback,
      bool enable_msaa = true);

  // |Surface|
  ~SurfaceVK() override;

 private:
  SwapCallback swap_callback_;

  SurfaceVK(const RenderTarget& target, SwapCallback swap_callback);

  // |Surface|
  bool Present() const override;

  SurfaceVK(const SurfaceVK&) = delete;

  SurfaceVK& operator=(const SurfaceVK&) = delete;
};

}  // namespace impeller

#endif  // FLUTTER_IMPELLER_RENDERER_BACKEND_VULKAN_SURFACE_VK_H_
