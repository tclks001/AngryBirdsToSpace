// Copyright Epic Games, Inc. All Rights Reserved.

#include "Crafting/ABTSCraftingSystem.h"

#include "ABTSRuntime.h"
#include "Crafting/ABTSCraftingCatalog.h"
#include "Crafting/ABTSCraftingStation.h"
#include "EngineUtils.h"
#include "Inventory/ABTSInventoryComponent.h"
#include "Party/ABTSBirdParty.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "Player/ABTSM5PlayerController.h"
#include "TimerManager.h"

AABTSCraftingSystem::AABTSCraftingSystem()
{
	PrimaryActorTick.bCanEverTick = false;
	Inventory = CreateDefaultSubobject<UABTSInventoryComponent>(TEXT("PartyInventory"));
	Catalog = CreateDefaultSubobject<UABTSCraftingCatalog>(TEXT("CraftingCatalog"));
}

void AABTSCraftingSystem::BeginPlay()
{
	Super::BeginPlay();
	// M5 asset-free smoke test: opening/closing through the same controller path
	// proves that the modal UI state can be reached in a fresh game process.
	if (FParse::Param(FCommandLine::Get(), TEXT("ABTSM5Smoke")))
	{
		GetWorldTimerManager().SetTimerForNextTick([WeakThis = TWeakObjectPtr<AABTSCraftingSystem>(this)]()
		{
			if (!WeakThis.IsValid() || WeakThis->GetWorld() == nullptr) return;
			if (AABTSM5PlayerController* Controller = Cast<AABTSM5PlayerController>(WeakThis->GetWorld()->GetFirstPlayerController()))
			{
				Controller->OpenCraftingInterface();
				Controller->CloseCraftingInterface();
			}
		});
	}
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M5][Inventory] Ready Stacks=%d Hotbar=%d Recipes=%d PrototypeSeed=0"),
		Inventory->GetOrderedStacks().Num(), Inventory->GetHotbarSlotCount(), Catalog->GetRecipes().Num());
}

AABTSBirdParty* AABTSCraftingSystem::FindParty() const
{
	if (Party.IsValid()) return Party.Get();
	for (TActorIterator<AABTSBirdParty> It(GetWorld()); It; ++It)
	{
		Party = *It;
		return Party.Get();
	}
	return nullptr;
}

bool AABTSCraftingSystem::IsRedBirdControlled() const
{
	const AABTSBirdParty* ResolvedParty = FindParty();
	return ResolvedParty != nullptr && ResolvedParty->GetControlledBirdId() == EABTSBirdId::Red;
}

bool AABTSCraftingSystem::IsStationAvailable(const EABTSCraftingStationType StationType) const
{
	if (StationType == EABTSCraftingStationType::None) return true;
	const AABTSBirdParty* ResolvedParty = FindParty();
	const AABTSM25BirdCharacter* ControlledBird = ResolvedParty ? ResolvedParty->GetControlledBird() : nullptr;
	if (ControlledBird == nullptr) return false;
	for (TActorIterator<AABTSCraftingStation> It(GetWorld()); It; ++It)
	{
		if (It->GetStationType() == StationType && It->IsWithinUseRange(ControlledBird->GetActorLocation())) return true;
	}
	return false;
}

bool AABTSCraftingSystem::Craft(const FName RecipeId, const int32 CraftCount)
{
	const bool bWorkbench = IsStationAvailable(EABTSCraftingStationType::Workbench);
	const bool bFurnace = IsStationAvailable(EABTSCraftingStationType::Furnace);
	const bool bSuccess = Catalog->Craft(
		RecipeId, CraftCount, *Inventory, IsRedBirdControlled(), bWorkbench, bFurnace);
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M5][Craft] Recipe=%s Count=%d Success=%d Red=%d Workbench=%d Furnace=%d"),
		*RecipeId.ToString(), CraftCount, bSuccess ? 1 : 0, IsRedBirdControlled() ? 1 : 0,
		bWorkbench ? 1 : 0, bFurnace ? 1 : 0);
	return bSuccess;
}
