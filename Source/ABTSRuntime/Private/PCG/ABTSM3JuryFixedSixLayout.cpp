// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCG/ABTSM3JuryFixedSixLayout.h"

#include "Algo/Find.h"
#include "Planet/ABTSM2Planet.h"

namespace ABTSM3JuryFixedSixPrivate
{
constexpr uint64 JuryFNVOffset = 14695981039346656037ull;
constexpr uint64 JuryFNVPrime = 1099511628211ull;
constexpr double AxisTolerance = 1.0e-4;
constexpr double PadSeparationMarginCM = 180.0;

struct FJuryCanonicalHash
{
	uint64 Value = JuryFNVOffset;

	void AddByte(const uint8 Byte)
	{
		Value ^= Byte;
		Value *= JuryFNVPrime;
	}

	void AddInt32(const int32 InValue)
	{
		const uint32 Bits = static_cast<uint32>(InValue);
		for (int32 Shift = 0; Shift < 32; Shift += 8)
		{
			AddByte(static_cast<uint8>((Bits >> Shift) & 0xffu));
		}
	}

	void AddInt64(const int64 InValue)
	{
		const uint64 Bits = static_cast<uint64>(InValue);
		for (int32 Shift = 0; Shift < 64; Shift += 8)
		{
			AddByte(static_cast<uint8>((Bits >> Shift) & 0xffull));
		}
	}

	void AddIntArray(const TArray<int32>& Values)
	{
		AddInt32(Values.Num());
		for (const int32 Element : Values)
		{
			AddInt32(Element);
		}
	}

	void AddName(const FName& Name)
	{
		const FString Text = Name.ToString();
		AddInt32(Text.Len());
		for (const TCHAR Character : Text)
		{
			AddInt32(static_cast<int32>(Character));
		}
	}

	void AddScalarCM(const double ValueCM)
	{
		AddInt64(FMath::RoundToInt64(ValueCM * 1000.0));
	}

	void AddVectorCM(const FVector& Vector)
	{
		AddScalarCM(Vector.X);
		AddScalarCM(Vector.Y);
		AddScalarCM(Vector.Z);
	}

	void AddVector2DCM(const FVector2D& Vector)
	{
		AddScalarCM(Vector.X);
		AddScalarCM(Vector.Y);
	}

	void AddBoxCM(const FBox& Box)
	{
		AddInt32(Box.IsValid != 0 ? 1 : 0);
		if (Box.IsValid != 0)
		{
			AddVectorCM(Box.Min);
			AddVectorCM(Box.Max);
		}
	}

	uint64 Get() const
	{
		return Value;
	}
};

FABTSM3JuryBuildingPlacementFixture MakeJuryFixture(
	const TCHAR* ManifestEntryId,
	const TCHAR* StableId,
	const int32 DifficultyTier,
	const int32 BuildingSeed,
	const FVector& BoundsMin,
	const FVector& BoundsMax,
	const FVector2D& PadHalfExtentCM,
	const uint64 StaticGeometryHash,
	const uint64 SourceDescriptorHash,
	const uint64 ProductionIdentityHash,
	const uint64 DeviceAssemblyHash,
	const FVector& EffectBoundsMin,
	const FVector& EffectBoundsMax,
	const bool bDynamicEnvelopeRequired)
{
	FABTSM3JuryBuildingPlacementFixture Fixture;
	Fixture.ManifestEntryId = FName(ManifestEntryId);
	Fixture.StableId = FName(StableId);
	Fixture.DifficultyTier = DifficultyTier;
	Fixture.BuildingSeed = BuildingSeed;
	Fixture.LocalBounds = FBox(BoundsMin, BoundsMax);
	Fixture.PhysicalBounds = Fixture.LocalBounds;
	Fixture.EffectBounds = FBox(EffectBoundsMin, EffectBoundsMax);
	Fixture.RequiredPadHalfExtentCM = PadHalfExtentCM;
	Fixture.StaticGeometryHash = static_cast<int64>(StaticGeometryHash);
	Fixture.SourceDescriptorHash = static_cast<int64>(SourceDescriptorHash);
	Fixture.ProductionIdentityHash = static_cast<int64>(ProductionIdentityHash);
	Fixture.DeviceAssemblyHash = static_cast<int64>(DeviceAssemblyHash);
	Fixture.bDynamicEnvelopeRequired = bDynamicEnvelopeRequired;
	return Fixture;
}

const TArray<FABTSM3JuryBuildingPlacementFixture>& GetJuryFixtures()
{
	static const TArray<FABTSM3JuryBuildingPlacementFixture> Fixtures = {
		MakeJuryFixture(
			TEXT("E1ColumnBreak"), TEXT("DemoE1ColumnBreak"), 0, 710000,
			FVector(-414.0, -162.0, 0.0), FVector(-90.0, 162.0, 648.0),
			FVector2D(450.0, 198.0),
			10276011350224018878ull, 10113758205408230493ull,
			6524532268529485689ull, 12560907909080588493ull,
			FVector(-1102.0, -850.0, -670.0), FVector(418.0, 670.0, 850.0), true),
		MakeJuryFixture(
			TEXT("E2DropTrigger"), TEXT("DemoE2DropTrigger"), 1, 740000,
			FVector(-774.0, -450.0, 0.0), FVector(486.0, 450.0, 1476.0),
			FVector2D(810.0, 486.0),
			1243337162086650128ull, 1108134973396587699ull,
			3864694895529971157ull, 1033929311817437759ull,
			FVector(-1462.0, -1138.0, -670.0), FVector(58.0, 382.0, 850.0), true),
		MakeJuryFixture(
			TEXT("E3SlideRelease"), TEXT("DemoE3SlideRelease"), 2, 750137,
			FVector(-1026.0, -414.0, 0.0), FVector(1026.0, 414.0, 1332.0),
			FVector2D(1062.0, 450.0),
			3075258440093988143ull, 17683520519518435068ull,
			15118401498293854757ull, 6073774060920401162ull,
			FVector(-1134.0, -522.0, -252.0), FVector(-774.0, -162.0, 468.0), true),
		MakeJuryFixture(
			TEXT("E4TipOver"), TEXT("DemoE4TipOver"), 3, 730000,
			FVector(-846.0, -378.0, 0.0), FVector(846.0, 378.0, 2376.0),
			FVector2D(882.0, 414.0),
			4328116049969586954ull, 11089610541129920709ull,
			3596567542130940914ull, 3035395675580472088ull,
			FVector(-378.0, -486.0, -144.0), FVector(342.0, -126.0, 216.0), true),
		MakeJuryFixture(
			TEXT("E5SeamRelease"), TEXT("DemoE5SeamRelease"), 4, 720000,
			FVector(-1350.0, -630.0, 0.0), FVector(1350.0, 630.0, 2376.0),
			FVector2D(1386.0, 666.0),
			461929562625370845ull, 7322844578368466709ull,
			12062404675177644267ull, 9042370151666144586ull,
			FVector(-1566.0, 126.0, -144.0), FVector(-846.0, 486.0, 216.0), true),
		MakeJuryFixture(
			TEXT("E6TipOver"), TEXT("DemoE6TipOver"), 5, 750000,
			FVector(-1062.0, -486.0, 0.0), FVector(1062.0, 486.0, 3384.0),
			FVector2D(1098.0, 522.0),
			6610608065286482828ull, 3963542007450344969ull,
			10510335516369342439ull, 1309116746468502251ull,
			FVector(-1278.0, -594.0, -144.0), FVector(-558.0, -234.0, 216.0), true)
	};
	return Fixtures;
}

const FABTSM3PocketContract* FindJuryPocket(
	const FABTSM3MonthlySpatialCandidate& Candidate,
	const int32 PocketId)
{
	return Candidate.Pockets.FindByPredicate(
		[PocketId](const FABTSM3PocketContract& Pocket)
		{
			return Pocket.PocketId == PocketId;
		});
}

int32 FindJuryNearestCell(
	const TArray<FABTSM2Cell>& Cells,
	const FVector& UnitDirection)
{
	int32 BestCellId = INDEX_NONE;
	double BestDot = -2.0;
	for (int32 CellId = 0; CellId < Cells.Num(); ++CellId)
	{
		const double Dot = FVector::DotProduct(
			Cells[CellId].UnitCenter,
			UnitDirection);
		if (Dot > BestDot)
		{
			BestDot = Dot;
			BestCellId = CellId;
		}
	}
	return BestCellId;
}

bool IsJuryPlacementFrameValid(
	const FVector& Forward,
	const FVector& Right,
	const FVector& Up)
{
	return Forward.IsNormalized()
		&& Right.IsNormalized()
		&& Up.IsNormalized()
		&& FMath::Abs(FVector::DotProduct(Forward, Right)) <= AxisTolerance
		&& FMath::Abs(FVector::DotProduct(Forward, Up)) <= AxisTolerance
		&& FMath::Abs(FVector::DotProduct(Right, Up)) <= AxisTolerance
		&& FVector::DotProduct(FVector::CrossProduct(Forward, Right), Up)
			>= 1.0 - AxisTolerance;
}

bool ValidateJuryPadReservation(
	const TArray<FABTSM2Cell>& Cells,
	const FABTSM3MonthlySpatialCandidate& Candidate,
	const FVector& WorldLocationCM,
	const FVector& Forward,
	const FVector& Right,
	const FVector2D& PadHalfExtentCM,
	TArray<int32>& OutReservedPadCellIds,
	FString& OutFailure)
{
	OutReservedPadCellIds.Reset();
	OutFailure.Reset();
	if (Candidate.Cells.Num() != Cells.Num())
	{
		OutFailure = TEXT("CandidateCellCount");
		return false;
	}

	for (int32 XIndex = -1; XIndex <= 1; ++XIndex)
	{
		for (int32 YIndex = -1; YIndex <= 1; ++YIndex)
		{
			const FVector SampleDirection = (
				WorldLocationCM
				+ Forward * (PadHalfExtentCM.X * XIndex)
				+ Right * (PadHalfExtentCM.Y * YIndex)).GetSafeNormal();
			const int32 CellId = FindJuryNearestCell(Cells, SampleDirection);
			if (!Candidate.Cells.IsValidIndex(CellId))
			{
				OutFailure = FString::Printf(
					TEXT("InvalidCell:%d:%d"), XIndex, YIndex);
				OutReservedPadCellIds.Reset();
				return false;
			}
			if (Candidate.Cells[CellId].bWater)
			{
				OutFailure = FString::Printf(TEXT("WaterCell:%d"), CellId);
				OutReservedPadCellIds.Reset();
				return false;
			}
			if (Candidate.RecomputedRoute.OrderedRoadCellIds.Contains(CellId))
			{
				OutFailure = FString::Printf(TEXT("RoadCell:%d"), CellId);
				OutReservedPadCellIds.Reset();
				return false;
			}
			OutReservedPadCellIds.AddUnique(CellId);
		}
	}
	OutReservedPadCellIds.Sort();
	return !OutReservedPadCellIds.IsEmpty();
}

bool ValidateJuryDynamicEnvelopeReservation(
	const TArray<FABTSM2Cell>& Cells,
	const FABTSM3MonthlySpatialCandidate& Candidate,
	const FVector& WorldLocationCM,
	const FVector& Forward,
	const FVector& Right,
	const FBox& EffectBounds,
	TArray<int32>& OutReservedCellIds,
	FString& OutFailure)
{
	OutReservedCellIds.Reset();
	OutFailure.Reset();
	if (Candidate.Cells.Num() != Cells.Num() || EffectBounds.IsValid == 0)
	{
		OutFailure = TEXT("CandidateCellCountOrEffectBounds");
		return false;
	}

	const double XSamples[] = {
		EffectBounds.Min.X,
		(EffectBounds.Min.X + EffectBounds.Max.X) * 0.5,
		EffectBounds.Max.X
	};
	const double YSamples[] = {
		EffectBounds.Min.Y,
		(EffectBounds.Min.Y + EffectBounds.Max.Y) * 0.5,
		EffectBounds.Max.Y
	};
	for (int32 XIndex = 0; XIndex < UE_ARRAY_COUNT(XSamples); ++XIndex)
	{
		for (int32 YIndex = 0; YIndex < UE_ARRAY_COUNT(YSamples); ++YIndex)
		{
			const FVector SampleDirection = (
				WorldLocationCM
					+ Forward * XSamples[XIndex]
					+ Right * YSamples[YIndex]).GetSafeNormal();
			const int32 CellId = FindJuryNearestCell(Cells, SampleDirection);
			if (!Candidate.Cells.IsValidIndex(CellId))
			{
				OutFailure = FString::Printf(
					TEXT("InvalidCell:%d:%d"), XIndex, YIndex);
				OutReservedCellIds.Reset();
				return false;
			}
			if (Candidate.Cells[CellId].bWater)
			{
				OutFailure = FString::Printf(TEXT("WaterCell:%d"), CellId);
				OutReservedCellIds.Reset();
				return false;
			}
			if (Candidate.RecomputedRoute.OrderedRoadCellIds.Contains(CellId))
			{
				OutFailure = FString::Printf(TEXT("RoadCell:%d"), CellId);
				OutReservedCellIds.Reset();
				return false;
			}
			OutReservedCellIds.AddUnique(CellId);
		}
	}
	OutReservedCellIds.Sort();
	return !OutReservedCellIds.IsEmpty();
}

double GetJuryHorizontalEnvelopeRadiusCM(const FBox& Bounds)
{
	if (Bounds.IsValid == 0)
	{
		return 0.0;
	}
	return FMath::Max(
		FMath::Max(
			FVector2D(Bounds.Min.X, Bounds.Min.Y).Size(),
			FVector2D(Bounds.Min.X, Bounds.Max.Y).Size()),
		FMath::Max(
			FVector2D(Bounds.Max.X, Bounds.Min.Y).Size(),
			FVector2D(Bounds.Max.X, Bounds.Max.Y).Size()));
}

void SetJuryRejected(
	FABTSM3JuryFixedSixLayoutResult& Result,
	const EABTSM3JuryFixedSixRejectReason Reason)
{
	Result.RejectReason = Reason;
	Result.bPlacementReady = false;
	Result.LayoutHash = static_cast<int64>(
		FABTSM3JuryFixedSixLayoutBuilder::ComputeLayoutHash(Result));
}
}

TConstArrayView<FABTSM3JuryBuildingPlacementFixture>
FABTSM3JuryFixedSixLayoutBuilder::GetFrozenPlacementFixtures()
{
	return MakeArrayView(ABTSM3JuryFixedSixPrivate::GetJuryFixtures());
}

uint64 FABTSM3JuryFixedSixLayoutBuilder::ComputeFixtureCatalogHash()
{
	ABTSM3JuryFixedSixPrivate::FJuryCanonicalHash Hash;
	Hash.AddInt32(SchemaVersion);
	Hash.AddInt32(M7PlacementSchemaVersion);
	Hash.AddInt32(M7SourceManifestVersion);
	Hash.AddInt64(M7SourceManifestHash);
	Hash.AddInt64(static_cast<int64>(M7PlacementCatalogHash));
	Hash.AddInt32(FixedSixContractVersion);
	const TConstArrayView<FABTSM3JuryBuildingPlacementFixture> Fixtures =
		GetFrozenPlacementFixtures();
	Hash.AddInt32(Fixtures.Num());
	for (const FABTSM3JuryBuildingPlacementFixture& Fixture : Fixtures)
	{
		Hash.AddName(Fixture.ManifestEntryId);
		Hash.AddName(Fixture.StableId);
		Hash.AddInt32(Fixture.DifficultyTier);
		Hash.AddInt32(Fixture.BuildingSeed);
		Hash.AddBoxCM(Fixture.LocalBounds);
		Hash.AddBoxCM(Fixture.PhysicalBounds);
		Hash.AddBoxCM(Fixture.EffectBounds);
		Hash.AddVector2DCM(Fixture.RequiredPadHalfExtentCM);
		Hash.AddInt64(Fixture.StaticGeometryHash);
		Hash.AddInt64(Fixture.ProductionIdentityHash);
		Hash.AddInt64(Fixture.DeviceAssemblyHash);
		Hash.AddInt64(Fixture.SourceDescriptorHash);
		Hash.AddInt32(Fixture.bDynamicEnvelopeRequired ? 1 : 0);
	}
	return Hash.Get();
}

uint64 FABTSM3JuryFixedSixLayoutBuilder::ComputePlacementHash(
	const FABTSM3JuryBuildingPlacement& Placement)
{
	ABTSM3JuryFixedSixPrivate::FJuryCanonicalHash Hash;
	Hash.AddInt32(SchemaVersion);
	Hash.AddInt32(Placement.EncounterIndex);
	Hash.AddName(Placement.ManifestEntryId);
	Hash.AddName(Placement.StableId);
	Hash.AddInt32(Placement.DifficultyTier);
	Hash.AddInt32(Placement.BuildingSeed);
	Hash.AddInt32(Placement.TargetAnchorCellId);
	Hash.AddInt32(Placement.PadCenterCellId);
	Hash.AddInt32(Placement.SlingshotAnchorCellId);
	Hash.AddVectorCM(Placement.WorldLocationCM);
	Hash.AddVectorCM(Placement.WorldForwardAxis);
	Hash.AddVectorCM(Placement.WorldRightAxis);
	Hash.AddVectorCM(Placement.WorldUpAxis);
	Hash.AddVector2DCM(Placement.RequiredPadHalfExtentCM);
	Hash.AddIntArray(Placement.ReservedPadCellIds);
	Hash.AddIntArray(Placement.ReservedDynamicEnvelopeCellIds);
	Hash.AddBoxCM(Placement.PhysicalBounds);
	Hash.AddBoxCM(Placement.EffectBounds);
	Hash.AddInt64(Placement.StaticGeometryHash);
	Hash.AddInt64(Placement.ProductionIdentityHash);
	Hash.AddInt64(Placement.DeviceAssemblyHash);
	Hash.AddInt64(Placement.SourceDescriptorHash);
	Hash.AddInt32(Placement.bDynamicEnvelopeRequired ? 1 : 0);
	return Hash.Get();
}

uint64 FABTSM3JuryFixedSixLayoutBuilder::ComputeLayoutHash(
	const FABTSM3JuryFixedSixLayoutResult& Result)
{
	ABTSM3JuryFixedSixPrivate::FJuryCanonicalHash Hash;
	Hash.AddInt32(Result.SchemaVersion);
	Hash.AddInt32(Result.FixedSixContractVersion);
	Hash.AddInt32(Result.WorldSeed);
	Hash.AddInt32(Result.SourceCandidateId);
	Hash.AddInt64(Result.SourceSpatialResultHash);
	Hash.AddInt64(Result.SourceSpatialCandidateHash);
	Hash.AddInt32(Result.M7PlacementSchemaVersion);
	Hash.AddInt32(Result.M7SourceManifestVersion);
	Hash.AddInt64(Result.M7SourceManifestHash);
	Hash.AddInt64(Result.M7PlacementCatalogHash);
	Hash.AddInt32(Result.Placements.Num());
	for (const FABTSM3JuryBuildingPlacement& Placement : Result.Placements)
	{
		Hash.AddInt64(Placement.PlacementHash);
	}
	Hash.AddInt32(static_cast<int32>(Result.RejectReason));
	Hash.AddInt32(Result.bPlacementReady ? 1 : 0);
	return Hash.Get();
}

bool FABTSM3JuryFixedSixLayoutBuilder::Build(
	const TArray<FABTSM2Cell>& Cells,
	const float PlanetRadiusCM,
	const FABTSM3MonthlySpatialResult& SourceSpatialResult,
	FABTSM3JuryFixedSixLayoutResult& OutResult,
	FString& OutFailure)
{
	using namespace ABTSM3JuryFixedSixPrivate;
	OutResult = FABTSM3JuryFixedSixLayoutResult();
	OutFailure.Reset();
	OutResult.SchemaVersion = SchemaVersion;
	OutResult.FixedSixContractVersion = FixedSixContractVersion;
	OutResult.WorldSeed = SourceSpatialResult.WorldSeed;
	OutResult.SourceCandidateId = FrozenSourceCandidateId;
	OutResult.SourceSpatialResultHash = SourceSpatialResult.SpatialResultHash;
	OutResult.M7PlacementSchemaVersion = M7PlacementSchemaVersion;
	OutResult.M7SourceManifestVersion = M7SourceManifestVersion;
	OutResult.M7SourceManifestHash = M7SourceManifestHash;
	OutResult.M7PlacementCatalogHash =
		static_cast<int64>(M7PlacementCatalogHash);

	if (Cells.IsEmpty()
		|| !FMath::IsFinite(PlanetRadiusCM)
		|| PlanetRadiusCM <= 0.0f)
	{
		OutFailure = TEXT("InvalidCellsOrPlanetRadius");
		SetJuryRejected(OutResult,
			EABTSM3JuryFixedSixRejectReason::InvalidInput);
		return false;
	}
	if (!SourceSpatialResult.bSpatialResultValid
		|| SourceSpatialResult.WorldSeed != FrozenWorldSeed
		|| static_cast<uint64>(SourceSpatialResult.SpatialResultHash)
			!= FrozenSourceSpatialResultHash)
	{
		OutFailure = TEXT("FrozenSpatialResultIdentity");
		SetJuryRejected(OutResult,
			EABTSM3JuryFixedSixRejectReason::SourceIdentityMismatch);
		return false;
	}

	const FABTSM3MonthlySpatialCandidate* Candidate =
		SourceSpatialResult.RetainedCandidates.FindByPredicate(
			[](const FABTSM3MonthlySpatialCandidate& Value)
			{
				return Value.SourceRouteCandidateId == FrozenSourceCandidateId;
			});
	if (Candidate == nullptr
		|| !Candidate->bHardPass
		|| Candidate->RejectReason != EABTSM3MonthlySpatialRejectReason::None
		|| static_cast<uint64>(Candidate->SpatialCandidateHash)
			!= FrozenSourceSpatialCandidateHash
		|| Candidate->Encounters.Num() != ExpectedEncounterCount)
	{
		OutFailure = TEXT("FrozenSpatialCandidateIdentity");
		SetJuryRejected(OutResult,
			EABTSM3JuryFixedSixRejectReason::SourceIdentityMismatch);
		return false;
	}
	OutResult.SourceSpatialCandidateHash = Candidate->SpatialCandidateHash;

	const TConstArrayView<FABTSM3JuryBuildingPlacementFixture> Fixtures =
		GetFrozenPlacementFixtures();
	if (Fixtures.Num() != ExpectedEncounterCount
		|| ComputeFixtureCatalogHash() == 0)
	{
		OutFailure = TEXT("FrozenPlacementFixtureCatalog");
		SetJuryRejected(OutResult,
			EABTSM3JuryFixedSixRejectReason::FrozenCatalogMismatch);
		return false;
	}

	OutResult.Placements.Reserve(ExpectedEncounterCount);
	for (int32 Index = 0; Index < ExpectedEncounterCount; ++Index)
	{
		const FABTSM3MonthlySpatialEncounter& Encounter =
			Candidate->Encounters[Index];
		const FABTSM3JuryBuildingPlacementFixture& Fixture = Fixtures[Index];
		const FABTSM3PocketContract* SlingshotPocket = FindJuryPocket(
			*Candidate,
			Encounter.Contract.SlingshotPocketId);
		if (!Cells.IsValidIndex(Encounter.TargetAnchorCellId)
			|| SlingshotPocket == nullptr
			|| SlingshotPocket->Role != EABTSM3PocketRole::Slingshot
			|| !Cells.IsValidIndex(SlingshotPocket->AnchorCellId)
			|| Fixture.DifficultyTier != Index
			|| Fixture.ManifestEntryId.IsNone()
			|| Fixture.StableId.IsNone()
			|| Fixture.StaticGeometryHash == 0
			|| Fixture.SourceDescriptorHash == 0
			|| Fixture.ProductionIdentityHash == 0
			|| Fixture.DeviceAssemblyHash == 0
			|| Fixture.LocalBounds.IsValid == 0
			|| Fixture.PhysicalBounds.IsValid == 0
			|| Fixture.EffectBounds.IsValid == 0
			|| !Fixture.PhysicalBounds.Min.Equals(Fixture.LocalBounds.Min)
			|| !Fixture.PhysicalBounds.Max.Equals(Fixture.LocalBounds.Max)
			|| Fixture.RequiredPadHalfExtentCM.X <= 0.0
			|| Fixture.RequiredPadHalfExtentCM.Y <= 0.0
			|| Fixture.RequiredPadHalfExtentCM.X
				< FMath::Max(FMath::Abs(Fixture.PhysicalBounds.Min.X),
					FMath::Abs(Fixture.PhysicalBounds.Max.X)) + 36.0
			|| Fixture.RequiredPadHalfExtentCM.Y
				< FMath::Max(FMath::Abs(Fixture.PhysicalBounds.Min.Y),
					FMath::Abs(Fixture.PhysicalBounds.Max.Y)) + 36.0
			|| !Fixture.bDynamicEnvelopeRequired)
		{
			OutFailure = FString::Printf(TEXT("EncounterIdentity:%d"), Index);
			SetJuryRejected(OutResult,
				EABTSM3JuryFixedSixRejectReason::EncounterIdentityMismatch);
			return false;
		}

		FABTSM3JuryBuildingPlacement Placement;
		Placement.EncounterIndex = Index;
		Placement.ManifestEntryId = Fixture.ManifestEntryId;
		Placement.StableId = Fixture.StableId;
		Placement.DifficultyTier = Fixture.DifficultyTier;
		Placement.BuildingSeed = Fixture.BuildingSeed;
		Placement.TargetAnchorCellId = Encounter.TargetAnchorCellId;
		Placement.SlingshotAnchorCellId = SlingshotPocket->AnchorCellId;
		Placement.RequiredPadHalfExtentCM = Fixture.RequiredPadHalfExtentCM;
		Placement.PhysicalBounds = Fixture.PhysicalBounds;
		Placement.EffectBounds = Fixture.EffectBounds;
		Placement.StaticGeometryHash = Fixture.StaticGeometryHash;
		Placement.ProductionIdentityHash = Fixture.ProductionIdentityHash;
		Placement.DeviceAssemblyHash = Fixture.DeviceAssemblyHash;
		Placement.SourceDescriptorHash = Fixture.SourceDescriptorHash;
		Placement.bDynamicEnvelopeRequired = Fixture.bDynamicEnvelopeRequired;

		TArray<int32> PadCenterCellIds;
		PadCenterCellIds.Add(Encounter.TargetAnchorCellId);
		for (const int32 CellId : Encounter.TargetNoRoadCellIds)
		{
			PadCenterCellIds.AddUnique(CellId);
		}
		for (const int32 CellId : Encounter.TargetFootprintCellIds)
		{
			PadCenterCellIds.AddUnique(CellId);
		}
		const FVector TargetAnchorDirection =
			Cells[Encounter.TargetAnchorCellId].UnitCenter;
		PadCenterCellIds.Sort(
			[&Cells, &TargetAnchorDirection](const int32 First, const int32 Second)
			{
				if (!Cells.IsValidIndex(First))
				{
					return false;
				}
				if (!Cells.IsValidIndex(Second))
				{
					return true;
				}
				const double FirstDot = FVector::DotProduct(
					Cells[First].UnitCenter, TargetAnchorDirection);
				const double SecondDot = FVector::DotProduct(
					Cells[Second].UnitCenter, TargetAnchorDirection);
				return FirstDot != SecondDot ? FirstDot > SecondDot : First < Second;
			});

		FString PadReservationFailure;
		FString DynamicEnvelopeFailure;
		bool bBuiltPlacementFrame = false;
		bool bResolvedStaticPad = false;
		bool bResolvedPlacementClearance = false;
		for (const int32 PadCenterCellId : PadCenterCellIds)
		{
			if (!Cells.IsValidIndex(PadCenterCellId)
				|| !Candidate->Cells.IsValidIndex(PadCenterCellId))
			{
				PadReservationFailure = FString::Printf(
					TEXT("InvalidCenterCell:%d"), PadCenterCellId);
				continue;
			}

			const FVector Up = Cells[PadCenterCellId].UnitCenter.GetSafeNormal();
			FVector Forward = FVector::VectorPlaneProject(
				Cells[SlingshotPocket->AnchorCellId].UnitCenter,
				Up).GetSafeNormal();
			if (Forward.IsNearlyZero())
			{
				Forward = FVector::VectorPlaneProject(
					FVector::ForwardVector,
					Up).GetSafeNormal();
			}
			if (Forward.IsNearlyZero())
			{
				Forward = FVector::VectorPlaneProject(
					FVector::RightVector,
					Up).GetSafeNormal();
			}
			const FVector Right = FVector::CrossProduct(
				Up, Forward).GetSafeNormal();
			Forward = FVector::CrossProduct(Right, Up).GetSafeNormal();
			if (!IsJuryPlacementFrameValid(Forward, Right, Up))
			{
				continue;
			}
			bBuiltPlacementFrame = true;
			if (Candidate->Cells[PadCenterCellId].bWater)
			{
				PadReservationFailure = FString::Printf(
					TEXT("WaterCenterCell:%d"), PadCenterCellId);
				continue;
			}
			if (Candidate->RecomputedRoute.OrderedRoadCellIds.Contains(
					PadCenterCellId))
			{
				PadReservationFailure = FString::Printf(
					TEXT("RoadCenterCell:%d"), PadCenterCellId);
				continue;
			}

			Placement.PadCenterCellId = PadCenterCellId;
			Placement.WorldLocationCM = Up * PlanetRadiusCM;
			Placement.WorldForwardAxis = Forward;
			Placement.WorldRightAxis = Right;
			Placement.WorldUpAxis = Up;
			if (ValidateJuryPadReservation(
					Cells,
					*Candidate,
					Placement.WorldLocationCM,
					Forward,
					Right,
					Placement.RequiredPadHalfExtentCM,
					Placement.ReservedPadCellIds,
					PadReservationFailure))
			{
				bResolvedStaticPad = true;
				if (ValidateJuryDynamicEnvelopeReservation(
						Cells,
						*Candidate,
						Placement.WorldLocationCM,
						Forward,
						Right,
						Placement.EffectBounds,
						Placement.ReservedDynamicEnvelopeCellIds,
						DynamicEnvelopeFailure))
				{
					bResolvedPlacementClearance = true;
					break;
				}
			}
		}
		if (!bBuiltPlacementFrame)
		{
			OutFailure = FString::Printf(TEXT("PlacementFrame:%d"), Index);
			SetJuryRejected(OutResult,
				EABTSM3JuryFixedSixRejectReason::PlacementFrameInvalid);
			return false;
		}
		if (!bResolvedPlacementClearance)
		{
			if (bResolvedStaticPad)
			{
				OutFailure = FString::Printf(
					TEXT("DynamicEnvelopeReservation:%d:%s:Centers=%d"),
					Index,
					*DynamicEnvelopeFailure,
					PadCenterCellIds.Num());
				SetJuryRejected(OutResult,
					EABTSM3JuryFixedSixRejectReason::DynamicEnvelopeReservationFailed);
			}
			else
			{
				OutFailure = FString::Printf(
					TEXT("PadReservation:%d:%s:Centers=%d"),
					Index,
					*PadReservationFailure,
					PadCenterCellIds.Num());
				SetJuryRejected(OutResult,
					EABTSM3JuryFixedSixRejectReason::PadReservationFailed);
			}
			return false;
		}
		Placement.PlacementHash = static_cast<int64>(
			ComputePlacementHash(Placement));
		OutResult.Placements.Add(Placement);
	}

	for (int32 A = 0; A < OutResult.Placements.Num(); ++A)
	{
		for (int32 B = A + 1; B < OutResult.Placements.Num(); ++B)
		{
			const FABTSM3JuryBuildingPlacement& First = OutResult.Placements[A];
			const FABTSM3JuryBuildingPlacement& Second = OutResult.Placements[B];
			const double AngularDistance = FMath::Acos(FMath::Clamp(
				FVector::DotProduct(First.WorldUpAxis, Second.WorldUpAxis),
				-1.0,
				1.0));
			const double SurfaceDistanceCM = AngularDistance * PlanetRadiusCM;
			const double FirstRadiusCM = FMath::Max(
				First.RequiredPadHalfExtentCM.Size(),
				GetJuryHorizontalEnvelopeRadiusCM(First.EffectBounds));
			const double SecondRadiusCM = FMath::Max(
				Second.RequiredPadHalfExtentCM.Size(),
				GetJuryHorizontalEnvelopeRadiusCM(Second.EffectBounds));
			const double RequiredDistanceCM =
				FirstRadiusCM
					+ SecondRadiusCM
					+ PadSeparationMarginCM;
			if (SurfaceDistanceCM <= RequiredDistanceCM)
			{
				OutFailure = FString::Printf(
					TEXT("DynamicEnvelopeSeparation:%d:%d"), A, B);
				SetJuryRejected(OutResult,
					EABTSM3JuryFixedSixRejectReason::DynamicEnvelopeSeparationFailed);
				return false;
			}
		}
	}

	OutResult.RejectReason = EABTSM3JuryFixedSixRejectReason::None;
	OutResult.bPlacementReady = true;
	OutResult.LayoutHash = static_cast<int64>(ComputeLayoutHash(OutResult));
	if (OutResult.LayoutHash == 0
		|| static_cast<uint64>(OutResult.LayoutHash) != ComputeLayoutHash(OutResult))
	{
		OutFailure = TEXT("LayoutHash");
		SetJuryRejected(OutResult,
			EABTSM3JuryFixedSixRejectReason::HashMismatch);
		return false;
	}
	return true;
}

const TCHAR* FABTSM3JuryFixedSixLayoutBuilder::GetRejectReasonName(
	const EABTSM3JuryFixedSixRejectReason Reason)
{
	switch (Reason)
	{
	case EABTSM3JuryFixedSixRejectReason::None:
		return TEXT("None");
	case EABTSM3JuryFixedSixRejectReason::NotEvaluated:
		return TEXT("NotEvaluated");
	case EABTSM3JuryFixedSixRejectReason::InvalidInput:
		return TEXT("InvalidInput");
	case EABTSM3JuryFixedSixRejectReason::SourceIdentityMismatch:
		return TEXT("SourceIdentityMismatch");
	case EABTSM3JuryFixedSixRejectReason::FrozenCatalogMismatch:
		return TEXT("FrozenCatalogMismatch");
	case EABTSM3JuryFixedSixRejectReason::EncounterIdentityMismatch:
		return TEXT("EncounterIdentityMismatch");
	case EABTSM3JuryFixedSixRejectReason::PlacementFrameInvalid:
		return TEXT("PlacementFrameInvalid");
	case EABTSM3JuryFixedSixRejectReason::PadReservationFailed:
		return TEXT("PadReservationFailed");
	case EABTSM3JuryFixedSixRejectReason::PadSeparationFailed:
		return TEXT("PadSeparationFailed");
	case EABTSM3JuryFixedSixRejectReason::DynamicEnvelopeReservationFailed:
		return TEXT("DynamicEnvelopeReservationFailed");
	case EABTSM3JuryFixedSixRejectReason::DynamicEnvelopeSeparationFailed:
		return TEXT("DynamicEnvelopeSeparationFailed");
	case EABTSM3JuryFixedSixRejectReason::HashMismatch:
		return TEXT("HashMismatch");
	default:
		return TEXT("Unknown");
	}
}
