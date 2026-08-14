// Copyright Epic Games, Inc. All Rights Reserved.

#include "Camera/ABTSM101LandingPreviewCamera.h"

#include "ABTSRuntime.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/ABTSStylizedRenderingTypes.h"
#include "Rendering/ABTSStylizedSceneCaptureRegistry.h"
#include "Slingshot/ABTSM6Types.h"
#include "Terrain/ABTSM3Planet.h"
#include "UObject/ConstructorHelpers.h"
#include "World/ABTSM9Satellite.h"

namespace
{
	constexpr float BasicShapeSphereDiameterCM = 100.0f;
	constexpr int32 LandingPreviewHistoryWarmupCaptureCount = 2;
	constexpr float LandingPreviewCameraCutRotationDegrees = 15.0f;

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
	// The main view keeps temporal lighting, shadow and AA histories. Preserve
	// the SceneCapture view state as well; genuine discontinuities are handled by
	// a bounded hidden warmup instead of treating every aim refresh as a cut.
	SceneCapture->bAlwaysPersistRenderingState = true;
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
	const bool bSubjectChanged =
		PreviewSubject != EABTSM101PreviewSubject::PrimaryLanding;
	SetPreviewSubject(EABTSM101PreviewSubject::PrimaryLanding);
	if (!bPreviewActive || bSubjectChanged)
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

void AABTSM101LandingPreviewCamera::UpdateSatellitePreview(
	const FABTSM6TrajectoryPreview& Preview,
	AABTSM9Satellite& Satellite,
	AActor& E5Target,
	const float DeltaSeconds)
{
	if (!IsSatelliteLandingTerminal(Preview)
		|| Preview.WorldPoints.Num() < 2)
	{
		DeactivatePreview();
		return;
	}
	const int32 TerminalSegmentStartIndex =
		FMath::Max(0, Preview.WorldPoints.Num() - 2);
	EnsureRenderTarget();
	if (RenderTarget == nullptr
		|| SceneCapture == nullptr
		|| TrajectoryPointInstances == nullptr)
	{
		return;
	}
	const bool bSubjectChanged =
		PreviewSubject != EABTSM101PreviewSubject::SatelliteLanding;
	SetPreviewSubject(EABTSM101PreviewSubject::SatelliteLanding);
	if (!bPreviewActive || bSubjectChanged)
	{
		bPreviewActive = true;
		CaptureAccumulatorSeconds = 0.0f;
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][M10.1][SatelliteLandingPreview] Activated Terminal=%s Distance=%.1f Pitch=%.1f FOV=%.1f CaptureHz=%.1f"),
			*UEnum::GetValueAsString(Preview.TerminalType),
			Settings.LandingViewCameraDistanceCM,
			Settings.SatelliteLandingViewPitchDegrees,
			Settings.LandingViewFieldOfViewDegrees,
			Settings.LandingViewCaptureHz);
		RefreshSatelliteCapture(
			Preview,
			Satellite,
			E5Target,
			TerminalSegmentStartIndex);
		return;
	}
	CaptureAccumulatorSeconds += FMath::Max(0.0f, DeltaSeconds);
	const float CaptureInterval =
		1.0f
		/ FMath::Clamp(
			Settings.LandingViewCaptureHz,
			1.0f,
			60.0f);
	if (CaptureAccumulatorSeconds < CaptureInterval) return;
	CaptureAccumulatorSeconds = FMath::Fmod(
		CaptureAccumulatorSeconds,
		CaptureInterval);
	RefreshSatelliteCapture(
		Preview,
		Satellite,
		E5Target,
		TerminalSegmentStartIndex);
}

void AABTSM101LandingPreviewCamera::DeactivatePreview()
{
	if (!bPreviewActive
		&& PreviewSubject == EABTSM101PreviewSubject::None)
	{
		return;
	}
	bPreviewActive = false;
	bHasPublishedCapture = false;
	bHasLastCaptureTransform = false;
	RemainingWarmupCaptures = 0;
	SetPreviewSubject(EABTSM101PreviewSubject::None);
	CaptureAccumulatorSeconds = 0.0f;
	if (TrajectoryPointInstances)
	{
		TrajectoryPointInstances->ClearInstances();
	}
	if (SceneCapture)
	{
		SceneCapture->TextureTarget = RenderTarget;
		SceneCapture->ClearShowOnlyComponents();
	}
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M10.1][LandingPreview] Hidden"));
}

void AABTSM101LandingPreviewCamera::SetPreviewSubject(
	const EABTSM101PreviewSubject NewSubject)
{
	if (PreviewSubject == NewSubject) return;
	const EABTSM101PreviewSubject PreviousSubject = PreviewSubject;
	PreviewSubject = NewSubject;
	// A semantic view-class transition changes the background/profile contract.
	// Never publish the prior subject under the new HUD label.
	bHasPublishedCapture = false;
	bHasLastCaptureTransform = false;
	RemainingWarmupCaptures = 0;
	if (SceneCapture)
	{
		switch (NewSubject)
		{
		case EABTSM101PreviewSubject::PrimaryLanding:
			FABTSStylizedSceneCaptureRegistry::Register(
				*SceneCapture,
				EABTSStylizedViewClass::GroundLandingPreview);
			break;
		case EABTSM101PreviewSubject::SatelliteLanding:
			FABTSStylizedSceneCaptureRegistry::Register(
				*SceneCapture,
				EABTSStylizedViewClass::SatelliteLandingPreview);
			break;
		case EABTSM101PreviewSubject::None:
		default:
			FABTSStylizedSceneCaptureRegistry::Unregister(*SceneCapture);
			break;
		}
	}
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M10.1][LandingPreview] Subject=%s Previous=%s"),
		*UEnum::GetValueAsString(NewSubject),
		*UEnum::GetValueAsString(PreviousSubject));
}

bool AABTSM101LandingPreviewCamera::IsSatelliteLandingTerminal(
	const FABTSM6TrajectoryPreview& Preview)
{
	return Preview.TerminalType
			== EABTSM6TrajectoryTerminalType::SatelliteBody
		|| Preview.TerminalType
			== EABTSM6TrajectoryTerminalType::SatelliteE5;
}

bool AABTSM101LandingPreviewCamera::BuildSatelliteLandingViewFrame(
	const FABTSM6TrajectoryPreview& Preview,
	const FVector& SatelliteCenterWorld,
	const float CameraDistanceCM,
	const float PitchDegrees,
	FVector& OutLandingWorld,
	FVector& OutCameraWorld,
	FVector& OutLookDirection,
	FVector& OutScreenUp)
{
	OutLandingWorld = FVector::ZeroVector;
	OutCameraWorld = FVector::ZeroVector;
	OutLookDirection = FVector::ZeroVector;
	OutScreenUp = FVector::ZeroVector;
	if (!IsSatelliteLandingTerminal(Preview)
		|| Preview.TerminalWorldLocation.ContainsNaN()
		|| SatelliteCenterWorld.ContainsNaN()
		|| !FMath::IsFinite(CameraDistanceCM)
		|| CameraDistanceCM <= 0.0f
		|| !FMath::IsFinite(PitchDegrees))
	{
		return false;
	}
	const FVector LandingUp =
		(Preview.TerminalWorldLocation - SatelliteCenterWorld).GetSafeNormal();
	if (LandingUp.IsNearlyZero()) return false;

	FVector TangentialApproach =
		FVector::VectorPlaneProject(
			Preview.TerminalWorldVelocity,
			LandingUp).GetSafeNormal();
	for (int32 Index = Preview.WorldPoints.Num() - 1;
		TangentialApproach.IsNearlyZero() && Index > 0;
		--Index)
	{
		TangentialApproach =
			FVector::VectorPlaneProject(
				Preview.WorldPoints[Index]
					- Preview.WorldPoints[Index - 1],
				LandingUp).GetSafeNormal();
	}
	if (TangentialApproach.IsNearlyZero())
	{
		TangentialApproach =
			FVector::VectorPlaneProject(
				FVector::ForwardVector,
				LandingUp).GetSafeNormal();
	}
	if (TangentialApproach.IsNearlyZero())
	{
		TangentialApproach =
			FVector::VectorPlaneProject(
				FVector::RightVector,
				LandingUp).GetSafeNormal();
	}
	if (TangentialApproach.IsNearlyZero()) return false;

	const float PitchRadians =
		FMath::DegreesToRadians(
			FMath::Clamp(PitchDegrees, 5.0f, 85.0f));
	const FVector Look =
		(TangentialApproach * FMath::Cos(PitchRadians)
			- LandingUp * FMath::Sin(PitchRadians)).GetSafeNormal();
	if (Look.IsNearlyZero()) return false;

	OutLandingWorld = Preview.TerminalWorldLocation;
	OutCameraWorld =
		OutLandingWorld
		- Look * FMath::Clamp(CameraDistanceCM, 100.0f, 100000.0f);
	OutLookDirection = Look;
	OutScreenUp = ResolveStableScreenUp(LandingUp, Look);
	return !OutCameraWorld.ContainsNaN()
		&& !OutLookDirection.ContainsNaN()
		&& !OutScreenUp.ContainsNaN();
}

void AABTSM101LandingPreviewCamera::EnsureRenderTarget()
{
	if (SceneCapture == nullptr) return;
	const int32 Width = FMath::Clamp(Settings.LandingViewRenderTargetWidth, 128, 2048);
	const int32 Height = FMath::Clamp(Settings.LandingViewRenderTargetHeight, 72, 2048);
	if (RenderTarget && WarmupRenderTarget
		&& RenderTarget->SizeX == Width && RenderTarget->SizeY == Height
		&& WarmupRenderTarget->SizeX == Width
		&& WarmupRenderTarget->SizeY == Height)
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
	WarmupRenderTarget = NewObject<UTextureRenderTarget2D>(
		this,
		NAME_None,
		RF_Transient);
	if (WarmupRenderTarget == nullptr)
	{
		RenderTarget = nullptr;
		return;
	}
	WarmupRenderTarget->ClearColor = RenderTarget->ClearColor;
	WarmupRenderTarget->TargetGamma = RenderTarget->TargetGamma;
	WarmupRenderTarget->InitCustomFormat(
		Width,
		Height,
		PF_B8G8R8A8,
		false);
	WarmupRenderTarget->UpdateResourceImmediate(true);
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
	// Restore the unchanged M10.1-B world capture after a calibration E5
	// preview used this same component's isolated ShowOnly/BaseColor mode.
	SceneCapture->PrimitiveRenderMode =
		ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
	SceneCapture->CaptureSource =
		ESceneCaptureSource::SCS_FinalColorLDR;
	// Capture the same production world lighting and GroundDay post-process
	// policy as the main view. There is intentionally no PIP-only shadow lift.
	const FABTSStylizedViewPolicy GroundPreviewPolicy =
		FABTSStylizedRenderingContract::ResolveViewPolicy(
			EABTSStylizedViewClass::GroundLandingPreview);
	SceneCapture->ShowFlags.SetLighting(GroundPreviewPolicy.bUseWorldLighting);
	SceneCapture->ClearShowOnlyComponents();
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
	const FTransform CaptureTransform(Rotation, CameraLocation);
	SceneCapture->SetWorldLocationAndRotation(CameraLocation, Rotation);
	SceneCapture->FOVAngle = FMath::Clamp(Settings.LandingViewFieldOfViewDegrees, 10.0f, 120.0f);
	RebuildTrajectoryPoints(Preview);
	CaptureWithPersistentHistory(CaptureTransform);
}

void AABTSM101LandingPreviewCamera::RefreshSatelliteCapture(
	const FABTSM6TrajectoryPreview& Preview,
	AABTSM9Satellite& Satellite,
	AActor& E5Target,
	const int32 TerminalSegmentStartIndex)
{
	if (SceneCapture == nullptr || TrajectoryPointInstances == nullptr) return;
	FVector LandingWorld;
	FVector CameraLocation;
	FVector Look;
	FVector ScreenUp;
	if (!BuildSatelliteLandingViewFrame(
		Preview,
		Satellite.GetPlanetCenterWorld(),
		Settings.LandingViewCameraDistanceCM,
		Settings.SatelliteLandingViewPitchDegrees,
		LandingWorld,
		CameraLocation,
		Look,
		ScreenUp))
	{
		return;
	}

	SceneCapture->PrimitiveRenderMode =
		ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	// Use final color so the satellite surface consumes the same world lighting,
	// GroundDay exposure/tone and outline as the gameplay camera. The registered
	// view class replaces only empty background pixels with deep space.
	SceneCapture->CaptureSource =
		ESceneCaptureSource::SCS_FinalColorLDR;
	const FABTSStylizedViewPolicy SatellitePreviewPolicy =
		FABTSStylizedRenderingContract::ResolveViewPolicy(
			EABTSStylizedViewClass::SatelliteLandingPreview);
	SceneCapture->ShowFlags.SetLighting(SatellitePreviewPolicy.bUseWorldLighting);
	SceneCapture->ClearShowOnlyComponents();
	SceneCapture->ShowOnlyActorComponents(&Satellite);
	if (Preview.TerminalType
		== EABTSM6TrajectoryTerminalType::SatelliteE5)
	{
		SceneCapture->ShowOnlyActorComponents(&E5Target);
	}
	SceneCapture->ShowOnlyComponent(TrajectoryPointInstances);
	const FQuat CaptureRotation =
		FRotationMatrix::MakeFromXZ(Look, ScreenUp).ToQuat();
	const FTransform CaptureTransform(CaptureRotation, CameraLocation);
	SceneCapture->SetWorldLocationAndRotation(CameraLocation, CaptureRotation);
	SceneCapture->FOVAngle =
		FMath::Clamp(
			Settings.LandingViewFieldOfViewDegrees,
			10.0f,
			120.0f);
	RebuildTrajectoryPointsAround(
		Preview,
		TerminalSegmentStartIndex);
	CaptureWithPersistentHistory(CaptureTransform);
}

bool AABTSM101LandingPreviewCamera::DoesPreviewPoseRequireCameraCut(
	const FTransform& PreviousTransform,
	const FTransform& CurrentTransform,
	const float CameraDistanceCM)
{
	if (PreviousTransform.ContainsNaN()
		|| CurrentTransform.ContainsNaN()
		|| !FMath::IsFinite(CameraDistanceCM))
	{
		return true;
	}
	const float TranslationThresholdCM = FMath::Max(
		400.0f,
		FMath::Clamp(
			CameraDistanceCM,
			100.0f,
			100000.0f) * 0.5f);
	const double TranslationDeltaCM = FVector::Distance(
		PreviousTransform.GetLocation(),
		CurrentTransform.GetLocation());
	const double RotationDeltaDegrees = FMath::RadiansToDegrees(
		PreviousTransform.GetRotation().AngularDistance(
			CurrentTransform.GetRotation()));
	return TranslationDeltaCM > TranslationThresholdCM
		|| RotationDeltaDegrees > LandingPreviewCameraCutRotationDegrees;
}

void AABTSM101LandingPreviewCamera::CaptureWithPersistentHistory(
	const FTransform& CaptureTransform)
{
	if (SceneCapture == nullptr
		|| RenderTarget == nullptr
		|| WarmupRenderTarget == nullptr)
	{
		return;
	}

	const bool bRequiresCut = !bHasLastCaptureTransform
		|| DoesPreviewPoseRequireCameraCut(
			LastCaptureTransform,
			CaptureTransform,
			Settings.LandingViewCameraDistanceCM);
	if (bRequiresCut)
	{
		RemainingWarmupCaptures = LandingPreviewHistoryWarmupCaptureCount;
	}
	LastCaptureTransform = CaptureTransform;
	bHasLastCaptureTransform = true;

	if (RemainingWarmupCaptures > 0)
	{
		SceneCapture->TextureTarget = WarmupRenderTarget;
		SceneCapture->bCameraCutThisFrame = bRequiresCut;
		SceneCapture->CaptureScene();
		--RemainingWarmupCaptures;
		return;
	}

	// Publish only a frame rendered after two distinct history-building captures.
	// On a same-subject jump the old public target remains visible until here.
	SceneCapture->TextureTarget = RenderTarget;
	SceneCapture->bCameraCutThisFrame = false;
	SceneCapture->CaptureScene();
	bHasPublishedCapture = true;
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

void AABTSM101LandingPreviewCamera::RebuildTrajectoryPointsAround(
	const FABTSM6TrajectoryPreview& Preview,
	const int32 CenterSegmentStartIndex)
{
	if (TrajectoryPointInstances == nullptr
		|| TrajectoryPointInstances->GetStaticMesh() == nullptr)
	{
		return;
	}
	TrajectoryPointInstances->ClearInstances();
	if (Preview.WorldPoints.IsEmpty()) return;
	const int32 Stride =
		FMath::Clamp(
			Settings.LandingViewTrajectoryStride,
			1,
			16);
	const int32 MaximumPointCount =
		FMath::Clamp(
			Settings.LandingViewTrajectoryPointCount,
			8,
			128);
	const int32 CenterIndex =
		FMath::Clamp(
			CenterSegmentStartIndex,
			0,
			Preview.WorldPoints.Num() - 1);
	const int32 HalfWindowSamples =
		FMath::Max(
			1,
			MaximumPointCount / 2) * Stride;
	const int32 FirstIndex =
		FMath::Max(
			0,
			CenterIndex - HalfWindowSamples);
	const int32 LastIndex =
		FMath::Min(
			Preview.WorldPoints.Num() - 1,
			FirstIndex
				+ MaximumPointCount * Stride);
	const float Scale =
		FMath::Clamp(
			Settings.LandingViewTrajectoryPointSizeCM,
			1.0f,
			100.0f)
		/ BasicShapeSphereDiameterCM;
	TArray<FTransform> Instances;
	Instances.Reserve(MaximumPointCount + 2);
	for (int32 Index = FirstIndex;
		Index <= LastIndex;
		Index += Stride)
	{
		Instances.Emplace(
			FQuat::Identity,
			Preview.WorldPoints[Index],
			FVector(Scale));
	}
	if (Instances.IsEmpty()
		|| !Instances.Last().GetLocation().Equals(
			Preview.WorldPoints[LastIndex],
			1.0f))
	{
		Instances.Emplace(
			FQuat::Identity,
			Preview.WorldPoints[LastIndex],
			FVector(Scale));
	}
	TrajectoryPointInstances->AddInstances(
		Instances,
		false,
		true,
		false);
}
