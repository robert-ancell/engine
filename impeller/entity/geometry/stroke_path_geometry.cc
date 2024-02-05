// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "impeller/entity/geometry/stroke_path_geometry.h"

#include "impeller/geometry/path_builder.h"

namespace impeller {

StrokePathGeometry::StrokePathGeometry(const Path& path,
                                       Scalar stroke_width,
                                       Scalar miter_limit,
                                       Cap stroke_cap,
                                       Join stroke_join)
    : path_(path),
      stroke_width_(stroke_width),
      miter_limit_(miter_limit),
      stroke_cap_(stroke_cap),
      stroke_join_(stroke_join) {}

StrokePathGeometry::~StrokePathGeometry() = default;

Scalar StrokePathGeometry::GetStrokeWidth() const {
  return stroke_width_;
}

Scalar StrokePathGeometry::GetMiterLimit() const {
  return miter_limit_;
}

Cap StrokePathGeometry::GetStrokeCap() const {
  return stroke_cap_;
}

Join StrokePathGeometry::GetStrokeJoin() const {
  return stroke_join_;
}

// static
Scalar StrokePathGeometry::CreateBevelAndGetDirection(
    VertexBufferBuilder<SolidFillVertexShader::PerVertexData>& vtx_builder,
    const Point& position,
    const Point& start_offset,
    const Point& end_offset) {
  SolidFillVertexShader::PerVertexData vtx;
  vtx.position = position;
  vtx_builder.AppendVertex(vtx);

  Scalar dir = start_offset.Cross(end_offset) > 0 ? -1 : 1;
  vtx.position = position + start_offset * dir;
  vtx_builder.AppendVertex(vtx);
  vtx.position = position + end_offset * dir;
  vtx_builder.AppendVertex(vtx);

  return dir;
}

// static
StrokePathGeometry::JoinProc StrokePathGeometry::GetJoinProc(Join stroke_join) {
  using VS = SolidFillVertexShader;
  StrokePathGeometry::JoinProc join_proc;
  switch (stroke_join) {
    case Join::kBevel:
      join_proc = [](VertexBufferBuilder<VS::PerVertexData>& vtx_builder,
                     const Point& position, const Point& start_offset,
                     const Point& end_offset, Scalar miter_limit,
                     Scalar scale) {
        CreateBevelAndGetDirection(vtx_builder, position, start_offset,
                                   end_offset);
      };
      break;
    case Join::kMiter:
      join_proc = [](VertexBufferBuilder<VS::PerVertexData>& vtx_builder,
                     const Point& position, const Point& start_offset,
                     const Point& end_offset, Scalar miter_limit,
                     Scalar scale) {
        Point start_normal = start_offset.Normalize();
        Point end_normal = end_offset.Normalize();

        // 1 for no joint (straight line), 0 for max joint (180 degrees).
        Scalar alignment = (start_normal.Dot(end_normal) + 1) / 2;
        if (ScalarNearlyEqual(alignment, 1)) {
          return;
        }

        Scalar dir = CreateBevelAndGetDirection(vtx_builder, position,
                                                start_offset, end_offset);

        Point miter_point = (start_offset + end_offset) / 2 / alignment;
        if (miter_point.GetDistanceSquared({0, 0}) >
            miter_limit * miter_limit) {
          return;  // Convert to bevel when we exceed the miter limit.
        }

        // Outer miter point.
        VS::PerVertexData vtx;
        vtx.position = position + miter_point * dir;
        vtx_builder.AppendVertex(vtx);
      };
      break;
    case Join::kRound:
      join_proc = [](VertexBufferBuilder<VS::PerVertexData>& vtx_builder,
                     const Point& position, const Point& start_offset,
                     const Point& end_offset, Scalar miter_limit,
                     Scalar scale) {
        Point start_normal = start_offset.Normalize();
        Point end_normal = end_offset.Normalize();

        // 0 for no joint (straight line), 1 for max joint (180 degrees).
        Scalar alignment = 1 - (start_normal.Dot(end_normal) + 1) / 2;
        if (ScalarNearlyEqual(alignment, 0)) {
          return;
        }

        Scalar dir = CreateBevelAndGetDirection(vtx_builder, position,
                                                start_offset, end_offset);

        Point middle =
            (start_offset + end_offset).Normalize() * start_offset.GetLength();
        Point middle_normal = middle.Normalize();

        Point middle_handle = middle + Point(-middle.y, middle.x) *
                                           PathBuilder::kArcApproximationMagic *
                                           alignment * dir;
        Point start_handle =
            start_offset + Point(start_offset.y, -start_offset.x) *
                               PathBuilder::kArcApproximationMagic * alignment *
                               dir;

        std::vector<Point> arc_points;
        CubicPathComponent(start_offset, start_handle, middle_handle, middle)
            .AppendPolylinePoints(scale, arc_points);

        VS::PerVertexData vtx;
        for (const auto& point : arc_points) {
          vtx.position = position + point * dir;
          vtx_builder.AppendVertex(vtx);
          vtx.position = position + (-point * dir).Reflect(middle_normal);
          vtx_builder.AppendVertex(vtx);
        }
      };
      break;
  }
  return join_proc;
}

// static
StrokePathGeometry::CapProc StrokePathGeometry::GetCapProc(Cap stroke_cap) {
  using VS = SolidFillVertexShader;
  StrokePathGeometry::CapProc cap_proc;
  switch (stroke_cap) {
    case Cap::kButt:
      cap_proc = [](VertexBufferBuilder<VS::PerVertexData>& vtx_builder,
                    const Point& position, const Point& offset, Scalar scale,
                    bool reverse) {
        Point orientation = offset * (reverse ? -1 : 1);
        VS::PerVertexData vtx;
        vtx.position = position + orientation;
        vtx_builder.AppendVertex(vtx);
        vtx.position = position - orientation;
        vtx_builder.AppendVertex(vtx);
      };
      break;
    case Cap::kRound:
      cap_proc = [](VertexBufferBuilder<VS::PerVertexData>& vtx_builder,
                    const Point& position, const Point& offset, Scalar scale,
                    bool reverse) {
        Point orientation = offset * (reverse ? -1 : 1);

        VS::PerVertexData vtx;

        Point forward(offset.y, -offset.x);
        Point forward_normal = forward.Normalize();

        CubicPathComponent arc;
        if (reverse) {
          arc = CubicPathComponent(
              forward,
              forward + orientation * PathBuilder::kArcApproximationMagic,
              orientation + forward * PathBuilder::kArcApproximationMagic,
              orientation);
        } else {
          arc = CubicPathComponent(
              orientation,
              orientation + forward * PathBuilder::kArcApproximationMagic,
              forward + orientation * PathBuilder::kArcApproximationMagic,
              forward);
        }

        vtx.position = position + orientation;
        vtx_builder.AppendVertex(vtx);
        vtx.position = position - orientation;
        vtx_builder.AppendVertex(vtx);
        std::vector<Point> arc_points;
        arc.AppendPolylinePoints(scale, arc_points);
        for (const auto& point : arc_points) {
          vtx.position = position + point;
          vtx_builder.AppendVertex(vtx);
          vtx.position = position + (-point).Reflect(forward_normal);
          vtx_builder.AppendVertex(vtx);
        }
      };
      break;
    case Cap::kSquare:
      cap_proc = [](VertexBufferBuilder<VS::PerVertexData>& vtx_builder,
                    const Point& position, const Point& offset, Scalar scale,
                    bool reverse) {
        Point orientation = offset * (reverse ? -1 : 1);

        VS::PerVertexData vtx;

        Point forward(offset.y, -offset.x);

        vtx.position = position + orientation;
        vtx_builder.AppendVertex(vtx);
        vtx.position = position - orientation;
        vtx_builder.AppendVertex(vtx);
        vtx.position = position + orientation + forward;
        vtx_builder.AppendVertex(vtx);
        vtx.position = position - orientation + forward;
        vtx_builder.AppendVertex(vtx);
      };
      break;
  }
  return cap_proc;
}

// static
VertexBufferBuilder<SolidFillVertexShader::PerVertexData>
StrokePathGeometry::CreateSolidStrokeVertices(
    const Path& path,
    Scalar stroke_width,
    Scalar scaled_miter_limit,
    const StrokePathGeometry::JoinProc& join_proc,
    const StrokePathGeometry::CapProc& cap_proc,
    Scalar scale) {
  VertexBufferBuilder<VS::PerVertexData> vtx_builder;
  auto point_buffer = std::make_unique<std::vector<Point>>();
  // 512 is an arbitrary choice that should be big enough for most paths without
  // needing to reallocate. If we have motivating benchmarks we should raise or
  // lower this number, cause dnfield just made it up!
  point_buffer->reserve(512);
  auto polyline = path.CreatePolyline(scale, std::move(point_buffer));

  VS::PerVertexData vtx;

  // Offset state.
  Point offset;
  Point previous_offset;  // Used for computing joins.

  // Computes offset by calculating the direction from point_i - 1 to point_i if
  // point_i is within `contour_start_point_i` and `contour_end_point_i`;
  // Otherwise, it uses direction from contour.
  auto compute_offset = [&polyline, &offset, &previous_offset, &stroke_width](
                            const size_t point_i,
                            const size_t contour_start_point_i,
                            const size_t contour_end_point_i,
                            const Path::PolylineContour& contour) {
    Point direction;
    if (point_i >= contour_end_point_i) {
      direction = contour.end_direction;
    } else if (point_i <= contour_start_point_i) {
      direction = -contour.start_direction;
    } else {
      direction = (polyline.GetPoint(point_i) - polyline.GetPoint(point_i - 1))
                      .Normalize();
    }
    previous_offset = offset;
    offset = Vector2{-direction.y, direction.x} * stroke_width * 0.5;
  };

  auto add_vertices_for_linear_component =
      [&vtx_builder, &offset, &previous_offset, &vtx, &polyline,
       &compute_offset, scaled_miter_limit, scale, &join_proc](
          const size_t component_start_index, const size_t component_end_index,
          const size_t contour_start_point_i, const size_t contour_end_point_i,
          const Path::PolylineContour& contour) {
        auto is_last_component =
            component_start_index ==
            contour.components.back().component_start_index;

        for (size_t point_i = component_start_index;
             point_i < component_end_index; point_i++) {
          auto is_end_of_component = point_i == component_end_index - 1;
          vtx.position = polyline.GetPoint(point_i) + offset;
          vtx_builder.AppendVertex(vtx);
          vtx.position = polyline.GetPoint(point_i) - offset;
          vtx_builder.AppendVertex(vtx);

          // For line components, two additional points need to be appended
          // prior to appending a join connecting the next component.
          vtx.position = polyline.GetPoint(point_i + 1) + offset;
          vtx_builder.AppendVertex(vtx);
          vtx.position = polyline.GetPoint(point_i + 1) - offset;
          vtx_builder.AppendVertex(vtx);

          compute_offset(point_i + 2, contour_start_point_i,
                         contour_end_point_i, contour);
          if (!is_last_component && is_end_of_component) {
            // Generate join from the current line to the next line.
            join_proc(vtx_builder, polyline.GetPoint(point_i + 1),
                      previous_offset, offset, scaled_miter_limit, scale);
          }
        }
      };

  auto add_vertices_for_curve_component =
      [&vtx_builder, &offset, &previous_offset, &vtx, &polyline,
       &compute_offset, scaled_miter_limit, scale, &join_proc](
          const size_t component_start_index, const size_t component_end_index,
          const size_t contour_start_point_i, const size_t contour_end_point_i,
          const Path::PolylineContour& contour) {
        auto is_last_component =
            component_start_index ==
            contour.components.back().component_start_index;

        for (size_t point_i = component_start_index;
             point_i < component_end_index; point_i++) {
          auto is_end_of_component = point_i == component_end_index - 1;

          vtx.position = polyline.GetPoint(point_i) + offset;
          vtx_builder.AppendVertex(vtx);
          vtx.position = polyline.GetPoint(point_i) - offset;
          vtx_builder.AppendVertex(vtx);

          compute_offset(point_i + 2, contour_start_point_i,
                         contour_end_point_i, contour);
          // For curve components, the polyline is detailed enough such that
          // it can avoid worrying about joins altogether.
          if (is_end_of_component) {
            vtx.position = polyline.GetPoint(point_i + 1) + offset;
            vtx_builder.AppendVertex(vtx);
            vtx.position = polyline.GetPoint(point_i + 1) - offset;
            vtx_builder.AppendVertex(vtx);
            // Generate join from the current line to the next line.
            if (!is_last_component) {
              join_proc(vtx_builder, polyline.GetPoint(point_i + 1),
                        previous_offset, offset, scaled_miter_limit, scale);
            }
          }
        }
      };

  for (size_t contour_i = 0; contour_i < polyline.contours.size();
       contour_i++) {
    auto contour = polyline.contours[contour_i];
    size_t contour_start_point_i, contour_end_point_i;
    std::tie(contour_start_point_i, contour_end_point_i) =
        polyline.GetContourPointBounds(contour_i);

    switch (contour_end_point_i - contour_start_point_i) {
      case 1: {
        Point p = polyline.GetPoint(contour_start_point_i);
        cap_proc(vtx_builder, p, {-stroke_width * 0.5f, 0}, scale, false);
        cap_proc(vtx_builder, p, {stroke_width * 0.5f, 0}, scale, false);
        continue;
      }
      case 0:
        continue;  // This contour has no renderable content.
      default:
        break;
    }

    compute_offset(contour_start_point_i, contour_start_point_i,
                   contour_end_point_i, contour);
    const Point contour_first_offset = offset;

    if (contour_i > 0) {
      // This branch only executes when we've just finished drawing a contour
      // and are switching to a new one.
      // We're drawing a triangle strip, so we need to "pick up the pen" by
      // appending two vertices at the end of the previous contour and two
      // vertices at the start of the new contour (thus connecting the two
      // contours with two zero volume triangles, which will be discarded by
      // the rasterizer).
      vtx.position = polyline.GetPoint(contour_start_point_i - 1);
      // Append two vertices when "picking up" the pen so that the triangle
      // drawn when moving to the beginning of the new contour will have zero
      // volume.
      vtx_builder.AppendVertex(vtx);
      vtx_builder.AppendVertex(vtx);

      vtx.position = polyline.GetPoint(contour_start_point_i);
      // Append two vertices at the beginning of the new contour, which
      // appends  two triangles of zero area.
      vtx_builder.AppendVertex(vtx);
      vtx_builder.AppendVertex(vtx);
    }

    // Generate start cap.
    if (!polyline.contours[contour_i].is_closed) {
      auto cap_offset =
          Vector2(-contour.start_direction.y, contour.start_direction.x) *
          stroke_width * 0.5;  // Counterclockwise normal
      cap_proc(vtx_builder, polyline.GetPoint(contour_start_point_i),
               cap_offset, scale, true);
    }

    for (size_t contour_component_i = 0;
         contour_component_i < contour.components.size();
         contour_component_i++) {
      auto component = contour.components[contour_component_i];
      auto is_last_component =
          contour_component_i == contour.components.size() - 1;

      auto component_start_index = component.component_start_index;
      auto component_end_index =
          is_last_component ? contour_end_point_i - 1
                            : contour.components[contour_component_i + 1]
                                  .component_start_index;
      if (component.is_curve) {
        add_vertices_for_curve_component(
            component_start_index, component_end_index, contour_start_point_i,
            contour_end_point_i, contour);
      } else {
        add_vertices_for_linear_component(
            component_start_index, component_end_index, contour_start_point_i,
            contour_end_point_i, contour);
      }
    }

    // Generate end cap or join.
    if (!contour.is_closed) {
      auto cap_offset =
          Vector2(-contour.end_direction.y, contour.end_direction.x) *
          stroke_width * 0.5;  // Clockwise normal
      cap_proc(vtx_builder, polyline.GetPoint(contour_end_point_i - 1),
               cap_offset, scale, false);
    } else {
      join_proc(vtx_builder, polyline.GetPoint(contour_start_point_i), offset,
                contour_first_offset, scaled_miter_limit, scale);
    }
  }

  return vtx_builder;
}

GeometryResult StrokePathGeometry::GetPositionBuffer(
    const ContentContext& renderer,
    const Entity& entity,
    RenderPass& pass) const {
  if (stroke_width_ < 0.0) {
    return {};
  }
  auto determinant = entity.GetTransform().GetDeterminant();
  if (determinant == 0) {
    return {};
  }

  Scalar min_size = 1.0f / sqrt(std::abs(determinant));
  Scalar stroke_width = std::max(stroke_width_, min_size);

  auto& host_buffer = renderer.GetTransientsBuffer();
  auto vertex_builder = CreateSolidStrokeVertices(
      path_, stroke_width, miter_limit_ * stroke_width_ * 0.5,
      GetJoinProc(stroke_join_), GetCapProc(stroke_cap_),
      entity.GetTransform().GetMaxBasisLength());

  return GeometryResult{
      .type = PrimitiveType::kTriangleStrip,
      .vertex_buffer = vertex_builder.CreateVertexBuffer(host_buffer),
      .transform = pass.GetOrthographicTransform() * entity.GetTransform(),
      .prevent_overdraw = true,
  };
}

GeometryResult StrokePathGeometry::GetPositionUVBuffer(
    Rect texture_coverage,
    Matrix effect_transform,
    const ContentContext& renderer,
    const Entity& entity,
    RenderPass& pass) const {
  if (stroke_width_ < 0.0) {
    return {};
  }
  auto determinant = entity.GetTransform().GetDeterminant();
  if (determinant == 0) {
    return {};
  }

  Scalar min_size = 1.0f / sqrt(std::abs(determinant));
  Scalar stroke_width = std::max(stroke_width_, min_size);

  auto& host_buffer = renderer.GetTransientsBuffer();
  auto stroke_builder = CreateSolidStrokeVertices(
      path_, stroke_width, miter_limit_ * stroke_width_ * 0.5,
      GetJoinProc(stroke_join_), GetCapProc(stroke_cap_),
      entity.GetTransform().GetMaxBasisLength());
  auto vertex_builder = ComputeUVGeometryCPU(
      stroke_builder, {0, 0}, texture_coverage.GetSize(), effect_transform);

  return GeometryResult{
      .type = PrimitiveType::kTriangleStrip,
      .vertex_buffer = vertex_builder.CreateVertexBuffer(host_buffer),
      .transform = pass.GetOrthographicTransform() * entity.GetTransform(),
      .prevent_overdraw = true,
  };
}

GeometryVertexType StrokePathGeometry::GetVertexType() const {
  return GeometryVertexType::kPosition;
}

std::optional<Rect> StrokePathGeometry::GetCoverage(
    const Matrix& transform) const {
  auto path_bounds = path_.GetBoundingBox();
  if (!path_bounds.has_value()) {
    return std::nullopt;
  }

  Scalar max_radius = 0.5;
  if (stroke_cap_ == Cap::kSquare) {
    max_radius = max_radius * kSqrt2;
  }
  if (stroke_join_ == Join::kMiter) {
    max_radius = std::max(max_radius, miter_limit_ * 0.5f);
  }
  Scalar determinant = transform.GetDeterminant();
  if (determinant == 0) {
    return std::nullopt;
  }
  Scalar min_size = 1.0f / sqrt(std::abs(determinant));
  max_radius *= std::max(stroke_width_, min_size);
  return path_bounds->Expand(max_radius).TransformBounds(transform);
}

}  // namespace impeller
