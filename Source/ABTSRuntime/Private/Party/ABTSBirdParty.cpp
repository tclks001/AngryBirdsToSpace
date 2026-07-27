// Copyright Epic Games, Inc. All Rights Reserved.

#include "Party/ABTSBirdParty.h"

#include "ABTSRuntime.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Party/ABTSBirdPartySettings.h"
#include "Planet/ABTSM2Planet.h"
#include "Player/ABTSM25BirdCharacter.h"

namespace
{
	constexpr int32 BirdCount = 4;
	constexpr int32 MaxPathSamples = 192;
	constexpr int32 MaxJumpEvents = 8;
	constexpr float JumpEventLifetimeSeconds = 4.0f;

	FABTSBirdPresentationConfig MakeDefaultPresentation(const EABTSBirdId BirdId)
	{
		FABTSBirdPresentationConfig Result;
		Result.BirdId = BirdId;
		switch (BirdId)
		{
		case EABTSBirdId::Blue:
			Result.DisplayName = FText::FromString(TEXT("青翎"));
			Result.FallbackColor = FLinearColor(0.04f, 0.28f, 0.95f);
			Result.SlingshotCapability = EABTSBirdSlingshotCapability::TwigScout;
			break;
		case EABTSBirdId::Yellow:
			Result.DisplayName = FText::FromString(TEXT("棱喙"));
			Result.FallbackColor = FLinearColor(1.0f, 0.72f, 0.02f);
			Result.SlingshotCapability = EABTSBirdSlingshotCapability::Simple;
			break;
		case EABTSBirdId::Black:
			Result.DisplayName = FText::FromString(TEXT("玄爪"));
			Result.FallbackColor = FLinearColor(0.015f, 0.015f, 0.02f);
			Result.SlingshotCapability = EABTSBirdSlingshotCapability::Reinforced;
			break;
		default:
			Result.DisplayName = FText::FromString(TEXT("绯翼"));
			Result.FallbackColor = FLinearColor(0.85f, 0.06f, 0.04f);
			Result.SlingshotCapability = EABTSBirdSlingshotCapability::Simple;
			break;
		}
		return Result;
	}
}

AABTSBirdParty::AABTSBirdParty()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
}

bool AABTSBirdParty::InitializeParty(AABTSM25BirdCharacter* InitialLeader)
{
	if (bPartyReady) return true;
	if (InitialLeader == nullptr || FindPlanet() == nullptr) return false;

	for (TActorIterator<AABTSBirdPartySettings> It(GetWorld()); It; ++It)
	{
		Settings = *It;
		break;
	}
	BuildResolvedPresentation();
	if (!SpawnFollowers(*InitialLeader)) return false;
	RebuildQueue(EABTSBirdId::Red);
	bPartyReady = PartyMembers.Num() == BirdCount;
	int32 PawnCollisionIgnoredCount = 0;
	for (const AABTSM25BirdCharacter* Bird : PartyMembers)
	{
		if (Bird != nullptr
			&& Bird->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Ignore)
		{
			++PawnCollisionIgnoredCount;
		}
	}
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M4][Party] Initialized=%d Members=%d Controlled=Red PawnCollisionIgnored=%d Settings=%s QueueSpacing=%.1f FollowStart=%.1f FollowStop=%.1f Separation=%.1f Steering=SoftContinuous"),
		bPartyReady ? 1 : 0,
		PartyMembers.Num(),
		PawnCollisionIgnoredCount,
		*GetNameSafe(Settings.Get()),
		Settings.IsValid() ? Settings->QueueSpacingCM : 190.0f,
		Settings.IsValid() ? Settings->FollowStartDistanceCM : 250.0f,
		Settings.IsValid() ? Settings->FollowStopDistanceCM : 145.0f,
		Settings.IsValid() ? Settings->SeparationDistanceCM : 95.0f);
	return bPartyReady;
}

bool AABTSBirdParty::InitializePlanarParty(
	AABTSM25BirdCharacter* InitialLeader,
	const FVector& InPlaneOrigin,
	const FVector& InPlaneUp)
{
	if (bPartyReady) return true;
	if (InitialLeader == nullptr) return false;
	bPlanarMode = true;
	PlanarOrigin = InPlaneOrigin;
	PlanarUp = InPlaneUp.GetSafeNormal();
	if (PlanarUp.IsNearlyZero()) PlanarUp = FVector::UpVector;
	for (TActorIterator<AABTSBirdPartySettings> It(GetWorld()); It; ++It) { Settings = *It; break; }
	BuildResolvedPresentation();
	InitialLeader->EnablePlanarChaosMovement(PlanarOrigin, PlanarUp);
	if (!SpawnFollowers(*InitialLeader)) return false;
	RebuildQueue(EABTSBirdId::Red);
	bPartyReady = PartyMembers.Num() == BirdCount;
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M7.1][Party] Planar=%d Members=%d Up=(%.3f,%.3f,%.3f)"),
		bPartyReady ? 1 : 0, PartyMembers.Num(), PlanarUp.X, PlanarUp.Y, PlanarUp.Z);
	return bPartyReady;
}

void AABTSBirdParty::BuildResolvedPresentation()
{
	ResolvedPresentation.Reset();
	ResolvedPresentation.SetNum(BirdCount);
	for (int32 Index = 0; Index < BirdCount; ++Index)
	{
		ResolvedPresentation[Index] = MakeDefaultPresentation(static_cast<EABTSBirdId>(Index));
	}
	if (!Settings.IsValid()) return;
	for (const FABTSBirdPresentationConfig& Config : Settings->Birds)
	{
		const int32 Index = ABTSBirdIdToIndex(Config.BirdId);
		if (ResolvedPresentation.IsValidIndex(Index)) ResolvedPresentation[Index] = Config;
	}
}

bool AABTSBirdParty::SpawnFollowers(AABTSM25BirdCharacter& InitialLeader)
{
	PartyMembers.Reset();
	RuntimeByFixedId.Reset();
	RuntimeByFixedId.SetNum(BirdCount);
	PartyMembers.Add(&InitialLeader);
	const FABTSBirdPresentationConfig& RedPresentation = ResolvedPresentation[ABTSBirdIdToIndex(EABTSBirdId::Red)];
	InitialLeader.SetBirdIdentity(EABTSBirdId::Red, RedPresentation.SlingshotCapability, true);
	InitialLeader.SetPartyCollisionIsolation(true);

	AABTSM2Planet* ResolvedPlanet = bPlanarMode ? nullptr : FindPlanet();
	if (!bPlanarMode && ResolvedPlanet == nullptr) return false;
	const FVector LeaderLocation = InitialLeader.GetActorLocation();
	const FVector Center = bPlanarMode ? PlanarOrigin : ResolvedPlanet->GetPlanetCenterWorld();
	const FVector LeaderUp = bPlanarMode ? PlanarUp : ResolvedPlanet->GetRadialUpAtWorldLocation(LeaderLocation);
	FVector BackDirection = -FVector::VectorPlaneProject(InitialLeader.GetActorForwardVector(), LeaderUp).GetSafeNormal();
	if (BackDirection.IsNearlyZero()) BackDirection = FVector::CrossProduct(LeaderUp, FVector::RightVector).GetSafeNormal();
	const float QueueSpacing = Settings.IsValid() ? Settings->QueueSpacingCM : 190.0f;

	for (int32 Index = 1; Index < BirdCount; ++Index)
	{
		const EABTSBirdId BirdId = static_cast<EABTSBirdId>(Index);
		const float CapsuleHalfHeight = InitialLeader.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		FVector SpawnUp = LeaderUp;
		FVector SpawnLocation;
		if (bPlanarMode)
		{
			SpawnLocation = LeaderLocation + BackDirection * QueueSpacing * Index;
			const float Height = FVector::DotProduct(SpawnLocation - PlanarOrigin, PlanarUp);
			SpawnLocation += PlanarUp * (CapsuleHalfHeight + 2.0f - Height);
		}
		else
		{
			const float Angle = QueueSpacing * Index / FMath::Max(ResolvedPlanet->GetPlanetRadiusCM(), 1.0f);
			SpawnUp = (LeaderUp * FMath::Cos(Angle) + BackDirection * FMath::Sin(Angle)).GetSafeNormal();
			const float SpawnRadius = ResolvedPlanet->GetSurfaceRadiusAtDirection(SpawnUp) + CapsuleHalfHeight + 2.0f;
			SpawnLocation = Center + SpawnUp * SpawnRadius;
		}
		const FVector SpawnForward = FVector::VectorPlaneProject(-BackDirection, SpawnUp).GetSafeNormal();
		const FTransform SpawnTransform(FRotationMatrix::MakeFromXZ(SpawnForward, SpawnUp).ToQuat(), SpawnLocation);
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = this;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		TSubclassOf<AABTSM25BirdCharacter> SpawnClass = FollowerBirdClass;
		if (!SpawnClass) SpawnClass = TSubclassOf<AABTSM25BirdCharacter>(InitialLeader.GetClass());
		AABTSM25BirdCharacter* Bird = GetWorld()->SpawnActor<AABTSM25BirdCharacter>(SpawnClass, SpawnTransform, SpawnParameters);
		if (Bird == nullptr) return false;

		const FABTSBirdPresentationConfig& Presentation = ResolvedPresentation[Index];
		Bird->SetBirdIdentity(BirdId, Presentation.SlingshotCapability, false);
		Bird->SetPartyCollisionIsolation(true);
		if (bPlanarMode) Bird->EnablePlanarChaosMovement(PlanarOrigin, PlanarUp);
		Bird->ResetRadialMovementState();
		PartyMembers.Add(Bird);
	}

	for (int32 Index = 0; Index < BirdCount; ++Index)
	{
		FABTSBirdPartyRuntime& Runtime = RuntimeByFixedId[Index];
		Runtime.BirdId = static_cast<EABTSBirdId>(Index);
		Runtime.Bird = PartyMembers[Index];
		PartyMembers[Index]->AddPartyTickPrerequisite(this);
		Runtime.bWasGrounded = PartyMembers[Index]->IsRadiallyGrounded();
		RecordPathSample(Runtime);
	}
	return PartyMembers.Num() == BirdCount;
}

void AABTSBirdParty::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bPartyReady || bSlingshotMode || DeltaSeconds <= SMALL_NUMBER) return;
	const bool bUseDirectExperiment = Settings.IsValid()
		&& Settings->bUseDirectControlledBirdFollowExperiment;
	if (!bFollowModeInitialized || bUsingDirectControlledBirdFollowExperiment != bUseDirectExperiment)
	{
		bFollowModeInitialized = true;
		bUsingDirectControlledBirdFollowExperiment = bUseDirectExperiment;
		++PathGeneration;
		ResetFollowingRuntimeState(!bUseDirectExperiment);
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M4][FollowExperiment] Mode=%s Breadcrumbs=%d JumpPropagation=%d Target=%s"),
			bUseDirectExperiment ? TEXT("DirectControlled") : TEXT("BreadcrumbQueue"),
			bUseDirectExperiment ? 0 : 1,
			bUseDirectExperiment ? 0 : 1,
			bUseDirectExperiment ? TEXT("ControlledBirdLivePosition") : TEXT("PredecessorBreadcrumb"));
	}
	if (!bUseDirectExperiment) RecordPathsAndJumpEvents(DeltaSeconds);
	if (FollowerUpdatePauseRemainingSeconds > 0.0f)
	{
		FollowerUpdatePauseRemainingSeconds = FMath::Max(0.0f, FollowerUpdatePauseRemainingSeconds - DeltaSeconds);
		return;
	}
	UpdateFollowers(DeltaSeconds);
}

void AABTSBirdParty::RecordPathsAndJumpEvents(const float DeltaSeconds)
{
	AABTSM2Planet* ResolvedPlanet = bPlanarMode ? nullptr : FindPlanet();
	if (!bPlanarMode && ResolvedPlanet == nullptr) return;
	for (FABTSBirdPartyRuntime& Runtime : RuntimeByFixedId)
	{
		AABTSM25BirdCharacter* Bird = Runtime.Bird.Get();
		if (Bird == nullptr) continue;
		RecordPathSample(Runtime);
		const bool bGrounded = Bird->IsRadiallyGrounded();
		if (Runtime.bWasGrounded && !bGrounded)
		{
			FABTSBirdJumpEvent& Event = Runtime.JumpEvents.AddDefaulted_GetRef();
			Event.Serial = NextJumpSerial++;
			Event.PathGeneration = PathGeneration;
			Event.TakeoffLocation = Bird->GetActorLocation();
			Event.TakeoffRadiusCM = bPlanarMode
				? FVector::DotProduct(Event.TakeoffLocation - PlanarOrigin, PlanarUp)
				: FVector::Distance(Event.TakeoffLocation, ResolvedPlanet->GetPlanetCenterWorld());
			if (Runtime.JumpEvents.Num() > MaxJumpEvents) Runtime.JumpEvents.RemoveAt(0);
		}
		if (!bGrounded && !Runtime.JumpEvents.IsEmpty())
		{
			FABTSBirdJumpEvent& Event = Runtime.JumpEvents.Last();
			if (!Event.bLanded && Event.PathGeneration == PathGeneration)
			{
				const float CurrentRadius = bPlanarMode
					? FVector::DotProduct(Bird->GetActorLocation() - PlanarOrigin, PlanarUp)
					: FVector::Distance(Bird->GetActorLocation(), ResolvedPlanet->GetPlanetCenterWorld());
				Event.MaxHeightAboveTakeoffCM = FMath::Max(Event.MaxHeightAboveTakeoffCM, CurrentRadius - Event.TakeoffRadiusCM);
			}
		}
		if (!Runtime.bWasGrounded && bGrounded && !Runtime.JumpEvents.IsEmpty()) Runtime.JumpEvents.Last().bLanded = true;
		for (FABTSBirdJumpEvent& Event : Runtime.JumpEvents) Event.AgeSeconds += DeltaSeconds;
		Runtime.JumpEvents.RemoveAll([](const FABTSBirdJumpEvent& Event)
		{
			return Event.AgeSeconds > JumpEventLifetimeSeconds;
		});
		Runtime.bWasGrounded = bGrounded;
	}
}

void AABTSBirdParty::RecordPathSample(FABTSBirdPartyRuntime& Runtime)
{
	AABTSM25BirdCharacter* Bird = Runtime.Bird.Get();
	if (Bird == nullptr) return;
	const FVector Location = Bird->GetActorLocation();
	const float Spacing = Settings.IsValid() ? Settings->PathSampleSpacingCM : 32.0f;
	if (!Runtime.Path.IsEmpty() && FVector::Distance(Location, Runtime.Path[0].Location) < Spacing) return;
	FABTSBirdPathSample Sample;
	Sample.Location = Location;
	Sample.Forward = Bird->GetActorForwardVector();
	Sample.DistanceFromPreviousCM = Runtime.Path.IsEmpty() ? 0.0f : GetSurfaceDistanceCM(Location, Runtime.Path[0].Location);
	Runtime.Path.Insert(Sample, 0);
	if (Runtime.Path.Num() > MaxPathSamples) Runtime.Path.SetNum(MaxPathSamples);
}

bool AABTSBirdParty::FindTargetBehind(
	const FABTSBirdPartyRuntime& Predecessor,
	const float DistanceBehindCM,
	FABTSBirdPathSample& OutTarget) const
{
	if (Predecessor.Path.IsEmpty()) return false;
	float AccumulatedDistance = 0.0f;
	OutTarget = Predecessor.Path.Last();
	for (int32 Index = 1; Index < Predecessor.Path.Num(); ++Index)
	{
		AccumulatedDistance += Predecessor.Path[Index - 1].DistanceFromPreviousCM;
		if (AccumulatedDistance >= DistanceBehindCM)
		{
			OutTarget = Predecessor.Path[Index];
			return true;
		}
	}
	return true;
}

void AABTSBirdParty::UpdateFollowers(const float DeltaSeconds)
{
	for (int32 QueueIndex = 1; QueueIndex < QueueOrder.Num(); ++QueueIndex)
	{
		FABTSBirdPartyRuntime* Follower = FindRuntime(QueueOrder[QueueIndex]);
		FABTSBirdPartyRuntime* Predecessor = FindRuntime(QueueOrder[QueueIndex - 1]);
		if (Follower && Predecessor) UpdateFollower(*Follower, *Predecessor, DeltaSeconds);
	}
}

void AABTSBirdParty::UpdateFollower(
	FABTSBirdPartyRuntime& Follower,
	FABTSBirdPartyRuntime& Predecessor,
	const float DeltaSeconds)
{
	AABTSM25BirdCharacter* Bird = Follower.Bird.Get();
	AABTSM25BirdCharacter* LeaderBird = Predecessor.Bird.Get();
	AABTSM2Planet* ResolvedPlanet = bPlanarMode ? nullptr : FindPlanet();
	if (Bird == nullptr || LeaderBird == nullptr || (!bPlanarMode && ResolvedPlanet == nullptr)) return;

	FABTSBirdPathSample Target;
	const float QueueSpacing = Settings.IsValid() ? Settings->QueueSpacingCM : 190.0f;
	if (bUsingDirectControlledBirdFollowExperiment)
	{
		AABTSM25BirdCharacter* ControlledBird = GetControlledBird();
		if (ControlledBird == nullptr || ControlledBird == Bird) return;
		Target.Location = ControlledBird->GetActorLocation();
		Target.Forward = ControlledBird->GetActorForwardVector();
	}
	else if (!FindTargetBehind(Predecessor, QueueSpacing, Target))
	{
		return;
	}
	const float DistanceToTarget = GetSurfaceDistanceCM(Bird->GetActorLocation(), Target.Location);
	const float FollowStart = Settings.IsValid() ? Settings->FollowStartDistanceCM : 250.0f;
	const float FollowStop = Settings.IsValid() ? Settings->FollowStopDistanceCM : 145.0f;
	const float SevereDistance = Settings.IsValid() ? Settings->SevereDetachDistanceCM : 1200.0f;
	if (Follower.bFollowing)
	{
		if (DistanceToTarget <= FollowStop) Follower.bFollowing = false;
	}
	else if (DistanceToTarget >= FollowStart)
	{
		Follower.bFollowing = true;
	}

	if (!bUsingDirectControlledBirdFollowExperiment) TryPropagateJump(Follower, Predecessor, Target);
	FVector Separation = FVector::ZeroVector;
	const float SeparationDistance = Settings.IsValid() ? Settings->SeparationDistanceCM : 95.0f;
	const FVector Up = bPlanarMode ? PlanarUp : ResolvedPlanet->GetRadialUpAtWorldLocation(Bird->GetActorLocation());
	for (AABTSM25BirdCharacter* Other : PartyMembers)
	{
		if (Other == nullptr || Other == Bird) continue;
		const FVector Offset = Bird->GetActorLocation() - Other->GetActorLocation();
		const float Distance = Offset.Size();
		if (Distance > SMALL_NUMBER && Distance < SeparationDistance)
		{
			// A squared falloff makes separation a soft steering intent. The old
			// normalized command turned even a sub-centimetre threshold crossing
			// into almost full acceleration, so inertia repeatedly crossed the
			// boundary and flipped the bird's facing direction every frame.
			const float Proximity = 1.0f - Distance / FMath::Max(SeparationDistance, 1.0f);
			Separation += FVector::VectorPlaneProject(Offset, Up).GetSafeNormal()
				* FMath::Square(Proximity);
		}
		else if (Distance <= SMALL_NUMBER)
		{
			// Exact overlap has no geometric escape direction. Use a stable,
			// pair-antisymmetric tangent so both birds choose opposite sides.
			const bool bUsePositiveSide = ABTSBirdIdToIndex(Follower.BirdId) < ABTSBirdIdToIndex(Other->GetBirdId());
			const FVector ReferenceAxis = FMath::Abs(FVector::DotProduct(Up, FVector::UpVector)) < 0.9f
				? FVector::UpVector
				: FVector::ForwardVector;
			const FVector PairTangent = FVector::CrossProduct(ReferenceAxis, Up).GetSafeNormal();
			Separation += bUsePositiveSide ? PairTangent : -PairTangent;
		}
	}

	// Follow and separation are independent continuous steering commands.
	// In particular, a resting follower must not receive a hidden pull toward
	// its target merely because a neighbour activated separation.
	FVector MoveCommand = Separation * 0.85f;
	if (Follower.bFollowing)
	{
		const FVector FollowDirection = FVector::VectorPlaneProject(Target.Location - Bird->GetActorLocation(), Up).GetSafeNormal();
		const float ArrivalScale = DistanceToTarget >= SevereDistance
			? 1.0f
			: FMath::Clamp(
				(DistanceToTarget - FollowStop) / FMath::Max(FollowStart - FollowStop, 1.0f),
				0.0f,
				1.0f);
		MoveCommand += FollowDirection * ArrivalScale;
	}

	MoveCommand = MoveCommand.GetClampedToMaxSize(1.0f);
	const float MoveScale = MoveCommand.Size();
	// Ignore a nearly cancelled command. Besides avoiding meaningless force,
	// this prevents microscopic direction changes from snapping visual facing.
	constexpr float SteeringDeadZone = 0.035f;
	if (MoveScale > SteeringDeadZone)
	{
		Bird->ApplyPartyMoveInput(MoveCommand / MoveScale, MoveScale);
	}

	if (DistanceToTarget >= SevereDistance)
	{
		const bool bMakingProgress = DistanceToTarget < Follower.PreviousTargetDistanceCM - 4.0f;
		Follower.SevereDetachSeconds = bMakingProgress ? 0.0f : Follower.SevereDetachSeconds + DeltaSeconds;
		if (Follower.SevereDetachSeconds >= 3.0f) RecoverFollower(Follower, Target);
	}
	else
	{
		Follower.SevereDetachSeconds = 0.0f;
	}
	Follower.PreviousTargetDistanceCM = DistanceToTarget;
}

void AABTSBirdParty::TryPropagateJump(
	FABTSBirdPartyRuntime& Follower,
	const FABTSBirdPartyRuntime& Predecessor,
	const FABTSBirdPathSample& Target)
{
	AABTSM25BirdCharacter* Bird = Follower.Bird.Get();
	if (Bird == nullptr || !Bird->IsRadiallyGrounded()) return;
	const float TriggerDistance = Settings.IsValid() ? Settings->JumpTriggerDistanceCM : 105.0f;
	const float HeightThreshold = Settings.IsValid() ? Settings->JumpHeightTriggerCM : 55.0f;
	for (const FABTSBirdJumpEvent& Event : Predecessor.JumpEvents)
	{
		if (Event.Serial <= Follower.LastConsumedJumpSerial || Event.PathGeneration != PathGeneration) continue;
		if (Event.MaxHeightAboveTakeoffCM < HeightThreshold) continue;
		if (GetSurfaceDistanceCM(Bird->GetActorLocation(), Event.TakeoffLocation) > TriggerDistance) continue;
		Follower.LastConsumedJumpSerial = Event.Serial;
		Bird->ApplyPartyMoveInput(Target.Forward, 1.0f);
		Bird->ApplyPartyJump();
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M4][JumpFollow] Bird=%d From=%d Event=%d Height=%.1f"),
			ABTSBirdIdToIndex(Follower.BirdId),
			ABTSBirdIdToIndex(Predecessor.BirdId),
			Event.Serial,
			Event.MaxHeightAboveTakeoffCM);
		break;
	}
}

void AABTSBirdParty::RecoverFollower(FABTSBirdPartyRuntime& Follower, const FABTSBirdPathSample& SafeTarget)
{
	AABTSM25BirdCharacter* Bird = Follower.Bird.Get();
	AABTSM2Planet* ResolvedPlanet = bPlanarMode ? nullptr : FindPlanet();
	if (Bird == nullptr || (!bPlanarMode && ResolvedPlanet == nullptr)) return;
	if (bPlanarMode)
	{
		const float CapsuleHalfHeight = Bird->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		FVector Location = SafeTarget.Location;
		Location += PlanarUp * (CapsuleHalfHeight + 4.0f - FVector::DotProduct(Location - PlanarOrigin, PlanarUp));
		Bird->ResetRadialMovementState();
		Bird->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
		Follower.SevereDetachSeconds = 0.0f;
		Follower.PreviousTargetDistanceCM = 0.0f;
		return;
	}
	const FVector Center = ResolvedPlanet->GetPlanetCenterWorld();
	const FVector Direction = (SafeTarget.Location - Center).GetSafeNormal();
	const float Radius = ResolvedPlanet->GetSurfaceRadiusAtDirection(Direction)
		+ Bird->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
		+ 4.0f;
	Bird->ResetRadialMovementState();
	Bird->SetActorLocation(Center + Direction * Radius, false, nullptr, ETeleportType::TeleportPhysics);
	Follower.SevereDetachSeconds = 0.0f;
	Follower.PreviousTargetDistanceCM = 0.0f;
	UE_LOG(LogABTSRuntime, Warning, TEXT("[ABTS][M4][Recovery] Bird=%d returned to safe breadcrumb."), ABTSBirdIdToIndex(Follower.BirdId));
}

bool AABTSBirdParty::SwitchControlledBird(const EABTSBirdId NewBirdId)
{
	if (!bPartyReady || NewBirdId == ControlledBirdId) return bPartyReady;
	FABTSBirdPartyRuntime* NewRuntime = FindRuntime(NewBirdId);
	FABTSBirdPartyRuntime* OldRuntime = FindRuntime(ControlledBirdId);
	AABTSM25BirdCharacter* NewBird = NewRuntime ? NewRuntime->Bird.Get() : nullptr;
	AABTSM25BirdCharacter* OldBird = OldRuntime ? OldRuntime->Bird.Get() : nullptr;
	APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (NewBird == nullptr || OldBird == nullptr || PlayerController == nullptr) return false;

	OldBird->SetPartyControlled(false);
	PlayerController->Possess(NewBird);
	NewBird->SetPartyControlled(true);
	// Possession callbacks and input-stack rebuilds are complete at this point.
	// Clear both sides afterwards so no pre-transfer force survives into physics.
	OldBird->ClearControlHandoffState();
	const bool bClearNewLeaderMotionCaches = Settings.IsValid()
		&& Settings->bClearNewLeaderMotionCachesOnHandoffExperiment;
	if (bClearNewLeaderMotionCaches)
	{
		NewBird->ResetForControlHandoffCacheExperiment();
	}
	else
	{
		NewBird->ClearControlHandoffState();
	}
	OldBird->BeginControlHandoffDiagnostics();
	NewBird->BeginControlHandoffDiagnostics();
	ControlledBirdId = NewBirdId;
	RebuildQueue(NewBirdId);
	FollowerUpdatePauseRemainingSeconds = 0.05f;
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M4][Switch] Controlled=%d Queue=%d,%d,%d,%d NewLeaderGroundAligned=1 CacheClearExperiment=%d PartyTickBeforeMovement=1 Pause=%.2f Authority=Possession"),
		ABTSBirdIdToIndex(ControlledBirdId),
		ABTSBirdIdToIndex(QueueOrder[0]),
		ABTSBirdIdToIndex(QueueOrder[1]),
		ABTSBirdIdToIndex(QueueOrder[2]),
		ABTSBirdIdToIndex(QueueOrder[3]),
		bClearNewLeaderMotionCaches ? 1 : 0,
		FollowerUpdatePauseRemainingSeconds);
	return true;
}

bool AABTSBirdParty::CycleControlledBird()
{
	const int32 NextIndex = (ABTSBirdIdToIndex(ControlledBirdId) + 1) % BirdCount;
	return SwitchControlledBird(static_cast<EABTSBirdId>(NextIndex));
}

void AABTSBirdParty::RebuildQueue(const EABTSBirdId NewLeaderId)
{
	QueueOrder.Reset();
	QueueOrder.Add(NewLeaderId);
	TArray<EABTSBirdId> Remaining;
	for (int32 Index = 0; Index < BirdCount; ++Index)
	{
		const EABTSBirdId Candidate = static_cast<EABTSBirdId>(Index);
		if (Candidate != NewLeaderId) Remaining.Add(Candidate);
	}
	EABTSBirdId Current = NewLeaderId;
	while (!Remaining.IsEmpty())
	{
		const FABTSBirdPartyRuntime* CurrentRuntime = FindRuntime(Current);
		int32 BestArrayIndex = 0;
		float BestDistance = TNumericLimits<float>::Max();
		for (int32 ArrayIndex = 0; ArrayIndex < Remaining.Num(); ++ArrayIndex)
		{
			const FABTSBirdPartyRuntime* CandidateRuntime = FindRuntime(Remaining[ArrayIndex]);
			if (CurrentRuntime == nullptr || CandidateRuntime == nullptr || !CurrentRuntime->Bird.IsValid() || !CandidateRuntime->Bird.IsValid()) continue;
			const float Distance = GetSurfaceDistanceCM(CurrentRuntime->Bird->GetActorLocation(), CandidateRuntime->Bird->GetActorLocation());
			if (Distance < BestDistance - KINDA_SMALL_NUMBER
				|| (FMath::IsNearlyEqual(Distance, BestDistance) && ABTSBirdIdToIndex(Remaining[ArrayIndex]) < ABTSBirdIdToIndex(Remaining[BestArrayIndex])))
			{
				BestDistance = Distance;
				BestArrayIndex = ArrayIndex;
			}
		}
		Current = Remaining[BestArrayIndex];
		QueueOrder.Add(Current);
		Remaining.RemoveAt(BestArrayIndex);
	}
	++PathGeneration;
	ResetFollowingRuntimeState(!bUsingDirectControlledBirdFollowExperiment);
}

void AABTSBirdParty::ResetFollowingRuntimeState(const bool bSeedPaths)
{
	for (FABTSBirdPartyRuntime& Runtime : RuntimeByFixedId)
	{
		Runtime.bFollowing = false;
		Runtime.Path.Reset();
		Runtime.JumpEvents.Reset();
		Runtime.LastConsumedJumpSerial = INDEX_NONE;
		Runtime.SevereDetachSeconds = 0.0f;
		Runtime.PreviousTargetDistanceCM = TNumericLimits<float>::Max();
		Runtime.bWasGrounded = Runtime.Bird.IsValid() && Runtime.Bird->IsRadiallyGrounded();
	}
	if (!bSeedPaths) return;
	// Breadcrumbs belong to a queue generation. Reusing paths recorded while a
	// bird had another predecessor makes the new queue steer along stale roles.
	for (FABTSBirdPartyRuntime& Runtime : RuntimeByFixedId)
	{
		RecordPathSample(Runtime);
	}
}

AABTSM25BirdCharacter* AABTSBirdParty::GetControlledBird() const
{
	const FABTSBirdPartyRuntime* Runtime = FindRuntime(ControlledBirdId);
	return Runtime ? Runtime->Bird.Get() : nullptr;
}

const FABTSBirdPresentationConfig* AABTSBirdParty::GetPresentation(const EABTSBirdId BirdId) const
{
	const int32 Index = ABTSBirdIdToIndex(BirdId);
	return ResolvedPresentation.IsValidIndex(Index) ? &ResolvedPresentation[Index] : nullptr;
}

FABTSBirdPartyRuntime* AABTSBirdParty::FindRuntime(const EABTSBirdId BirdId)
{
	const int32 Index = ABTSBirdIdToIndex(BirdId);
	return RuntimeByFixedId.IsValidIndex(Index) ? &RuntimeByFixedId[Index] : nullptr;
}

const FABTSBirdPartyRuntime* AABTSBirdParty::FindRuntime(const EABTSBirdId BirdId) const
{
	const int32 Index = ABTSBirdIdToIndex(BirdId);
	return RuntimeByFixedId.IsValidIndex(Index) ? &RuntimeByFixedId[Index] : nullptr;
}

AABTSM2Planet* AABTSBirdParty::FindPlanet()
{
	if (Planet.IsValid() && Planet->IsPlanetReady()) return Planet.Get();
	for (TActorIterator<AABTSM2Planet> It(GetWorld()); It; ++It)
	{
		if (It->IsPlanetReady())
		{
			Planet = *It;
			return Planet.Get();
		}
	}
	return nullptr;
}

float AABTSBirdParty::GetSurfaceDistanceCM(const FVector& A, const FVector& B) const
{
	if (bPlanarMode || !Planet.IsValid()) return FVector::Distance(A, B);
	const FVector Center = Planet->GetPlanetCenterWorld();
	const FVector DirectionA = (A - Center).GetSafeNormal();
	const FVector DirectionB = (B - Center).GetSafeNormal();
	const float Angle = FMath::Acos(FMath::Clamp(FVector::DotProduct(DirectionA, DirectionB), -1.0f, 1.0f));
	return Angle * Planet->GetPlanetRadiusCM();
}

FVector AABTSBirdParty::GetSurfaceUpAt(const FVector& WorldLocation) const
{
	if (bPlanarMode) return PlanarUp;
	return Planet.IsValid() ? Planet->GetRadialUpAtWorldLocation(WorldLocation) : FVector::UpVector;
}
