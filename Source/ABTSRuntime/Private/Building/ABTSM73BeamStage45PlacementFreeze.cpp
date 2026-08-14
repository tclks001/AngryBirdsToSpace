// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73BeamStage45PlacementFreeze.h"

#include "ABTSM73BeamC3V3SkeletonFirstTypes.h"
#include "ABTSM73BeamD1BrickCompiler.h"
#include "Building/ABTSM73BeamDemoManifest.h"

namespace ABTSM73BeamStage45PlacementFreezePrivate
{
	constexpr double BlockUnitsCM = 36.0;
	constexpr double HalfBlockCM = BlockUnitsCM * 0.5;
	constexpr double GeometryToleranceCM = 0.01;
	constexpr uint64 FnvOffsetBasis = 14695981039346656037ull;
	constexpr uint64 FnvPrime = 1099511628211ull;

	uint64 HashUtf8(const FString& Text)
	{
		FTCHARToUTF8 Utf8(*Text);
		uint64 Hash = FnvOffsetBasis;
		for (int32 Index = 0; Index < Utf8.Length(); ++Index)
		{
			Hash ^= static_cast<uint8>(Utf8.Get()[Index]);
			Hash *= FnvPrime;
		}
		return Hash;
	}

	int64 QuantizeCM(const double Value)
	{
		return FMath::RoundToInt64(Value * 100.0);
	}

	FBox MemberBounds(const ABTSM73BeamC3V3::FPlannedMember& Member)
	{
		FVector Minimum(
			FMath::Min(Member.LocalStart.X, Member.LocalEnd.X),
			FMath::Min(Member.LocalStart.Y, Member.LocalEnd.Y),
			FMath::Min(Member.LocalStart.Z, Member.LocalEnd.Z));
		FVector Maximum(
			FMath::Max(Member.LocalStart.X, Member.LocalEnd.X),
			FMath::Max(Member.LocalStart.Y, Member.LocalEnd.Y),
			FMath::Max(Member.LocalStart.Z, Member.LocalEnd.Z));
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (Axis == static_cast<int32>(Member.Axis))
			{
				continue;
			}
			const double Center =
				(Member.LocalStart[Axis] + Member.LocalEnd[Axis]) * 0.5;
			Minimum[Axis] = Center - HalfBlockCM;
			Maximum[Axis] = Center + HalfBlockCM;
		}
		return FBox(Minimum, Maximum);
	}

	bool HasPositiveVolumeOverlap(const FBox& A, const FBox& B)
	{
		return FMath::Min(A.Max.X, B.Max.X) - FMath::Max(A.Min.X, B.Min.X)
			> GeometryToleranceCM
			&& FMath::Min(A.Max.Y, B.Max.Y) - FMath::Max(A.Min.Y, B.Min.Y)
				> GeometryToleranceCM
			&& FMath::Min(A.Max.Z, B.Max.Z) - FMath::Max(A.Min.Z, B.Min.Z)
				> GeometryToleranceCM;
	}

	FString GeometryRow(const FBox& Bounds)
	{
		return FString::Printf(TEXT("%lld,%lld,%lld,%lld,%lld,%lld"),
			QuantizeCM(Bounds.Min.X), QuantizeCM(Bounds.Min.Y),
			QuantizeCM(Bounds.Min.Z), QuantizeCM(Bounds.Max.X),
			QuantizeCM(Bounds.Max.Y), QuantizeCM(Bounds.Max.Z));
	}

	FString StructureRow(
		const ABTSM73BeamC3V3::FPlannedMember& Member,
		const FBox& Bounds)
	{
		return FString::Printf(TEXT("%s,%d,%d,%d"), *GeometryRow(Bounds),
			static_cast<int32>(Member.Axis), static_cast<int32>(Member.Role),
			Member.bRequiresGroundSeat ? 1 : 0);
	}

	uint64 HashRows(TArray<FString>& Rows)
	{
		Rows.Sort();
		return HashUtf8(FString::Join(Rows, TEXT("|")));
	}

	uint64 CalculateDescriptorHash(
		const FABTSM73BeamStage45PlacementDescriptor& Descriptor)
	{
		const FBox& B = Descriptor.LocalBounds;
		return HashUtf8(FString::Printf(
			TEXT("Schema=%d|Manifest=%d:%lld|Entry=%d:%s:%s:%d:%d")
			TEXT("|Inputs=%lld:%lld:%lld:%lld|Plan=%lld|Structure=%llu|Geometry=%llu|Members=%d")
			TEXT("|Bounds=%lld,%lld,%lld,%lld,%lld,%lld")
			TEXT("|Footprint=%lld,%lld,%lld,%lld|Pivot=%lld,%lld,%lld|Ground=%lld|Offset=%lld")
			TEXT("|Axes=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld")
			TEXT("|Pad=%lld,%lld,%lld"),
			Descriptor.SchemaVersion, Descriptor.SourceManifestVersion,
			Descriptor.SourceManifestHash,
			static_cast<int32>(Descriptor.ManifestEntryId),
			*Descriptor.StableId.ToString(), *Descriptor.GameplayProfileId.ToString(),
			Descriptor.DifficultyTier, Descriptor.BuildingSeed,
			Descriptor.ProfileCatalogHash, Descriptor.ResolvedSettingsHash,
			Descriptor.GrammarHash, Descriptor.WFCHash, Descriptor.Stage4PlanHash,
			Descriptor.StaticStructureHash, Descriptor.StaticGeometryHash,
			Descriptor.ActiveMemberCount,
			QuantizeCM(B.Min.X), QuantizeCM(B.Min.Y), QuantizeCM(B.Min.Z),
			QuantizeCM(B.Max.X), QuantizeCM(B.Max.Y), QuantizeCM(B.Max.Z),
			QuantizeCM(Descriptor.FootprintMinCM.X),
			QuantizeCM(Descriptor.FootprintMinCM.Y),
			QuantizeCM(Descriptor.FootprintMaxCM.X),
			QuantizeCM(Descriptor.FootprintMaxCM.Y),
			QuantizeCM(Descriptor.PlacementPivotLocalCM.X),
			QuantizeCM(Descriptor.PlacementPivotLocalCM.Y),
			QuantizeCM(Descriptor.PlacementPivotLocalCM.Z),
			QuantizeCM(Descriptor.GroundPlaneZCM),
			QuantizeCM(Descriptor.PivotToGroundOffsetCM),
			QuantizeCM(Descriptor.LocalForwardAxis.X),
			QuantizeCM(Descriptor.LocalForwardAxis.Y),
			QuantizeCM(Descriptor.LocalForwardAxis.Z),
			QuantizeCM(Descriptor.LocalRightAxis.X),
			QuantizeCM(Descriptor.LocalRightAxis.Y),
			QuantizeCM(Descriptor.LocalRightAxis.Z),
			QuantizeCM(Descriptor.LocalUpAxis.X),
			QuantizeCM(Descriptor.LocalUpAxis.Y),
			QuantizeCM(Descriptor.LocalUpAxis.Z),
			QuantizeCM(Descriptor.RequiredPadHalfExtentCM.X),
			QuantizeCM(Descriptor.RequiredPadHalfExtentCM.Y),
			QuantizeCM(Descriptor.PadSafetyMarginCM)));
	}

	uint64 CalculateCatalogHash(
		const TArray<FABTSM73BeamStage45PlacementDescriptor>& Descriptors)
	{
		FString Canonical = FString::Printf(TEXT("Schema=%d|Manifest=%d:%lld|Count=%d"),
			FABTSM73BeamStage45PlacementFreeze::SchemaVersion,
			FABTSM73BeamDemoManifest::Version,
			FABTSM73BeamDemoManifest::CalculateHash(), Descriptors.Num());
		for (const FABTSM73BeamStage45PlacementDescriptor& Descriptor : Descriptors)
		{
			Canonical += FString::Printf(TEXT("|%d:%llu"),
				static_cast<int32>(Descriptor.ManifestEntryId), Descriptor.DescriptorHash);
		}
		return HashUtf8(Canonical);
	}

	FABTSM73BeamStage45PlacementDescriptor MakeFrozenDescriptor(
		const EABTSM73BeamDemoBuilding Id,
		const TCHAR* StableId,
		const TCHAR* ProfileId,
		const int32 Tier,
		const int32 Seed,
		const int64 ProfileCatalogHash,
		const int64 ResolvedSettingsHash,
		const int64 GrammarHash,
		const int64 WFCHash,
		const int64 Stage4PlanHash,
		const uint64 StructureHash,
		const uint64 GeometryHash,
		const int32 MemberCount,
		const FVector& BoundsMin,
		const FVector& BoundsMax,
		const FVector2D& PadHalfExtent,
		const uint64 DescriptorHash)
	{
		FABTSM73BeamStage45PlacementDescriptor Descriptor;
		Descriptor.SchemaVersion = FABTSM73BeamStage45PlacementFreeze::SchemaVersion;
		Descriptor.SourceManifestVersion =
			FABTSM73BeamStage45PlacementFreeze::FrozenSourceManifestVersion;
		Descriptor.SourceManifestHash =
			FABTSM73BeamStage45PlacementFreeze::FrozenSourceManifestHash;
		Descriptor.ManifestEntryId = Id;
		Descriptor.StableId = FName(StableId);
		Descriptor.GameplayProfileId = FName(ProfileId);
		Descriptor.DifficultyTier = Tier;
		Descriptor.BuildingSeed = Seed;
		Descriptor.ProfileCatalogHash = ProfileCatalogHash;
		Descriptor.ResolvedSettingsHash = ResolvedSettingsHash;
		Descriptor.GrammarHash = GrammarHash;
		Descriptor.WFCHash = WFCHash;
		Descriptor.Stage4PlanHash = Stage4PlanHash;
		Descriptor.StaticStructureHash = StructureHash;
		Descriptor.StaticGeometryHash = GeometryHash;
		Descriptor.ActiveMemberCount = MemberCount;
		Descriptor.LocalBounds = FBox(BoundsMin, BoundsMax);
		Descriptor.FootprintMinCM = FVector2D(BoundsMin.X, BoundsMin.Y);
		Descriptor.FootprintMaxCM = FVector2D(BoundsMax.X, BoundsMax.Y);
		Descriptor.PlacementPivotLocalCM = FVector::ZeroVector;
		Descriptor.GroundPlaneZCM = 0.0;
		Descriptor.PivotToGroundOffsetCM = 0.0;
		Descriptor.LocalForwardAxis = FVector::ForwardVector;
		Descriptor.LocalRightAxis = FVector::RightVector;
		Descriptor.LocalUpAxis = FVector::UpVector;
		Descriptor.RequiredPadHalfExtentCM = PadHalfExtent;
		Descriptor.PadSafetyMarginCM =
			FABTSM73BeamStage45PlacementFreeze::PadSafetyMarginCM;
		Descriptor.DescriptorHash = DescriptorHash;
		return Descriptor;
	}
}

const TArray<FABTSM73BeamStage45PlacementDescriptor>&
FABTSM73BeamStage45PlacementFreeze::GetFrozenDescriptors()
{
	using namespace ABTSM73BeamStage45PlacementFreezePrivate;
	static const TArray<FABTSM73BeamStage45PlacementDescriptor> Descriptors = {
		MakeFrozenDescriptor(EABTSM73BeamDemoBuilding::E1ColumnBreak,
			TEXT("DemoE1ColumnBreak"), TEXT("ColumnBreak"), 0, 710000,
			3702642162, 152542140, 3426745518, 1704479784,
			676922634961156217, 13006815641234581905ull,
			16780849829317489644ull, 52,
			FVector(-414.0, -162.0, 0.0), FVector(-90.0, 162.0, 648.0),
			FVector2D(450.0, 198.0), 14931273032555350531ull),
		MakeFrozenDescriptor(EABTSM73BeamDemoBuilding::E2DropTrigger,
			TEXT("DemoE2DropTrigger"), TEXT("DropTrigger"), 1, 740000,
			3702642162, 1103660032, 3978079831, 4287206679,
			6460664919201723082, 16652842673640082205ull,
			6593742478964351284ull, 238,
			FVector(-774.0, -450.0, 0.0), FVector(486.0, 450.0, 1476.0),
			FVector2D(810.0, 486.0), 6469289411973045234ull),
		MakeFrozenDescriptor(EABTSM73BeamDemoBuilding::E3SlideRelease,
			TEXT("DemoE3SlideRelease"), TEXT("SlideRelease"), 2, 750137,
			3702642162, 206239081, 2648477740, 3650167482,
			-7698616134605135490ll, 11992358704930630999ull,
			7875615895702053486ull, 373,
			FVector(-1026.0, -414.0, 0.0), FVector(1026.0, 414.0, 1332.0),
			FVector2D(1062.0, 450.0), 12006310113592064441ull),
		MakeFrozenDescriptor(EABTSM73BeamDemoBuilding::E4TipOver,
			TEXT("DemoE4TipOver"), TEXT("TipOver"), 3, 730000,
			3702642162, 4146166430, 4271539635, 639241170,
			-6201759884010229846ll, 7815516912831699761ull,
			14743414898489745685ull, 890,
			FVector(-846.0, -378.0, 0.0), FVector(846.0, 378.0, 2376.0),
			FVector2D(882.0, 414.0), 350362875744463079ull),
		MakeFrozenDescriptor(EABTSM73BeamDemoBuilding::E5SeamRelease,
			TEXT("DemoE5SeamRelease"), TEXT("SeamRelease"), 4, 720000,
			3702642162, 4041112202, 491350570, 1151036245,
			3461569929356418258, 15296823002088367770ull,
			3565081434077166339ull, 1910,
			FVector(-1350.0, -630.0, 0.0), FVector(1350.0, 630.0, 2376.0),
			FVector2D(1386.0, 666.0), 4982536737720910812ull),
		MakeFrozenDescriptor(EABTSM73BeamDemoBuilding::E6TipOver,
			TEXT("DemoE6TipOver"), TEXT("TipOver"), 5, 750000,
			3702642162, 46309185, 3561724892, 664390472,
			-2628650226020954654ll, 3322080867896505573ull,
			15282817313174714820ull, 2354,
			FVector(-1062.0, -486.0, 0.0), FVector(1062.0, 486.0, 3384.0),
			FVector2D(1098.0, 522.0), 7226425218995051565ull)};
	return Descriptors;
}

bool FABTSM73BeamStage45PlacementFreeze::ResolveFrozen(
	const EABTSM73BeamDemoBuilding Id,
	FABTSM73BeamStage45PlacementDescriptor& OutDescriptor,
	FString& OutError)
{
	OutDescriptor = FABTSM73BeamStage45PlacementDescriptor();
	OutError.Reset();
	for (const FABTSM73BeamStage45PlacementDescriptor& Descriptor
		: GetFrozenDescriptors())
	{
		if (Descriptor.ManifestEntryId == Id)
		{
			OutDescriptor = Descriptor;
			return true;
		}
	}
	OutError = FString::Printf(TEXT("Stage45FrozenDescriptorMissing:Id=%d"),
		static_cast<int32>(Id));
	return false;
}

uint64 FABTSM73BeamStage45PlacementFreeze::CalculateFrozenCatalogHash()
{
	return ABTSM73BeamStage45PlacementFreezePrivate::CalculateCatalogHash(
		GetFrozenDescriptors());
}

bool FABTSM73BeamStage45PlacementFreeze::DeriveAndValidate(
	const EABTSM73BeamDemoBuilding Id,
	FABTSM73BeamStage45PlacementDescriptor& OutDescriptor,
	FString& OutError)
{
	using namespace ABTSM73BeamStage45PlacementFreezePrivate;
	OutDescriptor = FABTSM73BeamStage45PlacementDescriptor();
	OutError.Reset();

	FABTSM73BeamDemoManifestEntry Entry;
	if (!FABTSM73BeamDemoManifest::Resolve(Id, Entry, OutError))
	{
		return false;
	}
	FABTSM73BeamD1StagePreviewResult Result;
	if (!FABTSM73BeamD1BrickCompiler().GenerateStagePreview(
		Entry.Settings, EABTSM73BeamC3GenerationStage::FloorInfillRoof,
		Result, OutError))
	{
		OutError = FString::Printf(TEXT("Stage45Stage4GenerationRejected:%s:%s"),
			*Entry.StableId.ToString(), *OutError);
		return false;
	}

	const ABTSM73BeamC3V3::FPlan& Plan = Result.Skeleton.Plan;
	if (!Plan.Summary.bAccepted || Plan.Summary.FinalGeometryHash == 0
		|| Plan.Summary.Stage4IntentHash == 0
		|| Plan.Summary.Stage4TopFrameHash == 0
		|| Plan.Summary.Stage4FacadeToTopHash == 0
		|| Plan.Summary.Stage4FloorStyleInfillHash == 0
		|| Plan.Summary.Stage4RoofCrownHash == 0
		|| Plan.Summary.Stage4UnresolvedIntentCount != 0
		|| Plan.Summary.Stage4IntentBindingViolationCount != 0
		|| Plan.Summary.Stage4TopFrameBindingViolationCount != 0
		|| Plan.Summary.Stage4TopFrameConflictCount != 0
		|| Plan.Summary.Stage4FacadeToTopBindingViolationCount != 0
		|| Plan.Summary.Stage4FacadeToTopConflictCount != 0
		|| Plan.Summary.Stage4FloorBindingViolationCount != 0
		|| Plan.Summary.Stage4FloorConflictCount != 0
		|| Plan.Summary.Stage4UnsupportedRoofMemberCount != 0
		|| Plan.Summary.Stage4RoofBindingViolationCount != 0
		|| Plan.Summary.Stage4RoofConflictCount != 0)
	{
		OutError = FString::Printf(
			TEXT("Stage45Stage4NotFrozen:%s:Accepted=%d:Geometry=%lld:Unresolved=%d:RoofUnsupported=%d"),
			*Entry.StableId.ToString(), Plan.Summary.bAccepted ? 1 : 0,
			Plan.Summary.FinalGeometryHash,
			Plan.Summary.Stage4UnresolvedIntentCount,
			Plan.Summary.Stage4UnsupportedRoofMemberCount);
		return false;
	}

	double GroundPlaneZCM = 0.0;
	bool bHasGroundPlane = false;
	for (const ABTSM73BeamC3V3::FComponentPlan& Component : Plan.Components)
	{
		if (!bHasGroundPlane)
		{
			GroundPlaneZCM = Component.GroundPlaneZCM;
			bHasGroundPlane = true;
		}
		else if (!FMath::IsNearlyEqual(
			GroundPlaneZCM, Component.GroundPlaneZCM, GeometryToleranceCM))
		{
			OutError = FString::Printf(TEXT("Stage45MultipleGroundPlanes:%s"),
				*Entry.StableId.ToString());
			return false;
		}
	}
	if (!bHasGroundPlane
		|| !FMath::IsNearlyZero(GroundPlaneZCM, GeometryToleranceCM))
	{
		OutError = FString::Printf(TEXT("Stage45GeneratorOriginIsNotGround:%s:Ground=%.3f"),
			*Entry.StableId.ToString(), GroundPlaneZCM);
		return false;
	}

	FBox ActiveBounds(EForceInit::ForceInit);
	TArray<FBox> ActiveMemberBounds;
	TArray<FString> GeometryRows;
	TArray<FString> StructureRows;
	int32 GroundSeatCount = 0;
	for (const ABTSM73BeamC3V3::FPlannedMember& Member : Plan.Members)
	{
		if (Member.bSuppressedByStage4FacadeToTop)
		{
			continue;
		}
		const FBox Bounds = MemberBounds(Member);
		if (!Bounds.IsValid || Bounds.Min.ContainsNaN() || Bounds.Max.ContainsNaN()
			|| Bounds.Min.Z < GroundPlaneZCM - GeometryToleranceCM)
		{
			OutError = FString::Printf(TEXT("Stage45InvalidOrBelowGroundMember:%s:Bounds=%s"),
				*Entry.StableId.ToString(), *Bounds.ToString());
			return false;
		}
		if (Member.bRequiresGroundSeat)
		{
			if (!FMath::IsNearlyEqual(
				Bounds.Min.Z, GroundPlaneZCM, GeometryToleranceCM))
			{
				OutError = FString::Printf(TEXT("Stage45GroundSeatMismatch:%s:Bounds=%s"),
					*Entry.StableId.ToString(), *Bounds.ToString());
				return false;
			}
			++GroundSeatCount;
		}
		ActiveBounds += Bounds;
		ActiveMemberBounds.Add(Bounds);
		GeometryRows.Add(GeometryRow(Bounds));
		StructureRows.Add(StructureRow(Member, Bounds));
	}
	if (!ActiveBounds.IsValid || ActiveMemberBounds.IsEmpty() || GroundSeatCount == 0
		|| !FMath::IsNearlyEqual(
			ActiveBounds.Min.Z, GroundPlaneZCM, GeometryToleranceCM))
	{
		OutError = FString::Printf(TEXT("Stage45BuildingDoesNotLand:%s:Members=%d:Seats=%d:Bounds=%s"),
			*Entry.StableId.ToString(), ActiveMemberBounds.Num(), GroundSeatCount,
			*ActiveBounds.ToString());
		return false;
	}
	for (int32 A = 0; A < ActiveMemberBounds.Num(); ++A)
	{
		for (int32 B = A + 1; B < ActiveMemberBounds.Num(); ++B)
		{
			if (HasPositiveVolumeOverlap(ActiveMemberBounds[A], ActiveMemberBounds[B]))
			{
				OutError = FString::Printf(
					TEXT("Stage45PositiveVolumeOverlap:%s:A=%d:%s:B=%d:%s"),
					*Entry.StableId.ToString(), A, *ActiveMemberBounds[A].ToString(),
					B, *ActiveMemberBounds[B].ToString());
				return false;
			}
		}
	}

	OutDescriptor.SchemaVersion = SchemaVersion;
	OutDescriptor.SourceManifestVersion = FABTSM73BeamDemoManifest::Version;
	OutDescriptor.SourceManifestHash = FABTSM73BeamDemoManifest::CalculateHash();
	OutDescriptor.ManifestEntryId = Entry.Id;
	OutDescriptor.StableId = Entry.StableId;
	OutDescriptor.GameplayProfileId = Entry.Settings.GameplayProfileId;
	OutDescriptor.DifficultyTier = Entry.Settings.DifficultyTier;
	OutDescriptor.BuildingSeed = Entry.Settings.BuildingSeed;
	OutDescriptor.ProfileCatalogHash = Plan.ProfileCatalogHash;
	OutDescriptor.ResolvedSettingsHash = Plan.ResolvedSettingsHash;
	OutDescriptor.GrammarHash = Plan.GrammarHash;
	OutDescriptor.WFCHash = Plan.WFCHash;
	OutDescriptor.Stage4PlanHash = Plan.Summary.FinalGeometryHash;
	OutDescriptor.StaticStructureHash = HashRows(StructureRows);
	OutDescriptor.StaticGeometryHash = HashRows(GeometryRows);
	OutDescriptor.ActiveMemberCount = ActiveMemberBounds.Num();
	OutDescriptor.LocalBounds = ActiveBounds;
	OutDescriptor.FootprintMinCM = FVector2D(ActiveBounds.Min.X, ActiveBounds.Min.Y);
	OutDescriptor.FootprintMaxCM = FVector2D(ActiveBounds.Max.X, ActiveBounds.Max.Y);
	OutDescriptor.PlacementPivotLocalCM = FVector::ZeroVector;
	OutDescriptor.GroundPlaneZCM = GroundPlaneZCM;
	OutDescriptor.PivotToGroundOffsetCM =
		GroundPlaneZCM - OutDescriptor.PlacementPivotLocalCM.Z;
	OutDescriptor.LocalForwardAxis = FVector::ForwardVector;
	OutDescriptor.LocalRightAxis = FVector::RightVector;
	OutDescriptor.LocalUpAxis = FVector::UpVector;
	OutDescriptor.PadSafetyMarginCM = PadSafetyMarginCM;
	OutDescriptor.RequiredPadHalfExtentCM = FVector2D(
		FMath::Max(FMath::Abs(ActiveBounds.Min.X), FMath::Abs(ActiveBounds.Max.X))
			+ PadSafetyMarginCM,
		FMath::Max(FMath::Abs(ActiveBounds.Min.Y), FMath::Abs(ActiveBounds.Max.Y))
			+ PadSafetyMarginCM);
	OutDescriptor.DescriptorHash = CalculateDescriptorHash(OutDescriptor);
	return true;
}

bool FABTSM73BeamStage45PlacementFreeze::DeriveAndValidateCatalog(
	TArray<FABTSM73BeamStage45PlacementDescriptor>& OutDescriptors,
	uint64& OutCatalogHash,
	FString& OutError)
{
	using namespace ABTSM73BeamStage45PlacementFreezePrivate;
	OutDescriptors.Reset();
	OutCatalogHash = 0;
	OutError.Reset();
	const TArray<FABTSM73BeamDemoManifestEntry>& Entries =
		FABTSM73BeamDemoManifest::GetEntries();
	if (Entries.Num() != ExpectedEntryCount)
	{
		OutError = FString::Printf(TEXT("Stage45ManifestEntryCountMismatch:Actual=%d:Expected=%d"),
			Entries.Num(), ExpectedEntryCount);
		return false;
	}
	TSet<EABTSM73BeamDemoBuilding> UniqueIds;
	TSet<FName> UniqueStableIds;
	for (const FABTSM73BeamDemoManifestEntry& Entry : Entries)
	{
		if (Entry.Id == EABTSM73BeamDemoBuilding::Custom
			|| UniqueIds.Contains(Entry.Id) || UniqueStableIds.Contains(Entry.StableId))
		{
			OutError = FString::Printf(TEXT("Stage45ManifestIdentityNotUnique:Id=%d:StableId=%s"),
				static_cast<int32>(Entry.Id), *Entry.StableId.ToString());
			return false;
		}
		UniqueIds.Add(Entry.Id);
		UniqueStableIds.Add(Entry.StableId);
		FABTSM73BeamStage45PlacementDescriptor Descriptor;
		if (!DeriveAndValidate(Entry.Id, Descriptor, OutError))
		{
			OutDescriptors.Reset();
			return false;
		}
		OutDescriptors.Add(Descriptor);
	}
	OutCatalogHash = CalculateCatalogHash(OutDescriptors);
	return true;
}
