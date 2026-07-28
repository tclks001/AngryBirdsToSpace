// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM51WorldSystem.h"

#include "ABTSRuntime.h"
#include "Crafting/ABTSCraftingStation.h"
#include "Crafting/ABTSCraftingSystem.h"
#include "EngineUtils.h"
#include "Inventory/ABTSInventoryComponent.h"
#include "Party/ABTSBirdParty.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "Terrain/ABTSM3Planet.h"
#include "World/ABTSM51WorldActors.h"

AABTSM51WorldSystem::AABTSM51WorldSystem()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.08f;
	PickupClass = AABTSM51PickupItem::StaticClass();
	DirtHoleClass = AABTSM51SlingshotDirtHole::StaticClass();
	StakeClass = AABTSM51SlingshotStake::StaticClass();
	CordClass = AABTSM51SlingshotCord::StaticClass();
	CraftingStationClass = AABTSCraftingStation::StaticClass();
}

void AABTSM51WorldSystem::BeginPlay()
{
	Super::BeginPlay();
	bInitialized = InitializeWorldContent();
}

void AABTSM51WorldSystem::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bInitialized) bInitialized = InitializeWorldContent();
	if (bInitialized) CollectNearbyPickups();
}

bool AABTSM51WorldSystem::InitializeWorldContent()
{
	if (!Planet.IsValid())
	{
		for (TActorIterator<AABTSM3Planet> It(GetWorld()); It; ++It)
		{
			if (It->IsPlanetReady()) { Planet = *It; break; }
		}
	}
	if (!Planet.IsValid() || FindCraftingSystem() == nullptr) return false;
	SpawnSlingshotHoles();
	SpawnSdfPickups();
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M5.1] World ready Pickups=%d OccupiedCells=%d"), Pickups.Num(), OccupiedCells.Num());
	return true;
}

AABTSCraftingSystem* AABTSM51WorldSystem::FindCraftingSystem() const
{
	if (CraftingSystem.IsValid()) return CraftingSystem.Get();
	for (TActorIterator<AABTSCraftingSystem> It(GetWorld()); It; ++It)
	{
		CraftingSystem = *It;
		return CraftingSystem.Get();
	}
	return nullptr;
}

bool AABTSM51WorldSystem::QueryCellTransform(const int32 CellId, const float SurfaceOffsetCM, FTransform& OutTransform) const
{
	if (!Planet.IsValid() || !Planet->LogicalCells.IsValidIndex(CellId)) return false;
	const FVector Direction = Planet->LogicalCells[CellId].UnitCenter;
	FVector Position;
	FVector Normal;
	float Radius = 0.0f;
	int32 ResolvedCell = INDEX_NONE;
	if (!Planet->QuerySurface(Direction, Position, Normal, Radius, ResolvedCell)) return false;
	FVector Forward = FVector::VectorPlaneProject(FVector::ForwardVector, Normal).GetSafeNormal();
	if (Forward.IsNearlyZero()) Forward = FVector::VectorPlaneProject(FVector::RightVector, Normal).GetSafeNormal();
	OutTransform = FTransform(FRotationMatrix::MakeFromXZ(Forward, Normal).ToQuat(), Position + Normal * SurfaceOffsetCM);
	return true;
}

bool AABTSM51WorldSystem::GetFinaleSpaceSlots(
	AABTSM51SlingshotDirtHole*& OutLeft,
	AABTSM51SlingshotDirtHole*& OutRight) const
{
	OutLeft = FinaleLeftSlot.Get();
	OutRight = FinaleRightSlot.Get();
	return OutLeft != nullptr
		&& OutRight != nullptr
		&& OutLeft != OutRight
		&& OutLeft->IsFinaleSpaceSlot()
		&& OutRight->IsFinaleSpaceSlot()
		&& OutLeft->GetSlotSide() == EABTSSlingshotSlotSide::Left
		&& OutRight->GetSlotSide() == EABTSSlingshotSlotSide::Right
		&& OutLeft->GetSlotPairId() != INDEX_NONE
		&& OutLeft->GetSlotPairId() == OutRight->GetSlotPairId();
}

void AABTSM51WorldSystem::SpawnSlingshotHoles()
{
	int32 StandardHoleCount = 0;
	for (const FABTSM3TaskNode& Task : Planet->GetGeneratedTasks())
	{
		if (Task.Type != EABTSM3TaskType::SlingshotRange || !Planet->LogicalCells.IsValidIndex(Task.SeedCellId)) continue;
		const int32 CellA = Task.SeedCellId;
		int32 CellB = INDEX_NONE;
		for (const int32 Neighbor : Planet->LogicalCells[CellA].NeighborCellIds)
		{
			if (Task.CellIds.Contains(Neighbor) && Planet->GetGeneratedCellStates().IsValidIndex(Neighbor)
				&& !Planet->GetGeneratedCellStates()[Neighbor].bWater)
			{
				CellB = Neighbor;
				break;
			}
		}
		if (CellB == INDEX_NONE) continue;
		for (const int32 CellId : {CellA, CellB})
		{
			FTransform Transform;
			if (!QueryCellTransform(CellId, 4.0f, Transform)) continue;
			AABTSM51SlingshotDirtHole* Hole = GetWorld()->SpawnActor<AABTSM51SlingshotDirtHole>(DirtHoleClass, Transform);
			if (Hole)
			{
				Hole->InitializeHole(CellId);
				OccupiedCells.Add(CellId);
				++StandardHoleCount;
			}
		}
	}

	int32 FinaleHoleCount = 0;
	const FABTSM110FinaleLocalFrame& FinaleFrame = Planet->GetFinaleLaunchFrame();
	if (!FinaleFrame.IsUsable())
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M11.0][FinaleSlots] Spawn rejected: M3 finale frame is not usable."));
	}
	else
	{
		const auto SpawnFinaleSlot = [this, &FinaleFrame, &FinaleHoleCount](
			const FVector& WorldLocation,
			const EABTSSlingshotSlotSide Side) -> AABTSM51SlingshotDirtHole*
		{
			const FTransform Transform(FinaleFrame.WorldTransform.GetRotation(), WorldLocation);
			AABTSM51SlingshotDirtHole* Hole =
				GetWorld()->SpawnActor<AABTSM51SlingshotDirtHole>(DirtHoleClass, Transform);
			if (Hole == nullptr)
			{
				return nullptr;
			}
			Hole->InitializeFinaleSpaceSlot(FinaleFrame.AnchorCellId, FinaleFrame.SlotPairId, Side);
			++FinaleHoleCount;
			return Hole;
		};
		FinaleLeftSlot = SpawnFinaleSlot(
			FinaleFrame.LeftSlotWorldLocation,
			EABTSSlingshotSlotSide::Left);
		FinaleRightSlot = SpawnFinaleSlot(
			FinaleFrame.RightSlotWorldLocation,
			EABTSSlingshotSlotSide::Right);
		AABTSM51SlingshotDirtHole* LeftSlot = FinaleLeftSlot.Get();
		AABTSM51SlingshotDirtHole* RightSlot = FinaleRightSlot.Get();
		if (LeftSlot == nullptr || RightSlot == nullptr)
		{
			if (LeftSlot != nullptr)
			{
				LeftSlot->Destroy();
			}
			if (RightSlot != nullptr)
			{
				RightSlot->Destroy();
			}
			FinaleLeftSlot.Reset();
			FinaleRightSlot.Reset();
			FinaleHoleCount = 0;
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M11.0][FinaleSlots] Atomic pair spawn failed; no terminal slot was retained."));
		}
		else
		{
			OccupiedCells.Add(FinaleFrame.AnchorCellId);
		}
	}
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M11.0][SlingshotSlots] Standard=%d Finale=%d Pair=%d AnchorCell=%d"),
		StandardHoleCount,
		FinaleHoleCount,
		FinaleFrame.SlotPairId,
		FinaleFrame.AnchorCellId);
}

void AABTSM51WorldSystem::SpawnSdfPickups()
{
	const TArray<FABTSM3CellState>& States = Planet->GetGeneratedCellStates();
	const int32 Seed = Planet->WorldSeed;
	int32 BranchCount = 0;
	int32 StoneCount = 0;
	int32 FiberCount = 0;
	int32 WoodCount = 0;
	int32 BranchCandidateCount = 0;
	const FABTSM3TaskNode* StartTask = Planet->GetGeneratedTasks().FindByPredicate([](const FABTSM3TaskNode& Task)
	{
		return Task.Type == EABTSM3TaskType::Start;
	});
	if (StartTask == nullptr || StartTask->CellIds.IsEmpty()) return;
	const auto ResolveStartPatchCenter = [this, StartTask](const int32 Numerator)
	{
		const int32 Index = FMath::Clamp(StartTask->CellIds.Num() * Numerator / 4, 0, StartTask->CellIds.Num() - 1);
		const int32 CellId = StartTask->CellIds[Index];
		return Planet->LogicalCells.IsValidIndex(CellId)
			? Planet->LogicalCells[CellId].UnitCenter
			: Planet->LogicalCells[StartTask->SeedCellId].UnitCenter;
	};
	const FVector CenterA = Planet->LogicalCells[StartTask->SeedCellId].UnitCenter;
	const FVector CenterB = ResolveStartPatchCenter(1);
	const FVector CenterC = ResolveStartPatchCenter(3);
	for (int32 CellId = 0; CellId < States.Num(); ++CellId)
	{
		if (Pickups.Num() >= MaxPickupActorCount) break;
		const FABTSM3CellState& State = States[CellId];
		if (CellId % FMath::Max(1, ResourceCellStride) != 0 || State.bWater || State.bBuildingAnchor || IsCellOccupied(CellId)) continue;
		const FVector Direction = Planet->LogicalCells[CellId].UnitCenter;
		// Three deterministic low-frequency spherical resource patches. The
		// angular distance minus patch radius is a signed spherical distance field,
		// sampled only at CellTopo centers.
		const float DotA = FVector::DotProduct(Direction, CenterA);
		const float DotB = FVector::DotProduct(Direction, CenterB);
		const float DotC = FVector::DotProduct(Direction, CenterC);
		const float BestDot = FMath::Max3(DotA, DotB, DotC);
		const float SignedDistanceRadians = FMath::Acos(FMath::Clamp(BestDot, -1.0f, 1.0f)) - ResourcePatchRadiusRadians;
		const uint32 Hash = HashCombineFast(GetTypeHash(Seed), GetTypeHash(CellId));
		const float BoundaryJitterRadians = (static_cast<float>(Hash & 1023u) / 1023.0f - 0.5f) * 0.08f;
		if (SignedDistanceRadians > BoundaryJitterRadians) continue;
		EABTSItemId ItemId = DotA >= DotB && DotA >= DotC
			? EABTSItemId::Branch
			: (DotB >= DotC ? EABTSItemId::Stone : EABTSItemId::PlantFiber);
		if (ItemId == EABTSItemId::Branch)
		{
			++BranchCandidateCount;
			if (BranchCandidateCount % 6 == 0) ItemId = EABTSItemId::Wood;
		}
		FTransform Transform;
		if (!QueryCellTransform(CellId, 24.0f, Transform)) continue;
		AABTSM51PickupItem* Pickup = GetWorld()->SpawnActor<AABTSM51PickupItem>(PickupClass, Transform);
		if (Pickup)
		{
			Pickup->InitializePickup(ItemId, 1 + static_cast<int32>((Hash >> 12u) & 1u), CellId);
			Pickups.Add(Pickup);
			BranchCount += ItemId == EABTSItemId::Branch ? 1 : 0;
			StoneCount += ItemId == EABTSItemId::Stone ? 1 : 0;
			FiberCount += ItemId == EABTSItemId::PlantFiber ? 1 : 0;
			WoodCount += ItemId == EABTSItemId::Wood ? 1 : 0;
		}
	}
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M5.1][PickupPCG] Spawned=%d Branch=%d Stone=%d Fiber=%d Wood=%d PatchRadiusRad=%.3f Stride=%d Cap=%d"),
		Pickups.Num(), BranchCount, StoneCount, FiberCount, WoodCount,
		ResourcePatchRadiusRadians, ResourceCellStride, MaxPickupActorCount);
}

void AABTSM51WorldSystem::CollectNearbyPickups()
{
	AABTSCraftingSystem* System = FindCraftingSystem();
	AABTSBirdParty* Party = System ? System->FindParty() : nullptr;
	AABTSM25BirdCharacter* Bird = Party ? Party->GetControlledBird() : nullptr;
	UABTSInventoryComponent* Inventory = System ? System->GetInventory() : nullptr;
	if (Bird == nullptr || Inventory == nullptr) return;
	for (int32 Index = Pickups.Num() - 1; Index >= 0; --Index)
	{
		AABTSM51PickupItem* Pickup = Pickups[Index].Get();
		if (Pickup == nullptr) { Pickups.RemoveAtSwap(Index); continue; }
		if (FVector::DistSquared(Bird->GetActorLocation(), Pickup->GetActorLocation()) > FMath::Square(AutoPickupRadiusCM)) continue;
		Inventory->AddItem(Pickup->GetItemId(), Pickup->GetQuantity());
		UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M5.1][Pickup] Item=%s Qty=%d Cell=%d"),
			*ABTSGetItemFallbackLabel(Pickup->GetItemId()), Pickup->GetQuantity(), Pickup->GetCellId());
		Pickup->Destroy();
		Pickups.RemoveAtSwap(Index);
	}
}

int32 AABTSM51WorldSystem::SelectPlacementCell(const FVector& UnitDirection) const
{
	if (!Planet.IsValid()) return INDEX_NONE;
	int32 BestCell = INDEX_NONE;
	float BestDot = -1.0f;
	const TArray<FABTSM3CellState>& States = Planet->GetGeneratedCellStates();
	for (int32 CellId = 0; CellId < States.Num(); ++CellId)
	{
		if (!States[CellId].bBuildable || States[CellId].bWater || IsCellOccupied(CellId)) continue;
		const float Dot = FVector::DotProduct(UnitDirection, Planet->LogicalCells[CellId].UnitCenter);
		if (Dot > BestDot) { BestDot = Dot; BestCell = CellId; }
	}
	return BestCell;
}

int32 AABTSM51WorldSystem::SelectDeveloperStakeCell(const FVector& UnitDirection) const
{
	if (!Planet.IsValid()) return INDEX_NONE;
	int32 BestCell = INDEX_NONE;
	float BestDot = -1.0f;
	for (int32 CellId = 0; CellId < Planet->LogicalCells.Num(); ++CellId)
	{
		if (IsCellOccupied(CellId)) continue;
		const float Dot = FVector::DotProduct(UnitDirection, Planet->LogicalCells[CellId].UnitCenter);
		if (Dot > BestDot) { BestDot = Dot; BestCell = CellId; }
	}
	return BestCell;
}

bool AABTSM51WorldSystem::IsCellOccupied(const int32 CellId) const
{
	return OccupiedCells.Contains(CellId);
}

void AABTSM51WorldSystem::LogPlaceFailure(const TCHAR* Reason) const
{
	UE_LOG(LogABTSRuntime, Warning, TEXT("[ABTS][M5.1][Place] Rejected Reason=%s"), Reason);
}

bool AABTSM51WorldSystem::PlaceHeldToolAtAim(APlayerController& Controller)
{
	AABTSCraftingSystem* System = FindCraftingSystem();
	UABTSInventoryComponent* Inventory = System ? System->GetInventory() : nullptr;
	EABTSItemId Held;
	if (Inventory == nullptr || !Inventory->GetHeldItem(Held) || !ABTSIsPlaceableTool(Held)) return false;
	FVector Origin;
	FVector Direction;
	if (!Controller.DeprojectMousePositionToWorld(Origin, Direction)) { LogPlaceFailure(TEXT("NoAimRay")); return false; }
	const FVector PlanetCenter = Planet->GetPlanetCenterWorld();
	FVector AimPoint = Origin + Direction * PlacementTraceDistanceCM;
	FHitResult SurfaceHit;
	if (Controller.GetHitResultUnderCursor(ECC_Visibility, false, SurfaceHit) && SurfaceHit.bBlockingHit)
	{
		AimPoint = SurfaceHit.ImpactPoint;
	}
	const int32 CellId = SelectPlacementCell((AimPoint - PlanetCenter).GetSafeNormal());
	if (CellId == INDEX_NONE) { LogPlaceFailure(TEXT("NoBuildableCell")); return false; }
	const float SnapDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(
		(AimPoint - PlanetCenter).GetSafeNormal(), Planet->LogicalCells[CellId].UnitCenter), -1.0f, 1.0f)));
	if (SnapDegrees > MaxPlacementSnapDegrees) { LogPlaceFailure(TEXT("SnapTooFar")); return false; }
	FTransform Transform;
	if (!QueryCellTransform(CellId, 23.0f, Transform)) { LogPlaceFailure(TEXT("SurfaceQuery")); return false; }
	if (Controller.GetPawn() == nullptr || FVector::Distance(Controller.GetPawn()->GetActorLocation(), Transform.GetLocation()) > PlacementReachCM)
	{
		LogPlaceFailure(TEXT("OutOfReach"));
		return false;
	}
	AABTSCraftingStation* Station = GetWorld()->SpawnActor<AABTSCraftingStation>(CraftingStationClass, Transform);
	if (Station == nullptr) { LogPlaceFailure(TEXT("SpawnFailed")); return false; }
	Station->SetStationType(Held == EABTSItemId::WorkbenchKit ? EABTSCraftingStationType::Workbench : EABTSCraftingStationType::Furnace);
	Station->SetCellId(CellId);
	OccupiedCells.Add(CellId);
	Inventory->RemoveItem(Held, 1);
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M5.1][Place] Tool=%s Cell=%d"), *ABTSGetItemFallbackLabel(Held), CellId);
	return true;
}

bool AABTSM51WorldSystem::PlaceHeldStakeAtAim(APlayerController& Controller)
{
	if (!bAllowDeveloperAnyCellStakePlacement) return false;
	AABTSCraftingSystem* System = FindCraftingSystem();
	UABTSInventoryComponent* Inventory = System ? System->GetInventory() : nullptr;
	EABTSItemId Held;
	if (Inventory == nullptr || !Inventory->GetHeldItem(Held) || !ABTSIsSlingshotStake(Held)) return false;
	EABTSSlingshotTier HeldTier = EABTSSlingshotTier::Simple;
	if (!ABTSTryResolveSlingshotPartTier(Held, HeldTier) || HeldTier == EABTSSlingshotTier::Space)
	{
		LogPlaceFailure(TEXT("SpaceStakeRequiresFinaleSlot"));
		return false;
	}
	if (!Planet.IsValid()) { LogPlaceFailure(TEXT("NoPlanet")); return false; }
	FVector Origin;
	FVector Direction;
	if (!Controller.DeprojectMousePositionToWorld(Origin, Direction)) { LogPlaceFailure(TEXT("NoAimRay")); return false; }
	const FVector PlanetCenter = Planet->GetPlanetCenterWorld();
	FVector AimPoint = Origin + Direction * PlacementTraceDistanceCM;
	FHitResult SurfaceHit;
	if (Controller.GetHitResultUnderCursor(ECC_Visibility, false, SurfaceHit) && SurfaceHit.bBlockingHit)
	{
		AimPoint = SurfaceHit.ImpactPoint;
	}
	const int32 CellId = SelectDeveloperStakeCell((AimPoint - PlanetCenter).GetSafeNormal());
	if (CellId == INDEX_NONE) { LogPlaceFailure(TEXT("NoUnoccupiedCell")); return false; }
	FTransform Transform;
	const FABTSSlingshotVisualPreset Preset = ABTSMakeDefaultSlingshotVisualPreset(HeldTier);
	if (!QueryCellTransform(CellId, Preset.StakeHeightCM * 0.5f, Transform)) { LogPlaceFailure(TEXT("SurfaceQuery")); return false; }
	if (Controller.GetPawn() == nullptr || FVector::Distance(Controller.GetPawn()->GetActorLocation(), Transform.GetLocation()) > PlacementReachCM)
	{
		LogPlaceFailure(TEXT("OutOfReach"));
		return false;
	}
	AABTSM51SlingshotStake* Stake = GetWorld()->SpawnActor<AABTSM51SlingshotStake>(StakeClass, Transform);
	if (Stake == nullptr) { LogPlaceFailure(TEXT("SpawnFailed")); return false; }
	Stake->InitializeStake(Held, CellId, Planet->LogicalCells[CellId].UnitCenter);
	OccupiedCells.Add(CellId);
	Inventory->RemoveItem(Held, 1);
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M5.1][DebugStake] Installed=%s Cell=%d AllowAnyCell=1"),
		*ABTSGetItemFallbackLabel(Held), CellId);
	return true;
}

bool AABTSM51WorldSystem::InstallHeldStake(AABTSM51SlingshotDirtHole& Hole)
{
	AABTSCraftingSystem* System = FindCraftingSystem();
	UABTSInventoryComponent* Inventory = System ? System->GetInventory() : nullptr;
	EABTSItemId Held;
	if (Inventory == nullptr || !Inventory->GetHeldItem(Held) || !ABTSIsSlingshotStake(Held) || Hole.IsOccupied()) return false;
	EABTSSlingshotTier HeldTier = EABTSSlingshotTier::Simple;
	if (!ABTSTryResolveSlingshotPartTier(Held, HeldTier))
	{
		LogPlaceFailure(TEXT("UnknownStakeTier"));
		return false;
	}
	const bool bSpaceStake = HeldTier == EABTSSlingshotTier::Space;
	if (Hole.IsFinaleSpaceSlot() != bSpaceStake)
	{
		LogPlaceFailure(Hole.IsFinaleSpaceSlot()
			? TEXT("FinaleSlotRequiresSpaceStake")
			: TEXT("SpaceStakeRequiresFinaleSlot"));
		return false;
	}
	const FVector Up = Hole.IsFinaleSpaceSlot() && Planet->GetFinaleLaunchFrame().IsUsable()
		? Planet->GetFinaleLaunchFrame().GetUp()
		: (Hole.GetActorLocation() - Planet->GetPlanetCenterWorld()).GetSafeNormal();
	FVector Forward = FVector::VectorPlaneProject(Hole.GetActorForwardVector(), Up).GetSafeNormal();
	if (Forward.IsNearlyZero()) Forward = FVector::VectorPlaneProject(FVector::ForwardVector, Up).GetSafeNormal();
	const FABTSSlingshotVisualPreset Preset = ABTSMakeDefaultSlingshotVisualPreset(HeldTier);
	const FTransform Transform(FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat(),
		Hole.GetActorLocation() + Up * (Preset.StakeHeightCM * 0.5f));
	AABTSM51SlingshotStake* Stake = GetWorld()->SpawnActor<AABTSM51SlingshotStake>(StakeClass, Transform);
	if (Stake == nullptr) return false;
	Stake->InitializeStake(Held, Hole.GetCellId(), Up);
	Stake->SetInstalledSlotIdentity(Hole.GetSlotKind(), Hole.GetSlotPairId(), Hole.GetSlotSide());
	Hole.SetOccupiedStake(Stake);
	Inventory->RemoveItem(Held, 1);
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M5.1][Stake] Installed=%s Cell=%d"), *ABTSGetItemFallbackLabel(Held), Hole.GetCellId());
	return true;
}

bool AABTSM51WorldSystem::SelectStakeForHeldCord(AABTSM51SlingshotStake& Stake)
{
	AABTSCraftingSystem* System = FindCraftingSystem();
	UABTSInventoryComponent* Inventory = System ? System->GetInventory() : nullptr;
	EABTSItemId Held;
	if (Inventory == nullptr || !Inventory->GetHeldItem(Held) || !ABTSIsSlingshotCord(Held)) return false;
	EABTSSlingshotTier ResolvedTier = EABTSSlingshotTier::Simple;
	if (Stake.HasCord() || !ABTSAreSlingshotPartsCompatible(Stake.GetStakeItem(), Held, ResolvedTier))
	{
		LogPlaceFailure(TEXT("StakeTypeOrOccupied"));
		return false;
	}
	const bool bFinaleStake = Stake.GetInstalledSlotKind() == EABTSSlingshotSlotKind::FinaleSpace;
	if ((ResolvedTier == EABTSSlingshotTier::Space) != bFinaleStake)
	{
		LogPlaceFailure(bFinaleStake ? TEXT("FinaleStakeRequiresSpaceCord") : TEXT("SpaceCordRequiresFinaleStake"));
		return false;
	}
	if (!PendingCordStake.IsValid())
	{
		PendingCordStake = &Stake;
		UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M5.1][Cord] FirstStake Cell=%d"), Stake.GetCellId());
		return true;
	}
	AABTSM51SlingshotStake* First = PendingCordStake.Get();
	if (First == &Stake) { PendingCordStake.Reset(); return false; }
	if (ResolvedTier == EABTSSlingshotTier::Space)
	{
		const bool bSameFinalePair =
			First->GetInstalledSlotKind() == EABTSSlingshotSlotKind::FinaleSpace
			&& First->GetInstalledSlotPairId() != INDEX_NONE
			&& First->GetInstalledSlotPairId() == Stake.GetInstalledSlotPairId()
			&& First->GetInstalledSlotSide() != Stake.GetInstalledSlotSide()
			&& First->GetInstalledSlotSide() != EABTSSlingshotSlotSide::None
			&& Stake.GetInstalledSlotSide() != EABTSSlingshotSlotSide::None;
		if (!bSameFinalePair)
		{
			PendingCordStake = &Stake;
			LogPlaceFailure(TEXT("SpaceCordRequiresSameFinalePair"));
			return false;
		}
	}
	const float AngleRadians = FMath::Acos(FMath::Clamp(
		FVector::DotProduct(First->GetUnitDirection(), Stake.GetUnitDirection()), -1.0f, 1.0f));
	if (AngleRadians > MaxStakeArcRadians || First->GetStakeItem() != Stake.GetStakeItem())
	{
		PendingCordStake = &Stake;
		LogPlaceFailure(TEXT("StakeArcOrType"));
		return false;
	}
	const FVector EndpointA = First->GetVisualTopWorldLocation();
	const FVector EndpointB = Stake.GetVisualTopWorldLocation();
	AABTSM51SlingshotCord* Cord = GetWorld()->SpawnActor<AABTSM51SlingshotCord>(CordClass, FTransform::Identity);
	if (Cord == nullptr) return false;
	Cord->InitializeCordWithTier(First, &Stake, EndpointA, EndpointB, ResolvedTier);
	First->SetHasCord(true);
	Stake.SetHasCord(true);
	PendingCordStake.Reset();
	Inventory->RemoveItem(Held, 1);
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M5.1][Cord] Complete ArcRadians=%.5f Item=%s Tier=%d FinalePair=%d"),
		AngleRadians,
		*ABTSGetItemFallbackLabel(Held),
		static_cast<int32>(ResolvedTier),
		Cord->GetFinaleSlotPairId());
	return true;
}
