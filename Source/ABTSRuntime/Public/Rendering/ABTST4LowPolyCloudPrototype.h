// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"

/** One deterministic, planet-relative cloud island used by T4-A2R0. */
struct ABTSRUNTIME_API FABTST4LowPolyCloudIslandDefinition
{
	int32 IslandIndex = INDEX_NONE;
	uint32 Seed = 0;
	FVector PlanetCenterWorld = FVector::ZeroVector;
	FVector CenterWorld = FVector::ZeroVector;
	FVector RadialUp = FVector::UpVector;
	FVector TangentX = FVector::ForwardVector;
	FVector TangentY = FVector::RightVector;
	FVector ExtentsCM = FVector::ZeroVector;
	uint64 IdentityHash = 0;

	bool IsValid() const;
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
	static constexpr int32 IslandCount = 3;
	static constexpr int32 CloudletCustomDataFloatCount = 5;
	static constexpr int32 TotalCloudletCount = 252;
	static constexpr int32 MacroClusterCountPerIsland = 6;

	static int32 GetCloudletLayerCount(
		int32 IslandIndex,
		EABTST4CloudletLayer Layer);

	static TArray<FABTST4LowPolyCloudIslandDefinition> BuildDefinitions(
		const FVector& PlanetCenterWorld,
		double PlanetRadiusCM,
		const FVector& SunDirectionToSunWorld,
		float CloudBaseAltitudeCM,
		float CloudLayerHeightCM);

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

	static uint64 ComputeCloudletLayoutHash(
		TConstArrayView<FABTST4InstancedCloudletDefinition> Cloudlets);
};
