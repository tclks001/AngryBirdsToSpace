// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73BuildingFreezeV3RuntimeRegistration.h"

#include "Building/ABTSM73StableBuildingActor.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Kismet/GameplayStatics.h"

namespace ABTSM73BuildingFreezeV3RuntimeRegistrationPrivate
{
	constexpr double RuntimeToleranceCM = 1.0e-3;
	constexpr uint64 FNVOffsetBasis = 1469598103934665603ull;
	constexpr uint64 FNVPrime = 1099511628211ull;

	void HashUInt64(uint64& Hash, const uint64 Value)
	{
		for (int32 ByteIndex = 0; ByteIndex < 8; ++ByteIndex)
		{
			Hash ^= (Value >> (ByteIndex * 8)) & 0xffull;
			Hash *= FNVPrime;
		}
	}

	void HashQuantizedDouble(uint64& Hash, const double Value)
	{
		const int64 Quantized = FMath::RoundToInt64(Value * 1000.0);
		HashUInt64(Hash, static_cast<uint64>(Quantized));
	}

	void HashTransform(uint64& Hash, const FTransform& Transform)
	{
		const FVector Translation = Transform.GetTranslation();
		FQuat Rotation = Transform.GetRotation().GetNormalized();
		if (Rotation.W < 0.0)
		{
			Rotation.X *= -1.0;
			Rotation.Y *= -1.0;
			Rotation.Z *= -1.0;
			Rotation.W *= -1.0;
		}
		const FVector Scale = Transform.GetScale3D();
		HashQuantizedDouble(Hash, Translation.X);
		HashQuantizedDouble(Hash, Translation.Y);
		HashQuantizedDouble(Hash, Translation.Z);
		HashQuantizedDouble(Hash, Rotation.X);
		HashQuantizedDouble(Hash, Rotation.Y);
		HashQuantizedDouble(Hash, Rotation.Z);
		HashQuantizedDouble(Hash, Rotation.W);
		HashQuantizedDouble(Hash, Scale.X);
		HashQuantizedDouble(Hash, Scale.Y);
		HashQuantizedDouble(Hash, Scale.Z);
	}

	bool BoxesEqual(
		const FBox& A,
		const FBox& B,
		const double Tolerance = RuntimeToleranceCM)
	{
		return A.IsValid && B.IsValid
			&& A.Min.Equals(B.Min, Tolerance)
			&& A.Max.Equals(B.Max, Tolerance);
	}

	int32 ResolveComplexityIndex(
		const EABTSM73BeamDemoBuilding ComplexityId)
	{
		return static_cast<int32>(ComplexityId)
			- static_cast<int32>(EABTSM73BeamDemoBuilding::E1ColumnBreak);
	}

	bool HistogramsEqual(
		const FABTSM73BuildingFreezeV3MaterialHistogram& A,
		const FABTSM73BuildingFreezeV3MaterialHistogram& B)
	{
		return A.Wood == B.Wood
			&& A.Stone == B.Stone
			&& A.Iron == B.Iron
			&& A.Glass == B.Glass
			&& A.Crystal == B.Crystal;
	}

	void AddMaterial(
		FABTSM73BuildingFreezeV3MaterialHistogram& Histogram,
		const EABTSM7BuildingMaterial Material)
	{
		switch (Material)
		{
		case EABTSM7BuildingMaterial::Wood: ++Histogram.Wood; break;
		case EABTSM7BuildingMaterial::Stone: ++Histogram.Stone; break;
		case EABTSM7BuildingMaterial::Iron: ++Histogram.Iron; break;
		case EABTSM7BuildingMaterial::Glass: ++Histogram.Glass; break;
		case EABTSM7BuildingMaterial::Crystal: ++Histogram.Crystal; break;
		default: break;
		}
	}

	const FABTSM73BuildingFreezeV3FrozenIdentity* FindFrozenIdentity(
		const EABTSM73BeamDemoBuilding ComplexityId)
	{
		return FABTSM73BuildingFreezeV3::GetFrozenIdentities().FindByPredicate(
			[ComplexityId](const FABTSM73BuildingFreezeV3FrozenIdentity& Frozen)
			{
				return Frozen.ManifestEntryId == ComplexityId;
			});
	}

	uint64 CalculatePlacementHash(
		TConstArrayView<FABTSM73BuildingFreezeV3RuntimePlacement> Placements)
	{
		uint64 Hash = FNVOffsetBasis;
		HashUInt64(Hash, FABTSM73BuildingFreezeV3::FrozenCatalogHash);
		for (const FABTSM73BuildingFreezeV3RuntimePlacement& Placement : Placements)
		{
			HashUInt64(Hash, static_cast<uint64>(Placement.ComplexityId));
			HashUInt64(Hash, static_cast<uint64>(Placement.EncounterSlot));
			HashTransform(Hash, Placement.WorldTransform);
		}
		return Hash;
	}

	uint64 CalculatePlacementHash(
		TConstArrayView<FABTSM73BuildingFreezeV3RuntimeEntry> Entries)
	{
		uint64 Hash = FNVOffsetBasis;
		HashUInt64(Hash, FABTSM73BuildingFreezeV3::FrozenCatalogHash);
		for (const FABTSM73BuildingFreezeV3RuntimeEntry& Entry : Entries)
		{
			HashUInt64(Hash, static_cast<uint64>(Entry.ComplexityId));
			HashUInt64(Hash, static_cast<uint64>(Entry.EncounterSlot));
			HashTransform(Hash, Entry.WorldTransform);
		}
		return Hash;
	}

	uint64 CalculateResultHash(
		const uint64 RuntimePlacementHash,
		TConstArrayView<FABTSM73BuildingFreezeV3RuntimeEntry> Entries)
	{
		uint64 Hash = FNVOffsetBasis;
		HashUInt64(Hash, FABTSM73BuildingFreezeV3::FrozenCatalogHash);
		HashUInt64(Hash, RuntimePlacementHash);
		for (const FABTSM73BuildingFreezeV3RuntimeEntry& Entry : Entries)
		{
			HashUInt64(Hash, Entry.DescriptorHash);
			HashUInt64(Hash, Entry.StaticGeometryHash);
			HashUInt64(Hash, Entry.ProductionHash);
			HashUInt64(Hash, Entry.DeviceAssemblyHash);
		}
		return Hash;
	}
}

const FName FABTSM73BuildingFreezeV3RuntimeRegistration::FixtureAuthority(
	TEXT("M7V3RuntimeFixture"));

bool FABTSM73BuildingFreezeV3RuntimeEntry::IsUsable(
	const double Tolerance) const
{
	using namespace ABTSM73BuildingFreezeV3RuntimeRegistrationPrivate;
	const double SafeTolerance = FMath::Max(Tolerance, UE_DOUBLE_SMALL_NUMBER);
	const int32 ExpectedComplexityIndex = ResolveComplexityIndex(ComplexityId);
	int32 ExpectedEncounterSlot = INDEX_NONE;
	EABTSM7BuildingMaterial ExpectedPrimaryMaterial =
		EABTSM7BuildingMaterial::Wood;
	FString Error;
	const FABTSM73BuildingFreezeV3FrozenIdentity* Frozen =
		FindFrozenIdentity(ComplexityId);
	if (Frozen == nullptr
		|| !FABTSM73BuildingFreezeV3::ResolveEncounterSlot(
			ComplexityId, ExpectedEncounterSlot, Error)
		|| !FABTSM73BuildingFreezeV3::ResolvePrimaryMaterial(
			ComplexityId, ExpectedPrimaryMaterial, Error))
	{
		return false;
	}

	FABTSM73BuildingFreezeV3MaterialHistogram ActualHistogram;
	for (const FABTSM73BeamD1BrickBinding& Brick : Bricks)
	{
		if (!Brick.LocalTransform.IsValid() || !Brick.LocalBounds.IsValid)
		{
			return false;
		}
		AddMaterial(ActualHistogram, Brick.BrickSpec.Material);
	}
	for (const FABTSM73BeamD1DeviceBinding& Device : Devices)
	{
		if (!Device.LocalTransform.IsValid()
			|| !Device.LocalBounds.IsValid
			|| !Device.EffectCorridorLocalBounds.IsValid)
		{
			return false;
		}
	}
	for (const FABTSM73BuildingFreezeV3CapBinding& Cap : Caps)
	{
		if (!Cap.SiteLocalTransform.IsValid()
			|| !Cap.SiteLocalBounds.IsValid
			|| Cap.BrickSpec.Material != EABTSM7BuildingMaterial::Crystal
			|| Cap.bLoadBearing
			|| Cap.bWeaknessCandidate
			|| Cap.DeviceRole != EABTSM73BeamD1DeviceRole::None)
		{
			return false;
		}
		AddMaterial(ActualHistogram, Cap.BrickSpec.Material);
	}

	return ComplexityId != EABTSM73BeamDemoBuilding::Custom
		&& !StableId.IsNone()
		&& !GameplayProfileId.IsNone()
		&& ExpectedComplexityIndex >= 0
		&& ExpectedComplexityIndex < FABTSM73BuildingFreezeV3::ExpectedEntryCount
		&& ComplexityIndex == ExpectedComplexityIndex
		&& ComplexityTier == ExpectedComplexityIndex
		&& ComplexityTier == Frozen->DifficultyTier
		&& EncounterSlot == ExpectedEncounterSlot
		&& EncounterSlot >= 0
		&& EncounterSlot < FABTSM73BuildingFreezeV3::ExpectedEntryCount
		&& DeterministicSeed == Frozen->BuildingSeed
		&& PrimaryMaterial == ExpectedPrimaryMaterial
		&& WorldTransform.IsValid()
		&& WorldTransform.GetScale3D().Equals(FVector::OneVector, SafeTolerance)
		&& BoxesEqual(SiteLocalBounds, Frozen->SiteLocalBounds, SafeTolerance)
		&& BoxesEqual(PadBounds, Frozen->PadBounds, SafeTolerance)
		&& BoxesEqual(EffectBounds, Frozen->EffectBounds, SafeTolerance)
		&& DescriptorHash == Frozen->DescriptorHash
		&& StaticGeometryHash == Frozen->StaticGeometryHash
		&& ProductionHash == Frozen->ProductionHash
		&& DeviceAssemblyHash == Frozen->SourceDeviceAssemblyHash
		&& RuntimePlacementHash != 0
		&& RegistrationResultHash != 0
		&& Bricks.Num() == Frozen->BrickCount
		&& Devices.Num() == Frozen->DeviceCount
		&& Caps.Num() == Frozen->CapCount
		&& !Bricks.IsEmpty()
		&& !Devices.IsEmpty()
		&& HistogramsEqual(MaterialHistogram, Frozen->MaterialHistogram)
		&& HistogramsEqual(ActualHistogram, Frozen->MaterialHistogram);
}

bool FABTSM73BuildingFreezeV3RuntimePlan::IsUsable(
	const double Tolerance) const
{
	using namespace ABTSM73BuildingFreezeV3RuntimeRegistrationPrivate;
	if (SchemaVersion != FABTSM73BuildingFreezeV3::SchemaVersion
		|| Authority
			!= FABTSM73BuildingFreezeV3RuntimeRegistration::FixtureAuthority
		|| CatalogHash != FABTSM73BuildingFreezeV3::FrozenCatalogHash
		|| SourceLayoutHash != 0
		|| RuntimePlacementHash == 0
		|| RegistrationResultHash == 0
		|| Entries.Num() != FABTSM73BuildingFreezeV3::ExpectedEntryCount
		|| CalculatePlacementHash(Entries) != RuntimePlacementHash
		|| CalculateResultHash(RuntimePlacementHash, Entries)
			!= RegistrationResultHash)
	{
		return false;
	}

	uint32 ComplexityMask = 0;
	for (int32 EncounterIndex = 0; EncounterIndex < Entries.Num(); ++EncounterIndex)
	{
		const FABTSM73BuildingFreezeV3RuntimeEntry& Entry = Entries[EncounterIndex];
		if (!Entry.IsUsable(Tolerance)
			|| Entry.EncounterSlot != EncounterIndex
			|| Entry.RuntimePlacementHash != RuntimePlacementHash
			|| Entry.RegistrationResultHash != RegistrationResultHash)
		{
			return false;
		}
		const uint32 ComplexityBit = 1u << Entry.ComplexityIndex;
		if ((ComplexityMask & ComplexityBit) != 0)
		{
			return false;
		}
		ComplexityMask |= ComplexityBit;
	}
	return ComplexityMask
		== ((1u << FABTSM73BuildingFreezeV3::ExpectedEntryCount) - 1u);
}

bool FABTSM73BuildingFreezeV3RuntimeRegistration::BuildFixturePlan(
	TConstArrayView<FABTSM73BuildingFreezeV3RuntimePlacement> Placements,
	FABTSM73BuildingFreezeV3RuntimePlan& OutPlan,
	FString& OutError)
{
	using namespace ABTSM73BuildingFreezeV3RuntimeRegistrationPrivate;
	OutPlan = FABTSM73BuildingFreezeV3RuntimePlan();
	OutError.Reset();
	const auto Reject = [&OutPlan, &OutError](const FString& Reason)
	{
		OutPlan = FABTSM73BuildingFreezeV3RuntimePlan();
		OutError = Reason;
		return false;
	};

	if (Placements.Num() != FABTSM73BuildingFreezeV3::ExpectedEntryCount)
	{
		return Reject(TEXT("BuildingFreezeV3RuntimeFixturePlacementCountRejected"));
	}

	TArray<FABTSM73BuildingFreezeV3Descriptor> Descriptors;
	uint64 CatalogHash = 0;
	if (!FABTSM73BuildingFreezeV3::DeriveAndValidateCatalog(
		Descriptors, CatalogHash, OutError))
	{
		return Reject(FString::Printf(
			TEXT("BuildingFreezeV3RuntimeCatalogRejected:%s"), *OutError));
	}
	if (Descriptors.Num() != Placements.Num()
		|| CatalogHash != FABTSM73BuildingFreezeV3::FrozenCatalogHash)
	{
		return Reject(TEXT("BuildingFreezeV3RuntimeCatalogIdentityRejected"));
	}

	FABTSM73BuildingFreezeV3RuntimePlan Candidate;
	Candidate.SchemaVersion = FABTSM73BuildingFreezeV3::SchemaVersion;
	Candidate.Authority = FixtureAuthority;
	Candidate.CatalogHash = CatalogHash;
	Candidate.SourceLayoutHash = 0;
	Candidate.RuntimePlacementHash = CalculatePlacementHash(Placements);
	Candidate.Entries.Reserve(Descriptors.Num());

	for (int32 EncounterIndex = 0;
		EncounterIndex < Descriptors.Num(); ++EncounterIndex)
	{
		FABTSM73BuildingFreezeV3Descriptor& Descriptor =
			Descriptors[EncounterIndex];
		const FABTSM73BuildingFreezeV3RuntimePlacement& Placement =
			Placements[EncounterIndex];
		if (Placement.EncounterSlot != EncounterIndex
			|| Descriptor.EncounterSlot != EncounterIndex
			|| Placement.ComplexityId != Descriptor.ManifestEntryId
			|| !Placement.WorldTransform.IsValid()
			|| !Placement.WorldTransform.GetScale3D().Equals(
				FVector::OneVector, RuntimeToleranceCM))
		{
			return Reject(FString::Printf(
				TEXT("BuildingFreezeV3RuntimePlacementRejected:")
				TEXT("Encounter=%d Complexity=%d Expected=%d"),
				EncounterIndex, static_cast<int32>(Placement.ComplexityId),
				static_cast<int32>(Descriptor.ManifestEntryId)));
		}

		FABTSM73BuildingFreezeV3RuntimeEntry& Entry =
			Candidate.Entries.AddDefaulted_GetRef();
		Entry.ComplexityId = Descriptor.ManifestEntryId;
		Entry.StableId = Descriptor.StableId;
		Entry.GameplayProfileId = Descriptor.GameplayProfileId;
		Entry.ComplexityIndex = ResolveComplexityIndex(
			Descriptor.ManifestEntryId);
		Entry.ComplexityTier = Descriptor.DifficultyTier;
		Entry.EncounterSlot = Descriptor.EncounterSlot;
		Entry.DeterministicSeed = Descriptor.BuildingSeed;
		Entry.PrimaryMaterial = Descriptor.PrimaryMaterial;
		Entry.WorldTransform = Placement.WorldTransform;
		Entry.SiteLocalBounds = Descriptor.SiteLocalBounds;
		Entry.PadBounds = Descriptor.PadBounds;
		Entry.EffectBounds = Descriptor.EffectBounds;
		Entry.MaterialHistogram = Descriptor.MaterialHistogram;
		Entry.DescriptorHash = Descriptor.DescriptorHash;
		Entry.StaticGeometryHash = Descriptor.StaticGeometryHash;
		Entry.ProductionHash = Descriptor.ProductionHash;
		Entry.DeviceAssemblyHash = Descriptor.SourceDeviceAssemblyHash;
		Entry.RuntimePlacementHash = Candidate.RuntimePlacementHash;
		Entry.Bricks = MoveTemp(Descriptor.Bricks);
		Entry.Devices = MoveTemp(Descriptor.Devices);
		Entry.Caps = MoveTemp(Descriptor.Caps);
	}

	Candidate.RegistrationResultHash = CalculateResultHash(
		Candidate.RuntimePlacementHash, Candidate.Entries);
	for (FABTSM73BuildingFreezeV3RuntimeEntry& Entry : Candidate.Entries)
	{
		Entry.RegistrationResultHash = Candidate.RegistrationResultHash;
	}
	if (!Candidate.IsUsable())
	{
		return Reject(TEXT("BuildingFreezeV3RuntimeFixturePlanRejected"));
	}
	OutPlan = MoveTemp(Candidate);
	return true;
}

bool FABTSM73BuildingFreezeV3RuntimeRegistration::SpawnStaticActors(
	UWorld& World,
	AABTSM7BuildingMaterialSystem& MaterialSystem,
	TSubclassOf<AABTSM73StableBuildingActor> BuildingClass,
	FABTSM73BuildingFreezeV3RuntimePlan&& Plan,
	TArray<TWeakObjectPtr<AABTSM73StableBuildingActor>>& OutActors,
	FString& OutError)
{
	OutActors.Reset();
	OutError.Reset();
	if (!Plan.IsUsable() || !BuildingClass)
	{
		OutError = TEXT("BuildingFreezeV3RuntimeSpawnPreflightRejected");
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
				Actor->RollbackBuildingFreezeV3RuntimeRegistration(Reason);
			}
		}
		CandidateActors.Reset();
	};

	for (FABTSM73BuildingFreezeV3RuntimeEntry& Entry : Plan.Entries)
	{
		const EABTSM73BeamDemoBuilding ComplexityId = Entry.ComplexityId;
		const int32 EncounterSlot = Entry.EncounterSlot;
		const int32 ExpectedModuleCount =
			Entry.Bricks.Num() + Entry.Devices.Num() + Entry.Caps.Num();
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
				TEXT("BuildingFreezeV3RuntimeSpawnDeferred:")
				TEXT("Complexity=%d Encounter=%d"),
				static_cast<int32>(ComplexityId), EncounterSlot);
			Rollback(OutError);
			return false;
		}
		CandidateActors.Add(Actor);
		FString ConfigureError;
		if (!Actor->ConfigureBuildingFreezeV3RuntimeRegistration(
			MoveTemp(Entry), ConfigureError))
		{
			OutError = FString::Printf(
				TEXT("BuildingFreezeV3RuntimeConfigure:")
				TEXT("Complexity=%d Encounter=%d:%s"),
				static_cast<int32>(ComplexityId), EncounterSlot,
				*ConfigureError);
			Rollback(OutError);
			return false;
		}
		UGameplayStatics::FinishSpawningActor(Actor, Actor->GetActorTransform());
		Actor->InitializeRuntimeBuilding(&MaterialSystem);
		if (!Actor->IsBuildingFreezeV3RuntimeRegistrationAccepted()
			|| Actor->GetBuildingFreezeV3RuntimeModuleCount()
				!= ExpectedModuleCount)
		{
			OutError = FString::Printf(
				TEXT("BuildingFreezeV3RuntimeInitialize:")
				TEXT("Complexity=%d Encounter=%d Expected=%d Actual=%d"),
				static_cast<int32>(ComplexityId), EncounterSlot,
				ExpectedModuleCount,
				Actor->GetBuildingFreezeV3RuntimeModuleCount());
			Rollback(OutError);
			return false;
		}
	}

	OutActors = MoveTemp(CandidateActors);
	return true;
}
