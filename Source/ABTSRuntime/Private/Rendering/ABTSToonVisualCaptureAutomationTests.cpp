// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Rendering/ABTSStylizedRenderingControl.h"
#include "Rendering/ABTSStylizedRenderingTypes.h"
#include "Rendering/ABTSToonVisualCaptureTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT0CommandLineContractTest,
	"ABTS.Rendering.Toon.T0.CommandLineContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT0CommandLineContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FABTSToonVisualCaptureRunConfig Config;
	FString Failure;
	TestTrue(
		TEXT("An unrelated process parses without enabling T0"),
		FABTSToonVisualCaptureRunConfig::Parse(
			TEXT("-NullRHI -Unattended"),
			Config,
			&Failure));
	TestFalse(TEXT("T0 is opt-in"), Config.bEnabled);

	Failure.Reset();
	TestTrue(
		TEXT("The named suite and GPU contract parse"),
		FABTSToonVisualCaptureRunConfig::Parse(
			TEXT("-ABTSVisualCaptureSuite=ToonT0 -ABTSToonT0Mode=GPU -ABTSToonT0ExpectedSeed=42 -ABTSToonT0ResX=2560 -ABTSToonT0ResY=1440 -ABTSToonT0WarmupFrames=12 -ABTSToonT0GPUSamples=5 -ABTSToonT0TimeoutSeconds=240 -ABTSToonT0AllowAnyResolution -ABTSToonT0KeepWorldRunning -ABTSToonT0ExitWhenDone -ABTSToonT0Output=VisualEvidence -ABTSToonT0BuildId=deadbeef"),
			Config,
			&Failure));
	TestTrue(TEXT("T0 enabled"), Config.bEnabled);
	TestEqual(
		TEXT("GPU mode"),
		static_cast<int32>(Config.Mode),
		static_cast<int32>(EABTSToonVisualCaptureMode::GPUProfile));
	TestEqual(TEXT("Seed override"), Config.ExpectedWorldSeed, 42);
	TestEqual(TEXT("Resolution X"), Config.ExpectedResolutionX, 2560);
	TestEqual(TEXT("Resolution Y"), Config.ExpectedResolutionY, 1440);
	TestEqual(TEXT("Warmup"), Config.WarmupFrames, 12);
	TestEqual(TEXT("GPU samples"), Config.GPUProfileSamplesPerVariant, 5);
	TestFalse(TEXT("Resolution preview escape hatch"), Config.bRequireExactResolution);
	TestFalse(TEXT("World-running escape hatch"), Config.bPauseWorldDuringCapture);
	TestTrue(TEXT("Exit flag"), Config.bExitWhenComplete);
	TestEqual(TEXT("Output root"), Config.OutputDirectory, FString(TEXT("VisualEvidence")));
	TestEqual(TEXT("Build identity"), Config.BuildIdentity, FString(TEXT("deadbeef")));

	Failure.Reset();
	TestFalse(
		TEXT("Unknown modes fail closed"),
		FABTSToonVisualCaptureRunConfig::Parse(
			TEXT("-ABTSToonT0Capture -ABTSToonT0Mode=Unknown"),
			Config,
			&Failure));
	TestTrue(TEXT("Invalid mode reports a reason"), !Failure.IsEmpty());

	Failure.Reset();
	TestFalse(
		TEXT("An enabled suite without build identity fails closed"),
		FABTSToonVisualCaptureRunConfig::Parse(
			TEXT("-ABTSToonT0Capture"),
			Config,
			&Failure));
	TestTrue(TEXT("Missing build identity reports a reason"), !Failure.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT0CatalogueAndCameraMathTest,
	"ABTS.Rendering.Toon.T0.CatalogueAndCameraMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT0CatalogueAndCameraMathTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const TArray<FABTSToonVisualCapturePointDefinition> Catalogue =
		FABTSToonVisualCaptureMath::BuildDefaultCatalogue();
	TestEqual(TEXT("Four semantic points"), Catalogue.Num(), 4);
	TSet<FName> PointIds;
	for (const FABTSToonVisualCapturePointDefinition& Definition : Catalogue)
	{
		TestTrue(TEXT("Every catalogue point is valid"), Definition.IsValid());
		PointIds.Add(Definition.PointId);
	}
	TestEqual(TEXT("Point IDs are unique"), PointIds.Num(), Catalogue.Num());
	TestEqual(
		TEXT("Ground profile"),
		static_cast<int32>(Catalogue[0].StyleProfile),
		static_cast<int32>(EABTSStylizedRenderProfile::GroundDay));
	TestEqual(
		TEXT("Satellite profile"),
		static_cast<int32>(Catalogue[2].StyleProfile),
		static_cast<int32>(EABTSStylizedRenderProfile::SatelliteGuide));
	TestEqual(
		TEXT("Finale profile"),
		static_cast<int32>(Catalogue[3].StyleProfile),
		static_cast<int32>(EABTSStylizedRenderProfile::FinaleSpace));

	const uint64 HashA =
		FABTSToonVisualCaptureMath::ComputeCatalogueHash(Catalogue);
	const uint64 HashB =
		FABTSToonVisualCaptureMath::ComputeCatalogueHash(Catalogue);
	TestTrue(TEXT("Catalogue hash is non-zero"), HashA != 0);
	TestEqual(TEXT("Catalogue hash is deterministic"), HashA, HashB);
	TArray<FABTSToonVisualCapturePointDefinition> Reordered = Catalogue;
	Reordered.Swap(0, 1);
	TestTrue(
		TEXT("Catalogue order is part of capture identity"),
		FABTSToonVisualCaptureMath::ComputeCatalogueHash(Reordered) != HashA);

	const FVector CameraLocation(50.0, -200.0, 80.0);
	const FVector LookAt(10.0, 20.0, 30.0);
	FTransform CameraTransform;
	FString Failure;
	TestTrue(
		TEXT("Look-at transform resolves"),
		FABTSToonVisualCaptureMath::BuildLookAtCameraTransform(
			CameraLocation,
			LookAt,
			FVector::UpVector,
			CameraTransform,
			&Failure));
	const FVector ExpectedForward =
		(LookAt - CameraLocation).GetSafeNormal();
	TestTrue(
		TEXT("Camera X axis looks at the target"),
		CameraTransform.GetUnitAxis(EAxis::X).Equals(
			ExpectedForward,
			1.0e-4));
	TestTrue(
		TEXT("Camera up is orthogonal to forward"),
		FMath::IsNearlyZero(FVector::DotProduct(
			CameraTransform.GetUnitAxis(EAxis::X),
			CameraTransform.GetUnitAxis(EAxis::Z)),
			1.0e-4));
	TestTrue(
		TEXT("Parallel preferred up uses a deterministic fallback"),
		FABTSToonVisualCaptureMath::BuildLookAtCameraTransform(
			FVector::ZeroVector,
			FVector::ForwardVector * 100.0,
			FVector::ForwardVector,
			CameraTransform,
			&Failure));
	TestFalse(
		TEXT("Coincident camera and target fail closed"),
		FABTSToonVisualCaptureMath::BuildLookAtCameraTransform(
			FVector::ZeroVector,
			FVector::ZeroVector,
			FVector::UpVector,
			CameraTransform,
			&Failure));

	const double WideDistance =
		FABTSToonVisualCaptureMath::ComputePerspectiveFitDistanceCM(
			500.0,
			80.0,
			16.0 / 9.0);
	const double NarrowDistance =
		FABTSToonVisualCaptureMath::ComputePerspectiveFitDistanceCM(
			500.0,
			40.0,
			16.0 / 9.0);
	TestTrue(TEXT("A narrower FOV requires more distance"), NarrowDistance > WideDistance);
	TestTrue(
		TEXT("Invalid fit inputs fail closed"),
		FMath::IsNearlyZero(
			FABTSToonVisualCaptureMath::ComputePerspectiveFitDistanceCM(
				0.0,
				60.0,
				16.0 / 9.0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT0StyleSwitchSeamTest,
	"ABTS.Rendering.Toon.T0.StyleSwitchSeam",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT0StyleSwitchSeamTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const bool bSavedEnabled = FABTSStylizedRenderingControl::IsEnabled();
	const EABTSStylizedRenderProfile SavedProfile =
		FABTSStylizedRenderingControl::GetProfile();

	FABTSStylizedRenderingControl::SetProfile(
		EABTSStylizedRenderProfile::FinaleSpace);
	FABTSStylizedRenderingControl::SetEnabled(true);
	TestTrue(TEXT("Style switch can be enabled"), FABTSStylizedRenderingControl::IsEnabled());
	TestEqual(
		TEXT("Profile switch is retained"),
		static_cast<int32>(FABTSStylizedRenderingControl::GetProfile()),
		static_cast<int32>(EABTSStylizedRenderProfile::FinaleSpace));
	TestEqual(
		TEXT("T2-A reports the temporally stabilized outline implementation"),
		FABTSStylizedRenderingControl::GetImplementationVersion(),
		3);
	TestTrue(
		TEXT("Any-thread switch mirrors the game-thread switch"),
		FABTSStylizedRenderingControl::IsEnabledOnAnyThread());
	TestEqual(
		TEXT("Any-thread profile mirrors the game-thread profile"),
		static_cast<int32>(
			FABTSStylizedRenderingControl::GetProfileOnAnyThread()),
		static_cast<int32>(EABTSStylizedRenderProfile::FinaleSpace));

	TArray<FABTSStylizedToneProfileParameters> Profiles;
	TArray<FABTSStylizedOutlineProfileParameters> OutlineProfiles;
	for (int32 ProfileIndex =
			static_cast<int32>(EABTSStylizedRenderProfile::GroundDay);
		ProfileIndex <=
			static_cast<int32>(EABTSStylizedRenderProfile::FinaleSpace);
		++ProfileIndex)
	{
		const FABTSStylizedToneProfileParameters Profile =
			FABTSStylizedRenderingControl::GetToneProfileParameters(
				static_cast<EABTSStylizedRenderProfile>(ProfileIndex));
		TestTrue(TEXT("Every T1 tone profile is valid"), Profile.IsValid());
		Profiles.Add(Profile);
		const FABTSStylizedOutlineProfileParameters OutlineProfile =
			FABTSStylizedRenderingControl::GetOutlineProfileParameters(
				static_cast<EABTSStylizedRenderProfile>(ProfileIndex));
		TestTrue(TEXT("Every T2-A outline profile is valid"), OutlineProfile.IsValid());
		OutlineProfiles.Add(OutlineProfile);
	}
	TestNotEqual(
		TEXT("Ground and satellite shadow thresholds differ"),
		Profiles[0].ShadowThreshold,
		Profiles[1].ShadowThreshold);
	TestNotEqual(
		TEXT("Satellite and finale strengths differ"),
		Profiles[1].Strength,
		Profiles[2].Strength);
	TestNotEqual(
		TEXT("Ground and finale outline widths differ"),
		OutlineProfiles[0].WidthPixels,
		OutlineProfiles[2].WidthPixels);
	TestFalse(
		TEXT("Out-of-range profiles are rejected"),
		FABTSStylizedRenderingControl::IsProfileValid(
			static_cast<EABTSStylizedRenderProfile>(255)));

	FABTSStylizedRenderingControl::SetProfile(SavedProfile);
	FABTSStylizedRenderingControl::SetEnabled(bSavedEnabled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT2ASharedRenderingContractTest,
	"ABTS.Rendering.Toon.T2A.SharedRenderingContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT2ASharedRenderingContractTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	TSet<uint8> SelectiveStencilValues;
	for (int32 ClassIndex =
		static_cast<int32>(EABTSStylizedObjectClass::None);
		ClassIndex <= static_cast<int32>(EABTSStylizedObjectClass::FinaleUFO);
		++ClassIndex)
	{
		const EABTSStylizedObjectClass ObjectClass =
			static_cast<EABTSStylizedObjectClass>(ClassIndex);
		TestTrue(
			TEXT("Every declared object class is valid"),
			FABTSStylizedRenderingContract::IsObjectClassValid(ObjectClass));
		const uint8 StencilValue =
			FABTSStylizedRenderingContract::ResolveStencilValueForRenderer(
				ObjectClass);
		TestEqual(
			TEXT("Selective classification matches the renderer allocation"),
			FABTSStylizedRenderingContract::RequiresSelectiveStencil(ObjectClass),
			StencilValue != 0);
		if (StencilValue != 0)
		{
			TestTrue(
				TEXT("Feature stencil values remain inside the Integration reserve"),
				StencilValue <= 31);
			TestFalse(
				TEXT("Every selective object class has a unique stencil value"),
				SelectiveStencilValues.Contains(StencilValue));
			SelectiveStencilValues.Add(StencilValue);
		}
	}
	TestEqual(TEXT("Seven gameplay classes are selective"), SelectiveStencilValues.Num(), 7);
	TestFalse(
		TEXT("Unknown object classes fail closed"),
		FABTSStylizedRenderingContract::IsObjectClassValid(
			static_cast<EABTSStylizedObjectClass>(255)));
	TestEqual(
		TEXT("Unknown object classes never receive a stencil value"),
		FABTSStylizedRenderingContract::ResolveStencilValueForRenderer(
			static_cast<EABTSStylizedObjectClass>(255)),
		static_cast<uint8>(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT2AViewPolicyTest,
	"ABTS.Rendering.Toon.T2A.ViewPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT2AViewPolicyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	for (int32 ViewIndex = static_cast<int32>(EABTSStylizedViewClass::MainWorld);
		ViewIndex <= static_cast<int32>(EABTSStylizedViewClass::FinaleRemotePreview);
		++ViewIndex)
	{
		const EABTSStylizedViewClass ViewClass =
			static_cast<EABTSStylizedViewClass>(ViewIndex);
		TestTrue(
			TEXT("Every declared view class is valid"),
			FABTSStylizedRenderingContract::IsViewClassValid(ViewClass));
		TestTrue(
			TEXT("Every declared view class resolves a valid policy"),
			FABTSStylizedRenderingContract::ResolveViewPolicy(
				ViewClass,
				EABTSStylizedRenderProfile::FinaleSpace).IsValid());
	}

	const FABTSStylizedViewPolicy MainPolicy =
		FABTSStylizedRenderingContract::ResolveViewPolicy(
			EABTSStylizedViewClass::MainWorld,
			EABTSStylizedRenderProfile::FinaleSpace);
	TestEqual(
		TEXT("Main view consumes the active runtime profile"),
		static_cast<int32>(MainPolicy.Profile),
		static_cast<int32>(EABTSStylizedRenderProfile::FinaleSpace));
	TestTrue(TEXT("Main view applies tone"), MainPolicy.bApplyTone);
	TestTrue(TEXT("Main view applies outline"), MainPolicy.bApplyOutline);
	TestTrue(TEXT("Main view permits selective stencil"), MainPolicy.bAllowSelectiveStencil);
	TestTrue(
		TEXT("T2-A implements the final main view"),
		FABTSStylizedRenderingContract::IsViewClassImplemented(
			EABTSStylizedViewClass::MainWorld));
	TestFalse(
		TEXT("T2-A leaves Scene Capture wiring to T2-B"),
		FABTSStylizedRenderingContract::IsViewClassImplemented(
			EABTSStylizedViewClass::SatelliteLandingPreview));

	TestEqual(
		TEXT("Ground preview has a frozen ground profile"),
		static_cast<int32>(
			FABTSStylizedRenderingContract::ResolveViewPolicy(
				EABTSStylizedViewClass::GroundLandingPreview).Profile),
		static_cast<int32>(EABTSStylizedRenderProfile::GroundDay));
	TestEqual(
		TEXT("Satellite preview has a frozen satellite profile"),
		static_cast<int32>(
			FABTSStylizedRenderingContract::ResolveViewPolicy(
				EABTSStylizedViewClass::SatelliteLandingPreview).Profile),
		static_cast<int32>(EABTSStylizedRenderProfile::SatelliteGuide));
	TestEqual(
		TEXT("Finale preview has a frozen finale profile"),
		static_cast<int32>(
			FABTSStylizedRenderingContract::ResolveViewPolicy(
				EABTSStylizedViewClass::FinaleRemotePreview).Profile),
		static_cast<int32>(EABTSStylizedRenderProfile::FinaleSpace));
	TestFalse(
		TEXT("Unknown view classes fail closed"),
		FABTSStylizedRenderingContract::IsViewClassValid(
			static_cast<EABTSStylizedViewClass>(255)));
	return true;
}

#endif
