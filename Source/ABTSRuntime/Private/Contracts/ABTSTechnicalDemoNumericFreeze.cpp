// Copyright Epic Games, Inc. All Rights Reserved.

#include "Contracts/ABTSTechnicalDemoNumericFreeze.h"

#include "Building/ABTSM73BeamStage45PlacementFreeze.h"
#include "Calibration/ABTSSlingshotSatelliteCalibrationTypes.h"
#include "Contracts/ABTSWorldGenerationContracts.h"
#include "Crafting/ABTSCraftingCatalog.h"
#include "UObject/UObjectGlobals.h"
#include "World/ABTSM11FinaleLayoutTypes.h"

namespace ABTSTechnicalDemoNumericFreezePrivate
{
	constexpr uint64 FnvOffsetBasis64 = 14695981039346656037ull;
	constexpr uint64 FnvPrime64 = 1099511628211ull;

	struct FHashBuilder
	{
		uint64 Value = FnvOffsetBasis64;

		void AddUInt64(const uint64 Input)
		{
			for (int32 ByteIndex = 0; ByteIndex < 8; ++ByteIndex)
			{
				Value ^= (Input >> (ByteIndex * 8)) & 0xffull;
				Value *= FnvPrime64;
			}
		}

		void AddString(const FString& Input)
		{
			const FTCHARToUTF8 Utf8(*Input);
			AddUInt64(static_cast<uint64>(Utf8.Length()));
			for (int32 ByteIndex = 0; ByteIndex < Utf8.Length(); ++ByteIndex)
			{
				Value ^= static_cast<uint8>(Utf8.Get()[ByteIndex]);
				Value *= FnvPrime64;
			}
		}
	};

	struct FFrozenM7Entry
	{
		const TCHAR* StableId;
		uint64 DescriptorHash;
		int32 ActiveMemberCount;
	};

	constexpr FFrozenM7Entry FrozenM7Entries[] =
	{
		{TEXT("DemoE1ColumnBreak"), 10113758205408230493ull, 52},
		{TEXT("DemoE2DropTrigger"), 1108134973396587699ull, 235},
		{TEXT("DemoE3SlideRelease"), 17683520519518435068ull, 364},
		{TEXT("DemoE4TipOver"), 11089610541129920709ull, 872},
		{TEXT("DemoE5SeamRelease"), 7322844578368466709ull, 1807},
		{TEXT("DemoE6TipOver"), 3963542007450344969ull, 2174}
	};

	bool Reject(FString* OutFailure, const FString& Reason)
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = Reason;
		}
		return false;
	}

	bool Reject(FString* OutFailure, const TCHAR* Reason)
	{
		return Reject(OutFailure, FString(Reason));
	}
}

TConstArrayView<FABTSFrozenRecipeTopologyEntry>
FABTSTechnicalDemoNumericFreeze::GetFrozenRecipeTopologyV1()
{
	static const TArray<FABTSFrozenRecipeTopologyEntry> Entries =
	{
		{FName(TEXT("BridgeKit")), EABTSItemId::BridgeKit, 1,
			EABTSCraftingStationType::Workbench},
		{FName(TEXT("FurnaceKit")), EABTSItemId::FurnaceKit, 1,
			EABTSCraftingStationType::Workbench},
		{FName(TEXT("ReinforcedCord")), EABTSItemId::ReinforcedCord, 1,
			EABTSCraftingStationType::Furnace},
		{FName(TEXT("ReinforcedStake")), EABTSItemId::ReinforcedStake, 1,
			EABTSCraftingStationType::Furnace},
		{FName(TEXT("SimpleCord")), EABTSItemId::SimpleCord, 1,
			EABTSCraftingStationType::Workbench},
		{FName(TEXT("SimpleStake")), EABTSItemId::SimpleStake, 1,
			EABTSCraftingStationType::Workbench},
		{FName(TEXT("SpaceCord")), EABTSItemId::SpaceCord, 1,
			EABTSCraftingStationType::Furnace},
		{FName(TEXT("SpaceStakePair")), EABTSItemId::SpaceStake, 2,
			EABTSCraftingStationType::Furnace},
		{FName(TEXT("WorkbenchKit")), EABTSItemId::WorkbenchKit, 1,
			EABTSCraftingStationType::None}
	};
	return Entries;
}

uint64 FABTSTechnicalDemoNumericFreeze::ComputeRecipeTopologyHash(
	const TConstArrayView<FABTSFrozenRecipeTopologyEntry> Entries)
{
	using namespace ABTSTechnicalDemoNumericFreezePrivate;
	TArray<FABTSFrozenRecipeTopologyEntry> Ordered;
	Ordered.Append(Entries.GetData(), Entries.Num());
	Ordered.Sort([](
		const FABTSFrozenRecipeTopologyEntry& A,
		const FABTSFrozenRecipeTopologyEntry& B)
	{
		return A.RecipeId.ToString() < B.RecipeId.ToString();
	});

	FHashBuilder Hash;
	Hash.AddUInt64(FrozenRecipeTopologyVersion);
	Hash.AddUInt64(static_cast<uint64>(Ordered.Num()));
	for (const FABTSFrozenRecipeTopologyEntry& Entry : Ordered)
	{
		Hash.AddString(Entry.RecipeId.ToString());
		Hash.AddUInt64(static_cast<uint8>(Entry.OutputItem));
		Hash.AddUInt64(static_cast<uint64>(Entry.OutputQuantity));
		Hash.AddUInt64(static_cast<uint8>(Entry.RequiredStation));
	}
	return Hash.Value;
}

bool FABTSTechnicalDemoNumericFreeze::ValidateRecipeTopologyV1(
	const TConstArrayView<FABTSCraftingRecipe> Recipes,
	FString* OutFailure)
{
	using namespace ABTSTechnicalDemoNumericFreezePrivate;
	const TConstArrayView<FABTSFrozenRecipeTopologyEntry> Expected =
		GetFrozenRecipeTopologyV1();
	if (Recipes.Num() != Expected.Num())
	{
		return Reject(OutFailure, FString::Printf(
			TEXT("RecipeTopologyCountMismatch:Expected=%d:Actual=%d"),
			Expected.Num(), Recipes.Num()));
	}

	TArray<FABTSFrozenRecipeTopologyEntry> Actual;
	Actual.Reserve(Recipes.Num());
	TSet<FName> SeenRecipeIds;
	for (const FABTSCraftingRecipe& Recipe : Recipes)
	{
		if (Recipe.RecipeId.IsNone() || SeenRecipeIds.Contains(Recipe.RecipeId))
		{
			return Reject(OutFailure, FString::Printf(
				TEXT("RecipeTopologyDuplicateOrEmptyId:%s"),
				*Recipe.RecipeId.ToString()));
		}
		SeenRecipeIds.Add(Recipe.RecipeId);
		Actual.Add({Recipe.RecipeId, Recipe.OutputItem, Recipe.OutputQuantity,
			Recipe.RequiredStation});
	}

	const uint64 ActualHash = ComputeRecipeTopologyHash(Actual);
	if (ActualHash != FrozenRecipeTopologyHash)
	{
		return Reject(OutFailure, FString::Printf(
			TEXT("RecipeTopologyHashMismatch:Expected=%016llX:Actual=%016llX"),
			static_cast<unsigned long long>(FrozenRecipeTopologyHash),
			static_cast<unsigned long long>(ActualHash)));
	}

	if (OutFailure != nullptr)
	{
		OutFailure->Reset();
	}
	return true;
}

uint64 FABTSTechnicalDemoNumericFreeze::ComputeManifestHash()
{
	using namespace ABTSTechnicalDemoNumericFreezePrivate;
	FHashBuilder Hash;
	Hash.AddUInt64(ManifestVersion);
	Hash.AddUInt64(LaunchCurveFormulaVersion);
	Hash.AddUInt64(CraftingTransactionFormulaVersion);
	Hash.AddUInt64(ResourceDemandFormulaVersion);
	Hash.AddUInt64(ImpactDamageFormulaVersion);

	Hash.AddUInt64(FrozenLaunchCatalogVersion);
	Hash.AddUInt64(FrozenLaunchProfileHash);

	Hash.AddUInt64(FrozenFixedSixContractVersion);
	Hash.AddUInt64(FrozenFixedSixPlacementSchemaVersion);
	Hash.AddUInt64(FrozenFixedSixDemoManifestVersion);
	Hash.AddUInt64(FrozenFixedSixDemoManifestHash);
	Hash.AddUInt64(FrozenFixedSixPlacementCatalogHash);
	Hash.AddUInt64(FrozenFixedSixWorldSeed);
	Hash.AddUInt64(FrozenFixedSixCandidateId);
	Hash.AddUInt64(FrozenFixedSixLayoutHash);

	Hash.AddUInt64(FrozenM7PlacementSchemaVersion);
	Hash.AddUInt64(FrozenM7PlacementCatalogHash);
	Hash.AddUInt64(UE_ARRAY_COUNT(FrozenM7Entries));
	for (const FFrozenM7Entry& Entry : FrozenM7Entries)
	{
		Hash.AddString(FString(Entry.StableId));
		Hash.AddUInt64(Entry.DescriptorHash);
		Hash.AddUInt64(Entry.ActiveMemberCount);
	}

	Hash.AddUInt64(FrozenM11PresetVersion);
	Hash.AddUInt64(FrozenM11PresetSourceHash);
	Hash.AddUInt64(FrozenM11PresetHash);
	Hash.AddUInt64(FrozenM11ScenarioHash);
	Hash.AddUInt64(FrozenM11ScanContractHash);
	Hash.AddUInt64(FrozenM11CertificationHash);
	Hash.AddUInt64(FrozenM11NominalTrajectoryHash);
	Hash.AddUInt64(FrozenM11PhysicalPlaybackContractVersion);
	Hash.AddUInt64(FrozenM11PhysicalPlaybackTrajectoryHash);
	Hash.AddUInt64(FrozenM11CertifiedBundleHash);

	Hash.AddUInt64(FrozenRecipeTopologyVersion);
	Hash.AddUInt64(FrozenRecipeTopologyHash);
	return Hash.Value;
}

bool FABTSTechnicalDemoNumericFreeze::ValidateCurrentProject(FString* OutFailure)
{
	using namespace ABTSTechnicalDemoNumericFreezePrivate;
	if (ComputeManifestHash() != FrozenManifestHash)
	{
		return Reject(OutFailure, TEXT("TechnicalDemoManifestHashMismatch"));
	}

	FABTSM6LaunchProfileCatalog LaunchCatalog;
	FString LaunchFailure;
	if (!FABTSSlingshotSatelliteCalibrationModel::ResolveCatalog(
		FABTSSlingshotSatelliteCalibrationModel::MakeFrozenLaunchProfileCatalogV0(),
		LaunchCatalog,
		&LaunchFailure)
		|| LaunchCatalog.Version != FrozenLaunchCatalogVersion
		|| FABTSSlingshotSatelliteCalibrationModel::ComputeLaunchProfileHash(
			LaunchCatalog) != FrozenLaunchProfileHash)
	{
		return Reject(OutFailure, FString::Printf(
			TEXT("LaunchProfileFreezeMismatch:%s"), *LaunchFailure));
	}

	if (FABTSJuryDemoFixedSixContract::SupportedV2ContractVersion
			!= FrozenFixedSixContractVersion
		|| FABTSJuryDemoFixedSixContract::FrozenPlacementSchemaVersion
			!= FrozenFixedSixPlacementSchemaVersion
		|| FABTSJuryDemoFixedSixContract::FrozenDemoManifestVersion
			!= FrozenFixedSixDemoManifestVersion
		|| FABTSJuryDemoFixedSixContract::FrozenDemoManifestHash
			!= FrozenFixedSixDemoManifestHash
		|| FABTSJuryDemoFixedSixContract::FrozenV2PlacementCatalogHash
			!= FrozenFixedSixPlacementCatalogHash
		|| FABTSJuryDemoFixedSixContract::FrozenWorldSeed
			!= FrozenFixedSixWorldSeed
		|| FABTSJuryDemoFixedSixContract::FrozenCandidateId
			!= FrozenFixedSixCandidateId
		|| FABTSJuryDemoFixedSixContract::FrozenV2LayoutHash
			!= FrozenFixedSixLayoutHash)
	{
		return Reject(OutFailure, TEXT("FixedSixV2FreezeMismatch"));
	}

	const TArray<FABTSM73BeamStage45PlacementDescriptor>& M7Descriptors =
		FABTSM73BeamStage45PlacementFreeze::GetFrozenDescriptors();
	if (FABTSM73BeamStage45PlacementFreeze::SchemaVersion
			!= FrozenM7PlacementSchemaVersion
		|| FABTSM73BeamStage45PlacementFreeze::FrozenCatalogHash
			!= FrozenM7PlacementCatalogHash
		|| FABTSM73BeamStage45PlacementFreeze::CalculateFrozenCatalogHash()
			!= FrozenM7PlacementCatalogHash
		|| M7Descriptors.Num() != UE_ARRAY_COUNT(FrozenM7Entries))
	{
		return Reject(OutFailure, TEXT("M7PlacementCatalogFreezeMismatch"));
	}
	int32 ActiveMemberCount = 0;
	for (int32 Index = 0; Index < M7Descriptors.Num(); ++Index)
	{
		const FABTSM73BeamStage45PlacementDescriptor& Actual = M7Descriptors[Index];
		const FFrozenM7Entry& Expected = FrozenM7Entries[Index];
		if (Actual.StableId != FName(Expected.StableId)
			|| Actual.DescriptorHash != Expected.DescriptorHash
			|| Actual.ActiveMemberCount != Expected.ActiveMemberCount)
		{
			return Reject(OutFailure, FString::Printf(
				TEXT("M7DescriptorFreezeMismatch:Index=%d:StableId=%s"),
				Index, *Actual.StableId.ToString()));
		}
		ActiveMemberCount += Actual.ActiveMemberCount;
	}
	if (ActiveMemberCount != FrozenM7ActiveMemberCount)
	{
		return Reject(OutFailure, TEXT("M7ActiveMemberCountMismatch"));
	}

	const FABTSM11FinaleLayoutPreset M11Preset =
		FABTSM11FinaleLayoutPreset::MakeCertifiedV1();
	FString M11Failure;
	if (!M11Preset.IsValid(&M11Failure)
		|| M11Preset.PresetVersion != FrozenM11PresetVersion
		|| M11Preset.PresetSourceHash != FrozenM11PresetSourceHash
		|| M11Preset.PresetHash != FrozenM11PresetHash
		|| M11Preset.CanonicalScenario.ScenarioHash != FrozenM11ScenarioHash
		|| M11Preset.ScanContractHash != FrozenM11ScanContractHash
		|| M11Preset.CertificationHash != FrozenM11CertificationHash
		|| M11Preset.NominalTrajectoryHash != FrozenM11NominalTrajectoryHash
		|| M11Preset.PhysicalPlaybackContractVersion
			!= FrozenM11PhysicalPlaybackContractVersion
		|| M11Preset.PhysicalPlaybackTrajectoryHash
			!= FrozenM11PhysicalPlaybackTrajectoryHash
		|| M11Preset.CertifiedBundleHash != FrozenM11CertifiedBundleHash)
	{
		return Reject(OutFailure, FString::Printf(
			TEXT("M11ProductionV1FreezeMismatch:%s"), *M11Failure));
	}

	const UABTSCraftingCatalog* CraftingCatalog =
		NewObject<UABTSCraftingCatalog>(GetTransientPackage());
	FString RecipeFailure;
	if (CraftingCatalog == nullptr
		|| !ValidateRecipeTopologyV1(
			CraftingCatalog->GetRecipes(), &RecipeFailure))
	{
		return Reject(OutFailure, FString::Printf(
			TEXT("CraftingTopologyFreezeMismatch:%s"), *RecipeFailure));
	}

	if (OutFailure != nullptr)
	{
		OutFailure->Reset();
	}
	return true;
}
