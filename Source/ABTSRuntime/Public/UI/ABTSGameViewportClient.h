// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameViewportClient.h"
#include "ABTSGameViewportClient.generated.h"

class FCanvas;
class UCanvas;
class UABTSGameUserSettings;

enum class EABTSSystemMenuPage : uint8
{
	Front,
	Pause,
	Settings
};

enum class EABTSSettingsSection : uint8
{
	Audio,
	Video,
	Accessibility
};

enum class EABTSSystemMenuDialog : uint8
{
	None,
	ConfirmVideo,
	ConfirmReset
};

/** Global asset-free front end and pause/settings overlay for every ABTS map. */
UCLASS(Transient)
class ABTSRUNTIME_API UABTSGameViewportClient final : public UGameViewportClient
{
	GENERATED_BODY()

public:
	virtual void Init(FWorldContext& WorldContext, UGameInstance* OwningGameInstance, bool bCreateNewAudioDevice = true) override;
	virtual void Tick(float DeltaTime) override;
	virtual void Draw(FViewport* InViewport, FCanvas* SceneCanvas) override;
	virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override;
	virtual bool InputAxis(const FInputKeyEventArgs& EventArgs) override;
	virtual EMouseCursor::Type GetCursor(FViewport* InViewport, int32 X, int32 Y) override;

	void OpenFrontEnd();
	void OpenPauseMenu();
	void OpenSettingsMenu(EABTSSettingsSection Section = EABTSSettingsSection::Audio);
	void CloseSystemMenu();
	void ResetSettingsToDefaults();
	bool IsSystemMenuVisible() const { return bMenuVisible; }
	EABTSSystemMenuPage GetSystemMenuPage() const { return MenuPage; }
	EABTSSettingsSection GetSettingsSection() const { return SettingsSection; }
	EABTSSystemMenuDialog GetDialog() const { return ActiveDialog; }

	static TArray<FIntPoint> BuildFallbackResolutionOptions(FIntPoint DesktopResolution);
	static FString FormatFrameRateLimit(float Limit);
	static int32 ComputeConfirmationSecondsRemaining(double DeadlineSeconds, double NowSeconds);
	/** Monotonic foreground progress: cap below completion until the authoritative world gate opens. */
	static float ComputeStartupLoadingProgress(double ElapsedSeconds, bool bReady);
	/** Two-phase latch: world authority and two complete front-end draws must both be stable. */
	static bool IsStartupPresentationReady(
		bool bWorldAuthorityReady,
		bool bPresentationSurfaceReady,
		int32 CompletedFrontEndDraws);

private:
	enum class EHitAction : uint8
	{
		Begin,
		Resume,
		Settings,
		ReturnToTitle,
		Quit,
		Back,
		ResetDefaults,
		TabAudio,
		TabVideo,
		TabAccessibility,
		AdjustSetting,
		KeepVideo,
		RevertVideo,
		ConfirmReset,
		CancelDialog
	};

	struct FHitTarget
	{
		FBox2D Box;
		EHitAction Action = EHitAction::Begin;
		int32 Row = INDEX_NONE;
		int32 Delta = 0;
	};

	void EnsureInitialMenuState();
	void RefreshStartupWorldState();
	bool IsStartupInputBlocked() const;
	void SetMenuVisible(bool bVisible, EABTSSystemMenuPage NewPage);
	void ApplyMenuInputMode();
	void RestoreGameplayInputMode();
	void RebuildResolutionOptions();
	void DrawMenu(UCanvas& Canvas, const FVector2D& ViewSize);
	void DrawStartupHandoffCover(UCanvas& Canvas, const FVector2D& ViewSize);
	void DrawBackdrop(UCanvas& Canvas, const FVector2D& ViewSize);
	void DrawFrontOrPause(UCanvas& Canvas, const FVector2D& ViewSize);
	void DrawSettings(UCanvas& Canvas, const FVector2D& ViewSize);
	void DrawDialog(UCanvas& Canvas, const FVector2D& ViewSize);
	void DrawButton(UCanvas& Canvas, const FBox2D& Box, const FString& Label, EHitAction Action, int32 NavigationIndex, int32 Row = INDEX_NONE, int32 Delta = 0, bool bEnabled = true);
	void DrawSettingsRow(UCanvas& Canvas, const FBox2D& Box, int32 Row, const FString& Label, const FString& Value, float NormalizedValue = -1.0f);
	void DrawLabel(UCanvas& Canvas, const FString& Text, const FVector2D& Position, float Scale, const FLinearColor& Color, bool bLarge = false, bool bCentered = false);
	void AddHitTarget(const FBox2D& Box, EHitAction Action, int32 Row = INDEX_NONE, int32 Delta = 0);
	bool HandlePointerClick(FViewport* InViewport);
	void HandleAction(EHitAction Action, int32 Row = INDEX_NONE, int32 Delta = 0);
	void Navigate(int32 Delta);
	void ActivateSelection();
	void CycleSettingsSection(int32 Delta);
	void AdjustSetting(int32 Row, int32 Delta);
	void ShowResetConfirmation();
	void BeginVideoConfirmation();
	void KeepVideoSettings();
	void RevertVideoSettings(const TCHAR* Reason);
	void CancelDialog();
	int32 GetSettingsRowCount() const;
	FString GetSettingsValue(int32 Row) const;
	float GetSettingsNormalizedValue(int32 Row) const;
	bool CanQuitCurrentWorld() const;
	void QuitGame();
	void PlayUIFeedback(bool bConfirm) const;
	void MaybeRequestCapture();
	void HandleScreenshotProcessed();

	UPROPERTY(Transient)
	TObjectPtr<UCanvas> MenuCanvas;

	TArray<FHitTarget> HitTargets;
	TArray<FIntPoint> ResolutionOptions;
	EABTSSystemMenuPage MenuPage = EABTSSystemMenuPage::Front;
	EABTSSystemMenuPage SettingsReturnPage = EABTSSystemMenuPage::Front;
	EABTSSettingsSection SettingsSection = EABTSSettingsSection::Audio;
	EABTSSystemMenuDialog ActiveDialog = EABTSSystemMenuDialog::None;
	int32 SelectedIndex = 0;
	int32 SelectedDialogAction = 0;
	int32 CurrentResolutionIndex = 0;
	bool bMenuVisible = false;
	bool bInitialMenuStateResolved = false;
	bool bPreviousMouseCursor = false;
	bool bWorldWasPaused = false;
	bool bInputStateCaptured = false;
	bool bCaptureMode = false;
	bool bScreenshotRequested = false;
	bool bStartupGateRequired = false;
	bool bStartupAuthorityReady = false;
	bool bStartupWorldReady = false;
	bool bStartupWorldFailed = false;
	bool bStartupFrontEndRequired = false;
	bool bStartupPresentationReady = false;
	bool bStartupGateStartedLogged = false;
	bool bStartupGateTerminalLogged = false;
	bool bOpeningCinematicAttempted = false;
	int32 StartupReadyPresentationFrameCount = 0;
	int32 CaptureFrameCount = 0;
	double CaptureStartSeconds = 0.0;
	double StartupForegroundStartSeconds = 0.0;
	double VideoConfirmationDeadlineSeconds = 0.0;
	FString CaptureOutputPath;
	FDelegateHandle ScreenshotDelegateHandle;
	TWeakObjectPtr<APlayerController> MenuPlayerController;
	TWeakObjectPtr<UWorld> StartupTrackedWorld;
};
