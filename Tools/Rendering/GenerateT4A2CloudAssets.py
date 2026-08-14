"""Generate and validate the Integration-owned T4-A2R1-B cloud assets.

Run only with the project's pinned UE 5.8 UnrealEditor-Cmd. The script is
idempotent: without ABTS_T4A2_REBUILD=1 it validates and performs no writes.
"""

import os

import unreal


ROOT = "/Game/Toon/Environment/Cloud"
MASTER_PATH = ROOT + "/M_ABTS_Toon_Cloudlet"
MESH_PATH = ROOT + "/SM_ABTS_Toon_Cloudlet"
SOURCE_MESH_PATH = "/Engine/BasicShapes/Sphere"

LIGHT_COLOR = "ABTS_CloudLightColor"
BODY_COLOR = "ABTS_CloudBodyColor"
SHADOW_COLOR = "ABTS_CloudShadowColor"
SUN_DIRECTION = "ABTS_CloudSunDirection"
PLANET_CENTER = "ABTS_CloudPlanetCenter"
NIGHT_BRIGHTNESS = "ABTS_CloudNightBrightness"
DAYLIGHT_BLEND_MIN = "ABTS_CloudDaylightBlendMinSolarHeight"
DAYLIGHT_BLEND_MAX = "ABTS_CloudDaylightBlendMaxSolarHeight"
LIGHT_CONTRAST = "ABTS_CloudLightContrast"
LIGHT_BIAS = "ABTS_CloudLightBias"
HEIGHT_LIFT = "ABTS_CloudHeightLift"
SUN_WHITE_STRENGTH = "ABTS_CloudSunWhiteStrength"
THIN_WHITE_STRENGTH = "ABTS_CloudThinWhiteStrength"
THIN_DENSITY_START = "ABTS_CloudThinDensityStart"
THIN_DENSITY_END = "ABTS_CloudThinDensityEnd"
GRADIENT_CONFIDENCE_START = "ABTS_CloudGradientConfidenceStart"
GRADIENT_CONFIDENCE_END = "ABTS_CloudGradientConfidenceEnd"
MACRO_LIGHTING_VERSION = "ABTS_CloudMacroLightingVersion"
ISLAND_CENTER = "ABTS_CloudIslandCenter"
ISLAND_AXIS_X = "ABTS_CloudIslandAxisX"
ISLAND_AXIS_Y = "ABTS_CloudIslandAxisY"
ISLAND_UP = "ABTS_CloudIslandUp"
ISLAND_EXTENTS = "ABTS_CloudIslandExtents"
MACRO_NORMAL_STRENGTH = "ABTS_CloudMacroNormalStrength"
CONTINUOUS_OCCLUSION_STRENGTH = "ABTS_CloudContinuousOcclusionStrength"
NOISE_FREQUENCY = "ABTS_CloudNoiseFrequency"
NOISE_AMPLITUDE = "ABTS_CloudNoiseAmplitudeCM"
TRAVERSAL_ACTIVE = "ABTS_CloudTraversalActive"
TRAVERSAL_PROTECTION_ACTIVE = "ABTS_CloudTraversalProtectionActive"
TRAVERSAL_CAMERA = "ABTS_CloudTraversalCameraWorld"
TRAVERSAL_BIRD = "ABTS_CloudTraversalBirdWorld"
TRAVERSAL_BIRD_COUNT = "ABTS_CloudTraversalBirdCount"
TRAVERSAL_BIRD_SPHERES = [
    "ABTS_CloudTraversalBirdSphere{}".format(index) for index in range(4)]
TRAVERSAL_BIRD_SPHERE_RADIUS_INPUTS = [
    "TraversalBirdSphereRadius{}".format(index) for index in range(4)]
TRAVERSAL_CAMERA_RADIUS = "ABTS_CloudTraversalCameraRadiusCM"
TRAVERSAL_BIRD_RADIUS = "ABTS_CloudTraversalBirdRadiusCM"
TRAVERSAL_CORRIDOR_RADIUS = "ABTS_CloudTraversalCorridorRadiusCM"
TRAVERSAL_FEATHER = "ABTS_CloudTraversalFeatherCM"
TRAVERSAL_RETAINED_COVERAGE = "ABTS_CloudTraversalRetainedCoverage"
TRAVERSAL_MASK_FREQUENCY = "ABTS_CloudTraversalMaskFrequency"
TRAVERSAL_CUSTOM_DESCRIPTION = (
    "ABTS T4-A2.3.1 Multi-Bird Stable Planar Traversal Coverage")

MACRO_CLUSTER_COUNT = 6
BASE_CUSTOM_DATA_COUNT = 5
ISLAND_CENTER_DATA_INDEX = 5
ISLAND_AXIS_X_DATA_INDEX = 8
ISLAND_AXIS_Y_DATA_INDEX = 11
ISLAND_UP_DATA_INDEX = 14
ISLAND_EXTENTS_DATA_INDEX = 17
MACRO_DATA_INDEX = 20
MACRO_DATA_STRIDE = 7
COLOR_VARIANT_DATA_INDEX = MACRO_DATA_INDEX + MACRO_CLUSTER_COUNT * MACRO_DATA_STRIDE
CUSTOM_DATA_COUNT = COLOR_VARIANT_DATA_INDEX + 1
MACRO_CUSTOM_DESCRIPTION = "ABTS_T4A22_LocalSolarHeightNightCloud"


asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
FORCE_REBUILD = os.environ.get("ABTS_T4A2_REBUILD", "0") == "1"


def ensure_directory(path):
    if not unreal.EditorAssetLibrary.does_directory_exist(path):
        unreal.EditorAssetLibrary.make_directory(path)


def load_required(path):
    asset = unreal.load_asset(path)
    if asset is None:
        raise RuntimeError("Required asset is missing: {}".format(path))
    return asset


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


def custom_data(material, index, default, x, y):
    del default  # UE 5.8 exposes DataIndex only; HISM always supplies the value.
    node = expression(
        material, unreal.MaterialExpressionPerInstanceCustomData, x, y)
    node.set_editor_property("data_index", int(index))
    return node


def append_vector(material, a, a_pin, b, b_pin, x, y):
    node = expression(material, unreal.MaterialExpressionAppendVector, x, y)
    connect(a, a_pin, node, "A")
    connect(b, b_pin, node, "B")
    return node


def custom_data_vector3(material, first_index, x, y):
    component_x = custom_data(material, first_index, 0.0, x, y)
    component_y = custom_data(material, first_index + 1, 0.0, x, y + 50)
    component_z = custom_data(material, first_index + 2, 0.0, x, y + 100)
    xy = append_vector(
        material, component_x, "", component_y, "", x + 180, y + 20)
    return append_vector(
        material, xy, "", component_z, "", x + 360, y + 45)


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


def binary(material, cls, a, a_pin, b, b_pin, x, y):
    node = expression(material, cls, x, y)
    connect(a, a_pin, node, "A")
    connect(b, b_pin, node, "B")
    return node


def multiply(material, a, a_pin, b, b_pin, x, y):
    return binary(
        material, unreal.MaterialExpressionMultiply,
        a, a_pin, b, b_pin, x, y)


def add(material, a, a_pin, b, b_pin, x, y):
    return binary(
        material, unreal.MaterialExpressionAdd,
        a, a_pin, b, b_pin, x, y)


def subtract(material, a, a_pin, b, b_pin, x, y):
    return binary(
        material, unreal.MaterialExpressionSubtract,
        a, a_pin, b, b_pin, x, y)


def lerp(material, a, a_pin, b, b_pin, alpha, alpha_pin, x, y):
    node = expression(material, unreal.MaterialExpressionLinearInterpolate, x, y)
    connect(a, a_pin, node, "A")
    connect(b, b_pin, node, "B")
    connect(alpha, alpha_pin, node, "Alpha")
    return node


def build_macro_lighting_code():
    lines = [
        "float3 ABTSUp = normalize(IslandUp);",
        "float3 ABTSX = normalize(AxisX);",
        "float3 ABTSY = normalize(AxisY);",
        "float3 ABTSSun = normalize(SunDirection);",
        "float3 ABTSPlanetRadial = normalize(WorldPos - PlanetCenter);",
        "float ABTSSolarHeight = dot(ABTSPlanetRadial, ABTSSun);",
        "float ABTSDaylightBlendMaximum = max(DaylightBlendMaxSolarHeight, DaylightBlendMinSolarHeight + 0.01);",
        "float ABTSLocalDay = smoothstep(DaylightBlendMinSolarHeight, ABTSDaylightBlendMaximum, ABTSSolarHeight);",
        "float3 ABTSExtents = max(IslandExtents, float3(1.0, 1.0, 1.0));",
		"float ABTSColorVariant = step(0.5, IslandColorVariant);",
		"float3 ABTSResolvedLightColor = lerp(LightColor, float3(0.86, 0.89, 0.94), ABTSColorVariant);",
		"float3 ABTSResolvedBodyColor = lerp(BodyColor, float3(0.38, 0.46, 0.58), ABTSColorVariant);",
		"float3 ABTSResolvedShadowColor = lerp(ShadowColor, float3(0.15, 0.20, 0.30), ABTSColorVariant);",
        "float3 ABTSRelative = WorldPos - IslandCenter;",
        "float3 ABTSP = float3(dot(ABTSRelative, ABTSX) / ABTSExtents.x, dot(ABTSRelative, ABTSY) / ABTSExtents.y, dot(ABTSRelative, ABTSUp) / ABTSExtents.z);",
        "float ABTSPlanarDensity = 0.0;",
        "float ABTSPlanarDensitySquared = 0.0;",
        "float ABTSWeightedMacroHeight = 0.0;",
        "float3 ABTSVolumeGradientP = float3(0.0, 0.0, 0.0);",
        "float ABTSVolumeGradientMagnitudeSum = 0.0;",
    ]
    for index in range(MACRO_CLUSTER_COUNT):
        lines.extend([
            "{",
            "    float3 ABTSM = Macro{};".format(index),
            "    float3 ABTSS = Shape{};".format(index),
            "    float ABTSC = ABTSS.y;",
            "    float ABTSSn = ABTSS.z;",
            "    float2 ABTSR = max(float2(ABTSM.z, ABTSS.x), float2(0.04, 0.04));",
            "    float2 ABTSD = ABTSP.xy - ABTSM.xy;",
            "    float ABTSHeight01 = saturate(MacroHeight{});".format(index),
            "    float ABTSCenterZ = lerp(-0.12, 0.24, ABTSHeight01);",
            "    float ABTSRadiusZ = lerp(0.34, 0.52, ABTSHeight01);",
            "    float3 ABTSQ = float3((ABTSD.x * ABTSC + ABTSD.y * ABTSSn) / ABTSR.x, (-ABTSD.x * ABTSSn + ABTSD.y * ABTSC) / ABTSR.y, (ABTSP.z - ABTSCenterZ) / ABTSRadiusZ);",
            "    float ABTSQ2 = dot(ABTSQ, ABTSQ);",
            "    float ABTSWeight = exp2(-1.35 * ABTSQ2) * lerp(0.82, 1.18, ABTSHeight01);",
            "    float3 ABTSWeightGradient = -1.871497 * ABTSWeight * float3(ABTSQ.x * ABTSC / ABTSR.x - ABTSQ.y * ABTSSn / ABTSR.y, ABTSQ.x * ABTSSn / ABTSR.x + ABTSQ.y * ABTSC / ABTSR.y, ABTSQ.z / ABTSRadiusZ);",
            "    ABTSPlanarDensity += ABTSWeight;",
            "    ABTSPlanarDensitySquared += ABTSWeight * ABTSWeight;",
            "    ABTSWeightedMacroHeight += ABTSWeight * ABTSHeight01;",
            "    ABTSVolumeGradientP += ABTSWeightGradient;",
            "    ABTSVolumeGradientMagnitudeSum += length(ABTSWeightGradient);",
            "}",
        ])
    lines.extend([
        "float3 ABTSCoreM = Macro0;",
        "float3 ABTSCoreS = Shape0;",
        "float ABTSCoreC = ABTSCoreS.y;",
        "float ABTSCoreSn = ABTSCoreS.z;",
        "float2 ABTSCoreR = max(float2(ABTSCoreM.z, ABTSCoreS.x), float2(0.04, 0.04));",
        "float2 ABTSCoreD = ABTSP.xy - ABTSCoreM.xy;",
        "float ABTSCoreHeight01 = saturate(MacroHeight0);",
        "float ABTSCoreCenterZ = lerp(-0.12, 0.24, ABTSCoreHeight01);",
        "float ABTSCoreRadiusZ = lerp(0.34, 0.52, ABTSCoreHeight01);",
        "float3 ABTSCoreQ = float3((ABTSCoreD.x * ABTSCoreC + ABTSCoreD.y * ABTSCoreSn) / ABTSCoreR.x, (-ABTSCoreD.x * ABTSCoreSn + ABTSCoreD.y * ABTSCoreC) / ABTSCoreR.y, (ABTSP.z - ABTSCoreCenterZ) / ABTSCoreRadiusZ);",
        "float ABTSCorePlanarWeight = exp2(-1.35 * dot(ABTSCoreQ.xy, ABTSCoreQ.xy));",
        "float ABTSCoreWeight = ABTSCorePlanarWeight * exp2(-1.35 * ABTSCoreQ.z * ABTSCoreQ.z) * lerp(0.82, 1.18, ABTSCoreHeight01);",
        "float3 ABTSCoreGradientP = -1.871497 * ABTSCoreWeight * float3(ABTSCoreQ.x * ABTSCoreC / ABTSCoreR.x - ABTSCoreQ.y * ABTSCoreSn / ABTSCoreR.y, ABTSCoreQ.x * ABTSCoreSn / ABTSCoreR.x + ABTSCoreQ.y * ABTSCoreC / ABTSCoreR.y, ABTSCoreQ.z / ABTSCoreRadiusZ);",
        "float ABTSDensitySafe = max(ABTSPlanarDensity, 1.0e-4);",
        "float ABTSCoreInfluence = smoothstep(0.08, 0.46, ABTSCorePlanarWeight);",
        "float ABTSUnionMacroHeight01 = saturate(ABTSWeightedMacroHeight / ABTSDensitySafe);",
        "float ABTSMacroHeight01 = lerp(ABTSUnionMacroHeight01, ABTSCoreHeight01, ABTSCoreInfluence);",
        "float ABTSDominance = saturate(ABTSPlanarDensitySquared / max(ABTSPlanarDensity * ABTSPlanarDensity, 1.0e-4));",
        "float ABTSMacroJunction = saturate((0.62 - ABTSDominance) * 2.35);",
        "float3 ABTSNormalizedVolumeGradient = ABTSVolumeGradientP / ABTSDensitySafe;",
        "float3 ABTSVolumeGradientWS = ABTSX * ABTSNormalizedVolumeGradient.x + ABTSY * ABTSNormalizedVolumeGradient.y + ABTSUp * ABTSNormalizedVolumeGradient.z;",
        "float ABTSVolumeGradientLength = length(ABTSVolumeGradientWS);",
        "float ABTSGradientCoherence = saturate(length(ABTSVolumeGradientP) / max(ABTSVolumeGradientMagnitudeSum, 1.0e-4));",
        "float ABTSGradientConfidenceRange = max(GradientConfidenceEnd - GradientConfidenceStart, 0.01);",
        "float ABTSGradientConfidence = smoothstep(GradientConfidenceStart, GradientConfidenceStart + ABTSGradientConfidenceRange, ABTSGradientCoherence);",
        "float3 ABTSUnionSurfaceNormal = ABTSVolumeGradientLength > 1.0e-4 ? -ABTSVolumeGradientWS / ABTSVolumeGradientLength : ABTSUp;",
        "float3 ABTSCoreGradientWS = ABTSX * ABTSCoreGradientP.x + ABTSY * ABTSCoreGradientP.y + ABTSUp * ABTSCoreGradientP.z;",
        "float ABTSCoreGradientLength = length(ABTSCoreGradientWS);",
        "float3 ABTSCoreSurfaceNormal = ABTSCoreGradientLength > 1.0e-4 ? -ABTSCoreGradientWS / ABTSCoreGradientLength : ABTSUp;",
        "float3 ABTSSurfaceNormal = normalize(lerp(ABTSUnionSurfaceNormal, ABTSCoreSurfaceNormal, ABTSCoreInfluence));",
        "float ABTSNormalConfidence = lerp(ABTSGradientConfidence, 1.0, ABTSCoreInfluence);",
        "float ABTSStableNormalWeight = saturate(MacroNormalStrength) * ABTSNormalConfidence;",
        "float3 ABTSFinalNormal = normalize(lerp(ABTSUp, ABTSSurfaceNormal, ABTSStableNormalWeight));",
        "float ABTSStableMacroJunction = ABTSMacroJunction * ABTSGradientConfidence * (1.0 - ABTSCoreInfluence);",
        "float ABTSDetail0 = sin(dot(ABTSP.xy, float2(4.10, 3.70)) + 1.31);",
        "float ABTSDetail1 = sin(dot(ABTSP.xy, float2(-3.30, 5.20)) - 0.73);",
        "float ABTSStableDetail = ABTSDetail0 * ABTSDetail1;",
        "float ABTSUndersideBlend = 1.0 - smoothstep(-0.18, 0.12, ABTSP.z);",
        "ABTSMacroHeight01 = lerp(ABTSMacroHeight01, 0.48, ABTSUndersideBlend);",
        "ABTSStableMacroJunction *= (1.0 - ABTSUndersideBlend);",
        "float ABTSVertical01 = lerp(smoothstep(-0.62, 0.68, ABTSP.z), 0.18, ABTSUndersideBlend);",
        "float ABTSUnionVolumeDensity01 = saturate(ABTSPlanarDensity * 0.34);",
        "float ABTSCoreVolumeDensity01 = saturate(ABTSCoreWeight * 1.10);",
        "float ABTSVolumeDensity01 = lerp(lerp(ABTSUnionVolumeDensity01, ABTSCoreVolumeDensity01, ABTSCoreInfluence), 0.70, ABTSUndersideBlend);",
        "float ABTSMacroRelief = saturate(0.50 + 0.62 * (ABTSMacroHeight01 - 0.5) + 0.18 * ABTSStableDetail - 0.24 * ABTSStableMacroJunction);",
        "float ABTSHalfLambert = saturate(0.5 + 0.5 * dot(ABTSFinalNormal, ABTSSun));",
        "float ABTSDirectLight = lerp(saturate((ABTSHalfLambert - 0.5) * LightContrast + 0.5 + LightBias), 0.46, ABTSUndersideBlend);",
        "float ABTSShadowAmount = saturate(ContinuousOcclusionStrength * (0.72 * (1.0 - ABTSVertical01) + 0.52 * ABTSVolumeDensity01 + 0.58 * ABTSStableMacroJunction + 0.34 * (1.0 - ABTSDirectLight)));",
        "float ABTSBodyLight = saturate(0.06 + 0.42 * ABTSDirectLight + 0.14 * ABTSVertical01 + 0.34 * ABTSMacroRelief + HeightLift * (ABTSMacroHeight01 - 0.5) - 0.32 * ABTSShadowAmount);",
        "float ABTSHighlight = smoothstep(0.60, 0.88, ABTSDirectLight) * smoothstep(0.30, 0.86, ABTSVertical01) * (0.10 + 0.22 * ABTSMacroRelief) * ABTSLocalDay;",
        "float ABTSSunWhite = smoothstep(0.46, 0.74, ABTSDirectLight) * lerp(0.72, 1.0, ABTSMacroRelief) * ABTSLocalDay;",
        "float ABTSThinDensityRange = max(ThinDensityEnd - ThinDensityStart, 0.01);",
        "float ABTSThinness = 1.0 - smoothstep(ThinDensityStart, ThinDensityStart + ABTSThinDensityRange, ABTSVolumeDensity01);",
        "float ABTSThinWhite = ABTSThinness * lerp(0.42, 1.0, ABTSDirectLight) * ABTSLocalDay;",
        "float ABTSWhiteWeight = saturate(SunWhiteStrength * ABTSSunWhite + ThinWhiteStrength * ABTSThinWhite);",
        "float3 ABTSCloudColor = lerp(ABTSResolvedShadowColor, ABTSResolvedBodyColor, ABTSBodyLight);",
        "ABTSCloudColor = lerp(ABTSCloudColor, ABTSResolvedLightColor, ABTSHighlight);",
        "ABTSCloudColor = lerp(ABTSCloudColor, ABTSResolvedLightColor, ABTSWhiteWeight);",
        "float ABTSNightRelief = saturate(0.78 + 0.22 * ABTSMacroRelief - 0.10 * ABTSShadowAmount);",
        "float3 ABTSNightColor = ABTSResolvedShadowColor * saturate(NightBrightness) * ABTSNightRelief;",
        "ABTSCloudColor = lerp(ABTSNightColor, ABTSCloudColor, ABTSLocalDay);",
        "return saturate(ABTSCloudColor);",
    ])
    return "\n".join(lines)


def macro_lighting(material, input_specs, x, y):
    node = expression(material, unreal.MaterialExpressionCustom, x, y)
    node.set_editor_property("desc", MACRO_CUSTOM_DESCRIPTION)
    node.set_editor_property(
        "output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom_inputs = []
    for input_name, _, _ in input_specs:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", input_name)
        custom_inputs.append(custom_input)
    node.set_editor_property("inputs", custom_inputs)
    node.set_editor_property("code", build_macro_lighting_code())
    for input_name, source, source_pin in input_specs:
        connect(source, source_pin, node, input_name)
    return node


def build_traversal_opacity_code():
    return "\n".join([
        "float ABTSActive = saturate(TraversalActive);",
        "float ABTSProtectionActive = saturate(TraversalProtectionActive);",
        "if (ABTSActive <= 0.0001 && ABTSProtectionActive <= 0.0001) return 1.0;",
        "float ABTSCameraRadius = max(TraversalCameraRadiusCM, 1.0);",
        "float ABTSBirdRadius = max(TraversalBirdRadiusCM, 1.0);",
        "float ABTSCorridorRadius = max(TraversalCorridorRadiusCM, 1.0);",
        "float ABTSFeather = max(TraversalFeatherCM, 1.0);",
        "float ABTSRetainedCoverage = saturate(TraversalRetainedCoverage);",
        "float ABTSMaskFrequency = max(TraversalMaskFrequency, 0.0001);",
        "float3 ABTSSegment = TraversalBirdWorld - TraversalCameraWorld;",
        "float ABTSSegmentLengthSquared = max(dot(ABTSSegment, ABTSSegment), 1.0);",
        "float ABTSSegmentAlpha = saturate(dot(WorldPos - TraversalCameraWorld, ABTSSegment) / ABTSSegmentLengthSquared);",
        "float3 ABTSClosestSegmentPoint = TraversalCameraWorld + ABTSSegment * ABTSSegmentAlpha;",
        "float ABTSCameraDistance = distance(WorldPos, TraversalCameraWorld);",
        "float ABTSBirdDistance = distance(WorldPos, TraversalBirdWorld);",
        "float ABTSCorridorDistance = distance(WorldPos, ABTSClosestSegmentPoint);",
        "float ABTSCameraCoreClear = 1.0 - smoothstep(ABTSCameraRadius, ABTSCameraRadius + ABTSFeather, ABTSCameraDistance);",
        "float ABTSBirdCoreClear = 0.0;",
        "if (TraversalBirdCount > 0.5) ABTSBirdCoreClear = max(ABTSBirdCoreClear, 1.0 - smoothstep(max(TraversalBirdSphereRadius0, 1.0), max(TraversalBirdSphereRadius0, 1.0) + ABTSFeather, distance(WorldPos, TraversalBirdSphere0)));",
        "if (TraversalBirdCount > 1.5) ABTSBirdCoreClear = max(ABTSBirdCoreClear, 1.0 - smoothstep(max(TraversalBirdSphereRadius1, 1.0), max(TraversalBirdSphereRadius1, 1.0) + ABTSFeather, distance(WorldPos, TraversalBirdSphere1)));",
        "if (TraversalBirdCount > 2.5) ABTSBirdCoreClear = max(ABTSBirdCoreClear, 1.0 - smoothstep(max(TraversalBirdSphereRadius2, 1.0), max(TraversalBirdSphereRadius2, 1.0) + ABTSFeather, distance(WorldPos, TraversalBirdSphere2)));",
        "if (TraversalBirdCount > 3.5) ABTSBirdCoreClear = max(ABTSBirdCoreClear, 1.0 - smoothstep(max(TraversalBirdSphereRadius3, 1.0), max(TraversalBirdSphereRadius3, 1.0) + ABTSFeather, distance(WorldPos, TraversalBirdSphere3)));",
        "float ABTSCorridorInfluence = 1.0 - smoothstep(ABTSCorridorRadius, ABTSCorridorRadius + ABTSFeather, ABTSCorridorDistance);",
        "float ABTSCoreClear = max(ABTSCameraCoreClear, ABTSBirdCoreClear);",
		"float ABTSSightlineCore = 1.0 - smoothstep(ABTSCorridorRadius * 0.72, ABTSCorridorRadius, ABTSCorridorDistance);",
		"ABTSCoreClear = max(ABTSCoreClear, ABTSSightlineCore);",
        "float ABTSCameraEndpoint = 1.0 - smoothstep(ABTSCameraRadius + ABTSFeather, ABTSCameraRadius + 2.0 * ABTSFeather, ABTSCameraDistance);",
        "float ABTSBirdEndpoint = 1.0 - smoothstep(ABTSBirdRadius + ABTSFeather, ABTSBirdRadius + 2.0 * ABTSFeather, ABTSBirdDistance);",
        "float ABTSCorridorOnly = ABTSCorridorInfluence * (1.0 - max(ABTSCameraEndpoint, ABTSBirdEndpoint));",
        "float ABTSPartialCoverage = lerp(1.0, ABTSRetainedCoverage, ABTSActive * ABTSCorridorOnly);",
        "float ABTSCoverage = saturate(ABTSPartialCoverage * (1.0 - ABTSProtectionActive * ABTSCoreClear));",
        "if (ABTSCoverage >= 0.9999) return 1.0;",
        "if (ABTSCoverage <= 0.0001) return 0.0;",
        "float3 ABTSLocal = WorldPos - IslandCenter;",
        "float2 ABTSPlanarUV = float2(dot(ABTSLocal, AxisX), dot(ABTSLocal, AxisY)) * ABTSMaskFrequency;",
        "float2 ABTSCell0 = floor(ABTSPlanarUV);",
        "float2 ABTSFrac0 = frac(ABTSPlanarUV);",
        "float2 ABTSSmooth0 = ABTSFrac0 * ABTSFrac0 * (3.0 - 2.0 * ABTSFrac0);",
        "float ABTSH00 = frac(sin(dot(ABTSCell0, float2(127.1, 311.7))) * 43758.5453);",
        "float ABTSH10 = frac(sin(dot(ABTSCell0 + float2(1.0, 0.0), float2(127.1, 311.7))) * 43758.5453);",
        "float ABTSH01 = frac(sin(dot(ABTSCell0 + float2(0.0, 1.0), float2(127.1, 311.7))) * 43758.5453);",
        "float ABTSH11 = frac(sin(dot(ABTSCell0 + float2(1.0, 1.0), float2(127.1, 311.7))) * 43758.5453);",
        "float ABTSNoise0 = lerp(lerp(ABTSH00, ABTSH10, ABTSSmooth0.x), lerp(ABTSH01, ABTSH11, ABTSSmooth0.x), ABTSSmooth0.y);",
        "float2 ABTSUV1 = ABTSPlanarUV * 2.07 + float2(19.31, 7.73);",
        "float2 ABTSCell1 = floor(ABTSUV1);",
        "float2 ABTSFrac1 = frac(ABTSUV1);",
        "float2 ABTSSmooth1 = ABTSFrac1 * ABTSFrac1 * (3.0 - 2.0 * ABTSFrac1);",
        "float ABTSG00 = frac(sin(dot(ABTSCell1, float2(269.5, 183.3))) * 43758.5453);",
        "float ABTSG10 = frac(sin(dot(ABTSCell1 + float2(1.0, 0.0), float2(269.5, 183.3))) * 43758.5453);",
        "float ABTSG01 = frac(sin(dot(ABTSCell1 + float2(0.0, 1.0), float2(269.5, 183.3))) * 43758.5453);",
        "float ABTSG11 = frac(sin(dot(ABTSCell1 + float2(1.0, 1.0), float2(269.5, 183.3))) * 43758.5453);",
        "float ABTSNoise1 = lerp(lerp(ABTSG00, ABTSG10, ABTSSmooth1.x), lerp(ABTSG01, ABTSG11, ABTSSmooth1.x), ABTSSmooth1.y);",
        "float ABTSPlanarNoise = saturate(ABTSNoise0 * 0.68 + ABTSNoise1 * 0.32);",
        "float ABTSCalibratedNoise = saturate((ABTSPlanarNoise - 0.16) / 0.68);",
        "return step(1.0 - ABTSCoverage, ABTSCalibratedNoise);",
    ])


def traversal_opacity(material, input_specs, x, y):
    node = expression(material, unreal.MaterialExpressionCustom, x, y)
    node.set_editor_property("desc", TRAVERSAL_CUSTOM_DESCRIPTION)
    node.set_editor_property(
        "output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    custom_inputs = []
    for input_name, _, _ in input_specs:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", input_name)
        custom_inputs.append(custom_input)
    node.set_editor_property("inputs", custom_inputs)
    node.set_editor_property("code", build_traversal_opacity_code())
    for input_name, source, source_pin in input_specs:
        connect(source, source_pin, node, input_name)
    return node


def rebuild_material():
    material = unreal.load_asset(MASTER_PATH)
    if material is None:
        material = asset_tools.create_asset(
            "M_ABTS_Toon_Cloudlet",
            ROOT,
            unreal.Material,
            unreal.MaterialFactoryNew())
    if material is None:
        raise RuntimeError("Could not create {}".format(MASTER_PATH))

    for existing in list(
            unreal.MaterialEditingLibrary.get_material_expressions(material)):
        unreal.MaterialEditingLibrary.delete_material_expression(
            material, existing)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property("opacity_mask_clip_value", 0.35)
    material.set_editor_property(
        "shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("used_with_static_mesh", True)
    material.set_editor_property("used_with_instanced_static_meshes", True)
    # The masked traversal aperture and WPO are pixel animations not fully
    # represented by the primitive transform.  Tell TSR to reject stale edge
    # history instead of producing bright trails during fast night-side moves.
    material.set_editor_property("has_pixel_animation", True)

    seed = custom_data(material, 0, 0.5, -1700, 420)
    height = custom_data(material, 1, 0.5, -1700, -340)
    occlusion = custom_data(material, 2, 0.35, -1700, -180)
    size_tier = custom_data(material, 3, 0.5, -1700, 560)
    layer01 = custom_data(material, 4, 0.0, -1700, 700)

    light_color = vector(
        material, LIGHT_COLOR,
        unreal.LinearColor(0.92, 0.93, 0.96, 1.0), -1700, -900)
    body_color = vector(
        material, BODY_COLOR,
        unreal.LinearColor(0.45, 0.53, 0.64, 1.0), -1700, -830)
    shadow_color = vector(
        material, SHADOW_COLOR,
        unreal.LinearColor(0.18, 0.24, 0.34, 1.0), -1700, -760)
    sun = vector(
        material, SUN_DIRECTION,
        unreal.LinearColor(0.35, -0.45, 0.82, 0.0), -1700, -620)
    planet_center = vector(
        material, PLANET_CENTER,
        unreal.LinearColor(0.0, 0.0, 0.0, 0.0), -1700, -1180)
    night_brightness = scalar(
        material, NIGHT_BRIGHTNESS, 0.42, -1700, -1120)
    daylight_blend_min = scalar(
        material, DAYLIGHT_BLEND_MIN, -0.16, -1700, -1060)
    daylight_blend_max = scalar(
        material, DAYLIGHT_BLEND_MAX, 0.14, -1700, -1000)
    # Keep the upward-facing cloud range below saturation.  A positive bias
    # erased the shared height-field normal before it reached the toon pass,
    # making the seam-free result read as a flat white card.
    contrast = scalar(material, LIGHT_CONTRAST, 1.08, -1700, -500)
    bias = scalar(material, LIGHT_BIAS, -0.15, -1700, -440)
    height_lift = scalar(material, HEIGHT_LIFT, 0.12, -1700, -280)
    sun_white_strength = scalar(
        material, SUN_WHITE_STRENGTH, 0.70, -1700, -220)
    thin_white_strength = scalar(
        material, THIN_WHITE_STRENGTH, 0.58, -1700, -160)
    thin_density_start = scalar(
        material, THIN_DENSITY_START, 0.30, -1700, -100)
    thin_density_end = scalar(
        material, THIN_DENSITY_END, 0.78, -1700, -40)
    gradient_confidence_start = scalar(
        material, GRADIENT_CONFIDENCE_START, 0.10, -1700, 20)
    gradient_confidence_end = scalar(
        material, GRADIENT_CONFIDENCE_END, 0.34, -1700, 80)
    macro_version = scalar(
        material, MACRO_LIGHTING_VERSION, 12.0, -1700, 140)
    del macro_version
    macro_strength = scalar(
        material, MACRO_NORMAL_STRENGTH, 0.84, -1700, 200)
    continuous_occlusion = scalar(
        material, CONTINUOUS_OCCLUSION_STRENGTH, 0.30, -1700, 260)

    island_center = custom_data_vector3(
        material, ISLAND_CENTER_DATA_INDEX, -1480, -1120)
    axis_x = custom_data_vector3(
        material, ISLAND_AXIS_X_DATA_INDEX, -1480, -940)
    axis_y = custom_data_vector3(
        material, ISLAND_AXIS_Y_DATA_INDEX, -1480, -760)
    island_up = custom_data_vector3(
        material, ISLAND_UP_DATA_INDEX, -1480, -580)
    island_extents = custom_data_vector3(
        material, ISLAND_EXTENTS_DATA_INDEX, -1480, -400)
    island_color_variant = custom_data(
        material, COLOR_VARIANT_DATA_INDEX, 0.0, -1480, -220)
    traversal_active = scalar(
        material, TRAVERSAL_ACTIVE, 0.0, -1480, -820)
    traversal_protection_active = scalar(
        material, TRAVERSAL_PROTECTION_ACTIVE, 0.0, -1480, -790)
    traversal_camera = vector(
        material, TRAVERSAL_CAMERA,
        unreal.LinearColor(0.0, 0.0, 0.0, 0.0), -1480, -760)
    traversal_bird = vector(
        material, TRAVERSAL_BIRD,
        unreal.LinearColor(0.0, 0.0, 0.0, 0.0), -1480, -700)
    traversal_bird_count = scalar(
        material, TRAVERSAL_BIRD_COUNT, 0.0, -1220, -700)
    traversal_bird_spheres = []
    for index, parameter_name in enumerate(TRAVERSAL_BIRD_SPHERES):
        traversal_bird_spheres.append(vector(
            material, parameter_name,
            unreal.LinearColor(0.0, 0.0, 0.0, 1.0),
            -1220, -640 + index * 60))
    traversal_camera_radius = scalar(
        material, TRAVERSAL_CAMERA_RADIUS, 280.0, -1480, -640)
    traversal_bird_radius = scalar(
        material, TRAVERSAL_BIRD_RADIUS, 220.0, -1480, -580)
    traversal_corridor_radius = scalar(
        material, TRAVERSAL_CORRIDOR_RADIUS, 150.0, -1480, -520)
    traversal_feather = scalar(
        material, TRAVERSAL_FEATHER, 90.0, -1480, -460)
    traversal_retained_coverage = scalar(
        material, TRAVERSAL_RETAINED_COVERAGE, 0.82, -1480, -400)
    traversal_mask_frequency = scalar(
        material, TRAVERSAL_MASK_FREQUENCY, 0.012, -1480, -340)

    macro_nodes = []
    shape_nodes = []
    macro_height_nodes = []
    for index in range(MACRO_CLUSTER_COUNT):
        first_index = MACRO_DATA_INDEX + index * MACRO_DATA_STRIDE
        macro_nodes.append(custom_data_vector3(
            material, first_index, -1480, 820 + index * 180))
        shape_nodes.append(custom_data_vector3(
            material, first_index + 3, -920, 820 + index * 180))
        macro_height_nodes.append(custom_data(
            material, first_index + 6, 0.5, -360, 865 + index * 180))

    normal = expression(
        material, unreal.MaterialExpressionVertexNormalWS, -1160, -720)
    world_position = expression(
        material, unreal.MaterialExpressionWorldPosition, -1160, -820)
    input_specs = [
        ("WorldPos", world_position, ""),
        ("IslandCenter", island_center, ""),
        ("AxisX", axis_x, ""),
        ("AxisY", axis_y, ""),
        ("IslandUp", island_up, ""),
        ("IslandExtents", island_extents, ""),
        ("IslandColorVariant", island_color_variant, ""),
        ("LightColor", light_color, ""),
        ("BodyColor", body_color, ""),
        ("ShadowColor", shadow_color, ""),
        ("SunDirection", sun, ""),
        ("PlanetCenter", planet_center, ""),
        ("NightBrightness", night_brightness, ""),
        ("DaylightBlendMinSolarHeight", daylight_blend_min, ""),
        ("DaylightBlendMaxSolarHeight", daylight_blend_max, ""),
        ("MacroNormalStrength", macro_strength, ""),
        ("ContinuousOcclusionStrength", continuous_occlusion, ""),
        ("LightContrast", contrast, ""),
        ("LightBias", bias, ""),
        ("HeightLift", height_lift, ""),
        ("SunWhiteStrength", sun_white_strength, ""),
        ("ThinWhiteStrength", thin_white_strength, ""),
        ("ThinDensityStart", thin_density_start, ""),
        ("ThinDensityEnd", thin_density_end, ""),
        ("GradientConfidenceStart", gradient_confidence_start, ""),
        ("GradientConfidenceEnd", gradient_confidence_end, ""),
    ]
    for index in range(MACRO_CLUSTER_COUNT):
        input_specs.append(("Macro{}".format(index), macro_nodes[index], ""))
        input_specs.append(("Shape{}".format(index), shape_nodes[index], ""))
        input_specs.append((
            "MacroHeight{}".format(index), macro_height_nodes[index], ""))
    cloud_color = macro_lighting(
        material, input_specs, 40, -500)
    connect_property(
        cloud_color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    traversal_mask = traversal_opacity(
        material,
        [
            ("WorldPos", world_position, ""),
            ("IslandCenter", island_center, ""),
            ("AxisX", axis_x, ""),
            ("AxisY", axis_y, ""),
            ("TraversalActive", traversal_active, ""),
            ("TraversalProtectionActive", traversal_protection_active, ""),
            ("TraversalCameraWorld", traversal_camera, ""),
            ("TraversalBirdWorld", traversal_bird, ""),
            ("TraversalBirdCount", traversal_bird_count, ""),
            ("TraversalBirdSphere0", traversal_bird_spheres[0], ""),
            ("TraversalBirdSphere1", traversal_bird_spheres[1], ""),
            ("TraversalBirdSphere2", traversal_bird_spheres[2], ""),
            ("TraversalBirdSphere3", traversal_bird_spheres[3], ""),
            (TRAVERSAL_BIRD_SPHERE_RADIUS_INPUTS[0],
             traversal_bird_spheres[0], "A"),
            (TRAVERSAL_BIRD_SPHERE_RADIUS_INPUTS[1],
             traversal_bird_spheres[1], "A"),
            (TRAVERSAL_BIRD_SPHERE_RADIUS_INPUTS[2],
             traversal_bird_spheres[2], "A"),
            (TRAVERSAL_BIRD_SPHERE_RADIUS_INPUTS[3],
             traversal_bird_spheres[3], "A"),
            ("TraversalCameraRadiusCM", traversal_camera_radius, ""),
            ("TraversalBirdRadiusCM", traversal_bird_radius, ""),
            ("TraversalCorridorRadiusCM", traversal_corridor_radius, ""),
            ("TraversalFeatherCM", traversal_feather, ""),
            ("TraversalRetainedCoverage", traversal_retained_coverage, ""),
            ("TraversalMaskFrequency", traversal_mask_frequency, ""),
        ], 760, -260)
    connect_property(
        traversal_mask, "", unreal.MaterialProperty.MP_OPACITY_MASK)

    seed_offset_color = vector(
        material, "ABTS_CloudSeedOffset",
        unreal.LinearColor(137.0, 293.0, 419.0, 0.0), -1700, 260)
    seed_offset = multiply(
        material, seed, "", seed_offset_color, "", -1400, 260)
    noise_position = add(
        material, world_position, "", seed_offset, "", -920, 220)
    frequency = scalar(material, NOISE_FREQUENCY, 0.0105, -1160, 360)
    scaled_position = multiply(
        material, noise_position, "", frequency, "", -700, 240)
    noise = expression(material, unreal.MaterialExpressionNoise, -460, 220)
    noise.set_editor_property("scale", 1.0)
    noise.set_editor_property("quality", 2)
    noise.set_editor_property("levels", 3)
    noise.set_editor_property("output_min", -1.0)
    noise.set_editor_property("output_max", 1.0)
    noise.set_editor_property("turbulence", True)
    # UE 5.8's Python MaterialEditingLibrary exposes the Noise position as the
    # unnamed first input even though the native FExpressionInput is named
    # Position.  Connecting by the editor-visible label fails in commandlets.
    connect(scaled_position, "", noise, "")
    amplitude = scalar(material, NOISE_AMPLITUDE, 18.0, -700, 440)
    half = scalar(material, "ABTS_CloudSizeTierHalf", 0.5, -1160, 660)
    size_scale = add(material, half, "", size_tier, "", -920, 600)
    scaled_amplitude = multiply(
        material, amplitude, "", size_scale, "", -680, 540)
    displacement = multiply(
        material, noise, "", scaled_amplitude, "", -240, 300)
    wpo = multiply(material, normal, "", displacement, "", 20, 220)
    connect_property(wpo, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(
        material, only_if_is_dirty=False)
    return material


def rebuild_mesh():
    if unreal.EditorAssetLibrary.does_asset_exist(MESH_PATH):
        mesh = load_required(MESH_PATH)
    else:
        mesh = unreal.EditorAssetLibrary.duplicate_asset(
            SOURCE_MESH_PATH, MESH_PATH)
    if mesh is None:
        raise RuntimeError("Could not create {}".format(MESH_PATH))
    mesh.set_editor_property(
        "positive_bounds_extension", unreal.Vector(30.0, 30.0, 30.0))
    mesh.set_editor_property(
        "negative_bounds_extension", unreal.Vector(30.0, 30.0, 30.0))
    unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)
    return mesh


def validate():
    material = load_required(MASTER_PATH)
    mesh = load_required(MESH_PATH)
    if material.get_editor_property("shading_model") != (
            unreal.MaterialShadingModel.MSM_UNLIT):
        raise RuntimeError("Cloudlet material is not Unlit")
    if material.get_editor_property("blend_mode") != (
            unreal.BlendMode.BLEND_MASKED):
        raise RuntimeError("Cloudlet material is not Masked for A2.3")
    if not material.get_editor_property("has_pixel_animation"):
        raise RuntimeError(
            "Cloudlet material must mark masked/WPO pixels as animated for TSR")
    for usage in (
            unreal.MaterialUsage.MATUSAGE_STATIC_MESH,
            unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES):
        if not unreal.MaterialEditingLibrary.has_material_usage(material, usage):
            raise RuntimeError(
                "Cloudlet material lacks usage {}".format(usage))

    expressions = list(
        unreal.MaterialEditingLibrary.get_material_expressions(material))
    custom_nodes = [
        node for node in expressions
        if isinstance(node, unreal.MaterialExpressionPerInstanceCustomData)]
    custom_indices = sorted(
        int(node.get_editor_property("data_index")) for node in custom_nodes)
    if custom_indices != list(range(CUSTOM_DATA_COUNT)):
        raise RuntimeError(
            "Cloudlet material custom data mismatch: {}".format(custom_indices))
    macro_custom_nodes = [
        node for node in expressions
        if isinstance(node, unreal.MaterialExpressionCustom)
        and node.get_editor_property("desc") == MACRO_CUSTOM_DESCRIPTION]
    if len(macro_custom_nodes) != 1:
        raise RuntimeError(
            "Cloudlet continuous macro lighting node mismatch: {}".format(
                len(macro_custom_nodes)))
    traversal_nodes = [
        node for node in expressions
        if isinstance(node, unreal.MaterialExpressionCustom)
        and node.get_editor_property("desc") == TRAVERSAL_CUSTOM_DESCRIPTION]
    if len(traversal_nodes) != 1:
        raise RuntimeError(
            "Cloudlet bounded traversal node mismatch: {}".format(
                len(traversal_nodes)))
    traversal_inputs = [
        str(custom_input.get_editor_property("input_name"))
        for custom_input in traversal_nodes[0].get_editor_property("inputs")]
    expected_traversal_inputs = [
        "WorldPos", "IslandCenter", "AxisX", "AxisY",
        "TraversalActive", "TraversalProtectionActive",
        "TraversalCameraWorld", "TraversalBirdWorld",
        "TraversalBirdCount", "TraversalBirdSphere0",
        "TraversalBirdSphere1", "TraversalBirdSphere2",
        "TraversalBirdSphere3", "TraversalBirdSphereRadius0",
        "TraversalBirdSphereRadius1", "TraversalBirdSphereRadius2",
        "TraversalBirdSphereRadius3", "TraversalCameraRadiusCM",
        "TraversalBirdRadiusCM", "TraversalCorridorRadiusCM",
        "TraversalFeatherCM", "TraversalRetainedCoverage",
        "TraversalMaskFrequency"]
    if traversal_inputs != expected_traversal_inputs:
        raise RuntimeError(
            "Cloudlet traversal inputs mismatch: {}".format(traversal_inputs))
    traversal_code = traversal_nodes[0].get_editor_property("code")
    if any("TraversalBirdSphere{}.w".format(index) in traversal_code
           for index in range(4)):
        raise RuntimeError(
            "Cloudlet traversal must not read alpha from a float3 Custom input")
    for marker in (
            "ABTSProtectionActive", "ABTSCameraCoreClear", "ABTSBirdCoreClear",
            "ABTSCorridorInfluence", "ABTSCoreClear",
            "ABTSPartialCoverage", "ABTSCoverage",
            "ABTSPlanarUV", "ABTSNoise0", "ABTSNoise1",
            "ABTSPlanarNoise"):
        if marker not in traversal_code:
            raise RuntimeError(
                "Cloudlet traversal marker is missing: {}".format(marker))
    macro_inputs = [
        str(custom_input.get_editor_property("input_name"))
        for custom_input in macro_custom_nodes[0].get_editor_property("inputs")]
    expected_inputs = [
        "WorldPos", "IslandCenter", "AxisX", "AxisY", "IslandUp",
        "IslandExtents", "IslandColorVariant",
        "LightColor", "BodyColor", "ShadowColor",
        "SunDirection", "PlanetCenter", "NightBrightness",
        "DaylightBlendMinSolarHeight", "DaylightBlendMaxSolarHeight",
        "MacroNormalStrength",
        "ContinuousOcclusionStrength", "LightContrast", "LightBias",
        "HeightLift", "SunWhiteStrength", "ThinWhiteStrength",
        "ThinDensityStart", "ThinDensityEnd",
        "GradientConfidenceStart", "GradientConfidenceEnd"]
    for index in range(MACRO_CLUSTER_COUNT):
        expected_inputs.extend([
            "Macro{}".format(index),
            "Shape{}".format(index),
            "MacroHeight{}".format(index)])
    if macro_inputs != expected_inputs:
        raise RuntimeError(
            "Cloudlet macro lighting inputs mismatch: {}".format(macro_inputs))
    macro_code = macro_custom_nodes[0].get_editor_property("code")
    for marker in (
            "ABTSPlanarDensity", "ABTSVolumeGradientP", "ABTSSurfaceNormal",
            "ABTSVertical01", "ABTSVolumeDensity01", "ABTSMacroRelief",
            "ABTSStableDetail", "ABTSSunWhite", "ABTSThinness",
            "ABTSThinWhite", "ABTSWhiteWeight", "ABTSUndersideBlend",
            "ABTSCorePlanarWeight",
            "ABTSCoreInfluence",
            "ABTSUnionMacroHeight01", "ABTSGradientCoherence",
            "ABTSGradientConfidence", "ABTSNormalConfidence",
            "ABTSUnionVolumeDensity01", "ABTSCoreVolumeDensity01",
            "ABTSStableMacroJunction",
            "ABTSStableNormalWeight", "ABTSLocalDay", "ABTSSolarHeight",
            "ABTSNightRelief", "ABTSNightColor", "ABTSCloudColor"):
        if marker not in macro_code:
            raise RuntimeError(
                "Cloudlet view-invariant lighting marker is missing: {}".format(
                    marker))
    for forbidden_marker in (
            "CameraPos", "ABTSViewRay", "ABTSImplicitHit",
            "ABTSOpticalDepth", "ABTSSunwardRim"):
        if forbidden_marker in macro_code:
            raise RuntimeError(
                "Cloudlet material still contains a camera-dependent marker: {}".
                format(forbidden_marker))
    forbidden_pixel_inputs = (
        "LocalNormal", "InstanceHeight", "InstanceOcclusion", "Layer01",
        "BodyDetail", "CrownDetail", "EdgeDetail",
        "InstanceVariationStrength")
    if any(name in macro_inputs for name in forbidden_pixel_inputs):
        raise RuntimeError(
            "Cloudlet pixel lighting still consumes per-instance seam sources")
    if len([
            node for node in expressions
            if isinstance(node, unreal.MaterialExpressionNoise)]) != 1:
        raise RuntimeError("Cloudlet material must contain exactly one Noise node")

    positive = mesh.get_editor_property("positive_bounds_extension")
    negative = mesh.get_editor_property("negative_bounds_extension")
    if min(positive.x, positive.y, positive.z) < 18.0:
        raise RuntimeError("Cloudlet positive WPO bounds are insufficient")
    if min(negative.x, negative.y, negative.z) < 18.0:
        raise RuntimeError("Cloudlet negative WPO bounds are insufficient")
    unreal.log(
        "[ABTS][Rendering][T4-A2R1C2B3B6][Validated] Material={} Mesh={} "
        "Shading=Unlit StaticMeshUsage=1 InstancedUsage=1 CustomData={} "
        "MacroClusters=6 ViewInvariantIslandField=1 CameraDependentLighting=0 "
        "ViewInvariantVolumeGradient=1 ContinuousMacroNormal=1 "
        "GenericObjectToneBypass=1 PixelLocalNormalWeight=0 "
        "PixelInstanceVariation=0 ThreeBandColor=1 SunwardWhitening=1 "
        "ThinDensityWhitening=1 ViewIndependentWhitening=1 "
        "GradientCoherenceGuard=1 GradientJunctionGate=1 PlanarCoreClosure=1 UndersideField=1 CriticalPointFallback=IslandUp LocalSolarHeight=1 NightWhiteningGate=1 "
        "Noise=1 BoundsExtension=30 BoundedTraversal=1 "
        "StablePlanarNoiseCoverage=1 HardBirdCameraCore=1 "
        "RetainedCoverage=0.82 MaskFrequency=0.012 "
        "Blend=Masked"
        .format(MASTER_PATH, MESH_PATH, CUSTOM_DATA_COUNT))


def generate():
    ensure_directory(ROOT)
    if FORCE_REBUILD:
        rebuild_material()
        rebuild_mesh()
    validate()
    if not FORCE_REBUILD:
        unreal.log(
            "[ABTS][Rendering][T4-A2R1C2B3B6][NoRewrite] Root={}".format(ROOT))


generate()
