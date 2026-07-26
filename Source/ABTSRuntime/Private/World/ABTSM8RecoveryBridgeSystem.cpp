// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM8RecoveryBridgeSystem.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Crafting/ABTSCraftingSystem.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/ABTSInventoryComponent.h"
#include "Inventory/ABTSInventoryTypes.h"
#include "Terrain/ABTSM3Planet.h"
#include "World/ABTSM8BridgeActors.h"

AABTSM8RecoveryBridgeSystem::AABTSM8RecoveryBridgeSystem()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.2f;
	WaterBarrierClass = AABTSM8WaterBarrierActor::StaticClass();
	BridgeClass = AABTSM8BridgeActor::StaticClass();
}

void AABTSM8RecoveryBridgeSystem::BeginPlay()
{
	Super::BeginPlay();
	bRuntimeInitialized = InitializeRuntime();
}

void AABTSM8RecoveryBridgeSystem::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bRuntimeInitialized) bRuntimeInitialized = InitializeRuntime();
	SubscribeToMaterialRecovery();
}

bool AABTSM8RecoveryBridgeSystem::InitializeRuntime()
{
	if (!Planet.IsValid())
	{
		for (TActorIterator<AABTSM3Planet> It(GetWorld()); It; ++It)
		{
			if (It->IsPlanetReady()) { Planet = *It; break; }
		}
	}
	if (!CraftingSystem.IsValid())
	{
		for (TActorIterator<AABTSCraftingSystem> It(GetWorld()); It; ++It) { CraftingSystem = *It; break; }
	}
	if (!Planet.IsValid() || !CraftingSystem.IsValid()) return false;
	SpawnWaterBarriers();
	SubscribeToMaterialRecovery();
	int32 BridgeSiteCount = 0;
	for (const FABTSM3CellEdgeState& Edge : Planet->GetGeneratedEdgeStates())
	{
		BridgeSiteCount += Edge.Crossing == EABTSM3CrossingType::BridgeSite ? 1 : 0;
	}
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M8] Ready Barriers=%d BridgeSites=%d"), WaterBarriers.Num(), BridgeSiteCount);
	return true;
}

void AABTSM8RecoveryBridgeSystem::SubscribeToMaterialRecovery()
{
	if (MaterialSystem.IsValid()) return;
	for (TActorIterator<AABTSM7BuildingMaterialSystem> It(GetWorld()); It; ++It)
	{
		MaterialSystem = *It;
		MaterialSystem->OnMaterialRecovered.AddUObject(this, &AABTSM8RecoveryBridgeSystem::HandleMaterialRecovered);
		UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M8][Recovery] Bound MaterialSystem=%s"), *MaterialSystem->GetName());
		break;
	}
}

bool AABTSM8RecoveryBridgeSystem::BuildEdgeFrame(const FABTSM3CellEdgeKey& Edge, FTransform& OutTransform, float& OutCellSpanCM) const
{
	if (!Planet.IsValid() || !Planet->LogicalCells.IsValidIndex(Edge.CellA) || !Planet->LogicalCells.IsValidIndex(Edge.CellB)) return false;
	const FVector DirectionA = Planet->LogicalCells[Edge.CellA].UnitCenter;
	const FVector DirectionB = Planet->LogicalCells[Edge.CellB].UnitCenter;
	const FVector MidDirection = (DirectionA + DirectionB).GetSafeNormal();
	FVector Position;
	FVector Up;
	float Radius = 0.0f;
	int32 CellId = INDEX_NONE;
	if (!Planet->QuerySurface(MidDirection, Position, Up, Radius, CellId)) return false;
	const FVector CrossDirection = FVector::VectorPlaneProject(DirectionB - DirectionA, Up).GetSafeNormal();
	if (CrossDirection.IsNearlyZero()) return false;
	const FVector AlongRiver = FVector::CrossProduct(Up, CrossDirection).GetSafeNormal();
	if (AlongRiver.IsNearlyZero()) return false;
	OutTransform = FTransform(FRotationMatrix::MakeFromXZ(AlongRiver, Up).ToQuat(), Position);
	OutCellSpanCM = FMath::Max(100.0f, Radius * FMath::Acos(FMath::Clamp(FVector::DotProduct(DirectionA, DirectionB), -1.0f, 1.0f)));
	return true;
}

void AABTSM8RecoveryBridgeSystem::SpawnWaterBarriers()
{
	if (!WaterBarrierClass || WaterBarriers.Num() > 0) return;
	for (const FABTSM3CellEdgeState& Edge : Planet->GetGeneratedEdgeStates())
	{
		if (!Edge.bBlocksOnFoot) continue;
		FTransform Transform;
		float CellSpanCM = 0.0f;
		if (!BuildEdgeFrame(Edge.Key, Transform, CellSpanCM)) continue;
		const FVector Up = Transform.GetUnitAxis(EAxis::Z);
		Transform.AddToTranslation(Up * (BarrierHeightCM * 0.5f));
		FActorSpawnParameters Params;
		Params.Owner = this;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AABTSM8WaterBarrierActor* Barrier = GetWorld()->SpawnActor<AABTSM8WaterBarrierActor>(WaterBarrierClass, Transform, Params);
		if (Barrier == nullptr) continue;
		Barrier->InitializeBarrier(Edge.Key, Transform, FVector(CellSpanCM * BarrierLengthMultiplier * 0.5f, BarrierHalfThicknessCM, BarrierHeightCM * 0.5f));
		WaterBarriers.Add(Edge.Key, Barrier);
	}
}

bool AABTSM8RecoveryBridgeSystem::FindNearestUnbuiltBridgeSite(const FVector& UnitDirection, FABTSM3CellEdgeKey& OutEdge, float& OutAngularDistanceDegrees) const
{
	if (!Planet.IsValid()) return false;
	float BestDot = -1.0f;
	OutEdge = FABTSM3CellEdgeKey();
	for (const FABTSM3CellEdgeState& Edge : Planet->GetGeneratedEdgeStates())
	{
		if (Edge.Crossing != EABTSM3CrossingType::BridgeSite || BuiltBridgeEdges.Contains(Edge.Key)) continue;
		if (!Planet->LogicalCells.IsValidIndex(Edge.Key.CellA) || !Planet->LogicalCells.IsValidIndex(Edge.Key.CellB)) continue;
		const FVector MidDirection = (Planet->LogicalCells[Edge.Key.CellA].UnitCenter + Planet->LogicalCells[Edge.Key.CellB].UnitCenter).GetSafeNormal();
		const float Dot = FVector::DotProduct(UnitDirection, MidDirection);
		if (Dot > BestDot) { BestDot = Dot; OutEdge = Edge.Key; }
	}
	OutAngularDistanceDegrees = BestDot > -1.0f ? FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(BestDot, -1.0f, 1.0f))) : BIG_NUMBER;
	return OutEdge.CellA != INDEX_NONE;
}

void AABTSM8RecoveryBridgeSystem::HandleMaterialRecovered(const EABTSM7BuildingMaterial Material, const int32 Quantity)
{
	if (!bEnableAutomaticMaterialRecovery || Quantity <= 0 || !CraftingSystem.IsValid()) return;
	EABTSItemId ItemId = EABTSItemId::Wood;
	switch (Material)
	{
	case EABTSM7BuildingMaterial::Stone: ItemId = EABTSItemId::Stone; break;
	case EABTSM7BuildingMaterial::Iron: ItemId = EABTSItemId::MetalParts; break;
	case EABTSM7BuildingMaterial::Glass: ItemId = EABTSItemId::Glass; break;
	default: break;
	}
	if (UABTSInventoryComponent* Inventory = CraftingSystem->GetInventory())
	{
		const int32 FinalQuantity = Quantity * RecoveryQuantityPerDestroyedBrick;
		Inventory->AddItem(ItemId, FinalQuantity);
		UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M8][Recovery] Added Item=%s Quantity=%d"), *ABTSGetItemFallbackLabel(ItemId), FinalQuantity);
	}
}

void AABTSM8RecoveryBridgeSystem::LogBridgePlacementFailure(const TCHAR* Reason) const
{
	UE_LOG(LogABTSRuntime, Warning, TEXT("[ABTS][M8][Bridge] Rejected Reason=%s"), Reason);
}

bool AABTSM8RecoveryBridgeSystem::PlaceHeldBridgeAtAim(APlayerController& Controller)
{
	if (!bRuntimeInitialized || !BridgeClass || !CraftingSystem.IsValid()) return false;
	UABTSInventoryComponent* Inventory = CraftingSystem->GetInventory();
	EABTSItemId HeldItem;
	if (Inventory == nullptr || !Inventory->GetHeldItem(HeldItem) || !ABTSIsBridgeKit(HeldItem)) return false;
	FVector RayOrigin;
	FVector RayDirection;
	if (!Controller.DeprojectMousePositionToWorld(RayOrigin, RayDirection)) { LogBridgePlacementFailure(TEXT("NoAimRay")); return false; }
	FHitResult Hit;
	FVector AimPoint = RayOrigin + RayDirection * 20000.0f;
	if (Controller.GetHitResultUnderCursor(ECC_Visibility, false, Hit) && Hit.bBlockingHit) AimPoint = Hit.ImpactPoint;
	const FVector UnitDirection = (AimPoint - Planet->GetPlanetCenterWorld()).GetSafeNormal();
	FABTSM3CellEdgeKey Edge;
	float SnapDegrees = BIG_NUMBER;
	if (!FindNearestUnbuiltBridgeSite(UnitDirection, Edge, SnapDegrees)) { LogBridgePlacementFailure(TEXT("NoUnbuiltBridgeSite")); return false; }
	if (SnapDegrees > MaxBridgePlacementSnapDegrees) { LogBridgePlacementFailure(TEXT("NotAtBridgeSite")); return false; }
	FTransform EdgeTransform;
	float CellSpanCM = 0.0f;
	if (!BuildEdgeFrame(Edge, EdgeTransform, CellSpanCM)) { LogBridgePlacementFailure(TEXT("InvalidBridgeEdge")); return false; }
	const FVector CrossDirection = EdgeTransform.GetUnitAxis(EAxis::Y);
	const FVector Up = EdgeTransform.GetUnitAxis(EAxis::Z);
	EdgeTransform.SetRotation(FRotationMatrix::MakeFromXZ(CrossDirection, Up).ToQuat());
	EdgeTransform.AddToTranslation(Up * BridgeDeckSurfaceOffsetCM);
	if (Controller.GetPawn() == nullptr || FVector::Distance(Controller.GetPawn()->GetActorLocation(), EdgeTransform.GetLocation()) > BridgePlacementReachCM)
	{
		LogBridgePlacementFailure(TEXT("OutOfReach"));
		return false;
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM8BridgeActor* Bridge = GetWorld()->SpawnActor<AABTSM8BridgeActor>(BridgeClass, EdgeTransform, Params);
	if (Bridge == nullptr) { LogBridgePlacementFailure(TEXT("SpawnFailed")); return false; }
	Bridge->InitializeBridge(Edge, EdgeTransform, FVector(CellSpanCM * 1.35f, BridgeDeckWidthCM, BridgeDeckThicknessCM));
	BuiltBridgeEdges.Add(Edge);
	if (TWeakObjectPtr<AABTSM8WaterBarrierActor>* Barrier = WaterBarriers.Find(Edge))
	{
		if (Barrier->IsValid()) Barrier->Get()->OpenPassage();
	}
	Inventory->RemoveItem(EABTSItemId::BridgeKit, 1);
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M8][Bridge] Built CellA=%d CellB=%d Span=%.1fcm"), Edge.CellA, Edge.CellB, CellSpanCM);
	return true;
}
