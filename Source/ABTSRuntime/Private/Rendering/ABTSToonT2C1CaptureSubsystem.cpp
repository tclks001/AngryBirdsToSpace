// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/ABTSToonT2C1CaptureSubsystem.h"

#include "ABTSRuntime.h"
#include "Calibration/ABTSCalibrationTargetProxy.h"
#include "Camera/ABTSM101LandingPreviewCamera.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Contracts/ABTSWorldGenerationContracts.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
#include "Game/ABTSM11GameMode.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "ImageUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Misc/SecureHash.h"
#include "PCG/ABTSM3MonthlySatellitePracticeRuntime.h"
#include "Rendering/ABTSStylizedRenderingControl.h"
#include "Rendering/ABTSStylizedRenderingTypes.h"
#include "Rendering/ABTSStylizedRenderingWorldSubsystem.h"
#include "Rendering/ABTSStylizedSceneCaptureRegistry.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Terrain/ABTSM3Planet.h"
#include "World/ABTSM10ScoutMapSystem.h"
#include "World/ABTSM11FinaleInteractionSystem.h"
#include "World/ABTSM11FinaleSystem.h"

namespace ABTSToonT2C1CaptureSubsystemPrivate
{
	constexpr TCHAR RequiredMapSuffix[] = TEXT("L_ABTS_M11");

	template <typename TActorType>
	void GatherActors(UWorld& World, TArray<TActorType*>& OutActors)
	{
		OutActors.Reset();
		for (TActorIterator<TActorType> It(&World); It; ++It)
		{
			if (IsValid(*It))
			{
				OutActors.Add(*It);
			}
		}
	}

	FString Hex64(const uint64 Value)
	{
		return FString::Printf(TEXT("0x%016llX"), Value);
	}

	FString StyleLabel(const bool bEnabled)
	{
		return bEnabled ? TEXT("StyleOn") : TEXT("StyleOff");
	}
}

bool UABTSToonT2C1CaptureSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	FString Suite;
	FParse::Value(
		FCommandLine::Get(),
		TEXT("ABTSVisualCaptureSuite="),
		Suite);
	return Super::ShouldCreateSubsystem(Outer)
		&& (FParse::Param(
				FCommandLine::Get(),
				TEXT("ABTSToonT2C1Capture"))
			|| Suite.Equals(TEXT("ToonT2C1"), ESearchCase::IgnoreCase));
}

void UABTSToonT2C1CaptureSubsystem::Initialize(
	FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (!FABTSToonT2C1CaptureConfig::Parse(
		FCommandLine::Get(),
		Config,
		&ConfigFailure))
	{
		UE_LOG(
			LogABTSRuntime,
			Error,
			TEXT("[ABTS][Rendering][T2-C1] ConfigRejected Reason=%s"),
			*ConfigFailure);
	}
}

void UABTSToonT2C1CaptureSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	StartRealSeconds = FPlatformTime::Seconds();
	if (!ConfigFailure.IsEmpty())
	{
		Finish(false, ConfigFailure);
		// This subsystem only exists for an explicit capture request.  Invalid
		// command lines must fail closed instead of leaving an unattended
		// RenderOffscreen process alive without a usable manifest.
		FGenericPlatformMisc::RequestExit(false);
		return;
	}
	if (!Config.bEnabled)
	{
		Phase = EABTSToonT2C1CapturePhase::Inactive;
		return;
	}
	IPlatformFile& PlatformFile =
		FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.CreateDirectoryTree(*Config.OutputDirectory))
	{
		Finish(false, TEXT("OutputDirectoryCreateFailed"));
		return;
	}
	bSavedStyleEnabled = FABTSStylizedRenderingControl::IsEnabled();
	SavedStyleProfile = FABTSStylizedRenderingControl::GetProfile();
	bRuntimeStateCaptured = true;
	if (IConsoleVariable* ScreenPercentage =
		IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
	{
		SavedScreenPercentage = ScreenPercentage->GetFloat();
		bSavedScreenPercentage = true;
		ScreenPercentage->Set(
			static_cast<float>(Config.ScreenPercentage),
			ECVF_SetByCommandline);
	}
	FABTSStylizedRenderingControl::SetProfile(
		Config.Slice == EABTSToonT2C1CaptureSlice::LandingPreviews
			? EABTSStylizedRenderProfile::GroundDay
			: EABTSStylizedRenderProfile::FinaleSpace);
	FABTSStylizedRenderingControl::SetEnabled(Config.bStylized);
	if (UABTSStylizedRenderingWorldSubsystem* StylizedSubsystem =
		InWorld.GetSubsystem<UABTSStylizedRenderingWorldSubsystem>())
	{
		StylizedSubsystem->RefreshNow();
		bM7AdapterReady = StylizedSubsystem->IsM7SemanticAdapterReady();
	}
	Phase = EABTSToonT2C1CapturePhase::WaitingForWorld;
	WriteManifest(TEXT("WaitingForWorld"), FString());
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][Rendering][T2-C1] Started Contract=%d Slice=%s Stylized=%d Build=%s Output=%s M7AdapterReady=%d"),
		FABTSToonT2C1CaptureConfig::ContractVersion,
		FABTSToonT2C1PreviewFixtureBuilder::LexToString(Config.Slice),
		Config.bStylized ? 1 : 0,
		*Config.BuildIdentity,
		*Config.OutputDirectory,
		bM7AdapterReady ? 1 : 0);
}

void UABTSToonT2C1CaptureSubsystem::Tick(const float DeltaTime)
{
	(void)DeltaTime;
	if (Phase == EABTSToonT2C1CapturePhase::Inactive
		|| Phase == EABTSToonT2C1CapturePhase::Terminal)
	{
		return;
	}
	if (FPlatformTime::Seconds() - StartRealSeconds > Config.TimeoutSeconds)
	{
		Finish(false, TEXT("Timeout"));
		return;
	}
	if (Phase == EABTSToonT2C1CapturePhase::WaitingForWorld)
	{
		FString Reason;
		const EResolveResult Result =
			Config.Slice == EABTSToonT2C1CaptureSlice::LandingPreviews
				? ResolveLandingPreviews(Reason)
				: ResolveFinaleRemotePreview(Reason);
		if (Result == EResolveResult::Failed)
		{
			Finish(false, Reason);
			return;
		}
		if (Result == EResolveResult::Waiting)
		{
			return;
		}
		if (Config.Slice == EABTSToonT2C1CaptureSlice::LandingPreviews
			&& !BeginLandingSubject(Reason))
		{
			Finish(false, Reason);
			return;
		}
		RemainingWarmupFrames = Config.WarmupFrames;
		Phase = EABTSToonT2C1CapturePhase::WarmingCapture;
		return;
	}
	if (Phase == EABTSToonT2C1CapturePhase::WarmingCapture)
	{
		if (Config.Slice == EABTSToonT2C1CaptureSlice::LandingPreviews)
		{
			FString RefreshFailure;
			if (!BeginLandingSubject(RefreshFailure))
			{
				Finish(false, RefreshFailure);
				return;
			}
		}
		if (--RemainingWarmupFrames > 0)
		{
			return;
		}
		if (UABTSStylizedRenderingWorldSubsystem* StylizedSubsystem =
			GetWorld()->GetSubsystem<UABTSStylizedRenderingWorldSubsystem>())
		{
			StylizedSubsystem->RefreshNow();
			bM7AdapterReady = StylizedSubsystem->IsM7SemanticAdapterReady();
		}
		if (bM7AdapterReady)
		{
			Finish(false, TEXT("NoM7SliceObservedM7SemanticAdapter"));
			return;
		}
		FString Failure;
		if (!CaptureCurrentArtifact(Failure))
		{
			Finish(false, Failure);
			return;
		}
		if (Config.Slice == EABTSToonT2C1CaptureSlice::LandingPreviews
			&& CurrentLandingSubjectIndex == 0)
		{
			++CurrentLandingSubjectIndex;
			if (!BeginLandingSubject(Failure))
			{
				Finish(false, Failure);
				return;
			}
			RemainingWarmupFrames = Config.WarmupFrames;
			return;
		}
		Finish(true, TEXT("Complete"));
	}
}

UABTSToonT2C1CaptureSubsystem::EResolveResult
UABTSToonT2C1CaptureSubsystem::ResolveLandingPreviews(FString& OutReason)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		OutReason = TEXT("WorldUnavailable");
		return EResolveResult::Waiting;
	}
	if (!World->GetMapName().EndsWith(
		ABTSToonT2C1CaptureSubsystemPrivate::RequiredMapSuffix))
	{
		OutReason = TEXT("T2C1RequiresLABTSM11");
		return EResolveResult::Failed;
	}
	TArray<AABTSM3Planet*> Planets;
	ABTSToonT2C1CaptureSubsystemPrivate::GatherActors(*World, Planets);
	if (Planets.Num() == 0 || !Planets[0]->IsM3PresentationReady())
	{
		OutReason = TEXT("WaitingForM3Presentation");
		return EResolveResult::Waiting;
	}
	if (Planets.Num() != 1)
	{
		OutReason = TEXT("M3PlanetCardinalityMismatch");
		return EResolveResult::Failed;
	}
	FABTSBuildingGenerationContract BuildingContract;
	if (!Planets[0]->TryExportBuildingGenerationContract(BuildingContract)
		|| !BuildingContract.IsUsable())
	{
		OutReason = TEXT("WaitingForWorldIdentity");
		return EResolveResult::Waiting;
	}
	ActualWorldSeed = BuildingContract.Identity.WorldSeed;
	if (ActualWorldSeed != Config.ExpectedWorldSeed)
	{
		OutReason = TEXT("WorldSeedMismatch");
		return EResolveResult::Failed;
	}

	TArray<AABTSM3MonthlySatellitePracticeRuntime*> SatelliteRuntimes;
	ABTSToonT2C1CaptureSubsystemPrivate::GatherActors(
		*World,
		SatelliteRuntimes);
	if (SatelliteRuntimes.Num() == 0
		|| !SatelliteRuntimes[0]->IsRuntimeReady())
	{
		OutReason = TEXT("WaitingForSatelliteRuntime");
		return EResolveResult::Waiting;
	}
	if (SatelliteRuntimes.Num() != 1
		|| !IsValid(SatelliteRuntimes[0]->GetRuntimeSatellite())
		|| !IsValid(SatelliteRuntimes[0]->GetRuntimeE5Target()))
	{
		OutReason = TEXT("SatelliteRuntimeCardinalityOrActorsInvalid");
		return EResolveResult::Failed;
	}

	TArray<AABTSM10ScoutMapSystem*> ScoutSystems;
	ABTSToonT2C1CaptureSubsystemPrivate::GatherActors(*World, ScoutSystems);
	if (ScoutSystems.Num() == 0)
	{
		OutReason = TEXT("WaitingForScoutMapSystem");
		return EResolveResult::Waiting;
	}
	if (ScoutSystems.Num() != 1)
	{
		OutReason = TEXT("ScoutMapSystemCardinalityMismatch");
		return EResolveResult::Failed;
	}

	Planet = Planets[0];
	SatelliteRuntime = SatelliteRuntimes[0];
	if (!IsValid(PreviewCamera))
	{
		PreviewCamera = World->SpawnActor<AABTSM101LandingPreviewCamera>(
			AABTSM101LandingPreviewCamera::StaticClass(),
			FTransform::Identity);
		if (!IsValid(PreviewCamera))
		{
			OutReason = TEXT("PreviewCameraSpawnFailed");
			return EResolveResult::Failed;
		}
		PreviewCamera->Configure(ScoutSystems[0]->GetSettings());
	}

	FTransform GroundTransform;
	int32 GroundCell = INDEX_NONE;
	FVector GroundLandingWorld = FVector::ZeroVector;
	FVector GroundLandingNormal = FVector::UpVector;
	float GroundSurfaceRadius = 0.0f;
	int32 GroundLandingCell = INDEX_NONE;
	if (!Planet->GetInitialRoadSpawnTransform(
		0.0f,
		GroundTransform,
		GroundCell)
		|| GroundCell == INDEX_NONE
		|| !Planet->QuerySurface(
			(GroundTransform.GetLocation()
				+ GroundTransform.GetRotation().GetForwardVector() * 1600.0
				- Planet->GetPlanetCenterWorld()).GetSafeNormal(),
			GroundLandingWorld,
			GroundLandingNormal,
			GroundSurfaceRadius,
			GroundLandingCell)
		|| GroundLandingCell == INDEX_NONE
		|| !FABTSToonT2C1PreviewFixtureBuilder::BuildGroundLandingPreview(
			GroundLandingWorld,
			GroundLandingNormal,
			GroundTransform.GetRotation().GetForwardVector(),
			GroundPreview))
	{
		OutReason = TEXT("GroundPreviewFixtureFailed");
		return EResolveResult::Failed;
	}
	const FABTSM3MonthlySatelliteRuntimeSnapshot& Snapshot =
		SatelliteRuntime->GetRuntimeSnapshot();
	if (!Snapshot.bValid
		|| !FABTSToonT2C1PreviewFixtureBuilder::BuildSatelliteE5Preview(
			Snapshot.SatelliteWorldTransform.GetLocation(),
			Snapshot.SatelliteRadiusCM,
			Snapshot.E5WorldTransform.GetLocation(),
			Snapshot.E5HalfExtentCM,
			Snapshot.PracticeLaunchWorldTransform.GetRotation().GetForwardVector(),
			SatellitePreview))
	{
		OutReason = TEXT("SatellitePreviewFixtureFailed");
		return EResolveResult::Failed;
	}
	return EResolveResult::Ready;
}

UABTSToonT2C1CaptureSubsystem::EResolveResult
UABTSToonT2C1CaptureSubsystem::ResolveFinaleRemotePreview(FString& OutReason)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		OutReason = TEXT("WorldUnavailable");
		return EResolveResult::Waiting;
	}
	if (!FParse::Param(FCommandLine::Get(), TEXT("ABTSM11CameraCapture")))
	{
		OutReason = TEXT("FinaleRemoteSliceRequiresM11CameraCapture");
		return EResolveResult::Failed;
	}
	TArray<AABTSM3Planet*> Planets;
	ABTSToonT2C1CaptureSubsystemPrivate::GatherActors(*World, Planets);
	if (Planets.Num() == 0 || !Planets[0]->IsM3PresentationReady())
	{
		OutReason = TEXT("WaitingForM3Presentation");
		return EResolveResult::Waiting;
	}
	if (Planets.Num() != 1)
	{
		OutReason = TEXT("M3PlanetCardinalityMismatch");
		return EResolveResult::Failed;
	}
	FABTSBuildingGenerationContract BuildingContract;
	if (!Planets[0]->TryExportBuildingGenerationContract(BuildingContract)
		|| !BuildingContract.IsUsable())
	{
		OutReason = TEXT("WaitingForWorldIdentity");
		return EResolveResult::Waiting;
	}
	ActualWorldSeed = BuildingContract.Identity.WorldSeed;
	if (ActualWorldSeed != Config.ExpectedWorldSeed)
	{
		OutReason = TEXT("WorldSeedMismatch");
		return EResolveResult::Failed;
	}
	AABTSM11GameMode* GameMode = Cast<AABTSM11GameMode>(World->GetAuthGameMode());
	if (GameMode == nullptr
		|| !IsValid(GameMode->GetFinaleSystem())
		|| !GameMode->GetFinaleSystem()->IsLayoutReady()
		|| !IsValid(GameMode->GetFinaleInteractionSystem()))
	{
		OutReason = TEXT("WaitingForM11Systems");
		return EResolveResult::Waiting;
	}
	FinaleInteraction = GameMode->GetFinaleInteractionSystem();
	if (FinaleInteraction->GetTargetCaptureCount() == 0
		|| !IsValid(FinaleInteraction->GetTargetPreviewRenderTarget()))
	{
		OutReason = TEXT("WaitingForM11RemotePreview");
		return EResolveResult::Waiting;
	}
	USceneCaptureComponent2D* Capture =
		FinaleInteraction->GetFinaleRemotePreviewCaptureComponent();
	EABTSStylizedViewClass ViewClass = EABTSStylizedViewClass::MainWorld;
	if (!IsValid(Capture)
		|| !FABTSStylizedSceneCaptureRegistry::TryGetViewClass(
			*Capture,
			ViewClass)
		|| ViewClass != EABTSStylizedViewClass::FinaleRemotePreview)
	{
		OutReason = TEXT("FinaleRemotePreviewViewClassMissing");
		return EResolveResult::Failed;
	}
	if (FABTSStylizedRenderingControl::IsEnabled() != Config.bStylized
		|| FABTSStylizedRenderingControl::GetProfile()
			!= EABTSStylizedRenderProfile::FinaleSpace)
	{
		OutReason = TEXT("FinaleRemotePreviewStyleIdentityMismatch");
		return EResolveResult::Failed;
	}
	return EResolveResult::Ready;
}

bool UABTSToonT2C1CaptureSubsystem::BeginLandingSubject(FString& OutFailure)
{
	if (!IsValid(PreviewCamera)
		|| !IsValid(Planet)
		|| !IsValid(SatelliteRuntime))
	{
		OutFailure = TEXT("LandingPreviewDependenciesInvalid");
		return false;
	}
	if (CurrentLandingSubjectIndex == 0)
	{
		FABTSStylizedRenderingControl::SetProfile(
			EABTSStylizedRenderProfile::GroundDay);
		PreviewCamera->UpdatePreview(GroundPreview, *Planet, 1.0f);
	}
	else
	{
		FABTSStylizedRenderingControl::SetProfile(
			EABTSStylizedRenderProfile::SatelliteGuide);
		PreviewCamera->UpdateSatellitePreview(
			SatellitePreview,
			*SatelliteRuntime->GetRuntimeSatellite(),
			*SatelliteRuntime->GetRuntimeE5Target(),
			1.0f);
	}
	if (!PreviewCamera->IsPreviewActive())
	{
		OutFailure = TEXT("LandingPreviewDidNotActivate");
		return false;
	}
	if (!IsValid(PreviewCamera->GetRenderTarget()))
	{
		OutFailure = TEXT("LandingPreviewRenderTargetMissing");
		return false;
	}
	return true;
}

bool UABTSToonT2C1CaptureSubsystem::CaptureCurrentArtifact(
	FString& OutFailure)
{
	if (Config.Slice == EABTSToonT2C1CaptureSlice::FinaleRemotePreview)
	{
		if (!IsValid(FinaleInteraction)
			|| !IsValid(FinaleInteraction->GetTargetPreviewRenderTarget()))
		{
			OutFailure = TEXT("FinaleRemotePreviewLostBeforeReadback");
			return false;
		}
		return SaveRenderTarget(
			*FinaleInteraction->GetTargetPreviewRenderTarget(),
			TEXT("FinaleRemotePreview"),
			TEXT("FinaleRemotePreview"),
			FinaleInteraction->GetPreviewPlaybackPlan().PlanHash,
			FinaleInteraction->GetTargetCaptureCount(),
			OutFailure);
	}

	if (!IsValid(PreviewCamera)
		|| !PreviewCamera->IsPreviewActive()
		|| !IsValid(PreviewCamera->GetRenderTarget()))
	{
		OutFailure = TEXT("LandingPreviewLostBeforeReadback");
		return false;
	}
	const bool bGround = CurrentLandingSubjectIndex == 0;
	return SaveRenderTarget(
		*PreviewCamera->GetRenderTarget(),
		bGround ? TEXT("GroundLandingPreview") : TEXT("SatelliteLandingPreview"),
		bGround ? TEXT("GroundLandingPreview") : TEXT("SatelliteLandingPreview"),
		FABTSToonT2C1PreviewFixtureBuilder::ComputeFixtureHash(
			bGround ? GroundPreview : SatellitePreview),
		0,
		OutFailure);
}

bool UABTSToonT2C1CaptureSubsystem::SaveRenderTarget(
	UTextureRenderTarget2D& RenderTarget,
	const FString& Subject,
	const FString& ViewClass,
	const uint64 FixtureHash,
	const uint64 RuntimeCaptureRevision,
	FString& OutFailure)
{
	FTextureRenderTargetResource* Resource =
		RenderTarget.GameThread_GetRenderTargetResource();
	TArray<FColor> Pixels;
	FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
	ReadFlags.SetLinearToGamma(false);
	const FIntPoint Size(RenderTarget.SizeX, RenderTarget.SizeY);
	if (Resource == nullptr
		|| Size.X <= 0
		|| Size.Y <= 0
		|| !Resource->ReadPixels(Pixels, ReadFlags)
		|| Pixels.Num() != Size.X * Size.Y)
	{
		OutFailure = TEXT("RenderTargetReadbackFailed");
		return false;
	}
	const FString Filename = FString::Printf(
		TEXT("%02d_%s_%s.png"),
		Records.Num() + 1,
		*Subject,
		*ABTSToonT2C1CaptureSubsystemPrivate::StyleLabel(Config.bStylized));
	const FString ArtifactPath = FPaths::Combine(
		Config.OutputDirectory,
		Filename);
	const FImageView Image(
		Pixels.GetData(),
		Size.X,
		Size.Y,
		EGammaSpace::sRGB);
	if (!FImageUtils::SaveImageByExtension(*ArtifactPath, Image, 100))
	{
		OutFailure = TEXT("PngWriteFailed");
		return false;
	}
	FABTSToonT2C1CaptureRecord& Record = Records.AddDefaulted_GetRef();
	Record.Subject = Subject;
	Record.ViewClass = ViewClass;
	Record.ArtifactPath = ArtifactPath;
	const FMD5Hash ArtifactHash = FMD5Hash::HashFile(*ArtifactPath);
	if (!ArtifactHash.IsValid())
	{
		OutFailure = TEXT("PngHashFailed");
		return false;
	}
	Record.ArtifactMD5 = LexToString(ArtifactHash);
	Record.Resolution = Size;
	Record.FixtureHash = FixtureHash;
	Record.RuntimeCaptureRevision = RuntimeCaptureRevision;
	WriteManifest(TEXT("Running"), FString());
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][Rendering][T2-C1] Artifact Subject=%s ViewClass=%s Stylized=%d Size=%dx%d Fixture=%s Revision=%llu MD5=%s Path=%s"),
		*Subject,
		*ViewClass,
		Config.bStylized ? 1 : 0,
		Size.X,
		Size.Y,
		*ABTSToonT2C1CaptureSubsystemPrivate::Hex64(FixtureHash),
		RuntimeCaptureRevision,
		*Record.ArtifactMD5,
		*ArtifactPath);
	return true;
}

void UABTSToonT2C1CaptureSubsystem::Finish(
	const bool bSuccess,
	const FString& Reason)
{
	if (Phase == EABTSToonT2C1CapturePhase::Terminal)
	{
		return;
	}
	Phase = EABTSToonT2C1CapturePhase::Terminal;
	WriteManifest(bSuccess ? TEXT("Succeeded") : TEXT("Failed"), Reason);
	RestoreRuntimeState();
	if (bSuccess)
	{
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][Rendering][T2-C1][Terminal] Success=1 Slice=%s Stylized=%d Records=%d M7AdapterReady=%d Reason=%s"),
			FABTSToonT2C1PreviewFixtureBuilder::LexToString(Config.Slice),
			Config.bStylized ? 1 : 0,
			Records.Num(),
			bM7AdapterReady ? 1 : 0,
			*Reason);
	}
	else
	{
		UE_LOG(
			LogABTSRuntime,
			Error,
			TEXT("[ABTS][Rendering][T2-C1][Terminal] Success=0 Slice=%s Stylized=%d Records=%d M7AdapterReady=%d Reason=%s"),
			FABTSToonT2C1PreviewFixtureBuilder::LexToString(Config.Slice),
			Config.bStylized ? 1 : 0,
			Records.Num(),
			bM7AdapterReady ? 1 : 0,
			*Reason);
	}
	if (Config.bExitWhenComplete)
	{
		FGenericPlatformMisc::RequestExit(false);
	}
}

void UABTSToonT2C1CaptureSubsystem::RestoreRuntimeState()
{
	if (IsValid(PreviewCamera))
	{
		PreviewCamera->DeactivatePreview();
		PreviewCamera->Destroy();
		PreviewCamera = nullptr;
	}
	if (bRuntimeStateCaptured)
	{
		FABTSStylizedRenderingControl::SetProfile(SavedStyleProfile);
		FABTSStylizedRenderingControl::SetEnabled(bSavedStyleEnabled);
		bRuntimeStateCaptured = false;
	}
	if (bSavedScreenPercentage)
	{
		if (IConsoleVariable* ScreenPercentage =
			IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
		{
			ScreenPercentage->Set(
				SavedScreenPercentage,
				ECVF_SetByCommandline);
		}
		bSavedScreenPercentage = false;
	}
}

bool UABTSToonT2C1CaptureSubsystem::WriteManifest(
	const TCHAR* Status,
	const FString& Reason) const
{
	if (Config.OutputDirectory.IsEmpty())
	{
		return false;
	}
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(
		TEXT("contractVersion"),
		FABTSToonT2C1CaptureConfig::ContractVersion);
	Root->SetStringField(TEXT("status"), Status);
	Root->SetStringField(TEXT("reason"), Reason);
	Root->SetStringField(TEXT("buildIdentity"), Config.BuildIdentity);
	Root->SetStringField(
		TEXT("slice"),
		FABTSToonT2C1PreviewFixtureBuilder::LexToString(Config.Slice));
	Root->SetBoolField(TEXT("stylizedEnabled"), Config.bStylized);
	Root->SetNumberField(
		TEXT("stylizedImplementationVersion"),
		FABTSStylizedRenderingControl::GetImplementationVersion());
	Root->SetNumberField(TEXT("expectedWorldSeed"), Config.ExpectedWorldSeed);
	Root->SetNumberField(TEXT("actualWorldSeed"), ActualWorldSeed);
	Root->SetBoolField(TEXT("m7AdapterReady"), bM7AdapterReady);
	Root->SetStringField(TEXT("authority"), TEXT("PreviewTest"));
	const IConsoleVariable* ScreenPercentage =
		IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
	Root->SetNumberField(TEXT("screenPercentage"), Config.ScreenPercentage);
	Root->SetNumberField(
		TEXT("screenPercentageCVar"),
		ScreenPercentage != nullptr ? ScreenPercentage->GetFloat() : -1.0f);
	TArray<TSharedPtr<FJsonValue>> JsonRecords;
	for (const FABTSToonT2C1CaptureRecord& Record : Records)
	{
		TSharedRef<FJsonObject> JsonRecord = MakeShared<FJsonObject>();
		JsonRecord->SetStringField(TEXT("subject"), Record.Subject);
		JsonRecord->SetStringField(TEXT("viewClass"), Record.ViewClass);
		JsonRecord->SetStringField(TEXT("authority"), Record.Authority);
		JsonRecord->SetStringField(TEXT("artifactPath"), Record.ArtifactPath);
		JsonRecord->SetStringField(TEXT("artifactMD5"), Record.ArtifactMD5);
		JsonRecord->SetNumberField(TEXT("width"), Record.Resolution.X);
		JsonRecord->SetNumberField(TEXT("height"), Record.Resolution.Y);
		JsonRecord->SetStringField(
			TEXT("fixtureHash"),
			ABTSToonT2C1CaptureSubsystemPrivate::Hex64(Record.FixtureHash));
		JsonRecord->SetStringField(
			TEXT("runtimeCaptureRevision"),
			FString::Printf(TEXT("%llu"), Record.RuntimeCaptureRevision));
		JsonRecords.Add(MakeShared<FJsonValueObject>(JsonRecord));
	}
	Root->SetArrayField(TEXT("records"), JsonRecords);
	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		return false;
	}
	return FFileHelper::SaveStringToFile(
		Json,
		*FPaths::Combine(Config.OutputDirectory, TEXT("manifest.json")),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void UABTSToonT2C1CaptureSubsystem::Deinitialize()
{
	RestoreRuntimeState();
	Super::Deinitialize();
}

TStatId UABTSToonT2C1CaptureSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(
		UABTSToonT2C1CaptureSubsystem,
		STATGROUP_Tickables);
}

bool UABTSToonT2C1CaptureSubsystem::IsTickable() const
{
	return Phase != EABTSToonT2C1CapturePhase::Inactive
		&& Phase != EABTSToonT2C1CapturePhase::Terminal;
}

bool UABTSToonT2C1CaptureSubsystem::DoesSupportWorldType(
	const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}
