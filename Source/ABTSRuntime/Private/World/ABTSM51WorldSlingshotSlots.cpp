// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM51WorldSystem.h"

#include "ABTSRuntime.h"
#include "Terrain/ABTSM3Planet.h"
#include "World/ABTSM51WorldActors.h"

bool AABTSM51WorldSystem::ConfigureAcceptedOrdinarySlingshotSlotSnapshot(
	const FABTSM51OrdinarySlingshotSlotSnapshot& InSnapshot)
{
	return ConfigureOrdinarySlingshotSlotSnapshot(
		InSnapshot,
		EABTSM51OrdinarySlingshotSlotSnapshotAuthority::AcceptedMonthly);
}

bool AABTSM51WorldSystem::ConfigurePreviewOrdinarySlingshotSlotSnapshot(
	const FABTSM51OrdinarySlingshotSlotSnapshot& InSnapshot)
{
	return ConfigureOrdinarySlingshotSlotSnapshot(
		InSnapshot,
		EABTSM51OrdinarySlingshotSlotSnapshotAuthority::PreviewTest);
}

bool AABTSM51WorldSystem::ConfigurePreviewFinaleFrame(
	const FABTSM51PreviewFinaleFrameContext& InContext)
{
	if (HasActorBegunPlay() || bInitialized || bSlingshotHolesSpawned)
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M5.1][PreviewFinaleFrame] Rejected: configuration must happen before BeginPlay."));
		return false;
	}

	bPreviewFinaleFrameRequested = true;
	bPreviewFinaleFrameValid = InContext.IsUsable();
	PreviewFinaleFrameContext = bPreviewFinaleFrameValid
		? InContext
		: FABTSM51PreviewFinaleFrameContext();
	if (!bPreviewFinaleFrameValid)
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M5.1][PreviewFinaleFrame] Rejected: invalid authority, identity, frame, or hash."));
	}
	return bPreviewFinaleFrameValid;
}

const FABTSM51PreviewFinaleFrameContext*
AABTSM51WorldSystem::GetPreviewFinaleFrameContext() const
{
	return bPreviewFinaleFrameRequested && bPreviewFinaleFrameValid
		? &PreviewFinaleFrameContext
		: nullptr;
}

const FABTSM110FinaleLocalFrame*
AABTSM51WorldSystem::ResolveFinaleFrame() const
{
	if (bPreviewFinaleFrameRequested)
	{
		return bPreviewFinaleFrameValid
			? &PreviewFinaleFrameContext.Frame
			: nullptr;
	}
	return Planet.IsValid()
		? &Planet->GetFinaleLaunchFrame()
		: nullptr;
}

const FABTSM110FinaleLocalFrame*
AABTSM51WorldSystem::GetActiveFinaleFrame() const
{
	const FABTSM110FinaleLocalFrame* Frame = ResolveFinaleFrame();
	return Frame != nullptr && Frame->IsUsable()
		? Frame
		: nullptr;
}

bool AABTSM51WorldSystem::ConfigureOrdinarySlingshotSlotSnapshot(
	const FABTSM51OrdinarySlingshotSlotSnapshot& InSnapshot,
	const EABTSM51OrdinarySlingshotSlotSnapshotAuthority InAuthority)
{
	if (HasActorBegunPlay() || bInitialized || bSlingshotHolesSpawned)
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M5.1][OrdinarySlots] Snapshot rejected: configuration must happen before BeginPlay."));
		return false;
	}

	bOrdinarySlotSnapshotRequested = true;
	OrdinarySlotSnapshotAuthority = InAuthority;
	bOrdinarySlotSnapshotValid =
		InAuthority
			!= EABTSM51OrdinarySlingshotSlotSnapshotAuthority::None
		&& InSnapshot.IsStructurallyUsable();
	OrdinarySlotSnapshot = bOrdinarySlotSnapshotValid
		? InSnapshot
		: FABTSM51OrdinarySlingshotSlotSnapshot();
	if (!bOrdinarySlotSnapshotValid)
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M5.1][OrdinarySlots] Snapshot rejected: invalid identity, length, group, or Cell identity."));
	}
	return bOrdinarySlotSnapshotValid;
}

int32 AABTSM51WorldSystem::GetActiveOrdinaryMaxCordLengthCM() const
{
	if (bOrdinarySlotSnapshotRequested)
	{
		return bOrdinarySlotSnapshotValid
			? OrdinarySlotSnapshot.MaxCordLengthCM
			: 0;
	}
	return FMath::Clamp(
		CompatibilityMaxCordLengthCM,
		FABTSM51OrdinarySlingshotSlotSnapshot::MinimumCordLengthCM,
		FABTSM51OrdinarySlingshotSlotSnapshot::MaximumCordLengthCM);
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

bool AABTSM51WorldSystem::SpawnSlingshotHoles()
{
	if (bSlingshotHolesSpawned)
	{
		return true;
	}

	const FABTSM110FinaleLocalFrame* ResolvedFinaleFrame =
		ResolveFinaleFrame();
	const FABTSM110FinaleLocalFrame EmptyFinaleFrame;
	const FABTSM110FinaleLocalFrame& FinaleFrame =
		ResolvedFinaleFrame != nullptr
			? *ResolvedFinaleFrame
			: EmptyFinaleFrame;
	TArray<int32> StandardCellIds;
	bool bOrdinaryPlanValid = true;
	if (bOrdinarySlotSnapshotRequested)
	{
		bOrdinaryPlanValid = bOrdinarySlotSnapshotValid
			&& OrdinarySlotSnapshot.TryBuildCellList(
				Planet->LogicalCells.Num(),
				StandardCellIds);
		if (bOrdinaryPlanValid)
		{
			const TArray<FABTSM3CellState>& CellStates =
				Planet->GetGeneratedCellStates();
			const bool bValidateCurrentMonthlyCellState =
				OrdinarySlotSnapshotAuthority
					!= EABTSM51OrdinarySlingshotSlotSnapshotAuthority::PreviewTest;
			for (const int32 CellId : StandardCellIds)
			{
				if (!CellStates.IsValidIndex(CellId)
					|| (bValidateCurrentMonthlyCellState
						&& (CellStates[CellId].bWater
							|| CellStates[CellId].bBuildingAnchor))
					|| (FinaleFrame.IsUsable()
						&& CellId == FinaleFrame.AnchorCellId))
				{
					bOrdinaryPlanValid = false;
					StandardCellIds.Reset();
					break;
				}
			}
		}
	}
	else
	{
		TSet<int32> SeenCells;
		for (const FABTSM3TaskNode& Task : Planet->GetGeneratedTasks())
		{
			if (Task.Type != EABTSM3TaskType::SlingshotRange
				|| !Planet->LogicalCells.IsValidIndex(Task.SeedCellId))
			{
				continue;
			}
			const int32 CellA = Task.SeedCellId;
			int32 CellB = INDEX_NONE;
			for (const int32 Neighbor :
				Planet->LogicalCells[CellA].NeighborCellIds)
			{
				if (Task.CellIds.Contains(Neighbor)
					&& Planet->GetGeneratedCellStates().IsValidIndex(Neighbor)
					&& !Planet->GetGeneratedCellStates()[Neighbor].bWater)
				{
					CellB = Neighbor;
					break;
				}
			}
			if (CellB == INDEX_NONE)
			{
				continue;
			}
			for (const int32 CellId : {CellA, CellB})
			{
				if (!SeenCells.Contains(CellId))
				{
					SeenCells.Add(CellId);
					StandardCellIds.Add(CellId);
				}
			}
		}
	}

	TArray<FTransform> StandardTransforms;
	if (bOrdinaryPlanValid)
	{
		StandardTransforms.Reserve(StandardCellIds.Num());
		for (const int32 CellId : StandardCellIds)
		{
			FTransform Transform;
			if (!QueryCellTransform(CellId, 4.0f, Transform))
			{
				bOrdinaryPlanValid = false;
				StandardTransforms.Reset();
				break;
			}
			StandardTransforms.Add(Transform);
		}
	}

	TArray<TWeakObjectPtr<AABTSM51SlingshotDirtHole>>
		SpawnedOrdinarySlots;
	if (bOrdinaryPlanValid)
	{
		SpawnedOrdinarySlots.Reserve(StandardCellIds.Num());
		for (int32 Index = 0;
			Index < StandardCellIds.Num();
			++Index)
		{
			AABTSM51SlingshotDirtHole* Hole =
				GetWorld()->SpawnActor<AABTSM51SlingshotDirtHole>(
					DirtHoleClass,
					StandardTransforms[Index]);
			if (Hole == nullptr)
			{
				bOrdinaryPlanValid = false;
				break;
			}
			Hole->InitializeHole(StandardCellIds[Index]);
			SpawnedOrdinarySlots.Add(Hole);
		}
	}
	if (!bOrdinaryPlanValid)
	{
		for (const TWeakObjectPtr<AABTSM51SlingshotDirtHole>& Hole :
			SpawnedOrdinarySlots)
		{
			if (Hole.IsValid())
			{
				Hole->Destroy();
			}
		}
		SpawnedOrdinarySlots.Reset();
		StandardCellIds.Reset();
	}
	else
	{
		OrdinarySlots = MoveTemp(SpawnedOrdinarySlots);
		for (const int32 CellId : StandardCellIds)
		{
			OccupiedCells.Add(CellId);
		}
	}

	int32 FinaleHoleCount = 0;
	if (!FinaleFrame.IsUsable())
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M11.0][FinaleSlots] Spawn rejected: M3 finale frame is not usable."));
	}
	else
	{
		const auto SpawnFinaleSlot =
			[this, &FinaleFrame, &FinaleHoleCount](
				const FVector& WorldLocation,
				const EABTSSlingshotSlotSide Side)
				-> AABTSM51SlingshotDirtHole*
		{
			const FTransform Transform(
				FinaleFrame.WorldTransform.GetRotation(),
				WorldLocation);
			AABTSM51SlingshotDirtHole* Hole =
				GetWorld()->SpawnActor<AABTSM51SlingshotDirtHole>(
					DirtHoleClass,
					Transform);
			if (Hole == nullptr)
			{
				return nullptr;
			}
			Hole->InitializeFinaleSpaceSlot(
				FinaleFrame.AnchorCellId,
				FinaleFrame.SlotPairId,
				Side);
			++FinaleHoleCount;
			return Hole;
		};
		FinaleLeftSlot = SpawnFinaleSlot(
			FinaleFrame.LeftSlotWorldLocation,
			EABTSSlingshotSlotSide::Left);
		FinaleRightSlot = SpawnFinaleSlot(
			FinaleFrame.RightSlotWorldLocation,
			EABTSSlingshotSlotSide::Right);
		AABTSM51SlingshotDirtHole* LeftSlot =
			FinaleLeftSlot.Get();
		AABTSM51SlingshotDirtHole* RightSlot =
			FinaleRightSlot.Get();
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
		TEXT("[ABTS][M11.0][SlingshotSlots] Standard=%d Finale=%d Pair=%d AnchorCell=%d Authority=%s Candidate=%d PreviewHash=%016llX ContextHash=%016llX MonthlyAccepted=0"),
		OrdinarySlots.Num(),
		FinaleHoleCount,
		FinaleFrame.SlotPairId,
		FinaleFrame.AnchorCellId,
		bPreviewFinaleFrameRequested
			? TEXT("PreviewTest")
			: TEXT("CompatibilityProduction"),
		bPreviewFinaleFrameValid
			? PreviewFinaleFrameContext.SourceRouteCandidateId
			: INDEX_NONE,
		static_cast<unsigned long long>(
			bPreviewFinaleFrameValid
				? static_cast<uint64>(
					PreviewFinaleFrameContext.SourcePreviewHash)
				: 0ull),
		static_cast<unsigned long long>(
			bPreviewFinaleFrameValid
				? static_cast<uint64>(
					PreviewFinaleFrameContext.ContextHash)
				: 0ull));
	const TCHAR* OrdinarySourceName =
		bOrdinarySlotSnapshotRequested
			? OrdinarySlotSnapshotAuthority
					== EABTSM51OrdinarySlingshotSlotSnapshotAuthority::PreviewTest
				? TEXT("PreviewTestSnapshot")
				: TEXT("AcceptedMonthlySnapshot")
			: TEXT("CompatibilityTaskGraph");
	const unsigned long long LayoutHash =
		static_cast<unsigned long long>(
			bOrdinarySlotSnapshotValid
				? OrdinarySlotSnapshot.LayoutHash
				: 0);
	const unsigned long long CandidateHash =
		static_cast<unsigned long long>(
			bOrdinarySlotSnapshotValid
				? OrdinarySlotSnapshot.CandidateHash
				: 0);
	if (bOrdinaryPlanValid)
	{
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M5.1][OrdinarySlots] Source=%s Accepted=1 Holes=%d MaxCordLengthCM=%d Layout=%016llX Candidate=%016llX"),
			OrdinarySourceName,
			OrdinarySlots.Num(),
			GetActiveOrdinaryMaxCordLengthCM(),
			LayoutHash,
			CandidateHash);
	}
	else
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M5.1][OrdinarySlots] Source=%s Accepted=0 Holes=0 MaxCordLengthCM=%d Layout=%016llX Candidate=%016llX"),
			OrdinarySourceName,
			GetActiveOrdinaryMaxCordLengthCM(),
			LayoutHash,
			CandidateHash);
	}
	const bool bRequiredFinalePairReady =
		!bPreviewFinaleFrameRequested || FinaleHoleCount == 2;
	if (!bRequiredFinalePairReady)
	{
		for (const TWeakObjectPtr<AABTSM51SlingshotDirtHole>& Hole :
			OrdinarySlots)
		{
			if (Hole.IsValid())
			{
				Hole->Destroy();
			}
		}
		OrdinarySlots.Reset();
		for (const int32 CellId : StandardCellIds)
		{
			OccupiedCells.Remove(CellId);
		}
	}
	bSlingshotHolesSpawned =
		bOrdinaryPlanValid && bRequiredFinalePairReady;
	return bSlingshotHolesSpawned;
}
