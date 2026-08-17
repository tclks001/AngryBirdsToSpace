// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM11FinalePostHitCinematicPreview.h"

#include "ABTSRuntime.h"
#include "Audio/ABTSAudioWorldSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PointLightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMisc.h"
#include "ImageUtils.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Party/ABTSBirdTypes.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "Presentation/ABTSBirdAnimationPresentationComponent.h"
#include "Rendering/ABTSStylizedRenderingControl.h"
#include "Rendering/ABTSStylizedRenderingTypes.h"
#include "Rendering/ABTSStylizedSceneCaptureRegistry.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Class.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace ABTSM11PostHitPreviewPrivate
{
	constexpr int32 BirdCount =
		static_cast<int32>(EABTSM11FinalePostHitBird::Count);
	constexpr float RealDebrisActivationDelaySeconds = 0.09f;
	constexpr float RealDebrisImpulseDelaySeconds = 0.07f;
	const FName GetInitialLocalRestTransformsFunctionName(
		TEXT("GetInitialLocalRestTransforms"));
	const FName ForceBrokenForCustomRendererFunctionName(
		TEXT("ForceBrokenForCustomRenderer"));
	const FName CrumbleActiveClustersFunctionName(TEXT("CrumbleActiveClusters"));
	const FName RemoveAllAnchorsFunctionName(TEXT("RemoveAllAnchors"));

	bool InvokeNoParameterFunction(UObject& Target, const FName FunctionName)
	{
		UFunction* Function = Target.FindFunction(FunctionName);
		if (Function == nullptr || Function->ParmsSize != 0)
		{
			return false;
		}
		Target.ProcessEvent(Function, nullptr);
		return true;
	}

	bool SetBoolProperty(UObject& Target, const FName PropertyName, const bool Value)
	{
		FBoolProperty* Property = FindFProperty<FBoolProperty>(
			Target.GetClass(),
			PropertyName);
		if (Property == nullptr)
		{
			return false;
		}
		Property->SetPropertyValue_InContainer(&Target, Value);
		return Property->GetPropertyValue_InContainer(&Target) == Value;
	}

	bool InvokeBoolFunction(
		UObject& Target,
		const FName FunctionName,
		const FName ParameterName,
		const bool Value)
	{
		UFunction* Function = Target.FindFunction(FunctionName);
		FBoolProperty* Parameter = Function != nullptr
			? FindFProperty<FBoolProperty>(Function, ParameterName)
			: nullptr;
		if (Function == nullptr || Parameter == nullptr)
		{
			return false;
		}
		FStructOnScope Parameters(Function);
		Parameter->SetPropertyValue_InContainer(
			Parameters.GetStructMemory(),
			Value);
		Target.ProcessEvent(Function, Parameters.GetStructMemory());
		return true;
	}

	bool ReadTransformArrayReturnValue(
		UObject& Target,
		const FName FunctionName,
		TArray<FTransform>& OutTransforms)
	{
		UFunction* Function = Target.FindFunction(FunctionName);
		FArrayProperty* ReturnProperty = Function != nullptr
			? FindFProperty<FArrayProperty>(Function, TEXT("ReturnValue"))
			: nullptr;
		FStructProperty* TransformProperty = ReturnProperty != nullptr
			? CastField<FStructProperty>(ReturnProperty->Inner)
			: nullptr;
		if (Function == nullptr || ReturnProperty == nullptr
			|| TransformProperty == nullptr
			|| TransformProperty->Struct != TBaseStructure<FTransform>::Get())
		{
			return false;
		}
		FStructOnScope Parameters(Function);
		Target.ProcessEvent(Function, Parameters.GetStructMemory());
		FScriptArrayHelper ArrayHelper(
			ReturnProperty,
			ReturnProperty->ContainerPtrToValuePtr<void>(
				Parameters.GetStructMemory()));
		OutTransforms.SetNum(ArrayHelper.Num());
		for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
		{
			TransformProperty->CopyCompleteValue(
				&OutTransforms[Index],
				ArrayHelper.GetRawPtr(Index));
		}
		return !OutTransforms.IsEmpty();
	}

	FTransform ResolvePreviewSpawnTransform(UWorld& World)
	{
		APlayerController* Controller = World.GetFirstPlayerController();
		const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
		FVector Up = Pawn ? Pawn->GetActorUpVector() : FVector::UpVector;
		FVector Forward = Pawn
			? Pawn->GetActorForwardVector()
			: FVector::ForwardVector;
		FVector Origin = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
		if (Controller != nullptr)
		{
			FVector ViewLocation;
			FRotator ViewRotation;
			Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
			if (Pawn == nullptr)
			{
				Origin = ViewLocation;
				Forward = ViewRotation.Vector();
			}
		}
		Up = Up.GetSafeNormal();
		Forward = FVector::VectorPlaneProject(Forward, Up).GetSafeNormal();
		if (Forward.IsNearlyZero())
		{
			Forward = FVector::ForwardVector;
		}
		Origin += Up * 5000.0f + Forward * 2500.0f;
		return FTransform(
			FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat(),
			Origin);
	}

	bool HasComponentSemantic(
		const UActorComponent& Component,
		const TCHAR* Tag,
		const TCHAR* NamePrefix)
	{
		return Component.ComponentHasTag(FName(Tag))
			|| Component.GetName().StartsWith(NamePrefix);
	}

	bool WriteBytes(IFileHandle& File, const void* Data, const int64 Size)
	{
		return Size >= 0
			&& File.Write(static_cast<const uint8*>(Data), Size);
	}

	bool WriteFourCC(IFileHandle& File, const ANSICHAR (&Value)[5])
	{
		return WriteBytes(File, Value, 4);
	}

	bool WriteUInt16(IFileHandle& File, const uint16 Value)
	{
		const uint8 Bytes[2] = {
			static_cast<uint8>(Value),
			static_cast<uint8>(Value >> 8)};
		return WriteBytes(File, Bytes, UE_ARRAY_COUNT(Bytes));
	}

	bool WriteUInt32(IFileHandle& File, const uint32 Value)
	{
		const uint8 Bytes[4] = {
			static_cast<uint8>(Value),
			static_cast<uint8>(Value >> 8),
			static_cast<uint8>(Value >> 16),
			static_cast<uint8>(Value >> 24)};
		return WriteBytes(File, Bytes, UE_ARRAY_COUNT(Bytes));
	}

	int64 BeginChunk(IFileHandle& File, const ANSICHAR (&FourCC)[5])
	{
		if (!WriteFourCC(File, FourCC))
		{
			return INDEX_NONE;
		}
		const int64 SizeOffset = File.Tell();
		return WriteUInt32(File, 0) ? SizeOffset : INDEX_NONE;
	}

	bool EndChunk(IFileHandle& File, const int64 SizeOffset)
	{
		if (SizeOffset < 0)
		{
			return false;
		}
		const int64 EndBeforePadding = File.Tell();
		const int64 PayloadSize = EndBeforePadding - SizeOffset - 4;
		if (PayloadSize < 0 || PayloadSize > MAX_uint32)
		{
			return false;
		}
		if ((PayloadSize & 1) != 0)
		{
			const uint8 Padding = 0;
			if (!WriteBytes(File, &Padding, 1))
			{
				return false;
			}
		}
		const int64 EndAfterPadding = File.Tell();
		return File.Seek(SizeOffset)
			&& WriteUInt32(File, static_cast<uint32>(PayloadSize))
			&& File.Seek(EndAfterPadding);
	}

	AABTSM11FinalePostHitCinematicPreview* SpawnPreviewDeferred(
		UWorld& World,
		const float TimeScale)
	{
		TActorIterator<AABTSM11FinalePostHitCinematicPreview> Existing(&World);
		if (Existing)
		{
			UE_LOG(
				LogABTSRuntime,
				Warning,
				TEXT("[ABTS][M11-D][PostHitPreview] A preview is already active."));
			return nullptr;
		}
		const FTransform SpawnTransform = ResolvePreviewSpawnTransform(World);
		AABTSM11FinalePostHitCinematicPreview* Preview =
			World.SpawnActorDeferred<AABTSM11FinalePostHitCinematicPreview>(
				AABTSM11FinalePostHitCinematicPreview::StaticClass(),
				SpawnTransform,
				nullptr,
				nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Preview != nullptr)
		{
			Preview->SetPreviewTimeScale(TimeScale);
		}
		return Preview;
	}

	void SpawnPostHitPreview(const TArray<FString>& Args, UWorld* World)
	{
		if (World == nullptr || !World->IsGameWorld())
		{
			UE_LOG(
				LogABTSRuntime,
				Warning,
				TEXT("[ABTS][M11-D][PostHitPreview] Command requires a running PIE or game world."));
			return;
		}
		const float TimeScale = Args.Num() > 0
			? FMath::Clamp(FCString::Atof(*Args[0]), 0.05f, 8.0f)
			: 1.0f;
		AABTSM11FinalePostHitCinematicPreview* Preview =
			SpawnPreviewDeferred(*World, TimeScale);
		if (Preview != nullptr)
		{
			Preview->FinishSpawning(Preview->GetActorTransform());
		}
	}

	void SpawnPostHitCapture(const TArray<FString>& Args, UWorld* World)
	{
		if (World == nullptr || !World->IsGameWorld() || Args.IsEmpty())
		{
			UE_LOG(
				LogABTSRuntime,
				Error,
				TEXT("[ABTS][M11-D][PostHitCapture] Usage: ABTS.M11Finale.PostHitCapture <AbsoluteOutputDirectory> [FrameRate] [Width] [Height] [JpegQuality]."));
			return;
		}
		const FString OutputDirectory = Args[0];
		const int32 FrameRate = Args.Num() > 1
			? FCString::Atoi(*Args[1])
			: 30;
		const int32 Width = Args.Num() > 2
			? FCString::Atoi(*Args[2])
			: 1280;
		const int32 Height = Args.Num() > 3
			? FCString::Atoi(*Args[3])
			: 720;
		const int32 Quality = Args.Num() > 4
			? FCString::Atoi(*Args[4])
			: 90;
		AABTSM11FinalePostHitCinematicPreview* Preview =
			SpawnPreviewDeferred(*World, 1.0f);
		if (Preview == nullptr)
		{
			return;
		}
		if (!Preview->ConfigureOffscreenCapture(
				OutputDirectory,
				TEXT("M11PostHitFinale"),
				FrameRate,
				Width,
				Height,
				Quality))
		{
			UE_LOG(
				LogABTSRuntime,
				Error,
				TEXT("[ABTS][M11-D][PostHitCapture] Configuration rejected: %s"),
				*Preview->GetCaptureFailureReason());
			Preview->Destroy();
			FPlatformMisc::RequestExitWithStatus(false, 2);
			return;
		}
		Preview->FinishSpawning(Preview->GetActorTransform());
	}

	void StopPostHitPreview(const TArray<FString>&, UWorld* World)
	{
		if (World == nullptr)
		{
			return;
		}
		TActorIterator<AABTSM11FinalePostHitCinematicPreview> Existing(World);
		if (Existing)
		{
			Existing->StopPreview();
		}
	}

	FAutoConsoleCommandWithWorldAndArgs GABTSM11PostHitPreviewCommand(
		TEXT("ABTS.M11Finale.PostHitPreview"),
		TEXT("Spawn the isolated 18-second M11 post-hit cinematic. Optional argument: time scale (0.05-8.0)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
			&SpawnPostHitPreview));

	FAutoConsoleCommandWithWorldAndArgs GABTSM11PostHitCaptureCommand(
		TEXT("ABTS.M11Finale.PostHitCapture"),
		TEXT("Capture the isolated M11 post-hit cinematic to JPEG frames plus MJPEG AVI and auto-exit."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
			&SpawnPostHitCapture));

	FAutoConsoleCommandWithWorldAndArgs GABTSM11PostHitPreviewStopCommand(
		TEXT("ABTS.M11Finale.PostHitPreview.Stop"),
		TEXT("Stop the active M11 post-hit preview and restore the previous view target."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
			&StopPostHitPreview));
}

AABTSM11FinalePostHitCinematicPreview::
	AABTSM11FinalePostHitCinematicPreview()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	SetActorEnableCollision(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	CinematicCamera = CreateDefaultSubobject<UCameraComponent>(
		TEXT("CinematicCamera"));
	CinematicCamera->SetupAttachment(SceneRoot);
	CinematicCamera->SetFieldOfView(62.0f);
	FPostProcessSettings& CinematicPostProcess =
		CinematicCamera->PostProcessSettings;
	CinematicPostProcess.bOverride_AutoExposureMethod = true;
	CinematicPostProcess.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	CinematicPostProcess.bOverride_AutoExposureBias = true;
	CinematicPostProcess.AutoExposureBias =
		FABTSM11FinalePostHitCinematicEvaluator::CinematicExposureBias;
	CinematicPostProcess.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	CinematicPostProcess.AutoExposureApplyPhysicalCameraExposure = false;
	CinematicCamera->SetPostProcessBlendWeight(1.0f);

	RecordingCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(
		TEXT("RecordingCapture"));
	RecordingCapture->SetupAttachment(SceneRoot);
	RecordingCapture->bCaptureEveryFrame = false;
	RecordingCapture->bCaptureOnMovement = false;
	RecordingCapture->bAlwaysPersistRenderingState = true;
	RecordingCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> BirdMeshAsset(
		TEXT("/Game/CuteBird/Meshes/SM_Cute_Bird.SM_Cute_Bird"));
	for (int32 Index = 0;
		Index < ABTSM11PostHitPreviewPrivate::BirdCount;
		++Index)
	{
		USkeletalMeshComponent* BirdVisual =
			CreateDefaultSubobject<USkeletalMeshComponent>(
				*FString::Printf(TEXT("BirdVisual%d"), Index));
		BirdVisual->SetupAttachment(SceneRoot);
		BirdVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BirdVisual->SetCollisionResponseToAllChannels(ECR_Ignore);
		BirdVisual->SetGenerateOverlapEvents(false);
		BirdVisual->SetSimulatePhysics(false);
		BirdVisual->SetCanEverAffectNavigation(false);
		BirdVisual->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		BirdVisual->SetRelativeScale3D(FVector(4.0f));
		if (BirdMeshAsset.Succeeded())
		{
			BirdVisual->SetSkeletalMesh(BirdMeshAsset.Object);
		}
		BirdVisuals.Add(BirdVisual);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> RedColor(
		TEXT("/Game/CuteBird/Materials/CuteBirdColor_Materials/M_CuteBird_12.M_CuteBird_12"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> RedFace(
		TEXT("/Game/CuteBird/Materials/Face_Materials/M_Dino_face_23.M_Dino_face_23"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BlueColor(
		TEXT("/Game/CuteBird/Materials/CuteBirdColor_Materials/M_CuteBird_3.M_CuteBird_3"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BlueFace(
		TEXT("/Game/CuteBird/Materials/Face_Materials/M_Dino_face_3.M_Dino_face_3"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> YellowColor(
		TEXT("/Game/CuteBird/Materials/CuteBirdColor_Materials/M_CuteBird_10.M_CuteBird_10"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> YellowFace(
		TEXT("/Game/CuteBird/Materials/Face_Materials/M_Dino_face_6.M_Dino_face_6"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BlackColor(
		TEXT("/Game/CuteBird/Materials/CuteBirdColor_Materials/M_CuteBird_16.M_CuteBird_16"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BlackFace(
		TEXT("/Game/CuteBird/Materials/Face_Materials/M_Dino_face_17.M_Dino_face_17"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> WhiteColor(
		TEXT("/Game/CuteBird/Materials/CuteBirdColor_Materials/M_CuteBird_0.M_CuteBird_0"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> WhiteFace(
		TEXT("/Game/CuteBird/Materials/Face_Materials/M_Dino_face_1.M_Dino_face_1"));
	UMaterialInterface* Colors[] = {
		RedColor.Object,
		BlueColor.Object,
		YellowColor.Object,
		BlackColor.Object,
		WhiteColor.Object};
	UMaterialInterface* Faces[] = {
		RedFace.Object,
		BlueFace.Object,
		YellowFace.Object,
		BlackFace.Object,
		WhiteFace.Object};
	for (int32 Index = 0; Index < BirdVisuals.Num(); ++Index)
	{
		if (Colors[Index] != nullptr)
		{
			BirdVisuals[Index]->SetMaterial(0, Colors[Index]);
		}
		if (Faces[Index] != nullptr)
		{
			BirdVisuals[Index]->SetMaterial(1, Faces[Index]);
		}
	}

	FallbackUFOVisual = CreateDefaultSubobject<UStaticMeshComponent>(
		TEXT("FallbackUFOVisual"));
	FallbackUFOVisual->SetupAttachment(SceneRoot);
	FallbackUFOVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FallbackUFOVisual->SetCollisionResponseToAllChannels(ECR_Ignore);
	FallbackUFOVisual->SetGenerateOverlapEvents(false);
	FallbackUFOVisual->SetCanEverAffectNavigation(false);
	FallbackUFOVisual->SetRelativeScale3D(FVector(3.6f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> UFOAsset(
		TEXT("/Game/StaticMesh/UFO/SM_UFO_Intact.SM_UFO_Intact"));
	if (UFOAsset.Succeeded())
	{
		FallbackUFOVisual->SetStaticMesh(UFOAsset.Object);
	}

	ImpactFlash = CreateDefaultSubobject<UPointLightComponent>(
		TEXT("ImpactFlash"));
	ImpactFlash->SetupAttachment(SceneRoot);
	ImpactFlash->SetLightColor(FColor(255, 225, 145));
	ImpactFlash->SetAttenuationRadius(900.0f);
	ImpactFlash->SetCastShadows(false);
	ImpactFlash->SetIntensity(0.0f);

	CinematicKeyLight = CreateDefaultSubobject<UPointLightComponent>(
		TEXT("CinematicKeyLight"));
	CinematicKeyLight->SetupAttachment(SceneRoot);
	CinematicKeyLight->SetLightColor(FColor(255, 218, 172));
	CinematicKeyLight->SetAttenuationRadius(2400.0f);
	CinematicKeyLight->SetCastShadows(false);
	CinematicKeyLight->SetIntensity(0.0f);

	CinematicFillLight = CreateDefaultSubobject<UPointLightComponent>(
		TEXT("CinematicFillLight"));
	CinematicFillLight->SetupAttachment(SceneRoot);
	CinematicFillLight->SetLightColor(FColor(105, 176, 255));
	CinematicFillLight->SetAttenuationRadius(2200.0f);
	CinematicFillLight->SetCastShadows(false);
	CinematicFillLight->SetIntensity(0.0f);

	CinematicRimLight = CreateDefaultSubobject<UPointLightComponent>(
		TEXT("CinematicRimLight"));
	CinematicRimLight->SetupAttachment(SceneRoot);
	CinematicRimLight->SetLightColor(FColor(172, 210, 255));
	CinematicRimLight->SetAttenuationRadius(2300.0f);
	CinematicRimLight->SetCastShadows(false);
	CinematicRimLight->SetIntensity(0.0f);

}

void AABTSM11FinalePostHitCinematicPreview::BeginPlay()
{
	Super::BeginPlay();
	InitializeAnimationDrivers();
	InitializeUFOPresentation();
	if (!bRealUFODebrisReady)
	{
		/*
		 * Production completion must not disappear solely because the optional
		 * Chaos presentation asset is unavailable in this build.  The intact
		 * fallback mesh keeps the authored camera/bird timeline alive; when the
		 * shared Geometry Collection is present, the existing real-debris path
		 * remains unchanged.
		 */
		UE_LOG(
			LogABTSRuntime,
			Warning,
			TEXT("[ABTS][M11-D][PostHitPreview] RealDebrisUnavailable; continuing with static UFO fallback."));
	}
	if (bProductionBinding
		&& (!ProductionController.IsValid()
			|| !ProductionAuthoritativeUFO.IsValid()
			|| ProductionBirdSources.Num() != 4
			|| ProductionBirdInitialWorldTransforms.Num() != 4))
	{
		FinishPreview(false, false, TEXT("ProductionBindingDependenciesLost"));
		return;
	}
	PreviewController = bProductionBinding
		? ProductionController.Get()
		: GetWorld() != nullptr
			? GetWorld()->GetFirstPlayerController()
			: nullptr;
	if (bProductionBinding && IsValid(UFOPresentationActor)
		&& IsValid(UFOIntactVisual))
	{
		UPrimitiveComponent* SourceUFOVisual =
			ProductionAuthoritativeUFO->FindComponentByClass<UPrimitiveComponent>();
		const float SourceRadius = IsValid(SourceUFOVisual)
			? SourceUFOVisual->Bounds.SphereRadius : 0.0f;
		const float ProxyRadius = UFOIntactVisual->Bounds.SphereRadius;
		if (FMath::IsFinite(SourceRadius) && FMath::IsFinite(ProxyRadius)
			&& SourceRadius > 1.0f && ProxyRadius > 1.0f)
		{
			UFOPresentationActor->SetActorRelativeScale3D(
				UFOPresentationActor->GetActorRelativeScale3D()
					* (SourceRadius / ProxyRadius));
		}
	}
	if (PreviewController != nullptr)
	{
		SavedViewTarget = PreviewController->GetViewTarget();
		if (bProductionBinding)
		{
			if (AHUD* HUD = PreviewController->GetHUD())
			{
				bProductionHUDInitiallyVisible = HUD->bShowHUD;
				HUD->bShowHUD = false;
			}
			for (TWeakObjectPtr<USkeletalMeshComponent>& Source
				: ProductionBirdSources)
			{
				if (Source.IsValid())
				{
					Source->SetVisibility(false, true);
				}
			}
			bProductionUFOInitiallyHidden =
				ProductionAuthoritativeUFO->IsHidden();
			ProductionAuthoritativeUFO->SetActorHiddenInGame(true);
			bProductionSourcesHidden = true;
		}
		PreviewController->SetViewTarget(this);
	}
	if (UABTSAudioWorldSubsystem* Audio = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<UABTSAudioWorldSubsystem>()
		: nullptr)
	{
		Audio->SetMusicState(EABTSMusicState::Finale, 0.4f);
	}
	if (!UpdatePresentation(0.0f))
	{
		FinishPreview(false, false, TEXT("RealGeometryCollectionDebrisActivationFailed"));
		return;
	}
	if (bCaptureEnabled && !StartOffscreenCapture())
	{
		FinishPreview(false, false, CaptureFailureReason);
		return;
	}
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M11-D][PostHitPreview][Started] Duration=%.1f TimeScale=%.2f Capture=%d DebrisSource=GC_UFO_Broken DebrisPlayback=StagedNativeChaos LiveChaos=1 LightingRig=CameraRelativeThreePoint ExposureBias=%.2f MaterialOverride=0 GameplayMutation=%d MapBinding=%d ProductionBinding=%d FirstFrameRealBirdsExact=%d"),
		FABTSM11FinalePostHitCinematicEvaluator::DurationSeconds,
		PreviewTimeScale,
		bCaptureEnabled ? 1 : 0,
		FABTSM11FinalePostHitCinematicEvaluator::CinematicExposureBias,
		bProductionBinding ? 1 : 0,
		bProductionBinding ? 1 : 0,
		bProductionBinding ? 1 : 0,
		bProductionBinding ? 1 : 0);
}

void AABTSM11FinalePostHitCinematicPreview::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bPreviewFinished)
	{
		return;
	}
	if (bCaptureEnabled && RemainingWarmupFrames > 0)
	{
		--RemainingWarmupFrames;
		if (!UpdatePresentation(0.0f))
		{
			FinishPreview(false, false, TEXT("RealGeometryCollectionDebrisActivationFailed"));
		}
		return;
	}
	if (!bPlaybackActionsStarted)
	{
		for (int32 Index = 0; Index < AnimationDrivers.Num(); ++Index)
		{
			if (AnimationDrivers[Index] == nullptr)
			{
				continue;
			}
			AnimationDrivers[Index]->RequestAction(
				Index == static_cast<int32>(
					EABTSM11FinalePostHitBird::White)
					? EABTSBirdPresentationAction::Damage
					: EABTSBirdPresentationAction::Impact);
		}
		bPlaybackActionsStarted = true;
	}

	const float PreviousTime = ElapsedSeconds;
	const float StepSeconds = bCaptureEnabled
		? 1.0f / static_cast<float>(CaptureFrameRate)
		: FMath::Max(0.0f, DeltaSeconds) * PreviewTimeScale;
	const float CurrentTime = FMath::Min(
		PreviousTime + StepSeconds,
		FABTSM11FinalePostHitCinematicEvaluator::DurationSeconds);
	TriggerCrossedAudioCues(PreviousTime, CurrentTime);
	ElapsedSeconds = CurrentTime;
	if (!UpdatePresentation(StepSeconds))
	{
		FinishPreview(false, false, TEXT("RealGeometryCollectionDebrisActivationFailed"));
		return;
	}
	if (bCaptureEnabled && !CaptureCurrentFrame())
	{
		FinishPreview(false, false, CaptureFailureReason);
		return;
	}
	if (ElapsedSeconds
		>= FABTSM11FinalePostHitCinematicEvaluator::DurationSeconds
		- KINDA_SMALL_NUMBER)
	{
		if (!UpdatePresentation(0.0f))
		{
			FinishPreview(false, false, TEXT("RealGeometryCollectionDebrisActivationFailed"));
			return;
		}
		FinishPreview(!bCaptureEnabled, true, TEXT("TimelineComplete"));
	}
}

void AABTSM11FinalePostHitCinematicPreview::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	if (!bPreviewFinished)
	{
		FinishPreview(false, false, TEXT("WorldEndedBeforeTimelineComplete"));
	}
	if (IsValid(UFOPresentationActor))
	{
		UFOPresentationActor->Destroy();
	}
	RestoreCaptureGlobals();
	RestoreGeometryCollectionRenderer();
	Super::EndPlay(EndPlayReason);
}

void AABTSM11FinalePostHitCinematicPreview::SetPreviewTimeScale(
	const float InTimeScale)
{
	PreviewTimeScale = FMath::Clamp(InTimeScale, 0.05f, 8.0f);
}

bool AABTSM11FinalePostHitCinematicPreview::ConfigureOffscreenCapture(
	const FString& InOutputDirectory,
	const FString& InMovieName,
	const int32 InFrameRate,
	const int32 InWidth,
	const int32 InHeight,
	const int32 InJpegQuality)
{
	if (bCaptureStarted || InOutputDirectory.IsEmpty()
		|| FPaths::IsRelative(InOutputDirectory)
		|| InMovieName.IsEmpty() || InMovieName.Contains(TEXT("/"))
		|| InMovieName.Contains(TEXT("\\"))
		|| InFrameRate < 1 || InFrameRate > 60
		|| InWidth < 320 || InWidth > 3840
		|| InHeight < 180 || InHeight > 2160
		|| InJpegQuality < 1 || InJpegQuality > 100)
	{
		CaptureFailureReason = TEXT("InvalidOffscreenCaptureConfiguration");
		return false;
	}
	CaptureOutputDirectory = FPaths::ConvertRelativePathToFull(
		InOutputDirectory);
	CaptureMovieName = InMovieName;
	CaptureFrameRate = InFrameRate;
	CaptureWidth = InWidth;
	CaptureHeight = InHeight;
	CaptureJpegQuality = InJpegQuality;
	bCaptureEnabled = true;
	PreviewTimeScale = 1.0f;
	return true;
}

bool AABTSM11FinalePostHitCinematicPreview::ConfigureProductionBinding(
	APlayerController& InController,
	AActor& InAuthoritativeUFO,
	const TArray<AABTSM25BirdCharacter*>& InBirds)
{
	if (HasActorBegunPlay() || bCaptureEnabled || InBirds.Num() != 4)
	{
		return false;
	}
	TArray<TWeakObjectPtr<USkeletalMeshComponent>> Sources;
	TArray<FTransform> InitialTransforms;
	TArray<uint8> InitialVisibility;
	Sources.SetNum(4);
	InitialTransforms.SetNum(4);
	InitialVisibility.SetNumZeroed(4);
	for (AABTSM25BirdCharacter* Bird : InBirds)
	{
		if (!IsValid(Bird))
		{
			return false;
		}
		const int32 BirdIndex = static_cast<int32>(Bird->GetBirdId());
		USkeletalMeshComponent* Visual = Bird->GetBirdVisual();
		if (BirdIndex < 0 || BirdIndex >= 4 || !IsValid(Visual)
			|| Sources[BirdIndex].IsValid())
		{
			return false;
		}
		Sources[BirdIndex] = Visual;
		InitialTransforms[BirdIndex] = Visual->GetComponentTransform();
		InitialVisibility[BirdIndex] =
			Visual->IsVisible() && !Visual->bHiddenInGame ? 1 : 0;
	}
	for (const TWeakObjectPtr<USkeletalMeshComponent>& Source : Sources)
	{
		if (!Source.IsValid())
		{
			return false;
		}
	}
	ProductionController = &InController;
	ProductionAuthoritativeUFO = &InAuthoritativeUFO;
	ProductionBirdSources = MoveTemp(Sources);
	ProductionBirdInitialWorldTransforms = MoveTemp(InitialTransforms);
	ProductionBirdInitialVisibility = MoveTemp(InitialVisibility);
	bProductionBinding = true;
	PreviewTimeScale = 1.0f;
	return true;
}

void AABTSM11FinalePostHitCinematicPreview::StopPreview()
{
	FinishPreview(
		!bCaptureEnabled,
		false,
		TEXT("StoppedByCommand"));
}

void AABTSM11FinalePostHitCinematicPreview::InitializeAnimationDrivers()
{
	AnimationDrivers.Reserve(BirdVisuals.Num());
	for (int32 Index = 0; Index < BirdVisuals.Num(); ++Index)
	{
		UABTSBirdAnimationPresentationComponent* Driver =
			NewObject<UABTSBirdAnimationPresentationComponent>(
				this,
				*FString::Printf(TEXT("M11PostHitBirdAnimation%d"), Index));
		if (Driver != nullptr)
		{
			Driver->RegisterComponent();
			Driver->InitializePresentation(BirdVisuals[Index], false);
		}
		AnimationDrivers.Add(Driver);
	}
}

void AABTSM11FinalePostHitCinematicPreview::InitializeUFOPresentation()
{
	if (GetWorld() == nullptr)
	{
		return;
	}
	IConsoleVariable* CustomRenderer = IConsoleManager::Get().FindConsoleVariable(
		TEXT("p.Chaos.GC.UseCustomRenderer"));
	if (CustomRenderer == nullptr)
	{
		UE_LOG(
			LogABTSRuntime,
			Error,
			TEXT("[ABTS][M11-D][PostHitPreview] Geometry Collection renderer control unavailable; real-debris preview rejected."));
		return;
	}
	PreviousGeometryCollectionCustomRenderer = CustomRenderer->GetInt();
	CustomRenderer->Set(0, ECVF_SetByCode);
	bGeometryCollectionRendererOverridden = true;
	UClass* PresentationClass = LoadClass<AActor>(
		nullptr,
		TEXT("/Game/Destruction/GeometryCollections/BP_UFOPresentation.BP_UFOPresentation_C"));
	if (PresentationClass == nullptr)
	{
		FallbackUFOVisual->SetVisibility(true, true);
		UE_LOG(
			LogABTSRuntime,
			Warning,
			TEXT("[ABTS][M11-D][PostHitPreview] BP_UFOPresentation unavailable; using static fallback."));
		return;
	}
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	UFOPresentationActor = GetWorld()->SpawnActor<AActor>(
		PresentationClass,
		GetActorTransform(),
		SpawnParameters);
	if (!IsValid(UFOPresentationActor))
	{
		FallbackUFOVisual->SetVisibility(true, true);
		return;
	}
	UFOPresentationActor->AttachToActor(
		this,
		FAttachmentTransformRules::KeepWorldTransform);
	UFOPresentationActor->SetActorRelativeTransform(FTransform(
		FRotator::ZeroRotator,
		FVector::ZeroVector,
		FVector(3.6f)));
	UFOPresentationActor->SetActorEnableCollision(false);

	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
	UFOPresentationActor->GetComponents(PrimitiveComponents);
	for (UPrimitiveComponent* Primitive : PrimitiveComponents)
	{
		if (Primitive == nullptr)
		{
			continue;
		}
		const bool bIsBrokenVisual =
			ABTSM11PostHitPreviewPrivate::HasComponentSemantic(
				*Primitive,
				TEXT("ABTS.UFO.BrokenVisual"),
				TEXT("BrokenVisual"));
		Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Primitive->SetCollisionResponseToAllChannels(ECR_Ignore);
		Primitive->SetGenerateOverlapEvents(false);
		Primitive->SetSimulatePhysics(false);
		Primitive->SetEnableGravity(false);
		Primitive->SetCanEverAffectNavigation(false);
		if (ABTSM11PostHitPreviewPrivate::HasComponentSemantic(
				*Primitive,
				TEXT("ABTS.UFO.IntactVisual"),
				TEXT("IntactVisual")))
		{
			UFOIntactVisual = Primitive;
		}
		else if (bIsBrokenVisual)
		{
			UFOBrokenVisual = Primitive;
		}
	}
	FallbackUFOVisual->SetVisibility(UFOIntactVisual == nullptr, true);
	if (UFOBrokenVisual != nullptr)
	{
		const bool bDisabledRemovalOnSleep =
			ABTSM11PostHitPreviewPrivate::SetBoolProperty(
				*UFOBrokenVisual,
				TEXT("bAllowRemovalOnSleep"),
				false);
		const bool bDisabledRemovalOnBreak =
			ABTSM11PostHitPreviewPrivate::SetBoolProperty(
				*UFOBrokenVisual,
				TEXT("bAllowRemovalOnBreak"),
				false);
		UFOBrokenVisual->SetVisibility(false, true);
		UFOBrokenVisual->SetHiddenInGame(true, true);
		if (!bDisabledRemovalOnSleep || !bDisabledRemovalOnBreak)
		{
			UFOBrokenVisual = nullptr;
		}
	}
	bRealUFODebrisReady = UFOBrokenVisual != nullptr
		&& UFOBrokenVisual->GetClass()->GetPathName()
			== TEXT("/Script/GeometryCollectionEngine.GeometryCollectionComponent")
		&& UFOBrokenVisual->FindFunction(
			ABTSM11PostHitPreviewPrivate::GetInitialLocalRestTransformsFunctionName)
			!= nullptr
		&& UFOBrokenVisual->FindFunction(
			ABTSM11PostHitPreviewPrivate::
				ForceBrokenForCustomRendererFunctionName)
			!= nullptr
		&& UFOBrokenVisual->FindFunction(
			ABTSM11PostHitPreviewPrivate::CrumbleActiveClustersFunctionName)
			!= nullptr
		&& UFOBrokenVisual->FindFunction(
			ABTSM11PostHitPreviewPrivate::RemoveAllAnchorsFunctionName)
			!= nullptr;
	if (bRealUFODebrisReady)
	{
		bRealUFODebrisReady =
			ABTSM11PostHitPreviewPrivate::ReadTransformArrayReturnValue(
				*UFOBrokenVisual,
				ABTSM11PostHitPreviewPrivate::
					GetInitialLocalRestTransformsFunctionName,
				InitialRealDebrisTransforms);
	}
	const float BrokenBoundsRadius = UFOBrokenVisual != nullptr
		? UFOBrokenVisual->Bounds.SphereRadius
		: 0.0f;
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M11-D][PostHitPreview] UFOPresentation Loaded=1 Intact=%d Broken=%d RealGeometryCollectionDebris=%d RemovalDisabled=%d RestTransforms=%d BrokenBoundsRadius=%.2f BrokenSimulating=%d Collision=0"),
		UFOIntactVisual != nullptr ? 1 : 0,
		UFOBrokenVisual != nullptr ? 1 : 0,
		bRealUFODebrisReady ? 1 : 0,
		UFOBrokenVisual != nullptr ? 1 : 0,
		InitialRealDebrisTransforms.Num(),
		BrokenBoundsRadius,
		UFOBrokenVisual != nullptr
			&& UFOBrokenVisual->IsSimulatingPhysics() ? 1 : 0);
	if (!bRealUFODebrisReady)
	{
		UE_LOG(
			LogABTSRuntime,
			Error,
			TEXT("[ABTS][M11-D][PostHitPreview] BrokenVisual does not expose usable Geometry Collection rest transforms; real-debris preview rejected."));
	}
}

bool AABTSM11FinalePostHitCinematicPreview::UpdatePresentation(
	const float DeltaSeconds)
{
	UpdateBirds(DeltaSeconds);
	if (!UpdateUFOAndDebris())
	{
		return false;
	}
	UpdateCamera();
	UpdateLighting();
	return true;
}

void AABTSM11FinalePostHitCinematicPreview::UpdateBirds(
	const float DeltaSeconds)
{
	for (int32 Index = 0; Index < BirdVisuals.Num(); ++Index)
	{
		USkeletalMeshComponent* Visual = BirdVisuals[Index];
		if (Visual == nullptr)
		{
			continue;
		}
		const FABTSM11FinalePostHitBirdPose Pose =
			FABTSM11FinalePostHitCinematicEvaluator::EvaluateBird(
				ElapsedSeconds,
				static_cast<EABTSM11FinalePostHitBird>(Index));
		Visual->SetVisibility(Pose.bVisible, true);
		const FTransform AuthoredRelativeTransform(
			ResolveBirdVisualRotation(Pose.LocalFacing),
			Pose.LocalPosition,
			FVector(4.0f * Pose.VisualScale));
		if (bProductionBinding && Index < 4
			&& ProductionBirdInitialWorldTransforms.IsValidIndex(Index))
		{
			const float RawAlpha = FMath::Clamp(
				ElapsedSeconds
					/ FABTSM11FinalePostHitCinematicEvaluator::ImpactEndSeconds,
				0.0f,
				1.0f);
			const float BlendAlpha = RawAlpha * RawAlpha
				* (3.0f - 2.0f * RawAlpha);
			FTransform BlendedTransform;
			BlendedTransform.Blend(
				ProductionBirdInitialWorldTransforms[Index],
				AuthoredRelativeTransform * GetActorTransform(),
				BlendAlpha);
			Visual->SetWorldTransform(
				BlendedTransform,
				false,
				nullptr,
				ETeleportType::TeleportPhysics);
		}
		else
		{
			Visual->SetRelativeTransform(AuthoredRelativeTransform);
		}
		if (AnimationDrivers.IsValidIndex(Index)
			&& AnimationDrivers[Index] != nullptr)
		{
			FABTSBirdAnimationSnapshot Snapshot;
			Snapshot.bGrounded = false;
			Snapshot.bForceFlight = true;
			AnimationDrivers[Index]->UpdatePresentation(
				Snapshot,
				DeltaSeconds);
		}
	}
}

bool AABTSM11FinalePostHitCinematicPreview::UpdateUFOAndDebris()
{
	const FABTSM11FinalePostHitUFOPose Pose =
		FABTSM11FinalePostHitCinematicEvaluator::EvaluateUFO(
			ElapsedSeconds);
	if (UFOIntactVisual != nullptr)
	{
		UFOIntactVisual->SetVisibility(Pose.bIntactVisible, true);
	}
	FallbackUFOVisual->SetVisibility(
		UFOIntactVisual == nullptr
			&& (Pose.bIntactVisible
				|| (!bRealUFODebrisReady && Pose.bBrokenVisible)),
		true);
	if (UFOBrokenVisual != nullptr)
	{
		UFOBrokenVisual->SetVisibility(Pose.bBrokenVisible, true);
		UFOBrokenVisual->SetHiddenInGame(!Pose.bBrokenVisible, true);
	}
	const bool bActivationTimeReached = ElapsedSeconds
		>= FABTSM11FinalePostHitCinematicEvaluator::ImpactBreakCueSeconds
			+ ABTSM11PostHitPreviewPrivate::RealDebrisActivationDelaySeconds;
	if (Pose.bBrokenVisible && bActivationTimeReached
		&& bRealUFODebrisReady
		&& !bRealUFODebrisActivated
		&& !ActivateRealUFODebris())
	{
		return false;
	}
	if (Pose.bBrokenVisible && bRealUFODebrisActivated
		&& !bRealUFODebrisStopped)
	{
		if (!AdvanceRealUFODebrisPlayback())
		{
			return false;
		}
	}
	else if (!Pose.bBrokenVisible && bRealUFODebrisActivated
		&& !bRealUFODebrisStopped)
	{
		StopRealUFODebrisSimulation();
	}
	ImpactFlash->SetIntensity(42000.0f * Pose.FlashAlpha);
	return true;
}

bool AABTSM11FinalePostHitCinematicPreview::ActivateRealUFODebris()
{
	using namespace ABTSM11PostHitPreviewPrivate;
	if (!bRealUFODebrisReady || !IsValid(UFOBrokenVisual))
	{
		return false;
	}
	UFOBrokenVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	UFOBrokenVisual->SetCollisionResponseToAllChannels(ECR_Ignore);
	UFOBrokenVisual->SetGenerateOverlapEvents(false);
	UFOBrokenVisual->SetEnableGravity(false);
	if (!InvokeBoolFunction(
			*UFOBrokenVisual,
			ForceBrokenForCustomRendererFunctionName,
			TEXT("bForceBroken"),
			true))
	{
		return false;
	}
	if (!InvokeNoParameterFunction(
			*UFOBrokenVisual,
			RemoveAllAnchorsFunctionName)
		|| !InvokeNoParameterFunction(
			*UFOBrokenVisual,
			CrumbleActiveClustersFunctionName))
	{
		return false;
	}
	bRealUFODebrisActivated = true;
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M11-D][PostHitPreview][RealDebrisPlaybackActivated] Asset=GC_UFO_Broken Component=BrokenVisual Mode=NativeRendererCrumble RootProxy=0 RestTransforms=%d Simulating=1 Gravity=0 BoundsOrigin=%s BoundsRadius=%.2f ComponentScale=%s"),
		InitialRealDebrisTransforms.Num(),
		*UFOBrokenVisual->Bounds.Origin.ToCompactString(),
		UFOBrokenVisual->Bounds.SphereRadius,
		*UFOBrokenVisual->GetComponentScale().ToCompactString());
	return true;
}

bool AABTSM11FinalePostHitCinematicPreview::AdvanceRealUFODebrisPlayback()
{
	using namespace ABTSM11PostHitPreviewPrivate;
	if (!IsValid(UFOBrokenVisual))
	{
		return false;
	}
	const float PlaybackSeconds = FMath::Max(
		0.0f,
		ElapsedSeconds
			- FABTSM11FinalePostHitCinematicEvaluator::ImpactBreakCueSeconds
			- RealDebrisActivationDelaySeconds);
	if (!bRealUFODebrisImpulseApplied
		&& PlaybackSeconds >= RealDebrisImpulseDelaySeconds)
	{
		const FVector BoundsOrigin = UFOBrokenVisual->Bounds.Origin;
		const float ImpulseRadius = FMath::Max(
			520.0f,
			UFOBrokenVisual->Bounds.SphereRadius * 1.4f);
		UFOBrokenVisual->AddRadialImpulse(
			BoundsOrigin - GetActorForwardVector() * 55.0f,
			ImpulseRadius,
			520.0f,
			ERadialImpulseFalloff::RIF_Linear,
			true);
		UFOBrokenVisual->AddImpulse(
			GetActorForwardVector() * 105.0f
				+ GetActorUpVector() * 58.0f,
			NAME_None,
			true);
		bRealUFODebrisImpulseApplied = true;
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][M11-D][PostHitPreview][RealDebrisImpulseApplied] Delay=%.2f Radius=%.2f RadialVelocity=520.00 ForwardVelocity=105.00 UpVelocity=58.00"),
			PlaybackSeconds,
			ImpulseRadius);
	}
	return true;
}

void AABTSM11FinalePostHitCinematicPreview::StopRealUFODebrisSimulation()
{
	if (!IsValid(UFOBrokenVisual) || bRealUFODebrisStopped)
	{
		return;
	}
	bRealUFODebrisStopped = true;
	UFOBrokenVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AABTSM11FinalePostHitCinematicPreview::UpdateCamera()
{
	const FABTSM11FinalePostHitCameraPose Pose =
		FABTSM11FinalePostHitCinematicEvaluator::EvaluateCamera(
			ElapsedSeconds);
	const FVector CameraWorldLocation = GetActorTransform().TransformPosition(
		Pose.LocalPosition);
	const FVector LookAtWorld = GetActorTransform().TransformPosition(
		Pose.LocalLookAt);
	const FVector LookDirection = (LookAtWorld - CameraWorldLocation)
		.GetSafeNormal();
	CinematicCamera->SetWorldLocationAndRotation(
		CameraWorldLocation,
		FRotationMatrix::MakeFromXZ(
			LookDirection,
			GetActorUpVector()).ToQuat());
	CinematicCamera->SetFieldOfView(Pose.FieldOfViewDegrees);
	if (bProductionBinding && IsValid(PreviewController)
		&& IsValid(PreviewController->PlayerCameraManager))
	{
		PreviewController->PlayerCameraManager->SetManualCameraFade(
			Pose.FadeToBlackAlpha,
			FLinearColor::Black,
			false);
		bProductionCameraFadeActive = Pose.FadeToBlackAlpha > 0.0f;
	}
}

void AABTSM11FinalePostHitCinematicPreview::UpdateLighting()
{
	const FABTSM11FinalePostHitLightingPose Pose =
		FABTSM11FinalePostHitCinematicEvaluator::EvaluateLighting(
			ElapsedSeconds);
	CinematicKeyLight->SetRelativeLocation(Pose.KeyLocalPosition);
	CinematicFillLight->SetRelativeLocation(Pose.FillLocalPosition);
	CinematicRimLight->SetRelativeLocation(Pose.RimLocalPosition);
	CinematicKeyLight->SetIntensity(Pose.KeyIntensity);
	CinematicFillLight->SetIntensity(Pose.FillIntensity);
	CinematicRimLight->SetIntensity(Pose.RimIntensity);
}

void AABTSM11FinalePostHitCinematicPreview::TriggerCrossedAudioCues(
	const float PreviousTimeSeconds,
	const float CurrentTimeSeconds)
{
	const EABTSM11FinalePostHitAudioCue Cues =
		FABTSM11FinalePostHitCinematicEvaluator::ResolveCrossedAudioCues(
			PreviousTimeSeconds,
			CurrentTimeSeconds);
	if (Cues == EABTSM11FinalePostHitAudioCue::None)
	{
		return;
	}
	UABTSAudioWorldSubsystem* Audio = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<UABTSAudioWorldSubsystem>()
		: nullptr;
	const FVector ImpactWorld = GetActorLocation();
	if (EnumHasAnyFlags(Cues, EABTSM11FinalePostHitAudioCue::ImpactBreak))
	{
		if (Audio != nullptr)
		{
			Audio->PlayImpact(
				ImpactWorld,
				EABTSM6ImpactMaterial::Iron,
				1800.0f);
			Audio->PlayExplosion(ImpactWorld, false);
		}
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][M11-D][PostHitAudio] Cue=ImpactBreak Time=%.2f UIConfirm=0"),
			FABTSM11FinalePostHitCinematicEvaluator::ImpactBreakCueSeconds);
	}
	if (EnumHasAnyFlags(Cues, EABTSM11FinalePostHitAudioCue::RescueRelease))
	{
		if (Audio != nullptr)
		{
			Audio->PlayUIEvent(EABTSUIAudioEvent::Select);
		}
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][M11-D][PostHitAudio] Cue=RescueRelease Time=%.2f Placeholder=SharedSelect"),
			FABTSM11FinalePostHitCinematicEvaluator::RescueReleaseCueSeconds);
	}
	if (EnumHasAnyFlags(Cues, EABTSM11FinalePostHitAudioCue::Reunion))
	{
		if (Audio != nullptr)
		{
			for (int32 BirdIndex = 0; BirdIndex < 4; ++BirdIndex)
			{
				const FABTSM11FinalePostHitBirdPose BirdPose =
					FABTSM11FinalePostHitCinematicEvaluator::EvaluateBird(
						CurrentTimeSeconds,
						static_cast<EABTSM11FinalePostHitBird>(BirdIndex));
				Audio->PlayBirdChirp(
					GetActorTransform().TransformPosition(
						BirdPose.LocalPosition),
					static_cast<EABTSBirdId>(BirdIndex),
					0.72f);
			}
		}
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][M11-D][PostHitAudio] Cue=Reunion Time=%.2f FourBirdChirps=1 WhiteCue=IntegrationPending"),
			FABTSM11FinalePostHitCinematicEvaluator::ReunionCueSeconds);
	}
	if (EnumHasAnyFlags(Cues, EABTSM11FinalePostHitAudioCue::Completion))
	{
		if (Audio != nullptr)
		{
			Audio->PlayUIEvent(EABTSUIAudioEvent::Confirm);
		}
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][M11-D][PostHitAudio] Cue=Completion Time=%.2f UIConfirm=1"),
			FABTSM11FinalePostHitCinematicEvaluator::CompletionCueSeconds);
	}
}

FQuat AABTSM11FinalePostHitCinematicPreview::ResolveBirdVisualRotation(
	const FVector& LocalFacing) const
{
	const FVector Facing = LocalFacing.IsNearlyZero()
		? FVector::ForwardVector
		: LocalFacing.GetSafeNormal();
	return FRotationMatrix::MakeFromXZ(Facing, FVector::UpVector).ToQuat()
		* FRotator(0.0f, -90.0f, 0.0f).Quaternion();
}

void AABTSM11FinalePostHitCinematicPreview::FinishPreview(
	const bool bBlendBack,
	const bool bSuccess,
	const FString& Reason)
{
	if (bPreviewFinished)
	{
		return;
	}
	bPreviewFinished = true;
	SetActorTickEnabled(false);
	bool bFinalSuccess = bSuccess;
	FString FinalReason = Reason;
	if (bCaptureEnabled && bSuccess && !MuxCapturedFramesToAvi())
	{
		bFinalSuccess = false;
		FinalReason = CaptureFailureReason;
	}
	if (bCaptureEnabled
		&& !WriteCaptureManifest(bFinalSuccess, FinalReason))
	{
		bFinalSuccess = false;
		FinalReason = TEXT("CaptureManifestWriteFailed");
	}
	RestoreCaptureGlobals();
	RestoreGeometryCollectionRenderer();
	if (bProductionBinding)
	{
		RestoreProductionSources(!bFinalSuccess);
	}
	if (PreviewController != nullptr && SavedViewTarget != nullptr
		&& PreviewController->GetViewTarget() == this)
	{
		if (bBlendBack)
		{
			PreviewController->SetViewTargetWithBlend(SavedViewTarget, 1.0f);
		}
		else
		{
			PreviewController->SetViewTarget(SavedViewTarget);
		}
	}
	const FString Artifact = bCaptureEnabled
		? GetCaptureVideoPath()
		: TEXT("None");
	const FString CompletionSummary = FString::Printf(
		TEXT("[ABTS][M11-D][PostHitPreview][Completed] Success=%d Elapsed=%.2f Capture=%d Frames=%d Reason=%s Artifact=%s DebrisSource=GC_UFO_Broken RealGeometryCollectionDebris=%d DebrisPlayback=StagedNativeChaos GameplayMutation=%d ProductionBinding=%d AuthoritativeUFORetired=%d"),
		bFinalSuccess ? 1 : 0,
		ElapsedSeconds,
		bCaptureEnabled ? 1 : 0,
		CapturedFrameCount,
		*FinalReason,
		*Artifact,
		bRealUFODebrisActivated ? 1 : 0,
		bProductionBinding ? 1 : 0,
		bProductionBinding ? 1 : 0,
		bProductionBinding && bFinalSuccess ? 1 : 0);
	if (bFinalSuccess)
	{
		UE_LOG(LogABTSRuntime, Log, TEXT("%s"), *CompletionSummary);
	}
	else
	{
		UE_LOG(LogABTSRuntime, Error, TEXT("%s"), *CompletionSummary);
	}
	if (bCaptureEnabled)
	{
		FPlatformMisc::RequestExitWithStatus(false, bFinalSuccess ? 0 : 2);
		return;
	}
	SetLifeSpan(bBlendBack ? 1.1f : 0.01f);
}

void AABTSM11FinalePostHitCinematicPreview::RestoreProductionSources(
	const bool bRestoreAuthoritativeUFO)
{
	if (!bProductionBinding)
	{
		return;
	}
	for (int32 Index = 0; Index < ProductionBirdSources.Num(); ++Index)
	{
		if (ProductionBirdSources[Index].IsValid())
		{
			const bool bVisible = ProductionBirdInitialVisibility.IsValidIndex(Index)
				&& ProductionBirdInitialVisibility[Index] != 0;
			ProductionBirdSources[Index]->SetVisibility(bVisible, true);
		}
	}
	if (bRestoreAuthoritativeUFO && ProductionAuthoritativeUFO.IsValid())
	{
		ProductionAuthoritativeUFO->SetActorHiddenInGame(
			bProductionUFOInitiallyHidden);
	}
	if (ProductionController.IsValid())
	{
		if (AHUD* HUD = ProductionController->GetHUD())
		{
			HUD->bShowHUD = bProductionHUDInitiallyVisible;
		}
		if (bProductionCameraFadeActive
			&& IsValid(ProductionController->PlayerCameraManager))
		{
			ProductionController->PlayerCameraManager->StopCameraFade();
			ProductionController->PlayerCameraManager->SetManualCameraFade(
				0.0f,
				FLinearColor::Black,
				false);
		}
	}
	bProductionSourcesHidden = false;
	bProductionCameraFadeActive = false;
}

bool AABTSM11FinalePostHitCinematicPreview::StartOffscreenCapture()
{
	if (!IFileManager::Get().MakeDirectory(*CaptureOutputDirectory, true))
	{
		CaptureFailureReason = TEXT("CaptureOutputDirectoryUnavailable");
		return false;
	}
	const FString RenderingRHI = GDynamicRHI != nullptr
		? GDynamicRHI->GetName()
		: TEXT("Unavailable");
	if (!RenderingRHI.Equals(TEXT("D3D12"), ESearchCase::IgnoreCase))
	{
		CaptureFailureReason = FString::Printf(
			TEXT("RealGeometryCollectionDebrisRequiresD3D12Nanite-ActualRHI=%s"),
			*RenderingRHI);
		return false;
	}
	TArray<FString> ExistingFrames;
	IFileManager::Get().FindFiles(
		ExistingFrames,
		*GetCaptureFrameWildcard(),
		true,
		false);
	const FString ManifestPath = FPaths::Combine(
		CaptureOutputDirectory,
		CaptureMovieName + TEXT(".manifest.json"));
	if (!ExistingFrames.IsEmpty()
		|| IFileManager::Get().FileExists(*GetCaptureVideoPath())
		|| IFileManager::Get().FileExists(*ManifestPath))
	{
		CaptureFailureReason = TEXT("CaptureOutputMustBeUnique");
		return false;
	}
	RecordingRenderTarget = NewObject<UTextureRenderTarget2D>(
		this,
		TEXT("M11PostHitRecordingRenderTarget"));
	if (!IsValid(RecordingRenderTarget) || !IsValid(RecordingCapture))
	{
		CaptureFailureReason = TEXT("CaptureRenderTargetAllocationFailed");
		return false;
	}
	RecordingRenderTarget->InitCustomFormat(
		CaptureWidth,
		CaptureHeight,
		PF_B8G8R8A8,
		false);
	RecordingRenderTarget->UpdateResourceImmediate(true);
	RecordingCapture->TextureTarget = RecordingRenderTarget;
	bPreviousUseFixedTimeStep = FApp::UseFixedTimeStep();
	PreviousFixedDeltaTime = FApp::GetFixedDeltaTime();
	bPreviousStylizedEnabled = FABTSStylizedRenderingControl::IsEnabled();
	PreviousStylizedProfile = static_cast<int32>(
		FABTSStylizedRenderingControl::GetProfile());
	FABTSStylizedRenderingControl::SetEnabled(true);
	FABTSStylizedRenderingControl::SetProfile(
		EABTSStylizedRenderProfile::FinaleSpace);
	bStylizedCaptureRegistered = FABTSStylizedSceneCaptureRegistry::Register(
		*RecordingCapture,
		EABTSStylizedViewClass::FinaleCinematicCapture);
	if (!bStylizedCaptureRegistered)
	{
		CaptureFailureReason = TEXT("StylizedCaptureRegistrationFailed");
		return false;
	}
	FApp::SetUseFixedTimeStep(true);
	FApp::SetFixedDeltaTime(
		1.0 / static_cast<double>(CaptureFrameRate));
	bCaptureStarted = true;
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M11-D][PostHitCapture][Started] Output=%s FPS=%d Size=%dx%d Warmup=%d Format=JPG+MJPEGAVI RHI=%s NaniteRequired=1 RenderOffscreenExpected=1"),
		*CaptureOutputDirectory,
		CaptureFrameRate,
		CaptureWidth,
		CaptureHeight,
		RemainingWarmupFrames,
		*RenderingRHI);
	return true;
}

bool AABTSM11FinalePostHitCinematicPreview::CaptureCurrentFrame()
{
	if (!IsValid(RecordingCapture) || !IsValid(RecordingRenderTarget))
	{
		CaptureFailureReason = TEXT("CaptureDependenciesUnavailable");
		return false;
	}
	RecordingCapture->SetWorldLocationAndRotation(
		CinematicCamera->GetComponentLocation(),
		CinematicCamera->GetComponentRotation());
	RecordingCapture->FOVAngle = CinematicCamera->FieldOfView;
	RecordingCapture->PostProcessSettings =
		CinematicCamera->PostProcessSettings;
	RecordingCapture->PostProcessBlendWeight =
		CinematicCamera->PostProcessBlendWeight;
	RecordingCapture->CaptureScene();
	FTextureRenderTargetResource* Resource =
		RecordingRenderTarget->GameThread_GetRenderTargetResource();
	TArray<FColor> Pixels;
	FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
	ReadFlags.SetLinearToGamma(false);
	const FIntPoint Size(CaptureWidth, CaptureHeight);
	if (Resource == nullptr || !Resource->ReadPixels(Pixels, ReadFlags)
		|| Pixels.Num() != Size.X * Size.Y)
	{
		CaptureFailureReason = TEXT("CaptureRenderTargetReadFailed");
		return false;
	}
	const FString FramePath = FPaths::Combine(
		CaptureOutputDirectory,
		FString::Printf(
			TEXT("%s.%06d.jpg"),
			*CaptureMovieName,
			CapturedFrameCount));
	const FImageView Image(
		Pixels.GetData(),
		Size.X,
		Size.Y,
		EGammaSpace::sRGB);
	if (!FImageUtils::SaveImageByExtension(
			*FramePath,
			Image,
			CaptureJpegQuality))
	{
		CaptureFailureReason = TEXT("CaptureJpegWriteFailed");
		return false;
	}
	++CapturedFrameCount;
	return true;
}

bool AABTSM11FinalePostHitCinematicPreview::MuxCapturedFramesToAvi()
{
	using namespace ABTSM11PostHitPreviewPrivate;
	TArray<FString> FrameNames;
	IFileManager::Get().FindFiles(
		FrameNames,
		*GetCaptureFrameWildcard(),
		true,
		false);
	FrameNames.Sort();
	if (FrameNames.Num() != CapturedFrameCount
		|| CapturedFrameCount <= 0)
	{
		CaptureFailureReason = TEXT("CaptureFrameCountMismatchBeforeMux");
		return false;
	}
	uint32 MaxFrameBytes = 0;
	for (int32 Index = 0; Index < FrameNames.Num(); ++Index)
	{
		const FString ExpectedName = FString::Printf(
			TEXT("%s.%06d.jpg"),
			*CaptureMovieName,
			Index);
		const int64 Size = IFileManager::Get().FileSize(
			*FPaths::Combine(CaptureOutputDirectory, FrameNames[Index]));
		if (FrameNames[Index] != ExpectedName
			|| Size <= 4 || Size > MAX_uint32)
		{
			CaptureFailureReason = TEXT("CaptureFrameIdentityInvalidBeforeMux");
			return false;
		}
		MaxFrameBytes = FMath::Max(
			MaxFrameBytes,
			static_cast<uint32>(Size));
	}
	const FString VideoPath = GetCaptureVideoPath();
	IPlatformFile& PlatformFile =
		FPlatformFileManager::Get().GetPlatformFile();
	TUniquePtr<IFileHandle> File(PlatformFile.OpenWrite(*VideoPath));
	if (!File)
	{
		CaptureFailureReason = TEXT("CaptureAviOpenWriteFailed");
		return false;
	}
	const auto FailMux = [this, &File, &PlatformFile, &VideoPath](
		const TCHAR* Reason)
	{
		CaptureFailureReason = Reason;
		File.Reset();
		PlatformFile.DeleteFile(*VideoPath);
		return false;
	};
	const int64 Riff = BeginChunk(*File, "RIFF");
	if (Riff < 0 || !WriteFourCC(*File, "AVI "))
	{
		return FailMux(TEXT("CaptureAviRiffHeaderWriteFailed"));
	}
	const int64 HeaderList = BeginChunk(*File, "LIST");
	if (HeaderList < 0 || !WriteFourCC(*File, "hdrl"))
	{
		return FailMux(TEXT("CaptureAviHeaderListWriteFailed"));
	}
	const int64 MainHeader = BeginChunk(*File, "avih");
	const uint32 TotalFrames = static_cast<uint32>(CapturedFrameCount);
	const uint32 Width = static_cast<uint32>(CaptureWidth);
	const uint32 Height = static_cast<uint32>(CaptureHeight);
	if (MainHeader < 0
		|| !WriteUInt32(*File, static_cast<uint32>(
			FMath::RoundToInt(1000000.0 / CaptureFrameRate)))
		|| !WriteUInt32(*File, MaxFrameBytes * CaptureFrameRate)
		|| !WriteUInt32(*File, 0) || !WriteUInt32(*File, 0x10)
		|| !WriteUInt32(*File, TotalFrames) || !WriteUInt32(*File, 0)
		|| !WriteUInt32(*File, 1) || !WriteUInt32(*File, MaxFrameBytes)
		|| !WriteUInt32(*File, Width) || !WriteUInt32(*File, Height)
		|| !WriteUInt32(*File, 0) || !WriteUInt32(*File, 0)
		|| !WriteUInt32(*File, 0) || !WriteUInt32(*File, 0)
		|| !EndChunk(*File, MainHeader))
	{
		return FailMux(TEXT("CaptureAviMainHeaderWriteFailed"));
	}
	const int64 StreamList = BeginChunk(*File, "LIST");
	const int64 StreamHeader = StreamList >= 0
		&& WriteFourCC(*File, "strl")
		? BeginChunk(*File, "strh")
		: INDEX_NONE;
	if (StreamHeader < 0
		|| !WriteFourCC(*File, "vids") || !WriteFourCC(*File, "MJPG")
		|| !WriteUInt32(*File, 0) || !WriteUInt16(*File, 0)
		|| !WriteUInt16(*File, 0) || !WriteUInt32(*File, 0)
		|| !WriteUInt32(*File, 1)
		|| !WriteUInt32(*File, CaptureFrameRate)
		|| !WriteUInt32(*File, 0) || !WriteUInt32(*File, TotalFrames)
		|| !WriteUInt32(*File, MaxFrameBytes)
		|| !WriteUInt32(*File, MAX_uint32)
		|| !WriteUInt32(*File, 0) || !WriteUInt16(*File, 0)
		|| !WriteUInt16(*File, 0)
		|| !WriteUInt16(*File, static_cast<uint16>(Width))
		|| !WriteUInt16(*File, static_cast<uint16>(Height))
		|| !EndChunk(*File, StreamHeader))
	{
		return FailMux(TEXT("CaptureAviStreamHeaderWriteFailed"));
	}
	const int64 StreamFormat = BeginChunk(*File, "strf");
	if (StreamFormat < 0
		|| !WriteUInt32(*File, 40) || !WriteUInt32(*File, Width)
		|| !WriteUInt32(*File, Height) || !WriteUInt16(*File, 1)
		|| !WriteUInt16(*File, 24) || !WriteFourCC(*File, "MJPG")
		|| !WriteUInt32(*File, Width * Height * 3)
		|| !WriteUInt32(*File, 0) || !WriteUInt32(*File, 0)
		|| !WriteUInt32(*File, 0) || !WriteUInt32(*File, 0)
		|| !EndChunk(*File, StreamFormat)
		|| !EndChunk(*File, StreamList)
		|| !EndChunk(*File, HeaderList))
	{
		return FailMux(TEXT("CaptureAviStreamFormatWriteFailed"));
	}
	const int64 MovieList = BeginChunk(*File, "LIST");
	if (MovieList < 0 || !WriteFourCC(*File, "movi"))
	{
		return FailMux(TEXT("CaptureAviMovieListWriteFailed"));
	}
	const int64 MovieTypeOffset = MovieList + 4;
	TArray<uint32> ChunkOffsets;
	TArray<uint32> ChunkSizes;
	ChunkOffsets.Reserve(CapturedFrameCount);
	ChunkSizes.Reserve(CapturedFrameCount);
	for (const FString& FrameName : FrameNames)
	{
		TArray<uint8> FrameBytes;
		if (!FFileHelper::LoadFileToArray(
				FrameBytes,
				*FPaths::Combine(CaptureOutputDirectory, FrameName))
			|| FrameBytes.Num() < 4 || FrameBytes[0] != 0xFF
			|| FrameBytes[1] != 0xD8
			|| FrameBytes[FrameBytes.Num() - 2] != 0xFF
			|| FrameBytes[FrameBytes.Num() - 1] != 0xD9)
		{
			return FailMux(TEXT("CaptureAviJpegFrameReadFailed"));
		}
		const int64 ChunkStart = File->Tell();
		const int64 FrameChunk = BeginChunk(*File, "00dc");
		if (FrameChunk < 0
			|| !WriteBytes(*File, FrameBytes.GetData(), FrameBytes.Num())
			|| !EndChunk(*File, FrameChunk))
		{
			return FailMux(TEXT("CaptureAviJpegFrameWriteFailed"));
		}
		ChunkOffsets.Add(static_cast<uint32>(ChunkStart - MovieTypeOffset));
		ChunkSizes.Add(static_cast<uint32>(FrameBytes.Num()));
	}
	if (!EndChunk(*File, MovieList))
	{
		return FailMux(TEXT("CaptureAviMovieListFinalizeFailed"));
	}
	const int64 IndexChunk = BeginChunk(*File, "idx1");
	for (int32 Index = 0; Index < ChunkOffsets.Num(); ++Index)
	{
		if (!WriteFourCC(*File, "00dc") || !WriteUInt32(*File, 0x10)
			|| !WriteUInt32(*File, ChunkOffsets[Index])
			|| !WriteUInt32(*File, ChunkSizes[Index]))
		{
			return FailMux(TEXT("CaptureAviIndexWriteFailed"));
		}
	}
	if (IndexChunk < 0 || !EndChunk(*File, IndexChunk)
		|| !EndChunk(*File, Riff))
	{
		return FailMux(TEXT("CaptureAviFinalizeFailed"));
	}
	File.Reset();
	if (IFileManager::Get().FileSize(*VideoPath) <= 4096)
	{
		return FailMux(TEXT("CaptureAviFinalSizeInvalid"));
	}
	return true;
}

bool AABTSM11FinalePostHitCinematicPreview::WriteCaptureManifest(
	const bool bSuccess,
	const FString& Reason) const
{
	if (!bCaptureEnabled || CaptureOutputDirectory.IsEmpty())
	{
		return true;
	}
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schemaVersion"), 4);
	Root->SetStringField(TEXT("stage"), TEXT("M11-D-PostHitPreview"));
	Root->SetBoolField(TEXT("success"), bSuccess);
	Root->SetStringField(TEXT("reason"), Reason);
	Root->SetNumberField(
		TEXT("durationSeconds"),
		FABTSM11FinalePostHitCinematicEvaluator::DurationSeconds);
	Root->SetNumberField(TEXT("frameRate"), CaptureFrameRate);
	Root->SetNumberField(TEXT("frameCount"), CapturedFrameCount);
	Root->SetNumberField(TEXT("width"), CaptureWidth);
	Root->SetNumberField(TEXT("height"), CaptureHeight);
	Root->SetStringField(TEXT("video"), GetCaptureVideoPath());
	Root->SetBoolField(TEXT("gameplayMutation"), false);
	Root->SetBoolField(TEXT("mapBinding"), false);
	Root->SetBoolField(TEXT("productionBinding"), false);
	Root->SetStringField(
		TEXT("lightingRig"),
		TEXT("CameraRelativeThreePoint"));
	Root->SetNumberField(
		TEXT("fixedExposureBias"),
		FABTSM11FinalePostHitCinematicEvaluator::CinematicExposureBias);
	Root->SetBoolField(TEXT("materialOverride"), false);
	Root->SetStringField(
		TEXT("debrisSource"),
		TEXT("/Game/Destruction/GeometryCollections/GC_UFO_Broken"));
	Root->SetStringField(
		TEXT("debrisComponent"),
		TEXT("BP_UFOPresentation.BrokenVisual"));
	Root->SetBoolField(TEXT("realGeometryCollectionDebris"), true);
	Root->SetBoolField(TEXT("liveChaos"), true);
	Root->SetStringField(
		TEXT("debrisPlayback"),
		TEXT("StagedNativeChaosCrumbleAndVelocityImpulse"));
	Root->SetStringField(
		TEXT("geometryCollectionRenderer"),
		TEXT("Native"));
	Root->SetStringField(
		TEXT("renderingRHI"),
		GDynamicRHI != nullptr ? GDynamicRHI->GetName() : TEXT("Unavailable"));
	Root->SetBoolField(TEXT("naniteRequired"), true);
	Root->SetStringField(
		TEXT("authority"),
		TEXT("PreviewTest-IsolatedPostHitDirection"));
	Root->SetNumberField(
		TEXT("impactBreakCueSeconds"),
		FABTSM11FinalePostHitCinematicEvaluator::ImpactBreakCueSeconds);
	Root->SetNumberField(
		TEXT("rescueReleaseCueSeconds"),
		FABTSM11FinalePostHitCinematicEvaluator::RescueReleaseCueSeconds);
	Root->SetNumberField(
		TEXT("reunionCueSeconds"),
		FABTSM11FinalePostHitCinematicEvaluator::ReunionCueSeconds);
	Root->SetNumberField(
		TEXT("completionCueSeconds"),
		FABTSM11FinalePostHitCinematicEvaluator::CompletionCueSeconds);
	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		return false;
	}
	return FFileHelper::SaveStringToFile(
		Json,
		*FPaths::Combine(
			CaptureOutputDirectory,
			CaptureMovieName + TEXT(".manifest.json")),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void AABTSM11FinalePostHitCinematicPreview::RestoreCaptureGlobals()
{
	if (!bCaptureEnabled || bCaptureGlobalsRestored)
	{
		return;
	}
	bCaptureGlobalsRestored = true;
	if (bStylizedCaptureRegistered && IsValid(RecordingCapture))
	{
		FABTSStylizedSceneCaptureRegistry::Unregister(*RecordingCapture);
		bStylizedCaptureRegistered = false;
	}
	if (bCaptureStarted)
	{
		FABTSStylizedRenderingControl::SetEnabled(
			bPreviousStylizedEnabled);
		FABTSStylizedRenderingControl::SetProfile(
			static_cast<EABTSStylizedRenderProfile>(PreviousStylizedProfile));
		FApp::SetUseFixedTimeStep(bPreviousUseFixedTimeStep);
		FApp::SetFixedDeltaTime(PreviousFixedDeltaTime);
	}
}

void AABTSM11FinalePostHitCinematicPreview::
	RestoreGeometryCollectionRenderer()
{
	if (!bGeometryCollectionRendererOverridden)
	{
		return;
	}
	bGeometryCollectionRendererOverridden = false;
	if (IConsoleVariable* CustomRenderer =
		IConsoleManager::Get().FindConsoleVariable(
			TEXT("p.Chaos.GC.UseCustomRenderer")))
	{
		CustomRenderer->Set(
			PreviousGeometryCollectionCustomRenderer,
			ECVF_SetByCode);
	}
}

FString AABTSM11FinalePostHitCinematicPreview::GetCaptureVideoPath() const
{
	return FPaths::Combine(
		CaptureOutputDirectory,
		CaptureMovieName + TEXT(".avi"));
}

FString AABTSM11FinalePostHitCinematicPreview::GetCaptureFrameWildcard() const
{
	return FPaths::Combine(
		CaptureOutputDirectory,
		CaptureMovieName + TEXT(".*.jpg"));
}
