// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/ABTSM25BirdCharacter.h"

#include "ABTSRuntime.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Movement/ABTSMovementModeSelector.h"
#include "Movement/ABTSM25RadialMovementComponent.h"
#include "Movement/ABTSRadialForceMovementComponent.h"
#include "Movement/ABTSRadialSurfaceSuspensionComponent.h"
#include "Planet/ABTSM2SphericalSurfaceComponent.h"
#include "Player/ABTSM4PlayerController.h"

AABTSM25BirdCharacter::AABTSM25BirdCharacter()
{
	RadialMovement = CreateDefaultSubobject<UABTSM25RadialMovementComponent>(TEXT("RadialMovement"));
	ForceMovement = CreateDefaultSubobject<UABTSRadialForceMovementComponent>(TEXT("ForceMovement"));
	SurfaceSuspension = CreateDefaultSubobject<UABTSRadialSurfaceSuspensionComponent>(TEXT("SurfaceSuspension"));
}

void AABTSM25BirdCharacter::BeginPlay()
{
	Super::BeginPlay();
	GetCharacterMovement()->DisableMovement();
	GetSphericalSurface()->SetProjectToBaseSurface(false);
	ConfigureMovementMode();
}

void AABTSM25BirdCharacter::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (ControlDiagnosticRemainingSeconds <= 0.0f) return;
	ControlDiagnosticRemainingSeconds = FMath::Max(0.0f, ControlDiagnosticRemainingSeconds - DeltaSeconds);
	ControlDiagnosticLogAccumulator += DeltaSeconds;
	if (ControlDiagnosticLogAccumulator >= 0.1f || ControlDiagnosticRemainingSeconds <= 0.0f)
	{
		ControlDiagnosticLogAccumulator = 0.0f;
		LogControlDiagnosticSnapshot();
	}
}

void AABTSM25BirdCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	UE_LOG(LogABTSRuntime, Warning, TEXT("[ABTS][M4][HandoffDiag][Possessed] Bird=%d Controller=%s Pawn=%s Flag=%d"),
		ABTSBirdIdToIndex(BirdId), *GetNameSafe(NewController), *GetNameSafe(NewController ? NewController->GetPawn() : nullptr), bPartyControlled ? 1 : 0);
}

void AABTSM25BirdCharacter::UnPossessed()
{
	UE_LOG(LogABTSRuntime, Warning, TEXT("[ABTS][M4][HandoffDiag][UnPossessed] Bird=%d Controller=%s Flag=%d"),
		ABTSBirdIdToIndex(BirdId), *GetNameSafe(Controller), bPartyControlled ? 1 : 0);
	Super::UnPossessed();
}

void AABTSM25BirdCharacter::ConfigureMovementMode()
{
	for (TActorIterator<AABTSMovementModeSelector> It(GetWorld()); It; ++It)
	{
		MovementMode = It->MovementMode;
		UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][MovementMode] Level selector found: %s"), *GetNameSafe(*It));
		break;
	}
	const bool bUseForceSuspension = MovementMode == EABTSBirdMovementMode::ForceSuspension;
	RadialMovement->SetComponentTickEnabled(!bUseForceSuspension);
	ForceMovement->SetComponentTickEnabled(bUseForceSuspension);
	ResetRadialMovementState();
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][MovementMode] Active=%s LegacyTick=%d ForceTick=%d"),
		bUseForceSuspension ? TEXT("ForceSuspension") : TEXT("LegacySweep"),
		RadialMovement->IsComponentTickEnabled() ? 1 : 0,
		ForceMovement->IsComponentTickEnabled() ? 1 : 0);
}

void AABTSM25BirdCharacter::ResetRadialMovementState()
{
	RadialMovement->ResetMotionState();
	ForceMovement->ResetMotionState();
}

void AABTSM25BirdCharacter::ClearControlHandoffState()
{
	RadialMovement->ClearControlHandoffState();
	// A newly possessed follower may currently be airborne because it was
	// executing a queued follow jump. Clearing velocity alone leaves it suspended
	// above the surface, so gravity looks like an autonomous post-switch command.
	// Establish a deterministic grounded handoff for the new controlled bird.
	if (Controller != nullptr && bPartyControlled)
	{
		ForceMovement->StabilizeForGroundedControlHandoff();
	}
	else
	{
		// A departing airborne leader keeps its physical trajectory; only steering
		// authority is removed. A grounded leader may stop immediately.
		if (ForceMovement->IsGrounded()) ForceMovement->ClearControlHandoffState();
		else ForceMovement->ClearControlHandoffInput();
	}
	RadialMovement->GrantControlHandoffJumpGrace(0.28f);
	ForceMovement->GrantControlHandoffJumpGrace(0.28f);
}

void AABTSM25BirdCharacter::BeginControlHandoffDiagnostics(const float Seconds)
{
	ControlDiagnosticRemainingSeconds = FMath::Max(0.0f, Seconds);
	ControlDiagnosticLogAccumulator = 1.0f;
	UE_LOG(LogABTSRuntime, Warning, TEXT("[ABTS][M4][HandoffDiag][Begin] Bird=%d Seconds=%.2f Controller=%s Flag=%d Mode=%d"),
		ABTSBirdIdToIndex(BirdId), ControlDiagnosticRemainingSeconds, *GetNameSafe(Controller), bPartyControlled ? 1 : 0, static_cast<int32>(MovementMode));
}

void AABTSM25BirdCharacter::SetBirdIdentity(
	const EABTSBirdId InBirdId,
	const EABTSBirdSlingshotCapability InCapability,
	const bool bInPlayerControlled)
{
	BirdId = InBirdId;
	SlingshotCapability = InCapability;
	SetPartyControlled(bInPlayerControlled);
}

void AABTSM25BirdCharacter::SetPartyControlled(const bool bInPlayerControlled)
{
	bPartyControlled = bInPlayerControlled;
	UE_LOG(LogABTSRuntime, Warning, TEXT("[ABTS][M4][HandoffDiag][Flag] Bird=%d NewFlag=%d Controller=%s"),
		ABTSBirdIdToIndex(BirdId), bPartyControlled ? 1 : 0, *GetNameSafe(Controller));
}

void AABTSM25BirdCharacter::SetPartyCollisionIsolation(const bool bIsolateFromParty)
{
	if (GetCapsuleComponent() == nullptr) return;
	// Party birds are not gameplay obstacles for one another. WorldStatic
	// buildings and terrain keep their normal responses.
	GetCapsuleComponent()->SetCollisionResponseToChannel(
		ECC_Pawn,
		bIsolateFromParty ? ECR_Ignore : ECR_Block);
	GetCapsuleComponent()->SetCollisionResponseToChannel(
		ECC_Camera,
		bIsolateFromParty ? ECR_Ignore : ECR_Block);
}

bool AABTSM25BirdCharacter::CanUseSlingshotCapability(const EABTSBirdSlingshotCapability RequiredCapability) const
{
	return SlingshotCapability == RequiredCapability;
}

void AABTSM25BirdCharacter::EnterSlingshotPouch(const FVector& WorldLocation, const FQuat& WorldRotation)
{
	SavedCapsuleCollision = GetCapsuleComponent()->GetCollisionEnabled();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ForceMovement->EndBallisticFlight(true);
	ForceMovement->SetComponentTickEnabled(false);
	SetActorLocationAndRotation(WorldLocation, WorldRotation, false, nullptr, ETeleportType::TeleportPhysics);
}

void AABTSM25BirdCharacter::LaunchFromSlingshot(const FVector& InitialVelocity, const float FlightAirDragPerSecond)
{
	GetCapsuleComponent()->SetCollisionEnabled(SavedCapsuleCollision);
	ForceMovement->SetComponentTickEnabled(true);
	ForceMovement->BeginBallisticFlight(InitialVelocity, FlightAirDragPerSecond);
}

void AABTSM25BirdCharacter::BeginSlingshotReturn()
{
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ForceMovement->EndBallisticFlight(true);
	ForceMovement->SetComponentTickEnabled(false);
}

void AABTSM25BirdCharacter::FinishSlingshotReturn()
{
	GetCapsuleComponent()->SetCollisionEnabled(SavedCapsuleCollision);
	ForceMovement->SetComponentTickEnabled(true);
	ForceMovement->EndBallisticFlight(true);
}

void AABTSM25BirdCharacter::SetSlingshotVelocity(const FVector& InVelocity)
{
	ForceMovement->SetVelocity(InVelocity);
}

FVector AABTSM25BirdCharacter::GetSlingshotVelocity() const
{
	return ForceMovement->GetVelocity();
}

bool AABTSM25BirdCharacter::IsSlingshotFlightActive() const
{
	return ForceMovement->IsBallisticFlight();
}

void AABTSM25BirdCharacter::ApplyMoveInput(const FVector& Direction, const float Scale)
{
	if (MovementMode == EABTSBirdMovementMode::ForceSuspension)
	{
		ForceMovement->SetMoveInput(Direction, Scale);
	}
	else
	{
		RadialMovement->SetMoveInput(Direction, Scale);
	}
}

void AABTSM25BirdCharacter::ApplyPartyMoveInput(const FVector& Direction, const float Scale)
{
	// Possession is the authoritative input owner.  A duplicated party flag can
	// be stale for part of a control-transfer frame; a possessed pawn must never
	// consume follower steering under any ordering.
	if (Controller != nullptr || bPartyControlled || Direction.IsNearlyZero() || FMath::IsNearlyZero(Scale))
	{
		if (IsControlHandoffDiagnosticsActive()) UE_LOG(LogABTSRuntime, Warning,
			TEXT("[ABTS][M4][HandoffDiag][PartyMoveRejected] Bird=%d Controller=%s Flag=%d Dir=(%.2f,%.2f,%.2f) Scale=%.2f"),
			ABTSBirdIdToIndex(BirdId), *GetNameSafe(Controller), bPartyControlled ? 1 : 0, Direction.X, Direction.Y, Direction.Z, Scale);
		return;
	}
	if (IsControlHandoffDiagnosticsActive()) UE_LOG(LogABTSRuntime, Warning,
		TEXT("[ABTS][M4][HandoffDiag][PartyMoveAccepted] Bird=%d Dir=(%.2f,%.2f,%.2f) Scale=%.2f"),
		ABTSBirdIdToIndex(BirdId), Direction.X, Direction.Y, Direction.Z, Scale);
	GetSphericalSurface()->SetMovementFacing(Scale >= 0.0f ? Direction : -Direction);
	ApplyMoveInput(Direction, Scale);
}

void AABTSM25BirdCharacter::ApplyPartyJump()
{
	if (Controller != nullptr || bPartyControlled)
	{
		if (IsControlHandoffDiagnosticsActive()) UE_LOG(LogABTSRuntime, Warning, TEXT("[ABTS][M4][HandoffDiag][PartyJumpRejected] Bird=%d Controller=%s Flag=%d"), ABTSBirdIdToIndex(BirdId), *GetNameSafe(Controller), bPartyControlled ? 1 : 0);
		return;
	}
	if (MovementMode == EABTSBirdMovementMode::ForceSuspension) ForceMovement->QueueJump();
	else RadialMovement->QueueJump();
}

bool AABTSM25BirdCharacter::IsRadiallyGrounded() const
{
	return MovementMode == EABTSBirdMovementMode::ForceSuspension
		? ForceMovement->IsGrounded()
		: RadialMovement->IsGrounded();
}

void AABTSM25BirdCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	check(PlayerInputComponent);
	PlayerInputComponent->BindAxis(TEXT("ABTS_MoveForward"), this, &AABTSM25BirdCharacter::MoveWithRadialPhysicsForward);
	PlayerInputComponent->BindAxis(TEXT("ABTS_MoveRight"), this, &AABTSM25BirdCharacter::MoveWithRadialPhysicsRight);
	PlayerInputComponent->BindAxis(TEXT("ABTS_Turn"), this, &AABTSM25BirdCharacter::TurnWithRadialPhysics);
	PlayerInputComponent->BindAxis(TEXT("ABTS_LookUp"), this, &AABTSM25BirdCharacter::LookWithRadialPhysics);
	PlayerInputComponent->BindAction(TEXT("ABTS_Jump"), IE_Pressed, this, &AABTSM25BirdCharacter::BeginRadialJump);
}

void AABTSM25BirdCharacter::MoveWithRadialPhysicsForward(const float Value)
{
	if (!FMath::IsNearlyZero(Value) && GetSphericalSurface()->IsSurfaceFrameReady())
	{
		FVector Direction = GetSphericalSurface()->GetTangentForward();
		FVector CameraRight = FVector::ZeroVector;
		if (const AABTSM4PlayerController* M4Controller = Cast<AABTSM4PlayerController>(Controller))
		{
			M4Controller->GetCameraRelativeMovementBasis(GetActorLocation(), Direction, CameraRight);
		}
		GetSphericalSurface()->SetMovementFacing(Value >= 0.0f ? Direction : -Direction);
		const bool bAccepted = Controller != nullptr && IsLocallyControlled();
		if (IsControlHandoffDiagnosticsActive())
		{
			const APlayerController* PlayerController = Cast<APlayerController>(Controller);
			UE_LOG(LogABTSRuntime, Warning,
				TEXT("[ABTS][M4][HandoffDiag][PlayerForward] Bird=%d Value=%.2f Accepted=%d Controller=%s W=%d S=%d"),
				ABTSBirdIdToIndex(BirdId), Value, bAccepted ? 1 : 0, *GetNameSafe(Controller),
				PlayerController && PlayerController->IsInputKeyDown(EKeys::W) ? 1 : 0,
				PlayerController && PlayerController->IsInputKeyDown(EKeys::S) ? 1 : 0);
		}
		if (bAccepted) ApplyMoveInput(Direction, Value);
	}
}

void AABTSM25BirdCharacter::MoveWithRadialPhysicsRight(const float Value)
{
	if (!FMath::IsNearlyZero(Value) && GetSphericalSurface()->IsSurfaceFrameReady())
	{
		FVector CameraForward = FVector::ZeroVector;
		FVector Direction = GetSphericalSurface()->GetTangentRight();
		if (const AABTSM4PlayerController* M4Controller = Cast<AABTSM4PlayerController>(Controller))
		{
			M4Controller->GetCameraRelativeMovementBasis(GetActorLocation(), CameraForward, Direction);
		}
		GetSphericalSurface()->SetMovementFacing(Value >= 0.0f ? Direction : -Direction);
		const bool bAccepted = Controller != nullptr && IsLocallyControlled();
		if (IsControlHandoffDiagnosticsActive())
		{
			const APlayerController* PlayerController = Cast<APlayerController>(Controller);
			UE_LOG(LogABTSRuntime, Warning,
				TEXT("[ABTS][M4][HandoffDiag][PlayerRight] Bird=%d Value=%.2f Accepted=%d Controller=%s D=%d A=%d"),
				ABTSBirdIdToIndex(BirdId), Value, bAccepted ? 1 : 0, *GetNameSafe(Controller),
				PlayerController && PlayerController->IsInputKeyDown(EKeys::D) ? 1 : 0,
				PlayerController && PlayerController->IsInputKeyDown(EKeys::A) ? 1 : 0);
		}
		if (bAccepted) ApplyMoveInput(Direction, Value);
	}
}

void AABTSM25BirdCharacter::TurnWithRadialPhysics(const float Value)
{
	if (Controller && Controller->IsA<AABTSM4PlayerController>()) return;
	TurnOnSphere(Value);
}

void AABTSM25BirdCharacter::LookWithRadialPhysics(const float Value)
{
	if (Controller && Controller->IsA<AABTSM4PlayerController>()) return;
	LookOnSphere(Value);
}

void AABTSM25BirdCharacter::BeginRadialJump()
{
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][Jump] Space input reached AABTSM25BirdCharacter. Mode=%s"),
		MovementMode == EABTSBirdMovementMode::ForceSuspension ? TEXT("ForceSuspension") : TEXT("LegacySweep"));
	const bool bAccepted = Controller != nullptr && IsLocallyControlled();
	if (IsControlHandoffDiagnosticsActive()) UE_LOG(LogABTSRuntime, Warning, TEXT("[ABTS][M4][HandoffDiag][PlayerJump] Bird=%d Accepted=%d Controller=%s Grounded=%d"), ABTSBirdIdToIndex(BirdId), bAccepted ? 1 : 0, *GetNameSafe(Controller), IsRadiallyGrounded() ? 1 : 0);
	if (!bAccepted) return;
	if (MovementMode == EABTSBirdMovementMode::ForceSuspension)
	{
		ForceMovement->QueueJump();
	}
	else
	{
		RadialMovement->QueueJump();
	}
}

void AABTSM25BirdCharacter::LogControlDiagnosticSnapshot()
{
	const FVector ForceVelocity = ForceMovement->GetVelocity();
	const FVector ForcePending = ForceMovement->GetPendingMoveVector();
	const FVector LegacyVelocity = RadialMovement->GetVelocity();
	const FVector LegacyPending = RadialMovement->GetPendingMoveVector();
	UE_LOG(LogABTSRuntime, Warning,
		TEXT("[ABTS][M4][HandoffDiag][Snapshot] Bird=%d Mode=%d Flag=%d Controller=%s Local=%d ForceVel=(%.1f,%.1f,%.1f) ForcePending=(%.2f,%.2f,%.2f) ForceGround=%d ForceDetach=%d ForceJump=%.3f ForceGrace=%.3f LegacyVel=(%.1f,%.1f,%.1f) LegacyPending=(%.2f,%.2f,%.2f) LegacyGround=%d LegacyJump=%.3f LegacyGrace=%.3f"),
		ABTSBirdIdToIndex(BirdId), static_cast<int32>(MovementMode), bPartyControlled ? 1 : 0, *GetNameSafe(Controller), IsLocallyControlled() ? 1 : 0,
		ForceVelocity.X, ForceVelocity.Y, ForceVelocity.Z, ForcePending.X, ForcePending.Y, ForcePending.Z, ForceMovement->IsGrounded() ? 1 : 0, SurfaceSuspension->IsJumpDetachActive() ? 1 : 0, ForceMovement->GetJumpBufferRemainingSeconds(), ForceMovement->GetControlHandoffJumpGraceRemainingSeconds(),
		LegacyVelocity.X, LegacyVelocity.Y, LegacyVelocity.Z, LegacyPending.X, LegacyPending.Y, LegacyPending.Z, RadialMovement->IsGrounded() ? 1 : 0, RadialMovement->GetJumpBufferRemainingSeconds(), RadialMovement->GetControlHandoffJumpGraceRemainingSeconds());
}
