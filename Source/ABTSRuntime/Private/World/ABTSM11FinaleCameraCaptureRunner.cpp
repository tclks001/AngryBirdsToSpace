// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM11FinaleCameraCaptureRunner.h"

#include "ABTSRuntime.h"
#include "Camera/ABTSM11FinaleFlightCamera.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Inventory/ABTSInventoryTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Rendering/ABTSStylizedRenderingControl.h"
#include "Rendering/ABTSStylizedRenderingTypes.h"
#include "Rendering/ABTSStylizedSceneCaptureRegistry.h"
#include "ImageUtils.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Slingshot/ABTSSlingshotVisualTypes.h"
#include "World/ABTSM11FinaleInteractionSystem.h"
#include "World/ABTSM11FinaleSystem.h"
#include "World/ABTSM51WorldActors.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace ABTSM11FinaleCameraCaptureRunnerPrivate
{
	bool WriteBytes(IFileHandle& File, const void* Data, const int64 Size)
	{
		return Size >= 0 && File.Write(static_cast<const uint8*>(Data), Size);
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

	bool RejectFinaleCameraCaptureConfig(
		FString* OutFailure,
		const FString& Reason)
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = Reason;
		}
		return false;
	}

	bool ParseBoolOption(
		const TCHAR* CommandLine,
		const TCHAR* Key,
		bool DefaultValue,
		bool& OutValue,
		FString* OutFailure)
	{
		int32 Value = DefaultValue ? 1 : 0;
		if (FParse::Value(CommandLine, Key, Value)
			&& Value != 0
			&& Value != 1)
		{
			return RejectFinaleCameraCaptureConfig(
				OutFailure,
				FString::Printf(TEXT("%s must be 0 or 1."), Key));
		}
		OutValue = Value != 0;
		return true;
	}

	FString Hex64(const uint64 Value)
	{
		return FString::Printf(TEXT("0x%016llX"), Value);
	}

	const TCHAR* StateLabel(const EABTSM11FinaleInteractionState State)
	{
		switch (State)
		{
		case EABTSM11FinaleInteractionState::Locked:
			return TEXT("Locked");
		case EABTSM11FinaleInteractionState::Ready:
			return TEXT("Ready");
		case EABTSM11FinaleInteractionState::Aiming:
			return TEXT("Aiming");
		case EABTSM11FinaleInteractionState::ReleasePending:
			return TEXT("ReleasePending");
		case EABTSM11FinaleInteractionState::Launched:
			return TEXT("Launched");
		case EABTSM11FinaleInteractionState::TargetHit:
			return TEXT("TargetHit");
		case EABTSM11FinaleInteractionState::Failed:
			return TEXT("Failed");
		case EABTSM11FinaleInteractionState::Recovering:
			return TEXT("Recovering");
		default:
			return TEXT("Unknown");
		}
	}
}

bool FABTSM11FinaleCameraCaptureConfig::Parse(
	const TCHAR* CommandLine,
	FABTSM11FinaleCameraCaptureConfig& OutConfig,
	FString* OutFailure)
{
	using namespace ABTSM11FinaleCameraCaptureRunnerPrivate;
	OutConfig = FABTSM11FinaleCameraCaptureConfig();
	if (CommandLine == nullptr)
	{
		return RejectFinaleCameraCaptureConfig(
			OutFailure,
			TEXT("Command line is null."));
	}
	OutConfig.bEnabled = FParse::Param(
		CommandLine,
		TEXT("ABTSM11CameraCapture"));
	if (!OutConfig.bEnabled)
	{
		return true;
	}

	FParse::Value(
		CommandLine,
		TEXT("ABTSM11CaptureRank="),
		OutConfig.CandidateRank);
	if (!ParseBoolOption(
		CommandLine,
		TEXT("ABTSM11CaptureStylized="),
		false,
		OutConfig.bStylized,
		OutFailure)
		|| !ParseBoolOption(
			CommandLine,
			TEXT("ABTSM11CaptureAutoExit="),
			true,
			OutConfig.bAutoExit,
			OutFailure))
	{
		return false;
	}
	FParse::Value(
		CommandLine,
		TEXT("ABTSM11CaptureWarmupFrames="),
		OutConfig.WarmupFrames);
	FParse::Value(
		CommandLine,
		TEXT("ABTSM11CaptureTerminalHoldFrames="),
		OutConfig.TerminalHoldFrames);
	FParse::Value(
		CommandLine,
		TEXT("MovieFrameRate="),
		OutConfig.FrameRate);
	FParse::Value(
		CommandLine,
		TEXT("MovieQuality="),
		OutConfig.JpegQuality);
	FParse::Value(CommandLine, TEXT("ResX="), OutConfig.CaptureWidth);
	FParse::Value(CommandLine, TEXT("ResY="), OutConfig.CaptureHeight);
	FParse::Value(
		CommandLine,
		TEXT("ABTSM11CaptureTimeoutSeconds="),
		OutConfig.TimeoutSeconds);
	FParse::Value(
		CommandLine,
		TEXT("MovieFolder="),
		OutConfig.OutputDirectory);
	FParse::Value(
		CommandLine,
		TEXT("MovieName="),
		OutConfig.MovieName);
	FParse::Value(
		CommandLine,
		TEXT("MovieFormat="),
		OutConfig.MovieFormat);
	if (!OutConfig.OutputDirectory.IsEmpty())
	{
		OutConfig.OutputDirectory = FPaths::ConvertRelativePathToFull(
			OutConfig.OutputDirectory);
		FPaths::NormalizeDirectoryName(OutConfig.OutputDirectory);
	}
	return OutConfig.IsValid(OutFailure);
}

bool FABTSM11FinaleCameraCaptureConfig::IsValid(
	FString* OutFailure) const
{
	using namespace ABTSM11FinaleCameraCaptureRunnerPrivate;
	if (!bEnabled)
	{
		return true;
	}
	if (CandidateRank < 0 || CandidateRank > 11)
	{
		return RejectFinaleCameraCaptureConfig(
			OutFailure,
			TEXT("ABTSM11CaptureRank must be in [0, 11]."));
	}
	if (WarmupFrames < 0 || WarmupFrames > 600
		|| TerminalHoldFrames < 0 || TerminalHoldFrames > 600)
	{
		return RejectFinaleCameraCaptureConfig(
			OutFailure,
			TEXT("Capture frame counts must be in [0, 600]."));
	}
	if (FrameRate < 1 || FrameRate > 120
		|| JpegQuality < 1 || JpegQuality > 100)
	{
		return RejectFinaleCameraCaptureConfig(
			OutFailure,
			TEXT("MovieFrameRate must be in [1, 120] and MovieQuality in [1, 100]."));
	}
	if (CaptureWidth < 320 || CaptureWidth > 7680
		|| CaptureHeight < 180 || CaptureHeight > 4320)
	{
		return RejectFinaleCameraCaptureConfig(
			OutFailure,
			TEXT("ResX/ResY must describe a capture in [320x180, 7680x4320]."));
	}
	if (!FMath::IsFinite(TimeoutSeconds)
		|| TimeoutSeconds < 10.0
		|| TimeoutSeconds > 1800.0)
	{
		return RejectFinaleCameraCaptureConfig(
			OutFailure,
			TEXT("ABTSM11CaptureTimeoutSeconds must be in [10, 1800]."));
	}
	if (OutputDirectory.IsEmpty() || FPaths::IsRelative(OutputDirectory))
	{
		return RejectFinaleCameraCaptureConfig(
			OutFailure,
			TEXT("-MovieFolder must be an absolute path."));
	}
	if (MovieName.IsEmpty()
		|| MovieName.Contains(TEXT("{"))
		|| MovieName.Contains(TEXT("}"))
		|| MovieName.Contains(TEXT("/"))
		|| MovieName.Contains(TEXT("\\")))
	{
		return RejectFinaleCameraCaptureConfig(
			OutFailure,
			TEXT("-MovieName must be a plain filename without format tokens."));
	}
	if (!MovieFormat.Equals(TEXT("JPG"), ESearchCase::IgnoreCase))
	{
		return RejectFinaleCameraCaptureConfig(
			OutFailure,
			TEXT("-MovieFormat=JPG is required; native startup AVI is not accepted because its finalized frame count is not reliable in unattended capture."));
	}
	return true;
}

FString FABTSM11FinaleCameraCaptureConfig::GetExpectedVideoPath() const
{
	return FPaths::Combine(OutputDirectory, MovieName + TEXT(".avi"));
}

FString FABTSM11FinaleCameraCaptureConfig::GetFrameWildcard() const
{
	return FPaths::Combine(OutputDirectory, MovieName + TEXT(".*.jpg"));
}

int32 FABTSM11FinaleCameraCaptureConfig::GetObservedFrameCount() const
{
	TArray<FString> FrameFiles;
	IFileManager::Get().FindFiles(FrameFiles, *GetFrameWildcard(), true, false);
	return FrameFiles.Num();
}

FString FABTSM11FinaleCameraCaptureConfig::GetManifestPath() const
{
	return FPaths::Combine(
		OutputDirectory,
		MovieName + TEXT(".manifest.json"));
}

AABTSM11FinaleCameraCaptureRunner::AABTSM11FinaleCameraCaptureRunner()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	RecordingCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(
		TEXT("RecordingCapture"));
	SetRootComponent(RecordingCapture);
	RecordingCapture->bCaptureEveryFrame = false;
	RecordingCapture->bCaptureOnMovement = false;
	RecordingCapture->bAlwaysPersistRenderingState = true;
	RecordingCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
}

bool AABTSM11FinaleCameraCaptureRunner::Initialize(
	const FABTSM11FinaleCameraCaptureConfig& InConfig,
	AABTSM11FinaleSystem& InFinaleSystem,
	AABTSM11FinaleInteractionSystem& InInteractionSystem)
{
	FString ConfigFailure;
	if (Phase != EABTSM11FinaleCameraCapturePhase::Inactive
		|| !InConfig.IsValid(&ConfigFailure)
		|| !InConfig.bEnabled
		|| GetWorld() == nullptr
		|| GetWorld()->WorldType != EWorldType::Game
		|| !InFinaleSystem.IsLayoutReady()
		|| InFinaleSystem.IsEditorCandidateMode()
			!= (InConfig.CandidateRank != 0)
		|| (InFinaleSystem.IsEditorCandidateMode()
			&& InFinaleSystem.GetEditorCandidateIdentity().Rank
				!= InConfig.CandidateRank))
	{
		FailureReason = ConfigFailure.IsEmpty()
			? TEXT("CaptureInitializationContractRejected")
			: ConfigFailure;
		return false;
	}

	Config = InConfig;
	if (!Config.bAutoExit)
	{
		FailureReason = TEXT("StandaloneCaptureRequiresAutoExit");
		return false;
	}
	if (!IFileManager::Get().MakeDirectory(*Config.OutputDirectory, true)
		|| Config.GetObservedFrameCount() > 0
		|| IFileManager::Get().FileExists(*Config.GetManifestPath()))
	{
		FailureReason = TEXT("CaptureOutputMustBeWritableAndUnique");
		return false;
	}
	FinaleSystem = &InFinaleSystem;
	InteractionSystem = &InInteractionSystem;
	StartUtc = FDateTime::UtcNow();
	StartPlatformSeconds = FPlatformTime::Seconds();
	RemainingWarmupFrames = Config.WarmupFrames;
	FABTSStylizedRenderingControl::SetEnabled(Config.bStylized);
	FABTSStylizedRenderingControl::SetProfile(
		EABTSStylizedRenderProfile::FinaleSpace);
	FApp::SetUseFixedTimeStep(true);
	FApp::SetFixedDeltaTime(1.0 / static_cast<double>(Config.FrameRate));
	RecordingRenderTarget = NewObject<UTextureRenderTarget2D>(this);
	if (!IsValid(RecordingRenderTarget) || !IsValid(RecordingCapture))
	{
		FailureReason = TEXT("RecordingRenderTargetAllocationFailed");
		return false;
	}
	RecordingRenderTarget->ClearColor = FLinearColor::Black;
	RecordingRenderTarget->InitCustomFormat(
		Config.CaptureWidth,
		Config.CaptureHeight,
		PF_B8G8R8A8,
		false);
	RecordingRenderTarget->UpdateResourceImmediate(true);
	RecordingCapture->TextureTarget = RecordingRenderTarget;
	bStylizedViewRegistered = FABTSStylizedSceneCaptureRegistry::Register(
		*RecordingCapture,
		EABTSStylizedViewClass::FinaleCinematicCapture);
	const FABTSStylizedViewPolicy CaptureViewPolicy =
		FABTSStylizedRenderingContract::ResolveViewPolicy(
			EABTSStylizedViewClass::FinaleCinematicCapture);
	if (!bStylizedViewRegistered || !CaptureViewPolicy.IsValid())
	{
		FailureReason = TEXT("RecordingStylizedViewRegistrationFailed");
		return false;
	}
	Phase = EABTSM11FinaleCameraCapturePhase::WarmingRenderMode;

	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M11][CameraCapture] Initialized Contract=%d WorldType=%d Mode=%s Format=%s Rank=%d Authority=%s Stylized=%d RenderVersion=%d ViewClass=FinaleCinematicCapture PolicyTone=%d PolicyOutline=%d PolicySelective=%d WarmupFrames=%d Frames=%s Video=%s"),
		FABTSM11FinaleCameraCaptureConfig::ContractVersion,
		static_cast<int32>(GetWorld()->WorldType),
		TEXT("StandaloneSceneCaptureFrameCapture"),
		*Config.MovieFormat,
		Config.CandidateRank,
		Config.CandidateRank == 0 ? TEXT("Certified") : TEXT("UNCERTIFIED"),
		Config.bStylized ? 1 : 0,
		FABTSStylizedRenderingControl::GetImplementationVersion(),
		CaptureViewPolicy.bApplyTone ? 1 : 0,
		CaptureViewPolicy.bApplyOutline ? 1 : 0,
		CaptureViewPolicy.bAllowSelectiveStencil ? 1 : 0,
		RemainingWarmupFrames,
		*Config.GetFrameWildcard(),
		*Config.GetExpectedVideoPath());
	return true;
}

void AABTSM11FinaleCameraCaptureRunner::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	if (bStylizedViewRegistered && IsValid(RecordingCapture))
	{
		FABTSStylizedSceneCaptureRegistry::Unregister(*RecordingCapture);
		bStylizedViewRegistered = false;
	}
	Super::EndPlay(EndPlayReason);
}

void AABTSM11FinaleCameraCaptureRunner::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Phase == EABTSM11FinaleCameraCapturePhase::Inactive
		|| Phase == EABTSM11FinaleCameraCapturePhase::Terminal)
	{
		return;
	}
	if (bMovieCaptureStarted
		&& !bMovieCaptureStopped
		&& !CaptureCurrentFrame())
	{
		Finish(false, FailureReason.IsEmpty()
			? TEXT("SceneCaptureFrameCaptureFailed")
			: FailureReason);
		return;
	}

	const double Elapsed =
		FPlatformTime::Seconds() - StartPlatformSeconds;
	if (Phase != EABTSM11FinaleCameraCapturePhase::Finalizing
		&& Elapsed > Config.TimeoutSeconds)
	{
		if (bMovieCaptureStarted && !bMovieCaptureStopped)
		{
			bPendingFinalizeSuccess = false;
			PendingFinalizeReason = TEXT("CaptureTimeout");
			StopRecording();
		}
		else
		{
			Finish(false, TEXT("CaptureTimeout"));
		}
		return;
	}

	if (!IsValid(FinaleSystem) || !IsValid(InteractionSystem))
	{
		Finish(false, TEXT("CaptureDependenciesLost"));
		return;
	}

	switch (Phase)
	{
	case EABTSM11FinaleCameraCapturePhase::WarmingRenderMode:
		if (RemainingWarmupFrames > 0)
		{
			--RemainingWarmupFrames;
			return;
		}
		bMovieCaptureStarted = true;
		Phase = EABTSM11FinaleCameraCapturePhase::WaitingForDependencies;
		break;

	case EABTSM11FinaleCameraCapturePhase::WaitingForDependencies:
		// Preserve an observable "recording started, then launch" order and
		// prove the post-render callback is producing real files before input.
		if (CapturedFrameCount < 2)
		{
			break;
		}
		FailureReason.Reset();
		if (TryBeginNominalAttempt())
		{
			Phase = EABTSM11FinaleCameraCapturePhase::WaitingForLaunch;
		}
		else if (!FailureReason.IsEmpty())
		{
			Finish(false, FailureReason);
		}
		break;

	case EABTSM11FinaleCameraCapturePhase::WaitingForLaunch:
		if (InteractionSystem->GetInteractionState()
			== EABTSM11FinaleInteractionState::Failed)
		{
			Finish(
				false,
				TEXT("NominalAttemptFailedBeforeRecording:" )
					+ InteractionSystem->GetRuntimeFailure());
			break;
		}
		if (InteractionSystem->GetInteractionState()
				== EABTSM11FinaleInteractionState::Launched
			&& IsValid(InteractionSystem->GetFlightCamera())
			&& InteractionSystem->GetFlightCamera()
				->IsAuthorityFollowActive())
		{
			if (!TryStartRecording())
			{
				if (!FailureReason.IsEmpty())
				{
					Finish(false, FailureReason);
				}
			}
		}
		break;

	case EABTSM11FinaleCameraCapturePhase::Recording:
		if (InteractionSystem->GetInteractionState()
			== EABTSM11FinaleInteractionState::TargetHit)
		{
			RemainingTerminalHoldFrames = Config.TerminalHoldFrames;
			Phase = EABTSM11FinaleCameraCapturePhase::HoldingTerminalFrame;
		}
		else if (InteractionSystem->GetInteractionState()
			== EABTSM11FinaleInteractionState::Failed)
		{
			bPendingFinalizeSuccess = false;
			PendingFinalizeReason = TEXT("PlaybackFailed:")
				+ InteractionSystem->GetRuntimeFailure();
			StopRecording();
		}
		break;

	case EABTSM11FinaleCameraCapturePhase::HoldingTerminalFrame:
		if (RemainingTerminalHoldFrames > 0)
		{
			--RemainingTerminalHoldFrames;
			return;
		}
		bPendingFinalizeSuccess = true;
		PendingFinalizeReason = TEXT("TargetHit");
		StopRecording();
		break;

	default:
		break;
	}
}

bool AABTSM11FinaleCameraCaptureRunner::TryBeginNominalAttempt()
{
	UWorld* World = GetWorld();
	APlayerController* Controller =
		UGameplayStatics::GetPlayerController(World, 0);
	if (!IsValid(Controller)
		|| InteractionSystem->GetInteractionState()
			!= EABTSM11FinaleInteractionState::Ready)
	{
		return false;
	}

	AABTSM51SlingshotCord* MatchingCord = nullptr;
	if (!TryResolveOrCreateCaptureCord(MatchingCord))
	{
		return false;
	}
	if (!InteractionSystem->TryLaunchNominalCaptureAttempt(
		*MatchingCord,
		*Controller))
	{
		FailureReason = TEXT("NominalCaptureAttemptRejected");
		return false;
	}

	const FABTSM11FinaleLaunchInput& Input =
		FinaleSystem->GetLayoutPreset().NominalInput;
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M11][CameraCapture] NominalAttemptQueued Rank=%d Yaw=%.6f Pitch=%.6f Power=%.6f"),
		Config.CandidateRank,
		Input.YawDegrees,
		Input.PitchDegrees,
		Input.Power);
	return true;
}

bool AABTSM11FinaleCameraCaptureRunner::TryResolveOrCreateCaptureCord(
	AABTSM51SlingshotCord*& OutCord)
{
	OutCord = nullptr;
	UWorld* World = GetWorld();
	if (World == nullptr || !IsValid(FinaleSystem))
	{
		FailureReason = TEXT("CaptureFixtureWorldUnavailable");
		return false;
	}

	const FABTSM110FinaleLocalFrame& Frame =
		FinaleSystem->GetFinaleFrame();
	int32 MatchingCordCount = 0;
	for (TActorIterator<AABTSM51SlingshotCord> It(World); It; ++It)
	{
		if (It->IsFinaleSpaceSlingshot()
			&& It->GetFinaleSlotPairId() == Frame.SlotPairId)
		{
			OutCord = *It;
			++MatchingCordCount;
		}
	}
	if (MatchingCordCount == 1 && IsValid(OutCord))
	{
		return true;
	}
	if (MatchingCordCount > 1)
	{
		FailureReason = FString::Printf(
			TEXT("ExpectedAtMostOneMatchingFinaleCord:Found=%d"),
			MatchingCordCount);
		return false;
	}

	AABTSM51SlingshotDirtHole* LeftSlot = nullptr;
	AABTSM51SlingshotDirtHole* RightSlot = nullptr;
	int32 LeftSlotCount = 0;
	int32 RightSlotCount = 0;
	for (TActorIterator<AABTSM51SlingshotDirtHole> It(World); It; ++It)
	{
		if (!It->IsFinaleSpaceSlot()
			|| It->GetSlotPairId() != Frame.SlotPairId)
		{
			continue;
		}
		if (It->GetSlotSide() == EABTSSlingshotSlotSide::Left)
		{
			LeftSlot = *It;
			++LeftSlotCount;
		}
		else if (It->GetSlotSide() == EABTSSlingshotSlotSide::Right)
		{
			RightSlot = *It;
			++RightSlotCount;
		}
	}
	if (LeftSlotCount == 0 || RightSlotCount == 0)
	{
		// The M5.1 system publishes the slots before WorldReady. If the
		// capture actor ticks first, wait for the unique pair to appear.
		return false;
	}
	if (LeftSlotCount != 1 || RightSlotCount != 1
		|| !IsValid(LeftSlot) || !IsValid(RightSlot))
	{
		FailureReason = FString::Printf(
			TEXT("ExpectedUniqueFinaleSlotPair:Left=%d Right=%d"),
			LeftSlotCount,
			RightSlotCount);
		return false;
	}
	if (LeftSlot->IsOccupied() || RightSlot->IsOccupied())
	{
		FailureReason = TEXT("FinaleSlotOccupiedWithoutMatchingCord");
		return false;
	}

	const FABTSSlingshotVisualPreset VisualPreset =
		ABTSMakeDefaultSlingshotVisualPreset(EABTSSlingshotTier::Space);
	const FVector Up = Frame.GetUp().GetSafeNormal();
	const FVector Forward = Frame.GetForward().GetSafeNormal();
	if (Up.IsNearlyZero() || Forward.IsNearlyZero())
	{
		FailureReason = TEXT("CaptureFixtureFrameAxesInvalid");
		return false;
	}
	const FQuat StakeRotation =
		FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat();
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const auto SpawnStake =
		[World, &SpawnParameters, &VisualPreset, &Up, &StakeRotation,
			&Frame](AABTSM51SlingshotDirtHole& Slot)
			-> AABTSM51SlingshotStake*
	{
		const FTransform Transform(
			StakeRotation,
			Slot.GetActorLocation()
				+ Up * (VisualPreset.StakeHeightCM * 0.5f));
		AABTSM51SlingshotStake* Stake =
			World->SpawnActor<AABTSM51SlingshotStake>(
				AABTSM51SlingshotStake::StaticClass(),
				Transform,
				SpawnParameters);
		if (!IsValid(Stake))
		{
			return nullptr;
		}
		Stake->InitializeStake(
			EABTSItemId::SpaceStake,
			Slot.GetCellId(),
			Up);
		Stake->SetInstalledSlotIdentity(
			EABTSSlingshotSlotKind::FinaleSpace,
			Frame.SlotPairId,
			Slot.GetSlotSide());
		Slot.SetOccupiedStake(Stake);
		return Stake;
	};

	AABTSM51SlingshotStake* LeftStake = SpawnStake(*LeftSlot);
	AABTSM51SlingshotStake* RightStake = SpawnStake(*RightSlot);
	if (!IsValid(LeftStake) || !IsValid(RightStake))
	{
		LeftSlot->SetOccupiedStake(nullptr);
		RightSlot->SetOccupiedStake(nullptr);
		if (IsValid(LeftStake))
		{
			LeftStake->Destroy();
		}
		if (IsValid(RightStake))
		{
			RightStake->Destroy();
		}
		FailureReason = TEXT("CaptureFixtureStakeSpawnFailed");
		return false;
	}

	AABTSM51SlingshotCord* Cord =
		World->SpawnActor<AABTSM51SlingshotCord>(
			AABTSM51SlingshotCord::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	if (!IsValid(Cord))
	{
		LeftSlot->SetOccupiedStake(nullptr);
		RightSlot->SetOccupiedStake(nullptr);
		LeftStake->Destroy();
		RightStake->Destroy();
		FailureReason = TEXT("CaptureFixtureCordSpawnFailed");
		return false;
	}
	Cord->InitializeCordWithTier(
		LeftStake,
		RightStake,
		LeftStake->GetVisualTopWorldLocation(),
		RightStake->GetVisualTopWorldLocation(),
		EABTSSlingshotTier::Space);
	LeftStake->SetHasCord(true);
	RightStake->SetHasCord(true);
	if (!Cord->IsFinaleSpaceSlingshot()
		|| Cord->GetFinaleSlotPairId() != Frame.SlotPairId)
	{
		Cord->Destroy();
		LeftSlot->SetOccupiedStake(nullptr);
		RightSlot->SetOccupiedStake(nullptr);
		LeftStake->Destroy();
		RightStake->Destroy();
		FailureReason = TEXT("CaptureFixtureIdentityRejected");
		return false;
	}

	CaptureFixtureLeftStake = LeftStake;
	CaptureFixtureRightStake = RightStake;
	CaptureFixtureCord = Cord;
	bCaptureFixtureCreated = true;
	OutCord = Cord;
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M11][CameraCapture] CaptureFixtureCreated Pair=%d LeftCell=%d RightCell=%d Endpoints=(%s,%s)"),
		Frame.SlotPairId,
		LeftSlot->GetCellId(),
		RightSlot->GetCellId(),
		*Cord->GetEndpointA().ToCompactString(),
		*Cord->GetEndpointB().ToCompactString());
	return true;
}

bool AABTSM11FinaleCameraCaptureRunner::TryStartRecording()
{
	// Frame recording began before the nominal attempt. Reaching this point
	// proves that the old flight camera has taken the view; no GUI, PIE command,
	// or legacy MovieScene FrameGrabber is involved.
	Phase = EABTSM11FinaleCameraCapturePhase::Recording;
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M11][CameraCapture] StandaloneRecordingObserved Rank=%d Stylized=%d Frames=%d Camera=%s Plan=0x%016llX"),
		Config.CandidateRank,
		Config.bStylized ? 1 : 0,
		CapturedFrameCount,
		*GetNameSafe(InteractionSystem->GetFlightCamera()),
		InteractionSystem->GetReleasedPlaybackPlan().PlanHash);
	return true;
}

bool AABTSM11FinaleCameraCaptureRunner::CaptureCurrentFrame()
{
	APlayerCameraManager* CameraManager =
		UGameplayStatics::GetPlayerCameraManager(GetWorld(), 0);
	if (!IsValid(CameraManager)
		|| !IsValid(RecordingCapture)
		|| !IsValid(RecordingRenderTarget))
	{
		FailureReason = TEXT("RecordingCameraDependenciesUnavailable");
		return false;
	}
	const FMinimalViewInfo& View = CameraManager->GetCameraCacheView();
	RecordingCapture->SetWorldLocationAndRotation(
		View.Location,
		View.Rotation);
	RecordingCapture->FOVAngle = View.FOV;
	RecordingCapture->PostProcessSettings = View.PostProcessSettings;
	RecordingCapture->PostProcessBlendWeight = View.PostProcessBlendWeight;
	RecordingCapture->CaptureScene();

	FTextureRenderTargetResource* Resource =
		RecordingRenderTarget->GameThread_GetRenderTargetResource();
	TArray<FColor> Pixels;
	FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
	ReadFlags.SetLinearToGamma(false);
	const FIntPoint Size(Config.CaptureWidth, Config.CaptureHeight);
	if (Resource == nullptr
		|| !Resource->ReadPixels(Pixels, ReadFlags)
		|| Pixels.Num() != Size.X * Size.Y)
	{
		FailureReason = TEXT("RecordingRenderTargetReadFailed");
		return false;
	}

	const FString FramePath = FPaths::Combine(
		Config.OutputDirectory,
		FString::Printf(
			TEXT("%s.%06d.jpg"),
			*Config.MovieName,
			CapturedFrameCount));
	const FImageView Image(
		Pixels.GetData(),
		Size.X,
		Size.Y,
		EGammaSpace::sRGB);
	if (!FImageUtils::SaveImageByExtension(
		*FramePath,
		Image,
		Config.JpegQuality))
	{
		FailureReason = TEXT("JpegFrameWriteFailed");
		return false;
	}

	CapturedFrameSize = Size;
	++CapturedFrameCount;
	return true;
}

bool AABTSM11FinaleCameraCaptureRunner::MuxCapturedFramesToAvi()
{
	using namespace ABTSM11FinaleCameraCaptureRunnerPrivate;
	TArray<FString> FrameNames;
	IFileManager::Get().FindFiles(
		FrameNames,
		*Config.GetFrameWildcard(),
		true,
		false);
	FrameNames.Sort();
	if (FrameNames.Num() != CapturedFrameCount
		|| CapturedFrameCount <= 0
		|| CapturedFrameSize.X <= 0
		|| CapturedFrameSize.Y <= 0)
	{
		FailureReason = TEXT("FrameSequenceCountMismatchBeforeMux");
		return false;
	}

	uint32 MaxFrameBytes = 0;
	for (int32 Index = 0; Index < FrameNames.Num(); ++Index)
	{
		const FString ExpectedName = FString::Printf(
			TEXT("%s.%06d.jpg"),
			*Config.MovieName,
			Index);
		const int64 Size = IFileManager::Get().FileSize(
			*FPaths::Combine(Config.OutputDirectory, FrameNames[Index]));
		if (FrameNames[Index] != ExpectedName
			|| Size <= 4
			|| Size > MAX_uint32)
		{
			FailureReason = TEXT("FrameSequenceIdentityInvalidBeforeMux");
			return false;
		}
		MaxFrameBytes = FMath::Max(MaxFrameBytes, static_cast<uint32>(Size));
	}

	const FString VideoPath = Config.GetExpectedVideoPath();
	IPlatformFile& PlatformFile =
		FPlatformFileManager::Get().GetPlatformFile();
	TUniquePtr<IFileHandle> File(PlatformFile.OpenWrite(*VideoPath));
	if (!File)
	{
		FailureReason = TEXT("AviOpenWriteFailed");
		return false;
	}
	const auto FailMux = [this, &File, &PlatformFile, &VideoPath](
		const TCHAR* Reason)
	{
		FailureReason = Reason;
		File.Reset();
		PlatformFile.DeleteFile(*VideoPath);
		return false;
	};

	const int64 Riff = BeginChunk(*File, "RIFF");
	if (Riff < 0 || !WriteFourCC(*File, "AVI "))
	{
		return FailMux(TEXT("AviRiffHeaderWriteFailed"));
	}
	const int64 HeaderList = BeginChunk(*File, "LIST");
	if (HeaderList < 0 || !WriteFourCC(*File, "hdrl"))
	{
		return FailMux(TEXT("AviHeaderListWriteFailed"));
	}
	const int64 MainHeader = BeginChunk(*File, "avih");
	const uint32 TotalFrames = static_cast<uint32>(CapturedFrameCount);
	const uint32 Width = static_cast<uint32>(CapturedFrameSize.X);
	const uint32 Height = static_cast<uint32>(CapturedFrameSize.Y);
	if (MainHeader < 0
		|| !WriteUInt32(*File, static_cast<uint32>(FMath::RoundToInt(1000000.0 / Config.FrameRate)))
		|| !WriteUInt32(*File, MaxFrameBytes * static_cast<uint32>(Config.FrameRate))
		|| !WriteUInt32(*File, 0) || !WriteUInt32(*File, 0x10)
		|| !WriteUInt32(*File, TotalFrames) || !WriteUInt32(*File, 0)
		|| !WriteUInt32(*File, 1) || !WriteUInt32(*File, MaxFrameBytes)
		|| !WriteUInt32(*File, Width) || !WriteUInt32(*File, Height)
		|| !WriteUInt32(*File, 0) || !WriteUInt32(*File, 0)
		|| !WriteUInt32(*File, 0) || !WriteUInt32(*File, 0)
		|| !EndChunk(*File, MainHeader))
	{
		return FailMux(TEXT("AviMainHeaderWriteFailed"));
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
		|| !WriteUInt32(*File, 1) || !WriteUInt32(*File, Config.FrameRate)
		|| !WriteUInt32(*File, 0) || !WriteUInt32(*File, TotalFrames)
		|| !WriteUInt32(*File, MaxFrameBytes) || !WriteUInt32(*File, MAX_uint32)
		|| !WriteUInt32(*File, 0) || !WriteUInt16(*File, 0)
		|| !WriteUInt16(*File, 0) || !WriteUInt16(*File, static_cast<uint16>(Width))
		|| !WriteUInt16(*File, static_cast<uint16>(Height))
		|| !EndChunk(*File, StreamHeader))
	{
		return FailMux(TEXT("AviStreamHeaderWriteFailed"));
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
		return FailMux(TEXT("AviStreamFormatWriteFailed"));
	}

	const int64 MovieList = BeginChunk(*File, "LIST");
	if (MovieList < 0 || !WriteFourCC(*File, "movi"))
	{
		return FailMux(TEXT("AviMovieListWriteFailed"));
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
			*FPaths::Combine(Config.OutputDirectory, FrameName))
			|| FrameBytes.Num() < 4
			|| FrameBytes[0] != 0xFF || FrameBytes[1] != 0xD8
			|| FrameBytes[FrameBytes.Num() - 2] != 0xFF
			|| FrameBytes[FrameBytes.Num() - 1] != 0xD9)
		{
			return FailMux(TEXT("AviJpegFrameReadFailed"));
		}
		const int64 ChunkStart = File->Tell();
		const int64 FrameChunk = BeginChunk(*File, "00dc");
		if (FrameChunk < 0
			|| !WriteBytes(*File, FrameBytes.GetData(), FrameBytes.Num())
			|| !EndChunk(*File, FrameChunk))
		{
			return FailMux(TEXT("AviJpegFrameWriteFailed"));
		}
		ChunkOffsets.Add(static_cast<uint32>(ChunkStart - MovieTypeOffset));
		ChunkSizes.Add(static_cast<uint32>(FrameBytes.Num()));
	}
	if (!EndChunk(*File, MovieList))
	{
		return FailMux(TEXT("AviMovieListFinalizeFailed"));
	}
	const int64 IndexChunk = BeginChunk(*File, "idx1");
	for (int32 Index = 0; Index < ChunkOffsets.Num(); ++Index)
	{
		if (!WriteFourCC(*File, "00dc") || !WriteUInt32(*File, 0x10)
			|| !WriteUInt32(*File, ChunkOffsets[Index])
			|| !WriteUInt32(*File, ChunkSizes[Index]))
		{
			return FailMux(TEXT("AviIndexWriteFailed"));
		}
	}
	if (IndexChunk < 0 || !EndChunk(*File, IndexChunk) || !EndChunk(*File, Riff))
	{
		return FailMux(TEXT("AviFinalizeFailed"));
	}
	File.Reset();
	if (IFileManager::Get().FileSize(*VideoPath) <= 4096)
	{
		return FailMux(TEXT("AviFinalSizeInvalid"));
	}
	return true;
}

void AABTSM11FinaleCameraCaptureRunner::StopRecording()
{
	if (!bMovieCaptureStarted || bMovieCaptureStopped)
	{
		Finish(false, TEXT("StopMovieCaptureWithoutActiveCapture"));
		return;
	}
	EndUtc = FDateTime::UtcNow();
	bMovieCaptureStopped = true;
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M11][CameraCapture] RecordingStopped PendingSuccess=%d Reason=%s"),
		bPendingFinalizeSuccess ? 1 : 0,
		*PendingFinalizeReason);
	if (!MuxCapturedFramesToAvi())
	{
		Finish(false, FailureReason);
		return;
	}
	Finish(bPendingFinalizeSuccess, PendingFinalizeReason);
}

void AABTSM11FinaleCameraCaptureRunner::Finish(
	const bool bSuccess,
	const FString& Reason)
{
	if (Phase == EABTSM11FinaleCameraCapturePhase::Terminal)
	{
		return;
	}
	EndUtc = FDateTime::UtcNow();
	FailureReason = bSuccess ? FString() : Reason;
	Phase = EABTSM11FinaleCameraCapturePhase::Terminal;
	SetActorTickEnabled(false);
	const bool bManifestWritten = WriteManifest(bSuccess, Reason);
	if (bStylizedViewRegistered && IsValid(RecordingCapture))
	{
		FABTSStylizedSceneCaptureRegistry::Unregister(*RecordingCapture);
		bStylizedViewRegistered = false;
	}

	const FString Summary = FString::Printf(
		TEXT("[ABTS][M11][CameraCapture] Complete Success=%d Rank=%d Stylized=%d State=%s Frames=%d Manifest=%d Reason=%s Video=%s"),
		bSuccess ? 1 : 0,
		Config.CandidateRank,
		Config.bStylized ? 1 : 0,
		IsValid(InteractionSystem)
			? ABTSM11FinaleCameraCaptureRunnerPrivate::StateLabel(
				InteractionSystem->GetInteractionState())
			: TEXT("Unavailable"),
		CapturedFrameCount,
		bManifestWritten ? 1 : 0,
		*Reason,
		*Config.GetExpectedVideoPath());
	if (bSuccess && bManifestWritten)
	{
		UE_LOG(LogABTSRuntime, Log, TEXT("%s"), *Summary);
	}
	else
	{
		UE_LOG(LogABTSRuntime, Error, TEXT("%s"), *Summary);
	}

	if (Config.bAutoExit)
	{
		FPlatformMisc::RequestExitWithStatus(
			false,
			bSuccess && bManifestWritten ? 0 : 2);
	}
}

bool AABTSM11FinaleCameraCaptureRunner::WriteManifest(
	const bool bSuccess,
	const FString& Reason) const
{
	using namespace ABTSM11FinaleCameraCaptureRunnerPrivate;
	if (!IFileManager::Get().MakeDirectory(
		*Config.OutputDirectory,
		true))
	{
		return false;
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(
		TEXT("contractVersion"),
		FABTSM11FinaleCameraCaptureConfig::ContractVersion);
	Root->SetStringField(
		TEXT("status"),
		bSuccess
			? TEXT("Complete")
			: TEXT("Failed"));
	Root->SetStringField(
		TEXT("captureFinalization"),
		TEXT("SynchronousFramesAndAviMuxComplete"));
	Root->SetStringField(TEXT("reason"), Reason);
	Root->SetStringField(TEXT("startUtc"), StartUtc.ToIso8601());
	Root->SetStringField(TEXT("endUtc"), EndUtc.ToIso8601());
	Root->SetNumberField(TEXT("rank"), Config.CandidateRank);
	Root->SetStringField(
		TEXT("authority"),
		Config.CandidateRank == 0 ? TEXT("Certified") : TEXT("UNCERTIFIED"));
	Root->SetBoolField(TEXT("stylizedEnabled"), Config.bStylized);
	Root->SetBoolField(
		TEXT("captureFixtureCreated"),
		bCaptureFixtureCreated);
	Root->SetStringField(TEXT("stylizedProfile"), TEXT("FinaleSpace"));
	Root->SetStringField(
		TEXT("stylizedViewClass"),
		TEXT("FinaleCinematicCapture"));
	const FABTSStylizedViewPolicy CaptureViewPolicy =
		FABTSStylizedRenderingContract::ResolveViewPolicy(
			EABTSStylizedViewClass::FinaleCinematicCapture);
	Root->SetBoolField(
		TEXT("stylizedViewRegistered"),
		bStylizedViewRegistered);
	Root->SetBoolField(
		TEXT("stylizedViewPolicyValid"),
		CaptureViewPolicy.IsValid());
	Root->SetBoolField(
		TEXT("stylizedTonePolicyEnabled"),
		Config.bStylized && CaptureViewPolicy.bApplyTone);
	Root->SetBoolField(
		TEXT("stylizedOutlinePolicyEnabled"),
		Config.bStylized && CaptureViewPolicy.bApplyOutline);
	Root->SetBoolField(
		TEXT("stylizedSelectiveStencilPolicyEnabled"),
		Config.bStylized && CaptureViewPolicy.bAllowSelectiveStencil);
	Root->SetStringField(
		TEXT("captureProtocol"),
		TEXT("SceneCaptureJPG+MJPEGAVI"));
	Root->SetNumberField(
		TEXT("stylizedImplementationVersion"),
		FABTSStylizedRenderingControl::GetImplementationVersion());
	Root->SetStringField(TEXT("videoPath"), Config.GetExpectedVideoPath());
	Root->SetStringField(TEXT("frameWildcard"), Config.GetFrameWildcard());
	Root->SetNumberField(
		TEXT("frameCountObserved"),
		CapturedFrameCount);
	Root->SetNumberField(TEXT("frameRate"), Config.FrameRate);
	Root->SetNumberField(TEXT("frameWidth"), CapturedFrameSize.X);
	Root->SetNumberField(TEXT("frameHeight"), CapturedFrameSize.Y);
	Root->SetNumberField(TEXT("jpegQuality"), Config.JpegQuality);
	Root->SetBoolField(TEXT("videoPostprocessRequired"), false);
	Root->SetNumberField(
		TEXT("videoBytesObserved"),
		static_cast<double>(IFileManager::Get().FileSize(
			*Config.GetExpectedVideoPath())));

	if (IsValid(FinaleSystem))
	{
		const FABTSM11FinaleLayoutPreset& Preset =
			FinaleSystem->GetLayoutPreset();
		Root->SetStringField(TEXT("presetHash"), Hex64(Preset.PresetHash));
		Root->SetStringField(
			TEXT("certifiedBundleHash"),
			Hex64(Preset.CertifiedBundleHash));
		if (FinaleSystem->IsEditorCandidateMode())
		{
			const FABTSM11CandidateExperienceIdentity& Identity =
				FinaleSystem->GetEditorCandidateIdentity();
			Root->SetStringField(
				TEXT("candidateSourceHash"),
				Hex64(Identity.CandidateSourceHash));
			Root->SetStringField(
				TEXT("candidateResultHash"),
				Hex64(Identity.NominalResultHash));
		}
	}
	if (IsValid(InteractionSystem))
	{
		const FABTSM11PlaybackPlan& Plan =
			InteractionSystem->GetReleasedPlaybackPlan();
		Root->SetStringField(
			TEXT("interactionState"),
			StateLabel(InteractionSystem->GetInteractionState()));
		Root->SetStringField(
			TEXT("releasedTrajectoryHash"),
			Hex64(Plan.ReleasedTrajectoryHash));
		Root->SetStringField(TEXT("playbackPlanHash"), Hex64(Plan.PlanHash));
		Root->SetBoolField(
			TEXT("candidateQualifiedIntercept"),
			Plan.bCandidateQualifiedIntercept);
		Root->SetBoolField(TEXT("physicalTargetHit"), Plan.bPhysicalTargetHit);
	}

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer =
		TJsonWriterFactory<>::Create(&Json);
	return FJsonSerializer::Serialize(Root, Writer)
		&& FFileHelper::SaveStringToFile(
			Json,
			*Config.GetManifestPath(),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11FinaleCameraCaptureConfigTest,
	"ABTS.M11C.CameraCapture.Config",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM11FinaleCameraCaptureConfigTest::RunTest(
	const FString& Parameters)
{
	FABTSM11FinaleCameraCaptureConfig Config;
	FString Failure;
	TestTrue(
		TEXT("Disabled command line parses without side effects"),
		FABTSM11FinaleCameraCaptureConfig::Parse(
			TEXT("-unattended"),
			Config,
			&Failure));
	TestFalse(TEXT("Disabled command line remains disabled"), Config.bEnabled);

	const FString Output = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("M11CaptureTest")));
	const FString ValidCommandLine = FString::Printf(
		TEXT("-ABTSM11CameraCapture -ABTSM11CaptureRank=11 ")
		TEXT("-ABTSM11CaptureStylized=1 -ABTSM11CaptureAutoExit=0 ")
		TEXT("-MovieFolder=\"%s\" -MovieName=Rank11_Stylized ")
		TEXT("-MovieFormat=JPG"),
		*Output);
	TestTrue(
		TEXT("Rank 11 stylized capture config parses"),
		FABTSM11FinaleCameraCaptureConfig::Parse(
			*ValidCommandLine,
			Config,
			&Failure));
	TestTrue(TEXT("Capture enabled"), Config.bEnabled);
	TestEqual(TEXT("Rank preserved"), Config.CandidateRank, 11);
	TestTrue(TEXT("Stylized preserved"), Config.bStylized);
	TestFalse(TEXT("Auto-exit override preserved"), Config.bAutoExit);
	TestEqual(
		TEXT("JPG capture protocol preserved"),
		Config.MovieFormat,
		FString(TEXT("JPG")));
	TestEqual(TEXT("Frame rate preserved"), Config.FrameRate, 30);
	TestEqual(
		TEXT("Muxed AVI filename is deterministic"),
		FPaths::GetCleanFilename(Config.GetExpectedVideoPath()),
		FString(TEXT("Rank11_Stylized.avi")));

	TestFalse(
		TEXT("Out-of-range Rank fails closed"),
		FABTSM11FinaleCameraCaptureConfig::Parse(
			TEXT("-ABTSM11CameraCapture -ABTSM11CaptureRank=12 ")
			TEXT("-MovieFolder=C:/Capture -MovieName=BadRank ")
			TEXT("-MovieFormat=JPG"),
			Config,
			&Failure));
	TestFalse(
		TEXT("Missing output folder fails closed"),
		FABTSM11FinaleCameraCaptureConfig::Parse(
			TEXT("-ABTSM11CameraCapture -MovieName=MissingFolder ")
			TEXT("-MovieFormat=JPG"),
			Config,
			&Failure));
	TestFalse(
		TEXT("Movie format tokens fail closed"),
		FABTSM11FinaleCameraCaptureConfig::Parse(
			TEXT("-ABTSM11CameraCapture -MovieFolder=C:/Capture ")
			TEXT("-MovieName={world} -MovieFormat=JPG"),
			Config,
			&Failure));
	TestFalse(
		TEXT("Native AVI capture fails closed"),
		FABTSM11FinaleCameraCaptureConfig::Parse(
			TEXT("-ABTSM11CameraCapture -MovieFolder=C:/Capture ")
			TEXT("-MovieName=NativeAvi"),
			Config,
			&Failure));
	return true;
}

#endif
