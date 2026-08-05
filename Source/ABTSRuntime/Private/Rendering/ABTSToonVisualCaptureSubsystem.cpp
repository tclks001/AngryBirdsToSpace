// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/ABTSToonVisualCaptureSubsystem.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM73StableBuildingActor.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Contracts/ABTSWorldGenerationContracts.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "ImageUtils.h"
#include "Scalability.h"
#include "EngineUtils.h"
#include "Game/ABTSM11GameMode.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Misc/SecureHash.h"
#include "Party/ABTSBirdParty.h"
#include "PCG/ABTSM3MonthlySatellitePracticeRuntime.h"
#include "Rendering/ABTSStylizedRenderingControl.h"
#include "DynamicRHI.h"
#include "GPUProfiler.h"
#include "RHIGlobals.h"
#include "RHIShaderPlatform.h"
#include "RHIStrings.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Slingshot/ABTSM6SlingshotSystem.h"
#include "Terrain/ABTSM3Planet.h"
#include "UnrealClient.h"
#include "World/ABTSM11FinaleInteractionSystem.h"
#include "World/ABTSM11FinaleActors.h"
#include "World/ABTSM11FinaleSystem.h"
#include "World/ABTSM51WorldActors.h"
#include "World/ABTSM51WorldSystem.h"

namespace ABTSToonVisualCaptureSubsystemPrivate
{
	constexpr TCHAR RequiredMapSuffix[] = TEXT("L_ABTS_M11");
	constexpr double ScreenshotFileGraceSeconds = 8.0;
	constexpr int32 GPUPostProfileSettleFrames = 30;
	constexpr double GPUPostProfileSettleSeconds = 1.0;

	template <typename ActorType>
	void GatherActors(UWorld& World, TArray<ActorType*>& OutActors)
	{
		OutActors.Reset();
		for (TActorIterator<ActorType> It(&World); It; ++It)
		{
			if (IsValid(*It))
			{
				OutActors.Add(*It);
			}
		}
	}

	uint64 Mix64(uint64 Seed, uint64 Value)
	{
		Value += 0x9e3779b97f4a7c15ull;
		Value = (Value ^ (Value >> 30)) * 0xbf58476d1ce4e5b9ull;
		Value = (Value ^ (Value >> 27)) * 0x94d049bb133111ebull;
		Value ^= Value >> 31;
		const uint64 Result = Seed ^ (Value + (Seed << 6) + (Seed >> 2));
		return Result != 0 ? Result : 1;
	}

	FString Hex64(uint64 Value)
	{
		return FString::Printf(TEXT("0x%016llX"), Value);
	}

	FString MD5ToString(const FMD5Hash& Hash)
	{
		if (!Hash.IsValid())
		{
			return FString();
		}
		FString Result;
		Result.Reserve(32);
		for (int32 Index = 0; Index < Hash.GetSize(); ++Index)
		{
			Result += FString::Printf(TEXT("%02x"), Hash.GetBytes()[Index]);
		}
		return Result;
	}

	bool ReadPngDimensions(const FString& Filename, FIntPoint& OutDimensions)
	{
		OutDimensions = FIntPoint::ZeroValue;
		FImage DecodedImage;
		if (!FImageUtils::LoadImage(*Filename, DecodedImage)
			|| DecodedImage.SizeX <= 0
			|| DecodedImage.SizeY <= 0
			|| DecodedImage.NumSlices != 1
			|| DecodedImage.RawData.IsEmpty())
		{
			return false;
		}
		OutDimensions = FIntPoint(
			DecodedImage.SizeX,
			DecodedImage.SizeY);
		return true;
	}

	FString ReadConsoleVariableIdentity(const TCHAR* Name)
	{
		const IConsoleVariable* Variable =
			IConsoleManager::Get().FindConsoleVariable(Name);
		return Variable != nullptr
			? Variable->GetString()
			: TEXT("Unavailable");
	}

	TSharedRef<FJsonObject> VectorToJson(const FVector& Value)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("x"), Value.X);
		Json->SetNumberField(TEXT("y"), Value.Y);
		Json->SetNumberField(TEXT("z"), Value.Z);
		return Json;
	}

	TSharedRef<FJsonObject> TransformToJson(const FTransform& Value)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetObjectField(
			TEXT("location"),
			VectorToJson(Value.GetLocation()));
		const FQuat Rotation = Value.GetRotation().GetNormalized();
		TSharedRef<FJsonObject> RotationJson = MakeShared<FJsonObject>();
		RotationJson->SetNumberField(TEXT("x"), Rotation.X);
		RotationJson->SetNumberField(TEXT("y"), Rotation.Y);
		RotationJson->SetNumberField(TEXT("z"), Rotation.Z);
		RotationJson->SetNumberField(TEXT("w"), Rotation.W);
		Json->SetObjectField(TEXT("rotation"), RotationJson);
		return Json;
	}

	void AddBoundsPoint(FBox& Bounds, const FVector& Point, const FVector& Extent)
	{
		Bounds += Point - Extent;
		Bounds += Point + Extent;
	}

	void AddOrientedBounds(
		FBox& Bounds,
		const FVector& Center,
		const FVector& Forward,
		const FVector& Right,
		const FVector& Up,
		double HalfForwardCM,
		double HalfRightCM,
		double HalfHeightCM)
	{
		for (int32 ForwardSign : { -1, 1 })
		{
			for (int32 RightSign : { -1, 1 })
			{
				for (int32 UpSign : { -1, 1 })
				{
					Bounds += Center
						+ Forward * HalfForwardCM * ForwardSign
						+ Right * HalfRightCM * RightSign
						+ Up * HalfHeightCM * UpSign;
				}
			}
		}
	}

	const TCHAR* LexToString(
		EABTSM51OrdinarySlingshotSlotSnapshotAuthority Authority)
	{
		switch (Authority)
		{
		case EABTSM51OrdinarySlingshotSlotSnapshotAuthority::AcceptedMonthly:
			return TEXT("AcceptedMonthly");
		case EABTSM51OrdinarySlingshotSlotSnapshotAuthority::PreviewTest:
			return TEXT("PreviewTest");
		case EABTSM51OrdinarySlingshotSlotSnapshotAuthority::None:
		default:
			return TEXT("None");
		}
	}

	const TCHAR* LexToString(EABTSM51FinaleFrameAuthority Authority)
	{
		return Authority == EABTSM51FinaleFrameAuthority::PreviewTest
			? TEXT("PreviewTest")
			: TEXT("None");
	}

	FVector MakeStablePlanarDirection(
		const FVector& Candidate,
		const FVector& Normal,
		const FVector& Fallback)
	{
		FVector Result = FVector::VectorPlaneProject(
			Candidate,
			Normal).GetSafeNormal();
		if (Result.IsNearlyZero())
		{
			Result = FVector::VectorPlaneProject(
				Fallback,
				Normal).GetSafeNormal();
		}
		if (Result.IsNearlyZero())
		{
			FVector AxisA;
			FVector AxisB;
			Normal.GetSafeNormal().FindBestAxisVectors(AxisA, AxisB);
			Result = AxisA;
		}
		return Result;
	}
}

bool UABTSToonVisualCaptureSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const TCHAR* CommandLine = FCommandLine::Get();
	FString Suite;
	const bool bExplicitlyRequested =
		FParse::Param(CommandLine, TEXT("ABTSToonT0Capture"))
		|| (FParse::Value(
			CommandLine,
			TEXT("ABTSVisualCaptureSuite="),
			Suite)
			&& Suite.Equals(TEXT("ToonT0"), ESearchCase::IgnoreCase));
	return bExplicitlyRequested && Super::ShouldCreateSubsystem(Outer);
}

void UABTSToonVisualCaptureSubsystem::Initialize(
	FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UABTSToonVisualCaptureSubsystem::Deinitialize()
{
	if (Phase != EABTSToonVisualCapturePhase::Inactive
		&& Phase != EABTSToonVisualCapturePhase::Terminal)
	{
		if (!WriteManifest(
			TEXT("Aborted"),
			TEXT("World subsystem deinitialized before capture completed.")))
		{
			UE_LOG(
				LogABTSRuntime,
				Error,
				TEXT("[ABTS][ToonT0][Manifest] Failed to write deinitialization evidence."));
		}
	}
	RestoreRuntimeState();
	Super::Deinitialize();
}

void UABTSToonVisualCaptureSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	FString ParseFailure;
	if (!FABTSToonVisualCaptureRunConfig::Parse(
		FCommandLine::Get(),
		RunConfig,
		&ParseFailure))
	{
		RunConfig.bEnabled = true;
		BeginCapture(InWorld);
		FinishCapture(false, ParseFailure);
		return;
	}
	if (RunConfig.bEnabled)
	{
		BeginCapture(InWorld);
	}
}

void UABTSToonVisualCaptureSubsystem::Tick(float DeltaTime)
{
	(void)DeltaTime;
	if (Phase == EABTSToonVisualCapturePhase::Inactive
		|| Phase == EABTSToonVisualCapturePhase::Terminal)
	{
		return;
	}

	const double Now = FPlatformTime::Seconds();
	if (Now - CaptureStartRealSeconds > RunConfig.TimeoutSeconds)
	{
		FinishCapture(
			false,
			FString::Printf(
				TEXT("Timed out after %.1f seconds in phase %d."),
				RunConfig.TimeoutSeconds,
				static_cast<int32>(Phase)));
		return;
	}

	switch (Phase)
	{
	case EABTSToonVisualCapturePhase::WaitingForWorld:
	{
		FString ResolveReason;
		const EWorldResolveResult ResolveResult =
			TryResolveWorldAndCapturePoints(ResolveReason);
		if (ResolveResult == EWorldResolveResult::Failed)
		{
			FinishCapture(false, ResolveReason);
		}
		else if (ResolveResult == EWorldResolveResult::Ready)
		{
			FString CameraFailure;
			if (!PrepareCaptureCamera(CameraFailure))
			{
				FinishCapture(false, CameraFailure);
			}
			else
			{
				BeginCurrentVariant();
			}
		}
		break;
	}
	case EABTSToonVisualCapturePhase::WarmingCamera:
		if (RemainingWarmupFrames > 0)
		{
			--RemainingWarmupFrames;
		}
		else
		{
			FString CameraFailure;
			if (!ValidateEffectiveCamera(CameraFailure))
			{
				FinishCapture(false, CameraFailure);
			}
			else if (RunConfig.Mode
				== EABTSToonVisualCaptureMode::Screenshots)
			{
				RequestCurrentScreenshot();
			}
			else
			{
				DispatchCurrentGPUProfile();
			}
		}
		break;
	case EABTSToonVisualCapturePhase::WaitingForScreenshot:
		if (bScreenshotProcessed
			&& IFileManager::Get().FileExists(*ActiveScreenshotPath))
		{
			CompleteCurrentScreenshot();
		}
		else if (bScreenshotProcessed
			&& Now - ScreenshotProcessedRealSeconds
				> ABTSToonVisualCaptureSubsystemPrivate::ScreenshotFileGraceSeconds)
		{
			FinishCapture(
				false,
				FString::Printf(
					TEXT("Screenshot callback completed but file was not written: %s"),
					*ActiveScreenshotPath));
		}
		break;
	case EABTSToonVisualCapturePhase::CoolingGPUProfile:
		if (UE::RHI::GPUProfiler::IsProfiling())
		{
			GPUProfileFalseObservedRealSeconds = 0.0;
			RemainingGPUCooldownFrames =
				ABTSToonVisualCaptureSubsystemPrivate::
					GPUPostProfileSettleFrames;
		}
		else if (GPUProfileFalseObservedRealSeconds <= 0.0)
		{
			GPUProfileFalseObservedRealSeconds = Now;
		}
		else if (RemainingGPUCooldownFrames > 0)
		{
			--RemainingGPUCooldownFrames;
		}
		else if (Now - GPUProfileFalseObservedRealSeconds
			>= ABTSToonVisualCaptureSubsystemPrivate::
				GPUPostProfileSettleSeconds)
		{
			CompleteCurrentGPUProfile();
		}
		break;
	default:
		break;
	}
}

TStatId UABTSToonVisualCaptureSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(
		UABTSToonVisualCaptureSubsystem,
		STATGROUP_Tickables);
}

bool UABTSToonVisualCaptureSubsystem::IsTickable() const
{
	return Phase != EABTSToonVisualCapturePhase::Inactive
		&& Phase != EABTSToonVisualCapturePhase::Terminal;
}

bool UABTSToonVisualCaptureSubsystem::DoesSupportWorldType(
	const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game
		|| WorldType == EWorldType::PIE
		|| WorldType == EWorldType::GamePreview;
}

void UABTSToonVisualCaptureSubsystem::BeginCapture(UWorld& World)
{
	CaptureStartRealSeconds = FPlatformTime::Seconds();
	Phase = EABTSToonVisualCapturePhase::WaitingForWorld;
	RunId = FString::Printf(
		TEXT("%s_%s_%u"),
		FABTSToonVisualCaptureMath::LexToString(RunConfig.Mode),
		*FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ")),
		FPlatformProcess::GetCurrentProcessId());

	FString Root = RunConfig.OutputDirectory;
	if (Root.IsEmpty())
	{
		Root = FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("ABTSVisualCaptures"),
			TEXT("ToonT0"));
	}
	else if (FPaths::IsRelative(Root))
	{
		Root = FPaths::Combine(FPaths::ProjectSavedDir(), Root);
	}
	OutputDirectory = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(Root, RunId));
	IFileManager& FileManager = IFileManager::Get();
	if (!FileManager.MakeDirectory(*OutputDirectory, true)
		|| !FileManager.DirectoryExists(*OutputDirectory))
	{
		FinishCapture(
			false,
			FString::Printf(
				TEXT("Unable to create capture output directory: %s"),
				*OutputDirectory));
		return;
	}

	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][ToonT0][Begin] Map=%s Mode=%s Build=%s ExpectedSeed=%d Resolution=%dx%d ExactResolution=%d GPUSamples=%d Output=%s"),
		*World.GetMapName(),
		FABTSToonVisualCaptureMath::LexToString(RunConfig.Mode),
		*RunConfig.BuildIdentity,
		RunConfig.ExpectedWorldSeed,
		RunConfig.ExpectedResolutionX,
		RunConfig.ExpectedResolutionY,
		RunConfig.bRequireExactResolution ? 1 : 0,
		RunConfig.GPUProfileSamplesPerVariant,
		*OutputDirectory);
	if (!WriteManifest(TEXT("WaitingForWorld"), FString()))
	{
		FinishCapture(
			false,
			FString::Printf(
				TEXT("Unable to write initial capture manifest: %s"),
				*OutputDirectory));
	}
}

UABTSToonVisualCaptureSubsystem::EWorldResolveResult
UABTSToonVisualCaptureSubsystem::TryResolveWorldAndCapturePoints(
	FString& OutReason)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		OutReason = TEXT("World is unavailable.");
		return EWorldResolveResult::Waiting;
	}
	if (!World->GetMapName().EndsWith(
		ABTSToonVisualCaptureSubsystemPrivate::RequiredMapSuffix))
	{
		OutReason = FString::Printf(
			TEXT("T0 requires L_ABTS_M11; current map is %s."),
			*World->GetMapName());
		return EWorldResolveResult::Failed;
	}

	TArray<AABTSM3Planet*> Planets;
	ABTSToonVisualCaptureSubsystemPrivate::GatherActors(*World, Planets);
	if (Planets.Num() == 0 || !Planets[0]->IsM3PresentationReady())
	{
		OutReason = TEXT("Waiting for the accepted M3 presentation planet.");
		return EWorldResolveResult::Waiting;
	}
	if (Planets.Num() != 1)
	{
		OutReason = FString::Printf(
			TEXT("Expected one M3 planet, found %d."),
			Planets.Num());
		return EWorldResolveResult::Failed;
	}
	AABTSM3Planet& Planet = *Planets[0];
	if (!Planet.IsMonthlyPresentationPreviewActive()
		|| Planet.GetMonthlyPresentationPreviewCandidateId() == INDEX_NONE
		|| Planet.GetMonthlyPresentationPreviewCandidateHash() == 0)
	{
		OutReason = TEXT("T0 requires one explicitly identified M3 Preview/Test presentation candidate.");
		return EWorldResolveResult::Failed;
	}
	MonthlyPresentationCandidateId =
		Planet.GetMonthlyPresentationPreviewCandidateId();
	MonthlyPresentationCandidateHash =
		Planet.GetMonthlyPresentationPreviewCandidateHash();

	FABTSBuildingGenerationContract BuildingContract;
	FABTSFinaleWorldContract FinaleWorldContract;
	if (!Planet.TryExportBuildingGenerationContract(BuildingContract)
		|| !BuildingContract.IsUsable()
		|| !Planet.TryExportFinaleWorldContract(FinaleWorldContract)
		|| !FinaleWorldContract.IsUsable())
	{
		OutReason = TEXT("Accepted M3 world contracts are not exportable.");
		return EWorldResolveResult::Failed;
	}
	if (BuildingContract.Identity.WorldSeed != RunConfig.ExpectedWorldSeed)
	{
		OutReason = FString::Printf(
			TEXT("Seed mismatch: expected %d, actual %d. T0 never rewrites a generated world."),
			RunConfig.ExpectedWorldSeed,
			BuildingContract.Identity.WorldSeed);
		return EWorldResolveResult::Failed;
	}
	if (BuildingContract.Identity.WorldSeed
			!= FinaleWorldContract.Identity.WorldSeed
		|| BuildingContract.Identity.GeneratorVersion
			!= FinaleWorldContract.Identity.GeneratorVersion
		|| BuildingContract.Identity.GenerationAttempt
			!= FinaleWorldContract.Identity.GenerationAttempt)
	{
		OutReason = TEXT("M7 and M11 world-contract identities disagree.");
		return EWorldResolveResult::Failed;
	}

	TArray<AABTSBirdParty*> Parties;
	ABTSToonVisualCaptureSubsystemPrivate::GatherActors(*World, Parties);
	if (Parties.Num() == 0 || !Parties[0]->IsPartyReady())
	{
		OutReason = TEXT("Waiting for the four-bird party.");
		return EWorldResolveResult::Waiting;
	}
	if (Parties.Num() != 1 || !IsValid(Parties[0]->GetControlledBird()))
	{
		OutReason = FString::Printf(
			TEXT("Expected one ready bird party, found %d."),
			Parties.Num());
		return EWorldResolveResult::Failed;
	}

	TArray<AABTSM6SlingshotSystem*> SlingshotSystems;
	ABTSToonVisualCaptureSubsystemPrivate::GatherActors(
		*World,
		SlingshotSystems);
	if (SlingshotSystems.Num() == 0
		|| !SlingshotSystems[0]->IsStartupPhysicsWarmupComplete())
	{
		OutReason = TEXT("Waiting for M6 startup physics WorldReady.");
		return EWorldResolveResult::Waiting;
	}
	if (SlingshotSystems.Num() != 1)
	{
		OutReason = FString::Printf(
			TEXT("Expected one M6 slingshot system, found %d."),
			SlingshotSystems.Num());
		return EWorldResolveResult::Failed;
	}

	TArray<AABTSM73StableBuildingActor*> Buildings;
	ABTSToonVisualCaptureSubsystemPrivate::GatherActors(*World, Buildings);
	TArray<AABTSM73StableBuildingActor*> AcceptedBuildings;
	for (AABTSM73StableBuildingActor* Building : Buildings)
	{
		switch (Building->GetIdleValidationState())
		{
		case EABTSM73IdleValidationState::Pending:
		case EABTSM73IdleValidationState::Running:
			OutReason = TEXT("Waiting for M7 idle validation.");
			return EWorldResolveResult::Waiting;
		case EABTSM73IdleValidationState::Rejected:
			OutReason = FString::Printf(
				TEXT("M7 building was rejected: %s."),
				*Building->GetName());
			return EWorldResolveResult::Failed;
		case EABTSM73IdleValidationState::Accepted:
			AcceptedBuildings.Add(Building);
			break;
		case EABTSM73IdleValidationState::NotRequired:
		default:
			break;
		}
	}
	if (AcceptedBuildings.IsEmpty())
	{
		OutReason = TEXT("No accepted M7 building is available.");
		return EWorldResolveResult::Waiting;
	}

	TArray<AABTSM3MonthlySatellitePracticeRuntime*> SatelliteRuntimes;
	ABTSToonVisualCaptureSubsystemPrivate::GatherActors(
		*World,
		SatelliteRuntimes);
	if (SatelliteRuntimes.Num() == 0)
	{
		OutReason = TEXT("Satellite practice runtime is absent. Launch the frozen M3 R5 preview candidate required by T0.");
		return EWorldResolveResult::Failed;
	}
	if (SatelliteRuntimes.Num() != 1)
	{
		OutReason = FString::Printf(
			TEXT("Expected one satellite practice runtime, found %d."),
			SatelliteRuntimes.Num());
		return EWorldResolveResult::Failed;
	}
	AABTSM3MonthlySatellitePracticeRuntime& SatelliteRuntime =
		*SatelliteRuntimes[0];
	if (!SatelliteRuntime.IsRuntimeReady())
	{
		OutReason = TEXT("Waiting for certified satellite/E5 practice runtime.");
		return EWorldResolveResult::Waiting;
	}
	const FABTSM3MonthlySatelliteRuntimeSnapshot& SatelliteSnapshot =
		SatelliteRuntime.GetRuntimeSnapshot();
	if (!SatelliteSnapshot.bValid
		|| !SatelliteSnapshot.bTrajectoryCertified
		|| !SatelliteRuntime.IsPracticePouchInteractionReady())
	{
		OutReason = TEXT("Satellite practice runtime reached an invalid terminal identity.");
		return EWorldResolveResult::Failed;
	}

	TArray<AABTSM51WorldSystem*> WorldSystems;
	ABTSToonVisualCaptureSubsystemPrivate::GatherActors(
		*World,
		WorldSystems);
	if (WorldSystems.Num() != 1)
	{
		OutReason = FString::Printf(
			TEXT("Expected one M5.1 world system, found %d."),
			WorldSystems.Num());
		return EWorldResolveResult::Failed;
	}
	AABTSM51WorldSystem& WorldSystem = *WorldSystems[0];
	const EABTSM51OrdinarySlingshotSlotSnapshotAuthority SlotAuthority =
		WorldSystem.GetOrdinarySlotSnapshotAuthority();
	if (SlotAuthority
		!= EABTSM51OrdinarySlingshotSlotSnapshotAuthority::PreviewTest)
	{
		OutReason = FString::Printf(
			TEXT("T0 requires M5.1 Preview/Test slot authority; actual=%s."),
			ABTSToonVisualCaptureSubsystemPrivate::LexToString(
				SlotAuthority));
		return EWorldResolveResult::Failed;
	}
	const FABTSM51PreviewFinaleFrameContext* PreviewFinaleContext =
		WorldSystem.GetPreviewFinaleFrameContext();
	if (PreviewFinaleContext == nullptr
		|| !PreviewFinaleContext->IsUsable()
		|| PreviewFinaleContext->Authority
			!= EABTSM51FinaleFrameAuthority::PreviewTest
		|| PreviewFinaleContext->bMonthlyWorldAccepted)
	{
		OutReason = TEXT("M5.1 Preview/Test finale-frame context is absent, unusable, or claims monthly authority.");
		return EWorldResolveResult::Failed;
	}
	OrdinarySlotAuthority = static_cast<int32>(SlotAuthority);
	OrdinaryMaxCordLengthCM =
		WorldSystem.GetActiveOrdinaryMaxCordLengthCM();
	FinaleFrameAuthority =
		static_cast<int32>(PreviewFinaleContext->Authority);
	FinaleFrameSourceCandidateId =
		PreviewFinaleContext->SourceRouteCandidateId;
	FinaleFrameSpatialCandidateHash =
		PreviewFinaleContext->SourceSpatialCandidateHash;
	FinaleFramePlanResultHash =
		PreviewFinaleContext->SourcePlanResultHash;
	FinaleFramePreviewHash = PreviewFinaleContext->SourcePreviewHash;
	FinaleFrameContextHash = PreviewFinaleContext->ContextHash;
	bFinaleFrameMonthlyWorldAccepted =
		PreviewFinaleContext->bMonthlyWorldAccepted;

	AABTSM11GameMode* GameMode = Cast<AABTSM11GameMode>(
		World->GetAuthGameMode());
	if (GameMode == nullptr)
	{
		OutReason = TEXT("Authoritative GameMode is not ABTSM11GameMode.");
		return EWorldResolveResult::Failed;
	}
	AABTSM11FinaleSystem* FinaleSystem = GameMode->GetFinaleSystem();
	if (!IsValid(FinaleSystem))
	{
		OutReason = TEXT("Waiting for M11 finale layout system.");
		return EWorldResolveResult::Waiting;
	}
	if (FinaleSystem->GetSystemState() == EABTSM11FinaleSystemState::Failed)
	{
		OutReason = FString::Printf(
			TEXT("M11 finale layout failed: %s"),
			*FinaleSystem->GetFailureReason());
		return EWorldResolveResult::Failed;
	}
	if (!FinaleSystem->IsLayoutReady()
		|| FinaleSystem->GetSpawnedAssistActorCount()
			!= AABTSM11FinaleSystem::ExpectedAssistPresentationCount
		|| !FinaleSystem->HasSpawnedUFOActor())
	{
		OutReason = TEXT("Waiting for the complete M11 three-assist/UFO presentation.");
		return EWorldResolveResult::Waiting;
	}

	AABTSM11FinaleInteractionSystem* FinaleInteraction =
		GameMode->GetFinaleInteractionSystem();
	if (!IsValid(FinaleInteraction))
	{
		OutReason = TEXT("Waiting for M11 finale interaction system.");
		return EWorldResolveResult::Waiting;
	}
	const EABTSM11FinaleInteractionState InteractionState =
		FinaleInteraction->GetInteractionState();
	if (InteractionState == EABTSM11FinaleInteractionState::Locked)
	{
		OutReason = TEXT("Waiting for M11 finale interaction readiness.");
		return EWorldResolveResult::Waiting;
	}
	if (InteractionState != EABTSM11FinaleInteractionState::Ready)
	{
		OutReason = FString::Printf(
			TEXT("T0 requires idle M11 interaction state Ready; actual=%d."),
			static_cast<int32>(InteractionState));
		return EWorldResolveResult::Failed;
	}
	FString InteractionFailure;
	if (!AABTSM11FinaleInteractionSystem::ValidateInteractionContract(
		*FinaleSystem,
		&InteractionFailure))
	{
		OutReason = FString::Printf(
			TEXT("M11 interaction contract rejected: %s"),
			*InteractionFailure);
		return EWorldResolveResult::Failed;
	}
	if (SatelliteSnapshot.SourceRouteCandidateId
			!= MonthlyPresentationCandidateId
		|| PreviewFinaleContext->SourceRouteCandidateId
			!= MonthlyPresentationCandidateId)
	{
		OutReason = FString::Printf(
			TEXT("Preview/Test candidate identity split: Presentation=%d Satellite=%d Finale=%d."),
			MonthlyPresentationCandidateId,
			SatelliteSnapshot.SourceRouteCandidateId,
			PreviewFinaleContext->SourceRouteCandidateId);
		return EWorldResolveResult::Failed;
	}
	if (AABTSM11FinaleSystem::ComputeFinaleFrameDiagnosticHash(
			FinaleSystem->GetFinaleFrame())
		!= AABTSM11FinaleSystem::ComputeFinaleFrameDiagnosticHash(
			PreviewFinaleContext->Frame))
	{
		OutReason = TEXT("M5.1 and M11 consumed different finale frames.");
		return EWorldResolveResult::Failed;
	}

	const FIntPoint ActualResolution = GetActualViewportResolution();
	if (ActualResolution.X <= 0 || ActualResolution.Y <= 0)
	{
		OutReason = TEXT("Waiting for the game viewport.");
		return EWorldResolveResult::Waiting;
	}
	if (RunConfig.bRequireExactResolution
		&& ActualResolution
			!= FIntPoint(
				RunConfig.ExpectedResolutionX,
				RunConfig.ExpectedResolutionY))
	{
		OutReason = FString::Printf(
			TEXT("Viewport is %dx%d; T0 requires %dx%d. Relaunch with -Windowed -ForceRes -ResX=%d -ResY=%d or explicitly use -ABTSToonT0AllowAnyResolution for a non-baseline preview."),
			ActualResolution.X,
			ActualResolution.Y,
			RunConfig.ExpectedResolutionX,
			RunConfig.ExpectedResolutionY,
			RunConfig.ExpectedResolutionX,
			RunConfig.ExpectedResolutionY);
		return EWorldResolveResult::Failed;
	}

	const TArray<FABTSToonVisualCapturePointDefinition> Definitions =
		FABTSToonVisualCaptureMath::BuildDefaultCatalogue();
	CaptureCatalogueHash =
		FABTSToonVisualCaptureMath::ComputeCatalogueHash(Definitions);
	ResolvedPoints.Reset();
	ResolvedPoints.Reserve(Definitions.Num());

	FTransform StartTransform;
	int32 StartCellId = INDEX_NONE;
	if (!Planet.GetInitialRoadSpawnTransform(
		100.0f,
		StartTransform,
		StartCellId))
	{
		OutReason = TEXT("Unable to resolve the semantic Start-road frame.");
		return EWorldResolveResult::Failed;
	}

	const FABTSGeneratedBuildingSite* SelectedSite = nullptr;
	double BestSiteScore = TNumericLimits<double>::Max();
	for (const FABTSGeneratedBuildingSite& Site : BuildingContract.Sites)
	{
		if (Site.Purpose != EABTSGeneratedBuildingPurpose::DestructibleTarget)
		{
			continue;
		}
		const double Progress = Site.NormalizedRouteProgress >= 0.0f
			? Site.NormalizedRouteProgress
			: 0.5;
		const double Score = FMath::Abs(Progress - 0.5);
		if (SelectedSite == nullptr
			|| Score < BestSiteScore
			|| (FMath::IsNearlyEqual(Score, BestSiteScore)
				&& Site.SiteId < SelectedSite->SiteId))
		{
			SelectedSite = &Site;
			BestSiteScore = Score;
		}
	}
	if (SelectedSite == nullptr)
	{
		OutReason = TEXT("Building contract has no destructible target site.");
		return EWorldResolveResult::Failed;
	}
	const FVector SelectedSiteLocation =
		SelectedSite->WorldTransform.GetLocation();

	AABTSM73StableBuildingActor* SelectedBuilding = nullptr;
	double BestBuildingDistanceSquared = TNumericLimits<double>::Max();
	for (AABTSM73StableBuildingActor* Building : AcceptedBuildings)
	{
		const double DistanceSquared = FVector::DistSquared(
			Building->GetActorLocation(),
			SelectedSite->WorldTransform.GetLocation());
		if (DistanceSquared < BestBuildingDistanceSquared)
		{
			SelectedBuilding = Building;
			BestBuildingDistanceSquared = DistanceSquared;
		}
	}
	if (SelectedBuilding == nullptr)
	{
		OutReason = TEXT("Unable to match an accepted building to the selected contract site.");
		return EWorldResolveResult::Failed;
	}
	const double MaxBuildingMatchDistanceCM = FMath::Max(
		1500.0,
		FMath::Max(
			static_cast<double>(SelectedSite->PadHalfExtentCM.X),
			static_cast<double>(SelectedSite->PadHalfExtentCM.Y)) * 2.0);
	if (BestBuildingDistanceSquared
		> FMath::Square(MaxBuildingMatchDistanceCM))
	{
		OutReason = FString::Printf(
			TEXT("Nearest accepted building is outside the selected site envelope: DistanceCM=%.2f LimitCM=%.2f Site=%llu Actor=%s."),
			FMath::Sqrt(BestBuildingDistanceSquared),
			MaxBuildingMatchDistanceCM,
			static_cast<unsigned long long>(SelectedSite->SiteId),
			*SelectedBuilding->GetName());
		return EWorldResolveResult::Failed;
	}

	FVector BuildingMarker;
	int32 LiveModuleCount = 0;
	if (!SelectedBuilding->QueryScoutMapMarkerLocation(
		&Planet,
		BuildingMarker,
		LiveModuleCount)
		|| LiveModuleCount <= 0
		|| BuildingMarker.ContainsNaN())
	{
		OutReason = TEXT("Selected building has no live presentation centroid.");
		return EWorldResolveResult::Failed;
	}

	TArray<AABTSM51SlingshotDirtHole*> StandardHoles;
	for (TActorIterator<AABTSM51SlingshotDirtHole> It(World); It; ++It)
	{
		if (IsValid(*It)
			&& (*It)->GetSlotKind() == EABTSSlingshotSlotKind::Standard)
		{
			StandardHoles.Add(*It);
		}
	}
	StandardHoles.Sort(
		[SelectedSiteLocation](
			const AABTSM51SlingshotDirtHole& Left,
			const AABTSM51SlingshotDirtHole& Right)
		{
			const double LeftDistance = FVector::DistSquared(
				Left.GetActorLocation(),
				SelectedSiteLocation);
			const double RightDistance = FVector::DistSquared(
				Right.GetActorLocation(),
				SelectedSiteLocation);
			if (!FMath::IsNearlyEqual(LeftDistance, RightDistance))
			{
				return LeftDistance < RightDistance;
			}
			return Left.GetCellId() < Right.GetCellId();
		});
	if (StandardHoles.Num() < 2)
	{
		OutReason = FString::Printf(
			TEXT("Building capture requires two standard slingshot slots; found %d."),
			StandardHoles.Num());
		return EWorldResolveResult::Failed;
	}
	TArray<int32> StandardHoleCellIds;
	StandardHoleCellIds.Reserve(StandardHoles.Num());
	for (const AABTSM51SlingshotDirtHole* Hole : StandardHoles)
	{
		StandardHoleCellIds.Add(Hole->GetCellId());
	}
	StandardHoleCellIds.Sort();
	uint64 SlotEvidence =
		ABTSToonVisualCaptureSubsystemPrivate::Mix64(
			static_cast<uint64>(OrdinaryMaxCordLengthCM),
			static_cast<uint64>(StandardHoleCellIds.Num()));
	for (int32 CellId : StandardHoleCellIds)
	{
		SlotEvidence = ABTSToonVisualCaptureSubsystemPrivate::Mix64(
			SlotEvidence,
			static_cast<uint64>(CellId));
	}
	OrdinarySlotEvidenceHash = SlotEvidence;
	if (OrdinaryMaxCordLengthCM <= 0
		|| OrdinarySlotEvidenceHash == 0)
	{
		OutReason = TEXT("M5.1 ordinary-slot evidence is invalid.");
		return EWorldResolveResult::Failed;
	}

	const FABTSM110FinaleLocalFrame& FinaleFrame =
		FinaleSystem->GetFinaleFrame();
	if (!FinaleFrame.IsUsable())
	{
		OutReason = TEXT("Finale local frame is unusable.");
		return EWorldResolveResult::Failed;
	}

	for (const FABTSToonVisualCapturePointDefinition& Definition : Definitions)
	{
		FABTSToonResolvedCapturePoint Point;
		Point.Definition = Definition;
		FString CameraFailure;
		switch (Definition.Anchor)
		{
		case EABTSToonVisualCaptureAnchor::GroundStart:
		{
			const FVector Forward = StartTransform.GetUnitAxis(EAxis::X);
			const FVector Right = StartTransform.GetUnitAxis(EAxis::Y);
			const FVector Up = StartTransform.GetUnitAxis(EAxis::Z);
			Point.LookAtWorld = StartTransform.GetLocation()
				+ Forward * 450.0
				+ Up * 110.0;
			const FVector CameraLocation = Point.LookAtWorld
				- Forward * 1250.0
				+ Right * 650.0
				+ Up * 520.0;
			if (!FABTSToonVisualCaptureMath::BuildLookAtCameraTransform(
				CameraLocation,
				Point.LookAtWorld,
				Up,
				Point.CameraWorldTransform,
				&CameraFailure))
			{
				OutReason = CameraFailure;
				return EWorldResolveResult::Failed;
			}
			Point.SemanticIdentityHash =
				ABTSToonVisualCaptureSubsystemPrivate::Mix64(
					static_cast<uint64>(BuildingContract.Identity.WorldSeed),
					static_cast<uint64>(StartCellId));
			break;
		}
		case EABTSToonVisualCaptureAnchor::SlingshotBuilding:
		{
			const FVector Up =
				SelectedSite->AnchorDirection.GetSafeNormal();
			const FVector Forward =
				ABTSToonVisualCaptureSubsystemPrivate::MakeStablePlanarDirection(
					SelectedSite->TangentForward,
					Up,
					SelectedSite->WorldTransform.GetUnitAxis(EAxis::X));
			const FVector SiteRight = FVector::CrossProduct(
				Up,
				Forward).GetSafeNormal();
			const double HalfForwardCM = FMath::Max(
				static_cast<double>(SelectedSite->PadHalfExtentCM.X),
				300.0);
			const double HalfRightCM = FMath::Max(
				static_cast<double>(SelectedSite->PadHalfExtentCM.Y),
				300.0);
			const double HalfHeightCM = FMath::Max(
				FMath::Max(HalfForwardCM, HalfRightCM) * 1.25,
				600.0);
			const FVector BuildingEnvelopeCenter = SelectedSiteLocation
				+ Up * HalfHeightCM;
			FBox Bounds(EForceInit::ForceInit);
			ABTSToonVisualCaptureSubsystemPrivate::AddOrientedBounds(
				Bounds,
				BuildingEnvelopeCenter,
				Forward,
				SiteRight,
				Up,
				HalfForwardCM,
				HalfRightCM,
				HalfHeightCM);
			for (int32 HoleIndex = 0; HoleIndex < 2; ++HoleIndex)
			{
				ABTSToonVisualCaptureSubsystemPrivate::AddBoundsPoint(
					Bounds,
					StandardHoles[HoleIndex]->GetActorLocation(),
					FVector(120.0));
			}
			Point.LookAtWorld = Bounds.GetCenter();
			const FVector HoleCenter =
				(StandardHoles[0]->GetActorLocation()
					+ StandardHoles[1]->GetActorLocation()) * 0.5;
			const FVector ViewForward =
				ABTSToonVisualCaptureSubsystemPrivate::MakeStablePlanarDirection(
					SelectedSiteLocation - HoleCenter,
					Up,
					Forward);
			const FVector Right = FVector::CrossProduct(
				Up,
				ViewForward).GetSafeNormal();
			const double FitDistance = FMath::Clamp(
				FABTSToonVisualCaptureMath::ComputePerspectiveFitDistanceCM(
					Bounds.GetExtent().Size(),
					Definition.FieldOfViewDegrees,
					static_cast<double>(ActualResolution.X)
						/ ActualResolution.Y,
					1.25),
				1300.0,
				8000.0);
			const FVector CameraLocation = Point.LookAtWorld
				- ViewForward * FitDistance
				+ Right * FitDistance * 0.18
				+ Up * FitDistance * 0.16;
			if (!FABTSToonVisualCaptureMath::BuildLookAtCameraTransform(
				CameraLocation,
				Point.LookAtWorld,
				Up,
				Point.CameraWorldTransform,
				&CameraFailure))
			{
				OutReason = CameraFailure;
				return EWorldResolveResult::Failed;
			}
			uint64 Identity = ABTSToonVisualCaptureSubsystemPrivate::Mix64(
				SelectedSite->SiteId,
				static_cast<uint64>(SelectedSite->DeterministicSeed));
			Identity = ABTSToonVisualCaptureSubsystemPrivate::Mix64(
				Identity,
				static_cast<uint64>(StandardHoles[0]->GetCellId()));
			Point.SemanticIdentityHash =
				ABTSToonVisualCaptureSubsystemPrivate::Mix64(
					Identity,
					static_cast<uint64>(StandardHoles[1]->GetCellId()));
			break;
		}
		case EABTSToonVisualCaptureAnchor::SatelliteE5:
		{
			const FVector SatelliteCenter =
				SatelliteSnapshot.SatelliteWorldTransform.GetLocation();
			const FVector E5Location =
				SatelliteSnapshot.E5WorldTransform.GetLocation();
			const FVector Outward =
				(SatelliteCenter - Planet.GetPlanetCenterWorld()).GetSafeNormal();
			if (Outward.IsNearlyZero())
			{
				OutReason = TEXT("Satellite radial direction is degenerate.");
				return EWorldResolveResult::Failed;
			}
			FBox Bounds(EForceInit::ForceInit);
			ABTSToonVisualCaptureSubsystemPrivate::AddBoundsPoint(
				Bounds,
				SatelliteCenter,
				FVector(SatelliteSnapshot.SatelliteRadiusCM));
			ABTSToonVisualCaptureSubsystemPrivate::AddBoundsPoint(
				Bounds,
				E5Location,
				SatelliteSnapshot.E5HalfExtentCM);
			Point.LookAtWorld = Bounds.GetCenter();
			const FVector SideView =
				ABTSToonVisualCaptureSubsystemPrivate::MakeStablePlanarDirection(
					FinaleFrame.GetForward(),
					Outward,
					SatelliteSnapshot.PracticeLaunchWorldTransform.GetUnitAxis(EAxis::X));
			const double FitDistance = FMath::Max(
				FABTSToonVisualCaptureMath::ComputePerspectiveFitDistanceCM(
					Bounds.GetExtent().Size(),
					Definition.FieldOfViewDegrees,
					static_cast<double>(ActualResolution.X)
						/ ActualResolution.Y,
					1.25),
				SatelliteSnapshot.SatelliteRadiusCM * 3.0);
			const FVector CameraLocation = Point.LookAtWorld
				- SideView * FitDistance
				+ Outward * FitDistance * 0.12;
			if (!FABTSToonVisualCaptureMath::BuildLookAtCameraTransform(
				CameraLocation,
				Point.LookAtWorld,
				Outward,
				Point.CameraWorldTransform,
				&CameraFailure))
			{
				OutReason = CameraFailure;
				return EWorldResolveResult::Failed;
			}
			Point.SemanticIdentityHash =
				ABTSToonVisualCaptureSubsystemPrivate::Mix64(
					static_cast<uint64>(SatelliteSnapshot.RuntimeLayoutSnapshotHash),
					static_cast<uint64>(SatelliteSnapshot.TrajectoryCertificationHash));
			break;
		}
		case EABTSToonVisualCaptureAnchor::FinaleLayout:
		{
			FBox Bounds(EForceInit::ForceInit);
			for (AABTSM11GravityBodyActor* GravityActor
				: FinaleSystem->GetGravityBodyActors())
			{
				if (!IsValid(GravityActor))
				{
					OutReason = TEXT("Finale assist presentation contains an invalid Actor.");
					return EWorldResolveResult::Failed;
				}
				FVector Origin;
				FVector Extent;
				GravityActor->GetActorBounds(false, Origin, Extent, true);
				ABTSToonVisualCaptureSubsystemPrivate::AddBoundsPoint(
					Bounds,
					Origin,
					Extent.ComponentMax(FVector(100.0)));
			}
			AABTSM11UFOActor* UFOActor = FinaleSystem->GetUFOActor();
			if (!IsValid(UFOActor))
			{
				OutReason = TEXT("Finale UFO presentation is invalid.");
				return EWorldResolveResult::Failed;
			}
			FVector UFOOrigin;
			FVector UFOExtent;
			UFOActor->GetActorBounds(false, UFOOrigin, UFOExtent, true);
			ABTSToonVisualCaptureSubsystemPrivate::AddBoundsPoint(
				Bounds,
				UFOOrigin,
				UFOExtent.ComponentMax(FVector(100.0)));
			Bounds += FinaleFrame.GetOrigin();
			Point.LookAtWorld = Bounds.GetCenter();
			const double FitDistance =
				FABTSToonVisualCaptureMath::ComputePerspectiveFitDistanceCM(
					Bounds.GetExtent().Size(),
					Definition.FieldOfViewDegrees,
					static_cast<double>(ActualResolution.X)
						/ ActualResolution.Y,
					1.2);
			const FVector CameraLocation = Point.LookAtWorld
				- FinaleFrame.GetRight() * FitDistance
				+ FinaleFrame.GetUp() * FitDistance * 0.1;
			if (!FABTSToonVisualCaptureMath::BuildLookAtCameraTransform(
				CameraLocation,
				Point.LookAtWorld,
				FinaleFrame.GetUp(),
				Point.CameraWorldTransform,
				&CameraFailure))
			{
				OutReason = CameraFailure;
				return EWorldResolveResult::Failed;
			}
			const FABTSM11FinaleLayoutPreset& Preset =
				FinaleSystem->GetLayoutPreset();
			Point.SemanticIdentityHash =
				ABTSToonVisualCaptureSubsystemPrivate::Mix64(
					Preset.PresetHash,
					Preset.CertifiedBundleHash);
			break;
		}
		default:
			OutReason = TEXT("Capture catalogue contains an unsupported anchor.");
			return EWorldResolveResult::Failed;
		}

		Point.CameraPoseHash =
			FABTSToonVisualCaptureMath::ComputeCameraPoseHash(
				Point.CameraWorldTransform,
				Point.LookAtWorld,
				Definition.FieldOfViewDegrees);
		if (!Point.IsValid())
		{
			OutReason = FString::Printf(
				TEXT("Resolved capture point %s is invalid."),
				*Definition.PointId.ToString());
			return EWorldResolveResult::Failed;
		}
		ResolvedPoints.Add(Point);
	}

	ActualWorldSeed = BuildingContract.Identity.WorldSeed;
	ActualGeneratorVersion = BuildingContract.Identity.GeneratorVersion;
	ActualGenerationAttempt = BuildingContract.Identity.GenerationAttempt;
	bActualSourceWorldAccepted =
		BuildingContract.Identity.bSourceWorldAccepted;
	SatelliteRuntimeLayoutHash =
		SatelliteSnapshot.RuntimeLayoutSnapshotHash;
	SatelliteSourceCandidateId =
		SatelliteSnapshot.SourceRouteCandidateId;
	SatelliteSourcePreviewResultHash =
		SatelliteSnapshot.SourcePreviewResultHash;
	SatelliteSourceCandidateHash =
		SatelliteSnapshot.SourceCandidateHash;
	SatelliteLaunchProfileHash = SatelliteSnapshot.LaunchProfileHash;
	SatelliteProductionLaunchProfileHash =
		SatelliteSnapshot.ProductionLaunchProfileHash;
	SatellitePresetHash = SatelliteSnapshot.SatellitePracticePresetHash;
	SatelliteTrajectoryCertificationHash =
		SatelliteSnapshot.TrajectoryCertificationHash;
	FinalePresetHash = FinaleSystem->GetLayoutPreset().PresetHash;
	FinaleCertifiedBundleHash =
		FinaleSystem->GetLayoutPreset().CertifiedBundleHash;
	bFinaleEditorCandidateMode = FinaleSystem->IsEditorCandidateMode();
	if (bFinaleEditorCandidateMode)
	{
		const FABTSM11CandidateExperienceIdentity& CandidateIdentity =
			FinaleSystem->GetEditorCandidateIdentity();
		if (!CandidateIdentity.IsValid())
		{
			OutReason = TEXT("M11 reports Editor Candidate mode with an invalid identity.");
			return EWorldResolveResult::Failed;
		}
		FinaleEditorCandidateRank = CandidateIdentity.Rank;
		FinaleEditorCandidateSourceHash =
			CandidateIdentity.CandidateSourceHash;
		FinaleEditorCandidateResultHash =
			CandidateIdentity.NominalResultHash;
	}
	OutReason = TEXT("All semantic capture points resolved.");
	return EWorldResolveResult::Ready;
}

bool UABTSToonVisualCaptureSubsystem::PrepareCaptureCamera(
	FString& OutFailure)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		OutFailure = TEXT("World disappeared before camera setup.");
		return false;
	}
	APlayerController* Controller = World->GetFirstPlayerController();
	if (!IsValid(Controller))
	{
		OutFailure = TEXT("Local player controller is unavailable.");
		return false;
	}

	CaptureController = Controller;
	SavedViewTarget = Controller->GetViewTarget();
	bWorldWasPaused = UGameplayStatics::IsGamePaused(World);
	bSavedScreenMessagesEnabled = GAreScreenMessagesEnabled;
	bSavedStyleEnabled = FABTSStylizedRenderingControl::IsEnabled();
	SavedStyleProfile = FABTSStylizedRenderingControl::GetProfile();
	bRuntimeStateCaptured = true;

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = MakeUniqueObjectName(
		World,
		ACameraActor::StaticClass(),
		TEXT("ABTSToonT0CaptureCamera"));
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.ObjectFlags |= RF_Transient;
	CaptureCamera = World->SpawnActor<ACameraActor>(
		ACameraActor::StaticClass(),
		FTransform::Identity,
		SpawnParameters);
	if (!IsValid(CaptureCamera)
		|| CaptureCamera->GetCameraComponent() == nullptr)
	{
		OutFailure = TEXT("Unable to spawn the transient T0 camera.");
		return false;
	}

	CaptureCamera->GetCameraComponent()->SetProjectionMode(
		ECameraProjectionMode::Perspective);
	// T0 captures static comparison frames after teleporting one transient
	// camera between distant semantic anchors. With world time frozen, scene
	// motion-blur history cannot decay after those teleports and would smear a
	// valid still frame. Disable motion blur only on this transient baseline
	// camera; the game's production cameras and render settings are untouched.
	FPostProcessSettings& CapturePostProcess =
		CaptureCamera->GetCameraComponent()->PostProcessSettings;
	CapturePostProcess.bOverride_MotionBlurAmount = true;
	CapturePostProcess.MotionBlurAmount = 0.0f;
	CapturePostProcess.bOverride_MotionBlurMax = true;
	CapturePostProcess.MotionBlurMax = 0.0f;
	CaptureCamera->GetCameraComponent()->SetPostProcessBlendWeight(1.0f);
	Controller->SetViewTarget(CaptureCamera);
	// Keep real HUD/PIP rendering while excluding transient AddOnScreenDebugMessage
	// overlays from the visual baseline.
	GAreScreenMessagesEnabled = false;

	if (RunConfig.bPauseWorldDuringCapture
		&& !bWorldWasPaused
		&& !UGameplayStatics::SetGamePaused(World, true))
	{
		OutFailure = TEXT("Unable to pause the world for identical A/B timing.");
		return false;
	}

	if (RunConfig.Mode == EABTSToonVisualCaptureMode::Screenshots)
	{
		ScreenshotProcessedHandle =
			FScreenshotRequest::OnScreenshotRequestProcessed().AddUObject(
				this,
				&UABTSToonVisualCaptureSubsystem::HandleScreenshotProcessed);
	}
	else
	{
#if WITH_PROFILEGPU
		IConsoleVariable* ShowUI =
			IConsoleManager::Get().FindConsoleVariable(
				TEXT("r.ProfileGPU.ShowUI"));
		if (ShowUI == nullptr)
		{
			OutFailure = TEXT("r.ProfileGPU.ShowUI is unavailable in this build.");
			return false;
		}
		bSavedProfileGPUShowUI = ShowUI->GetBool();
		SavedProfileGPUShowUISetBy =
			static_cast<uint32>(ShowUI->GetFlags())
			& static_cast<uint32>(ECVF_SetByMask);
		bProfileGPUShowUIStateCaptured = true;
		ShowUI->Set(0, ECVF_SetByCode);
		if (ShowUI->GetInt() != 0)
		{
			OutFailure = TEXT("Unable to suppress ProfileGPU UI for an uncontaminated capture sequence.");
			return false;
		}
#else
		OutFailure = TEXT("This build does not support ProfileGPU.");
		return false;
#endif
	}
	return true;
}

void UABTSToonVisualCaptureSubsystem::BeginCurrentVariant()
{
	if (!ResolvedPoints.IsValidIndex(CurrentPointIndex)
		|| !IsValid(CaptureCamera)
		|| CaptureCamera->GetCameraComponent() == nullptr)
	{
		FinishCapture(false, TEXT("Capture variant index or camera is invalid."));
		return;
	}

	const FABTSToonResolvedCapturePoint& Point =
		ResolvedPoints[CurrentPointIndex];
	FABTSStylizedRenderingControl::SetProfile(Point.Definition.StyleProfile);
	FABTSStylizedRenderingControl::SetEnabled(bCurrentStyleEnabled);
	if (FABTSStylizedRenderingControl::GetProfile()
			!= Point.Definition.StyleProfile
		|| FABTSStylizedRenderingControl::IsEnabled()
			!= bCurrentStyleEnabled)
	{
		FinishCapture(
			false,
			TEXT("The stylized rendering seam did not retain the requested Style/Profile state."));
		return;
	}
	CaptureCamera->SetActorTransform(
		Point.CameraWorldTransform,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	CaptureCamera->GetCameraComponent()->SetFieldOfView(
		Point.Definition.FieldOfViewDegrees);
	if (APlayerController* Controller = CaptureController.Get())
	{
		Controller->SetViewTarget(CaptureCamera);
		if (Controller->PlayerCameraManager != nullptr)
		{
			// The T0 runner intentionally pauses the world so Off/On variants
			// share an identical world time. A paused PlayerCameraManager does
			// not tick after the transient camera moves to the next anchor, so
			// its camera cache would otherwise keep the previous anchor's pose.
			// Refresh only the camera evaluation with a zero delta; gameplay and
			// world simulation remain frozen.
			Controller->PlayerCameraManager->UpdateCamera(0.0f);
			// Give Off and On the same temporal-history starting condition.
			Controller->PlayerCameraManager->SetGameCameraCutThisFrame();
		}
	}

	RemainingWarmupFrames = Point.Definition.WarmupFrameOverride >= 0
		? Point.Definition.WarmupFrameOverride
		: RunConfig.WarmupFrames;
	CurrentGPUProfileSampleIndex = 0;
	CurrentEffectiveCameraPoseHash = 0;
	Phase = EABTSToonVisualCapturePhase::WarmingCamera;
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][ToonT0][Variant] Point=%s Anchor=%s Style=%s Profile=%s StyleImplementation=%d PoseHash=%s SemanticHash=%s Warmup=%d"),
		*Point.Definition.PointId.ToString(),
		FABTSToonVisualCaptureMath::LexToString(Point.Definition.Anchor),
		bCurrentStyleEnabled ? TEXT("On") : TEXT("Off"),
		FABTSToonVisualCaptureMath::LexToString(
			Point.Definition.StyleProfile),
		FABTSStylizedRenderingControl::GetImplementationVersion(),
		*ABTSToonVisualCaptureSubsystemPrivate::Hex64(Point.CameraPoseHash),
		*ABTSToonVisualCaptureSubsystemPrivate::Hex64(
			Point.SemanticIdentityHash),
		RemainingWarmupFrames);
}

bool UABTSToonVisualCaptureSubsystem::ValidateEffectiveCamera(
	FString& OutFailure)
{
	if (!ResolvedPoints.IsValidIndex(CurrentPointIndex)
		|| !IsValid(CaptureCamera))
	{
		OutFailure = TEXT("The requested capture camera is unavailable.");
		return false;
	}
	APlayerController* Controller = CaptureController.Get();
	if (!IsValid(Controller)
		|| Controller->GetViewTarget() != CaptureCamera
		|| Controller->PlayerCameraManager == nullptr)
	{
		OutFailure = TEXT("The player camera manager is not consuming the T0 capture camera.");
		return false;
	}

	const FABTSToonResolvedCapturePoint& Point =
		ResolvedPoints[CurrentPointIndex];
	if (FABTSStylizedRenderingControl::GetProfile()
			!= Point.Definition.StyleProfile
		|| FABTSStylizedRenderingControl::IsEnabled()
			!= bCurrentStyleEnabled)
	{
		OutFailure = TEXT("Style/Profile state changed during camera warmup.");
		return false;
	}

	const FMinimalViewInfo& EffectiveView =
		Controller->PlayerCameraManager->GetCameraCacheView();
	const FTransform EffectiveTransform(
		EffectiveView.Rotation,
		EffectiveView.Location);
	const double LocationErrorCM = FVector::Distance(
		EffectiveTransform.GetLocation(),
		Point.CameraWorldTransform.GetLocation());
	const double RotationErrorDegrees = FMath::RadiansToDegrees(
		EffectiveTransform.GetRotation().AngularDistance(
			Point.CameraWorldTransform.GetRotation()));
	const double FovErrorDegrees = FMath::Abs(
		static_cast<double>(EffectiveView.FOV)
			- Point.Definition.FieldOfViewDegrees);
	if (LocationErrorCM > 0.5
		|| RotationErrorDegrees > 0.05
		|| FovErrorDegrees > 0.01)
	{
		OutFailure = FString::Printf(
			TEXT("Effective camera diverged from the requested pose: LocationCM=%.4f RotationDeg=%.4f FOVDeg=%.4f."),
			LocationErrorCM,
			RotationErrorDegrees,
			FovErrorDegrees);
		return false;
	}

	const uint64 EffectiveCameraPoseHash =
		FABTSToonVisualCaptureMath::ComputeCameraPoseHash(
			EffectiveTransform,
			Point.LookAtWorld,
			EffectiveView.FOV);
	if (CurrentEffectiveCameraPoseHash != 0
		&& CurrentEffectiveCameraPoseHash != EffectiveCameraPoseHash)
	{
		OutFailure = FString::Printf(
			TEXT("Effective camera identity drifted between samples at %s."),
			*Point.Definition.PointId.ToString());
		return false;
	}
	CurrentEffectiveCameraPoseHash = EffectiveCameraPoseHash;
	for (const FABTSToonVisualCaptureManifestRecord& Record : ManifestRecords)
	{
		if (Record.PointId == Point.Definition.PointId
			&& Record.EffectiveCameraPoseHash != 0
			&& Record.EffectiveCameraPoseHash
				!= CurrentEffectiveCameraPoseHash)
		{
			OutFailure = FString::Printf(
				TEXT("Style Off/On effective camera identity diverged at %s."),
				*Point.Definition.PointId.ToString());
			return false;
		}
	}
	return true;
}

void UABTSToonVisualCaptureSubsystem::RequestCurrentScreenshot()
{
	const IConsoleVariable* ScreenshotDelegateCVar =
		IConsoleManager::Get().FindConsoleVariable(
			TEXT("r.ScreenshotDelegate"));
	if (UGameViewportClient::OnScreenshotCaptured().IsBound()
		&& ScreenshotDelegateCVar != nullptr
		&& ScreenshotDelegateCVar->GetInt() != 0)
	{
		FinishCapture(
			false,
			TEXT("A screenshot pixel delegate is bound while r.ScreenshotDelegate is enabled; UE would suppress PNG writeback."));
		return;
	}
	if (FScreenshotRequest::IsScreenshotRequested())
	{
		FinishCapture(
			false,
			TEXT("Another screenshot request is already active."));
		return;
	}
	ActiveScreenshotPath = MakeCurrentArtifactPath();
	bScreenshotProcessed = false;
	ScreenshotProcessedRealSeconds = 0.0;
	FScreenshotRequest::RequestScreenshot(
		ActiveScreenshotPath,
		true,
		false,
		false,
		FIntRect(),
		true);
	if (!FScreenshotRequest::IsScreenshotRequested())
	{
		FinishCapture(false, TEXT("UE rejected the screenshot request."));
		return;
	}
	Phase = EABTSToonVisualCapturePhase::WaitingForScreenshot;
}

void UABTSToonVisualCaptureSubsystem::HandleScreenshotProcessed()
{
	if (Phase == EABTSToonVisualCapturePhase::WaitingForScreenshot)
	{
		bScreenshotProcessed = true;
		ScreenshotProcessedRealSeconds = FPlatformTime::Seconds();
	}
}

void UABTSToonVisualCaptureSubsystem::CompleteCurrentScreenshot()
{
	const FABTSToonResolvedCapturePoint& Point =
		ResolvedPoints[CurrentPointIndex];
	FIntPoint PngResolution;
	if (!ABTSToonVisualCaptureSubsystemPrivate::ReadPngDimensions(
		ActiveScreenshotPath,
		PngResolution))
	{
		FinishCapture(
			false,
			FString::Printf(
				TEXT("Screenshot is not a readable PNG: %s"),
				*ActiveScreenshotPath));
		return;
	}
	const FIntPoint ViewportResolution = GetActualViewportResolution();
	if (PngResolution != ViewportResolution
		|| (RunConfig.bRequireExactResolution
			&& PngResolution
				!= FIntPoint(
					RunConfig.ExpectedResolutionX,
					RunConfig.ExpectedResolutionY)))
	{
		FinishCapture(
			false,
			FString::Printf(
				TEXT("PNG resolution mismatch: PNG=%dx%d Viewport=%dx%d Expected=%dx%d."),
				PngResolution.X,
				PngResolution.Y,
				ViewportResolution.X,
				ViewportResolution.Y,
				RunConfig.ExpectedResolutionX,
				RunConfig.ExpectedResolutionY));
		return;
	}
	const FMD5Hash MD5 = FMD5Hash::HashFile(*ActiveScreenshotPath);
	if (!MD5.IsValid())
	{
		FinishCapture(
			false,
			FString::Printf(
				TEXT("Unable to hash screenshot: %s"),
				*ActiveScreenshotPath));
		return;
	}

	FABTSToonVisualCaptureManifestRecord Record;
	Record.PointId = Point.Definition.PointId;
	Record.Anchor = Point.Definition.Anchor;
	Record.Profile = Point.Definition.StyleProfile;
	Record.bStyleEnabled = bCurrentStyleEnabled;
	Record.StyleImplementationVersion =
		FABTSStylizedRenderingControl::GetImplementationVersion();
	Record.CameraWorldTransform = Point.CameraWorldTransform;
	Record.LookAtWorld = Point.LookAtWorld;
	Record.FieldOfViewDegrees = Point.Definition.FieldOfViewDegrees;
	Record.SemanticIdentityHash = Point.SemanticIdentityHash;
	Record.CameraPoseHash = Point.CameraPoseHash;
	Record.EffectiveCameraPoseHash = CurrentEffectiveCameraPoseHash;
	Record.Resolution = PngResolution;
	Record.ArtifactPath = ActiveScreenshotPath;
	Record.ArtifactMD5 =
		ABTSToonVisualCaptureSubsystemPrivate::MD5ToString(MD5);
	ManifestRecords.Add(Record);
	if (!WriteManifest(TEXT("Running"), FString()))
	{
		FinishCapture(false, TEXT("Unable to update the running screenshot manifest."));
		return;
	}
	AdvanceVariantOrFinish();
}

void UABTSToonVisualCaptureSubsystem::DispatchCurrentGPUProfile()
{
#if WITH_PROFILEGPU
	UWorld* World = GetWorld();
	if (World == nullptr || GEngine == nullptr)
	{
		FinishCapture(false, TEXT("World or engine is unavailable for ProfileGPU."));
		return;
	}
	if (UE::RHI::GPUProfiler::IsProfiling())
	{
		FinishCapture(
			false,
			TEXT("A GPU profile is already active; T0 will not overlap samples."));
		return;
	}
	const IConsoleVariable* ShowUI =
		IConsoleManager::Get().FindConsoleVariable(
			TEXT("r.ProfileGPU.ShowUI"));
	if (ShowUI == nullptr || ShowUI->GetInt() != 0)
	{
		FinishCapture(
			false,
			TEXT("ProfileGPU UI suppression was not retained."));
		return;
	}
	const FABTSToonResolvedCapturePoint& Point =
		ResolvedPoints[CurrentPointIndex];
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][ToonT0][GPUProfileMarker] Run=%s Point=%s Style=%s PoseHash=%s EffectivePoseHash=%s Sample=%d/%d"),
		*RunId,
		*Point.Definition.PointId.ToString(),
		bCurrentStyleEnabled ? TEXT("On") : TEXT("Off"),
		*ABTSToonVisualCaptureSubsystemPrivate::Hex64(Point.CameraPoseHash),
		*ABTSToonVisualCaptureSubsystemPrivate::Hex64(
			CurrentEffectiveCameraPoseHash),
		CurrentGPUProfileSampleIndex + 1,
		RunConfig.GPUProfileSamplesPerVariant);
	if (!GEngine->Exec(World, TEXT("ProfileGPU"))
		|| !UE::RHI::GPUProfiler::IsProfiling())
	{
		FinishCapture(
			false,
			TEXT("ProfileGPU command was not accepted by the active RHI."));
		return;
	}
	GPUProfileFalseObservedRealSeconds = 0.0;
	RemainingGPUCooldownFrames =
		ABTSToonVisualCaptureSubsystemPrivate::GPUPostProfileSettleFrames;
	Phase = EABTSToonVisualCapturePhase::CoolingGPUProfile;
#else
	FinishCapture(false, TEXT("This build does not support ProfileGPU."));
#endif
}

void UABTSToonVisualCaptureSubsystem::CompleteCurrentGPUProfile()
{
	if (GLog != nullptr)
	{
		GLog->FlushThreadedLogs();
		GLog->Flush();
	}
	++CurrentGPUProfileSampleIndex;
	if (CurrentGPUProfileSampleIndex
		< RunConfig.GPUProfileSamplesPerVariant)
	{
		RemainingWarmupFrames = 2;
		Phase = EABTSToonVisualCapturePhase::WarmingCamera;
		return;
	}

	const FABTSToonResolvedCapturePoint& Point =
		ResolvedPoints[CurrentPointIndex];
	FABTSToonVisualCaptureManifestRecord Record;
	Record.PointId = Point.Definition.PointId;
	Record.Anchor = Point.Definition.Anchor;
	Record.Profile = Point.Definition.StyleProfile;
	Record.bStyleEnabled = bCurrentStyleEnabled;
	Record.StyleImplementationVersion =
		FABTSStylizedRenderingControl::GetImplementationVersion();
	Record.CameraWorldTransform = Point.CameraWorldTransform;
	Record.LookAtWorld = Point.LookAtWorld;
	Record.FieldOfViewDegrees = Point.Definition.FieldOfViewDegrees;
	Record.SemanticIdentityHash = Point.SemanticIdentityHash;
	Record.CameraPoseHash = Point.CameraPoseHash;
	Record.EffectiveCameraPoseHash = CurrentEffectiveCameraPoseHash;
	Record.Resolution = GetActualViewportResolution();
	Record.bGPUProfileCommandAccepted = true;
	Record.GPUProfileSampleCount = CurrentGPUProfileSampleIndex;
	ManifestRecords.Add(Record);
	if (!WriteManifest(TEXT("Running"), FString()))
	{
		FinishCapture(false, TEXT("Unable to update the running GPU manifest."));
		return;
	}
	AdvanceVariantOrFinish();
}

void UABTSToonVisualCaptureSubsystem::AdvanceVariantOrFinish()
{
	if (!bCurrentStyleEnabled)
	{
		bCurrentStyleEnabled = true;
		BeginCurrentVariant();
		return;
	}

	bCurrentStyleEnabled = false;
	++CurrentPointIndex;
	if (CurrentPointIndex >= ResolvedPoints.Num())
	{
		FinishCapture(true, FString());
	}
	else
	{
		BeginCurrentVariant();
	}
}

void UABTSToonVisualCaptureSubsystem::FinishCapture(
	bool bSuccess,
	const FString& Reason)
{
	if (Phase == EABTSToonVisualCapturePhase::Terminal)
	{
		return;
	}
	bool bEffectiveSuccess = bSuccess;
	FString EffectiveReason = Reason;
	const int32 ExpectedRecordCount = ResolvedPoints.Num() * 2;
	if (bEffectiveSuccess
		&& ManifestRecords.Num() != ExpectedRecordCount)
	{
		bEffectiveSuccess = false;
		EffectiveReason = FString::Printf(
			TEXT("Record count mismatch: actual=%d expected=%d."),
			ManifestRecords.Num(),
			ExpectedRecordCount);
	}
	const TCHAR* Status =
		bEffectiveSuccess ? TEXT("Succeeded") : TEXT("Failed");
	if (!WriteManifest(Status, EffectiveReason))
	{
		bEffectiveSuccess = false;
		EffectiveReason = EffectiveReason.IsEmpty()
			? TEXT("Unable to write the terminal manifest.")
			: EffectiveReason
				+ TEXT(" Terminal manifest write also failed.");
		WriteManifest(TEXT("Failed"), EffectiveReason);
	}
	RestoreRuntimeState();
	Phase = EABTSToonVisualCapturePhase::Terminal;

	if (bEffectiveSuccess)
	{
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][ToonT0][Terminal] Success=1 Mode=%s Records=%d Expected=%d Output=%s Reason=None"),
			FABTSToonVisualCaptureMath::LexToString(RunConfig.Mode),
			ManifestRecords.Num(),
			ExpectedRecordCount,
			*OutputDirectory);
	}
	else
	{
		UE_LOG(
			LogABTSRuntime,
			Error,
			TEXT("[ABTS][ToonT0][Terminal] Success=0 Mode=%s Records=%d Expected=%d Output=%s Reason=%s"),
			FABTSToonVisualCaptureMath::LexToString(RunConfig.Mode),
			ManifestRecords.Num(),
			ExpectedRecordCount,
			*OutputDirectory,
			EffectiveReason.IsEmpty() ? TEXT("None") : *EffectiveReason);
	}

	if (RunConfig.bExitWhenComplete)
	{
		FPlatformMisc::RequestExitWithStatus(
			false,
			static_cast<uint8>(bEffectiveSuccess ? 0 : 1),
			TEXT("ABTSToonVisualCaptureSubsystem"));
	}
}

void UABTSToonVisualCaptureSubsystem::RestoreRuntimeState()
{
	if (ScreenshotProcessedHandle.IsValid())
	{
		FScreenshotRequest::OnScreenshotRequestProcessed().Remove(
			ScreenshotProcessedHandle);
		ScreenshotProcessedHandle.Reset();
	}
	if (FScreenshotRequest::IsScreenshotRequested()
		&& !ActiveScreenshotPath.IsEmpty())
	{
		FScreenshotRequest::Reset();
	}

	if (bRuntimeStateCaptured)
	{
		if (bProfileGPUShowUIStateCaptured)
		{
			if (IConsoleVariable* ShowUI =
				IConsoleManager::Get().FindConsoleVariable(
					TEXT("r.ProfileGPU.ShowUI")))
			{
				ShowUI->Set(
					bSavedProfileGPUShowUI ? 1 : 0,
					static_cast<EConsoleVariableFlags>(
						SavedProfileGPUShowUISetBy));
			}
			bProfileGPUShowUIStateCaptured = false;
		}
		GAreScreenMessagesEnabled = bSavedScreenMessagesEnabled;
		FABTSStylizedRenderingControl::SetProfile(SavedStyleProfile);
		FABTSStylizedRenderingControl::SetEnabled(bSavedStyleEnabled);
		if (UWorld* World = GetWorld())
		{
			if (UGameplayStatics::IsGamePaused(World) != bWorldWasPaused)
			{
				UGameplayStatics::SetGamePaused(World, bWorldWasPaused);
			}
		}
		if (APlayerController* Controller = CaptureController.Get())
		{
			if (AActor* ViewTarget = SavedViewTarget.Get())
			{
				Controller->SetViewTarget(ViewTarget);
			}
			else if (APawn* Pawn = Controller->GetPawn())
			{
				Controller->SetViewTarget(Pawn);
			}
		}
	}
	if (IsValid(CaptureCamera))
	{
		CaptureCamera->Destroy();
	}
	CaptureCamera = nullptr;
	bRuntimeStateCaptured = false;
}

bool UABTSToonVisualCaptureSubsystem::WriteManifest(
	const TCHAR* Status,
	const FString& FailureReason)
{
	if (OutputDirectory.IsEmpty())
	{
		return false;
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schemaVersion"), 2);
	Root->SetStringField(TEXT("suite"), TEXT("ToonT0"));
	Root->SetStringField(TEXT("runId"), RunId);
	Root->SetStringField(TEXT("buildIdentity"), RunConfig.BuildIdentity);
	Root->SetStringField(TEXT("status"), Status);
	Root->SetStringField(
		TEXT("mode"),
		FABTSToonVisualCaptureMath::LexToString(RunConfig.Mode));
	Root->SetStringField(TEXT("failureReason"), FailureReason);
	Root->SetStringField(
		TEXT("engineVersion"),
		FEngineVersion::Current().ToString());
	Root->SetStringField(TEXT("project"), FApp::GetProjectName());
	Root->SetStringField(
		TEXT("map"),
		GetWorld() != nullptr ? GetWorld()->GetMapName() : FString());
	Root->SetStringField(
		TEXT("timestampUtc"),
		FDateTime::UtcNow().ToIso8601());

	TSharedRef<FJsonObject> Environment = MakeShared<FJsonObject>();
	Environment->SetStringField(
		TEXT("primaryGPUBrand"),
		FPlatformMisc::GetPrimaryGPUBrand());
	Environment->SetStringField(
		TEXT("dynamicRHI"),
		GDynamicRHI != nullptr ? GDynamicRHI->GetName() : TEXT("Unavailable"));
	Environment->SetStringField(TEXT("adapterName"), GRHIAdapterName);
	Environment->SetStringField(
		TEXT("adapterInternalDriverVersion"),
		GRHIAdapterInternalDriverVersion);
	Environment->SetStringField(
		TEXT("adapterUserDriverVersion"),
		GRHIAdapterUserDriverVersion);
	Environment->SetStringField(
		TEXT("adapterDriverDate"),
		GRHIAdapterDriverDate);
	Environment->SetNumberField(TEXT("vendorId"), GRHIVendorId);
	Environment->SetNumberField(TEXT("deviceId"), GRHIDeviceId);
	Environment->SetStringField(
		TEXT("shaderPlatform"),
		LexToString(GMaxRHIShaderPlatform));
	Environment->SetStringField(
		TEXT("screenPercentage"),
		ABTSToonVisualCaptureSubsystemPrivate::ReadConsoleVariableIdentity(
			TEXT("r.ScreenPercentage")));
	Environment->SetStringField(
		TEXT("dynamicResolutionOperationMode"),
		ABTSToonVisualCaptureSubsystemPrivate::ReadConsoleVariableIdentity(
			TEXT("r.DynamicRes.OperationMode")));
	Environment->SetStringField(
		TEXT("vSync"),
		ABTSToonVisualCaptureSubsystemPrivate::ReadConsoleVariableIdentity(
			TEXT("r.VSync")));
	Environment->SetStringField(
		TEXT("profileGPUShowUI"),
		ABTSToonVisualCaptureSubsystemPrivate::ReadConsoleVariableIdentity(
			TEXT("r.ProfileGPU.ShowUI")));
	Environment->SetStringField(
		TEXT("exposurePolicy"),
		TEXT("SceneConfiguredAutoExposure_CameraCut_PerVariantWarmup"));
	const Scalability::FQualityLevels Quality =
		Scalability::GetQualityLevels();
	TSharedRef<FJsonObject> ScalabilityJson = MakeShared<FJsonObject>();
	ScalabilityJson->SetNumberField(
		TEXT("qualityHash"),
		Quality.GetHash());
	ScalabilityJson->SetNumberField(
		TEXT("resolutionQuality"),
		Quality.ResolutionQuality);
	ScalabilityJson->SetNumberField(
		TEXT("viewDistance"),
		Quality.ViewDistanceQuality);
	ScalabilityJson->SetNumberField(
		TEXT("antiAliasing"),
		Quality.AntiAliasingQuality);
	ScalabilityJson->SetNumberField(TEXT("shadow"), Quality.ShadowQuality);
	ScalabilityJson->SetNumberField(
		TEXT("globalIllumination"),
		Quality.GlobalIlluminationQuality);
	ScalabilityJson->SetNumberField(
		TEXT("reflection"),
		Quality.ReflectionQuality);
	ScalabilityJson->SetNumberField(
		TEXT("postProcess"),
		Quality.PostProcessQuality);
	ScalabilityJson->SetNumberField(TEXT("texture"), Quality.TextureQuality);
	ScalabilityJson->SetNumberField(TEXT("effects"), Quality.EffectsQuality);
	ScalabilityJson->SetNumberField(TEXT("foliage"), Quality.FoliageQuality);
	ScalabilityJson->SetNumberField(TEXT("shading"), Quality.ShadingQuality);
	ScalabilityJson->SetNumberField(
		TEXT("landscape"),
		Quality.LandscapeQuality);
	Environment->SetObjectField(TEXT("scalability"), ScalabilityJson);
	Root->SetObjectField(TEXT("environment"), Environment);

	TSharedRef<FJsonObject> WorldIdentity = MakeShared<FJsonObject>();
	WorldIdentity->SetNumberField(
		TEXT("expectedSeed"),
		RunConfig.ExpectedWorldSeed);
	WorldIdentity->SetNumberField(TEXT("actualSeed"), ActualWorldSeed);
	WorldIdentity->SetNumberField(
		TEXT("generatorVersion"),
		ActualGeneratorVersion);
	WorldIdentity->SetNumberField(
		TEXT("generationAttempt"),
		ActualGenerationAttempt);
	WorldIdentity->SetBoolField(
		TEXT("sourceWorldAccepted"),
		bActualSourceWorldAccepted);
	WorldIdentity->SetNumberField(
		TEXT("monthlyPresentationCandidateId"),
		MonthlyPresentationCandidateId);
	WorldIdentity->SetStringField(
		TEXT("monthlyPresentationCandidateHash"),
		ABTSToonVisualCaptureSubsystemPrivate::Hex64(
			static_cast<uint64>(MonthlyPresentationCandidateHash)));
	WorldIdentity->SetStringField(
		TEXT("ordinarySlotAuthority"),
		ABTSToonVisualCaptureSubsystemPrivate::LexToString(
			static_cast<EABTSM51OrdinarySlingshotSlotSnapshotAuthority>(
				OrdinarySlotAuthority)));
	WorldIdentity->SetNumberField(
		TEXT("ordinaryMaxCordLengthCM"),
		OrdinaryMaxCordLengthCM);
	WorldIdentity->SetStringField(
		TEXT("ordinarySlotEvidenceHash"),
		ABTSToonVisualCaptureSubsystemPrivate::Hex64(
			OrdinarySlotEvidenceHash));
	WorldIdentity->SetStringField(
		TEXT("finaleFrameAuthority"),
		ABTSToonVisualCaptureSubsystemPrivate::LexToString(
			static_cast<EABTSM51FinaleFrameAuthority>(
				FinaleFrameAuthority)));
	WorldIdentity->SetNumberField(
		TEXT("finaleFrameSourceCandidateId"),
		FinaleFrameSourceCandidateId);
	WorldIdentity->SetStringField(
		TEXT("finaleFrameSpatialCandidateHash"),
		ABTSToonVisualCaptureSubsystemPrivate::Hex64(
			static_cast<uint64>(FinaleFrameSpatialCandidateHash)));
	WorldIdentity->SetStringField(
		TEXT("finaleFramePlanResultHash"),
		ABTSToonVisualCaptureSubsystemPrivate::Hex64(
			static_cast<uint64>(FinaleFramePlanResultHash)));
	WorldIdentity->SetStringField(
		TEXT("finaleFramePreviewHash"),
		ABTSToonVisualCaptureSubsystemPrivate::Hex64(
			static_cast<uint64>(FinaleFramePreviewHash)));
	WorldIdentity->SetStringField(
		TEXT("finaleFrameContextHash"),
		ABTSToonVisualCaptureSubsystemPrivate::Hex64(
			static_cast<uint64>(FinaleFrameContextHash)));
	WorldIdentity->SetBoolField(
		TEXT("finaleFrameMonthlyWorldAccepted"),
		bFinaleFrameMonthlyWorldAccepted);
	WorldIdentity->SetStringField(
		TEXT("satelliteRuntimeLayoutHash"),
		ABTSToonVisualCaptureSubsystemPrivate::Hex64(
			static_cast<uint64>(SatelliteRuntimeLayoutHash)));
	WorldIdentity->SetNumberField(
		TEXT("satelliteSourceCandidateId"),
		SatelliteSourceCandidateId);
	WorldIdentity->SetStringField(
		TEXT("satelliteSourcePreviewResultHash"),
		ABTSToonVisualCaptureSubsystemPrivate::Hex64(
			static_cast<uint64>(SatelliteSourcePreviewResultHash)));
	WorldIdentity->SetStringField(
		TEXT("satelliteSourceCandidateHash"),
		ABTSToonVisualCaptureSubsystemPrivate::Hex64(
			static_cast<uint64>(SatelliteSourceCandidateHash)));
	WorldIdentity->SetStringField(
		TEXT("satelliteLaunchProfileHash"),
		ABTSToonVisualCaptureSubsystemPrivate::Hex64(
			static_cast<uint64>(SatelliteLaunchProfileHash)));
	WorldIdentity->SetStringField(
		TEXT("satelliteProductionLaunchProfileHash"),
		ABTSToonVisualCaptureSubsystemPrivate::Hex64(
			static_cast<uint64>(SatelliteProductionLaunchProfileHash)));
	WorldIdentity->SetStringField(
		TEXT("satellitePresetHash"),
		ABTSToonVisualCaptureSubsystemPrivate::Hex64(
			static_cast<uint64>(SatellitePresetHash)));
	WorldIdentity->SetStringField(
		TEXT("satelliteTrajectoryCertificationHash"),
		ABTSToonVisualCaptureSubsystemPrivate::Hex64(
			static_cast<uint64>(SatelliteTrajectoryCertificationHash)));
	WorldIdentity->SetStringField(
		TEXT("finalePresetHash"),
		ABTSToonVisualCaptureSubsystemPrivate::Hex64(FinalePresetHash));
	WorldIdentity->SetStringField(
		TEXT("finaleCertifiedBundleHash"),
		ABTSToonVisualCaptureSubsystemPrivate::Hex64(
			FinaleCertifiedBundleHash));
	WorldIdentity->SetBoolField(
		TEXT("finaleEditorCandidateMode"),
		bFinaleEditorCandidateMode);
	WorldIdentity->SetNumberField(
		TEXT("finaleEditorCandidateRank"),
		FinaleEditorCandidateRank);
	WorldIdentity->SetStringField(
		TEXT("finaleEditorCandidateSourceHash"),
		ABTSToonVisualCaptureSubsystemPrivate::Hex64(
			FinaleEditorCandidateSourceHash));
	WorldIdentity->SetStringField(
		TEXT("finaleEditorCandidateResultHash"),
		ABTSToonVisualCaptureSubsystemPrivate::Hex64(
			FinaleEditorCandidateResultHash));
	WorldIdentity->SetStringField(
		TEXT("captureCatalogueHash"),
		ABTSToonVisualCaptureSubsystemPrivate::Hex64(
			CaptureCatalogueHash));
	Root->SetObjectField(TEXT("worldIdentity"), WorldIdentity);

	const FIntPoint ActualResolution = GetActualViewportResolution();
	TSharedRef<FJsonObject> Resolution = MakeShared<FJsonObject>();
	Resolution->SetNumberField(
		TEXT("expectedX"),
		RunConfig.ExpectedResolutionX);
	Resolution->SetNumberField(
		TEXT("expectedY"),
		RunConfig.ExpectedResolutionY);
	Resolution->SetNumberField(TEXT("actualX"), ActualResolution.X);
	Resolution->SetNumberField(TEXT("actualY"), ActualResolution.Y);
	Resolution->SetBoolField(
		TEXT("exactRequired"),
		RunConfig.bRequireExactResolution);
	Root->SetObjectField(TEXT("resolution"), Resolution);

	TSharedRef<FJsonObject> Style = MakeShared<FJsonObject>();
	Style->SetNumberField(
		TEXT("implementationVersion"),
		FABTSStylizedRenderingControl::GetImplementationVersion());
	Style->SetStringField(
		TEXT("t0Contract"),
		TEXT("Style implementation is versioned; Off bypasses project stylization."));
	Style->SetNumberField(
		TEXT("gpuProfileSamplesPerVariant"),
		RunConfig.GPUProfileSamplesPerVariant);
	Root->SetObjectField(TEXT("style"), Style);

	TArray<TSharedPtr<FJsonValue>> Records;
	Records.Reserve(ManifestRecords.Num());
	for (const FABTSToonVisualCaptureManifestRecord& Record : ManifestRecords)
	{
		TSharedRef<FJsonObject> RecordJson = MakeShared<FJsonObject>();
		RecordJson->SetStringField(
			TEXT("pointId"),
			Record.PointId.ToString());
		RecordJson->SetStringField(
			TEXT("anchor"),
			FABTSToonVisualCaptureMath::LexToString(Record.Anchor));
		RecordJson->SetStringField(
			TEXT("profile"),
			FABTSToonVisualCaptureMath::LexToString(Record.Profile));
		RecordJson->SetBoolField(
			TEXT("styleEnabled"),
			Record.bStyleEnabled);
		RecordJson->SetNumberField(
			TEXT("styleImplementationVersion"),
			Record.StyleImplementationVersion);
		RecordJson->SetObjectField(
			TEXT("cameraWorldTransform"),
			ABTSToonVisualCaptureSubsystemPrivate::TransformToJson(
				Record.CameraWorldTransform));
		RecordJson->SetObjectField(
			TEXT("lookAtWorld"),
			ABTSToonVisualCaptureSubsystemPrivate::VectorToJson(
				Record.LookAtWorld));
		RecordJson->SetNumberField(
			TEXT("fieldOfViewDegrees"),
			Record.FieldOfViewDegrees);
		RecordJson->SetStringField(
			TEXT("semanticIdentityHash"),
			ABTSToonVisualCaptureSubsystemPrivate::Hex64(
				Record.SemanticIdentityHash));
		RecordJson->SetStringField(
			TEXT("cameraPoseHash"),
			ABTSToonVisualCaptureSubsystemPrivate::Hex64(
				Record.CameraPoseHash));
		RecordJson->SetStringField(
			TEXT("effectiveCameraPoseHash"),
			ABTSToonVisualCaptureSubsystemPrivate::Hex64(
				Record.EffectiveCameraPoseHash));
		RecordJson->SetNumberField(TEXT("resolutionX"), Record.Resolution.X);
		RecordJson->SetNumberField(TEXT("resolutionY"), Record.Resolution.Y);
		RecordJson->SetStringField(TEXT("artifactPath"), Record.ArtifactPath);
		RecordJson->SetStringField(TEXT("artifactMD5"), Record.ArtifactMD5);
		RecordJson->SetBoolField(
			TEXT("gpuProfileCommandAccepted"),
			Record.bGPUProfileCommandAccepted);
		RecordJson->SetNumberField(
			TEXT("gpuProfileSampleCount"),
			Record.GPUProfileSampleCount);
		Records.Add(MakeShared<FJsonValueObject>(RecordJson));
	}
	Root->SetArrayField(TEXT("records"), Records);

	FString JsonText;
	const TSharedRef<TJsonWriter<>> Writer =
		TJsonWriterFactory<>::Create(&JsonText);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		return false;
	}
	const FString ManifestPath =
		FPaths::Combine(OutputDirectory, TEXT("manifest.json"));
	const FString TemporaryManifestPath = ManifestPath + TEXT(".tmp");
	if (!FFileHelper::SaveStringToFile(
		JsonText,
		*TemporaryManifestPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		return false;
	}
	return IFileManager::Get().Move(
		*ManifestPath,
		*TemporaryManifestPath,
		true,
		false,
		false,
		true);
}

FIntPoint UABTSToonVisualCaptureSubsystem::GetActualViewportResolution() const
{
	const UWorld* World = GetWorld();
	const UGameViewportClient* ViewportClient =
		World != nullptr ? World->GetGameViewport() : nullptr;
	return ViewportClient != nullptr && ViewportClient->Viewport != nullptr
		? ViewportClient->Viewport->GetSizeXY()
		: FIntPoint::ZeroValue;
}

FString UABTSToonVisualCaptureSubsystem::MakeCurrentArtifactPath() const
{
	if (!ResolvedPoints.IsValidIndex(CurrentPointIndex))
	{
		return FString();
	}
	return FPaths::Combine(
		OutputDirectory,
		FString::Printf(
			TEXT("%02d_%s_Style%s.png"),
			CurrentPointIndex + 1,
			*ResolvedPoints[CurrentPointIndex].Definition.PointId.ToString(),
			bCurrentStyleEnabled ? TEXT("On") : TEXT("Off")));
}
