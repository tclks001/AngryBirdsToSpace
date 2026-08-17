// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "CoreGlobals.h"
#include "Modules/ModuleManager.h"
#include "MoviePlayer.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Styling/CoreStyle.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogABTSLoadingScreen, Log, All);

namespace ABTSLoadingScreen
{
	enum class EPhase : int32
	{
		Boot = 0,
		LoadingMap = 1,
		MapLoaded = 2,
	};

	float ComputeLoadingProgress(const double ElapsedSeconds, const EPhase Phase)
	{
		const float Floor = Phase == EPhase::Boot ? 0.03f : 0.12f;
		const float TimedProgress = static_cast<float>(
			FMath::Max(0.0, ElapsedSeconds) / 30.0);
		return FMath::Clamp(FMath::Max(Floor, TimedProgress), Floor, 0.92f);
	}

	struct FState final
	{
		FState()
			: StartSeconds(GStartTime)
		{
		}

		float GetProgress() const
		{
			const EPhase CurrentPhase = static_cast<EPhase>(Phase.Load());
			return ComputeLoadingProgress(
				FPlatformTime::Seconds() - StartSeconds,
				CurrentPhase);
		}

		FText GetStatusText() const
		{
			switch (static_cast<EPhase>(Phase.Load()))
			{
			case EPhase::LoadingMap:
				return NSLOCTEXT("ABTSLoadingScreen", "LoadingMap", "GENERATING PLANETARY WORLD");
			case EPhase::MapLoaded:
				// PostLoadMap is only the handoff to the ticking world-generation phase.
				// The runtime authority gate is the sole owner of WORLD READY.
				return NSLOCTEXT("ABTSLoadingScreen", "MapLoaded", "GENERATING PLANETARY WORLD");
			default:
				return NSLOCTEXT("ABTSLoadingScreen", "Boot", "INITIALIZING FLIGHT SYSTEMS");
			}
		}

		TAtomic<int32> Phase { static_cast<int32>(EPhase::Boot) };
		const double StartSeconds;
	};
	using FLoadingStatePtr = TSharedPtr<FState, ESPMode::ThreadSafe>;

	bool EvaluateEnablePolicy(
		const bool bCommandlet,
		const bool bDedicatedServer,
		const bool bEditor,
		const bool bUnattended,
		const bool bForce,
		const bool bSkip)
	{
		if (bSkip)
		{
			return false;
		}
		return bForce || (!bCommandlet && !bDedicatedServer && !bEditor && !bUnattended);
	}

	class SABTSStartupLoadingScreen final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SABTSStartupLoadingScreen) {}
			SLATE_ARGUMENT(FLoadingStatePtr, State)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			State = InArgs._State;
			const FLoadingStatePtr CapturedState = State;

			ChildSlot
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SBorder)
					.BorderBackgroundColor(FLinearColor::Black)
				]
				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Padding(FMargin(48.0f))
				[
					SNew(SBox)
					.WidthOverride(720.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						.Padding(0.0f, 0.0f, 0.0f, 18.0f)
						[
							SNew(STextBlock)
							.Text(NSLOCTEXT("ABTSLoadingScreen", "Title", "ANGRY BIRDS TO SPACE"))
							.ColorAndOpacity(FLinearColor(0.31f, 0.91f, 1.0f, 1.0f))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 34))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						.Padding(0.0f, 0.0f, 0.0f, 12.0f)
						[
							SNew(STextBlock)
							.Text_Lambda([CapturedState]() { return CapturedState->GetStatusText(); })
							.ColorAndOpacity(FLinearColor(0.72f, 0.82f, 0.92f, 1.0f))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 15))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SBox)
							.HeightOverride(10.0f)
							[
								SNew(SOverlay)
								+ SOverlay::Slot()
								[
									SNew(SBorder)
									.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
									.BorderBackgroundColor(FLinearColor(0.08f, 0.15f, 0.24f, 1.0f))
									.Padding(0.0f)
								]
								+ SOverlay::Slot()
								.HAlign(HAlign_Left)
								[
									SNew(SBox)
									.WidthOverride_Lambda([CapturedState]() -> FOptionalSize
									{
										return FOptionalSize(720.0f * CapturedState->GetProgress());
									})
									[
										SNew(SBorder)
										.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
										.BorderBackgroundColor(FLinearColor(0.18f, 0.82f, 0.94f, 1.0f))
										.Padding(0.0f)
									]
								]
							]
						]
					]
				]
			];
		}

	private:
		FLoadingStatePtr State;
	};
}

class FABTSLoadingScreenModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		const bool bForce = FParse::Param(FCommandLine::Get(), TEXT("ABTSStartupLoadingScreen"));
		const bool bSkip = FParse::Param(FCommandLine::Get(), TEXT("ABTSSkipStartupLoadingScreen"));
		if (!ABTSLoadingScreen::EvaluateEnablePolicy(
			IsRunningCommandlet(),
			IsRunningDedicatedServer(),
			GIsEditor,
			FApp::IsUnattended(),
			bForce,
			bSkip))
		{
			UE_LOG(LogABTSLoadingScreen, Verbose, TEXT("Startup loading screen disabled by runtime policy."));
			return;
		}

		State = MakeShared<ABTSLoadingScreen::FState, ESPMode::ThreadSafe>();
		PreLoadMapHandle = FCoreUObjectDelegates::PreLoadMap.AddRaw(this, &FABTSLoadingScreenModule::HandlePreLoadMap);
		PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddRaw(this, &FABTSLoadingScreenModule::HandlePostLoadMap);

		FLoadingScreenAttributes Attributes;
		Attributes.WidgetLoadingScreen = SNew(ABTSLoadingScreen::SABTSStartupLoadingScreen).State(State);
		Attributes.MinimumLoadingScreenDisplayTime = -1.0f;
		Attributes.bAutoCompleteWhenLoadingCompletes = true;
		Attributes.bMoviesAreSkippable = false;
		Attributes.bWaitForManualStop = false;
		Attributes.bAllowInEarlyStartup = true;
		Attributes.bAllowEngineTick = false;
		GetMoviePlayer()->SetupLoadingScreen(Attributes);
		UE_LOG(LogABTSLoadingScreen, Display, TEXT("Startup loading screen prepared TargetGenerationSeconds=30."));
	}

	virtual void ShutdownModule() override
	{
		if (PreLoadMapHandle.IsValid())
		{
			FCoreUObjectDelegates::PreLoadMap.Remove(PreLoadMapHandle);
			PreLoadMapHandle.Reset();
		}
		if (PostLoadMapHandle.IsValid())
		{
			FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
			PostLoadMapHandle.Reset();
		}
		State.Reset();
	}

private:
	void HandlePreLoadMap(const FString& MapName) const
	{
		if (State.IsValid())
		{
			State->Phase.Store(static_cast<int32>(ABTSLoadingScreen::EPhase::LoadingMap));
		}
		UE_LOG(LogABTSLoadingScreen, Display, TEXT("Map generation started Map=%s."), *MapName);
	}

	void HandlePostLoadMap(UWorld*) const
	{
		if (State.IsValid())
		{
			State->Phase.Store(static_cast<int32>(ABTSLoadingScreen::EPhase::MapLoaded));
		}
		UE_LOG(LogABTSLoadingScreen, Display, TEXT("Map generation load phase completed."));
	}

	TSharedPtr<ABTSLoadingScreen::FState, ESPMode::ThreadSafe> State;
	FDelegateHandle PreLoadMapHandle;
	FDelegateHandle PostLoadMapHandle;
};

IMPLEMENT_MODULE(FABTSLoadingScreenModule, ABTSLoadingScreen)

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSStartupLoadingScreenPolicyTest,
	"ABTS.UI.StartupLoadingScreen.Policy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSStartupLoadingScreenPolicyTest::RunTest(const FString& Parameters)
{
	using ABTSLoadingScreen::EvaluateEnablePolicy;
	TestTrue(TEXT("Interactive game enables the startup layer"),
		EvaluateEnablePolicy(false, false, false, false, false, false));
	TestFalse(TEXT("Unattended automation skips it by default"),
		EvaluateEnablePolicy(false, false, false, true, false, false));
	TestFalse(TEXT("Editor skips it by default"),
		EvaluateEnablePolicy(false, false, true, false, false, false));
	TestTrue(TEXT("Explicit offscreen evidence can force it"),
		EvaluateEnablePolicy(false, false, true, true, true, false));
	TestFalse(TEXT("Explicit skip wins over force"),
		EvaluateEnablePolicy(false, false, false, false, true, true));
	TestEqual(TEXT("MoviePlayer progress uses the shared 30 second clock"),
		ABTSLoadingScreen::ComputeLoadingProgress(15.0, ABTSLoadingScreen::EPhase::LoadingMap),
		0.5f);
	TestEqual(TEXT("Map load completion remains capped until runtime authority Ready"),
		ABTSLoadingScreen::ComputeLoadingProgress(90.0, ABTSLoadingScreen::EPhase::MapLoaded),
		0.92f);
	TestEqual(TEXT("MoviePlayer handoff never restarts or advances the same elapsed clock"),
		ABTSLoadingScreen::ComputeLoadingProgress(12.0, ABTSLoadingScreen::EPhase::LoadingMap),
		ABTSLoadingScreen::ComputeLoadingProgress(12.0, ABTSLoadingScreen::EPhase::MapLoaded));
	return true;
}
#endif
