"""Read-only UE 5.8 validation for the four shared bird portrait textures."""

from __future__ import annotations

import json
import os

import unreal


ASSET_PATHS = (
    "/Game/Icons/Birds/T_Icon_Bird_Red",
    "/Game/Icons/Birds/T_Icon_Bird_Blue",
    "/Game/Icons/Birds/T_Icon_Bird_Yellow",
    "/Game/Icons/Birds/T_Icon_Bird_Black",
)


def _export_png(asset: unreal.Texture2D, output_path: str) -> bool:
    task = unreal.AssetExportTask()
    task.object = asset
    task.filename = output_path
    task.automated = True
    task.prompt = False
    task.replace_identical = True
    task.use_file_archive = False
    task.write_empty_files = False
    task.exporter = unreal.TextureExporterPNG()
    return bool(unreal.Exporter.run_asset_export_task(task))


def _property_string(asset: unreal.Texture2D, property_name: str) -> str:
    try:
        return str(asset.get_editor_property(property_name))
    except Exception as exc:
        return f"<unavailable: {exc}>"


def main() -> None:
    project_dir = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    output_path = os.path.join(
        project_dir, "Saved", "Diagnostics", "M4BirdPortraitAssets.json"
    )
    export_dir = os.path.join(
        project_dir, "Saved", "Diagnostics", "M4BirdPortraitAssets"
    )
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    os.makedirs(export_dir, exist_ok=True)

    records = []
    failures = []
    for asset_path in ASSET_PATHS:
        asset = unreal.load_asset(asset_path)
        if not isinstance(asset, unreal.Texture2D):
            failures.append(f"Not a Texture2D: {asset_path}")
            continue
        record = {
            "asset": asset_path,
            "object_path": asset.get_path_name(),
            "size_x": asset.blueprint_get_size_x(),
            "size_y": asset.blueprint_get_size_y(),
            "srgb": bool(asset.get_editor_property("srgb")),
            "never_stream": bool(asset.get_editor_property("never_stream")),
            "compression_settings": _property_string(asset, "compression_settings"),
            "lod_group": _property_string(asset, "lod_group"),
            "mip_gen_settings": _property_string(asset, "mip_gen_settings"),
            "filter": _property_string(asset, "filter"),
        }
        export_path = os.path.join(
            export_dir, f"{asset.get_name()}.png"
        )
        record["export_png"] = export_path
        record["export_succeeded"] = _export_png(asset, export_path)
        if not record["export_succeeded"]:
            failures.append(f"PNG export failed: {asset_path}")
        records.append(record)
        unreal.log(
            "[ABTS][M4UI][PortraitAsset] "
            f"Path={asset_path} Size={record['size_x']}x{record['size_y']} "
            f"SRGB={int(record['srgb'])} NeverStream={int(record['never_stream'])} "
            f"LOD={record['lod_group']} Compression={record['compression_settings']}"
        )

    result = {"assets": records, "failures": failures}
    with open(output_path, "w", encoding="utf-8") as output_file:
        json.dump(result, output_file, ensure_ascii=False, indent=2)
    if failures or len(records) != len(ASSET_PATHS):
        raise RuntimeError(f"Bird portrait validation failed: {failures}")
    unreal.log(
        f"[ABTS][M4UI][PortraitAsset] Complete=1 Count={len(records)} Output={output_path}"
    )


if __name__ == "__main__":
    main()
