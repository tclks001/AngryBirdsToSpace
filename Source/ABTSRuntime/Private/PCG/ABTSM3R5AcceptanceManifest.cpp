// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCG/ABTSM3R5AcceptanceManifest.h"

#include "Containers/StringConv.h"
#include "PCG/ABTSM3MonthlyPresentation.h"
#include "PCG/ABTSM3R3AcceptanceManifest.h"

namespace ABTSM3R5ManifestPrivate
{
constexpr uint64 FnvOffset = 14695981039346656037ull;
constexpr uint64 FnvPrime = 1099511628211ull;

uint64 HashUtf8(const FString& Text)
{
	const FTCHARToUTF8 Utf8(*Text);
	uint64 Hash = FnvOffset;
	for (int32 Index = 0; Index < Utf8.Length(); ++Index)
	{
		Hash ^= static_cast<uint8>(Utf8.Get()[Index]);
		Hash *= FnvPrime;
	}
	return Hash;
}

const TArray<int32>& GetSweepSeedStorage()
{
	static const TArray<int32> Seeds = []
	{
		TArray<int32> Result;
		Result.Reserve(
			FABTSM3R5AcceptanceManifest::SweepSeedCount);
		Result.Add(
			FABTSM3R5AcceptanceManifest::DisplaySeed);
		for (int32 Seed = 0; Seed <= 98; ++Seed)
		{
			Result.Add(Seed);
		}
		return Result;
	}();
	return Seeds;
}

FString BuildSeedList(const TConstArrayView<int32> Seeds)
{
	FString Result;
	for (int32 Index = 0; Index < Seeds.Num(); ++Index)
	{
		if (Index > 0)
		{
			Result.AppendChar(TEXT(','));
		}
		Result.Append(FString::FromInt(Seeds[Index]));
	}
	return Result;
}

const FABTSM3R5AcceptanceEntry AcceptanceEntries[] = {
	{
		TEXT("Automation.BiomeCore"),
		EABTSM3R5AcceptanceLayer::Automation,
		TEXT("ABTS.M3.Monthly.Biome.0"),
		1,
		1
	},
	{
		TEXT("Automation.BiomeFailure"),
		EABTSM3R5AcceptanceLayer::Automation,
		TEXT("ABTS.M3.Monthly.BiomeFailure"),
		1,
		1
	},
	{
		TEXT("Automation.BiomeSweep100"),
		EABTSM3R5AcceptanceLayer::Automation,
		TEXT("ABTS.M3.Monthly.Biome.Sweep100"),
		1,
		1
	},
	{
		TEXT("Automation.EncounterSpatial"),
		EABTSM3R5AcceptanceLayer::Automation,
		TEXT("ABTS.M3.Monthly.EncounterSpatial.0"),
		8,
		1
	},
	{
		TEXT("Automation.EncounterSpatialFailure"),
		EABTSM3R5AcceptanceLayer::Automation,
		TEXT("ABTS.M3.Monthly.EncounterSpatialFailure"),
		2,
		1
	},
	{
		TEXT("Automation.RouteCore"),
		EABTSM3R5AcceptanceLayer::Automation,
		TEXT("ABTS.M3.Monthly.RouteCore"),
		7,
		1
	},
	{
		TEXT("Automation.RouteFailure"),
		EABTSM3R5AcceptanceLayer::Automation,
		TEXT("ABTS.M3.Monthly.RouteFailure"),
		1,
		1
	},
	{
		TEXT("Automation.MonthlySchema"),
		EABTSM3R5AcceptanceLayer::Automation,
		TEXT("ABTS.M3.Monthly.Schema"),
		8,
		1
	},
	{
		TEXT("Automation.WeekOne"),
		EABTSM3R5AcceptanceLayer::Automation,
		TEXT("ABTS.M3.WeekOne"),
		2,
		1
	},
	{
		TEXT("Automation.WorldGenerationContract"),
		EABTSM3R5AcceptanceLayer::Automation,
		TEXT("ABTS.Contracts.WorldGeneration"),
		2,
		1
	},
	{
		TEXT("Automation.M110FinaleSeparation"),
		EABTSM3R5AcceptanceLayer::Automation,
		TEXT("ABTS.M110.TaskGraphFinaleSeparation"),
		1,
		1
	},
	{
		TEXT("Runtime.LABTSM3"),
		EABTSM3R5AcceptanceLayer::FreshRuntime,
		TEXT("/Game/Maps/L_ABTS_M3?-ABTSM3R5Smoke?-ABTSM3R5Preview?-ABTSM3R5PreviewCandidate=4"),
		1,
		1
	},
	{
		TEXT("Integration.CharacterSweepVisibility"),
		EABTSM3R5AcceptanceLayer::IntegrationAutomation,
		TEXT("ABTS.M3.Monthly.R5CharacterSweepVisibility"),
		1,
		1
	},
	{
		TEXT("Integration.M6DynamicProxy"),
		EABTSM3R5AcceptanceLayer::IntegrationAutomation,
		TEXT("ABTS.M6.Monthly.R5DynamicProxy"),
		1,
		1
	},
	{
		TEXT("Integration.M9DeveloperTraversal"),
		EABTSM3R5AcceptanceLayer::IntegrationAutomation,
		TEXT("ABTS.M9.Monthly.R5DeveloperTraversal"),
		1,
		1
	},
	{
		TEXT("VisiblePIE.DisplaySeed"),
		EABTSM3R5AcceptanceLayer::VisiblePIE,
		TEXT("/Game/Maps/L_ABTS_M3?Seed=312503?-ABTSM3R5Preview?-ABTSM3R5PreviewCandidate=4?Lit+Unlit+Debug"),
		3,
		1
	}
};
}

TConstArrayView<int32>
FABTSM3R5AcceptanceManifest::GetSweepSeeds()
{
	return MakeArrayView(
		ABTSM3R5ManifestPrivate::GetSweepSeedStorage());
}

TConstArrayView<FABTSM3R5AcceptanceEntry>
FABTSM3R5AcceptanceManifest::GetEntries()
{
	return MakeArrayView(
		ABTSM3R5ManifestPrivate::AcceptanceEntries);
}

const TCHAR* FABTSM3R5AcceptanceManifest::GetLayerName(
	const EABTSM3R5AcceptanceLayer Layer)
{
	switch (Layer)
	{
	case EABTSM3R5AcceptanceLayer::Automation:
		return TEXT("Automation");
	case EABTSM3R5AcceptanceLayer::FreshRuntime:
		return TEXT("FreshRuntime");
	case EABTSM3R5AcceptanceLayer::IntegrationAutomation:
		return TEXT("IntegrationAutomation");
	case EABTSM3R5AcceptanceLayer::VisiblePIE:
		return TEXT("VisiblePIE");
	default:
		return TEXT("Unknown");
	}
}

uint64 FABTSM3R5AcceptanceManifest::
	ComputeSweepSeedManifestHash()
{
	return ABTSM3R5ManifestPrivate::HashUtf8(
		ABTSM3R5ManifestPrivate::BuildSeedList(
			GetSweepSeeds()));
}

FString FABTSM3R5AcceptanceManifest::
	BuildCanonicalPayload()
{
	FString Payload;
	Payload.Append(TEXT("M3R5AcceptanceManifest\n"));
	Payload.Appendf(
		TEXT("ManifestSchema=%d\nPresentationSchema=%d\nPlanner=%d\nGenerator=%d\nMonthlyPolicy=%d\n"),
		ManifestSchemaVersion,
		PresentationSchemaVersion,
		PlannerVersion,
		GeneratorVersion,
		MonthlyLayoutPolicyVersion);
	Payload.Appendf(
		TEXT("DisplaySeed=%d\nDisplayPreviewSourceCandidate=%d\nSweepSeeds=%s\n"),
		DisplaySeed,
		DisplayPreviewSourceCandidateId,
		*ABTSM3R5ManifestPrivate::BuildSeedList(
			GetSweepSeeds()));
	Payload.Appendf(
		TEXT("RequiredR3=%016llX\nSeedManifest=%016llX\n"),
		static_cast<unsigned long long>(
			RequiredR3ManifestHash),
		static_cast<unsigned long long>(
			FrozenSweepSeedManifestHash));
	Payload.Appendf(
		TEXT("Defaults=Beat:%d/%d/%d,Themes:%d,Active:%d,DeepWild:%d,Decor:%d/%d\n"),
		DefaultMinVisualBeatLengthCM,
		DefaultTargetVisualBeatLengthCM,
		DefaultMaxVisualBeatLengthCM,
		DefaultMinBiomeArchetypeCount,
		DefaultMinActiveRoleCoveragePermille,
		DefaultMaxDeepWildPermille,
		DefaultMaxDecorInstancesPerCell,
		DefaultMaxDecorInstancesPerCandidate);
	Payload.Appendf(
		TEXT("Budgets=PlannerP95MS:%d,PlannerMaxMS:%d,FullRebuildMS:%d,SurfaceSubdivision:%d,PeakPhysicalBaselineMB:%d,PeakPhysicalPermille:%d,MinVisualBiomeComponentCells:%d,MaxVisualBiomeBoundaryPermille:%d\n"),
		PlannerP95BudgetMS,
		PlannerMaxBudgetMS,
		FullRebuildBudgetMS,
		RequiredSurfaceSubdivision,
		BaselinePeakPhysicalMB,
		MaxPeakPhysicalPermille,
		MinVisualBiomeComponentCells,
		MaxVisualBiomeBoundaryPermille);
	Payload.Appendf(
		TEXT("Display=Config:%016llX,SourceSpatial:%016llX,Result:%016llX,PreviewCandidate:%016llX\n"),
		static_cast<unsigned long long>(
			FrozenDisplayConfigHash),
		static_cast<unsigned long long>(
			FrozenDisplaySourceSpatialHash),
		static_cast<unsigned long long>(
			FrozenDisplayResultHash),
		static_cast<unsigned long long>(
			FrozenDisplayPreviewCandidateHash));
	Payload.Appendf(
		TEXT("Sweep=Candidates:%d,MergedLogicalSingletons:%d,MergedSmallVisualFragmentCells:%d,Oracle:%016llX\n"),
		SweepCandidatePlanCount,
		SweepMergedLogicalSingletonCount,
		SweepMergedSmallVisualFragmentCellCount,
		static_cast<unsigned long long>(
			FrozenSweepOracleHash));
	Payload.Append(
		TEXT("Boundary=M3LocalAccepted:PreviewAuthority+VisualRhythmConsumers+PlannerBudget+Subdivision7RebuildBudget+PeakMemoryBudget;IntegrationPending:CharacterSweepVisibility+M6DynamicProxy+M9DeveloperTraversal+VisiblePIE;SelectedCandidate=0;MonthlyAccepted=0\n"));
	for (const FABTSM3R5AcceptanceEntry& Entry :
		GetEntries())
	{
		Payload.Appendf(
			TEXT("%s|%s|%s|Cases=%d|Terminal=%d\n"),
			Entry.EntryId,
			GetLayerName(Entry.Layer),
			Entry.Target,
			Entry.ExpectedCaseCount,
			Entry.ExpectedTerminalCount);
	}
	return Payload;
}

uint64 FABTSM3R5AcceptanceManifest::ComputeManifestHash()
{
	return ABTSM3R5ManifestPrivate::HashUtf8(
		BuildCanonicalPayload());
}

bool FABTSM3R5AcceptanceManifest::Validate(
	FString& OutFailure)
{
	OutFailure.Reset();
	FString ParentFailure;
	if (!FABTSM3R3AcceptanceManifest::Validate(
			ParentFailure)
		|| FABTSM3R3AcceptanceManifest::
				ComputeManifestHash()
			!= RequiredR3ManifestHash)
	{
		OutFailure = FString::Printf(
			TEXT("RequiredR3:%s"),
			*ParentFailure);
		return false;
	}
	if (ComputeSweepSeedManifestHash()
			!= FrozenSweepSeedManifestHash
		|| GetSweepSeeds().Num() != SweepSeedCount
		|| GetSweepSeeds()[0] != DisplaySeed)
	{
		OutFailure = TEXT("SweepSeedManifest");
		return false;
	}
	const FABTSM3MonthlyPresentationConfig Defaults;
	if (Defaults.MinVisualBeatLengthCM
			!= DefaultMinVisualBeatLengthCM
		|| Defaults.TargetVisualBeatLengthCM
			!= DefaultTargetVisualBeatLengthCM
		|| Defaults.MaxVisualBeatLengthCM
			!= DefaultMaxVisualBeatLengthCM
		|| Defaults.MinBiomeArchetypeCount
			!= DefaultMinBiomeArchetypeCount
		|| Defaults.MinActiveRoleCoveragePermille
			!= DefaultMinActiveRoleCoveragePermille
		|| Defaults.MaxDeepWildPermille
			!= DefaultMaxDeepWildPermille
		|| Defaults.MinVisualBiomeComponentCells
			!= MinVisualBiomeComponentCells
		|| Defaults.MaxVisualBiomeBoundaryPermille
			!= MaxVisualBiomeBoundaryPermille
		|| Defaults.MaxDecorInstancesPerCell
			!= DefaultMaxDecorInstancesPerCell
		|| Defaults.MaxDecorInstancesPerCandidate
			!= DefaultMaxDecorInstancesPerCandidate
		|| !Defaults.bSuppressDecorOnActiveRoles
		|| FABTSM3MonthlyPresentationBuilder::
				PresentationSchemaVersion
			!= PresentationSchemaVersion
		|| FABTSM3MonthlyPresentationBuilder::
				PlannerVersion
			!= PlannerVersion
		|| FABTSM3MonthlyPresentationBuilder::
				ComputeConfigHash(Defaults)
			!= FrozenDisplayConfigHash)
	{
		OutFailure = TEXT("DefaultContract");
		return false;
	}
	TSet<FString> EntryIds;
	TSet<FString> Targets;
	if (GetEntries().Num() != 16)
	{
		OutFailure = TEXT("AcceptanceEntryCount");
		return false;
	}
	for (const FABTSM3R5AcceptanceEntry& Entry :
		GetEntries())
	{
		if (FCString::Strlen(Entry.EntryId) == 0
			|| FCString::Strlen(Entry.Target) == 0
			|| Entry.ExpectedCaseCount <= 0
			|| Entry.ExpectedTerminalCount != 1
			|| EntryIds.Contains(Entry.EntryId)
			|| Targets.Contains(Entry.Target))
		{
			OutFailure = TEXT("AcceptanceEntry");
			return false;
		}
		EntryIds.Add(Entry.EntryId);
		Targets.Add(Entry.Target);
	}
	if (FrozenDisplayConfigHash == 0
		|| FrozenDisplaySourceSpatialHash == 0
		|| FrozenDisplayResultHash == 0
		|| FrozenDisplayPreviewCandidateHash == 0
		|| FrozenSweepOracleHash == 0)
	{
		OutFailure = TEXT("FrozenIdentity");
		return false;
	}
	if (ComputeManifestHash() != FrozenManifestHash)
	{
		OutFailure = FString::Printf(
			TEXT("ManifestHash:%016llX"),
			static_cast<unsigned long long>(
				ComputeManifestHash()));
		return false;
	}
	return true;
}
