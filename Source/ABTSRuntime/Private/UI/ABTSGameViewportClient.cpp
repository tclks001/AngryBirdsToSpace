// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/ABTSGameViewportClient.h"

#include "ABTSRuntime.h"
#include "CoreGlobals.h"
#include "Audio/ABTSAudioWorldSubsystem.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Console.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "Guide/ABTSGuideWorldSubsystem.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Presentation/ABTSOpeningCinematicPreview.h"
#include "RHI.h"
#include "Slingshot/ABTSM6SlingshotSystem.h"
#include "UI/ABTSCanvasUI.h"
#include "UI/ABTSGameUserSettings.h"
#include "UI/ABTSUITheme.h"
#include "UnrealClient.h"
#include "World/ABTSM51WorldSystem.h"

namespace
{
	constexpr int32 AudioRowCount = 5;
	constexpr int32 VideoRowCount = 6;
	constexpr int32 AccessibilityRowCount = 6;
	constexpr float SettingStep = 0.05f;
	constexpr double VideoConfirmationDurationSeconds = 12.0;

	const TCHAR* QualityLabels[] = { TEXT("LOW"), TEXT("MEDIUM"), TEXT("HIGH"), TEXT("EPIC"), TEXT("CINEMATIC") };
	const TCHAR* ModeLabels[] = { TEXT("WINDOWED"), TEXT("BORDERLESS"), TEXT("FULLSCREEN") };
	const float FrameRateLimits[] = { 30.0f, 60.0f, 120.0f, 144.0f, 0.0f };

	UABTSGameViewportClient* ResolveABTSViewport()
	{
		return GEngine ? Cast<UABTSGameViewportClient>(GEngine->GameViewport) : nullptr;
	}

	EWindowMode::Type ModeFromIndex(const int32 Index)
	{
		switch (FMath::Clamp(Index, 0, 2))
		{
		case 0: return EWindowMode::Windowed;
		case 1: return EWindowMode::WindowedFullscreen;
		default: return EWindowMode::Fullscreen;
		}
	}

	int32 IndexFromMode(const EWindowMode::Type Mode)
	{
		if (Mode == EWindowMode::Windowed) return 0;
		if (Mode == EWindowMode::WindowedFullscreen) return 1;
		return 2;
	}

	FAutoConsoleCommand OpenMenuCommand(
		TEXT("abts.Menu.Open"),
		TEXT("Open the shared ABTS pause menu."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			if (UABTSGameViewportClient* Viewport = ResolveABTSViewport()) Viewport->OpenPauseMenu();
		}));

	FAutoConsoleCommand OpenFrontCommand(
		TEXT("abts.Menu.Front"),
		TEXT("Open the shared ABTS front screen."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			if (UABTSGameViewportClient* Viewport = ResolveABTSViewport()) Viewport->OpenFrontEnd();
		}));

	FAutoConsoleCommand OpenSettingsCommand(
		TEXT("abts.Menu.Settings"),
		TEXT("Open the shared ABTS settings screen."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			if (UABTSGameViewportClient* Viewport = ResolveABTSViewport()) Viewport->OpenSettingsMenu();
		}));

	FAutoConsoleCommand CloseMenuCommand(
		TEXT("abts.Menu.Close"),
		TEXT("Close the shared ABTS system menu."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			if (UABTSGameViewportClient* Viewport = ResolveABTSViewport()) Viewport->CloseSystemMenu();
		}));

	FAutoConsoleCommand DumpSettingsCommand(
		TEXT("abts.Settings.Dump"),
		TEXT("Print all resolved ABTS player settings."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			if (const UABTSGameUserSettings* Settings = UABTSGameUserSettings::Get())
			{
				UE_LOG(LogABTSRuntime, Display, TEXT("[ABTS][Settings] %s"), *Settings->BuildDiagnosticSummary());
			}
		}));

	FAutoConsoleCommand ResetSettingsCommand(
		TEXT("abts.Settings.Reset"),
		TEXT("Reset, apply, and save all ABTS player settings."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			if (UABTSGameViewportClient* Viewport = ResolveABTSViewport()) Viewport->ResetSettingsToDefaults();
		}));
}

void UABTSGameViewportClient::Init(
	FWorldContext& WorldContext,
	UGameInstance* OwningGameInstance,
	const bool bCreateNewAudioDevice)
{
	Super::Init(WorldContext, OwningGameInstance, bCreateNewAudioDevice);
	MenuCanvas = NewObject<UCanvas>(this, TEXT("ABTSSystemMenuCanvas"));
	RebuildResolutionOptions();
	// Continue the exact process-level clock used by the MoviePlayer screen.
	StartupForegroundStartSeconds = GStartTime;
	bStartupDiagnosticTraceEnabled = FParse::Param(
		FCommandLine::Get(), TEXT("ABTSReleaseStartupTrace"));
	if (bStartupDiagnosticTraceEnabled)
	{
		FParse::Value(
			FCommandLine::Get(),
			TEXT("ABTSReleaseStartupTraceOutput="),
			StartupDiagnosticTracePath);
		if (StartupDiagnosticTracePath.IsEmpty())
		{
			StartupDiagnosticTracePath = FPaths::Combine(
				FPaths::ProjectSavedDir(),
				TEXT("ABTSReleaseDiagnostics"),
				TEXT("StartupTrace.log"));
		}
		StartupDiagnosticTracePath = FPaths::ConvertRelativePathToFull(
			StartupDiagnosticTracePath);
		IFileManager::Get().MakeDirectory(
			*FPaths::GetPath(StartupDiagnosticTracePath), true);
		IFileManager::Get().Delete(*StartupDiagnosticTracePath, false, true);
		NextStartupDiagnosticTraceSeconds = 0.0;
	}

	FString CapturePage;
#if UE_BUILD_SHIPPING
	bCaptureMode = false;
#else
	bCaptureMode = FParse::Value(FCommandLine::Get(), TEXT("ABTSMenuCapture="), CapturePage);
#endif
	if (bCaptureMode)
	{
		CapturePage.TrimStartAndEndInline();
		if (CapturePage.StartsWith(TEXT("Opening"), ESearchCase::IgnoreCase))
		{
			bOpeningProductionCapture = true;
			MenuPage = EABTSSystemMenuPage::Front;
			FString DelaySuffix = CapturePage.RightChop(7);
			DelaySuffix.TrimStartAndEndInline();
			if (!DelaySuffix.IsEmpty())
			{
				OpeningProductionCaptureDelaySeconds = FMath::Clamp(
					FCString::Atof(*DelaySuffix),
					1.0f,
					40.0f);
			}
		}
		else if (CapturePage.Equals(TEXT("Pause"), ESearchCase::IgnoreCase))
		{
			MenuPage = EABTSSystemMenuPage::Pause;
		}
		else if (CapturePage.StartsWith(TEXT("Settings"), ESearchCase::IgnoreCase))
		{
			MenuPage = EABTSSystemMenuPage::Settings;
			if (CapturePage.Contains(TEXT("Video"))) SettingsSection = EABTSSettingsSection::Video;
			else if (CapturePage.Contains(TEXT("Accessibility"))) SettingsSection = EABTSSettingsSection::Accessibility;
			else SettingsSection = EABTSSettingsSection::Audio;
			if (CapturePage.Contains(TEXT("ResetConfirm")))
			{
				ActiveDialog = EABTSSystemMenuDialog::ConfirmReset;
				SelectedDialogAction = 1;
			}
			else if (CapturePage.Contains(TEXT("VideoConfirm")))
			{
				ActiveDialog = EABTSSystemMenuDialog::ConfirmVideo;
				VideoConfirmationDeadlineSeconds = FPlatformTime::Seconds() + VideoConfirmationDurationSeconds;
			}
		}
		else
		{
			MenuPage = EABTSSystemMenuPage::Front;
		}
		bMenuVisible = true;
		bInitialMenuStateResolved = true;
		// Front-end capture must traverse the same presentation-ready gate as the
		// interactive startup path. Otherwise its completed-menu frame counter is
		// never advanced and the capture waits until its timeout.
		bStartupFrontEndRequired = MenuPage == EABTSSystemMenuPage::Front;
		FParse::Value(FCommandLine::Get(), TEXT("ABTSMenuCaptureOutput="), CaptureOutputPath);
		if (CaptureOutputPath.IsEmpty())
		{
			const FString Directory = FPaths::Combine(
				FPaths::ProjectSavedDir(),
				TEXT("ABTSVisualCaptures/SystemMenu"),
				FString::Printf(TEXT("%s_%u"), *FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ")), FPlatformProcess::GetCurrentProcessId()));
			CaptureOutputPath = FPaths::Combine(Directory, CapturePage + TEXT(".png"));
		}
		CaptureOutputPath = FPaths::ConvertRelativePathToFull(CaptureOutputPath);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(CaptureOutputPath), true);
		CaptureStartSeconds = FPlatformTime::Seconds();
		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][SystemMenuCapture] Armed Page=%s OpeningProduction=%d DelaySeconds=%.2f Output=%s"),
			*CapturePage,
			bOpeningProductionCapture ? 1 : 0,
			OpeningProductionCaptureDelaySeconds,
			*CaptureOutputPath);
	}
	else
	{
#if UE_BUILD_SHIPPING
		constexpr bool bSkipFrontEnd = false;
#else
		const bool bSkipFrontEnd = FParse::Param(
			FCommandLine::Get(),
			TEXT("ABTSSkipFrontEnd"));
#endif
		bStartupFrontEndRequired = !IsRunningCommandlet()
			&& !FApp::IsUnattended()
			&& !bSkipFrontEnd;
		if (bStartupFrontEndRequired)
		{
			// Arm the correct full-screen front end before the first game viewport
			// present.  Waiting for the first local PlayerController created a visible
			// MoviePlayer -> partial HUD/clear-color gap in packaged builds.
			MenuPage = EABTSSystemMenuPage::Front;
			SelectedIndex = 0;
			bMenuVisible = true;
			bInitialMenuStateResolved = true;
			UE_LOG(LogABTSRuntime, Display,
				TEXT("[ABTS][StartupFlow] FrontEndPrearmed BeforeFirstPresent=1"));
		}
	}
}

void UABTSGameViewportClient::Tick(const float DeltaTime)
{
	Super::Tick(DeltaTime);
	RefreshStartupWorldState();
	WriteStartupDiagnosticTrace();
	EnsureInitialMenuState();
	if (bMenuVisible) ApplyMenuInputMode();
	UpdateOpeningProductionCapture();
	if (ActiveDialog == EABTSSystemMenuDialog::ConfirmVideo
		&& FPlatformTime::Seconds() >= VideoConfirmationDeadlineSeconds
		&& !bCaptureMode)
	{
		RevertVideoSettings(TEXT("Timeout"));
	}
	const double CaptureTimeoutSeconds = MenuPage == EABTSSystemMenuPage::Front ? 180.0 : 45.0;
	if (bCaptureMode && !bScreenshotRequested
		&& FPlatformTime::Seconds() - CaptureStartSeconds > CaptureTimeoutSeconds)
	{
		UE_LOG(LogABTSRuntime, Error, TEXT("[ABTS][SystemMenuCapture] Complete Success=0 Reason=Timeout Output=%s"), *CaptureOutputPath);
		FGenericPlatformMisc::RequestExitWithStatus(false, 1);
	}
}

float UABTSGameViewportClient::ComputeStartupLoadingProgress(
	const double ElapsedSeconds,
	const bool bReady)
{
	return bReady
		? 1.0f
		: FMath::Min(
			static_cast<float>(FMath::Max(0.0, ElapsedSeconds) / 30.0),
			0.92f);
}

bool UABTSGameViewportClient::IsStartupPresentationReady(
	const bool bWorldAuthorityReady,
	const bool bPresentationSurfaceReady,
	const int32 CompletedFrontEndDraws)
{
	// The counter is advanced only after DrawMenu has emitted a complete front
	// screen.  Keep two such frames behind the opaque handoff cover, then reveal
	// the already-warm menu atomically on the following present.
	return bWorldAuthorityReady
		&& bPresentationSurfaceReady
		&& CompletedFrontEndDraws >= 2;
}

bool UABTSGameViewportClient::IsStartupAuthorityReady(
	const bool bStartupFrontEndRequired,
	const bool bCaptureMode,
	const bool bFoundAuthority,
	const bool bAllAuthoritiesReady,
	const bool bAnyAuthorityFailed)
{
	if (bAnyAuthorityFailed)
	{
		return false;
	}

	const bool bAuthorityExpected = bStartupFrontEndRequired && !bCaptureMode;
	return bFoundAuthority
		? bAllAuthoritiesReady
		: !bAuthorityExpected;
}

void UABTSGameViewportClient::WriteStartupDiagnosticTrace()
{
	if (!bStartupDiagnosticTraceEnabled || StartupDiagnosticTracePath.IsEmpty())
	{
		return;
	}

	const double NowSeconds = FPlatformTime::Seconds();
	UWorld* GameWorld = GetWorld();
	const bool bHasBegunPlay = GameWorld != nullptr && GameWorld->HasBegunPlay();
	const bool bPaused = GameWorld != nullptr
		&& UGameplayStatics::IsGamePaused(GameWorld);
	int32 GateCount = 0;
	TArray<FString> GateStates;
	int32 M51Count = 0;
	TArray<FString> M51States;
	FString ActiveGuideId = TEXT("None");
	if (GameWorld != nullptr)
	{
		for (TActorIterator<AABTSM6SlingshotSystem> It(GameWorld); It; ++It)
		{
			++GateCount;
			GateStates.Add(It->BuildStartupPhysicsDiagnosticSummary());
		}
		for (TActorIterator<AABTSM51WorldSystem> It(GameWorld); It; ++It)
		{
			++M51Count;
			M51States.Add(It->BuildReleaseDiagnosticSummary());
		}
		if (const UABTSGuideWorldSubsystem* GuideSubsystem =
			GameWorld->GetSubsystem<UABTSGuideWorldSubsystem>())
		{
			FABTSGuidePresentationSnapshot ActiveGuide;
			if (GuideSubsystem->GetActiveGuide(ActiveGuide))
			{
				ActiveGuideId = ActiveGuide.GuideId.ToString();
			}
		}
	}
	const FString State = FString::Printf(
		TEXT("World=%s BegunPlay=%d WorldSeconds=%.3f Paused=%d")
		TEXT(" FrontEnd=%d MenuVisible=%d MenuPage=%d Canvas=%d")
		TEXT(" GateCount=%d GateRequired=%d AuthorityDiscovered=%d AuthorityReady=%d")
		TEXT(" PresentationReady=%d WorldReady=%d WorldFailed=%d Draws=%d Gates=[%s]")
		TEXT(" M51Count=%d M51=[%s] Guide=%s"),
		GameWorld != nullptr ? *GameWorld->GetName() : TEXT("None"),
		bHasBegunPlay ? 1 : 0,
		GameWorld != nullptr ? GameWorld->GetTimeSeconds() : -1.0f,
		bPaused ? 1 : 0,
		bStartupFrontEndRequired ? 1 : 0,
		bMenuVisible ? 1 : 0,
		static_cast<int32>(MenuPage),
		MenuCanvas != nullptr ? 1 : 0,
		GateCount,
		bStartupGateRequired ? 1 : 0,
		bStartupAuthorityDiscoveredForTrackedWorld ? 1 : 0,
		bStartupAuthorityReady ? 1 : 0,
		bStartupPresentationReady ? 1 : 0,
		bStartupWorldReady ? 1 : 0,
		bStartupWorldFailed ? 1 : 0,
		StartupReadyPresentationFrameCount,
		*FString::Join(GateStates, TEXT(" | ")),
		M51Count,
		*FString::Join(M51States, TEXT(" | ")),
		*ActiveGuideId);
	if (NowSeconds < NextStartupDiagnosticTraceSeconds)
	{
		return;
	}
	NextStartupDiagnosticTraceSeconds = NowSeconds + 2.0;
	const FString Line = FString::Printf(
		TEXT("Elapsed=%.3f Build=%s %s\n"),
		NowSeconds - StartupForegroundStartSeconds,
		UE_BUILD_SHIPPING ? TEXT("Shipping") : TEXT("NonShipping"),
		*State);
	FFileHelper::SaveStringToFile(
		Line,
		*StartupDiagnosticTracePath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
		&IFileManager::Get(),
		FILEWRITE_Append);
}

bool UABTSGameViewportClient::ShouldKeepWorldTickingForStartup(
	const bool bStartupFrontEndRequired,
	const bool bStartupWorldReady,
	const bool bStartupWorldFailed)
{
	// Shipping pre-arms the foreground before the M6 authority actor is
	// necessarily discoverable. Keying this decision on bStartupGateRequired
	// can pause the world before that actor has a chance to spawn or tick,
	// deadlocking the handoff cover at its pre-Ready progress cap.
	return bStartupFrontEndRequired
		&& !bStartupWorldReady
		&& !bStartupWorldFailed;
}

void UABTSGameViewportClient::RefreshStartupWorldState()
{
	UWorld* GameWorld = GetWorld();
	if (StartupTrackedWorld.Get() != GameWorld)
	{
		StartupTrackedWorld = GameWorld;
		// The first game world is the continuation of the MoviePlayer startup,
		// not a second load. Preserve the process-level foreground clock so the
		// Canvas bridge continues the same progress instead of restarting at 0.
		// Later world travel is a genuinely new handoff and gets a fresh clock.
		if (bStartupWorldHasBeenTracked)
		{
			StartupForegroundStartSeconds = FPlatformTime::Seconds();
		}
		bStartupWorldHasBeenTracked = true;
		bStartupGateRequired = false;
		bStartupAuthorityDiscoveredForTrackedWorld = false;
		bStartupAuthorityReady = false;
		bStartupWorldReady = false;
		bStartupWorldFailed = false;
		bStartupPresentationReady = false;
		StartupReadyPresentationFrameCount = 0;
		bStartupGateStartedLogged = false;
		bStartupGateTerminalLogged = false;
	}
	if (GameWorld == nullptr || !GameWorld->HasBegunPlay()) return;

	bool bFoundGate = false;
	bool bAllReady = true;
	bool bAnyFailed = false;
	for (TActorIterator<AABTSM6SlingshotSystem> It(GameWorld); It; ++It)
	{
		bFoundGate = true;
		bAllReady = bAllReady && It->IsStartupPhysicsWarmupComplete();
		bAnyFailed = bAnyFailed || It->HasStartupPhysicsWarmupFailed();
	}
	if (bFoundGate)
	{
		bStartupAuthorityDiscoveredForTrackedWorld = true;
	}
	const bool bAuthorityExpected = bStartupFrontEndRequired && !bCaptureMode;
	const bool bAuthorityLost = bAuthorityExpected
		&& bStartupAuthorityDiscoveredForTrackedWorld
		&& !bFoundGate;
	bStartupGateRequired = bAuthorityExpected || bFoundGate;
	bStartupWorldFailed = bAnyFailed || bAuthorityLost;
	const bool bWorldAuthorityReady = IsStartupAuthorityReady(
		bStartupFrontEndRequired,
		bCaptureMode,
		bFoundGate,
		bAllReady,
		bStartupWorldFailed);
	bStartupAuthorityReady = bWorldAuthorityReady;
	const bool bPresentationSurfaceReady = !bStartupFrontEndRequired
		|| (bMenuVisible
			&& MenuPage == EABTSSystemMenuPage::Front
			&& MenuCanvas != nullptr);
	if (!bWorldAuthorityReady || !bPresentationSurfaceReady)
	{
		StartupReadyPresentationFrameCount = 0;
	}
	bStartupPresentationReady = IsStartupPresentationReady(
		bWorldAuthorityReady,
		bPresentationSurfaceReady,
		StartupReadyPresentationFrameCount);
	bStartupWorldReady = bWorldAuthorityReady
		&& bStartupPresentationReady;
	if (bFoundGate && !bStartupGateStartedLogged)
	{
		bStartupGateStartedLogged = true;
		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][StartupFlow] ForegroundGateStarted TargetSeconds=30"));
	}
	if (bFoundGate && (bStartupWorldReady || bStartupWorldFailed)
		&& !bStartupGateTerminalLogged)
	{
		bStartupGateTerminalLogged = true;
		if (bStartupWorldReady)
		{
			UE_LOG(LogABTSRuntime, Display,
				TEXT("[ABTS][StartupFlow] ForegroundGateTerminal Ready=1 Failed=0 PresentationFrames=%d ElapsedSeconds=%.3f"),
				StartupReadyPresentationFrameCount,
				FPlatformTime::Seconds() - StartupForegroundStartSeconds);
		}
		else
		{
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][StartupFlow] ForegroundGateTerminal Ready=0 Failed=1 ElapsedSeconds=%.3f"),
				FPlatformTime::Seconds() - StartupForegroundStartSeconds);
		}
	}
}

bool UABTSGameViewportClient::IsStartupInputBlocked() const
{
	return (bStartupFrontEndRequired && !bStartupPresentationReady)
		|| (bStartupGateRequired && !bStartupWorldReady);
}

void UABTSGameViewportClient::EnsureInitialMenuState()
{
	if (bInitialMenuStateResolved || bCaptureMode) return;
	UWorld* GameWorld = GetWorld();
	if (!GameWorld || !GameWorld->HasBegunPlay() || !GEngine || !GEngine->GetFirstLocalPlayerController(GameWorld)) return;
	bInitialMenuStateResolved = true;
#if UE_BUILD_SHIPPING
	constexpr bool bSkip = false;
#else
	const bool bSkip = FParse::Param(FCommandLine::Get(), TEXT("ABTSSkipFrontEnd"));
#endif
	const bool bInteractive = !IsRunningCommandlet() && !FApp::IsUnattended();
	if (bInteractive && !bSkip) OpenFrontEnd();
}

void UABTSGameViewportClient::Draw(FViewport* InViewport, FCanvas* SceneCanvas)
{
	Super::Draw(InViewport, SceneCanvas);
	if (!bMenuVisible || !InViewport || !SceneCanvas || !MenuCanvas) return;
	const FIntPoint ViewSize = InViewport->GetSizeXY();
	if (ViewSize.X <= 0 || ViewSize.Y <= 0) return;
	// System UI must be the final overlay. Using the debug canvas here keeps
	// transient world debug messages and stage HUD diagnostics behind the menu.
	FCanvas* OverlayCanvas = InViewport->GetDebugCanvas();
	if (!OverlayCanvas) OverlayCanvas = SceneCanvas;
	MenuCanvas->Init(ViewSize.X, ViewSize.Y, nullptr, OverlayCanvas);
	DrawMenu(*MenuCanvas, FVector2D(ViewSize));
	if (bStartupFrontEndRequired
		&& !bStartupPresentationReady
		&& MenuPage == EABTSSystemMenuPage::Front)
	{
		// Count only fully emitted menus, not merely allocated UCanvas objects.
		// The cover is drawn last and opaque, so neither the world HUD nor a
		// partially initialized front screen can leak between MoviePlayer and
		// the first valid interactive frame.
		if (bStartupAuthorityReady && HitTargets.Num() >= 2)
		{
			++StartupReadyPresentationFrameCount;
		}
		else
		{
			StartupReadyPresentationFrameCount = 0;
		}
		DrawStartupHandoffCover(*MenuCanvas, FVector2D(ViewSize));
	}
	if (bCaptureMode)
	{
		++CaptureFrameCount;
		MaybeRequestCapture();
	}
}

bool UABTSGameViewportClient::InputKey(const FInputKeyEventArgs& EventArgs)
{
	if (ViewportConsole && ViewportConsole->ConsoleState != NAME_None)
	{
		return Super::InputKey(EventArgs);
	}
	const bool bPressed = EventArgs.Event == IE_Pressed || EventArgs.Event == IE_Repeat;
	if (!bMenuVisible)
	{
		if (bPressed && (EventArgs.Key == EKeys::Escape || EventArgs.Key == EKeys::Gamepad_Special_Right))
		{
			OpenPauseMenu();
			return true;
		}
		return Super::InputKey(EventArgs);
	}
	if (!bPressed) return true;
	if (EventArgs.Key == EKeys::LeftMouseButton) return HandlePointerClick(EventArgs.Viewport ? EventArgs.Viewport : Viewport);
	if (ActiveDialog != EABTSSystemMenuDialog::None)
	{
		if (EventArgs.Key == EKeys::Left || EventArgs.Key == EKeys::Right
			|| EventArgs.Key == EKeys::A || EventArgs.Key == EKeys::D
			|| EventArgs.Key == EKeys::Up || EventArgs.Key == EKeys::Down
			|| EventArgs.Key == EKeys::Gamepad_DPad_Left || EventArgs.Key == EKeys::Gamepad_DPad_Right)
		{
			SelectedDialogAction = 1 - SelectedDialogAction;
			PlayUIFeedback(false);
			return true;
		}
		if (EventArgs.Key == EKeys::Enter || EventArgs.Key == EKeys::SpaceBar || EventArgs.Key == EKeys::Gamepad_FaceButton_Bottom)
		{
			if (ActiveDialog == EABTSSystemMenuDialog::ConfirmVideo)
			{
				if (SelectedDialogAction == 0) KeepVideoSettings(); else RevertVideoSettings(TEXT("User"));
			}
			else
			{
				if (SelectedDialogAction == 0) ResetSettingsToDefaults(); else CancelDialog();
			}
			return true;
		}
		if (EventArgs.Key == EKeys::Escape || EventArgs.Key == EKeys::Gamepad_FaceButton_Right)
		{
			if (ActiveDialog == EABTSSystemMenuDialog::ConfirmVideo) RevertVideoSettings(TEXT("Cancel"));
			else CancelDialog();
			return true;
		}
		return true;
	}
	if (EventArgs.Key == EKeys::Up || EventArgs.Key == EKeys::W || EventArgs.Key == EKeys::Gamepad_DPad_Up) { Navigate(-1); return true; }
	if (EventArgs.Key == EKeys::Down || EventArgs.Key == EKeys::S || EventArgs.Key == EKeys::Gamepad_DPad_Down) { Navigate(1); return true; }
	if (EventArgs.Key == EKeys::Left || EventArgs.Key == EKeys::A || EventArgs.Key == EKeys::Gamepad_DPad_Left)
	{
		if (MenuPage == EABTSSystemMenuPage::Settings && SelectedIndex < GetSettingsRowCount()) AdjustSetting(SelectedIndex, -1);
		return true;
	}
	if (EventArgs.Key == EKeys::Right || EventArgs.Key == EKeys::D || EventArgs.Key == EKeys::Gamepad_DPad_Right)
	{
		if (MenuPage == EABTSSystemMenuPage::Settings && SelectedIndex < GetSettingsRowCount()) AdjustSetting(SelectedIndex, 1);
		return true;
	}
	if (EventArgs.Key == EKeys::Q || EventArgs.Key == EKeys::Gamepad_LeftShoulder) { CycleSettingsSection(-1); return true; }
	if (EventArgs.Key == EKeys::E || EventArgs.Key == EKeys::Gamepad_RightShoulder) { CycleSettingsSection(1); return true; }
	if (EventArgs.Key == EKeys::Enter || EventArgs.Key == EKeys::SpaceBar || EventArgs.Key == EKeys::Gamepad_FaceButton_Bottom) { ActivateSelection(); return true; }
	if (EventArgs.Key == EKeys::Escape || EventArgs.Key == EKeys::Gamepad_FaceButton_Right)
	{
		if (MenuPage == EABTSSystemMenuPage::Settings) HandleAction(EHitAction::Back);
		else CloseSystemMenu();
		return true;
	}
	return true;
}

bool UABTSGameViewportClient::InputAxis(const FInputKeyEventArgs& EventArgs)
{
	return bMenuVisible ? true : Super::InputAxis(EventArgs);
}

EMouseCursor::Type UABTSGameViewportClient::GetCursor(FViewport* InViewport, const int32 X, const int32 Y)
{
	if (bMenuVisible)
	{
		const FVector2D Point(static_cast<double>(X), static_cast<double>(Y));
		for (const FHitTarget& Target : HitTargets)
		{
			if (Target.Box.IsInside(Point)) return EMouseCursor::Hand;
		}
		return EMouseCursor::Default;
	}
	return Super::GetCursor(InViewport, X, Y);
}

void UABTSGameViewportClient::OpenFrontEnd()
{
	SelectedIndex = 0;
	SetMenuVisible(true, EABTSSystemMenuPage::Front);
}

void UABTSGameViewportClient::OpenPauseMenu()
{
	SelectedIndex = 0;
	SetMenuVisible(true, EABTSSystemMenuPage::Pause);
}

void UABTSGameViewportClient::OpenSettingsMenu(const EABTSSettingsSection Section)
{
	SettingsReturnPage = bMenuVisible && MenuPage == EABTSSystemMenuPage::Pause
		? EABTSSystemMenuPage::Pause
		: EABTSSystemMenuPage::Front;
	SettingsSection = Section;
	SelectedIndex = 0;
	SetMenuVisible(true, EABTSSystemMenuPage::Settings);
}

void UABTSGameViewportClient::CloseSystemMenu()
{
	if (!bMenuVisible || (bCaptureMode && !bOpeningProductionCapture)) return;
	if (IsStartupInputBlocked())
	{
		UE_LOG(LogABTSRuntime, Warning,
			TEXT("[ABTS][StartupFlow] BeginBlocked Ready=0 Failed=%d"),
			bStartupWorldFailed ? 1 : 0);
		return;
	}
	SetMenuVisible(false, MenuPage);
}

void UABTSGameViewportClient::SetMenuVisible(const bool bVisible, const EABTSSystemMenuPage NewPage)
{
	// Console commands remain available while the menu owns input. Resolve any exclusive
	// confirmation before an explicit page transition so a hidden dialog cannot keep a
	// newly applied display mode alive or leak stale hit targets into the next page.
	if (ActiveDialog == EABTSSystemMenuDialog::ConfirmVideo)
	{
		RevertVideoSettings(TEXT("MenuNavigation"));
	}
	else if (ActiveDialog == EABTSSystemMenuDialog::ConfirmReset)
	{
		CancelDialog();
	}
	MenuPage = NewPage;
	if (bVisible && !bMenuVisible)
	{
		if (UWorld* GameWorld = GetWorld())
		{
			bWorldWasPaused = UGameplayStatics::IsGamePaused(GameWorld);
			if (GEngine)
			{
				if (APlayerController* PC = GEngine->GetFirstLocalPlayerController(GameWorld))
				{
					bPreviousMouseCursor = PC->bShowMouseCursor;
					MenuPlayerController = PC;
					bInputStateCaptured = true;
				}
			}
		}
	}
	bMenuVisible = bVisible;
	if (bMenuVisible)
	{
		ApplyMenuInputMode();
		PlayUIFeedback(false);
	}
	else
	{
		RestoreGameplayInputMode();
		PlayUIFeedback(false);
	}
}

void UABTSGameViewportClient::ApplyMenuInputMode()
{
	UWorld* GameWorld = GetWorld();
	APlayerController* PC = GEngine && GameWorld ? GEngine->GetFirstLocalPlayerController(GameWorld) : nullptr;
	if (!PC) return;
	if (!bInputStateCaptured)
	{
		bWorldWasPaused = UGameplayStatics::IsGamePaused(GameWorld);
		bPreviousMouseCursor = PC->bShowMouseCursor;
		bInputStateCaptured = true;
		MenuPlayerController = PC;
	}
	// The foreground owns input during startup, but the authoritative M3/M7/M6
	// generation and Chaos gates still need World Tick.  Do not let a pause that
	// predates input-state capture (including the transient packaged-startup
	// pause) deadlock the authority actors.  While this menu is visible it owns
	// the active pause state; RestoreGameplayInputMode restores the state that
	// was captured before ownership began.
	const bool bStartupGenerationRunning = ShouldKeepWorldTickingForStartup(
		bStartupFrontEndRequired,
		bStartupWorldReady,
		bStartupWorldFailed);
	const bool bShouldPauseForMenu = !bStartupGenerationRunning;
	if (UGameplayStatics::IsGamePaused(GameWorld) != bShouldPauseForMenu)
	{
		PC->SetPause(bShouldPauseForMenu);
	}
	PC->bShowMouseCursor = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(InputMode);
}

void UABTSGameViewportClient::RestoreGameplayInputMode()
{
	APlayerController* PC = MenuPlayerController.Get();
	if (!PC && GEngine && GetWorld()) PC = GEngine->GetFirstLocalPlayerController(GetWorld());
	if (PC)
	{
		if (UWorld* GameWorld = PC->GetWorld())
		{
			if (UGameplayStatics::IsGamePaused(GameWorld) != bWorldWasPaused)
			{
				PC->SetPause(bWorldWasPaused);
			}
		}
		PC->bShowMouseCursor = bPreviousMouseCursor;
		if (bPreviousMouseCursor) PC->SetInputMode(FInputModeGameAndUI());
		else PC->SetInputMode(FInputModeGameOnly());
	}
	bInputStateCaptured = false;
	MenuPlayerController.Reset();
}

void UABTSGameViewportClient::RebuildResolutionOptions()
{
	UABTSGameUserSettings* Settings = UABTSGameUserSettings::Get();
	const FIntPoint Desktop = Settings ? Settings->GetDesktopResolution() : FIntPoint(1920, 1080);
	ResolutionOptions = BuildFallbackResolutionOptions(Desktop);
	if (GDynamicRHI)
	{
		FScreenResolutionArray Available;
		if (RHIGetAvailableResolutions(Available, true))
		{
			for (const FScreenResolutionRHI& Resolution : Available)
			{
				const FIntPoint Candidate(Resolution.Width, Resolution.Height);
				if (Candidate.X >= 1024 && Candidate.Y >= 576) ResolutionOptions.AddUnique(Candidate);
			}
		}
	}
	ResolutionOptions.Sort([](const FIntPoint& A, const FIntPoint& B)
	{
		return A.X == B.X ? A.Y < B.Y : A.X < B.X;
	});
	const FIntPoint Current = Settings ? Settings->GetScreenResolution() : Desktop;
	CurrentResolutionIndex = ResolutionOptions.IndexOfByKey(Current);
	if (CurrentResolutionIndex == INDEX_NONE)
	{
		ResolutionOptions.Add(Current);
		ResolutionOptions.Sort([](const FIntPoint& A, const FIntPoint& B)
		{
			return A.X == B.X ? A.Y < B.Y : A.X < B.X;
		});
		CurrentResolutionIndex = ResolutionOptions.IndexOfByKey(Current);
	}
}

TArray<FIntPoint> UABTSGameViewportClient::BuildFallbackResolutionOptions(const FIntPoint DesktopResolution)
{
	TArray<FIntPoint> Result = {
		FIntPoint(1280, 720),
		FIntPoint(1600, 900),
		FIntPoint(1920, 1080),
		FIntPoint(2560, 1440),
		FIntPoint(3840, 2160)
	};
	Result.RemoveAll([DesktopResolution](const FIntPoint& Candidate)
	{
		return Candidate.X > DesktopResolution.X || Candidate.Y > DesktopResolution.Y;
	});
	if (DesktopResolution.X > 0 && DesktopResolution.Y > 0) Result.AddUnique(DesktopResolution);
	if (Result.IsEmpty()) Result.Add(FIntPoint(1280, 720));
	Result.Sort([](const FIntPoint& A, const FIntPoint& B)
	{
		return A.X == B.X ? A.Y < B.Y : A.X < B.X;
	});
	return Result;
}

FString UABTSGameViewportClient::FormatFrameRateLimit(const float Limit)
{
	return Limit <= 1.0f ? FString(TEXT("UNLIMITED")) : FString::Printf(TEXT("%.0f FPS"), Limit);
}

int32 UABTSGameViewportClient::ComputeConfirmationSecondsRemaining(
	const double DeadlineSeconds,
	const double NowSeconds)
{
	return FMath::Max(0, FMath::CeilToInt(DeadlineSeconds - NowSeconds));
}

void UABTSGameViewportClient::DrawMenu(UCanvas& Canvas, const FVector2D& ViewSize)
{
	HitTargets.Reset();
	DrawBackdrop(Canvas, ViewSize);
	if (MenuPage == EABTSSystemMenuPage::Settings) DrawSettings(Canvas, ViewSize);
	else DrawFrontOrPause(Canvas, ViewSize);
	if (ActiveDialog != EABTSSystemMenuDialog::None) DrawDialog(Canvas, ViewSize);
}

void UABTSGameViewportClient::DrawStartupHandoffCover(
	UCanvas& Canvas,
	const FVector2D& ViewSize)
{
	// This Canvas bridge deliberately mirrors the asset-free MoviePlayer page.
	// It survives the one-or-more presents between MoviePlayer teardown and the
	// first fully initialized front-end draw without depending on Slate lifetime.
	// Slate and Canvas travel through different color/gamma paths. Authored black
	// is invariant across both and makes the ownership handoff visually exact.
	const FLinearColor Background = FLinearColor::Black;
	const FLinearColor Accent(0.18f, 0.82f, 0.94f, 1.0f);
	const FLinearColor Text(0.72f, 0.82f, 0.92f, 1.0f);
	Canvas.K2_DrawTexture(
		Canvas.DefaultTexture,
		FVector2D::ZeroVector,
		ViewSize,
		FVector2D::ZeroVector,
		FVector2D::UnitVector,
		Background,
		BLEND_Translucent);

	const float Scale = FMath::Clamp(
		FMath::Min(ViewSize.X / 1920.0f, ViewSize.Y / 1080.0f),
		0.65f,
		1.35f);
	const float Width = FMath::Min(720.0f * Scale, ViewSize.X * 0.72f);
	const FVector2D Center(ViewSize.X * 0.5f, ViewSize.Y * 0.5f);
	DrawLabel(
		Canvas,
		TEXT("ANGRY BIRDS TO SPACE"),
		Center - FVector2D(0.0f, 30.0f * Scale),
		1.34f * Scale,
		FLinearColor(0.31f, 0.91f, 1.0f, 1.0f),
		true,
		true);
	DrawLabel(
		Canvas,
		bStartupAuthorityReady
			? TEXT("WORLD READY")
			: TEXT("GENERATING PLANETARY WORLD"),
		Center + FVector2D(0.0f, 18.0f * Scale),
		0.72f * Scale,
		Text,
		false,
		true);

	const float Progress = ComputeStartupLoadingProgress(
		FPlatformTime::Seconds() - StartupForegroundStartSeconds,
		bStartupAuthorityReady);
	const FVector2D TrackMin(
		Center.X - Width * 0.5f,
		Center.Y + 44.0f * Scale);
	const FVector2D TrackSize(Width, 10.0f * Scale);
	Canvas.K2_DrawTexture(
		Canvas.DefaultTexture,
		TrackMin,
		TrackSize,
		FVector2D::ZeroVector,
		FVector2D::UnitVector,
		FLinearColor(0.08f, 0.15f, 0.24f, 1.0f),
		BLEND_Translucent);
	Canvas.K2_DrawTexture(
		Canvas.DefaultTexture,
		TrackMin,
		FVector2D(TrackSize.X * Progress, TrackSize.Y),
		FVector2D::ZeroVector,
		FVector2D::UnitVector,
		Accent,
		BLEND_Translucent);
}

void UABTSGameViewportClient::DrawBackdrop(UCanvas& Canvas, const FVector2D& ViewSize)
{
	const FABTSUIThemeSnapshot Theme = FABTSUITheme::Get();
	const UABTSGameUserSettings* Settings = UABTSGameUserSettings::Get();
	FLinearColor Dim = Theme.PanelPrimary;
	Dim.A = Settings && Settings->GetHighContrastMenu() ? 0.96f : 0.86f;
	Canvas.K2_DrawTexture(Canvas.DefaultTexture, FVector2D::ZeroVector, ViewSize, FVector2D::ZeroVector, FVector2D::UnitVector, Dim, BLEND_Translucent);
	const float Grid = FMath::Clamp(ViewSize.Y / 12.0f, 48.0f, 96.0f);
	FLinearColor GridColor = Theme.PanelBorder;
	GridColor.A = 0.18f;
	for (float X = 0.0f; X < ViewSize.X; X += Grid) Canvas.K2_DrawLine(FVector2D(X, 0.0f), FVector2D(X, ViewSize.Y), 1.0f, GridColor);
	for (float Y = 0.0f; Y < ViewSize.Y; Y += Grid) Canvas.K2_DrawLine(FVector2D(0.0f, Y), FVector2D(ViewSize.X, Y), 1.0f, GridColor);
	Canvas.K2_DrawLine(FVector2D(0.0f, 5.0f), FVector2D(ViewSize.X, 5.0f), 5.0f, Theme.AccentSecondary);
	Canvas.K2_DrawLine(FVector2D(0.0f, ViewSize.Y - 5.0f), FVector2D(ViewSize.X, ViewSize.Y - 5.0f), 5.0f, Theme.AccentPrimary);
}

void UABTSGameViewportClient::DrawFrontOrPause(UCanvas& Canvas, const FVector2D& ViewSize)
{
	const FABTSUIThemeSnapshot Theme = FABTSUITheme::Get();
	const UABTSGameUserSettings* Settings = UABTSGameUserSettings::Get();
	const float Scale = FMath::Clamp(FMath::Min(ViewSize.X / 1920.0f, ViewSize.Y / 1080.0f) * (Settings ? Settings->GetMenuScale() : 1.0f), 0.65f, 1.35f);
	const float Margin = 82.0f * Scale;
	const FBox2D MainBox(FVector2D(Margin, Margin), FVector2D(ViewSize.X - Margin, ViewSize.Y - Margin));
	FABTSCanvasUI::DrawFacetedBox(Canvas, Theme, MainBox, Theme.PanelPrimary, Theme.SlotBorder, 24.0f * Scale, 5.0f * Scale);
	FABTSCanvasUI::DrawCornerBrackets(Canvas, Theme, MainBox, Theme.AccentSecondary, 42.0f * Scale, 12.0f * Scale, 3.0f * Scale);

	const FString Kicker = MenuPage == EABTSSystemMenuPage::Front ? TEXT("ORBITAL EXPEDITION CONTROL") : TEXT("MISSION TEMPORARILY SUSPENDED");
	const FString Title = MenuPage == EABTSSystemMenuPage::Front ? TEXT("ANGRY BIRDS // TO SPACE") : TEXT("PAUSE MENU");
	DrawLabel(Canvas, Kicker, MainBox.Min + FVector2D(58.0f, 52.0f) * Scale, 0.78f * Scale, Theme.AccentSecondary);
	DrawLabel(Canvas, Title, MainBox.Min + FVector2D(56.0f, 92.0f) * Scale, 1.32f * Scale, Theme.TextPrimary, true);
	DrawLabel(Canvas, TEXT("BUILD  /  LAUNCH  /  BREAK ORBIT"), MainBox.Min + FVector2D(60.0f, 156.0f) * Scale, 0.72f * Scale, Theme.TextMuted);

	const float ButtonX = MainBox.Min.X + 60.0f * Scale;
	const float ButtonW = FMath::Min(560.0f * Scale, MainBox.GetSize().X * 0.44f);
	const float ButtonH = 72.0f * Scale;
	float ButtonY = MainBox.Min.Y + 235.0f * Scale;
	int32 NavigationIndex = 0;
	if (MenuPage == EABTSSystemMenuPage::Front)
	{
		const bool bCanBegin = !IsStartupInputBlocked();
		DrawButton(Canvas,
			FBox2D(FVector2D(ButtonX, ButtonY), FVector2D(ButtonX + ButtonW, ButtonY + ButtonH)),
			bCanBegin ? TEXT("BEGIN EXPEDITION") : TEXT("GENERATING WORLD"),
			EHitAction::Begin, NavigationIndex++, INDEX_NONE, 0, bCanBegin);
		if (!bCanBegin)
		{
			const float Progress = ComputeStartupLoadingProgress(
				FPlatformTime::Seconds() - StartupForegroundStartSeconds,
				false);
			const FBox2D Track(
				FVector2D(ButtonX, ButtonY + ButtonH + 10.0f * Scale),
				FVector2D(ButtonX + ButtonW, ButtonY + ButtonH + 20.0f * Scale));
			Canvas.K2_DrawTexture(Canvas.DefaultTexture, Track.Min, Track.GetSize(),
				FVector2D::ZeroVector, FVector2D::UnitVector,
				Theme.SlotBorder, BLEND_Translucent);
			const FBox2D Fill(Track.Min,
				FVector2D(FMath::Lerp(Track.Min.X, Track.Max.X, Progress), Track.Max.Y));
			Canvas.K2_DrawTexture(Canvas.DefaultTexture, Fill.Min, Fill.GetSize(),
				FVector2D::ZeroVector, FVector2D::UnitVector,
				bStartupWorldFailed ? Theme.Danger : Theme.AccentSecondary,
				BLEND_Translucent);
			const FString LoadingStatus = bStartupWorldFailed
				? FString(TEXT("WORLD GENERATION FAILED // GAMEPLAY LOCKED"))
				: FString::Printf(TEXT("LOADING %d%% // GAMEPLAY LOCKED UNTIL READY"),
					FMath::RoundToInt(Progress * 100.0f));
			DrawLabel(Canvas,
				LoadingStatus,
				FVector2D(ButtonX, Track.Max.Y + 10.0f * Scale),
				0.54f * Scale,
				bStartupWorldFailed ? Theme.Danger : Theme.TextMuted);
		}
		ButtonY += (bCanBegin ? 88.0f : 122.0f) * Scale;
		DrawButton(Canvas, FBox2D(FVector2D(ButtonX, ButtonY), FVector2D(ButtonX + ButtonW, ButtonY + ButtonH)), TEXT("SETTINGS"), EHitAction::Settings, NavigationIndex++);
	}
	else
	{
		DrawButton(Canvas, FBox2D(FVector2D(ButtonX, ButtonY), FVector2D(ButtonX + ButtonW, ButtonY + ButtonH)), TEXT("RESUME MISSION"), EHitAction::Resume, NavigationIndex++);
		ButtonY += 88.0f * Scale;
		DrawButton(Canvas, FBox2D(FVector2D(ButtonX, ButtonY), FVector2D(ButtonX + ButtonW, ButtonY + ButtonH)), TEXT("SETTINGS"), EHitAction::Settings, NavigationIndex++);
		ButtonY += 88.0f * Scale;
		DrawButton(Canvas, FBox2D(FVector2D(ButtonX, ButtonY), FVector2D(ButtonX + ButtonW, ButtonY + ButtonH)), TEXT("RETURN TO TITLE"), EHitAction::ReturnToTitle, NavigationIndex++);
	}
	if (CanQuitCurrentWorld())
	{
		ButtonY += 88.0f * Scale;
		DrawButton(Canvas, FBox2D(FVector2D(ButtonX, ButtonY), FVector2D(ButtonX + ButtonW, ButtonY + ButtonH)), TEXT("QUIT GAME"), EHitAction::Quit, NavigationIndex++);
	}

	const float OrbitalPanelX = MainBox.Min.X + MainBox.GetSize().X * 0.58f;
	const FBox2D OrbitalBox(
		FVector2D(OrbitalPanelX, MainBox.Min.Y + 185.0f * Scale),
		FVector2D(MainBox.Max.X - 54.0f * Scale, MainBox.Max.Y - 92.0f * Scale));
	FABTSCanvasUI::DrawFacetedBox(Canvas, Theme, OrbitalBox, Theme.PanelSecondary, Theme.PanelBorder, 18.0f * Scale, 3.0f * Scale);
	DrawLabel(Canvas, TEXT("MISSION TELEMETRY"), OrbitalBox.Min + FVector2D(28.0f, 24.0f) * Scale, 0.72f * Scale, Theme.TextMuted);
	DrawLabel(Canvas, TEXT("PRIMARY WORLD"), OrbitalBox.Min + FVector2D(28.0f, 66.0f) * Scale, 0.82f * Scale, Theme.TextPrimary);
	DrawLabel(Canvas, TEXT("ONLINE"), FVector2D(OrbitalBox.Max.X - 30.0f * Scale, OrbitalBox.Min.Y + 66.0f * Scale), 0.82f * Scale, Theme.Success, false, true);
	const FVector2D Center(OrbitalBox.GetCenter().X, OrbitalBox.GetCenter().Y + 26.0f * Scale);
	const float Radius = FMath::Min(OrbitalBox.GetSize().X, OrbitalBox.GetSize().Y) * 0.27f;
	for (int32 Ring = 0; Ring < 3; ++Ring)
	{
		const float RingRadius = Radius * (0.48f + Ring * 0.28f);
		for (int32 Segment = 0; Segment < 40; ++Segment)
		{
			const float A0 = UE_TWO_PI * static_cast<float>(Segment) / 40.0f;
			const float A1 = UE_TWO_PI * static_cast<float>(Segment + 1) / 40.0f;
			const FVector2D P0 = Center + FVector2D(FMath::Cos(A0), FMath::Sin(A0) * 0.42f) * RingRadius;
			const FVector2D P1 = Center + FVector2D(FMath::Cos(A1), FMath::Sin(A1) * 0.42f) * RingRadius;
			Canvas.K2_DrawLine(P0, P1, Ring == 1 ? 3.0f * Scale : 1.5f * Scale, Ring == 1 ? Theme.AccentSecondary : Theme.PanelBorder);
		}
	}
	Canvas.K2_DrawBox(Center - FVector2D(9.0f * Scale), FVector2D(18.0f * Scale), 3.0f * Scale, Theme.AccentPrimary);
	DrawLabel(
		Canvas,
		TEXT("ESC  PAUSE     ARROWS  NAVIGATE     ENTER  CONFIRM"),
		FVector2D(MainBox.Min.X + 60.0f * Scale, MainBox.Max.Y - 54.0f * Scale),
		0.62f * Scale,
		Theme.TextMuted);
}

void UABTSGameViewportClient::DrawSettings(UCanvas& Canvas, const FVector2D& ViewSize)
{
	const FABTSUIThemeSnapshot Theme = FABTSUITheme::Get();
	const UABTSGameUserSettings* Settings = UABTSGameUserSettings::Get();
	const float Scale = FMath::Clamp(FMath::Min(ViewSize.X / 1920.0f, ViewSize.Y / 1080.0f) * (Settings ? Settings->GetMenuScale() : 1.0f), 0.65f, 1.35f);
	const float Margin = 68.0f * Scale;
	const FBox2D Panel(FVector2D(Margin, Margin), FVector2D(ViewSize.X - Margin, ViewSize.Y - Margin));
	FABTSCanvasUI::DrawFacetedBox(Canvas, Theme, Panel, Theme.PanelPrimary, Theme.SlotBorder, 24.0f * Scale, 5.0f * Scale);
	FABTSCanvasUI::DrawCornerBrackets(Canvas, Theme, Panel, Theme.AccentSecondary, 44.0f * Scale, 12.0f * Scale, 3.0f * Scale);
	DrawLabel(Canvas, TEXT("SYSTEM CONFIGURATION"), Panel.Min + FVector2D(48.0f, 34.0f) * Scale, 1.08f * Scale, Theme.TextPrimary, true);
	DrawLabel(Canvas, TEXT("ALL CHANGES APPLY AND SAVE IMMEDIATELY"), Panel.Min + FVector2D(50.0f, 82.0f) * Scale, 0.62f * Scale, Theme.TextMuted);

	const float TabsY = Panel.Min.Y + 118.0f * Scale;
	const float TabX = Panel.Min.X + 48.0f * Scale;
	const float TabGap = 12.0f * Scale;
	const float TabW = (Panel.GetSize().X - 96.0f * Scale - 2.0f * TabGap) / 3.0f;
	const float TabH = 54.0f * Scale;
	const EHitAction TabActions[] = { EHitAction::TabAudio, EHitAction::TabVideo, EHitAction::TabAccessibility };
	const TCHAR* TabLabels[] = { TEXT("AUDIO"), TEXT("VIDEO"), TEXT("ACCESSIBILITY") };
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const FBox2D Box(
			FVector2D(TabX + Index * (TabW + TabGap), TabsY),
			FVector2D(TabX + Index * (TabW + TabGap) + TabW, TabsY + TabH));
		const bool bActive = Index == static_cast<int32>(SettingsSection);
		FABTSCanvasUI::DrawFacetedBox(Canvas, Theme, Box, bActive ? Theme.SlotSelected : Theme.SlotNormal, bActive ? Theme.AccentPrimary : Theme.PanelBorder, 10.0f * Scale, bActive ? 3.0f * Scale : 2.0f * Scale);
		DrawLabel(Canvas, TabLabels[Index], Box.GetCenter(), 0.72f * Scale, bActive ? Theme.TextPrimary : Theme.TextMuted, false, true);
		AddHitTarget(Box, TabActions[Index]);
	}

	const int32 RowCount = GetSettingsRowCount();
	const float RowsTop = TabsY + TabH + 28.0f * Scale;
	const float BottomReserve = 112.0f * Scale;
	const float RowGap = 10.0f * Scale;
	const float RowHeight = FMath::Min(82.0f * Scale, (Panel.Max.Y - BottomReserve - RowsTop - RowGap * (RowCount - 1)) / RowCount);
	for (int32 Row = 0; Row < RowCount; ++Row)
	{
		const float Y = RowsTop + Row * (RowHeight + RowGap);
		const FBox2D RowBox(
			FVector2D(Panel.Min.X + 48.0f * Scale, Y),
			FVector2D(Panel.Max.X - 48.0f * Scale, Y + RowHeight));
		const TCHAR* Label = TEXT("");
		if (SettingsSection == EABTSSettingsSection::Audio)
		{
			const TCHAR* Labels[] = { TEXT("MASTER VOLUME"), TEXT("MUSIC"), TEXT("SOUND EFFECTS"), TEXT("USER INTERFACE"), TEXT("AMBIENCE") };
			Label = Labels[Row];
		}
		else if (SettingsSection == EABTSSettingsSection::Video)
		{
			const TCHAR* Labels[] = { TEXT("QUALITY PRESET"), TEXT("RESOLUTION"), TEXT("DISPLAY MODE"), TEXT("VERTICAL SYNC"), TEXT("FRAME RATE LIMIT"), TEXT("DYNAMIC RESOLUTION") };
			Label = Labels[Row];
		}
		else
		{
			const TCHAR* Labels[] = { TEXT("MENU SCALE"), TEXT("DISPLAY GAMMA"), TEXT("SUBTITLES"), TEXT("MUTE WHEN UNFOCUSED"), TEXT("REDUCE MOTION"), TEXT("HIGH CONTRAST MENU") };
			Label = Labels[Row];
		}
		DrawSettingsRow(Canvas, RowBox, Row, Label, GetSettingsValue(Row), GetSettingsNormalizedValue(Row));
	}

	const float FooterY = Panel.Max.Y - 82.0f * Scale;
	const float FooterW = 360.0f * Scale;
	DrawButton(Canvas, FBox2D(FVector2D(Panel.Min.X + 48.0f * Scale, FooterY), FVector2D(Panel.Min.X + 48.0f * Scale + FooterW, FooterY + 52.0f * Scale)), TEXT("RESET DEFAULTS"), EHitAction::ResetDefaults, RowCount);
	DrawButton(Canvas, FBox2D(FVector2D(Panel.Max.X - 48.0f * Scale - FooterW, FooterY), FVector2D(Panel.Max.X - 48.0f * Scale, FooterY + 52.0f * Scale)), TEXT("BACK"), EHitAction::Back, RowCount + 1);
	DrawLabel(Canvas, TEXT("Q / E  SWITCH TAB     LEFT / RIGHT  ADJUST"), FVector2D(Panel.GetCenter().X, Panel.Max.Y - 24.0f * Scale), 0.58f * Scale, Theme.TextMuted, false, true);
}

void UABTSGameViewportClient::DrawDialog(UCanvas& Canvas, const FVector2D& ViewSize)
{
	const FABTSUIThemeSnapshot Theme = FABTSUITheme::Get();
	// Remove underlying page hit targets while a confirmation owns input.
	HitTargets.Reset();
	FLinearColor Scrim = Theme.SlotBorder;
	Scrim.A = 0.72f;
	Canvas.K2_DrawTexture(Canvas.DefaultTexture, FVector2D::ZeroVector, ViewSize, FVector2D::ZeroVector, FVector2D::UnitVector, Scrim, BLEND_Translucent);
	const FVector2D DialogSize(FMath::Min(680.0f, ViewSize.X - 96.0f), FMath::Min(310.0f, ViewSize.Y - 96.0f));
	const FBox2D Dialog(ViewSize * 0.5f - DialogSize * 0.5f, ViewSize * 0.5f + DialogSize * 0.5f);
	FABTSCanvasUI::DrawFacetedBox(Canvas, Theme, Dialog, Theme.PanelPrimary, Theme.AccentPrimary, 22.0f, 4.0f);
	FABTSCanvasUI::DrawCornerBrackets(Canvas, Theme, Dialog, Theme.AccentSecondary, 32.0f, 10.0f, 3.0f);
	const bool bVideo = ActiveDialog == EABTSSystemMenuDialog::ConfirmVideo;
	DrawLabel(Canvas, bVideo ? TEXT("KEEP THESE DISPLAY SETTINGS?") : TEXT("RESET ALL SETTINGS?"), Dialog.GetCenter() - FVector2D(0.0f, 88.0f), 0.92f, Theme.TextPrimary, true, true);
	if (bVideo)
	{
		// Map startup can legitimately exceed the interactive timeout. Capture mode freezes the
		// reference state so visual evidence is deterministic instead of showing a stale "0".
		const int32 Seconds = bCaptureMode
			? static_cast<int32>(VideoConfirmationDurationSeconds)
			: ComputeConfirmationSecondsRemaining(VideoConfirmationDeadlineSeconds, FPlatformTime::Seconds());
		DrawLabel(Canvas, FString::Printf(TEXT("REVERTING AUTOMATICALLY IN %d SECONDS"), Seconds), Dialog.GetCenter() - FVector2D(0.0f, 38.0f), 0.68f, Theme.Warning, false, true);
	}
	else
	{
		DrawLabel(Canvas, TEXT("AUDIO, VIDEO AND ACCESSIBILITY VALUES WILL RETURN TO DEFAULTS"), Dialog.GetCenter() - FVector2D(0.0f, 38.0f), 0.58f, Theme.TextMuted, false, true);
	}
	const float ButtonY = Dialog.Max.Y - 92.0f;
	const float ButtonWidth = 240.0f;
	const float ButtonHeight = 56.0f;
	const FBox2D Primary(FVector2D(Dialog.Min.X + 58.0f, ButtonY), FVector2D(Dialog.Min.X + 58.0f + ButtonWidth, ButtonY + ButtonHeight));
	const FBox2D Secondary(FVector2D(Dialog.Max.X - 58.0f - ButtonWidth, ButtonY), FVector2D(Dialog.Max.X - 58.0f, ButtonY + ButtonHeight));
	DrawButton(Canvas, Primary, bVideo ? TEXT("KEEP") : TEXT("RESET"), bVideo ? EHitAction::KeepVideo : EHitAction::ConfirmReset, SelectedDialogAction == 0 ? SelectedIndex : INDEX_NONE);
	DrawButton(Canvas, Secondary, bVideo ? TEXT("REVERT") : TEXT("CANCEL"), bVideo ? EHitAction::RevertVideo : EHitAction::CancelDialog, SelectedDialogAction == 1 ? SelectedIndex : INDEX_NONE);
}

void UABTSGameViewportClient::DrawButton(
	UCanvas& Canvas,
	const FBox2D& Box,
	const FString& Label,
	const EHitAction Action,
	const int32 NavigationIndex,
	const int32 Row,
	const int32 Delta,
	const bool bEnabled)
{
	const FABTSUIThemeSnapshot Theme = FABTSUITheme::Get();
	const bool bSelected = NavigationIndex == SelectedIndex;
	const FLinearColor Fill = !bEnabled ? Theme.Disabled : (bSelected ? Theme.SlotSelected : Theme.SlotNormal);
	const FLinearColor Border = bSelected ? Theme.AccentPrimary : Theme.PanelBorder;
	FABTSCanvasUI::DrawFacetedBox(Canvas, Theme, Box, Fill, Border, FMath::Min(12.0f, Box.GetSize().Y * 0.18f), bSelected ? 3.0f : 2.0f);
	DrawLabel(Canvas, Label, Box.GetCenter(), 0.76f, bEnabled ? Theme.TextPrimary : Theme.TextMuted, false, true);
	if (bEnabled) AddHitTarget(Box, Action, Row, Delta);
}

void UABTSGameViewportClient::DrawSettingsRow(
	UCanvas& Canvas,
	const FBox2D& Box,
	const int32 Row,
	const FString& Label,
	const FString& Value,
	const float NormalizedValue)
{
	const FABTSUIThemeSnapshot Theme = FABTSUITheme::Get();
	const bool bSelected = SelectedIndex == Row;
	FABTSCanvasUI::DrawFacetedBox(Canvas, Theme, Box, bSelected ? Theme.SlotHeld : Theme.PanelSecondary, bSelected ? Theme.AccentPrimary : Theme.PanelBorder, 10.0f, bSelected ? 3.0f : 2.0f);
	DrawLabel(Canvas, Label, Box.Min + FVector2D(24.0f, Box.GetSize().Y * 0.35f), 0.68f, Theme.TextMuted);
	const float ArrowSize = FMath::Min(48.0f, Box.GetSize().Y - 14.0f);
	const FBox2D MinusBox(FVector2D(Box.Max.X - 244.0f, Box.GetCenter().Y - ArrowSize * 0.5f), FVector2D(Box.Max.X - 244.0f + ArrowSize, Box.GetCenter().Y + ArrowSize * 0.5f));
	const FBox2D PlusBox(FVector2D(Box.Max.X - ArrowSize - 12.0f, Box.GetCenter().Y - ArrowSize * 0.5f), FVector2D(Box.Max.X - 12.0f, Box.GetCenter().Y + ArrowSize * 0.5f));
	FABTSCanvasUI::DrawFacetedBox(Canvas, Theme, MinusBox, Theme.SlotNormal, Theme.PanelBorder, 7.0f, 2.0f);
	FABTSCanvasUI::DrawFacetedBox(Canvas, Theme, PlusBox, Theme.SlotNormal, Theme.PanelBorder, 7.0f, 2.0f);
	DrawLabel(Canvas, TEXT("<"), MinusBox.GetCenter(), 0.86f, Theme.AccentSecondary, false, true);
	DrawLabel(Canvas, TEXT(">"), PlusBox.GetCenter(), 0.86f, Theme.AccentSecondary, false, true);
	DrawLabel(Canvas, Value, FVector2D((MinusBox.Max.X + PlusBox.Min.X) * 0.5f, Box.GetCenter().Y), 0.72f, Theme.TextPrimary, false, true);
	AddHitTarget(MinusBox, EHitAction::AdjustSetting, Row, -1);
	AddHitTarget(PlusBox, EHitAction::AdjustSetting, Row, 1);
	AddHitTarget(FBox2D(Box.Min, FVector2D(MinusBox.Min.X - 8.0f, Box.Max.Y)), EHitAction::AdjustSetting, Row, 1);
	if (NormalizedValue >= 0.0f)
	{
		const float BarWidth = FMath::Min(280.0f, Box.GetSize().X * 0.24f);
		const FBox2D Track(FVector2D(Box.Min.X + 24.0f, Box.Max.Y - 16.0f), FVector2D(Box.Min.X + 24.0f + BarWidth, Box.Max.Y - 9.0f));
		Canvas.K2_DrawTexture(Canvas.DefaultTexture, Track.Min, Track.GetSize(), FVector2D::ZeroVector, FVector2D::UnitVector, Theme.SlotBorder, BLEND_Translucent);
		const FBox2D Fill(Track.Min, FVector2D(FMath::Lerp(Track.Min.X, Track.Max.X, FMath::Clamp(NormalizedValue, 0.0f, 1.0f)), Track.Max.Y));
		Canvas.K2_DrawTexture(Canvas.DefaultTexture, Fill.Min, Fill.GetSize(), FVector2D::ZeroVector, FVector2D::UnitVector, Theme.AccentSecondary, BLEND_Translucent);
	}
}

void UABTSGameViewportClient::DrawLabel(
	UCanvas& Canvas,
	const FString& Text,
	const FVector2D& Position,
	const float Scale,
	const FLinearColor& Color,
	const bool bLarge,
	const bool bCentered)
{
	UFont* Font = bLarge && GEngine ? GEngine->GetLargeFont() : (GEngine ? GEngine->GetSmallFont() : nullptr);
	if (!Font) return;
	const float ReadableScale = Scale * 1.8f;
	Canvas.K2_DrawText(Font, Text, Position, FVector2D(ReadableScale), Color, 1.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.8f), FVector2D(1.0f, 1.0f), bCentered, bCentered, true, FLinearColor::Black);
}

void UABTSGameViewportClient::AddHitTarget(const FBox2D& Box, const EHitAction Action, const int32 Row, const int32 Delta)
{
	FHitTarget& Target = HitTargets.AddDefaulted_GetRef();
	Target.Box = Box;
	Target.Action = Action;
	Target.Row = Row;
	Target.Delta = Delta;
}

bool UABTSGameViewportClient::HandlePointerClick(FViewport* InViewport)
{
	if (!InViewport) return true;
	FIntPoint Mouse;
	InViewport->GetMousePos(Mouse);
	const FVector2D Point(Mouse);
	for (const FHitTarget& Target : HitTargets)
	{
		if (Target.Box.IsInside(Point))
		{
			HandleAction(Target.Action, Target.Row, Target.Delta);
			return true;
		}
	}
	PlayUIFeedback(false);
	return true;
}

void UABTSGameViewportClient::HandleAction(const EHitAction Action, const int32 Row, const int32 Delta)
{
	switch (Action)
	{
	case EHitAction::Begin:
	{
		CloseSystemMenu();
		if (bMenuVisible) break;
		if (!bOpeningCinematicAttempted)
		{
			bOpeningCinematicAttempted = true;
			const EABTSOpeningStartResult StartResult =
				AABTSOpeningCinematicPreview::TryStartProductionOpening(GetWorld());
			const bool bStarted = StartResult == EABTSOpeningStartResult::Started;
			const bool bDebugSkip = StartResult == EABTSOpeningStartResult::DebugSkipped;
			bOpeningCinematicStarted = bStarted;
			UE_LOG(LogABTSRuntime, Log,
				TEXT("[ABTS][StartupFlow] OpeningCinematicAttempted=1 Started=%d DebugSkipped=%d"),
				bStarted ? 1 : 0, bDebugSkip ? 1 : 0);
			if (bStarted || bDebugSkip)
			{
				// The front-end gate has completed its one startup handoff. It must no
				// longer re-arm merely because the cinematic now owns the viewport.
				bStartupFrontEndRequired = false;
				bStartupPresentationReady = true;
				bStartupWorldReady = bStartupAuthorityReady && !bStartupWorldFailed;
			}
			else
			{
				// A release binding rejection must not silently reveal an interactive
				// world without its required opening handoff.
				bOpeningCinematicAttempted = false;
				bOpeningCinematicStarted = false;
				OpenFrontEnd();
			}
		}
		break;
	}
	case EHitAction::Resume: CloseSystemMenu(); break;
	case EHitAction::Settings: OpenSettingsMenu(); break;
	case EHitAction::ReturnToTitle: OpenFrontEnd(); break;
	case EHitAction::Quit: QuitGame(); break;
	case EHitAction::Back:
		MenuPage = SettingsReturnPage;
		SelectedIndex = 0;
		PlayUIFeedback(false);
		break;
	case EHitAction::ResetDefaults: ShowResetConfirmation(); break;
	case EHitAction::TabAudio: SettingsSection = EABTSSettingsSection::Audio; SelectedIndex = 0; PlayUIFeedback(false); break;
	case EHitAction::TabVideo: SettingsSection = EABTSSettingsSection::Video; SelectedIndex = 0; PlayUIFeedback(false); break;
	case EHitAction::TabAccessibility: SettingsSection = EABTSSettingsSection::Accessibility; SelectedIndex = 0; PlayUIFeedback(false); break;
	case EHitAction::AdjustSetting: SelectedIndex = Row; AdjustSetting(Row, Delta); break;
	case EHitAction::KeepVideo: KeepVideoSettings(); break;
	case EHitAction::RevertVideo: RevertVideoSettings(TEXT("User")); break;
	case EHitAction::ConfirmReset: ResetSettingsToDefaults(); break;
	case EHitAction::CancelDialog: CancelDialog(); break;
	}
}

void UABTSGameViewportClient::Navigate(const int32 Delta)
{
	const int32 Count = MenuPage == EABTSSystemMenuPage::Settings
		? GetSettingsRowCount() + 2
		: (MenuPage == EABTSSystemMenuPage::Front ? 2 : 3) + (CanQuitCurrentWorld() ? 1 : 0);
	SelectedIndex = (SelectedIndex + Delta + Count) % Count;
	PlayUIFeedback(false);
}

void UABTSGameViewportClient::ActivateSelection()
{
	if (MenuPage == EABTSSystemMenuPage::Settings)
	{
		const int32 Rows = GetSettingsRowCount();
		if (SelectedIndex < Rows) AdjustSetting(SelectedIndex, 1);
		else if (SelectedIndex == Rows) ShowResetConfirmation();
		else HandleAction(EHitAction::Back);
		return;
	}
	TArray<EHitAction> Actions;
	if (MenuPage == EABTSSystemMenuPage::Front)
	{
		Actions = { EHitAction::Begin, EHitAction::Settings };
	}
	else
	{
		Actions = { EHitAction::Resume, EHitAction::Settings, EHitAction::ReturnToTitle };
	}
	if (CanQuitCurrentWorld()) Actions.Add(EHitAction::Quit);
	if (Actions.IsValidIndex(SelectedIndex)) HandleAction(Actions[SelectedIndex]);
}

void UABTSGameViewportClient::CycleSettingsSection(const int32 Delta)
{
	if (MenuPage != EABTSSystemMenuPage::Settings) return;
	const int32 Current = static_cast<int32>(SettingsSection);
	SettingsSection = static_cast<EABTSSettingsSection>((Current + Delta + 3) % 3);
	SelectedIndex = 0;
	PlayUIFeedback(false);
}

void UABTSGameViewportClient::AdjustSetting(const int32 Row, const int32 Delta)
{
	UABTSGameUserSettings* Settings = UABTSGameUserSettings::Get();
	if (!Settings || Delta == 0 || Row < 0 || Row >= GetSettingsRowCount()) return;
	bool bApplyResolution = false;
	if (SettingsSection == EABTSSettingsSection::Audio)
	{
		switch (Row)
		{
		case 0: Settings->SetMasterVolume(Settings->GetMasterVolume() + Delta * SettingStep); break;
		case 1: Settings->SetMusicVolume(Settings->GetMusicVolume() + Delta * SettingStep); break;
		case 2: Settings->SetSFXVolume(Settings->GetSFXVolume() + Delta * SettingStep); break;
		case 3: Settings->SetUIVolume(Settings->GetUIVolume() + Delta * SettingStep); break;
		case 4: Settings->SetAmbienceVolume(Settings->GetAmbienceVolume() + Delta * SettingStep); break;
		default: break;
		}
	}
	else if (SettingsSection == EABTSSettingsSection::Video)
	{
		switch (Row)
		{
		case 0:
		{
			const int32 Current = Settings->GetOverallScalabilityLevel();
			Settings->SetOverallScalabilityLevel(FMath::Clamp((Current < 0 ? 3 : Current) + Delta, 0, 4));
			break;
		}
		case 1:
			if (!ResolutionOptions.IsEmpty())
			{
				CurrentResolutionIndex = (CurrentResolutionIndex + Delta + ResolutionOptions.Num()) % ResolutionOptions.Num();
				Settings->SetScreenResolution(ResolutionOptions[CurrentResolutionIndex]);
				bApplyResolution = true;
			}
			break;
		case 2:
		{
			const int32 Current = IndexFromMode(Settings->GetFullscreenMode());
			Settings->SetFullscreenMode(ModeFromIndex((Current + Delta + 3) % 3));
			bApplyResolution = true;
			break;
		}
		case 3: Settings->SetVSyncEnabled(!Settings->IsVSyncEnabled()); break;
		case 4:
		{
			int32 Index = 0;
			float BestDistance = TNumericLimits<float>::Max();
			for (int32 Candidate = 0; Candidate < UE_ARRAY_COUNT(FrameRateLimits); ++Candidate)
			{
				const float Distance = FMath::Abs(FrameRateLimits[Candidate] - Settings->GetFrameRateLimit());
				if (Distance < BestDistance) { BestDistance = Distance; Index = Candidate; }
			}
			Index = (Index + Delta + UE_ARRAY_COUNT(FrameRateLimits)) % UE_ARRAY_COUNT(FrameRateLimits);
			Settings->SetFrameRateLimit(FrameRateLimits[Index]);
			break;
		}
		case 5: Settings->SetDynamicResolutionEnabled(!Settings->IsDynamicResolutionEnabled()); break;
		default: break;
		}
	}
	else
	{
		switch (Row)
		{
		case 0: Settings->SetMenuScale(Settings->GetMenuScale() + Delta * SettingStep); break;
		case 1: Settings->SetDisplayGamma(Settings->GetABTSDisplayGamma() + Delta * 0.1f); break;
		case 2: Settings->SetSubtitlesEnabled(!Settings->GetSubtitlesEnabled()); break;
		case 3: Settings->SetMuteWhenUnfocused(!Settings->GetMuteWhenUnfocused()); break;
		case 4: Settings->SetReduceMotion(!Settings->GetReduceMotion()); break;
		case 5: Settings->SetHighContrastMenu(!Settings->GetHighContrastMenu()); break;
		default: break;
		}
	}
	Settings->ApplyAndSave(GetWorld(), bApplyResolution);
	if (bApplyResolution) BeginVideoConfirmation();
	PlayUIFeedback(false);
}

void UABTSGameViewportClient::ShowResetConfirmation()
{
	ActiveDialog = EABTSSystemMenuDialog::ConfirmReset;
	SelectedDialogAction = 1;
	HitTargets.Reset();
	PlayUIFeedback(false);
}

void UABTSGameViewportClient::BeginVideoConfirmation()
{
	ActiveDialog = EABTSSystemMenuDialog::ConfirmVideo;
	SelectedDialogAction = 0;
	VideoConfirmationDeadlineSeconds = FPlatformTime::Seconds() + VideoConfirmationDurationSeconds;
	HitTargets.Reset();
	UE_LOG(LogABTSRuntime, Display, TEXT("[ABTS][Settings] VideoConfirmation Started Seconds=%.0f"), VideoConfirmationDurationSeconds);
}

void UABTSGameViewportClient::KeepVideoSettings()
{
	if (ActiveDialog != EABTSSystemMenuDialog::ConfirmVideo) return;
	if (UABTSGameUserSettings* Settings = UABTSGameUserSettings::Get())
	{
		Settings->ConfirmVideoMode();
		Settings->SaveSettings();
		UE_LOG(LogABTSRuntime, Display, TEXT("[ABTS][Settings] VideoConfirmation Kept %s"), *Settings->BuildDiagnosticSummary());
	}
	ActiveDialog = EABTSSystemMenuDialog::None;
	PlayUIFeedback(true);
}

void UABTSGameViewportClient::RevertVideoSettings(const TCHAR* Reason)
{
	if (ActiveDialog != EABTSSystemMenuDialog::ConfirmVideo) return;
	if (UABTSGameUserSettings* Settings = UABTSGameUserSettings::Get())
	{
		Settings->RevertVideoMode();
		Settings->ApplyResolutionSettings(false);
		Settings->SaveSettings();
		RebuildResolutionOptions();
		UE_LOG(LogABTSRuntime, Display, TEXT("[ABTS][Settings] VideoConfirmation Reverted Reason=%s %s"), Reason, *Settings->BuildDiagnosticSummary());
	}
	ActiveDialog = EABTSSystemMenuDialog::None;
	PlayUIFeedback(false);
}

void UABTSGameViewportClient::CancelDialog()
{
	ActiveDialog = EABTSSystemMenuDialog::None;
	PlayUIFeedback(false);
}

int32 UABTSGameViewportClient::GetSettingsRowCount() const
{
	if (SettingsSection == EABTSSettingsSection::Audio) return AudioRowCount;
	if (SettingsSection == EABTSSettingsSection::Video) return VideoRowCount;
	return AccessibilityRowCount;
}

FString UABTSGameViewportClient::GetSettingsValue(const int32 Row) const
{
	const UABTSGameUserSettings* Settings = UABTSGameUserSettings::Get();
	if (!Settings) return TEXT("UNAVAILABLE");
	if (SettingsSection == EABTSSettingsSection::Audio)
	{
		const float Values[] = { Settings->GetMasterVolume(), Settings->GetMusicVolume(), Settings->GetSFXVolume(), Settings->GetUIVolume(), Settings->GetAmbienceVolume() };
		return FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Values[FMath::Clamp(Row, 0, AudioRowCount - 1)] * 100.0f));
	}
	if (SettingsSection == EABTSSettingsSection::Video)
	{
		switch (Row)
		{
		case 0:
		{
			const int32 Quality = Settings->GetOverallScalabilityLevel();
			return Quality >= 0 && Quality < UE_ARRAY_COUNT(QualityLabels) ? QualityLabels[Quality] : TEXT("CUSTOM");
		}
		case 1:
		{
			const FIntPoint Resolution = Settings->GetScreenResolution();
			return FString::Printf(TEXT("%d x %d"), Resolution.X, Resolution.Y);
		}
		case 2: return ModeLabels[IndexFromMode(Settings->GetFullscreenMode())];
		case 3: return Settings->IsVSyncEnabled() ? TEXT("ON") : TEXT("OFF");
		case 4: return FormatFrameRateLimit(Settings->GetFrameRateLimit());
		case 5: return Settings->IsDynamicResolutionEnabled() ? TEXT("ON") : TEXT("OFF");
		default: return FString();
		}
	}
	switch (Row)
	{
	case 0: return FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Settings->GetMenuScale() * 100.0f));
	case 1: return FString::Printf(TEXT("%.1f"), Settings->GetABTSDisplayGamma());
	case 2: return Settings->GetSubtitlesEnabled() ? TEXT("ON") : TEXT("OFF");
	case 3: return Settings->GetMuteWhenUnfocused() ? TEXT("ON") : TEXT("OFF");
	case 4: return Settings->GetReduceMotion() ? TEXT("ON") : TEXT("OFF");
	case 5: return Settings->GetHighContrastMenu() ? TEXT("ON") : TEXT("OFF");
	default: return FString();
	}
}

float UABTSGameViewportClient::GetSettingsNormalizedValue(const int32 Row) const
{
	const UABTSGameUserSettings* Settings = UABTSGameUserSettings::Get();
	if (!Settings) return -1.0f;
	if (SettingsSection == EABTSSettingsSection::Audio)
	{
		const float Values[] = { Settings->GetMasterVolume(), Settings->GetMusicVolume(), Settings->GetSFXVolume(), Settings->GetUIVolume(), Settings->GetAmbienceVolume() };
		return Values[FMath::Clamp(Row, 0, AudioRowCount - 1)];
	}
	if (SettingsSection == EABTSSettingsSection::Accessibility)
	{
		if (Row == 0) return (Settings->GetMenuScale() - 0.80f) / 0.45f;
		if (Row == 1) return (Settings->GetABTSDisplayGamma() - 1.8f) / 0.8f;
	}
	return -1.0f;
}

bool UABTSGameViewportClient::CanQuitCurrentWorld() const
{
	const UWorld* GameWorld = GetWorld();
	return GameWorld && GameWorld->WorldType != EWorldType::PIE;
}

void UABTSGameViewportClient::QuitGame()
{
	if (!CanQuitCurrentWorld()) return;
	PlayUIFeedback(true);
	UKismetSystemLibrary::QuitGame(GetWorld(), MenuPlayerController.Get(), EQuitPreference::Quit, false);
}

void UABTSGameViewportClient::PlayUIFeedback(const bool bConfirm) const
{
	if (UWorld* GameWorld = GetWorld())
	{
		if (UABTSAudioWorldSubsystem* Audio = GameWorld->GetSubsystem<UABTSAudioWorldSubsystem>())
		{
			Audio->PlayUIEvent(bConfirm ? EABTSUIAudioEvent::Confirm : EABTSUIAudioEvent::Tick);
		}
	}
}

void UABTSGameViewportClient::ResetSettingsToDefaults()
{
	if (UABTSGameUserSettings* Settings = UABTSGameUserSettings::Get())
	{
		Settings->SetToDefaults();
		Settings->ApplyAndSave(GetWorld(), true, 0.12f);
		RebuildResolutionOptions();
		SelectedIndex = 0;
		ActiveDialog = EABTSSystemMenuDialog::None;
		PlayUIFeedback(true);
	}
}

void UABTSGameViewportClient::UpdateOpeningProductionCapture()
{
	if (!bCaptureMode || !bOpeningProductionCapture || bScreenshotRequested) return;

	if (!bOpeningProductionStarted)
	{
		if (!bMenuVisible
			|| MenuPage != EABTSSystemMenuPage::Front
			|| IsStartupInputBlocked())
		{
			return;
		}

		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][OpeningProductionCapture] BeginRequested Ready=1"));
		HandleAction(EHitAction::Begin);
		if (!bOpeningCinematicStarted)
		{
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][OpeningProductionCapture] Complete Success=0 Reason=OpeningDidNotStart DebugSkipped=%d"),
				bOpeningCinematicAttempted ? 1 : 0);
			bScreenshotRequested = true;
			FGenericPlatformMisc::RequestExitWithStatus(false, 1);
			return;
		}

		bOpeningProductionStarted = true;
		OpeningProductionCaptureStartSeconds = FPlatformTime::Seconds();
		CaptureFrameCount = 0;
		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][OpeningProductionCapture] Started=1 DelaySeconds=%.2f"),
			OpeningProductionCaptureDelaySeconds);
		return;
	}

	if (FPlatformTime::Seconds() - OpeningProductionCaptureStartSeconds
		< OpeningProductionCaptureDelaySeconds)
	{
		return;
	}

	CaptureFrameCount = FMath::Max(CaptureFrameCount, 60);
	MaybeRequestCapture();
}

void UABTSGameViewportClient::MaybeRequestCapture()
{
	if (!bCaptureMode || bScreenshotRequested || CaptureFrameCount < 60 || !GetWorld() || !GetWorld()->HasBegunPlay()) return;
	if (!bOpeningProductionStarted
		&& MenuPage == EABTSSystemMenuPage::Front
		&& IsStartupInputBlocked()) return;
	bScreenshotRequested = true;
	ScreenshotDelegateHandle = FScreenshotRequest::OnScreenshotRequestProcessed().AddUObject(this, &UABTSGameViewportClient::HandleScreenshotProcessed);
	FScreenshotRequest::RequestScreenshot(CaptureOutputPath, true, false, false, FIntRect(), true);
	UE_LOG(LogABTSRuntime, Display, TEXT("[ABTS][SystemMenuCapture] Requested Frame=%d Output=%s"), CaptureFrameCount, *CaptureOutputPath);
}

void UABTSGameViewportClient::HandleScreenshotProcessed()
{
	FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotDelegateHandle);
	const bool bExists = IFileManager::Get().FileSize(*CaptureOutputPath) > 0;
	if (bExists)
	{
		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][SystemMenuCapture] Complete Success=1 Reason=None Output=%s"),
			*CaptureOutputPath);
	}
	else
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][SystemMenuCapture] Complete Success=0 Reason=ScreenshotMissing Output=%s"),
			*CaptureOutputPath);
	}
	FGenericPlatformMisc::RequestExitWithStatus(false, bExists ? 0 : 1);
}
