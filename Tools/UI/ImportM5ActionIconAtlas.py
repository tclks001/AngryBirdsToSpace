"""Import and configure the generated M5 action-icon atlas with UE 5.8."""

from __future__ import annotations

import os

import unreal


ASSET_DIRECTORY = "/Game/UI/Icons"
ASSET_NAME = "T_ABTS_ActionIconAtlas_v001"
ASSET_PATH = f"{ASSET_DIRECTORY}/{ASSET_NAME}"
SOURCE_RELATIVE_PATH = os.path.join(
    "SourceArt", "UI", "ABTS_UI_ActionIconAtlas_v001.png"
)


def _set_enum_property(asset: unreal.Texture2D, property_name: str, enum_type, *names: str) -> None:
    for name in names:
        value = getattr(enum_type, name, None)
        if value is not None:
            asset.set_editor_property(property_name, value)
            return
    raise RuntimeError(f"UE 5.8 enum value missing for {property_name}: {names}")


def main() -> None:
    project_directory = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    source_filename = os.path.normpath(os.path.join(project_directory, SOURCE_RELATIVE_PATH))
    if not os.path.isfile(source_filename):
        raise RuntimeError(f"Action icon atlas source is missing: {source_filename}")

    unreal.EditorAssetLibrary.make_directory(ASSET_DIRECTORY)
    if not unreal.EditorAssetLibrary.does_asset_exist(ASSET_PATH):
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", source_filename)
        task.set_editor_property("destination_path", ASSET_DIRECTORY)
        task.set_editor_property("destination_name", ASSET_NAME)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", False)
        task.set_editor_property("save", False)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        imported_object_paths = task.get_editor_property("imported_object_paths")
        expected_object_path = f"{ASSET_PATH}.{ASSET_NAME}"
        if expected_object_path not in imported_object_paths:
            raise RuntimeError(
                f"Atlas import did not produce {expected_object_path}: "
                f"{imported_object_paths}"
            )

    atlas = unreal.load_asset(ASSET_PATH)
    if not isinstance(atlas, unreal.Texture2D):
        raise RuntimeError(f"Imported atlas is not Texture2D: {ASSET_PATH}")

    atlas.modify()
    atlas.set_editor_property("srgb", True)
    atlas.set_editor_property("never_stream", True)
    _set_enum_property(
        atlas,
        "compression_settings",
        unreal.TextureCompressionSettings,
        "TC_EDITOR_ICON",
        "TC_USER_INTERFACE_2D",
    )
    _set_enum_property(
        atlas,
        "lod_group",
        unreal.TextureGroup,
        "TEXTUREGROUP_UI",
    )
    _set_enum_property(
        atlas,
        "mip_gen_settings",
        unreal.TextureMipGenSettings,
        "TMGS_NO_MIPMAPS",
    )
    _set_enum_property(
        atlas,
        "filter",
        unreal.TextureFilter,
        "TF_BILINEAR",
    )
    if not unreal.EditorAssetLibrary.save_loaded_asset(atlas, only_if_is_dirty=False):
        raise RuntimeError(f"Failed to save action icon atlas: {ASSET_PATH}")

    unreal.log(
        "[ABTS][M5UI][ActionAtlas] "
        f"Ready=1 Asset={ASSET_PATH} Source={source_filename} "
        f"Size={atlas.blueprint_get_size_x()}x{atlas.blueprint_get_size_y()} "
        f"SRGB={int(atlas.get_editor_property('srgb'))} "
        f"NeverStream={int(atlas.get_editor_property('never_stream'))}"
    )


if __name__ == "__main__":
    main()
