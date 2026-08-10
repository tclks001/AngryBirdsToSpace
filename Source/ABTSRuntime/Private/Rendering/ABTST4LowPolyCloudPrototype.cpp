// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/ABTST4LowPolyCloudPrototype.h"

#include "Math/RotationMatrix.h"
#include "ProceduralMeshComponent.h"

namespace ABTST4LowPolyCloudPrototypePrivate
{
	constexpr int32 LatitudeSegments = 6;
	constexpr int32 LongitudeSegments = 12;

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
		constexpr int32 Counts[FABTST4LowPolyCloudPrototype::IslandCount] = {
			96, 72, 84
		};
		return IslandIndex >= 0
			&& IslandIndex < FABTST4LowPolyCloudPrototype::IslandCount
			? Counts[IslandIndex]
			: 0;
	}

	int32 GetCloudletLayerCount(
		const int32 IslandIndex,
		const EABTST4CloudletLayer Layer)
	{
		constexpr int32 BodyCounts[
			FABTST4LowPolyCloudPrototype::IslandCount] = { 28, 21, 24 };
		constexpr int32 CrownCounts[
			FABTST4LowPolyCloudPrototype::IslandCount] = { 44, 33, 39 };
		constexpr int32 EdgeCounts[
			FABTST4LowPolyCloudPrototype::IslandCount] = { 24, 18, 21 };
		if (IslandIndex < 0
			|| IslandIndex >= FABTST4LowPolyCloudPrototype::IslandCount)
		{
			return 0;
		}
		switch (Layer)
		{
		case EABTST4CloudletLayer::Body:
			return BodyCounts[IslandIndex];
		case EABTST4CloudletLayer::Crown:
			return CrownCounts[IslandIndex];
		case EABTST4CloudletLayer::Edge:
			return EdgeCounts[IslandIndex];
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
		&& Seed != 0
		&& !PlanetCenterWorld.ContainsNaN()
		&& !CenterWorld.ContainsNaN()
		&& RadialUp.IsNormalized()
		&& TangentX.IsNormalized()
		&& TangentY.IsNormalized()
		&& ExtentsCM.GetMin() > 100.0
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
	const FVector& SunDirectionToSunWorld,
	const float CloudBaseAltitudeCM,
	const float CloudLayerHeightCM)
{
	TArray<FABTST4LowPolyCloudIslandDefinition> Result;
	const FVector Sun = SunDirectionToSunWorld.GetSafeNormal();
	if (PlanetCenterWorld.ContainsNaN()
		|| !FMath::IsFinite(PlanetRadiusCM)
		|| PlanetRadiusCM <= 100.0
		|| Sun.IsNearlyZero()
		|| CloudBaseAltitudeCM <= 0.0f
		|| CloudLayerHeightCM <= 0.0f)
	{
		return Result;
	}

	FVector AxisA;
	FVector AxisB;
	Sun.FindBestAxisVectors(AxisA, AxisB);
	AxisA.Normalize();
	AxisB = FVector::CrossProduct(Sun, AxisA).GetSafeNormal();

	const FVector Directions[IslandCount] = {
		(Sun * 0.82 + AxisA * 0.47 + AxisB * 0.32).GetSafeNormal(),
		(Sun * 0.66 - AxisA * 0.69 + AxisB * 0.27).GetSafeNormal(),
		(Sun * 0.48 + AxisA * 0.70 - AxisB * 0.53).GetSafeNormal()
	};
	const FVector ExtentScales[IslandCount] = {
		FVector(1.80, 1.68, 0.78),
		FVector(1.42, 1.34, 0.68),
		FVector(2.05, 1.90, 0.84)
	};
	const float AltitudeScales[IslandCount] = { 1.20f, 1.90f, 2.70f };
	const uint32 Seeds[IslandCount] = {
		0xA2C10001u, 0xA2C10002u, 0xA2C10003u
	};

	Result.Reserve(IslandCount);
	for (int32 Index = 0; Index < IslandCount; ++Index)
	{
		FABTST4LowPolyCloudIslandDefinition Definition;
		Definition.IslandIndex = Index;
		Definition.Seed = Seeds[Index];
		Definition.PlanetCenterWorld = PlanetCenterWorld;
		Definition.RadialUp = Directions[Index];
		Definition.TangentX = FVector::VectorPlaneProject(
			AxisA, Definition.RadialUp).GetSafeNormal();
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
			+ CloudLayerHeightCM * AltitudeScales[Index];
		Definition.CenterWorld = PlanetCenterWorld
			+ Definition.RadialUp * (PlanetRadiusCM + AltitudeCM);
		const double HorizontalUnit = FMath::Clamp(
			PlanetRadiusCM * 0.105, 650.0, 1250.0);
		Definition.ExtentsCM = FVector(
			HorizontalUnit * ExtentScales[Index].X,
			HorizontalUnit * ExtentScales[Index].Y,
			FMath::Max(
				static_cast<double>(CloudLayerHeightCM)
					* ExtentScales[Index].Z,
				420.0));
		uint64 Identity = ABTST4LowPolyCloudPrototypePrivate::Mix64(
			static_cast<uint64>(Definition.Seed),
			static_cast<uint64>(Index + 1));
		Identity = ABTST4LowPolyCloudPrototypePrivate::Mix64(
			Identity,
			ABTST4LowPolyCloudPrototypePrivate::HashVector(
				Definition.CenterWorld - PlanetCenterWorld));
		Definition.IdentityHash = ABTST4LowPolyCloudPrototypePrivate::Mix64(
			Identity,
			ABTST4LowPolyCloudPrototypePrivate::HashVector(
				Definition.ExtentsCM));
		Result.Add(Definition);
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
			EquivalentRadius = FMath::Lerp(0.34, 0.39,
				static_cast<double>(RadiusSample));
		}
		else
		{
			const int32 OuterIndex = Index - 1;
			Angle += UE_TWO_PI * static_cast<double>(OuterIndex)
				/ static_cast<double>(OuterClusterCount)
				+ FMath::Lerp(-0.24, 0.24,
					static_cast<double>(AngleSample));
			Distance = FMath::Lerp(0.20, 0.32,
				static_cast<double>(DistanceSample));
			EquivalentRadius = FMath::Lerp(0.28, 0.35,
				static_cast<double>(RadiusSample));
		}
		const double Aspect = FMath::Lerp(0.84, 1.18,
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
