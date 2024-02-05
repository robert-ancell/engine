// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_IMPELLER_ENTITY_ENTITY_PASS_H_
#define FLUTTER_IMPELLER_ENTITY_ENTITY_PASS_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "impeller/entity/contents/contents.h"
#include "impeller/entity/contents/filters/filter_contents.h"
#include "impeller/entity/entity.h"
#include "impeller/entity/entity_pass_delegate.h"
#include "impeller/entity/inline_pass_context.h"
#include "impeller/renderer/render_target.h"

namespace impeller {

class ContentContext;
class EntityPassClipRecorder;

class EntityPass {
 public:
  /// Elements are renderable items in the `EntityPass`. Each can either be an
  /// `Entity` or a child `EntityPass`.
  ///
  /// When the element is a child `EntityPass`, it may be rendered to an
  /// offscreen texture and converted into an `Entity` that draws the texture
  /// into the current pass, or its children may be collapsed into the current
  ///
  /// `EntityPass`. Elements are converted to Entities in
  /// `GetEntityForElement()`.
  using Element = std::variant<Entity, std::unique_ptr<EntityPass>>;

  static const std::string kCaptureDocumentName;

  using BackdropFilterProc = std::function<std::shared_ptr<FilterContents>(
      FilterInput::Ref,
      const Matrix& effect_transform,
      Entity::RenderingMode rendering_mode)>;

  struct ClipCoverageLayer {
    std::optional<Rect> coverage;
    size_t clip_depth;
  };

  using ClipCoverageStack = std::vector<ClipCoverageLayer>;

  EntityPass();

  ~EntityPass();

  void SetDelegate(std::shared_ptr<EntityPassDelegate> delgate);

  /// @brief  Set the bounds limit, which is provided by the user when creating
  ///         a SaveLayer. This is a hint that allows the user to communicate
  ///         that it's OK to not render content outside of the bounds.
  ///
  ///         For consistency with Skia, we effectively treat this like a
  ///         rectangle clip by forcing the subpass texture size to never exceed
  ///         it.
  void SetBoundsLimit(std::optional<Rect> bounds_limit);

  /// @brief  Get the bounds limit, which is provided by the user when creating
  ///         a SaveLayer.
  std::optional<Rect> GetBoundsLimit() const;

  size_t GetSubpassesDepth() const;

  /// @brief Add an entity to the current entity pass.
  void AddEntity(Entity entity);

  void PushClip(Entity entity);

  void PopClips(size_t num_clips, uint64_t depth);

  void PopAllClips(uint64_t depth);

  void SetElements(std::vector<Element> elements);

  //----------------------------------------------------------------------------
  /// @brief  Appends a given pass as a subpass.
  ///
  EntityPass* AddSubpass(std::unique_ptr<EntityPass> pass);

  //----------------------------------------------------------------------------
  /// @brief  Merges a given pass into this pass. Useful for drawing
  ///         pre-recorded pictures that don't require rendering into a separate
  ///         subpass.
  ///
  void AddSubpassInline(std::unique_ptr<EntityPass> pass);

  EntityPass* GetSuperpass() const;

  bool Render(ContentContext& renderer,
              const RenderTarget& render_target) const;

  /// @brief  Iterate all elements (entities and subpasses) in this pass,
  ///         recursively including elements of child passes. The iteration
  ///         order is depth-first. Whenever a subpass elements is encountered,
  ///         it's included in the stream before its children.
  void IterateAllElements(const std::function<bool(Element&)>& iterator);

  void IterateAllElements(
      const std::function<bool(const Element&)>& iterator) const;

  //----------------------------------------------------------------------------
  /// @brief  Iterate all entities in this pass, recursively including entities
  ///         of child passes. The iteration order is depth-first.
  ///
  void IterateAllEntities(const std::function<bool(Entity&)>& iterator);

  //----------------------------------------------------------------------------
  /// @brief  Iterate all entities in this pass, recursively including entities
  ///         of child passes. The iteration order is depth-first and does not
  ///         allow modification of the entities.
  ///
  void IterateAllEntities(
      const std::function<bool(const Entity&)>& iterator) const;

  //----------------------------------------------------------------------------
  /// @brief  Iterate entities in this pass up until the first subpass is found.
  ///         This is useful for limiting look-ahead optimizations.
  ///
  /// @return Returns whether a subpass was encountered.
  ///
  bool IterateUntilSubpass(const std::function<bool(Entity&)>& iterator);

  //----------------------------------------------------------------------------
  /// @brief Return the number of elements on this pass.
  ///
  size_t GetElementCount() const;

  void SetTransform(Matrix transform);

  void SetClipDepth(size_t clip_depth);

  size_t GetClipDepth() const;

  void SetNewClipDepth(size_t clip_depth);

  uint32_t GetNewClipDepth() const;

  void SetBlendMode(BlendMode blend_mode);

  /// @brief Return the premultiplied clear color of the pass entities, if any.
  std::optional<Color> GetClearColor(ISize size = ISize::Infinite()) const;

  /// @brief Return the premultiplied clear color of the pass entities.
  ///
  /// If the entity pass has no clear color, this will return transparent black.
  Color GetClearColorOrDefault(ISize size = ISize::Infinite()) const;

  void SetBackdropFilter(BackdropFilterProc proc);

  void SetEnableOffscreenCheckerboard(bool enabled);

  int32_t GetRequiredMipCount() const { return required_mip_count_; }

  void SetRequiredMipCount(int32_t mip_count) {
    required_mip_count_ = mip_count;
  }

  //----------------------------------------------------------------------------
  /// @brief  Computes the coverage of a given subpass. This is used to
  ///         determine the texture size of a given subpass before it's rendered
  ///         to and passed through the subpass ImageFilter, if any.
  ///
  /// @param[in]  subpass         The EntityPass for which to compute
  ///                             pre-filteredcoverage.
  /// @param[in]  coverage_limit  Confines coverage to a specified area. This
  ///                             hint is used to trim coverage to the root
  ///                             framebuffer area. `std::nullopt` means there
  ///                             is no limit.
  ///
  /// @return  The screen space pixel area that the subpass contents will render
  ///          into, prior to being transformed by the subpass ImageFilter, if
  ///          any. `std::nullopt` means rendering the subpass will have no
  ///          effect on the color attachment.
  ///
  std::optional<Rect> GetSubpassCoverage(
      const EntityPass& subpass,
      std::optional<Rect> coverage_limit) const;

  std::optional<Rect> GetElementsCoverage(
      std::optional<Rect> coverage_limit) const;

 private:
  struct EntityResult {
    enum Status {
      /// The entity was successfully resolved and can be rendered.
      kSuccess,
      /// An unexpected rendering error occurred while resolving the Entity.
      kFailure,
      /// The entity should be skipped because rendering it will contribute
      /// nothing to the frame.
      kSkip,
    };

    /// @brief  The resulting entity that should be rendered. If `std::nullopt`,
    ///         there is nothing to render.
    Entity entity;
    /// @brief  This is set to `false` if there was an unexpected rendering
    ///         error while resolving the Entity.
    Status status = kFailure;

    static EntityResult Success(Entity e) { return {std::move(e), kSuccess}; }
    static EntityResult Failure() { return {{}, kFailure}; }
    static EntityResult Skip() { return {{}, kSkip}; }
  };

  bool RenderElement(Entity& element_entity,
                     size_t clip_depth_floor,
                     InlinePassContext& pass_context,
                     int32_t pass_depth,
                     ContentContext& renderer,
                     ClipCoverageStack& clip_coverage_stack,
                     Point global_pass_position) const;

  EntityResult GetEntityForElement(const EntityPass::Element& element,
                                   ContentContext& renderer,
                                   Capture& capture,
                                   InlinePassContext& pass_context,
                                   ISize root_pass_size,
                                   Point global_pass_position,
                                   uint32_t pass_depth,
                                   ClipCoverageStack& clip_coverage_stack,
                                   size_t clip_depth_floor) const;

  //----------------------------------------------------------------------------
  /// @brief     OnRender is the internal command recording routine for
  ///            `EntityPass`. Its job is to walk through each `Element` which
  ///            was appended to the scene (either an `Entity` via `AddEntity()`
  ///            or a child `EntityPass` via `AddSubpass()`) and render them to
  ///            the given `pass_target`.
  /// @param[in]  renderer                 The Contents context, which manages
  ///                                      pipeline state.
  /// @param[in]  root_pass_size           The size of the texture being
  ///                                      rendered into at the root of the
  ///                                      `EntityPass` tree. This is the size
  ///                                      of the `RenderTarget` color
  ///                                      attachment passed to the public
  ///                                      `EntityPass::Render` method.
  /// @param[out] pass_target              Stores the render target that should
  ///                                      be used for rendering.
  /// @param[in]  global_pass_position     The position that this `EntityPass`
  ///                                      will be drawn to the parent pass
  ///                                      relative to the root pass origin.
  ///                                      Used for offsetting drawn `Element`s,
  ///                                      whose origins are all in root
  ///                                      pass/screen space,
  /// @param[in]  local_pass_position      The position that this `EntityPass`
  ///                                      will be drawn to the parent pass
  ///                                      relative to the parent pass origin.
  ///                                      Used for positioning backdrop
  ///                                      filters.
  /// @param[in]  pass_depth               The tree depth of the `EntityPass` at
  ///                                      render time. Only used for labeling
  ///                                      and debugging purposes. This can vary
  ///                                      depending on whether passes are
  ///                                      collapsed or not.
  /// @param[in]  clip_coverage_stack      A global stack of coverage rectangles
  ///                                      for the clip buffer at each depth.
  ///                                      Higher depths are more restrictive.
  ///                                      Used to cull Elements that we
  ///                                      know won't result in a visible
  ///                                      change.
  /// @param[in]  clip_depth_floor         The clip depth that a value of
  ///                                      zero corresponds to in the given
  ///                                      `pass_target` clip buffer.
  ///                                      When new `pass_target`s are created
  ///                                      for subpasses, their clip buffers are
  ///                                      initialized at zero, and so this
  ///                                      value is used to offset Entity clip
  ///                                      depths to match the clip buffer.
  /// @param[in]  backdrop_filter_contents Optional. Is supplied, this contents
  ///                                      is rendered prior to anything else in
  ///                                      the `EntityPass`, offset by the
  ///                                      `local_pass_position`.
  /// @param[in]  collapsed_parent_pass    Optional. If supplied, this
  ///                                      `InlinePassContext` state is used to
  ///                                      begin rendering elements instead of
  ///                                      creating a new `RenderPass`. This
  ///                                      "collapses" the Elements into the
  ///                                      parent pass.
  ///
  bool OnRender(ContentContext& renderer,
                Capture& capture,
                ISize root_pass_size,
                EntityPassTarget& pass_target,
                Point global_pass_position,
                Point local_pass_position,
                uint32_t pass_depth,
                ClipCoverageStack& clip_coverage_stack,
                size_t clip_depth_floor = 0,
                std::shared_ptr<Contents> backdrop_filter_contents = nullptr,
                const std::optional<InlinePassContext::RenderPassResult>&
                    collapsed_parent_pass = std::nullopt) const;

  /// The list of renderable items in the scene. Each of these items is
  /// evaluated and recorded to an `EntityPassTarget` by the `OnRender` method.
  std::vector<Element> elements_;

  /// The stack of currently active clips (during Aiks recording time). Each
  /// entry is an index into the `elements_` list. The depth value of a clip is
  /// the max of all the entities it affects, so assignment of the depth value
  /// is deferred until clip restore or end of the EntityPass.
  std::vector<size_t> active_clips_;

  EntityPass* superpass_ = nullptr;
  Matrix transform_;
  size_t clip_depth_ = 0u;
  uint32_t new_clip_depth_ = 1u;
  BlendMode blend_mode_ = BlendMode::kSourceOver;
  bool flood_clip_ = false;
  bool enable_offscreen_debug_checkerboard_ = false;
  std::optional<Rect> bounds_limit_;
  std::unique_ptr<EntityPassClipRecorder> clip_replay_ =
      std::make_unique<EntityPassClipRecorder>();
  int32_t required_mip_count_ = 1;

  /// These values are incremented whenever something is added to the pass that
  /// requires reading from the backdrop texture. Currently, this can happen in
  /// the following scenarios:
  ///   1. An entity with an "advanced blend" is added to the pass.
  ///   2. A subpass with a backdrop filter is added to the pass.
  /// These are tracked as separate values because we may ignore
  /// blend_reads_from_pass_texture_ if the device supports framebuffer based
  /// advanced blends.
  uint32_t advanced_blend_reads_from_pass_texture_ = 0;
  uint32_t backdrop_filter_reads_from_pass_texture_ = 0;

  uint32_t GetTotalPassReads(ContentContext& renderer) const;

  BackdropFilterProc backdrop_filter_proc_ = nullptr;

  std::shared_ptr<EntityPassDelegate> delegate_ =
      EntityPassDelegate::MakeDefault();

  EntityPass(const EntityPass&) = delete;

  EntityPass& operator=(const EntityPass&) = delete;
};

/// @brief A class that tracks all clips that have been recorded in the current
///        entity pass stencil.
///
///        These clips are replayed when restoring the backdrop so that the
///        stencil buffer is left in an identical state.
class EntityPassClipRecorder {
 public:
  EntityPassClipRecorder();

  ~EntityPassClipRecorder() = default;

  /// @brief Record the entity based on the provided coverage [type].
  void RecordEntity(const Entity& entity, Contents::ClipCoverage::Type type);

  const std::vector<Entity>& GetReplayEntities() const;

 private:
  std::vector<Entity> rendered_clip_entities_;
};

}  // namespace impeller

#endif  // FLUTTER_IMPELLER_ENTITY_ENTITY_PASS_H_
