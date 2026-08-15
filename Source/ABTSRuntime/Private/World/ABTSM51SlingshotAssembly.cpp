// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM51WorldSystem.h"

#include "ABTSRuntime.h"
#include "EngineUtils.h"
#include "Guide/ABTSGuideEvents.h"
#include "Inventory/ABTSInventoryComponent.h"
#include "Slingshot/ABTSM6CordConnectionRules.h"
#include "World/ABTSM51WorldActors.h"

namespace
{
constexpr float FinaleCordMaximumLengthCM = 1200.0f;
}

bool AABTSM51WorldSystem::TryConnectCord(
	AABTSM51SlingshotStake& First,
	AABTSM51SlingshotStake& Second,
	const EABTSItemId HeldCord,
	const EABTSSlingshotTier Tier,
	UABTSInventoryComponent& Inventory)
{
	if (GetWorld() == nullptr
		|| &First == &Second
		|| First.IsActorBeingDestroyed()
		|| Second.IsActorBeingDestroyed()
		|| First.HasCord()
		|| Second.HasCord()
		|| Inventory.GetQuantity(HeldCord) <= 0
		|| First.GetStakeItem() != Second.GetStakeItem())
	{
		LogPlaceFailure(TEXT("CordStateChanged"));
		return false;
	}

	EABTSSlingshotTier FirstTier = EABTSSlingshotTier::Simple;
	EABTSSlingshotTier SecondTier = EABTSSlingshotTier::Simple;
	if (!ABTSAreSlingshotPartsCompatible(
			First.GetStakeItem(),
			HeldCord,
			FirstTier)
		|| !ABTSAreSlingshotPartsCompatible(
			Second.GetStakeItem(),
			HeldCord,
			SecondTier)
		|| FirstTier != Tier
		|| SecondTier != Tier)
	{
		LogPlaceFailure(TEXT("StakeTypeOrOccupied"));
		return false;
	}

	const bool bFirstFinale =
		First.GetInstalledSlotKind()
		== EABTSSlingshotSlotKind::FinaleSpace;
	const bool bSecondFinale =
		Second.GetInstalledSlotKind()
		== EABTSSlingshotSlotKind::FinaleSpace;
	if (Tier == EABTSSlingshotTier::Space)
	{
		const bool bSameFinalePair =
			bFirstFinale
			&& bSecondFinale
			&& First.GetInstalledSlotPairId() != INDEX_NONE
			&& First.GetInstalledSlotPairId()
				== Second.GetInstalledSlotPairId()
			&& First.GetInstalledSlotSide()
				!= Second.GetInstalledSlotSide()
			&& First.GetInstalledSlotSide()
				!= EABTSSlingshotSlotSide::None
			&& Second.GetInstalledSlotSide()
				!= EABTSSlingshotSlotSide::None;
		if (!bSameFinalePair)
		{
			LogPlaceFailure(TEXT("SpaceCordRequiresSameFinalePair"));
			return false;
		}
	}
	else if (bFirstFinale || bSecondFinale)
	{
		LogPlaceFailure(TEXT("FinaleStakeRequiresSpaceCord"));
		return false;
	}

	const FABTSSlingshotVisualPreset Preset =
		ABTSMakeDefaultSlingshotVisualPreset(Tier);
	FABTSM6CordConnectionQuery Query;
	Query.EndpointA = First.GetVisualTopWorldLocation();
	Query.EndpointB = Second.GetVisualTopWorldLocation();
	Query.MaxCordLengthCM =
		Tier == EABTSSlingshotTier::Space
			? FinaleCordMaximumLengthCM
			: static_cast<float>(
				GetActiveOrdinaryMaxCordLengthCM());
	Query.CandidateCordRadiusCM =
		FMath::Max(0.1f, Preset.CordThicknessCM)
		* 0.5f
		* FMath::Max(
			FMath::Abs(Preset.CordVisual.LocalScale.X),
			FMath::Abs(Preset.CordVisual.LocalScale.Y));
	Query.ClearanceCM = CordConnectionClearanceCM;

	for (TActorIterator<AABTSM51SlingshotStake> It(GetWorld());
		It;
		++It)
	{
		AABTSM51SlingshotStake* Obstacle = *It;
		if (Obstacle == &First
			|| Obstacle == &Second
			|| Obstacle->IsActorBeingDestroyed())
		{
			continue;
		}
		FABTSM6StakeConnectionObstacle& Entry =
			Query.StakeObstacles.AddDefaulted_GetRef();
		Entry.SegmentStart =
			Obstacle->GetVisualBottomWorldLocation();
		Entry.SegmentEnd =
			Obstacle->GetVisualTopWorldLocation();
		Entry.RadiusCM =
			Obstacle->GetStakeObstructionRadiusCM();
	}
	for (TActorIterator<AABTSM51SlingshotCord> It(GetWorld());
		It;
		++It)
	{
		AABTSM51SlingshotCord* Obstacle = *It;
		if (Obstacle->IsActorBeingDestroyed())
		{
			continue;
		}
		FABTSM6CordConnectionObstacle& Entry =
			Query.CordObstacles.AddDefaulted_GetRef();
		Entry.SegmentStart = Obstacle->GetEndpointA();
		Entry.SegmentEnd = Obstacle->GetEndpointB();
		Entry.RadiusCM =
			Obstacle->GetCordObstructionRadiusCM();
	}

	const FABTSM6CordConnectionResult Geometry =
		FABTSM6CordConnectionRules::Evaluate(Query);
	if (!Geometry.IsAccepted())
	{
		UE_LOG(LogABTSRuntime, Warning,
			TEXT("[ABTS][M5.1][Cord] Rejected Reason=%s LengthCM=%.2f MaxCM=%.2f Obstacle=%d DistanceCM=%.2f"),
			FABTSM6CordConnectionRules::GetRejectReasonName(
				Geometry.RejectReason),
			Geometry.CandidateLengthCM,
			Query.MaxCordLengthCM,
			Geometry.BlockingObstacleIndex,
			Geometry.BlockingDistanceCM);
		return false;
	}

	AABTSM51SlingshotCord* Cord =
		GetWorld()->SpawnActor<AABTSM51SlingshotCord>(
			CordClass,
			FTransform::Identity);
	if (Cord == nullptr)
	{
		LogPlaceFailure(TEXT("CordSpawnFailed"));
		return false;
	}
	Cord->InitializeCordWithTier(
		&First,
		&Second,
		Query.EndpointA,
		Query.EndpointB,
		Tier);
	if (!Inventory.RemoveItem(HeldCord, 1))
	{
		Cord->Destroy();
		LogPlaceFailure(TEXT("CordInventoryCommitFailed"));
		return false;
	}
	First.SetHasCord(true);
	Second.SetHasCord(true);
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M5.1][Cord] Complete LengthCM=%.2f MaxCM=%.2f Item=%s Tier=%d FinalePair=%d"),
		Geometry.CandidateLengthCM,
		Query.MaxCordLengthCM,
		*ABTSGetItemFallbackLabel(HeldCord),
		static_cast<int32>(Tier),
		Cord->GetFinaleSlotPairId());
	FABTSGuideEventBus::Publish(this, FABTSGuideEventIds::SlingshotAssembled,
		FABTSGuideSubjects::FromSlingshotTier(Tier), Cord,
		FMath::RoundToInt(Geometry.CandidateLengthCM), Cord->GetFinaleSlotPairId());
	return true;
}
