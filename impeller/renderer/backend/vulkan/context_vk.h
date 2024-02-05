// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_IMPELLER_RENDERER_BACKEND_VULKAN_CONTEXT_VK_H_
#define FLUTTER_IMPELLER_RENDERER_BACKEND_VULKAN_CONTEXT_VK_H_

#include <memory>

#include "flutter/fml/concurrent_message_loop.h"
#include "flutter/fml/mapping.h"
#include "flutter/fml/unique_fd.h"
#include "fml/thread.h"
#include "impeller/base/backend_cast.h"
#include "impeller/core/formats.h"
#include "impeller/renderer/backend/vulkan/command_pool_vk.h"
#include "impeller/renderer/backend/vulkan/device_holder.h"
#include "impeller/renderer/backend/vulkan/pipeline_library_vk.h"
#include "impeller/renderer/backend/vulkan/queue_vk.h"
#include "impeller/renderer/backend/vulkan/sampler_library_vk.h"
#include "impeller/renderer/backend/vulkan/shader_library_vk.h"
#include "impeller/renderer/capabilities.h"
#include "impeller/renderer/command_queue.h"
#include "impeller/renderer/context.h"

namespace impeller {

bool HasValidationLayers();

class CommandEncoderFactoryVK;
class CommandEncoderVK;
class CommandPoolRecyclerVK;
class DebugReportVK;
class FenceWaiterVK;
class ResourceManagerVK;
class SurfaceContextVK;
class GPUTracerVK;
class DescriptorPoolRecyclerVK;
class CommandQueueVK;

class ContextVK final : public Context,
                        public BackendCast<ContextVK, Context>,
                        public std::enable_shared_from_this<ContextVK> {
 public:
  struct Settings {
    PFN_vkGetInstanceProcAddr proc_address_callback = nullptr;
    std::vector<std::shared_ptr<fml::Mapping>> shader_libraries_data;
    fml::UniqueFD cache_directory;
    bool enable_validation = false;
    bool enable_gpu_tracing = false;

    Settings() = default;

    Settings(Settings&&) = default;
  };

  static std::shared_ptr<ContextVK> Create(Settings settings);

  uint64_t GetHash() const { return hash_; }

  // |Context|
  ~ContextVK() override;

  // |Context|
  BackendType GetBackendType() const override;

  // |Context|
  std::string DescribeGpuModel() const override;

  // |Context|
  bool IsValid() const override;

  // |Context|
  std::shared_ptr<Allocator> GetResourceAllocator() const override;

  // |Context|
  std::shared_ptr<ShaderLibrary> GetShaderLibrary() const override;

  // |Context|
  std::shared_ptr<SamplerLibrary> GetSamplerLibrary() const override;

  // |Context|
  std::shared_ptr<PipelineLibrary> GetPipelineLibrary() const override;

  // |Context|
  std::shared_ptr<CommandBuffer> CreateCommandBuffer() const override;

  // |Context|
  const std::shared_ptr<const Capabilities>& GetCapabilities() const override;

  // |Context|
  void Shutdown() override;

  // |Context|
  void SetSyncPresentation(bool value) override { sync_presentation_ = value; }

  bool GetSyncPresentation() const { return sync_presentation_; }

  void SetOffscreenFormat(PixelFormat pixel_format);

  template <typename T>
  bool SetDebugName(T handle, std::string_view label) const {
    return SetDebugName(GetDevice(), handle, label);
  }

  template <typename T>
  static bool SetDebugName(const vk::Device& device,
                           T handle,
                           std::string_view label) {
    if (!HasValidationLayers()) {
      // No-op if validation layers are not enabled.
      return true;
    }

    auto c_handle = static_cast<typename T::CType>(handle);

    vk::DebugUtilsObjectNameInfoEXT info;
    info.objectType = T::objectType;
    info.pObjectName = label.data();
    info.objectHandle = reinterpret_cast<decltype(info.objectHandle)>(c_handle);

    if (device.setDebugUtilsObjectNameEXT(info) != vk::Result::eSuccess) {
      VALIDATION_LOG << "Unable to set debug name: " << label;
      return false;
    }

    return true;
  }

  std::shared_ptr<DeviceHolder> GetDeviceHolder() const {
    return device_holder_;
  }

  vk::Instance GetInstance() const;

  const vk::Device& GetDevice() const;

  const std::shared_ptr<fml::ConcurrentTaskRunner>
  GetConcurrentWorkerTaskRunner() const;

  /// @brief A single-threaded task runner that should only be used for
  ///        submitKHR.
  ///
  /// SubmitKHR will block until all previously submitted command buffers have
  /// been scheduled. If there are no platform views in the scene (excluding
  /// texture backed platform views). Then it is safe for SwapchainImpl::Present
  /// to return before submit has completed. To do so, we offload the submit
  /// command to a specialized single threaded task runner. The single thread
  /// ensures that we do not queue up too much work and that the submissions
  /// proceed in order.
  const fml::RefPtr<fml::TaskRunner> GetQueueSubmitRunner() const;

  std::shared_ptr<SurfaceContextVK> CreateSurfaceContext();

  const std::shared_ptr<QueueVK>& GetGraphicsQueue() const;

  vk::PhysicalDevice GetPhysicalDevice() const;

  std::shared_ptr<FenceWaiterVK> GetFenceWaiter() const;

  std::shared_ptr<ResourceManagerVK> GetResourceManager() const;

  std::shared_ptr<CommandPoolRecyclerVK> GetCommandPoolRecycler() const;

  std::shared_ptr<DescriptorPoolRecyclerVK> GetDescriptorPoolRecycler() const;

  std::shared_ptr<CommandQueue> GetCommandQueue() const override;

  std::shared_ptr<GPUTracerVK> GetGPUTracer() const;

  void RecordFrameEndTime() const;

 private:
  struct DeviceHolderImpl : public DeviceHolder {
    // |DeviceHolder|
    const vk::Device& GetDevice() const override { return device.get(); }
    // |DeviceHolder|
    const vk::PhysicalDevice& GetPhysicalDevice() const override {
      return physical_device;
    }

    vk::UniqueInstance instance;
    vk::PhysicalDevice physical_device;
    vk::UniqueDevice device;
  };

  std::shared_ptr<DeviceHolderImpl> device_holder_;
  std::unique_ptr<DebugReportVK> debug_report_;
  std::shared_ptr<Allocator> allocator_;
  std::shared_ptr<ShaderLibraryVK> shader_library_;
  std::shared_ptr<SamplerLibraryVK> sampler_library_;
  std::shared_ptr<PipelineLibraryVK> pipeline_library_;
  QueuesVK queues_;
  std::shared_ptr<const Capabilities> device_capabilities_;
  std::shared_ptr<FenceWaiterVK> fence_waiter_;
  std::shared_ptr<ResourceManagerVK> resource_manager_;
  std::shared_ptr<CommandPoolRecyclerVK> command_pool_recycler_;
  std::string device_name_;
  std::shared_ptr<fml::ConcurrentMessageLoop> raster_message_loop_;
  std::unique_ptr<fml::Thread> queue_submit_thread_;
  std::shared_ptr<GPUTracerVK> gpu_tracer_;
  std::shared_ptr<DescriptorPoolRecyclerVK> descriptor_pool_recycler_;
  std::shared_ptr<CommandQueue> command_queue_vk_;

  bool sync_presentation_ = false;
  const uint64_t hash_;

  bool is_valid_ = false;

  ContextVK();

  void Setup(Settings settings);

  std::unique_ptr<CommandEncoderFactoryVK> CreateGraphicsCommandEncoderFactory()
      const;

  ContextVK(const ContextVK&) = delete;

  ContextVK& operator=(const ContextVK&) = delete;
};

}  // namespace impeller

#endif  // FLUTTER_IMPELLER_RENDERER_BACKEND_VULKAN_CONTEXT_VK_H_
