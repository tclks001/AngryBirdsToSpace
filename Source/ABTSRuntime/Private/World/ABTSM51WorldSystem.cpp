// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM51WorldSystem.h"

#include "ABTSRuntime.h"
#include "Audio/ABTSAudioWorldSubsystem.h"
#include "Crafting/ABTSCraftingStation.h"
#include "Crafting/ABTSCraftingSystem.h"
#include "EngineUtils.h"
#include "Guide/ABTSGuideEvents.h"
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

FString AABTSM51WorldSystem::BuildReleaseDiagnosticSummary() const
{
	const TCHAR* AuthorityName = TEXT("None");
	switch (OrdinarySlotSnapshotAuthority)
	{
	case EABTSM51OrdinarySlingshotSlotSnapshotAuthority::AcceptedMonthly:
		AuthorityName = TEXT("AcceptedMonthly");
		break;
	case EABTSM51OrdinarySlingshotSlotSnapshotAuthority::PreviewTest:
		AuthorityName = TEXT("PreviewTest");
		break;
	default:
		break;
	}
	const AABTSCraftingSystem* ResolvedCraftingSystem =
		FindCraftingSystem();
	const UABTSInventoryComponent* Inventory =
		ResolvedCraftingSystem != nullptr
			? ResolvedCraftingSystem->GetInventory()
			: nullptr;
	return FString::Printf(
		TEXT("Ready=%d Rejected=%d OrdinarySlots=%d Pickups=%d")
		TEXT(" MaxCord=%d Authority=%s StarterBranch=%d")
		TEXT(" StarterFiber=%d InventoryStacks=%d"),
		bInitialized ? 1 : 0,
		bInitializationRejected ? 1 : 0,
		OrdinarySlots.Num(),
		Pickups.Num(),
		GetActiveOrdinaryMaxCordLengthCM(),
		AuthorityName,
		Inventory != nullptr
			? Inventory->GetQuantity(EABTSItemId::Branch)
			: INDEX_NONE,
		Inventory != nullptr
			? Inventory->GetQuantity(EABTSItemId::PlantFiber)
			: INDEX_NONE,
		Inventory != nullptr
			? Inventory->GetOrderedStacks().Num()
			: INDEX_NONE);
}

void AABTSM51WorldSystem::BeginPlay()
{
	Super::BeginPlay();
	bInitialized = InitializeWorldContent();
}

void AABTSM51WorldSystem::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bInitialized && !bInitializationRejected)
	{
		bInitialized = InitializeWorldContent();
	}
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
	if (!SpawnSlingshotHoles())
	{
		bInitializationRejected = true;
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M5.1] World initialization rejected by ordinary slingshot slot gate."));
		return false;
	}
	SpawnSdfPickups();
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M5.1] World ready Pickups=%d OccupiedCells=%d"), Pickups.Num(), OccupiedCells.Num());
	FABTSGuideEventBus::Publish(this, FABTSGuideEventIds::WorldReady,
		NAME_None, this, Pickups.Num(), OccupiedCells.Num());
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

bool AABTSM51WorldSystem::SpawnPickupShowcase(const float RequestedDistanceCM)
{
	if (GetWorld() == nullptr)
	{
		UE_LOG(LogABTSRuntime, Warning,
			TEXT("[ABTS][M5.1][PickupShowcase] Accepted=0 Reason=WorldUnavailable"));
		return false;
	}
	const APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
	const APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (Pawn == nullptr)
	{
		UE_LOG(LogABTSRuntime, Warning,
			TEXT("[ABTS][M5.1][PickupShowcase] Accepted=0 Reason=PlayerPawnUnavailable"));
		return false;
	}
	return SpawnPickupShowcaseAroundPawn(*Pawn, RequestedDistanceCM);
}

bool AABTSM51WorldSystem::SpawnPickupShowcaseAroundPawn(
	const APawn& Pawn,
	const float RequestedDistanceCM)
{
	if (GetWorld() == nullptr || Pawn.GetWorld() != GetWorld())
	{
		UE_LOG(LogABTSRuntime, Warning,
			TEXT("[ABTS][M5.1][PickupShowcase] Accepted=0 Reason=InvalidPawnWorld"));
		return false;
	}
	if (!Planet.IsValid())
	{
		for (TActorIterator<AABTSM3Planet> It(GetWorld()); It; ++It)
		{
			if (It->IsPlanetReady())
			{
				Planet = *It;
				break;
			}
		}
	}
	if (!Planet.IsValid())
	{
		UE_LOG(LogABTSRuntime, Warning,
			TEXT("[ABTS][M5.1][PickupShowcase] Accepted=0 Reason=PlanetUnavailable"));
		return false;
	}

	const FVector PlanetCenter = Planet->GetPlanetCenterWorld();
	const FVector PlayerUp = (Pawn.GetActorLocation() - PlanetCenter).GetSafeNormal();
	const float PlayerRadiusCM = FVector::Distance(Pawn.GetActorLocation(), PlanetCenter);
	if (PlayerUp.IsNearlyZero() || PlayerRadiusCM <= KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogABTSRuntime, Warning,
			TEXT("[ABTS][M5.1][PickupShowcase] Accepted=0 Reason=InvalidPlayerFrame"));
		return false;
	}
	FVector PlayerForward = FVector::VectorPlaneProject(
		Pawn.GetActorForwardVector(), PlayerUp).GetSafeNormal();
	if (PlayerForward.IsNearlyZero())
	{
		PlayerForward = FVector::VectorPlaneProject(
			FVector::ForwardVector, PlayerUp).GetSafeNormal();
	}
	if (PlayerForward.IsNearlyZero())
	{
		PlayerForward = FVector::VectorPlaneProject(
			FVector::RightVector, PlayerUp).GetSafeNormal();
	}
	const FVector PlayerRight = FVector::CrossProduct(
		PlayerUp, PlayerForward).GetSafeNormal();
	const float EffectiveDistanceCM = FMath::Max(
		RequestedDistanceCM,
		AutoPickupRadiusCM + 250.0f);
	const float AngularDistanceRadians = FMath::Clamp(
		EffectiveDistanceCM / PlayerRadiusCM,
		0.01f,
		PI / 3.0f);

	TSet<int32> ReservedCells;
	for (const TWeakObjectPtr<AABTSM51PickupItem>& ExistingPickup : Pickups)
	{
		if (ExistingPickup.IsValid()) ReservedCells.Add(ExistingPickup->GetCellId());
	}
	const TArray<FABTSM3CellState>& States = Planet->GetGeneratedCellStates();
	static const float FanYawDegrees[4] = {-54.0f, -18.0f, 18.0f, 54.0f};
	static const EABTSItemId ShowcaseItems[4] = {
		EABTSItemId::Branch,
		EABTSItemId::Stone,
		EABTSItemId::Wood,
		EABTSItemId::PlantFiber};
	int32 CellIds[4] = {INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE};
	FTransform Transforms[4];
	float MinimumResolvedDistanceCM = TNumericLimits<float>::Max();
	for (int32 ItemIndex = 0; ItemIndex < 4; ++ItemIndex)
	{
		const float FanYawRadians = FMath::DegreesToRadians(FanYawDegrees[ItemIndex]);
		const FVector TangentDirection =
			PlayerForward * FMath::Cos(FanYawRadians)
			+ PlayerRight * FMath::Sin(FanYawRadians);
		const FVector DesiredDirection =
			(PlayerUp * FMath::Cos(AngularDistanceRadians)
				+ TangentDirection * FMath::Sin(AngularDistanceRadians)).GetSafeNormal();
		float BestDot = -1.0f;
		for (int32 CellId = 0; CellId < States.Num(); ++CellId)
		{
			const FABTSM3CellState& State = States[CellId];
			if (State.bWater
				|| State.bBuildingAnchor
				|| IsCellOccupied(CellId)
				|| ReservedCells.Contains(CellId))
			{
				continue;
			}
			const float Dot = FVector::DotProduct(
				DesiredDirection,
				Planet->LogicalCells[CellId].UnitCenter);
			if (Dot > BestDot)
			{
				BestDot = Dot;
				CellIds[ItemIndex] = CellId;
			}
		}
		if (CellIds[ItemIndex] == INDEX_NONE
			|| !QueryCellTransform(CellIds[ItemIndex], 24.0f, Transforms[ItemIndex]))
		{
			UE_LOG(LogABTSRuntime, Warning,
				TEXT("[ABTS][M5.1][PickupShowcase] Accepted=0 Reason=SafeCellUnavailable Item=%s"),
				*ABTSGetItemFallbackLabel(ShowcaseItems[ItemIndex]));
			return false;
		}
		const float ResolvedDistanceCM = FVector::Distance(
			Pawn.GetActorLocation(),
			Transforms[ItemIndex].GetLocation());
		if (ResolvedDistanceCM <= AutoPickupRadiusCM + 50.0f)
		{
			UE_LOG(LogABTSRuntime, Warning,
				TEXT("[ABTS][M5.1][PickupShowcase] Accepted=0 Reason=InsideAutoPickupRange Item=%s Distance=%.1f AutoRadius=%.1f"),
				*ABTSGetItemFallbackLabel(ShowcaseItems[ItemIndex]),
				ResolvedDistanceCM,
				AutoPickupRadiusCM);
			return false;
		}
		MinimumResolvedDistanceCM = FMath::Min(
			MinimumResolvedDistanceCM,
			ResolvedDistanceCM);
		ReservedCells.Add(CellIds[ItemIndex]);
	}

	TArray<AABTSM51PickupItem*> SpawnedPickups;
	for (int32 ItemIndex = 0; ItemIndex < 4; ++ItemIndex)
	{
		AABTSM51PickupItem* Pickup = GetWorld()->SpawnActor<AABTSM51PickupItem>(
			PickupClass,
			Transforms[ItemIndex]);
		if (Pickup == nullptr)
		{
			for (AABTSM51PickupItem* SpawnedPickup : SpawnedPickups)
			{
				SpawnedPickup->Destroy();
			}
			UE_LOG(LogABTSRuntime, Warning,
				TEXT("[ABTS][M5.1][PickupShowcase] Accepted=0 Reason=SpawnFailed RolledBack=%d"),
				SpawnedPickups.Num());
			return false;
		}
		Pickup->InitializePickup(ShowcaseItems[ItemIndex], 1, CellIds[ItemIndex]);
		SpawnedPickups.Add(Pickup);
	}
	for (AABTSM51PickupItem* Pickup : SpawnedPickups)
	{
		Pickups.Add(Pickup);
	}
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M5.1][PickupShowcase] Accepted=1 Spawned=4 RequestedDistance=%.1f EffectiveDistance=%.1f MinimumResolvedDistance=%.1f AutoRadius=%.1f Cells=[%d,%d,%d,%d]"),
		RequestedDistanceCM,
		EffectiveDistanceCM,
		MinimumResolvedDistanceCM,
		AutoPickupRadiusCM,
		CellIds[0], CellIds[1], CellIds[2], CellIds[3]);
	return true;
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
		if (UABTSAudioWorldSubsystem* Audio = GetWorld()
			? GetWorld()->GetSubsystem<UABTSAudioWorldSubsystem>()
			: nullptr)
		{
			Audio->PlayPickup(Pickup->GetActorLocation(), Pickup->GetQuantity());
		}
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
		// Workbench/Furnace placement is a lightweight interaction placeholder,
		// not a frozen M7 building pad. Keep water, building anchors and occupied
		// cells fail-closed, but allow ordinary terrain up to a broad slope limit
		// instead of inheriting M3's strict 8-degree building-site flag.
		if (!IsToolPlacementCellEligible(
				States[CellId],
				IsCellOccupied(CellId),
				MaxToolPlacementSlopeDegrees))
		{
			continue;
		}
		const float Dot = FVector::DotProduct(UnitDirection, Planet->LogicalCells[CellId].UnitCenter);
		if (Dot > BestDot) { BestDot = Dot; BestCell = CellId; }
	}
	return BestCell;
}

bool AABTSM51WorldSystem::IsToolPlacementCellEligible(
	const FABTSM3CellState& State,
	const bool bOccupied,
	const float MaximumSlopeDegrees)
{
	return !bOccupied
		&& !State.bWater
		&& !State.bBuildingAnchor
		&& !State.bBuildingRoadExclusion
		&& FMath::IsFinite(State.LogicalSlopeDegrees)
		&& State.LogicalSlopeDegrees
			<= FMath::Clamp(MaximumSlopeDegrees, 0.0f, 60.0f);
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
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M5.1][Place] Tool=%s Cell=%d Slope=%.2f SlopeLimit=%.2f"),
		*ABTSGetItemFallbackLabel(Held), CellId,
		Planet->GetGeneratedCellStates()[CellId].LogicalSlopeDegrees,
		MaxToolPlacementSlopeDegrees);
	return true;
}

bool AABTSM51WorldSystem::PlaceHeldStakeAtAim(APlayerController& Controller)
{
#if UE_BUILD_SHIPPING
	(void)Controller;
	return false;
#else
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
#endif
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
	const FABTSM110FinaleLocalFrame* ActiveFinaleFrame =
		GetActiveFinaleFrame();
	const FVector Up = Hole.IsFinaleSpaceSlot()
		&& ActiveFinaleFrame != nullptr
		? ActiveFinaleFrame->GetUp()
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
	FABTSGuideEventBus::Publish(this, FABTSGuideEventIds::StakeInstalled,
		FABTSGuideSubjects::FromSlingshotTier(HeldTier), Stake,
		Hole.GetCellId(), Hole.GetSlotPairId());
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
		FABTSGuideEventBus::Publish(this, FABTSGuideEventIds::CordEndpointSelected,
			FABTSGuideSubjects::FromSlingshotTier(ResolvedTier), &Stake,
			Stake.GetCellId(), Stake.GetInstalledSlotPairId());
		return true;
	}
	AABTSM51SlingshotStake* First = PendingCordStake.Get();
	if (First == &Stake) { PendingCordStake.Reset(); return false; }
	if (TryConnectCord(*First, Stake, Held, ResolvedTier, *Inventory))
	{
		PendingCordStake.Reset();
		return true;
	}

	// Preserve the two-click workflow: a valid second stake becomes the next
	// first choice after a rejected pair, while every gameplay side effect stays
	// unchanged.
	if (!Stake.HasCord())
	{
		PendingCordStake = &Stake;
	}
	return false;
}
