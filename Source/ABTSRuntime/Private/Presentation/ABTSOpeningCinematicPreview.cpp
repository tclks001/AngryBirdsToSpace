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
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Party/ABTSBirdParty.h"
#include "Presentation/ABTSBirdAnimationPresentationComponent.h"
#include "Presentation/ABTSCinematicPlaybackPolicy.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "Player/ABTSM4PlayerController.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr int32 OpeningBirdCount = static_cast<int32>(EABTSOpeningBird::Count);
	constexpr int32 ProductionPartyBirdCount = 4;

	void BuildCaptureBeamFrustum(
		UProceduralMeshComponent& Mesh,
		const float TopRadius,
		const float BottomRadius)
	{
		constexpr int32 SideCount = 16;
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<FLinearColor> Colors;
		TArray<FProcMeshTangent> Tangents;
		Vertices.Reserve(SideCount * 2);
		Normals.Reserve(SideCount * 2);
		UVs.Reserve(SideCount * 2);
		Colors.Reserve(SideCount * 2);
		Tangents.Reserve(SideCount * 2);
		for (int32 Side = 0; Side < SideCount; ++Side)
		{
			const float Angle = 2.0f * PI * static_cast<float>(Side) / static_cast<float>(SideCount);
			const FVector Radial(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
			Vertices.Add(Radial * TopRadius);
			Vertices.Add(Radial * BottomRadius + FVector(0.0f, 0.0f, 100.0f));
			Normals.Add(Radial);
			Normals.Add(Radial);
			const float U = static_cast<float>(Side) / static_cast<float>(SideCount);
			UVs.Add(FVector2D(U, 0.0f));
			UVs.Add(FVector2D(U, 1.0f));
			Colors.Add(FLinearColor::White);
			Colors.Add(FLinearColor::White);
			Tangents.Add(FProcMeshTangent(-FMath::Sin(Angle), FMath::Cos(Angle), 0.0f));
			Tangents.Add(FProcMeshTangent(-FMath::Sin(Angle), FMath::Cos(Angle), 0.0f));
		}
		for (int32 Side = 0; Side < SideCount; ++Side)
		{
			const int32 Next = (Side + 1) % SideCount;
			const int32 A = Side * 2;
			const int32 B = Next * 2;
			const int32 C = Next * 2 + 1;
			const int32 D = Side * 2 + 1;
			Triangles.Append({A, C, B, A, D, C});
			// Keep the energy shell visible from inside the capture cone too.
			Triangles.Append({B, C, A, C, D, A});
		}
		Mesh.CreateMeshSection_LinearColor(
			0, Vertices, Triangles, Normals, UVs, Colors, Tangents, false);
	}

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

	bool ResolveProductionOpeningFrame(UWorld& World, FTransform& OutTransform)
	{
		AABTSBirdParty* ReadyParty = nullptr;
		for (TActorIterator<AABTSBirdParty> It(&World); It; ++It)
		{
			if (It->IsPartyReady() && It->GetMemberCount() == ProductionPartyBirdCount)
			{
				ReadyParty = *It;
				break;
			}
		}
		if (!ReadyParty) return false;

		FVector Origin = FVector::ZeroVector;
		FVector Up = FVector::ZeroVector;
		int32 ValidBirds = 0;
		for (const AABTSM25BirdCharacter* Bird : ReadyParty->GetPartyMembers())
		{
			if (!Bird || !Bird->GetBirdVisual()) continue;
			Origin += Bird->GetBirdVisual()->GetComponentLocation();
			Up += ReadyParty->GetSurfaceUpAt(Bird->GetActorLocation());
			++ValidBirds;
		}
		if (ValidBirds != ProductionPartyBirdCount) return false;
		Origin /= static_cast<float>(ValidBirds);
		Up = Up.GetSafeNormal();
		if (Up.IsNearlyZero()) Up = FVector::UpVector;
		const AABTSM25BirdCharacter* Leader = ReadyParty->GetControlledBird();
		FVector Forward = Leader
			? FVector::VectorPlaneProject(Leader->GetActorForwardVector(), Up).GetSafeNormal()
			: FVector::ZeroVector;
		if (Forward.IsNearlyZero()) Forward = FVector::CrossProduct(FVector::RightVector, Up).GetSafeNormal();
		if (Forward.IsNearlyZero()) Forward = FVector::ForwardVector;
		OutTransform = FTransform(FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat(), Origin);
		return true;
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

#if !UE_BUILD_SHIPPING
	FAutoConsoleCommandWithWorldAndArgs GABTSOpeningPreviewCommand(
		TEXT("ABTS.OpeningPreview"),
		TEXT("Spawn the isolated 42-second C++ opening preview. Optional argument: time scale (0.05-8.0)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SpawnOpeningPreview));

	FAutoConsoleCommandWithWorldAndArgs GABTSOpeningPreviewStopCommand(
		TEXT("ABTS.OpeningPreview.Stop"),
		TEXT("Stop the active isolated opening preview and restore the previous view target."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&StopOpeningPreview));
#endif
}

EABTSOpeningStartResult AABTSOpeningCinematicPreview::TryStartProductionOpening(UWorld* World)
{
	if (!World || !World->IsGameWorld()) return EABTSOpeningStartResult::Rejected;
	if (FABTSCinematicPlaybackPolicy::ShouldSkipCinematics())
	{
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][CinematicPolicy] Sequence=Opening Skip=1 ShippingHardLock=%d"),
			FABTSCinematicPlaybackPolicy::IsShippingPlaybackHardLocked() ? 1 : 0);
		return EABTSOpeningStartResult::DebugSkipped;
	}
	for (TActorIterator<AABTSOpeningCinematicPreview> It(World); It; ++It)
	{
		return EABTSOpeningStartResult::Rejected;
	}

	FTransform OpeningFrame;
	if (!ResolveProductionOpeningFrame(*World, OpeningFrame))
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][OpeningProduction][Rejected] Reason=ReadyFourBirdPartyMissing"));
		return EABTSOpeningStartResult::Rejected;
	}
	AABTSOpeningCinematicPreview* Opening = World->SpawnActorDeferred<AABTSOpeningCinematicPreview>(
		AABTSOpeningCinematicPreview::StaticClass(),
		OpeningFrame,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Opening) return EABTSOpeningStartResult::Rejected;
	Opening->bProductionBinding = true;
	Opening->FinishSpawning(OpeningFrame);
	return Opening->bPreviewFinished
		? EABTSOpeningStartResult::Rejected
		: EABTSOpeningStartResult::Started;
}

AABTSOpeningCinematicPreview::AABTSOpeningCinematicPreview()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	PrimaryActorTick.bTickEvenWhenPaused = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	CinematicCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("CinematicCamera"));
	CinematicCamera->SetupAttachment(SceneRoot);
	CinematicCamera->SetFieldOfView(50.0f);
	CinematicCamera->PrimaryComponentTick.bTickEvenWhenPaused = true;

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
		BirdVisual->PrimaryComponentTick.bTickEvenWhenPaused = true;
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

	CaptureBeam = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("CaptureBeamCore"));
	CaptureBeam->SetupAttachment(SceneRoot);
	CaptureBeam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CaptureBeam->SetGenerateOverlapEvents(false);
	CaptureBeam->SetCanEverAffectNavigation(false);
	CaptureBeam->bUseAsyncCooking = false;

	CaptureBeamHalo = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("CaptureBeamHalo"));
	CaptureBeamHalo->SetupAttachment(SceneRoot);
	CaptureBeamHalo->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CaptureBeamHalo->SetGenerateOverlapEvents(false);
	CaptureBeamHalo->SetCanEverAffectNavigation(false);
	CaptureBeamHalo->bUseAsyncCooking = false;
	CaptureBeamCoreMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(
		TEXT("/Game/NiagaraExamples/Materials/MasterMaterials/M_BrightCore.M_BrightCore")));
	CaptureBeamHaloMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(
		TEXT("/Game/NiagaraExamples/Materials/MI_SimpleDebris_Translucent.MI_SimpleDebris_Translucent")));

	CaptureLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("CaptureLight"));
	CaptureLight->SetupAttachment(SceneRoot);
	CaptureLight->SetIntensity(7500.0f);
	CaptureLight->SetAttenuationRadius(700.0f);
	CaptureLight->SetLightColor(FColor(125, 220, 255));
	CaptureLight->SetCastShadows(false);

	UFOVisual->SetVisibility(false);
	CaptureBeam->SetVisibility(false);
	CaptureBeamHalo->SetVisibility(false);
	CaptureLight->SetVisibility(false);
}

void AABTSOpeningCinematicPreview::BeginPlay()
{
	Super::BeginPlay();
	InitializeCaptureBeamVisual();
	InitializeAnimationDrivers();
	PreviewController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (PreviewController)
	{
		SavedViewTarget = PreviewController->GetViewTarget();
		PreviewController->SetViewTarget(this);
	}
	if (bProductionBinding && !InitializeProductionBinding())
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][OpeningProduction][Rejected] Reason=BindingInitializationFailed"));
		FinishPreview(false);
		return;
	}
	UpdateBirds(0.0f);
	UpdateUFOAndCaptureBeam();
	UpdateCamera();
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][OpeningPreview][Started] Duration=%.1f TimeScale=%.2f ProductionBinding=%d SpawnContinuous=%d StylizedBeam=1"),
		FABTSOpeningCinematicEvaluator::DurationSeconds, PreviewTimeScale,
		bProductionBinding ? 1 : 0, bProductionBinding ? 1 : 0);
}

void AABTSOpeningCinematicPreview::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bPreviewFinished) return;
	float SequenceDeltaSeconds = FMath::Max(0.0f, DeltaSeconds);
	if (bProductionBinding)
	{
		const double Now = FPlatformTime::Seconds();
		SequenceDeltaSeconds = LastProductionWallSeconds > 0.0
			? static_cast<float>(FMath::Clamp(Now - LastProductionWallSeconds, 0.0, 0.1))
			: SequenceDeltaSeconds;
		LastProductionWallSeconds = Now;
	}
	ElapsedSeconds += SequenceDeltaSeconds * PreviewTimeScale;
	UpdateBirds(SequenceDeltaSeconds * PreviewTimeScale);
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
	ReleaseProductionBinding();
	Super::EndPlay(EndPlayReason);
}

void AABTSOpeningCinematicPreview::CalcCamera(
	const float DeltaTime,
	FMinimalViewInfo& OutResult)
{
	if (CinematicCamera)
	{
		CinematicCamera->GetCameraView(DeltaTime, OutResult);
		return;
	}
	Super::CalcCamera(DeltaTime, OutResult);
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
	PreviousAnimationCues.Init(
		EABTSOpeningAnimationCue::Idle,
		BirdVisuals.Num());
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

void AABTSOpeningCinematicPreview::InitializeCaptureBeamVisual()
{
	if (!CaptureBeam || !CaptureBeamHalo) return;
	BuildCaptureBeamFrustum(*CaptureBeam, 15.0f, 42.0f);
	BuildCaptureBeamFrustum(*CaptureBeamHalo, 34.0f, 82.0f);

	UMaterialInterface* Fallback = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	UMaterialInterface* CoreParent = CaptureBeamCoreMaterial.LoadSynchronous();
	UMaterialInterface* HaloParent = CaptureBeamHaloMaterial.LoadSynchronous();
	CaptureBeamCoreMID = UMaterialInstanceDynamic::Create(CoreParent ? CoreParent : Fallback, this);
	CaptureBeamHaloMID = UMaterialInstanceDynamic::Create(HaloParent ? HaloParent : Fallback, this);
	const FLinearColor CoreColor(0.35f, 4.5f, 8.0f, 1.0f);
	const FLinearColor HaloColor(0.08f, 1.6f, 3.8f, 0.24f);
	for (const FName Parameter : {FName(TEXT("Color")), FName(TEXT("BaseColor")),
		FName(TEXT("EmissiveColor")), FName(TEXT("Tint"))})
	{
		if (CaptureBeamCoreMID) CaptureBeamCoreMID->SetVectorParameterValue(Parameter, CoreColor);
		if (CaptureBeamHaloMID) CaptureBeamHaloMID->SetVectorParameterValue(Parameter, HaloColor);
	}
	if (CaptureBeamCoreMID)
	{
		CaptureBeamCoreMID->SetScalarParameterValue(TEXT("EmissiveStrength"), 8.0f);
		CaptureBeam->SetMaterial(0, CaptureBeamCoreMID);
	}
	if (CaptureBeamHaloMID)
	{
		CaptureBeamHaloMID->SetScalarParameterValue(TEXT("Opacity"), 0.24f);
		CaptureBeamHaloMID->SetScalarParameterValue(TEXT("EmissiveStrength"), 3.5f);
		CaptureBeamHalo->SetMaterial(0, CaptureBeamHaloMID);
	}
}

bool AABTSOpeningCinematicPreview::InitializeProductionBinding()
{
	if (!GetWorld() || !PreviewController) return false;
	AABTSM4PlayerController* ABTSController =
		Cast<AABTSM4PlayerController>(PreviewController);
	if (!ABTSController) return false;
	AABTSBirdParty* ReadyParty = nullptr;
	for (TActorIterator<AABTSBirdParty> It(GetWorld()); It; ++It)
	{
		if (It->IsPartyReady() && It->GetMemberCount() == ProductionPartyBirdCount)
		{
			ReadyParty = *It;
			break;
		}
	}
	if (!ReadyParty) return false;

	ProductionPartyBirds.Reset();
	ProductionBirdWasHidden.Reset();
	ProductionHandoffLocalLocations.Reset();
	ProductionHandoffLocalRotations.Reset();
	for (AABTSM25BirdCharacter* Bird : ReadyParty->GetPartyMembers())
	{
		if (!Bird || !Bird->GetBirdVisual()) return false;
		ProductionPartyBirds.Add(Bird);
		ProductionBirdWasHidden.Add(Bird->IsHidden());
		ProductionHandoffLocalLocations.Add(
			GetActorTransform().InverseTransformPosition(Bird->GetBirdVisual()->GetComponentLocation()));
		ProductionHandoffLocalRotations.Add(
			GetActorQuat().Inverse() * Bird->GetBirdVisual()->GetComponentQuat());
	}
	if (ProductionPartyBirds.Num() != ProductionPartyBirdCount) return false;
	for (const TWeakObjectPtr<AABTSM25BirdCharacter>& WeakBird : ProductionPartyBirds)
	{
		if (AABTSM25BirdCharacter* Bird = WeakBird.Get()) Bird->SetActorHiddenInGame(true);
	}
	if (PreviewStage) PreviewStage->SetVisibility(false, true);

	bProductionWorldWasPaused = UGameplayStatics::IsGamePaused(GetWorld());
	bProductionControllerFullTickWhenPaused =
		ABTSController->IsCinematicFullTickWhenPaused();
	ABTSController->SetCinematicFullTickWhenPaused(true);
	bProductionInputWasBlocked = ABTSController->IsCinematicInputBlocked();
	ABTSController->SetCinematicInputBlocked(true);
	if (PreviewController->MyHUD)
	{
		bProductionHUDWasVisible = PreviewController->MyHUD->bShowHUD;
		PreviewController->MyHUD->bShowHUD = false;
	}
	PreviewController->SetPause(true);
	LastProductionWallSeconds = FPlatformTime::Seconds();
	bProductionBindingReleased = false;
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][OpeningProduction][Bound] RealParty=4 SpawnFrame=ReadyParty WorldPaused=1 FullControllerTickWhenPaused=1 CameraPausedTick=1 BirdPausedTick=1 HUDHidden=1 InputBlocked=1"));
	return true;
}

void AABTSOpeningCinematicPreview::ReleaseProductionBinding()
{
	if (!bProductionBinding || bProductionBindingReleased) return;
	bProductionBindingReleased = true;
	for (int32 Index = 0; Index < ProductionPartyBirds.Num(); ++Index)
	{
		if (AABTSM25BirdCharacter* Bird = ProductionPartyBirds[Index].Get())
		{
			const bool bWasHidden = ProductionBirdWasHidden.IsValidIndex(Index)
				&& ProductionBirdWasHidden[Index];
			Bird->ResetRadialMovementState();
			Bird->SetActorHiddenInGame(bWasHidden);
		}
	}
	if (PreviewController)
	{
		if (AABTSM4PlayerController* ABTSController = Cast<AABTSM4PlayerController>(PreviewController))
		{
			ABTSController->SetCinematicFullTickWhenPaused(
				bProductionControllerFullTickWhenPaused);
			ABTSController->SetCinematicInputBlocked(bProductionInputWasBlocked);
		}
		if (PreviewController->MyHUD) PreviewController->MyHUD->bShowHUD = bProductionHUDWasVisible;
		PreviewController->SetPause(bProductionWorldWasPaused);
	}
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][OpeningProduction][Released] RealPartyVisible=1 InputRestored=1 HUDRestored=1 PauseRestored=%d"),
		bProductionWorldWasPaused ? 1 : 0);
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
		FVector LocalPosition = Pose.LocalPosition;
		FQuat LocalRotation = ResolveBirdVisualRotation(Pose.LocalFacing);
		if (bProductionBinding
			&& Index < ProductionPartyBirdCount
			&& ProductionHandoffLocalLocations.IsValidIndex(Index)
			&& ProductionHandoffLocalRotations.IsValidIndex(Index))
		{
			// Start on the exact real visuals, ease into the authored play circle,
			// then converge back to the same real visuals for a zero-delta swap.
			const float IntroAlpha = FMath::SmoothStep(0.0f, 2.0f, ElapsedSeconds);
			LocalPosition = FMath::Lerp(
				ProductionHandoffLocalLocations[Index], LocalPosition, IntroAlpha);
			LocalRotation = FQuat::Slerp(
				ProductionHandoffLocalRotations[Index], LocalRotation, IntroAlpha).GetNormalized();
			const float HandoffAlpha = FMath::SmoothStep(34.0f, 42.0f, ElapsedSeconds);
			LocalPosition = FMath::Lerp(LocalPosition, ProductionHandoffLocalLocations[Index], HandoffAlpha);
			LocalRotation = FQuat::Slerp(LocalRotation, ProductionHandoffLocalRotations[Index], HandoffAlpha).GetNormalized();
		}
		Visual->SetRelativeLocation(LocalPosition);
		Visual->SetRelativeRotation(LocalRotation);
		if (AnimationDrivers.IsValidIndex(Index) && AnimationDrivers[Index])
		{
			const EABTSOpeningAnimationCue PreviousCue =
				PreviousAnimationCues.IsValidIndex(Index)
					? PreviousAnimationCues[Index]
					: EABTSOpeningAnimationCue::Idle;
			if (Pose.AnimationCue == EABTSOpeningAnimationCue::Celebrate
				&& PreviousCue != EABTSOpeningAnimationCue::Celebrate)
			{
				AnimationDrivers[Index]->RequestAction(
					EABTSBirdPresentationAction::Celebrate);
			}
			FABTSBirdAnimationSnapshot Snapshot;
			Snapshot.bGrounded = Pose.AnimationCue != EABTSOpeningAnimationCue::Fly;
			Snapshot.bForceFlight = Pose.AnimationCue == EABTSOpeningAnimationCue::Fly;
			Snapshot.TangentialSpeedCMPerSecond =
				Pose.AnimationCue == EABTSOpeningAnimationCue::Move
					? 315.0f
					: 0.0f;
			AnimationDrivers[Index]->UpdatePresentation(Snapshot, DeltaSeconds);
			if (PreviousAnimationCues.IsValidIndex(Index))
			{
				PreviousAnimationCues[Index] = Pose.AnimationCue;
			}
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
	CaptureBeamHalo->SetVisibility(bShowBeam, true);
	CaptureLight->SetVisibility(bShowBeam, true);
	if (bShowBeam)
	{
		const FQuat BeamRotation = FQuat::FindBetweenNormals(FVector::UpVector, BeamVector.GetSafeNormal());
		const float Pulse = 1.0f + 0.09f * FMath::Sin(ElapsedSeconds * 9.0f);
		CaptureBeam->SetRelativeLocation(BeamStart);
		CaptureBeam->SetRelativeRotation(BeamRotation);
		CaptureBeam->SetRelativeScale3D(FVector(Pulse, Pulse, BeamLength / 100.0f));
		CaptureBeamHalo->SetRelativeLocation(BeamStart);
		CaptureBeamHalo->SetRelativeRotation(BeamRotation);
		CaptureBeamHalo->SetRelativeScale3D(FVector(1.0f / Pulse, 1.0f / Pulse, BeamLength / 100.0f));
		CaptureLight->SetRelativeLocation(BeamEnd);
		CaptureLight->SetIntensity(6500.0f + 1800.0f * FMath::Abs(FMath::Sin(ElapsedSeconds * 7.0f)));
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
	float MaximumHandoffDeltaCM = 0.0f;
	if (bProductionBinding)
	{
		for (int32 Index = 0; Index < ProductionPartyBirds.Num() && Index < BirdVisuals.Num(); ++Index)
		{
			const AABTSM25BirdCharacter* Bird = ProductionPartyBirds[Index].Get();
			if (Bird && Bird->GetBirdVisual() && BirdVisuals[Index])
			{
				MaximumHandoffDeltaCM = FMath::Max(
					MaximumHandoffDeltaCM,
					FVector::Distance(BirdVisuals[Index]->GetComponentLocation(), Bird->GetBirdVisual()->GetComponentLocation()));
			}
		}
		for (USkeletalMeshComponent* Visual : BirdVisuals)
		{
			if (Visual) Visual->SetVisibility(false, true);
		}
		if (UFOVisual) UFOVisual->SetVisibility(false, true);
		if (CaptureBeam) CaptureBeam->SetVisibility(false, true);
		if (CaptureBeamHalo) CaptureBeamHalo->SetVisibility(false, true);
		if (CaptureLight) CaptureLight->SetVisibility(false, true);
		ReleaseProductionBinding();
	}
	if (PreviewController && SavedViewTarget && PreviewController->GetViewTarget() == this)
	{
		if (bBlendBack) PreviewController->SetViewTargetWithBlend(SavedViewTarget, 1.0f);
		else PreviewController->SetViewTarget(SavedViewTarget);
	}
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][OpeningPreview][Completed] Elapsed=%.2f BlendBack=%d ProductionBinding=%d MaxHandoffDeltaCM=%.3f"),
		ElapsedSeconds, bBlendBack ? 1 : 0, bProductionBinding ? 1 : 0,
		MaximumHandoffDeltaCM);
	SetLifeSpan(bBlendBack ? 1.1f : 0.01f);
}

FQuat AABTSOpeningCinematicPreview::ResolveBirdVisualRotation(const FVector& LocalFacing) const
{
	const FVector Facing = LocalFacing.IsNearlyZero() ? FVector::ForwardVector : LocalFacing.GetSafeNormal();
	return FRotationMatrix::MakeFromXZ(Facing, FVector::UpVector).ToQuat()
		* FRotator(0.0f, -90.0f, 0.0f).Quaternion();
}
