// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/ABTST4LowPolyCloudPrototype.h"

#include "Math/RotationMatrix.h"
#include "ProceduralMeshComponent.h"

namespace ABTST4LowPolyCloudPrototypePrivate
{
	constexpr int32 LatitudeSegments = 6;
	constexpr int32 LongitudeSegments = 12;
	// Macro-cluster centres and radii occupy roughly 64% of the authoring
	// extents. Using the raw island box made the data contract report a fused
	// weather mass while the rendered cloudlets still had visible sky gaps.
	constexpr double VisibleEnvelopeExtentScale = 0.64;

	double ComputeVisibleAngularRadiusRadians(
		const FABTST4LowPolyCloudIslandDefinition& Cloud)
	{
		const double CenterRadiusCM = FVector::Distance(
			Cloud.CenterWorld, Cloud.PlanetCenterWorld);
		return FMath::Atan2(
			FMath::Max(Cloud.ExtentsCM.X, Cloud.ExtentsCM.Y)
				* VisibleEnvelopeExtentScale,
			FMath::Max(CenterRadiusCM, 1.0));
	}

	bool DoVisibleEnvelopesOverlap(
		const FABTST4LowPolyCloudIslandDefinition& First,
		const FABTST4LowPolyCloudIslandDefinition& Second)
	{
		if (!First.IsValid() || !Second.IsValid())
		{
			return false;
		}
		const double CenterAngle = FMath::Acos(FMath::Clamp(
			FVector::DotProduct(First.RadialUp, Second.RadialUp),
			-1.0,
			1.0));
		const double AngularSupport =
			ComputeVisibleAngularRadiusRadians(First)
			+ ComputeVisibleAngularRadiusRadians(Second);
		const double FirstRadius = FVector::Distance(
			First.CenterWorld, First.PlanetCenterWorld);
		const double SecondRadius = FVector::Distance(
			Second.CenterWorld, Second.PlanetCenterWorld);
		const double RadialSupport = 0.92
			* (First.ExtentsCM.Z + Second.ExtentsCM.Z);
		return CenterAngle <= AngularSupport
			&& FMath::Abs(FirstRadius - SecondRadius) <= RadialSupport;
	}

	uint64 Mix64(uint64 A, uint64 B)
	{
		A ^= B + 0x9e3779b97f4a7c15ull + (A << 6) + (A >> 2);
		return A;
	}

	uint64 HashVector(const FVector& Value)
	{
		uint64 Hash = static_cast<uint64>(GetTypeHash(
			FMath::RoundToInt64(Value.X * 100.0)));
		Hash = Mix64(Hash, static_cast<uint64>(GetTypeHash(
			FMath::RoundToInt64(Value.Y * 100.0))));
		return Mix64(Hash, static_cast<uint64>(GetTypeHash(
			FMath::RoundToInt64(Value.Z * 100.0))));
	}

	float Hash01(const uint32 Seed, const uint32 Index, const uint32 Channel)
	{
		uint32 Value = Seed ^ (Index * 0x9e3779b9u)
			^ (Channel * 0x85ebca6bu);
		Value ^= Value >> 16;
		Value *= 0x7feb352du;
		Value ^= Value >> 15;
		Value *= 0x846ca68bu;
		Value ^= Value >> 16;
		return static_cast<float>(Value & 0x00ffffffu)
			/ static_cast<float>(0x01000000u);
	}

	int32 GetCloudletCount(const int32 IslandIndex)
	{
		return IslandIndex >= 0
			&& IslandIndex < FABTST4LowPolyCloudPrototype::MaxIslandCount
			? FABTST4LowPolyCloudPrototype::CloudletsPerIsland
			: 0;
	}

	int32 GetCloudletLayerCount(
		const int32 IslandIndex,
		const EABTST4CloudletLayer Layer)
	{
		if (IslandIndex < 0
			|| IslandIndex >= FABTST4LowPolyCloudPrototype::MaxIslandCount)
		{
			return 0;
		}
		switch (Layer)
		{
		case EABTST4CloudletLayer::Body:
			return FABTST4LowPolyCloudPrototype::BodyCloudletsPerIsland;
		case EABTST4CloudletLayer::Crown:
			return FABTST4LowPolyCloudPrototype::CrownCloudletsPerIsland;
		case EABTST4CloudletLayer::Edge:
			return FABTST4LowPolyCloudPrototype::EdgeCloudletsPerIsland;
		default:
			return 0;
		}
	}

	void AddLogicalTriangle(
		TArray<int32>& Triangles,
		int32 A,
		int32 B,
		int32 C)
	{
		Triangles.Add(A);
		Triangles.Add(B);
		Triangles.Add(C);
	}

	uint64 MakeUndirectedEdgeKey(int32 A, int32 B)
	{
		const uint32 Low = static_cast<uint32>(FMath::Min(A, B));
		const uint32 High = static_cast<uint32>(FMath::Max(A, B));
		return (static_cast<uint64>(Low) << 32) | High;
	}
}

bool FABTST4LowPolyCloudIslandDefinition::IsValid() const
{
	return IslandIndex >= 0
		&& LogicalCloudIndex >= 0
		&& LogicalCloudIndex < FABTST4LowPolyCloudPrototype::MaxIslandCount
		&& CloudletCount == FABTST4LowPolyCloudPrototype::CloudletsPerIsland
		&& Seed != 0
		&& !PlanetCenterWorld.ContainsNaN()
		&& !CenterWorld.ContainsNaN()
		&& RadialUp.IsNormalized()
		&& TangentX.IsNormalized()
		&& TangentY.IsNormalized()
		&& ExtentsCM.GetMin() > 100.0
		&& LogicalCloudIdentityHash != 0
		&& IdentityHash != 0;
}

bool FABTST4LowPolyCloudMeshData::IsValid() const
{
	return bClosed
		&& GeometryHash != 0
		&& LogicalVertexCount > 0
		&& LogicalTriangleCount > 0
		&& Vertices.Num() == Triangles.Num()
		&& Vertices.Num() == Normals.Num()
		&& Vertices.Num() == UV0.Num()
		&& Vertices.Num() == Colors.Num()
		&& Vertices.Num() == Tangents.Num()
		&& Triangles.Num() % 3 == 0;
}

bool FABTST4CloudMacroClusterDefinition::IsValid() const
{
	return IslandIndex >= 0
		&& ClusterIndex >= 0
		&& ClusterIndex
			< FABTST4LowPolyCloudPrototype::MacroClusterCountPerIsland
		&& !NormalizedCenter.ContainsNaN()
		&& !NormalizedRadii.ContainsNaN()
		&& NormalizedRadii.GetMin() > 0.05
		&& FMath::IsFinite(OrientationRadians)
		&& HeightBias >= 0.0f && HeightBias <= 1.0f
		&& IdentityHash != 0;
}

bool FABTST4InstancedCloudletDefinition::IsValid() const
{
	const FVector Location = TransformRelativeToPlanet.GetLocation();
	const FVector Scale = TransformRelativeToPlanet.GetScale3D();
	const int32 LayerValue = static_cast<int32>(Layer);
	return IslandIndex >= 0
		&& CloudletIndex >= 0
		&& MacroClusterIndex >= 0
		&& MacroClusterIndex
			< FABTST4LowPolyCloudPrototype::MacroClusterCountPerIsland
		&& LayerValue >= static_cast<int32>(EABTST4CloudletLayer::Body)
		&& LayerValue <= static_cast<int32>(EABTST4CloudletLayer::Edge)
		&& !NormalizedPlanarCenter.ContainsNaN()
		&& !NormalizedPlanarRadii.ContainsNaN()
		&& NormalizedPlanarRadii.GetMin() > 0.001
		&& FMath::IsFinite(PlanarOrientationRadians)
		&& RadialUp.IsNormalized()
		&& !TransformRelativeToPlanet.ContainsNaN()
		&& !Location.ContainsNaN()
		&& Scale.GetMin() > 0.01
		&& Seed01 >= 0.0f && Seed01 <= 1.0f
		&& NormalizedHeight >= 0.0f && NormalizedHeight <= 1.0f
		&& FakeOcclusion >= 0.0f && FakeOcclusion <= 1.0f
		&& SizeTier >= 0.0f && SizeTier <= 1.0f
		&& IdentityHash != 0;
}

FABTST4CloudTraversalRelation
FABTST4LowPolyCloudPrototype::EvaluateTraversalRelation(
	const FABTST4LowPolyCloudIslandDefinition& Cloud,
	const FVector& CameraWorld,
	const FVector& BirdWorld,
	const float BirdRadiusCM,
	const float EnvelopePaddingScale)
{
	FABTST4CloudTraversalRelation Result;
	if (!Cloud.IsValid()
		|| CameraWorld.ContainsNaN()
		|| BirdWorld.ContainsNaN()
		|| !FMath::IsFinite(BirdRadiusCM)
		|| !FMath::IsFinite(EnvelopePaddingScale)
		|| EnvelopePaddingScale < 1.0f)
	{
		return Result;
	}

	const FVector VisibleExtents(
		Cloud.ExtentsCM.X
			* ABTST4LowPolyCloudPrototypePrivate::VisibleEnvelopeExtentScale,
		Cloud.ExtentsCM.Y
			* ABTST4LowPolyCloudPrototypePrivate::VisibleEnvelopeExtentScale,
		Cloud.ExtentsCM.Z * 0.92);
	const FVector PaddedExtents = VisibleExtents
		* static_cast<double>(EnvelopePaddingScale)
		+ FVector(FMath::Max(0.0f, BirdRadiusCM));
	auto ToUnitEllipsoid = [&Cloud, &PaddedExtents](const FVector& World)
	{
		const FVector Relative = World - Cloud.CenterWorld;
		return FVector(
			FVector::DotProduct(Relative, Cloud.TangentX) / PaddedExtents.X,
			FVector::DotProduct(Relative, Cloud.TangentY) / PaddedExtents.Y,
			FVector::DotProduct(Relative, Cloud.RadialUp) / PaddedExtents.Z);
	};

	const FVector CameraLocal = ToUnitEllipsoid(CameraWorld);
	const FVector BirdLocal = ToUnitEllipsoid(BirdWorld);
	const double CameraRadius = CameraLocal.Size();
	const double BirdRadius = BirdLocal.Size();
	Result.bCameraInside = CameraRadius <= 1.0;
	Result.bBirdInside = BirdRadius <= 1.0;
	// The padded envelope already opens before visible cloud reaches either
	// endpoint. Blend across its final 22% instead of toggling at radius 1.0.
	// This remains a pure position query and therefore cannot pump with view
	// direction, frame rate or logical-cloud identity.
	auto ComputeInteriorWeight = [](const double UnitRadius)
	{
		return 1.0f - FMath::SmoothStep(
			0.82f,
			1.04f,
			static_cast<float>(UnitRadius));
	};
	Result.CameraInteriorWeight = ComputeInteriorWeight(CameraRadius);
	Result.BirdInteriorWeight = ComputeInteriorWeight(BirdRadius);

	const FVector Segment = BirdLocal - CameraLocal;
	const double SegmentSquared = Segment.SizeSquared();
	Result.ClosestSegmentAlpha = SegmentSquared > UE_DOUBLE_SMALL_NUMBER
		? static_cast<float>(FMath::Clamp(
			-FVector::DotProduct(CameraLocal, Segment) / SegmentSquared,
			0.0,
			1.0))
		: 0.0f;
	const FVector Closest = CameraLocal
		+ Segment * static_cast<double>(Result.ClosestSegmentAlpha);
	const double ClosestRadius = Closest.Size();
	Result.bCloudBetweenCameraAndBird =
		Result.ClosestSegmentAlpha > 0.001f
		&& Result.ClosestSegmentAlpha < 0.999f
		&& ClosestRadius <= 1.0;
	Result.CorridorInteriorWeight =
		Result.ClosestSegmentAlpha > 0.001f
		&& Result.ClosestSegmentAlpha < 0.999f
		? ComputeInteriorWeight(ClosestRadius)
		: 0.0f;
	Result.TraversalWeight = FMath::Max3(
		Result.CameraInteriorWeight,
		Result.BirdInteriorWeight,
		Result.CorridorInteriorWeight);
	Result.bTraversalActive = Result.bCameraInside
		|| Result.bBirdInside
		|| Result.bCloudBetweenCameraAndBird;
	return Result;
}

int32 FABTST4LowPolyCloudPrototype::GetCloudletLayerCount(
	const int32 IslandIndex,
	const EABTST4CloudletLayer Layer)
{
	return ABTST4LowPolyCloudPrototypePrivate::GetCloudletLayerCount(
		IslandIndex, Layer);
}

TArray<FABTST4LowPolyCloudIslandDefinition>
FABTST4LowPolyCloudPrototype::BuildDefinitions(
	const FVector& PlanetCenterWorld,
	const double PlanetRadiusCM,
	const uint32 CloudFieldSeed,
	const FVector& SunDirectionToSunWorld,
	const float CloudBaseAltitudeCM,
	const float CloudLayerHeightCM)

{
	return BuildDefinitions(
		PlanetCenterWorld,
		PlanetRadiusCM,
		CloudFieldSeed,
		SunDirectionToSunWorld,
		CloudBaseAltitudeCM,
		CloudLayerHeightCM,
		FABTST4CloudClusterDistributionParameters());
}

TArray<int32> FABTST4LowPolyCloudPrototype::BuildGlobalClusterMemberCounts(
	const uint32 CloudFieldSeed,
	const FABTST4CloudClusterDistributionParameters& Distribution)
{
	TArray<int32> Result;
	if (CloudFieldSeed == 0 || !Distribution.IsValid())
	{
		return Result;
	}
	int32 TotalMembers = 0;
	const double StandardDeviation = FMath::Sqrt(
		static_cast<double>(Distribution.CloudsPerClusterVariance));
	Result.Reserve(Distribution.ClusterCount);
	for (int32 ClusterIndex = 0;
		ClusterIndex < Distribution.ClusterCount; ++ClusterIndex)
	{
		const double U1 = FMath::Max(
			1.0e-6,
			static_cast<double>(ABTST4LowPolyCloudPrototypePrivate::Hash01(
				CloudFieldSeed, ClusterIndex, 91)));
		const double U2 = static_cast<double>(
			ABTST4LowPolyCloudPrototypePrivate::Hash01(
				CloudFieldSeed, ClusterIndex, 92));
		const double NormalSample = FMath::Sqrt(-2.0 * FMath::Loge(U1))
			* FMath::Cos(UE_TWO_PI * U2);
		int32 MemberCount = FMath::RoundToInt(
			static_cast<double>(Distribution.CloudsPerClusterMean)
				+ StandardDeviation * NormalSample);
		MemberCount = FMath::Clamp(
			MemberCount,
			1,
			static_cast<int32>(
				FABTST4CloudClusterDistributionParameters::
					MaxCloudsPerClusterMean));
		TotalMembers += MemberCount;
		if (TotalMembers > MaxGlobalIslandCount)
		{
			Result.Reset();
			return Result;
		}
		Result.Add(MemberCount);
	}
	return Result;
}

TArray<FABTST4LowPolyCloudIslandDefinition>
FABTST4LowPolyCloudPrototype::BuildDefinitions(
	const FVector& PlanetCenterWorld,
	const double PlanetRadiusCM,
	const uint32 CloudFieldSeed,
	const FVector& SunDirectionToSunWorld,
	const float CloudBaseAltitudeCM,
	const float CloudLayerHeightCM,
	const FABTST4CloudClusterDistributionParameters& Distribution)
{
	TArray<FABTST4LowPolyCloudIslandDefinition> Result;
	const FVector SunDirection = SunDirectionToSunWorld.GetSafeNormal();
	if (PlanetCenterWorld.ContainsNaN()
		|| !FMath::IsFinite(PlanetRadiusCM)
		|| PlanetRadiusCM <= 100.0
		|| CloudFieldSeed == 0
		|| SunDirection.IsNearlyZero()
		|| CloudBaseAltitudeCM <= 0.0f
		|| CloudLayerHeightCM <= 0.0f
		|| !Distribution.IsValid())
	{
		return Result;
	}
	const TArray<int32> GlobalClusterMemberCounts =
		BuildGlobalClusterMemberCounts(CloudFieldSeed, Distribution);
	int32 GlobalMemberTotal = 0;
	for (const int32 Count : GlobalClusterMemberCounts)
	{
		GlobalMemberTotal += Count;
	}
	if (GlobalMemberTotal <= 0 || GlobalMemberTotal > MaxGlobalIslandCount)
	{
		return Result;
	}

	Result.Reserve(GlobalMemberTotal + TerminatorMegaClusterIslandCount);
	auto AppendDefinition = [&Result, &PlanetCenterWorld, PlanetRadiusCM,
		CloudFieldSeed, CloudBaseAltitudeCM, CloudLayerHeightCM](
		const int32 Index,
		const FVector& Direction,
		const double SizeScale,
		const double Aspect,
		const double VerticalScale,
		const double AltitudeLayers,
		const bool bTerminatorMegaCluster,
		const int32 WeatherClusterIndex,
		const int32 WeatherClusterMemberIndex,
		const int32 WeatherClusterMemberCount)
	{
		const uint32 Seed = CloudFieldSeed
			^ (0x9e3779b9u * static_cast<uint32>(Index + 1));
		FABTST4LowPolyCloudIslandDefinition Definition;
		Definition.IslandIndex = Index;
		Definition.LogicalCloudIndex = Index;
		Definition.CloudletCount =
			ABTST4LowPolyCloudPrototypePrivate::GetCloudletCount(Index);
		Definition.Seed = Seed;
		Definition.PlanetCenterWorld = PlanetCenterWorld;
		Definition.RadialUp = Direction.GetSafeNormal();
		const FVector ReferenceAxis = FMath::Abs(Definition.RadialUp.Z) < 0.82
			? FVector::UpVector : FVector::ForwardVector;
		Definition.TangentX = FVector::VectorPlaneProject(
			ReferenceAxis, Definition.RadialUp).GetSafeNormal();
		if (Definition.TangentX.IsNearlyZero())
		{
			Definition.RadialUp.FindBestAxisVectors(
				Definition.TangentX, Definition.TangentY);
		}
		Definition.TangentY = FVector::CrossProduct(
			Definition.RadialUp, Definition.TangentX).GetSafeNormal();
		Definition.TangentX = FVector::CrossProduct(
			Definition.TangentY, Definition.RadialUp).GetSafeNormal();
		const double AltitudeCM = CloudBaseAltitudeCM
			+ CloudLayerHeightCM * AltitudeLayers;
		Definition.CenterWorld = PlanetCenterWorld
			+ Definition.RadialUp * (PlanetRadiusCM + AltitudeCM);
		const double HorizontalUnit = FMath::Clamp(
			PlanetRadiusCM * 0.075, 520.0, 1000.0);
		Definition.ExtentsCM = FVector(
			HorizontalUnit * SizeScale * Aspect,
			HorizontalUnit * SizeScale / Aspect,
			FMath::Max(
				static_cast<double>(CloudLayerHeightCM) * VerticalScale,
				360.0));
		Definition.bTerminatorMegaCluster = bTerminatorMegaCluster;
		Definition.WeatherClusterIndex = WeatherClusterIndex;
		Definition.WeatherClusterMemberIndex = WeatherClusterMemberIndex;
		Definition.WeatherClusterMemberCount = WeatherClusterMemberCount;
		uint64 LogicalIdentity = ABTST4LowPolyCloudPrototypePrivate::Mix64(
			0xA22C1000ull,
			static_cast<uint64>(Definition.LogicalCloudIndex + 1));
		LogicalIdentity = ABTST4LowPolyCloudPrototypePrivate::Mix64(
			LogicalIdentity, static_cast<uint64>(Definition.Seed));
		LogicalIdentity = ABTST4LowPolyCloudPrototypePrivate::Mix64(
			LogicalIdentity,
			ABTST4LowPolyCloudPrototypePrivate::HashVector(
				Definition.CenterWorld - PlanetCenterWorld));
		if (bTerminatorMegaCluster)
		{
			LogicalIdentity = ABTST4LowPolyCloudPrototypePrivate::Mix64(
				LogicalIdentity, 0x5445524d4d454741ull);
		}
		Definition.LogicalCloudIdentityHash = LogicalIdentity;
		uint64 Identity = ABTST4LowPolyCloudPrototypePrivate::Mix64(
			static_cast<uint64>(Definition.Seed),
			static_cast<uint64>(Index + 1));
		Identity = ABTST4LowPolyCloudPrototypePrivate::Mix64(
			Identity, Definition.LogicalCloudIdentityHash);
		Identity = ABTST4LowPolyCloudPrototypePrivate::Mix64(
			Identity,
			ABTST4LowPolyCloudPrototypePrivate::HashVector(
				Definition.CenterWorld - PlanetCenterWorld));
		Definition.IdentityHash = ABTST4LowPolyCloudPrototypePrivate::Mix64(
			Identity,
			ABTST4LowPolyCloudPrototypePrivate::HashVector(
				Definition.ExtentsCM));
		Definition.IdentityHash = ABTST4LowPolyCloudPrototypePrivate::Mix64(
			Definition.IdentityHash,
			static_cast<uint64>(Definition.CloudletCount));
		Result.Add(Definition);
	};

	constexpr double GoldenRatioConjugate = 0.6180339887498948482;
	struct FBackgroundMemberAuthoring
	{
		double SizeScale = 1.0;
		double Aspect = 1.0;
		double VerticalScale = 1.0;
		double AltitudeLayers = 1.0;
		double VisibleAngularRadius = 0.0;
	};
	int32 IslandIndex = 0;
	for (int32 ClusterIndex = 0;
		ClusterIndex < GlobalClusterMemberCounts.Num(); ++ClusterIndex)
	{
		const int32 MemberCount = GlobalClusterMemberCounts[ClusterIndex];
		const double Z = 1.0 - 2.0
			* (static_cast<double>(ClusterIndex) + 0.5)
			/ static_cast<double>(GlobalClusterMemberCounts.Num());
		const double RadiusXY = FMath::Sqrt(FMath::Max(0.0, 1.0 - Z * Z));
		const double Phase = ABTST4LowPolyCloudPrototypePrivate::Hash01(
			CloudFieldSeed, ClusterIndex, 93) * 0.22;
		const double Azimuth = UE_TWO_PI * FMath::Frac(
			(static_cast<double>(ClusterIndex) + 0.5)
				* GoldenRatioConjugate + Phase);
		const FVector ClusterDirection(
			RadiusXY * FMath::Cos(Azimuth),
			RadiusXY * FMath::Sin(Azimuth), Z);
		const double ClusterPhase = UE_TWO_PI
			* ABTST4LowPolyCloudPrototypePrivate::Hash01(
				CloudFieldSeed, ClusterIndex, 94);
		const double ClusterAltitudeLayers = FMath::Lerp(
			1.25,
			2.45,
			static_cast<double>(ABTST4LowPolyCloudPrototypePrivate::Hash01(
				CloudFieldSeed, ClusterIndex, 95)));
		const double HorizontalUnit = FMath::Clamp(
			PlanetRadiusCM * 0.075, 520.0, 1000.0);
		TArray<FBackgroundMemberAuthoring> MemberAuthoring;
		MemberAuthoring.Reserve(MemberCount);
		for (int32 MemberIndex = 0; MemberIndex < MemberCount; ++MemberIndex)
		{
			const int32 DefinitionIndex = IslandIndex + MemberIndex;
			const uint32 Seed = CloudFieldSeed
				^ (0x9e3779b9u * static_cast<uint32>(DefinitionIndex + 1));
			const double SizeSample = ABTST4LowPolyCloudPrototypePrivate::Hash01(
				Seed, DefinitionIndex, 23);
			const double AspectSample = ABTST4LowPolyCloudPrototypePrivate::Hash01(
				Seed, DefinitionIndex, 24);
			const double AltitudeJitter = FMath::Lerp(
				-0.14,
				0.14,
				static_cast<double>(ABTST4LowPolyCloudPrototypePrivate::Hash01(
					Seed, DefinitionIndex, 26)));
			FBackgroundMemberAuthoring Authoring;
			Authoring.SizeScale = FMath::Lerp(
				0.96, 1.78, FMath::Pow(SizeSample, 1.08));
			// Keep the authoring envelope below the 1.08 no-dominant-axis gate.
			// Large production populations otherwise expose rare endpoints that
			// read as a stretched puff even though the cluster remains connected.
			Authoring.Aspect = FMath::Lerp(0.965, 1.035, AspectSample);
			Authoring.VerticalScale = FMath::Lerp(
				0.66,
				1.10,
				static_cast<double>(ABTST4LowPolyCloudPrototypePrivate::Hash01(
					Seed, DefinitionIndex, 25)));
			Authoring.AltitudeLayers = FMath::Clamp(
				ClusterAltitudeLayers + AltitudeJitter, 1.0, 2.8);
			const double VisibleRadiusCM = HorizontalUnit
				* Authoring.SizeScale
				* FMath::Max(Authoring.Aspect, 1.0 / Authoring.Aspect)
				* ABTST4LowPolyCloudPrototypePrivate::VisibleEnvelopeExtentScale;
			const double CenterRadiusCM = PlanetRadiusCM + CloudBaseAltitudeCM
				+ CloudLayerHeightCM * Authoring.AltitudeLayers;
			Authoring.VisibleAngularRadius = FMath::Atan2(
				VisibleRadiusCM, FMath::Max(CenterRadiusCM, 1.0));
			MemberAuthoring.Add(Authoring);
		}

		TArray<FVector> MemberDirections;
		MemberDirections.Reserve(MemberCount);
		for (int32 MemberIndex = 0; MemberIndex < MemberCount; ++MemberIndex)
		{
			FVector Direction = ClusterDirection;
			if (MemberIndex > 0)
			{
				// A compact ternary growth tree makes every new island overlap a
				// previously accepted parent. This is a geometric connectivity
				// guarantee, not a proximity hint or a lucky random scatter.
				const int32 ParentIndex = (MemberIndex - 1) / 3;
				const FVector ParentDirection = MemberDirections[ParentIndex];
				FVector ParentTangentX;
				FVector ParentTangentY;
				ParentDirection.FindBestAxisVectors(
					ParentTangentX, ParentTangentY);
				const double AngleJitter = FMath::Lerp(
					-0.32,
					0.32,
					static_cast<double>(ABTST4LowPolyCloudPrototypePrivate::Hash01(
						CloudFieldSeed,
						ClusterIndex * 64 + MemberIndex,
						96)));
				const double MemberAngle = ClusterPhase
					+ static_cast<double>(MemberIndex) * UE_TWO_PI
						* GoldenRatioConjugate
					+ AngleJitter;
				const FVector GrowthTangent = (
					ParentTangentX * FMath::Cos(MemberAngle)
						+ ParentTangentY * FMath::Sin(MemberAngle)).GetSafeNormal();
				// Keep a broad overlap rather than a tangent contact. The calibrated
				// 0.56 step tolerates amorphous silhouette erosion and reads as one
				// cloud mass from the global overview instead of a necklace of puffs.
				const double StepAngle = 0.56 * (
					MemberAuthoring[ParentIndex].VisibleAngularRadius
					+ MemberAuthoring[MemberIndex].VisibleAngularRadius);
				Direction = (
					ParentDirection * FMath::Cos(StepAngle)
						+ GrowthTangent * FMath::Sin(StepAngle)).GetSafeNormal();
			}
			MemberDirections.Add(Direction);
			const FBackgroundMemberAuthoring& Authoring =
				MemberAuthoring[MemberIndex];
			AppendDefinition(
				IslandIndex,
				Direction,
				Authoring.SizeScale,
				Authoring.Aspect,
				Authoring.VerticalScale,
				Authoring.AltitudeLayers,
				false,
				ClusterIndex,
				MemberIndex,
				MemberCount);
			++IslandIndex;
		}
	}

	// One dedicated acceptance cluster spans the terminator. It is deliberately
	// separate from the 24 sun-independent background weather clusters: a central member
	// and six overlapping members form a connected approximately 30-degree
	// envelope. Four inner bridges prevent tangent-view gaps; two symmetric
	// outer members provide the full weather-front span across day and night.
	const FVector SeedReference(
		ABTST4LowPolyCloudPrototypePrivate::Hash01(
			CloudFieldSeed, 0, 80) * 2.0f - 1.0f,
		ABTST4LowPolyCloudPrototypePrivate::Hash01(
			CloudFieldSeed, 0, 81) * 2.0f - 1.0f,
		ABTST4LowPolyCloudPrototypePrivate::Hash01(
			CloudFieldSeed, 0, 82) * 2.0f - 1.0f);
	FVector TerminatorCenter = FVector::VectorPlaneProject(
		SeedReference, SunDirection).GetSafeNormal();
	if (TerminatorCenter.IsNearlyZero())
	{
		FVector UnusedAxis;
		SunDirection.FindBestAxisVectors(TerminatorCenter, UnusedAxis);
	}
	const FVector AcrossTerminator = SunDirection;
	const FVector AlongTerminator = FVector::CrossProduct(
		TerminatorCenter, AcrossTerminator).GetSafeNormal();
	const double ClusterPhase = UE_TWO_PI
		* ABTST4LowPolyCloudPrototypePrivate::Hash01(
			CloudFieldSeed, 0, 83);
	for (int32 MemberIndex = 0;
		MemberIndex < TerminatorMegaClusterIslandCount;
		++MemberIndex)
	{
		const int32 Index = GlobalMemberTotal + MemberIndex;
		const uint32 Seed = CloudFieldSeed
			^ (0x9e3779b9u * static_cast<uint32>(Index + 1));
		FVector Direction = TerminatorCenter;
		if (MemberIndex > 0)
		{
			double MemberAngle = 0.0;
			double OffsetDegrees = 0.0;
			if (MemberIndex <= 4)
			{
				const int32 InnerIndex = MemberIndex - 1;
				MemberAngle = ClusterPhase
					+ UE_HALF_PI * static_cast<double>(InnerIndex);
				const int32 OppositePair = InnerIndex % 2;
				OffsetDegrees = FMath::Lerp(
					4.8,
					5.2,
					static_cast<double>(
						ABTST4LowPolyCloudPrototypePrivate::Hash01(
							CloudFieldSeed, OppositePair, 85)));
			}
			else
			{
				const bool bOpposite = MemberIndex == 6;
				MemberAngle = ClusterPhase + 0.22
					+ (bOpposite ? UE_PI : 0.0);
				OffsetDegrees = FMath::Lerp(
					9.4,
					9.8,
					static_cast<double>(
						ABTST4LowPolyCloudPrototypePrivate::Hash01(
							CloudFieldSeed, 0, 86)));
			}
			const double OffsetAngle = FMath::DegreesToRadians(OffsetDegrees);
			const FVector RingTangent = (
				AcrossTerminator * FMath::Cos(MemberAngle)
					+ AlongTerminator * FMath::Sin(MemberAngle)).GetSafeNormal();
			Direction = (
				TerminatorCenter * FMath::Cos(OffsetAngle)
				+ RingTangent * FMath::Sin(OffsetAngle)).GetSafeNormal();
		}
		const double SizeSample =
			ABTST4LowPolyCloudPrototypePrivate::Hash01(
				Seed, MemberIndex, 86);
		const double AspectSample =
			ABTST4LowPolyCloudPrototypePrivate::Hash01(
				Seed, MemberIndex, 87);
		const double VerticalSample =
			ABTST4LowPolyCloudPrototypePrivate::Hash01(
				Seed, MemberIndex, 88);
		const double AltitudeSample =
			ABTST4LowPolyCloudPrototypePrivate::Hash01(
				Seed, MemberIndex, 89);
		// The authoring extent is intentionally larger than the populated
		// macro-cluster mask. These scales make the *visible* support overlap,
		// while the bridged centres retain the approximately 30-degree weather
		// footprint required by the terminator diagnostic.
		const double SizeScale = MemberIndex == 0
			? FMath::Lerp(2.30, 2.48, SizeSample)
			: FMath::Lerp(2.08, 2.32, SizeSample);
		AppendDefinition(
			Index,
			Direction,
			SizeScale,
			FMath::Lerp(0.97, 1.03, AspectSample),
			FMath::Lerp(0.78, 1.14, VerticalSample),
			FMath::Lerp(1.72, 1.88, AltitudeSample),
			true,
			GlobalClusterMemberCounts.Num(),
			MemberIndex,
			TerminatorMegaClusterIslandCount);
	}
	return Result;
}

TArray<FABTST4CloudMacroClusterDefinition>
FABTST4LowPolyCloudPrototype::BuildMacroClusters(
	const FABTST4LowPolyCloudIslandDefinition& Definition)
{
	TArray<FABTST4CloudMacroClusterDefinition> Result;
	if (!Definition.IsValid())
	{
		return Result;
	}
	// One off-centre core plus five seed-driven outer lobes gives every island
	// its own silhouette without sacrificing horizontal azimuth coverage. A
	// fixed five-point angular scaffold only prevents all lobes falling onto one
	// side; angle, distance, size, aspect, orientation and height remain seeded.
	constexpr int32 OuterClusterCount = MacroClusterCountPerIsland - 1;
	const double IslandPhase = UE_TWO_PI
		* ABTST4LowPolyCloudPrototypePrivate::Hash01(
			Definition.Seed, 0, 24);
	Result.Reserve(MacroClusterCountPerIsland);
	for (int32 Index = 0; Index < MacroClusterCountPerIsland; ++Index)
	{
		const float AngleSample = ABTST4LowPolyCloudPrototypePrivate::Hash01(
			Definition.Seed, Index, 25);
		const float DistanceSample = ABTST4LowPolyCloudPrototypePrivate::Hash01(
			Definition.Seed, Index, 26);
		const float RadiusSample = ABTST4LowPolyCloudPrototypePrivate::Hash01(
			Definition.Seed, Index, 27);
		const float AspectSample = ABTST4LowPolyCloudPrototypePrivate::Hash01(
			Definition.Seed, Index, 28);
		const float OrientationSample = ABTST4LowPolyCloudPrototypePrivate::Hash01(
			Definition.Seed, Index, 29);
		const float HeightSample = ABTST4LowPolyCloudPrototypePrivate::Hash01(
			Definition.Seed, Index, 30);
		double Angle = IslandPhase;
		double Distance = 0.0;
		double EquivalentRadius = 0.0;
		if (Index == 0)
		{
			Angle += UE_TWO_PI * static_cast<double>(AngleSample);
			Distance = FMath::Lerp(0.025, 0.085,
				static_cast<double>(DistanceSample));
			EquivalentRadius = FMath::Lerp(0.36, 0.40,
				static_cast<double>(RadiusSample));
		}
		else
		{
			const int32 OuterIndex = Index - 1;
			constexpr double AngularOffsetPattern[OuterClusterCount] = {
				-0.08, 0.06, -0.03, 0.08, -0.03
			};
			Angle += UE_TWO_PI * static_cast<double>(OuterIndex)
				/ static_cast<double>(OuterClusterCount)
				+ AngularOffsetPattern[OuterIndex]
				+ FMath::Lerp(-0.018, 0.018,
					static_cast<double>(AngleSample));
			// Stratified low-frequency ranges keep every seed amorphous instead
			// of relying on five random samples to accidentally span enough
			// distance and size. The island phase and per-lobe jitter still
			// remove any stable world-space direction.
			const double DistanceBand = static_cast<double>(OuterIndex % 3);
			const double RadiusBand = static_cast<double>((OuterIndex + 1) % 3);
			Distance = 0.20 + DistanceBand * 0.032
				+ static_cast<double>(DistanceSample) * 0.018;
			EquivalentRadius = 0.285 + RadiusBand * 0.018
				+ static_cast<double>(RadiusSample) * 0.010;
		}
		// Keep individual macro lobes close enough to round that the union remains
		// broad for every seed. The off-centre scaffold, radii bands and height
		// variation still provide the amorphous silhouette; allowing each lobe to
		// stretch independently creates rare long-axis tails in production-sized
		// cloud populations.
		const double Aspect = FMath::Lerp(0.90, 1.10,
			static_cast<double>(AspectSample));
		const double AspectRoot = FMath::Sqrt(Aspect);
		FABTST4CloudMacroClusterDefinition Cluster;
		Cluster.IslandIndex = Definition.IslandIndex;
		Cluster.ClusterIndex = Index;
		Cluster.NormalizedCenter = FVector2D(
			FMath::Cos(Angle), FMath::Sin(Angle)) * Distance;
		Cluster.NormalizedRadii = FVector2D(
			EquivalentRadius * AspectRoot,
			EquivalentRadius / AspectRoot);
		Cluster.OrientationRadians = IslandPhase
			+ UE_TWO_PI * static_cast<double>(OrientationSample);
		Cluster.HeightBias = Index == 0
			? FMath::Lerp(0.72f, 0.90f, HeightSample)
			: FMath::Lerp(0.18f, 0.82f, HeightSample);
		uint64 Identity = ABTST4LowPolyCloudPrototypePrivate::Mix64(
			Definition.IdentityHash, static_cast<uint64>(Index + 1));
		Identity = ABTST4LowPolyCloudPrototypePrivate::Mix64(
			Identity,
			ABTST4LowPolyCloudPrototypePrivate::HashVector(FVector(
				Cluster.NormalizedCenter.X,
				Cluster.NormalizedCenter.Y,
				Cluster.OrientationRadians)));
		Cluster.IdentityHash = ABTST4LowPolyCloudPrototypePrivate::Mix64(
			Identity,
			ABTST4LowPolyCloudPrototypePrivate::HashVector(FVector(
				Cluster.NormalizedRadii.X,
				Cluster.NormalizedRadii.Y,
				Cluster.HeightBias)));
		Result.Add(Cluster);
	}
	return Result;
}

bool FABTST4LowPolyCloudPrototype::BuildClosedMesh(
	const FABTST4LowPolyCloudIslandDefinition& Definition,
	FABTST4LowPolyCloudMeshData& OutMesh,
	FString* OutFailure)
{
	OutMesh = FABTST4LowPolyCloudMeshData();
	if (OutFailure != nullptr)
	{
		OutFailure->Reset();
	}
	if (!Definition.IsValid())
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = TEXT("Cloud island definition is invalid.");
		}
		return false;
	}

	TArray<FVector> LogicalVertices;
	TArray<FVector> LogicalVertexLobeCenters;
	TArray<int32> LogicalTriangles;
	struct FLobe
	{
		FVector CenterScale;
		FVector ExtentScale;
	};
	const FLobe Lobes[] = {
		{ FVector(0.00, 0.00, -0.06), FVector(0.64, 0.72, 0.62) },
		{ FVector(-0.43, -0.08, 0.05), FVector(0.52, 0.59, 0.58) },
		{ FVector(0.43, 0.06, 0.02), FVector(0.50, 0.57, 0.55) },
		{ FVector(-0.10, 0.02, 0.43), FVector(0.45, 0.50, 0.60) },
		{ FVector(0.08, 0.39, 0.18), FVector(0.47, 0.46, 0.50) }
	};
	const int32 VerticesPerLobe = 2
		+ (ABTST4LowPolyCloudPrototypePrivate::LatitudeSegments - 1)
			* ABTST4LowPolyCloudPrototypePrivate::LongitudeSegments;
	LogicalVertices.Reserve(UE_ARRAY_COUNT(Lobes) * VerticesPerLobe);
	LogicalVertexLobeCenters.Reserve(UE_ARRAY_COUNT(Lobes) * VerticesPerLobe);

	const double SeedPhase = static_cast<double>(Definition.Seed & 0xffffu)
		/ 65535.0 * UE_TWO_PI;
	for (int32 LobeIndex = 0; LobeIndex < UE_ARRAY_COUNT(Lobes); ++LobeIndex)
	{
		const FLobe& Lobe = Lobes[LobeIndex];
		const FVector LobeCenter = Lobe.CenterScale * Definition.ExtentsCM;
		const FVector LobeExtents = Lobe.ExtentScale * Definition.ExtentsCM;
		const int32 VertexBase = LogicalVertices.Num();
		LogicalVertices.Add(LobeCenter + FVector(0.0, 0.0, LobeExtents.Z));
		LogicalVertexLobeCenters.Add(LobeCenter);
		for (int32 Latitude = 1;
			Latitude < ABTST4LowPolyCloudPrototypePrivate::LatitudeSegments;
			++Latitude)
		{
			const double Theta = UE_PI * static_cast<double>(Latitude)
				/ ABTST4LowPolyCloudPrototypePrivate::LatitudeSegments;
			for (int32 Longitude = 0;
				Longitude < ABTST4LowPolyCloudPrototypePrivate::LongitudeSegments;
				++Longitude)
			{
				const double Phi = UE_TWO_PI * static_cast<double>(Longitude)
					/ ABTST4LowPolyCloudPrototypePrivate::LongitudeSegments;
				const double SinTheta = FMath::Sin(Theta);
				const FVector Unit(
					SinTheta * FMath::Cos(Phi),
					SinTheta * FMath::Sin(Phi),
					FMath::Cos(Theta));
				const double Puff = FMath::Clamp(
					1.0
					+ 0.055 * FMath::Sin(
						Phi * 3.0 + SeedPhase + LobeIndex * 0.83)
						* FMath::Square(SinTheta),
					0.90,
					1.10);
				const double LowerFlatten = Unit.Z < 0.0 ? 0.82 : 1.0;
				LogicalVertices.Add(LobeCenter + FVector(
					Unit.X * LobeExtents.X * Puff,
					Unit.Y * LobeExtents.Y * Puff,
					Unit.Z * LobeExtents.Z * Puff * LowerFlatten));
				LogicalVertexLobeCenters.Add(LobeCenter);
			}
		}
		const int32 BottomIndex = LogicalVertices.Add(
			LobeCenter + FVector(0.0, 0.0, -LobeExtents.Z * 0.82));
		LogicalVertexLobeCenters.Add(LobeCenter);

		for (int32 Longitude = 0;
			Longitude < ABTST4LowPolyCloudPrototypePrivate::LongitudeSegments;
			++Longitude)
		{
			const int32 Next = (Longitude + 1)
				% ABTST4LowPolyCloudPrototypePrivate::LongitudeSegments;
			ABTST4LowPolyCloudPrototypePrivate::AddLogicalTriangle(
				LogicalTriangles,
				VertexBase,
				VertexBase + 1 + Longitude,
				VertexBase + 1 + Next);
		}
		for (int32 Latitude = 0;
			Latitude < ABTST4LowPolyCloudPrototypePrivate::LatitudeSegments - 2;
			++Latitude)
		{
			const int32 CurrentStart = VertexBase + 1 + Latitude
				* ABTST4LowPolyCloudPrototypePrivate::LongitudeSegments;
			const int32 NextStart = CurrentStart
				+ ABTST4LowPolyCloudPrototypePrivate::LongitudeSegments;
			for (int32 Longitude = 0;
				Longitude < ABTST4LowPolyCloudPrototypePrivate::LongitudeSegments;
				++Longitude)
			{
				const int32 Next = (Longitude + 1)
					% ABTST4LowPolyCloudPrototypePrivate::LongitudeSegments;
				ABTST4LowPolyCloudPrototypePrivate::AddLogicalTriangle(
					LogicalTriangles, CurrentStart + Longitude,
					NextStart + Longitude, NextStart + Next);
				ABTST4LowPolyCloudPrototypePrivate::AddLogicalTriangle(
					LogicalTriangles, CurrentStart + Longitude,
					NextStart + Next, CurrentStart + Next);
			}
		}
		const int32 LastRingStart = BottomIndex
			- ABTST4LowPolyCloudPrototypePrivate::LongitudeSegments;
		for (int32 Longitude = 0;
			Longitude < ABTST4LowPolyCloudPrototypePrivate::LongitudeSegments;
			++Longitude)
		{
			const int32 Next = (Longitude + 1)
				% ABTST4LowPolyCloudPrototypePrivate::LongitudeSegments;
			ABTST4LowPolyCloudPrototypePrivate::AddLogicalTriangle(
				LogicalTriangles,
				LastRingStart + Longitude,
				BottomIndex,
				LastRingStart + Next);
		}
	}

	TMap<uint64, int32> EdgeUseCounts;
	for (int32 Triangle = 0; Triangle < LogicalTriangles.Num(); Triangle += 3)
	{
		const int32 A = LogicalTriangles[Triangle];
		const int32 B = LogicalTriangles[Triangle + 1];
		const int32 C = LogicalTriangles[Triangle + 2];
		++EdgeUseCounts.FindOrAdd(
			ABTST4LowPolyCloudPrototypePrivate::MakeUndirectedEdgeKey(A, B));
		++EdgeUseCounts.FindOrAdd(
			ABTST4LowPolyCloudPrototypePrivate::MakeUndirectedEdgeKey(B, C));
		++EdgeUseCounts.FindOrAdd(
			ABTST4LowPolyCloudPrototypePrivate::MakeUndirectedEdgeKey(C, A));
	}
	OutMesh.bClosed = true;
	for (const TPair<uint64, int32>& Edge : EdgeUseCounts)
	{
		if (Edge.Value != 2)
		{
			OutMesh.bClosed = false;
			break;
		}
	}
	if (!OutMesh.bClosed)
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = TEXT("Generated cloud topology is not closed.");
		}
		return false;
	}

	const FVector CenterRelative = Definition.CenterWorld
		- Definition.PlanetCenterWorld;
	OutMesh.LogicalVertexCount = LogicalVertices.Num();
	OutMesh.LogicalTriangleCount = LogicalTriangles.Num() / 3;
	OutMesh.Vertices.Reserve(LogicalTriangles.Num());
	OutMesh.Triangles.Reserve(LogicalTriangles.Num());
	OutMesh.Normals.Reserve(LogicalTriangles.Num());
	OutMesh.UV0.Reserve(LogicalTriangles.Num());
	OutMesh.Colors.Reserve(LogicalTriangles.Num());
	OutMesh.Tangents.Reserve(LogicalTriangles.Num());

	uint64 GeometryHash = Definition.IdentityHash;
	for (int32 Triangle = 0; Triangle < LogicalTriangles.Num(); Triangle += 3)
	{
		FVector Local[3] = {
			LogicalVertices[LogicalTriangles[Triangle]],
			LogicalVertices[LogicalTriangles[Triangle + 1]],
			LogicalVertices[LogicalTriangles[Triangle + 2]]
		};
		FVector WorldRelative[3];
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			WorldRelative[Corner] = CenterRelative
				+ Definition.TangentX * Local[Corner].X
				+ Definition.TangentY * Local[Corner].Y
				+ Definition.RadialUp * Local[Corner].Z;
		}
		FVector FaceNormal = FVector::CrossProduct(
			WorldRelative[1] - WorldRelative[0],
			WorldRelative[2] - WorldRelative[0]).GetSafeNormal();
		const FVector FaceCenter =
			(WorldRelative[0] + WorldRelative[1] + WorldRelative[2]) / 3.0;
		const FVector LocalLobeCenter =
			LogicalVertexLobeCenters[LogicalTriangles[Triangle]];
		const FVector WorldLobeCenter = CenterRelative
			+ Definition.TangentX * LocalLobeCenter.X
			+ Definition.TangentY * LocalLobeCenter.Y
			+ Definition.RadialUp * LocalLobeCenter.Z;
		if (FVector::DotProduct(FaceNormal, FaceCenter - WorldLobeCenter) < 0.0)
		{
			Swap(WorldRelative[1], WorldRelative[2]);
			FaceNormal *= -1.0;
		}
		// R0 is an exterior readability prototype, not a physically lit rock.
		// Bias every closed lobe toward the local radial up so its underside does
		// not collapse to black under the single directional sun. Retaining a
		// small face-normal contribution preserves the deliberately low-poly
		// value steps without requiring a new material asset in this phase.
		const FVector LightingNormal = (
			Definition.RadialUp * 0.82
			+ FaceNormal * 0.18).GetSafeNormal(
				UE_SMALL_NUMBER,
				Definition.RadialUp);
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const int32 VertexIndex = OutMesh.Vertices.Add(WorldRelative[Corner]);
			OutMesh.Triangles.Add(VertexIndex);
			OutMesh.Normals.Add(LightingNormal);
			OutMesh.UV0.Emplace(Corner == 1 ? 1.0 : 0.0, Corner == 2 ? 1.0 : 0.0);
			OutMesh.Colors.Add(FLinearColor::White);
			OutMesh.Tangents.Emplace(Definition.TangentX, false);
			GeometryHash = ABTST4LowPolyCloudPrototypePrivate::Mix64(
				GeometryHash,
				ABTST4LowPolyCloudPrototypePrivate::HashVector(WorldRelative[Corner]));
		}
	}
	OutMesh.GeometryHash = GeometryHash;
	if (!OutMesh.IsValid())
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = TEXT("Generated cloud mesh arrays are inconsistent.");
		}
		return false;
	}
	return true;
}

bool FABTST4LowPolyCloudPrototype::BuildInstancedCloudlets(
	const FABTST4LowPolyCloudIslandDefinition& Definition,
	TArray<FABTST4InstancedCloudletDefinition>& OutCloudlets,
	FString* OutFailure)
{
	OutCloudlets.Reset();
	if (OutFailure != nullptr)
	{
		OutFailure->Reset();
	}
	if (!Definition.IsValid())
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = TEXT("Cloud island definition is invalid.");
		}
		return false;
	}

	const int32 Count =
		ABTST4LowPolyCloudPrototypePrivate::GetCloudletCount(
			Definition.IslandIndex);
	if (Count <= 0)
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = TEXT("Cloud island has no R1-C2-A2 instance budget.");
		}
		return false;
	}
	const int32 BodyCount = GetCloudletLayerCount(
		Definition.IslandIndex, EABTST4CloudletLayer::Body);
	const int32 CrownCount = GetCloudletLayerCount(
		Definition.IslandIndex, EABTST4CloudletLayer::Crown);
	const int32 EdgeCount = GetCloudletLayerCount(
		Definition.IslandIndex, EABTST4CloudletLayer::Edge);
	if (BodyCount + CrownCount + EdgeCount != Count)
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = TEXT("R1-C2-A2 layer budget does not match island budget.");
		}
		return false;
	}
	const TArray<FABTST4CloudMacroClusterDefinition> MacroClusters =
		BuildMacroClusters(Definition);
	if (MacroClusters.Num() != MacroClusterCountPerIsland)
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = TEXT("R1-C2-A2 macro-cluster mask is incomplete.");
		}
		return false;
	}

	OutCloudlets.Reserve(Count);
	const FVector CenterRelative = Definition.CenterWorld
		- Definition.PlanetCenterWorld;
	const double ShellRadiusCM = CenterRelative.Size();
	constexpr double GoldenAngle = 2.39996322972865332;
	constexpr double EngineSphereRadiusCM = 50.0;
	const double MinHorizontalExtent = FMath::Min(
		Definition.ExtentsCM.X, Definition.ExtentsCM.Y);
	auto ResolveClusterAllocation = [Definition](
		const EABTST4CloudletLayer Layer,
		const int32 LayerIndex,
		const int32 LayerCount,
		int32& OutClusterIndex,
		int32& OutIndexWithinCluster,
		int32& OutCountWithinCluster)
	{
		const int32 BaseCount = LayerCount / MacroClusterCountPerIsland;
		const int32 Remainder = LayerCount % MacroClusterCountPerIsland;
		const int32 Rotation = (
			Definition.IslandIndex + static_cast<int32>(Layer) * 2)
			% MacroClusterCountPerIsland;
		int32 Cursor = 0;
		for (int32 Order = 0; Order < MacroClusterCountPerIsland; ++Order)
		{
			const int32 ClusterIndex = (Order + Rotation)
				% MacroClusterCountPerIsland;
			const int32 ClusterCount = BaseCount + (Order < Remainder ? 1 : 0);
			if (LayerIndex < Cursor + ClusterCount)
			{
				OutClusterIndex = ClusterIndex;
				OutIndexWithinCluster = LayerIndex - Cursor;
				OutCountWithinCluster = ClusterCount;
				return;
			}
			Cursor += ClusterCount;
		}
		OutClusterIndex = INDEX_NONE;
		OutIndexWithinCluster = INDEX_NONE;
		OutCountWithinCluster = 0;
	};

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const EABTST4CloudletLayer Layer = Index < BodyCount
			? EABTST4CloudletLayer::Body
			: (Index < BodyCount + CrownCount
				? EABTST4CloudletLayer::Crown
				: EABTST4CloudletLayer::Edge);
		const int32 LayerStart = Layer == EABTST4CloudletLayer::Body
			? 0
			: (Layer == EABTST4CloudletLayer::Crown
				? BodyCount
				: BodyCount + CrownCount);
		const int32 LayerIndex = Index - LayerStart;
		const int32 LayerCount = Layer == EABTST4CloudletLayer::Body
			? BodyCount
			: (Layer == EABTST4CloudletLayer::Crown
				? CrownCount
				: EdgeCount);
		int32 MacroClusterIndex = INDEX_NONE;
		int32 IndexWithinCluster = INDEX_NONE;
		int32 CountWithinCluster = 0;
		ResolveClusterAllocation(
			Layer,
			LayerIndex,
			LayerCount,
			MacroClusterIndex,
			IndexWithinCluster,
			CountWithinCluster);
		if (!MacroClusters.IsValidIndex(MacroClusterIndex)
			|| CountWithinCluster <= 0)
		{
			if (OutFailure != nullptr)
			{
				*OutFailure = TEXT("R1-C2-A2 cluster allocation failed.");
			}
			OutCloudlets.Reset();
			return false;
		}
		const FABTST4CloudMacroClusterDefinition& Cluster =
			MacroClusters[MacroClusterIndex];
		const float JitterRadius =
			ABTST4LowPolyCloudPrototypePrivate::Hash01(
				Definition.Seed, Index, 0);
		const float JitterAngle =
			ABTST4LowPolyCloudPrototypePrivate::Hash01(
				Definition.Seed, Index, 1);
		double LocalRadius01 = 0.0;
		double LocalAngle = 0.0;
		if (Layer == EABTST4CloudletLayer::Edge)
		{
			LocalAngle = UE_TWO_PI
				* (static_cast<double>(IndexWithinCluster)
					+ 0.12 * JitterAngle)
				/ static_cast<double>(CountWithinCluster);
			LocalRadius01 = FMath::Lerp(0.68, 0.76,
				static_cast<double>(JitterRadius));
		}
		else if (Layer == EABTST4CloudletLayer::Body)
		{
			if (IndexWithinCluster == 0)
			{
				LocalRadius01 = 0.0;
				LocalAngle = 0.0;
			}
			else
			{
				const int32 RingCount = FMath::Max(CountWithinCluster - 1, 1);
				LocalAngle = UE_TWO_PI
					* static_cast<double>(IndexWithinCluster - 1)
					/ static_cast<double>(RingCount)
					+ 0.10 * (JitterAngle - 0.5);
				LocalRadius01 = FMath::Lerp(0.34, 0.42,
					static_cast<double>(JitterRadius));
			}
		}
		else
		{
			LocalRadius01 = FMath::Sqrt(
				(static_cast<double>(IndexWithinCluster) + 0.34
					+ 0.28 * JitterRadius)
				/ static_cast<double>(CountWithinCluster)) * 0.64;
			LocalAngle = GoldenAngle * static_cast<double>(IndexWithinCluster)
				+ UE_TWO_PI * static_cast<double>(JitterAngle)
					/ static_cast<double>(CountWithinCluster);
		}
		const double ClusterCos = FMath::Cos(Cluster.OrientationRadians);
		const double ClusterSin = FMath::Sin(Cluster.OrientationRadians);
		const double ClusterLocalX = FMath::Cos(LocalAngle)
			* LocalRadius01 * Cluster.NormalizedRadii.X;
		const double ClusterLocalY = FMath::Sin(LocalAngle)
			* LocalRadius01 * Cluster.NormalizedRadii.Y;
		const double X01 = Cluster.NormalizedCenter.X
			+ ClusterLocalX * ClusterCos - ClusterLocalY * ClusterSin;
		const double Y01 = Cluster.NormalizedCenter.Y
			+ ClusterLocalX * ClusterSin + ClusterLocalY * ClusterCos;
		const double Edge01 = FMath::Clamp(
			LocalRadius01, 0.0, 1.0);
		const double Core01 = 1.0 - Edge01;

		const float ShapeJitter =
			ABTST4LowPolyCloudPrototypePrivate::Hash01(
				Definition.Seed, Index, 2);
		const float HeightJitter =
			ABTST4LowPolyCloudPrototypePrivate::Hash01(
				Definition.Seed, Index, 3);
		const float SpinJitter =
			ABTST4LowPolyCloudPrototypePrivate::Hash01(
				Definition.Seed, Index, 4);
		double HorizontalRadiusXCM = 0.0;
		double HorizontalRadiusYCM = 0.0;
		double VerticalRadiusCM = 0.0;
		double NormalizedHeight = 0.0;
		double PlanarOrientationRadians = 0.0;
		if (Layer == EABTST4CloudletLayer::Body)
		{
			HorizontalRadiusXCM = Definition.ExtentsCM.X
				* Cluster.NormalizedRadii.X
				* FMath::Lerp(0.68, 0.74, static_cast<double>(ShapeJitter));
			HorizontalRadiusYCM = Definition.ExtentsCM.Y
				* Cluster.NormalizedRadii.Y
				* FMath::Lerp(0.66, 0.72, static_cast<double>(HeightJitter));
			VerticalRadiusCM = FMath::Min(
				HorizontalRadiusXCM, HorizontalRadiusYCM)
				* FMath::Lerp(0.28, 0.38, static_cast<double>(HeightJitter));
			NormalizedHeight = FMath::Clamp(
				0.08 + 0.14 * Core01 + 0.06 * HeightJitter
					+ 0.08 * Cluster.HeightBias, 0.0, 1.0);
			PlanarOrientationRadians = Cluster.OrientationRadians
				+ FMath::Lerp(-0.10, 0.10, static_cast<double>(SpinJitter));
		}
		else if (Layer == EABTST4CloudletLayer::Crown)
		{
			const double ClusterRadiusCM = FMath::Min(
				Definition.ExtentsCM.X * Cluster.NormalizedRadii.X,
				Definition.ExtentsCM.Y * Cluster.NormalizedRadii.Y);
			const double RadiusCM = ClusterRadiusCM
				* FMath::Lerp(0.30, 0.46, Core01)
				* FMath::Lerp(0.88, 1.13, static_cast<double>(ShapeJitter));
			HorizontalRadiusXCM = RadiusCM
				* FMath::Lerp(0.96, 1.18, static_cast<double>(ShapeJitter));
			HorizontalRadiusYCM = RadiusCM
				* FMath::Lerp(0.92, 1.12, static_cast<double>(SpinJitter));
			VerticalRadiusCM = RadiusCM
				* FMath::Lerp(0.82, 1.18, static_cast<double>(HeightJitter));
			NormalizedHeight = FMath::Clamp(
				0.25 + 0.36 * FMath::Pow(Core01, 1.20)
					+ 0.13 * HeightJitter + 0.22 * Cluster.HeightBias,
				0.0, 1.0);
			PlanarOrientationRadians = Cluster.OrientationRadians
				+ UE_TWO_PI * SpinJitter;
		}
		else
		{
			const double ClusterRadiusCM = FMath::Min(
				Definition.ExtentsCM.X * Cluster.NormalizedRadii.X,
				Definition.ExtentsCM.Y * Cluster.NormalizedRadii.Y);
			const double RadiusCM = ClusterRadiusCM
				* FMath::Lerp(0.27, 0.34, Core01)
				* FMath::Lerp(0.88, 1.12, static_cast<double>(ShapeJitter));
			HorizontalRadiusXCM = RadiusCM
				* FMath::Lerp(1.10, 1.30, static_cast<double>(ShapeJitter));
			HorizontalRadiusYCM = RadiusCM
				* FMath::Lerp(0.90, 1.08, static_cast<double>(HeightJitter));
			VerticalRadiusCM = RadiusCM
				* FMath::Lerp(0.68, 0.94, static_cast<double>(HeightJitter));
			NormalizedHeight = FMath::Clamp(
				0.08 + 0.13 * Core01 + 0.08 * HeightJitter
					+ 0.06 * Cluster.HeightBias, 0.0, 1.0);
			PlanarOrientationRadians = Cluster.OrientationRadians
				+ FMath::Atan2(
					FMath::Cos(LocalAngle), -FMath::Sin(LocalAngle))
				+ FMath::Lerp(-0.14, 0.14, static_cast<double>(SpinJitter));
		}
		const double LocalZ = -Definition.ExtentsCM.Z * 0.22
			+ VerticalRadiusCM * 0.52
			+ Definition.ExtentsCM.Z * NormalizedHeight * 0.64;
		const FVector TangentOffset =
			Definition.TangentX * (X01 * Definition.ExtentsCM.X)
			+ Definition.TangentY * (Y01 * Definition.ExtentsCM.Y);
		const double ArcAngle = TangentOffset.Size() / ShellRadiusCM;
		const FVector CloudletUp = (
			Definition.RadialUp * FMath::Cos(ArcAngle)
			+ TangentOffset.GetSafeNormal() * FMath::Sin(ArcAngle))
			.GetSafeNormal(UE_SMALL_NUMBER, Definition.RadialUp);
		const FVector Translation = CloudletUp * (ShellRadiusCM + LocalZ);
		FVector LocalTangentX = FVector::VectorPlaneProject(
			Definition.TangentX, CloudletUp).GetSafeNormal();
		if (LocalTangentX.IsNearlyZero())
		{
			FVector LocalTangentFallback;
			CloudletUp.FindBestAxisVectors(
				LocalTangentX, LocalTangentFallback);
		}
		const FVector LocalTangentY = FVector::CrossProduct(
			CloudletUp, LocalTangentX).GetSafeNormal();
		const FVector PlanarAxisX = (
			LocalTangentX * FMath::Cos(PlanarOrientationRadians)
			+ LocalTangentY * FMath::Sin(PlanarOrientationRadians))
			.GetSafeNormal();
		const FQuat Rotation = FRotationMatrix::MakeFromXZ(
			PlanarAxisX, CloudletUp).ToQuat();
		const FVector Scale(
			HorizontalRadiusXCM / EngineSphereRadiusCM,
			HorizontalRadiusYCM / EngineSphereRadiusCM,
			VerticalRadiusCM / EngineSphereRadiusCM);
		const double EquivalentRadiusCM = FMath::Sqrt(
			HorizontalRadiusXCM * HorizontalRadiusYCM);

		FABTST4InstancedCloudletDefinition Cloudlet;
		Cloudlet.IslandIndex = Definition.IslandIndex;
		Cloudlet.CloudletIndex = Index;
		Cloudlet.MacroClusterIndex = MacroClusterIndex;
		Cloudlet.Layer = Layer;
		Cloudlet.NormalizedPlanarCenter = FVector2D(X01, Y01);
		Cloudlet.NormalizedPlanarRadii = FVector2D(
			HorizontalRadiusXCM / Definition.ExtentsCM.X,
			HorizontalRadiusYCM / Definition.ExtentsCM.Y);
		Cloudlet.PlanarOrientationRadians =
			static_cast<float>(PlanarOrientationRadians);
		Cloudlet.RadialUp = CloudletUp;
		Cloudlet.TransformRelativeToPlanet = FTransform(
			Rotation, Translation, Scale);
		Cloudlet.Seed01 =
			ABTST4LowPolyCloudPrototypePrivate::Hash01(
				Definition.Seed, Index, 5);
		Cloudlet.NormalizedHeight = static_cast<float>(NormalizedHeight);
		Cloudlet.FakeOcclusion = static_cast<float>(FMath::Clamp(
			0.14 + 0.62 * (1.0 - NormalizedHeight) + 0.20 * Core01,
			0.0,
			1.0));
		Cloudlet.SizeTier = EquivalentRadiusCM > MinHorizontalExtent * 0.16
			? 1.0f
			: (EquivalentRadiusCM > MinHorizontalExtent * 0.105 ? 0.5f : 0.0f);
		uint64 Identity = ABTST4LowPolyCloudPrototypePrivate::Mix64(
			Definition.IdentityHash, static_cast<uint64>(Index + 1));
		Identity = ABTST4LowPolyCloudPrototypePrivate::Mix64(
			Identity,
			ABTST4LowPolyCloudPrototypePrivate::HashVector(Translation));
		Identity = ABTST4LowPolyCloudPrototypePrivate::Mix64(
			Identity,
			ABTST4LowPolyCloudPrototypePrivate::HashVector(Scale));
		Identity = ABTST4LowPolyCloudPrototypePrivate::Mix64(
			Identity,
			static_cast<uint64>(FMath::RoundToInt(Cloudlet.Seed01 * 65535.0f)));
		Identity = ABTST4LowPolyCloudPrototypePrivate::Mix64(
			Identity, static_cast<uint64>(static_cast<int32>(Layer)) + 1);
		Identity = ABTST4LowPolyCloudPrototypePrivate::Mix64(
			Identity, static_cast<uint64>(MacroClusterIndex + 1));
		Cloudlet.IdentityHash = Identity;
		if (!Cloudlet.IsValid())
		{
			OutCloudlets.Reset();
			if (OutFailure != nullptr)
			{
				*OutFailure = FString::Printf(
					TEXT("Cloudlet %d failed validation."), Index);
			}
			return false;
		}
		OutCloudlets.Add(Cloudlet);
	}
	return true;
}

uint64 FABTST4LowPolyCloudPrototype::ComputeLayoutHash(
	const TConstArrayView<FABTST4LowPolyCloudIslandDefinition> Definitions)
{
	uint64 Hash = 0xA2C1F00Dull;
	for (const FABTST4LowPolyCloudIslandDefinition& Definition : Definitions)
	{
		Hash = ABTST4LowPolyCloudPrototypePrivate::Mix64(
			Hash, Definition.IdentityHash);
	}
	return Definitions.IsEmpty() ? 0 : Hash;
}

uint64 FABTST4LowPolyCloudPrototype::ComputeLogicalCloudLayoutHash(
	const TConstArrayView<FABTST4LowPolyCloudIslandDefinition> Definitions)
{
	uint64 Hash = 0xA22C600Dull;
	TSet<int32> SeenLogicalClouds;
	for (const FABTST4LowPolyCloudIslandDefinition& Definition : Definitions)
	{
		if (!Definition.IsValid()
			|| SeenLogicalClouds.Contains(Definition.LogicalCloudIndex))
		{
			return 0;
		}
		SeenLogicalClouds.Add(Definition.LogicalCloudIndex);
		Hash = ABTST4LowPolyCloudPrototypePrivate::Mix64(
			Hash, static_cast<uint64>(Definition.LogicalCloudIndex + 1));
		Hash = ABTST4LowPolyCloudPrototypePrivate::Mix64(
			Hash, Definition.LogicalCloudIdentityHash);
	}
	return Definitions.IsEmpty() ? 0 : Hash;
}

int32 FABTST4LowPolyCloudPrototype::CountCloudFusionPairs(
	const TConstArrayView<FABTST4LowPolyCloudIslandDefinition> Definitions)
{
	if (Definitions.IsEmpty())
	{
		return 0;
	}
	int32 PairCount = 0;
	const double MinimumAlignment = FMath::Cos(FMath::DegreesToRadians(23.0));
	for (int32 FirstIndex = 0; FirstIndex < Definitions.Num(); ++FirstIndex)
	{
		const FABTST4LowPolyCloudIslandDefinition& First =
			Definitions[FirstIndex];
		if (!First.IsValid())
		{
			return 0;
		}
		for (int32 SecondIndex = FirstIndex + 1;
			SecondIndex < Definitions.Num(); ++SecondIndex)
		{
			const FABTST4LowPolyCloudIslandDefinition& Second =
				Definitions[SecondIndex];
			if (!Second.IsValid())
			{
				return 0;
			}
			if (FVector::DotProduct(First.RadialUp, Second.RadialUp)
				>= MinimumAlignment)
			{
				++PairCount;
			}
		}
	}
	return PairCount;
}

bool FABTST4LowPolyCloudPrototype::
	AreBackgroundWeatherClusterEnvelopesConnected(
		const TConstArrayView<FABTST4LowPolyCloudIslandDefinition> Definitions)
{
	TMap<int32, TArray<const FABTST4LowPolyCloudIslandDefinition*>> Clusters;
	for (const FABTST4LowPolyCloudIslandDefinition& Definition : Definitions)
	{
		if (Definition.bTerminatorMegaCluster)
		{
			continue;
		}
		if (!Definition.IsValid() || Definition.WeatherClusterIndex < 0)
		{
			return false;
		}
		Clusters.FindOrAdd(Definition.WeatherClusterIndex).Add(&Definition);
	}
	if (Clusters.IsEmpty())
	{
		return false;
	}
	for (const TPair<int32,
		TArray<const FABTST4LowPolyCloudIslandDefinition*>>& Pair : Clusters)
	{
		const TArray<const FABTST4LowPolyCloudIslandDefinition*>& Members =
			Pair.Value;
		if (Members.IsEmpty())
		{
			return false;
		}
		const int32 ExpectedMemberCount = Members[0]->WeatherClusterMemberCount;
		if (ExpectedMemberCount <= 0 || ExpectedMemberCount != Members.Num())
		{
			return false;
		}
		TBitArray<> SeenMemberIndices(false, ExpectedMemberCount);
		for (const FABTST4LowPolyCloudIslandDefinition* Member : Members)
		{
			if (Member == nullptr
				|| Member->WeatherClusterIndex != Pair.Key
				|| Member->WeatherClusterMemberCount != ExpectedMemberCount
				|| Member->WeatherClusterMemberIndex < 0
				|| Member->WeatherClusterMemberIndex >= ExpectedMemberCount
				|| SeenMemberIndices[Member->WeatherClusterMemberIndex])
			{
				return false;
			}
			SeenMemberIndices[Member->WeatherClusterMemberIndex] = true;
		}

		TBitArray<> Visited(false, Members.Num());
		TArray<int32> Pending;
		Visited[0] = true;
		Pending.Add(0);
		while (!Pending.IsEmpty())
		{
			const int32 FirstIndex = Pending.Pop(EAllowShrinking::No);
			for (int32 SecondIndex = 0; SecondIndex < Members.Num(); ++SecondIndex)
			{
				if (Visited[SecondIndex] || FirstIndex == SecondIndex)
				{
					continue;
				}
				if (ABTST4LowPolyCloudPrototypePrivate::DoVisibleEnvelopesOverlap(
					*Members[FirstIndex], *Members[SecondIndex]))
				{
					Visited[SecondIndex] = true;
					Pending.Add(SecondIndex);
				}
			}
		}
		if (Visited.CountSetBits() != Members.Num())
		{
			return false;
		}
	}
	return true;
}

int32 FABTST4LowPolyCloudPrototype::CountTerminatorMegaClusterClouds(
	const TConstArrayView<FABTST4LowPolyCloudIslandDefinition> Definitions)
{
	int32 Count = 0;
	for (const FABTST4LowPolyCloudIslandDefinition& Definition : Definitions)
	{
		if (Definition.IsValid() && Definition.bTerminatorMegaCluster)
		{
			++Count;
		}
	}
	return Count;
}

double FABTST4LowPolyCloudPrototype::
	ComputeTerminatorMegaClusterAngularSpanDegrees(
		const TConstArrayView<FABTST4LowPolyCloudIslandDefinition> Definitions)
{
	TArray<const FABTST4LowPolyCloudIslandDefinition*> Members;
	for (const FABTST4LowPolyCloudIslandDefinition& Definition : Definitions)
	{
		if (Definition.IsValid() && Definition.bTerminatorMegaCluster)
		{
			Members.Add(&Definition);
		}
	}
	if (Members.IsEmpty())
	{
		return 0.0;
	}
	double MaximumSpanRadians =
		ABTST4LowPolyCloudPrototypePrivate::ComputeVisibleAngularRadiusRadians(
			*Members[0]) * 2.0;
	for (int32 FirstIndex = 0; FirstIndex < Members.Num(); ++FirstIndex)
	{
		for (int32 SecondIndex = FirstIndex + 1;
			SecondIndex < Members.Num(); ++SecondIndex)
		{
			const double CenterAngle = FMath::Acos(FMath::Clamp(
				FVector::DotProduct(
					Members[FirstIndex]->RadialUp,
					Members[SecondIndex]->RadialUp),
				-1.0,
				1.0));
			MaximumSpanRadians = FMath::Max(
				MaximumSpanRadians,
				CenterAngle
					+ ABTST4LowPolyCloudPrototypePrivate::
						ComputeVisibleAngularRadiusRadians(*Members[FirstIndex])
					+ ABTST4LowPolyCloudPrototypePrivate::
						ComputeVisibleAngularRadiusRadians(*Members[SecondIndex]));
		}
	}
	return FMath::RadiansToDegrees(MaximumSpanRadians);
}

bool FABTST4LowPolyCloudPrototype::
	IsTerminatorMegaClusterEnvelopeConnected(
		const TConstArrayView<FABTST4LowPolyCloudIslandDefinition> Definitions)
{
	TArray<const FABTST4LowPolyCloudIslandDefinition*> Members;
	for (const FABTST4LowPolyCloudIslandDefinition& Definition : Definitions)
	{
		if (Definition.IsValid() && Definition.bTerminatorMegaCluster)
		{
			Members.Add(&Definition);
		}
	}
	if (Members.Num() != TerminatorMegaClusterIslandCount)
	{
		return false;
	}
	TArray<int32> Pending;
	TBitArray<> Visited(false, Members.Num());
	Pending.Add(0);
	Visited[0] = true;
	while (!Pending.IsEmpty())
	{
		const int32 FirstIndex = Pending.Pop(EAllowShrinking::No);
		for (int32 SecondIndex = 0;
			SecondIndex < Members.Num(); ++SecondIndex)
		{
			if (Visited[SecondIndex] || FirstIndex == SecondIndex)
			{
				continue;
			}
			const double CenterAngle = FMath::Acos(FMath::Clamp(
				FVector::DotProduct(
					Members[FirstIndex]->RadialUp,
					Members[SecondIndex]->RadialUp),
				-1.0,
				1.0));
			const double ConnectedEnvelope = 1.02 * (
				ABTST4LowPolyCloudPrototypePrivate::
					ComputeVisibleAngularRadiusRadians(*Members[FirstIndex])
					+ ABTST4LowPolyCloudPrototypePrivate::
						ComputeVisibleAngularRadiusRadians(*Members[SecondIndex]));
			if (CenterAngle <= ConnectedEnvelope)
			{
				Visited[SecondIndex] = true;
				Pending.Add(SecondIndex);
			}
		}
	}
	return Visited.CountSetBits() == Members.Num();
}

float FABTST4LowPolyCloudPrototype::ComputeLocalDaylightBlend(
	const float SolarHeight)
{
	const float Alpha = FMath::Clamp(
		(SolarHeight - DaylightBlendMinSolarHeight)
			/ (DaylightBlendMaxSolarHeight - DaylightBlendMinSolarHeight),
		0.0f,
		1.0f);
	return Alpha * Alpha * (3.0f - 2.0f * Alpha);
}

uint64 FABTST4LowPolyCloudPrototype::ComputeCloudletLayoutHash(
	const TConstArrayView<FABTST4InstancedCloudletDefinition> Cloudlets)
{
	uint64 Hash = 0xA2C1A11Aull;
	for (const FABTST4InstancedCloudletDefinition& Cloudlet : Cloudlets)
	{
		Hash = ABTST4LowPolyCloudPrototypePrivate::Mix64(
			Hash, Cloudlet.IdentityHash);
	}
	return Cloudlets.IsEmpty() ? 0 : Hash;
}
