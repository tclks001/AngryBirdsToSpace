// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/ABTSM11FinaleFlightCamera.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "SceneUtils.h"
#include "World/ABTSM11GravityAssistTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11CFlightCameraAuthorityFrameTest,
	"ABTS.M11C.Unit.FlightCameraAuthorityFrame",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM11CFlightCameraAuthorityFrameTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const int32 PreviousM2 = ABTSM11FinaleCameraDirector::IsM2Enabled()
		? 1 : 0;
	const int32 PreviousM3 = ABTSM11FinaleCameraDirector::IsM3Enabled()
		? 1 : 0;
	const int32 PreviousProductionOverride =
		ABTSM11FinaleCameraDirector::GetProductionModeOverride();
	ABTSM11FinaleCameraDirector::SetM2Enabled(false);
	ABTSM11FinaleCameraDirector::SetM3Enabled(false);
	ABTSM11FinaleCameraDirector::SetProductionModeOverride(-1);
	TestEqual(
		TEXT("Normal production auto mode consumes the existing M3 director"),
		static_cast<uint8>(
			ABTSM11FinaleCameraDirector::ResolveProductionDirectorMode()),
		static_cast<uint8>(
			EABTSM11FinaleCameraDirectorMode::MultiAssistM3));
	ABTSM11FinaleCameraDirector::SetProductionModeOverride(0);
	TestEqual(TEXT("Explicit console Legacy override wins over production auto"),
		static_cast<uint8>(
			ABTSM11FinaleCameraDirector::ResolveProductionDirectorMode()),
		static_cast<uint8>(EABTSM11FinaleCameraDirectorMode::Legacy));
	ABTSM11FinaleCameraDirector::SetProductionModeOverride(1);
	TestEqual(TEXT("Explicit console M2 override remains available"),
		static_cast<uint8>(
			ABTSM11FinaleCameraDirector::ResolveProductionDirectorMode()),
		static_cast<uint8>(
			EABTSM11FinaleCameraDirectorMode::Assist1OnlyM2));
	ABTSM11FinaleCameraDirector::SetProductionModeOverride(2);
	TestEqual(TEXT("Explicit console M3 override remains available"),
		static_cast<uint8>(
			ABTSM11FinaleCameraDirector::ResolveProductionDirectorMode()),
		static_cast<uint8>(
			EABTSM11FinaleCameraDirectorMode::MultiAssistM3));
	TestEqual(TEXT("Capture false/false remains explicit Legacy"),
		static_cast<uint8>(
			ABTSM11FinaleCameraDirector::ResolveCaptureDirectorMode(false, false)),
		static_cast<uint8>(EABTSM11FinaleCameraDirectorMode::Legacy));
	TestEqual(TEXT("Capture M2 remains explicit Assist1 mode"),
		static_cast<uint8>(
			ABTSM11FinaleCameraDirector::ResolveCaptureDirectorMode(true, false)),
		static_cast<uint8>(
			EABTSM11FinaleCameraDirectorMode::Assist1OnlyM2));
	TestEqual(TEXT("Capture M3 remains explicit multi-assist mode"),
		static_cast<uint8>(
			ABTSM11FinaleCameraDirector::ResolveCaptureDirectorMode(false, true)),
		static_cast<uint8>(
			EABTSM11FinaleCameraDirectorMode::MultiAssistM3));
	ABTSM11FinaleCameraDirector::SetProductionModeOverride(
		PreviousProductionOverride);
	ABTSM11FinaleCameraDirector::SetM2Enabled(PreviousM2 != 0);
	ABTSM11FinaleCameraDirector::SetM3Enabled(PreviousM3 != 0);
	const FVector Target(100.0, 200.0, 300.0);
	FABTSM11FinaleFlightCameraFrame Frame;
	TestTrue(
		TEXT("Initial authority tangent builds a usable frame"),
		ABTSM11FinaleFlightCameraMath::BuildDesiredFrame(
			Target,
			FVector::ForwardVector,
			FVector::UpVector,
			FVector::ZeroVector,
			FVector::ZeroVector,
			false,
			920.0,
			310.0,
			80.0,
			80.0,
			Frame));
	TestTrue(
		TEXT("Initial camera location is behind and above authority sample"),
		Frame.DesiredTransform.GetLocation().Equals(
			Target
				- FVector::ForwardVector * 920.0
				+ FVector::UpVector * 310.0,
			1.0e-3));
	TestTrue(
		TEXT("Initial transported Up is orthogonal to tangent"),
		FMath::Abs(FVector::DotProduct(
			Frame.TrajectoryForward,
			Frame.TransportedUp)) <= 1.0e-5);

	FVector PreviousForward = Frame.TrajectoryForward;
	FVector PreviousUp = Frame.TransportedUp;
	for (int32 Index = 1; Index <= 90; ++Index)
	{
		const double Alpha = static_cast<double>(Index) / 90.0;
		const double YawRadians =
			FMath::DegreesToRadians(100.0 * Alpha);
		const double PitchRadians =
			FMath::DegreesToRadians(35.0 * Alpha);
		const FVector Tangent(
			FMath::Cos(PitchRadians) * FMath::Cos(YawRadians),
			FMath::Cos(PitchRadians) * FMath::Sin(YawRadians),
			FMath::Sin(PitchRadians));
		FABTSM11FinaleFlightCameraFrame Next;
		if (!TestTrue(
			TEXT("Every curved authority sample builds a finite frame"),
			ABTSM11FinaleFlightCameraMath::BuildDesiredFrame(
				Target + Tangent * Index * 100.0,
				Tangent,
				FVector::UpVector,
				PreviousForward,
				PreviousUp,
				true,
				920.0,
				310.0,
				80.0,
				80.0,
				Next)))
		{
			return false;
		}
		TestTrue(
			TEXT("Parallel-transported Up stays orthogonal"),
			FMath::Abs(FVector::DotProduct(
				Next.TrajectoryForward,
				Next.TransportedUp)) <= 1.0e-4);
		TestTrue(
			TEXT("Parallel transport does not flip camera Up"),
			FVector::DotProduct(
				PreviousUp,
				Next.TransportedUp) > 0.0);
		PreviousForward = Next.TrajectoryForward;
		PreviousUp = Next.TransportedUp;
	}

	FABTSM11FinaleFlightCameraFrame Rejected;
	TestFalse(
		TEXT("A zero authority tangent is rejected"),
		ABTSM11FinaleFlightCameraMath::BuildDesiredFrame(
			Target,
			FVector::ZeroVector,
			FVector::UpVector,
			PreviousForward,
			PreviousUp,
			true,
			920.0,
			310.0,
			80.0,
			80.0,
			Rejected));

	FABTSM11TrajectoryResult EventResult;
	EventResult.ValidationHash = 1;
	EventResult.CompletedAssistCount =
		FABTSM11GravityScenario::AssistCount;
	for (int32 AssistIndex = 1;
		AssistIndex <= FABTSM11GravityScenario::AssistCount;
		++AssistIndex)
	{
		for (int32 EventIndex = 0; EventIndex < 3; ++EventIndex)
		{
			FABTSM11TrajectoryEvent& Event =
				EventResult.Events.AddDefaulted_GetRef();
			Event.AssistIndex = AssistIndex;
			Event.Type = static_cast<EABTSM11TrajectoryEventType>(EventIndex);
			Event.TimeSeconds = AssistIndex * 10.0 + EventIndex * 2.0;
		}
	}
	const FABTSM11FinaleCameraStageSelection Approach =
		ABTSM11FinaleCameraDirector::ResolveStage(
			true,
			false,
			11.0,
			&EventResult);
	TestTrue(TEXT("Assist1 approach is the M2 window"),
		Approach.IsM2Assist1Window());
	TestEqual(
		TEXT("Approach progress is event-derived"),
		Approach.StageProgress,
		0.5,
		1.0e-9);
	const FABTSM11FinaleCameraStageSelection Cruise =
		ABTSM11FinaleCameraDirector::ResolveStage(
			true,
			false,
			5.0,
			&EventResult);
	TestTrue(
		TEXT("Assist1 cruise is eligible for the M2 lead-in"),
		Cruise.IsM2Assist1Window());
	TestEqual(
		TEXT("Cruise progress is event-derived"),
		Cruise.StageProgress,
		0.5,
		1.0e-9);
	const FABTSM11FinaleCameraStageSelection Periapsis =
		ABTSM11FinaleCameraDirector::ResolveStage(
			true,
			false,
			13.0,
			&EventResult);
	TestEqual(
		TEXT("Periapsis progress is event-derived"),
		Periapsis.StageProgress,
		0.5,
		1.0e-9);
	const FABTSM11FinaleCameraShotSettings M4ShotSettings;
	const FABTSM11FinaleCameraStageSelection BeforeTerminalAcquire =
		ABTSM11FinaleCameraDirector::ResolveStage(
			true,
			false,
			32.40,
			&EventResult,
			true,
			&M4ShotSettings);
	TestEqual(
		TEXT("Assist3 foreground transit retains Authority before clear"),
		static_cast<uint8>(BeforeTerminalAcquire.ShotPhase),
		static_cast<uint8>(EABTSM11FinaleCameraShotPhase::Authority));
	const FABTSM11FinaleCameraStageSelection DuringTerminalAcquire =
		ABTSM11FinaleCameraDirector::ResolveStage(
			true,
			false,
			33.0,
			&EventResult,
			true,
			&M4ShotSettings);
	TestTrue(
		TEXT("Assist3 departure enters the M4 terminal acquire chain"),
		DuringTerminalAcquire.IsM4TerminalTransition());
	TestEqual(
		TEXT("M4 acquire uses normalized Assist3 clearance-to-exit progress"),
		DuringTerminalAcquire.ShotPhaseProgress,
		(0.5 - M4ShotSettings.ForegroundTransitClearProgress)
			/ (1.0 - M4ShotSettings.ForegroundTransitClearProgress),
		1.0e-9);
	FABTSM11FinaleCameraStageSelection M4FinalApproach =
		ABTSM11FinaleCameraDirector::ResolveStage(
			true,
			false,
			34.001,
			&EventResult,
			true,
			&M4ShotSettings);
	TestTrue(
		TEXT("Assist3 exit hands directly to the UFO terminal window"),
		M4FinalApproach.IsM4TerminalWindow());
	TestEqual(
		TEXT("FinalApproach retains the terminal two-subject solver"),
		static_cast<uint8>(M4FinalApproach.ShotPhase),
		static_cast<uint8>(EABTSM11FinaleCameraShotPhase::TerminalTrack));
	TestTrue(
		TEXT("Playback plan supplies the M4 terminal closure timeline"),
		ABTSM11FinaleCameraDirector::ApplyM4TerminalTimeline(
			34.0,
			34.0,
			38.0,
			M4FinalApproach));
	TestEqual(
		TEXT("M4 terminal closure starts at zero progress"),
		M4FinalApproach.StageProgress,
		0.0,
		1.0e-9);
	TestEqual(
		TEXT("Candidate-qualified terminal authority has an explicit label"),
		FString(ABTSM11FinaleCameraDirector::EndpointAuthorityLabel(
			EABTSM11FinaleCameraEndpointAuthority::CandidateQualified)),
		FString(TEXT("CandidateQualified")));
	FVector EncounterScreenRight;
	FVector EncounterScreenUp;
	TestTrue(
		TEXT("Frozen authority events build a chronological encounter basis"),
		ABTSM11FinaleCameraDirector::BuildAssistEncounterBasis(
			FVector::ZeroVector,
			FVector(-5000.0, 3000.0, 0.0),
			FVector(0.0, 3000.0, 0.0),
			FVector(1000.0, 0.0, 0.0),
			FVector(5000.0, 3000.0, 0.0),
			EncounterScreenRight,
			EncounterScreenUp));
	TestTrue(
		TEXT("Encounter chronology maps to screen-right"),
		EncounterScreenRight.Equals(FVector::ForwardVector, 1.0e-6));
	TestTrue(
		TEXT("Encounter keeps the closest radial in camera depth"),
		EncounterScreenUp.Equals(FVector::UpVector, 1.0e-6));

	FABTSM11FinaleCameraDirectorSample DirectorSample;
	DirectorSample.Selection = Approach;
	DirectorSample.TargetCenter = FVector(5000.0, 3000.0, 0.0);
	DirectorSample.TargetRadiusCM = 1000.0;
	DirectorSample.BirdRadiusCM = 60.0;
	DirectorSample.EncounterScreenRight = EncounterScreenRight;
	DirectorSample.EncounterScreenUp = EncounterScreenUp;
	FTransform DirectedTransform;
	FABTSM11FinaleCameraM2Settings M2Settings;
	FABTSM11FinaleCameraM2Diagnostics Diagnostics;
	const FVector M4BirdPosition(1000.0, 0.0, 0.0);
	const FVector M4PlanetCenter(5000.0, 0.0, 0.0);
	const FVector M4TargetCenter(11000.0, 0.0, 0.0);
	FABTSM11FinaleCameraDirectorSample M4AcquireSample;
	M4AcquireSample.Selection = DuringTerminalAcquire;
	M4AcquireSample.Selection.StageProgress =
		M4ShotSettings.ForegroundTransitClearProgress;
	M4AcquireSample.Selection.ShotProgress = 0.0;
	M4AcquireSample.Selection.ShotPhaseProgress = 0.0;
	M4AcquireSample.TargetCenter = M4PlanetCenter;
	M4AcquireSample.TargetRadiusCM = 1000.0;
	M4AcquireSample.BirdRadiusCM = 60.0;
	M4AcquireSample.EncounterScreenRight = FVector::ForwardVector;
	M4AcquireSample.EncounterScreenUp = FVector::UpVector;
	M4AcquireSample.TerminalScreenRight = FVector::ForwardVector;
	M4AcquireSample.TerminalScreenUp = FVector::UpVector;
	M4AcquireSample.TerminalTargetCenter = M4TargetCenter;
	M4AcquireSample.TerminalTargetRadiusCM = 800.0;
	FABTSM11FinaleCameraDirectorSample M4AuthoritySample = M4AcquireSample;
	M4AuthoritySample.Selection.ShotPhase =
		EABTSM11FinaleCameraShotPhase::Authority;
	M4AuthoritySample.Selection.ShotReason = TEXT("AuthorityStage");
	M4AuthoritySample.Selection.bTerminalTransition = false;
	FTransform M4AuthorityTransform;
	FABTSM11FinaleCameraM2Diagnostics M4AuthorityDiagnostics;
	FTransform M4AcquireStartTransform;
	FABTSM11FinaleCameraM2Diagnostics M4AcquireStartDiagnostics;
	TestTrue(
		TEXT("M4 clearance Authority frame builds"),
		ABTSM11FinaleFlightCameraMath::BuildM3AssistFrame(
			Frame,
			M4BirdPosition,
			M4AuthoritySample,
			M2Settings,
			M4AuthorityTransform,
			M4AuthorityDiagnostics));
	TestTrue(
		TEXT("M4 terminal acquire start frame builds"),
		ABTSM11FinaleFlightCameraMath::BuildM3AssistFrame(
			Frame,
			M4BirdPosition,
			M4AcquireSample,
			M2Settings,
			M4AcquireStartTransform,
			M4AcquireStartDiagnostics));
	TestTrue(
		TEXT("Terminal acquire begins at the exact Lucy transform"),
		M4AuthorityTransform.Equals(M4AcquireStartTransform, 1.0e-5));
	TestEqual(
		TEXT("Terminal acquire begins at the exact Lucy FOV"),
		M4AcquireStartDiagnostics.DirectedFovDegrees,
		M4AuthorityDiagnostics.DirectedFovDegrees,
		1.0e-9);

	M4AcquireSample.Selection.StageProgress = 1.0;
	M4AcquireSample.Selection.ShotProgress = 1.0;
	M4AcquireSample.Selection.ShotPhaseProgress = 1.0;
	FABTSM11FinaleCameraDirectorSample M4TrackSample = M4AcquireSample;
	M4TrackSample.Selection = M4FinalApproach;
	M4TrackSample.TargetCenter = M4TargetCenter;
	M4TrackSample.TargetRadiusCM = 800.0;
	FTransform M4AcquireEndTransform;
	FABTSM11FinaleCameraM2Diagnostics M4AcquireEndDiagnostics;
	FTransform M4TrackTransform;
	FABTSM11FinaleCameraM2Diagnostics M4TrackDiagnostics;
	TestTrue(
		TEXT("M4 terminal acquire end frame builds"),
		ABTSM11FinaleFlightCameraMath::BuildM3AssistFrame(
			Frame,
			M4BirdPosition,
			M4AcquireSample,
			M2Settings,
			M4AcquireEndTransform,
			M4AcquireEndDiagnostics));
	TestTrue(
		TEXT("M4 final approach frame builds"),
		ABTSM11FinaleFlightCameraMath::BuildM3AssistFrame(
			Frame,
			M4BirdPosition,
			M4TrackSample,
			M2Settings,
			M4TrackTransform,
			M4TrackDiagnostics));
	TestTrue(
		TEXT("Assist3 exit matches the exact FinalApproach transform"),
		M4AcquireEndTransform.Equals(M4TrackTransform, 1.0e-5));
	TestEqual(
		TEXT("Assist3 exit matches the exact FinalApproach FOV"),
		M4AcquireEndDiagnostics.DirectedFovDegrees,
		M4TrackDiagnostics.DirectedFovDegrees,
		1.0e-9);
	const FVector M4Forward =
		M4TrackTransform.GetRotation().GetForwardVector();
	const FVector BirdRelative =
		M4BirdPosition - M4TrackTransform.GetLocation();
	const FVector TargetRelative =
		M4TargetCenter - M4TrackTransform.GetLocation();
	TestTrue(
		TEXT("Terminal bird remains in front of the camera"),
		FVector::DotProduct(BirdRelative, M4Forward) > 0.0);
	TestTrue(
		TEXT("Terminal UFO remains in front of the camera"),
		FVector::DotProduct(TargetRelative, M4Forward) > 0.0);
	const auto ProjectM4Ndc = [&] (
		const FTransform& Transform,
		const FVector& Position)
	{
		const FVector Relative = Position - Transform.GetLocation();
		const double Depth = FVector::DotProduct(
			Relative,
			Transform.GetRotation().GetForwardVector());
		const double TanHalfHorizontal = FMath::Tan(
			FMath::DegreesToRadians(
				M2Settings.TerminalFovDegrees * 0.5));
		const double TanHalfVertical = TanHalfHorizontal / (16.0 / 9.0);
		return FVector2D(
			FVector::DotProduct(
				Relative,
				Transform.GetRotation().GetRightVector())
				/ (Depth * TanHalfHorizontal),
			FVector::DotProduct(
				Relative,
				Transform.GetRotation().GetUpVector())
				/ (Depth * TanHalfVertical));
	};
	const FVector2D M4StartBirdNdc = ProjectM4Ndc(
		M4TrackTransform,
		M4BirdPosition);
	const FVector2D M4StartTargetNdc = ProjectM4Ndc(
		M4TrackTransform,
		M4TargetCenter);
	TestTrue(
		TEXT("Terminal UFO begins exactly at screen centre"),
		M4StartTargetNdc.IsNearlyZero(1.0e-6));
	TestEqual(
		TEXT("Terminal bird begins on the lower-screen anchor"),
		M4StartBirdNdc.Y,
		M2Settings.TerminalBirdStartNdcY,
		1.0e-6);
	TestEqual(
		TEXT("Terminal bird shares the UFO vertical centreline"),
		M4StartBirdNdc.X,
		0.0,
		1.0e-6);
	TestEqual(
		TEXT("Terminal dolly starts at the configured bird distance"),
		FVector::Distance(
			M4TrackTransform.GetLocation(),
			M4BirdPosition),
		M2Settings.TerminalStartBirdDistanceCM,
		1.0e-3);

	FABTSM11FinaleCameraDirectorSample M4MidTrackSample = M4TrackSample;
	M4MidTrackSample.Selection.StageProgress = 0.5;
	const FVector M4MidBirdPosition(6000.0, 0.0, 0.0);
	FTransform M4MidTrackTransform;
	FABTSM11FinaleCameraM2Diagnostics M4MidTrackDiagnostics;
	TestTrue(
		TEXT("M4 midpoint closure frame builds"),
		ABTSM11FinaleFlightCameraMath::BuildM3AssistFrame(
			Frame,
			M4MidBirdPosition,
			M4MidTrackSample,
			M2Settings,
			M4MidTrackTransform,
			M4MidTrackDiagnostics));
	FABTSM11FinaleCameraDirectorSample M4ContactTrackSample = M4TrackSample;
	M4ContactTrackSample.Selection.StageProgress = 1.0;
	const FVector M4ContactBirdPosition(10200.0, 0.0, 0.0);
	FTransform M4ContactTrackTransform;
	FABTSM11FinaleCameraM2Diagnostics M4ContactTrackDiagnostics;
	TestTrue(
		TEXT("M4 800 cm contact closure frame builds"),
		ABTSM11FinaleFlightCameraMath::BuildM3AssistFrame(
			Frame,
			M4ContactBirdPosition,
			M4ContactTrackSample,
			M2Settings,
			M4ContactTrackTransform,
			M4ContactTrackDiagnostics));
	const FVector2D M4MidBirdNdc = ProjectM4Ndc(
		M4MidTrackTransform,
		M4MidBirdPosition);
	const FVector2D M4ContactBirdNdc = ProjectM4Ndc(
		M4ContactTrackTransform,
		M4ContactBirdPosition);
	const FVector2D M4MidTargetNdc = ProjectM4Ndc(
		M4MidTrackTransform,
		M4TargetCenter);
	const FVector2D M4ContactTargetNdc = ProjectM4Ndc(
		M4ContactTrackTransform,
		M4TargetCenter);
	TestTrue(
		TEXT("UFO remains centred through the entire closure"),
		M4MidTargetNdc.IsNearlyZero(1.0e-6)
			&& M4ContactTargetNdc.IsNearlyZero(1.0e-6));
	TestEqual(
		TEXT("First half and second half have equal bird screen travel"),
		M4MidBirdNdc.Y - M4StartBirdNdc.Y,
		M4ContactBirdNdc.Y - M4MidBirdNdc.Y,
		1.0e-6);
	TestEqual(
		TEXT("Bird remains below the UFO at physical contact"),
		M4ContactBirdNdc.Y,
		M2Settings.TerminalBirdContactNdcY,
		1.0e-6);
	TestEqual(
		TEXT("Terminal dolly reaches the configured contact distance"),
		FVector::Distance(
			M4ContactTrackTransform.GetLocation(),
			M4ContactBirdPosition),
		M2Settings.TerminalContactBirdDistanceCM,
		1.0e-3);
	DirectorSample.Selection = Cruise;
	DirectorSample.Selection.StageProgress = 0.15;
	TestTrue(
		TEXT("M2 Cruise lead-in start remains mathematically valid"),
		ABTSM11FinaleFlightCameraMath::BuildM2Assist1Frame(
			Frame,
			Target,
			DirectorSample,
			M2Settings,
			DirectedTransform,
			Diagnostics));
	TestEqual(
		TEXT("Cruise lead-in starts at exact legacy blend"),
		Diagnostics.DirectorBlendAlpha,
		0.0,
		1.0e-9);
	DirectorSample.Selection.StageProgress = 0.325;
	TestTrue(
		TEXT("M2 Cruise lead-in midpoint remains valid"),
		ABTSM11FinaleFlightCameraMath::BuildM2Assist1Frame(
			Frame,
			Target,
			DirectorSample,
			M2Settings,
			DirectedTransform,
			Diagnostics));
	TestEqual(
		TEXT("Cruise lead-in midpoint reaches half director blend"),
		Diagnostics.DirectorBlendAlpha,
		0.5,
		1.0e-9);
	DirectorSample.Selection.StageProgress = 0.5;
	TestTrue(
		TEXT("M2 Cruise lead-in completes before Assist1 enter"),
		ABTSM11FinaleFlightCameraMath::BuildM2Assist1Frame(
			Frame,
			Target,
			DirectorSample,
			M2Settings,
			DirectedTransform,
			Diagnostics));
	TestEqual(
		TEXT("Cruise lead-in reaches full director blend"),
		Diagnostics.DirectorBlendAlpha,
		1.0,
		1.0e-9);
	TestTrue(
		TEXT("Cruise is still outside while the director reaches full weight"),
		Diagnostics.TransitScreenXInTargetRadii <
			-M2Settings.TransitEntryOffsetRadii);
	TestTrue(
		TEXT("Cruise motion advances during the director blend"),
		Diagnostics.TransitScreenXInTargetRadii >
			-M2Settings.TransitCruiseFarOffsetRadii);
	DirectorSample.Selection.StageProgress =
		M2Settings.CruiseLeadInStartFraction;
	TestTrue(
		TEXT("M2 Cruise reveal starts from the far screen-left mark"),
		ABTSM11FinaleFlightCameraMath::BuildM2Assist1Frame(
			Frame,
			Target,
			DirectorSample,
			M2Settings,
			DirectedTransform,
			Diagnostics));
	TestEqual(
		TEXT("Cruise reveal owns the exact far offset"),
		Diagnostics.TransitScreenXInTargetRadii,
		-M2Settings.TransitCruiseFarOffsetRadii,
		1.0e-9);
	TestEqual(
		TEXT("Cruise preserves the baseline lens"),
		Diagnostics.DirectedFovDegrees,
		M2Settings.BaselineFovDegrees,
		1.0e-9);
	DirectorSample.Selection.StageProgress = 1.0;
	TestTrue(
		TEXT("M2 Cruise motion reaches the Approach entry offset"),
		ABTSM11FinaleFlightCameraMath::BuildM2Assist1Frame(
			Frame,
			Target,
			DirectorSample,
			M2Settings,
			DirectedTransform,
			Diagnostics));
	TestEqual(
		TEXT("Cruise continuously approaches the left planet limb"),
		Diagnostics.TransitScreenXInTargetRadii,
		-M2Settings.TransitEntryOffsetRadii,
		1.0e-9);

	DirectorSample.Selection = Approach;
	TestTrue(
		TEXT("M2 builds a finite Assist1 dual-subject frame"),
		ABTSM11FinaleFlightCameraMath::BuildM2Assist1Frame(
			Frame,
			Target,
			DirectorSample,
			M2Settings,
			DirectedTransform,
			Diagnostics));
	TestEqual(TEXT("Mid-approach reaches full director blend"),
		Diagnostics.DirectorBlendAlpha, 1.0, 1.0e-9);
	TestEqual(
		TEXT("Mid-approach narrows the lens halfway"),
		Diagnostics.DirectedFovDegrees,
		FMath::Lerp(
			M2Settings.BaselineFovDegrees,
			M2Settings.ClosestFovDegrees,
			0.5),
		1.0e-9);
	const FVector DirectedForward =
		DirectedTransform.GetRotation().GetForwardVector();
	TestTrue(
		TEXT("Directed camera keeps bird in front"),
		FVector::DotProduct(
			Target - DirectedTransform.GetLocation(),
			DirectedForward) > 0.0);
	TestTrue(
		TEXT("Directed camera keeps Assist1 in front"),
		FVector::DotProduct(
			DirectorSample.TargetCenter - DirectedTransform.GetLocation(),
			DirectedForward) > 0.0);
	const FVector CameraToBirdDirection =
		(Target - DirectedTransform.GetLocation()).GetSafeNormal();
	const FVector CameraToTargetDirection =
		(DirectorSample.TargetCenter
			- DirectedTransform.GetLocation()).GetSafeNormal();
	TestTrue(
		TEXT("Approach introduces deliberate foreground transit parallax"),
		FVector::DotProduct(
			CameraToBirdDirection,
			CameraToTargetDirection) < 0.9999);
	FQuat OffsetCompositionRotation;
	TestTrue(
		TEXT("M2 can re-aim after location smoothing introduces parallax"),
		ABTSM11FinaleFlightCameraMath::BuildM2PlanetAnchoredRotation(
			DirectedTransform.GetLocation() + FVector(0.0, 1500.0, 0.0),
			Frame.TransportedUp,
			DirectorSample.TargetCenter,
			OffsetCompositionRotation));
	TestTrue(
		TEXT("Location-aware M2 rotation stays normalized"),
		OffsetCompositionRotation.IsNormalized());

	DirectorSample.Selection.Stage = EABTSM11FinaleCameraStage::Approach;
	DirectorSample.Selection.StageProgress = 1.0;
	FTransform ApproachBoundaryTransform;
	FABTSM11FinaleCameraM2Diagnostics ApproachBoundaryDiagnostics;
	TestTrue(
		TEXT("M2 builds the Approach side of the Closest boundary"),
		ABTSM11FinaleFlightCameraMath::BuildM2Assist1Frame(
			Frame,
			Target,
			DirectorSample,
			M2Settings,
			ApproachBoundaryTransform,
			ApproachBoundaryDiagnostics));
	DirectorSample.Selection.Stage = EABTSM11FinaleCameraStage::Periapsis;
	DirectorSample.Selection.StageProgress = 0.0;
	FTransform PeriapsisBoundaryTransform;
	FABTSM11FinaleCameraM2Diagnostics PeriapsisBoundaryDiagnostics;
	TestTrue(
		TEXT("M2 builds the Periapsis side of the Closest boundary"),
		ABTSM11FinaleFlightCameraMath::BuildM2Assist1Frame(
			Frame,
			Target,
			DirectorSample,
			M2Settings,
			PeriapsisBoundaryTransform,
			PeriapsisBoundaryDiagnostics));
	TestTrue(
		TEXT("Closest boundary keeps camera location continuous"),
		ApproachBoundaryTransform.GetLocation().Equals(
			PeriapsisBoundaryTransform.GetLocation(),
			1.0e-6));
	TestTrue(
		TEXT("Closest boundary keeps camera rotation continuous"),
		ApproachBoundaryTransform.GetRotation().AngularDistance(
			PeriapsisBoundaryTransform.GetRotation()) <= 1.0e-6);
	TestEqual(
		TEXT("Closest boundary keeps the same partial retreat"),
		ApproachBoundaryDiagnostics.RetreatAlpha,
		PeriapsisBoundaryDiagnostics.RetreatAlpha,
		1.0e-9);
	TestEqual(
		TEXT("Closest boundary keeps the same foreground transit X"),
		ApproachBoundaryDiagnostics.TransitScreenXInTargetRadii,
		PeriapsisBoundaryDiagnostics.TransitScreenXInTargetRadii,
		1.0e-9);
	TestEqual(
		TEXT("Closest boundary reaches the intended screen-right offset"),
		ApproachBoundaryDiagnostics.TransitScreenXInTargetRadii,
		M2Settings.TransitClosestOffsetRadii,
		1.0e-9);
	TestEqual(
		TEXT("Approach reaches the close lens at Closest"),
		ApproachBoundaryDiagnostics.DirectedFovDegrees,
		M2Settings.ClosestFovDegrees,
		1.0e-9);
	TestEqual(
		TEXT("Periapsis inherits the same close lens"),
		PeriapsisBoundaryDiagnostics.DirectedFovDegrees,
		M2Settings.ClosestFovDegrees,
		1.0e-9);

	auto ResolveSubjectProjection = [&M2Settings](
		const FVector& BirdPosition,
		const EABTSM11FinaleCameraStage Stage,
		const double StageProgress,
		double& OutBirdX,
		double& OutTargetX,
		double& OutNormalizedCenterSeparation,
		bool& bOutBirdIsForeground)
	{
		FABTSM11FinaleFlightCameraFrame EncounterFrame;
		if (!ABTSM11FinaleFlightCameraMath::BuildDesiredFrame(
			BirdPosition,
			FVector::ForwardVector,
			FVector::UpVector,
			FVector::ZeroVector,
			FVector::ZeroVector,
			false,
			920.0,
			310.0,
			80.0,
			80.0,
			EncounterFrame))
		{
			return false;
		}
		FABTSM11FinaleCameraDirectorSample Sample;
		Sample.Selection.Stage = Stage;
		Sample.Selection.StageProgress = StageProgress;
		Sample.Selection.AssistIndex = 1;
		Sample.Selection.TargetLabel = TEXT("Assist1");
		Sample.Selection.Reason = TEXT("UnitEncounter");
		Sample.TargetCenter = FVector::ZeroVector;
		Sample.TargetRadiusCM = 1000.0;
		Sample.BirdRadiusCM = 60.0;
		Sample.EncounterScreenRight = FVector::ForwardVector;
		Sample.EncounterScreenUp = FVector::UpVector;
		FTransform CameraTransform;
		FABTSM11FinaleCameraM2Diagnostics LocalDiagnostics;
		if (!ABTSM11FinaleFlightCameraMath::BuildM2Assist1Frame(
			EncounterFrame,
			BirdPosition,
			Sample,
			M2Settings,
			CameraTransform,
			LocalDiagnostics))
		{
			return false;
		}
		const FVector Forward =
			CameraTransform.GetRotation().GetForwardVector();
		const FVector Right =
			CameraTransform.GetRotation().GetRightVector();
		const FVector Up =
			CameraTransform.GetRotation().GetUpVector();
		const FVector ToBird = BirdPosition - CameraTransform.GetLocation();
		const FVector ToTarget = -CameraTransform.GetLocation();
		const double BirdDepth = FVector::DotProduct(ToBird, Forward);
		const double TargetDepth = FVector::DotProduct(ToTarget, Forward);
		if (BirdDepth <= UE_DOUBLE_SMALL_NUMBER
			|| TargetDepth <= UE_DOUBLE_SMALL_NUMBER)
		{
			return false;
		}
		OutBirdX = FVector::DotProduct(ToBird, Right) / BirdDepth;
		OutTargetX = FVector::DotProduct(ToTarget, Right) / TargetDepth;
		const double BirdY = FVector::DotProduct(ToBird, Up) / BirdDepth;
		const double TargetY = FVector::DotProduct(ToTarget, Up) / TargetDepth;
		const double TargetAngularRadius = Sample.TargetRadiusCM / TargetDepth;
		OutNormalizedCenterSeparation = FVector2D(
			OutBirdX - OutTargetX,
			BirdY - TargetY).Size() / TargetAngularRadius;
		bOutBirdIsForeground = BirdDepth < TargetDepth;
		return FMath::IsFinite(OutBirdX)
			&& FMath::IsFinite(OutTargetX)
			&& FMath::IsFinite(OutNormalizedCenterSeparation);
	};
	double ApproachBirdX = 0.0;
	double ApproachTargetX = 0.0;
	double ApproachNormalizedSeparation = 0.0;
	bool bApproachBirdIsForeground = false;
	TestTrue(
		TEXT("Approach composition projects both subjects"),
		ResolveSubjectProjection(
			FVector(-5000.0, 3000.0, 0.0),
			EABTSM11FinaleCameraStage::Approach,
			0.5,
			ApproachBirdX,
			ApproachTargetX,
			ApproachNormalizedSeparation,
			bApproachBirdIsForeground));
	TestTrue(
		TEXT("Approach keeps the bird screen-left of the planet"),
		ApproachBirdX < ApproachTargetX);
	TestTrue(
		TEXT("Mid-approach projects the foreground bird onto the planet disc"),
		ApproachNormalizedSeparation > 0.65
			&& ApproachNormalizedSeparation < 1.0
			&& bApproachBirdIsForeground);
	double DepartureBirdX = 0.0;
	double DepartureTargetX = 0.0;
	double DepartureNormalizedSeparation = 0.0;
	bool bDepartureBirdIsForeground = false;
	TestTrue(
		TEXT("Departure composition projects both subjects"),
		ResolveSubjectProjection(
			FVector(5000.0, 3000.0, 0.0),
			EABTSM11FinaleCameraStage::Periapsis,
			0.08,
			DepartureBirdX,
			DepartureTargetX,
			DepartureNormalizedSeparation,
			bDepartureBirdIsForeground));
	TestTrue(
		TEXT("Departure keeps the bird screen-right of the planet"),
		DepartureBirdX > DepartureTargetX);
	TestTrue(
		TEXT("Immediate departure remains a foreground planet transit"),
		DepartureNormalizedSeparation < 1.0
			&& bDepartureBirdIsForeground);
	double EntryBirdX = 0.0;
	double EntryTargetX = 0.0;
	double EntryNormalizedSeparation = 0.0;
	bool bEntryBirdIsForeground = false;
	TestTrue(
		TEXT("Approach entry projection remains valid"),
		ResolveSubjectProjection(
			FVector(-5000.0, 3000.0, 0.0),
			EABTSM11FinaleCameraStage::Approach,
			0.0,
			EntryBirdX,
			EntryTargetX,
			EntryNormalizedSeparation,
			bEntryBirdIsForeground));
	TestTrue(
		TEXT("Approach begins outside the screen-left planet limb"),
		EntryBirdX < EntryTargetX
			&& EntryNormalizedSeparation > 1.3
			&& EntryNormalizedSeparation < 1.9
			&& bEntryBirdIsForeground);

	DirectorSample.Selection.StageProgress = 1.0;
	TestTrue(
		TEXT("M2 exit boundary remains mathematically valid"),
		ABTSM11FinaleFlightCameraMath::BuildM2Assist1Frame(
			Frame,
			Target,
			DirectorSample,
			M2Settings,
			DirectedTransform,
			Diagnostics));
	TestEqual(
		TEXT("Assist1 exit returns exactly to legacy blend"),
		Diagnostics.DirectorBlendAlpha,
		0.0,
		1.0e-9);
	TestEqual(
		TEXT("Assist1 exit restores the baseline lens"),
		Diagnostics.DirectedFovDegrees,
		M2Settings.BaselineFovDegrees,
		1.0e-9);

	FABTSM11FinaleCameraShotSettings M3ShotSettings;
	FABTSM11FinaleCameraShotSettings AcceleratedM3ShotSettings;
	TestTrue(
		TEXT("M3 presentation durations convert to the playback clock"),
		M3ShotSettings.BuildPlaybackClockSettings(
			18.0,
			AcceleratedM3ShotSettings));
	TestEqual(
		TEXT("Playback conversion scales bridge duration"),
		AcceleratedM3ShotSettings.DualBodyBridgeSeconds,
		M3ShotSettings.DualBodyBridgeSeconds * 18.0,
		1.0e-9);
	TestEqual(
		TEXT("Playback conversion scales incoming track protection"),
		AcceleratedM3ShotSettings.MinimumIncomingTrackSeconds,
		M3ShotSettings.MinimumIncomingTrackSeconds * 18.0,
		1.0e-9);
	TestEqual(
		TEXT("Playback conversion preserves the geometric clear gate"),
		AcceleratedM3ShotSettings.ForegroundTransitClearProgress,
		M3ShotSettings.ForegroundTransitClearProgress,
		1.0e-9);
	TestFalse(
		TEXT("Playback conversion rejects a non-positive clock scale"),
		M3ShotSettings.BuildPlaybackClockSettings(
			0.0,
			AcceleratedM3ShotSettings));
	double ClearBirdX = 0.0;
	double ClearTargetX = 0.0;
	double ClearNormalizedSeparation = 0.0;
	bool bClearBirdIsForeground = false;
	TestTrue(
		TEXT("Foreground-clear scheduling threshold projects both subjects"),
		ResolveSubjectProjection(
			FVector(5000.0, 3000.0, 0.0),
			EABTSM11FinaleCameraStage::Periapsis,
			M3ShotSettings.ForegroundTransitClearProgress,
			ClearBirdX,
			ClearTargetX,
			ClearNormalizedSeparation,
			bClearBirdIsForeground));
	TestTrue(
		TEXT("Default scheduling threshold clears the foreground bird silhouette"),
		ClearBirdX > ClearTargetX
			&& ClearNormalizedSeparation > 1.22
			&& bClearBirdIsForeground);
	const FABTSM11FinaleCameraStageSelection M3LaunchAcquire =
		ABTSM11FinaleCameraDirector::ResolveStage(
			true,
			false,
			0.2,
			&EventResult,
			true,
			&M3ShotSettings);
	TestEqual(
		TEXT("Launch immediately acquires Assist1"),
		M3LaunchAcquire.FramingAssistIndex,
		1);
	TestEqual(
		TEXT("Launch acquisition is an explicit incoming reveal"),
		static_cast<uint8>(M3LaunchAcquire.ShotPhase),
		static_cast<uint8>(
			EABTSM11FinaleCameraShotPhase::IncomingReveal));
	TestEqual(
		TEXT("Launch acquisition starts at the launch authority time"),
		M3LaunchAcquire.ShotDurationSeconds,
		10.0,
		1.0e-9);
	TestEqual(
		TEXT("Launch acquisition uses the full first encounter progress"),
		M3LaunchAcquire.ShotProgress,
		0.02,
		1.0e-9);

	const FABTSM11FinaleCameraStageSelection M3LaunchTrack =
		ABTSM11FinaleCameraDirector::ResolveStage(
			true,
			false,
			2.5,
			&EventResult,
			true,
			&M3ShotSettings);
	TestEqual(
		TEXT("Assist1 becomes an established incoming track after acquire"),
		static_cast<uint8>(M3LaunchTrack.ShotPhase),
		static_cast<uint8>(
			EABTSM11FinaleCameraShotPhase::IncomingTrack));
	TestTrue(
		TEXT("Established Assist1 track remains an incoming shot"),
		M3LaunchTrack.IsM3IncomingShot());
	TestFalse(
		TEXT("Established Assist1 track is no longer the acquire phase"),
		M3LaunchTrack.IsM3IncomingAcquire());

	const FABTSM11FinaleCameraStageSelection M3LaunchEntryMatch =
		ABTSM11FinaleCameraDirector::ResolveStage(
			true,
			false,
			9.6,
			&EventResult,
			true,
			&M3ShotSettings);
	TestEqual(
		TEXT("Assist1 uses the same pre-enter match state as later bodies"),
		static_cast<uint8>(M3LaunchEntryMatch.ShotPhase),
		static_cast<uint8>(
			EABTSM11FinaleCameraShotPhase::IncomingEntryMatch));

	DirectorSample.Selection = M3LaunchAcquire;
	DirectorSample.Selection.ShotProgress =
		0.5 * M2Settings.HandoffLeadInSeconds
		/ DirectorSample.Selection.ShotDurationSeconds;
	TestTrue(
		TEXT("Launch-anchored Assist1 acquisition builds a finite frame"),
		ABTSM11FinaleFlightCameraMath::BuildM3AssistFrame(
			Frame,
			Target,
			DirectorSample,
			M2Settings,
			DirectedTransform,
			Diagnostics));
	TestEqual(
		TEXT("Launch-anchored acquisition reaches half director weight"),
		Diagnostics.DirectorBlendAlpha,
		0.5,
		1.0e-9);

	const FVector LaunchSafeLocation(1200.0, -350.0, 640.0);
	const FVector LaunchBirdPosition(500.0, 100.0, 200.0);
	const FVector LaunchDirectedLocation(-800.0, 950.0, 1100.0);
	FVector LaunchReleaseLocation = FVector::ZeroVector;
	TestTrue(
		TEXT("Launch carry release builds a finite start location"),
		ABTSM11FinaleFlightCameraMath::BuildM3LaunchReleaseLocation(
			LaunchSafeLocation,
			LaunchBirdPosition,
			LaunchDirectedLocation,
			0.0,
			LaunchReleaseLocation));
	TestTrue(
		TEXT("Launch carry release starts at the limiter location"),
		LaunchReleaseLocation.Equals(LaunchSafeLocation, 1.0e-6));
	TestTrue(
		TEXT("Launch carry release builds a finite terminal location"),
		ABTSM11FinaleFlightCameraMath::BuildM3LaunchReleaseLocation(
			LaunchSafeLocation,
			LaunchBirdPosition,
			LaunchDirectedLocation,
			1.0,
			LaunchReleaseLocation));
	TestTrue(
		TEXT("Launch carry release terminates at the exact directed location"),
		LaunchReleaseLocation.Equals(LaunchDirectedLocation, 1.0e-6));

	const FABTSM11FinaleCameraStageSelection M3OutgoingHold =
		ABTSM11FinaleCameraDirector::ResolveStage(
			true,
			false,
			14.25,
			&EventResult,
			true,
			&M3ShotSettings);
	TestEqual(
		TEXT("Authority CurrentBody still switches at the physical Handoff"),
		M3OutgoingHold.AssistIndex,
		2);
	TestEqual(
		TEXT("Outgoing hold keeps framing the departed body"),
		M3OutgoingHold.FramingAssistIndex,
		1);
	TestEqual(
		TEXT("Physical Handoff can carry an outgoing presentation shot"),
		static_cast<uint8>(M3OutgoingHold.ShotPhase),
		static_cast<uint8>(
			EABTSM11FinaleCameraShotPhase::OutgoingHold));
	TestEqual(
		TEXT("Outgoing hold exposes phase-local progress"),
		M3OutgoingHold.ShotPhaseProgress,
		M3OutgoingHold.ShotProgress,
		1.0e-9);
	TestTrue(
		TEXT("M3 Handoff is a directed assist window"),
		M3OutgoingHold.IsM3AssistWindow());
	TestFalse(
		TEXT("M3 Handoff does not leak into the M2 scope"),
		M3OutgoingHold.IsM2Assist1Window());
	DirectorSample.Selection = M3OutgoingHold;
	DirectorSample.TargetCenter = FVector(5000.0, 3000.0, 0.0);
	DirectorSample.OutgoingTargetCenter = DirectorSample.TargetCenter;
	DirectorSample.OutgoingTargetRadiusCM = DirectorSample.TargetRadiusCM;
	DirectorSample.IncomingTargetCenter = FVector(9000.0, 3000.0, 0.0);
	DirectorSample.IncomingTargetRadiusCM = DirectorSample.TargetRadiusCM;
	TestTrue(
		TEXT("Outgoing hold remains a finite directed frame"),
		ABTSM11FinaleFlightCameraMath::BuildM3AssistFrame(
			Frame,
			Target,
			DirectorSample,
			M2Settings,
			DirectedTransform,
			Diagnostics));
	TestEqual(
		TEXT("Physical Handoff preserves the outgoing Lucy exit mark"),
		Diagnostics.TransitScreenXInTargetRadii,
		M2Settings.TransitExitOffsetRadii,
		1.0e-9);
	TestEqual(
		TEXT("Outgoing hold never fades to the subject-losing legacy chase"),
		Diagnostics.DirectorBlendAlpha,
		1.0,
		1.0e-9);

	const FABTSM11FinaleCameraStageSelection M3DualBodyBridge =
		ABTSM11FinaleCameraDirector::ResolveStage(
			true,
			false,
			14.75,
			&EventResult,
			true,
			&M3ShotSettings);
	TestEqual(
		TEXT("Dual-body bridge frames Assist2 before AssistEnter"),
		M3DualBodyBridge.FramingAssistIndex,
		2);
	TestEqual(
		TEXT("Inter-body handoff has an explicit bridge state"),
		static_cast<uint8>(M3DualBodyBridge.ShotPhase),
		static_cast<uint8>(
			EABTSM11FinaleCameraShotPhase::DualBodyBridge));
	TestEqual(
		TEXT("Dual-body bridge keeps the full incoming shot budget"),
		M3DualBodyBridge.ShotDurationSeconds,
		M3ShotSettings.IncomingRevealLeadSeconds,
		1.0e-9);
	TestEqual(
		TEXT("Dual-body bridge exposes its own phase duration"),
		M3DualBodyBridge.ShotPhaseDurationSeconds,
		M3ShotSettings.DualBodyBridgeSeconds,
		1.0e-9);
	TestEqual(
		TEXT("Bridge identifies the outgoing assist"),
		M3DualBodyBridge.OutgoingAssistIndex,
		1);
	TestEqual(
		TEXT("Bridge identifies the incoming assist"),
		M3DualBodyBridge.IncomingAssistIndex,
		2);
	TestEqual(
		TEXT("Bridge exposes both subjects in its framing label"),
		M3DualBodyBridge.FramingTargetLabel,
		FString(TEXT("Assist1+Assist2")));
	DirectorSample.Selection = M3DualBodyBridge;
	DirectorSample.TargetCenter = DirectorSample.IncomingTargetCenter;
	TestTrue(
		TEXT("Dual-body bridge builds one finite three-subject frame"),
		ABTSM11FinaleFlightCameraMath::BuildM3AssistFrame(
			Frame,
			Target,
			DirectorSample,
			M2Settings,
			DirectedTransform,
			Diagnostics));
	TestEqual(
		TEXT("Dual-body bridge owns the wide lens"),
		Diagnostics.DirectedFovDegrees,
		M2Settings.DualBodyBridgeFovDegrees,
		1.0e-9);
	TestEqual(
		TEXT("Dual-body bridge is fully presentation authoritative"),
		Diagnostics.DirectorBlendAlpha,
		1.0,
		1.0e-9);

	// Freeze a deliberately non-coplanar three-subject bridge. The planets
	// still own the horizontal baseline, while the bird must retain a stable
	// vertical narrative line and all projected spheres remain inside margin.
	FABTSM11FinaleCameraDirectorSample VerticalBridgeSample = DirectorSample;
	VerticalBridgeSample.Selection = M3DualBodyBridge;
	VerticalBridgeSample.OutgoingTargetCenter = FVector(0.0, -6000.0, -2000.0);
	VerticalBridgeSample.IncomingTargetCenter = FVector(0.0, 6000.0, -2000.0);
	VerticalBridgeSample.TargetCenter =
		VerticalBridgeSample.IncomingTargetCenter;
	VerticalBridgeSample.OutgoingTargetRadiusCM = 1000.0;
	VerticalBridgeSample.IncomingTargetRadiusCM = 1000.0;
	VerticalBridgeSample.TargetRadiusCM = 1000.0;
	VerticalBridgeSample.BirdRadiusCM = 120.0;
	const FVector VerticalBridgeBird(6000.0, 0.0, 3000.0);
	FTransform VerticalBridgeTransform;
	TestTrue(
		TEXT("Non-coplanar bridge builds a projection-safe anchored frame"),
		ABTSM11FinaleFlightCameraMath::BuildM3DualBodyBridgeFrame(
			Frame,
			VerticalBridgeBird,
			VerticalBridgeSample,
			M2Settings,
			VerticalBridgeTransform));
	const auto ProjectBridgeNdc = [&] (
		const FVector& SubjectCenter,
		FVector2D& OutNdc,
		double& OutDepth)
	{
		const FQuat Rotation = VerticalBridgeTransform.GetRotation();
		const FVector Relative =
			SubjectCenter - VerticalBridgeTransform.GetLocation();
		OutDepth = FVector::DotProduct(Relative, Rotation.GetForwardVector());
		const double TanHalfHorizontal = FMath::Tan(FMath::DegreesToRadians(
			M2Settings.DualBodyBridgeFovDegrees * 0.5));
		const double TanHalfVertical = TanHalfHorizontal / (16.0 / 9.0);
		if (OutDepth <= UE_DOUBLE_SMALL_NUMBER)
		{
			return false;
		}
		OutNdc.X = FVector::DotProduct(Relative, Rotation.GetRightVector())
			/ (OutDepth * TanHalfHorizontal);
		OutNdc.Y = FVector::DotProduct(Relative, Rotation.GetUpVector())
			/ (OutDepth * TanHalfVertical);
		return FMath::IsFinite(OutNdc.X) && FMath::IsFinite(OutNdc.Y);
	};
	FVector2D VerticalBirdNdc = FVector2D::ZeroVector;
	FVector2D VerticalOutgoingNdc = FVector2D::ZeroVector;
	FVector2D VerticalIncomingNdc = FVector2D::ZeroVector;
	double VerticalBirdDepth = 0.0;
	double VerticalOutgoingDepth = 0.0;
	double VerticalIncomingDepth = 0.0;
	const bool bProjectedVerticalBridge = ProjectBridgeNdc(
		VerticalBridgeBird,
		VerticalBirdNdc,
		VerticalBirdDepth)
		&& ProjectBridgeNdc(
			VerticalBridgeSample.OutgoingTargetCenter,
			VerticalOutgoingNdc,
			VerticalOutgoingDepth)
		&& ProjectBridgeNdc(
			VerticalBridgeSample.IncomingTargetCenter,
			VerticalIncomingNdc,
			VerticalIncomingDepth);
	TestTrue(
		TEXT("Anchored bridge projects all three subjects"),
		bProjectedVerticalBridge);
	if (bProjectedVerticalBridge)
	{
		TestEqual(
			TEXT("Bridge holds the bird on the canonical vertical NDC anchor"),
			VerticalBirdNdc.Y,
			M2Settings.DualBodyBridgeBirdNdcY,
			1.0e-6);
		TestEqual(
			TEXT("Vertical bird anchor preserves the two-planet horizontal baseline"),
			VerticalOutgoingNdc.Y,
			VerticalIncomingNdc.Y,
			1.0e-6);
		const double TanHalfHorizontal = FMath::Tan(FMath::DegreesToRadians(
			M2Settings.DualBodyBridgeFovDegrees * 0.5));
		const double TanHalfVertical = TanHalfHorizontal / (16.0 / 9.0);
		const double SafeNdcLimit = 1.0 / M2Settings.DualBodyBridgeFitMargin;
		const auto SphereFitsMargin = [&] (
			const FVector2D& CenterNdc,
			const double Depth,
			const double Radius)
		{
			const double NearDepth = Depth - Radius;
			return NearDepth > UE_DOUBLE_SMALL_NUMBER
				&& FMath::Abs(CenterNdc.X)
					+ Radius / (NearDepth * TanHalfHorizontal)
					<= SafeNdcLimit + 1.0e-6
				&& FMath::Abs(CenterNdc.Y)
					+ Radius / (NearDepth * TanHalfVertical)
					<= SafeNdcLimit + 1.0e-6;
		};
		TestTrue(
			TEXT("Bridge bird sphere retains the projection margin"),
			SphereFitsMargin(
				VerticalBirdNdc,
				VerticalBirdDepth,
				VerticalBridgeSample.BirdRadiusCM));
		TestTrue(
			TEXT("Bridge outgoing planet retains the projection margin"),
			SphereFitsMargin(
				VerticalOutgoingNdc,
				VerticalOutgoingDepth,
				VerticalBridgeSample.OutgoingTargetRadiusCM));
		TestTrue(
			TEXT("Bridge incoming planet retains the projection margin"),
			SphereFitsMargin(
				VerticalIncomingNdc,
				VerticalIncomingDepth,
				VerticalBridgeSample.IncomingTargetRadiusCM));
	}
	const FABTSM11FinaleCameraStageSelection M3InterBodyReveal =
		ABTSM11FinaleCameraDirector::ResolveStage(
			true,
			false,
			16.0,
			&EventResult,
			true,
			&M3ShotSettings);
	TestEqual(
		TEXT("Bridge hands off through a reachable incoming reveal"),
		static_cast<uint8>(M3InterBodyReveal.ShotPhase),
		static_cast<uint8>(
			EABTSM11FinaleCameraShotPhase::IncomingReveal));
	TestEqual(
		TEXT("Incoming reveal uses phase-local progress after bridge hold"),
		M3InterBodyReveal.ShotPhaseProgress,
		(16.0 - (14.5 + M3ShotSettings.DualBodyBridgeSeconds))
			/ M3ShotSettings.IncomingAcquireSeconds,
		1.0e-9);
	const FABTSM11FinaleCameraStageSelection M3IncomingTrack =
		ABTSM11FinaleCameraDirector::ResolveStage(
			true,
			false,
			18.5,
			&EventResult,
			true,
			&M3ShotSettings);
	TestEqual(
		TEXT("Incoming body enters Track after bridge and reveal"),
		static_cast<uint8>(M3IncomingTrack.ShotPhase),
		static_cast<uint8>(
			EABTSM11FinaleCameraShotPhase::IncomingTrack));

	const FABTSM11FinaleCameraStageSelection M3EntryMatch =
		ABTSM11FinaleCameraDirector::ResolveStage(
			true,
			false,
			19.6,
			&EventResult,
			true,
			&M3ShotSettings);
	TestEqual(
		TEXT("Final pre-enter interval is explicitly EntryMatch"),
		static_cast<uint8>(M3EntryMatch.ShotPhase),
		static_cast<uint8>(
			EABTSM11FinaleCameraShotPhase::IncomingEntryMatch));
	const FABTSM11FinaleCameraStageSelection M3AfterEnter =
		ABTSM11FinaleCameraDirector::ResolveStage(
			true,
			false,
			20.1,
			&EventResult,
			true,
			&M3ShotSettings);
	TestEqual(
		TEXT("AssistEnter does not replay EntryMatch as a post-enter settle"),
		static_cast<uint8>(M3AfterEnter.ShotPhase),
		static_cast<uint8>(EABTSM11FinaleCameraShotPhase::Authority));

	const FABTSM11FinaleCameraStageSelection M3AtEnter =
		ABTSM11FinaleCameraDirector::ResolveStage(
			true,
			false,
			20.0,
			&EventResult,
			true,
			&M3ShotSettings);
	auto BuildM3BoundaryFrame = [&] (
		const FABTSM11FinaleCameraStageSelection& Selection,
		const FVector& TargetCenter,
		FTransform& OutTransform,
		FABTSM11FinaleCameraM2Diagnostics& OutLocalDiagnostics)
	{
		DirectorSample.Selection = Selection;
		DirectorSample.TargetCenter = TargetCenter;
		return ABTSM11FinaleFlightCameraMath::BuildM3AssistFrame(
			Frame,
			Target,
			DirectorSample,
			M2Settings,
			OutTransform,
			OutLocalDiagnostics);
	};
	auto TestM3BoundaryContinuity = [&] (
		const TCHAR* BoundaryLabel,
		const FABTSM11FinaleCameraStageSelection& FromSelection,
		const FVector& FromTargetCenter,
		const FABTSM11FinaleCameraStageSelection& ToSelection,
		const FVector& ToTargetCenter)
	{
		FTransform FromTransform;
		FTransform ToTransform;
		FABTSM11FinaleCameraM2Diagnostics FromDiagnostics;
		FABTSM11FinaleCameraM2Diagnostics ToDiagnostics;
		const bool bFromBuilt = BuildM3BoundaryFrame(
			FromSelection,
			FromTargetCenter,
			FromTransform,
			FromDiagnostics);
		const bool bToBuilt = BuildM3BoundaryFrame(
			ToSelection,
			ToTargetCenter,
			ToTransform,
			ToDiagnostics);
		TestTrue(
			FString::Printf(TEXT("%s builds both endpoint frames"), BoundaryLabel),
			bFromBuilt && bToBuilt);
		if (!bFromBuilt || !bToBuilt)
		{
			return;
		}
		TestTrue(
			FString::Printf(TEXT("%s keeps location continuous"), BoundaryLabel),
			FromTransform.GetLocation().Equals(
				ToTransform.GetLocation(),
				1.0e-4));
		TestTrue(
			FString::Printf(TEXT("%s keeps rotation continuous"), BoundaryLabel),
			FromTransform.GetRotation().AngularDistance(
				ToTransform.GetRotation()) <= 1.0e-6);
		TestEqual(
			FString::Printf(TEXT("%s keeps FOV continuous"), BoundaryLabel),
			FromDiagnostics.DirectedFovDegrees,
			ToDiagnostics.DirectedFovDegrees,
			1.0e-9);
	};
	FABTSM11FinaleCameraStageSelection OutgoingEnd = M3OutgoingHold;
	OutgoingEnd.ShotProgress = 1.0;
	OutgoingEnd.ShotPhaseProgress = 1.0;
	FABTSM11FinaleCameraStageSelection BridgeStart = M3DualBodyBridge;
	BridgeStart.ShotPhaseProgress = 0.0;
	TestM3BoundaryContinuity(
		TEXT("OutgoingHold to DualBodyBridge"),
		OutgoingEnd,
		DirectorSample.OutgoingTargetCenter,
		BridgeStart,
		DirectorSample.IncomingTargetCenter);

	FABTSM11FinaleCameraStageSelection BridgeEnd = M3DualBodyBridge;
	BridgeEnd.ShotPhaseProgress = 1.0;
	FABTSM11FinaleCameraStageSelection RevealStart = M3InterBodyReveal;
	RevealStart.ShotProgress = M3ShotSettings.DualBodyBridgeSeconds
		/ RevealStart.ShotDurationSeconds;
	RevealStart.ShotPhaseProgress = 0.0;
	TestM3BoundaryContinuity(
		TEXT("DualBodyBridge to IncomingReveal"),
		BridgeEnd,
		DirectorSample.IncomingTargetCenter,
		RevealStart,
		DirectorSample.IncomingTargetCenter);

	FABTSM11FinaleCameraStageSelection RevealEnd = M3InterBodyReveal;
	RevealEnd.ShotProgress =
		(M3ShotSettings.DualBodyBridgeSeconds
			+ M3ShotSettings.IncomingAcquireSeconds)
		/ M3ShotSettings.IncomingRevealLeadSeconds;
	RevealEnd.ShotPhaseProgress = 1.0;
	FABTSM11FinaleCameraStageSelection TrackStart = M3IncomingTrack;
	TrackStart.ShotProgress = RevealEnd.ShotProgress;
	TrackStart.ShotPhaseProgress = 0.0;
	TestM3BoundaryContinuity(
		TEXT("IncomingReveal to IncomingTrack"),
		RevealEnd,
		DirectorSample.IncomingTargetCenter,
		TrackStart,
		DirectorSample.IncomingTargetCenter);

	FABTSM11FinaleCameraStageSelection TrackEnd = M3IncomingTrack;
	TrackEnd.ShotProgress = 1.0
		- M3ShotSettings.EntryMatchSeconds
			/ M3ShotSettings.IncomingRevealLeadSeconds;
	TrackEnd.ShotPhaseProgress = 1.0;
	FABTSM11FinaleCameraStageSelection EntryStart = M3EntryMatch;
	EntryStart.ShotProgress = TrackEnd.ShotProgress;
	EntryStart.ShotPhaseProgress = 0.0;
	TestM3BoundaryContinuity(
		TEXT("IncomingTrack to IncomingEntryMatch"),
		TrackEnd,
		DirectorSample.IncomingTargetCenter,
		EntryStart,
		DirectorSample.IncomingTargetCenter);

	FTransform MidRevealTransform;
	FABTSM11FinaleCameraM2Diagnostics MidRevealDiagnostics;
	const bool bTrackCurveBuilt = BuildM3BoundaryFrame(
		M3InterBodyReveal,
		DirectorSample.IncomingTargetCenter,
		MidRevealTransform,
		MidRevealDiagnostics);
	TestTrue(
		TEXT("Incoming composition match builds a finite reveal sample"),
		bTrackCurveBuilt);
	if (bTrackCurveBuilt)
	{
		TestEqual(
			TEXT("Incoming reveal retains the wide lens for outgoing-body egress"),
			MidRevealDiagnostics.DirectedFovDegrees,
			M2Settings.DualBodyBridgeFovDegrees,
			1.0e-9);
	}

	FABTSM11FinaleCameraStageSelection MidTrack = TrackStart;
	MidTrack.ShotProgress = 0.5
		* (TrackStart.ShotProgress + TrackEnd.ShotProgress);
	MidTrack.ShotPhaseProgress = 0.5;
	FTransform MidTrackTransform;
	FABTSM11FinaleCameraM2Diagnostics MidTrackDiagnostics;
	const bool bMidTrackBuilt = BuildM3BoundaryFrame(
		MidTrack,
		DirectorSample.IncomingTargetCenter,
		MidTrackTransform,
		MidTrackDiagnostics);
	TestTrue(
		TEXT("Incoming track builds the split anchor/depth sample"),
		bMidTrackBuilt);
	if (bMidTrackBuilt)
	{
		TestTrue(
			TEXT("Incoming track has started releasing the wide lens"),
			MidTrackDiagnostics.DirectedFovDegrees
				< M2Settings.DualBodyBridgeFovDegrees);
		TestTrue(
			TEXT("Incoming track has not reached the final Lucy lens early"),
			MidTrackDiagnostics.DirectedFovDegrees
				> M2Settings.BaselineFovDegrees);
	}

	FABTSM11FinaleCameraStageSelection EntryEnd = M3EntryMatch;
	EntryEnd.ShotProgress = 1.0;
	EntryEnd.ShotPhaseProgress = 1.0;
	TestM3BoundaryContinuity(
		TEXT("IncomingEntryMatch to Authority Approach"),
		EntryEnd,
		DirectorSample.IncomingTargetCenter,
		M3AtEnter,
		DirectorSample.IncomingTargetCenter);

	FABTSM11FinaleCameraShotSettings TightBudgetSettings = M3ShotSettings;
	TightBudgetSettings.IncomingRevealLeadSeconds = 7.0;
	TightBudgetSettings.MinimumIncomingTrackSeconds = 3.6;
	const FABTSM11FinaleCameraStageSelection BeforeForegroundClear =
		ABTSM11FinaleCameraDirector::ResolveStage(
			true,
			false,
			12.459,
			&EventResult,
			true,
			&TightBudgetSettings);
	TestEqual(
		TEXT("Tight schedule keeps Authority until the foreground transit clears"),
		static_cast<uint8>(BeforeForegroundClear.ShotPhase),
		static_cast<uint8>(EABTSM11FinaleCameraShotPhase::Authority));
	TestEqual(
		TEXT("Foreground-clear hold keeps framing the outgoing assist"),
		BeforeForegroundClear.FramingAssistIndex,
		1);
	const FABTSM11FinaleCameraStageSelection AtForegroundClear =
		ABTSM11FinaleCameraDirector::ResolveStage(
			true,
			false,
			12.46,
			&EventResult,
			true,
			&TightBudgetSettings);
	TestEqual(
		TEXT("Outgoing pullback may begin at the foreground-clear threshold"),
		static_cast<uint8>(AtForegroundClear.ShotPhase),
		static_cast<uint8>(EABTSM11FinaleCameraShotPhase::OutgoingHold));
	TestEqual(
		TEXT("Foreground-clear threshold maps to the configured Periapsis progress"),
		AtForegroundClear.StageProgress,
		TightBudgetSettings.ForegroundTransitClearProgress,
		1.0e-9);
	const FABTSM11FinaleCameraStageSelection TightBudgetBridge =
		ABTSM11FinaleCameraDirector::ResolveStage(
			true,
			false,
			14.0,
			&EventResult,
			true,
			&TightBudgetSettings);
	TestEqual(
		TEXT("Tight schedule yields to the bridge at its latest safe time"),
		static_cast<uint8>(TightBudgetBridge.ShotPhase),
		static_cast<uint8>(
			EABTSM11FinaleCameraShotPhase::DualBodyBridge));
	const FABTSM11FinaleCameraStageSelection TightBudgetTrack =
		ABTSM11FinaleCameraDirector::ResolveStage(
			true,
			false,
			16.0,
			&EventResult,
			true,
			&TightBudgetSettings);
	TestEqual(
		TEXT("Tight schedule retains an explicit incoming Track"),
		static_cast<uint8>(TightBudgetTrack.ShotPhase),
		static_cast<uint8>(EABTSM11FinaleCameraShotPhase::IncomingTrack));
	TestEqual(
		TEXT("Tight schedule protects the configured minimum Track duration"),
		TightBudgetTrack.ShotPhaseDurationSeconds,
		TightBudgetSettings.MinimumIncomingTrackSeconds,
		1.0e-9);

	FABTSM11FinaleCameraShotSettings ImpossibleBudgetSettings =
		M3ShotSettings;
	ImpossibleBudgetSettings.IncomingRevealLeadSeconds = 8.0;
	ImpossibleBudgetSettings.MinimumIncomingTrackSeconds = 5.0;
	TestTrue(
		TEXT("Impossible event budget fixture still uses individually valid settings"),
		ImpossibleBudgetSettings.IsUsable());
	const FABTSM11FinaleCameraStageSelection ImpossibleBudget =
		ABTSM11FinaleCameraDirector::ResolveStage(
			true,
			false,
			12.5,
			&EventResult,
			true,
			&ImpossibleBudgetSettings);
	TestNotEqual(
		TEXT("M7 keeps a valid gameplay path available under a short camera budget"),
		static_cast<uint8>(ImpossibleBudget.Stage),
		static_cast<uint8>(EABTSM11FinaleCameraStage::Unavailable));
	FABTSM11FinaleCameraShotPlan AdaptivePlan;
	FString AdaptivePlanFailure;
	TestTrue(
		TEXT("M7 prebuilds a release-frozen adaptive schedule"),
		AdaptivePlan.Build(
			EventResult,
			ImpossibleBudgetSettings,
			&AdaptivePlanFailure));
	TestTrue(
		TEXT("Short fixture records adaptive compression"),
		AdaptivePlan.bUsesAdaptiveCompression);
	TestTrue(
		TEXT("Frozen schedule identity matches its released trajectory"),
		AdaptivePlan.IsUsableFor(EventResult));
	FABTSM11TrajectoryResult DifferentResult = EventResult;
	DifferentResult.ValidationHash = 2;
	TestFalse(
		TEXT("Frozen schedule rejects a later trajectory identity"),
		AdaptivePlan.IsUsableFor(DifferentResult));

	FABTSM11TrajectoryResult IncompleteAssistResult = EventResult;
	IncompleteAssistResult.CompletedAssistCount =
		FABTSM11GravityScenario::AssistCount - 1;
	FABTSM11FinaleCameraShotPlan IncompleteAssistPlan;
	FString IncompleteAssistFailure;
	TestFalse(
		TEXT("M7 rejects a shot plan when the released route did not complete every assist"),
		IncompleteAssistPlan.Build(
			IncompleteAssistResult,
			M3ShotSettings,
			&IncompleteAssistFailure));
	TestEqual(
		TEXT("Incomplete assist rejection has a stable diagnostic"),
		IncompleteAssistFailure,
		FString(TEXT("M7ShotPlanRequiresAllAssistsCompleted")));
	TestFalse(
		TEXT("Rejected incomplete route cannot retain a frozen shot plan"),
		IncompleteAssistPlan.IsUsableFor(IncompleteAssistResult));
	const FABTSM11FinaleCameraStageSelection IncompleteAssistLiveRebuild =
		ABTSM11FinaleCameraDirector::ResolveStage(
			true,
			false,
			5.0,
			&IncompleteAssistResult,
			true,
			&M3ShotSettings);
	TestEqual(
		TEXT("Live director rebuild also fails closed for an incomplete route"),
		static_cast<uint8>(IncompleteAssistLiveRebuild.Stage),
		static_cast<uint8>(EABTSM11FinaleCameraStage::Unavailable));

	FABTSM11FinaleCameraShotSettings BorrowedTimeSettings = M3ShotSettings;
	BorrowedTimeSettings.IncomingRevealLeadSeconds = 7.0;
	const FABTSM11FinaleCameraStageSelection BorrowedPeriapsis =
		ABTSM11FinaleCameraDirector::ResolveStage(
			true,
			false,
			13.5,
			&EventResult,
			true,
			&BorrowedTimeSettings);
	TestEqual(
		TEXT("Authority remains in outgoing Periapsis during early reveal"),
		static_cast<uint8>(BorrowedPeriapsis.Stage),
		static_cast<uint8>(EABTSM11FinaleCameraStage::Periapsis));
	TestEqual(
		TEXT("Early lead cannot preempt the outgoing foreground transit"),
		BorrowedPeriapsis.FramingAssistIndex,
		1);
	TestEqual(
		TEXT("Early lead remains an outgoing pullback after silhouette clearance"),
		static_cast<uint8>(BorrowedPeriapsis.ShotPhase),
		static_cast<uint8>(EABTSM11FinaleCameraShotPhase::OutgoingHold));
	TestFalse(
		TEXT("Early lead does not borrow protected foreground time"),
		BorrowedPeriapsis.IsM3IncomingShot());

	FABTSM11FinaleCameraStageSelection M3IncomingReveal = M3DualBodyBridge;
	M3IncomingReveal.ShotPhase =
		EABTSM11FinaleCameraShotPhase::IncomingReveal;
	M3IncomingReveal.OutgoingAssistIndex = 0;
	M3IncomingReveal.IncomingAssistIndex = 0;
	M3IncomingReveal.FramingTargetLabel = TEXT("Assist2");
	M3IncomingReveal.ShotReason = TEXT("Assist2AcquireUnit");
	DirectorSample.Selection = M3IncomingReveal;
	DirectorSample.TargetCenter = FVector(9000.0, 3000.0, 0.0);
	DirectorSample.Selection.ShotProgress = 0.0;
	TestTrue(
		TEXT("M3 IncomingReveal start builds the incoming assist frame"),
		ABTSM11FinaleFlightCameraMath::BuildM3AssistFrame(
			Frame,
			Target,
			DirectorSample,
			M2Settings,
			DirectedTransform,
			Diagnostics));
	TestEqual(
		TEXT("M3 IncomingReveal starts at exact legacy weight"),
		Diagnostics.DirectorBlendAlpha,
		0.0,
		1.0e-9);
	TestEqual(
		TEXT("M3 IncomingReveal starts from the far incoming mark"),
		Diagnostics.TransitScreenXInTargetRadii,
		-M2Settings.TransitCruiseFarOffsetRadii,
		1.0e-9);
	DirectorSample.Selection.ShotProgress =
		0.5 * M2Settings.HandoffLeadInSeconds
		/ DirectorSample.Selection.ShotDurationSeconds;
	TestTrue(
		TEXT("M3 IncomingReveal blend midpoint remains finite"),
		ABTSM11FinaleFlightCameraMath::BuildM3AssistFrame(
			Frame,
			Target,
			DirectorSample,
			M2Settings,
			DirectedTransform,
			Diagnostics));
	TestEqual(
		TEXT("M3 IncomingReveal midpoint reaches half director weight"),
		Diagnostics.DirectorBlendAlpha,
		0.5,
		1.0e-9);
	DirectorSample.Selection.ShotProgress =
		M2Settings.HandoffLeadInSeconds
		/ DirectorSample.Selection.ShotDurationSeconds;
	TestTrue(
		TEXT("M3 IncomingReveal establishment remains finite"),
		ABTSM11FinaleFlightCameraMath::BuildM3AssistFrame(
			Frame,
			Target,
			DirectorSample,
			M2Settings,
			DirectedTransform,
			Diagnostics));
	TestEqual(
		TEXT("M3 reaches full incoming director weight after lead-in"),
		Diagnostics.DirectorBlendAlpha,
		1.0,
		1.0e-9);
	TestEqual(
		TEXT("M3 IncomingReveal retains the baseline lens"),
		Diagnostics.DirectedFovDegrees,
		M2Settings.BaselineFovDegrees,
		1.0e-9);

	const double DerivativeStep = 1.0e-4;
	DirectorSample.Selection = M3IncomingReveal;
	DirectorSample.Selection.ShotProgress = 1.0 - DerivativeStep;
	FABTSM11FinaleCameraM2Diagnostics HandoffBeforeBoundary;
	TestTrue(
		TEXT("Incoming reveal derivative sample is valid"),
		ABTSM11FinaleFlightCameraMath::BuildM3AssistFrame(
			Frame,
			Target,
			DirectorSample,
			M2Settings,
			DirectedTransform,
			HandoffBeforeBoundary));
	DirectorSample.Selection.ShotProgress = 1.0;
	FABTSM11FinaleCameraM2Diagnostics HandoffAtBoundary;
	TestTrue(
		TEXT("Incoming reveal reaches the entry boundary"),
		ABTSM11FinaleFlightCameraMath::BuildM3AssistFrame(
			Frame,
			Target,
			DirectorSample,
			M2Settings,
			DirectedTransform,
			HandoffAtBoundary));
	const double HandoffBoundarySpeed =
		(HandoffAtBoundary.TransitScreenXInTargetRadii
			- HandoffBeforeBoundary.TransitScreenXInTargetRadii)
		/ (DerivativeStep
			* DirectorSample.Selection.ShotDurationSeconds);

	DirectorSample.Selection.Stage = EABTSM11FinaleCameraStage::Approach;
	DirectorSample.Selection.ShotPhase =
		EABTSM11FinaleCameraShotPhase::Authority;
	DirectorSample.Selection.ShotProgress = 0.0;
	DirectorSample.Selection.ShotDurationSeconds = 0.0;
	DirectorSample.Selection.ShotEndSlope = 0.0;
	DirectorSample.Selection.ShotReason = TEXT("AuthorityStage");
	DirectorSample.Selection.AssistIndex = 2;
	DirectorSample.Selection.FramingAssistIndex = 2;
	DirectorSample.Selection.TargetLabel = TEXT("Assist2");
	DirectorSample.Selection.FramingTargetLabel = TEXT("Assist2");
	DirectorSample.Selection.StageProgress = 0.0;
	FABTSM11FinaleCameraM2Diagnostics ApproachAtBoundary;
	TestTrue(
		TEXT("M3 Approach entry remains finite"),
		ABTSM11FinaleFlightCameraMath::BuildM3AssistFrame(
			Frame,
			Target,
			DirectorSample,
			M2Settings,
			DirectedTransform,
			ApproachAtBoundary));
	DirectorSample.Selection.StageProgress = DerivativeStep;
	FABTSM11FinaleCameraM2Diagnostics ApproachAfterBoundary;
	TestTrue(
		TEXT("M3 Approach derivative sample remains finite"),
		ABTSM11FinaleFlightCameraMath::BuildM3AssistFrame(
			Frame,
			Target,
			DirectorSample,
			M2Settings,
			DirectedTransform,
			ApproachAfterBoundary));
	const double ApproachBoundarySpeed =
		(ApproachAfterBoundary.TransitScreenXInTargetRadii
			- ApproachAtBoundary.TransitScreenXInTargetRadii)
		/ (DerivativeStep * 2.0);
	TestEqual(
		TEXT("Incoming reveal matches Approach screen velocity"),
		HandoffBoundarySpeed,
		ApproachBoundarySpeed,
		1.0e-3);

	DirectorSample.Selection.StageProgress = 0.5;
	TestTrue(
		TEXT("M3 applies the Lucy encounter to Assist2"),
		ABTSM11FinaleFlightCameraMath::BuildM3AssistFrame(
			Frame,
			Target,
			DirectorSample,
			M2Settings,
			DirectedTransform,
			Diagnostics));
	TestFalse(
		TEXT("M2 still rejects Assist2"),
		ABTSM11FinaleFlightCameraMath::BuildM2Assist1Frame(
			Frame,
			Target,
			DirectorSample,
			M2Settings,
			DirectedTransform,
			Diagnostics));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11CFlightCameraFormationAdaptiveRetreatTest,
	"ABTS.M11C.Unit.FlightCameraFormationAdaptiveRetreat",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM11CFlightCameraFormationAdaptiveRetreatTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const UWorld::InitializationValues WorldValues =
		UWorld::InitializationValues()
			.InitializeScenes(false)
			.AllowAudioPlayback(false)
			.RequiresHitProxies(false)
			.CreatePhysicsScene(false)
			.CreateNavigation(false)
			.CreateAISystem(false)
			.ShouldSimulatePhysics(false)
			.EnableTraceCollision(false)
			.SetTransactional(false)
			.CreateFXSystem(false);
	UWorld* World = UWorld::CreateWorld(
		EWorldType::Game,
		false,
		TEXT("ABTSM11CFormationAdaptiveRetreatWorld"),
		nullptr,
		true,
		ERHIFeatureLevel::Num,
		&WorldValues);
	TestNotNull(TEXT("Transient formation camera World is created"), World);
	if (World == nullptr)
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM11FinaleFlightCamera* FlightCamera =
		World->SpawnActor<AABTSM11FinaleFlightCamera>(
			AABTSM11FinaleFlightCamera::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	TestNotNull(TEXT("Formation flight camera spawns"), FlightCamera);
	if (FlightCamera != nullptr)
	{
		TArray<FABTSM11FinaleFormationCameraSubject> Subjects;
		Subjects.Add({FVector(1000.0, 0.0, 0.0), 84.0});
		Subjects.Add({FVector(1000.0, 0.0, -6800.0), 84.0});
		Subjects.Add({FVector(1000.0, 250.0, -500.0), 84.0});
		Subjects.Add({FVector(1000.0, -250.0, -1000.0), 84.0});
		FVector CameraLocation = FVector::ZeroVector;
		FString Failure;
		TestTrue(
			TEXT("Formation safety expands beyond the former 30000 cm limit"),
			FlightCamera->ApplyM6FormationSafetyEnvelopeForTesting(
				Subjects[0].Center,
				Subjects,
				FQuat::Identity,
				50.0,
				CameraLocation,
				&Failure));
		TestTrue(
			TEXT("Adaptive formation retreat exceeds the former fixed limit"),
			-CameraLocation.X > 30000.0);
		TestTrue(
			TEXT("Adaptive formation retreat remains bounded"),
			-CameraLocation.X < 120000.0);
		TestTrue(
			TEXT("Successful adaptive retreat does not publish a failure"),
			Failure.IsEmpty());
	}
	World->DestroyWorld(false);
	World->RemoveFromRoot();
	return FlightCamera != nullptr;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11CFlightCameraScopedFXAATest,
	"ABTS.M11C.Unit.FlightCameraScopedFXAA",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM11CFlightCameraScopedFXAATest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const int32 PreviousScopedProductionOverride =
		ABTSM11FinaleCameraDirector::GetProductionModeOverride();
	IConsoleVariable* AntiAliasingMethod =
		IConsoleManager::Get().FindConsoleVariable(
			TEXT("r.AntiAliasingMethod"));
	TestNotNull(
		TEXT("Engine anti-aliasing console variable is registered"),
		AntiAliasingMethod);
	if (AntiAliasingMethod == nullptr)
	{
		return false;
	}

	TGuardConsoleVariable<int32> AntiAliasingGuard(
		AntiAliasingMethod,
		static_cast<int32>(AAM_TSR));
	const UWorld::InitializationValues WorldValues =
		UWorld::InitializationValues()
			.InitializeScenes(false)
			.AllowAudioPlayback(false)
			.RequiresHitProxies(false)
			.CreatePhysicsScene(false)
			.CreateNavigation(false)
			.CreateAISystem(false)
			.ShouldSimulatePhysics(false)
			.EnableTraceCollision(false)
			.SetTransactional(false)
			.CreateFXSystem(false);
	UWorld* World = UWorld::CreateWorld(
		EWorldType::Game,
		false,
		TEXT("ABTSM11CFlightCameraFXAAAutomationWorld"),
		nullptr,
		true,
		ERHIFeatureLevel::Num,
		&WorldValues);
	TestNotNull(TEXT("Transient flight-camera World is created"), World);
	if (World == nullptr)
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM11FinaleFlightCamera* FlightCamera =
		World->SpawnActor<AABTSM11FinaleFlightCamera>(
			AABTSM11FinaleFlightCamera::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	TestNotNull(TEXT("M11 finale flight camera spawns"), FlightCamera);
	if (FlightCamera != nullptr)
	{
		TestTrue(
			TEXT("Authority follow begins with a valid deterministic frame"),
			FlightCamera->BeginAuthorityFollow(
				FVector::ZeroVector,
				FVector::ForwardVector,
				FVector::UpVector,
				FTransform::Identity,
				EABTSM11FinaleCameraDirectorMode::MultiAssistM3));
		TestTrue(TEXT("Flight camera freezes the requested M3 mode"),
			FlightCamera->IsM3DirectorFrozenEnabled());
		TestFalse(TEXT("Flight camera does not conflate M3 with M2"),
			FlightCamera->IsM2DirectorFrozenEnabled());
		ABTSM11FinaleCameraDirector::SetProductionModeOverride(0);
		TestTrue(TEXT("Later console changes cannot replace frozen M3 mode"),
			FlightCamera->IsM3DirectorFrozenEnabled());
		ABTSM11FinaleCameraDirector::SetProductionModeOverride(
			PreviousScopedProductionOverride);
		TestEqual(
			TEXT("M11 finale takeover selects FXAA"),
			AntiAliasingMethod->GetInt(),
			static_cast<int32>(AAM_FXAA));

		AntiAliasingMethod->SetWithCurrentPriority(
			static_cast<int32>(AAM_TSR));
		TestTrue(
			TEXT("Authority update accepts a valid follow sample"),
			FlightCamera->UpdateAuthoritySample(
				FVector(100.0, 0.0, 0.0),
				FVector::ForwardVector,
				FVector::UpVector,
				nullptr,
				1.0f / 60.0f));
		TestEqual(
			TEXT("Active finale camera repairs later temporal-AA changes"),
			AntiAliasingMethod->GetInt(),
			static_cast<int32>(AAM_FXAA));

		FlightCamera->ResetAuthorityFollow();
		TestEqual(
			TEXT("Finale camera reset restores the pre-takeover AA method"),
			AntiAliasingMethod->GetInt(),
			static_cast<int32>(AAM_TSR));
	}
	World->DestroyWorld(false);
	World->RemoveFromRoot();
	return FlightCamera != nullptr;
}

#endif
