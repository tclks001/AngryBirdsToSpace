// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/ABTSM11FinaleFlightCamera.h"
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
			1.0,
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

	const FABTSM11FinaleCameraStageSelection M3OutgoingHold =
		ABTSM11FinaleCameraDirector::ResolveStage(
			true,
			false,
			15.0,
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
			17.0,
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
	const FABTSM11FinaleCameraStageSelection M3IncomingTrack =
		ABTSM11FinaleCameraDirector::ResolveStage(
			true,
			false,
			17.5,
			&EventResult,
			true,
			&M3ShotSettings);
	TestEqual(
		TEXT("Incoming body enters Track after its acquisition interval"),
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
		TEXT("Presentation can already frame the next assist"),
		BorrowedPeriapsis.FramingAssistIndex,
		2);
	TestTrue(
		TEXT("Early reveal borrows only presentation time"),
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

#endif
