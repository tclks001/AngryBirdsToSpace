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
#include "Player/ABTSM25BirdCharacter.h"
#include "Rendering/ABTSStylizedRenderingControl.h"
#include "Rendering/ABTSStylizedRenderingTypes.h"
#include "Rendering/ABTST4LowPolyCloudPrototype.h"
#include "Rendering/ABTSStylizedRenderingWorldSubsystem.h"
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
		|| FParse::Param(CommandLine, TEXT("ABTSToonT4A0Capture"))
		|| FParse::Param(CommandLine, TEXT("ABTSToonT4A1Capture"))
		|| FParse::Param(CommandLine, TEXT("ABTSToonT4A2Capture"))
		|| FParse::Param(CommandLine, TEXT("ABTSToonT4A3Capture"))
		|| (FParse::Value(
			CommandLine,
			TEXT("ABTSVisualCaptureSuite="),
			Suite)
			&& (Suite.Equals(TEXT("ToonT0"), ESearchCase::IgnoreCase)
				|| Suite.Equals(TEXT("ToonT4A0"), ESearchCase::IgnoreCase)
				|| Suite.Equals(TEXT("ToonT4A1"), ESearchCase::IgnoreCase)
				|| Suite.Equals(TEXT("ToonT4A2"), ESearchCase::IgnoreCase)
				|| Suite.Equals(TEXT("ToonT4A3"), ESearchCase::IgnoreCase)));
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
	if (RunConfig.ExpectedScreenPercentage != 0)
	{
		if (IConsoleVariable* ScreenPercentage =
			IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
		{
			SavedScreenPercentage = ScreenPercentage->GetFloat();
			SavedScreenPercentageSetBy =
				ScreenPercentage->GetFlags() & ECVF_SetByMask;
			ScreenPercentage->Set(
				static_cast<float>(RunConfig.ExpectedScreenPercentage),
				ECVF_SetByCode);
			bScreenPercentageStateCaptured = true;
		}
	}
	CaptureStartRealSeconds = FPlatformTime::Seconds();
	Phase = EABTSToonVisualCapturePhase::WaitingForWorld;
	RunId = FString::Printf(
		TEXT("%s_%s_%s_%u"),
		FABTSToonVisualCaptureMath::LexToString(RunConfig.Suite),
		FABTSToonVisualCaptureMath::LexToString(RunConfig.Mode),
		*FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ")),
		FPlatformProcess::GetCurrentProcessId());

	FString Root = RunConfig.OutputDirectory;
	if (Root.IsEmpty())
	{
		Root = FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("ABTSVisualCaptures"),
			FABTSToonVisualCaptureMath::LexToString(RunConfig.Suite));
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
		TEXT("[ABTS][%s][Begin] Map=%s Mode=%s Build=%s ExpectedSeed=%d Resolution=%dx%d ExactResolution=%d GPUSamples=%d Output=%s"),
		FABTSToonVisualCaptureMath::LexToString(RunConfig.Suite),
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

	TArray<FABTSToonVisualCapturePointDefinition> Definitions =
		RunConfig.Suite == EABTSToonVisualCaptureSuite::ToonT4A3
			? FABTSToonVisualCaptureMath::BuildT4A3Catalogue()
		: RunConfig.Suite == EABTSToonVisualCaptureSuite::ToonT4A2
			? FABTSToonVisualCaptureMath::BuildT4A2Catalogue()
			: RunConfig.Suite == EABTSToonVisualCaptureSuite::ToonT4A1
			? FABTSToonVisualCaptureMath::BuildT4A1Catalogue()
			: RunConfig.Suite == EABTSToonVisualCaptureSuite::ToonT4A0
				? FABTSToonVisualCaptureMath::BuildT4A0Catalogue()
				: FABTSToonVisualCaptureMath::BuildDefaultCatalogue();
	VariantDefinitions =
		FABTSToonVisualCaptureMath::BuildVariantCatalogue(RunConfig.Suite);
	auto ApplyNameFilter = [&OutReason](
		auto& Values,
		const TArray<FName>& Requested,
		auto GetId,
		const TCHAR* Label)
	{
		if (Requested.IsEmpty())
		{
			return true;
		}
		using ValueType = typename std::decay_t<decltype(Values)>::ElementType;
		TArray<ValueType> Filtered;
		Filtered.Reserve(Requested.Num());
		for (const FName RequestedId : Requested)
		{
			const ValueType* Match = Values.FindByPredicate(
				[RequestedId, &GetId](const ValueType& Value)
				{
					return GetId(Value) == RequestedId;
				});
			if (Match == nullptr)
			{
				OutReason = FString::Printf(
					TEXT("Requested %s '%s' is not present in the suite catalogue."),
					Label,
					*RequestedId.ToString());
				return false;
			}
			Filtered.Add(*Match);
		}
		Values = MoveTemp(Filtered);
		return true;
	};
	if (!ApplyNameFilter(
		Definitions,
		RunConfig.RequestedPointIds,
		[](const FABTSToonVisualCapturePointDefinition& Value)
		{
			return Value.PointId;
		},
		TEXT("capture point"))
		|| !ApplyNameFilter(
			VariantDefinitions,
			RunConfig.RequestedVariantIds,
			[](const FABTSToonDiagnosticVariantDefinition& Value)
			{
				return Value.VariantId;
			},
			TEXT("capture variant")))
	{
		return EWorldResolveResult::Failed;
	}
	if (Definitions.IsEmpty() || VariantDefinitions.IsEmpty())
	{
		OutReason = TEXT("Capture point or variant catalogue is empty.");
		return EWorldResolveResult::Failed;
	}
	for (const FABTSToonDiagnosticVariantDefinition& Variant
		: VariantDefinitions)
	{
		if (!Variant.IsValid())
		{
			OutReason = TEXT("Capture variant catalogue contains an invalid entry.");
			return EWorldResolveResult::Failed;
		}
	}
	const IConsoleVariable* ScreenPercentage =
		IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
	if (RunConfig.ExpectedScreenPercentage != 0
		&& (ScreenPercentage == nullptr
		|| !FMath::IsNearlyEqual(
			ScreenPercentage->GetFloat(),
			static_cast<float>(RunConfig.ExpectedScreenPercentage),
			0.01f)))
	{
		OutReason = FString::Printf(
			TEXT("r.ScreenPercentage does not match the A2.4 identity: Actual=%s Expected=%d."),
			ScreenPercentage != nullptr
				? *FString::SanitizeFloat(ScreenPercentage->GetFloat())
				: TEXT("Unavailable"),
			RunConfig.ExpectedScreenPercentage);
		return EWorldResolveResult::Failed;
	}
	CaptureCatalogueHash =
		FABTSToonVisualCaptureMath::ComputeCatalogueHash(Definitions);
	VariantCatalogueHash =
		FABTSToonVisualCaptureMath::ComputeVariantCatalogueHash(
			VariantDefinitions);
	FString EnvironmentFailure;
	if (!FABTSToonEnvironmentResolver::ResolveWorldSnapshot(
		*World,
		EABTSStylizedRenderProfile::GroundDay,
		EnvironmentSnapshot,
		&EnvironmentFailure))
	{
		OutReason = FString::Printf(
			TEXT("T4 environment contract is unavailable: %s"),
			EnvironmentFailure.IsEmpty()
				? TEXT("Unknown")
				: *EnvironmentFailure);
		return EWorldResolveResult::Failed;
	}
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
	const FVector EnvironmentReferenceUp =
		(StartTransform.GetLocation()
			- EnvironmentSnapshot.PlanetCenterWorld).GetSafeNormal();
	FVector EnvironmentDawnUp = FVector::VectorPlaneProject(
		EnvironmentReferenceUp,
		EnvironmentSnapshot.SunDirectionToSunWorld).GetSafeNormal();
	if (EnvironmentDawnUp.IsNearlyZero())
	{
		FVector AxisA;
		FVector AxisB;
		EnvironmentSnapshot.SunDirectionToSunWorld.FindBestAxisVectors(
			AxisA,
			AxisB);
		EnvironmentDawnUp = AxisA;
	}
	if (FVector::DotProduct(EnvironmentDawnUp, EnvironmentReferenceUp) < 0.0)
	{
		EnvironmentDawnUp *= -1.0;
	}

	// Production Fixed-Six actors are registered from the exact V3 snapshot,
	// not from the legacy generic Sites array.  Selecting a generic midpoint
	// site and then looking for the nearest Fixed-Six actor can pair unrelated
	// identities (and used to reject the packaged screenshot gate at ~10 km).
	// Materialize the exact snapshot site into the generic camera-frame shape so
	// the remainder of the capture catalogue continues to share one code path.
	FABTSGeneratedBuildingSite FixedSixCaptureSite;
	FName FixedSixCaptureEntryId = NAME_None;
	const FABTSGeneratedBuildingSite* SelectedSite = nullptr;
	double BestSiteScore = TNumericLimits<double>::Max();
	if (BuildingContract.JuryDemoFixedSix.IsUsable())
	{
		const FABTSJuryDemoFixedSixBuildingSite* SelectedFixedSite = nullptr;
		const double CentreEncounter =
			(FABTSJuryDemoFixedSixContract::ExpectedSiteCount - 1) * 0.5;
		double BestSolarHeight = -2.0;
		double BestCentreDistance = TNumericLimits<double>::Max();
		for (const FABTSJuryDemoFixedSixBuildingSite& Site
			: BuildingContract.JuryDemoFixedSix.Sites)
		{
			if (!Site.IsUsableForContractVersion(
					BuildingContract.JuryDemoFixedSix.ContractVersion)
				|| (BuildingContract.JuryDemoFixedSix.ContractVersion
						>= FABTSJuryDemoFixedSixContract::SupportedV3ContractVersion
					&& Site.V3Envelope.SurfaceKind
						!= EABTSJuryDemoFixedSixSurfaceKind::PrimaryPlanet))
			{
				continue;
			}
			const double CentreDistance = FMath::Abs(
				static_cast<double>(Site.EncounterIndex) - CentreEncounter);
			const double SolarHeight = FVector::DotProduct(
				Site.WorldTransform.GetUnitAxis(EAxis::Z),
				EnvironmentSnapshot.SunDirectionToSunWorld);
			if (SelectedFixedSite == nullptr
				|| SolarHeight > BestSolarHeight + KINDA_SMALL_NUMBER
				|| (FMath::IsNearlyEqual(SolarHeight, BestSolarHeight)
					&& (CentreDistance < BestCentreDistance
						|| (FMath::IsNearlyEqual(
							CentreDistance, BestCentreDistance)
							&& Site.EncounterIndex
								< SelectedFixedSite->EncounterIndex))))
			{
				SelectedFixedSite = &Site;
				BestSolarHeight = SolarHeight;
				BestCentreDistance = CentreDistance;
			}
		}
		if (SelectedFixedSite != nullptr)
		{
			FixedSixCaptureEntryId = SelectedFixedSite->ManifestEntryId;
			FixedSixCaptureSite.SiteId =
				SelectedFixedSite->V3Envelope.PlacementHash != 0
					? SelectedFixedSite->V3Envelope.PlacementHash
					: SelectedFixedSite->V2Envelope.StaticGeometryHash != 0
						? SelectedFixedSite->V2Envelope.StaticGeometryHash
						: static_cast<uint64>(GetTypeHash(
							SelectedFixedSite->ManifestEntryId));
			FixedSixCaptureSite.TaskId = SelectedFixedSite->EncounterIndex;
			FixedSixCaptureSite.EncounterIndex =
				SelectedFixedSite->EncounterIndex;
			FixedSixCaptureSite.DifficultyTier =
				SelectedFixedSite->DifficultyTier;
			FixedSixCaptureSite.NormalizedRouteProgress =
				static_cast<float>(SelectedFixedSite->EncounterIndex)
				/ (FABTSJuryDemoFixedSixContract::ExpectedSiteCount - 1);
			FixedSixCaptureSite.LayoutArchetypeId =
				SelectedFixedSite->ManifestEntryId;
			FixedSixCaptureSite.DeterministicSeed =
				SelectedFixedSite->DeterministicSeed;
			FixedSixCaptureSite.Purpose =
				EABTSGeneratedBuildingPurpose::DestructibleTarget;
			FixedSixCaptureSite.WorldTransform =
				SelectedFixedSite->WorldTransform;
			FixedSixCaptureSite.AnchorDirection =
				SelectedFixedSite->WorldTransform.GetUnitAxis(EAxis::Z);
			FixedSixCaptureSite.TangentForward =
				SelectedFixedSite->WorldTransform.GetUnitAxis(EAxis::X);
			FixedSixCaptureSite.TangentRight =
				SelectedFixedSite->WorldTransform.GetUnitAxis(EAxis::Y);
			FixedSixCaptureSite.PadHalfExtentCM =
				SelectedFixedSite->PadHalfExtentCM;
			SelectedSite = &FixedSixCaptureSite;
		}
	}
	if (SelectedSite == nullptr)
	{
		BestSiteScore = TNumericLimits<double>::Max();
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
	int32 ExactFixedSixMatches = 0;
	for (AABTSM73StableBuildingActor* Building : AcceptedBuildings)
	{
		if (!FixedSixCaptureEntryId.IsNone()
			&& Building->GetJuryDemoFixedSixManifestEntryId()
				!= FixedSixCaptureEntryId)
		{
			continue;
		}
		if (!FixedSixCaptureEntryId.IsNone())
		{
			++ExactFixedSixMatches;
		}
		const double DistanceSquared = FVector::DistSquared(
			Building->GetActorLocation(),
			SelectedSite->WorldTransform.GetLocation());
		if (DistanceSquared < BestBuildingDistanceSquared)
		{
			SelectedBuilding = Building;
			BestBuildingDistanceSquared = DistanceSquared;
		}
	}
	if (!FixedSixCaptureEntryId.IsNone() && ExactFixedSixMatches != 1)
	{
		OutReason = FString::Printf(
			TEXT("Fixed-Six capture site did not resolve exactly one accepted actor: Entry=%s Matches=%d."),
			*FixedSixCaptureEntryId.ToString(),
			ExactFixedSixMatches);
		return EWorldResolveResult::Failed;
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
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][ToonT0][BuildingCaptureBinding]")
		TEXT(" Source=%s Site=%llu Entry=%s Actor=%s AnchorDistanceCM=%.2f")
		TEXT(" LimitCM=%.2f SolarHeight=%.4f Accepted=1"),
		FixedSixCaptureEntryId.IsNone() ? TEXT("GenericSites") : TEXT("FixedSixExact"),
		static_cast<unsigned long long>(SelectedSite->SiteId),
		FixedSixCaptureEntryId.IsNone()
			? TEXT("None")
			: *FixedSixCaptureEntryId.ToString(),
		*SelectedBuilding->GetName(),
		FMath::Sqrt(BestBuildingDistanceSquared),
		MaxBuildingMatchDistanceCM,
		FVector::DotProduct(
			SelectedSite->AnchorDirection.GetSafeNormal(),
			EnvironmentSnapshot.SunDirectionToSunWorld));

	FBox SelectedBuildingPresentationBounds(EForceInit::ForceInit);
	int32 LiveModuleCount = 0;
	if (!SelectedBuilding->QueryLivePresentationBounds(
		SelectedBuildingPresentationBounds,
		LiveModuleCount)
		|| LiveModuleCount <= 0
		|| !SelectedBuildingPresentationBounds.IsValid)
	{
		OutReason = TEXT("Selected building has no live presentation bounds.");
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

	auto BuildEnvironmentSurfacePoint = [
		this,
		&Planet,
		&EnvironmentDawnUp](
		const FVector& RequestedUp,
		const FABTSToonVisualCapturePointDefinition& Definition,
		FABTSToonResolvedCapturePoint& OutPoint,
		FString& OutFailure)
	{
		const FVector Up = RequestedUp.GetSafeNormal();
		if (Up.IsNearlyZero())
		{
			OutFailure = TEXT("Environment surface radial direction is degenerate.");
			return false;
		}
		FVector Forward = FVector::VectorPlaneProject(
			EnvironmentSnapshot.SunDirectionToSunWorld,
			Up).GetSafeNormal();
		if (Forward.IsNearlyZero())
		{
			Forward = FVector::VectorPlaneProject(
				EnvironmentDawnUp,
				Up).GetSafeNormal();
		}
		if (Forward.IsNearlyZero())
		{
			FVector FallbackRight;
			Up.FindBestAxisVectors(Forward, FallbackRight);
		}
		const FVector Right = FVector::CrossProduct(Up, Forward).GetSafeNormal();
		if (Forward.IsNearlyZero() || Right.IsNearlyZero())
		{
			OutFailure = TEXT("Environment surface tangent frame is degenerate.");
			return false;
		}
		const double SurfaceRadiusCM =
			Planet.GetSurfaceRadiusAtDirection(Up);
		const FVector SurfaceLocation =
			EnvironmentSnapshot.PlanetCenterWorld + Up * SurfaceRadiusCM;
		OutPoint.LookAtWorld = SurfaceLocation
			+ Forward * 320.0
			+ Up * 120.0;
		const FVector CameraLocation = OutPoint.LookAtWorld
			- Forward * 1450.0
			+ Right * 420.0
			+ Up * 560.0;
		if (!FABTSToonVisualCaptureMath::BuildLookAtCameraTransform(
			CameraLocation,
			OutPoint.LookAtWorld,
			Up,
			OutPoint.CameraWorldTransform,
			&OutFailure))
		{
			return false;
		}
		OutPoint.SemanticIdentityHash =
			ABTSToonVisualCaptureSubsystemPrivate::Mix64(
				EnvironmentSnapshot.IdentityHash,
				static_cast<uint64>(Definition.Anchor));
		return true;
	};
	const FABTSStylizedEnvironmentParameters CloudEnvironmentParameters =
		FABTSStylizedRenderingControl::BuildEnvironmentParameters(
			EnvironmentSnapshot.PlanetCenterWorld,
			EnvironmentSnapshot.PlanetRadiusCM,
			EnvironmentSnapshot.SunDirectionToSunWorld,
			EABTSStylizedRenderProfile::GroundDay);
	const TArray<FABTST4LowPolyCloudIslandDefinition> CloudDefinitions =
		FABTST4LowPolyCloudPrototype::BuildDefinitions(
			EnvironmentSnapshot.PlanetCenterWorld,
			EnvironmentSnapshot.PlanetRadiusCM,
			CloudEnvironmentParameters.StarSeed ^ 0xC10DF13Du,
			EnvironmentSnapshot.SunDirectionToSunWorld,
			CloudEnvironmentParameters.CloudBaseAltitudeCM,
			CloudEnvironmentParameters.CloudLayerHeightCM);
	const uint64 CloudLayoutHash =
		FABTST4LowPolyCloudPrototype::ComputeLayoutHash(CloudDefinitions);

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
			// The frozen site pad is a placement contract, not a visual-size
			// contract.  Large Fixed-Six buildings can extend well beyond it;
			// include their live physical bounds so the QA shot proves the whole
			// building rather than an empty pad or a clipped facade.
			Bounds += SelectedBuildingPresentationBounds;
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
		case EABTSToonVisualCaptureAnchor::EnvironmentGroundDay:
			if (!BuildEnvironmentSurfacePoint(
				EnvironmentSnapshot.SunDirectionToSunWorld,
				Definition,
				Point,
				CameraFailure))
			{
				OutReason = CameraFailure;
				return EWorldResolveResult::Failed;
			}
			// Preserve the accepted deterministic camera position, but face due
			// north on the spherical surface: project world +Z into the local
			// tangent plane so the frame includes the daytime cloud horizon.
			const FVector GroundDayCameraLocation =
				Point.CameraWorldTransform.GetLocation();
			const FVector GroundDayRadialUp = (GroundDayCameraLocation
				- EnvironmentSnapshot.PlanetCenterWorld).GetSafeNormal();
			FVector GroundDayNorth = FVector::VectorPlaneProject(
				FVector::UpVector,
				GroundDayRadialUp).GetSafeNormal();
			if (GroundDayNorth.IsNearlyZero())
			{
				GroundDayNorth = EnvironmentDawnUp;
			}
			Point.LookAtWorld = GroundDayCameraLocation
				+ GroundDayNorth * 10000.0;
			if (!FABTSToonVisualCaptureMath::BuildLookAtCameraTransform(
				GroundDayCameraLocation,
				Point.LookAtWorld,
				GroundDayRadialUp,
				Point.CameraWorldTransform,
				&CameraFailure))
			{
				OutReason = CameraFailure;
				return EWorldResolveResult::Failed;
			}
			break;
		case EABTSToonVisualCaptureAnchor::EnvironmentGroundDawn:
			if (!BuildEnvironmentSurfacePoint(
				EnvironmentDawnUp,
				Definition,
				Point,
				CameraFailure))
			{
				OutReason = CameraFailure;
				return EWorldResolveResult::Failed;
			}
			break;
		case EABTSToonVisualCaptureAnchor::EnvironmentTerminatorSky:
		{
			// Frozen repro supplied by the T4-A1 PIE visual gate. Unlike a derived
			// terminator approximation, this pose observes the exact sky sector that
			// exposed the remaining staircase. UE's Details panel reports rotation
			// as X=Roll, Y=Pitch, Z=Yaw; FRotator stores Pitch, Yaw, Roll.
			const FVector ReportedCameraLocation(
				7330.839531,
				-8195.326369,
				-2132.005920);
			const FRotator ReportedCameraRotation(
				41.294936,
				59.050687,
				-87.010167);
			Point.CameraWorldTransform = FTransform(
				ReportedCameraRotation,
				ReportedCameraLocation);
			Point.LookAtWorld = ReportedCameraLocation
				+ ReportedCameraRotation.Vector() * 10000.0;
			Point.SemanticIdentityHash =
				ABTSToonVisualCaptureSubsystemPrivate::Mix64(
					EnvironmentSnapshot.IdentityHash,
					static_cast<uint64>(Definition.Anchor));
			break;
		}
		case EABTSToonVisualCaptureAnchor::EnvironmentBrightSkyBanding:
		{
			// Frozen bright-side repro supplied by the T4-A1 PIE visual gate.
			// Details reports X=Roll, Y=Pitch, Z=Yaw; FRotator stores
			// Pitch, Yaw, Roll.
			const FVector ReportedCameraLocation(
				5055.549427,
				-9511.377996,
				-4376.699690);
			const FRotator ReportedCameraRotation(
				82.960042,
				151.889451,
				35.775644);
			Point.CameraWorldTransform = FTransform(
				ReportedCameraRotation,
				ReportedCameraLocation);
			Point.LookAtWorld = ReportedCameraLocation
				+ ReportedCameraRotation.Vector() * 10000.0;
			Point.SemanticIdentityHash =
				ABTSToonVisualCaptureSubsystemPrivate::Mix64(
					EnvironmentSnapshot.IdentityHash,
					static_cast<uint64>(Definition.Anchor));
			break;
		}
		case EABTSToonVisualCaptureAnchor::EnvironmentTerminatorSunwardSky:
		{
			// Frozen PIE repro: the camera is on the night-side edge of the
			// terminator and looks almost exactly toward the accepted sun vector.
			// Details reports X=Roll, Y=Pitch, Z=Yaw.
			const FVector ReportedCameraLocation(
				8174.509529,
				-7018.140929,
				4234.994413);
			const FRotator ReportedCameraRotation(
				39.625340,
				161.729072,
				23.100288);
			Point.CameraWorldTransform = FTransform(
				ReportedCameraRotation,
				ReportedCameraLocation);
			Point.LookAtWorld = ReportedCameraLocation
				+ ReportedCameraRotation.Vector() * 10000.0;
			Point.SemanticIdentityHash =
				ABTSToonVisualCaptureSubsystemPrivate::Mix64(
					EnvironmentSnapshot.IdentityHash,
					static_cast<uint64>(Definition.Anchor));
			break;
		}
		case EABTSToonVisualCaptureAnchor::EnvironmentTerminatorAntiSunwardSky:
		{
			// Frozen PIE repro: the camera is on the day-side edge of the
			// terminator and looks almost exactly away from the accepted sun vector.
			// Details reports X=Roll, Y=Pitch, Z=Yaw.
			const FVector ReportedCameraLocation(
				4378.023910,
				-6442.873313,
				8680.170513);
			const FRotator ReportedCameraRotation(
				-57.560810,
				5.318501,
				-41.056320);
			Point.CameraWorldTransform = FTransform(
				ReportedCameraRotation,
				ReportedCameraLocation);
			Point.LookAtWorld = ReportedCameraLocation
				+ ReportedCameraRotation.Vector() * 10000.0;
			Point.SemanticIdentityHash =
				ABTSToonVisualCaptureSubsystemPrivate::Mix64(
					EnvironmentSnapshot.IdentityHash,
					static_cast<uint64>(Definition.Anchor));
			break;
		}
		case EABTSToonVisualCaptureAnchor::EnvironmentGroundNight:
			if (!BuildEnvironmentSurfacePoint(
				-EnvironmentSnapshot.SunDirectionToSunWorld,
				Definition,
				Point,
				CameraFailure))
			{
				OutReason = CameraFailure;
				return EWorldResolveResult::Failed;
			}
			break;
		case EABTSToonVisualCaptureAnchor::EnvironmentBacklitBirdParty:
		{
			FBox PartyBounds(EForceInit::ForceInit);
			uint64 PartyIdentity = EnvironmentSnapshot.IdentityHash;
			for (const AABTSM25BirdCharacter* Bird
				: Parties[0]->GetPartyMembers())
			{
				if (!IsValid(Bird))
				{
					OutReason = TEXT("Backlit bird-party member is invalid.");
					return EWorldResolveResult::Failed;
				}
				PartyBounds += Bird->GetComponentsBoundingBox(true);
				PartyIdentity = ABTSToonVisualCaptureSubsystemPrivate::Mix64(
					PartyIdentity,
					static_cast<uint64>(Bird->GetBirdId()));
			}
			if (!PartyBounds.IsValid)
			{
				OutReason = TEXT("Backlit bird-party bounds are invalid.");
				return EWorldResolveResult::Failed;
			}
			const FVector PartyCenter = PartyBounds.GetCenter();
			const FVector RadialUp = (PartyCenter
				- EnvironmentSnapshot.PlanetCenterWorld).GetSafeNormal();
			FVector SunTangent = FVector::VectorPlaneProject(
				EnvironmentSnapshot.SunDirectionToSunWorld,
				RadialUp).GetSafeNormal();
			if (SunTangent.IsNearlyZero())
			{
				SunTangent = ABTSToonVisualCaptureSubsystemPrivate::
					MakeStablePlanarDirection(
						StartTransform.GetUnitAxis(EAxis::X),
						RadialUp,
						EnvironmentDawnUp);
			}
			Point.LookAtWorld = PartyCenter + RadialUp * 65.0;
			const double FitDistance = FMath::Clamp(
				FABTSToonVisualCaptureMath::ComputePerspectiveFitDistanceCM(
					PartyBounds.GetExtent().Size(),
					Definition.FieldOfViewDegrees,
					static_cast<double>(ActualResolution.X) / ActualResolution.Y,
					1.35),
				700.0,
				1800.0);
			const FVector CameraLocation = Point.LookAtWorld
				- SunTangent * FitDistance
				+ RadialUp * FitDistance * 0.20;
			if (!FABTSToonVisualCaptureMath::BuildLookAtCameraTransform(
				CameraLocation,
				Point.LookAtWorld,
				RadialUp,
				Point.CameraWorldTransform,
				&CameraFailure))
			{
				OutReason = CameraFailure;
				return EWorldResolveResult::Failed;
			}
			Point.SemanticIdentityHash = ABTSToonVisualCaptureSubsystemPrivate::
				Mix64(PartyIdentity, static_cast<uint64>(Definition.Anchor));
			break;
		}
		case EABTSToonVisualCaptureAnchor::EnvironmentHighAltitude:
		{
			const FVector HighUp = (
				EnvironmentSnapshot.SunDirectionToSunWorld
				+ EnvironmentDawnUp * 0.55).GetSafeNormal();
			const FVector CameraLocation =
				EnvironmentSnapshot.PlanetCenterWorld
				+ HighUp * EnvironmentSnapshot.PlanetRadiusCM * 3.2
				+ EnvironmentDawnUp
					* EnvironmentSnapshot.PlanetRadiusCM * 0.35;
			Point.LookAtWorld = EnvironmentSnapshot.PlanetCenterWorld
				+ HighUp * EnvironmentSnapshot.PlanetRadiusCM * 0.15;
			if (!FABTSToonVisualCaptureMath::BuildLookAtCameraTransform(
				CameraLocation,
				Point.LookAtWorld,
				HighUp,
				Point.CameraWorldTransform,
				&CameraFailure))
			{
				OutReason = CameraFailure;
				return EWorldResolveResult::Failed;
			}
			Point.SemanticIdentityHash =
				ABTSToonVisualCaptureSubsystemPrivate::Mix64(
					EnvironmentSnapshot.IdentityHash,
					static_cast<uint64>(Definition.Anchor));
			break;
		}
		case EABTSToonVisualCaptureAnchor::HighAltitudeGround:
		case EABTSToonVisualCaptureAnchor::HighAltitudeCloudTop:
		case EABTSToonVisualCaptureAnchor::HighAltitudeTransitionMid:
		case EABTSToonVisualCaptureAnchor::HighAltitudeSatellite:
		case EABTSToonVisualCaptureAnchor::HighAltitudeSpace:
		{
			const FABTSStylizedEnvironmentParameters Presentation =
				FABTSStylizedRenderingControl::BuildEnvironmentParameters(
					EnvironmentSnapshot.PlanetCenterWorld,
					EnvironmentSnapshot.PlanetRadiusCM,
					EnvironmentSnapshot.SunDirectionToSunWorld,
					EABTSStylizedRenderProfile::GroundDay);
			const double StartRatio =
				Presentation.HighAltitudeTransitionStartCM
				/ Presentation.PlanetRadiusCM;
			const double EndRatio =
				Presentation.HighAltitudeTransitionEndCM
				/ Presentation.PlanetRadiusCM;
			double AltitudeRatio = 0.04;
			switch (Definition.Anchor)
			{
			case EABTSToonVisualCaptureAnchor::HighAltitudeCloudTop:
				AltitudeRatio = FMath::Min(
					StartRatio,
					(Presentation.CloudBaseAltitudeCM
						+ Presentation.CloudLayerHeightCM)
					/ Presentation.PlanetRadiusCM + 0.01);
				break;
			case EABTSToonVisualCaptureAnchor::HighAltitudeTransitionMid:
				AltitudeRatio = 0.5 * (StartRatio + EndRatio);
				break;
			case EABTSToonVisualCaptureAnchor::HighAltitudeSatellite:
				AltitudeRatio = 0.55;
				break;
			case EABTSToonVisualCaptureAnchor::HighAltitudeSpace:
				AltitudeRatio = 0.72;
				break;
			default:
				break;
			}
			const FVector RadialUp = (
				EnvironmentSnapshot.SunDirectionToSunWorld
				+ EnvironmentDawnUp * 0.28).GetSafeNormal();
			const FVector Tangent =
				ABTSToonVisualCaptureSubsystemPrivate::MakeStablePlanarDirection(
					EnvironmentDawnUp,
					RadialUp,
					StartTransform.GetUnitAxis(EAxis::X));
			const double CameraRadiusRatio = 1.0 + AltitudeRatio;
			const double TangentViewHeight = -FMath::Sqrt(FMath::Max(
				1.0 - 1.0 / (CameraRadiusRatio * CameraRadiusRatio),
				0.0));
			const double FramingViewHeight = FMath::Clamp(
				TangentViewHeight + 0.055,
				-0.96,
				0.25);
			const FVector ViewDirection = (
				RadialUp * FramingViewHeight
				+ Tangent * FMath::Sqrt(FMath::Max(
					1.0 - FramingViewHeight * FramingViewHeight,
					0.0))).GetSafeNormal();
			const FVector CameraLocation =
				EnvironmentSnapshot.PlanetCenterWorld
				+ RadialUp * EnvironmentSnapshot.PlanetRadiusCM
					* CameraRadiusRatio;
			Point.LookAtWorld = CameraLocation
				+ ViewDirection * EnvironmentSnapshot.PlanetRadiusCM * 2.0;
			if (!FABTSToonVisualCaptureMath::BuildLookAtCameraTransform(
				CameraLocation,
				Point.LookAtWorld,
				RadialUp,
				Point.CameraWorldTransform,
				&CameraFailure))
			{
				OutReason = CameraFailure;
				return EWorldResolveResult::Failed;
			}
			Point.SemanticIdentityHash =
				ABTSToonVisualCaptureSubsystemPrivate::Mix64(
					EnvironmentSnapshot.IdentityHash,
					static_cast<uint64>(Definition.Anchor));
			break;
		}
		case EABTSToonVisualCaptureAnchor::CloudR0Ground:
		case EABTSToonVisualCaptureAnchor::CloudR0Side:
		case EABTSToonVisualCaptureAnchor::CloudR0Above:
		case EABTSToonVisualCaptureAnchor::CloudR0FlyThrough:
		case EABTSToonVisualCaptureAnchor::CloudR0SideOrthogonal:
		case EABTSToonVisualCaptureAnchor::CloudR0GroundObliqueUp:
		case EABTSToonVisualCaptureAnchor::CloudR0GroundZenith:
		case EABTSToonVisualCaptureAnchor::CloudFieldGlobal:
		case EABTSToonVisualCaptureAnchor::CloudFieldFusion:
		case EABTSToonVisualCaptureAnchor::CloudFieldVariety:
		case EABTSToonVisualCaptureAnchor::CloudFieldNight:
		case EABTSToonVisualCaptureAnchor::CloudFieldTerminatorMega:
		case EABTSToonVisualCaptureAnchor::CloudTraversalBirdInside:
		case EABTSToonVisualCaptureAnchor::CloudTraversalCameraInside:
		case EABTSToonVisualCaptureAnchor::CloudTraversalBetween:
		case EABTSToonVisualCaptureAnchor::CloudTraversalBothInside:
		{
			if (CloudDefinitions.Num()
				!= FABTST4LowPolyCloudPrototype::IslandCount
				|| CloudLayoutHash == 0)
			{
				OutReason = TEXT("T4-A2R0 cloud layout is unavailable.");
				return EWorldResolveResult::Failed;
			}
			const bool bTraversalDiagnostic = Definition.Anchor
				== EABTSToonVisualCaptureAnchor::CloudTraversalBirdInside
				|| Definition.Anchor
					== EABTSToonVisualCaptureAnchor::CloudTraversalCameraInside
				|| Definition.Anchor
					== EABTSToonVisualCaptureAnchor::CloudTraversalBetween
				|| Definition.Anchor
					== EABTSToonVisualCaptureAnchor::CloudTraversalBothInside;
			if (bTraversalDiagnostic)
			{
				const FABTST4LowPolyCloudIslandDefinition& Cloud =
					CloudDefinitions[0];
				FVector CameraLocation = Cloud.CenterWorld;
				FVector BirdCenter = Cloud.CenterWorld;
				if (Definition.Anchor
					== EABTSToonVisualCaptureAnchor::CloudTraversalBirdInside)
				{
					CameraLocation = Cloud.CenterWorld
						- Cloud.TangentX * Cloud.ExtentsCM.X * 1.55
						+ Cloud.RadialUp * Cloud.ExtentsCM.Z * 0.20;
					BirdCenter = Cloud.CenterWorld
						+ Cloud.RadialUp * Cloud.ExtentsCM.Z * 0.04;
				}
				else if (Definition.Anchor
					== EABTSToonVisualCaptureAnchor::CloudTraversalCameraInside)
				{
					CameraLocation = Cloud.CenterWorld
						- Cloud.TangentX * Cloud.ExtentsCM.X * 0.10;
					BirdCenter = Cloud.CenterWorld
						+ Cloud.TangentX * Cloud.ExtentsCM.X * 1.22;
				}
				else if (Definition.Anchor
					== EABTSToonVisualCaptureAnchor::CloudTraversalBetween)
				{
					CameraLocation = Cloud.CenterWorld
						- Cloud.TangentX * Cloud.ExtentsCM.X * 1.32
						+ Cloud.RadialUp * Cloud.ExtentsCM.Z * 0.08;
					BirdCenter = Cloud.CenterWorld
						+ Cloud.TangentX * Cloud.ExtentsCM.X * 1.28;
				}
				else
				{
					CameraLocation = Cloud.CenterWorld
						- Cloud.TangentX * Cloud.ExtentsCM.X * 0.24
						- Cloud.TangentY * Cloud.ExtentsCM.Y * 0.10;
					BirdCenter = Cloud.CenterWorld
						+ Cloud.TangentX * Cloud.ExtentsCM.X * 0.26
						+ Cloud.TangentY * Cloud.ExtentsCM.Y * 0.08;
				}
				Point.bRelocateBirdPartyForDiagnostic = true;
				Point.DiagnosticBirdPartyCenterWorld = BirdCenter;
				Point.DiagnosticBirdPartyUp = Cloud.RadialUp;
				Point.LookAtWorld = BirdCenter;
				if (!FABTSToonVisualCaptureMath::BuildLookAtCameraTransform(
					CameraLocation,
					Point.LookAtWorld,
					Cloud.RadialUp,
					Point.CameraWorldTransform,
					&CameraFailure))
				{
					OutReason = CameraFailure;
					return EWorldResolveResult::Failed;
				}
				Point.SemanticIdentityHash =
					ABTSToonVisualCaptureSubsystemPrivate::Mix64(
						Cloud.LogicalCloudIdentityHash,
						static_cast<uint64>(Definition.Anchor));
				break;
			}
			const bool bCloudFieldDiagnostic = Definition.Anchor
				== EABTSToonVisualCaptureAnchor::CloudFieldGlobal
				|| Definition.Anchor
					== EABTSToonVisualCaptureAnchor::CloudFieldFusion
				|| Definition.Anchor
					== EABTSToonVisualCaptureAnchor::CloudFieldVariety
				|| Definition.Anchor
					== EABTSToonVisualCaptureAnchor::CloudFieldNight
				|| Definition.Anchor
					== EABTSToonVisualCaptureAnchor::CloudFieldTerminatorMega;
			if (bCloudFieldDiagnostic)
			{
				const uint64 LogicalCloudHash =
					FABTST4LowPolyCloudPrototype::
						ComputeLogicalCloudLayoutHash(CloudDefinitions);
				if (Definition.Anchor
					== EABTSToonVisualCaptureAnchor::CloudFieldGlobal)
				{
					const FVector ViewDirection = (
						EnvironmentSnapshot.SunDirectionToSunWorld * 0.62
						+ CloudDefinitions[0].RadialUp * 0.38).GetSafeNormal();
					const FVector CameraLocation =
						EnvironmentSnapshot.PlanetCenterWorld
						+ ViewDirection * EnvironmentSnapshot.PlanetRadiusCM * 3.25;
					Point.LookAtWorld = EnvironmentSnapshot.PlanetCenterWorld;
					FVector PreferredUp = FVector::VectorPlaneProject(
						FVector::UpVector, ViewDirection).GetSafeNormal();
					if (PreferredUp.IsNearlyZero())
					{
						FVector UnusedAxis;
						ViewDirection.FindBestAxisVectors(PreferredUp, UnusedAxis);
					}
					if (!FABTSToonVisualCaptureMath::BuildLookAtCameraTransform(
						CameraLocation,
						Point.LookAtWorld,
						PreferredUp,
						Point.CameraWorldTransform,
						&CameraFailure))
					{
						OutReason = CameraFailure;
						return EWorldResolveResult::Failed;
					}
					Point.SemanticIdentityHash =
						ABTSToonVisualCaptureSubsystemPrivate::Mix64(
							LogicalCloudHash,
							static_cast<uint64>(Definition.Anchor));
					break;
				}
				if (Definition.Anchor
					== EABTSToonVisualCaptureAnchor::CloudFieldNight)
				{
					int32 NightCloudIndex = INDEX_NONE;
					double MinimumSolarHeight = 2.0;
					for (int32 CloudIndex = 0;
						CloudIndex < FABTST4LowPolyCloudPrototype::GlobalIslandCount;
						++CloudIndex)
					{
						const double SolarHeight = FVector::DotProduct(
							CloudDefinitions[CloudIndex].RadialUp,
							EnvironmentSnapshot.SunDirectionToSunWorld);
						if (SolarHeight < MinimumSolarHeight)
						{
							MinimumSolarHeight = SolarHeight;
							NightCloudIndex = CloudIndex;
						}
					}
					if (!CloudDefinitions.IsValidIndex(NightCloudIndex))
					{
						OutReason = TEXT("Night-cloud diagnostic could not select a background cloud.");
						return EWorldResolveResult::Failed;
					}
					const FABTST4LowPolyCloudIslandDefinition& NightCloud =
						CloudDefinitions[NightCloudIndex];
					const double SurfaceRadius =
						Planet.GetSurfaceRadiusAtDirection(NightCloud.RadialUp);
					const FVector CameraLocation =
						EnvironmentSnapshot.PlanetCenterWorld
						+ NightCloud.RadialUp * (SurfaceRadius + 175.0)
						- NightCloud.TangentX * NightCloud.ExtentsCM.X * 2.55;
					Point.LookAtWorld = NightCloud.CenterWorld
						- NightCloud.RadialUp * NightCloud.ExtentsCM.Z * 0.08;
					if (!FABTSToonVisualCaptureMath::BuildLookAtCameraTransform(
						CameraLocation,
						Point.LookAtWorld,
						NightCloud.RadialUp,
						Point.CameraWorldTransform,
						&CameraFailure))
					{
						OutReason = CameraFailure;
						return EWorldResolveResult::Failed;
					}
					Point.SemanticIdentityHash =
						ABTSToonVisualCaptureSubsystemPrivate::Mix64(
							NightCloud.LogicalCloudIdentityHash,
							static_cast<uint64>(Definition.Anchor));
					break;
				}
				if (Definition.Anchor
					== EABTSToonVisualCaptureAnchor::CloudFieldTerminatorMega)
				{
					FVector ClusterDirectionSum = FVector::ZeroVector;
					FVector ClusterCenterSum = FVector::ZeroVector;
					int32 ClusterMemberCount = 0;
					for (const FABTST4LowPolyCloudIslandDefinition& Cloud
						: CloudDefinitions)
					{
						if (!Cloud.bTerminatorMegaCluster)
						{
							continue;
						}
						ClusterDirectionSum += Cloud.RadialUp;
						ClusterCenterSum += Cloud.CenterWorld;
						++ClusterMemberCount;
					}
					if (ClusterMemberCount
						!= FABTST4LowPolyCloudPrototype::
							TerminatorMegaClusterIslandCount)
					{
						OutReason = TEXT("Terminator mega-cluster diagnostic is incomplete.");
						return EWorldResolveResult::Failed;
					}
					const FVector ClusterUp = ClusterDirectionSum.GetSafeNormal();
					const FVector ClusterCenter = ClusterCenterSum
						/ static_cast<double>(ClusterMemberCount);
					FVector AlongTerminator = FVector::CrossProduct(
						ClusterUp,
						EnvironmentSnapshot.SunDirectionToSunWorld).GetSafeNormal();
					if (AlongTerminator.IsNearlyZero())
					{
						FVector UnusedAxis;
						ClusterUp.FindBestAxisVectors(AlongTerminator, UnusedAxis);
					}
					const double SpanDegrees = FABTST4LowPolyCloudPrototype::
						ComputeTerminatorMegaClusterAngularSpanDegrees(
							CloudDefinitions);
					const double SpanRadiusCM = EnvironmentSnapshot.PlanetRadiusCM
						* FMath::Sin(FMath::DegreesToRadians(SpanDegrees * 0.5));
					const FVector CameraLocation = ClusterCenter
						- AlongTerminator * SpanRadiusCM * 2.65
						+ ClusterUp * SpanRadiusCM * 0.42;
					Point.LookAtWorld = ClusterCenter;
					if (!FABTSToonVisualCaptureMath::BuildLookAtCameraTransform(
						CameraLocation,
						Point.LookAtWorld,
						ClusterUp,
						Point.CameraWorldTransform,
						&CameraFailure))
					{
						OutReason = CameraFailure;
						return EWorldResolveResult::Failed;
					}
					Point.SemanticIdentityHash =
						ABTSToonVisualCaptureSubsystemPrivate::Mix64(
							LogicalCloudHash,
							static_cast<uint64>(Definition.Anchor));
					break;
				}

				int32 FirstCloudIndex = 0;
				int32 SecondCloudIndex = 1;
				double BestAlignment = -2.0;
				double BestVarietyRatio = 0.0;
				for (int32 FirstIndex = 0;
					FirstIndex < FABTST4LowPolyCloudPrototype::GlobalIslandCount;
					++FirstIndex)
				{
					for (int32 SecondIndex = FirstIndex + 1;
						SecondIndex < FABTST4LowPolyCloudPrototype::GlobalIslandCount;
						++SecondIndex)
					{
						const double Alignment = FVector::DotProduct(
							CloudDefinitions[FirstIndex].RadialUp,
							CloudDefinitions[SecondIndex].RadialUp);
						const double FirstArea =
							CloudDefinitions[FirstIndex].ExtentsCM.X
							* CloudDefinitions[FirstIndex].ExtentsCM.Y;
						const double SecondArea =
							CloudDefinitions[SecondIndex].ExtentsCM.X
							* CloudDefinitions[SecondIndex].ExtentsCM.Y;
						const double VarietyRatio = FMath::Max(
							FirstArea, SecondArea) / FMath::Max(
							1.0, FMath::Min(FirstArea, SecondArea));
						const bool bPreferVariety = Definition.Anchor
							== EABTSToonVisualCaptureAnchor::CloudFieldVariety;
						const bool bAcceptVariety = bPreferVariety
							&& Alignment >= FMath::Cos(
								FMath::DegreesToRadians(38.0))
							&& VarietyRatio > BestVarietyRatio;
						const bool bAcceptFusion = !bPreferVariety
							&& Alignment > BestAlignment;
						if (bAcceptVariety || bAcceptFusion)
						{
							BestAlignment = Alignment;
							BestVarietyRatio = VarietyRatio;
							FirstCloudIndex = FirstIndex;
							SecondCloudIndex = SecondIndex;
						}
					}
				}
				const FABTST4LowPolyCloudIslandDefinition& FirstCloud =
					CloudDefinitions[FirstCloudIndex];
				const FABTST4LowPolyCloudIslandDefinition& SecondCloud =
					CloudDefinitions[SecondCloudIndex];
				const FVector PairAxis = (SecondCloud.CenterWorld
					- FirstCloud.CenterWorld).GetSafeNormal();
				const FVector PairMidpoint = (FirstCloud.CenterWorld
					+ SecondCloud.CenterWorld) * 0.5;
				const FVector PairUp = (PairMidpoint
					- EnvironmentSnapshot.PlanetCenterWorld).GetSafeNormal();
				FVector PairSide = FVector::CrossProduct(
					PairAxis, PairUp).GetSafeNormal();
				if (PairSide.IsNearlyZero())
				{
					FVector UnusedFallbackAxis;
					PairUp.FindBestAxisVectors(PairSide, UnusedFallbackAxis);
				}
				const double PairSpanCM = FVector::Distance(
					FirstCloud.CenterWorld, SecondCloud.CenterWorld);
				const double MaximumCloudDepthCM = FMath::Max(
					FirstCloud.ExtentsCM.Z, SecondCloud.ExtentsCM.Z);
				FVector CameraLocation = PairMidpoint;
				const FVector PreferredUp = PairUp;
				Point.LookAtWorld = PairMidpoint;
				if (Definition.Anchor
					== EABTSToonVisualCaptureAnchor::CloudFieldVariety)
				{
					CameraLocation = PairMidpoint
						+ PairUp * PairSpanCM * 2.55
						+ PairSide * PairSpanCM * 0.42;
				}
				else
				{
					CameraLocation = PairMidpoint
						- PairAxis * PairSpanCM * 2.35
						+ PairUp * MaximumCloudDepthCM * 0.85;
					Point.LookAtWorld = PairMidpoint
						+ PairUp * MaximumCloudDepthCM * 0.12;
				}
				if (!FABTSToonVisualCaptureMath::BuildLookAtCameraTransform(
					CameraLocation,
					Point.LookAtWorld,
					PreferredUp,
					Point.CameraWorldTransform,
					&CameraFailure))
				{
					OutReason = CameraFailure;
					return EWorldResolveResult::Failed;
				}
				Point.SemanticIdentityHash =
					ABTSToonVisualCaptureSubsystemPrivate::Mix64(
						LogicalCloudHash,
						static_cast<uint64>(Definition.Anchor));
				break;
			}

			const int32 CloudIndex = Definition.Anchor
				== EABTSToonVisualCaptureAnchor::CloudR0FlyThrough ? 1 : 0;
			const FABTST4LowPolyCloudIslandDefinition& Cloud =
				CloudDefinitions[CloudIndex];
			FVector CameraLocation = Cloud.CenterWorld;
			Point.LookAtWorld = Cloud.CenterWorld;
			FVector PreferredUp = Cloud.RadialUp;
			if (Definition.Anchor
				== EABTSToonVisualCaptureAnchor::CloudR0Ground)
			{
				const double SurfaceRadius =
					Planet.GetSurfaceRadiusAtDirection(Cloud.RadialUp);
				CameraLocation = EnvironmentSnapshot.PlanetCenterWorld
					+ Cloud.RadialUp * (SurfaceRadius + 180.0)
					- Cloud.TangentX * Cloud.ExtentsCM.X * 2.65;
				Point.LookAtWorld = Cloud.CenterWorld
					+ Cloud.TangentX * Cloud.ExtentsCM.X * 0.08;
				PreferredUp = Cloud.TangentY;
			}
			else if (Definition.Anchor
				== EABTSToonVisualCaptureAnchor::CloudR0Side)
			{
				CameraLocation = Cloud.CenterWorld
					- Cloud.TangentX * Cloud.ExtentsCM.X * 2.80
					+ Cloud.RadialUp * Cloud.ExtentsCM.Z * 0.18;
			}
			else if (Definition.Anchor
				== EABTSToonVisualCaptureAnchor::CloudR0SideOrthogonal)
			{
				CameraLocation = Cloud.CenterWorld
					- Cloud.TangentY * Cloud.ExtentsCM.Y * 2.80
					+ Cloud.RadialUp * Cloud.ExtentsCM.Z * 0.18;
			}
			else if (Definition.Anchor
				== EABTSToonVisualCaptureAnchor::CloudR0Above)
			{
				CameraLocation = Cloud.CenterWorld
					+ Cloud.RadialUp * Cloud.ExtentsCM.X * 3.0
					+ Cloud.TangentY * Cloud.ExtentsCM.Y * 0.18;
				PreferredUp = Cloud.TangentY;
			}
			else if (Definition.Anchor
				== EABTSToonVisualCaptureAnchor::CloudR0GroundObliqueUp)
			{
				const double SurfaceRadius =
					Planet.GetSurfaceRadiusAtDirection(Cloud.RadialUp);
				CameraLocation = EnvironmentSnapshot.PlanetCenterWorld
					+ Cloud.RadialUp * (SurfaceRadius + 165.0)
					- Cloud.TangentX * Cloud.ExtentsCM.X * 2.10
					- Cloud.TangentY * Cloud.ExtentsCM.Y * 0.24;
				Point.LookAtWorld = Cloud.CenterWorld
					+ Cloud.TangentX * Cloud.ExtentsCM.X * 0.08
					- Cloud.RadialUp * Cloud.ExtentsCM.Z * 0.10;
				PreferredUp = Cloud.RadialUp;
			}
			else if (Definition.Anchor
				== EABTSToonVisualCaptureAnchor::CloudR0GroundZenith)
			{
				const double SurfaceRadius =
					Planet.GetSurfaceRadiusAtDirection(Cloud.RadialUp);
				CameraLocation = EnvironmentSnapshot.PlanetCenterWorld
					+ Cloud.RadialUp * (SurfaceRadius + 165.0);
				Point.LookAtWorld = Cloud.CenterWorld
					- Cloud.RadialUp * Cloud.ExtentsCM.Z * 0.04;
				PreferredUp = Cloud.TangentY;
			}
			else
			{
				// R0 freezes the entry-side view of a path that crosses the second
				// island. R1 adds the bounded interior-fog response and moving
				// temporal gate after the exterior silhouette is accepted.
				CameraLocation = Cloud.CenterWorld
					- Cloud.TangentY * Cloud.ExtentsCM.Y * 1.75;
				Point.LookAtWorld = Cloud.CenterWorld
					+ Cloud.TangentY * Cloud.ExtentsCM.Y * 2.0;
				PreferredUp = Cloud.RadialUp;
			}
			if (!FABTSToonVisualCaptureMath::BuildLookAtCameraTransform(
				CameraLocation,
				Point.LookAtWorld,
				PreferredUp,
				Point.CameraWorldTransform,
				&CameraFailure))
			{
				OutReason = CameraFailure;
				return EWorldResolveResult::Failed;
			}
			Point.SemanticIdentityHash =
				ABTSToonVisualCaptureSubsystemPrivate::Mix64(
					CloudLayoutHash,
					static_cast<uint64>(Definition.Anchor));
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

		FABTSToonEnvironmentSnapshot PointEnvironment;
		FString PointEnvironmentFailure;
		if (!FABTSToonEnvironmentResolver::BuildSnapshot(
			EnvironmentSnapshot.PlanetCenterWorld,
			EnvironmentSnapshot.PlanetRadiusCM,
			EnvironmentSnapshot.SunDirectionToSunWorld,
			Definition.StyleProfile,
			EnvironmentSnapshot.WorldSeed,
			EnvironmentSnapshot.GeneratorVersion,
			EnvironmentSnapshot.GenerationAttempt,
			EnvironmentSnapshot.bSourceWorldAccepted,
			PointEnvironment,
			&PointEnvironmentFailure))
		{
			OutReason = FString::Printf(
				TEXT("Capture point environment identity failed: %s"),
				*PointEnvironmentFailure);
			return EWorldResolveResult::Failed;
		}
		Point.EnvironmentSnapshotHash = PointEnvironment.IdentityHash;

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
	SavedDiagnosticPassMask =
		FABTSStylizedRenderingControl::GetDiagnosticPassMask();
	IConsoleVariable* ShadowQuality =
		IConsoleManager::Get().FindConsoleVariable(TEXT("r.ShadowQuality"));
	if (ShadowQuality == nullptr)
	{
		OutFailure = TEXT("r.ShadowQuality is unavailable for T4 isolation.");
		return false;
	}
	SavedShadowQuality = ShadowQuality->GetInt();
	SavedShadowQualitySetBy =
		static_cast<uint32>(ShadowQuality->GetFlags())
		& static_cast<uint32>(ECVF_SetByMask);
	bShadowQualityStateCaptured = true;
	if (RunConfig.Suite == EABTSToonVisualCaptureSuite::ToonT4A0
		&& SavedShadowQuality <= 0)
	{
		OutFailure = TEXT("ToonT4A0 requires a shadow-enabled source baseline.");
		return false;
	}
	bRuntimeStateCaptured = true;
	if (!CaptureBirdPartyTransforms(OutFailure))
	{
		return false;
	}

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

bool UABTSToonVisualCaptureSubsystem::CaptureBirdPartyTransforms(
	FString& OutFailure)
{
	SavedBirdPartyTransforms.Reset();
	bBirdPartyTransformsCaptured = false;
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		OutFailure = TEXT("World is unavailable while capturing bird transforms.");
		return false;
	}
	AABTSBirdParty* Party = nullptr;
	for (TActorIterator<AABTSBirdParty> It(World); It; ++It)
	{
		if (It->IsPartyReady())
		{
			Party = *It;
			break;
		}
	}
	if (!IsValid(Party))
	{
		OutFailure = TEXT("Bird party is unavailable for traversal diagnostics.");
		return false;
	}
	FBox Bounds(EForceInit::ForceInit);
	for (AABTSM25BirdCharacter* Bird : Party->GetPartyMembers())
	{
		if (!IsValid(Bird))
		{
			OutFailure = TEXT("Bird party contains an invalid member.");
			return false;
		}
		FABTSToonSavedActorTransform Saved;
		Saved.Actor = Bird;
		Saved.Transform = Bird->GetActorTransform();
		SavedBirdPartyTransforms.Add(Saved);
		Bounds += Bird->GetComponentsBoundingBox(true);
	}
	if (SavedBirdPartyTransforms.IsEmpty() || !Bounds.IsValid)
	{
		OutFailure = TEXT("Bird party transform baseline is empty.");
		return false;
	}
	SavedBirdPartyCenterWorld = Bounds.GetCenter();
	SavedBirdPartyUp = EnvironmentSnapshot.IsValid()
		? (SavedBirdPartyCenterWorld
			- EnvironmentSnapshot.PlanetCenterWorld).GetSafeNormal()
		: FVector::UpVector;
	if (SavedBirdPartyUp.IsNearlyZero())
	{
		SavedBirdPartyUp = FVector::UpVector;
	}
	bBirdPartyTransformsCaptured = true;
	return true;
}

bool UABTSToonVisualCaptureSubsystem::ApplyCurrentDiagnosticBirdPartyPlacement(
	FString& OutFailure)
{
	if (!bBirdPartyTransformsCaptured
		|| !ResolvedPoints.IsValidIndex(CurrentPointIndex))
	{
		OutFailure = TEXT("Bird-party capture baseline is unavailable.");
		return false;
	}
	const FABTSToonResolvedCapturePoint& Point =
		ResolvedPoints[CurrentPointIndex];
	const FVector TargetCenter = Point.bRelocateBirdPartyForDiagnostic
		? Point.DiagnosticBirdPartyCenterWorld
		: SavedBirdPartyCenterWorld;
	const FVector TargetUp = Point.bRelocateBirdPartyForDiagnostic
		? Point.DiagnosticBirdPartyUp
		: SavedBirdPartyUp;
	const FQuat FrameRotation = FQuat::FindBetweenNormals(
		SavedBirdPartyUp, TargetUp);
	for (const FABTSToonSavedActorTransform& Saved
		: SavedBirdPartyTransforms)
	{
		AActor* Actor = Saved.Actor.Get();
		if (!IsValid(Actor))
		{
			OutFailure = TEXT("A captured bird disappeared during diagnostics.");
			return false;
		}
		FTransform Target = Saved.Transform;
		Target.SetLocation(
			TargetCenter
			+ FrameRotation.RotateVector(
				Saved.Transform.GetLocation() - SavedBirdPartyCenterWorld));
		Target.SetRotation(FrameRotation * Saved.Transform.GetRotation());
		Actor->SetActorTransform(
			Target, false, nullptr, ETeleportType::TeleportPhysics);
	}
	return true;
}

void UABTSToonVisualCaptureSubsystem::RestoreBirdPartyTransforms()
{
	if (!bBirdPartyTransformsCaptured)
	{
		return;
	}
	for (const FABTSToonSavedActorTransform& Saved
		: SavedBirdPartyTransforms)
	{
		if (AActor* Actor = Saved.Actor.Get())
		{
			Actor->SetActorTransform(
				Saved.Transform, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}
	SavedBirdPartyTransforms.Reset();
	bBirdPartyTransformsCaptured = false;
}

void UABTSToonVisualCaptureSubsystem::BeginCurrentVariant()
{
	if (!ResolvedPoints.IsValidIndex(CurrentPointIndex)
		|| !VariantDefinitions.IsValidIndex(CurrentVariantIndex)
		|| !IsValid(CaptureCamera)
		|| CaptureCamera->GetCameraComponent() == nullptr)
	{
		FinishCapture(false, TEXT("Capture variant index or camera is invalid."));
		return;
	}

	const FABTSToonResolvedCapturePoint& Point =
		ResolvedPoints[CurrentPointIndex];
	const FABTSToonDiagnosticVariantDefinition& Variant =
		VariantDefinitions[CurrentVariantIndex];
	FABTSStylizedRenderingControl::SetProfile(Point.Definition.StyleProfile);
	FABTSStylizedRenderingControl::SetDiagnosticPassMask(Variant.PassMask);
	FABTSStylizedRenderingControl::SetEnabled(Variant.bStyleEnabled);
	FString BirdPlacementFailure;
	if (!ApplyCurrentDiagnosticBirdPartyPlacement(BirdPlacementFailure))
	{
		FinishCapture(false, BirdPlacementFailure);
		return;
	}
	IConsoleVariable* ShadowQuality =
		IConsoleManager::Get().FindConsoleVariable(TEXT("r.ShadowQuality"));
	if (ShadowQuality == nullptr)
	{
		FinishCapture(false, TEXT("r.ShadowQuality disappeared during capture."));
		return;
	}
	ShadowQuality->Set(
		Variant.bShadowsEnabled ? SavedShadowQuality : 0,
		ECVF_SetByCode);
	if (UWorld* World = GetWorld())
	{
		if (UABTSStylizedRenderingWorldSubsystem* StyleSubsystem =
			World->GetSubsystem<UABTSStylizedRenderingWorldSubsystem>())
		{
			StyleSubsystem->RefreshNow();
		}
	}
	if (FABTSStylizedRenderingControl::GetProfile()
			!= Point.Definition.StyleProfile
		|| FABTSStylizedRenderingControl::IsEnabled()
			!= Variant.bStyleEnabled
		|| FABTSStylizedRenderingControl::GetDiagnosticPassMask()
			!= Variant.PassMask
		|| ShadowQuality->GetInt()
			!= (Variant.bShadowsEnabled ? SavedShadowQuality : 0))
	{
		FinishCapture(
			false,
			TEXT("The stylized rendering seam did not retain the requested Style/Profile/pass/shadow state."));
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
	if (UWorld* World = GetWorld())
	{
		if (UABTSStylizedRenderingWorldSubsystem* StyleSubsystem =
			World->GetSubsystem<UABTSStylizedRenderingWorldSubsystem>())
		{
			StyleSubsystem->RefreshCloudTraversalNow(true);
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
		TEXT("[ABTS][%s][Variant] Point=%s Anchor=%s Variant=%s Style=%s Tone=%d Outline=%d Shadows=%d Profile=%s StyleImplementation=%d PoseHash=%s SemanticHash=%s EnvironmentHash=%s Warmup=%d"),
		FABTSToonVisualCaptureMath::LexToString(RunConfig.Suite),
		*Point.Definition.PointId.ToString(),
		FABTSToonVisualCaptureMath::LexToString(Point.Definition.Anchor),
		*Variant.VariantId.ToString(),
		Variant.bStyleEnabled ? TEXT("On") : TEXT("Off"),
		(static_cast<uint8>(Variant.PassMask)
			& static_cast<uint8>(EABTSStylizedDiagnosticPassMask::Tone)) != 0,
		(static_cast<uint8>(Variant.PassMask)
			& static_cast<uint8>(EABTSStylizedDiagnosticPassMask::Outline)) != 0,
		Variant.bShadowsEnabled ? 1 : 0,
		FABTSToonVisualCaptureMath::LexToString(
			Point.Definition.StyleProfile),
		FABTSStylizedRenderingControl::GetImplementationVersion(),
		*ABTSToonVisualCaptureSubsystemPrivate::Hex64(Point.CameraPoseHash),
		*ABTSToonVisualCaptureSubsystemPrivate::Hex64(
			Point.SemanticIdentityHash),
		*ABTSToonVisualCaptureSubsystemPrivate::Hex64(
			Point.EnvironmentSnapshotHash),
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
	if (!VariantDefinitions.IsValidIndex(CurrentVariantIndex))
	{
		OutFailure = TEXT("The requested diagnostic variant is unavailable.");
		return false;
	}
	const FABTSToonDiagnosticVariantDefinition& Variant =
		VariantDefinitions[CurrentVariantIndex];
	const IConsoleVariable* ShadowQuality =
		IConsoleManager::Get().FindConsoleVariable(TEXT("r.ShadowQuality"));
	if (FABTSStylizedRenderingControl::GetProfile()
			!= Point.Definition.StyleProfile
		|| FABTSStylizedRenderingControl::IsEnabled()
			!= Variant.bStyleEnabled
		|| FABTSStylizedRenderingControl::GetDiagnosticPassMask()
			!= Variant.PassMask
		|| ShadowQuality == nullptr
		|| ShadowQuality->GetInt()
			!= (Variant.bShadowsEnabled ? SavedShadowQuality : 0))
	{
		OutFailure = TEXT("Style/Profile/pass/shadow state changed during camera warmup.");
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
				TEXT("Diagnostic variants have divergent effective camera identity at %s."),
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
	const FABTSToonDiagnosticVariantDefinition& Variant =
		VariantDefinitions[CurrentVariantIndex];
	Record.PointId = Point.Definition.PointId;
	Record.Anchor = Point.Definition.Anchor;
	Record.Profile = Point.Definition.StyleProfile;
	Record.VariantId = Variant.VariantId;
	Record.bStyleEnabled = Variant.bStyleEnabled;
	Record.PassMask = Variant.PassMask;
	Record.bShadowsEnabled = Variant.bShadowsEnabled;
	Record.StyleImplementationVersion =
		FABTSStylizedRenderingControl::GetImplementationVersion();
	Record.CameraWorldTransform = Point.CameraWorldTransform;
	Record.LookAtWorld = Point.LookAtWorld;
	Record.FieldOfViewDegrees = Point.Definition.FieldOfViewDegrees;
	Record.SemanticIdentityHash = Point.SemanticIdentityHash;
	Record.CameraPoseHash = Point.CameraPoseHash;
	Record.EffectiveCameraPoseHash = CurrentEffectiveCameraPoseHash;
	Record.EnvironmentSnapshotHash = Point.EnvironmentSnapshotHash;
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
	const FABTSToonDiagnosticVariantDefinition& Variant =
		VariantDefinitions[CurrentVariantIndex];
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][ToonT0][GPUProfileMarker] Run=%s Point=%s Style=%s PoseHash=%s EffectivePoseHash=%s Sample=%d/%d"),
		*RunId,
		*Point.Definition.PointId.ToString(),
		*Variant.VariantId.ToString(),
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
	const FABTSToonDiagnosticVariantDefinition& Variant =
		VariantDefinitions[CurrentVariantIndex];
	FABTSToonVisualCaptureManifestRecord Record;
	Record.PointId = Point.Definition.PointId;
	Record.Anchor = Point.Definition.Anchor;
	Record.Profile = Point.Definition.StyleProfile;
	Record.VariantId = Variant.VariantId;
	Record.bStyleEnabled = Variant.bStyleEnabled;
	Record.PassMask = Variant.PassMask;
	Record.bShadowsEnabled = Variant.bShadowsEnabled;
	Record.StyleImplementationVersion =
		FABTSStylizedRenderingControl::GetImplementationVersion();
	Record.CameraWorldTransform = Point.CameraWorldTransform;
	Record.LookAtWorld = Point.LookAtWorld;
	Record.FieldOfViewDegrees = Point.Definition.FieldOfViewDegrees;
	Record.SemanticIdentityHash = Point.SemanticIdentityHash;
	Record.CameraPoseHash = Point.CameraPoseHash;
	Record.EffectiveCameraPoseHash = CurrentEffectiveCameraPoseHash;
	Record.EnvironmentSnapshotHash = Point.EnvironmentSnapshotHash;
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
	++CurrentVariantIndex;
	if (CurrentVariantIndex < VariantDefinitions.Num())
	{
		BeginCurrentVariant();
		return;
	}

	CurrentVariantIndex = 0;
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
	const int32 ExpectedRecordCount =
		ResolvedPoints.Num() * VariantDefinitions.Num();
	if (bEffectiveSuccess
		&& ManifestRecords.Num() != ExpectedRecordCount)
	{
		bEffectiveSuccess = false;
		EffectiveReason = FString::Printf(
			TEXT("Record count mismatch: actual=%d expected=%d."),
			ManifestRecords.Num(),
			ExpectedRecordCount);
	}
	if (bEffectiveSuccess
		&& RunConfig.Suite == EABTSToonVisualCaptureSuite::ToonT4A2)
	{
		UWorld* World = GetWorld();
		const UABTSStylizedRenderingWorldSubsystem* StyleSubsystem =
			World != nullptr
				? World->GetSubsystem<UABTSStylizedRenderingWorldSubsystem>()
				: nullptr;
		const bool bRuntimeCloudActive = StyleSubsystem != nullptr
			&& StyleSubsystem->IsLowPolyCloudPrototypeActive();
		const bool bExpectedRuntimeCloudActive =
			!RunConfig.bDisableLowPolyCloudsForPerformanceBaseline;
		if (bRuntimeCloudActive != bExpectedRuntimeCloudActive)
		{
			bEffectiveSuccess = false;
			EffectiveReason = FString::Printf(
				TEXT("A2.4 runtime cloud baseline mismatch: Active=%d Expected=%d."),
				bRuntimeCloudActive ? 1 : 0,
				bExpectedRuntimeCloudActive ? 1 : 0);
		}
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
			TEXT("[ABTS][%s][Terminal] Success=1 Mode=%s Records=%d Expected=%d Output=%s Reason=None"),
			FABTSToonVisualCaptureMath::LexToString(RunConfig.Suite),
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
			TEXT("[ABTS][%s][Terminal] Success=0 Mode=%s Records=%d Expected=%d Output=%s Reason=%s"),
			FABTSToonVisualCaptureMath::LexToString(RunConfig.Suite),
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
		RestoreBirdPartyTransforms();
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
		FABTSStylizedRenderingControl::SetDiagnosticPassMask(
			SavedDiagnosticPassMask);
		FABTSStylizedRenderingControl::SetEnabled(bSavedStyleEnabled);
		if (bShadowQualityStateCaptured)
		{
			if (IConsoleVariable* ShadowQuality =
				IConsoleManager::Get().FindConsoleVariable(
					TEXT("r.ShadowQuality")))
			{
				ShadowQuality->Set(
					SavedShadowQuality,
					static_cast<EConsoleVariableFlags>(
						SavedShadowQualitySetBy));
			}
			bShadowQualityStateCaptured = false;
		}
		if (UWorld* World = GetWorld())
		{
			if (UABTSStylizedRenderingWorldSubsystem* StyleSubsystem =
				World->GetSubsystem<UABTSStylizedRenderingWorldSubsystem>())
			{
				StyleSubsystem->RefreshNow();
			}
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
	if (bScreenPercentageStateCaptured)
	{
		if (IConsoleVariable* ScreenPercentage =
			IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
		{
			ScreenPercentage->Set(
				SavedScreenPercentage,
				static_cast<EConsoleVariableFlags>(
					SavedScreenPercentageSetBy));
		}
		bScreenPercentageStateCaptured = false;
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
	TArray<FABTST4LowPolyCloudIslandDefinition> ManifestLogicalClouds;
	uint64 ManifestLogicalCloudLayoutHash = 0;
	int32 ManifestCloudFusionPairCount = 0;
	if (RunConfig.Suite == EABTSToonVisualCaptureSuite::ToonT4A2
		&& EnvironmentSnapshot.IsValid())
	{
		const FABTSStylizedEnvironmentParameters CloudPresentation =
			FABTSStylizedRenderingControl::BuildEnvironmentParameters(
				EnvironmentSnapshot.PlanetCenterWorld,
				EnvironmentSnapshot.PlanetRadiusCM,
				EnvironmentSnapshot.SunDirectionToSunWorld,
				EABTSStylizedRenderProfile::GroundDay);
		ManifestLogicalClouds = FABTST4LowPolyCloudPrototype::BuildDefinitions(
			EnvironmentSnapshot.PlanetCenterWorld,
			EnvironmentSnapshot.PlanetRadiusCM,
			CloudPresentation.StarSeed ^ 0xC10DF13Du,
			EnvironmentSnapshot.SunDirectionToSunWorld,
			CloudPresentation.CloudBaseAltitudeCM,
			CloudPresentation.CloudLayerHeightCM);
		ManifestLogicalCloudLayoutHash = FABTST4LowPolyCloudPrototype::
			ComputeLogicalCloudLayoutHash(ManifestLogicalClouds);
		ManifestCloudFusionPairCount = FABTST4LowPolyCloudPrototype::
			CountCloudFusionPairs(ManifestLogicalClouds);
	}
	const int32 ManifestTerminatorMegaCloudCount =
		FABTST4LowPolyCloudPrototype::CountTerminatorMegaClusterClouds(
			ManifestLogicalClouds);
	const double ManifestTerminatorMegaSpanDegrees =
		FABTST4LowPolyCloudPrototype::ComputeTerminatorMegaClusterAngularSpanDegrees(
			ManifestLogicalClouds);
	const bool bManifestTerminatorMegaConnected =
		FABTST4LowPolyCloudPrototype::IsTerminatorMegaClusterEnvelopeConnected(
			ManifestLogicalClouds);
	UWorld* ManifestWorld = GetWorld();
	const UABTSStylizedRenderingWorldSubsystem* ManifestStyleSubsystem =
		ManifestWorld != nullptr
			? ManifestWorld->GetSubsystem<UABTSStylizedRenderingWorldSubsystem>()
			: nullptr;
	const bool bRuntimeCloudPrototypeActive = ManifestStyleSubsystem != nullptr
		&& ManifestStyleSubsystem->IsLowPolyCloudPrototypeActive();
	const int32 RuntimeLogicalCloudCount = ManifestStyleSubsystem != nullptr
		? ManifestStyleSubsystem->GetLowPolyLogicalCloudCount()
		: 0;
	const uint64 RuntimeLogicalCloudLayoutHash =
		ManifestStyleSubsystem != nullptr
			? ManifestStyleSubsystem->GetLowPolyLogicalCloudLayoutHash()
			: 0;
	const int32 RuntimeCloudMaterialBatchCount =
		ManifestStyleSubsystem != nullptr
			? ManifestStyleSubsystem->GetLowPolyCloudMaterialBatchCount()
			: 0;

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(
		TEXT("schemaVersion"),
		RunConfig.Suite == EABTSToonVisualCaptureSuite::ToonT4A3
			? 15
			: RunConfig.Suite == EABTSToonVisualCaptureSuite::ToonT4A2 ? 14 : 4);
	Root->SetStringField(
		TEXT("suite"),
		FABTSToonVisualCaptureMath::LexToString(RunConfig.Suite));
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
	Environment->SetNumberField(
		TEXT("expectedScreenPercentage"),
		RunConfig.ExpectedScreenPercentage);
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
		(RunConfig.Suite == EABTSToonVisualCaptureSuite::ToonT4A1
			|| RunConfig.Suite == EABTSToonVisualCaptureSuite::ToonT4A2
			|| RunConfig.Suite == EABTSToonVisualCaptureSuite::ToonT4A3)
			? TEXT("T4A1_StyleOnManualExposure_StyleOffSceneConfigured_PerVariantWarmup")
			: TEXT("SceneConfiguredAutoExposure_CameraCut_PerVariantWarmup"));
	Environment->SetNumberField(
		TEXT("snapshotContractVersion"),
		EnvironmentSnapshot.Version);
	Environment->SetObjectField(
		TEXT("planetCenterWorld"),
		ABTSToonVisualCaptureSubsystemPrivate::VectorToJson(
			EnvironmentSnapshot.PlanetCenterWorld));
	Environment->SetNumberField(
		TEXT("planetRadiusCM"),
		EnvironmentSnapshot.PlanetRadiusCM);
	Environment->SetObjectField(
		TEXT("sunDirectionToSunWorld"),
		ABTSToonVisualCaptureSubsystemPrivate::VectorToJson(
			EnvironmentSnapshot.SunDirectionToSunWorld));
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
	WorldIdentity->SetStringField(
		TEXT("variantCatalogueHash"),
		ABTSToonVisualCaptureSubsystemPrivate::Hex64(
			VariantCatalogueHash));
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
		TEXT("captureContract"),
		RunConfig.Suite == EABTSToonVisualCaptureSuite::ToonT4A0
			? TEXT("T4-A0 freezes six Tone/Outline/Shadow isolation variants without changing gameplay authority.")
			: RunConfig.Suite == EABTSToonVisualCaptureSuite::ToonT4A3
				? TEXT("T4-A3.1 captures five GroundDay-profile altitudes proving a continuous atmosphere-to-space transition without a gameplay profile cut.")
			: RunConfig.Suite == EABTSToonVisualCaptureSuite::ToonT4A2
				? TEXT("T4-A2 captures ten A1 poses, seven A2.1 cloud views, five A2.2 field views and four A2.3.1 stable-planar-noise traversal relations with reversible StyleOff/StyleOn.")
				: RunConfig.Suite == EABTSToonVisualCaptureSuite::ToonT4A1
				? TEXT("T4-A1 freezes ten spherical-environment points with reversible StyleOff/StyleOn presentation and GPU evidence.")
				: TEXT("Style implementation is versioned; Off bypasses project stylization."));
	Style->SetNumberField(
		TEXT("gpuProfileSamplesPerVariant"),
		RunConfig.GPUProfileSamplesPerVariant);
	if (RunConfig.Suite == EABTSToonVisualCaptureSuite::ToonT4A3)
	{
		const FABTSStylizedEnvironmentParameters Presentation =
			FABTSStylizedRenderingControl::BuildEnvironmentParameters(
				EnvironmentSnapshot.PlanetCenterWorld,
				EnvironmentSnapshot.PlanetRadiusCM,
				EnvironmentSnapshot.SunDirectionToSunWorld,
				EABTSStylizedRenderProfile::GroundDay);
		Style->SetNumberField(TEXT("highAltitudeTransitionStartCM"),
			Presentation.HighAltitudeTransitionStartCM);
		Style->SetNumberField(TEXT("highAltitudeTransitionEndCM"),
			Presentation.HighAltitudeTransitionEndCM);
		Style->SetNumberField(TEXT("highAltitudeTransitionStartPrimaryRadiusRatio"),
			Presentation.HighAltitudeTransitionStartCM / Presentation.PlanetRadiusCM);
		Style->SetNumberField(TEXT("highAltitudeTransitionEndPrimaryRadiusRatio"),
			Presentation.HighAltitudeTransitionEndCM / Presentation.PlanetRadiusCM);
		Style->SetBoolField(TEXT("highAltitudePerViewCameraDriven"), true);
		Style->SetBoolField(TEXT("highAltitudeGameplayProfileCut"), false);
	}
	Style->SetBoolField(
		TEXT("cloudPerformanceBaselineDisabled"),
		RunConfig.bDisableLowPolyCloudsForPerformanceBaseline);
	Style->SetBoolField(
		TEXT("runtimeCloudPrototypeActive"),
		bRuntimeCloudPrototypeActive);
	Style->SetNumberField(
		TEXT("runtimeLogicalCloudCount"),
		RuntimeLogicalCloudCount);
	Style->SetStringField(
		TEXT("runtimeLogicalCloudLayoutHash"),
		ABTSToonVisualCaptureSubsystemPrivate::Hex64(
			RuntimeLogicalCloudLayoutHash));
	Style->SetNumberField(
		TEXT("runtimeCloudMaterialBatchCount"),
		RuntimeCloudMaterialBatchCount);
	TArray<TSharedPtr<FJsonValue>> RequestedPointsJson;
	for (const FName PointId : RunConfig.RequestedPointIds)
	{
		RequestedPointsJson.Add(MakeShared<FJsonValueString>(PointId.ToString()));
	}
	Style->SetArrayField(TEXT("requestedPointIds"), RequestedPointsJson);
	TArray<TSharedPtr<FJsonValue>> RequestedVariantsJson;
	for (const FName VariantId : RunConfig.RequestedVariantIds)
	{
		RequestedVariantsJson.Add(
			MakeShared<FJsonValueString>(VariantId.ToString()));
	}
	Style->SetArrayField(TEXT("requestedVariantIds"), RequestedVariantsJson);
	if (RunConfig.Suite == EABTSToonVisualCaptureSuite::ToonT4A2)
	{
		Style->SetNumberField(
			TEXT("logicalCloudCount"), ManifestLogicalClouds.Num());
		Style->SetStringField(
			TEXT("logicalCloudLayoutHash"),
			ABTSToonVisualCaptureSubsystemPrivate::Hex64(
				ManifestLogicalCloudLayoutHash));
		Style->SetNumberField(
			TEXT("cloudCompositeStencilValue"),
			FABTSStylizedRenderingContract::
				ResolveCloudCompositeStencilValueForRenderer());
		Style->SetBoolField(TEXT("cloudToCloudOutlineSuppression"), true);
		Style->SetBoolField(TEXT("cloudToWorldOutlinePreserved"), true);
		Style->SetBoolField(TEXT("cloudSingleHISMBatchedIslandFields"), true);
		Style->SetNumberField(
			TEXT("cloudPerInstanceCustomDataFloatCount"),
			FABTST4LowPolyCloudPrototype::CloudletCustomDataFloatCount);
		Style->SetBoolField(TEXT("cloudFieldGlobal"), true);
		Style->SetNumberField(
			TEXT("globalBackgroundLogicalCloudCount"),
			FABTST4LowPolyCloudPrototype::GlobalIslandCount);
		Style->SetNumberField(
			TEXT("terminatorMegaClusterLogicalCloudCount"),
			ManifestTerminatorMegaCloudCount);
		Style->SetNumberField(
			TEXT("terminatorMegaClusterAngularSpanDegrees"),
			ManifestTerminatorMegaSpanDegrees);
		Style->SetBoolField(
			TEXT("terminatorMegaClusterConnected"),
			bManifestTerminatorMegaConnected);
		Style->SetBoolField(
			TEXT("cloudGlobalBackgroundSunIndependentPlacement"), true);
		Style->SetBoolField(
			TEXT("cloudTerminatorMegaClusterSunRelativePlacement"), true);
		Style->SetBoolField(TEXT("cloudLocalSolarHeightLighting"), true);
		Style->SetBoolField(TEXT("cloudNightWhiteningGated"), true);
		Style->SetBoolField(TEXT("cloudBoundedTraversalVisibility"), true);
		Style->SetBoolField(TEXT("cloudTraversalCameraSphere"), true);
		Style->SetBoolField(TEXT("cloudTraversalPerBirdVisualSpheres"), true);
		Style->SetNumberField(TEXT("cloudTraversalBirdSphereCapacity"), 4.0);
		Style->SetBoolField(TEXT("cloudTraversalImmediateHardProtection"), true);
		Style->SetBoolField(TEXT("cloudTraversalTsrPixelAnimation"), true);
		Style->SetBoolField(TEXT("cloudTraversalCameraBirdCorridor"), true);
		Style->SetBoolField(TEXT("cloudTraversalStablePlanarNoiseCoverage"), true);
		Style->SetBoolField(TEXT("cloudTraversalHardBirdCameraCore"), true);
		Style->SetBoolField(TEXT("cloudTraversalFullTranslucency"), false);
		Style->SetNumberField(TEXT("cloudTraversalRetainedCoverage"), 0.82);
		Style->SetNumberField(TEXT("cloudTraversalMaskFrequency"), 0.012);
		Style->SetBoolField(TEXT("cloudTraversalVeilPermanentlyRemoved"), true);
		Style->SetBoolField(TEXT("cloudTraversalContinuousEnvelopeWeight"), true);
		Style->SetBoolField(TEXT("cloudTraversalExplicitCameraCutOnly"), true);
		Style->SetBoolField(TEXT("cloudTraversalAffectsLighting"), false);
		Style->SetNumberField(
			TEXT("cloudNightBrightnessMultiplier"),
			FABTST4LowPolyCloudPrototype::NightBrightness);
		Style->SetNumberField(
			TEXT("cloudDaylightBlendMinSolarHeight"),
			FABTST4LowPolyCloudPrototype::DaylightBlendMinSolarHeight);
		Style->SetNumberField(
			TEXT("cloudDaylightBlendMaxSolarHeight"),
			FABTST4LowPolyCloudPrototype::DaylightBlendMaxSolarHeight);
		Style->SetNumberField(
			TEXT("cloudFusionPairCount"), ManifestCloudFusionPairCount);
		Style->SetStringField(
			TEXT("cloudOutlineIdentityPolicy"),
			TEXT("LogicalCloudIdentitySeparatedFromSharedCloudCompositeStencil"));
		TArray<TSharedPtr<FJsonValue>> LogicalCloudsJson;
		LogicalCloudsJson.Reserve(ManifestLogicalClouds.Num());
		for (const FABTST4LowPolyCloudIslandDefinition& LogicalCloud
			: ManifestLogicalClouds)
		{
			TSharedRef<FJsonObject> CloudJson = MakeShared<FJsonObject>();
			CloudJson->SetNumberField(
				TEXT("logicalCloudIndex"), LogicalCloud.LogicalCloudIndex);
			CloudJson->SetNumberField(
				TEXT("sourceIslandIndex"), LogicalCloud.IslandIndex);
			CloudJson->SetNumberField(
				TEXT("seed"), LogicalCloud.Seed);
			CloudJson->SetNumberField(
				TEXT("cloudletCount"), LogicalCloud.CloudletCount);
			CloudJson->SetStringField(
				TEXT("role"),
				LogicalCloud.bTerminatorMegaCluster
					? TEXT("TerminatorMegaCluster")
					: TEXT("GlobalBackground"));
			CloudJson->SetBoolField(
				TEXT("isTerminatorMegaCluster"),
				LogicalCloud.bTerminatorMegaCluster);
			CloudJson->SetNumberField(
				TEXT("solarHeight"),
				FVector::DotProduct(
					LogicalCloud.RadialUp,
					EnvironmentSnapshot.SunDirectionToSunWorld));
			CloudJson->SetStringField(
				TEXT("identityHash"),
				ABTSToonVisualCaptureSubsystemPrivate::Hex64(
					LogicalCloud.LogicalCloudIdentityHash));
			LogicalCloudsJson.Add(MakeShared<FJsonValueObject>(CloudJson));
		}
		Style->SetArrayField(TEXT("logicalClouds"), LogicalCloudsJson);
	}
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
		RecordJson->SetStringField(
			TEXT("variantId"),
			Record.VariantId.ToString());
		RecordJson->SetBoolField(
			TEXT("styleEnabled"),
			Record.bStyleEnabled);
		RecordJson->SetNumberField(
			TEXT("diagnosticPassMask"),
			static_cast<int32>(Record.PassMask));
		RecordJson->SetBoolField(
			TEXT("toneEnabled"),
			(static_cast<uint8>(Record.PassMask)
				& static_cast<uint8>(EABTSStylizedDiagnosticPassMask::Tone)) != 0);
		RecordJson->SetBoolField(
			TEXT("outlineEnabled"),
			(static_cast<uint8>(Record.PassMask)
				& static_cast<uint8>(EABTSStylizedDiagnosticPassMask::Outline)) != 0);
		RecordJson->SetBoolField(
			TEXT("shadowsEnabled"),
			Record.bShadowsEnabled);
		RecordJson->SetNumberField(
			TEXT("styleImplementationVersion"),
			Record.StyleImplementationVersion);
		if (RunConfig.Suite == EABTSToonVisualCaptureSuite::ToonT4A1
			|| RunConfig.Suite == EABTSToonVisualCaptureSuite::ToonT4A2
			|| RunConfig.Suite == EABTSToonVisualCaptureSuite::ToonT4A3)
		{
			const FABTSStylizedEnvironmentParameters Presentation =
				FABTSStylizedRenderingControl::BuildEnvironmentParameters(
					EnvironmentSnapshot.PlanetCenterWorld,
					EnvironmentSnapshot.PlanetRadiusCM,
					EnvironmentSnapshot.SunDirectionToSunWorld,
					Record.Profile);
			RecordJson->SetBoolField(
				TEXT("environmentPresentationEnabled"),
				Record.bStyleEnabled);
			RecordJson->SetNumberField(
				TEXT("atmosphereHeightCM"),
				Presentation.AtmosphereHeightCM);
			RecordJson->SetNumberField(TEXT("starSeed"), Presentation.StarSeed);
			RecordJson->SetNumberField(
				TEXT("starGridResolution"),
				Presentation.StarGridResolution);
			RecordJson->SetNumberField(
				TEXT("starCellProbability"),
				Presentation.StarCellProbability);
			RecordJson->SetNumberField(
				TEXT("starAngularRadiusScale"),
				Presentation.StarAngularRadiusScale);
			RecordJson->SetNumberField(
				TEXT("starHDRIntensity"),
				Presentation.StarHDRIntensity);
			RecordJson->SetNumberField(
				TEXT("fixedExposureBias"),
				Presentation.FixedExposureBias);
			if (RunConfig.Suite == EABTSToonVisualCaptureSuite::ToonT4A3)
			{
				const double CameraAltitudeCM = FMath::Max(
					(Record.CameraWorldTransform.GetLocation()
						- Presentation.PlanetCenterWorld).Length()
						- Presentation.PlanetRadiusCM,
					0.0);
				RecordJson->SetNumberField(TEXT("cameraAltitudeCM"), CameraAltitudeCM);
				RecordJson->SetNumberField(TEXT("cameraAltitudePrimaryRadiusRatio"),
					CameraAltitudeCM / Presentation.PlanetRadiusCM);
				RecordJson->SetNumberField(TEXT("highAltitudeSpaceBlend"),
					FABTSStylizedRenderingControl::ComputeHighAltitudeSpaceBlend(
						static_cast<float>(CameraAltitudeCM),
						Presentation.HighAltitudeTransitionStartCM,
						Presentation.HighAltitudeTransitionEndCM));
			}
			if (RunConfig.Suite == EABTSToonVisualCaptureSuite::ToonT4A2)
			{
				const bool bRuntimeCloudEnabledForRecord =
					Record.bStyleEnabled
					&& Presentation.bCloudsEnabled != 0u
					&& !RunConfig.bDisableLowPolyCloudsForPerformanceBaseline
					&& bRuntimeCloudPrototypeActive;
				RecordJson->SetBoolField(
					TEXT("cloudEnabled"),
					bRuntimeCloudEnabledForRecord);
			}
			if (RunConfig.Suite == EABTSToonVisualCaptureSuite::ToonT4A2
				&& Record.bStyleEnabled
				&& Presentation.bCloudsEnabled != 0u
				&& !RunConfig.bDisableLowPolyCloudsForPerformanceBaseline
				&& bRuntimeCloudPrototypeActive)
			{
				RecordJson->SetStringField(
					TEXT("cloudRoute"),
					TEXT("SingleHISMBatchedIslandFieldsA2_4"));
				const bool bTraversalPoint = Record.Anchor
					== EABTSToonVisualCaptureAnchor::CloudTraversalBirdInside
					|| Record.Anchor
						== EABTSToonVisualCaptureAnchor::CloudTraversalCameraInside
					|| Record.Anchor
						== EABTSToonVisualCaptureAnchor::CloudTraversalBetween
					|| Record.Anchor
						== EABTSToonVisualCaptureAnchor::CloudTraversalBothInside;
				RecordJson->SetBoolField(
					TEXT("cloudBoundedTraversalVisibility"), true);
				RecordJson->SetBoolField(
					TEXT("cloudTraversalStablePlanarNoiseCoverage"), true);
				RecordJson->SetBoolField(
					TEXT("cloudTraversalHardBirdCameraCore"), true);
				RecordJson->SetBoolField(
					TEXT("cloudTraversalPerBirdVisualSpheres"), true);
				RecordJson->SetBoolField(
					TEXT("cloudTraversalImmediateHardProtection"), true);
				RecordJson->SetBoolField(
					TEXT("cloudTraversalTsrPixelAnimation"), true);
				RecordJson->SetBoolField(
					TEXT("cloudTraversalFullTranslucency"), false);
				RecordJson->SetNumberField(
					TEXT("cloudTraversalRetainedCoverage"), 0.82);
				RecordJson->SetNumberField(
					TEXT("cloudTraversalMaskFrequency"), 0.012);
				RecordJson->SetBoolField(
					TEXT("cloudTraversalVeilPermanentlyRemoved"), true);
				RecordJson->SetBoolField(
					TEXT("cloudTraversalContinuousEnvelopeWeight"), true);
				RecordJson->SetBoolField(
					TEXT("cloudTraversalExplicitCameraCutOnly"), true);
				RecordJson->SetBoolField(
					TEXT("cloudTraversalDiagnosticPoint"), bTraversalPoint);
				RecordJson->SetBoolField(
					TEXT("cloudTraversalCameraInside"),
					Record.Anchor == EABTSToonVisualCaptureAnchor::CloudTraversalCameraInside
						|| Record.Anchor == EABTSToonVisualCaptureAnchor::CloudTraversalBothInside);
				RecordJson->SetBoolField(
					TEXT("cloudTraversalBirdInside"),
					Record.Anchor == EABTSToonVisualCaptureAnchor::CloudTraversalBirdInside
						|| Record.Anchor == EABTSToonVisualCaptureAnchor::CloudTraversalBothInside);
				RecordJson->SetBoolField(
					TEXT("cloudTraversalCloudBetween"),
					Record.Anchor == EABTSToonVisualCaptureAnchor::CloudTraversalBetween);
				RecordJson->SetNumberField(
					TEXT("cloudMacroClusters"),
					FABTST4LowPolyCloudPrototype::IslandCount *
						FABTST4LowPolyCloudPrototype::MacroClusterCountPerIsland);
				RecordJson->SetBoolField(TEXT("cloudViewInvariantIslandField"), true);
				RecordJson->SetBoolField(TEXT("cloudViewInvariantVolumeGradient"), true);
				RecordJson->SetBoolField(TEXT("cloudCameraDependentLighting"), false);
				RecordJson->SetBoolField(TEXT("cloudBypassGenericObjectTone"), true);
				RecordJson->SetBoolField(TEXT("cloudSunwardWhitening"), true);
				RecordJson->SetBoolField(TEXT("cloudThinDensityWhitening"), true);
				RecordJson->SetBoolField(TEXT("cloudViewIndependentWhitening"), true);
				RecordJson->SetBoolField(TEXT("cloudGradientCoherenceGuard"), true);
				RecordJson->SetBoolField(TEXT("cloudGradientJunctionGate"), true);
				RecordJson->SetBoolField(TEXT("cloudPlanarCoreClosure"), true);
				RecordJson->SetBoolField(TEXT("cloudUndersideField"), true);
				RecordJson->SetBoolField(TEXT("cloudCriticalPointFallbackToIslandUp"), true);
				RecordJson->SetNumberField(
					TEXT("cloudBodyCloudlets"),
					FABTST4LowPolyCloudPrototype::TotalBodyCloudletCount);
				RecordJson->SetNumberField(
					TEXT("cloudCrownCloudlets"),
					FABTST4LowPolyCloudPrototype::TotalCrownCloudletCount);
				RecordJson->SetNumberField(
					TEXT("cloudEdgeCloudlets"),
					FABTST4LowPolyCloudPrototype::TotalEdgeCloudletCount);
				RecordJson->SetBoolField(
					TEXT("cloudSharedImplicitVolume"), false);
				RecordJson->SetBoolField(
					TEXT("cloudAnalyticRaySurface"), false);
				RecordJson->SetBoolField(
					TEXT("cloudContinuousVolumeNormal"), false);
				RecordJson->SetBoolField(
					TEXT("cloudOpticalDepth"), false);
				RecordJson->SetBoolField(
					TEXT("cloudContinuousMacroNormal"), true);
				RecordJson->SetBoolField(
					TEXT("cloudThreeBandColor"), true);
				RecordJson->SetNumberField(
					TEXT("cloudMacroNormalStrength"), 0.84);
				RecordJson->SetNumberField(
					TEXT("cloudPixelLocalNormalWeight"), 0.0);
				RecordJson->SetNumberField(
					TEXT("cloudPixelInstanceVariation"), 0.0);
				RecordJson->SetBoolField(
					TEXT("cloudContinuousMacroRelief"), true);
				RecordJson->SetNumberField(TEXT("cloudMacroMaskCoverageMinimum"), 0.98);
				RecordJson->SetBoolField(TEXT("cloudSphericalConformal"), true);
				RecordJson->SetNumberField(
					TEXT("cloudHorizontalEnvelopeAspectMaximum"), 1.08);
				RecordJson->SetNumberField(
					TEXT("cloudAzimuthalFootprintIsotropyMinimum"), 0.80);
				RecordJson->SetNumberField(TEXT("cloudDetachedEdgeMaximum"), 0);
				RecordJson->SetNumberField(
					TEXT("logicalCloudCount"), ManifestLogicalClouds.Num());
				RecordJson->SetStringField(
					TEXT("logicalCloudLayoutHash"),
					ABTSToonVisualCaptureSubsystemPrivate::Hex64(
						ManifestLogicalCloudLayoutHash));
				RecordJson->SetNumberField(
					TEXT("cloudCompositeStencilValue"),
					FABTSStylizedRenderingContract::
						ResolveCloudCompositeStencilValueForRenderer());
				RecordJson->SetBoolField(
					TEXT("cloudToCloudOutlineSuppression"), true);
				RecordJson->SetBoolField(
					TEXT("cloudToWorldOutlinePreserved"), true);
				RecordJson->SetBoolField(TEXT("cloudFieldGlobal"), true);
				RecordJson->SetNumberField(
					TEXT("globalBackgroundLogicalCloudCount"),
					FABTST4LowPolyCloudPrototype::GlobalIslandCount);
				RecordJson->SetNumberField(
					TEXT("terminatorMegaClusterLogicalCloudCount"),
					ManifestTerminatorMegaCloudCount);
				RecordJson->SetNumberField(
					TEXT("terminatorMegaClusterAngularSpanDegrees"),
					ManifestTerminatorMegaSpanDegrees);
				RecordJson->SetBoolField(
					TEXT("terminatorMegaClusterConnected"),
					bManifestTerminatorMegaConnected);
				RecordJson->SetBoolField(
					TEXT("cloudGlobalBackgroundSunIndependentPlacement"), true);
				RecordJson->SetBoolField(
					TEXT("cloudTerminatorMegaClusterSunRelativePlacement"), true);
				RecordJson->SetBoolField(TEXT("cloudLocalSolarHeightLighting"), true);
				RecordJson->SetBoolField(TEXT("cloudNightWhiteningGated"), true);
				RecordJson->SetNumberField(
					TEXT("cloudNightBrightnessMultiplier"),
					FABTST4LowPolyCloudPrototype::NightBrightness);
				RecordJson->SetNumberField(
					TEXT("cloudDaylightBlendMinSolarHeight"),
					FABTST4LowPolyCloudPrototype::DaylightBlendMinSolarHeight);
				RecordJson->SetNumberField(
					TEXT("cloudDaylightBlendMaxSolarHeight"),
					FABTST4LowPolyCloudPrototype::DaylightBlendMaxSolarHeight);
				RecordJson->SetNumberField(
					TEXT("cloudFusionPairCount"), ManifestCloudFusionPairCount);
				RecordJson->SetBoolField(
					TEXT("cloudInternalOutlineSuppression"), true);
				RecordJson->SetNumberField(TEXT("cloudBaseAltitudeCM"), Presentation.CloudBaseAltitudeCM);
				RecordJson->SetNumberField(TEXT("cloudLayerHeightCM"), Presentation.CloudLayerHeightCM);
				RecordJson->SetNumberField(TEXT("cloudGlobalScaleKM"), Presentation.CloudGlobalScaleKM);
				RecordJson->SetNumberField(TEXT("cloudCoverage"), Presentation.CloudCoverage);
				RecordJson->SetNumberField(TEXT("cloudDensity"), Presentation.CloudDensity);
				RecordJson->SetNumberField(TEXT("cloudViewSampleCountScale"), Presentation.CloudViewSampleCountScale);
			}
		}
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
		RecordJson->SetStringField(
			TEXT("environmentSnapshotHash"),
			ABTSToonVisualCaptureSubsystemPrivate::Hex64(
				Record.EnvironmentSnapshotHash));
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
	if (!VariantDefinitions.IsValidIndex(CurrentVariantIndex))
	{
		return FString();
	}
	const FString Filename = RunConfig.Suite
		== EABTSToonVisualCaptureSuite::ToonT0
		? FString::Printf(
			TEXT("%02d_%s_%s.png"),
			CurrentPointIndex + 1,
			*ResolvedPoints[CurrentPointIndex].Definition.PointId.ToString(),
			*VariantDefinitions[CurrentVariantIndex].VariantId.ToString())
		: FString::Printf(
			TEXT("%02d_%s_%02d_%s.png"),
			CurrentPointIndex + 1,
			*ResolvedPoints[CurrentPointIndex].Definition.PointId.ToString(),
			CurrentVariantIndex + 1,
			*VariantDefinitions[CurrentVariantIndex].VariantId.ToString());
	return FPaths::Combine(OutputDirectory, Filename);
}
