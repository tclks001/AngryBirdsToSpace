// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73JuryDemoFixedSixRegistration.h"

#include "Building/ABTSM73BeamDemoManifest.h"
#include "Building/ABTSM73BeamStage45PlacementFreeze.h"
#include "Building/ABTSM73StableBuildingActor.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Kismet/GameplayStatics.h"

#include "ABTSM73BeamD1BrickCompiler.h"

namespace ABTSM73JuryDemoFixedSixRegistrationPrivate
{
	constexpr double RegistrationToleranceCM = 1.0e-3;
	constexpr uint64 FNVOffsetBasis = 1469598103934665603ull;
	constexpr uint64 FNVPrime = 1099511628211ull;

	bool FixedSixRegistrationBoxesEqual(
		const FBox& A,
		const FBox& B,
		const double Tolerance = RegistrationToleranceCM)
	{
		return A.IsValid && B.IsValid
			&& A.Min.Equals(B.Min, Tolerance)
			&& A.Max.Equals(B.Max, Tolerance);
	}

	void FixedSixRegistrationAppendBox(FBox& Aggregate, const FBox& Box)
	{
		if (!Box.IsValid)
		{
			return;
		}
		Aggregate += Box.Min;
		Aggregate += Box.Max;
	}

	void FixedSixRegistrationHashUInt64(uint64& Hash, const uint64 Value)
	{
		for (int32 ByteIndex = 0; ByteIndex < 8; ++ByteIndex)
		{
			Hash ^= (Value >> (ByteIndex * 8)) & 0xffull;
			Hash *= FNVPrime;
		}
	}

	bool ResolveFixedSixManifestEntry(
		const FName ContractManifestEntryId,
		FABTSM73BeamDemoManifestEntry& OutEntry,
		FString& OutError)
	{
		OutEntry = FABTSM73BeamDemoManifestEntry();
		OutError.Reset();
		for (const FABTSM73BeamDemoManifestEntry& Entry
			: FABTSM73BeamDemoManifest::GetEntries())
		{
			FString ContractId = Entry.StableId.ToString();
			if (!ContractId.RemoveFromStart(TEXT("Demo")))
			{
				continue;
			}
			if (FName(*ContractId) == ContractManifestEntryId)
			{
				OutEntry = Entry;
				return true;
			}
		}
		OutError = FString::Printf(
			TEXT("FixedSixManifestEntryUnknown:%s"),
			*ContractManifestEntryId.ToString());
		return false;
	}

	bool FixedSixEffectExitsPad(
		const FBox& EffectBounds,
		const FVector2D& PadHalfExtentCM)
	{
		return !EffectBounds.IsValid
			|| EffectBounds.Min.X < -PadHalfExtentCM.X - RegistrationToleranceCM
			|| EffectBounds.Max.X > PadHalfExtentCM.X + RegistrationToleranceCM
			|| EffectBounds.Min.Y < -PadHalfExtentCM.Y - RegistrationToleranceCM
			|| EffectBounds.Max.Y > PadHalfExtentCM.Y + RegistrationToleranceCM;
	}
}

bool FABTSM73JuryDemoFixedSixStaticEntry::IsUsable(
	const double Tolerance) const
{
	const double SafeTolerance = FMath::Max(Tolerance, UE_DOUBLE_SMALL_NUMBER);
	return !ManifestEntryId.IsNone()
		&& DemoBuilding != EABTSM73BeamDemoBuilding::Custom
		&& EncounterIndex >= 0
		&& EncounterIndex
			< FABTSJuryDemoFixedSixContract::ExpectedSiteCount
		&& DifficultyTier == EncounterIndex
		&& DeterministicSeed > 0
		&& WorldTransform.IsValid()
		&& WorldTransform.GetScale3D().Equals(
			FVector::OneVector, SafeTolerance)
		&& PadHalfExtentCM.X > 0.0
		&& PadHalfExtentCM.Y > 0.0
		&& LocalBounds.IsValid
		&& EffectBounds.IsValid
		&& DescriptorHash != 0
		&& StaticGeometryHash != 0
		&& ProductionIdentityHash != 0
		&& DeviceAssemblyHash != 0
		&& SourceLayoutHash != 0
		&& RegistrationResultHash != 0
		&& !Bricks.IsEmpty()
		&& !Devices.IsEmpty();
}

bool FABTSM73JuryDemoFixedSixStaticPlan::IsUsable(
	const double Tolerance) const
{
	if (ContractVersion
			!= FABTSJuryDemoFixedSixContract::SupportedV2ContractVersion
		|| WorldSeed != FABTSJuryDemoFixedSixContract::FrozenWorldSeed
		|| PlacementCatalogHash
			!= FABTSJuryDemoFixedSixContract::FrozenV2PlacementCatalogHash
		|| LayoutHash != FABTSJuryDemoFixedSixContract::FrozenV2LayoutHash
		|| RegistrationResultHash == 0
		|| Entries.Num()
			!= FABTSJuryDemoFixedSixContract::ExpectedSiteCount)
	{
		return false;
	}
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FABTSM73JuryDemoFixedSixStaticEntry& Entry = Entries[Index];
		if (!Entry.IsUsable(Tolerance)
			|| Entry.EncounterIndex != Index
			|| Entry.SourceLayoutHash != LayoutHash
			|| Entry.RegistrationResultHash != RegistrationResultHash)
		{
			return false;
		}
	}
	return true;
}

bool FABTSM73JuryDemoFixedSixRegistration::BuildStaticPlan(
	const FABTSBuildingGenerationContract& Contract,
	FABTSM73JuryDemoFixedSixStaticPlan& OutPlan,
	FString& OutError)
{
	using namespace ABTSM73JuryDemoFixedSixRegistrationPrivate;
	OutPlan = FABTSM73JuryDemoFixedSixStaticPlan();
	OutError.Reset();
	const auto Reject = [&OutPlan, &OutError](const FString& Reason)
	{
		OutPlan = FABTSM73JuryDemoFixedSixStaticPlan();
		OutError = Reason;
		return false;
	};

	if (!Contract.IsUsable())
	{
		return Reject(TEXT("FixedSixV2OuterContractRejected"));
	}
	const FABTSJuryDemoFixedSixContract& Snapshot =
		Contract.JuryDemoFixedSix;
	if (Snapshot.ContractVersion
			!= FABTSJuryDemoFixedSixContract::SupportedV2ContractVersion
		|| Snapshot.PlacementSchemaVersion
			!= FABTSM73BeamStage45PlacementFreeze::SchemaVersion
		|| Snapshot.DemoManifestVersion != FABTSM73BeamDemoManifest::Version
		|| Snapshot.DemoManifestHash
			!= static_cast<uint64>(FABTSM73BeamDemoManifest::CalculateHash())
		|| Snapshot.PlacementCatalogHash
			!= FABTSM73BeamStage45PlacementFreeze::FrozenCatalogHash
		|| Snapshot.LayoutHash
			!= FABTSJuryDemoFixedSixContract::FrozenV2LayoutHash
		|| Snapshot.WorldSeed != Contract.Identity.WorldSeed
		|| Snapshot.Sites.Num()
			!= FABTSJuryDemoFixedSixContract::ExpectedSiteCount)
	{
		return Reject(TEXT("FixedSixV2SnapshotIdentityRejected"));
	}

	FABTSM73JuryDemoFixedSixStaticPlan CandidatePlan;
	CandidatePlan.ContractVersion = Snapshot.ContractVersion;
	CandidatePlan.WorldSeed = Snapshot.WorldSeed;
	CandidatePlan.PlacementCatalogHash = Snapshot.PlacementCatalogHash;
	CandidatePlan.LayoutHash = Snapshot.LayoutHash;
	CandidatePlan.Entries.Reserve(Snapshot.Sites.Num());
	uint64 ResultHash = FNVOffsetBasis;
	FixedSixRegistrationHashUInt64(ResultHash, Snapshot.LayoutHash);
	FixedSixRegistrationHashUInt64(ResultHash, Snapshot.PlacementCatalogHash);

	for (int32 Index = 0; Index < Snapshot.Sites.Num(); ++Index)
	{
		const FABTSJuryDemoFixedSixBuildingSite& Site = Snapshot.Sites[Index];
		FABTSM73BeamDemoManifestEntry ManifestEntry;
		FString Error;
		if (!ResolveFixedSixManifestEntry(
			Site.ManifestEntryId, ManifestEntry, Error))
		{
			return Reject(Error);
		}
		const int32 ManifestEncounterIndex =
			static_cast<int32>(ManifestEntry.Id) - 1;
		FABTSM73BeamStage45PlacementDescriptor Frozen;
		if (!FABTSM73BeamStage45PlacementFreeze::ResolveFrozen(
			ManifestEntry.Id, Frozen, Error))
		{
			return Reject(FString::Printf(
				TEXT("FixedSixV2FrozenResolve:%s:%s"),
				*Site.ManifestEntryId.ToString(), *Error));
		}
		if (Site.EncounterIndex != Index
			|| Site.EncounterIndex != ManifestEncounterIndex
			|| Site.DifficultyTier
				!= ManifestEntry.Settings.DifficultyTier
			|| Site.DeterministicSeed
				!= ManifestEntry.Settings.BuildingSeed
			|| Frozen.StableId != ManifestEntry.StableId
			|| Site.DescriptorHash != Frozen.DescriptorHash
			|| Site.V2Envelope.StaticGeometryHash
				!= Frozen.StaticGeometryHash
			|| !Site.PadHalfExtentCM.Equals(
				Frozen.RequiredPadHalfExtentCM, RegistrationToleranceCM)
			|| !FixedSixRegistrationBoxesEqual(
				Site.LocalBounds, Frozen.LocalBounds)
			|| !FixedSixRegistrationBoxesEqual(
				Site.V2Envelope.PhysicalBounds, Frozen.LocalBounds))
		{
			return Reject(FString::Printf(
				TEXT("FixedSixV2SiteIdentityRejected:%s:Encounter=%d"),
				*Site.ManifestEntryId.ToString(), Index));
		}

		FABTSM73BeamD1Stage55Result Generated;
		if (!FABTSM73BeamD1BrickCompiler().GenerateStage55DeviceAssembly(
			ManifestEntry.Settings, Generated, Error))
		{
			return Reject(FString::Printf(
				TEXT("FixedSixV2ProductionRejected:%s:%s"),
				*Site.ManifestEntryId.ToString(), *Error));
		}
		if (!Generated.Summary.bAccepted
			|| !Generated.Stage5.Summary.bAccepted
			|| static_cast<uint64>(Generated.Stage5.ProductionIdentityHash)
				!= Site.V2Envelope.ProductionIdentityHash
			|| static_cast<uint64>(Generated.DeviceAssemblyHash)
				!= Site.V2Envelope.DeviceAssemblyHash)
		{
			return Reject(FString::Printf(
				TEXT("FixedSixV2ProductionIdentityRejected:%s"),
				*Site.ManifestEntryId.ToString()));
		}

		FBox PhysicalBounds(EForceInit::ForceInit);
		for (const FABTSM73BeamD1BrickBinding& Brick : Generated.Stage5.Bricks)
		{
			FixedSixRegistrationAppendBox(PhysicalBounds, Brick.LocalBounds);
		}
		FBox EffectBounds(EForceInit::ForceInit);
		for (const FABTSM73BeamD1DeviceBinding& Device : Generated.Devices)
		{
			FixedSixRegistrationAppendBox(PhysicalBounds, Device.LocalBounds);
			FixedSixRegistrationAppendBox(
				EffectBounds, Device.EffectCorridorLocalBounds);
		}
		const bool bDynamicEnvelopeRequired = FixedSixEffectExitsPad(
			EffectBounds, Site.PadHalfExtentCM);
		if (!FixedSixRegistrationBoxesEqual(
				PhysicalBounds, Site.V2Envelope.PhysicalBounds)
			|| !FixedSixRegistrationBoxesEqual(
				EffectBounds, Site.V2Envelope.EffectBounds)
			|| Site.V2Envelope.bDynamicEnvelopeRequired
				!= bDynamicEnvelopeRequired)
		{
			return Reject(FString::Printf(
				TEXT("FixedSixV2EnvelopeRejected:%s"),
				*Site.ManifestEntryId.ToString()));
		}

		FABTSM73JuryDemoFixedSixStaticEntry& Entry =
			CandidatePlan.Entries.AddDefaulted_GetRef();
		Entry.ManifestEntryId = Site.ManifestEntryId;
		Entry.DemoBuilding = ManifestEntry.Id;
		Entry.EncounterIndex = Site.EncounterIndex;
		Entry.DifficultyTier = Site.DifficultyTier;
		Entry.DeterministicSeed = Site.DeterministicSeed;
		Entry.WorldTransform = Site.WorldTransform;
		Entry.PadHalfExtentCM = Site.PadHalfExtentCM;
		Entry.LocalBounds = PhysicalBounds;
		Entry.EffectBounds = EffectBounds;
		Entry.DescriptorHash = Site.DescriptorHash;
		Entry.StaticGeometryHash = Site.V2Envelope.StaticGeometryHash;
		Entry.ProductionIdentityHash =
			Site.V2Envelope.ProductionIdentityHash;
		Entry.DeviceAssemblyHash = Site.V2Envelope.DeviceAssemblyHash;
		Entry.SourceLayoutHash = Snapshot.LayoutHash;
		Entry.bDynamicEnvelopeRequired = bDynamicEnvelopeRequired;
		Entry.Bricks = MoveTemp(Generated.Stage5.Bricks);
		Entry.Devices = MoveTemp(Generated.Devices);

		FixedSixRegistrationHashUInt64(ResultHash, Entry.DescriptorHash);
		FixedSixRegistrationHashUInt64(ResultHash, Entry.StaticGeometryHash);
		FixedSixRegistrationHashUInt64(
			ResultHash, Entry.ProductionIdentityHash);
		FixedSixRegistrationHashUInt64(ResultHash, Entry.DeviceAssemblyHash);
	}

	CandidatePlan.RegistrationResultHash = ResultHash;
	for (FABTSM73JuryDemoFixedSixStaticEntry& Entry : CandidatePlan.Entries)
	{
		Entry.RegistrationResultHash = ResultHash;
	}
	if (!CandidatePlan.IsUsable())
	{
		return Reject(TEXT("FixedSixV2StaticPlanRejected"));
	}
	OutPlan = MoveTemp(CandidatePlan);
	return true;
}

bool FABTSM73JuryDemoFixedSixRegistration::SpawnStaticActors(
	UWorld& World,
	AABTSM7BuildingMaterialSystem& MaterialSystem,
	TSubclassOf<AABTSM73StableBuildingActor> BuildingClass,
	FABTSM73JuryDemoFixedSixStaticPlan&& Plan,
	TArray<TWeakObjectPtr<AABTSM73StableBuildingActor>>& OutActors,
	FString& OutError)
{
	OutActors.Reset();
	OutError.Reset();
	if (!Plan.IsUsable() || !BuildingClass)
	{
		OutError = TEXT("FixedSixV2StaticSpawnPreflightRejected");
		return false;
	}

	TArray<TWeakObjectPtr<AABTSM73StableBuildingActor>> CandidateActors;
	CandidateActors.Reserve(Plan.Entries.Num());
	const auto Rollback = [&CandidateActors](const FString& Reason)
	{
		for (const TWeakObjectPtr<AABTSM73StableBuildingActor>& WeakActor
			: CandidateActors)
		{
			if (AABTSM73StableBuildingActor* Actor = WeakActor.Get())
			{
				Actor->RollbackJuryDemoFixedSixStaticRegistration(Reason);
			}
		}
		CandidateActors.Reset();
	};

	for (FABTSM73JuryDemoFixedSixStaticEntry& Entry : Plan.Entries)
	{
		const FName EntryId = Entry.ManifestEntryId;
		const int32 ExpectedModuleCount = Entry.Bricks.Num() + Entry.Devices.Num();
		AABTSM73StableBuildingActor* Actor =
			World.SpawnActorDeferred<AABTSM73StableBuildingActor>(
				BuildingClass,
				Entry.WorldTransform,
				nullptr,
				nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Actor == nullptr)
		{
			OutError = FString::Printf(
				TEXT("FixedSixV2StaticSpawnDeferred:%s"),
				*EntryId.ToString());
			Rollback(OutError);
			return false;
		}
		CandidateActors.Add(Actor);
		FString ConfigureError;
		if (!Actor->ConfigureJuryDemoFixedSixStaticRegistration(
			MoveTemp(Entry), ConfigureError))
		{
			OutError = FString::Printf(
				TEXT("FixedSixV2StaticConfigure:%s:%s"),
				*EntryId.ToString(), *ConfigureError);
			Rollback(OutError);
			return false;
		}
		UGameplayStatics::FinishSpawningActor(Actor, Actor->GetActorTransform());
		Actor->InitializeRuntimeBuilding(&MaterialSystem);
		if (!Actor->IsJuryDemoFixedSixStaticRegistrationAccepted()
			|| Actor->GetJuryDemoFixedSixStaticModuleCount()
				!= ExpectedModuleCount)
		{
			OutError = FString::Printf(
				TEXT("FixedSixV2StaticInitialize:%s:Expected=%d:Actual=%d"),
				*EntryId.ToString(), ExpectedModuleCount,
				Actor->GetJuryDemoFixedSixStaticModuleCount());
			Rollback(OutError);
			return false;
		}
	}

	OutActors = MoveTemp(CandidateActors);
	return true;
}
