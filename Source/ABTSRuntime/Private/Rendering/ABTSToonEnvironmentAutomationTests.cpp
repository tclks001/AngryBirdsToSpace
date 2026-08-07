// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Rendering/ABTSStylizedRenderingControl.h"
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

#endif
