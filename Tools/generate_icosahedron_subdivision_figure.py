#!/usr/bin/env python3
"""
Generate a perspective wireframe of a once-subdivided regular icosahedron.

The script has no numerical-library dependency. SVG output always works; PNG
output uses Pillow when it is available.

Default output:
    Docs/DefenseAssets/icosahedron-subdivision-1.svg
    Docs/DefenseAssets/icosahedron-subdivision-1.png

Examples:
    python Tools/generate_icosahedron_subdivision_figure.py
    python Tools/generate_icosahedron_subdivision_figure.py --style uniform
    python Tools/generate_icosahedron_subdivision_figure.py --hidden omit
    python Tools/generate_icosahedron_subdivision_figure.py --background transparent
    python Tools/generate_icosahedron_subdivision_figure.py --validate-only
"""

from __future__ import annotations

import argparse
import html
import math
from collections import defaultdict
from pathlib import Path
from typing import Iterable, NamedTuple, Sequence


Vec3 = tuple[float, float, float]
Vec2 = tuple[float, float]
Face = tuple[int, int, int]
Edge = tuple[int, int]


class ProjectedVertex(NamedTuple):
    x: float
    y: float
    depth: float


def add(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def subtract(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def scale(v: Vec3, scalar: float) -> Vec3:
    return (v[0] * scalar, v[1] * scalar, v[2] * scalar)


def dot(a: Vec3, b: Vec3) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a: Vec3, b: Vec3) -> Vec3:
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def length(v: Vec3) -> float:
    return math.sqrt(dot(v, v))


def normalize(v: Vec3) -> Vec3:
    magnitude = length(v)
    if magnitude <= 1.0e-12:
        raise ValueError("Cannot normalize a zero-length vector")
    return scale(v, 1.0 / magnitude)


def canonical_edge(a: int, b: int) -> Edge:
    return (a, b) if a < b else (b, a)


def face_edges(face: Face) -> tuple[Edge, Edge, Edge]:
    a, b, c = face
    return (
        canonical_edge(a, b),
        canonical_edge(b, c),
        canonical_edge(c, a),
    )


def orient_face_outward(vertices: Sequence[Vec3], face: Face) -> Face:
    a, b, c = face
    va, vb, vc = vertices[a], vertices[b], vertices[c]
    normal = cross(subtract(vb, va), subtract(vc, va))
    centroid = scale(add(add(va, vb), vc), 1.0 / 3.0)
    return face if dot(normal, centroid) > 0.0 else (a, c, b)


def build_icosahedron() -> tuple[list[Vec3], list[Face], set[Edge]]:
    """Build a unit-radius icosahedron and derive its faces from adjacency."""
    phi = (1.0 + math.sqrt(5.0)) / 2.0
    raw_vertices: list[Vec3] = [
        (0.0, -1.0, -phi),
        (0.0, -1.0, phi),
        (0.0, 1.0, -phi),
        (0.0, 1.0, phi),
        (-1.0, -phi, 0.0),
        (-1.0, phi, 0.0),
        (1.0, -phi, 0.0),
        (1.0, phi, 0.0),
        (-phi, 0.0, -1.0),
        (phi, 0.0, -1.0),
        (-phi, 0.0, 1.0),
        (phi, 0.0, 1.0),
    ]
    vertices = [normalize(vertex) for vertex in raw_vertices]

    distances = [
        length(subtract(vertices[i], vertices[j]))
        for i in range(len(vertices))
        for j in range(i + 1, len(vertices))
    ]
    edge_length = min(distance for distance in distances if distance > 1.0e-9)
    tolerance = 1.0e-8
    edges = {
        (i, j)
        for i in range(len(vertices))
        for j in range(i + 1, len(vertices))
        if abs(length(subtract(vertices[i], vertices[j])) - edge_length) < tolerance
    }

    faces: list[Face] = []
    for i in range(len(vertices)):
        for j in range(i + 1, len(vertices)):
            for k in range(j + 1, len(vertices)):
                if (
                    canonical_edge(i, j) in edges
                    and canonical_edge(j, k) in edges
                    and canonical_edge(k, i) in edges
                ):
                    faces.append(orient_face_outward(vertices, (i, j, k)))

    return vertices, faces, edges


def subdivide_once(
    base_vertices: Sequence[Vec3],
    base_faces: Sequence[Face],
) -> tuple[list[Vec3], list[Face], set[Edge], set[Edge]]:
    """
    Split every triangular face into four triangles.

    Midpoints are normalized back onto the unit sphere. The returned
    parent_segments set contains the two child segments lying on each original
    icosahedron edge, which lets the teaching style show the parent topology.
    """
    vertices = list(base_vertices)
    midpoint_cache: dict[Edge, int] = {}

    def midpoint_index(a: int, b: int) -> int:
        edge = canonical_edge(a, b)
        cached = midpoint_cache.get(edge)
        if cached is not None:
            return cached

        midpoint = normalize(scale(add(vertices[a], vertices[b]), 0.5))
        index = len(vertices)
        vertices.append(midpoint)
        midpoint_cache[edge] = index
        return index

    faces: list[Face] = []
    for a, b, c in base_faces:
        ab = midpoint_index(a, b)
        bc = midpoint_index(b, c)
        ca = midpoint_index(c, a)
        faces.extend(
            (
                (a, ab, ca),
                (b, bc, ab),
                (c, ca, bc),
                (ab, bc, ca),
            )
        )

    faces = [orient_face_outward(vertices, face) for face in faces]
    edges = {edge for face in faces for edge in face_edges(face)}
    parent_segments: set[Edge] = set()
    for edge, midpoint in midpoint_cache.items():
        a, b = edge
        parent_segments.add(canonical_edge(a, midpoint))
        parent_segments.add(canonical_edge(midpoint, b))

    return vertices, faces, edges, parent_segments


def validate_mesh(
    base_vertices: Sequence[Vec3],
    base_faces: Sequence[Face],
    base_edges: set[Edge],
    vertices: Sequence[Vec3],
    faces: Sequence[Face],
    edges: set[Edge],
    parent_segments: set[Edge],
) -> None:
    expected = {
        "base vertices": (len(base_vertices), 12),
        "base faces": (len(base_faces), 20),
        "base edges": (len(base_edges), 30),
        "subdivided vertices": (len(vertices), 42),
        "subdivided faces": (len(faces), 80),
        "subdivided edges": (len(edges), 120),
        "parent edge segments": (len(parent_segments), 60),
    }
    failures = [
        f"{label}: got {actual}, expected {target}"
        for label, (actual, target) in expected.items()
        if actual != target
    ]

    incidence: dict[Edge, int] = defaultdict(int)
    for face in faces:
        for edge in face_edges(face):
            incidence[edge] += 1
    non_manifold = [edge for edge, count in incidence.items() if count != 2]
    if non_manifold:
        failures.append(f"{len(non_manifold)} edges do not have two incident faces")

    if len(vertices) - len(edges) + len(faces) != 2:
        failures.append("Euler characteristic V-E+F is not 2")

    off_sphere = [
        index
        for index, vertex in enumerate(vertices)
        if abs(length(vertex) - 1.0) > 1.0e-9
    ]
    if off_sphere:
        failures.append(f"{len(off_sphere)} vertices are not on the unit sphere")

    if failures:
        raise AssertionError("Mesh validation failed:\n- " + "\n- ".join(failures))


def camera_position(azimuth_degrees: float, elevation_degrees: float, distance: float) -> Vec3:
    azimuth = math.radians(azimuth_degrees)
    elevation = math.radians(elevation_degrees)
    return (
        distance * math.cos(elevation) * math.cos(azimuth),
        distance * math.cos(elevation) * math.sin(azimuth),
        distance * math.sin(elevation),
    )


def project_vertices(
    vertices: Sequence[Vec3],
    camera: Vec3,
    field_of_view_degrees: float,
    canvas_size: int,
) -> list[ProjectedVertex]:
    forward = normalize(scale(camera, -1.0))
    world_up: Vec3 = (0.0, 0.0, 1.0)
    right_raw = cross(forward, world_up)
    if length(right_raw) < 1.0e-8:
        world_up = (0.0, 1.0, 0.0)
        right_raw = cross(forward, world_up)
    right = normalize(right_raw)
    up = normalize(cross(right, forward))
    focal_length = 1.0 / math.tan(math.radians(field_of_view_degrees) * 0.5)

    projected: list[ProjectedVertex] = []
    canvas_center = canvas_size * 0.5
    for vertex in vertices:
        relative = subtract(vertex, camera)
        depth = dot(relative, forward)
        if depth <= 1.0e-6:
            raise ValueError("A vertex is behind the camera; increase --distance")
        projected_x = dot(relative, right) * focal_length / depth
        projected_y = dot(relative, up) * focal_length / depth
        projected.append(
            ProjectedVertex(
                canvas_center * (1.0 + projected_x),
                canvas_center * (1.0 - projected_y),
                depth,
            )
        )

    if any(
        point.x < 0.0
        or point.x > canvas_size
        or point.y < 0.0
        or point.y > canvas_size
        for point in projected
    ):
        raise ValueError(
            "Projection does not fit the canvas; increase --fov or --distance"
        )
    return projected


def edge_visibility(
    vertices: Sequence[Vec3],
    faces: Sequence[Face],
    edges: Iterable[Edge],
    camera: Vec3,
) -> dict[Edge, bool]:
    front_facing: list[bool] = []
    for a, b, c in faces:
        va, vb, vc = vertices[a], vertices[b], vertices[c]
        normal = cross(subtract(vb, va), subtract(vc, va))
        centroid = scale(add(add(va, vb), vc), 1.0 / 3.0)
        front_facing.append(dot(normal, subtract(camera, centroid)) > 0.0)

    incident_faces: dict[Edge, list[int]] = defaultdict(list)
    for face_index, face in enumerate(faces):
        for edge in face_edges(face):
            incident_faces[edge].append(face_index)

    return {
        edge: any(front_facing[index] for index in incident_faces[edge])
        for edge in edges
    }


def xml_escape(value: str) -> str:
    return html.escape(value, quote=True)


def render_svg(
    output_path: Path,
    size: int,
    projected: Sequence[ProjectedVertex],
    edges: set[Edge],
    parent_segments: set[Edge],
    visibility: dict[Edge, bool],
    style: str,
    hidden_mode: str,
    background: str,
    show_vertices: bool,
) -> None:
    line_unit = size / 1800.0
    colors = {
        "parent": "#102A43",
        "child": "#2F80C9",
        "hidden": "#8FA2B8",
        "base_vertex": "#E46C2A",
        "midpoint": "#2F80C9",
    }
    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        (
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{size}" height="{size}" '
            f'viewBox="0 0 {size} {size}" role="img" '
            'aria-labelledby="title description">'
        ),
        "  <title id=\"title\">正二十面体一次递归细分透视线框</title>",
        (
            "  <desc id=\"description\">标准正二十面体每个三角面被分成四个三角面，"
            "顶点归一化回单位球面；共 42 个顶点、80 个三角面和 120 条边。</desc>"
        ),
    ]
    if background.lower() != "transparent":
        lines.append(
            f'  <rect width="{size}" height="{size}" fill="{xml_escape(background)}"/>'
        )

    visible_vertices = {
        index
        for edge in edges
        if visibility[edge]
        for index in edge
    }

    def append_vertex(index: int, visible: bool) -> None:
        point = projected[index]
        if visible:
            if index < 12:
                radius = 6.0 * line_unit
                fill = colors["base_vertex"]
                stroke = colors["parent"]
                stroke_width = 1.3 * line_unit
            else:
                radius = 3.2 * line_unit
                fill = colors["midpoint"]
                stroke = "#FFFFFF"
                stroke_width = 0.9 * line_unit
            opacity = "1"
        else:
            radius = (3.7 if index < 12 else 2.5) * line_unit
            fill = colors["hidden"]
            stroke = "none"
            stroke_width = 0.0
            opacity = "0.38"
        lines.append(
            "  "
            f'<circle cx="{point.x:.3f}" cy="{point.y:.3f}" r="{radius:.3f}" '
            f'fill="{fill}" stroke="{stroke}" stroke-width="{stroke_width:.3f}" '
            f'opacity="{opacity}"/>'
        )

    if show_vertices and hidden_mode != "omit":
        for index, point in sorted(
            enumerate(projected),
            key=lambda item: item[1].depth,
            reverse=True,
        ):
            if index not in visible_vertices:
                append_vertex(index, False)

    def append_edge(edge: Edge, visible: bool) -> None:
        a, b = edge
        pa, pb = projected[a], projected[b]
        is_parent = edge in parent_segments
        if style == "uniform":
            color = colors["child"] if visible else colors["hidden"]
            width = 2.6 if visible else 1.8
        else:
            color = colors["parent"] if is_parent and visible else colors["child"]
            width = 4.0 if is_parent and visible else 2.3
            if not visible:
                color = colors["hidden"]
                width = 1.8

        dash = ""
        opacity = "1"
        if not visible:
            if hidden_mode == "omit":
                return
            if hidden_mode == "dashed":
                dash = f' stroke-dasharray="{8.0 * line_unit:.2f} {8.0 * line_unit:.2f}"'
            opacity = "0.46"

        lines.append(
            "  "
            f'<line x1="{pa.x:.3f}" y1="{pa.y:.3f}" '
            f'x2="{pb.x:.3f}" y2="{pb.y:.3f}" '
            f'stroke="{color}" stroke-width="{width * line_unit:.3f}" '
            f'stroke-linecap="round" opacity="{opacity}"{dash}/>'
        )

    hidden_edges = sorted(edge for edge in edges if not visibility[edge])
    visible_edges = sorted(edge for edge in edges if visibility[edge])
    for edge in hidden_edges:
        append_edge(edge, False)
    for edge in visible_edges:
        append_edge(edge, True)

    if show_vertices:
        for index, point in sorted(
            enumerate(projected),
            key=lambda item: item[1].depth,
            reverse=True,
        ):
            if index in visible_vertices:
                append_vertex(index, True)

    lines.append("</svg>")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def parse_hex_color(value: str, alpha: int = 255) -> tuple[int, int, int, int]:
    if value.lower() == "transparent":
        return (255, 255, 255, 0)
    if not value.startswith("#") or len(value) not in (4, 7):
        raise ValueError(f"PNG colors must use #RGB or #RRGGBB, got: {value}")
    digits = value[1:]
    if len(digits) == 3:
        digits = "".join(character * 2 for character in digits)
    return (
        int(digits[0:2], 16),
        int(digits[2:4], 16),
        int(digits[4:6], 16),
        alpha,
    )


def dashed_segments(
    start: Vec2,
    end: Vec2,
    dash_length: float,
    gap_length: float,
) -> Iterable[tuple[Vec2, Vec2]]:
    dx = end[0] - start[0]
    dy = end[1] - start[1]
    total = math.hypot(dx, dy)
    if total <= 1.0e-9:
        return
    ux, uy = dx / total, dy / total
    cursor = 0.0
    while cursor < total:
        segment_end = min(cursor + dash_length, total)
        yield (
            (start[0] + ux * cursor, start[1] + uy * cursor),
            (start[0] + ux * segment_end, start[1] + uy * segment_end),
        )
        cursor += dash_length + gap_length


def render_png(
    output_path: Path,
    size: int,
    projected: Sequence[ProjectedVertex],
    edges: set[Edge],
    parent_segments: set[Edge],
    visibility: dict[Edge, bool],
    style: str,
    hidden_mode: str,
    background: str,
    show_vertices: bool,
) -> None:
    try:
        from PIL import Image, ImageDraw
    except ImportError as error:
        raise RuntimeError(
            "PNG output requires Pillow. Install it with: python -m pip install Pillow"
        ) from error

    supersampling = 4
    render_size = size * supersampling
    image = Image.new("RGBA", (render_size, render_size), parse_hex_color(background))
    draw = ImageDraw.Draw(image, "RGBA")

    colors = {
        "parent": parse_hex_color("#102A43"),
        "child": parse_hex_color("#2F80C9"),
        "hidden": parse_hex_color("#8FA2B8", 116),
        "base_vertex": parse_hex_color("#E46C2A"),
        "midpoint": parse_hex_color("#2F80C9"),
        "white": parse_hex_color("#FFFFFF"),
    }

    def point(index: int) -> Vec2:
        vertex = projected[index]
        return (vertex.x * supersampling, vertex.y * supersampling)

    visible_vertices = {
        index
        for edge in edges
        if visibility[edge]
        for index in edge
    }

    def draw_vertex(index: int, visible: bool) -> None:
        vertex = projected[index]
        center = (vertex.x * supersampling, vertex.y * supersampling)
        if visible and index < 12:
            radius = 6.0 * supersampling * size / 1800.0
            fill = colors["base_vertex"]
            outline = colors["parent"]
            outline_width = max(1, round(1.3 * supersampling * size / 1800.0))
        elif visible:
            radius = 3.2 * supersampling * size / 1800.0
            fill = colors["midpoint"]
            outline = colors["white"]
            outline_width = max(1, round(0.9 * supersampling * size / 1800.0))
        else:
            radius = (3.7 if index < 12 else 2.5) * supersampling * size / 1800.0
            fill = colors["hidden"]
            outline = None
            outline_width = 0
        box = (
            center[0] - radius,
            center[1] - radius,
            center[0] + radius,
            center[1] + radius,
        )
        draw.ellipse(
            box,
            fill=fill,
            outline=outline,
            width=outline_width,
        )

    if show_vertices and hidden_mode != "omit":
        for index, vertex in sorted(
            enumerate(projected),
            key=lambda item: item[1].depth,
            reverse=True,
        ):
            if index not in visible_vertices:
                draw_vertex(index, False)

    def draw_edge(edge: Edge, visible: bool) -> None:
        is_parent = edge in parent_segments
        if style == "uniform":
            color = colors["child"] if visible else colors["hidden"]
            width = 3 if visible else 2
        else:
            color = colors["parent"] if is_parent and visible else colors["child"]
            width = 4 if is_parent and visible else 2
            if not visible:
                color = colors["hidden"]
                width = 2
        width_pixels = max(1, round(width * supersampling * size / 1800.0))
        start, end = point(edge[0]), point(edge[1])

        if visible or hidden_mode == "solid":
            draw.line((start, end), fill=color, width=width_pixels)
        elif hidden_mode == "dashed":
            dash = 8.0 * supersampling * size / 1800.0
            for segment_start, segment_end in dashed_segments(start, end, dash, dash):
                draw.line(
                    (segment_start, segment_end),
                    fill=color,
                    width=width_pixels,
                )

    for edge in sorted(edge for edge in edges if not visibility[edge]):
        if hidden_mode != "omit":
            draw_edge(edge, False)
    for edge in sorted(edge for edge in edges if visibility[edge]):
        draw_edge(edge, True)

    if show_vertices:
        for index, vertex in sorted(
            enumerate(projected),
            key=lambda item: item[1].depth,
            reverse=True,
        ):
            if index in visible_vertices:
                draw_vertex(index, True)

    resampling = getattr(Image, "Resampling", Image).LANCZOS
    image = image.resize((size, size), resampling)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    image.save(output_path, "PNG", optimize=True)


def parse_arguments() -> argparse.Namespace:
    repository_root = Path(__file__).resolve().parents[1]
    default_output = (
        repository_root
        / "Docs"
        / "DefenseAssets"
        / "icosahedron-subdivision-1"
    )
    parser = argparse.ArgumentParser(
        description="生成正二十面体一次球面递归细分的透视线框 SVG/PNG。"
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=default_output,
        help="输出文件的基础路径；扩展名会被替换为 .svg 和 .png。",
    )
    parser.add_argument("--size", type=int, default=1800, help="正方形输出边长，默认 1800。")
    parser.add_argument("--azimuth", type=float, default=32.0, help="相机方位角，单位为度。")
    parser.add_argument("--elevation", type=float, default=21.0, help="相机仰角，单位为度。")
    parser.add_argument("--distance", type=float, default=4.2, help="相机到球心的距离。")
    parser.add_argument("--fov", type=float, default=34.0, help="垂直视场角，单位为度。")
    parser.add_argument(
        "--style",
        choices=("teaching", "uniform"),
        default="teaching",
        help="teaching 强调原始边与原始顶点；uniform 使用统一线型。",
    )
    parser.add_argument(
        "--hidden",
        choices=("dashed", "omit", "solid"),
        default="dashed",
        help="背面边显示方式，默认 dashed。",
    )
    parser.add_argument(
        "--background",
        default="#FFFFFF",
        help="transparent 或 #RRGGBB；默认白色。",
    )
    parser.add_argument(
        "--no-vertices",
        action="store_true",
        help="不绘制顶点标记。",
    )
    parser.add_argument(
        "--no-png",
        action="store_true",
        help="只输出 SVG，不输出 PNG。",
    )
    parser.add_argument(
        "--validate-only",
        action="store_true",
        help="只验证拓扑计数、流形性和球面归一化，不生成文件。",
    )
    return parser.parse_args()


def validate_arguments(arguments: argparse.Namespace) -> None:
    if arguments.size < 256:
        raise ValueError("--size must be at least 256")
    if arguments.distance <= 1.05:
        raise ValueError("--distance must be greater than 1.05")
    if not 5.0 <= arguments.fov <= 120.0:
        raise ValueError("--fov must be in [5, 120] degrees")
    if arguments.background.lower() != "transparent":
        parse_hex_color(arguments.background)


def main() -> int:
    arguments = parse_arguments()
    validate_arguments(arguments)

    base_vertices, base_faces, base_edges = build_icosahedron()
    vertices, faces, edges, parent_segments = subdivide_once(
        base_vertices,
        base_faces,
    )
    validate_mesh(
        base_vertices,
        base_faces,
        base_edges,
        vertices,
        faces,
        edges,
        parent_segments,
    )

    if arguments.validate_only:
        print(
            "Topology OK: "
            f"V={len(vertices)} E={len(edges)} F={len(faces)} "
            f"Euler={len(vertices) - len(edges) + len(faces)}"
        )
        return 0

    camera = camera_position(
        arguments.azimuth,
        arguments.elevation,
        arguments.distance,
    )
    projected = project_vertices(
        vertices,
        camera,
        arguments.fov,
        arguments.size,
    )
    visibility = edge_visibility(vertices, faces, edges, camera)

    output_base = arguments.output
    if output_base.suffix.lower() in (".svg", ".png"):
        output_base = output_base.with_suffix("")
    svg_path = output_base.with_suffix(".svg")
    png_path = output_base.with_suffix(".png")

    render_svg(
        svg_path,
        arguments.size,
        projected,
        edges,
        parent_segments,
        visibility,
        arguments.style,
        arguments.hidden,
        arguments.background,
        not arguments.no_vertices,
    )
    print(f"Wrote {svg_path}")

    if not arguments.no_png:
        render_png(
            png_path,
            arguments.size,
            projected,
            edges,
            parent_segments,
            visibility,
            arguments.style,
            arguments.hidden,
            arguments.background,
            not arguments.no_vertices,
        )
        print(f"Wrote {png_path}")

    visible_count = sum(1 for edge in edges if visibility[edge])
    print(
        "Topology: "
        f"V={len(vertices)} E={len(edges)} F={len(faces)}; "
        f"visible edges={visible_count}, hidden edges={len(edges) - visible_count}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
