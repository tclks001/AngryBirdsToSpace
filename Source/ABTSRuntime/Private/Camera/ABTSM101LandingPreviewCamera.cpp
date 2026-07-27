// Copyright Epic Games, Inc. All Rights Reserved.

#include "Camera/ABTSM101LandingPreviewCamera.h"

#include "ABTSRuntime.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Slingshot/ABTSM6Types.h"
#include "Terrain/ABTSM3Planet.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float BasicShapeSphereDiameterCM = 100.0f;

	FVector ResolveStableScreenUp(const FVector& LandingUp, const FVector& Look)
	{
		FVector ScreenUp = FVector::VectorPlaneProject(LandingUp, Look).GetSafeNormal();
		if (ScreenUp.IsNearlyZero())
		{
			ScreenUp = FVector::VectorPlaneProject(FVector::ForwardVector, Look).GetSafeNormal();
		}
		if (ScreenUp.IsNearlyZero())
		{
			ScreenUp = FVector::VectorPlaneProject(FVector::RightVector, Look).GetSafeNormal();
		}
		return ScreenUp.IsNearlyZero() ? FVector::UpVector : ScreenUp;
	}
}

AABTSM101LandingPreviewCamera::AABTSM101LandingPreviewCamera()
{
	PrimaryActorTick.bCanEverTick = false;
	SetActorEnableCollision(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	SceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("LandingSceneCapture"));
	SceneCapture->SetupAttachment(Root);
	SceneCapture->bCaptureEveryFrame = false;
	SceneCapture->bCaptureOnMovement = false;
	// The capture is manually updated at a low cadence while aim changes can
	// move it a long distance. Treat every manual capture as a camera cut so
	// temporal history from the prior predicted landing never ghosts into the
	// new frame.
	SceneCapture->bAlwaysPersistRenderingState = false;
	SceneCapture->bExcludeFromSceneTextureExtents = true;
	SceneCapture->CaptureSource = SCS_FinalColorLDR;

	TrajectoryPointInstances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("LandingTrajectoryPoints"));
	TrajectoryPointInstances->SetupAttachment(Root);
	TrajectoryPointInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TrajectoryPointInstances->SetGenerateOverlapEvents(false);
	TrajectoryPointInstances->SetCanEverAffectNavigation(false);
	TrajectoryPointInstances->SetVisibleInSceneCaptureOnly(true);
	// A SceneCapture-only primitive must remain logically visible: visibility
	// false removes it from every view, including the SceneCapture.  Empty
	// instances are the inactive state; the SceneCapture-only flag hides it
	// from the player camera.
	TrajectoryPointInstances->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded()) TrajectoryPointInstances->SetStaticMesh(SphereMesh.Object);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicShapeMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BasicShapeMaterial.Succeeded()) TrajectoryPointInstances->SetMaterial(0, BasicShapeMaterial.Object);
}

void AABTSM101LandingPreviewCamera::Configure(const FABTSM10ScoutMapSettings& InSettings)
{
	Settings = InSettings;
	EnsureRenderTarget();
}

void AABTSM101LandingPreviewCamera::UpdatePreview(
	const FABTSM6TrajectoryPreview& Preview,
	const AABTSM3Planet& Planet,
	const float DeltaSeconds)
{
	if (!Preview.bHasPrimarySurfaceLanding)
	{
		DeactivatePreview();
		return;
	}

	EnsureRenderTarget();
	if (RenderTarget == nullptr || SceneCapture == nullptr || TrajectoryPointInstances == nullptr) return;
	if (!bPreviewActive)
	{
		bPreviewActive = true;
		CaptureAccumulatorSeconds = 0.0f;
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M10.1][LandingPreview] Activated Distance=%.1f FOV=%.1f CaptureHz=%.1f"),
			Settings.LandingViewCameraDistanceCM, Settings.LandingViewFieldOfViewDegrees,
			Settings.LandingViewCaptureHz);
		// The first eligible frame must already contain an image. Subsequent
		// captures are limited by the cadence below.
		RefreshCapture(Preview, Planet);
		return;
	}

	CaptureAccumulatorSeconds += FMath::Max(0.0f, DeltaSeconds);
	const float CaptureInterval = 1.0f / FMath::Clamp(Settings.LandingViewCaptureHz, 1.0f, 60.0f);
	if (CaptureAccumulatorSeconds < CaptureInterval) return;
	CaptureAccumulatorSeconds = FMath::Fmod(CaptureAccumulatorSeconds, CaptureInterval);
	RefreshCapture(Preview, Planet);
}

void AABTSM101LandingPreviewCamera::DeactivatePreview()
{
	if (!bPreviewActive) return;
	bPreviewActive = false;
	CaptureAccumulatorSeconds = 0.0f;
	if (TrajectoryPointInstances)
	{
		TrajectoryPointInstances->ClearInstances();
	}
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M10.1][LandingPreview] Hidden"));
}

void AABTSM101LandingPreviewCamera::EnsureRenderTarget()
{
	if (SceneCapture == nullptr) return;
	const int32 Width = FMath::Clamp(Settings.LandingViewRenderTargetWidth, 128, 2048);
	const int32 Height = FMath::Clamp(Settings.LandingViewRenderTargetHeight, 72, 2048);
	if (RenderTarget && RenderTarget->SizeX == Width && RenderTarget->SizeY == Height)
	{
		SceneCapture->TextureTarget = RenderTarget;
		SceneCapture->FOVAngle = FMath::Clamp(Settings.LandingViewFieldOfViewDegrees, 10.0f, 120.0f);
		if (TrajectoryMaterial)
		{
			TrajectoryMaterial->SetVectorParameterValue(TEXT("Color"), Settings.LandingViewTrajectoryColor);
			TrajectoryMaterial->SetVectorParameterValue(TEXT("BaseColor"), Settings.LandingViewTrajectoryColor);
		}
		return;
	}

	RenderTarget = NewObject<UTextureRenderTarget2D>(this, NAME_None, RF_Transient);
	if (RenderTarget == nullptr) return;
	RenderTarget->ClearColor = FLinearColor(0.008f, 0.012f, 0.020f, 1.0f);
	RenderTarget->TargetGamma = 2.2f;
	RenderTarget->InitCustomFormat(Width, Height, PF_B8G8R8A8, false);
	RenderTarget->UpdateResourceImmediate(true);
	SceneCapture->TextureTarget = RenderTarget;
	SceneCapture->FOVAngle = FMath::Clamp(Settings.LandingViewFieldOfViewDegrees, 10.0f, 120.0f);

	if (TrajectoryPointInstances && !TrajectoryMaterial)
	{
		TrajectoryMaterial = UMaterialInstanceDynamic::Create(TrajectoryPointInstances->GetMaterial(0), this);
		if (TrajectoryMaterial)
		{
			TrajectoryPointInstances->SetMaterial(0, TrajectoryMaterial);
		}
	}
	if (TrajectoryMaterial)
	{
		TrajectoryMaterial->SetVectorParameterValue(TEXT("Color"), Settings.LandingViewTrajectoryColor);
		TrajectoryMaterial->SetVectorParameterValue(TEXT("BaseColor"), Settings.LandingViewTrajectoryColor);
	}
}

void AABTSM101LandingPreviewCamera::RefreshCapture(
	const FABTSM6TrajectoryPreview& Preview,
	const AABTSM3Planet& Planet)
{
	if (SceneCapture == nullptr) return;
	const FVector Landing = Preview.PrimarySurfaceLandingWorld;
	// Gameplay's stable surface frame is radial.  Do not use the rendered terrain
	// normal here: the M3 blend can vary locally and would introduce visual roll
	// while the player makes a small aim adjustment.
	const FVector LandingUp = Planet.GetRadialUpAtWorldLocation(Landing).GetSafeNormal();
	const FVector SafeLandingUp = LandingUp.IsNearlyZero() ? FVector::UpVector : LandingUp;
	const FVector IncidenceDirection = ResolveIncidenceDirection(Preview, SafeLandingUp);
	const float Distance = FMath::Clamp(Settings.LandingViewCameraDistanceCM, 100.0f, 100000.0f);
	// This is intentionally the unmodified reverse extension of the contact
	// velocity.  A radial "safety lift" would make the preview disagree with the
	// editor-facing contract and with the player-visible approach direction.
	const FVector CameraLocation = Landing - IncidenceDirection * Distance;
	const FVector Look = (Landing - CameraLocation).GetSafeNormal();
	if (Look.IsNearlyZero()) return;
	const FVector ScreenUp = ResolveStableScreenUp(SafeLandingUp, Look);
	const FQuat Rotation = FRotationMatrix::MakeFromXZ(Look, ScreenUp).ToQuat();

	// Keep Root at identity so prior AddInstance(..., true) world transforms do
	// not get reinterpreted when this camera moves to the next predicted landing.
	SceneCapture->SetWorldLocationAndRotation(CameraLocation, Rotation);
	SceneCapture->FOVAngle = FMath::Clamp(Settings.LandingViewFieldOfViewDegrees, 10.0f, 120.0f);
	RebuildTrajectoryPoints(Preview);
	SceneCapture->bCameraCutThisFrame = true;
	SceneCapture->CaptureScene();
}

FVector AABTSM101LandingPreviewCamera::ResolveIncidenceDirection(
	const FABTSM6TrajectoryPreview& Preview,
	const FVector& LandingUp) const
{
	FVector Direction = Preview.PrimarySurfaceLandingVelocity.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		for (int32 Index = Preview.WorldPoints.Num() - 1; Index > 0; --Index)
		{
			Direction = (Preview.WorldPoints[Index] - Preview.WorldPoints[Index - 1]).GetSafeNormal();
			if (!Direction.IsNearlyZero()) break;
		}
	}
	// A missing velocity should still place the camera on the outward radial
	// side and look back toward the surface landing.
	return Direction.IsNearlyZero() ? -LandingUp : Direction;
}

void AABTSM101LandingPreviewCamera::RebuildTrajectoryPoints(const FABTSM6TrajectoryPreview& Preview)
{
	if (TrajectoryPointInstances == nullptr || TrajectoryPointInstances->GetStaticMesh() == nullptr) return;
	TrajectoryPointInstances->ClearInstances();
	const int32 Stride = FMath::Clamp(Settings.LandingViewTrajectoryStride, 1, 16);
	const int32 MaximumPointCount = FMath::Clamp(Settings.LandingViewTrajectoryPointCount, 8, 128);
	const int32 FirstIndex = FMath::Max(0, Preview.WorldPoints.Num() - MaximumPointCount * Stride);
	const float Scale = FMath::Clamp(Settings.LandingViewTrajectoryPointSizeCM, 1.0f, 100.0f)
		/ BasicShapeSphereDiameterCM;
	TArray<FTransform> Instances;
	Instances.Reserve(MaximumPointCount + 1);
	for (int32 Index = FirstIndex; Index < Preview.WorldPoints.Num(); Index += Stride)
	{
		Instances.Emplace(FQuat::Identity, Preview.WorldPoints[Index], FVector(Scale));
	}
	if (Instances.IsEmpty()
		|| !Instances.Last().GetLocation().Equals(Preview.PrimarySurfaceLandingWorld, 1.0f))
	{
		Instances.Emplace(FQuat::Identity, Preview.PrimarySurfaceLandingWorld, FVector(Scale));
	}
	TrajectoryPointInstances->AddInstances(Instances, false, true, false);
}
