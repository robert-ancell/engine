// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/impeller/aiks/aiks_unittests.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include "flutter/testing/testing.h"
#include "impeller/aiks/canvas.h"
#include "impeller/aiks/color_filter.h"
#include "impeller/aiks/image.h"
#include "impeller/aiks/image_filter.h"
#include "impeller/aiks/paint_pass_delegate.h"
#include "impeller/aiks/testing/context_spy.h"
#include "impeller/core/capture.h"
#include "impeller/entity/contents/conical_gradient_contents.h"
#include "impeller/entity/contents/filters/gaussian_blur_filter_contents.h"
#include "impeller/entity/contents/filters/inputs/filter_input.h"
#include "impeller/entity/contents/linear_gradient_contents.h"
#include "impeller/entity/contents/radial_gradient_contents.h"
#include "impeller/entity/contents/solid_color_contents.h"
#include "impeller/entity/contents/sweep_gradient_contents.h"
#include "impeller/entity/render_target_cache.h"
#include "impeller/geometry/color.h"
#include "impeller/geometry/constants.h"
#include "impeller/geometry/geometry_asserts.h"
#include "impeller/geometry/matrix.h"
#include "impeller/geometry/path.h"
#include "impeller/geometry/path_builder.h"
#include "impeller/playground/widgets.h"
#include "impeller/renderer/command_buffer.h"
#include "impeller/renderer/snapshot.h"
#include "impeller/renderer/testing/mocks.h"
#include "impeller/scene/material.h"
#include "impeller/scene/node.h"
#include "impeller/typographer/backends/skia/text_frame_skia.h"
#include "impeller/typographer/backends/skia/typographer_context_skia.h"
#include "impeller/typographer/backends/stb/text_frame_stb.h"
#include "impeller/typographer/backends/stb/typeface_stb.h"
#include "impeller/typographer/backends/stb/typographer_context_stb.h"
#include "third_party/imgui/imgui.h"
#include "third_party/skia/include/core/SkFontMgr.h"
#include "third_party/skia/include/core/SkTypeface.h"
#include "txt/platform.h"

namespace impeller {
namespace testing {

INSTANTIATE_PLAYGROUND_SUITE(AiksTest);

TEST_P(AiksTest, CanvasCTMCanBeUpdated) {
  Canvas canvas;
  Matrix identity;
  ASSERT_MATRIX_NEAR(canvas.GetCurrentTransform(), identity);
  canvas.Translate(Size{100, 100});
  ASSERT_MATRIX_NEAR(canvas.GetCurrentTransform(),
                     Matrix::MakeTranslation({100.0, 100.0, 0.0}));
}

TEST_P(AiksTest, CanvasCanPushPopCTM) {
  Canvas canvas;
  ASSERT_EQ(canvas.GetSaveCount(), 1u);
  ASSERT_EQ(canvas.Restore(), false);

  canvas.Translate(Size{100, 100});
  canvas.Save();
  ASSERT_EQ(canvas.GetSaveCount(), 2u);
  ASSERT_MATRIX_NEAR(canvas.GetCurrentTransform(),
                     Matrix::MakeTranslation({100.0, 100.0, 0.0}));
  ASSERT_TRUE(canvas.Restore());
  ASSERT_EQ(canvas.GetSaveCount(), 1u);
  ASSERT_MATRIX_NEAR(canvas.GetCurrentTransform(),
                     Matrix::MakeTranslation({100.0, 100.0, 0.0}));
}

TEST_P(AiksTest, CanRenderColoredRect) {
  Canvas canvas;
  Paint paint;
  paint.color = Color::Blue();
  canvas.DrawPath(PathBuilder{}
                      .AddRect(Rect::MakeXYWH(100.0, 100.0, 100.0, 100.0))
                      .TakePath(),
                  paint);
  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CanRenderImage) {
  Canvas canvas;
  Paint paint;
  auto image = std::make_shared<Image>(CreateTextureForFixture("kalimba.jpg"));
  paint.color = Color::Red();
  canvas.DrawImage(image, Point::MakeXY(100.0, 100.0), paint);
  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CanRenderInvertedImageWithColorFilter) {
  Canvas canvas;
  Paint paint;
  auto image = std::make_shared<Image>(CreateTextureForFixture("kalimba.jpg"));
  paint.color = Color::Red();
  paint.color_filter =
      ColorFilter::MakeBlend(BlendMode::kSourceOver, Color::Yellow());
  paint.invert_colors = true;

  canvas.DrawImage(image, Point::MakeXY(100.0, 100.0), paint);
  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CanRenderColorFilterWithInvertColors) {
  Canvas canvas;
  Paint paint;
  paint.color = Color::Red();
  paint.color_filter =
      ColorFilter::MakeBlend(BlendMode::kSourceOver, Color::Yellow());
  paint.invert_colors = true;

  canvas.DrawRect(Rect::MakeLTRB(0, 0, 100, 100), paint);
  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CanRenderColorFilterWithInvertColorsDrawPaint) {
  Canvas canvas;
  Paint paint;
  paint.color = Color::Red();
  paint.color_filter =
      ColorFilter::MakeBlend(BlendMode::kSourceOver, Color::Yellow());
  paint.invert_colors = true;

  canvas.DrawPaint(paint);
  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CanRenderAdvancedBlendColorFilterWithSaveLayer) {
  Canvas canvas;

  Rect layer_rect = Rect::MakeXYWH(0, 0, 500, 500);
  canvas.ClipRect(layer_rect);

  canvas.SaveLayer(
      {
          .color_filter = ColorFilter::MakeBlend(BlendMode::kDifference,
                                                 Color(0, 1, 0, 0.5)),
      },
      layer_rect);

  Paint paint;
  canvas.DrawPaint({.color = Color::Black()});
  canvas.DrawRect(Rect::MakeXYWH(100, 100, 300, 300),
                  {.color = Color::White()});
  canvas.Restore();

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

namespace {
bool GenerateMipmap(const std::shared_ptr<Context>& context,
                    std::shared_ptr<Texture> texture,
                    std::string label) {
  auto buffer = context->CreateCommandBuffer();
  if (!buffer) {
    return false;
  }
  auto pass = buffer->CreateBlitPass();
  if (!pass) {
    return false;
  }
  pass->GenerateMipmap(std::move(texture), std::move(label));

  pass->EncodeCommands(context->GetResourceAllocator());
  return context->GetCommandQueue()->Submit({buffer}).ok();
}

void CanRenderTiledTexture(AiksTest* aiks_test,
                           Entity::TileMode tile_mode,
                           Matrix local_matrix = {}) {
  auto context = aiks_test->GetContext();
  ASSERT_TRUE(context);
  auto texture = aiks_test->CreateTextureForFixture("table_mountain_nx.png",
                                                    /*enable_mipmapping=*/true);
  GenerateMipmap(context, texture, "table_mountain_nx");
  Canvas canvas;
  canvas.Scale(aiks_test->GetContentScale());
  canvas.Translate({100.0f, 100.0f, 0});
  Paint paint;
  paint.color_source =
      ColorSource::MakeImage(texture, tile_mode, tile_mode, {}, local_matrix);
  paint.color = Color(1, 1, 1, 1);
  canvas.DrawRect(Rect::MakeXYWH(0, 0, 600, 600), paint);

  // Should not change the image.
  constexpr auto stroke_width = 64;
  paint.style = Paint::Style::kStroke;
  paint.stroke_width = stroke_width;
  if (tile_mode == Entity::TileMode::kDecal) {
    canvas.DrawRect(Rect::MakeXYWH(stroke_width, stroke_width, 600, 600),
                    paint);
  } else {
    canvas.DrawRect(Rect::MakeXYWH(0, 0, 600, 600), paint);
  }

  {
    // Should not change the image.
    PathBuilder path_builder;
    path_builder.AddCircle({150, 150}, 150);
    path_builder.AddRoundedRect(Rect::MakeLTRB(300, 300, 600, 600), 10);
    paint.style = Paint::Style::kFill;
    canvas.DrawPath(path_builder.TakePath(), paint);
  }

  {
    // Should not change the image. Tests the Convex short-cut code.
    PathBuilder path_builder;
    path_builder.AddCircle({150, 450}, 150);
    path_builder.SetConvexity(Convexity::kConvex);
    paint.style = Paint::Style::kFill;
    canvas.DrawPath(path_builder.TakePath(), paint);
  }

  ASSERT_TRUE(aiks_test->OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}
}  // namespace

TEST_P(AiksTest, CanRenderTiledTextureClamp) {
  CanRenderTiledTexture(this, Entity::TileMode::kClamp);
}

TEST_P(AiksTest, CanRenderTiledTextureRepeat) {
  CanRenderTiledTexture(this, Entity::TileMode::kRepeat);
}

TEST_P(AiksTest, CanRenderTiledTextureMirror) {
  CanRenderTiledTexture(this, Entity::TileMode::kMirror);
}

TEST_P(AiksTest, CanRenderTiledTextureDecal) {
  CanRenderTiledTexture(this, Entity::TileMode::kDecal);
}

TEST_P(AiksTest, CanRenderTiledTextureClampWithTranslate) {
  CanRenderTiledTexture(this, Entity::TileMode::kClamp,
                        Matrix::MakeTranslation({172.f, 172.f, 0.f}));
}

TEST_P(AiksTest, CanRenderImageRect) {
  Canvas canvas;
  Paint paint;
  auto image = std::make_shared<Image>(CreateTextureForFixture("kalimba.jpg"));
  Size image_half_size = Size(image->GetSize()) * 0.5;

  // Render the bottom right quarter of the source image in a stretched rect.
  auto source_rect = Rect::MakeSize(image_half_size);
  source_rect = source_rect.Shift(Point(image_half_size));

  canvas.DrawImageRect(image, source_rect, Rect::MakeXYWH(100, 100, 600, 600),
                       paint);
  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CanRenderSimpleClips) {
  Canvas canvas;
  canvas.Scale(GetContentScale());
  Paint paint;

  paint.color = Color::White();
  canvas.DrawPaint(paint);

  auto draw = [&canvas](const Paint& paint, Scalar x, Scalar y) {
    canvas.Save();
    canvas.Translate({x, y});
    {
      canvas.Save();
      canvas.ClipRect(Rect::MakeLTRB(50, 50, 150, 150));
      canvas.DrawPaint(paint);
      canvas.Restore();
    }
    {
      canvas.Save();
      canvas.ClipOval(Rect::MakeLTRB(200, 50, 300, 150));
      canvas.DrawPaint(paint);
      canvas.Restore();
    }
    {
      canvas.Save();
      canvas.ClipRRect(Rect::MakeLTRB(50, 200, 150, 300), {20, 20});
      canvas.DrawPaint(paint);
      canvas.Restore();
    }
    {
      canvas.Save();
      canvas.ClipRRect(Rect::MakeLTRB(200, 230, 300, 270), {20, 20});
      canvas.DrawPaint(paint);
      canvas.Restore();
    }
    {
      canvas.Save();
      canvas.ClipRRect(Rect::MakeLTRB(230, 200, 270, 300), {20, 20});
      canvas.DrawPaint(paint);
      canvas.Restore();
    }
    canvas.Restore();
  };

  paint.color = Color::Blue();
  draw(paint, 0, 0);

  std::vector<Color> gradient_colors = {
      Color{0x1f / 255.0, 0.0, 0x5c / 255.0, 1.0},
      Color{0x5b / 255.0, 0.0, 0x60 / 255.0, 1.0},
      Color{0x87 / 255.0, 0x01 / 255.0, 0x60 / 255.0, 1.0},
      Color{0xac / 255.0, 0x25 / 255.0, 0x53 / 255.0, 1.0},
      Color{0xe1 / 255.0, 0x6b / 255.0, 0x5c / 255.0, 1.0},
      Color{0xf3 / 255.0, 0x90 / 255.0, 0x60 / 255.0, 1.0},
      Color{0xff / 255.0, 0xb5 / 255.0, 0x6b / 250.0, 1.0}};
  std::vector<Scalar> stops = {
      0.0,
      (1.0 / 6.0) * 1,
      (1.0 / 6.0) * 2,
      (1.0 / 6.0) * 3,
      (1.0 / 6.0) * 4,
      (1.0 / 6.0) * 5,
      1.0,
  };
  auto texture = CreateTextureForFixture("airplane.jpg",
                                         /*enable_mipmapping=*/true);

  paint.color_source = ColorSource::MakeRadialGradient(
      {500, 600}, 75, std::move(gradient_colors), std::move(stops),
      Entity::TileMode::kMirror, {});
  draw(paint, 0, 300);

  paint.color_source = ColorSource::MakeImage(
      texture, Entity::TileMode::kRepeat, Entity::TileMode::kRepeat, {},
      Matrix::MakeTranslation({0, 0}));
  draw(paint, 300, 0);

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CanRenderNestedClips) {
  Canvas canvas;
  Paint paint;
  paint.color = Color::Fuchsia();
  canvas.Save();
  canvas.ClipPath(PathBuilder{}.AddCircle({200, 400}, 300).TakePath());
  canvas.Restore();
  canvas.ClipPath(PathBuilder{}.AddCircle({600, 400}, 300).TakePath());
  canvas.ClipPath(PathBuilder{}.AddCircle({400, 600}, 300).TakePath());
  canvas.DrawRect(Rect::MakeXYWH(200, 200, 400, 400), paint);
  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CanRenderDifferenceClips) {
  Paint paint;
  Canvas canvas;
  canvas.Translate({400, 400});

  // Limit drawing to face circle with a clip.
  canvas.ClipPath(PathBuilder{}.AddCircle(Point(), 200).TakePath());
  canvas.Save();

  // Cut away eyes/mouth using difference clips.
  canvas.ClipPath(PathBuilder{}.AddCircle({-100, -50}, 30).TakePath(),
                  Entity::ClipOperation::kDifference);
  canvas.ClipPath(PathBuilder{}.AddCircle({100, -50}, 30).TakePath(),
                  Entity::ClipOperation::kDifference);
  canvas.ClipPath(PathBuilder{}
                      .AddQuadraticCurve({-100, 50}, {0, 150}, {100, 50})
                      .TakePath(),
                  Entity::ClipOperation::kDifference);

  // Draw a huge yellow rectangle to prove the clipping works.
  paint.color = Color::Yellow();
  canvas.DrawRect(Rect::MakeXYWH(-1000, -1000, 2000, 2000), paint);

  // Remove the difference clips and draw hair that partially covers the eyes.
  canvas.Restore();
  paint.color = Color::Maroon();
  canvas.DrawPath(PathBuilder{}
                      .MoveTo({200, -200})
                      .HorizontalLineTo(-200)
                      .VerticalLineTo(-40)
                      .CubicCurveTo({0, -40}, {0, -80}, {200, -80})
                      .TakePath(),
                  paint);

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CanRenderWithContiguousClipRestores) {
  Canvas canvas;

  // Cover the whole canvas with red.
  canvas.DrawPaint({.color = Color::Red()});

  canvas.Save();

  // Append two clips, the second resulting in empty coverage.
  canvas.ClipPath(
      PathBuilder{}.AddRect(Rect::MakeXYWH(100, 100, 100, 100)).TakePath());
  canvas.ClipPath(
      PathBuilder{}.AddRect(Rect::MakeXYWH(300, 300, 100, 100)).TakePath());

  // Restore to no clips.
  canvas.Restore();

  // Replace the whole canvas with green.
  canvas.DrawPaint({.color = Color::Green()});

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, ClipsUseCurrentTransform) {
  std::array<Color, 5> colors = {Color::White(), Color::Black(),
                                 Color::SkyBlue(), Color::Red(),
                                 Color::Yellow()};
  Canvas canvas;
  Paint paint;

  canvas.Translate(Vector3(300, 300));
  for (int i = 0; i < 15; i++) {
    canvas.Scale(Vector3(0.8, 0.8));

    paint.color = colors[i % colors.size()];
    canvas.ClipPath(PathBuilder{}.AddCircle({0, 0}, 300).TakePath());
    canvas.DrawRect(Rect::MakeXYWH(-300, -300, 600, 600), paint);
  }
  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CanSaveLayerStandalone) {
  Canvas canvas;

  Paint red;
  red.color = Color::Red();

  Paint alpha;
  alpha.color = Color::Red().WithAlpha(0.5);

  canvas.SaveLayer(alpha);

  canvas.DrawCircle({125, 125}, 125, red);

  canvas.Restore();

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CanRenderDifferentShapesWithSameColorSource) {
  Canvas canvas;
  Paint paint;

  std::vector<Color> colors = {Color{0.9568, 0.2627, 0.2118, 1.0},
                               Color{0.1294, 0.5882, 0.9529, 1.0}};
  std::vector<Scalar> stops = {
      0.0,
      1.0,
  };

  paint.color_source = ColorSource::MakeLinearGradient(
      {0, 0}, {100, 100}, std::move(colors), std::move(stops),
      Entity::TileMode::kRepeat, {});

  canvas.Save();
  canvas.Translate({100, 100, 0});
  canvas.DrawRect(Rect::MakeXYWH(0, 0, 200, 200), paint);
  canvas.Restore();

  canvas.Save();
  canvas.Translate({100, 400, 0});
  canvas.DrawCircle({100, 100}, 100, paint);
  canvas.Restore();
  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CanPictureConvertToImage) {
  Canvas recorder_canvas;
  Paint paint;
  paint.color = Color{0.9568, 0.2627, 0.2118, 1.0};
  recorder_canvas.DrawRect(Rect::MakeXYWH(100.0, 100.0, 600, 600), paint);
  paint.color = Color{0.1294, 0.5882, 0.9529, 1.0};
  recorder_canvas.DrawRect(Rect::MakeXYWH(200.0, 200.0, 600, 600), paint);

  Canvas canvas;
  AiksContext renderer(GetContext(), nullptr);
  paint.color = Color::BlackTransparent();
  canvas.DrawPaint(paint);
  Picture picture = recorder_canvas.EndRecordingAsPicture();
  auto image = picture.ToImage(renderer, ISize{1000, 1000});
  if (image) {
    canvas.DrawImage(image, Point(), Paint());
    paint.color = Color{0.1, 0.1, 0.1, 0.2};
    canvas.DrawRect(Rect::MakeSize(ISize{1000, 1000}), paint);
  }

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, BlendModeShouldCoverWholeScreen) {
  Canvas canvas;
  Paint paint;

  paint.color = Color::Red();
  canvas.DrawPaint(paint);

  paint.blend_mode = BlendMode::kSourceOver;
  canvas.SaveLayer(paint);

  paint.color = Color::White();
  canvas.DrawRect(Rect::MakeXYWH(100, 100, 400, 400), paint);

  paint.blend_mode = BlendMode::kSource;
  canvas.SaveLayer(paint);

  paint.color = Color::Blue();
  canvas.DrawRect(Rect::MakeXYWH(200, 200, 200, 200), paint);

  canvas.Restore();
  canvas.Restore();

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CanRenderGroupOpacity) {
  Canvas canvas;

  Paint red;
  red.color = Color::Red();
  Paint green;
  green.color = Color::Green().WithAlpha(0.5);
  Paint blue;
  blue.color = Color::Blue();

  Paint alpha;
  alpha.color = Color::Red().WithAlpha(0.5);

  canvas.SaveLayer(alpha);

  canvas.DrawRect(Rect::MakeXYWH(000, 000, 100, 100), red);
  canvas.DrawRect(Rect::MakeXYWH(020, 020, 100, 100), green);
  canvas.DrawRect(Rect::MakeXYWH(040, 040, 100, 100), blue);

  canvas.Restore();

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CoordinateConversionsAreCorrect) {
  Canvas canvas;

  // Render a texture directly.
  {
    Paint paint;
    auto image =
        std::make_shared<Image>(CreateTextureForFixture("kalimba.jpg"));
    paint.color = Color::Red();

    canvas.Save();
    canvas.Translate({100, 200, 0});
    canvas.Scale(Vector2{0.5, 0.5});
    canvas.DrawImage(image, Point::MakeXY(100.0, 100.0), paint);
    canvas.Restore();
  }

  // Render an offscreen rendered texture.
  {
    Paint red;
    red.color = Color::Red();
    Paint green;
    green.color = Color::Green();
    Paint blue;
    blue.color = Color::Blue();

    Paint alpha;
    alpha.color = Color::Red().WithAlpha(0.5);

    canvas.SaveLayer(alpha);

    canvas.DrawRect(Rect::MakeXYWH(000, 000, 100, 100), red);
    canvas.DrawRect(Rect::MakeXYWH(020, 020, 100, 100), green);
    canvas.DrawRect(Rect::MakeXYWH(040, 040, 100, 100), blue);

    canvas.Restore();
  }

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CanPerformFullScreenMSAA) {
  Canvas canvas;

  Paint red;
  red.color = Color::Red();

  canvas.DrawCircle({250, 250}, 125, red);

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CanPerformSkew) {
  Canvas canvas;

  Paint red;
  red.color = Color::Red();

  canvas.Skew(2, 5);
  canvas.DrawRect(Rect::MakeXYWH(0, 0, 100, 100), red);

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CanPerformSaveLayerWithBounds) {
  Canvas canvas;

  Paint red;
  red.color = Color::Red();

  Paint green;
  green.color = Color::Green();

  Paint blue;
  blue.color = Color::Blue();

  Paint save;
  save.color = Color::Black();

  canvas.SaveLayer(save, Rect::MakeXYWH(0, 0, 50, 50));

  canvas.DrawRect(Rect::MakeXYWH(0, 0, 100, 100), red);
  canvas.DrawRect(Rect::MakeXYWH(10, 10, 100, 100), green);
  canvas.DrawRect(Rect::MakeXYWH(20, 20, 100, 100), blue);

  canvas.Restore();

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest,
       CanPerformSaveLayerWithBoundsAndLargerIntermediateIsNotAllocated) {
  Canvas canvas;

  Paint red;
  red.color = Color::Red();

  Paint green;
  green.color = Color::Green();

  Paint blue;
  blue.color = Color::Blue();

  Paint save;
  save.color = Color::Black().WithAlpha(0.5);

  canvas.SaveLayer(save, Rect::MakeXYWH(0, 0, 100000, 100000));

  canvas.DrawRect(Rect::MakeXYWH(0, 0, 100, 100), red);
  canvas.DrawRect(Rect::MakeXYWH(10, 10, 100, 100), green);
  canvas.DrawRect(Rect::MakeXYWH(20, 20, 100, 100), blue);

  canvas.Restore();

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CanRenderRoundedRectWithNonUniformRadii) {
  Canvas canvas;

  Paint paint;
  paint.color = Color::Red();

  PathBuilder::RoundingRadii radii;
  radii.top_left = {50, 25};
  radii.top_right = {25, 50};
  radii.bottom_right = {50, 25};
  radii.bottom_left = {25, 50};

  auto path = PathBuilder{}
                  .AddRoundedRect(Rect::MakeXYWH(100, 100, 500, 500), radii)
                  .TakePath();

  canvas.DrawPath(path, paint);

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

struct TextRenderOptions {
  Scalar font_size = 50;
  Color color = Color::Yellow();
  Point position = Vector2(100, 200);
  std::optional<Paint::MaskBlurDescriptor> mask_blur_descriptor;
};

bool RenderTextInCanvasSkia(const std::shared_ptr<Context>& context,
                            Canvas& canvas,
                            const std::string& text,
                            const std::string_view& font_fixture,
                            TextRenderOptions options = {}) {
  // Draw the baseline.
  canvas.DrawRect(
      Rect::MakeXYWH(options.position.x - 50, options.position.y, 900, 10),
      Paint{.color = Color::Aqua().WithAlpha(0.25)});

  // Mark the point at which the text is drawn.
  canvas.DrawCircle(options.position, 5.0,
                    Paint{.color = Color::Red().WithAlpha(0.25)});

  // Construct the text blob.
  auto c_font_fixture = std::string(font_fixture);
  auto mapping = flutter::testing::OpenFixtureAsSkData(c_font_fixture.c_str());
  if (!mapping) {
    return false;
  }
  sk_sp<SkFontMgr> font_mgr = txt::GetDefaultFontManager();
  SkFont sk_font(font_mgr->makeFromData(mapping), options.font_size);
  auto blob = SkTextBlob::MakeFromString(text.c_str(), sk_font);
  if (!blob) {
    return false;
  }

  // Create the Impeller text frame and draw it at the designated baseline.
  auto frame = MakeTextFrameFromTextBlobSkia(blob);

  Paint text_paint;
  text_paint.color = options.color;
  text_paint.mask_blur_descriptor = options.mask_blur_descriptor;
  canvas.DrawTextFrame(frame, options.position, text_paint);
  return true;
}

bool RenderTextInCanvasSTB(const std::shared_ptr<Context>& context,
                           Canvas& canvas,
                           const std::string& text,
                           const std::string& font_fixture,
                           TextRenderOptions options = {}) {
  // Draw the baseline.
  canvas.DrawRect(
      Rect::MakeXYWH(options.position.x - 50, options.position.y, 900, 10),
      Paint{.color = Color::Aqua().WithAlpha(0.25)});

  // Mark the point at which the text is drawn.
  canvas.DrawCircle(options.position, 5.0,
                    Paint{.color = Color::Red().WithAlpha(0.25)});

  // Construct the text blob.
  auto mapping = flutter::testing::OpenFixtureAsMapping(font_fixture.c_str());
  if (!mapping) {
    return false;
  }
  auto typeface_stb = std::make_shared<TypefaceSTB>(std::move(mapping));

  auto frame = MakeTextFrameSTB(
      typeface_stb, Font::Metrics{.point_size = options.font_size}, text);

  Paint text_paint;
  text_paint.color = options.color;
  canvas.DrawTextFrame(frame, options.position, text_paint);
  return true;
}

TEST_P(AiksTest, CanRenderTextFrame) {
  Canvas canvas;
  canvas.DrawPaint({.color = Color(0.1, 0.1, 0.1, 1.0)});
  ASSERT_TRUE(RenderTextInCanvasSkia(
      GetContext(), canvas, "the quick brown fox jumped over the lazy dog!.?",
      "Roboto-Regular.ttf"));
  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CanRenderTextFrameSTB) {
  Canvas canvas;
  canvas.DrawPaint({.color = Color(0.1, 0.1, 0.1, 1.0)});
  ASSERT_TRUE(RenderTextInCanvasSTB(
      GetContext(), canvas, "the quick brown fox jumped over the lazy dog!.?",
      "Roboto-Regular.ttf"));

  SetTypographerContext(TypographerContextSTB::Make());
  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, TextFrameSubpixelAlignment) {
  std::array<Scalar, 20> phase_offsets;
  for (Scalar& offset : phase_offsets) {
    auto rand = std::rand();  // NOLINT
    offset = (static_cast<float>(rand) / static_cast<float>(RAND_MAX)) * k2Pi;
  }

  auto callback = [&](AiksContext& renderer) -> std::optional<Picture> {
    static float font_size = 20;
    static float phase_variation = 0.2;
    static float speed = 0.5;
    static float magnitude = 100;
    ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::SliderFloat("Font size", &font_size, 5, 50);
    ImGui::SliderFloat("Phase variation", &phase_variation, 0, 1);
    ImGui::SliderFloat("Oscillation speed", &speed, 0, 2);
    ImGui::SliderFloat("Oscillation magnitude", &magnitude, 0, 300);
    ImGui::End();

    Canvas canvas;
    canvas.Scale(GetContentScale());

    for (size_t i = 0; i < phase_offsets.size(); i++) {
      auto position = Point(
          200 + magnitude * std::sin((-phase_offsets[i] * phase_variation +
                                      GetSecondsElapsed() * speed)),  //
          200 + i * font_size * 1.1                                   //
      );
      if (!RenderTextInCanvasSkia(
              GetContext(), canvas,
              "the quick brown fox jumped over "
              "the lazy dog!.?",
              "Roboto-Regular.ttf",
              {.font_size = font_size, .position = position})) {
        return std::nullopt;
      }
    }
    return canvas.EndRecordingAsPicture();
  };

  ASSERT_TRUE(OpenPlaygroundHere(callback));
}

TEST_P(AiksTest, CanRenderItalicizedText) {
  Canvas canvas;
  canvas.DrawPaint({.color = Color(0.1, 0.1, 0.1, 1.0)});

  ASSERT_TRUE(RenderTextInCanvasSkia(
      GetContext(), canvas, "the quick brown fox jumped over the lazy dog!.?",
      "HomemadeApple.ttf"));
  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

static constexpr std::string_view kFontFixture =
#if FML_OS_MACOSX
    "Apple Color Emoji.ttc";
#else
    "NotoColorEmoji.ttf";
#endif

TEST_P(AiksTest, CanRenderEmojiTextFrame) {
  Canvas canvas;
  canvas.DrawPaint({.color = Color(0.1, 0.1, 0.1, 1.0)});

  ASSERT_TRUE(RenderTextInCanvasSkia(
      GetContext(), canvas, "😀 😃 😄 😁 😆 😅 😂 🤣 🥲 😊", kFontFixture));
  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CanRenderEmojiTextFrameWithBlur) {
  Canvas canvas;
  canvas.DrawPaint({.color = Color(0.1, 0.1, 0.1, 1.0)});

  ASSERT_TRUE(RenderTextInCanvasSkia(
      GetContext(), canvas, "😀 😃 😄 😁 😆 😅 😂 🤣 🥲 😊", kFontFixture,
      TextRenderOptions{.color = Color::Blue(),
                        .mask_blur_descriptor = Paint::MaskBlurDescriptor{
                            .style = FilterContents::BlurStyle::kNormal,
                            .sigma = Sigma(4)}}));
  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CanRenderEmojiTextFrameWithAlpha) {
  Canvas canvas;
  canvas.DrawPaint({.color = Color(0.1, 0.1, 0.1, 1.0)});

  ASSERT_TRUE(RenderTextInCanvasSkia(
      GetContext(), canvas, "😀 😃 😄 😁 😆 😅 😂 🤣 🥲 😊", kFontFixture,
      {.color = Color::Black().WithAlpha(0.5)}));
  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CanRenderTextInSaveLayer) {
  Canvas canvas;
  canvas.DrawPaint({.color = Color(0.1, 0.1, 0.1, 1.0)});

  canvas.Translate({100, 100});
  canvas.Scale(Vector2{0.5, 0.5});

  // Blend the layer with the parent pass using kClear to expose the coverage.
  canvas.SaveLayer({.blend_mode = BlendMode::kClear});
  ASSERT_TRUE(RenderTextInCanvasSkia(
      GetContext(), canvas, "the quick brown fox jumped over the lazy dog!.?",
      "Roboto-Regular.ttf"));
  canvas.Restore();

  // Render the text again over the cleared coverage rect.
  ASSERT_TRUE(RenderTextInCanvasSkia(
      GetContext(), canvas, "the quick brown fox jumped over the lazy dog!.?",
      "Roboto-Regular.ttf"));

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CanRenderTextOutsideBoundaries) {
  Canvas canvas;
  canvas.Translate({200, 150});

  // Construct the text blob.
  auto mapping = flutter::testing::OpenFixtureAsSkData("wtf.otf");
  ASSERT_NE(mapping, nullptr);

  Scalar font_size = 80;
  sk_sp<SkFontMgr> font_mgr = txt::GetDefaultFontManager();
  SkFont sk_font(font_mgr->makeFromData(mapping), font_size);

  Paint text_paint;
  text_paint.color = Color::Blue().WithAlpha(0.8);

  struct {
    Point position;
    const char* text;
  } text[] = {{Point(0, 0), "0F0F0F0"},
              {Point(1, 2), "789"},
              {Point(1, 3), "456"},
              {Point(1, 4), "123"},
              {Point(0, 6), "0F0F0F0"}};
  for (auto& t : text) {
    canvas.Save();
    canvas.Translate(t.position * Point(font_size * 2, font_size * 1.1));
    {
      auto blob = SkTextBlob::MakeFromString(t.text, sk_font);
      ASSERT_NE(blob, nullptr);
      auto frame = MakeTextFrameFromTextBlobSkia(blob);
      canvas.DrawTextFrame(frame, Point(), text_paint);
    }
    canvas.Restore();
  }

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, TextRotated) {
  Canvas canvas;
  canvas.Scale(GetContentScale());
  canvas.DrawPaint({.color = Color(0.1, 0.1, 0.1, 1.0)});

  canvas.Transform(Matrix(0.25, -0.3, 0, -0.002,  //
                          0, 0.5, 0, 0,           //
                          0, 0, 0.3, 0,           //
                          100, 100, 0, 1.3));
  ASSERT_TRUE(RenderTextInCanvasSkia(
      GetContext(), canvas, "the quick brown fox jumped over the lazy dog!.?",
      "Roboto-Regular.ttf"));

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CanDrawPaint) {
  Canvas canvas;
  canvas.Scale(Vector2(0.2, 0.2));
  canvas.DrawPaint({.color = Color::MediumTurquoise()});
  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CanDrawPaintMultipleTimes) {
  Canvas canvas;
  canvas.Scale(Vector2(0.2, 0.2));
  canvas.DrawPaint({.color = Color::MediumTurquoise()});
  canvas.DrawPaint({.color = Color::Color::OrangeRed().WithAlpha(0.5)});
  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CanDrawPaintWithAdvancedBlend) {
  Canvas canvas;
  canvas.Scale(Vector2(0.2, 0.2));
  canvas.DrawPaint({.color = Color::MediumTurquoise()});
  canvas.DrawPaint({.color = Color::Color::OrangeRed().WithAlpha(0.5),
                    .blend_mode = BlendMode::kHue});
  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, DrawPaintWithAdvancedBlendOverFilter) {
  Paint filtered = {
      .color = Color::Black(),
      .mask_blur_descriptor =
          Paint::MaskBlurDescriptor{
              .style = FilterContents::BlurStyle::kNormal,
              .sigma = Sigma(60),
          },
  };

  Canvas canvas;
  canvas.DrawPaint({.color = Color::White()});
  canvas.DrawCircle({300, 300}, 200, filtered);
  canvas.DrawPaint({.color = Color::Green(), .blend_mode = BlendMode::kScreen});
  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, DrawAdvancedBlendPartlyOffscreen) {
  std::vector<Color> colors = {Color{0.9568, 0.2627, 0.2118, 1.0},
                               Color{0.1294, 0.5882, 0.9529, 1.0}};
  std::vector<Scalar> stops = {0.0, 1.0};

  Paint paint = {
      .color_source = ColorSource::MakeLinearGradient(
          {0, 0}, {100, 100}, std::move(colors), std::move(stops),
          Entity::TileMode::kRepeat, Matrix::MakeScale(Vector3(0.3, 0.3, 0.3))),
      .blend_mode = BlendMode::kLighten,
  };

  Canvas canvas;
  canvas.DrawPaint({.color = Color::Blue()});
  canvas.Scale(Vector2(2, 2));
  canvas.ClipRect(Rect::MakeLTRB(0, 0, 200, 200));
  canvas.DrawCircle({100, 100}, 100, paint);
  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

#define BLEND_MODE_TUPLE(blend_mode) {#blend_mode, BlendMode::k##blend_mode},

struct BlendModeSelection {
  std::vector<const char*> blend_mode_names;
  std::vector<BlendMode> blend_mode_values;
};

static BlendModeSelection GetBlendModeSelection() {
  std::vector<const char*> blend_mode_names;
  std::vector<BlendMode> blend_mode_values;
  {
    const std::vector<std::tuple<const char*, BlendMode>> blends = {
        IMPELLER_FOR_EACH_BLEND_MODE(BLEND_MODE_TUPLE)};
    assert(blends.size() ==
           static_cast<size_t>(Entity::kLastAdvancedBlendMode) + 1);
    for (const auto& [name, mode] : blends) {
      blend_mode_names.push_back(name);
      blend_mode_values.push_back(mode);
    }
  }

  return {blend_mode_names, blend_mode_values};
}

TEST_P(AiksTest, CanDrawPaintMultipleTimesInteractive) {
  auto modes = GetBlendModeSelection();

  auto callback = [&](AiksContext& renderer) -> std::optional<Picture> {
    static Color background = Color::MediumTurquoise();
    static Color foreground = Color::Color::OrangeRed().WithAlpha(0.5);
    static int current_blend_index = 3;

    ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    {
      ImGui::ColorEdit4("Background", reinterpret_cast<float*>(&background));
      ImGui::ColorEdit4("Foreground", reinterpret_cast<float*>(&foreground));
      ImGui::ListBox("Blend mode", &current_blend_index,
                     modes.blend_mode_names.data(),
                     modes.blend_mode_names.size());
    }
    ImGui::End();

    Canvas canvas;
    canvas.Scale(Vector2(0.2, 0.2));
    canvas.DrawPaint({.color = background});
    canvas.DrawPaint(
        {.color = foreground,
         .blend_mode = static_cast<BlendMode>(current_blend_index)});
    return canvas.EndRecordingAsPicture();
  };
  ASSERT_TRUE(OpenPlaygroundHere(callback));
}

TEST_P(AiksTest, PaintBlendModeIsRespected) {
  Paint paint;
  Canvas canvas;
  // Default is kSourceOver.
  paint.color = Color(1, 0, 0, 0.5);
  canvas.DrawCircle(Point(150, 200), 100, paint);
  paint.color = Color(0, 1, 0, 0.5);
  canvas.DrawCircle(Point(250, 200), 100, paint);

  paint.blend_mode = BlendMode::kPlus;
  paint.color = Color::Red();
  canvas.DrawCircle(Point(450, 250), 100, paint);
  paint.color = Color::Green();
  canvas.DrawCircle(Point(550, 250), 100, paint);
  paint.color = Color::Blue();
  canvas.DrawCircle(Point(500, 150), 100, paint);
  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, ColorWheel) {
  // Compare with https://fiddle.skia.org/c/@BlendModes

  BlendModeSelection blend_modes = GetBlendModeSelection();

  auto draw_color_wheel = [](Canvas& canvas) {
    /// color_wheel_sampler: r=0 -> fuchsia, r=2pi/3 -> yellow, r=4pi/3 ->
    /// cyan domain: r >= 0 (because modulo used is non euclidean)
    auto color_wheel_sampler = [](Radians r) {
      Scalar x = r.radians / k2Pi + 1;

      // https://www.desmos.com/calculator/6nhjelyoaj
      auto color_cycle = [](Scalar x) {
        Scalar cycle = std::fmod(x, 6.0f);
        return std::max(0.0f, std::min(1.0f, 2 - std::abs(2 - cycle)));
      };
      return Color(color_cycle(6 * x + 1),  //
                   color_cycle(6 * x - 1),  //
                   color_cycle(6 * x - 3),  //
                   1);
    };

    Paint paint;
    paint.blend_mode = BlendMode::kSourceOver;

    // Draw a fancy color wheel for the backdrop.
    // https://www.desmos.com/calculator/xw7kafthwd
    const int max_dist = 900;
    for (int i = 0; i <= 900; i++) {
      Radians r(kPhi / k2Pi * i);
      Scalar distance = r.radians / std::powf(4.12, 0.0026 * r.radians);
      Scalar normalized_distance = static_cast<Scalar>(i) / max_dist;

      paint.color =
          color_wheel_sampler(r).WithAlpha(1.0f - normalized_distance);
      Point position(distance * std::sin(r.radians),
                     -distance * std::cos(r.radians));

      canvas.DrawCircle(position, 9 + normalized_distance * 3, paint);
    }
  };

  std::shared_ptr<Image> color_wheel_image;
  Matrix color_wheel_transform;

  auto callback = [&](AiksContext& renderer) -> std::optional<Picture> {
    // UI state.
    static bool cache_the_wheel = true;
    static int current_blend_index = 3;
    static float dst_alpha = 1;
    static float src_alpha = 1;
    static Color color0 = Color::Red();
    static Color color1 = Color::Green();
    static Color color2 = Color::Blue();

    ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    {
      ImGui::Checkbox("Cache the wheel", &cache_the_wheel);
      ImGui::ListBox("Blending mode", &current_blend_index,
                     blend_modes.blend_mode_names.data(),
                     blend_modes.blend_mode_names.size());
      ImGui::SliderFloat("Source alpha", &src_alpha, 0, 1);
      ImGui::ColorEdit4("Color A", reinterpret_cast<float*>(&color0));
      ImGui::ColorEdit4("Color B", reinterpret_cast<float*>(&color1));
      ImGui::ColorEdit4("Color C", reinterpret_cast<float*>(&color2));
      ImGui::SliderFloat("Destination alpha", &dst_alpha, 0, 1);
    }
    ImGui::End();

    static Point content_scale;
    Point new_content_scale = GetContentScale();

    if (!cache_the_wheel || new_content_scale != content_scale) {
      content_scale = new_content_scale;

      // Render the color wheel to an image.

      Canvas canvas;
      canvas.Scale(content_scale);

      canvas.Translate(Vector2(500, 400));
      canvas.Scale(Vector2(3, 3));

      draw_color_wheel(canvas);
      auto color_wheel_picture = canvas.EndRecordingAsPicture();
      auto snapshot = color_wheel_picture.Snapshot(renderer);
      if (!snapshot.has_value() || !snapshot->texture) {
        return std::nullopt;
      }
      color_wheel_image = std::make_shared<Image>(snapshot->texture);
      color_wheel_transform = snapshot->transform;
    }

    Canvas canvas;

    // Blit the color wheel backdrop to the screen with managed alpha.
    canvas.SaveLayer({.color = Color::White().WithAlpha(dst_alpha),
                      .blend_mode = BlendMode::kSource});
    {
      canvas.DrawPaint({.color = Color::White()});

      canvas.Save();
      canvas.Transform(color_wheel_transform);
      canvas.DrawImage(color_wheel_image, Point(), Paint());
      canvas.Restore();
    }
    canvas.Restore();

    canvas.Scale(content_scale);
    canvas.Translate(Vector2(500, 400));
    canvas.Scale(Vector2(3, 3));

    // Draw 3 circles to a subpass and blend it in.
    canvas.SaveLayer(
        {.color = Color::White().WithAlpha(src_alpha),
         .blend_mode = blend_modes.blend_mode_values[current_blend_index]});
    {
      Paint paint;
      paint.blend_mode = BlendMode::kPlus;
      const Scalar x = std::sin(k2Pi / 3);
      const Scalar y = -std::cos(k2Pi / 3);
      paint.color = color0;
      canvas.DrawCircle(Point(-x, y) * 45, 65, paint);
      paint.color = color1;
      canvas.DrawCircle(Point(0, -1) * 45, 65, paint);
      paint.color = color2;
      canvas.DrawCircle(Point(x, y) * 45, 65, paint);
    }
    canvas.Restore();

    return canvas.EndRecordingAsPicture();
  };

  ASSERT_TRUE(OpenPlaygroundHere(callback));
}

TEST_P(AiksTest, TransformMultipliesCorrectly) {
  Canvas canvas;
  ASSERT_MATRIX_NEAR(canvas.GetCurrentTransform(), Matrix());

  // clang-format off
  canvas.Translate(Vector3(100, 200));
  ASSERT_MATRIX_NEAR(
    canvas.GetCurrentTransform(),
    Matrix(  1,   0,   0,   0,
             0,   1,   0,   0,
             0,   0,   1,   0,
           100, 200,   0,   1));

  canvas.Rotate(Radians(kPiOver2));
  ASSERT_MATRIX_NEAR(
    canvas.GetCurrentTransform(),
    Matrix(  0,   1,   0,   0,
            -1,   0,   0,   0,
             0,   0,   1,   0,
           100, 200,   0,   1));

  canvas.Scale(Vector3(2, 3));
  ASSERT_MATRIX_NEAR(
    canvas.GetCurrentTransform(),
    Matrix(  0,   2,   0,   0,
            -3,   0,   0,   0,
             0,   0,   0,   0,
           100, 200,   0,   1));

  canvas.Translate(Vector3(100, 200));
  ASSERT_MATRIX_NEAR(
    canvas.GetCurrentTransform(),
    Matrix(   0,   2,   0,   0,
             -3,   0,   0,   0,
              0,   0,   0,   0,
           -500, 400,   0,   1));
  // clang-format on
}

TEST_P(AiksTest, FilledCirclesRenderCorrectly) {
  Canvas canvas;
  canvas.Scale(GetContentScale());
  Paint paint;
  const int color_count = 3;
  Color colors[color_count] = {
      Color::Blue(),
      Color::Green(),
      Color::Crimson(),
  };

  paint.color = Color::White();
  canvas.DrawPaint(paint);

  int c_index = 0;
  int radius = 600;
  while (radius > 0) {
    paint.color = colors[(c_index++) % color_count];
    canvas.DrawCircle({10, 10}, radius, paint);
    if (radius > 30) {
      radius -= 10;
    } else {
      radius -= 2;
    }
  }

  std::vector<Color> gradient_colors = {
      Color{0x1f / 255.0, 0.0, 0x5c / 255.0, 1.0},
      Color{0x5b / 255.0, 0.0, 0x60 / 255.0, 1.0},
      Color{0x87 / 255.0, 0x01 / 255.0, 0x60 / 255.0, 1.0},
      Color{0xac / 255.0, 0x25 / 255.0, 0x53 / 255.0, 1.0},
      Color{0xe1 / 255.0, 0x6b / 255.0, 0x5c / 255.0, 1.0},
      Color{0xf3 / 255.0, 0x90 / 255.0, 0x60 / 255.0, 1.0},
      Color{0xff / 255.0, 0xb5 / 255.0, 0x6b / 250.0, 1.0}};
  std::vector<Scalar> stops = {
      0.0,
      (1.0 / 6.0) * 1,
      (1.0 / 6.0) * 2,
      (1.0 / 6.0) * 3,
      (1.0 / 6.0) * 4,
      (1.0 / 6.0) * 5,
      1.0,
  };
  auto texture = CreateTextureForFixture("airplane.jpg",
                                         /*enable_mipmapping=*/true);

  paint.color_source = ColorSource::MakeRadialGradient(
      {500, 600}, 75, std::move(gradient_colors), std::move(stops),
      Entity::TileMode::kMirror, {});
  canvas.DrawCircle({500, 600}, 100, paint);

  paint.color_source = ColorSource::MakeImage(
      texture, Entity::TileMode::kRepeat, Entity::TileMode::kRepeat, {},
      Matrix::MakeTranslation({700, 200}));
  canvas.DrawCircle({800, 300}, 100, paint);

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, StrokedCirclesRenderCorrectly) {
  Canvas canvas;
  canvas.Scale(GetContentScale());
  Paint paint;
  const int color_count = 3;
  Color colors[color_count] = {
      Color::Blue(),
      Color::Green(),
      Color::Crimson(),
  };

  paint.color = Color::White();
  canvas.DrawPaint(paint);

  int c_index = 0;

  auto draw = [&paint, &colors, &c_index](Canvas& canvas, Point center,
                                          Scalar r, Scalar dr, int n) {
    for (int i = 0; i < n; i++) {
      paint.color = colors[(c_index++) % color_count];
      canvas.DrawCircle(center, r, paint);
      r += dr;
    }
  };

  paint.style = Paint::Style::kStroke;
  paint.stroke_width = 1;
  draw(canvas, {10, 10}, 2, 2, 14);  // r = [2, 28], covers [1,29]
  paint.stroke_width = 5;
  draw(canvas, {10, 10}, 35, 10, 56);  // r = [35, 585], covers [30,590]

  std::vector<Color> gradient_colors = {
      Color{0x1f / 255.0, 0.0, 0x5c / 255.0, 1.0},
      Color{0x5b / 255.0, 0.0, 0x60 / 255.0, 1.0},
      Color{0x87 / 255.0, 0x01 / 255.0, 0x60 / 255.0, 1.0},
      Color{0xac / 255.0, 0x25 / 255.0, 0x53 / 255.0, 1.0},
      Color{0xe1 / 255.0, 0x6b / 255.0, 0x5c / 255.0, 1.0},
      Color{0xf3 / 255.0, 0x90 / 255.0, 0x60 / 255.0, 1.0},
      Color{0xff / 255.0, 0xb5 / 255.0, 0x6b / 250.0, 1.0}};
  std::vector<Scalar> stops = {
      0.0,
      (1.0 / 6.0) * 1,
      (1.0 / 6.0) * 2,
      (1.0 / 6.0) * 3,
      (1.0 / 6.0) * 4,
      (1.0 / 6.0) * 5,
      1.0,
  };
  auto texture = CreateTextureForFixture("airplane.jpg",
                                         /*enable_mipmapping=*/true);

  paint.color_source = ColorSource::MakeRadialGradient(
      {500, 600}, 75, std::move(gradient_colors), std::move(stops),
      Entity::TileMode::kMirror, {});
  draw(canvas, {500, 600}, 5, 10, 10);

  paint.color_source = ColorSource::MakeImage(
      texture, Entity::TileMode::kRepeat, Entity::TileMode::kRepeat, {},
      Matrix::MakeTranslation({700, 200}));
  draw(canvas, {800, 300}, 5, 10, 10);

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, FilledEllipsesRenderCorrectly) {
  Canvas canvas;
  canvas.Scale(GetContentScale());
  Paint paint;
  const int color_count = 3;
  Color colors[color_count] = {
      Color::Blue(),
      Color::Green(),
      Color::Crimson(),
  };

  paint.color = Color::White();
  canvas.DrawPaint(paint);

  int c_index = 0;
  int long_radius = 600;
  int short_radius = 600;
  while (long_radius > 0 && short_radius > 0) {
    paint.color = colors[(c_index++) % color_count];
    canvas.DrawOval(Rect::MakeXYWH(10 - long_radius, 10 - short_radius,
                                   long_radius * 2, short_radius * 2),
                    paint);
    canvas.DrawOval(Rect::MakeXYWH(1000 - short_radius, 750 - long_radius,
                                   short_radius * 2, long_radius * 2),
                    paint);
    if (short_radius > 30) {
      short_radius -= 10;
      long_radius -= 5;
    } else {
      short_radius -= 2;
      long_radius -= 1;
    }
  }

  std::vector<Color> gradient_colors = {
      Color{0x1f / 255.0, 0.0, 0x5c / 255.0, 1.0},
      Color{0x5b / 255.0, 0.0, 0x60 / 255.0, 1.0},
      Color{0x87 / 255.0, 0x01 / 255.0, 0x60 / 255.0, 1.0},
      Color{0xac / 255.0, 0x25 / 255.0, 0x53 / 255.0, 1.0},
      Color{0xe1 / 255.0, 0x6b / 255.0, 0x5c / 255.0, 1.0},
      Color{0xf3 / 255.0, 0x90 / 255.0, 0x60 / 255.0, 1.0},
      Color{0xff / 255.0, 0xb5 / 255.0, 0x6b / 250.0, 1.0}};
  std::vector<Scalar> stops = {
      0.0,
      (1.0 / 6.0) * 1,
      (1.0 / 6.0) * 2,
      (1.0 / 6.0) * 3,
      (1.0 / 6.0) * 4,
      (1.0 / 6.0) * 5,
      1.0,
  };
  auto texture = CreateTextureForFixture("airplane.jpg",
                                         /*enable_mipmapping=*/true);

  paint.color = Color::White().WithAlpha(0.5);

  paint.color_source = ColorSource::MakeRadialGradient(
      {300, 650}, 75, std::move(gradient_colors), std::move(stops),
      Entity::TileMode::kMirror, {});
  canvas.DrawOval(Rect::MakeXYWH(200, 625, 200, 50), paint);
  canvas.DrawOval(Rect::MakeXYWH(275, 550, 50, 200), paint);

  paint.color_source = ColorSource::MakeImage(
      texture, Entity::TileMode::kRepeat, Entity::TileMode::kRepeat, {},
      Matrix::MakeTranslation({610, 15}));
  canvas.DrawOval(Rect::MakeXYWH(610, 90, 200, 50), paint);
  canvas.DrawOval(Rect::MakeXYWH(685, 15, 50, 200), paint);

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, FilledRoundRectsRenderCorrectly) {
  Canvas canvas;
  canvas.Scale(GetContentScale());
  Paint paint;
  const int color_count = 3;
  Color colors[color_count] = {
      Color::Blue(),
      Color::Green(),
      Color::Crimson(),
  };

  paint.color = Color::White();
  canvas.DrawPaint(paint);

  int c_index = 0;
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      paint.color = colors[(c_index++) % color_count];
      canvas.DrawRRect(Rect::MakeXYWH(i * 100 + 10, j * 100 + 20, 80, 80),
                       Size(i * 5 + 10, j * 5 + 10), paint);
    }
  }
  paint.color = colors[(c_index++) % color_count];
  canvas.DrawRRect(Rect::MakeXYWH(10, 420, 380, 80), Size(40, 40), paint);
  paint.color = colors[(c_index++) % color_count];
  canvas.DrawRRect(Rect::MakeXYWH(410, 20, 80, 380), Size(40, 40), paint);

  std::vector<Color> gradient_colors = {
      Color{0x1f / 255.0, 0.0, 0x5c / 255.0, 1.0},
      Color{0x5b / 255.0, 0.0, 0x60 / 255.0, 1.0},
      Color{0x87 / 255.0, 0x01 / 255.0, 0x60 / 255.0, 1.0},
      Color{0xac / 255.0, 0x25 / 255.0, 0x53 / 255.0, 1.0},
      Color{0xe1 / 255.0, 0x6b / 255.0, 0x5c / 255.0, 1.0},
      Color{0xf3 / 255.0, 0x90 / 255.0, 0x60 / 255.0, 1.0},
      Color{0xff / 255.0, 0xb5 / 255.0, 0x6b / 250.0, 1.0}};
  std::vector<Scalar> stops = {
      0.0,
      (1.0 / 6.0) * 1,
      (1.0 / 6.0) * 2,
      (1.0 / 6.0) * 3,
      (1.0 / 6.0) * 4,
      (1.0 / 6.0) * 5,
      1.0,
  };
  auto texture = CreateTextureForFixture("airplane.jpg",
                                         /*enable_mipmapping=*/true);

  paint.color = Color::White().WithAlpha(0.1);
  paint.color_source = ColorSource::MakeRadialGradient(
      {550, 550}, 75, gradient_colors, stops, Entity::TileMode::kMirror, {});
  for (int i = 1; i <= 10; i++) {
    int j = 11 - i;
    canvas.DrawRRect(Rect::MakeLTRB(550 - i * 20, 550 - j * 20,  //
                                    550 + i * 20, 550 + j * 20),
                     Size(i * 10, j * 10), paint);
  }
  paint.color = Color::White().WithAlpha(0.5);
  paint.color_source = ColorSource::MakeRadialGradient(
      {200, 650}, 75, std::move(gradient_colors), std::move(stops),
      Entity::TileMode::kMirror, {});
  canvas.DrawRRect(Rect::MakeLTRB(100, 610, 300, 690), Size(40, 40), paint);
  canvas.DrawRRect(Rect::MakeLTRB(160, 550, 240, 750), Size(40, 40), paint);

  paint.color = Color::White().WithAlpha(0.1);
  paint.color_source = ColorSource::MakeImage(
      texture, Entity::TileMode::kRepeat, Entity::TileMode::kRepeat, {},
      Matrix::MakeTranslation({520, 20}));
  for (int i = 1; i <= 10; i++) {
    int j = 11 - i;
    canvas.DrawRRect(Rect::MakeLTRB(720 - i * 20, 220 - j * 20,  //
                                    720 + i * 20, 220 + j * 20),
                     Size(i * 10, j * 10), paint);
  }
  paint.color = Color::White().WithAlpha(0.5);
  paint.color_source = ColorSource::MakeImage(
      texture, Entity::TileMode::kRepeat, Entity::TileMode::kRepeat, {},
      Matrix::MakeTranslation({800, 300}));
  canvas.DrawRRect(Rect::MakeLTRB(800, 410, 1000, 490), Size(40, 40), paint);
  canvas.DrawRRect(Rect::MakeLTRB(860, 350, 940, 550), Size(40, 40), paint);

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, SolidColorCirclesOvalsRRectsMaskBlurCorrectly) {
  Canvas canvas;
  canvas.Scale(GetContentScale());
  Paint paint;
  paint.mask_blur_descriptor = Paint::MaskBlurDescriptor{
      .style = FilterContents::BlurStyle::kNormal,
      .sigma = Sigma{1},
  };

  canvas.DrawPaint({.color = Color::White()});

  paint.color = Color::Crimson();
  Scalar y = 100.0f;
  for (int i = 0; i < 5; i++) {
    Scalar x = (i + 1) * 100;
    Scalar radius = x / 10.0f;
    canvas.DrawRect(Rect::MakeXYWH(x + 25 - radius / 2, y + radius / 2,  //
                                   radius, 60.0f - radius),
                    paint);
  }

  paint.color = Color::Blue();
  y += 100.0f;
  for (int i = 0; i < 5; i++) {
    Scalar x = (i + 1) * 100;
    Scalar radius = x / 10.0f;
    canvas.DrawCircle({x + 25, y + 25}, radius, paint);
  }

  paint.color = Color::Green();
  y += 100.0f;
  for (int i = 0; i < 5; i++) {
    Scalar x = (i + 1) * 100;
    Scalar radius = x / 10.0f;
    canvas.DrawOval(Rect::MakeXYWH(x + 25 - radius / 2, y + radius / 2,  //
                                   radius, 60.0f - radius),
                    paint);
  }

  paint.color = Color::Purple();
  y += 100.0f;
  for (int i = 0; i < 5; i++) {
    Scalar x = (i + 1) * 100;
    Scalar radius = x / 20.0f;
    canvas.DrawRRect(Rect::MakeXYWH(x, y, 60.0f, 60.0f),  //
                     {radius, radius},                    //
                     paint);
  }

  paint.color = Color::Orange();
  y += 100.0f;
  for (int i = 0; i < 5; i++) {
    Scalar x = (i + 1) * 100;
    Scalar radius = x / 20.0f;
    canvas.DrawRRect(Rect::MakeXYWH(x, y, 60.0f, 60.0f),  //
                     {radius, 5.0f}, paint);
  }

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, FilledRoundRectPathsRenderCorrectly) {
  Canvas canvas;
  canvas.Scale(GetContentScale());
  Paint paint;
  const int color_count = 3;
  Color colors[color_count] = {
      Color::Blue(),
      Color::Green(),
      Color::Crimson(),
  };

  paint.color = Color::White();
  canvas.DrawPaint(paint);

  auto draw_rrect_as_path = [&canvas](const Rect& rect, const Size& radii,
                                      const Paint& paint) {
    PathBuilder builder = PathBuilder();
    builder.AddRoundedRect(rect, radii);
    canvas.DrawPath(builder.TakePath(), paint);
  };

  int c_index = 0;
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      paint.color = colors[(c_index++) % color_count];
      draw_rrect_as_path(Rect::MakeXYWH(i * 100 + 10, j * 100 + 20, 80, 80),
                         Size(i * 5 + 10, j * 5 + 10), paint);
    }
  }
  paint.color = colors[(c_index++) % color_count];
  draw_rrect_as_path(Rect::MakeXYWH(10, 420, 380, 80), Size(40, 40), paint);
  paint.color = colors[(c_index++) % color_count];
  draw_rrect_as_path(Rect::MakeXYWH(410, 20, 80, 380), Size(40, 40), paint);

  std::vector<Color> gradient_colors = {
      Color{0x1f / 255.0, 0.0, 0x5c / 255.0, 1.0},
      Color{0x5b / 255.0, 0.0, 0x60 / 255.0, 1.0},
      Color{0x87 / 255.0, 0x01 / 255.0, 0x60 / 255.0, 1.0},
      Color{0xac / 255.0, 0x25 / 255.0, 0x53 / 255.0, 1.0},
      Color{0xe1 / 255.0, 0x6b / 255.0, 0x5c / 255.0, 1.0},
      Color{0xf3 / 255.0, 0x90 / 255.0, 0x60 / 255.0, 1.0},
      Color{0xff / 255.0, 0xb5 / 255.0, 0x6b / 250.0, 1.0}};
  std::vector<Scalar> stops = {
      0.0,
      (1.0 / 6.0) * 1,
      (1.0 / 6.0) * 2,
      (1.0 / 6.0) * 3,
      (1.0 / 6.0) * 4,
      (1.0 / 6.0) * 5,
      1.0,
  };
  auto texture = CreateTextureForFixture("airplane.jpg",
                                         /*enable_mipmapping=*/true);

  paint.color = Color::White().WithAlpha(0.1);
  paint.color_source = ColorSource::MakeRadialGradient(
      {550, 550}, 75, gradient_colors, stops, Entity::TileMode::kMirror, {});
  for (int i = 1; i <= 10; i++) {
    int j = 11 - i;
    draw_rrect_as_path(Rect::MakeLTRB(550 - i * 20, 550 - j * 20,  //
                                      550 + i * 20, 550 + j * 20),
                       Size(i * 10, j * 10), paint);
  }
  paint.color = Color::White().WithAlpha(0.5);
  paint.color_source = ColorSource::MakeRadialGradient(
      {200, 650}, 75, std::move(gradient_colors), std::move(stops),
      Entity::TileMode::kMirror, {});
  draw_rrect_as_path(Rect::MakeLTRB(100, 610, 300, 690), Size(40, 40), paint);
  draw_rrect_as_path(Rect::MakeLTRB(160, 550, 240, 750), Size(40, 40), paint);

  paint.color = Color::White().WithAlpha(0.1);
  paint.color_source = ColorSource::MakeImage(
      texture, Entity::TileMode::kRepeat, Entity::TileMode::kRepeat, {},
      Matrix::MakeTranslation({520, 20}));
  for (int i = 1; i <= 10; i++) {
    int j = 11 - i;
    draw_rrect_as_path(Rect::MakeLTRB(720 - i * 20, 220 - j * 20,  //
                                      720 + i * 20, 220 + j * 20),
                       Size(i * 10, j * 10), paint);
  }
  paint.color = Color::White().WithAlpha(0.5);
  paint.color_source = ColorSource::MakeImage(
      texture, Entity::TileMode::kRepeat, Entity::TileMode::kRepeat, {},
      Matrix::MakeTranslation({800, 300}));
  draw_rrect_as_path(Rect::MakeLTRB(800, 410, 1000, 490), Size(40, 40), paint);
  draw_rrect_as_path(Rect::MakeLTRB(860, 350, 940, 550), Size(40, 40), paint);

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CoverageOriginShouldBeAccountedForInSubpasses) {
  auto callback = [&](AiksContext& renderer) -> std::optional<Picture> {
    Canvas canvas;
    canvas.Scale(GetContentScale());

    Paint alpha;
    alpha.color = Color::Red().WithAlpha(0.5);

    auto current = Point{25, 25};
    const auto offset = Point{25, 25};
    const auto size = Size(100, 100);

    auto [b0, b1] = IMPELLER_PLAYGROUND_LINE(Point(40, 40), Point(160, 160), 10,
                                             Color::White(), Color::White());
    auto bounds = Rect::MakeLTRB(b0.x, b0.y, b1.x, b1.y);

    canvas.DrawRect(bounds, Paint{.color = Color::Yellow(),
                                  .stroke_width = 5.0f,
                                  .style = Paint::Style::kStroke});

    canvas.SaveLayer(alpha, bounds);

    canvas.DrawRect(Rect::MakeOriginSize(current, size),
                    Paint{.color = Color::Red()});
    canvas.DrawRect(Rect::MakeOriginSize(current += offset, size),
                    Paint{.color = Color::Green()});
    canvas.DrawRect(Rect::MakeOriginSize(current += offset, size),
                    Paint{.color = Color::Blue()});

    canvas.Restore();

    return canvas.EndRecordingAsPicture();
  };

  ASSERT_TRUE(OpenPlaygroundHere(callback));
}

TEST_P(AiksTest, SaveLayerDrawsBehindSubsequentEntities) {
  // Compare with https://fiddle.skia.org/c/9e03de8567ffb49e7e83f53b64bcf636
  Canvas canvas;
  Paint paint;

  paint.color = Color::Black();
  Rect rect = Rect::MakeXYWH(25, 25, 25, 25);
  canvas.DrawRect(rect, paint);

  canvas.Translate({10, 10});
  canvas.SaveLayer({});

  paint.color = Color::Green();
  canvas.DrawRect(rect, paint);

  canvas.Restore();

  canvas.Translate({10, 10});
  paint.color = Color::Red();
  canvas.DrawRect(rect, paint);

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, SiblingSaveLayerBoundsAreRespected) {
  Canvas canvas;
  Paint paint;
  Rect rect = Rect::MakeXYWH(0, 0, 1000, 1000);

  // Black, green, and red squares offset by [10, 10].
  {
    canvas.SaveLayer({}, Rect::MakeXYWH(25, 25, 25, 25));
    paint.color = Color::Black();
    canvas.DrawRect(rect, paint);
    canvas.Restore();
  }

  {
    canvas.SaveLayer({}, Rect::MakeXYWH(35, 35, 25, 25));
    paint.color = Color::Green();
    canvas.DrawRect(rect, paint);
    canvas.Restore();
  }

  {
    canvas.SaveLayer({}, Rect::MakeXYWH(45, 45, 25, 25));
    paint.color = Color::Red();
    canvas.DrawRect(rect, paint);
    canvas.Restore();
  }

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CanRenderClippedLayers) {
  Canvas canvas;

  canvas.DrawPaint({.color = Color::White()});

  // Draw a green circle on the screen.
  {
    // Increase the clip depth for the savelayer to contend with.
    canvas.ClipPath(PathBuilder{}.AddCircle({100, 100}, 50).TakePath());

    canvas.SaveLayer({}, Rect::MakeXYWH(50, 50, 100, 100));

    // Fill the layer with white.
    canvas.DrawRect(Rect::MakeSize(Size{400, 400}), {.color = Color::White()});
    // Fill the layer with green, but do so with a color blend that can't be
    // collapsed into the parent pass.
    // TODO(jonahwilliams): this blend mode was changed from color burn to
    // hardlight to work around https://github.com/flutter/flutter/issues/136554
    // .
    canvas.DrawRect(
        Rect::MakeSize(Size{400, 400}),
        {.color = Color::Green(), .blend_mode = BlendMode::kHardLight});
  }

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, SaveLayerFiltersScaleWithTransform) {
  Canvas canvas;
  canvas.Scale(GetContentScale());
  canvas.Translate(Vector2(100, 100));

  auto texture = std::make_shared<Image>(CreateTextureForFixture("boston.jpg"));
  auto draw_image_layer = [&canvas, &texture](const Paint& paint) {
    canvas.SaveLayer(paint);
    canvas.DrawImage(texture, {}, Paint{});
    canvas.Restore();
  };

  Paint effect_paint;
  effect_paint.mask_blur_descriptor = Paint::MaskBlurDescriptor{
      .style = FilterContents::BlurStyle::kNormal,
      .sigma = Sigma{6},
  };
  draw_image_layer(effect_paint);

  canvas.Translate(Vector2(300, 300));
  canvas.Scale(Vector2(3, 3));
  draw_image_layer(effect_paint);

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

#if IMPELLER_ENABLE_3D
TEST_P(AiksTest, SceneColorSource) {
  // Load up the scene.
  auto mapping =
      flutter::testing::OpenFixtureAsMapping("flutter_logo_baked.glb.ipscene");
  ASSERT_NE(mapping, nullptr);

  std::shared_ptr<scene::Node> gltf_scene = scene::Node::MakeFromFlatbuffer(
      *mapping, *GetContext()->GetResourceAllocator());
  ASSERT_NE(gltf_scene, nullptr);

  auto callback = [&](AiksContext& renderer) -> std::optional<Picture> {
    Paint paint;

    ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    static Scalar distance = 2;
    ImGui::SliderFloat("Distance", &distance, 0, 4);
    static Scalar y_pos = 0;
    ImGui::SliderFloat("Y", &y_pos, -3, 3);
    static Scalar fov = 45;
    ImGui::SliderFloat("FOV", &fov, 1, 180);
    ImGui::End();

    Scalar angle = GetSecondsElapsed();
    auto camera_position =
        Vector3(distance * std::sin(angle), y_pos, -distance * std::cos(angle));

    paint.color_source = ColorSource::MakeScene(
        gltf_scene,
        Matrix::MakePerspective(Degrees(fov), GetWindowSize(), 0.1, 1000) *
            Matrix::MakeLookAt(camera_position, {0, 0, 0}, {0, 1, 0}));

    Canvas canvas;
    canvas.DrawPaint(Paint{.color = Color::MakeRGBA8(0xf9, 0xf9, 0xf9, 0xff)});
    canvas.Scale(GetContentScale());
    canvas.DrawPaint(paint);
    return canvas.EndRecordingAsPicture();
  };

  ASSERT_TRUE(OpenPlaygroundHere(callback));
}
#endif  // IMPELLER_ENABLE_3D

TEST_P(AiksTest, PaintWithFilters) {
  // validate that a paint with a color filter "HasFilters", no other filters
  // impact this setting.
  Paint paint;

  ASSERT_FALSE(paint.HasColorFilter());

  paint.color_filter =
      ColorFilter::MakeBlend(BlendMode::kSourceOver, Color::Blue());

  ASSERT_TRUE(paint.HasColorFilter());

  paint.image_filter = ImageFilter::MakeBlur(Sigma(1.0), Sigma(1.0),
                                             FilterContents::BlurStyle::kNormal,
                                             Entity::TileMode::kClamp);

  ASSERT_TRUE(paint.HasColorFilter());

  paint.mask_blur_descriptor = {};

  ASSERT_TRUE(paint.HasColorFilter());

  paint.color_filter = nullptr;

  ASSERT_FALSE(paint.HasColorFilter());
}

TEST_P(AiksTest, OpacityPeepHoleApplicationTest) {
  auto entity_pass = std::make_shared<EntityPass>();
  auto rect = Rect::MakeLTRB(0, 0, 100, 100);
  Paint paint;
  paint.color = Color::White().WithAlpha(0.5);
  paint.color_filter =
      ColorFilter::MakeBlend(BlendMode::kSourceOver, Color::Blue());

  // Paint has color filter, can't elide.
  auto delegate = std::make_shared<OpacityPeepholePassDelegate>(paint);
  ASSERT_FALSE(delegate->CanCollapseIntoParentPass(entity_pass.get()));

  paint.color_filter = nullptr;
  paint.image_filter = ImageFilter::MakeBlur(Sigma(1.0), Sigma(1.0),
                                             FilterContents::BlurStyle::kNormal,
                                             Entity::TileMode::kClamp);

  // Paint has image filter, can't elide.
  delegate = std::make_shared<OpacityPeepholePassDelegate>(paint);
  ASSERT_FALSE(delegate->CanCollapseIntoParentPass(entity_pass.get()));

  paint.image_filter = nullptr;
  paint.color = Color::Red();

  // Paint has no alpha, can't elide;
  delegate = std::make_shared<OpacityPeepholePassDelegate>(paint);
  ASSERT_FALSE(delegate->CanCollapseIntoParentPass(entity_pass.get()));

  // Positive test.
  Entity entity;
  entity.SetContents(SolidColorContents::Make(
      PathBuilder{}.AddRect(rect).TakePath(), Color::Red()));
  entity_pass->AddEntity(std::move(entity));
  paint.color = Color::Red().WithAlpha(0.5);

  delegate = std::make_shared<OpacityPeepholePassDelegate>(paint);
  ASSERT_TRUE(delegate->CanCollapseIntoParentPass(entity_pass.get()));
}

TEST_P(AiksTest, DrawPaintAbsorbsClears) {
  Canvas canvas;
  canvas.DrawPaint({.color = Color::Red(), .blend_mode = BlendMode::kSource});
  canvas.DrawPaint({.color = Color::CornflowerBlue().WithAlpha(0.75),
                    .blend_mode = BlendMode::kSourceOver});

  Picture picture = canvas.EndRecordingAsPicture();
  auto expected = Color::Red().Blend(Color::CornflowerBlue().WithAlpha(0.75),
                                     BlendMode::kSourceOver);
  ASSERT_EQ(picture.pass->GetClearColor(), expected);

  std::shared_ptr<ContextSpy> spy = ContextSpy::Make();
  std::shared_ptr<Context> real_context = GetContext();
  std::shared_ptr<ContextMock> mock_context = spy->MakeContext(real_context);
  AiksContext renderer(mock_context, nullptr);
  std::shared_ptr<Image> image = picture.ToImage(renderer, {300, 300});

  ASSERT_EQ(spy->render_passes_.size(), 1llu);
  std::shared_ptr<RenderPass> render_pass = spy->render_passes_[0];
  ASSERT_EQ(render_pass->GetCommands().size(), 0llu);
}

// This is important to enforce with texture reuse, since cached textures need
// to be cleared before reuse.
TEST_P(AiksTest,
       ParentSaveLayerCreatesRenderPassWhenChildBackdropFilterIsPresent) {
  Canvas canvas;
  canvas.SaveLayer({}, std::nullopt, ImageFilter::MakeMatrix(Matrix(), {}));
  canvas.DrawPaint({.color = Color::Red(), .blend_mode = BlendMode::kSource});
  canvas.DrawPaint({.color = Color::CornflowerBlue().WithAlpha(0.75),
                    .blend_mode = BlendMode::kSourceOver});
  canvas.Restore();

  Picture picture = canvas.EndRecordingAsPicture();

  std::shared_ptr<ContextSpy> spy = ContextSpy::Make();
  std::shared_ptr<Context> real_context = GetContext();
  std::shared_ptr<ContextMock> mock_context = spy->MakeContext(real_context);
  AiksContext renderer(mock_context, nullptr);
  std::shared_ptr<Image> image = picture.ToImage(renderer, {300, 300});

  ASSERT_EQ(spy->render_passes_.size(),
            GetBackend() == PlaygroundBackend::kOpenGLES ? 4llu : 3llu);
  std::shared_ptr<RenderPass> render_pass = spy->render_passes_[0];
  ASSERT_EQ(render_pass->GetCommands().size(), 0llu);
}

TEST_P(AiksTest, DrawRectAbsorbsClears) {
  Canvas canvas;
  canvas.DrawRect(Rect::MakeXYWH(0, 0, 300, 300),
                  {.color = Color::Red(), .blend_mode = BlendMode::kSource});
  canvas.DrawRect(Rect::MakeXYWH(0, 0, 300, 300),
                  {.color = Color::CornflowerBlue().WithAlpha(0.75),
                   .blend_mode = BlendMode::kSourceOver});

  std::shared_ptr<ContextSpy> spy = ContextSpy::Make();
  Picture picture = canvas.EndRecordingAsPicture();
  std::shared_ptr<Context> real_context = GetContext();
  std::shared_ptr<ContextMock> mock_context = spy->MakeContext(real_context);
  AiksContext renderer(mock_context, nullptr);
  std::shared_ptr<Image> image = picture.ToImage(renderer, {300, 300});

  ASSERT_EQ(spy->render_passes_.size(), 1llu);
  std::shared_ptr<RenderPass> render_pass = spy->render_passes_[0];
  ASSERT_EQ(render_pass->GetCommands().size(), 0llu);
}

TEST_P(AiksTest, DrawRectAbsorbsClearsNegativeRRect) {
  Canvas canvas;
  canvas.DrawRRect(Rect::MakeXYWH(0, 0, 300, 300), {5.0, 5.0},
                   {.color = Color::Red(), .blend_mode = BlendMode::kSource});
  canvas.DrawRRect(Rect::MakeXYWH(0, 0, 300, 300), {5.0, 5.0},
                   {.color = Color::CornflowerBlue().WithAlpha(0.75),
                    .blend_mode = BlendMode::kSourceOver});

  std::shared_ptr<ContextSpy> spy = ContextSpy::Make();
  Picture picture = canvas.EndRecordingAsPicture();
  std::shared_ptr<Context> real_context = GetContext();
  std::shared_ptr<ContextMock> mock_context = spy->MakeContext(real_context);
  AiksContext renderer(mock_context, nullptr);
  std::shared_ptr<Image> image = picture.ToImage(renderer, {300, 300});

  ASSERT_EQ(spy->render_passes_.size(), 1llu);
  std::shared_ptr<RenderPass> render_pass = spy->render_passes_[0];
  ASSERT_EQ(render_pass->GetCommands().size(), 2llu);
}

TEST_P(AiksTest, DrawRectAbsorbsClearsNegativeRotation) {
  Canvas canvas;
  canvas.Translate(Vector3(150.0, 150.0, 0.0));
  canvas.Rotate(Degrees(45.0));
  canvas.Translate(Vector3(-150.0, -150.0, 0.0));
  canvas.DrawRect(Rect::MakeXYWH(0, 0, 300, 300),
                  {.color = Color::Red(), .blend_mode = BlendMode::kSource});

  std::shared_ptr<ContextSpy> spy = ContextSpy::Make();
  Picture picture = canvas.EndRecordingAsPicture();
  std::shared_ptr<Context> real_context = GetContext();
  std::shared_ptr<ContextMock> mock_context = spy->MakeContext(real_context);
  AiksContext renderer(mock_context, nullptr);
  std::shared_ptr<Image> image = picture.ToImage(renderer, {300, 300});

  ASSERT_EQ(spy->render_passes_.size(), 1llu);
  std::shared_ptr<RenderPass> render_pass = spy->render_passes_[0];
  ASSERT_EQ(render_pass->GetCommands().size(), 1llu);
}

TEST_P(AiksTest, DrawRectAbsorbsClearsNegative) {
  Canvas canvas;
  canvas.DrawRect(Rect::MakeXYWH(0, 0, 300, 300),
                  {.color = Color::Red(), .blend_mode = BlendMode::kSource});
  canvas.DrawRect(Rect::MakeXYWH(0, 0, 300, 300),
                  {.color = Color::CornflowerBlue().WithAlpha(0.75),
                   .blend_mode = BlendMode::kSourceOver});

  std::shared_ptr<ContextSpy> spy = ContextSpy::Make();
  Picture picture = canvas.EndRecordingAsPicture();
  std::shared_ptr<Context> real_context = GetContext();
  std::shared_ptr<ContextMock> mock_context = spy->MakeContext(real_context);
  AiksContext renderer(mock_context, nullptr);
  std::shared_ptr<Image> image = picture.ToImage(renderer, {301, 301});

  ASSERT_EQ(spy->render_passes_.size(), 1llu);
  std::shared_ptr<RenderPass> render_pass = spy->render_passes_[0];
  ASSERT_EQ(render_pass->GetCommands().size(), 2llu);
}

TEST_P(AiksTest, ClipRectElidesNoOpClips) {
  Canvas canvas(Rect::MakeXYWH(0, 0, 100, 100));
  canvas.ClipRect(Rect::MakeXYWH(0, 0, 100, 100));
  canvas.ClipRect(Rect::MakeXYWH(-100, -100, 300, 300));
  canvas.DrawPaint({.color = Color::Red(), .blend_mode = BlendMode::kSource});
  canvas.DrawPaint({.color = Color::CornflowerBlue().WithAlpha(0.75),
                    .blend_mode = BlendMode::kSourceOver});

  Picture picture = canvas.EndRecordingAsPicture();
  auto expected = Color::Red().Blend(Color::CornflowerBlue().WithAlpha(0.75),
                                     BlendMode::kSourceOver);
  ASSERT_EQ(picture.pass->GetClearColor(), expected);

  std::shared_ptr<ContextSpy> spy = ContextSpy::Make();
  std::shared_ptr<Context> real_context = GetContext();
  std::shared_ptr<ContextMock> mock_context = spy->MakeContext(real_context);
  AiksContext renderer(mock_context, nullptr);
  std::shared_ptr<Image> image = picture.ToImage(renderer, {300, 300});

  ASSERT_EQ(spy->render_passes_.size(), 1llu);
  std::shared_ptr<RenderPass> render_pass = spy->render_passes_[0];
  ASSERT_EQ(render_pass->GetCommands().size(), 0llu);
}

TEST_P(AiksTest, ClearColorOptimizationDoesNotApplyForBackdropFilters) {
  Canvas canvas;
  canvas.SaveLayer({}, std::nullopt,
                   ImageFilter::MakeBlur(Sigma(3), Sigma(3),
                                         FilterContents::BlurStyle::kNormal,
                                         Entity::TileMode::kClamp));
  canvas.DrawPaint({.color = Color::Red(), .blend_mode = BlendMode::kSource});
  canvas.DrawPaint({.color = Color::CornflowerBlue().WithAlpha(0.75),
                    .blend_mode = BlendMode::kSourceOver});
  canvas.Restore();

  Picture picture = canvas.EndRecordingAsPicture();

  std::optional<Color> actual_color;
  bool found_subpass = false;
  picture.pass->IterateAllElements([&](EntityPass::Element& element) -> bool {
    if (auto subpass = std::get_if<std::unique_ptr<EntityPass>>(&element)) {
      actual_color = subpass->get()->GetClearColor();
      found_subpass = true;
    }
    // Fail if the first element isn't a subpass.
    return true;
  });

  EXPECT_TRUE(found_subpass);
  EXPECT_FALSE(actual_color.has_value());
}

TEST_P(AiksTest, CollapsedDrawPaintInSubpass) {
  Canvas canvas;
  canvas.DrawPaint(
      {.color = Color::Yellow(), .blend_mode = BlendMode::kSource});
  canvas.SaveLayer({.blend_mode = BlendMode::kMultiply});
  canvas.DrawPaint({.color = Color::CornflowerBlue().WithAlpha(0.75),
                    .blend_mode = BlendMode::kSourceOver});

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CollapsedDrawPaintInSubpassBackdropFilter) {
  // Bug: https://github.com/flutter/flutter/issues/131576
  Canvas canvas;
  canvas.DrawPaint(
      {.color = Color::Yellow(), .blend_mode = BlendMode::kSource});
  canvas.SaveLayer({}, {},
                   ImageFilter::MakeBlur(Sigma(20.0), Sigma(20.0),
                                         FilterContents::BlurStyle::kNormal,
                                         Entity::TileMode::kDecal));
  canvas.DrawPaint(
      {.color = Color::CornflowerBlue(), .blend_mode = BlendMode::kSourceOver});

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, ForegroundBlendSubpassCollapseOptimization) {
  Canvas canvas;

  canvas.SaveLayer({
      .color_filter =
          ColorFilter::MakeBlend(BlendMode::kColorDodge, Color::Red()),
  });

  canvas.Translate({500, 300, 0});
  canvas.Rotate(Radians(2 * kPi / 3));
  canvas.DrawRect(Rect::MakeXYWH(100, 100, 200, 200), {.color = Color::Blue()});

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, ColorMatrixFilterSubpassCollapseOptimization) {
  Canvas canvas;

  canvas.SaveLayer({
      .color_filter =
          ColorFilter::MakeMatrix({.array =
                                       {
                                           -1.0, 0,    0,    1.0, 0,  //
                                           0,    -1.0, 0,    1.0, 0,  //
                                           0,    0,    -1.0, 1.0, 0,  //
                                           1.0,  1.0,  1.0,  1.0, 0   //
                                       }}),
  });

  canvas.Translate({500, 300, 0});
  canvas.Rotate(Radians(2 * kPi / 3));
  canvas.DrawRect(Rect::MakeXYWH(100, 100, 200, 200), {.color = Color::Blue()});

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, LinearToSrgbFilterSubpassCollapseOptimization) {
  Canvas canvas;

  canvas.SaveLayer({
      .color_filter = ColorFilter::MakeLinearToSrgb(),
  });

  canvas.Translate({500, 300, 0});
  canvas.Rotate(Radians(2 * kPi / 3));
  canvas.DrawRect(Rect::MakeXYWH(100, 100, 200, 200), {.color = Color::Blue()});

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, SrgbToLinearFilterSubpassCollapseOptimization) {
  Canvas canvas;

  canvas.SaveLayer({
      .color_filter = ColorFilter::MakeSrgbToLinear(),
  });

  canvas.Translate({500, 300, 0});
  canvas.Rotate(Radians(2 * kPi / 3));
  canvas.DrawRect(Rect::MakeXYWH(100, 100, 200, 200), {.color = Color::Blue()});

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

static Picture BlendModeTest(Vector2 content_scale,
                             BlendMode blend_mode,
                             const std::shared_ptr<Image>& src_image,
                             const std::shared_ptr<Image>& dst_image) {
  Color destination_color = Color::CornflowerBlue().WithAlpha(0.75);
  auto source_colors = std::vector<Color>({Color::White().WithAlpha(0.75),
                                           Color::LimeGreen().WithAlpha(0.75),
                                           Color::Black().WithAlpha(0.75)});

  Canvas canvas;
  canvas.DrawPaint({.color = Color::Black()});
  // TODO(bdero): Why does this cause the left image to double scale on high DPI
  //              displays.
  // canvas.Scale(content_scale);

  //----------------------------------------------------------------------------
  /// 1. Save layer blending (top squares).
  ///

  canvas.Save();
  for (const auto& color : source_colors) {
    canvas.Save();
    {
      canvas.ClipRect(Rect::MakeXYWH(25, 25, 100, 100));
      // Perform the blend in a SaveLayer so that the initial backdrop color is
      // fully transparent black. SourceOver blend the result onto the parent
      // pass.
      canvas.SaveLayer({});
      {
        canvas.DrawPaint({.color = destination_color});
        // Draw the source color in an offscreen pass and blend it to the parent
        // pass.
        canvas.SaveLayer({.blend_mode = blend_mode});
        {  //
          canvas.DrawRect(Rect::MakeXYWH(25, 25, 100, 100), {.color = color});
        }
        canvas.Restore();
      }
      canvas.Restore();
    }
    canvas.Restore();
    canvas.Translate(Vector2(100, 0));
  }
  canvas.RestoreToCount(0);

  //----------------------------------------------------------------------------
  /// 2. CPU blend modes (bottom squares).
  ///

  canvas.Save();
  canvas.Translate({0, 100});
  // Perform the blend in a SaveLayer so that the initial backdrop color is
  // fully transparent black. SourceOver blend the result onto the parent pass.
  canvas.SaveLayer({});
  for (const auto& color : source_colors) {
    // Simply write the CPU blended color to the pass.
    canvas.DrawRect(Rect::MakeXYWH(25, 25, 100, 100),
                    {.color = destination_color.Blend(color, blend_mode),
                     .blend_mode = BlendMode::kSourceOver});
    canvas.Translate(Vector2(100, 0));
  }
  canvas.Restore();
  canvas.Restore();

  //----------------------------------------------------------------------------
  /// 3. Image blending (bottom images).
  ///
  /// Compare these results with the images in the Flutter blend mode
  /// documentation: https://api.flutter.dev/flutter/dart-ui/BlendMode.html
  ///

  canvas.Translate({0, 250});

  // Draw grid behind the images.
  canvas.DrawRect(Rect::MakeLTRB(0, 0, 800, 400),
                  {.color = Color::MakeRGBA8(41, 41, 41, 255)});
  Paint square_paint = {.color = Color::MakeRGBA8(15, 15, 15, 255)};
  for (int y = 0; y < 400 / 8; y++) {
    for (int x = 0; x < 800 / 16; x++) {
      canvas.DrawRect(Rect::MakeXYWH(x * 16 + (y % 2) * 8, y * 8, 8, 8),
                      square_paint);
    }
  }

  // Uploaded image source (left image).
  canvas.Save();
  canvas.SaveLayer({.blend_mode = BlendMode::kSourceOver});
  {
    canvas.DrawImage(dst_image, {0, 0}, {.blend_mode = BlendMode::kSourceOver});
    canvas.DrawImage(src_image, {0, 0}, {.blend_mode = blend_mode});
  }
  canvas.Restore();
  canvas.Restore();

  // Rendered image source (right image).
  canvas.Save();
  canvas.SaveLayer({.blend_mode = BlendMode::kSourceOver});
  {
    canvas.DrawImage(dst_image, {400, 0},
                     {.blend_mode = BlendMode::kSourceOver});
    canvas.SaveLayer({.blend_mode = blend_mode});
    {
      canvas.DrawImage(src_image, {400, 0},
                       {.blend_mode = BlendMode::kSourceOver});
    }
    canvas.Restore();
  }
  canvas.Restore();
  canvas.Restore();

  return canvas.EndRecordingAsPicture();
}

#define BLEND_MODE_TEST(blend_mode)                                          \
  TEST_P(AiksTest, BlendMode##blend_mode) {                                  \
    auto src_image = std::make_shared<Image>(                                \
        CreateTextureForFixture("blend_mode_src.png"));                      \
    auto dst_image = std::make_shared<Image>(                                \
        CreateTextureForFixture("blend_mode_dst.png"));                      \
    OpenPlaygroundHere(BlendModeTest(                                        \
        GetContentScale(), BlendMode::k##blend_mode, src_image, dst_image)); \
  }
IMPELLER_FOR_EACH_BLEND_MODE(BLEND_MODE_TEST)

TEST_P(AiksTest, TranslucentSaveLayerDrawsCorrectly) {
  Canvas canvas;

  canvas.DrawRect(Rect::MakeXYWH(100, 100, 300, 300), {.color = Color::Blue()});

  canvas.SaveLayer({.color = Color::Black().WithAlpha(0.5)});
  canvas.DrawRect(Rect::MakeXYWH(100, 500, 300, 300), {.color = Color::Blue()});
  canvas.Restore();

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, TranslucentSaveLayerWithBlendColorFilterDrawsCorrectly) {
  Canvas canvas;

  canvas.DrawRect(Rect::MakeXYWH(100, 100, 300, 300), {.color = Color::Blue()});

  canvas.SaveLayer({
      .color = Color::Black().WithAlpha(0.5),
      .color_filter =
          ColorFilter::MakeBlend(BlendMode::kDestinationOver, Color::Red()),
  });
  canvas.DrawRect(Rect::MakeXYWH(100, 500, 300, 300), {.color = Color::Blue()});
  canvas.Restore();

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, TranslucentSaveLayerWithBlendImageFilterDrawsCorrectly) {
  Canvas canvas;

  canvas.DrawRect(Rect::MakeXYWH(100, 100, 300, 300), {.color = Color::Blue()});

  canvas.SaveLayer({
      .color = Color::Black().WithAlpha(0.5),
      .image_filter = ImageFilter::MakeFromColorFilter(
          *ColorFilter::MakeBlend(BlendMode::kDestinationOver, Color::Red())),
  });

  canvas.DrawRect(Rect::MakeXYWH(100, 500, 300, 300), {.color = Color::Blue()});
  canvas.Restore();

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, TranslucentSaveLayerWithColorAndImageFilterDrawsCorrectly) {
  Canvas canvas;

  canvas.DrawRect(Rect::MakeXYWH(100, 100, 300, 300), {.color = Color::Blue()});

  canvas.SaveLayer({
      .color = Color::Black().WithAlpha(0.5),
      .color_filter =
          ColorFilter::MakeBlend(BlendMode::kDestinationOver, Color::Red()),
  });

  canvas.DrawRect(Rect::MakeXYWH(100, 500, 300, 300), {.color = Color::Blue()});
  canvas.Restore();

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, ImageFilteredSaveLayerWithUnboundedContents) {
  Canvas canvas;
  canvas.Scale(GetContentScale());

  auto test = [&canvas](const std::shared_ptr<ImageFilter>& filter) {
    auto DrawLine = [&canvas](const Point& p0, const Point& p1,
                              const Paint& p) {
      auto path = PathBuilder{}
                      .AddLine(p0, p1)
                      .SetConvexity(Convexity::kConvex)
                      .TakePath();
      Paint paint = p;
      paint.style = Paint::Style::kStroke;
      canvas.DrawPath(path, paint);
    };
    // Registration marks for the edge of the SaveLayer
    DrawLine(Point(75, 100), Point(225, 100), {.color = Color::White()});
    DrawLine(Point(75, 200), Point(225, 200), {.color = Color::White()});
    DrawLine(Point(100, 75), Point(100, 225), {.color = Color::White()});
    DrawLine(Point(200, 75), Point(200, 225), {.color = Color::White()});

    canvas.SaveLayer({.image_filter = filter},
                     Rect::MakeLTRB(100, 100, 200, 200));
    {
      // DrawPaint to verify correct behavior when the contents are unbounded.
      canvas.DrawPaint({.color = Color::Yellow()});

      // Contrasting rectangle to see interior blurring
      canvas.DrawRect(Rect::MakeLTRB(125, 125, 175, 175),
                      {.color = Color::Blue()});
    }
    canvas.Restore();
  };

  test(ImageFilter::MakeBlur(Sigma{10.0}, Sigma{10.0},
                             FilterContents::BlurStyle::kNormal,
                             Entity::TileMode::kDecal));

  canvas.Translate({200.0, 0.0});

  test(ImageFilter::MakeDilate(Radius{10.0}, Radius{10.0}));

  canvas.Translate({200.0, 0.0});

  test(ImageFilter::MakeErode(Radius{10.0}, Radius{10.0}));

  canvas.Translate({-400.0, 200.0});

  auto rotate_filter =
      ImageFilter::MakeMatrix(Matrix::MakeTranslation({150, 150}) *
                                  Matrix::MakeRotationZ(Degrees{10.0}) *
                                  Matrix::MakeTranslation({-150, -150}),
                              SamplerDescriptor{});
  test(rotate_filter);

  canvas.Translate({200.0, 0.0});

  auto rgb_swap_filter = ImageFilter::MakeFromColorFilter(
      *ColorFilter::MakeMatrix({.array = {
                                    0, 1, 0, 0, 0,  //
                                    0, 0, 1, 0, 0,  //
                                    1, 0, 0, 0, 0,  //
                                    0, 0, 0, 1, 0   //
                                }}));
  test(rgb_swap_filter);

  canvas.Translate({200.0, 0.0});

  test(ImageFilter::MakeCompose(*rotate_filter, *rgb_swap_filter));

  canvas.Translate({-400.0, 200.0});

  test(ImageFilter::MakeLocalMatrix(Matrix::MakeTranslation({25.0, 25.0}),
                                    *rotate_filter));

  canvas.Translate({200.0, 0.0});

  test(ImageFilter::MakeLocalMatrix(Matrix::MakeTranslation({25.0, 25.0}),
                                    *rgb_swap_filter));

  canvas.Translate({200.0, 0.0});

  test(ImageFilter::MakeLocalMatrix(
      Matrix::MakeTranslation({25.0, 25.0}),
      *ImageFilter::MakeCompose(*rotate_filter, *rgb_swap_filter)));

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, ImageFilteredUnboundedSaveLayerWithUnboundedContents) {
  Canvas canvas;
  canvas.Scale(GetContentScale());

  auto blur_filter = ImageFilter::MakeBlur(Sigma{10.0}, Sigma{10.0},
                                           FilterContents::BlurStyle::kNormal,
                                           Entity::TileMode::kDecal);

  canvas.SaveLayer({.image_filter = blur_filter}, std::nullopt);
  {
    // DrawPaint to verify correct behavior when the contents are unbounded.
    canvas.DrawPaint({.color = Color::Yellow()});

    // Contrasting rectangle to see interior blurring
    canvas.DrawRect(Rect::MakeLTRB(125, 125, 175, 175),
                    {.color = Color::Blue()});
  }
  canvas.Restore();

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, TranslucentSaveLayerImageDrawsCorrectly) {
  Canvas canvas;

  auto image = std::make_shared<Image>(CreateTextureForFixture("airplane.jpg"));
  canvas.DrawImage(image, {100, 100}, {});

  canvas.SaveLayer({.color = Color::Black().WithAlpha(0.5)});
  canvas.DrawImage(image, {100, 500}, {});
  canvas.Restore();

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, TranslucentSaveLayerWithColorMatrixColorFilterDrawsCorrectly) {
  Canvas canvas;

  auto image = std::make_shared<Image>(CreateTextureForFixture("airplane.jpg"));
  canvas.DrawImage(image, {100, 100}, {});

  canvas.SaveLayer({
      .color = Color::Black().WithAlpha(0.5),
      .color_filter = ColorFilter::MakeMatrix({.array =
                                                   {
                                                       1, 0, 0, 0, 0,  //
                                                       0, 1, 0, 0, 0,  //
                                                       0, 0, 1, 0, 0,  //
                                                       0, 0, 0, 2, 0   //
                                                   }}),
  });
  canvas.DrawImage(image, {100, 500}, {});
  canvas.Restore();

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, TranslucentSaveLayerWithColorMatrixImageFilterDrawsCorrectly) {
  Canvas canvas;

  auto image = std::make_shared<Image>(CreateTextureForFixture("airplane.jpg"));
  canvas.DrawImage(image, {100, 100}, {});

  canvas.SaveLayer({
      .color = Color::Black().WithAlpha(0.5),
      .image_filter = ImageFilter::MakeFromColorFilter(
          *ColorFilter::MakeMatrix({.array =
                                        {
                                            1, 0, 0, 0, 0,  //
                                            0, 1, 0, 0, 0,  //
                                            0, 0, 1, 0, 0,  //
                                            0, 0, 0, 2, 0   //
                                        }})),
  });
  canvas.DrawImage(image, {100, 500}, {});
  canvas.Restore();

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest,
       TranslucentSaveLayerWithColorFilterAndImageFilterDrawsCorrectly) {
  Canvas canvas;

  auto image = std::make_shared<Image>(CreateTextureForFixture("airplane.jpg"));
  canvas.DrawImage(image, {100, 100}, {});

  canvas.SaveLayer({
      .color = Color::Black().WithAlpha(0.5),
      .image_filter = ImageFilter::MakeFromColorFilter(
          *ColorFilter::MakeMatrix({.array =
                                        {
                                            1, 0,   0, 0,   0,  //
                                            0, 1,   0, 0,   0,  //
                                            0, 0.2, 1, 0,   0,  //
                                            0, 0,   0, 0.5, 0   //
                                        }})),
      .color_filter =
          ColorFilter::MakeBlend(BlendMode::kModulate, Color::Green()),
  });
  canvas.DrawImage(image, {100, 500}, {});
  canvas.Restore();

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, TranslucentSaveLayerWithAdvancedBlendModeDrawsCorrectly) {
  Canvas canvas;
  canvas.DrawRect(Rect::MakeXYWH(0, 0, 400, 400), {.color = Color::Red()});
  canvas.SaveLayer({
      .color = Color::Black().WithAlpha(0.5),
      .blend_mode = BlendMode::kLighten,
  });
  canvas.DrawCircle({200, 200}, 100, {.color = Color::Green()});
  canvas.Restore();
  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

/// This is a regression check for https://github.com/flutter/engine/pull/41129
/// The entire screen is green if successful. If failing, no frames will render,
/// or the entire screen will be transparent black.
TEST_P(AiksTest, CanRenderTinyOverlappingSubpasses) {
  Canvas canvas;
  canvas.DrawPaint({.color = Color::Red()});

  // Draw two overlapping subpixel circles.
  canvas.SaveLayer({});
  canvas.DrawCircle({100, 100}, 0.1, {.color = Color::Yellow()});
  canvas.Restore();
  canvas.SaveLayer({});
  canvas.DrawCircle({100, 100}, 0.1, {.color = Color::Yellow()});
  canvas.Restore();

  canvas.DrawPaint({.color = Color::Green()});

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

/// Tests that the debug checkerboard displays for offscreen textures when
/// enabled. Most of the complexity here is just to future proof by making pass
/// collapsing hard.
TEST_P(AiksTest, CanRenderOffscreenCheckerboard) {
  Canvas canvas;
  canvas.debug_options.offscreen_texture_checkerboard = true;

  canvas.DrawPaint({.color = Color::AntiqueWhite()});
  canvas.DrawCircle({400, 300}, 200,
                    {.color = Color::CornflowerBlue().WithAlpha(0.75)});

  canvas.SaveLayer({.blend_mode = BlendMode::kMultiply});
  {
    canvas.DrawCircle({500, 400}, 200,
                      {.color = Color::DarkBlue().WithAlpha(0.75)});
    canvas.DrawCircle({550, 450}, 200,
                      {.color = Color::LightCoral().WithAlpha(0.75),
                       .blend_mode = BlendMode::kLuminosity});
  }
  canvas.Restore();

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, OpaqueEntitiesGetCoercedToSource) {
  Canvas canvas;
  canvas.Scale(Vector2(1.618, 1.618));
  canvas.DrawCircle(Point(), 10,
                    {
                        .color = Color::CornflowerBlue(),
                        .blend_mode = BlendMode::kSourceOver,
                    });
  Picture picture = canvas.EndRecordingAsPicture();

  // Extract the SolidColorSource.
  // Entity entity;
  std::vector<Entity> entity;
  std::shared_ptr<SolidColorContents> contents;
  picture.pass->IterateAllEntities([e = &entity, &contents](Entity& entity) {
    if (ScalarNearlyEqual(entity.GetTransform().GetScale().x, 1.618f)) {
      contents =
          std::static_pointer_cast<SolidColorContents>(entity.GetContents());
      e->emplace_back(entity.Clone());
      return false;
    }
    return true;
  });

  ASSERT_TRUE(entity.size() >= 1);
  ASSERT_TRUE(contents->IsOpaque());
  ASSERT_EQ(entity[0].GetBlendMode(), BlendMode::kSource);
}

TEST_P(AiksTest, CanRenderDestructiveSaveLayer) {
  Canvas canvas;

  canvas.DrawPaint({.color = Color::Red()});
  // Draw an empty savelayer with a destructive blend mode, which will replace
  // the entire red screen with fully transparent black, except for the green
  // circle drawn within the layer.
  canvas.SaveLayer({.blend_mode = BlendMode::kSource});
  canvas.DrawCircle({300, 300}, 100, {.color = Color::Green()});
  canvas.Restore();

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CanRenderMaskBlurHugeSigma) {
  Canvas canvas;
  canvas.DrawCircle({400, 400}, 300,
                    {.color = Color::Green(),
                     .mask_blur_descriptor = Paint::MaskBlurDescriptor{
                         .style = FilterContents::BlurStyle::kNormal,
                         .sigma = Sigma(99999),
                     }});
  canvas.Restore();

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CanRenderBackdropBlurInteractive) {
  auto callback = [&](AiksContext& renderer) -> std::optional<Picture> {
    auto [a, b] = IMPELLER_PLAYGROUND_LINE(Point(50, 50), Point(300, 200), 30,
                                           Color::White(), Color::White());

    Canvas canvas;
    canvas.DrawCircle({100, 100}, 50, {.color = Color::CornflowerBlue()});
    canvas.DrawCircle({300, 200}, 100, {.color = Color::GreenYellow()});
    canvas.DrawCircle({140, 170}, 75, {.color = Color::DarkMagenta()});
    canvas.DrawCircle({180, 120}, 100, {.color = Color::OrangeRed()});
    canvas.ClipRRect(Rect::MakeLTRB(a.x, a.y, b.x, b.y), {20, 20});
    canvas.SaveLayer({.blend_mode = BlendMode::kSource}, std::nullopt,
                     ImageFilter::MakeBlur(Sigma(20.0), Sigma(20.0),
                                           FilterContents::BlurStyle::kNormal,
                                           Entity::TileMode::kClamp));
    canvas.Restore();

    return canvas.EndRecordingAsPicture();
  };

  ASSERT_TRUE(OpenPlaygroundHere(callback));
}

TEST_P(AiksTest, CanRenderBackdropBlur) {
  Canvas canvas;
  canvas.DrawCircle({100, 100}, 50, {.color = Color::CornflowerBlue()});
  canvas.DrawCircle({300, 200}, 100, {.color = Color::GreenYellow()});
  canvas.DrawCircle({140, 170}, 75, {.color = Color::DarkMagenta()});
  canvas.DrawCircle({180, 120}, 100, {.color = Color::OrangeRed()});
  canvas.ClipRRect(Rect::MakeLTRB(75, 50, 375, 275), {20, 20});
  canvas.SaveLayer({.blend_mode = BlendMode::kSource}, std::nullopt,
                   ImageFilter::MakeBlur(Sigma(30.0), Sigma(30.0),
                                         FilterContents::BlurStyle::kNormal,
                                         Entity::TileMode::kClamp));
  canvas.Restore();

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CanRenderBackdropBlurHugeSigma) {
  Canvas canvas;
  canvas.DrawCircle({400, 400}, 300, {.color = Color::Green()});
  canvas.SaveLayer({.blend_mode = BlendMode::kSource}, std::nullopt,
                   ImageFilter::MakeBlur(Sigma(999999), Sigma(999999),
                                         FilterContents::BlurStyle::kNormal,
                                         Entity::TileMode::kClamp));
  canvas.Restore();

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CanRenderClippedBlur) {
  Canvas canvas;
  canvas.ClipRect(Rect::MakeXYWH(100, 150, 400, 400));
  canvas.DrawCircle(
      {400, 400}, 200,
      {
          .color = Color::Green(),
          .image_filter = ImageFilter::MakeBlur(
              Sigma(20.0), Sigma(20.0), FilterContents::BlurStyle::kNormal,
              Entity::TileMode::kDecal),
      });
  canvas.Restore();

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CanRenderForegroundBlendWithMaskBlur) {
  // This case triggers the ForegroundPorterDuffBlend path. The color filter
  // should apply to the color only, and respect the alpha mask.
  Canvas canvas;
  canvas.ClipRect(Rect::MakeXYWH(100, 150, 400, 400));
  canvas.DrawCircle({400, 400}, 200,
                    {
                        .color = Color::White(),
                        .color_filter = ColorFilter::MakeBlend(
                            BlendMode::kSource, Color::Green()),
                        .mask_blur_descriptor =
                            Paint::MaskBlurDescriptor{
                                .style = FilterContents::BlurStyle::kNormal,
                                .sigma = Radius(20),
                            },
                    });
  canvas.Restore();

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CanRenderForegroundAdvancedBlendWithMaskBlur) {
  // This case triggers the ForegroundAdvancedBlend path. The color filter
  // should apply to the color only, and respect the alpha mask.
  Canvas canvas;
  canvas.ClipRect(Rect::MakeXYWH(100, 150, 400, 400));
  canvas.DrawCircle({400, 400}, 200,
                    {
                        .color = Color::Grey(),
                        .color_filter = ColorFilter::MakeBlend(
                            BlendMode::kColor, Color::Green()),
                        .mask_blur_descriptor =
                            Paint::MaskBlurDescriptor{
                                .style = FilterContents::BlurStyle::kNormal,
                                .sigma = Radius(20),
                            },
                    });
  canvas.Restore();

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

// Regression test for https://github.com/flutter/flutter/issues/126701 .
TEST_P(AiksTest, CanRenderClippedRuntimeEffects) {
  auto runtime_stages =
      OpenAssetAsRuntimeStage("runtime_stage_example.frag.iplr");

  auto runtime_stage =
      runtime_stages[PlaygroundBackendToRuntimeStageBackend(GetBackend())];
  ASSERT_TRUE(runtime_stage);
  ASSERT_TRUE(runtime_stage->IsDirty());

  struct FragUniforms {
    Vector2 iResolution;
    Scalar iTime;
  } frag_uniforms = {.iResolution = Vector2(400, 400), .iTime = 100.0};
  auto uniform_data = std::make_shared<std::vector<uint8_t>>();
  uniform_data->resize(sizeof(FragUniforms));
  memcpy(uniform_data->data(), &frag_uniforms, sizeof(FragUniforms));

  std::vector<RuntimeEffectContents::TextureInput> texture_inputs;

  Paint paint;
  paint.color_source = ColorSource::MakeRuntimeEffect(
      runtime_stage, uniform_data, texture_inputs);

  Canvas canvas;
  canvas.Save();
  canvas.ClipRRect(Rect::MakeXYWH(0, 0, 400, 400), {10.0, 10.0},
                   Entity::ClipOperation::kIntersect);
  canvas.DrawRect(Rect::MakeXYWH(0, 0, 400, 400), paint);
  canvas.Restore();

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, DrawPaintTransformsBounds) {
  auto runtime_stages = OpenAssetAsRuntimeStage("gradient.frag.iplr");
  auto runtime_stage =
      runtime_stages[PlaygroundBackendToRuntimeStageBackend(GetBackend())];
  ASSERT_TRUE(runtime_stage);
  ASSERT_TRUE(runtime_stage->IsDirty());

  struct FragUniforms {
    Size size;
  } frag_uniforms = {.size = Size::MakeWH(400, 400)};
  auto uniform_data = std::make_shared<std::vector<uint8_t>>();
  uniform_data->resize(sizeof(FragUniforms));
  memcpy(uniform_data->data(), &frag_uniforms, sizeof(FragUniforms));

  std::vector<RuntimeEffectContents::TextureInput> texture_inputs;

  Paint paint;
  paint.color_source = ColorSource::MakeRuntimeEffect(
      runtime_stage, uniform_data, texture_inputs);

  Canvas canvas;
  canvas.Save();
  canvas.Scale(GetContentScale());
  canvas.DrawPaint(paint);
  canvas.Restore();

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CanDrawPoints) {
  std::vector<Point> points = {
      {0, 0},      //
      {100, 100},  //
      {100, 0},    //
      {0, 100},    //
      {0, 0},      //
      {48, 48},    //
      {52, 52},    //
  };
  std::vector<PointStyle> caps = {
      PointStyle::kRound,
      PointStyle::kSquare,
  };
  Paint paint;
  paint.color = Color::Yellow().WithAlpha(0.5);

  Paint background;
  background.color = Color::Black();

  Canvas canvas;
  canvas.DrawPaint(background);
  canvas.Translate({200, 200});
  canvas.DrawPoints(points, 10, paint, PointStyle::kRound);
  canvas.Translate({150, 0});
  canvas.DrawPoints(points, 10, paint, PointStyle::kSquare);

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

// Regression test for https://github.com/flutter/flutter/issues/127374.
TEST_P(AiksTest, DrawAtlasWithColorAdvancedAndTransform) {
  // Draws the image as four squares stiched together.
  auto atlas = CreateTextureForFixture("bay_bridge.jpg");
  auto size = atlas->GetSize();
  auto image = std::make_shared<Image>(atlas);
  // Divide image into four quadrants.
  Scalar half_width = size.width / 2;
  Scalar half_height = size.height / 2;
  std::vector<Rect> texture_coordinates = {
      Rect::MakeLTRB(0, 0, half_width, half_height),
      Rect::MakeLTRB(half_width, 0, size.width, half_height),
      Rect::MakeLTRB(0, half_height, half_width, size.height),
      Rect::MakeLTRB(half_width, half_height, size.width, size.height)};
  // Position quadrants adjacent to eachother.
  std::vector<Matrix> transforms = {
      Matrix::MakeTranslation({0, 0, 0}),
      Matrix::MakeTranslation({half_width, 0, 0}),
      Matrix::MakeTranslation({0, half_height, 0}),
      Matrix::MakeTranslation({half_width, half_height, 0})};
  std::vector<Color> colors = {Color::Red(), Color::Green(), Color::Blue(),
                               Color::Yellow()};

  Paint paint;

  Canvas canvas;
  canvas.Scale({0.25, 0.25, 1.0});
  canvas.DrawAtlas(image, transforms, texture_coordinates, colors,
                   BlendMode::kModulate, {}, std::nullopt, paint);

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

// Regression test for https://github.com/flutter/flutter/issues/127374.
TEST_P(AiksTest, DrawAtlasAdvancedAndTransform) {
  // Draws the image as four squares stiched together.
  auto atlas = CreateTextureForFixture("bay_bridge.jpg");
  auto size = atlas->GetSize();
  auto image = std::make_shared<Image>(atlas);
  // Divide image into four quadrants.
  Scalar half_width = size.width / 2;
  Scalar half_height = size.height / 2;
  std::vector<Rect> texture_coordinates = {
      Rect::MakeLTRB(0, 0, half_width, half_height),
      Rect::MakeLTRB(half_width, 0, size.width, half_height),
      Rect::MakeLTRB(0, half_height, half_width, size.height),
      Rect::MakeLTRB(half_width, half_height, size.width, size.height)};
  // Position quadrants adjacent to eachother.
  std::vector<Matrix> transforms = {
      Matrix::MakeTranslation({0, 0, 0}),
      Matrix::MakeTranslation({half_width, 0, 0}),
      Matrix::MakeTranslation({0, half_height, 0}),
      Matrix::MakeTranslation({half_width, half_height, 0})};

  Paint paint;

  Canvas canvas;
  canvas.Scale({0.25, 0.25, 1.0});
  canvas.DrawAtlas(image, transforms, texture_coordinates, {},
                   BlendMode::kModulate, {}, std::nullopt, paint);

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CanDrawPointsWithTextureMap) {
  auto texture = CreateTextureForFixture("table_mountain_nx.png",
                                         /*enable_mipmapping=*/true);

  std::vector<Point> points = {
      {0, 0},      //
      {100, 100},  //
      {100, 0},    //
      {0, 100},    //
      {0, 0},      //
      {48, 48},    //
      {52, 52},    //
  };
  std::vector<PointStyle> caps = {
      PointStyle::kRound,
      PointStyle::kSquare,
  };
  Paint paint;
  paint.color_source = ColorSource::MakeImage(texture, Entity::TileMode::kClamp,
                                              Entity::TileMode::kClamp, {}, {});

  Canvas canvas;
  canvas.Translate({200, 200});
  canvas.DrawPoints(points, 100, paint, PointStyle::kRound);
  canvas.Translate({150, 0});
  canvas.DrawPoints(points, 100, paint, PointStyle::kSquare);

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

// This currently renders solid blue, as the support for text color sources was
// moved into DLDispatching. Path data requires the SkTextBlobs which are not
// used in impeller::TextFrames.
TEST_P(AiksTest, TextForegroundShaderWithTransform) {
  auto mapping = flutter::testing::OpenFixtureAsSkData("Roboto-Regular.ttf");
  ASSERT_NE(mapping, nullptr);

  Scalar font_size = 100;
  sk_sp<SkFontMgr> font_mgr = txt::GetDefaultFontManager();
  SkFont sk_font(font_mgr->makeFromData(mapping), font_size);

  Paint text_paint;
  text_paint.color = Color::Blue();

  std::vector<Color> colors = {Color{0.9568, 0.2627, 0.2118, 1.0},
                               Color{0.1294, 0.5882, 0.9529, 1.0}};
  std::vector<Scalar> stops = {
      0.0,
      1.0,
  };
  text_paint.color_source = ColorSource::MakeLinearGradient(
      {0, 0}, {100, 100}, std::move(colors), std::move(stops),
      Entity::TileMode::kRepeat, {});

  Canvas canvas;
  canvas.Translate({100, 100});
  canvas.Rotate(Radians(kPi / 4));

  auto blob = SkTextBlob::MakeFromString("Hello", sk_font);
  ASSERT_NE(blob, nullptr);
  auto frame = MakeTextFrameFromTextBlobSkia(blob);
  canvas.DrawTextFrame(frame, Point(), text_paint);

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, MatrixSaveLayerFilter) {
  Canvas canvas;
  canvas.DrawPaint({.color = Color::Black()});
  canvas.SaveLayer({}, std::nullopt);
  {
    canvas.DrawCircle(Point(200, 200), 100,
                      {.color = Color::Green().WithAlpha(0.5),
                       .blend_mode = BlendMode::kPlus});
    // Should render a second circle, centered on the bottom-right-most edge of
    // the circle.
    canvas.SaveLayer({.image_filter = ImageFilter::MakeMatrix(
                          Matrix::MakeTranslation(Vector2(1, 1) *
                                                  (200 + 100 * k1OverSqrt2)) *
                              Matrix::MakeScale(Vector2(1, 1) * 0.5) *
                              Matrix::MakeTranslation(Vector2(-200, -200)),
                          SamplerDescriptor{})},
                     std::nullopt);
    canvas.DrawCircle(Point(200, 200), 100,
                      {.color = Color::Green().WithAlpha(0.5),
                       .blend_mode = BlendMode::kPlus});
    canvas.Restore();
  }
  canvas.Restore();

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, MatrixBackdropFilter) {
  Canvas canvas;
  canvas.DrawPaint({.color = Color::Black()});
  canvas.SaveLayer({}, std::nullopt);
  {
    canvas.DrawCircle(Point(200, 200), 100,
                      {.color = Color::Green().WithAlpha(0.5),
                       .blend_mode = BlendMode::kPlus});
    // Should render a second circle, centered on the bottom-right-most edge of
    // the circle.
    canvas.SaveLayer(
        {}, std::nullopt,
        ImageFilter::MakeMatrix(
            Matrix::MakeTranslation(Vector2(1, 1) * (100 + 100 * k1OverSqrt2)) *
                Matrix::MakeScale(Vector2(1, 1) * 0.5) *
                Matrix::MakeTranslation(Vector2(-100, -100)),
            SamplerDescriptor{}));
    canvas.Restore();
  }
  canvas.Restore();

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, SolidColorApplyColorFilter) {
  auto contents = SolidColorContents();
  contents.SetColor(Color::CornflowerBlue().WithAlpha(0.75));
  auto result = contents.ApplyColorFilter([](const Color& color) {
    return color.Blend(Color::LimeGreen().WithAlpha(0.75), BlendMode::kScreen);
  });
  ASSERT_TRUE(result);
  ASSERT_COLOR_NEAR(contents.GetColor(),
                    Color(0.424452, 0.828743, 0.79105, 0.9375));
}

TEST_P(AiksTest, DrawScaledTextWithPerspectiveNoSaveLayer) {
  Canvas canvas;
  // clang-format off
  canvas.Transform(Matrix(
       2.000000,       0.000000,   0.000000,  0.000000,
       1.445767,       2.637070,  -0.507928,  0.001524,
      -2.451887,      -0.534662,   0.861399, -0.002584,
    1063.481934,    1025.951416, -48.300270,  1.144901
  ));
  // clang-format on

  ASSERT_TRUE(RenderTextInCanvasSkia(GetContext(), canvas, "Hello world",
                                     "Roboto-Regular.ttf"));

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, DrawScaledTextWithPerspectiveSaveLayer) {
  Canvas canvas;
  Paint save_paint;
  canvas.SaveLayer(save_paint);
  // clang-format off
  canvas.Transform(Matrix(
       2.000000,       0.000000,   0.000000,  0.000000,
       1.445767,       2.637070,  -0.507928,  0.001524,
      -2.451887,      -0.534662,   0.861399, -0.002584,
    1063.481934,    1025.951416, -48.300270,  1.144901
  ));
  // clang-format on

  ASSERT_TRUE(RenderTextInCanvasSkia(GetContext(), canvas, "Hello world",
                                     "Roboto-Regular.ttf"));
}

TEST_P(AiksTest, PipelineBlendSingleParameter) {
  Canvas canvas;

  // Should render a green square in the middle of a blue circle.
  canvas.SaveLayer({});
  {
    canvas.Translate(Point(100, 100));
    canvas.DrawCircle(Point(200, 200), 200, {.color = Color::Blue()});
    canvas.ClipRect(Rect::MakeXYWH(100, 100, 200, 200));
    canvas.DrawCircle(Point(200, 200), 200,
                      {
                          .color = Color::Green(),
                          .blend_mode = BlendMode::kSourceOver,
                          .image_filter = ImageFilter::MakeFromColorFilter(
                              *ColorFilter::MakeBlend(BlendMode::kDestination,
                                                      Color::White())),
                      });
    canvas.Restore();
  }

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, ClippedBlurFilterRendersCorrectlyInteractive) {
  auto callback = [&](AiksContext& renderer) -> std::optional<Picture> {
    auto point = IMPELLER_PLAYGROUND_POINT(Point(400, 400), 20, Color::Green());

    Canvas canvas;
    canvas.Translate(point - Point(400, 400));
    Paint paint;
    paint.mask_blur_descriptor = Paint::MaskBlurDescriptor{
        .style = FilterContents::BlurStyle::kNormal,
        .sigma = Radius{120 * 3},
    };
    paint.color = Color::Red();
    PathBuilder builder{};
    builder.AddRect(Rect::MakeLTRB(0, 0, 800, 800));
    canvas.DrawPath(builder.TakePath(), paint);
    return canvas.EndRecordingAsPicture();
  };
  ASSERT_TRUE(OpenPlaygroundHere(callback));
}

TEST_P(AiksTest, ClippedBlurFilterRendersCorrectly) {
  Canvas canvas;
  canvas.Translate(Point(0, -400));
  Paint paint;
  paint.mask_blur_descriptor = Paint::MaskBlurDescriptor{
      .style = FilterContents::BlurStyle::kNormal,
      .sigma = Radius{120 * 3},
  };
  paint.color = Color::Red();
  PathBuilder builder{};
  builder.AddRect(Rect::MakeLTRB(0, 0, 800, 800));
  canvas.DrawPath(builder.TakePath(), paint);
  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CaptureContext) {
  auto capture_context = CaptureContext::MakeAllowlist({"TestDocument"});

  auto callback = [&](AiksContext& renderer) -> std::optional<Picture> {
    Canvas canvas;

    capture_context.Rewind();
    auto document = capture_context.GetDocument("TestDocument");

    auto color = document.AddColor("Background color", Color::CornflowerBlue());
    canvas.DrawPaint({.color = color});

    ImGui::Begin("TestDocument", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    document.GetElement()->properties.Iterate([](CaptureProperty& property) {
      property.Invoke({.color = [](CaptureColorProperty& p) {
        ImGui::ColorEdit4(p.label.c_str(), reinterpret_cast<float*>(&p.value));
      }});
    });
    ImGui::End();

    return canvas.EndRecordingAsPicture();
  };
  OpenPlaygroundHere(callback);
}

TEST_P(AiksTest, CaptureInactivatedByDefault) {
  ASSERT_FALSE(GetContext()->capture.IsActive());
}

// Regression test for https://github.com/flutter/flutter/issues/134678.
TEST_P(AiksTest, ReleasesTextureOnTeardown) {
  auto context = MakeContext();
  std::weak_ptr<Texture> weak_texture;

  {
    auto texture = CreateTextureForFixture("table_mountain_nx.png");

    Canvas canvas;
    canvas.Scale(GetContentScale());
    canvas.Translate({100.0f, 100.0f, 0});

    Paint paint;
    paint.color_source = ColorSource::MakeImage(
        texture, Entity::TileMode::kClamp, Entity::TileMode::kClamp, {}, {});
    canvas.DrawRect(Rect::MakeXYWH(0, 0, 600, 600), paint);

    ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
  }

  // See https://github.com/flutter/flutter/issues/134751.
  //
  // If the fence waiter was working this may not be released by the end of the
  // scope above. Adding a manual shutdown so that future changes to the fence
  // waiter will not flake this test.
  context->Shutdown();

  // The texture should be released by now.
  ASSERT_TRUE(weak_texture.expired()) << "When the texture is no longer in use "
                                         "by the backend, it should be "
                                         "released.";
}

// Regression test for https://github.com/flutter/flutter/issues/135441 .
TEST_P(AiksTest, VerticesGeometryUVPositionData) {
  Canvas canvas;
  Paint paint;
  auto texture = CreateTextureForFixture("table_mountain_nx.png");

  paint.color_source = ColorSource::MakeImage(texture, Entity::TileMode::kClamp,
                                              Entity::TileMode::kClamp, {}, {});

  auto vertices = {Point(0, 0), Point(texture->GetSize().width, 0),
                   Point(0, texture->GetSize().height)};
  std::vector<uint16_t> indices = {0u, 1u, 2u};
  std::vector<Point> texture_coordinates = {};
  std::vector<Color> vertex_colors = {};
  auto geometry = std::make_shared<VerticesGeometry>(
      vertices, indices, texture_coordinates, vertex_colors,
      Rect::MakeLTRB(0, 0, 1, 1), VerticesGeometry::VertexMode::kTriangleStrip);

  canvas.DrawVertices(geometry, BlendMode::kSourceOver, paint);
  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

// Regression test for https://github.com/flutter/flutter/issues/135441 .
TEST_P(AiksTest, VerticesGeometryUVPositionDataWithTranslate) {
  Canvas canvas;
  Paint paint;
  auto texture = CreateTextureForFixture("table_mountain_nx.png");

  paint.color_source = ColorSource::MakeImage(
      texture, Entity::TileMode::kClamp, Entity::TileMode::kClamp, {},
      Matrix::MakeTranslation({100.0, 100.0}));

  auto vertices = {Point(0, 0), Point(texture->GetSize().width, 0),
                   Point(0, texture->GetSize().height)};
  std::vector<uint16_t> indices = {0u, 1u, 2u};
  std::vector<Point> texture_coordinates = {};
  std::vector<Color> vertex_colors = {};
  auto geometry = std::make_shared<VerticesGeometry>(
      vertices, indices, texture_coordinates, vertex_colors,
      Rect::MakeLTRB(0, 0, 1, 1), VerticesGeometry::VertexMode::kTriangleStrip);

  canvas.DrawVertices(geometry, BlendMode::kSourceOver, paint);
  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, ClearBlendWithBlur) {
  Canvas canvas;
  Paint white;
  white.color = Color::Blue();
  canvas.DrawRect(Rect::MakeXYWH(0, 0, 600.0, 600.0), white);

  Paint clear;
  clear.blend_mode = BlendMode::kClear;
  clear.mask_blur_descriptor = Paint::MaskBlurDescriptor{
      .style = FilterContents::BlurStyle::kNormal,
      .sigma = Sigma(20),
  };

  canvas.DrawCircle(Point::MakeXY(300.0, 300.0), 200.0, clear);

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, ClearBlend) {
  Canvas canvas;
  Paint white;
  white.color = Color::Blue();
  canvas.DrawRect(Rect::MakeXYWH(0, 0, 600.0, 600.0), white);

  Paint clear;
  clear.blend_mode = BlendMode::kClear;

  canvas.DrawCircle(Point::MakeXY(300.0, 300.0), 200.0, clear);
}

TEST_P(AiksTest, MatrixImageFilterMagnify) {
  Canvas canvas;
  canvas.Scale(GetContentScale());
  auto image = std::make_shared<Image>(CreateTextureForFixture("airplane.jpg"));
  canvas.Translate({600, -200});
  canvas.SaveLayer({
      .image_filter = std::make_shared<MatrixImageFilter>(
          Matrix::MakeScale({2, 2, 2}), SamplerDescriptor{}),
  });
  canvas.DrawImage(image, {0, 0},
                   Paint{.color = Color::White().WithAlpha(0.5)});
  canvas.Restore();

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

// Render a white circle at the top left corner of the screen.
TEST_P(AiksTest, MatrixImageFilterDoesntCullWhenTranslatedFromOffscreen) {
  Canvas canvas;
  canvas.Scale(GetContentScale());
  canvas.Translate({100, 100});
  // Draw a circle in a SaveLayer at -300, but move it back on-screen with a
  // +300 translation applied by a SaveLayer image filter.
  canvas.SaveLayer({
      .image_filter = std::make_shared<MatrixImageFilter>(
          Matrix::MakeTranslation({300, 0}), SamplerDescriptor{}),
  });
  canvas.DrawCircle({-300, 0}, 100, {.color = Color::Green()});
  canvas.Restore();

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

// Render a white circle at the top left corner of the screen.
TEST_P(AiksTest,
       MatrixImageFilterDoesntCullWhenScaledAndTranslatedFromOffscreen) {
  Canvas canvas;
  canvas.Scale(GetContentScale());
  canvas.Translate({100, 100});
  // Draw a circle in a SaveLayer at -300, but move it back on-screen with a
  // +300 translation applied by a SaveLayer image filter.
  canvas.SaveLayer({
      .image_filter = std::make_shared<MatrixImageFilter>(
          Matrix::MakeTranslation({300, 0}) * Matrix::MakeScale({2, 2, 2}),
          SamplerDescriptor{}),
  });
  canvas.DrawCircle({-150, 0}, 50, {.color = Color::Green()});
  canvas.Restore();

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

// This should be solid red, if you see a little red box this is broken.
TEST_P(AiksTest, ClearColorOptimizationWhenSubpassIsBiggerThanParentPass) {
  SetWindowSize({400, 400});
  Canvas canvas;
  canvas.Scale(GetContentScale());
  canvas.DrawRect(Rect::MakeLTRB(200, 200, 300, 300), {.color = Color::Red()});
  canvas.SaveLayer({
      .image_filter = std::make_shared<MatrixImageFilter>(
          Matrix::MakeScale({2, 2, 1}), SamplerDescriptor{}),
  });
  // Draw a rectangle that would fully cover the parent pass size, but not
  // the subpass that it is rendered in.
  canvas.DrawRect(Rect::MakeLTRB(0, 0, 400, 400), {.color = Color::Green()});
  // Draw a bigger rectangle to force the subpass to be bigger.
  canvas.DrawRect(Rect::MakeLTRB(0, 0, 800, 800), {.color = Color::Red()});
  canvas.Restore();

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, BlurHasNoEdge) {
  Canvas canvas;
  canvas.Scale(GetContentScale());
  canvas.DrawPaint({});
  Paint blur = {
      .color = Color::Green(),
      .mask_blur_descriptor =
          Paint::MaskBlurDescriptor{
              .style = FilterContents::BlurStyle::kNormal,
              .sigma = Sigma(47.6),
          },
  };
  canvas.DrawRect(Rect::MakeXYWH(300, 300, 200, 200), blur);
  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, EmptySaveLayerIgnoresPaint) {
  Canvas canvas;
  canvas.Scale(GetContentScale());
  canvas.DrawPaint(Paint{.color = Color::Red()});
  canvas.ClipRect(Rect::MakeXYWH(100, 100, 200, 200));
  canvas.SaveLayer(Paint{.color = Color::Blue()});
  canvas.Restore();
  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, EmptySaveLayerRendersWithClear) {
  Canvas canvas;
  canvas.Scale(GetContentScale());
  auto image = std::make_shared<Image>(CreateTextureForFixture("airplane.jpg"));
  canvas.DrawImage(image, {10, 10}, {});
  canvas.ClipRect(Rect::MakeXYWH(100, 100, 200, 200));
  canvas.SaveLayer(Paint{.blend_mode = BlendMode::kClear});
  canvas.Restore();
  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, BlurredRectangleWithShader) {
  Canvas canvas;
  canvas.Scale(GetContentScale());

  auto paint_lines = [&canvas](Scalar dx, Scalar dy, Paint paint) {
    auto draw_line = [&canvas, &paint](Point a, Point b) {
      canvas.DrawPath(PathBuilder{}.AddLine(a, b).TakePath(), paint);
    };
    paint.stroke_width = 5;
    paint.style = Paint::Style::kStroke;
    draw_line(Point(dx + 100, dy + 100), Point(dx + 200, dy + 200));
    draw_line(Point(dx + 100, dy + 200), Point(dx + 200, dy + 100));
    draw_line(Point(dx + 150, dy + 100), Point(dx + 200, dy + 150));
    draw_line(Point(dx + 100, dy + 150), Point(dx + 150, dy + 200));
  };

  AiksContext renderer(GetContext(), nullptr);
  Canvas recorder_canvas;
  for (int x = 0; x < 5; ++x) {
    for (int y = 0; y < 5; ++y) {
      Rect rect = Rect::MakeXYWH(x * 20, y * 20, 20, 20);
      Paint paint{.color =
                      ((x + y) & 1) == 0 ? Color::Yellow() : Color::Blue()};
      recorder_canvas.DrawRect(rect, paint);
    }
  }
  Picture picture = recorder_canvas.EndRecordingAsPicture();
  std::shared_ptr<Texture> texture =
      picture.ToImage(renderer, ISize{100, 100})->GetTexture();

  ColorSource image_source = ColorSource::MakeImage(
      texture, Entity::TileMode::kRepeat, Entity::TileMode::kRepeat, {}, {});
  std::shared_ptr<ImageFilter> blur_filter = ImageFilter::MakeBlur(
      Sigma(5), Sigma(5), FilterContents::BlurStyle::kNormal,
      Entity::TileMode::kDecal);
  canvas.DrawRect(Rect::MakeLTRB(0, 0, 300, 600),
                  Paint{.color = Color::DarkGreen()});
  canvas.DrawRect(Rect::MakeLTRB(100, 100, 200, 200),
                  Paint{.color_source = image_source});
  canvas.DrawRect(Rect::MakeLTRB(300, 0, 600, 600),
                  Paint{.color = Color::Red()});
  canvas.DrawRect(
      Rect::MakeLTRB(400, 100, 500, 200),
      Paint{.color_source = image_source, .image_filter = blur_filter});
  paint_lines(0, 300, Paint{.color_source = image_source});
  paint_lines(300, 300,
              Paint{.color_source = image_source, .image_filter = blur_filter});
  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, MaskBlurWithZeroSigmaIsSkipped) {
  Canvas canvas;

  Paint paint = {
      .color = Color::Blue(),
      .mask_blur_descriptor =
          Paint::MaskBlurDescriptor{
              .style = FilterContents::BlurStyle::kNormal,
              .sigma = Sigma(0),
          },
  };

  canvas.DrawCircle({300, 300}, 200, paint);
  canvas.DrawRect(Rect::MakeLTRB(100, 300, 500, 600), paint);

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, GaussianBlurAtPeripheryVertical) {
  Canvas canvas;

  canvas.Scale(GetContentScale());
  canvas.DrawRRect(Rect::MakeLTRB(0, 0, GetWindowSize().width, 100),
                   Size(10, 10), Paint{.color = Color::LimeGreen()});
  canvas.DrawRRect(Rect::MakeLTRB(0, 110, GetWindowSize().width, 210),
                   Size(10, 10), Paint{.color = Color::Magenta()});
  canvas.ClipRect(Rect::MakeLTRB(100, 0, 200, GetWindowSize().height));
  canvas.SaveLayer({.blend_mode = BlendMode::kSource}, std::nullopt,
                   ImageFilter::MakeBlur(Sigma(20.0), Sigma(20.0),
                                         FilterContents::BlurStyle::kNormal,
                                         Entity::TileMode::kClamp));
  canvas.Restore();

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, GaussianBlurAtPeripheryHorizontal) {
  Canvas canvas;

  canvas.Scale(GetContentScale());
  std::shared_ptr<Texture> boston = CreateTextureForFixture("boston.jpg");
  canvas.DrawImageRect(
      std::make_shared<Image>(boston),
      Rect::MakeXYWH(0, 0, boston->GetSize().width, boston->GetSize().height),
      Rect::MakeLTRB(0, 0, GetWindowSize().width, 100), Paint{});
  canvas.DrawRRect(Rect::MakeLTRB(0, 110, GetWindowSize().width, 210),
                   Size(10, 10), Paint{.color = Color::Magenta()});
  canvas.ClipRect(Rect::MakeLTRB(0, 50, GetWindowSize().width, 150));
  canvas.SaveLayer({.blend_mode = BlendMode::kSource}, std::nullopt,
                   ImageFilter::MakeBlur(Sigma(20.0), Sigma(20.0),
                                         FilterContents::BlurStyle::kNormal,
                                         Entity::TileMode::kClamp));
  canvas.Restore();
  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

#define FLT_FORWARD(mock, real, method) \
  EXPECT_CALL(*mock, method())          \
      .WillRepeatedly(::testing::Return(real->method()));

TEST_P(AiksTest, GaussianBlurWithoutDecalSupport) {
  if (GetParam() != PlaygroundBackend::kMetal) {
    GTEST_SKIP_(
        "This backend doesn't yet support setting device capabilities.");
  }
  if (!WillRenderSomething()) {
    // Sometimes these tests are run without playgrounds enabled which is
    // pointless for this test since we are asserting that
    // `SupportsDecalSamplerAddressMode` is called.
    GTEST_SKIP_("This test requires playgrounds.");
  }

  std::shared_ptr<const Capabilities> old_capabilities =
      GetContext()->GetCapabilities();
  auto mock_capabilities = std::make_shared<MockCapabilities>();
  EXPECT_CALL(*mock_capabilities, SupportsDecalSamplerAddressMode())
      .Times(::testing::AtLeast(1))
      .WillRepeatedly(::testing::Return(false));
  FLT_FORWARD(mock_capabilities, old_capabilities, GetDefaultColorFormat);
  FLT_FORWARD(mock_capabilities, old_capabilities, GetDefaultStencilFormat);
  FLT_FORWARD(mock_capabilities, old_capabilities,
              GetDefaultDepthStencilFormat);
  FLT_FORWARD(mock_capabilities, old_capabilities, SupportsOffscreenMSAA);
  FLT_FORWARD(mock_capabilities, old_capabilities,
              SupportsImplicitResolvingMSAA);
  FLT_FORWARD(mock_capabilities, old_capabilities, SupportsReadFromResolve);
  FLT_FORWARD(mock_capabilities, old_capabilities, SupportsFramebufferFetch);
  FLT_FORWARD(mock_capabilities, old_capabilities, SupportsSSBO);
  FLT_FORWARD(mock_capabilities, old_capabilities, SupportsCompute);
  FLT_FORWARD(mock_capabilities, old_capabilities,
              SupportsTextureToTextureBlits);
  ASSERT_TRUE(SetCapabilities(mock_capabilities).ok());

  auto texture = std::make_shared<Image>(CreateTextureForFixture("boston.jpg"));
  Canvas canvas;
  canvas.Scale(GetContentScale() * 0.5);
  canvas.DrawPaint({.color = Color::Black()});
  canvas.DrawImage(
      texture, Point(200, 200),
      {
          .image_filter = ImageFilter::MakeBlur(
              Sigma(20.0), Sigma(20.0), FilterContents::BlurStyle::kNormal,
              Entity::TileMode::kDecal),
      });
  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, GaussianBlurOneDimension) {
  Canvas canvas;

  canvas.Scale(GetContentScale());
  canvas.Scale({0.5, 0.5, 1.0});
  std::shared_ptr<Texture> boston = CreateTextureForFixture("boston.jpg");
  canvas.DrawImage(std::make_shared<Image>(boston), Point(100, 100), Paint{});
  canvas.SaveLayer({.blend_mode = BlendMode::kSource}, std::nullopt,
                   ImageFilter::MakeBlur(Sigma(50.0), Sigma(0.0),
                                         FilterContents::BlurStyle::kNormal,
                                         Entity::TileMode::kClamp));
  canvas.Restore();
  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

// Smoketest to catch issues with the coverage hint.
// Draws a rotated blurred image within a rectangle clip. The center of the clip
// rectangle is the center of the rotated image. The entire area of the clip
// rectangle should be filled with opaque colors output by the blur.
TEST_P(AiksTest, GaussianBlurRotatedAndClipped) {
  Canvas canvas;
  std::shared_ptr<Texture> boston = CreateTextureForFixture("boston.jpg");
  Rect bounds =
      Rect::MakeXYWH(0, 0, boston->GetSize().width, boston->GetSize().height);
  Vector2 image_center = Vector2(bounds.GetSize() / 2);
  Paint paint = {.image_filter =
                     ImageFilter::MakeBlur(Sigma(20.0), Sigma(20.0),
                                           FilterContents::BlurStyle::kNormal,
                                           Entity::TileMode::kDecal)};
  Vector2 clip_size = {150, 75};
  Vector2 center = Vector2(1024, 768) / 2;
  canvas.Scale(GetContentScale());
  canvas.ClipRect(
      Rect::MakeLTRB(center.x, center.y, center.x, center.y).Expand(clip_size));
  canvas.Translate({center.x, center.y, 0});
  canvas.Scale({0.6, 0.6, 1});
  canvas.Rotate(Degrees(25));

  canvas.DrawImageRect(std::make_shared<Image>(boston), /*source=*/bounds,
                       /*dest=*/bounds.Shift(-image_center), paint);

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, GaussianBlurScaledAndClipped) {
  Canvas canvas;
  std::shared_ptr<Texture> boston = CreateTextureForFixture("boston.jpg");
  Rect bounds =
      Rect::MakeXYWH(0, 0, boston->GetSize().width, boston->GetSize().height);
  Vector2 image_center = Vector2(bounds.GetSize() / 2);
  Paint paint = {.image_filter =
                     ImageFilter::MakeBlur(Sigma(20.0), Sigma(20.0),
                                           FilterContents::BlurStyle::kNormal,
                                           Entity::TileMode::kDecal)};
  Vector2 clip_size = {150, 75};
  Vector2 center = Vector2(1024, 768) / 2;
  canvas.Scale(GetContentScale());
  canvas.ClipRect(
      Rect::MakeLTRB(center.x, center.y, center.x, center.y).Expand(clip_size));
  canvas.Translate({center.x, center.y, 0});
  canvas.Scale({0.6, 0.6, 1});

  canvas.DrawImageRect(std::make_shared<Image>(boston), /*source=*/bounds,
                       /*dest=*/bounds.Shift(-image_center), paint);

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, GaussianBlurRotatedAndClippedInteractive) {
  std::shared_ptr<Texture> boston = CreateTextureForFixture("boston.jpg");

  auto callback = [&](AiksContext& renderer) -> std::optional<Picture> {
    const char* tile_mode_names[] = {"Clamp", "Repeat", "Mirror", "Decal"};
    const Entity::TileMode tile_modes[] = {
        Entity::TileMode::kClamp, Entity::TileMode::kRepeat,
        Entity::TileMode::kMirror, Entity::TileMode::kDecal};

    static float rotation = 0;
    static float scale = 0.6;
    static int selected_tile_mode = 3;

    ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    {
      ImGui::SliderFloat("Rotation (degrees)", &rotation, -180, 180);
      ImGui::SliderFloat("Scale", &scale, 0, 2.0);
      ImGui::Combo("Tile mode", &selected_tile_mode, tile_mode_names,
                   sizeof(tile_mode_names) / sizeof(char*));
    }
    ImGui::End();

    Canvas canvas;
    Rect bounds =
        Rect::MakeXYWH(0, 0, boston->GetSize().width, boston->GetSize().height);
    Vector2 image_center = Vector2(bounds.GetSize() / 2);
    Paint paint = {.image_filter =
                       ImageFilter::MakeBlur(Sigma(20.0), Sigma(20.0),
                                             FilterContents::BlurStyle::kNormal,
                                             tile_modes[selected_tile_mode])};
    auto [handle_a, handle_b] = IMPELLER_PLAYGROUND_LINE(
        Point(362, 309), Point(662, 459), 20, Color::Red(), Color::Red());
    Vector2 center = Vector2(1024, 768) / 2;
    canvas.Scale(GetContentScale());
    canvas.ClipRect(
        Rect::MakeLTRB(handle_a.x, handle_a.y, handle_b.x, handle_b.y));
    canvas.Translate({center.x, center.y, 0});
    canvas.Scale({scale, scale, 1});
    canvas.Rotate(Degrees(rotation));

    canvas.DrawImageRect(std::make_shared<Image>(boston), /*source=*/bounds,
                         /*dest=*/bounds.Shift(-image_center), paint);
    return canvas.EndRecordingAsPicture();
  };

  ASSERT_TRUE(OpenPlaygroundHere(callback));
}

TEST_P(AiksTest, SubpassWithClearColorOptimization) {
  Canvas canvas;

  // Use a non-srcOver blend mode to ensure that we don't detect this as an
  // opacity peephole optimization.
  canvas.SaveLayer(
      {.color = Color::Blue().WithAlpha(0.5), .blend_mode = BlendMode::kSource},
      Rect::MakeLTRB(0, 0, 200, 200));
  canvas.DrawPaint(
      {.color = Color::BlackTransparent(), .blend_mode = BlendMode::kSource});
  canvas.Restore();

  canvas.SaveLayer(
      {.color = Color::Blue(), .blend_mode = BlendMode::kDestinationOver});
  canvas.Restore();

  // This playground should appear blank on CI since we are only drawing
  // transparent black. If the clear color optimization is broken, the texture
  // will be filled with NaNs and may produce a magenta texture on macOS or iOS.
  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, GuassianBlurUpdatesMipmapContents) {
  // This makes sure if mip maps are recycled across invocations of blurs the
  // contents get updated each frame correctly. If they aren't updated the color
  // inside the blur and outside the blur will be different.
  //
  // If there is some change to render target caching this could display a false
  // positive in the future.  Also, if the LOD that is rendered is 1 it could
  // present a false positive.
  int32_t count = 0;
  auto callback = [&](AiksContext& renderer) -> std::optional<Picture> {
    Canvas canvas;
    if (count++ == 0) {
      canvas.DrawCircle({100, 100}, 50, {.color = Color::CornflowerBlue()});
    } else {
      canvas.DrawCircle({100, 100}, 50, {.color = Color::Chartreuse()});
    }
    canvas.ClipRRect(Rect::MakeLTRB(75, 50, 375, 275), {20, 20});
    canvas.SaveLayer({.blend_mode = BlendMode::kSource}, std::nullopt,
                     ImageFilter::MakeBlur(Sigma(30.0), Sigma(30.0),
                                           FilterContents::BlurStyle::kNormal,
                                           Entity::TileMode::kClamp));
    canvas.Restore();
    return canvas.EndRecordingAsPicture();
  };

  ASSERT_TRUE(OpenPlaygroundHere(callback));
}

TEST_P(AiksTest, GaussianBlurSetsMipCountOnPass) {
  Canvas canvas;
  canvas.DrawCircle({100, 100}, 50, {.color = Color::CornflowerBlue()});
  canvas.SaveLayer({}, std::nullopt,
                   ImageFilter::MakeBlur(Sigma(3), Sigma(3),
                                         FilterContents::BlurStyle::kNormal,
                                         Entity::TileMode::kClamp));
  canvas.Restore();

  Picture picture = canvas.EndRecordingAsPicture();
  EXPECT_EQ(4, picture.pass->GetRequiredMipCount());
}

TEST_P(AiksTest, GaussianBlurAllocatesCorrectMipCountRenderTarget) {
  size_t blur_required_mip_count =
      GetParam() == PlaygroundBackend::kOpenGLES ? 1 : 4;

  Canvas canvas;
  canvas.DrawCircle({100, 100}, 50, {.color = Color::CornflowerBlue()});
  canvas.SaveLayer({}, std::nullopt,
                   ImageFilter::MakeBlur(Sigma(3), Sigma(3),
                                         FilterContents::BlurStyle::kNormal,
                                         Entity::TileMode::kClamp));
  canvas.Restore();

  Picture picture = canvas.EndRecordingAsPicture();
  std::shared_ptr<RenderTargetCache> cache =
      std::make_shared<RenderTargetCache>(GetContext()->GetResourceAllocator());
  AiksContext aiks_context(GetContext(), nullptr, cache);
  picture.ToImage(aiks_context, {100, 100});

  size_t max_mip_count = 0;
  for (auto it = cache->GetTextureDataBegin(); it != cache->GetTextureDataEnd();
       ++it) {
    max_mip_count =
        std::max(it->texture->GetTextureDescriptor().mip_count, max_mip_count);
  }
  EXPECT_EQ(max_mip_count, blur_required_mip_count);
}

TEST_P(AiksTest, GaussianBlurMipMapNestedLayer) {
  fml::testing::LogCapture log_capture;
  size_t blur_required_mip_count =
      GetParam() == PlaygroundBackend::kOpenGLES ? 1 : 4;

  Canvas canvas;
  canvas.DrawPaint({.color = Color::Wheat()});
  canvas.SaveLayer({.blend_mode = BlendMode::kMultiply});
  canvas.DrawCircle({100, 100}, 50, {.color = Color::CornflowerBlue()});
  canvas.SaveLayer({}, std::nullopt,
                   ImageFilter::MakeBlur(Sigma(30), Sigma(30),
                                         FilterContents::BlurStyle::kNormal,
                                         Entity::TileMode::kClamp));
  canvas.DrawCircle({200, 200}, 50, {.color = Color::Chartreuse()});

  Picture picture = canvas.EndRecordingAsPicture();
  std::shared_ptr<RenderTargetCache> cache =
      std::make_shared<RenderTargetCache>(GetContext()->GetResourceAllocator());
  AiksContext aiks_context(GetContext(), nullptr, cache);
  picture.ToImage(aiks_context, {100, 100});

  size_t max_mip_count = 0;
  for (auto it = cache->GetTextureDataBegin(); it != cache->GetTextureDataEnd();
       ++it) {
    max_mip_count =
        std::max(it->texture->GetTextureDescriptor().mip_count, max_mip_count);
  }
  EXPECT_EQ(max_mip_count, blur_required_mip_count);
  // The log is FML_DLOG, so only check in debug builds.
#ifndef NDEBUG
  if (GetParam() != PlaygroundBackend::kOpenGLES) {
    EXPECT_EQ(log_capture.str().find(GaussianBlurFilterContents::kNoMipsError),
              std::string::npos);
  } else {
    EXPECT_NE(log_capture.str().find(GaussianBlurFilterContents::kNoMipsError),
              std::string::npos);
  }
#endif
}

TEST_P(AiksTest, GaussianBlurMipMapImageFilter) {
  size_t blur_required_mip_count =
      GetParam() == PlaygroundBackend::kOpenGLES ? 1 : 4;
  fml::testing::LogCapture log_capture;
  Canvas canvas;
  canvas.SaveLayer(
      {.image_filter = ImageFilter::MakeBlur(Sigma(30), Sigma(30),
                                             FilterContents::BlurStyle::kNormal,
                                             Entity::TileMode::kClamp)});
  canvas.DrawCircle({200, 200}, 50, {.color = Color::Chartreuse()});

  Picture picture = canvas.EndRecordingAsPicture();
  std::shared_ptr<RenderTargetCache> cache =
      std::make_shared<RenderTargetCache>(GetContext()->GetResourceAllocator());
  AiksContext aiks_context(GetContext(), nullptr, cache);
  picture.ToImage(aiks_context, {1024, 768});

  size_t max_mip_count = 0;
  for (auto it = cache->GetTextureDataBegin(); it != cache->GetTextureDataEnd();
       ++it) {
    max_mip_count =
        std::max(it->texture->GetTextureDescriptor().mip_count, max_mip_count);
  }
  EXPECT_EQ(max_mip_count, blur_required_mip_count);
  // The log is FML_DLOG, so only check in debug builds.
#ifndef NDEBUG
  if (GetParam() != PlaygroundBackend::kOpenGLES) {
    EXPECT_EQ(log_capture.str().find(GaussianBlurFilterContents::kNoMipsError),
              std::string::npos);
  } else {
    EXPECT_NE(log_capture.str().find(GaussianBlurFilterContents::kNoMipsError),
              std::string::npos);
  }
#endif
}

TEST_P(AiksTest, GaussianBlurMipMapSolidColor) {
  size_t blur_required_mip_count =
      GetParam() == PlaygroundBackend::kOpenGLES ? 1 : 4;
  fml::testing::LogCapture log_capture;
  Canvas canvas;
  canvas.DrawPath(PathBuilder{}
                      .MoveTo({100, 100})
                      .LineTo({200, 100})
                      .LineTo({150, 200})
                      .LineTo({50, 200})
                      .Close()
                      .TakePath(),
                  {.color = Color::Chartreuse(),
                   .image_filter = ImageFilter::MakeBlur(
                       Sigma(30), Sigma(30), FilterContents::BlurStyle::kNormal,
                       Entity::TileMode::kClamp)});

  Picture picture = canvas.EndRecordingAsPicture();
  std::shared_ptr<RenderTargetCache> cache =
      std::make_shared<RenderTargetCache>(GetContext()->GetResourceAllocator());
  AiksContext aiks_context(GetContext(), nullptr, cache);
  picture.ToImage(aiks_context, {1024, 768});

  size_t max_mip_count = 0;
  for (auto it = cache->GetTextureDataBegin(); it != cache->GetTextureDataEnd();
       ++it) {
    max_mip_count =
        std::max(it->texture->GetTextureDescriptor().mip_count, max_mip_count);
  }
  EXPECT_EQ(max_mip_count, blur_required_mip_count);
  // The log is FML_DLOG, so only check in debug builds.
#ifndef NDEBUG
  if (GetParam() != PlaygroundBackend::kOpenGLES) {
    EXPECT_EQ(log_capture.str().find(GaussianBlurFilterContents::kNoMipsError),
              std::string::npos);
  } else {
    EXPECT_NE(log_capture.str().find(GaussianBlurFilterContents::kNoMipsError),
              std::string::npos);
  }
#endif
}

TEST_P(AiksTest, ImageColorSourceEffectTransform) {
  // Compare with https://fiddle.skia.org/c/6cdc5aefb291fda3833b806ca347a885

  Canvas canvas;
  auto texture = CreateTextureForFixture("monkey.png");

  canvas.DrawPaint({.color = Color::White()});

  // Translation
  {
    Paint paint;
    paint.color_source = ColorSource::MakeImage(
        texture, Entity::TileMode::kRepeat, Entity::TileMode::kRepeat, {},
        Matrix::MakeTranslation({50, 50}));
    canvas.DrawRect(Rect::MakeLTRB(0, 0, 100, 100), paint);
  }

  // Rotation/skew
  {
    canvas.Save();
    canvas.Rotate(Degrees(45));
    Paint paint;
    paint.color_source = ColorSource::MakeImage(
        texture, Entity::TileMode::kRepeat, Entity::TileMode::kRepeat, {},
        Matrix(1, -1, 0, 0,  //
               1, 1, 0, 0,   //
               0, 0, 1, 0,   //
               0, 0, 0, 1)   //
    );
    canvas.DrawRect(Rect::MakeLTRB(100, 0, 200, 100), paint);
    canvas.Restore();
  }

  // Scale
  {
    canvas.Translate(Vector2(100, 0));
    canvas.Scale(Vector2(100, 100));
    Paint paint;
    paint.color_source = ColorSource::MakeImage(
        texture, Entity::TileMode::kRepeat, Entity::TileMode::kRepeat, {},
        Matrix::MakeScale(Vector2(0.005, 0.005)));
    canvas.DrawRect(Rect::MakeLTRB(0, 0, 1, 1), paint);
  }

  ASSERT_TRUE(OpenPlaygroundHere(canvas.EndRecordingAsPicture()));
}

TEST_P(AiksTest, CorrectClipDepthAssignedToEntities) {
  Canvas canvas;  // Depth 1 (base pass)
  canvas.DrawRRect(Rect::MakeLTRB(0, 0, 100, 100), {10, 10}, {});  // Depth 2
  canvas.ClipRRect(Rect::MakeLTRB(0, 0, 50, 50), {10, 10}, {});    // Depth 4
  canvas.SaveLayer({});                                            // Depth 3
  canvas.DrawRRect(Rect::MakeLTRB(0, 0, 50, 50), {10, 10}, {});    // Depth 4

  auto picture = canvas.EndRecordingAsPicture();
  std::array<uint32_t, 4> expected = {2, 4, 3, 4};
  std::vector<uint32_t> actual;

  picture.pass->IterateAllElements([&](EntityPass::Element& element) -> bool {
    if (auto* subpass = std::get_if<std::unique_ptr<EntityPass>>(&element)) {
      actual.push_back(subpass->get()->GetNewClipDepth());
    }
    if (Entity* entity = std::get_if<Entity>(&element)) {
      actual.push_back(entity->GetNewClipDepth());
    }
    return true;
  });

  ASSERT_EQ(actual.size(), expected.size());
  for (size_t i = 0; i < expected.size(); i++) {
    EXPECT_EQ(actual[i], expected[i]) << "Index: " << i;
  }
}

// This addresses a bug where tiny blurs could result in mip maps that beyond
// the limits for the textures used for blurring.
// See also: b/323402168
TEST_P(AiksTest, GaussianBlurSolidColorTinyMipMap) {
  for (int32_t i = 1; i < 5; ++i) {
    Canvas canvas;
    Scalar fi = i;
    canvas.DrawPath(
        PathBuilder{}
            .MoveTo({100, 100})
            .LineTo({100.f + fi, 100.f + fi})
            .TakePath(),
        {.color = Color::Chartreuse(),
         .image_filter = ImageFilter::MakeBlur(
             Sigma(0.1), Sigma(0.1), FilterContents::BlurStyle::kNormal,
             Entity::TileMode::kClamp)});

    Picture picture = canvas.EndRecordingAsPicture();
    std::shared_ptr<RenderTargetCache> cache =
        std::make_shared<RenderTargetCache>(
            GetContext()->GetResourceAllocator());
    AiksContext aiks_context(GetContext(), nullptr, cache);
    std::shared_ptr<Image> image = picture.ToImage(aiks_context, {1024, 768});
    EXPECT_TRUE(image) << " length " << i;
  }
}

// This addresses a bug where tiny blurs could result in mip maps that beyond
// the limits for the textures used for blurring.
// See also: b/323402168
TEST_P(AiksTest, GaussianBlurBackdropTinyMipMap) {
  for (int32_t i = 0; i < 5; ++i) {
    Canvas canvas;
    ISize clip_size = ISize(i, i);
    canvas.ClipRect(
        Rect::MakeXYWH(400, 400, clip_size.width, clip_size.height));
    canvas.DrawCircle(
        {400, 400}, 200,
        {
            .color = Color::Green(),
            .image_filter = ImageFilter::MakeBlur(
                Sigma(0.1), Sigma(0.1), FilterContents::BlurStyle::kNormal,
                Entity::TileMode::kDecal),
        });
    canvas.Restore();

    Picture picture = canvas.EndRecordingAsPicture();
    std::shared_ptr<RenderTargetCache> cache =
        std::make_shared<RenderTargetCache>(
            GetContext()->GetResourceAllocator());
    AiksContext aiks_context(GetContext(), nullptr, cache);
    std::shared_ptr<Image> image = picture.ToImage(aiks_context, {1024, 768});
    EXPECT_TRUE(image) << " clip rect " << i;
  }
}

TEST_P(AiksTest, GaussianBlurAnimatedBackdrop) {
  // This test is for checking out how stable rendering is when content is
  // translated underneath a blur.  Animating under a blur can cause
  // *shimmering* to happen as a result of pixel alignment.
  // See also: https://github.com/flutter/flutter/issues/140193
  auto boston = std::make_shared<Image>(
      CreateTextureForFixture("boston.jpg", /*enable_mipmapping=*/true));
  ASSERT_TRUE(boston);
  int64_t count = 0;
  Scalar sigma = 20.0;
  Scalar freq = 0.1;
  Scalar amp = 50.0;
  auto callback = [&](AiksContext& renderer) -> std::optional<Picture> {
    ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    {
      ImGui::SliderFloat("Sigma", &sigma, 0, 200);
      ImGui::SliderFloat("Frequency", &freq, 0.01, 2.0);
      ImGui::SliderFloat("Amplitude", &amp, 1, 100);
    }
    ImGui::End();

    Canvas canvas;
    canvas.Scale(GetContentScale());
    Scalar y = amp * sin(freq * 2.0 * M_PI * count / 60);
    canvas.DrawImage(boston,
                     Point(1024 / 2 - boston->GetSize().width / 2,
                           (768 / 2 - boston->GetSize().height / 2) + y),
                     {});
    canvas.ClipRect(Rect::MakeLTRB(100, 100, 900, 700));
    canvas.SaveLayer({.blend_mode = BlendMode::kSource}, std::nullopt,
                     ImageFilter::MakeBlur(Sigma(sigma), Sigma(sigma),
                                           FilterContents::BlurStyle::kNormal,
                                           Entity::TileMode::kClamp));
    count += 1;
    return canvas.EndRecordingAsPicture();
  };
  ASSERT_TRUE(OpenPlaygroundHere(callback));
}

}  // namespace testing
}  // namespace impeller
