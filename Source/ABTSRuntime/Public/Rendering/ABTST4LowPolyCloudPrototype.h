// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"

/** Runtime-tunable A2.4 background weather-cluster population. */
struct ABTSRUNTIME_API FABTST4CloudClusterDistributionParameters
{
	static constexpr int32 MinClusterCount = 1;
	static constexpr int32 MaxClusterCount = 64;
	static constexpr float MinCloudsPerClusterMean = 1.0f;
	static constexpr float MaxCloudsPerClusterMean = 64.0f;
	static constexpr float MaxCloudsPerClusterVariance = 1024.0f;
	static constexpr int32 ProductionClusterCount = 24;
	static constexpr float ProductionCloudsPerClusterMean = 10.0f;
	static constexpr float ProductionCloudsPerClusterVariance = 64.0f;

	/** Frozen production weather-cluster count; never derived from cloud size. */
	int32 ClusterCount = ProductionClusterCount;
	/** Requested logical-cloud count per global weather cluster. */
	float CloudsPerClusterMean = ProductionCloudsPerClusterMean;
	/** Variance of the truncated Gaussian member-count sampler. */
	float CloudsPerClusterVariance = ProductionCloudsPerClusterVariance;

	bool IsValid() const
	{
		return ClusterCount >= MinClusterCount
			&& ClusterCount <= MaxClusterCount
			&& FMath::IsFinite(CloudsPerClusterMean)
			&& CloudsPerClusterMean >= MinCloudsPerClusterMean
			&& CloudsPerClusterMean <= MaxCloudsPerClusterMean
			&& FMath::IsFinite(CloudsPerClusterVariance)
			&& CloudsPerClusterVariance >= 0.0f
			&& CloudsPerClusterVariance <= MaxCloudsPerClusterVariance;
	}
};

/** One deterministic, planet-relative cloud island used by T4-A2R0. */
struct ABTSRUNTIME_API FABTST4LowPolyCloudIslandDefinition
{
	int32 IslandIndex = INDEX_NONE;
	/** Stable logical identity used for generation, LOD, hashing and diagnostics. */
	int32 LogicalCloudIndex = INDEX_NONE;
	int32 CloudletCount = 0;
	uint32 Seed = 0;
	FVector PlanetCenterWorld = FVector::ZeroVector;
	FVector CenterWorld = FVector::ZeroVector;
	FVector RadialUp = FVector::UpVector;
	FVector TangentX = FVector::ForwardVector;
	FVector TangentY = FVector::RightVector;
	FVector ExtentsCM = FVector::ZeroVector;
	/** Deterministic global grouping metadata used by A2.4 diagnostics. */
	int32 WeatherClusterIndex = INDEX_NONE;
	int32 WeatherClusterMemberIndex = INDEX_NONE;
	int32 WeatherClusterMemberCount = 0;
	/** True only for the sun-relative 30-degree terminator acceptance cluster. */
	bool bTerminatorMegaCluster = false;
	uint64 LogicalCloudIdentityHash = 0;
	uint64 IdentityHash = 0;

	bool IsValid() const;
};

/** CPU-side relation used to activate the bounded A2.3 visibility corridor. */
struct ABTSRUNTIME_API FABTST4CloudTraversalRelation
{
	bool bCameraInside = false;
	bool bBirdInside = false;
	bool bCloudBetweenCameraAndBird = false;
	bool bTraversalActive = false;
	/** Continuous envelope depths avoid a binary material/veil flip at cloud edges. */
	float CameraInteriorWeight = 0.0f;
	float BirdInteriorWeight = 0.0f;
	float CorridorInteriorWeight = 0.0f;
	float TraversalWeight = 0.0f;
	float ClosestSegmentAlpha = 0.0f;

	bool IsValid() const
	{
		return FMath::IsFinite(ClosestSegmentAlpha)
			&& ClosestSegmentAlpha >= 0.0f
			&& ClosestSegmentAlpha <= 1.0f
			&& FMath::IsFinite(CameraInteriorWeight)
			&& CameraInteriorWeight >= 0.0f
			&& CameraInteriorWeight <= 1.0f
			&& FMath::IsFinite(BirdInteriorWeight)
			&& BirdInteriorWeight >= 0.0f
			&& BirdInteriorWeight <= 1.0f
			&& FMath::IsFinite(CorridorInteriorWeight)
			&& CorridorInteriorWeight >= 0.0f
			&& CorridorInteriorWeight <= 1.0f
			&& FMath::IsFinite(TraversalWeight)
			&& TraversalWeight >= 0.0f
			&& TraversalWeight <= 1.0f
			&& bTraversalActive == (bCameraInside || bBirdInside
				|| bCloudBetweenCameraAndBird);
	}
};

/** Flat-shaded closed mesh data; positions are relative to PlanetCenterWorld. */
struct ABTSRUNTIME_API FABTST4LowPolyCloudMeshData
{
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	int32 LogicalVertexCount = 0;
	int32 LogicalTriangleCount = 0;
	bool bClosed = false;
	uint64 GeometryHash = 0;

	bool IsValid() const;
};

/** Visual role of one cloudlet in the deterministic R1-C2 hierarchy. */
enum class EABTST4CloudletLayer : uint8
{
	Body = 0,
	Crown,
	Edge
};

/** One deterministic non-convex mask lobe used by the R1-C2-A2 island. */
struct ABTSRUNTIME_API FABTST4CloudMacroClusterDefinition
{
	int32 IslandIndex = INDEX_NONE;
	int32 ClusterIndex = INDEX_NONE;
	FVector2D NormalizedCenter = FVector2D::ZeroVector;
	FVector2D NormalizedRadii = FVector2D::ZeroVector;
	float OrientationRadians = 0.0f;
	float HeightBias = 0.0f;
	uint64 IdentityHash = 0;

	bool IsValid() const;
};

/**
 * One deterministic R1-A cloudlet. The transform is relative to the planet-
 * centre prototype actor so the entire island can move without rewriting the
 * instance buffer. Custom data is deliberately material-agnostic until R1-B.
 */
struct ABTSRUNTIME_API FABTST4InstancedCloudletDefinition
{
	int32 IslandIndex = INDEX_NONE;
	int32 CloudletIndex = INDEX_NONE;
	int32 MacroClusterIndex = INDEX_NONE;
	EABTST4CloudletLayer Layer = EABTST4CloudletLayer::Body;
	FVector2D NormalizedPlanarCenter = FVector2D::ZeroVector;
	FVector2D NormalizedPlanarRadii = FVector2D::ZeroVector;
	float PlanarOrientationRadians = 0.0f;
	FVector RadialUp = FVector::UpVector;
	FTransform TransformRelativeToPlanet = FTransform::Identity;
	float Seed01 = 0.0f;
	float NormalizedHeight = 0.0f;
	float FakeOcclusion = 0.0f;
	float SizeTier = 0.0f;
	uint64 IdentityHash = 0;

	bool IsValid() const;
};

/** Pure deterministic layout/geometry contract shared by runtime and captures. */
class ABTSRUNTIME_API FABTST4LowPolyCloudPrototype
{
public:
	static constexpr int32 WeatherSystemCount =
		FABTST4CloudClusterDistributionParameters::ProductionClusterCount;
	/**
	 * Frozen production background count for cloud seed 0xC1A5466C and the
	 * accepted 24 / 10 / 64 distribution. Other PIE seeds remain dynamic.
	 */
	static constexpr int32 GlobalIslandCount = 277;
	static constexpr int32 TerminatorMegaClusterIslandCount = 7;
	/** Frozen production total; PIE tuning may produce a larger dynamic total. */
	static constexpr int32 IslandCount =
		GlobalIslandCount + TerminatorMegaClusterIslandCount;
	/** Fail-closed A2.4 guard: 384 background logical clouds / 32,256 instances. */
	static constexpr int32 MaxGlobalIslandCount = 384;
	static constexpr int32 MaxIslandCount =
		MaxGlobalIslandCount + TerminatorMegaClusterIslandCount;
	static constexpr int32 CloudletsPerIsland = 84;
	static constexpr int32 BodyCloudletsPerIsland = 24;
	static constexpr int32 CrownCloudletsPerIsland = 39;
	static constexpr int32 EdgeCloudletsPerIsland = 21;
	static constexpr int32 MacroClusterCountPerIsland = 6;
	// A2.4 batches every logical cloud into one HISM.  The first five values
	// retain the A2.1 cloudlet contract; the remaining values carry the exact
	// logical-island field that used to require one MID/draw submission per
	// cloud.  Six macro clusters each consume center/radii (3), shape (3) and
	// height (1), followed by one legacy colour-variant flag.
	static constexpr int32 CloudletBaseCustomDataFloatCount = 5;
	static constexpr int32 CloudletIslandCenterCustomDataIndex = 5;
	static constexpr int32 CloudletIslandAxisXCustomDataIndex = 8;
	static constexpr int32 CloudletIslandAxisYCustomDataIndex = 11;
	static constexpr int32 CloudletIslandUpCustomDataIndex = 14;
	static constexpr int32 CloudletIslandExtentsCustomDataIndex = 17;
	static constexpr int32 CloudletMacroCustomDataIndex = 20;
	static constexpr int32 CloudletMacroCustomDataStride = 7;
	static constexpr int32 CloudletColorVariantCustomDataIndex =
		CloudletMacroCustomDataIndex
		+ MacroClusterCountPerIsland * CloudletMacroCustomDataStride;
	static constexpr int32 CloudletCustomDataFloatCount =
		CloudletColorVariantCustomDataIndex + 1;
	static constexpr int32 TotalCloudletCount =
		IslandCount * CloudletsPerIsland;
	static constexpr int32 TotalBodyCloudletCount =
		IslandCount * BodyCloudletsPerIsland;
	static constexpr int32 TotalCrownCloudletCount =
		IslandCount * CrownCloudletsPerIsland;
	static constexpr int32 TotalEdgeCloudletCount =
		IslandCount * EdgeCloudletsPerIsland;
	static constexpr float NightBrightness = 0.42f;
	static constexpr float DaylightBlendMinSolarHeight = -0.16f;
	static constexpr float DaylightBlendMaxSolarHeight = 0.14f;

	static int32 GetCloudletLayerCount(
		int32 IslandIndex,
		EABTST4CloudletLayer Layer);

	static TArray<FABTST4LowPolyCloudIslandDefinition> BuildDefinitions(
		const FVector& PlanetCenterWorld,
		double PlanetRadiusCM,
		uint32 CloudFieldSeed,
		const FVector& SunDirectionToSunWorld,
		float CloudBaseAltitudeCM,
		float CloudLayerHeightCM);

	/** A2.4 overload used by PIE tuning with an explicit bounded population. */
	static TArray<FABTST4LowPolyCloudIslandDefinition> BuildDefinitions(
		const FVector& PlanetCenterWorld,
		double PlanetRadiusCM,
		uint32 CloudFieldSeed,
		const FVector& SunDirectionToSunWorld,
		float CloudBaseAltitudeCM,
		float CloudLayerHeightCM,
		const FABTST4CloudClusterDistributionParameters& Distribution);

	/** Pure deterministic partition consumed by runtime, logs and automation. */
	static TArray<int32> BuildGlobalClusterMemberCounts(
		uint32 CloudFieldSeed,
		const FABTST4CloudClusterDistributionParameters& Distribution);

	/** Build the seeded amorphous, azimuth-balanced mask for one island. */
	static TArray<FABTST4CloudMacroClusterDefinition> BuildMacroClusters(
		const FABTST4LowPolyCloudIslandDefinition& Definition);

	static bool BuildClosedMesh(
		const FABTST4LowPolyCloudIslandDefinition& Definition,
		FABTST4LowPolyCloudMeshData& OutMesh,
		FString* OutFailure = nullptr);

	/** Build the R1-C2-A4 seeded-amorphous spherical population. */
	static bool BuildInstancedCloudlets(
		const FABTST4LowPolyCloudIslandDefinition& Definition,
		TArray<FABTST4InstancedCloudletDefinition>& OutCloudlets,
		FString* OutFailure = nullptr);

	static uint64 ComputeLayoutHash(
		TConstArrayView<FABTST4LowPolyCloudIslandDefinition> Definitions);

	/** A2.2 logical identity used by runtime logs and capture manifests. */
	static uint64 ComputeLogicalCloudLayoutHash(
		TConstArrayView<FABTST4LowPolyCloudIslandDefinition> Definitions);

	/** Count deterministic neighbouring pairs used to prove cloud-field fusion. */
	static int32 CountCloudFusionPairs(
		TConstArrayView<FABTST4LowPolyCloudIslandDefinition> Definitions);

	/**
	 * True when every non-terminator weather cluster forms one connected graph
	 * under the calibrated visible cloud-envelope overlap relation.
	 */
	static bool AreBackgroundWeatherClusterEnvelopesConnected(
		TConstArrayView<FABTST4LowPolyCloudIslandDefinition> Definitions);

	/** Count the dedicated sun-relative members of the terminator test cluster. */
	static int32 CountTerminatorMegaClusterClouds(
		TConstArrayView<FABTST4LowPolyCloudIslandDefinition> Definitions);

	/** Full spherical span of the calibrated visible cloudlet support. */
	static double ComputeTerminatorMegaClusterAngularSpanDegrees(
		TConstArrayView<FABTST4LowPolyCloudIslandDefinition> Definitions);

	/** True when the seven calibrated visible supports form one connected mass. */
	static bool IsTerminatorMegaClusterEnvelopeConnected(
		TConstArrayView<FABTST4LowPolyCloudIslandDefinition> Definitions);

	/** Shared CPU oracle for the material's continuous local day/night blend. */
	static float ComputeLocalDaylightBlend(float SolarHeight);

	/**
	 * Camera-independent cloud envelope query. Padding starts the dissolve
	 * before the opaque cloud reaches the camera/bird line, leaving enough
	 * time for the bounded material corridor to open without a one-frame pop.
	 */
	static FABTST4CloudTraversalRelation EvaluateTraversalRelation(
		const FABTST4LowPolyCloudIslandDefinition& Cloud,
		const FVector& CameraWorld,
		const FVector& BirdWorld,
		float BirdRadiusCM,
		float EnvelopePaddingScale = 1.12f);

	static uint64 ComputeCloudletLayoutHash(
		TConstArrayView<FABTST4InstancedCloudletDefinition> Cloudlets);
};
