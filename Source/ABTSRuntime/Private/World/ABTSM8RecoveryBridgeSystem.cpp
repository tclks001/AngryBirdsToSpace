// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM8RecoveryBridgeSystem.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Components/InputComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Crafting/ABTSCraftingSystem.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Inventory/ABTSInventoryComponent.h"
#include "Inventory/ABTSInventoryTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Terrain/ABTSM3Planet.h"
#include "Terrain/ABTSM3RiverVisualBuilder.h"
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
#if WITH_EDITOR
	bBridgeSiteDebugEnabled = FParse::Param(
		FCommandLine::Get(),
		TEXT("ABTSM3R5LogicRegions"));
	bBridgeSiteDebugReadyLogged = false;
	BridgeSiteDebugRefreshRemaining = 0.0f;
	if (APlayerController* Controller = GetWorld() != nullptr
		? GetWorld()->GetFirstPlayerController()
		: nullptr)
	{
		EnableInput(Controller);
		if (InputComponent != nullptr)
		{
			FInputKeyBinding& Binding = InputComponent->BindKey(
				EKeys::F7,
				IE_Pressed,
				this,
				&AABTSM8RecoveryBridgeSystem::ToggleBridgeSiteDebug);
			Binding.bConsumeInput = false;
			Binding.bExecuteWhenPaused = true;
		}
	}
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M8][BridgeDebug] Shortcut=F7 StartupEnabled=%d EdgeColor=Green MarkerColor=Yellow"),
		bBridgeSiteDebugEnabled ? 1 : 0);
#endif
}

void AABTSM8RecoveryBridgeSystem::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bRuntimeInitialized) bRuntimeInitialized = InitializeRuntime();
	SubscribeToMaterialRecovery();
#if WITH_EDITOR
	if (bBridgeSiteDebugEnabled)
	{
		RefreshBridgeSiteDebug(DeltaSeconds);
	}
#endif
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
	int32 WaterEdgeCount = 0;
	for (const FABTSM3CellEdgeState& Edge : Planet->GetGeneratedEdgeStates())
	{
		BridgeSiteCount += Edge.Crossing == EABTSM3CrossingType::BridgeSite ? 1 : 0;
		WaterEdgeCount += Edge.Water != EABTSM3WaterEdgeType::None ? 1 : 0;
	}
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M8] Ready Barriers=%d WaterEdges=%d CertifiedBridgeSites=%d BridgeAuthority=GeneratedEdgeStates PreviewAuthority=%d BarrierCoverage=VisibleWaterPlusMargin BankMarginCM=%.1f BridgePassageSideClearanceCM=%.1f"),
		WaterBarriers.Num(),
		WaterEdgeCount,
		BridgeSiteCount,
		Planet->IsMonthlyPresentationPreviewActive() ? 1 : 0,
		BarrierHalfThicknessCM,
		BridgeBarrierSideClearanceCM);
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

void AABTSM8RecoveryBridgeSystem::SpawnWaterBarriers()
{
	if (!WaterBarrierClass || WaterBarriers.Num() > 0) return;
	for (const FABTSM3CellEdgeState& Edge : Planet->GetGeneratedEdgeStates())
	{
		if (!Edge.bBlocksOnFoot) continue;
		if (!Planet->LogicalCells.IsValidIndex(Edge.Key.CellA)
			|| !Planet->LogicalCells.IsValidIndex(Edge.Key.CellB))
		{
			continue;
		}
		const FVector EdgeMidDirection = (
			Planet->LogicalCells[Edge.Key.CellA].UnitCenter
			+ Planet->LogicalCells[Edge.Key.CellB].UnitCenter).GetSafeNormal();
		FABTSM8BridgePlacementGeometry Geometry;
		if (!ResolveSemanticBridgeGeometry(
			*Planet,
			Edge,
			EdgeMidDirection,
			Geometry))
		{
			continue;
		}
		const FVector Up = Geometry.BridgeTransform.GetUnitAxis(EAxis::Z);
		const FVector AlongRiver = Geometry.BridgeTransform.GetUnitAxis(EAxis::Y);
		FTransform Transform(
			FRotationMatrix::MakeFromXZ(AlongRiver, Up).ToQuat(),
			Geometry.BridgeTransform.GetLocation());
		Transform.AddToTranslation(Up * (BarrierHeightCM * 0.5f));
		FActorSpawnParameters Params;
		Params.Owner = this;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AABTSM8WaterBarrierActor* Barrier = GetWorld()->SpawnActor<AABTSM8WaterBarrierActor>(WaterBarrierClass, Transform, Params);
		if (Barrier == nullptr) continue;
		Barrier->InitializeBarrier(
			Edge.Key,
			Transform,
			FVector(
				Geometry.RiverSegmentLengthCM * BarrierLengthMultiplier * 0.5f,
				ComputeWaterBarrierHalfWidthCM(
					Geometry.WaterHalfWidthCM,
					BarrierHalfThicknessCM),
				BarrierHeightCM * 0.5f));
		WaterBarriers.Add(Edge.Key, Barrier);
	}
}

float AABTSM8RecoveryBridgeSystem::ComputeWaterBarrierHalfWidthCM(
	const float VisibleWaterHalfWidthCM,
	const float BankSafetyMarginCM)
{
	return FMath::Max(1.0f, VisibleWaterHalfWidthCM)
		+ FMath::Max(0.0f, BankSafetyMarginCM);
}

bool AABTSM8RecoveryBridgeSystem::ResolveSemanticBridgeGeometry(
	const AABTSM3Planet& InPlanet,
	const FABTSM3CellEdgeState& EdgeState,
	const FVector& AimUnitDirection,
	FABTSM8BridgePlacementGeometry& OutGeometry)
{
	OutGeometry = FABTSM8BridgePlacementGeometry();
	if (EdgeState.Water == EABTSM3WaterEdgeType::None
		|| AimUnitDirection.IsNearlyZero()
		|| !InPlanet.LogicalCells.IsValidIndex(EdgeState.Key.CellA)
		|| !InPlanet.LogicalCells.IsValidIndex(EdgeState.Key.CellB))
	{
		return false;
	}

	TArray<FABTSM3CellEdgeState> SingleEdge;
	SingleEdge.Add(EdgeState);
	TArray<FABTSM3RiverVisualSegment> Segments;
	FABTSM3RiverVisualBuilder::BuildSegments(
		InPlanet.LogicalCells,
		SingleEdge,
		InPlanet.StreamVisualHalfWidthCM,
		InPlanet.ShallowRiverVisualHalfWidthCM,
		InPlanet.DeepRiverVisualHalfWidthCM,
		Segments);
	if (Segments.Num() != 1)
	{
		return false;
	}

	const FABTSM3RiverVisualSegment& Segment = Segments[0];
	const FVector SegmentVector = Segment.EndUnit - Segment.StartUnit;
	const float SegmentLengthSquared = SegmentVector.SizeSquared();
	if (SegmentLengthSquared <= SMALL_NUMBER)
	{
		return false;
	}
	const FVector NormalizedAim = AimUnitDirection.GetSafeNormal();
	const float Projection = FMath::Clamp(
		FVector::DotProduct(NormalizedAim - Segment.StartUnit, SegmentVector)
			/ SegmentLengthSquared,
		0.0f,
		1.0f);
	const FVector ClosestChordPoint = Segment.StartUnit + Projection * SegmentVector;
	const FVector AnchorDirection = ClosestChordPoint.GetSafeNormal();
	if (AnchorDirection.IsNearlyZero())
	{
		return false;
	}

	FVector Position;
	FVector Up;
	float SurfaceRadiusCM = 0.0f;
	int32 CellId = INDEX_NONE;
	if (!InPlanet.QuerySurface(
		AnchorDirection,
		Position,
		Up,
		SurfaceRadiusCM,
		CellId))
	{
		return false;
	}
	const FVector RiverTangent = FVector::VectorPlaneProject(
		SegmentVector,
		Up).GetSafeNormal();
	FVector AcrossRiver = FVector::CrossProduct(RiverTangent, Up).GetSafeNormal();
	if (RiverTangent.IsNearlyZero() || AcrossRiver.IsNearlyZero())
	{
		return false;
	}
	const FVector CellAToB = FVector::VectorPlaneProject(
		InPlanet.LogicalCells[EdgeState.Key.CellB].UnitCenter
			- InPlanet.LogicalCells[EdgeState.Key.CellA].UnitCenter,
		Up).GetSafeNormal();
	if (!CellAToB.IsNearlyZero()
		&& FMath::Abs(FVector::DotProduct(AcrossRiver, CellAToB)) > 0.5f
		&& FVector::DotProduct(AcrossRiver, CellAToB) < 0.0f)
	{
		AcrossRiver *= -1.0f;
	}

	OutGeometry.BridgeTransform = FTransform(
		FRotationMatrix::MakeFromXZ(AcrossRiver, Up).ToQuat(),
		Position);
	OutGeometry.AimDistanceCM = FABTSM3RiverVisualBuilder::GetDistanceToSegmentCM(
		NormalizedAim,
		Segment,
		InPlanet.GetPlanetRadiusCM());
	OutGeometry.WaterHalfWidthCM = Segment.HalfWidthCM;
	OutGeometry.RiverSegmentLengthCM = FMath::Max(
		100.0f,
		SurfaceRadiusCM * FMath::Acos(FMath::Clamp(
			FVector::DotProduct(Segment.StartUnit, Segment.EndUnit),
			-1.0f,
			1.0f)));
	OutGeometry.bBarrierSegment =
		EdgeState.DownstreamCellId == INDEX_NONE || EdgeState.bBlocksOnFoot;
	OutGeometry.bCertifiedBridgeSite =
		EdgeState.Crossing == EABTSM3CrossingType::BridgeSite;
	return true;
}

bool AABTSM8RecoveryBridgeSystem::FindNearestUnbuiltWaterSegment(
	const FVector& UnitDirection,
	FABTSM3CellEdgeState& OutEdgeState,
	FABTSM8BridgePlacementGeometry& OutGeometry) const
{
	if (!Planet.IsValid()) return false;
	bool bFound = false;
	float BestDistanceCM = BIG_NUMBER;
	for (const FABTSM3CellEdgeState& EdgeState : Planet->GetGeneratedEdgeStates())
	{
		if (EdgeState.Water == EABTSM3WaterEdgeType::None
			|| BuiltBridgeEdges.Contains(EdgeState.Key))
		{
			continue;
		}
		FABTSM8BridgePlacementGeometry Candidate;
		if (!ResolveSemanticBridgeGeometry(
			*Planet,
			EdgeState,
			UnitDirection,
			Candidate)
			|| Candidate.AimDistanceCM >= BestDistanceCM)
		{
			continue;
		}
		bFound = true;
		BestDistanceCM = Candidate.AimDistanceCM;
		OutEdgeState = EdgeState;
		OutGeometry = Candidate;
	}
	return bFound;
}

void AABTSM8RecoveryBridgeSystem::HandleMaterialRecovered(const EABTSM7BuildingMaterial Material, const int32 Quantity)
{
	if (!bEnableAutomaticMaterialRecovery || Quantity <= 0 || !CraftingSystem.IsValid()) return;
	EABTSItemId ItemId;
	if (!TryMapRecoveredMaterialToItem(Material, ItemId))
	{
		UE_LOG(
			LogABTSRuntime,
			Warning,
			TEXT("[ABTS][M8][Recovery] Rejected unknown building material=%d"),
			static_cast<int32>(Material));
		return;
	}
	if (UABTSInventoryComponent* Inventory = CraftingSystem->GetInventory())
	{
		const int32 FinalQuantity = Quantity * RecoveryQuantityPerDestroyedBrick;
		Inventory->AddItem(ItemId, FinalQuantity);
		UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M8][Recovery] Added Item=%s Quantity=%d"), *ABTSGetItemFallbackLabel(ItemId), FinalQuantity);
	}
}

bool AABTSM8RecoveryBridgeSystem::TryMapRecoveredMaterialToItem(
	const EABTSM7BuildingMaterial Material,
	EABTSItemId& OutItemId)
{
	switch (Material)
	{
	case EABTSM7BuildingMaterial::Wood: OutItemId = EABTSItemId::Wood; return true;
	case EABTSM7BuildingMaterial::Stone: OutItemId = EABTSItemId::Stone; return true;
	case EABTSM7BuildingMaterial::Iron: OutItemId = EABTSItemId::MetalParts; return true;
	case EABTSM7BuildingMaterial::Glass: OutItemId = EABTSItemId::Glass; return true;
	case EABTSM7BuildingMaterial::Crystal: OutItemId = EABTSItemId::CrystalCore; return true;
	default: return false;
	}
}

void AABTSM8RecoveryBridgeSystem::LogBridgePlacementFailure(
	const TCHAR* Reason,
	const FABTSM3CellEdgeState* EdgeState,
	const float AimDistanceCM,
	const float AllowedAimDistanceCM,
	const float PlayerDistanceCM,
	const FVector* AimPoint,
	const FHitResult* Hit) const
{
	const int32 CellA = EdgeState != nullptr ? EdgeState->Key.CellA : INDEX_NONE;
	const int32 CellB = EdgeState != nullptr ? EdgeState->Key.CellB : INDEX_NONE;
	const int32 WaterType = EdgeState != nullptr
		? static_cast<int32>(EdgeState->Water)
		: static_cast<int32>(EABTSM3WaterEdgeType::None);
	const TCHAR* Semantic = EdgeState == nullptr
		? TEXT("Unavailable")
		: (EdgeState->DownstreamCellId != INDEX_NONE && !EdgeState->bBlocksOnFoot
			? TEXT("FlowCenterline")
			: TEXT("BarrierDualEdge"));
	const FString AimPointText = AimPoint != nullptr
		? AimPoint->ToCompactString()
		: TEXT("Unavailable");
	const AActor* HitActor = Hit != nullptr ? Hit->GetActor() : nullptr;
	const UPrimitiveComponent* HitComponent = Hit != nullptr
		? Hit->GetComponent()
		: nullptr;
	UE_LOG(
		LogABTSRuntime,
		Warning,
		TEXT("[ABTS][M8][Bridge] Rejected Reason=%s NearestWaterEdge=(%d,%d) WaterType=%d Semantic=%s AimDistanceCM=%.1f AllowedAimDistanceCM=%.1f PlayerDistanceCM=%.1f ReachCM=%.1f AimPoint=%s HitActor=%s HitComponent=%s"),
		Reason,
		CellA,
		CellB,
		WaterType,
		Semantic,
		AimDistanceCM,
		AllowedAimDistanceCM,
		PlayerDistanceCM,
		BridgePlacementReachCM,
		*AimPointText,
		*GetNameSafe(HitActor),
		*GetNameSafe(HitComponent));
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
	const bool bHasAimHit = Controller.GetHitResultUnderCursor(ECC_Visibility, false, Hit) && Hit.bBlockingHit;
	if (bHasAimHit) AimPoint = Hit.ImpactPoint;
	const FVector UnitDirection = (AimPoint - Planet->GetPlanetCenterWorld()).GetSafeNormal();
	FABTSM3CellEdgeState EdgeState;
	FABTSM8BridgePlacementGeometry Geometry;
	if (!FindNearestUnbuiltWaterSegment(UnitDirection, EdgeState, Geometry))
	{
		LogBridgePlacementFailure(
			TEXT("NoUnbuiltWaterSegment"),
			nullptr,
			-1.0f,
			-1.0f,
			-1.0f,
			&AimPoint,
			bHasAimHit ? &Hit : nullptr);
		return false;
	}
	const float AllowedAimDistanceCM =
		Geometry.WaterHalfWidthCM + BridgePlacementAimToleranceCM;
	if (Geometry.AimDistanceCM > AllowedAimDistanceCM)
	{
		const FVector PreviewLocation = Geometry.BridgeTransform.GetLocation()
			+ Geometry.BridgeTransform.GetUnitAxis(EAxis::Z)
				* BridgeDeckSurfaceOffsetCM;
		const float NearestPlayerDistanceCM = Controller.GetPawn() != nullptr
			? FVector::Distance(
				Controller.GetPawn()->GetActorLocation(),
				PreviewLocation)
			: -1.0f;
		LogBridgePlacementFailure(
			TEXT("NotOverWater"),
			&EdgeState,
			Geometry.AimDistanceCM,
			AllowedAimDistanceCM,
			NearestPlayerDistanceCM,
			&AimPoint,
			bHasAimHit ? &Hit : nullptr);
		return false;
	}
	FTransform EdgeTransform = Geometry.BridgeTransform;
	const FVector Up = EdgeTransform.GetUnitAxis(EAxis::Z);
	EdgeTransform.AddToTranslation(Up * BridgeDeckSurfaceOffsetCM);
	const float PlayerDistanceCM = Controller.GetPawn() != nullptr
		? FVector::Distance(
			Controller.GetPawn()->GetActorLocation(),
			EdgeTransform.GetLocation())
		: -1.0f;
	if (PlayerDistanceCM < 0.0f || PlayerDistanceCM > BridgePlacementReachCM)
	{
		LogBridgePlacementFailure(
			TEXT("OutOfReach"),
			&EdgeState,
			Geometry.AimDistanceCM,
			AllowedAimDistanceCM,
			PlayerDistanceCM,
			&AimPoint,
			bHasAimHit ? &Hit : nullptr);
		return false;
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM8BridgeActor* Bridge = GetWorld()->SpawnActor<AABTSM8BridgeActor>(BridgeClass, EdgeTransform, Params);
	if (Bridge == nullptr)
	{
		LogBridgePlacementFailure(
			TEXT("SpawnFailed"),
			&EdgeState,
			Geometry.AimDistanceCM,
			AllowedAimDistanceCM,
			PlayerDistanceCM,
			&AimPoint,
			bHasAimHit ? &Hit : nullptr);
		return false;
	}
	const float BridgeSpanCM = FMath::Max(
		300.0f,
		2.0f * (Geometry.WaterHalfWidthCM + BridgeBankOverlapCM));
	const FVector BridgeDimensionsCM(
		BridgeSpanCM,
		BridgeDeckWidthCM,
		BridgeDeckThicknessCM);
	Bridge->InitializeBridge(
		EdgeState.Key,
		EdgeTransform,
		BridgeDimensionsCM);
	BuiltBridgeEdges.Add(EdgeState.Key);
	int32 CarvedBarrierCount = 0;
	for (const TPair<FABTSM3CellEdgeKey, TWeakObjectPtr<AABTSM8WaterBarrierActor>>& Pair : WaterBarriers)
	{
		if (Pair.Value.IsValid()
			&& Pair.Value->OpenPassage(
				EdgeTransform,
				BridgeDimensionsCM,
				BridgeBarrierSideClearanceCM))
		{
			++CarvedBarrierCount;
		}
	}
	Inventory->RemoveItem(EABTSItemId::BridgeKit, 1);
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M8][Bridge] Built CellA=%d CellB=%d WaterType=%d Semantic=%s CertifiedSite=%d AimDistanceCM=%.1f Span=%.1fcm DeckWidth=%.1fcm PassageSideClearance=%.1fcm BarrierSegmentsCarved=%d BarrierOpened=%d"),
		EdgeState.Key.CellA,
		EdgeState.Key.CellB,
		static_cast<int32>(EdgeState.Water),
		Geometry.bBarrierSegment ? TEXT("BarrierDualEdge") : TEXT("FlowCenterline"),
		Geometry.bCertifiedBridgeSite ? 1 : 0,
		Geometry.AimDistanceCM,
		BridgeSpanCM,
		BridgeDeckWidthCM,
		BridgeBarrierSideClearanceCM,
		CarvedBarrierCount,
		CarvedBarrierCount > 0 ? 1 : 0);
	return true;
}

#if WITH_EDITOR
void AABTSM8RecoveryBridgeSystem::ToggleBridgeSiteDebug()
{
	bBridgeSiteDebugEnabled = !bBridgeSiteDebugEnabled;
	bBridgeSiteDebugReadyLogged = false;
	BridgeSiteDebugRefreshRemaining = 0.0f;
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M8][BridgeDebug] Enabled=%d Shortcut=F7"),
		bBridgeSiteDebugEnabled ? 1 : 0);
	if (!bBridgeSiteDebugEnabled && GEngine != nullptr)
	{
		GEngine->AddOnScreenDebugMessage(
			0x4D384252,
			2.0f,
			FColor::Silver,
			TEXT("M8 Bridge Site OFF (F7)"));
	}
}

void AABTSM8RecoveryBridgeSystem::RefreshBridgeSiteDebug(
	const float DeltaSeconds)
{
	BridgeSiteDebugRefreshRemaining -= FMath::Max(0.0f, DeltaSeconds);
	if (BridgeSiteDebugRefreshRemaining > 0.0f)
	{
		return;
	}
	constexpr float RefreshIntervalSeconds = 0.20f;
	constexpr float DrawLifeTimeSeconds = 0.35f;
	BridgeSiteDebugRefreshRemaining = RefreshIntervalSeconds;

	int32 BridgeSiteCount = 0;
	int32 UnbuiltBridgeSiteCount = 0;
	int32 WaterEdgeCount = 0;
	if (Planet.IsValid())
	{
		for (const FABTSM3CellEdgeState& EdgeState : Planet->GetGeneratedEdgeStates())
		{
			WaterEdgeCount += EdgeState.Water != EABTSM3WaterEdgeType::None ? 1 : 0;
			if (EdgeState.Crossing != EABTSM3CrossingType::BridgeSite)
			{
				continue;
			}
			if (!Planet->LogicalCells.IsValidIndex(EdgeState.Key.CellA)
				|| !Planet->LogicalCells.IsValidIndex(EdgeState.Key.CellB))
			{
				continue;
			}
			const FVector EdgeMidDirection = (
				Planet->LogicalCells[EdgeState.Key.CellA].UnitCenter
				+ Planet->LogicalCells[EdgeState.Key.CellB].UnitCenter).GetSafeNormal();
			FABTSM8BridgePlacementGeometry Geometry;
			if (!ResolveSemanticBridgeGeometry(
				*Planet,
				EdgeState,
				EdgeMidDirection,
				Geometry))
			{
				continue;
			}
			++BridgeSiteCount;
			const bool bBuilt = BuiltBridgeEdges.Contains(EdgeState.Key);
			UnbuiltBridgeSiteCount += bBuilt ? 0 : 1;
			const FColor EdgeColor = bBuilt ? FColor::Silver : FColor::Green;
			const FVector Up = Geometry.BridgeTransform.GetUnitAxis(EAxis::Z);
			const FVector AcrossRiver = Geometry.BridgeTransform.GetUnitAxis(EAxis::X);
			const FVector MarkerCenter = Geometry.BridgeTransform.GetLocation() + Up * 100.0f;
			const float HalfMarkerLengthCM = FMath::Max(
				150.0f,
				Geometry.WaterHalfWidthCM + BridgeBankOverlapCM);
			const FVector EdgeStart = MarkerCenter - AcrossRiver * HalfMarkerLengthCM;
			const FVector EdgeEnd = MarkerCenter + AcrossRiver * HalfMarkerLengthCM;
			DrawDebugLine(
				GetWorld(),
				EdgeStart,
				EdgeEnd,
				EdgeColor,
				false,
				DrawLifeTimeSeconds,
				0,
				16.0f);
			DrawDebugSphere(
				GetWorld(),
				MarkerCenter,
				110.0f,
				16,
				FColor::Yellow,
				false,
				DrawLifeTimeSeconds,
				0,
				6.0f);
			DrawDebugDirectionalArrow(
				GetWorld(),
				MarkerCenter,
				MarkerCenter + Up * 650.0f,
				120.0f,
				FColor::Yellow,
				false,
				DrawLifeTimeSeconds,
				0,
				8.0f);
			DrawDebugString(
				GetWorld(),
				MarkerCenter + Up * 700.0f,
				FString::Printf(
					TEXT("BRIDGE SITE (%d,%d) %s"),
					EdgeState.Key.CellA,
					EdgeState.Key.CellB,
					bBuilt ? TEXT("BUILT") : TEXT("UNBUILT")),
				nullptr,
				EdgeColor,
				DrawLifeTimeSeconds,
				false,
				1.25f);
		}

		APlayerController* Controller = GetWorld() != nullptr
			? GetWorld()->GetFirstPlayerController()
			: nullptr;
		FHitResult CursorHit;
		if (Controller != nullptr
			&& Controller->GetHitResultUnderCursor(ECC_Visibility, false, CursorHit)
			&& CursorHit.bBlockingHit)
		{
			const FVector AimDirection = (
				CursorHit.ImpactPoint - Planet->GetPlanetCenterWorld()).GetSafeNormal();
			FABTSM3CellEdgeState SelectedEdge;
			FABTSM8BridgePlacementGeometry SelectedGeometry;
			if (FindNearestUnbuiltWaterSegment(
				AimDirection,
				SelectedEdge,
				SelectedGeometry))
			{
				const FVector Up = SelectedGeometry.BridgeTransform.GetUnitAxis(EAxis::Z);
				const FVector AcrossRiver = SelectedGeometry.BridgeTransform.GetUnitAxis(EAxis::X);
				const FVector MarkerCenter = SelectedGeometry.BridgeTransform.GetLocation()
					+ Up * 130.0f;
				const float HalfSpanCM = FMath::Max(
					150.0f,
					SelectedGeometry.WaterHalfWidthCM + BridgeBankOverlapCM);
				const bool bSelectable = SelectedGeometry.AimDistanceCM
					<= SelectedGeometry.WaterHalfWidthCM + BridgePlacementAimToleranceCM;
				const FColor SelectionColor = bSelectable ? FColor::Yellow : FColor::Orange;
				DrawDebugLine(
					GetWorld(),
					MarkerCenter - AcrossRiver * HalfSpanCM,
					MarkerCenter + AcrossRiver * HalfSpanCM,
					SelectionColor,
					false,
					DrawLifeTimeSeconds,
					0,
					20.0f);
				DrawDebugString(
					GetWorld(),
					MarkerCenter + Up * 120.0f,
					FString::Printf(
						TEXT("WATER EDGE (%d,%d) %s Aim=%.0fcm"),
						SelectedEdge.Key.CellA,
						SelectedEdge.Key.CellB,
						SelectedGeometry.bBarrierSegment
							? TEXT("BARRIER")
							: TEXT("FLOW"),
						SelectedGeometry.AimDistanceCM),
					nullptr,
					SelectionColor,
					DrawLifeTimeSeconds,
					false,
					1.1f);
			}
		}
	}

	if (WaterEdgeCount > 0 && !bBridgeSiteDebugReadyLogged)
	{
		bBridgeSiteDebugReadyLogged = true;
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][M8][BridgeDebug] Ready=1 Enabled=1 Shortcut=F7 WaterEdges=%d CertifiedBridgeSites=%d UnbuiltCertified=%d"),
			WaterEdgeCount,
			BridgeSiteCount,
			UnbuiltBridgeSiteCount);
	}
	if (GEngine != nullptr)
	{
		GEngine->AddOnScreenDebugMessage(
			0x4D384252,
			DrawLifeTimeSeconds + 0.1f,
			WaterEdgeCount > 0 ? FColor::Green : FColor::Red,
			WaterEdgeCount > 0
				? FString::Printf(
					TEXT("M8 Bridge ON (F7)  GREEN=Certified  YELLOW=Selected Water  Water=%d Certified=%d Unbuilt=%d"),
					WaterEdgeCount,
					BridgeSiteCount,
					UnbuiltBridgeSiteCount)
				: TEXT("M8 Bridge Site unavailable: M8/Planet is not ready"));
	}
}
#endif
