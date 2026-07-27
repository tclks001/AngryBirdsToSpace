// Copyright Epic Games, Inc. All Rights Reserved.

#include "Slingshot/ABTSM6SlingshotSystem.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Movement/ABTSChaosBirdMovementComponent.h"
#include "Movement/ABTSRadialForceMovementComponent.h"
#include "Party/ABTSBirdParty.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "Player/ABTSM6PlayerController.h"
#include "Slingshot/ABTSM6DestructibleProxy.h"
#include "Terrain/ABTSM3Planet.h"

void AABTSM6SlingshotSystem::BeginSettlement()
{
	if (LaunchState != EABTSM6LaunchState::Flying || !LaunchedBird.IsValid()) return;
	const float Now = GetWorld()->GetTimeSeconds();
	LaunchState = EABTSM6LaunchState::Settling;
	PhysicsSettleMonitor.BeginSettlement(Now);
	NextSettleDiagnosticTimeSeconds = Now;
	TArray<UPrimitiveComponent*> Bodies;
	CollectDynamicPhysicsBodies(Bodies);
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M6][Settle] Begin FlightSeconds=%.2f Bodies=%d LinearThreshold=%.1f AngularThreshold=%.1f Hold=%.2f MinPostActivity=%.2f MaxWait=%.2f"),
		FlightElapsedSeconds, Bodies.Num(), SettleLinearSpeedThresholdCMPerSec,
		SettleAngularSpeedThresholdDegPerSec, SettleStableHoldSeconds,
		SettleMinimumPostActivitySeconds, SettleMaximumWaitSeconds);
}

void AABTSM6SlingshotSystem::CollectDynamicPhysicsBodies(TArray<UPrimitiveComponent*>& OutBodies)
{
	OutBodies.Reset();
	DynamicProxies.RemoveAllSwap([](const TWeakObjectPtr<AABTSM6DestructibleProxy>& Entry)
	{
		return !Entry.IsValid();
	});
	for (const TWeakObjectPtr<AABTSM6DestructibleProxy>& WeakProxy : DynamicProxies)
	{
		AABTSM6DestructibleProxy* Proxy = WeakProxy.Get();
		UStaticMeshComponent* Body = Proxy ? Proxy->GetMeshComponent() : nullptr;
		if (Body != nullptr && Body->IsSimulatingPhysics()) OutBodies.Add(Body);
	}
	if (BuildingMaterialSystem.IsValid()) BuildingMaterialSystem->AppendDynamicPhysicsBodies(OutBodies);
}

void AABTSM6SlingshotSystem::MarkPhysicsActivity()
{
	if (const UWorld* World = GetWorld()) PhysicsSettleMonitor.MarkActivity(World->GetTimeSeconds());
}

void AABTSM6SlingshotSystem::UpdatePhysicsSettlement(const float DeltaSeconds)
{
	if (LaunchState != EABTSM6LaunchState::Settling || !LaunchedBird.IsValid()) return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (!LaunchedBird->IsRadiallyGrounded()) PhysicsSettleMonitor.MarkActivity(Now);
	if (BuildingMaterialSystem.IsValid()) PhysicsSettleMonitor.MarkActivityAtLeast(BuildingMaterialSystem->GetLastPhysicsActivityTimeSeconds());
	TArray<UPrimitiveComponent*> Bodies;
	CollectDynamicPhysicsBodies(Bodies);
	FABTSM6PhysicsActivitySummary Summary;
	const EABTSM6PhysicsSettleResult Result = PhysicsSettleMonitor.Update(DeltaSeconds, Now, Bodies, Summary);
	if (Now >= NextSettleDiagnosticTimeSeconds)
	{
		NextSettleDiagnosticTimeSeconds = Now + 1.0f;
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M6][Settle] Sample Bodies=%d Moving=%d Awake=%d MaxLinear=%.1f MaxAngular=%.1f Stable=%.2f SinceActivity=%.2f Elapsed=%.2f Grounded=%d"),
			Summary.ActiveBodyCount, Summary.MovingBodyCount, Summary.AwakeBodyCount,
			Summary.MaximumLinearSpeedCMPerSec, Summary.MaximumAngularSpeedDegPerSec,
			Summary.StableElapsedSeconds, Summary.SecondsSinceLastActivity,
			Summary.SettlementElapsedSeconds, LaunchedBird->IsRadiallyGrounded() ? 1 : 0);
	}
	if (Result == EABTSM6PhysicsSettleResult::Settled && LaunchedBird->IsRadiallyGrounded())
	{
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M6][Settle] Settled Bodies=%d Stable=%.2f SinceActivity=%.2f Elapsed=%.2f"),
			Summary.ActiveBodyCount, Summary.StableElapsedSeconds,
			Summary.SecondsSinceLastActivity, Summary.SettlementElapsedSeconds);
		BeginReturn();
	}
	else if (Result == EABTSM6PhysicsSettleResult::TimedOut)
	{
		UE_LOG(LogABTSRuntime, Warning,
			TEXT("[ABTS][M6][Settle] ForcedTimeout Bodies=%d Moving=%d Awake=%d MaxLinear=%.1f MaxAngular=%.1f Elapsed=%.2f Grounded=%d"),
			Summary.ActiveBodyCount, Summary.MovingBodyCount, Summary.AwakeBodyCount,
			Summary.MaximumLinearSpeedCMPerSec, Summary.MaximumAngularSpeedDegPerSec,
			Summary.SettlementElapsedSeconds, LaunchedBird->IsRadiallyGrounded() ? 1 : 0);
		BeginReturn();
	}
}

void AABTSM6SlingshotSystem::FreezeDynamicProxies()
{
	for (TWeakObjectPtr<AABTSM6DestructibleProxy>& WeakProxy : DynamicProxies)
	{
		if (AABTSM6DestructibleProxy* Proxy = WeakProxy.Get()) Proxy->Freeze();
	}
	if (BuildingMaterialSystem.IsValid()) BuildingMaterialSystem->FreezeDynamicModules();
}

void AABTSM6SlingshotSystem::BeginReturn()
{
	if (!LaunchedBird.IsValid()) return;
	// Capture the settled landing point before BeginSlingshotReturn/UpdateReturn
	// moves the bird back to the slingshot. M10 consumes this only after M6 has
	// fully restored walking mode and the party camera.
	PendingCompletedBirdId = LaunchedBird->GetBirdId();
	PendingCompletedLandingLocation = LaunchedBird->GetActorLocation();
	bHasPendingLaunchCompletion = true;
	FreezeDynamicProxies();
	LaunchedBird->BeginSlingshotReturn();
	ReturnStartLocation = LaunchedBird->GetActorLocation();
	const FVector ApproxTarget = SlingCenter - SlingForward * 230.0f;
	if (bPlanarTestMode)
	{
		ReturnTargetLocation = ApproxTarget;
		const float DesiredHeight = LaunchedBird->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 10.0f;
		ReturnTargetLocation += PlanarUp * (DesiredHeight - FVector::DotProduct(ReturnTargetLocation - PlanarOrigin, PlanarUp));
	}
	else
	{
		const FVector Direction = (ApproxTarget - Planet->GetPlanetCenterWorld()).GetSafeNormal();
		ReturnTargetLocation = Planet->GetPlanetCenterWorld() + Direction *
			(Planet->GetSurfaceRadiusAtDirection(Direction) + LaunchedBird->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 10.0f);
	}
	ReturnElapsedSeconds = 0.0f;
	LaunchState = EABTSM6LaunchState::Returning;
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M6][Return] Begin FlightSeconds=%.2f Proxies=%d"), FlightElapsedSeconds, DynamicProxies.Num());
}

void AABTSM6SlingshotSystem::UpdateReturn(const float DeltaSeconds)
{
	if (!LaunchedBird.IsValid()) { FinishReturn(); return; }
	ReturnElapsedSeconds += DeltaSeconds;
	const float Alpha = FMath::Clamp(ReturnElapsedSeconds / FMath::Max(ReturnDurationSeconds, 0.1f), 0.0f, 1.0f);
	if (bPlanarTestMode)
	{
		const FVector Location = FMath::Lerp(ReturnStartLocation, ReturnTargetLocation, FMath::SmoothStep(0.0f, 1.0f, Alpha))
			+ PlanarUp * (FMath::Sin(Alpha * PI) * 280.0f);
		LaunchedBird->SetActorLocationAndRotation(Location, FRotationMatrix::MakeFromXZ(SlingForward, PlanarUp).ToQuat(), false, nullptr, ETeleportType::TeleportPhysics);
		if (Alpha >= 1.0f) FinishReturn();
		return;
	}
	const FVector Center = Planet->GetPlanetCenterWorld();
	const FVector StartOffset = ReturnStartLocation - Center;
	const FVector EndOffset = ReturnTargetLocation - Center;
	const FQuat Arc = FQuat::FindBetweenNormals(StartOffset.GetSafeNormal(), EndOffset.GetSafeNormal());
	const FVector Direction = FQuat::Slerp(FQuat::Identity, Arc, FMath::SmoothStep(0.0f, 1.0f, Alpha)).RotateVector(StartOffset.GetSafeNormal()).GetSafeNormal();
	const float Radius = FMath::Lerp(StartOffset.Size(), EndOffset.Size(), Alpha) + FMath::Sin(Alpha * PI) * 280.0f;
	LaunchedBird->SetActorLocationAndRotation(Center + Direction * Radius, FRotationMatrix::MakeFromXZ(SlingForward, Direction).ToQuat(), false, nullptr, ETeleportType::TeleportPhysics);
	if (Alpha >= 1.0f) FinishReturn();
}

void AABTSM6SlingshotSystem::FinishReturn()
{
	const bool bShouldBroadcastCompletion = bHasPendingLaunchCompletion;
	const EABTSBirdId CompletedBirdId = PendingCompletedBirdId;
	const FVector CompletedLandingLocation = PendingCompletedLandingLocation;
	SetPouchVisualActive(false);
	if (LaunchedBird.IsValid())
	{
		if (UABTSRadialForceMovementComponent* Movement = LaunchedBird->GetForceMovementComponent()) Movement->OnBlockingImpact().RemoveAll(this);
		if (UABTSChaosBirdMovementComponent* Movement = LaunchedBird->GetChaosMovementComponent()) Movement->OnBlockingImpact().RemoveAll(this);
		LaunchedBird->FinishSlingshotReturn();
	}
	if (Party.IsValid()) Party->SetSlingshotMode(false);
	if (AABTSM6PlayerController* PC = Cast<AABTSM6PlayerController>(GetWorld()->GetFirstPlayerController()))
	{
		PC->SetLaunchModeInputBlocked(false);
		PC->RestorePartyCameraView();
	}
	LaunchState = EABTSM6LaunchState::Inactive;
	ClearCurrentTrajectoryPreview();
	ActiveCord.Reset();
	LaunchedBird.Reset();
	bHasPendingLaunchCompletion = false;
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M6][Return] Complete StaticProxies=%d"), DynamicProxies.Num());
	if (bShouldBroadcastCompletion)
	{
		LaunchCompletedNative.Broadcast(CompletedBirdId, CompletedLandingLocation);
	}
}
