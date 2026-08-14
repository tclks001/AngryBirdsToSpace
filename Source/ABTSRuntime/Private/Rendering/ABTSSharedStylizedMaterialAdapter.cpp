// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/ABTSSharedStylizedMaterialAdapter.h"

#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UObjectGlobals.h"

namespace ABTSSharedStylizedMaterialAdapterPrivate
{
	struct FCatalogEntry
	{
		const TCHAR* SourcePath;
		const TCHAR* StylizedPath;
		EABTSStylizedMaterialFamily Family;
	};

	// Append-only within T3-A2. Source paths are accepted shared-asset identities,
	// not slot-number guesses. Reinforced remains Organic because its single
	// texture slot is predominantly wrapped wood; Steel is the Metal tier.
	constexpr FCatalogEntry Catalog[] =
	{
		{TEXT("/Game/CuteBird/Materials/CuteBirdColor_Materials/M_CuteBird_12.M_CuteBird_12"), TEXT("/Game/Toon/Shared/Birds/MI_ABTS_Toon_BirdBody_Red.MI_ABTS_Toon_BirdBody_Red"), EABTSStylizedMaterialFamily::CuteBirdBody},
		{TEXT("/Game/CuteBird/Materials/CuteBirdColor_Materials/M_CuteBird_3.M_CuteBird_3"), TEXT("/Game/Toon/Shared/Birds/MI_ABTS_Toon_BirdBody_Blue.MI_ABTS_Toon_BirdBody_Blue"), EABTSStylizedMaterialFamily::CuteBirdBody},
		{TEXT("/Game/CuteBird/Materials/CuteBirdColor_Materials/M_CuteBird_10.M_CuteBird_10"), TEXT("/Game/Toon/Shared/Birds/MI_ABTS_Toon_BirdBody_Yellow.MI_ABTS_Toon_BirdBody_Yellow"), EABTSStylizedMaterialFamily::CuteBirdBody},
		{TEXT("/Game/CuteBird/Materials/CuteBirdColor_Materials/M_CuteBird_16.M_CuteBird_16"), TEXT("/Game/Toon/Shared/Birds/MI_ABTS_Toon_BirdBody_Black.MI_ABTS_Toon_BirdBody_Black"), EABTSStylizedMaterialFamily::CuteBirdBody},
		{TEXT("/Game/CuteBird/Materials/CuteBirdColor_Materials/M_CuteBird_0.M_CuteBird_0"), TEXT("/Game/Toon/Shared/Birds/MI_ABTS_Toon_BirdBody_White.MI_ABTS_Toon_BirdBody_White"), EABTSStylizedMaterialFamily::CuteBirdBody},
		{TEXT("/Game/CuteBird/Materials/Face_Materials/M_Dino_face_23.M_Dino_face_23"), TEXT("/Game/Toon/Shared/Birds/MI_ABTS_Toon_BirdFace_Red.MI_ABTS_Toon_BirdFace_Red"), EABTSStylizedMaterialFamily::CuteBirdFace},
		{TEXT("/Game/CuteBird/Materials/Face_Materials/M_Dino_face_3.M_Dino_face_3"), TEXT("/Game/Toon/Shared/Birds/MI_ABTS_Toon_BirdFace_Blue.MI_ABTS_Toon_BirdFace_Blue"), EABTSStylizedMaterialFamily::CuteBirdFace},
		{TEXT("/Game/CuteBird/Materials/Face_Materials/M_Dino_face_6.M_Dino_face_6"), TEXT("/Game/Toon/Shared/Birds/MI_ABTS_Toon_BirdFace_Yellow.MI_ABTS_Toon_BirdFace_Yellow"), EABTSStylizedMaterialFamily::CuteBirdFace},
		{TEXT("/Game/CuteBird/Materials/Face_Materials/M_Dino_face_17.M_Dino_face_17"), TEXT("/Game/Toon/Shared/Birds/MI_ABTS_Toon_BirdFace_Black.MI_ABTS_Toon_BirdFace_Black"), EABTSStylizedMaterialFamily::CuteBirdFace},
		{TEXT("/Game/CuteBird/Materials/Face_Materials/M_Dino_face_1.M_Dino_face_1"), TEXT("/Game/Toon/Shared/Birds/MI_ABTS_Toon_BirdFace_White.MI_ABTS_Toon_BirdFace_White"), EABTSStylizedMaterialFamily::CuteBirdFace},
		{TEXT("/Game/StaticMesh/Stake/Twig/MI_Stake_Twig.MI_Stake_Twig"), TEXT("/Game/Toon/Shared/Slingshot/MI_ABTS_Toon_Slingshot_Stake_Twig.MI_ABTS_Toon_Slingshot_Stake_Twig"), EABTSStylizedMaterialFamily::SlingshotOrganic},
		{TEXT("/Game/StaticMesh/Cord/Twig/MI_Cord_Twig.MI_Cord_Twig"), TEXT("/Game/Toon/Shared/Slingshot/MI_ABTS_Toon_Slingshot_Cord_Twig.MI_ABTS_Toon_Slingshot_Cord_Twig"), EABTSStylizedMaterialFamily::SlingshotOrganic},
		{TEXT("/Game/StaticMesh/Pouch/Twig/MI_Pouch_Twig.MI_Pouch_Twig"), TEXT("/Game/Toon/Shared/Slingshot/MI_ABTS_Toon_Slingshot_Pouch_Twig.MI_ABTS_Toon_Slingshot_Pouch_Twig"), EABTSStylizedMaterialFamily::SlingshotOrganic},
		{TEXT("/Game/StaticMesh/Stake/Simple/MI_Stake_Simple.MI_Stake_Simple"), TEXT("/Game/Toon/Shared/Slingshot/MI_ABTS_Toon_Slingshot_Stake_Simple.MI_ABTS_Toon_Slingshot_Stake_Simple"), EABTSStylizedMaterialFamily::SlingshotOrganic},
		{TEXT("/Game/StaticMesh/Cord/Simple/MI_Cord_Simple.MI_Cord_Simple"), TEXT("/Game/Toon/Shared/Slingshot/MI_ABTS_Toon_Slingshot_Cord_Simple.MI_ABTS_Toon_Slingshot_Cord_Simple"), EABTSStylizedMaterialFamily::SlingshotOrganic},
		{TEXT("/Game/StaticMesh/Pouch/Simple/MI_Pouch_Simple.MI_Pouch_Simple"), TEXT("/Game/Toon/Shared/Slingshot/MI_ABTS_Toon_Slingshot_Pouch_Simple.MI_ABTS_Toon_Slingshot_Pouch_Simple"), EABTSStylizedMaterialFamily::SlingshotOrganic},
		{TEXT("/Game/StaticMesh/Stake/Reinforced/MI_Stack_Reinforced.MI_Stack_Reinforced"), TEXT("/Game/Toon/Shared/Slingshot/MI_ABTS_Toon_Slingshot_Stake_Reinforced.MI_ABTS_Toon_Slingshot_Stake_Reinforced"), EABTSStylizedMaterialFamily::SlingshotOrganic},
		{TEXT("/Game/StaticMesh/Cord/Reinforced/MI_Cord_Reinforced.MI_Cord_Reinforced"), TEXT("/Game/Toon/Shared/Slingshot/MI_ABTS_Toon_Slingshot_Cord_Reinforced.MI_ABTS_Toon_Slingshot_Cord_Reinforced"), EABTSStylizedMaterialFamily::SlingshotOrganic},
		{TEXT("/Game/StaticMesh/Pouch/Reinforced/MI_Pouch_Reinforced.MI_Pouch_Reinforced"), TEXT("/Game/Toon/Shared/Slingshot/MI_ABTS_Toon_Slingshot_Pouch_Reinforced.MI_ABTS_Toon_Slingshot_Pouch_Reinforced"), EABTSStylizedMaterialFamily::SlingshotOrganic},
		{TEXT("/Game/StaticMesh/Stake/Steel/MI_Stack_Steel.MI_Stack_Steel"), TEXT("/Game/Toon/Shared/Slingshot/MI_ABTS_Toon_Slingshot_Stake_Steel.MI_ABTS_Toon_Slingshot_Stake_Steel"), EABTSStylizedMaterialFamily::SlingshotMetal},
		{TEXT("/Game/StaticMesh/Cord/Steel/MI_Cord_Steel.MI_Cord_Steel"), TEXT("/Game/Toon/Shared/Slingshot/MI_ABTS_Toon_Slingshot_Cord_Steel.MI_ABTS_Toon_Slingshot_Cord_Steel"), EABTSStylizedMaterialFamily::SlingshotMetal},
		{TEXT("/Game/StaticMesh/Pouch/Steel/MI_Pouch_Steel.MI_Pouch_Steel"), TEXT("/Game/Toon/Shared/Slingshot/MI_ABTS_Toon_Slingshot_Pouch_Steel.MI_ABTS_Toon_Slingshot_Pouch_Steel"), EABTSStylizedMaterialFamily::SlingshotMetal},
	};

	const FCatalogEntry* FindEntry(const FString& SourcePath)
	{
		for (const FCatalogEntry& Entry : Catalog)
		{
			if (SourcePath.Equals(Entry.SourcePath, ESearchCase::CaseSensitive)
				|| SourcePath.Equals(Entry.StylizedPath, ESearchCase::CaseSensitive))
			{
				return &Entry;
			}
		}
		return nullptr;
	}
}

int32 FABTSSharedStylizedMaterialAdapter::GatherActorBindings(
	const AActor& Actor,
	TArray<FABTSStylizedMaterialSlotBinding>& OutBindings)
{
	TInlineComponentArray<UPrimitiveComponent*> Primitives;
	Actor.GetComponents(Primitives);
	return GatherPrimitiveBindings(Primitives, OutBindings);
}

int32 FABTSSharedStylizedMaterialAdapter::GatherPrimitiveBindings(
	const TConstArrayView<UPrimitiveComponent*> Primitives,
	TArray<FABTSStylizedMaterialSlotBinding>& OutBindings)
{
	const int32 InitialCount = OutBindings.Num();
	for (UPrimitiveComponent* Component : Primitives)
	{
		if (!IsValid(Component))
		{
			continue;
		}
		for (int32 SlotIndex = 0; SlotIndex < Component->GetNumMaterials(); ++SlotIndex)
		{
			UMaterialInterface* SourceMaterial = Component->GetMaterial(SlotIndex);
			UMaterialInterface* StylizedMaterial = nullptr;
			EABTSStylizedMaterialFamily Family = EABTSStylizedMaterialFamily::None;
			if (!IsValid(SourceMaterial)
				|| !TryResolveMaterial(*SourceMaterial, StylizedMaterial, Family))
			{
				continue;
			}

			FABTSStylizedMaterialSlotBinding& Binding = OutBindings.AddDefaulted_GetRef();
			Binding.Component = Component;
			Binding.MaterialSlotIndex = SlotIndex;
			Binding.StylizedMaterial = StylizedMaterial;
			Binding.Family = Family;
		}
	}
	return OutBindings.Num() - InitialCount;
}

bool FABTSSharedStylizedMaterialAdapter::TryResolveMaterial(
	const UMaterialInterface& SourceMaterial,
	UMaterialInterface*& OutStylizedMaterial,
	EABTSStylizedMaterialFamily& OutFamily)
{
	OutStylizedMaterial = nullptr;
	OutFamily = EABTSStylizedMaterialFamily::None;
	using namespace ABTSSharedStylizedMaterialAdapterPrivate;
	const FCatalogEntry* Entry = FindEntry(SourceMaterial.GetPathName());
	if (Entry == nullptr)
	{
		return false;
	}
	UMaterialInterface* Candidate = LoadObject<UMaterialInterface>(nullptr, Entry->StylizedPath);
	if (!IsValid(Candidate))
	{
		return false;
	}
	OutStylizedMaterial = Candidate;
	OutFamily = Entry->Family;
	return true;
}

int32 FABTSSharedStylizedMaterialAdapter::PreloadCatalogMaterials(
	TArray<UMaterialInterface*>& OutMaterials,
	int32& OutFailureCount)
{
	OutMaterials.Reset();
	OutFailureCount = 0;
	using namespace ABTSSharedStylizedMaterialAdapterPrivate;
	for (const FCatalogEntry& Entry : Catalog)
	{
		UMaterialInterface* Candidate =
			LoadObject<UMaterialInterface>(nullptr, Entry.StylizedPath);
		if (!IsValid(Candidate))
		{
			++OutFailureCount;
			continue;
		}
		OutMaterials.AddUnique(Candidate);
	}
	return OutMaterials.Num();
}

int32 FABTSSharedStylizedMaterialAdapter::GetCatalogEntryCount()
{
	return UE_ARRAY_COUNT(ABTSSharedStylizedMaterialAdapterPrivate::Catalog);
}

uint32 FABTSSharedStylizedMaterialAdapter::GetCatalogHash()
{
	uint32 Hash = 0;
	for (const ABTSSharedStylizedMaterialAdapterPrivate::FCatalogEntry& Entry
		: ABTSSharedStylizedMaterialAdapterPrivate::Catalog)
	{
		Hash = HashCombineFast(Hash, GetTypeHash(FString(Entry.SourcePath)));
		Hash = HashCombineFast(Hash, GetTypeHash(FString(Entry.StylizedPath)));
		Hash = HashCombineFast(Hash, GetTypeHash(static_cast<uint8>(Entry.Family)));
	}
	return Hash;
}
