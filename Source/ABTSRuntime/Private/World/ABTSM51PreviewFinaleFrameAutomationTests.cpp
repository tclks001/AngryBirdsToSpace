// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "PCG/ABTSM3MonthlyFinaleAnchor.h"
#include "World/ABTSM51PreviewFinaleFrame.h"
#include "World/ABTSM51WorldSystem.h"

namespace
{
FABTSM110FinaleLocalFrame MakeCompatibilityFrame()
{
	FABTSM110FinaleLocalFrame Frame;
	Frame.LayoutVersion = 1;
	Frame.LaunchTaskId = 6;
	Frame.AnchorCellId = 7683;
	Frame.SlotPairId = 91;
	Frame.WorldTransform = FTransform(
		FQuat::Identity,
		FVector(1000.0, 2000.0, 3000.0),
		FVector::OneVector);
	Frame.LeftSlotWorldLocation =
		Frame.GetOrigin() - Frame.GetRight() * 105.0;
	Frame.RightSlotWorldLocation =
		Frame.GetOrigin() + Frame.GetRight() * 105.0;
	Frame.bValid = true;
	return Frame;
}

FABTSM3MonthlyFinaleAnchorPreview MakePreview()
{
	const FVector Up = FVector(0.36, -0.48, 0.8).GetSafeNormal();
	const FVector Forward = FVector::VectorPlaneProject(
		FVector(0.8, 0.6, 0.1),
		Up).GetSafeNormal();
	const FVector Right = FVector::CrossProduct(Up, Forward).GetSafeNormal();
	const FVector Origin(4200.0, -7300.0, 8100.0);

	FABTSM3MonthlyFinaleAnchorPreview Preview;
	Preview.SourceRouteCandidateId = 4;
	Preview.SourceSpatialCandidateHash =
		static_cast<int64>(0x16A44AF72C58261Eull);
	Preview.SourcePlanCandidateHash =
		static_cast<int64>(0x24B8D61AA973CC31ull);
	Preview.SourcePlanResultHash =
		static_cast<int64>(0x7EC967908A36B3A1ull);
	Preview.RoadTerminalCellId = 7012;
	Preview.AnchorCellId = 6998;
	Preview.LeftSlotNearestCellId = 6997;
	Preview.RightSlotNearestCellId = 6999;
	Preview.FrameOriginWorld = Origin;
	Preview.ForwardWorld = Forward;
	Preview.RightWorld = Right;
	Preview.UpWorld = Up;
	Preview.AnchorSurfaceWorld = Origin - Up * 4.0;
	Preview.LeftSlotSurfaceWorld = Origin - Right * 105.0 - Up * 4.0;
	Preview.RightSlotSurfaceWorld = Origin + Right * 105.0 - Up * 4.0;
	Preview.LeftSlotWorldLocation = Origin - Right * 105.0;
	Preview.RightSlotWorldLocation = Origin + Right * 105.0;
	Preview.ActualSlotSeparationCM = 210.0f;
	Preview.MaxResolvedSurfaceSlopeDegrees = 5.0f;
	Preview.bPreviewValid = true;
	Preview.bMonthlyWorldAccepted = false;
	Preview.PreviewHash =
		static_cast<int64>(0xA596D320726B3501ull);
	return Preview;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM51PreviewFinaleFrameAdapterTest,
	"ABTS.Integration.PreviewFinaleFrame.Adapter",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM51PreviewFinaleFrameAdapterTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FABTSM110FinaleLocalFrame Compatibility =
		MakeCompatibilityFrame();
	const FABTSM3MonthlyFinaleAnchorPreview Preview = MakePreview();
	FABTSM51PreviewFinaleFrameContext First;
	FABTSM51PreviewFinaleFrameContext Second;
	FString Failure;
	TestTrue(TEXT("Valid preview adapts"),
		FABTSM51PreviewFinaleFrameAdapter::Build(
			Preview, Compatibility, First, Failure));
	TestTrue(TEXT("Adapted context is usable"), First.IsUsable());
	TestEqual(TEXT("Candidate identity is retained"),
		First.SourceRouteCandidateId, Preview.SourceRouteCandidateId);
	TestEqual(TEXT("Anchor moves to monthly road terminal proposal"),
		First.Frame.AnchorCellId, Preview.AnchorCellId);
	TestEqual(TEXT("Compatibility launch identity is retained"),
		First.Frame.LaunchTaskId, Compatibility.LaunchTaskId);
	TestEqual(TEXT("Compatibility pair identity is retained"),
		First.Frame.SlotPairId, Compatibility.SlotPairId);
	TestEqual(TEXT("Production frame remains unchanged"),
		Compatibility.AnchorCellId, 7683);
	TestTrue(TEXT("Preview frame origin is exact"),
		First.Frame.GetOrigin().Equals(Preview.FrameOriginWorld, 1.0e-3));
	TestTrue(TEXT("Preview frame right matches slot direction"),
		FVector::DotProduct(First.Frame.GetRight(), Preview.RightWorld)
			> 1.0 - 1.0e-3);

	Failure.Reset();
	TestTrue(TEXT("Repeated adaptation succeeds"),
		FABTSM51PreviewFinaleFrameAdapter::Build(
			Preview, Compatibility, Second, Failure));
	TestEqual(TEXT("Context hash is deterministic"),
		First.ContextHash, Second.ContextHash);

	FABTSM3MonthlyFinaleAnchorPreview AcceptedPreview = Preview;
	AcceptedPreview.bMonthlyWorldAccepted = true;
	Failure.Reset();
	TestFalse(TEXT("MonthlyAccepted cannot enter Preview/Test adapter"),
		FABTSM51PreviewFinaleFrameAdapter::Build(
			AcceptedPreview, Compatibility, Second, Failure));
	TestEqual(TEXT("Accepted preview rejection is explicit"), Failure,
		FString(TEXT("InvalidM3FinaleAnchorPreview")));

	FABTSM3MonthlyFinaleAnchorPreview SplitSlots = Preview;
	SplitSlots.FrameOriginWorld += FVector(1.0, 0.0, 0.0);
	Failure.Reset();
	TestFalse(TEXT("Split slot/frame identity fails closed"),
		FABTSM51PreviewFinaleFrameAdapter::Build(
			SplitSlots, Compatibility, Second, Failure));
	TestEqual(TEXT("Split rejection is explicit"), Failure,
		FString(TEXT("PreviewSlotFrameMismatch")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM51PreviewFinaleFrameWorldSystemGateTest,
	"ABTS.Integration.PreviewFinaleFrame.WorldSystemGate",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM51PreviewFinaleFrameWorldSystemGateTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FABTSM51PreviewFinaleFrameContext Context;
	FString Failure;
	TestTrue(TEXT("Fixture context builds"),
		FABTSM51PreviewFinaleFrameAdapter::Build(
			MakePreview(), MakeCompatibilityFrame(), Context, Failure));

	AABTSM51WorldSystem* ValidSystem =
		NewObject<AABTSM51WorldSystem>();
	TestTrue(TEXT("Valid pre-BeginPlay configuration succeeds"),
		ValidSystem->ConfigurePreviewFinaleFrame(Context));
	TestNotNull(TEXT("Valid context is exposed"),
		ValidSystem->GetPreviewFinaleFrameContext());
	TestNotNull(TEXT("Valid frame becomes active"),
		ValidSystem->GetActiveFinaleFrame());
	TestEqual(TEXT("Active frame uses preview anchor"),
		ValidSystem->GetActiveFinaleFrame()->AnchorCellId,
		Context.Frame.AnchorCellId);

	AABTSM51WorldSystem* InvalidSystem =
		NewObject<AABTSM51WorldSystem>();
	AddExpectedError(
		TEXT("invalid authority, identity, frame, or hash"),
		EAutomationExpectedErrorFlags::Contains,
		1);
	TestFalse(TEXT("Invalid requested context is rejected"),
		InvalidSystem->ConfigurePreviewFinaleFrame(
			FABTSM51PreviewFinaleFrameContext()));
	TestNull(TEXT("Rejected request cannot fall back to compatibility context"),
		InvalidSystem->GetPreviewFinaleFrameContext());
	TestNull(TEXT("Rejected request has no active finale frame"),
		InvalidSystem->GetActiveFinaleFrame());
	return true;
}

#endif
