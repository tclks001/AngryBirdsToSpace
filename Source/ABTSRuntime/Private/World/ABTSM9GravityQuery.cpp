// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM9GravityQuery.h"

#include "EngineUtils.h"
#include "World/ABTSM9Satellite.h"

namespace ABTSM9GravityQueryPrivate
{
	constexpr uint64 FnvOffsetBasis64 = 14695981039346656037ull;
	constexpr uint64 FnvPrime64 = 1099511628211ull;

	void AppendHash(uint64& InOutHash, const int64 Value)
	{
		const uint64 Bits = static_cast<uint64>(Value);
		for (int32 ByteIndex = 0; ByteIndex < 8; ++ByteIndex)
		{
			InOutHash ^= (Bits >> (ByteIndex * 8)) & 0xffull;
			InOutHash *= FnvPrime64;
		}
	}

	int64 Quantize(const double Value)
	{
		return FMath::RoundToInt64(Value * 1000.0);
	}

	struct FSatelliteHashEntry
	{
		FVector RelativeCenter = FVector::ZeroVector;
		float RadiusCM = 0.0f;
		float SurfaceGravityCMPerSec2 = 0.0f;
		bool bEnabled = false;
	};
}

FVector ABTSM9Gravity::GetSatelliteAcceleration(const UWorld* World, const FVector& WorldLocation)
{
	if (World == nullptr) return FVector::ZeroVector;
	FVector Acceleration = FVector::ZeroVector;
	for (TActorIterator<AABTSM9Satellite> It(World); It; ++It)
	{
		Acceleration += It->GetGravityAccelerationAt(WorldLocation);
	}
	return Acceleration;
}

uint64 ABTSM9Gravity::GetSatelliteGravitySnapshotHash(
	const UWorld* World,
	const FVector& PrimaryCenterWorld)
{
	using namespace ABTSM9GravityQueryPrivate;
	uint64 Hash = FnvOffsetBasis64;
	if (World == nullptr)
	{
		AppendHash(Hash, 0);
		return Hash;
	}
	TArray<FSatelliteHashEntry> Entries;
	for (TActorIterator<AABTSM9Satellite> It(World); It; ++It)
	{
		FSatelliteHashEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.RelativeCenter = It->GetPlanetCenterWorld() - PrimaryCenterWorld;
		Entry.RadiusCM = It->GetPlanetRadiusCM();
		Entry.SurfaceGravityCMPerSec2 =
			It->GetSurfaceGravityAccelerationCMPerSec2();
		Entry.bEnabled = It->bGravityEnabled;
	}
	Entries.Sort([](const FSatelliteHashEntry& A, const FSatelliteHashEntry& B)
	{
		const int64 AX = Quantize(A.RelativeCenter.X);
		const int64 BX = Quantize(B.RelativeCenter.X);
		if (AX != BX)
		{
			return AX < BX;
		}
		const int64 AY = Quantize(A.RelativeCenter.Y);
		const int64 BY = Quantize(B.RelativeCenter.Y);
		if (AY != BY)
		{
			return AY < BY;
		}
		const int64 AZ = Quantize(A.RelativeCenter.Z);
		const int64 BZ = Quantize(B.RelativeCenter.Z);
		if (AZ != BZ)
		{
			return AZ < BZ;
		}
		const int64 ARadius = Quantize(A.RadiusCM);
		const int64 BRadius = Quantize(B.RadiusCM);
		if (ARadius != BRadius)
		{
			return ARadius < BRadius;
		}
		const int64 AGravity = Quantize(A.SurfaceGravityCMPerSec2);
		const int64 BGravity = Quantize(B.SurfaceGravityCMPerSec2);
		if (AGravity != BGravity)
		{
			return AGravity < BGravity;
		}
		return static_cast<int32>(A.bEnabled)
			< static_cast<int32>(B.bEnabled);
	});
	AppendHash(Hash, Entries.Num());
	for (const FSatelliteHashEntry& Entry : Entries)
	{
		AppendHash(Hash, Quantize(Entry.RelativeCenter.X));
		AppendHash(Hash, Quantize(Entry.RelativeCenter.Y));
		AppendHash(Hash, Quantize(Entry.RelativeCenter.Z));
		AppendHash(Hash, Quantize(Entry.RadiusCM));
		AppendHash(Hash, Quantize(Entry.SurfaceGravityCMPerSec2));
		AppendHash(Hash, Entry.bEnabled ? 1 : 0);
	}
	return Hash;
}
