// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/windows/egl/manager.h"

#include <vector>

#include "flutter/fml/logging.h"
#include "flutter/shell/platform/windows/egl/egl.h"

namespace flutter {
namespace egl {

int Manager::instance_count_ = 0;

std::unique_ptr<Manager> Manager::Create(bool enable_impeller) {
  std::unique_ptr<Manager> manager;
  manager.reset(new Manager(enable_impeller));
  if (!manager->IsValid()) {
    return nullptr;
  }
  return std::move(manager);
}

Manager::Manager(bool enable_impeller) {
  ++instance_count_;

  if (!InitializeDisplay()) {
    return;
  }

  if (!InitializeConfig(enable_impeller)) {
    return;
  }

  if (!InitializeContexts()) {
    return;
  }

  is_valid_ = true;
}

Manager::~Manager() {
  CleanUp();
  --instance_count_;
}

bool Manager::InitializeDisplay() {
  // These are preferred display attributes and request ANGLE's D3D11
  // renderer. eglInitialize will only succeed with these attributes if the
  // hardware supports D3D11 Feature Level 10_0+.
  const EGLint d3d11_display_attributes[] = {
      EGL_PLATFORM_ANGLE_TYPE_ANGLE,
      EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,

      // EGL_PLATFORM_ANGLE_ENABLE_AUTOMATIC_TRIM_ANGLE is an option that will
      // enable ANGLE to automatically call the IDXGIDevice3::Trim method on
      // behalf of the application when it gets suspended.
      EGL_PLATFORM_ANGLE_ENABLE_AUTOMATIC_TRIM_ANGLE,
      EGL_TRUE,

      // This extension allows angle to render directly on a D3D swapchain
      // in the correct orientation on D3D11.
      EGL_EXPERIMENTAL_PRESENT_PATH_ANGLE,
      EGL_EXPERIMENTAL_PRESENT_PATH_FAST_ANGLE,

      EGL_NONE,
  };

  // These are used to request ANGLE's D3D11 renderer, with D3D11 Feature
  // Level 9_3.
  const EGLint d3d11_fl_9_3_display_attributes[] = {
      EGL_PLATFORM_ANGLE_TYPE_ANGLE,
      EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
      EGL_PLATFORM_ANGLE_MAX_VERSION_MAJOR_ANGLE,
      9,
      EGL_PLATFORM_ANGLE_MAX_VERSION_MINOR_ANGLE,
      3,
      EGL_PLATFORM_ANGLE_ENABLE_AUTOMATIC_TRIM_ANGLE,
      EGL_TRUE,
      EGL_NONE,
  };

  // These attributes request D3D11 WARP (software rendering fallback) in case
  // hardware-backed D3D11 is unavailable.
  const EGLint d3d11_warp_display_attributes[] = {
      EGL_PLATFORM_ANGLE_TYPE_ANGLE,
      EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
      EGL_PLATFORM_ANGLE_ENABLE_AUTOMATIC_TRIM_ANGLE,
      EGL_TRUE,
      EGL_NONE,
  };

  std::vector<const EGLint*> display_attributes_configs = {
      d3d11_display_attributes,
      d3d11_fl_9_3_display_attributes,
      d3d11_warp_display_attributes,
  };

  PFNEGLGETPLATFORMDISPLAYEXTPROC egl_get_platform_display_EXT =
      reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
          ::eglGetProcAddress("eglGetPlatformDisplayEXT"));
  if (!egl_get_platform_display_EXT) {
    LogEGLError("eglGetPlatformDisplayEXT not available");
    return false;
  }

  // Attempt to initialize ANGLE's renderer in order of: D3D11, D3D11 Feature
  // Level 9_3 and finally D3D11 WARP.
  for (auto config : display_attributes_configs) {
    bool is_last = (config == display_attributes_configs.back());

    display_ = egl_get_platform_display_EXT(EGL_PLATFORM_ANGLE_ANGLE,
                                            EGL_DEFAULT_DISPLAY, config);

    if (display_ == EGL_NO_DISPLAY) {
      if (is_last) {
        LogEGLError("Failed to get a compatible EGLdisplay");
        return false;
      }

      // Try the next config.
      continue;
    }

    if (::eglInitialize(display_, nullptr, nullptr) == EGL_FALSE) {
      if (is_last) {
        LogEGLError("Failed to initialize EGL via ANGLE");
        return false;
      }

      // Try the next config.
      continue;
    }

    return true;
  }

  FML_UNREACHABLE();
}

bool Manager::InitializeConfig(bool enable_impeller) {
  const EGLint config_attributes[] = {EGL_RED_SIZE,   8, EGL_GREEN_SIZE,   8,
                                      EGL_BLUE_SIZE,  8, EGL_ALPHA_SIZE,   8,
                                      EGL_DEPTH_SIZE, 8, EGL_STENCIL_SIZE, 8,
                                      EGL_NONE};

  const EGLint impeller_config_attributes[] = {
      EGL_RED_SIZE,       8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE,    8,
      EGL_ALPHA_SIZE,     8, EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 8,
      EGL_SAMPLE_BUFFERS, 1, EGL_SAMPLES,    4, EGL_NONE};
  const EGLint impeller_config_attributes_no_msaa[] = {
      EGL_RED_SIZE,   8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE,    8,
      EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 8,
      EGL_NONE};

  EGLBoolean result;
  EGLint num_config = 0;

  if (enable_impeller) {
    // First try the MSAA configuration.
    result = ::eglChooseConfig(display_, impeller_config_attributes, &config_,
                               1, &num_config);

    if (result == EGL_TRUE && num_config > 0) {
      return true;
    }

    // Next fall back to disabled MSAA.
    result = ::eglChooseConfig(display_, impeller_config_attributes_no_msaa,
                               &config_, 1, &num_config);
    if (result == EGL_TRUE && num_config == 0) {
      return true;
    }
  } else {
    result = ::eglChooseConfig(display_, config_attributes, &config_, 1,
                               &num_config);

    if (result == EGL_TRUE && num_config > 0) {
      return true;
    }
  }

  LogEGLError("Failed to choose EGL config");
  return false;
}

bool Manager::InitializeContexts() {
  const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};

  auto const render_context =
      ::eglCreateContext(display_, config_, EGL_NO_CONTEXT, context_attributes);
  if (render_context == EGL_NO_CONTEXT) {
    LogEGLError("Failed to create EGL render context");
    return false;
  }

  auto const resource_context =
      ::eglCreateContext(display_, config_, render_context, context_attributes);
  if (resource_context == EGL_NO_CONTEXT) {
    LogEGLError("Failed to create EGL resource context");
    return false;
  }

  render_context_ = std::make_unique<Context>(display_, render_context);
  resource_context_ = std::make_unique<Context>(display_, resource_context);
  return true;
}

bool Manager::InitializeDevice() {
  const auto query_display_attrib_EXT =
      reinterpret_cast<PFNEGLQUERYDISPLAYATTRIBEXTPROC>(
          ::eglGetProcAddress("eglQueryDisplayAttribEXT"));
  const auto query_device_attrib_EXT =
      reinterpret_cast<PFNEGLQUERYDEVICEATTRIBEXTPROC>(
          ::eglGetProcAddress("eglQueryDeviceAttribEXT"));

  if (query_display_attrib_EXT == nullptr ||
      query_device_attrib_EXT == nullptr) {
    return false;
  }

  EGLAttrib egl_device = 0;
  EGLAttrib angle_device = 0;

  auto result = query_display_attrib_EXT(display_, EGL_DEVICE_EXT, &egl_device);
  if (result != EGL_TRUE) {
    return false;
  }

  result = query_device_attrib_EXT(reinterpret_cast<EGLDeviceEXT>(egl_device),
                                   EGL_D3D11_DEVICE_ANGLE, &angle_device);
  if (result != EGL_TRUE) {
    return false;
  }

  resolved_device_ = reinterpret_cast<ID3D11Device*>(angle_device);
  return true;
}

void Manager::CleanUp() {
  EGLBoolean result = EGL_FALSE;

  // Needs to be reset before destroying the contexts.
  resolved_device_.Reset();

  // Needs to be reset before destroying the EGLDisplay.
  render_context_.reset();
  resource_context_.reset();

  if (display_ != EGL_NO_DISPLAY) {
    // Display is reused between instances so only terminate display
    // if destroying last instance
    if (instance_count_ == 1) {
      ::eglTerminate(display_);
    }
    display_ = EGL_NO_DISPLAY;
  }
}

bool Manager::IsValid() const {
  return is_valid_;
}

bool Manager::CreateWindowSurface(HWND hwnd, size_t width, size_t height) {
  FML_DCHECK(surface_ == nullptr || !surface_->IsValid());

  if (!hwnd || !is_valid_) {
    return false;
  }

  // Disable ANGLE's automatic surface resizing and provide an explicit size.
  // The surface will need to be destroyed and re-created if the HWND is
  // resized.
  const EGLint surface_attributes[] = {EGL_FIXED_SIZE_ANGLE,
                                       EGL_TRUE,
                                       EGL_WIDTH,
                                       static_cast<EGLint>(width),
                                       EGL_HEIGHT,
                                       static_cast<EGLint>(height),
                                       EGL_NONE};

  auto const surface = ::eglCreateWindowSurface(
      display_, config_, static_cast<EGLNativeWindowType>(hwnd),
      surface_attributes);
  if (surface == EGL_NO_SURFACE) {
    LogEGLError("Surface creation failed.");
    return false;
  }

  surface_ = std::make_unique<WindowSurface>(
      display_, render_context_->GetHandle(), surface, width, height);
  return true;
}

void Manager::ResizeWindowSurface(HWND hwnd, size_t width, size_t height) {
  FML_CHECK(surface_ != nullptr);

  auto const existing_width = surface_->width();
  auto const existing_height = surface_->height();
  auto const existing_vsync = surface_->vsync_enabled();

  if (width != existing_width || height != existing_height) {
    // TODO: Destroying the surface and re-creating it is expensive.
    // Ideally this would use ANGLE's automatic surface sizing instead.
    // See: https://github.com/flutter/flutter/issues/79427
    if (!surface_->Destroy()) {
      FML_LOG(ERROR) << "Manager::ResizeSurface failed to destroy surface";
      return;
    }

    if (!CreateWindowSurface(hwnd, width, height)) {
      FML_LOG(ERROR) << "Manager::ResizeSurface failed to create surface";
      return;
    }

    if (!surface_->MakeCurrent() ||
        !surface_->SetVSyncEnabled(existing_vsync)) {
      // Surfaces block until the v-blank by default.
      // Failing to update the vsync might result in unnecessary blocking.
      // This regresses performance but not correctness.
      FML_LOG(ERROR) << "Manager::ResizeSurface failed to set vsync";
    }
  }
}

bool Manager::HasContextCurrent() {
  return ::eglGetCurrentContext() != EGL_NO_CONTEXT;
}

EGLSurface Manager::CreateSurfaceFromHandle(EGLenum handle_type,
                                            EGLClientBuffer handle,
                                            const EGLint* attributes) const {
  return ::eglCreatePbufferFromClientBuffer(display_, handle_type, handle,
                                            config_, attributes);
}

bool Manager::GetDevice(ID3D11Device** device) {
  if (!resolved_device_) {
    if (!InitializeDevice()) {
      return false;
    }
  }

  resolved_device_.CopyTo(device);
  return (resolved_device_ != nullptr);
}

Context* Manager::render_context() const {
  return render_context_.get();
}

Context* Manager::resource_context() const {
  return resource_context_.get();
}

WindowSurface* Manager::surface() const {
  return surface_.get();
}

}  // namespace egl
}  // namespace flutter
