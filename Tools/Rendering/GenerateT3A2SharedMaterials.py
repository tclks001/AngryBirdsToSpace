"""Generate the Integration-owned T3-A2 CuteBird and slingshot materials.

Run only through the project's pinned UE 5.8 UnrealEditor-Cmd. The script is
idempotent: it updates the four owned master graphs and the fixed instance set,
but never edits the accepted source materials.
"""

import os

import unreal


ROOT = "/Game/Toon/Shared"
MASTER_ROOT = ROOT + "/Masters"
BIRD_ROOT = ROOT + "/Birds"
SLINGSHOT_ROOT = ROOT + "/Slingshot"

STYLE = "ABTS_StyleEnabled"
TINT = "ABTS_BaseColorTint"
ROUGHNESS_FLOOR = "ABTS_RoughnessFloor"
ROUGHNESS_SCALE = "ABTS_RoughnessScale"
SPECULAR_SCALE = "ABTS_SpecularScale"
METALLIC_SCALE = "ABTS_MetallicScale"
RIM_STRENGTH = "ABTS_RimStrength"
RIM_POWER = "ABTS_RimPower"

SOURCE_COLOR_TEXTURE = "ABTS_SourceColorTexture"
SOURCE_NORMAL_TEXTURE = "ABTS_SourceNormalTexture"
SOURCE_ROUGHNESS_TEXTURE = "ABTS_SourceRoughnessTexture"
SOURCE_COLOR = "ABTS_SourceColor"
SOURCE_ROUGHNESS = "ABTS_SourceRoughness"
SOURCE_SPECULAR = "ABTS_SourceSpecular"
SOURCE_METALLIC = "ABTS_SourceMetallic"


BODY_VARIANTS = {
    "Red": "/Game/CuteBird/Textures/CuteBirdColor_Textures/T_Cutebird_12",
    "Blue": "/Game/CuteBird/Textures/CuteBirdColor_Textures/T_Cutebird_03",
    "Yellow": "/Game/CuteBird/Textures/CuteBirdColor_Textures/T_Cutebird_10",
    "Black": "/Game/CuteBird/Textures/CuteBirdColor_Textures/T_Cutebird_16",
    "White": "/Game/CuteBird/Textures/CuteBirdColor_Textures/T_Cutebird_00",
}

FACE_VARIANTS = {
    "Red": "/Game/CuteBird/Textures/Face_Textures/T_Dino_Face23",
    "Blue": "/Game/CuteBird/Textures/Face_Textures/T_Dino_Face03",
    "Yellow": "/Game/CuteBird/Textures/Face_Textures/T_Dino_Face06",
    "Black": "/Game/CuteBird/Textures/Face_Textures/T_Dino_Face17",
    "White": "/Game/CuteBird/Textures/Face_Textures/T_Dino_Face01",
}

SLINGSHOT_VARIANTS = {
    "Twig": {
        "family": "organic",
        "stake_color": "/Game/StaticMesh/Stake/Twig/T_Stake_Twig_Basecolor",
        "stake_normal": "/Game/StaticMesh/Stake/Twig/T_Stake_Twig_Normal",
        "stake_roughness": "/Game/StaticMesh/Stake/Twig/T_Stake_Twig_Roughness",
        "pouch_color": "/Game/StaticMesh/Pouch/Twig/T_Pouch_Twig_Basecolor",
        "pouch_normal": "/Game/StaticMesh/Pouch/Twig/T_Pouch_Twig_Normal",
        "pouch_roughness": "/Game/StaticMesh/Pouch/Twig/T_Pouch_Twig_Roughness",
        "cord_color": unreal.LinearColor(0.070362, 0.140625, 0.027432, 1.0),
        "cord_roughness": 0.5,
        "cord_metallic": 0.0,
    },
    "Simple": {
        "family": "organic",
        "stake_color": "/Game/StaticMesh/Stake/Simple/T_Stake_Simple_Basecolor",
        "stake_normal": "/Game/StaticMesh/Stake/Simple/T_Stake_Simple_Normal",
        "stake_roughness": "/Game/StaticMesh/Stake/Simple/T_Stake_Simple_Roughness",
        "pouch_color": "/Game/StaticMesh/Pouch/Simple/T_Pouch_Simple_Basecolor",
        "pouch_normal": "/Game/StaticMesh/Pouch/Simple/T_Pouch_Simple_Normal",
        "pouch_roughness": "/Game/StaticMesh/Pouch/Simple/T_Pouch_Simple_Roughness",
        "cord_color": unreal.LinearColor(0.692708, 0.402239, 0.102908, 1.0),
        "cord_roughness": 0.8,
        "cord_metallic": 0.0,
    },
    "Reinforced": {
        "family": "organic",
        "stake_color": "/Game/StaticMesh/Stake/Reinforced/T_Stack_Reinforced_Basecolor",
        "stake_normal": "/Game/StaticMesh/Stake/Reinforced/T_Stack_Reinforced_Normal",
        "stake_roughness": "/Game/StaticMesh/Stake/Reinforced/T_Stack_Reinforced_Roughness",
        "pouch_color": "/Game/StaticMesh/Pouch/Reinforced/T_Pouch_Reinforced_Basecolor",
        "pouch_normal": "/Game/StaticMesh/Pouch/Reinforced/T_Pouch_Reinforced_Normal",
        "pouch_roughness": "/Game/StaticMesh/Pouch/Reinforced/T_Pouch_Reinforced_Roughness",
        "cord_color": unreal.LinearColor(0.041667, 0.024195, 0.006190, 1.0),
        "cord_roughness": 0.8,
        "cord_metallic": 0.0,
    },
    "Steel": {
        "family": "metal",
        "stake_color": "/Game/StaticMesh/Stake/Steel/T_Stack_Steel_Basecolor",
        "stake_normal": "/Game/StaticMesh/Stake/Steel/T_Stack_Steel_Normal",
        "stake_roughness": "/Game/StaticMesh/Stake/Steel/T_Stack_Steel_Roughness",
        "pouch_color": "/Game/StaticMesh/Pouch/Steel/T_Pouch_Steel_Basecolor",
        "pouch_normal": "/Game/StaticMesh/Pouch/Steel/T_Pouch_Steel_Normal",
        "pouch_roughness": "/Game/StaticMesh/Pouch/Steel/T_Pouch_Steel_Roughness",
        "cord_color": unreal.LinearColor(0.484375, 0.589945, 1.0, 1.0),
        "cord_roughness": 0.1,
        "cord_metallic": 1.0,
    },
}


FAMILY_DEFAULTS = {
    "body": (0.68, 0.28, 1.0, 0.12, 5.0),
    "face": (0.72, 0.20, 1.0, 0.00, 4.0),
    "organic": (0.78, 0.20, 1.0, 0.00, 4.0),
    "metal": (0.36, 0.58, 0.92, 0.10, 8.0),
}


asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
FORCE_REBUILD = os.environ.get("ABTS_T3A2_REBUILD", "0") == "1"


def expected_asset_paths():
    paths = [
        MASTER_ROOT + "/M_ABTS_Toon_BirdBody",
        MASTER_ROOT + "/M_ABTS_Toon_BirdBody_LegacyNormalColor",
        MASTER_ROOT + "/M_ABTS_Toon_BirdFace",
        MASTER_ROOT + "/M_ABTS_Toon_SlingshotTextured",
        MASTER_ROOT + "/M_ABTS_Toon_SlingshotSolid",
    ]
    for identity in BODY_VARIANTS:
        paths.append(BIRD_ROOT + "/MI_ABTS_Toon_BirdBody_" + identity)
    for identity in FACE_VARIANTS:
        paths.append(BIRD_ROOT + "/MI_ABTS_Toon_BirdFace_" + identity)
    for tier in SLINGSHOT_VARIANTS:
        for part in ("Stake", "Cord", "Pouch"):
            paths.append(
                SLINGSHOT_ROOT
                + "/MI_ABTS_Toon_Slingshot_{}_{}".format(part, tier))
    return paths


def ensure_directory(path):
    if not unreal.EditorAssetLibrary.does_directory_exist(path):
        unreal.EditorAssetLibrary.make_directory(path)


def load_required(path):
    asset = unreal.load_asset(path)
    if asset is None:
        raise RuntimeError("Required source asset is missing: {}".format(path))
    return asset


def get_or_create_material(name):
    path = MASTER_ROOT + "/" + name
    material = unreal.load_asset(path)
    if material is None:
        material = asset_tools.create_asset(
            name, MASTER_ROOT, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        raise RuntimeError("Could not create material: {}".format(path))
    # UE 5.8 commandlet rebuilds did not reliably serialize
    # delete_all_material_expressions(): every explicit rebuild appended a
    # second graph, leaving disconnected stale sampler nodes that still made
    # shader validation fail. Delete the snapshot one expression at a time so
    # the saved graph remains truly idempotent.
    existing_expressions = list(
        unreal.MaterialEditingLibrary.get_material_expressions(material))
    for existing_expression in existing_expressions:
        unreal.MaterialEditingLibrary.delete_material_expression(
            material, existing_expression)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property(
        "shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    return material


def expression(material, cls, x, y):
    result = unreal.MaterialEditingLibrary.create_material_expression(
        material, cls, x, y)
    if result is None:
        raise RuntimeError("Could not create expression {}".format(cls))
    return result


def scalar(material, name, default, x, y):
    node = expression(material, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", float(default))
    return node


def vector(material, name, default, x, y):
    node = expression(material, unreal.MaterialExpressionVectorParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", default)
    return node


def texture_parameter(material, name, x, y, normal=False, default_texture=None):
    node = expression(
        material, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property(
        "sampler_type",
        unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL
        if normal else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    if default_texture is not None:
        node.set_editor_property("texture", load_required(default_texture))
    return node


def connect(source, source_pin, target, target_pin):
    if not unreal.MaterialEditingLibrary.connect_material_expressions(
            source, source_pin, target, target_pin):
        raise RuntimeError(
            "Could not connect {}.{} -> {}.{}".format(
                source.get_name(), source_pin, target.get_name(), target_pin))


def connect_property(source, source_pin, material_property):
    if not unreal.MaterialEditingLibrary.connect_material_property(
            source, source_pin, material_property):
        raise RuntimeError(
            "Could not connect {}.{} to {}".format(
                source.get_name(), source_pin, material_property))


def multiply(material, a, a_pin, b, b_pin, x, y):
    node = expression(material, unreal.MaterialExpressionMultiply, x, y)
    connect(a, a_pin, node, "A")
    connect(b, b_pin, node, "B")
    return node


def lerp(material, a, a_pin, b, b_pin, alpha, x, y):
    node = expression(material, unreal.MaterialExpressionLinearInterpolate, x, y)
    connect(a, a_pin, node, "A")
    connect(b, b_pin, node, "B")
    connect(alpha, "", node, "Alpha")
    return node


def build_surface_graph(material, color_node, color_pin, roughness_node,
                        roughness_pin, normal_node=None, masked=False):
    style = scalar(material, STYLE, 1.0, -1200, -700)
    tint = vector(material, TINT, unreal.LinearColor.WHITE, -1200, -560)
    rough_floor = scalar(material, ROUGHNESS_FLOOR, 0.6, -1200, -360)
    rough_scale = scalar(material, ROUGHNESS_SCALE, 1.0, -1200, -260)
    spec_scale = scalar(material, SPECULAR_SCALE, 0.35, -1200, -60)
    metallic_scale = scalar(material, METALLIC_SCALE, 1.0, -1200, 140)
    rim_strength = scalar(material, RIM_STRENGTH, 0.0, -1200, 360)
    rim_power = scalar(material, RIM_POWER, 4.0, -1200, 500)
    source_specular = scalar(material, SOURCE_SPECULAR, 0.5, -1200, 20)
    source_metallic = scalar(material, SOURCE_METALLIC, 0.0, -1200, 220)

    tinted = multiply(material, color_node, color_pin, tint, "", -620, -560)
    final_color = lerp(
        material, color_node, color_pin, tinted, "", style, -340, -520)
    connect_property(final_color, "", unreal.MaterialProperty.MP_BASE_COLOR)

    scaled_roughness = multiply(
        material, roughness_node, roughness_pin, rough_scale, "", -620, -260)
    max_roughness = expression(
        material, unreal.MaterialExpressionMax, -380, -260)
    connect(scaled_roughness, "", max_roughness, "A")
    connect(rough_floor, "", max_roughness, "B")
    final_roughness = lerp(
        material, roughness_node, roughness_pin, max_roughness, "", style,
        -120, -240)
    connect_property(final_roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)

    styled_specular = multiply(
        material, source_specular, "", spec_scale, "", -620, -20)
    final_specular = lerp(
        material, source_specular, "", styled_specular, "", style,
        -340, -20)
    connect_property(final_specular, "", unreal.MaterialProperty.MP_SPECULAR)

    styled_metallic = multiply(
        material, source_metallic, "", metallic_scale, "", -620, 180)
    final_metallic = lerp(
        material, source_metallic, "", styled_metallic, "", style,
        -340, 180)
    connect_property(final_metallic, "", unreal.MaterialProperty.MP_METALLIC)

    fresnel = expression(material, unreal.MaterialExpressionFresnel, -620, 420)
    connect(rim_power, "", fresnel, "ExponentIn")
    rim_amount = multiply(
        material, fresnel, "", rim_strength, "", -380, 420)
    enabled_rim = multiply(material, rim_amount, "", style, "", -160, 420)
    rim_color = multiply(
        material, final_color, "", enabled_rim, "", 60, 380)
    connect_property(rim_color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    if normal_node is not None:
        connect_property(normal_node, "RGB", unreal.MaterialProperty.MP_NORMAL)

    if masked:
        material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        connect_property(color_node, "A", unreal.MaterialProperty.MP_OPACITY_MASK)


def build_bird_master(
        name, masked, source_uses_normal_sampler=False,
        default_texture=None):
    material = get_or_create_material(name)
    # These masters are assigned to USkeletalMeshComponent slots at runtime.
    # Without the persisted usage bit UE deliberately substitutes the engine
    # default material in PIE, even though every material instance and texture
    # parameter is otherwise valid.
    material.set_editor_property("used_with_skeletal_mesh", True)
    color = texture_parameter(
        material,
        SOURCE_COLOR_TEXTURE,
        -1500,
        -980,
        normal=source_uses_normal_sampler,
        default_texture=default_texture)
    source_roughness = scalar(
        material, SOURCE_ROUGHNESS, 0.5, -1500, -260)
    build_surface_graph(
        material, color, "RGB", source_roughness, "", masked=masked)
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    return material


def build_textured_slingshot_master():
    material = get_or_create_material("M_ABTS_Toon_SlingshotTextured")
    # All accepted stake/pouch meshes are Nanite-enabled. These candidates are
    # first assigned when M6 enters launch mode, so relying on editor-time
    # automatic usage discovery makes the runtime assignment fall back to the
    # default material.
    material.set_editor_property("used_with_static_mesh", True)
    material.set_editor_property("used_with_nanite", True)
    color = texture_parameter(material, SOURCE_COLOR_TEXTURE, -1500, -980)
    normal = texture_parameter(
        material,
        SOURCE_NORMAL_TEXTURE,
        -1500,
        -820,
        normal=True,
        default_texture=SLINGSHOT_VARIANTS["Twig"]["stake_normal"])
    roughness = texture_parameter(
        material, SOURCE_ROUGHNESS_TEXTURE, -1500, -660)
    build_surface_graph(
        material, color, "RGB", roughness, "R", normal_node=normal)
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    return material


def build_solid_slingshot_master():
    material = get_or_create_material("M_ABTS_Toon_SlingshotSolid")
    material.set_editor_property("used_with_static_mesh", True)
    material.set_editor_property("used_with_nanite", True)
    color = vector(material, SOURCE_COLOR, unreal.LinearColor.WHITE, -1500, -980)
    roughness = scalar(material, SOURCE_ROUGHNESS, 0.5, -1500, -260)
    build_surface_graph(material, color, "", roughness, "")
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    return material


def get_or_create_instance(name, package_path, parent):
    path = package_path + "/" + name
    instance = unreal.load_asset(path)
    if instance is None:
        instance = asset_tools.create_asset(
            name,
            package_path,
            unreal.MaterialInstanceConstant,
            unreal.MaterialInstanceConstantFactoryNew())
    if instance is None:
        raise RuntimeError("Could not create material instance: {}".format(path))
    instance.set_editor_property("parent", parent)
    return instance


def set_scalar(instance, name, value):
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
        instance, name, float(value))


def set_vector(instance, name, value):
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
        instance, name, value)


def set_texture(instance, name, asset_path):
    texture = load_required(asset_path)
    unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(
        instance, name, texture)


def apply_family_defaults(instance, family):
    rough_floor, spec_scale, metallic_scale, rim_strength, rim_power = (
        FAMILY_DEFAULTS[family])
    set_scalar(instance, STYLE, 1.0)
    set_vector(instance, TINT, unreal.LinearColor.WHITE)
    set_scalar(instance, ROUGHNESS_FLOOR, rough_floor)
    set_scalar(instance, ROUGHNESS_SCALE, 1.0)
    set_scalar(instance, SPECULAR_SCALE, spec_scale)
    set_scalar(instance, METALLIC_SCALE, metallic_scale)
    set_scalar(instance, RIM_STRENGTH, rim_strength)
    set_scalar(instance, RIM_POWER, rim_power)


def save_instance(instance):
    unreal.MaterialEditingLibrary.update_material_instance(instance)
    unreal.EditorAssetLibrary.save_loaded_asset(instance, only_if_is_dirty=False)


def validate_texture_override(instance_path, source_path):
    instance = load_required(instance_path)
    actual = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(
        instance, SOURCE_COLOR_TEXTURE)
    expected = load_required(source_path)
    if actual != expected:
        raise RuntimeError(
            "{} has invalid {}: expected {}, got {}".format(
                instance_path,
                SOURCE_COLOR_TEXTURE,
                expected.get_path_name(),
                actual.get_path_name() if actual else "None"))


def validate_blue_legacy_sampler_contract():
    blue_texture_path = BODY_VARIANTS["Blue"]
    blue_texture = load_required(blue_texture_path)
    if blue_texture.get_editor_property("compression_settings") != (
            unreal.TextureCompressionSettings.TC_NORMALMAP):
        raise RuntimeError(
            "Blue source texture no longer uses TC_NORMALMAP: {}".format(
                blue_texture_path))
    if blue_texture.get_editor_property("srgb"):
        raise RuntimeError(
            "Blue source texture unexpectedly enables sRGB: {}".format(
                blue_texture_path))

    master_path = (
        MASTER_ROOT + "/M_ABTS_Toon_BirdBody_LegacyNormalColor")
    master = load_required(master_path)
    matching_samples = [
        material_expression
        for material_expression in
        unreal.MaterialEditingLibrary.get_material_expressions(master)
        if isinstance(
            material_expression,
            unreal.MaterialExpressionTextureSampleParameter2D)
        and str(material_expression.get_editor_property("parameter_name"))
        == SOURCE_COLOR_TEXTURE]
    if len(matching_samples) != 1:
        raise RuntimeError(
            "Blue legacy master must expose exactly one {} sample: {}".format(
                SOURCE_COLOR_TEXTURE, master_path))
    if matching_samples[0].get_editor_property("sampler_type") != (
            unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL):
        raise RuntimeError(
            "Blue legacy master must preserve the accepted Normal sampler: {}".format(
                master_path))

    blue_instance_path = BIRD_ROOT + "/MI_ABTS_Toon_BirdBody_Blue"
    blue_instance = load_required(blue_instance_path)
    if blue_instance.get_editor_property("parent") != master:
        raise RuntimeError(
            "Blue candidate has the wrong parent: {}".format(
                blue_instance_path))


def validate_slingshot_master_contract():
    static_mesh_usage = unreal.MaterialUsage.MATUSAGE_STATIC_MESH
    nanite_usage = unreal.MaterialUsage.MATUSAGE_NANITE
    for name in (
            "M_ABTS_Toon_SlingshotTextured",
            "M_ABTS_Toon_SlingshotSolid"):
        master_path = MASTER_ROOT + "/" + name
        master = load_required(master_path)
        if not unreal.MaterialEditingLibrary.has_material_usage(
                master, static_mesh_usage):
            raise RuntimeError(
                "Slingshot master lacks StaticMesh usage: {}".format(
                    master_path))
        if not unreal.MaterialEditingLibrary.has_material_usage(
                master, nanite_usage):
            raise RuntimeError(
                "Slingshot master lacks Nanite usage: {}".format(
                    master_path))

    textured_path = MASTER_ROOT + "/M_ABTS_Toon_SlingshotTextured"
    textured = load_required(textured_path)
    normal_samples = [
        material_expression
        for material_expression in
        unreal.MaterialEditingLibrary.get_material_expressions(textured)
        if isinstance(
            material_expression,
            unreal.MaterialExpressionTextureSampleParameter2D)
        and str(material_expression.get_editor_property("parameter_name"))
        == SOURCE_NORMAL_TEXTURE]
    if len(normal_samples) != 1:
        raise RuntimeError(
            "Textured slingshot master must expose exactly one {} sample: {}".format(
                SOURCE_NORMAL_TEXTURE, textured_path))
    normal_sample = normal_samples[0]
    if normal_sample.get_editor_property("sampler_type") != (
            unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL):
        raise RuntimeError(
            "Textured slingshot normal parameter has the wrong sampler: {}".format(
                textured_path))
    default_normal = normal_sample.get_editor_property("texture")
    if default_normal is None or default_normal.get_editor_property(
            "compression_settings") != (
                unreal.TextureCompressionSettings.TC_NORMALMAP):
        raise RuntimeError(
            "Textured slingshot normal parameter lacks a Normalmap default: {}".format(
                textured_path))


def validate_generated_assets():
    expected = expected_asset_paths()
    missing = [
        path for path in expected
        if not unreal.EditorAssetLibrary.does_asset_exist(path)]
    if missing:
        raise RuntimeError("Generated T3-A2 assets are missing: {}".format(missing))

    skeletal_usage = unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH
    for name in (
            "M_ABTS_Toon_BirdBody",
            "M_ABTS_Toon_BirdBody_LegacyNormalColor",
            "M_ABTS_Toon_BirdFace"):
        master_path = MASTER_ROOT + "/" + name
        master = load_required(master_path)
        if not unreal.MaterialEditingLibrary.has_material_usage(
                master, skeletal_usage):
            raise RuntimeError(
                "Bird master lacks SkeletalMesh usage: {}".format(master_path))

    for identity, source_path in BODY_VARIANTS.items():
        validate_texture_override(
            BIRD_ROOT + "/MI_ABTS_Toon_BirdBody_" + identity,
            source_path)
    for identity, source_path in FACE_VARIANTS.items():
        validate_texture_override(
            BIRD_ROOT + "/MI_ABTS_Toon_BirdFace_" + identity,
            source_path)

    validate_blue_legacy_sampler_contract()
    validate_slingshot_master_contract()

    unreal.log(
        "[ABTS][Rendering][T3-A2][Validated] Assets={} SkeletalMasters=3 BirdTextureOverrides=10 BlueLegacySampler=Normal SlingshotNaniteMasters=2 TexturedNormalDefault=Normalmap".format(
            len(expected)))


def generate():
    for path in (ROOT, MASTER_ROOT, BIRD_ROOT, SLINGSHOT_ROOT):
        ensure_directory(path)

    if not FORCE_REBUILD:
        validate_generated_assets()
        unreal.log(
            "[ABTS][Rendering][T3-A2][NoRewrite] Root={}".format(ROOT))
        return

    body_master = build_bird_master("M_ABTS_Toon_BirdBody", masked=False)
    blue_body_master = build_bird_master(
        "M_ABTS_Toon_BirdBody_LegacyNormalColor",
        masked=False,
        source_uses_normal_sampler=True,
        default_texture=BODY_VARIANTS["Blue"])
    face_master = build_bird_master("M_ABTS_Toon_BirdFace", masked=True)
    textured_slingshot_master = build_textured_slingshot_master()
    solid_slingshot_master = build_solid_slingshot_master()

    for identity, texture_path in BODY_VARIANTS.items():
        instance = get_or_create_instance(
            "MI_ABTS_Toon_BirdBody_" + identity,
            BIRD_ROOT,
            blue_body_master if identity == "Blue" else body_master)
        set_texture(instance, SOURCE_COLOR_TEXTURE, texture_path)
        set_scalar(instance, SOURCE_ROUGHNESS, 0.5)
        set_scalar(instance, SOURCE_SPECULAR, 0.5)
        set_scalar(instance, SOURCE_METALLIC, 0.0)
        apply_family_defaults(instance, "body")
        save_instance(instance)

    for identity, texture_path in FACE_VARIANTS.items():
        instance = get_or_create_instance(
            "MI_ABTS_Toon_BirdFace_" + identity, BIRD_ROOT, face_master)
        set_texture(instance, SOURCE_COLOR_TEXTURE, texture_path)
        set_scalar(instance, SOURCE_ROUGHNESS, 0.5)
        set_scalar(instance, SOURCE_SPECULAR, 0.5)
        set_scalar(instance, SOURCE_METALLIC, 0.0)
        apply_family_defaults(instance, "face")
        save_instance(instance)

    for tier, values in SLINGSHOT_VARIANTS.items():
        for part in ("Stake", "Pouch"):
            key = part.lower()
            instance = get_or_create_instance(
                "MI_ABTS_Toon_Slingshot_{}_{}".format(part, tier),
                SLINGSHOT_ROOT,
                textured_slingshot_master)
            set_texture(instance, SOURCE_COLOR_TEXTURE, values[key + "_color"])
            set_texture(instance, SOURCE_NORMAL_TEXTURE, values[key + "_normal"])
            set_texture(
                instance, SOURCE_ROUGHNESS_TEXTURE, values[key + "_roughness"])
            set_scalar(instance, SOURCE_SPECULAR, 0.5)
            set_scalar(
                instance, SOURCE_METALLIC,
                1.0 if values["family"] == "metal" else 0.0)
            apply_family_defaults(instance, values["family"])
            save_instance(instance)

        cord = get_or_create_instance(
            "MI_ABTS_Toon_Slingshot_Cord_" + tier,
            SLINGSHOT_ROOT,
            solid_slingshot_master)
        set_vector(cord, SOURCE_COLOR, values["cord_color"])
        set_scalar(cord, SOURCE_ROUGHNESS, values["cord_roughness"])
        set_scalar(cord, SOURCE_SPECULAR, 0.5)
        set_scalar(cord, SOURCE_METALLIC, values["cord_metallic"])
        apply_family_defaults(cord, values["family"])
        save_instance(cord)

    unreal.log(
        "[ABTS][Rendering][T3-A2][Generated] Masters=5 Instances=22 Root={}".format(
            ROOT))
    validate_generated_assets()


generate()
