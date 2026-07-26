// Copyright Epic Games, Inc. All Rights Reserved.

#include "Slingshot/ABTSM6SlingshotSystem.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Slingshot/ABTSM6DestructibleProxy.h"
#include "Terrain/ABTSM3Planet.h"
#include "TestStage/ABTSM71TestStageActors.h"

namespace
{
struct FABTSM6StartupHISMScanData
{
	UHierarchicalInstancedStaticMeshComponent* Component = nullptr;
	TArray<FTransform> WorldTransforms;
	TSet<int32> SelectedCandidateIndices;
};

bool ABTSStartupBodiesOverlap(
	UHierarchicalInstancedStaticMeshComponent& ComponentA,
	const int32 InstanceA,
	const FTransform& TransformA,
	UHierarchicalInstancedStaticMeshComponent& ComponentB,
	const int32 InstanceB)
{
	FBodyInstance* BodyA = ComponentA.GetBodyInstance(NAME_None, false, InstanceA);
	FBodyInstance* BodyB = ComponentB.GetBodyInstance(NAME_None, false, InstanceB);
	if (BodyA == nullptr || BodyB == nullptr || !BodyA->IsValidBodyInstance() || !BodyB->IsValidBodyInstance())
	{
		return false;
	}
	return BodyA->OverlapTestForBody(
		TransformA.GetLocation(), TransformA.GetRotation().GetNormalized(), BodyB, false);
}
}

void AABTSM6SlingshotSystem::UpdateStartupPhysicsWarmup(const float DeltaSeconds)
{
	if (GetWorld() == nullptr || bStartupPhysicsWarmupComplete) return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (!bStartupPhysicsWarmupStarted)
	{
		if (!ResolveDependencies() || Now < StartupPhysicsWarmupEligibleTimeSeconds)
		{
			if (!bStartupPhysicsWarmupWaitingLogged && Now >= StartupPhysicsWarmupEligibleTimeSeconds)
			{
				bStartupPhysicsWarmupWaitingLogged = true;
				UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][StartupPhysics] Waiting for party/planet dependencies before bounded Chaos warmup."));
			}
			return;
		}
		BeginStartupPhysicsWarmup();
		return;
	}

	TArray<UPrimitiveComponent*> Bodies;
	CollectDynamicPhysicsBodies(Bodies);
	FABTSM6PhysicsActivitySummary Summary;
	const EABTSM6PhysicsSettleResult Result = StartupPhysicsSettleMonitor.Update(DeltaSeconds, Now, Bodies, Summary);
	if (Now >= NextStartupWarmupDiagnosticTimeSeconds)
	{
		NextStartupWarmupDiagnosticTimeSeconds = Now + 1.0f;
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][StartupPhysics] Phase=%s Batch=%d Bodies=%d Moving=%d Awake=%d MaxLinear=%.1f MaxAngular=%.1f Stable=%.2f Elapsed=%.2f Remaining=%d"),
			bStartupBuildingSettlementActive ? TEXT("Buildings") : TEXT("HISM"),
			StartupHISMWarmupBatchIndex, Summary.ActiveBodyCount, Summary.MovingBodyCount, Summary.AwakeBodyCount,
			Summary.MaximumLinearSpeedCMPerSec, Summary.MaximumAngularSpeedDegPerSec,
			Summary.StableElapsedSeconds, Summary.SettlementElapsedSeconds,
			FMath::Max(0, StartupHISMWarmupTotalCandidates - StartupHISMWarmupPromotedTotal));
	}

	if (bStartupBuildingSettlementActive)
	{
		const bool bNoActiveBuildingBodies = Bodies.IsEmpty();
		if (!bNoActiveBuildingBodies
			&& Result != EABTSM6PhysicsSettleResult::Settled
			&& Result != EABTSM6PhysicsSettleResult::TimedOut) return;
		if (Result == EABTSM6PhysicsSettleResult::TimedOut && !bNoActiveBuildingBodies)
		{
			++StartupHISMWarmupTimedOutBatches;
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][StartupPhysics] BuildingTimeout Limit=%.1fs Bodies=%d Moving=%d Awake=%d MaxLinear=%.1f MaxAngular=%.1f; forcing structures static."),
				StartupSettleDiagnosticPeriodSeconds, Summary.ActiveBodyCount, Summary.MovingBodyCount,
				Summary.AwakeBodyCount, Summary.MaximumLinearSpeedCMPerSec, Summary.MaximumAngularSpeedDegPerSec);
		}
		else if (bNoActiveBuildingBodies)
		{
			UE_LOG(LogABTSRuntime, Log,
				TEXT("[ABTS][StartupPhysics] BuildingsSettled NoDynamicBodies=1 Elapsed=%.2f"),
				Summary.SettlementElapsedSeconds);
		}
		FreezeDynamicProxies();
		bStartupBuildingSettlementActive = false;
		if (HasPendingStartupHISMCandidates() && StartNextStartupHISMWarmupBatch() > 0) return;
		FinishStartupPhysicsWarmup(Summary);
		return;
	}

	const float RelaxationSeconds = FMath::Clamp(
		StartupHISMBatchRelaxationSeconds, 0.1f, FMath::Max(0.1f, StartupSettleDiagnosticPeriodSeconds));
	if (Result != EABTSM6PhysicsSettleResult::Settled
		&& Summary.SettlementElapsedSeconds < RelaxationSeconds) return;
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][StartupPhysics] BatchRelaxed Batch=%d Duration=%.2f Bodies=%d MovingBeforeFreeze=%d"),
		StartupHISMWarmupBatchIndex, Summary.SettlementElapsedSeconds,
		Summary.ActiveBodyCount, Summary.MovingBodyCount);
	const int32 RestoredInstanceCount = RestoreStartupHISMProxies();
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][StartupPhysics] BatchRestored Batch=%d HISMInstances=%d DynamicProxies=%d"),
		StartupHISMWarmupBatchIndex, RestoredInstanceCount, DynamicProxies.Num());
	if (HasPendingStartupHISMCandidates() && StartNextStartupHISMWarmupBatch() > 0) return;
	FinishStartupPhysicsWarmup(Summary);
}

void AABTSM6SlingshotSystem::BeginStartupPhysicsWarmup()
{
	if (bStartupPhysicsWarmupStarted || GetWorld() == nullptr) return;
	bStartupPhysicsWarmupStarted = true;
	StartupHISMWarmupQueues.Reset();
	StartupProxySourceHISMs.Reset();
	StartupHISMWarmupTotalCandidates = 0;
	StartupHISMWarmupPromotedTotal = 0;
	StartupHISMWarmupBatchIndex = 0;
	StartupHISMWarmupTimedOutBatches = 0;
	bStartupBuildingSettlementActive = BuildingMaterialSystem.IsValid();
	const float Now = GetWorld()->GetTimeSeconds();
	NextStartupWarmupDiagnosticTimeSeconds = Now;

	if (BuildingMaterialSystem.IsValid())
	{
		BuildingMaterialSystem->BeginLaunchPhysics(
			bPlanarTestMode,
			bPlanarTestMode ? PlanarUp : Planet->GetPlanetCenterWorld(),
			LaunchObjectGravityAccelerationCMPerSec2);
		// This is a non-gameplay solve. Initial contacts cannot become damage.
		BuildingMaterialSystem->SetDynamicContactDamageGraceSeconds(1000000.0f);
	}

	TArray<UHierarchicalInstancedStaticMeshComponent*> StartupHISMs;
	if (bPlanarTestMode)
	{
		for (TActorIterator<AABTSM71PlaceableHISMActor> It(GetWorld()); It; ++It)
		{
			if (UHierarchicalInstancedStaticMeshComponent* HISM = It->GetHISM()) StartupHISMs.Add(HISM);
		}
	}
	else if (Planet.IsValid())
	{
		if (Planet->ForestHISM) StartupHISMs.Add(Planet->ForestHISM);
		if (Planet->RockHISM) StartupHISMs.Add(Planet->RockHISM);
	}

	int32 OverlapPairCount = 0;
	int32 FallbackPairCount = 0;
	const double ScanStartSeconds = FPlatformTime::Seconds();
	StartupHISMWarmupTotalCandidates = BuildStartupHISMOverlapQueues(
		StartupHISMs, OverlapPairCount, FallbackPairCount);
	const double ScanMilliseconds = (FPlatformTime::Seconds() - ScanStartSeconds) * 1000.0;
	int32 TotalHISMInstanceCount = 0;
	for (const UHierarchicalInstancedStaticMeshComponent* HISM : StartupHISMs)
	{
		TotalHISMInstanceCount += HISM ? HISM->GetInstanceCount() : 0;
	}
	int32 FirstBatchCount = 0;
	if (bStartupBuildingSettlementActive)
	{
		// Structural modules get the strict velocity-based settlement first. HISM
		// relaxation begins only after structures are frozen, keeping each phase bounded.
		StartupPhysicsSettleMonitor.BeginSettlement(Now);
	}
	else
	{
		FirstBatchCount = StartNextStartupHISMWarmupBatch();
		if (FirstBatchCount <= 0)
		{
			FABTSM6PhysicsActivitySummary EmptySummary;
			FinishStartupPhysicsWarmup(EmptySummary);
			return;
		}
	}

	TArray<UPrimitiveComponent*> Bodies;
	CollectDynamicPhysicsBodies(Bodies);
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][StartupPhysics] Begin Planar=%d BuildingSystem=%d HISMInstances=%d OverlapPairs=%d FallbackPairs=%d Candidates=%d BatchLimit=%d Relaxation=%.2f FirstBatch=%d DynamicBodies=%d ScanMs=%.2f BuildingHardLimit=%.1f"),
		bPlanarTestMode ? 1 : 0, BuildingMaterialSystem.IsValid() ? 1 : 0,
		TotalHISMInstanceCount,
		OverlapPairCount, FallbackPairCount, StartupHISMWarmupTotalCandidates,
		FMath::Clamp(StartupHISMMaxSimultaneousBodies, 8, 512), StartupHISMBatchRelaxationSeconds,
		FirstBatchCount, Bodies.Num(),
		ScanMilliseconds, StartupSettleDiagnosticPeriodSeconds);
}

int32 AABTSM6SlingshotSystem::BuildStartupHISMOverlapQueues(
	const TArray<UHierarchicalInstancedStaticMeshComponent*>& HISMs,
	int32& OutOverlapPairCount,
	int32& OutFallbackPairCount)
{
	OutOverlapPairCount = 0;
	OutFallbackPairCount = 0;
	StartupHISMWarmupQueues.Reset();
	const float SearchRadiusCM = FMath::Max(10.0f, StartupHISMOverlapSearchRadiusCM);
	const float SearchRadiusSquared = FMath::Square(SearchRadiusCM);
	const float FallbackDistanceSquared = FMath::Square(FMath::Max(0.0f, StartupHISMFallbackCenterDistanceCM));

	TArray<FABTSM6StartupHISMScanData> ScanData;
	for (UHierarchicalInstancedStaticMeshComponent* HISM : HISMs)
	{
		if (HISM == nullptr || HISM->GetInstanceCount() <= 0) continue;
		FABTSM6StartupHISMScanData& Data = ScanData.AddDefaulted_GetRef();
		Data.Component = HISM;
		Data.WorldTransforms.SetNum(HISM->GetInstanceCount());
		for (int32 InstanceIndex = 0; InstanceIndex < HISM->GetInstanceCount(); ++InstanceIndex)
		{
			HISM->GetInstanceTransform(InstanceIndex, Data.WorldTransforms[InstanceIndex], true);
		}
	}

	for (int32 ComponentAIndex = 0; ComponentAIndex < ScanData.Num(); ++ComponentAIndex)
	{
		FABTSM6StartupHISMScanData& DataA = ScanData[ComponentAIndex];
		for (int32 InstanceA = 0; InstanceA < DataA.WorldTransforms.Num(); ++InstanceA)
		{
			if (DataA.SelectedCandidateIndices.Contains(InstanceA)) continue;
			const FTransform& TransformA = DataA.WorldTransforms[InstanceA];
			bool bSelectedCurrentInstance = false;
			for (int32 ComponentBIndex = ComponentAIndex; ComponentBIndex < ScanData.Num(); ++ComponentBIndex)
			{
				FABTSM6StartupHISMScanData& DataB = ScanData[ComponentBIndex];
				TArray<int32> NearbyInstances = DataB.Component->GetInstancesOverlappingSphere(
					TransformA.GetLocation(), SearchRadiusCM, true);
				NearbyInstances.Sort();
				for (const int32 InstanceB : NearbyInstances)
				{
					if (!DataB.WorldTransforms.IsValidIndex(InstanceB)) continue;
					if (ComponentAIndex == ComponentBIndex && InstanceB <= InstanceA) continue;
					if (DataB.SelectedCandidateIndices.Contains(InstanceB)) continue;
					const FVector Delta = DataB.WorldTransforms[InstanceB].GetLocation() - TransformA.GetLocation();
					const float CenterDistanceSquared = Delta.SizeSquared();
					if (CenterDistanceSquared > SearchRadiusSquared) continue;

					const bool bExactOverlap = ABTSStartupBodiesOverlap(
						*DataA.Component, InstanceA, TransformA, *DataB.Component, InstanceB);
					const bool bFallbackOverlap = !bExactOverlap
						&& FallbackDistanceSquared > 0.0f
						&& CenterDistanceSquared <= FallbackDistanceSquared;
					if (!bExactOverlap && !bFallbackOverlap) continue;
					++OutOverlapPairCount;
					OutFallbackPairCount += bFallbackOverlap ? 1 : 0;

					if (ComponentAIndex == ComponentBIndex)
					{
						// Keep the lower index as the static anchor; removing descending indices later
						// therefore never invalidates an unprocessed queue entry.
						DataB.SelectedCandidateIndices.Add(InstanceB);
					}
					else
					{
						// One dynamic body is sufficient to resolve a pair. Prefer the earlier
						// component (forest before rock on the spherical map) as the movable side.
						DataA.SelectedCandidateIndices.Add(InstanceA);
						bSelectedCurrentInstance = true;
						break;
					}
				}
				if (bSelectedCurrentInstance) break;
			}
		}
	}

	int32 TotalCandidateCount = 0;
	for (FABTSM6StartupHISMScanData& Data : ScanData)
	{
		if (Data.SelectedCandidateIndices.IsEmpty()) continue;
		FABTSM6StartupHISMWarmupQueue& Queue = StartupHISMWarmupQueues.AddDefaulted_GetRef();
		Queue.Component = Data.Component;
		Queue.CandidateIndicesDescending = Data.SelectedCandidateIndices.Array();
		Queue.CandidateIndicesDescending.Sort(TGreater<int32>());
		TotalCandidateCount += Queue.CandidateIndicesDescending.Num();
	}
	return TotalCandidateCount;
}

int32 AABTSM6SlingshotSystem::StartNextStartupHISMWarmupBatch()
{
	const int32 BatchLimit = FMath::Clamp(StartupHISMMaxSimultaneousBodies, 8, 512);
	int32 PromotedCount = 0;
	for (FABTSM6StartupHISMWarmupQueue& Queue : StartupHISMWarmupQueues)
	{
		UHierarchicalInstancedStaticMeshComponent* HISM = Queue.Component.Get();
		if (HISM == nullptr)
		{
			Queue.NextCandidateOffset = Queue.CandidateIndicesDescending.Num();
			continue;
		}
		const EABTSM6ImpactMaterial Material = ResolveMaterial(HISM);
		const FABTSM6MaterialImpactProfile& Profile = GetMaterialProfile(Material);
		while (Queue.NextCandidateOffset < Queue.CandidateIndicesDescending.Num() && PromotedCount < BatchLimit)
		{
			const int32 InstanceIndex = Queue.CandidateIndicesDescending[Queue.NextCandidateOffset++];
			const int32 ProxyCountBeforePromotion = DynamicProxies.Num();
			if (PromoteOrBreakHISM(*HISM, InstanceIndex, Material, Profile,
				0.0f, FVector::ZeroVector, 0.0f, BIG_NUMBER, 0.0f))
			{
				++PromotedCount;
				if (DynamicProxies.Num() > ProxyCountBeforePromotion)
				{
					if (AABTSM6DestructibleProxy* Proxy = DynamicProxies.Last().Get())
					{
						StartupProxySourceHISMs.Add(Proxy, HISM);
					}
				}
			}
		}
		if (PromotedCount >= BatchLimit) break;
	}

	if (PromotedCount > 0)
	{
		++StartupHISMWarmupBatchIndex;
		StartupHISMWarmupPromotedTotal += PromotedCount;
		const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
		StartupPhysicsSettleMonitor.BeginSettlement(Now);
		NextStartupWarmupDiagnosticTimeSeconds = Now;
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][StartupPhysics] BatchBegin Batch=%d Promoted=%d TotalPromoted=%d/%d Limit=%d"),
			StartupHISMWarmupBatchIndex, PromotedCount, StartupHISMWarmupPromotedTotal,
			StartupHISMWarmupTotalCandidates, BatchLimit);
	}
	return PromotedCount;
}

bool AABTSM6SlingshotSystem::HasPendingStartupHISMCandidates() const
{
	for (const FABTSM6StartupHISMWarmupQueue& Queue : StartupHISMWarmupQueues)
	{
		if (Queue.NextCandidateOffset < Queue.CandidateIndicesDescending.Num()) return true;
	}
	return false;
}

int32 AABTSM6SlingshotSystem::RestoreStartupHISMProxies()
{
	TMap<UHierarchicalInstancedStaticMeshComponent*, TArray<FTransform>> RestoredTransformsByHISM;
	TSet<AABTSM6DestructibleProxy*> RestoredProxies;
	for (const TPair<TWeakObjectPtr<AABTSM6DestructibleProxy>, TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent>>& Pair
		: StartupProxySourceHISMs)
	{
		AABTSM6DestructibleProxy* Proxy = Pair.Key.Get();
		UHierarchicalInstancedStaticMeshComponent* HISM = Pair.Value.Get();
		if (Proxy == nullptr || HISM == nullptr) continue;
		const UStaticMeshComponent* Mesh = Proxy->GetMeshComponent();
		if (Mesh == nullptr) continue;
		RestoredTransformsByHISM.FindOrAdd(HISM).Add(Mesh->GetComponentTransform());
		RestoredProxies.Add(Proxy);
	}

	int32 RestoredInstanceCount = 0;
	for (TPair<UHierarchicalInstancedStaticMeshComponent*, TArray<FTransform>>& Pair : RestoredTransformsByHISM)
	{
		if (Pair.Key == nullptr || Pair.Value.IsEmpty()) continue;
		Pair.Key->AddInstances(Pair.Value, false, true, false);
		RestoredInstanceCount += Pair.Value.Num();
	}
	for (AABTSM6DestructibleProxy* Proxy : RestoredProxies)
	{
		if (Proxy == nullptr) continue;
		Proxy->Freeze();
		Proxy->Destroy();
	}
	DynamicProxies.RemoveAllSwap([&RestoredProxies](const TWeakObjectPtr<AABTSM6DestructibleProxy>& Entry)
	{
		return !Entry.IsValid() || RestoredProxies.Contains(Entry.Get());
	});
	StartupProxySourceHISMs.Reset();
	return RestoredInstanceCount;
}

void AABTSM6SlingshotSystem::FinishStartupPhysicsWarmup(const FABTSM6PhysicsActivitySummary& Summary)
{
	if (bStartupPhysicsWarmupComplete) return;
	FreezeDynamicProxies();
	bStartupPhysicsWarmupComplete = true;
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][StartupPhysics] Complete WorldReady=1 LastBatchBodies=%d Stable=%.2f Elapsed=%.2f Candidates=%d Promoted=%d Batches=%d TimedOutBatches=%d StaticProxies=%d"),
		Summary.ActiveBodyCount, Summary.StableElapsedSeconds, Summary.SettlementElapsedSeconds,
		StartupHISMWarmupTotalCandidates, StartupHISMWarmupPromotedTotal,
		StartupHISMWarmupBatchIndex, StartupHISMWarmupTimedOutBatches, DynamicProxies.Num());
}
