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
	const uint64 SourceDescriptorHash)
{
	FABTSM3JuryBuildingPlacementFixture Fixture;
	Fixture.ManifestEntryId = FName(ManifestEntryId);
	Fixture.StableId = FName(StableId);
	Fixture.DifficultyTier = DifficultyTier;
	Fixture.BuildingSeed = BuildingSeed;
	Fixture.LocalBounds = FBox(BoundsMin, BoundsMax);
	Fixture.RequiredPadHalfExtentCM = PadHalfExtentCM;
	Fixture.StaticGeometryHash = static_cast<int64>(StaticGeometryHash);
	Fixture.SourceDescriptorHash = static_cast<int64>(SourceDescriptorHash);
	return Fixture;
}

const TArray<FABTSM3JuryBuildingPlacementFixture>& GetJuryFixtures()
{
	static const TArray<FABTSM3JuryBuildingPlacementFixture> Fixtures = {
		MakeJuryFixture(
			TEXT("E1ColumnBreak"), TEXT("DemoE1ColumnBreak"), 0, 710000,
			FVector(-414.0, -162.0, 0.0), FVector(-90.0, 162.0, 648.0),
			FVector2D(450.0, 198.0),
			16780849829317489644ull, 14931273032555350531ull),
		MakeJuryFixture(
			TEXT("E2DropTrigger"), TEXT("DemoE2DropTrigger"), 1, 740000,
			FVector(-774.0, -450.0, 0.0), FVector(486.0, 450.0, 1476.0),
			FVector2D(810.0, 486.0),
			2343934176722587840ull, 17636075314117899824ull),
		MakeJuryFixture(
			TEXT("E3SlideRelease"), TEXT("DemoE3SlideRelease"), 2, 750137,
			FVector(-1026.0, -414.0, 0.0), FVector(1026.0, 414.0, 1332.0),
			FVector2D(1062.0, 450.0),
			4060368085179305333ull, 3277746625945437825ull),
		MakeJuryFixture(
			TEXT("E4TipOver"), TEXT("DemoE4TipOver"), 3, 730000,
			FVector(-846.0, -378.0, 0.0), FVector(846.0, 378.0, 2376.0),
			FVector2D(882.0, 414.0),
			3905124247026714506ull, 5284820191875006966ull),
		MakeJuryFixture(
			TEXT("E5SeamRelease"), TEXT("DemoE5SeamRelease"), 4, 720000,
			FVector(-1350.0, -630.0, 0.0), FVector(1350.0, 630.0, 2376.0),
			FVector2D(1386.0, 666.0),
			10244968675392635774ull, 15983895412278031603ull),
		MakeJuryFixture(
			TEXT("E6TipOver"), TEXT("DemoE6TipOver"), 5, 750000,
			FVector(-1062.0, -486.0, 0.0), FVector(1062.0, 486.0, 3384.0),
			FVector2D(1098.0, 522.0),
			10028734189939141390ull, 9843082278464018151ull)
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
	const FABTSM3MonthlySpatialEncounter& Encounter,
	const FVector& WorldLocationCM,
	const FVector& Forward,
	const FVector& Right,
	const FVector2D& PadHalfExtentCM)
{
	if (Candidate.Cells.Num() != Cells.Num()
		|| Encounter.TargetNoRoadCellIds.IsEmpty())
	{
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
			if (!Candidate.Cells.IsValidIndex(CellId)
				|| !Encounter.TargetNoRoadCellIds.Contains(CellId)
				|| !Candidate.Cells[CellId].bNoRoad
				|| Candidate.Cells[CellId].bWater)
			{
				return false;
			}
		}
	}
	return true;
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
	const TConstArrayView<FABTSM3JuryBuildingPlacementFixture> Fixtures =
		GetFrozenPlacementFixtures();
	Hash.AddInt32(Fixtures.Num());
	for (const FABTSM3JuryBuildingPlacementFixture& Fixture : Fixtures)
	{
		Hash.AddName(Fixture.ManifestEntryId);
		Hash.AddName(Fixture.StableId);
		Hash.AddInt32(Fixture.DifficultyTier);
		Hash.AddInt32(Fixture.BuildingSeed);
		Hash.AddVectorCM(Fixture.LocalBounds.Min);
		Hash.AddVectorCM(Fixture.LocalBounds.Max);
		Hash.AddVector2DCM(Fixture.RequiredPadHalfExtentCM);
		Hash.AddInt64(Fixture.StaticGeometryHash);
		Hash.AddInt64(Fixture.SourceDescriptorHash);
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
	Hash.AddInt32(Placement.SlingshotAnchorCellId);
	Hash.AddVectorCM(Placement.WorldLocationCM);
	Hash.AddVectorCM(Placement.WorldForwardAxis);
	Hash.AddVectorCM(Placement.WorldRightAxis);
	Hash.AddVectorCM(Placement.WorldUpAxis);
	Hash.AddVector2DCM(Placement.RequiredPadHalfExtentCM);
	Hash.AddInt64(Placement.SourceDescriptorHash);
	return Hash.Get();
}

uint64 FABTSM3JuryFixedSixLayoutBuilder::ComputeLayoutHash(
	const FABTSM3JuryFixedSixLayoutResult& Result)
{
	ABTSM3JuryFixedSixPrivate::FJuryCanonicalHash Hash;
	Hash.AddInt32(Result.SchemaVersion);
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
			|| Fixture.SourceDescriptorHash == 0
			|| !Fixture.LocalBounds.IsValid
			|| Fixture.RequiredPadHalfExtentCM.X <= 0.0
			|| Fixture.RequiredPadHalfExtentCM.Y <= 0.0)
		{
			OutFailure = FString::Printf(TEXT("EncounterIdentity:%d"), Index);
			SetJuryRejected(OutResult,
				EABTSM3JuryFixedSixRejectReason::EncounterIdentityMismatch);
			return false;
		}

		const FVector Up = Cells[Encounter.TargetAnchorCellId]
			.UnitCenter.GetSafeNormal();
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
		const FVector Right = FVector::CrossProduct(Up, Forward).GetSafeNormal();
		Forward = FVector::CrossProduct(Right, Up).GetSafeNormal();
		if (!IsJuryPlacementFrameValid(Forward, Right, Up))
		{
			OutFailure = FString::Printf(TEXT("PlacementFrame:%d"), Index);
			SetJuryRejected(OutResult,
				EABTSM3JuryFixedSixRejectReason::PlacementFrameInvalid);
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
		Placement.WorldLocationCM = Up * PlanetRadiusCM;
		Placement.WorldForwardAxis = Forward;
		Placement.WorldRightAxis = Right;
		Placement.WorldUpAxis = Up;
		Placement.RequiredPadHalfExtentCM = Fixture.RequiredPadHalfExtentCM;
		Placement.SourceDescriptorHash = Fixture.SourceDescriptorHash;
		if (!ValidateJuryPadReservation(
				Cells,
				*Candidate,
				Encounter,
				Placement.WorldLocationCM,
				Forward,
				Right,
				Placement.RequiredPadHalfExtentCM))
		{
			OutFailure = FString::Printf(TEXT("PadReservation:%d"), Index);
			SetJuryRejected(OutResult,
				EABTSM3JuryFixedSixRejectReason::PadReservationFailed);
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
			const double RequiredDistanceCM =
				First.RequiredPadHalfExtentCM.Size()
				+ Second.RequiredPadHalfExtentCM.Size()
				+ PadSeparationMarginCM;
			if (SurfaceDistanceCM <= RequiredDistanceCM)
			{
				OutFailure = FString::Printf(TEXT("PadSeparation:%d:%d"), A, B);
				SetJuryRejected(OutResult,
					EABTSM3JuryFixedSixRejectReason::PadSeparationFailed);
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
	case EABTSM3JuryFixedSixRejectReason::HashMismatch:
		return TEXT("HashMismatch");
	default:
		return TEXT("Unknown");
	}
}
