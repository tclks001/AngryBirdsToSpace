// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Rendering/ABTSStylizedRenderingControl.h"
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
		TEXT("T0 truthfully reports an identity-only implementation"),
		FABTSStylizedRenderingControl::GetImplementationVersion(),
		0);
	TestFalse(
		TEXT("Out-of-range profiles are rejected"),
		FABTSStylizedRenderingControl::IsProfileValid(
			static_cast<EABTSStylizedRenderProfile>(255)));

	FABTSStylizedRenderingControl::SetProfile(SavedProfile);
	FABTSStylizedRenderingControl::SetEnabled(bSavedEnabled);
	return true;
}

#endif
