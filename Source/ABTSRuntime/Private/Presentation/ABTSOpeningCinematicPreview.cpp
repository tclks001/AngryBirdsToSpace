// Copyright Epic Games, Inc. All Rights Reserved.

#include "Presentation/ABTSOpeningCinematicPreview.h"

#include "ABTSRuntime.h"
#include "Camera/CameraComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Presentation/ABTSBirdAnimationPresentationComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr int32 OpeningBirdCount = static_cast<int32>(EABTSOpeningBird::Count);

	FTransform ResolvePreviewSpawnTransform(UWorld& World)
	{
		APlayerController* Controller = World.GetFirstPlayerController();
		const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
		FVector Up = Pawn ? Pawn->GetActorUpVector() : FVector::UpVector;
		FVector Forward = Pawn ? Pawn->GetActorForwardVector() : FVector::ForwardVector;
		FVector Origin = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
		if (Controller)
		{
			FVector ViewLocation;
			FRotator ViewRotation;
			Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
			if (!Pawn)
			{
				Forward = ViewRotation.Vector();
				Origin = ViewLocation;
			}
		}
		Up = Up.GetSafeNormal();
		Forward = FVector::VectorPlaneProject(Forward, Up).GetSafeNormal();
		if (Forward.IsNearlyZero()) Forward = FVector::ForwardVector;
		Origin += Up * 2500.0f;
		return FTransform(FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat(), Origin);
	}

	void SpawnOpeningPreview(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || !World->IsGameWorld())
		{
			UE_LOG(LogABTSRuntime, Warning, TEXT("[ABTS][OpeningPreview] Command requires a running PIE or game world."));
			return;
		}
		for (TActorIterator<AABTSOpeningCinematicPreview> It(World); It; ++It)
		{
			UE_LOG(LogABTSRuntime, Warning, TEXT("[ABTS][OpeningPreview] A preview is already active."));
			return;
		}
		const float TimeScale = Args.Num() > 0 ? FMath::Clamp(FCString::Atof(*Args[0]), 0.05f, 8.0f) : 1.0f;
		const FTransform SpawnTransform = ResolvePreviewSpawnTransform(*World);
		AABTSOpeningCinematicPreview* Preview = World->SpawnActorDeferred<AABTSOpeningCinematicPreview>(
			AABTSOpeningCinematicPreview::StaticClass(),
			SpawnTransform,
			nullptr,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Preview) return;
		Preview->SetPreviewTimeScale(TimeScale);
		Preview->FinishSpawning(SpawnTransform);
	}

	void StopOpeningPreview(const TArray<FString>&, UWorld* World)
	{
		if (!World) return;
		for (TActorIterator<AABTSOpeningCinematicPreview> It(World); It; ++It)
		{
			It->StopPreview();
			return;
		}
	}

	FAutoConsoleCommandWithWorldAndArgs GABTSOpeningPreviewCommand(
		TEXT("ABTS.OpeningPreview"),
		TEXT("Spawn the isolated 42-second C++ opening preview. Optional argument: time scale (0.05-8.0)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SpawnOpeningPreview));

	FAutoConsoleCommandWithWorldAndArgs GABTSOpeningPreviewStopCommand(
		TEXT("ABTS.OpeningPreview.Stop"),
		TEXT("Stop the active isolated opening preview and restore the previous view target."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&StopOpeningPreview));
}

AABTSOpeningCinematicPreview::AABTSOpeningCinematicPreview()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	CinematicCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("CinematicCamera"));
	CinematicCamera->SetupAttachment(SceneRoot);
	CinematicCamera->SetFieldOfView(50.0f);

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> BirdMeshAsset(
		TEXT("/Game/CuteBird/Meshes/SM_Cute_Bird.SM_Cute_Bird"));
	for (int32 Index = 0; Index < OpeningBirdCount; ++Index)
	{
		USkeletalMeshComponent* BirdVisual = CreateDefaultSubobject<USkeletalMeshComponent>(
			*FString::Printf(TEXT("BirdVisual%d"), Index));
		BirdVisual->SetupAttachment(SceneRoot);
		BirdVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BirdVisual->SetGenerateOverlapEvents(false);
		BirdVisual->SetSimulatePhysics(false);
		BirdVisual->SetCanEverAffectNavigation(false);
		BirdVisual->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		BirdVisual->SetRelativeScale3D(FVector(4.0f));
		if (BirdMeshAsset.Succeeded()) BirdVisual->SetSkeletalMesh(BirdMeshAsset.Object);
		BirdVisuals.Add(BirdVisual);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> RedColor(TEXT("/Game/CuteBird/Materials/CuteBirdColor_Materials/M_CuteBird_12.M_CuteBird_12"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> RedFace(TEXT("/Game/CuteBird/Materials/Face_Materials/M_Dino_face_23.M_Dino_face_23"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BlueColor(TEXT("/Game/CuteBird/Materials/CuteBirdColor_Materials/M_CuteBird_3.M_CuteBird_3"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BlueFace(TEXT("/Game/CuteBird/Materials/Face_Materials/M_Dino_face_3.M_Dino_face_3"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> YellowColor(TEXT("/Game/CuteBird/Materials/CuteBirdColor_Materials/M_CuteBird_10.M_CuteBird_10"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> YellowFace(TEXT("/Game/CuteBird/Materials/Face_Materials/M_Dino_face_6.M_Dino_face_6"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BlackColor(TEXT("/Game/CuteBird/Materials/CuteBirdColor_Materials/M_CuteBird_16.M_CuteBird_16"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BlackFace(TEXT("/Game/CuteBird/Materials/Face_Materials/M_Dino_face_17.M_Dino_face_17"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> WhiteColor(TEXT("/Game/CuteBird/Materials/CuteBirdColor_Materials/M_CuteBird_0.M_CuteBird_0"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> WhiteFace(TEXT("/Game/CuteBird/Materials/Face_Materials/M_Dino_face_1.M_Dino_face_1"));
	UMaterialInterface* Colors[] = {RedColor.Object, BlueColor.Object, YellowColor.Object, BlackColor.Object, WhiteColor.Object};
	UMaterialInterface* Faces[] = {RedFace.Object, BlueFace.Object, YellowFace.Object, BlackFace.Object, WhiteFace.Object};
	for (int32 Index = 0; Index < BirdVisuals.Num(); ++Index)
	{
		if (Colors[Index]) BirdVisuals[Index]->SetMaterial(0, Colors[Index]);
		if (Faces[Index]) BirdVisuals[Index]->SetMaterial(1, Faces[Index]);
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderAsset(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	PreviewStage = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PreviewStage"));
	PreviewStage->SetupAttachment(SceneRoot);
	PreviewStage->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewStage->SetGenerateOverlapEvents(false);
	PreviewStage->SetCanEverAffectNavigation(false);
	PreviewStage->SetRelativeLocation(FVector(0.0f, 0.0f, -18.0f));
	PreviewStage->SetRelativeScale3D(FVector(16.0f, 16.0f, 0.12f));
	if (CylinderAsset.Succeeded()) PreviewStage->SetStaticMesh(CylinderAsset.Object);

	UFOVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("UFOVisual"));
	UFOVisual->SetupAttachment(SceneRoot);
	UFOVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	UFOVisual->SetGenerateOverlapEvents(false);
	UFOVisual->SetCanEverAffectNavigation(false);
	UFOVisual->SetRelativeScale3D(FVector(2.4f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> UFOAsset(TEXT("/Game/StaticMesh/UFO/SM_UFO_Intact.SM_UFO_Intact"));
	if (UFOAsset.Succeeded()) UFOVisual->SetStaticMesh(UFOAsset.Object);

	CaptureBeam = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CaptureBeam"));
	CaptureBeam->SetupAttachment(SceneRoot);
	CaptureBeam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CaptureBeam->SetGenerateOverlapEvents(false);
	CaptureBeam->SetCanEverAffectNavigation(false);
	if (CylinderAsset.Succeeded()) CaptureBeam->SetStaticMesh(CylinderAsset.Object);

	CaptureLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("CaptureLight"));
	CaptureLight->SetupAttachment(SceneRoot);
	CaptureLight->SetIntensity(7500.0f);
	CaptureLight->SetAttenuationRadius(700.0f);
	CaptureLight->SetLightColor(FColor(125, 220, 255));
	CaptureLight->SetCastShadows(false);

	UFOVisual->SetVisibility(false);
	CaptureBeam->SetVisibility(false);
	CaptureLight->SetVisibility(false);
}

void AABTSOpeningCinematicPreview::BeginPlay()
{
	Super::BeginPlay();
	InitializeAnimationDrivers();
	PreviewController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (PreviewController)
	{
		SavedViewTarget = PreviewController->GetViewTarget();
		PreviewController->SetViewTarget(this);
	}
	UpdateBirds(0.0f);
	UpdateUFOAndCaptureBeam();
	UpdateCamera();
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][OpeningPreview][Started] Duration=%.1f TimeScale=%.2f GameplayMutation=0 MapBinding=0"),
		FABTSOpeningCinematicEvaluator::DurationSeconds, PreviewTimeScale);
}

void AABTSOpeningCinematicPreview::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bPreviewFinished) return;
	ElapsedSeconds += FMath::Max(0.0f, DeltaSeconds) * PreviewTimeScale;
	UpdateBirds(DeltaSeconds * PreviewTimeScale);
	UpdateUFOAndCaptureBeam();
	UpdateCamera();
	if (ElapsedSeconds >= FABTSOpeningCinematicEvaluator::DurationSeconds)
	{
		FinishPreview(true);
	}
}

void AABTSOpeningCinematicPreview::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (!bPreviewFinished) FinishPreview(false);
	Super::EndPlay(EndPlayReason);
}

void AABTSOpeningCinematicPreview::SetPreviewTimeScale(const float InTimeScale)
{
	PreviewTimeScale = FMath::Clamp(InTimeScale, 0.05f, 8.0f);
}

void AABTSOpeningCinematicPreview::StopPreview()
{
	FinishPreview(true);
}

void AABTSOpeningCinematicPreview::InitializeAnimationDrivers()
{
	AnimationDrivers.Reserve(BirdVisuals.Num());
	for (int32 Index = 0; Index < BirdVisuals.Num(); ++Index)
	{
		UABTSBirdAnimationPresentationComponent* Driver = NewObject<UABTSBirdAnimationPresentationComponent>(
			this, *FString::Printf(TEXT("OpeningBirdAnimation%d"), Index));
		if (Driver)
		{
			Driver->RegisterComponent();
			Driver->InitializePresentation(BirdVisuals[Index], true);
		}
		AnimationDrivers.Add(Driver);
	}
}

void AABTSOpeningCinematicPreview::UpdateBirds(const float DeltaSeconds)
{
	for (int32 Index = 0; Index < BirdVisuals.Num(); ++Index)
	{
		const FABTSOpeningBirdPose Pose = FABTSOpeningCinematicEvaluator::EvaluateBird(
			ElapsedSeconds, static_cast<EABTSOpeningBird>(Index));
		USkeletalMeshComponent* Visual = BirdVisuals[Index];
		if (!Visual) continue;
		Visual->SetVisibility(Pose.bVisible, true);
		Visual->SetRelativeLocation(Pose.LocalPosition);
		Visual->SetRelativeRotation(ResolveBirdVisualRotation(Pose.LocalFacing));
		if (AnimationDrivers.IsValidIndex(Index) && AnimationDrivers[Index])
		{
			FABTSBirdAnimationSnapshot Snapshot;
			Snapshot.bGrounded = Pose.AnimationCue != EABTSOpeningAnimationCue::Fly;
			Snapshot.bForceFlight = Pose.AnimationCue == EABTSOpeningAnimationCue::Fly;
			Snapshot.TangentialSpeedCMPerSecond = Pose.AnimationCue == EABTSOpeningAnimationCue::Move ? 315.0f : 0.0f;
			AnimationDrivers[Index]->UpdatePresentation(Snapshot, DeltaSeconds);
		}
	}
}

void AABTSOpeningCinematicPreview::UpdateUFOAndCaptureBeam()
{
	const FABTSOpeningUFOPose Pose = FABTSOpeningCinematicEvaluator::EvaluateUFO(ElapsedSeconds);
	UFOVisual->SetVisibility(Pose.bVisible, true);
	UFOVisual->SetRelativeLocation(Pose.LocalPosition);
	UFOVisual->SetRelativeRotation(Pose.LocalRotation);

	const FABTSOpeningBirdPose WhitePose = FABTSOpeningCinematicEvaluator::EvaluateBird(
		ElapsedSeconds, EABTSOpeningBird::White);
	const FVector BeamStart = Pose.LocalPosition - FVector(0.0f, 0.0f, 55.0f);
	const FVector BeamEnd = WhitePose.LocalPosition + FVector(0.0f, 0.0f, 25.0f);
	const FVector BeamVector = BeamEnd - BeamStart;
	const float BeamLength = BeamVector.Size();
	const bool bShowBeam = Pose.bCaptureBeamVisible && WhitePose.bVisible && BeamLength > 1.0f;
	CaptureBeam->SetVisibility(bShowBeam, true);
	CaptureLight->SetVisibility(bShowBeam, true);
	if (bShowBeam)
	{
		CaptureBeam->SetRelativeLocation((BeamStart + BeamEnd) * 0.5f);
		CaptureBeam->SetRelativeRotation(FQuat::FindBetweenNormals(FVector::UpVector, BeamVector.GetSafeNormal()));
		CaptureBeam->SetRelativeScale3D(FVector(0.28f, 0.28f, BeamLength / 100.0f));
		CaptureLight->SetRelativeLocation(BeamEnd);
	}
}

void AABTSOpeningCinematicPreview::UpdateCamera()
{
	const FABTSOpeningCameraPose Pose = FABTSOpeningCinematicEvaluator::EvaluateCamera(ElapsedSeconds);
	const FVector CameraWorldLocation = GetActorTransform().TransformPosition(Pose.LocalPosition);
	const FVector LookAtWorld = GetActorTransform().TransformPosition(Pose.LocalLookAt);
	const FVector LookDirection = (LookAtWorld - CameraWorldLocation).GetSafeNormal();
	CinematicCamera->SetWorldLocationAndRotation(
		CameraWorldLocation,
		FRotationMatrix::MakeFromXZ(LookDirection, GetActorUpVector()).ToQuat());
	CinematicCamera->SetFieldOfView(Pose.FieldOfViewDegrees);
}

void AABTSOpeningCinematicPreview::FinishPreview(const bool bBlendBack)
{
	if (bPreviewFinished) return;
	bPreviewFinished = true;
	SetActorTickEnabled(false);
	if (PreviewController && SavedViewTarget && PreviewController->GetViewTarget() == this)
	{
		if (bBlendBack) PreviewController->SetViewTargetWithBlend(SavedViewTarget, 1.0f);
		else PreviewController->SetViewTarget(SavedViewTarget);
	}
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][OpeningPreview][Completed] Elapsed=%.2f BlendBack=%d GameplayMutation=0"),
		ElapsedSeconds, bBlendBack ? 1 : 0);
	SetLifeSpan(bBlendBack ? 1.1f : 0.01f);
}

FQuat AABTSOpeningCinematicPreview::ResolveBirdVisualRotation(const FVector& LocalFacing) const
{
	const FVector Facing = LocalFacing.IsNearlyZero() ? FVector::ForwardVector : LocalFacing.GetSafeNormal();
	return FRotationMatrix::MakeFromXZ(Facing, FVector::UpVector).ToQuat()
		* FRotator(0.0f, -90.0f, 0.0f).Quaternion();
}
