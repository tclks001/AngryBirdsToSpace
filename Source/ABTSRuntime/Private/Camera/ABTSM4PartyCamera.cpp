// Copyright Epic Games, Inc. All Rights Reserved.

#include "Camera/ABTSM4PartyCamera.h"

#include "ABTSRuntime.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "Party/ABTSBirdParty.h"
#include "Party/ABTSBirdPartySettings.h"
#include "Planet/ABTSM2Planet.h"
#include "Player/ABTSM25BirdCharacter.h"

AABTSM4PartyCamera::AABTSM4PartyCamera()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	GetCameraComponent()->SetFieldOfView(52.0f);
}

void AABTSM4PartyCamera::BeginPlay()
{
	Super::BeginPlay();
	UpdateCamera(0.0f, true);
}

void AABTSM4PartyCamera::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateCamera(DeltaSeconds, false);
}

void AABTSM4PartyCamera::UpdateCamera(const float DeltaSeconds, const bool bForceInstant)
{
	AABTSBirdParty* ResolvedParty = FindParty();
	AABTSM2Planet* ResolvedPlanet = FindPlanet();
	AABTSM25BirdCharacter* TargetBird = ResolvedParty ? ResolvedParty->GetControlledBird() : nullptr;
	if (ResolvedParty == nullptr || ResolvedPlanet == nullptr || TargetBird == nullptr) return;

	const AABTSBirdPartySettings* Settings = ResolvedParty->GetResolvedSettings();
	const float HeightCM = Settings ? Settings->CameraHeightCM : 720.0f;
	const float BackDistanceCM = Settings ? Settings->CameraBackDistanceCM : 320.0f;
	const float LookAtHeightCM = Settings ? Settings->CameraLookAtHeightCM : 30.0f;
	const float PositionLag = Settings ? Settings->CameraPositionLagSpeed : 4.5f;
	const float RotationLag = Settings ? Settings->CameraRotationLagSpeed : 7.0f;
	GetCameraComponent()->SetFieldOfView(Settings ? Settings->CameraFieldOfViewDegrees : 52.0f);

	const FVector TargetLocation = TargetBird->GetActorLocation();
	const FVector Up = ResolvedPlanet->GetRadialUpAtWorldLocation(TargetLocation);
	FVector Forward = FVector::VectorPlaneProject(TargetBird->GetActorForwardVector(), Up).GetSafeNormal();
	if (Forward.IsNearlyZero()) Forward = FVector::VectorPlaneProject(LastValidForward, Up).GetSafeNormal();
	if (Forward.IsNearlyZero()) Forward = FVector::CrossProduct(Up, FVector::RightVector).GetSafeNormal();
	LastValidForward = Forward;

	const FVector DesiredLocation = TargetLocation + Up * HeightCM - Forward * BackDistanceCM;
	const FVector LookAtLocation = TargetLocation + Up * LookAtHeightCM;
	const FRotator DesiredRotation = FRotationMatrix::MakeFromX(LookAtLocation - DesiredLocation).Rotator();
	const bool bTargetChanged = LastTargetBird.Get() != TargetBird;
	const bool bInstant = bForceInstant || !bInitializedView;
	const FVector NewLocation = bInstant
		? DesiredLocation
		: FMath::VInterpTo(GetActorLocation(), DesiredLocation, DeltaSeconds, PositionLag);
	const FRotator NewRotation = bInstant
		? DesiredRotation
		: FMath::RInterpTo(GetActorRotation(), DesiredRotation, DeltaSeconds, RotationLag);
	SetActorLocationAndRotation(NewLocation, NewRotation, false, nullptr, ETeleportType::TeleportPhysics);

	if (bTargetChanged)
	{
		const FVector LookDirection = (LookAtLocation - DesiredLocation).GetSafeNormal();
		const float DegreesFromStraightDown = FMath::RadiansToDegrees(FMath::Acos(
			FMath::Clamp(FVector::DotProduct(LookDirection, -Up), -1.0f, 1.0f)));
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M4][Camera] Target=%d Height=%.1f Back=%.1f DownAngle=%.1f PositionLag=%.2f RotationLag=%.2f"),
			ABTSBirdIdToIndex(TargetBird->GetBirdId()),
			HeightCM,
			BackDistanceCM,
			DegreesFromStraightDown,
			PositionLag,
			RotationLag);
	}
	LastTargetBird = TargetBird;
	bInitializedView = true;
}

AABTSBirdParty* AABTSM4PartyCamera::FindParty()
{
	if (Party.IsValid()) return Party.Get();
	for (TActorIterator<AABTSBirdParty> It(GetWorld()); It; ++It)
	{
		Party = *It;
		return Party.Get();
	}
	return nullptr;
}

AABTSM2Planet* AABTSM4PartyCamera::FindPlanet()
{
	if (Planet.IsValid()) return Planet.Get();
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
