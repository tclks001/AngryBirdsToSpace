// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Rendering/ABTSStylizedRenderingControl.h"
#include "Rendering/ABTSStylizedRenderingTypes.h"
#include "Rendering/ABTSToonEnvironmentTypes.h"
#include "Rendering/ABTSToonVisualCaptureTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT4A0EnvironmentSnapshotContractTest,
	"ABTS.Rendering.Toon.T4A0.EnvironmentSnapshotContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT4A0EnvironmentSnapshotContractTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FABTSToonEnvironmentSnapshot SnapshotA;
	FABTSToonEnvironmentSnapshot SnapshotB;
	FString Failure;
	const FVector Center(100.0, -200.0, 300.0);
	TestTrue(
		TEXT("A normalized, accepted environment builds"),
		FABTSToonEnvironmentResolver::BuildSnapshot(
			Center,
			10000.0,
			FVector(10.0, 0.0, 0.0),
			EABTSStylizedRenderProfile::GroundDay,
			312503,
			17,
			2,
			true,
			SnapshotA,
			&Failure));
	TestTrue(TEXT("The snapshot validates"), SnapshotA.IsValid());
	TestTrue(
		TEXT("Sun direction is normalized"),
		SnapshotA.SunDirectionToSunWorld.Equals(FVector::ForwardVector));
	TestEqual(
		TEXT("Surface altitude is zero"),
		SnapshotA.ComputeAltitudeCM(Center + FVector::ForwardVector * 10000.0),
		0.0);
	TestTrue(
		TEXT("Radial up is planet relative"),
		SnapshotA.ComputeRadialUp(Center + FVector::UpVector * 12000.0)
			.Equals(FVector::UpVector));

	TestTrue(
		TEXT("The same inputs rebuild"),
		FABTSToonEnvironmentResolver::BuildSnapshot(
			Center,
			10000.0,
			FVector::ForwardVector,
			EABTSStylizedRenderProfile::GroundDay,
			312503,
			17,
			2,
			true,
			SnapshotB,
			&Failure));
	TestEqual(
		TEXT("Snapshot identity is deterministic"),
		SnapshotA.IdentityHash,
		SnapshotB.IdentityHash);

	Failure.Reset();
	TestFalse(
		TEXT("An unaccepted world fails closed"),
		FABTSToonEnvironmentResolver::BuildSnapshot(
			Center,
			10000.0,
			FVector::ForwardVector,
			EABTSStylizedRenderProfile::GroundDay,
			312503,
			17,
			2,
			false,
			SnapshotB,
			&Failure));
	TestTrue(TEXT("Failure has a reason"), !Failure.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT4A0CaptureCatalogueTest,
	"ABTS.Rendering.Toon.T4A0.CaptureCatalogue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT4A0CaptureCatalogueTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FABTSToonVisualCaptureRunConfig ParsedConfig;
	FString ParseFailure;
	TestTrue(
		TEXT("The named T4-A0 screenshot suite parses"),
		FABTSToonVisualCaptureRunConfig::Parse(
			TEXT("-ABTSVisualCaptureSuite=ToonT4A0 -ABTSToonT0BuildId=T4A0-Test"),
			ParsedConfig,
			&ParseFailure));
	TestTrue(TEXT("The T4-A0 suite is enabled"), ParsedConfig.bEnabled);
	TestEqual(
		TEXT("The parser selects the T4-A0 suite"),
		static_cast<int32>(ParsedConfig.Suite),
		static_cast<int32>(EABTSToonVisualCaptureSuite::ToonT4A0));
	TestEqual(
		TEXT("T4-A0 defaults to screenshots"),
		static_cast<int32>(ParsedConfig.Mode),
		static_cast<int32>(EABTSToonVisualCaptureMode::Screenshots));

	ParseFailure.Reset();
	TestFalse(
		TEXT("T4-A0 rejects GPU mode before T4-A1"),
		FABTSToonVisualCaptureRunConfig::Parse(
			TEXT("-ABTSVisualCaptureSuite=ToonT4A0 -ABTSToonT0Mode=GPU -ABTSToonT0BuildId=T4A0-Test"),
			ParsedConfig,
			&ParseFailure));
	TestTrue(TEXT("GPU rejection has a reason"), !ParseFailure.IsEmpty());

	const TArray<FABTSToonVisualCapturePointDefinition> Points =
		FABTSToonVisualCaptureMath::BuildT4A0Catalogue();
	TestEqual(TEXT("Five environment points"), Points.Num(), 5);
	const FName ExpectedPointIds[] = {
		TEXT("GroundDay"),
		TEXT("GroundDawn"),
		TEXT("GroundNight"),
		TEXT("HighAltitude"),
		TEXT("FinaleSpace")
	};
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		TestTrue(TEXT("Every T4 point is valid"), Points[Index].IsValid());
		TestEqual(
			TEXT("Point order is frozen"),
			Points[Index].PointId,
			ExpectedPointIds[Index]);
	}

	const TArray<FABTSToonDiagnosticVariantDefinition> Variants =
		FABTSToonVisualCaptureMath::BuildVariantCatalogue(
			EABTSToonVisualCaptureSuite::ToonT4A0);
	TestEqual(TEXT("Six isolation variants"), Variants.Num(), 6);
	TSet<FName> VariantIds;
	for (const FABTSToonDiagnosticVariantDefinition& Variant : Variants)
	{
		TestTrue(TEXT("Every T4 variant is valid"), Variant.IsValid());
		VariantIds.Add(Variant.VariantId);
	}
	TestEqual(TEXT("Variant IDs are unique"), VariantIds.Num(), 6);
	TestEqual(
		TEXT("Tone-only mask"),
		static_cast<int32>(Variants[1].PassMask),
		static_cast<int32>(EABTSStylizedDiagnosticPassMask::Tone));
	TestEqual(
		TEXT("Outline-only mask"),
		static_cast<int32>(Variants[2].PassMask),
		static_cast<int32>(EABTSStylizedDiagnosticPassMask::Outline));
	TestFalse(TEXT("Shadow-off disables shadows"), Variants[4].bShadowsEnabled);
	TestEqual(
		TEXT("Shadow-off removes post passes"),
		static_cast<int32>(Variants[4].PassMask),
		static_cast<int32>(EABTSStylizedDiagnosticPassMask::None));
	TestTrue(TEXT("Lighting-only retains shadows"), Variants[5].bShadowsEnabled);
	TestEqual(
		TEXT("Lighting-only removes post passes"),
		static_cast<int32>(Variants[5].PassMask),
		static_cast<int32>(EABTSStylizedDiagnosticPassMask::None));
	TestNotEqual(
		TEXT("Point and variant catalogues have independent identities"),
		FABTSToonVisualCaptureMath::ComputeCatalogueHash(Points),
		FABTSToonVisualCaptureMath::ComputeVariantCatalogueHash(Variants));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT4A0DiagnosticPassControlTest,
	"ABTS.Rendering.Toon.T4A0.DiagnosticPassControl",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT4A0DiagnosticPassControlTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const EABTSStylizedDiagnosticPassMask Saved =
		FABTSStylizedRenderingControl::GetDiagnosticPassMask();

	FABTSStylizedRenderingControl::SetDiagnosticPassMask(
		EABTSStylizedDiagnosticPassMask::Tone);
	TestTrue(
		TEXT("Tone-only enables tone"),
		FABTSStylizedRenderingControl::IsTonePassEnabledOnAnyThread());
	TestFalse(
		TEXT("Tone-only disables outline"),
		FABTSStylizedRenderingControl::IsOutlinePassEnabledOnAnyThread());

	FABTSStylizedRenderingControl::SetDiagnosticPassMask(
		EABTSStylizedDiagnosticPassMask::Outline);
	TestFalse(
		TEXT("Outline-only disables tone"),
		FABTSStylizedRenderingControl::IsTonePassEnabledOnAnyThread());
	TestTrue(
		TEXT("Outline-only enables outline"),
		FABTSStylizedRenderingControl::IsOutlinePassEnabledOnAnyThread());

	FABTSStylizedRenderingControl::SetDiagnosticPassMask(
		EABTSStylizedDiagnosticPassMask::None);
	TestFalse(
		TEXT("None disables tone"),
		FABTSStylizedRenderingControl::IsTonePassEnabledOnAnyThread());
	TestFalse(
		TEXT("None disables outline"),
		FABTSStylizedRenderingControl::IsOutlinePassEnabledOnAnyThread());

	FABTSStylizedRenderingControl::SetDiagnosticPassMask(Saved);
	TestEqual(
		TEXT("Diagnostic state restores"),
		static_cast<int32>(FABTSStylizedRenderingControl::GetDiagnosticPassMask()),
		static_cast<int32>(Saved));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT4A1EnvironmentControlTest,
	"ABTS.Rendering.Toon.T4A1.EnvironmentControl",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT4A1EnvironmentControlTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FABTSStylizedRenderingControl::ClearEnvironmentParameters();
	FABTSStylizedEnvironmentParameters Readback;
	TestFalse(
		TEXT("Environment parameters fail closed before publication"),
		FABTSStylizedRenderingControl::TryGetEnvironmentParametersOnAnyThread(
			Readback));

	const FABTSStylizedEnvironmentParameters Ground =
		FABTSStylizedRenderingControl::BuildEnvironmentParameters(
			FVector(100.0, 200.0, -300.0),
			500000.0,
			FVector(10.0, 0.0, 0.0),
			EABTSStylizedRenderProfile::GroundDay);
	const FABTSStylizedEnvironmentParameters Satellite =
		FABTSStylizedRenderingControl::BuildEnvironmentParameters(
			FVector(100.0, 200.0, -300.0),
			500000.0,
			FVector(10.0, 0.0, 0.0),
			EABTSStylizedRenderProfile::SatelliteGuide);
	const FABTSStylizedEnvironmentParameters Finale =
		FABTSStylizedRenderingControl::BuildEnvironmentParameters(
			FVector(100.0, 200.0, -300.0),
			500000.0,
			FVector(10.0, 0.0, 0.0),
			EABTSStylizedRenderProfile::FinaleSpace);
	TestTrue(TEXT("Ground parameters validate"), Ground.IsValid());
	TestTrue(TEXT("Satellite parameters validate"), Satellite.IsValid());
	TestTrue(TEXT("Finale parameters validate"), Finale.IsValid());
	TestEqual(
		TEXT("The art-directed star field seed is profile-independent"),
		Ground.StarSeed,
		Finale.StarSeed);
	TestTrue(
		TEXT("Finale stars are intentionally stronger"),
		Finale.StarHDRIntensity > Ground.StarHDRIntensity);
	TestTrue(
		TEXT("Ground stars retain a stable pixel footprint at 1080p"),
		Ground.StarAngularRadiusScale >= 0.120f);
	TestTrue(
		TEXT("Ground stars remain readable after SDR tone mapping"),
		Ground.StarHDRIntensity >= 2.6f);
	TestTrue(
		TEXT("Satellite stars remain stronger than the ground field"),
		Satellite.StarHDRIntensity > Ground.StarHDRIntensity);
	TestTrue(
		TEXT("Atmosphere height derives from the accepted radius"),
		FMath::IsNearlyEqual(Ground.AtmosphereHeightCM, 300000.0f));
	TestTrue(
		TEXT("High-altitude transition starts at 0.22 primary radii"),
		FMath::IsNearlyEqual(
			Ground.HighAltitudeTransitionStartCM,
			110000.0f));
	TestTrue(
		TEXT("High-altitude transition completes at 0.52 primary radii"),
		FMath::IsNearlyEqual(
			Ground.HighAltitudeTransitionEndCM,
			260000.0f));

	FABTSStylizedRenderingControl::SetEnvironmentParameters(Finale);
	TestTrue(
		TEXT("A valid immutable snapshot publishes to render readers"),
		FABTSStylizedRenderingControl::TryGetEnvironmentParametersOnAnyThread(
			Readback));
	TestEqual(TEXT("Readback keeps the deterministic seed"),
		Readback.StarSeed, Finale.StarSeed);
	TestTrue(TEXT("Readback keeps the planet center"),
		Readback.PlanetCenterWorld.Equals(Finale.PlanetCenterWorld));
	FABTSStylizedRenderingControl::ClearEnvironmentParameters();
	TestFalse(
		TEXT("Clearing the contract disables the pass"),
		FABTSStylizedRenderingControl::TryGetEnvironmentParametersOnAnyThread(
			Readback));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT4A31HighAltitudeTransitionContractTest,
	"ABTS.Rendering.Toon.T4A3_1.HighAltitudeTransitionContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT4A31HighAltitudeTransitionContractTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FABTSStylizedEnvironmentParameters Ground =
		FABTSStylizedRenderingControl::BuildEnvironmentParameters(
			FVector::ZeroVector,
			10000.0,
			FVector::UpVector,
			EABTSStylizedRenderProfile::GroundDay);
	TestTrue(TEXT("A3.1 environment validates"), Ground.IsValid());
	const float Start = Ground.HighAltitudeTransitionStartCM;
	const float End = Ground.HighAltitudeTransitionEndCM;
	const float Mid = 0.5f * (Start + End);
	TestEqual(TEXT("Ground remains fully atmospheric"),
		FABTSStylizedRenderingControl::ComputeHighAltitudeSpaceBlend(
			0.04f * Ground.PlanetRadiusCM, Start, End), 0.0f);
	TestEqual(TEXT("Transition start is continuous at zero"),
		FABTSStylizedRenderingControl::ComputeHighAltitudeSpaceBlend(
			Start, Start, End), 0.0f);
	TestTrue(TEXT("Transition midpoint is half space"), FMath::IsNearlyEqual(
		FABTSStylizedRenderingControl::ComputeHighAltitudeSpaceBlend(
			Mid, Start, End), 0.5f));
	TestEqual(TEXT("Transition end is fully space"),
		FABTSStylizedRenderingControl::ComputeHighAltitudeSpaceBlend(
			End, Start, End), 1.0f);
	TestEqual(TEXT("Practice-satellite altitude is fully space"),
		FABTSStylizedRenderingControl::ComputeHighAltitudeSpaceBlend(
			0.55f * Ground.PlanetRadiusCM, Start, End), 1.0f);
	const float TerminatorSunward =
		FABTSStylizedRenderingControl::ComputeGroundStarNightFactor(0.0f, 1.0f);
	const float TerminatorAntiSunward =
		FABTSStylizedRenderingControl::ComputeGroundStarNightFactor(0.0f, -1.0f);
	TestTrue(TEXT("Terminator sunward sky suppresses stars"),
		TerminatorSunward < 0.20f);
	TestTrue(TEXT("Terminator anti-sunward sky reveals stars"),
		TerminatorAntiSunward > 0.80f);
	TestTrue(TEXT("Terminator direction ordering is physically coherent"),
		TerminatorAntiSunward > TerminatorSunward + 0.60f);
	const float HorizontalSkyVisibility =
		FABTSStylizedRenderingControl::ComputeGroundStarHorizonVisibility(0.0f);
	TestTrue(TEXT("A horizontal sky ray retains readable star visibility"),
		HorizontalSkyVisibility > 0.55f);
	TestTrue(TEXT("Sunward horizontal twilight remains star-suppressed"),
		TerminatorSunward * HorizontalSkyVisibility < 0.12f);
	TestTrue(TEXT("Anti-sunward horizontal twilight remains star-readable"),
		TerminatorAntiSunward * HorizontalSkyVisibility > 0.45f);
	TestEqual(TEXT("Below-ground sky rays are rejected"),
		FABTSStylizedRenderingControl::ComputeGroundStarHorizonVisibility(-0.10f),
		0.0f);
	TestEqual(TEXT("Clearly upward sky rays are fully visible"),
		FABTSStylizedRenderingControl::ComputeGroundStarHorizonVisibility(0.08f),
		1.0f);
	TestTrue(TEXT("Deep day remains star-free even when looking anti-sunward"),
		FABTSStylizedRenderingControl::ComputeGroundStarNightFactor(0.90f, -1.0f)
			< 0.01f);
	TestTrue(TEXT("Deep night retains stars even when looking sunward"),
		FABTSStylizedRenderingControl::ComputeGroundStarNightFactor(-0.90f, 1.0f)
			> 0.99f);
	const float CameraRadius = Ground.PlanetRadiusCM * 1.01f;
	const float RadiusRatio = Ground.PlanetRadiusCM / CameraRadius;
	const float HorizonCos = FMath::Sqrt(
		1.0f - RadiusRatio * RadiusRatio);
	TestTrue(TEXT("A ray away from the planet keeps the analytic sun visible"),
		FABTSStylizedRenderingControl::ComputeGroundSkyRayPlanetClearance(
			CameraRadius, Ground.PlanetRadiusCM, -1.0f) > 0.0f);
	TestTrue(TEXT("A ray through the planet occludes the analytic sun"),
		FABTSStylizedRenderingControl::ComputeGroundSkyRayPlanetClearance(
			CameraRadius, Ground.PlanetRadiusCM, 1.0f) < 0.0f);
	TestTrue(TEXT("The accepted base sphere defines a continuous tangent horizon"),
		FMath::IsNearlyZero(
			FABTSStylizedRenderingControl::ComputeGroundSkyRayPlanetClearance(
				CameraRadius, Ground.PlanetRadiusCM, HorizonCos),
			1.0e-5f));
	TestTrue(TEXT("Sky-ray visibility is view dependent at one observer position"),
		FABTSStylizedRenderingControl::ComputeGroundSkyRayPlanetClearance(
			CameraRadius, Ground.PlanetRadiusCM, -0.20f)
			> FABTSStylizedRenderingControl::ComputeGroundSkyRayPlanetClearance(
				CameraRadius, Ground.PlanetRadiusCM, 0.20f));
	float Previous = 0.0f;
	for (int32 Step = 0; Step <= 32; ++Step)
	{
		const float Altitude = End * static_cast<float>(Step) / 32.0f;
		const float Blend =
			FABTSStylizedRenderingControl::ComputeHighAltitudeSpaceBlend(
				Altitude, Start, End);
		TestTrue(TEXT("Altitude blend remains finite"), FMath::IsFinite(Blend));
		TestTrue(TEXT("Altitude blend is monotonic"), Blend + KINDA_SMALL_NUMBER >= Previous);
		Previous = Blend;
	}

	FABTSToonVisualCaptureRunConfig Config;
	FString Failure;
	TestTrue(TEXT("T4-A3 capture suite parses"),
		FABTSToonVisualCaptureRunConfig::Parse(
			TEXT("-ABTSVisualCaptureSuite=ToonT4A3 -ABTSToonT0BuildId=T4A3-Test"),
			Config,
			&Failure));
	TestEqual(TEXT("Parser selects T4-A3"),
		static_cast<int32>(Config.Suite),
		static_cast<int32>(EABTSToonVisualCaptureSuite::ToonT4A3));
	const TArray<FABTSToonVisualCapturePointDefinition> Points =
		FABTSToonVisualCaptureMath::BuildT4A3Catalogue();
	TestEqual(TEXT("A3.1 has five altitude points"), Points.Num(), 5);
	const EABTSToonVisualCaptureAnchor ExpectedAnchors[] = {
		EABTSToonVisualCaptureAnchor::HighAltitudeGround,
		EABTSToonVisualCaptureAnchor::HighAltitudeCloudTop,
		EABTSToonVisualCaptureAnchor::HighAltitudeTransitionMid,
		EABTSToonVisualCaptureAnchor::HighAltitudeSatellite,
		EABTSToonVisualCaptureAnchor::HighAltitudeSpace
	};
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		TestEqual(TEXT("Altitude diagnostic order is frozen"),
			static_cast<int32>(Points[Index].Anchor),
			static_cast<int32>(ExpectedAnchors[Index]));
		TestEqual(TEXT("Every altitude point retains GroundDay profile"),
			static_cast<int32>(Points[Index].StyleProfile),
			static_cast<int32>(EABTSStylizedRenderProfile::GroundDay));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT4A32EnvironmentProfileAssemblyContractTest,
	"ABTS.Rendering.Toon.T4A3_2.EnvironmentProfileAssemblyContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT4A32EnvironmentProfileAssemblyContractTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FABTSStylizedEnvironmentProfilePolicy Ground =
		FABTSStylizedRenderingControl::GetEnvironmentProfilePolicy(
			EABTSStylizedRenderProfile::GroundDay);
	const FABTSStylizedEnvironmentProfilePolicy Satellite =
		FABTSStylizedRenderingControl::GetEnvironmentProfilePolicy(
			EABTSStylizedRenderProfile::SatelliteGuide);
	const FABTSStylizedEnvironmentProfilePolicy Finale =
		FABTSStylizedRenderingControl::GetEnvironmentProfilePolicy(
			EABTSStylizedRenderProfile::FinaleSpace);
	TestTrue(TEXT("GroundDay formal actor policy validates"), Ground.IsValid());
	TestTrue(TEXT("SatelliteGuide formal actor policy validates"),
		Satellite.IsValid());
	TestTrue(TEXT("FinaleSpace formal actor policy validates"), Finale.IsValid());
	TestTrue(TEXT("Only GroundDay retains spherical atmosphere and clouds"),
		Ground.bSkyAtmosphereVisible && Ground.bLowPolyCloudsVisible
			&& !Satellite.bSkyAtmosphereVisible
			&& !Satellite.bLowPolyCloudsVisible
			&& !Finale.bSkyAtmosphereVisible
			&& !Finale.bLowPolyCloudsVisible);
	TestTrue(TEXT("Global-Z height fog remains disabled in every profile"),
		!Ground.bHeightFogVisible
			&& !Satellite.bHeightFogVisible
			&& !Finale.bHeightFogVisible);
	TestEqual(TEXT("Normal world resolves GroundDay"),
		static_cast<int32>(
			FABTSStylizedRenderingContract::ResolveMainWorldProfile(
				false, EABTSStylizedRenderProfile::GroundDay)),
		static_cast<int32>(EABTSStylizedRenderProfile::GroundDay));
	TestEqual(TEXT("Finale activity has profile precedence"),
		static_cast<int32>(
			FABTSStylizedRenderingContract::ResolveMainWorldProfile(
				true, EABTSStylizedRenderProfile::GroundDay)),
		static_cast<int32>(EABTSStylizedRenderProfile::FinaleSpace));
	TestEqual(TEXT("Finale exit returns to configured GroundDay"),
		static_cast<int32>(
			FABTSStylizedRenderingContract::ResolveMainWorldProfile(
				false, EABTSStylizedRenderProfile::GroundDay)),
		static_cast<int32>(EABTSStylizedRenderProfile::GroundDay));
	const FABTSStylizedViewPolicy SatellitePreview =
		FABTSStylizedRenderingContract::ResolveViewPolicy(
			EABTSStylizedViewClass::SatelliteLandingPreview);
	TestEqual(TEXT("Satellite preview preserves GroundDay surface profile"),
		static_cast<int32>(SatellitePreview.Profile),
		static_cast<int32>(EABTSStylizedRenderProfile::GroundDay));
	TestEqual(TEXT("Satellite preview consumes SatelliteGuide background"),
		static_cast<int32>(SatellitePreview.EnvironmentProfile),
		static_cast<int32>(EABTSStylizedRenderProfile::SatelliteGuide));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT4A1CaptureCatalogueTest,
	"ABTS.Rendering.Toon.T4A1.CaptureCatalogue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT4A1CaptureCatalogueTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FABTSToonVisualCaptureRunConfig Config;
	FString Failure;
	TestTrue(
		TEXT("T4-A1 screenshot mode parses"),
		FABTSToonVisualCaptureRunConfig::Parse(
			TEXT("-ABTSVisualCaptureSuite=ToonT4A1 -ABTSToonT0BuildId=T4A1-Test"),
			Config,
			&Failure));
	TestEqual(TEXT("The parser selects T4-A1"),
		static_cast<int32>(Config.Suite),
		static_cast<int32>(EABTSToonVisualCaptureSuite::ToonT4A1));

	Failure.Reset();
	TestTrue(
		TEXT("T4-A1 is the first T4 suite to allow GPU evidence"),
		FABTSToonVisualCaptureRunConfig::Parse(
			TEXT("-ABTSVisualCaptureSuite=ToonT4A1 -ABTSToonT0Mode=GPU -ABTSToonT0BuildId=T4A1-GPU"),
			Config,
			&Failure));
	TestEqual(TEXT("GPU mode is retained"),
		static_cast<int32>(Config.Mode),
		static_cast<int32>(EABTSToonVisualCaptureMode::GPUProfile));

	const TArray<FABTSToonVisualCapturePointDefinition> Points =
		FABTSToonVisualCaptureMath::BuildT4A1Catalogue();
	const TArray<FABTSToonDiagnosticVariantDefinition> Variants =
		FABTSToonVisualCaptureMath::BuildVariantCatalogue(
			EABTSToonVisualCaptureSuite::ToonT4A1);
	TestEqual(TEXT("T4-A1 adds banding, terminator direction and backlit diagnostics"),
		Points.Num(), 10);
	if (Points.Num() == 10)
	{
		TestEqual(TEXT("Terminator sky diagnostic order is frozen"),
			Points[2].PointId, FName(TEXT("TerminatorSky")));
		TestEqual(TEXT("Bright sky banding diagnostic order is frozen"),
			Points[3].PointId, FName(TEXT("BrightSkyBanding")));
		TestEqual(TEXT("Terminator sunward diagnostic order is frozen"),
			Points[4].PointId, FName(TEXT("TerminatorSunwardSky")));
		TestEqual(TEXT("Terminator anti-sunward diagnostic order is frozen"),
			Points[5].PointId, FName(TEXT("TerminatorAntiSunwardSky")));
		TestEqual(TEXT("Backlit diagnostic order is frozen"),
			Points[7].PointId, FName(TEXT("BacklitBirdParty")));
	}
	TestEqual(TEXT("T4-A1 compares only reversible Off and On states"),
		Variants.Num(), 2);
	if (Variants.Num() == 2)
	{
		TestEqual(TEXT("First variant is StyleOff"),
			Variants[0].VariantId, FName(TEXT("StyleOff")));
		TestEqual(TEXT("Second variant is StyleOn"),
			Variants[1].VariantId, FName(TEXT("StyleOn")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT4A2CloudContractTest,
	"ABTS.Rendering.Toon.T4A2.CloudContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT4A2CloudContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FABTSStylizedEnvironmentParameters Ground =
		FABTSStylizedRenderingControl::BuildEnvironmentParameters(
			FVector::ZeroVector,
			10000.0,
			FVector::UpVector,
			EABTSStylizedRenderProfile::GroundDay);
	const FABTSStylizedEnvironmentParameters Satellite =
		FABTSStylizedRenderingControl::BuildEnvironmentParameters(
			FVector::ZeroVector,
			10000.0,
			FVector::UpVector,
			EABTSStylizedRenderProfile::SatelliteGuide);
	TestTrue(TEXT("Ground cloud contract validates"), Ground.IsValid());
	TestEqual(TEXT("Ground profile enables bounded cloud islands"),
		Ground.bCloudsEnabled, 1u);
	TestTrue(TEXT("Cloud base is above the accepted planet"),
		Ground.CloudBaseAltitudeCM > 0.0f);
	TestTrue(TEXT("Cloud island envelope has finite thickness"),
		Ground.CloudLayerHeightCM > 0.0f);
	TestEqual(TEXT("Satellite profile suppresses the ground cloud actor"),
		Satellite.bCloudsEnabled, 0u);

	FABTSToonVisualCaptureRunConfig Config;
	FString Failure;
	TestTrue(TEXT("T4-A2 capture suite parses"),
		FABTSToonVisualCaptureRunConfig::Parse(
			TEXT("-ABTSVisualCaptureSuite=ToonT4A2 -ABTSToonT0BuildId=T4A2-Test"),
			Config,
			&Failure));
	TestEqual(TEXT("Parser selects T4-A2"),
		static_cast<int32>(Config.Suite),
		static_cast<int32>(EABTSToonVisualCaptureSuite::ToonT4A2));
	const TArray<FABTSToonVisualCapturePointDefinition> CloudCatalogue =
		FABTSToonVisualCaptureMath::BuildT4A2Catalogue();
	TestEqual(TEXT("T4-A2 keeps ten atmosphere poses, seven A2.1 views, five A2.2 field views and four A2.3 traversal views"),
		CloudCatalogue.Num(), 26);
	TestTrue(TEXT("T4-A2 includes the gameplay-facing ground oblique-up view"),
		CloudCatalogue.ContainsByPredicate(
			[](const FABTSToonVisualCapturePointDefinition& Point)
			{
				return Point.Anchor
					== EABTSToonVisualCaptureAnchor::CloudR0GroundObliqueUp;
			}));
	TestTrue(TEXT("T4-A2 includes the gameplay-facing ground zenith view"),
		CloudCatalogue.ContainsByPredicate(
			[](const FABTSToonVisualCapturePointDefinition& Point)
			{
				return Point.Anchor
					== EABTSToonVisualCaptureAnchor::CloudR0GroundZenith;
			}));
	TestTrue(TEXT("T4-A2.2 includes the deterministic global field composition"),
		CloudCatalogue.ContainsByPredicate(
			[](const FABTSToonVisualCapturePointDefinition& Point)
			{
				return Point.Anchor
					== EABTSToonVisualCaptureAnchor::CloudFieldGlobal;
			}));
	TestTrue(TEXT("T4-A2.2 includes the neighbouring-cloud fusion composition"),
		CloudCatalogue.ContainsByPredicate(
			[](const FABTSToonVisualCapturePointDefinition& Point)
			{
				return Point.Anchor
					== EABTSToonVisualCaptureAnchor::CloudFieldFusion;
			}));
	TestTrue(TEXT("T4-A2.2 includes the size and silhouette variety composition"),
		CloudCatalogue.ContainsByPredicate(
			[](const FABTSToonVisualCapturePointDefinition& Point)
			{
				return Point.Anchor
					== EABTSToonVisualCaptureAnchor::CloudFieldVariety;
			}));
	TestTrue(TEXT("T4-A2.2 includes a deep-night cloud-lighting diagnostic"),
		CloudCatalogue.ContainsByPredicate(
			[](const FABTSToonVisualCapturePointDefinition& Point)
			{
				return Point.Anchor
					== EABTSToonVisualCaptureAnchor::CloudFieldNight;
			}));
	TestTrue(TEXT("T4-A2.2 includes the connected terminator mega-cluster view"),
		CloudCatalogue.ContainsByPredicate(
			[](const FABTSToonVisualCapturePointDefinition& Point)
			{
				return Point.Anchor
					== EABTSToonVisualCaptureAnchor::CloudFieldTerminatorMega;
			}));
	TestTrue(TEXT("T4-A2.3 includes bird-inside-cloud visibility"),
		CloudCatalogue.ContainsByPredicate(
			[](const FABTSToonVisualCapturePointDefinition& Point)
			{
				return Point.Anchor
					== EABTSToonVisualCaptureAnchor::CloudTraversalBirdInside;
			}));
	TestTrue(TEXT("T4-A2.3 includes camera-inside-cloud visibility"),
		CloudCatalogue.ContainsByPredicate(
			[](const FABTSToonVisualCapturePointDefinition& Point)
			{
				return Point.Anchor
					== EABTSToonVisualCaptureAnchor::CloudTraversalCameraInside;
			}));
	TestTrue(TEXT("T4-A2.3 includes cloud-between-camera-and-bird visibility"),
		CloudCatalogue.ContainsByPredicate(
			[](const FABTSToonVisualCapturePointDefinition& Point)
			{
				return Point.Anchor
					== EABTSToonVisualCaptureAnchor::CloudTraversalBetween;
			}));
	TestTrue(TEXT("T4-A2.3 includes camera-and-bird-both-inside visibility"),
		CloudCatalogue.ContainsByPredicate(
			[](const FABTSToonVisualCapturePointDefinition& Point)
			{
				return Point.Anchor
					== EABTSToonVisualCaptureAnchor::CloudTraversalBothInside;
			}));
	return true;
}

#endif
