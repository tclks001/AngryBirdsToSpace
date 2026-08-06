// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/ABTSStylizedMaterialOverrideRegistry.h"

#include "Components/PrimitiveComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UObjectGlobals.h"

namespace ABTSStylizedMaterialOverrideRegistryPrivate
{
	struct FSlotKey
	{
		TWeakObjectPtr<UPrimitiveComponent> Component;
		int32 SlotIndex = INDEX_NONE;

		bool operator==(const FSlotKey& Other) const
		{
			return Component == Other.Component && SlotIndex == Other.SlotIndex;
		}

		friend uint32 GetTypeHash(const FSlotKey& Key)
		{
			return HashCombineFast(GetTypeHash(Key.Component), GetTypeHash(Key.SlotIndex));
		}
	};

	struct FSavedSlotState
	{
		TObjectPtr<UMaterialInterface> OriginalMaterial = nullptr;
		TObjectPtr<UMaterialInterface> AppliedMaterial = nullptr;
		EABTSStylizedMaterialFamily Family = EABTSStylizedMaterialFamily::None;
	};
}

class FABTSStylizedMaterialOverrideRegistry::FImpl
{
public:
	using FSlotKey = ABTSStylizedMaterialOverrideRegistryPrivate::FSlotKey;
	using FSavedSlotState =
		ABTSStylizedMaterialOverrideRegistryPrivate::FSavedSlotState;

	void Apply(
		TConstArrayView<FABTSStylizedMaterialSlotBinding> DesiredBindings,
		const bool bStyleEnabled)
	{
		RejectedBindingCount = 0;
		TMap<FSlotKey, FABTSStylizedMaterialSlotBinding> Desired;
		TSet<FSlotKey> DuplicateConflicts;
		if (bStyleEnabled)
		{
			for (const FABTSStylizedMaterialSlotBinding& Binding : DesiredBindings)
			{
				if (!Binding.IsValid()
					|| FABTSStylizedMaterialContract::ResolveAdoptionMode(Binding.Family)
						!= EABTSStylizedMaterialAdoptionMode::ReversibleSlotOverride)
				{
					++RejectedBindingCount;
					continue;
				}
				const FSlotKey Key{Binding.Component, Binding.MaterialSlotIndex};
				if (const FABTSStylizedMaterialSlotBinding* Existing = Desired.Find(Key))
				{
					if (Existing->StylizedMaterial != Binding.StylizedMaterial
						|| Existing->Family != Binding.Family)
					{
						Desired.Remove(Key);
						DuplicateConflicts.Add(Key);
						ConflictingSlots.Add(Key);
					}
					continue;
				}
				if (!DuplicateConflicts.Contains(Key))
				{
					Desired.Add(Key, Binding);
				}
			}
		}

		for (auto It = ConflictingSlots.CreateIterator(); It; ++It)
		{
			if (!It->Component.IsValid()
				|| (!Desired.Contains(*It) && !DuplicateConflicts.Contains(*It)))
			{
				It.RemoveCurrent();
			}
		}

		for (auto It = SavedSlots.CreateIterator(); It; ++It)
		{
			UPrimitiveComponent* Component = It.Key().Component.Get();
			if (!IsValid(Component))
			{
				It.RemoveCurrent();
				continue;
			}
			if (!Desired.Contains(It.Key()))
			{
				RestoreIfStillOwned(*Component, It.Key(), It.Value());
				It.RemoveCurrent();
			}
		}

		for (const TPair<FSlotKey, FABTSStylizedMaterialSlotBinding>& Pair : Desired)
		{
			if (DuplicateConflicts.Contains(Pair.Key)
				|| ConflictingSlots.Contains(Pair.Key))
			{
				continue;
			}
			UPrimitiveComponent* Component = Pair.Key.Component.Get();
			if (!IsValid(Component))
			{
				continue;
			}

			if (FSavedSlotState* Saved = SavedSlots.Find(Pair.Key))
			{
				if (Component->GetMaterial(Pair.Key.SlotIndex)
					!= Saved->AppliedMaterial.Get())
				{
					ConflictingSlots.Add(Pair.Key);
					SavedSlots.Remove(Pair.Key);
					continue;
				}
				if (Saved->AppliedMaterial.Get() != Pair.Value.StylizedMaterial)
				{
					Component->SetMaterial(
						Pair.Key.SlotIndex,
						Pair.Value.StylizedMaterial);
					Saved->AppliedMaterial = Pair.Value.StylizedMaterial;
					Saved->Family = Pair.Value.Family;
				}
				continue;
			}

			UMaterialInterface* Original = Component->GetMaterial(Pair.Key.SlotIndex);
			if (Original == Pair.Value.StylizedMaterial)
			{
				// The baseline is unknowable if a provider pre-applies its candidate.
				ConflictingSlots.Add(Pair.Key);
				continue;
			}
			FSavedSlotState Saved;
			Saved.OriginalMaterial = Original;
			Saved.AppliedMaterial = Pair.Value.StylizedMaterial;
			Saved.Family = Pair.Value.Family;
			SavedSlots.Add(Pair.Key, Saved);
			Component->SetMaterial(Pair.Key.SlotIndex, Pair.Value.StylizedMaterial);
		}
	}

	void RestoreAll()
	{
		for (const TPair<FSlotKey, FSavedSlotState>& Pair : SavedSlots)
		{
			if (UPrimitiveComponent* Component = Pair.Key.Component.Get())
			{
				RestoreIfStillOwned(*Component, Pair.Key, Pair.Value);
			}
		}
		SavedSlots.Reset();
		ConflictingSlots.Reset();
		RejectedBindingCount = 0;
	}

	void AddReferencedObjects(FReferenceCollector& Collector)
	{
		for (TPair<FSlotKey, FSavedSlotState>& Pair : SavedSlots)
		{
			Collector.AddStableReference(&Pair.Value.OriginalMaterial);
			Collector.AddStableReference(&Pair.Value.AppliedMaterial);
		}
	}

	int32 Num() const { return SavedSlots.Num(); }
	int32 GetConflictCount() const { return ConflictingSlots.Num(); }
	int32 GetRejectedBindingCount() const { return RejectedBindingCount; }

private:
	void RestoreIfStillOwned(
		UPrimitiveComponent& Component,
		const FSlotKey& Key,
		const FSavedSlotState& Saved)
	{
		if (Component.GetMaterial(Key.SlotIndex) != Saved.AppliedMaterial.Get())
		{
			ConflictingSlots.Add(Key);
			return;
		}
		Component.SetMaterial(Key.SlotIndex, Saved.OriginalMaterial.Get());
	}

	TMap<FSlotKey, FSavedSlotState> SavedSlots;
	TSet<FSlotKey> ConflictingSlots;
	int32 RejectedBindingCount = 0;
};

bool FABTSStylizedMaterialSlotBinding::IsValid() const
{
	return ::IsValid(Component)
		&& MaterialSlotIndex >= 0
		&& ::IsValid(StylizedMaterial)
		&& FABTSStylizedMaterialContract::IsFamilyValid(Family);
}

FABTSStylizedMaterialOverrideRegistry::FABTSStylizedMaterialOverrideRegistry()
	: Impl(MakeUnique<FImpl>())
{
}

FABTSStylizedMaterialOverrideRegistry::~FABTSStylizedMaterialOverrideRegistry()
{
	RestoreAll();
}

void FABTSStylizedMaterialOverrideRegistry::Apply(
	TConstArrayView<FABTSStylizedMaterialSlotBinding> DesiredBindings,
	const bool bStyleEnabled)
{
	Impl->Apply(DesiredBindings, bStyleEnabled);
}

void FABTSStylizedMaterialOverrideRegistry::RestoreAll()
{
	if (Impl)
	{
		Impl->RestoreAll();
	}
}

int32 FABTSStylizedMaterialOverrideRegistry::Num() const
{
	return Impl ? Impl->Num() : 0;
}

int32 FABTSStylizedMaterialOverrideRegistry::GetConflictCount() const
{
	return Impl ? Impl->GetConflictCount() : 0;
}

int32 FABTSStylizedMaterialOverrideRegistry::GetRejectedBindingCount() const
{
	return Impl ? Impl->GetRejectedBindingCount() : 0;
}

void FABTSStylizedMaterialOverrideRegistry::AddReferencedObjects(
	FReferenceCollector& Collector)
{
	if (Impl)
	{
		Impl->AddReferencedObjects(Collector);
	}
}

FString FABTSStylizedMaterialOverrideRegistry::GetReferencerName() const
{
	return TEXT("FABTSStylizedMaterialOverrideRegistry");
}
