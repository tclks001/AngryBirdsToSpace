// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/ABTSToonT2C1CaptureTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT2C1CommandLineContractTest,
	"ABTS.Rendering.Toon.T2C1.CommandLineContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT2C1CommandLineContractTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FABTSToonT2C1CaptureConfig Config;
	FString Failure;
	TestTrue(
		TEXT("Absent flag keeps the subsystem disabled"),
		FABTSToonT2C1CaptureConfig::Parse(TEXT(""), Config, &Failure));
	TestFalse(TEXT("Disabled without explicit flag"), Config.bEnabled);

	const FString LandingCommand =
		TEXT("-ABTSToonT2C1Capture -ABTSToonT2C1Slice=LandingPreviews ")
		TEXT("-ABTSToonT2C1Stylized=0 -ABTSToonT2C1ExpectedSeed=42 ")
		TEXT("-ABTSToonT2C1ScreenPercentage=75 ")
		TEXT("-ABTSToonT2C1WarmupFrames=12 -ABTSToonT2C1TimeoutSeconds=240 ")
		TEXT("-ABTSToonT2C1Output=C:/Capture/T2C1 -ABTSToonT2C1BuildId=deadbeef ")
		TEXT("-ABTSToonT2C1ExitWhenDone");
	TestTrue(
		TEXT("Landing slice accepts the formal contract"),
		FABTSToonT2C1CaptureConfig::Parse(
			*LandingCommand,
			Config,
			&Failure));
	TestTrue(TEXT("Landing capture enabled"), Config.bEnabled);
	TestEqual(
		TEXT("Landing slice parsed"),
		static_cast<int32>(Config.Slice),
		static_cast<int32>(EABTSToonT2C1CaptureSlice::LandingPreviews));
	TestFalse(TEXT("Style Off parsed"), Config.bStylized);
	TestTrue(TEXT("Landing slice owns process exit"), Config.bExitWhenComplete);
	TestEqual(TEXT("Expected seed parsed"), Config.ExpectedWorldSeed, 42);
	TestEqual(TEXT("Screen percentage parsed"), Config.ScreenPercentage, 75);
	TestEqual(TEXT("Warmup parsed"), Config.WarmupFrames, 12);

	const FString FinaleCommand =
		TEXT("-ABTSVisualCaptureSuite=ToonT2C1 ")
		TEXT("-ABTSToonT2C1Slice=FinaleRemotePreview ")
		TEXT("-ABTSToonT2C1Stylized=1 ")
		TEXT("-ABTSToonT2C1Output=C:/Capture/T2C1Finale ")
		TEXT("-ABTSToonT2C1BuildId=feedface");
	TestTrue(
		TEXT("Finale observer accepts M11-owned process lifetime"),
		FABTSToonT2C1CaptureConfig::Parse(
			*FinaleCommand,
			Config,
			&Failure));
	TestEqual(
		TEXT("Finale slice parsed"),
		static_cast<int32>(Config.Slice),
		static_cast<int32>(EABTSToonT2C1CaptureSlice::FinaleRemotePreview));
	TestTrue(TEXT("Finale style on parsed"), Config.bStylized);
	TestFalse(TEXT("Finale does not steal auto exit"), Config.bExitWhenComplete);

	TestFalse(
		TEXT("Unknown slice fails closed"),
		FABTSToonT2C1CaptureConfig::Parse(
			TEXT("-ABTSToonT2C1Capture -ABTSToonT2C1Slice=Unknown"),
			Config,
			&Failure));
	TestFalse(
		TEXT("Invalid style boolean fails closed"),
		FABTSToonT2C1CaptureConfig::Parse(
			TEXT("-ABTSToonT2C1Capture -ABTSToonT2C1Stylized=2"),
			Config,
			&Failure));
	TestFalse(
		TEXT("Unsupported screen percentage fails closed"),
		FABTSToonT2C1CaptureConfig::Parse(
			TEXT("-ABTSToonT2C1Capture -ABTSToonT2C1ScreenPercentage=60"),
			Config,
			&Failure));
	TestFalse(
		TEXT("Finale slice cannot own M11 process exit"),
		FABTSToonT2C1CaptureConfig::Parse(
			TEXT("-ABTSToonT2C1Capture -ABTSToonT2C1Slice=FinaleRemotePreview ")
			TEXT("-ABTSToonT2C1Output=C:/Capture -ABTSToonT2C1BuildId=x ")
			TEXT("-ABTSToonT2C1ExitWhenDone"),
			Config,
			&Failure));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT2C1PreviewFixtureTest,
	"ABTS.Rendering.Toon.T2C1.PreviewFixtures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT2C1PreviewFixtureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FABTSM6TrajectoryPreview GroundA;
	FABTSM6TrajectoryPreview GroundB;
	TestTrue(
		TEXT("Ground preview fixture builds"),
		FABTSToonT2C1PreviewFixtureBuilder::BuildGroundLandingPreview(
			FVector(100.0, 200.0, 300.0),
			FVector::UpVector,
			FVector::ForwardVector,
			GroundA));
	TestTrue(
		TEXT("Repeated ground fixture builds"),
		FABTSToonT2C1PreviewFixtureBuilder::BuildGroundLandingPreview(
			FVector(100.0, 200.0, 300.0),
			FVector::UpVector,
			FVector::ForwardVector,
			GroundB));
	TestTrue(TEXT("Ground marks primary landing"), GroundA.bHasPrimarySurfaceLanding);
	TestEqual(
		TEXT("Ground uses reinforced tier"),
		static_cast<int32>(GroundA.SlingshotTier),
		static_cast<int32>(EABTSSlingshotTier::Reinforced));
	TestEqual(
		TEXT("Ground terminal is primary"),
		static_cast<int32>(GroundA.TerminalType),
		static_cast<int32>(EABTSM6TrajectoryTerminalType::PrimarySurface));
	TestEqual(TEXT("Ground fixture point count"), GroundA.WorldPoints.Num(), 12);
	TestTrue(
		TEXT("Ground fixture exposes a curved approach in the PIP"),
		FVector::CrossProduct(
			GroundA.WorldPoints[0]
				- GroundA.PrimarySurfaceLandingWorld,
			GroundA.PrimarySurfaceLandingVelocity.GetSafeNormal()).Size() > 250.0);
	const uint64 GroundHash =
		FABTSToonT2C1PreviewFixtureBuilder::ComputeFixtureHash(GroundA);
	TestNotEqual(TEXT("Ground hash is non-zero"), GroundHash, uint64(0));
	TestEqual(
		TEXT("Ground fixture is deterministic"),
		GroundHash,
		FABTSToonT2C1PreviewFixtureBuilder::ComputeFixtureHash(GroundB));

	FABTSM6TrajectoryPreview Satellite;
	TestTrue(
		TEXT("Satellite E5 fixture builds"),
		FABTSToonT2C1PreviewFixtureBuilder::BuildSatelliteE5Preview(
			FVector::ZeroVector,
			500.0,
			FVector(0.0, 0.0, 500.0),
			FVector(50.0),
			FVector::ForwardVector,
			Satellite));
	TestEqual(
		TEXT("Satellite fixture targets E5"),
		static_cast<int32>(Satellite.TerminalType),
		static_cast<int32>(EABTSM6TrajectoryTerminalType::SatelliteE5));
	TestTrue(TEXT("Satellite encounter is explicit"), Satellite.bHasSatelliteEncounter);
	TestTrue(
		TEXT("Satellite terminal is the outward E5 face"),
		Satellite.TerminalWorldLocation.Equals(
			FVector(0.0, 0.0, 550.0),
			0.01));
	TestEqual(TEXT("Satellite fixture point count"), Satellite.WorldPoints.Num(), 16);
	TestNotEqual(
		TEXT("Ground and satellite fixtures have distinct identities"),
		GroundHash,
		FABTSToonT2C1PreviewFixtureBuilder::ComputeFixtureHash(Satellite));
	TestFalse(
		TEXT("Degenerate tangent fails closed"),
		FABTSToonT2C1PreviewFixtureBuilder::BuildSatelliteE5Preview(
			FVector::ZeroVector,
			500.0,
			FVector(0.0, 0.0, 500.0),
			FVector(50.0),
			FVector::UpVector,
			Satellite));
	return true;
}

#endif
