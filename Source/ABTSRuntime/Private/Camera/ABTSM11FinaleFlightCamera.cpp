// Copyright Epic Games, Inc. All Rights Reserved.

#include "Camera/ABTSM11FinaleFlightCamera.h"

#include "ABTSRuntime.h"
#include "Camera/CameraComponent.h"
#include "HAL/IConsoleManager.h"
#include "SceneUtils.h"

namespace
{
	bool IsFiniteFlightCameraVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X)
			&& FMath::IsFinite(Value.Y)
			&& FMath::IsFinite(Value.Z);
	}

	FVector ResolveProjectedUp(
		const FVector& CandidateUp,
		const FVector& PreferredUp,
		const FVector& Forward)
	{
		FVector Up = FVector::VectorPlaneProject(
			CandidateUp,
			Forward).GetSafeNormal();
		if (Up.IsNearlyZero())
		{
			Up = FVector::VectorPlaneProject(
				PreferredUp,
				Forward).GetSafeNormal();
		}
		if (Up.IsNearlyZero())
		{
			const FVector FallbackAxis =
				FMath::Abs(Forward.Z) < 0.9f
					? FVector::UpVector
					: FVector::RightVector;
			Up = FVector::VectorPlaneProject(
				FallbackAxis,
				Forward).GetSafeNormal();
		}
		return Up;
	}

	double SmootherStep01(const double Value)
	{
		const double Clamped = FMath::Clamp(Value, 0.0, 1.0);
		return Clamped * Clamped * Clamped
			* (Clamped * (Clamped * 6.0 - 15.0) + 10.0);
	}

	double SmoothStep01(const double Value)
	{
		const double Clamped = FMath::Clamp(Value, 0.0, 1.0);
		return Clamped * Clamped * (3.0 - 2.0 * Clamped);
	}

	double ResolveIncomingEntryAnchorAlpha(
		const FABTSM11FinaleCameraStageSelection& Selection,
		const FABTSM11FinaleCameraM2Settings& Settings)
	{
		// Move the camera's view-plane position toward the Lucy entry early enough
		// that the bird cannot overshoot and then be pulled back. Camera depth and
		// lens deliberately use a slower envelope so the outgoing planet can leave
		// the wide frame before the incoming flyby fills it.
		const double MatchProgress = Selection.ShotPhase
			== EABTSM11FinaleCameraShotPhase::IncomingTrack
				? FMath::Clamp(Selection.ShotPhaseProgress, 0.0, 1.0)
				: 0.0;
		const double FrontLoaded = 1.0 - FMath::Pow(
			1.0 - MatchProgress,
			Settings.IncomingMatchEaseOutPower);
		return SmoothStep01(FrontLoaded);
	}

	double ResolveIncomingDepthMatchAlpha(
		const FABTSM11FinaleCameraStageSelection& Selection)
	{
		return Selection.ShotPhase
			== EABTSM11FinaleCameraShotPhase::IncomingTrack
				? SmoothStep01(Selection.ShotPhaseProgress)
				: 0.0;
	}

	double Hermite01WithEndSlope(
		const double Value,
		const double EndSlope)
	{
		const double T = FMath::Clamp(Value, 0.0, 1.0);
		const double T2 = T * T;
		const double T3 = T2 * T;
		return FMath::Clamp(
			(-2.0 * T3 + 3.0 * T2)
				+ FMath::Max(0.0, EndSlope) * (T3 - T2),
			0.0,
			1.0);
	}

	double ResolveEncounterOrientationAlpha(
		const FABTSM11FinaleCameraStageSelection& Selection,
		const double DirectorBlendAlpha)
	{
		if (Selection.IsM3TransitionShot())
		{
			return FMath::Clamp(DirectorBlendAlpha, 0.0, 1.0);
		}
		if (Selection.Stage == EABTSM11FinaleCameraStage::CruiseToBody)
		{
			return FMath::Clamp(DirectorBlendAlpha, 0.0, 1.0);
		}
		if (Selection.Stage == EABTSM11FinaleCameraStage::Handoff)
		{
			return FMath::Clamp(DirectorBlendAlpha, 0.0, 1.0);
		}
		return Selection.Stage == EABTSM11FinaleCameraStage::Approach
			|| Selection.Stage == EABTSM11FinaleCameraStage::Periapsis
				? 1.0
				: 0.0;
	}

	double ResolveTransitScreenXInTargetRadii(
		const FABTSM11FinaleCameraStageSelection& Selection,
		const FABTSM11FinaleCameraM2Settings& Settings)
	{
		// Once authority has crossed AssistExit, its physical stage is Handoff.
		// The presentation layer may deliberately keep the outgoing planet for a
		// short release. Preserve the previous Lucy exit mark in that interval;
		// reusing the incoming Handoff curve here would teleport the bird from the
		// right side of the outgoing planet back to its left side.
		if (Selection.ShotPhase
			== EABTSM11FinaleCameraShotPhase::OutgoingHold
			&& Selection.Stage == EABTSM11FinaleCameraStage::Handoff)
		{
			return Settings.TransitExitOffsetRadii;
		}
		if (Selection.IsM3IncomingShot())
		{
			return FMath::Lerp(
				-Settings.TransitCruiseFarOffsetRadii,
				-Settings.TransitEntryOffsetRadii,
				Hermite01WithEndSlope(
					Selection.ShotProgress,
					Selection.ShotEndSlope));
		}
		if (Selection.Stage == EABTSM11FinaleCameraStage::CruiseToBody)
		{
			const double CruiseMotionAlpha = FMath::Clamp(
				(Selection.StageProgress
					- Settings.CruiseLeadInStartFraction)
					/ FMath::Max(UE_DOUBLE_SMALL_NUMBER,
						1.0 - Settings.CruiseLeadInStartFraction),
				0.0,
				1.0);
			const double AcceleratingCruiseAlpha = CruiseMotionAlpha
				* (0.8 + 0.2 * CruiseMotionAlpha);
			return FMath::Lerp(
				-Settings.TransitCruiseFarOffsetRadii,
				-Settings.TransitEntryOffsetRadii,
				AcceleratingCruiseAlpha);
		}
		if (Selection.Stage == EABTSM11FinaleCameraStage::Handoff)
		{
			const double HandoffMotionAlpha = FMath::Clamp(
				Selection.StageProgress,
				0.0,
				1.0);
			const double AcceleratingHandoffAlpha = HandoffMotionAlpha
				* (0.8 + 0.2 * HandoffMotionAlpha);
			return FMath::Lerp(
				-Settings.TransitCruiseFarOffsetRadii,
				-Settings.TransitEntryOffsetRadii,
				AcceleratingHandoffAlpha);
		}
		if (Selection.Stage == EABTSM11FinaleCameraStage::Approach)
		{
			const double Progress = FMath::Clamp(
				Selection.StageProgress,
				0.0,
				1.0);
			const double AcceleratingApproachAlpha = Progress
				* (0.6 + 0.4 * Progress);
			return FMath::Lerp(
				-Settings.TransitEntryOffsetRadii,
				Settings.TransitClosestOffsetRadii,
				AcceleratingApproachAlpha);
		}
		const double ExitAlpha = FMath::Clamp(
			Selection.StageProgress
				/ Settings.TransitExitProgressFraction,
			0.0,
			1.0);
		const double DeceleratingExitAlpha =
			1.0 - FMath::Square(1.0 - ExitAlpha);
		return FMath::Lerp(
			Settings.TransitClosestOffsetRadii,
			Settings.TransitExitOffsetRadii,
			DeceleratingExitAlpha);
	}

	double ResolveDirectedFovDegrees(
		const FABTSM11FinaleCameraStageSelection& Selection,
		const FABTSM11FinaleCameraM2Settings& Settings)
	{
		if (Selection.ShotPhase
			== EABTSM11FinaleCameraShotPhase::OutgoingHold)
		{
			const double AuthorityFovDegrees =
				Selection.Stage == EABTSM11FinaleCameraStage::Periapsis
				? FMath::Lerp(
					Settings.ClosestFovDegrees,
					Settings.BaselineFovDegrees,
					SmootherStep01(
						Selection.StageProgress
							/ Settings.PeriapsisFovRestoreFraction))
				: Settings.BaselineFovDegrees;
			return FMath::Lerp(
				AuthorityFovDegrees,
				Settings.DualBodyBridgeFovDegrees,
				SmootherStep01(Selection.ShotPhaseProgress));
		}
		if (Selection.ShotPhase
			== EABTSM11FinaleCameraShotPhase::DualBodyBridge)
		{
			return Settings.DualBodyBridgeFovDegrees;
		}
		if ((Selection.ShotPhase
				== EABTSM11FinaleCameraShotPhase::IncomingReveal
			|| Selection.ShotPhase
				== EABTSM11FinaleCameraShotPhase::IncomingTrack)
			&& Selection.IsM3InterBodyTransition())
		{
			return FMath::Lerp(
				Settings.DualBodyBridgeFovDegrees,
				Settings.BaselineFovDegrees,
				ResolveIncomingDepthMatchAlpha(Selection));
		}
		if (Selection.IsM3IncomingShot())
		{
			return Settings.BaselineFovDegrees;
		}
		if (Selection.Stage == EABTSM11FinaleCameraStage::Approach)
		{
			return FMath::Lerp(
				Settings.BaselineFovDegrees,
				Settings.ClosestFovDegrees,
				SmootherStep01(Selection.StageProgress));
		}
		if (Selection.Stage == EABTSM11FinaleCameraStage::Periapsis)
		{
			return FMath::Lerp(
				Settings.ClosestFovDegrees,
				Settings.BaselineFovDegrees,
				SmootherStep01(
					Selection.StageProgress
						/ Settings.PeriapsisFovRestoreFraction));
		}
		return Settings.BaselineFovDegrees;
	}

	FVector SlerpDirection(
		const FVector& From,
		const FVector& To,
		const double Alpha)
	{
		const FVector Start = From.GetSafeNormal();
		const FVector End = To.GetSafeNormal();
		const double Dot = FMath::Clamp(
			static_cast<double>(FVector::DotProduct(Start, End)),
			-1.0,
			1.0);
		const double Angle = FMath::Acos(Dot);
		FVector Axis = FVector::CrossProduct(Start, End).GetSafeNormal();
		if (Angle <= UE_DOUBLE_SMALL_NUMBER)
		{
			return FMath::Lerp(Start, End, Alpha).GetSafeNormal();
		}
		if (Axis.IsNearlyZero())
		{
			const FVector FallbackAxis =
				FMath::Abs(Start.Z) < 0.9f
					? FVector::UpVector
					: FVector::RightVector;
			Axis = FVector::CrossProduct(
				Start,
				FallbackAxis).GetSafeNormal();
		}
		return FQuat(Axis, Angle * FMath::Clamp(Alpha, 0.0, 1.0))
			.RotateVector(Start)
			.GetSafeNormal();
	}

	bool BuildBirdAnchoredRotation(
		const FVector& CameraLocation,
		const FVector& BirdPosition,
		const FVector& PreferredUp,
		FQuat& OutRotation)
	{
		const FVector ViewDirection =
			(BirdPosition - CameraLocation).GetSafeNormal();
		const FVector ViewUp = ResolveProjectedUp(
			PreferredUp,
			FVector::UpVector,
			ViewDirection);
		if (ViewDirection.IsNearlyZero() || ViewUp.IsNearlyZero())
		{
			return false;
		}
		OutRotation = FRotationMatrix::MakeFromXZ(
			ViewDirection,
			ViewUp).ToQuat();
		return OutRotation.IsNormalized();
	}

	bool BuildM3FittedSubjectFrame(
		const FQuat& ViewRotation,
		const double HorizontalFovDegrees,
		const double FitMargin,
		const FVector& BirdPosition,
		const double BirdRadiusCM,
		const FVector& IncomingTargetCenter,
		const double IncomingTargetRadiusCM,
		const FVector* OutgoingTargetCenter,
		const double OutgoingTargetRadiusCM,
		FTransform& OutTransform)
	{
		if (!ViewRotation.IsNormalized()
			|| !FMath::IsFinite(HorizontalFovDegrees)
			|| HorizontalFovDegrees <= 0.0
			|| !FMath::IsFinite(FitMargin)
			|| FitMargin <= 1.0
			|| !IsFiniteFlightCameraVector(BirdPosition)
			|| !IsFiniteFlightCameraVector(IncomingTargetCenter)
			|| (OutgoingTargetCenter != nullptr
				&& !IsFiniteFlightCameraVector(*OutgoingTargetCenter)))
		{
			return false;
		}
		const int32 SubjectCount = OutgoingTargetCenter != nullptr ? 3 : 2;
		FVector Origin = BirdPosition + IncomingTargetCenter;
		if (OutgoingTargetCenter != nullptr)
		{
			Origin += *OutgoingTargetCenter;
		}
		Origin /= static_cast<double>(SubjectCount);
		const FVector ViewForward = ViewRotation.GetForwardVector();
		const FVector ViewRight = ViewRotation.GetRightVector();
		const FVector ViewUp = ViewRotation.GetUpVector();
		double MinRight = TNumericLimits<double>::Max();
		double MaxRight = TNumericLimits<double>::Lowest();
		double MinUp = TNumericLimits<double>::Max();
		double MaxUp = TNumericLimits<double>::Lowest();
		double MaximumAbsoluteDepth = 0.0;
		const auto IncludeSphere = [&] (const FVector& Center, const double Radius)
		{
			const FVector Relative = Center - Origin;
			const double SafeRadius = FMath::Max(0.0, Radius);
			const double Right = FVector::DotProduct(Relative, ViewRight);
			const double Up = FVector::DotProduct(Relative, ViewUp);
			const double Depth = FVector::DotProduct(Relative, ViewForward);
			MinRight = FMath::Min(MinRight, Right - SafeRadius);
			MaxRight = FMath::Max(MaxRight, Right + SafeRadius);
			MinUp = FMath::Min(MinUp, Up - SafeRadius);
			MaxUp = FMath::Max(MaxUp, Up + SafeRadius);
			MaximumAbsoluteDepth = FMath::Max(
				MaximumAbsoluteDepth,
				FMath::Abs(Depth) + SafeRadius);
		};
		IncludeSphere(BirdPosition, BirdRadiusCM);
		IncludeSphere(IncomingTargetCenter, IncomingTargetRadiusCM);
		if (OutgoingTargetCenter != nullptr)
		{
			IncludeSphere(*OutgoingTargetCenter, OutgoingTargetRadiusCM);
		}
		const double HalfWidth = FMath::Max(
			FMath::Abs(MinRight),
			FMath::Abs(MaxRight)) * FitMargin;
		const double HalfHeight = FMath::Max(
			FMath::Abs(MinUp),
			FMath::Abs(MaxUp)) * FitMargin;
		const double TanHalfHorizontal = FMath::Tan(
			FMath::DegreesToRadians(HorizontalFovDegrees * 0.5));
		constexpr double AspectRatio = 16.0 / 9.0;
		const double TanHalfVertical = TanHalfHorizontal / AspectRatio;
		if (!FMath::IsFinite(HalfWidth)
			|| !FMath::IsFinite(HalfHeight)
			|| TanHalfHorizontal <= UE_DOUBLE_SMALL_NUMBER
			|| TanHalfVertical <= UE_DOUBLE_SMALL_NUMBER)
		{
			return false;
		}
		const double FitDistance = FMath::Max(
			HalfWidth / TanHalfHorizontal,
			HalfHeight / TanHalfVertical) + MaximumAbsoluteDepth;
		const FVector CameraLocation = Origin - ViewForward * FitDistance;
		OutTransform = FTransform(ViewRotation, CameraLocation);
		return IsFiniteFlightCameraVector(CameraLocation);
	}

	bool ProjectSubjectToNdc(
		const FTransform& ViewTransform,
		const double HorizontalFovDegrees,
		const FVector& SubjectPosition,
		FVector2D& OutNdc)
	{
		OutNdc = FVector2D::ZeroVector;
		if (!ViewTransform.GetRotation().IsNormalized()
			|| !FMath::IsFinite(HorizontalFovDegrees)
			|| HorizontalFovDegrees <= 0.0
			|| !IsFiniteFlightCameraVector(SubjectPosition))
		{
			return false;
		}
		const FVector Relative =
			SubjectPosition - ViewTransform.GetLocation();
		const double Depth = FVector::DotProduct(
			Relative,
			ViewTransform.GetRotation().GetForwardVector());
		const double TanHalfHorizontal = FMath::Tan(
			FMath::DegreesToRadians(HorizontalFovDegrees * 0.5));
		constexpr double AspectRatio = 16.0 / 9.0;
		const double TanHalfVertical = TanHalfHorizontal / AspectRatio;
		if (Depth <= UE_DOUBLE_SMALL_NUMBER
			|| TanHalfHorizontal <= UE_DOUBLE_SMALL_NUMBER
			|| TanHalfVertical <= UE_DOUBLE_SMALL_NUMBER)
		{
			return false;
		}
		OutNdc.X = FVector::DotProduct(
			Relative,
			ViewTransform.GetRotation().GetRightVector())
			/ (Depth * TanHalfHorizontal);
		OutNdc.Y = FVector::DotProduct(
			Relative,
			ViewTransform.GetRotation().GetUpVector())
			/ (Depth * TanHalfVertical);
		return FMath::IsFinite(OutNdc.X) && FMath::IsFinite(OutNdc.Y);
	}

	/**
	 * Keeps the bird on a canonical vertical screen line without changing the
	 * supplied rotation or horizontal camera coordinate. If that view-plane
	 * shift would crop any active subject, the camera is moved only backward
	 * until every projected sphere satisfies the requested fit margin.
	 */
	bool BuildM3BirdYAnchoredSubjectFrame(
		const FTransform& BaseTransform,
		const double HorizontalFovDegrees,
		const double FitMargin,
		const double DesiredBirdNdcY,
		const FVector& BirdPosition,
		const double BirdRadiusCM,
		const FVector& PrimaryTargetCenter,
		const double PrimaryTargetRadiusCM,
		const FVector* SecondaryTargetCenter,
		const double SecondaryTargetRadiusCM,
		FTransform& OutTransform)
	{
		OutTransform = BaseTransform;
		if (!BaseTransform.GetRotation().IsNormalized()
			|| !FMath::IsFinite(HorizontalFovDegrees)
			|| HorizontalFovDegrees <= 0.0
			|| !FMath::IsFinite(FitMargin)
			|| FitMargin <= 1.0
			|| !FMath::IsFinite(DesiredBirdNdcY)
			|| !IsFiniteFlightCameraVector(BirdPosition)
			|| !IsFiniteFlightCameraVector(PrimaryTargetCenter)
			|| (SecondaryTargetCenter != nullptr
				&& !IsFiniteFlightCameraVector(*SecondaryTargetCenter)))
		{
			return false;
		}

		const FQuat ViewRotation = BaseTransform.GetRotation();
		const FVector ViewForward = ViewRotation.GetForwardVector();
		const FVector ViewRight = ViewRotation.GetRightVector();
		const FVector ViewUp = ViewRotation.GetUpVector();
		const double TanHalfHorizontal = FMath::Tan(
			FMath::DegreesToRadians(HorizontalFovDegrees * 0.5));
		constexpr double AspectRatio = 16.0 / 9.0;
		const double TanHalfVertical = TanHalfHorizontal / AspectRatio;
		if (TanHalfHorizontal <= UE_DOUBLE_SMALL_NUMBER
			|| TanHalfVertical <= UE_DOUBLE_SMALL_NUMBER)
		{
			return false;
		}

		const double BaseCameraForward = FVector::DotProduct(
			BaseTransform.GetLocation(),
			ViewForward);
		const double BaseCameraRight = FVector::DotProduct(
			BaseTransform.GetLocation(),
			ViewRight);
		const double BirdForward = FVector::DotProduct(
			BirdPosition,
			ViewForward);
		const double BirdUp = FVector::DotProduct(BirdPosition, ViewUp);
		const double BaseBirdDepth = BirdForward - BaseCameraForward;
		if (BaseBirdDepth <= UE_DOUBLE_SMALL_NUMBER)
		{
			return false;
		}

		const double SafeNdcLimit = 1.0 / FitMargin;
		const auto EvaluateDistance = [&] (
			const double ExtraDistance,
			FTransform& CandidateTransform)
		{
			const double CameraForward = BaseCameraForward - ExtraDistance;
			const double BirdDepth = BirdForward - CameraForward;
			const double CameraUp = BirdUp
				- DesiredBirdNdcY * BirdDepth * TanHalfVertical;
			const FVector CameraLocation =
				ViewForward * CameraForward
				+ ViewRight * BaseCameraRight
				+ ViewUp * CameraUp;
			CandidateTransform = FTransform(ViewRotation, CameraLocation);
			if (!IsFiniteFlightCameraVector(CameraLocation))
			{
				return false;
			}

			const auto SphereFits = [&] (
				const FVector& Center,
				const double Radius)
			{
				const FVector Relative = Center - CameraLocation;
				const double SafeRadius = FMath::Max(0.0, Radius);
				const double Depth = FVector::DotProduct(
					Relative,
					ViewForward);
				const double NearDepth = Depth - SafeRadius;
				if (NearDepth <= UE_DOUBLE_SMALL_NUMBER)
				{
					return false;
				}
				const double CenterNdcX = FVector::DotProduct(
					Relative,
					ViewRight) / (Depth * TanHalfHorizontal);
				const double CenterNdcY = FVector::DotProduct(
					Relative,
					ViewUp) / (Depth * TanHalfVertical);
				const double RadiusNdcX = SafeRadius
					/ (NearDepth * TanHalfHorizontal);
				const double RadiusNdcY = SafeRadius
					/ (NearDepth * TanHalfVertical);
				return FMath::IsFinite(CenterNdcX)
					&& FMath::IsFinite(CenterNdcY)
					&& FMath::Abs(CenterNdcX) + RadiusNdcX
						<= SafeNdcLimit
					&& FMath::Abs(CenterNdcY) + RadiusNdcY
						<= SafeNdcLimit;
			};

			return SphereFits(BirdPosition, BirdRadiusCM)
				&& SphereFits(
					PrimaryTargetCenter,
					PrimaryTargetRadiusCM)
				&& (SecondaryTargetCenter == nullptr
					|| SphereFits(
						*SecondaryTargetCenter,
						SecondaryTargetRadiusCM));
		};

		FTransform CandidateTransform;
		if (EvaluateDistance(0.0, CandidateTransform))
		{
			OutTransform = CandidateTransform;
			return true;
		}

		double LowDistance = 0.0;
		double HighDistance = FMath::Max(100.0, BaseBirdDepth * 0.1);
		bool bFoundFittingDistance = false;
		for (int32 Iteration = 0; Iteration < 20; ++Iteration)
		{
			if (EvaluateDistance(HighDistance, CandidateTransform))
			{
				bFoundFittingDistance = true;
				break;
			}
			LowDistance = HighDistance;
			HighDistance *= 2.0;
		}
		if (!bFoundFittingDistance)
		{
			return false;
		}

		for (int32 Iteration = 0; Iteration < 24; ++Iteration)
		{
			const double MiddleDistance =
				(LowDistance + HighDistance) * 0.5;
			if (EvaluateDistance(MiddleDistance, CandidateTransform))
			{
				HighDistance = MiddleDistance;
			}
			else
			{
				LowDistance = MiddleDistance;
			}
		}
		return EvaluateDistance(HighDistance, OutTransform);
	}

	bool BuildM3BirdAnchorLocation(
		const FTransform& WideTransform,
		const double WideProjectionFovDegrees,
		const FTransform& LucyTransform,
		const double LucyProjectionFovDegrees,
		const double OutputFovDegrees,
		const double EntryAnchorAlpha,
		const double DepthMatchAlpha,
		const FVector& BirdPosition,
		FVector& OutLocation)
	{
		OutLocation = WideTransform.GetLocation();
		if (!WideTransform.GetRotation().Equals(
				LucyTransform.GetRotation(),
				1.0e-6)
			|| !FMath::IsFinite(OutputFovDegrees)
			|| OutputFovDegrees <= 0.0)
		{
			return false;
		}
		FVector2D WideBirdNdc;
		FVector2D LucyBirdNdc;
		if (!ProjectSubjectToNdc(
				WideTransform,
				WideProjectionFovDegrees,
				BirdPosition,
				WideBirdNdc)
			|| !ProjectSubjectToNdc(
				LucyTransform,
				LucyProjectionFovDegrees,
				BirdPosition,
				LucyBirdNdc))
		{
			return false;
		}

		const FVector2D DesiredBirdNdc = FMath::Lerp(
			WideBirdNdc,
			LucyBirdNdc,
			FMath::Clamp(EntryAnchorAlpha, 0.0, 1.0));
		const FQuat ViewRotation = WideTransform.GetRotation();
		const FVector ViewForward = ViewRotation.GetForwardVector();
		const FVector ViewRight = ViewRotation.GetRightVector();
		const FVector ViewUp = ViewRotation.GetUpVector();
		const double TanHalfHorizontal = FMath::Tan(
			FMath::DegreesToRadians(OutputFovDegrees * 0.5));
		constexpr double AspectRatio = 16.0 / 9.0;
		const double TanHalfVertical = TanHalfHorizontal / AspectRatio;
		const double BirdTangentX = DesiredBirdNdc.X * TanHalfHorizontal;
		const double BirdTangentY = DesiredBirdNdc.Y * TanHalfVertical;
		if (TanHalfHorizontal <= UE_DOUBLE_SMALL_NUMBER
			|| TanHalfVertical <= UE_DOUBLE_SMALL_NUMBER)
		{
			return false;
		}

		const FVector SlowLocation = FMath::Lerp(
			WideTransform.GetLocation(),
			LucyTransform.GetLocation(),
			FMath::Clamp(DepthMatchAlpha, 0.0, 1.0));
		const double BirdForward = FVector::DotProduct(
			BirdPosition,
			ViewForward);
		const double BirdRight = FVector::DotProduct(
			BirdPosition,
			ViewRight);
		const double BirdUp = FVector::DotProduct(
			BirdPosition,
			ViewUp);
		const double CameraForward = FVector::DotProduct(
			SlowLocation,
			ViewForward);
		const double CameraRight =
			BirdRight - BirdTangentX * (BirdForward - CameraForward);
		const double CameraUp =
			BirdUp - BirdTangentY * (BirdForward - CameraForward);
		const double BirdDepth = BirdForward - CameraForward;
		if (!FMath::IsFinite(CameraForward)
			|| !FMath::IsFinite(CameraRight)
			|| !FMath::IsFinite(CameraUp)
			|| BirdDepth <= UE_DOUBLE_SMALL_NUMBER)
		{
			return false;
		}
		OutLocation = ViewForward * CameraForward
			+ ViewRight * CameraRight
			+ ViewUp * CameraUp;
		return IsFiniteFlightCameraVector(OutLocation);
	}

}

bool ABTSM11FinaleFlightCameraMath::BuildM4TerminalClosureFrame(
	const FVector& BirdPosition,
	const double BirdRadiusCM,
	const FVector& TargetCenter,
	const double TargetRadiusCM,
	const FVector& PreferredViewUp,
	const double HorizontalFovDegrees,
	const double FitMargin,
	const double ClosureProgress,
	const double BirdStartNdcY,
	const double BirdContactNdcY,
	const double StartCameraToBirdDistanceCM,
	const double ContactCameraToBirdDistanceCM,
	FTransform& OutTransform)
{
	OutTransform = FTransform::Identity;
	if (!IsFiniteFlightCameraVector(BirdPosition)
		|| !IsFiniteFlightCameraVector(TargetCenter)
		|| !IsFiniteFlightCameraVector(PreferredViewUp)
		|| PreferredViewUp.IsNearlyZero()
		|| !FMath::IsFinite(BirdRadiusCM)
		|| BirdRadiusCM <= 0.0
		|| !FMath::IsFinite(TargetRadiusCM)
		|| TargetRadiusCM <= 0.0
		|| !FMath::IsFinite(HorizontalFovDegrees)
		|| HorizontalFovDegrees <= 0.0
		|| !FMath::IsFinite(FitMargin)
		|| FitMargin <= 1.0
		|| !FMath::IsFinite(ClosureProgress)
		|| !FMath::IsFinite(BirdStartNdcY)
		|| !FMath::IsFinite(BirdContactNdcY)
		|| BirdStartNdcY >= BirdContactNdcY
		|| BirdContactNdcY >= 0.0
		|| !FMath::IsFinite(StartCameraToBirdDistanceCM)
		|| !FMath::IsFinite(ContactCameraToBirdDistanceCM)
		|| StartCameraToBirdDistanceCM
			<= ContactCameraToBirdDistanceCM
		|| ContactCameraToBirdDistanceCM <= BirdRadiusCM)
	{
		return false;
	}

	const FVector ApproachAxis =
		(TargetCenter - BirdPosition).GetSafeNormal();
	const double SubjectDistance = FVector::Distance(
		BirdPosition,
		TargetCenter);
	const FVector ViewPlaneReference = ResolveProjectedUp(
		PreferredViewUp,
		FVector::UpVector,
		ApproachAxis);
	const double TanHalfHorizontal = FMath::Tan(
		FMath::DegreesToRadians(HorizontalFovDegrees * 0.5));
	constexpr double AspectRatio = 16.0 / 9.0;
	const double TanHalfVertical = TanHalfHorizontal / AspectRatio;
	if (ApproachAxis.IsNearlyZero()
		|| ViewPlaneReference.IsNearlyZero()
		|| !FMath::IsFinite(SubjectDistance)
		|| SubjectDistance <= UE_DOUBLE_SMALL_NUMBER
		|| TanHalfHorizontal <= UE_DOUBLE_SMALL_NUMBER
		|| TanHalfVertical <= UE_DOUBLE_SMALL_NUMBER)
	{
		return false;
	}

	const double Progress = FMath::Clamp(ClosureProgress, 0.0, 1.0);
	// The NDC anchor deliberately remains linear. Equal playback-time steps
	// therefore move the bird by equal screen-space steps toward the UFO.
	const double DesiredBirdNdcY = FMath::Lerp(
		BirdStartNdcY,
		BirdContactNdcY,
		Progress);
	const double DesiredCameraToBirdDistance = FMath::Lerp(
		StartCameraToBirdDistanceCM,
		ContactCameraToBirdDistanceCM,
		SmootherStep01(Progress));
	const double BirdTangentY = DesiredBirdNdcY * TanHalfVertical;
	const double BirdDepth = DesiredCameraToBirdDistance
		/ FMath::Sqrt(1.0 + BirdTangentY * BirdTangentY);
	const double BirdUpOffset = BirdTangentY * BirdDepth;
	const double UpAxisFraction = -BirdUpOffset / SubjectDistance;
	if (!FMath::IsFinite(BirdDepth)
		|| BirdDepth <= BirdRadiusCM
		|| !FMath::IsFinite(UpAxisFraction)
		|| UpAxisFraction <= 0.0
		|| UpAxisFraction >= 1.0 - 1.0e-6)
	{
		return false;
	}

	const double ForwardAxisFraction = FMath::Sqrt(
		FMath::Max(0.0,
			1.0 - UpAxisFraction * UpAxisFraction));
	const FVector ViewForward = (
		ApproachAxis * ForwardAxisFraction
		- ViewPlaneReference * UpAxisFraction).GetSafeNormal();
	const FVector ViewUp = (
		ApproachAxis * UpAxisFraction
		+ ViewPlaneReference * ForwardAxisFraction).GetSafeNormal();
	const FQuat ViewRotation = FRotationMatrix::MakeFromXZ(
		ViewForward,
		ViewUp).ToQuat().GetNormalized();
	const double TargetDepth = BirdDepth
		+ SubjectDistance * ForwardAxisFraction;
	const FVector CameraLocation = TargetCenter - ViewForward * TargetDepth;
	if (ViewForward.IsNearlyZero()
		|| ViewUp.IsNearlyZero()
		|| !ViewRotation.IsNormalized()
		|| !IsFiniteFlightCameraVector(CameraLocation))
	{
		return false;
	}

	const double SafeNdcLimit = 1.0 / FitMargin;
	const auto SphereFits = [&] (
		const FVector& Center,
		const double Radius)
	{
		const FVector Relative = Center - CameraLocation;
		const double Depth = FVector::DotProduct(Relative, ViewForward);
		const double NearDepth = Depth - Radius;
		if (NearDepth <= UE_DOUBLE_SMALL_NUMBER)
		{
			return false;
		}
		const double CenterNdcX = FVector::DotProduct(
			Relative,
			ViewRotation.GetRightVector()) / (Depth * TanHalfHorizontal);
		const double CenterNdcY = FVector::DotProduct(
			Relative,
			ViewUp) / (Depth * TanHalfVertical);
		const double RadiusNdcX = Radius
			/ (NearDepth * TanHalfHorizontal);
		const double RadiusNdcY = Radius
			/ (NearDepth * TanHalfVertical);
		return FMath::IsFinite(CenterNdcX)
			&& FMath::IsFinite(CenterNdcY)
			&& FMath::Abs(CenterNdcX) + RadiusNdcX <= SafeNdcLimit
			&& FMath::Abs(CenterNdcY) + RadiusNdcY <= SafeNdcLimit;
	};
	if (!SphereFits(BirdPosition, BirdRadiusCM)
		|| !SphereFits(TargetCenter, TargetRadiusCM))
	{
		return false;
	}

	OutTransform = FTransform(ViewRotation, CameraLocation);
	return true;
}

bool FABTSM11FinaleFlightCameraFrame::IsUsable() const
{
	return IsFiniteFlightCameraVector(TrajectoryForward)
		&& IsFiniteFlightCameraVector(TransportedUp)
		&& IsFiniteFlightCameraVector(DesiredTransform.GetLocation())
		&& !TrajectoryForward.IsNearlyZero()
		&& !TransportedUp.IsNearlyZero()
		&& FMath::Abs(FVector::DotProduct(
			TrajectoryForward,
			TransportedUp)) <= 1.0e-3f
		&& DesiredTransform.GetRotation().IsNormalized();
}

bool FABTSM11FinaleCameraM2Settings::IsUsable() const
{
	return FMath::IsFinite(MaximumRetreatCM)
		&& MaximumRetreatCM >= 0.0
		&& FMath::IsFinite(CruiseLeadInStartFraction)
		&& CruiseLeadInStartFraction >= 0.0
		&& FMath::IsFinite(CruiseLeadInBlendFraction)
		&& CruiseLeadInBlendFraction > 0.0
		&& CruiseLeadInStartFraction + CruiseLeadInBlendFraction <= 1.0
		&& FMath::IsFinite(HandoffLeadInSeconds)
		&& HandoffLeadInSeconds > 0.0
		&& FMath::IsFinite(ApproachBrakeStartFraction)
		&& ApproachBrakeStartFraction >= 0.0
		&& ApproachBrakeStartFraction < 1.0
		&& FMath::IsFinite(ClosestRetreatFraction)
		&& ClosestRetreatFraction >= 0.0
		&& ClosestRetreatFraction <= 1.0
		&& FMath::IsFinite(PeriapsisReleaseFraction)
		&& PeriapsisReleaseFraction > 0.0
		&& PeriapsisReleaseFraction <= 1.0
		&& FMath::IsFinite(MinimumForegroundBirdDistanceCM)
		&& MinimumForegroundBirdDistanceCM > 0.0
		&& FMath::IsFinite(TransitCruiseFarOffsetRadii)
		&& TransitCruiseFarOffsetRadii > TransitEntryOffsetRadii
		&& FMath::IsFinite(TransitEntryOffsetRadii)
		&& TransitEntryOffsetRadii >= 1.0
		&& FMath::IsFinite(TransitClosestOffsetRadii)
		&& TransitClosestOffsetRadii >= 0.0
		&& TransitClosestOffsetRadii < 1.0
		&& FMath::IsFinite(TransitExitOffsetRadii)
		&& TransitExitOffsetRadii > TransitClosestOffsetRadii
		&& FMath::IsFinite(TransitVerticalOffsetRadii)
		&& FMath::Abs(TransitVerticalOffsetRadii) < 1.0
		&& FMath::IsFinite(TransitExitProgressFraction)
		&& TransitExitProgressFraction > 0.0
		&& TransitExitProgressFraction <= 1.0
		&& FMath::IsFinite(BaselineFovDegrees)
		&& BaselineFovDegrees > 0.0
		&& FMath::IsFinite(ClosestFovDegrees)
		&& ClosestFovDegrees > 0.0
		&& ClosestFovDegrees < BaselineFovDegrees
		&& FMath::IsFinite(PeriapsisFovRestoreFraction)
		&& PeriapsisFovRestoreFraction > 0.0
		&& PeriapsisFovRestoreFraction <= 1.0
		&& FMath::IsFinite(DualBodyBridgeFovDegrees)
		&& DualBodyBridgeFovDegrees >= BaselineFovDegrees
		&& DualBodyBridgeFovDegrees < 120.0
		&& FMath::IsFinite(DualBodyBridgeFitMargin)
		&& DualBodyBridgeFitMargin > 1.0
		&& FMath::IsFinite(DualBodyBridgeBirdNdcY)
		&& FMath::Abs(DualBodyBridgeBirdNdcY) <= 0.5
		&& FMath::IsFinite(TerminalFovDegrees)
		&& TerminalFovDegrees >= 35.0
		&& TerminalFovDegrees <= 85.0
		&& FMath::IsFinite(TerminalFitMargin)
		&& TerminalFitMargin > 1.0
		&& TerminalFitMargin <= 1.5
		&& FMath::IsFinite(TerminalBirdStartNdcY)
		&& FMath::IsFinite(TerminalBirdContactNdcY)
		&& TerminalBirdStartNdcY < TerminalBirdContactNdcY
		&& TerminalBirdContactNdcY < 0.0
		&& FMath::Abs(TerminalBirdStartNdcY) <= 0.8
		&& FMath::IsFinite(TerminalStartBirdDistanceCM)
		&& FMath::IsFinite(TerminalContactBirdDistanceCM)
		&& TerminalStartBirdDistanceCM > TerminalContactBirdDistanceCM
		&& TerminalContactBirdDistanceCM > 0.0
		&& FMath::IsFinite(DualBodyBridgeSeconds)
		&& DualBodyBridgeSeconds > 0.0
		&& FMath::IsFinite(IncomingMatchEaseOutPower)
		&& IncomingMatchEaseOutPower >= 1.0
		&& IncomingMatchEaseOutPower <= 4.0
		&& FMath::IsFinite(IncomingEntryMatchSeconds)
		&& IncomingEntryMatchSeconds > 0.0;
}

bool FABTSM11FinaleCameraM2Diagnostics::IsUsable() const
{
	return FMath::IsFinite(DirectorBlendAlpha)
		&& DirectorBlendAlpha >= 0.0
		&& DirectorBlendAlpha <= 1.0
		&& FMath::IsFinite(RetreatAlpha)
		&& RetreatAlpha >= 0.0
		&& RetreatAlpha <= 1.0
		&& FMath::IsFinite(TransitScreenXInTargetRadii)
		&& FMath::IsFinite(DirectedFovDegrees)
		&& DirectedFovDegrees > 0.0;
}

bool ABTSM11FinaleFlightCameraMath::BuildDesiredFrame(
	const FVector& TargetPosition,
	const FVector& TrajectoryTangent,
	const FVector& PreferredUp,
	const FVector& PreviousForward,
	const FVector& PreviousUp,
	const bool bHasPreviousFrame,
	const double FollowDistanceCM,
	const double FollowHeightCM,
	const double LookAheadDistanceCM,
	const double LookTargetHeightCM,
	FABTSM11FinaleFlightCameraFrame& OutFrame)
{
	OutFrame = FABTSM11FinaleFlightCameraFrame();
	if (!IsFiniteFlightCameraVector(TargetPosition)
		|| !IsFiniteFlightCameraVector(TrajectoryTangent)
		|| !IsFiniteFlightCameraVector(PreferredUp)
		|| !FMath::IsFinite(FollowDistanceCM)
		|| !FMath::IsFinite(FollowHeightCM)
		|| !FMath::IsFinite(LookAheadDistanceCM)
		|| !FMath::IsFinite(LookTargetHeightCM)
		|| FollowDistanceCM < 0.0)
	{
		return false;
	}

	const FVector Forward = TrajectoryTangent.GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		return false;
	}
	FVector CandidateUp = PreferredUp.GetSafeNormal();
	if (bHasPreviousFrame
		&& IsFiniteFlightCameraVector(PreviousForward)
		&& IsFiniteFlightCameraVector(PreviousUp)
		&& !PreviousForward.IsNearlyZero()
		&& !PreviousUp.IsNearlyZero())
	{
		const FQuat Transport = FQuat::FindBetweenNormals(
			PreviousForward.GetSafeNormal(),
			Forward);
		CandidateUp = Transport.RotateVector(
			PreviousUp.GetSafeNormal());
	}
	const FVector Up = ResolveProjectedUp(
		CandidateUp,
		PreferredUp,
		Forward);
	if (Up.IsNearlyZero())
	{
		return false;
	}

	const FVector DesiredLocation =
		TargetPosition
		- Forward * FollowDistanceCM
		+ Up * FollowHeightCM;
	const FVector LookTarget =
		TargetPosition
		+ Forward * LookAheadDistanceCM
		+ Up * LookTargetHeightCM;
	const FVector ViewForward =
		(LookTarget - DesiredLocation).GetSafeNormal();
	if (ViewForward.IsNearlyZero())
	{
		return false;
	}
	const FVector ViewUp = ResolveProjectedUp(
		Up,
		PreferredUp,
		ViewForward);
	if (ViewUp.IsNearlyZero())
	{
		return false;
	}

	OutFrame.TrajectoryForward = Forward;
	OutFrame.TransportedUp = Up;
	OutFrame.DesiredTransform = FTransform(
		FRotationMatrix::MakeFromXZ(
			ViewForward,
			ViewUp).ToQuat(),
		DesiredLocation);
	return OutFrame.IsUsable();
}

bool ABTSM11FinaleFlightCameraMath::BuildM2Assist1Frame(
	const FABTSM11FinaleFlightCameraFrame& BaselineFrame,
	const FVector& BirdPosition,
	const FABTSM11FinaleCameraDirectorSample& DirectorSample,
	const FABTSM11FinaleCameraM2Settings& Settings,
	FTransform& OutDirectedTransform,
	FABTSM11FinaleCameraM2Diagnostics& OutDiagnostics)
{
	if (!DirectorSample.Selection.IsM2Assist1Window())
	{
		OutDirectedTransform = BaselineFrame.DesiredTransform;
		OutDiagnostics = FABTSM11FinaleCameraM2Diagnostics();
		return false;
	}
	return BuildM3AssistFrame(
		BaselineFrame,
		BirdPosition,
		DirectorSample,
		Settings,
		OutDirectedTransform,
		OutDiagnostics);
}

bool ABTSM11FinaleFlightCameraMath::BuildM3DualBodyBridgeFrame(
	const FABTSM11FinaleFlightCameraFrame& BaselineFrame,
	const FVector& BirdPosition,
	const FABTSM11FinaleCameraDirectorSample& DirectorSample,
	const FABTSM11FinaleCameraM2Settings& Settings,
	FTransform& OutBridgeTransform)
{
	OutBridgeTransform = BaselineFrame.DesiredTransform;
	if (!BaselineFrame.IsUsable()
		|| !DirectorSample.IsUsable()
		|| !DirectorSample.Selection.IsM3InterBodyTransition()
		|| !IsFiniteFlightCameraVector(BirdPosition)
		|| !Settings.IsUsable())
	{
		return false;
	}

	const FVector BridgeRight =
		(DirectorSample.IncomingTargetCenter
			- DirectorSample.OutgoingTargetCenter).GetSafeNormal();
	// The bridge basis must not change when the primary framing target changes
	// from outgoing to incoming. The transported authority up is continuous;
	// either planet's encounter up is not.
	FVector BridgeUp = FVector::VectorPlaneProject(
		BaselineFrame.TransportedUp,
		BridgeRight).GetSafeNormal();
	FVector BridgeForward = FVector::CrossProduct(
		BridgeRight,
		BridgeUp).GetSafeNormal();
	if (BridgeRight.IsNearlyZero()
		|| BridgeUp.IsNearlyZero()
		|| BridgeForward.IsNearlyZero())
	{
		return false;
	}

	const FVector Origin =
		(DirectorSample.OutgoingTargetCenter
			+ DirectorSample.IncomingTargetCenter
			+ BirdPosition) / 3.0;
	double MinRight = TNumericLimits<double>::Max();
	double MaxRight = TNumericLimits<double>::Lowest();
	double MinUp = TNumericLimits<double>::Max();
	double MaxUp = TNumericLimits<double>::Lowest();
	double MaximumAbsoluteDepth = 0.0;
	const auto IncludeSphere = [&](const FVector& Center, const double Radius)
	{
		const FVector Relative = Center - Origin;
		const double Right = FVector::DotProduct(Relative, BridgeRight);
		const double Up = FVector::DotProduct(Relative, BridgeUp);
		const double Depth = FVector::DotProduct(Relative, BridgeForward);
		MinRight = FMath::Min(MinRight, Right - Radius);
		MaxRight = FMath::Max(MaxRight, Right + Radius);
		MinUp = FMath::Min(MinUp, Up - Radius);
		MaxUp = FMath::Max(MaxUp, Up + Radius);
		MaximumAbsoluteDepth = FMath::Max(
			MaximumAbsoluteDepth,
			FMath::Abs(Depth) + Radius);
	};
	IncludeSphere(
		DirectorSample.OutgoingTargetCenter,
		DirectorSample.OutgoingTargetRadiusCM);
	IncludeSphere(
		DirectorSample.IncomingTargetCenter,
		DirectorSample.IncomingTargetRadiusCM);
	IncludeSphere(BirdPosition, DirectorSample.BirdRadiusCM);

	const double HalfWidth = FMath::Max(
		FMath::Abs(MinRight),
		FMath::Abs(MaxRight)) * Settings.DualBodyBridgeFitMargin;
	const double HalfHeight = FMath::Max(
		FMath::Abs(MinUp),
		FMath::Abs(MaxUp)) * Settings.DualBodyBridgeFitMargin;
	const double TanHalfHorizontal = FMath::Tan(FMath::DegreesToRadians(
		Settings.DualBodyBridgeFovDegrees * 0.5));
	constexpr double BridgeAspectRatio = 16.0 / 9.0;
	const double TanHalfVertical = TanHalfHorizontal / BridgeAspectRatio;
	if (!FMath::IsFinite(HalfWidth)
		|| !FMath::IsFinite(HalfHeight)
		|| TanHalfHorizontal <= UE_DOUBLE_SMALL_NUMBER
		|| TanHalfVertical <= UE_DOUBLE_SMALL_NUMBER)
	{
		return false;
	}
	const double FitDistance = FMath::Max(
		HalfWidth / TanHalfHorizontal,
		HalfHeight / TanHalfVertical) + MaximumAbsoluteDepth;
	const FVector Focus = Origin;
	const FVector BridgeLocation = Focus - BridgeForward * FitDistance;
	const FQuat BridgeRotation = FRotationMatrix::MakeFromXZ(
		BridgeForward,
		BridgeUp).ToQuat();
	const FTransform UnanchoredBridgeTransform(
		BridgeRotation,
		BridgeLocation);
	return BridgeRotation.IsNormalized()
		&& BuildM3BirdYAnchoredSubjectFrame(
			UnanchoredBridgeTransform,
			Settings.DualBodyBridgeFovDegrees,
			Settings.DualBodyBridgeFitMargin,
			Settings.DualBodyBridgeBirdNdcY,
			BirdPosition,
			DirectorSample.BirdRadiusCM,
			DirectorSample.OutgoingTargetCenter,
			DirectorSample.OutgoingTargetRadiusCM,
			&DirectorSample.IncomingTargetCenter,
			DirectorSample.IncomingTargetRadiusCM,
			OutBridgeTransform);
}

bool ABTSM11FinaleFlightCameraMath::BuildM3AssistFrame(
	const FABTSM11FinaleFlightCameraFrame& BaselineFrame,
	const FVector& BirdPosition,
	const FABTSM11FinaleCameraDirectorSample& DirectorSample,
	const FABTSM11FinaleCameraM2Settings& Settings,
	FTransform& OutDirectedTransform,
	FABTSM11FinaleCameraM2Diagnostics& OutDiagnostics)
{
	OutDirectedTransform = BaselineFrame.DesiredTransform;
	OutDiagnostics = FABTSM11FinaleCameraM2Diagnostics();
	if (!BaselineFrame.IsUsable()
		|| !DirectorSample.IsUsable()
		|| (!DirectorSample.Selection.IsM3AssistWindow()
			&& !DirectorSample.Selection.IsM4TerminalWindow())
		|| !IsFiniteFlightCameraVector(BirdPosition)
		|| !Settings.IsUsable())
	{
		return false;
	}

	const double Progress = DirectorSample.Selection.StageProgress;
	if (DirectorSample.Selection.IsM3InterBodyTransition()
		|| (DirectorSample.Selection.IsM4TerminalWindow()
			&& !DirectorSample.Selection.IsM4TerminalTransition()))
	{
		// Every inter-body phase is one deterministic composition chain. Blending
		// any of it back toward the chase camera would create a second authority at
		// the exact boundaries this chain is designed to make continuous.
		OutDiagnostics.DirectorBlendAlpha = 1.0;
	}
	else if (DirectorSample.Selection.IsM3IncomingShot())
	{
		const double ShotElapsedSeconds =
			DirectorSample.Selection.ShotProgress
				* DirectorSample.Selection.ShotDurationSeconds;
		OutDiagnostics.DirectorBlendAlpha = SmootherStep01(
			ShotElapsedSeconds / Settings.HandoffLeadInSeconds);
		OutDiagnostics.RetreatAlpha = 0.0;
	}
	else if (DirectorSample.Selection.Stage
		== EABTSM11FinaleCameraStage::CruiseToBody)
	{
		OutDiagnostics.DirectorBlendAlpha = SmootherStep01(
			(Progress - Settings.CruiseLeadInStartFraction)
			/ Settings.CruiseLeadInBlendFraction);
	}
	else if (DirectorSample.Selection.Stage
		== EABTSM11FinaleCameraStage::Handoff)
	{
		const double HandoffElapsedSeconds = Progress
			* DirectorSample.Selection.StageDurationSeconds;
		OutDiagnostics.DirectorBlendAlpha = SmootherStep01(
			HandoffElapsedSeconds / Settings.HandoffLeadInSeconds);
	}
	else if (DirectorSample.Selection.Stage
		== EABTSM11FinaleCameraStage::Approach)
	{
		OutDiagnostics.DirectorBlendAlpha = 1.0;
		OutDiagnostics.RetreatAlpha =
			Settings.ClosestRetreatFraction * SmootherStep01(
				(Progress - Settings.ApproachBrakeStartFraction)
					/ (1.0 - Settings.ApproachBrakeStartFraction));
	}
	else
	{
		OutDiagnostics.DirectorBlendAlpha =
			Settings.bReleaseDirectorDuringPeriapsis
				? SmootherStep01((1.0 - Progress) / 0.25)
				: 1.0;
		OutDiagnostics.RetreatAlpha =
			Settings.ClosestRetreatFraction
			+ (1.0 - Settings.ClosestRetreatFraction)
				* SmootherStep01(
					Progress / Settings.PeriapsisReleaseFraction);
	}
	OutDiagnostics.TransitScreenXInTargetRadii =
		ResolveTransitScreenXInTargetRadii(
			DirectorSample.Selection,
			Settings);
	OutDiagnostics.DirectedFovDegrees = ResolveDirectedFovDegrees(
		DirectorSample.Selection,
		Settings);

	const FVector BaselineLocation =
		BaselineFrame.DesiredTransform.GetLocation();
	const double BaselineBirdDistance =
		FMath::Max(1.0, FVector::Distance(
			BaselineLocation,
			BirdPosition));
	const double DirectedBirdDistance =
		FMath::Max(
			BaselineBirdDistance,
			Settings.MinimumForegroundBirdDistanceCM)
		+ Settings.MaximumRetreatCM * OutDiagnostics.RetreatAlpha;

	const double EncounterOrientationAlpha =
		ResolveEncounterOrientationAlpha(
			DirectorSample.Selection,
			OutDiagnostics.DirectorBlendAlpha);
	const FVector DirectedUp = SlerpDirection(
		BaselineFrame.TransportedUp,
		DirectorSample.EncounterScreenUp,
		EncounterOrientationAlpha);

	// Solve the foreground transit in the camera's actual planet-centred
	// projection plane. A frozen world-space chord is not sufficient: when the
	// camera turns, that chord can acquire depth and collapse a requested
	// 1.6-radius entry to a nearly central screen point. Iterating the plane
	// basis and camera ray makes the requested left/lower chord self-consistent.
	FVector DirectedLocation = BirdPosition
		+ (BirdPosition - DirectorSample.TargetCenter).GetSafeNormal()
			* DirectedBirdDistance;
	if (DirectedLocation.Equals(BirdPosition))
	{
		return false;
	}
	for (int32 Iteration = 0; Iteration < 4; ++Iteration)
	{
		FQuat PlaneRotation = FQuat::Identity;
		if (!BuildM2PlanetAnchoredRotation(
			DirectedLocation,
			DirectedUp,
			DirectorSample.TargetCenter,
			PlaneRotation))
		{
			return false;
		}
		const FVector TargetPlanePoint = DirectorSample.TargetCenter
			+ PlaneRotation.GetRightVector()
				* OutDiagnostics.TransitScreenXInTargetRadii
				* DirectorSample.TargetRadiusCM
			+ PlaneRotation.GetUpVector()
				* Settings.TransitVerticalOffsetRadii
				* DirectorSample.TargetRadiusCM;
		const FVector DirectedOffsetDirection =
			(BirdPosition - TargetPlanePoint).GetSafeNormal();
		if (DirectedOffsetDirection.IsNearlyZero())
		{
			return false;
		}
		DirectedLocation = BirdPosition
			+ DirectedOffsetDirection * DirectedBirdDistance;
	}
	FQuat DirectedRotation = FQuat::Identity;
	if (!BuildM2PlanetAnchoredRotation(
		DirectedLocation,
		DirectedUp,
		DirectorSample.TargetCenter,
		DirectedRotation))
	{
		return false;
	}
	OutDirectedTransform = FTransform(
		DirectedRotation,
		DirectedLocation);
	if (DirectorSample.Selection.IsM4TerminalWindow())
	{
		const FTransform LucyTransform = OutDirectedTransform;
		if (DirectorSample.Selection.IsM4TerminalTransition())
		{
			const double TerminalProgress = FMath::Clamp(
				DirectorSample.Selection.ShotPhaseProgress,
				0.0,
				1.0);
			const double AuthorityFovDegrees =
				OutDiagnostics.DirectedFovDegrees;
			const FVector BridgeRight = (
				DirectorSample.TerminalTargetCenter
					- DirectorSample.TargetCenter).GetSafeNormal();
			const FVector BridgeUp = (
				DirectorSample.TerminalScreenUp
				- BridgeRight * FVector::DotProduct(
					DirectorSample.TerminalScreenUp,
					BridgeRight)).GetSafeNormal();
			const FQuat BridgeRotation = FRotationMatrix::MakeFromYZ(
				BridgeRight,
				BridgeUp).ToQuat().GetNormalized();
			if (BridgeRight.IsNearlyZero()
				|| BridgeUp.IsNearlyZero()
				|| !BridgeRotation.IsNormalized())
			{
				return false;
			}
			if (TerminalProgress <= 0.5)
			{
				const double Alpha = SmootherStep01(
					TerminalProgress / 0.5);
				OutDiagnostics.DirectedFovDegrees = FMath::Lerp(
					AuthorityFovDegrees,
					Settings.DualBodyBridgeFovDegrees,
					Alpha);
				const FQuat AcquireRotation = FQuat::Slerp(
					LucyTransform.GetRotation(),
					BridgeRotation,
					Alpha).GetNormalized();
				FTransform ThreeSubjectTransform;
				if (!BuildM3FittedSubjectFrame(
					AcquireRotation,
					OutDiagnostics.DirectedFovDegrees,
					Settings.DualBodyBridgeFitMargin,
					BirdPosition,
					DirectorSample.BirdRadiusCM,
					DirectorSample.TerminalTargetCenter,
					DirectorSample.TerminalTargetRadiusCM,
					&DirectorSample.TargetCenter,
					DirectorSample.TargetRadiusCM,
					ThreeSubjectTransform))
				{
					return false;
				}
				OutDirectedTransform = FTransform(
					AcquireRotation,
					FMath::Lerp(
						LucyTransform.GetLocation(),
						ThreeSubjectTransform.GetLocation(),
						Alpha));
			}
			else
			{
				const double Alpha = SmootherStep01(
					(TerminalProgress - 0.5) / 0.5);
				OutDiagnostics.DirectedFovDegrees = FMath::Lerp(
					Settings.DualBodyBridgeFovDegrees,
					Settings.TerminalFovDegrees,
					Alpha);
				FTransform TerminalClosureTransform;
				if (!BuildM4TerminalClosureFrame(
					BirdPosition,
					DirectorSample.BirdRadiusCM,
					DirectorSample.TerminalTargetCenter,
					DirectorSample.TerminalTargetRadiusCM,
					DirectorSample.TerminalScreenUp,
					Settings.TerminalFovDegrees,
					Settings.TerminalFitMargin,
					0.0,
					Settings.TerminalBirdStartNdcY,
					Settings.TerminalBirdContactNdcY,
					Settings.TerminalStartBirdDistanceCM,
					Settings.TerminalContactBirdDistanceCM,
					TerminalClosureTransform))
				{
					return false;
				}
				const FQuat AcquireRotation = FQuat::Slerp(
					BridgeRotation,
					TerminalClosureTransform.GetRotation(),
					Alpha).GetNormalized();
				FTransform ThreeSubjectTransform;
				if (!BuildM3FittedSubjectFrame(
					AcquireRotation,
					OutDiagnostics.DirectedFovDegrees,
					Settings.DualBodyBridgeFitMargin,
					BirdPosition,
					DirectorSample.BirdRadiusCM,
					DirectorSample.TerminalTargetCenter,
					DirectorSample.TerminalTargetRadiusCM,
					&DirectorSample.TargetCenter,
					DirectorSample.TargetRadiusCM,
					ThreeSubjectTransform))
				{
					return false;
				}
				OutDirectedTransform = FTransform(
					AcquireRotation,
					FMath::Lerp(
						ThreeSubjectTransform.GetLocation(),
						TerminalClosureTransform.GetLocation(),
						Alpha));
			}
		}
		else
		{
			OutDiagnostics.DirectedFovDegrees = Settings.TerminalFovDegrees;
			if (!BuildM4TerminalClosureFrame(
				BirdPosition,
				DirectorSample.BirdRadiusCM,
				DirectorSample.TerminalTargetCenter,
				DirectorSample.TerminalTargetRadiusCM,
				DirectorSample.TerminalScreenUp,
				Settings.TerminalFovDegrees,
				Settings.TerminalFitMargin,
				DirectorSample.Selection.StageProgress,
				Settings.TerminalBirdStartNdcY,
				Settings.TerminalBirdContactNdcY,
				Settings.TerminalStartBirdDistanceCM,
				Settings.TerminalContactBirdDistanceCM,
				OutDirectedTransform))
			{
				return false;
			}
		}
		OutDiagnostics.DirectorBlendAlpha = 1.0;
		return IsFiniteFlightCameraVector(
				OutDirectedTransform.GetLocation())
			&& OutDirectedTransform.GetRotation().IsNormalized()
			&& OutDiagnostics.IsUsable();
	}
	if (DirectorSample.Selection.IsM3InterBodyTransition())
	{
		const FTransform LucyTransform = OutDirectedTransform;
		FTransform BridgeTransform;
		if (!BuildM3DualBodyBridgeFrame(
			BaselineFrame,
			BirdPosition,
			DirectorSample,
			Settings,
			BridgeTransform))
		{
			return false;
		}
		const double PhaseBlendAlpha =
			DirectorSample.Selection.ShotPhase
				== EABTSM11FinaleCameraShotPhase::OutgoingHold
				? SmootherStep01(
					DirectorSample.Selection.ShotPhaseProgress)
				: SmoothStep01(
					DirectorSample.Selection.ShotPhaseProgress);
		if (DirectorSample.Selection.ShotPhase
			== EABTSM11FinaleCameraShotPhase::OutgoingHold)
		{
			const FQuat PullbackRotation = FQuat::Slerp(
				LucyTransform.GetRotation(),
				BridgeTransform.GetRotation(),
				PhaseBlendAlpha).GetNormalized();
			FTransform OutgoingPairTransform;
			FTransform ThreeSubjectTransform;
			if (!BuildM3FittedSubjectFrame(
				PullbackRotation,
				OutDiagnostics.DirectedFovDegrees,
				Settings.DualBodyBridgeFitMargin,
				BirdPosition,
				DirectorSample.BirdRadiusCM,
				DirectorSample.OutgoingTargetCenter,
				DirectorSample.OutgoingTargetRadiusCM,
				nullptr,
				0.0,
				OutgoingPairTransform)
				|| !BuildM3FittedSubjectFrame(
					PullbackRotation,
					OutDiagnostics.DirectedFovDegrees,
					Settings.DualBodyBridgeFitMargin,
					BirdPosition,
					DirectorSample.BirdRadiusCM,
					DirectorSample.IncomingTargetCenter,
					DirectorSample.IncomingTargetRadiusCM,
					&DirectorSample.OutgoingTargetCenter,
					DirectorSample.OutgoingTargetRadiusCM,
					ThreeSubjectTransform))
			{
				return false;
			}
			const FVector PairAnchoredLocation = FMath::Lerp(
				LucyTransform.GetLocation(),
				OutgoingPairTransform.GetLocation(),
				PhaseBlendAlpha);
			const double IncomingFitAlpha = SmoothStep01(
				(DirectorSample.Selection.ShotPhaseProgress - 0.35) / 0.65);
			const FTransform UnanchoredOutgoingTransform(
				PullbackRotation,
				FMath::Lerp(
					PairAnchoredLocation,
					ThreeSubjectTransform.GetLocation(),
					IncomingFitAlpha));
			FVector2D LucyBirdNdc;
			if (!ProjectSubjectToNdc(
				LucyTransform,
				OutDiagnostics.DirectedFovDegrees,
				BirdPosition,
				LucyBirdNdc))
			{
				return false;
			}
			const double DesiredBirdNdcY = FMath::Lerp(
				LucyBirdNdc.Y,
				Settings.DualBodyBridgeBirdNdcY,
				PhaseBlendAlpha);
			// Introduce the incoming fit continuously. Before it is narratively
			// present, a virtual zero-radius subject stays on the outgoing body;
			// at the bridge boundary it has become the real incoming planet.
			const FVector ActiveIncomingCenter = FMath::Lerp(
				DirectorSample.OutgoingTargetCenter,
				DirectorSample.IncomingTargetCenter,
				IncomingFitAlpha);
			const double ActiveIncomingRadius =
				DirectorSample.IncomingTargetRadiusCM * IncomingFitAlpha;
			if (!BuildM3BirdYAnchoredSubjectFrame(
				UnanchoredOutgoingTransform,
				OutDiagnostics.DirectedFovDegrees,
				Settings.DualBodyBridgeFitMargin,
				DesiredBirdNdcY,
				BirdPosition,
				DirectorSample.BirdRadiusCM,
				DirectorSample.OutgoingTargetCenter,
				DirectorSample.OutgoingTargetRadiusCM,
				&ActiveIncomingCenter,
				ActiveIncomingRadius,
				OutDirectedTransform))
			{
				return false;
			}
		}
		else if (DirectorSample.Selection.IsM3DualBodyBridge())
		{
			OutDirectedTransform = BridgeTransform;
		}
		else if (DirectorSample.Selection.ShotPhase
			== EABTSM11FinaleCameraShotPhase::IncomingReveal)
		{
			const FQuat RevealRotation = FQuat::Slerp(
				BridgeTransform.GetRotation(),
				LucyTransform.GetRotation(),
				PhaseBlendAlpha).GetNormalized();
			FTransform UnanchoredRevealTransform;
			if (!BuildM3FittedSubjectFrame(
				RevealRotation,
				Settings.DualBodyBridgeFovDegrees,
				Settings.DualBodyBridgeFitMargin,
				BirdPosition,
				DirectorSample.BirdRadiusCM,
				DirectorSample.IncomingTargetCenter,
				DirectorSample.IncomingTargetRadiusCM,
				&DirectorSample.OutgoingTargetCenter,
				DirectorSample.OutgoingTargetRadiusCM,
				UnanchoredRevealTransform))
			{
				return false;
			}
			FVector2D UnanchoredBirdNdc;
			if (!ProjectSubjectToNdc(
				UnanchoredRevealTransform,
				Settings.DualBodyBridgeFovDegrees,
				BirdPosition,
				UnanchoredBirdNdc))
			{
				return false;
			}
			const double DesiredBirdNdcY = FMath::Lerp(
				Settings.DualBodyBridgeBirdNdcY,
				UnanchoredBirdNdc.Y,
				PhaseBlendAlpha);
			if (!BuildM3BirdYAnchoredSubjectFrame(
				UnanchoredRevealTransform,
				Settings.DualBodyBridgeFovDegrees,
				Settings.DualBodyBridgeFitMargin,
				DesiredBirdNdcY,
				BirdPosition,
				DirectorSample.BirdRadiusCM,
				DirectorSample.IncomingTargetCenter,
				DirectorSample.IncomingTargetRadiusCM,
				&DirectorSample.OutgoingTargetCenter,
				DirectorSample.OutgoingTargetRadiusCM,
				OutDirectedTransform))
			{
				return false;
			}
		}
		else if (DirectorSample.Selection.ShotPhase
			== EABTSM11FinaleCameraShotPhase::IncomingTrack)
		{
			const double EntryAnchorAlpha =
				ResolveIncomingEntryAnchorAlpha(
					DirectorSample.Selection,
					Settings);
			const double DepthMatchAlpha =
				ResolveIncomingDepthMatchAlpha(
					DirectorSample.Selection);
			const FQuat TrackRotation = LucyTransform.GetRotation();
			FTransform AlignedThreeSubjectTransform;
			if (!BuildM3FittedSubjectFrame(
				TrackRotation,
				Settings.DualBodyBridgeFovDegrees,
				Settings.DualBodyBridgeFitMargin,
				BirdPosition,
				DirectorSample.BirdRadiusCM,
				DirectorSample.IncomingTargetCenter,
				DirectorSample.IncomingTargetRadiusCM,
				&DirectorSample.OutgoingTargetCenter,
				DirectorSample.OutgoingTargetRadiusCM,
				AlignedThreeSubjectTransform))
			{
				return false;
			}
			const FVector LocationDelta =
				LucyTransform.GetLocation()
					- AlignedThreeSubjectTransform.GetLocation();
			const FVector ForwardDelta = TrackRotation.GetForwardVector()
				* FVector::DotProduct(
					LocationDelta,
					TrackRotation.GetForwardVector());
			const FVector ViewPlaneDelta = LocationDelta - ForwardDelta;
			FVector SplitAnchorLocation;
			const bool bBuiltSplitAnchor = BuildM3BirdAnchorLocation(
				AlignedThreeSubjectTransform,
				OutDiagnostics.DirectedFovDegrees,
				LucyTransform,
				Settings.BaselineFovDegrees,
				OutDiagnostics.DirectedFovDegrees,
				EntryAnchorAlpha,
				DepthMatchAlpha,
				BirdPosition,
				SplitAnchorLocation);
			OutDirectedTransform = FTransform(
				TrackRotation,
				bBuiltSplitAnchor
					? SplitAnchorLocation
					: AlignedThreeSubjectTransform.GetLocation()
						+ ViewPlaneDelta * EntryAnchorAlpha
						+ ForwardDelta * DepthMatchAlpha);
		}
		// IncomingEntryMatch retains the exact Lucy transform. Authority/Approach
		// starts from that same solver at entry, so the final boundary cannot switch
		// basis or release accumulated lag.
		OutDiagnostics.DirectorBlendAlpha = 1.0;
	}
	return IsFiniteFlightCameraVector(OutDirectedTransform.GetLocation())
		&& OutDirectedTransform.GetRotation().IsNormalized()
		&& OutDiagnostics.IsUsable();
}

bool ABTSM11FinaleFlightCameraMath::BuildM2PlanetAnchoredRotation(
	const FVector& CameraLocation,
	const FVector& PreferredUp,
	const FVector& TargetCenter,
	FQuat& OutRotation)
{
	OutRotation = FQuat::Identity;
	if (!IsFiniteFlightCameraVector(CameraLocation)
		|| !IsFiniteFlightCameraVector(PreferredUp)
		|| !IsFiniteFlightCameraVector(TargetCenter))
	{
		return false;
	}
	const FVector ViewDirection =
		(TargetCenter - CameraLocation).GetSafeNormal();
	if (ViewDirection.IsNearlyZero())
	{
		return false;
	}
	const FVector ViewUp = ResolveProjectedUp(
		PreferredUp,
		FVector::UpVector,
		ViewDirection);
	if (ViewDirection.IsNearlyZero() || ViewUp.IsNearlyZero())
	{
		return false;
	}
	OutRotation = FRotationMatrix::MakeFromXZ(
		ViewDirection,
		ViewUp).ToQuat();
	return OutRotation.IsNormalized();
}

bool ABTSM11FinaleFlightCameraMath::BuildM3LaunchReleaseLocation(
	const FVector& SafeLocation,
	const FVector& BirdPosition,
	const FVector& DirectedLocation,
	const double ReleaseAlpha,
	FVector& OutLocation)
{
	OutLocation = FVector::ZeroVector;
	if (!IsFiniteFlightCameraVector(SafeLocation)
		|| !IsFiniteFlightCameraVector(BirdPosition)
		|| !IsFiniteFlightCameraVector(DirectedLocation)
		|| !FMath::IsFinite(ReleaseAlpha))
	{
		return false;
	}
	const double ClampedReleaseAlpha = FMath::Clamp(
		ReleaseAlpha,
		0.0,
		1.0);
	const FVector ReleasedOffsetDirection = SlerpDirection(
		SafeLocation - BirdPosition,
		DirectedLocation - BirdPosition,
		ClampedReleaseAlpha);
	const double ReleasedOffsetDistance = FMath::Lerp(
		FVector::Distance(SafeLocation, BirdPosition),
		FVector::Distance(DirectedLocation, BirdPosition),
		ClampedReleaseAlpha);
	OutLocation = BirdPosition
		+ ReleasedOffsetDirection * ReleasedOffsetDistance;
	return IsFiniteFlightCameraVector(OutLocation);
}

AABTSM11FinaleFlightCamera::AABTSM11FinaleFlightCamera()
{
	PrimaryActorTick.bCanEverTick = false;
	GetCameraComponent()->SetFieldOfView(
		static_cast<float>(BaselineFovDegrees));
}

void AABTSM11FinaleFlightCamera::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	RestoreFinaleAntiAliasingOverride();
	Super::EndPlay(EndPlayReason);
}

bool AABTSM11FinaleFlightCamera::BeginAuthorityFollow(
	const FVector& TargetPosition,
	const FVector& TrajectoryTangent,
	const FVector& PreferredUp,
	const FTransform& InitialViewTransform,
	const EABTSM11FinaleCameraDirectorMode DirectorMode)
{
	ResetAuthorityFollow();
	FABTSM11FinaleFlightCameraFrame Frame;
	if (!BuildAuthorityFrame(
		TargetPosition,
		TrajectoryTangent,
		PreferredUp,
		Frame))
	{
		return false;
	}
	SetActorTransform(
		InitialViewTransform,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	LastAuthorityForward = Frame.TrajectoryForward;
	LastTransportedUp = Frame.TransportedUp;
	LastAuthorityTargetPosition = TargetPosition;
	bM2DirectorFrozenEnabled = DirectorMode
		== EABTSM11FinaleCameraDirectorMode::Assist1OnlyM2;
	bM3DirectorFrozenEnabled = DirectorMode
		== EABTSM11FinaleCameraDirectorMode::MultiAssistM3;
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M11-C][FlightCamera] DirectorFrozen M2=%d M3=%d Mode=%s"),
		bM2DirectorFrozenEnabled ? 1 : 0,
		bM3DirectorFrozenEnabled ? 1 : 0,
		ABTSM11FinaleCameraDirector::DirectorModeLabel(DirectorMode));
	if (bM2DirectorFrozenEnabled && !bM3DirectorFrozenEnabled)
	{
		UE_LOG(
			LogABTSRuntime,
			Warning,
			TEXT("[ABTS][M11-C][FlightCamera] M2OnlyFirstAssist: M2 directs Assist1 only; set abts.M11.CameraDirector.M2.Enabled 0 and abts.M11.CameraDirector.M3.Enabled 1 before release for the complete finale camera."));
	}
	LastM2BlendAlpha = 0.0;
	LastM2RetreatAlpha = 0.0;
	LastM2TransitScreenXInTargetRadii = -M2TransitCruiseFarOffsetRadii;
	GetCameraComponent()->SetFieldOfView(
		static_cast<float>(BaselineFovDegrees));
	LastDirectorStage = EABTSM11FinaleCameraStage::PreLaunch;
	ActivateFinaleAntiAliasingOverride();
	bAuthorityFollowActive = true;
	return true;
}

bool AABTSM11FinaleFlightCamera::ApplyM6FormationSafetyEnvelope(
	const FVector& PrimaryTargetPosition,
	const TConstArrayView<FABTSM11FinaleFormationCameraSubject>
		FormationSubjects,
	const FQuat& CameraRotation,
	const double HorizontalFovDegrees,
	FVector& InOutCameraLocation) const
{
	if (FormationSubjects.Num() <= 1)
	{
		return true;
	}
	constexpr double AspectRatio = 16.0 / 9.0;
	constexpr double SafeHorizontalNdc = 0.88;
	constexpr double SafeVerticalNdc = 0.84;
	constexpr double MaximumRetreatCM = 30000.0;
	const double TanHalfHorizontal = FMath::Tan(
		FMath::DegreesToRadians(HorizontalFovDegrees * 0.5));
	const double TanHalfVertical = TanHalfHorizontal / AspectRatio;
	if (!FMath::IsFinite(TanHalfHorizontal)
		|| !FMath::IsFinite(TanHalfVertical)
		|| TanHalfHorizontal <= UE_DOUBLE_SMALL_NUMBER
		|| TanHalfVertical <= UE_DOUBLE_SMALL_NUMBER)
	{
		return false;
	}
	const FVector Forward = CameraRotation.GetForwardVector();
	const FVector Right = CameraRotation.GetRightVector();
	const FVector Up = CameraRotation.GetUpVector();
	const FVector PrimaryOffset = PrimaryTargetPosition - InOutCameraLocation;
	const double PrimaryDepth = FVector::DotProduct(PrimaryOffset, Forward);
	const double PrimaryRight = FVector::DotProduct(PrimaryOffset, Right);
	const double PrimaryUp = FVector::DotProduct(PrimaryOffset, Up);
	if (!FMath::IsFinite(PrimaryDepth)
		|| PrimaryDepth <= UE_DOUBLE_SMALL_NUMBER)
	{
		return false;
	}
	const FVector BaseLocation = InOutCameraLocation;
	const auto BuildCandidateLocation = [&](const double RetreatCM)
	{
		// Retreat along the primary ray, not only along camera forward. This
		// preserves the director's primary-bird NDC anchor while creating room
		// for the three followers.
		return BaseLocation
			- Forward * RetreatCM
			- Right * (PrimaryRight * RetreatCM / PrimaryDepth)
			- Up * (PrimaryUp * RetreatCM / PrimaryDepth);
	};
	const auto Fits = [&](const double RetreatCM)
	{
		const FVector CandidateLocation = BuildCandidateLocation(RetreatCM);
		for (const FABTSM11FinaleFormationCameraSubject& Subject
			: FormationSubjects)
		{
			if (!Subject.IsUsable())
			{
				return false;
			}
			const FVector Offset = Subject.Center - CandidateLocation;
			const double Depth = FVector::DotProduct(Offset, Forward);
			if (Depth <= Subject.RadiusCM + 1.0)
			{
				return false;
			}
			const double ConservativeDepth = Depth - Subject.RadiusCM;
			const double CenterX = FVector::DotProduct(Offset, Right)
				/ (Depth * TanHalfHorizontal);
			const double CenterY = FVector::DotProduct(Offset, Up)
				/ (Depth * TanHalfVertical);
			const double RadiusX = Subject.RadiusCM
				/ (ConservativeDepth * TanHalfHorizontal);
			const double RadiusY = Subject.RadiusCM
				/ (ConservativeDepth * TanHalfVertical);
			if (FMath::Abs(CenterX) + RadiusX > SafeHorizontalNdc
				|| FMath::Abs(CenterY) + RadiusY > SafeVerticalNdc)
			{
				return false;
			}
		}
		return true;
	};
	if (Fits(0.0))
	{
		return true;
	}
	if (!Fits(MaximumRetreatCM))
	{
		return false;
	}
	double Low = 0.0;
	double High = MaximumRetreatCM;
	for (int32 Iteration = 0; Iteration < 32; ++Iteration)
	{
		const double Mid = (Low + High) * 0.5;
		if (Fits(Mid))
		{
			High = Mid;
		}
		else
		{
			Low = Mid;
		}
	}
	InOutCameraLocation = BuildCandidateLocation(High);
	return !InOutCameraLocation.ContainsNaN();
}

bool AABTSM11FinaleFlightCamera::UpdateAuthoritySample(
	const FVector& TargetPosition,
	const FVector& TrajectoryTangent,
	const FVector& PreferredUp,
	const FABTSM11FinaleCameraDirectorSample* DirectorSample,
	const float DeltaSeconds,
	const TConstArrayView<FABTSM11FinaleFormationCameraSubject>
		FormationSubjects)
{
	if (!bAuthorityFollowActive)
	{
		return false;
	}
	EnsureFinaleAntiAliasingOverride();
	const FVector AuthorityTargetTranslation =
		TargetPosition - LastAuthorityTargetPosition;
	FABTSM11FinaleFlightCameraFrame Frame;
	if (!BuildAuthorityFrame(
		TargetPosition,
		TrajectoryTangent,
		PreferredUp,
		Frame))
	{
		return false;
	}
	LastAuthorityForward = Frame.TrajectoryForward;
	LastTransportedUp = Frame.TransportedUp;
	const double PreviousDirectorBlendAlpha = LastM2BlendAlpha;
	LastM2BlendAlpha = 0.0;
	LastM2RetreatAlpha = 0.0;
	LastM2TransitScreenXInTargetRadii = -M2TransitCruiseFarOffsetRadii;
	LastDirectorStage = DirectorSample != nullptr
		? DirectorSample->Selection.Stage
		: EABTSM11FinaleCameraStage::Unavailable;
	FTransform DesiredTransform = Frame.DesiredTransform;
	double DesiredFovDegrees = BaselineFovDegrees;
	const bool bM3Window = bM3DirectorFrozenEnabled
		&& DirectorSample != nullptr
		&& (DirectorSample->Selection.IsM3AssistWindow()
			|| DirectorSample->Selection.IsM4TerminalWindow());
	const bool bM2Window = bM2DirectorFrozenEnabled
		&& DirectorSample != nullptr
		&& DirectorSample->Selection.IsM2Assist1Window();
	if (bM3Window || bM2Window)
	{
		FTransform DirectedTransform;
		FABTSM11FinaleCameraM2Settings Settings;
		Settings.MaximumRetreatCM = M2MaximumRetreatCM;
		Settings.CruiseLeadInStartFraction =
			M2CruiseLeadInStartFraction;
		Settings.CruiseLeadInBlendFraction =
			M2CruiseLeadInBlendFraction;
		Settings.HandoffLeadInSeconds = M3HandoffLeadInSeconds;
		Settings.bReleaseDirectorDuringPeriapsis = !bM3Window;
		Settings.ApproachBrakeStartFraction =
			M2ApproachBrakeStartFraction;
		Settings.ClosestRetreatFraction =
			M2ClosestRetreatFraction;
		Settings.PeriapsisReleaseFraction =
			M2PeriapsisReleaseFraction;
		Settings.MinimumForegroundBirdDistanceCM =
			M2MinimumForegroundBirdDistanceCM;
		Settings.TransitCruiseFarOffsetRadii =
			M2TransitCruiseFarOffsetRadii;
		Settings.TransitEntryOffsetRadii =
			M2TransitEntryOffsetRadii;
		Settings.TransitClosestOffsetRadii =
			M2TransitClosestOffsetRadii;
		Settings.TransitExitOffsetRadii =
			M2TransitExitOffsetRadii;
		Settings.TransitVerticalOffsetRadii =
			M2TransitVerticalOffsetRadii;
		Settings.TransitExitProgressFraction =
			M2TransitExitProgressFraction;
		Settings.BaselineFovDegrees = BaselineFovDegrees;
		Settings.ClosestFovDegrees = M2ClosestFovDegrees;
		Settings.PeriapsisFovRestoreFraction =
			M2PeriapsisFovRestoreFraction;
		Settings.DualBodyBridgeFovDegrees =
			M3DualBodyBridgeFovDegrees;
		Settings.DualBodyBridgeFitMargin =
			M3DualBodyBridgeFitMargin;
		Settings.DualBodyBridgeBirdNdcY =
			M3DualBodyBridgeBirdNdcY;
	Settings.TerminalFovDegrees = M4TerminalFovDegrees;
	Settings.TerminalFitMargin = M4TerminalFitMargin;
	Settings.TerminalBirdStartNdcY = M4TerminalBirdStartNdcY;
	Settings.TerminalBirdContactNdcY = M4TerminalBirdContactNdcY;
	Settings.TerminalStartBirdDistanceCM =
		M4TerminalStartBirdDistanceCM;
	Settings.TerminalContactBirdDistanceCM =
		M4TerminalContactBirdDistanceCM;
		Settings.DualBodyBridgeSeconds =
			M3DualBodyBridgeHoldSeconds;
		Settings.IncomingMatchEaseOutPower =
			M3IncomingMatchEaseOutPower;
		Settings.IncomingEntryMatchSeconds = M3HandoffReleaseSeconds;
		FABTSM11FinaleCameraM2Diagnostics Diagnostics;
		const bool bBuilt = bM3Window
			? ABTSM11FinaleFlightCameraMath::BuildM3AssistFrame(
				Frame,
				TargetPosition,
				*DirectorSample,
				Settings,
				DirectedTransform,
				Diagnostics)
			: ABTSM11FinaleFlightCameraMath::BuildM2Assist1Frame(
				Frame,
				TargetPosition,
				*DirectorSample,
				Settings,
				DirectedTransform,
				Diagnostics);
		if (!bBuilt)
		{
			return false;
		}
		LastM2BlendAlpha = Diagnostics.DirectorBlendAlpha;
		LastM2RetreatAlpha = Diagnostics.RetreatAlpha;
		LastM2TransitScreenXInTargetRadii =
			Diagnostics.TransitScreenXInTargetRadii;
		DesiredFovDegrees = Diagnostics.DirectedFovDegrees;
		const FVector BaselineOffset =
			Frame.DesiredTransform.GetLocation() - TargetPosition;
		const FVector DirectedOffset =
			DirectedTransform.GetLocation() - TargetPosition;
		const FVector BlendedOffsetDirection = SlerpDirection(
			BaselineOffset,
			DirectedOffset,
			LastM2BlendAlpha);
		const double BlendedOffsetDistance = FMath::Lerp(
			BaselineOffset.Size(),
			DirectedOffset.Size(),
			LastM2BlendAlpha);
		if (BlendedOffsetDirection.IsNearlyZero()
			|| !FMath::IsFinite(BlendedOffsetDistance))
		{
			return false;
		}
		DesiredTransform.SetLocation(
			TargetPosition
			+ BlendedOffsetDirection * BlendedOffsetDistance);
		DesiredTransform.SetRotation(FQuat::Slerp(
			Frame.DesiredTransform.GetRotation(),
			DirectedTransform.GetRotation(),
			LastM2BlendAlpha).GetNormalized());
	}
	const double SafeDeltaSeconds =
		FMath::Max(0.0, static_cast<double>(DeltaSeconds));
	const bool bM3DirectorReleasing = bM3DirectorFrozenEnabled
		&& LastM2BlendAlpha + 1.0e-9 < PreviousDirectorBlendAlpha;
	const bool bM3RotationSafetyEnvelope = bM3DirectorFrozenEnabled
		&& DirectorSample != nullptr
		&& (DirectorSample->Selection.Stage
				== EABTSM11FinaleCameraStage::Handoff
			|| DirectorSample->Selection.Stage
				== EABTSM11FinaleCameraStage::Approach
			|| DirectorSample->Selection.IsM3TransitionShot()
			|| bM3DirectorReleasing);
	const double ResponseSpeed = LastM2BlendAlpha > UE_DOUBLE_SMALL_NUMBER
		|| bM3RotationSafetyEnvelope
		? M2FollowLagSpeed
		: FollowLagSpeed;
	const double Alpha = FMath::Clamp(
		1.0 - FMath::Exp(
			-FMath::Max(0.0, ResponseSpeed)
			* SafeDeltaSeconds),
		0.0,
		1.0);
	FVector SmoothedLocation = FVector::ZeroVector;
	FQuat SmoothedRotation = FQuat::Identity;
	const auto BuildLocationAwareDesiredRotation =
		[&](const FVector& CameraLocation, FQuat& OutRotation) -> bool
	{
		OutRotation = DesiredTransform.GetRotation();
		if (DirectorSample != nullptr
			&& (DirectorSample->Selection.IsM3InterBodyTransition()
				|| DirectorSample->Selection.IsM4TerminalWindow()))
		{
			return true;
		}
		if (LastM2BlendAlpha <= UE_DOUBLE_SMALL_NUMBER
			|| DirectorSample == nullptr)
		{
			return true;
		}
		const double EncounterOrientationAlpha =
			ResolveEncounterOrientationAlpha(
				DirectorSample->Selection,
				LastM2BlendAlpha);
		const FVector LocationAwareUp = SlerpDirection(
			Frame.TransportedUp,
			DirectorSample->EncounterScreenUp,
			EncounterOrientationAlpha);
		FQuat DirectedRotationAtLocation;
		if (!ABTSM11FinaleFlightCameraMath::BuildM2PlanetAnchoredRotation(
			CameraLocation,
			LocationAwareUp,
			DirectorSample->TargetCenter,
			DirectedRotationAtLocation))
		{
			return false;
		}
		OutRotation = FQuat::Slerp(
			Frame.DesiredTransform.GetRotation(),
			DirectedRotationAtLocation,
			LastM2BlendAlpha).GetNormalized();
		return true;
	};
	const auto BuildStandardSmoothedFrame =
		[&](FVector& OutLocation, FQuat& OutRotation) -> bool
	{
		OutLocation = FMath::Lerp(
			GetActorLocation(),
			DesiredTransform.GetLocation(),
			Alpha);
		FQuat LocationAwareDesiredRotation;
		if (!BuildLocationAwareDesiredRotation(
			OutLocation,
			LocationAwareDesiredRotation))
		{
			return false;
		}
		OutRotation = FQuat::Slerp(
			GetActorQuat(),
			LocationAwareDesiredRotation,
			Alpha).GetNormalized();
		return true;
	};
	if (!BuildStandardSmoothedFrame(
		SmoothedLocation,
		SmoothedRotation))
	{
		return false;
	}
	const bool bM3InterBodyComposition = bM3DirectorFrozenEnabled
		&& DirectorSample != nullptr
		&& DirectorSample->Selection.IsM3InterBodyTransition();
	const bool bM4TerminalComposition = bM3DirectorFrozenEnabled
		&& DirectorSample != nullptr
		&& DirectorSample->Selection.IsM4TerminalWindow();
	const bool bM3DirectedEncounterComposition = bM3DirectorFrozenEnabled
		&& DirectorSample != nullptr
		&& DirectorSample->Selection.ShotPhase
			== EABTSM11FinaleCameraShotPhase::Authority
		&& (DirectorSample->Selection.Stage
				== EABTSM11FinaleCameraStage::Approach
			|| DirectorSample->Selection.Stage
				== EABTSM11FinaleCameraStage::Periapsis);
	const bool bM3LaunchAnchoredIncomingComposition =
		bM3DirectorFrozenEnabled
		&& DirectorSample != nullptr
		&& DirectorSample->Selection.IsM3IncomingShot()
		&& !DirectorSample->Selection.IsM3InterBodyTransition()
		&& DirectorSample->Selection.FramingAssistIndex == 1
		&& DirectorSample->Selection.Stage
			== EABTSM11FinaleCameraStage::CruiseToBody;
	const bool bM3FullyDirectedIncomingComposition =
		bM3DirectorFrozenEnabled
		&& DirectorSample != nullptr
		&& DirectorSample->Selection.IsM3IncomingShot()
		&& !bM3LaunchAnchoredIncomingComposition
		&& LastM2BlendAlpha >= 1.0 - UE_DOUBLE_SMALL_NUMBER;
	if (bM3InterBodyComposition
		|| bM4TerminalComposition
		|| bM3DirectedEncounterComposition
		|| bM3FullyDirectedIncomingComposition)
	{
		// Position and rotation are one constrained composition solution. Lagging
		// or clipping them independently can keep the location from one frame and
		// the aim from another, which ejects the bird during the pull-in.
		SmoothedLocation = DesiredTransform.GetLocation();
		SmoothedRotation = DesiredTransform.GetRotation();
	}
	// Only the launch-to-first-planet reveal needs the legacy bird-carried
	// limiter. Every inter-body transition is already a continuous, constrained
	// composition and must not acquire a second runtime authority.
	const bool bM3IncomingTransition = bM3DirectorFrozenEnabled
		&& DirectorSample != nullptr
		&& DirectorSample->Selection.IsM3IncomingShot()
		&& !DirectorSample->Selection.IsM3InterBodyTransition()
		&& !bM3FullyDirectedIncomingComposition;
	if (bM3IncomingTransition)
	{
		// First carry the camera by the bird's authoritative translation. The
		// transition limiter should constrain only the orbit around the bird, not
		// mistake fast trajectory travel for an orbital camera jump. Without this
		// separation, the camera advances only a few hundred centimetres while the
		// bird advances nearly a thousand, and the 10-degree rotation envelope can
		// no longer keep the bird in frame.
		const FVector TranslationCarriedLocation =
			GetActorLocation() + AuthorityTargetTranslation;
		double SafePositionAlpha = 1.0;
		const double SafeDesiredLocationDelta = FVector::Distance(
			TranslationCarriedLocation,
			DesiredTransform.GetLocation());
		if (SafeDesiredLocationDelta > M3TransitionMaximumPositionStepCM)
		{
			SafePositionAlpha =
				M3TransitionMaximumPositionStepCM / SafeDesiredLocationDelta;
		}
		const FVector CurrentBirdOffset =
			TranslationCarriedLocation - TargetPosition;
		const FVector SafeDesiredBirdOffset =
			DesiredTransform.GetLocation() - TargetPosition;
		const double BirdOrbitRadians = FMath::Acos(FMath::Clamp(
			static_cast<double>(FVector::DotProduct(
				CurrentBirdOffset.GetSafeNormal(),
				SafeDesiredBirdOffset.GetSafeNormal())),
			-1.0,
			1.0));
		const double MaximumBirdOrbitStepRadians = FMath::DegreesToRadians(
			M3TransitionMaximumRotationStepDegrees * 0.5);
		if (BirdOrbitRadians > MaximumBirdOrbitStepRadians)
		{
			SafePositionAlpha = FMath::Min(
				SafePositionAlpha,
				MaximumBirdOrbitStepRadians / BirdOrbitRadians);
		}
		const FVector SafeOffsetDirection = SlerpDirection(
			CurrentBirdOffset,
			SafeDesiredBirdOffset,
			SafePositionAlpha);
		const double SafeOffsetDistance = FMath::Lerp(
			CurrentBirdOffset.Size(),
			SafeDesiredBirdOffset.Size(),
			SafePositionAlpha);
		const FVector SafeLocation = TargetPosition
			+ SafeOffsetDirection * SafeOffsetDistance;
		const double ShotElapsedSeconds =
			DirectorSample->Selection.ShotProgress
				* DirectorSample->Selection.ShotDurationSeconds;
		const double AlignmentDurationSeconds =
			bM3LaunchAnchoredIncomingComposition
			? M3HandoffLeadInSeconds
			: FMath::Max(
				M3HandoffLeadInSeconds,
				DirectorSample->Selection.ShotDurationSeconds
					- M3HandoffReleaseSeconds);
		const double ReleaseAlpha = SmootherStep01(
			ShotElapsedSeconds
				/ FMath::Max(
					AlignmentDurationSeconds,
					UE_DOUBLE_SMALL_NUMBER));
		if (!ABTSM11FinaleFlightCameraMath::BuildM3LaunchReleaseLocation(
			SafeLocation,
			TargetPosition,
			DesiredTransform.GetLocation(),
			ReleaseAlpha,
			SmoothedLocation))
		{
			return false;
		}

		FQuat BirdAnchoredRotation;
		FQuat LocationAwareDesiredRotation;
		if (!BuildBirdAnchoredRotation(
			SmoothedLocation,
			TargetPosition,
			GetActorQuat().GetUpVector(),
			BirdAnchoredRotation)
			|| !BuildLocationAwareDesiredRotation(
				SmoothedLocation,
				LocationAwareDesiredRotation))
		{
			return false;
		}
		const FQuat ReleasedDesiredRotation = FQuat::Slerp(
			BirdAnchoredRotation,
			LocationAwareDesiredRotation,
			ReleaseAlpha).GetNormalized();
		const double ReleasedRotationResponse = FMath::Lerp(
			Alpha,
			1.0,
			ReleaseAlpha);
		SmoothedRotation = FQuat::Slerp(
			GetActorQuat(),
			ReleasedDesiredRotation,
			ReleasedRotationResponse).GetNormalized();
	}
	if (bM3RotationSafetyEnvelope
		&& !bM3InterBodyComposition
		&& !bM3DirectedEncounterComposition
		&& !bM3FullyDirectedIncomingComposition)
	{
		const double FinalRotationDeltaRadians =
			GetActorQuat().AngularDistance(SmoothedRotation);
		const double MaximumRotationStepRadians = FMath::DegreesToRadians(
			M3TransitionMaximumRotationStepDegrees);
		if (FinalRotationDeltaRadians > MaximumRotationStepRadians)
		{
			SmoothedRotation = FQuat::Slerp(
				GetActorQuat(),
				SmoothedRotation,
				MaximumRotationStepRadians
					/ FinalRotationDeltaRadians).GetNormalized();
		}
	}
	if (!ApplyM6FormationSafetyEnvelope(
		TargetPosition,
		FormationSubjects,
		SmoothedRotation,
		DesiredFovDegrees,
		SmoothedLocation))
	{
		return false;
	}
	SetActorLocationAndRotation(
		SmoothedLocation,
		SmoothedRotation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	GetCameraComponent()->SetFieldOfView(
		static_cast<float>(DesiredFovDegrees));
	LastAuthorityTargetPosition = TargetPosition;
	return true;
}

void AABTSM11FinaleFlightCamera::ResetAuthorityFollow()
{
	RestoreFinaleAntiAliasingOverride();
	bAuthorityFollowActive = false;
	LastAuthorityForward = FVector::ForwardVector;
	LastTransportedUp = FVector::UpVector;
	LastAuthorityTargetPosition = FVector::ZeroVector;
	LastM2BlendAlpha = 0.0;
	LastM2RetreatAlpha = 0.0;
	LastM2TransitScreenXInTargetRadii = -M2TransitCruiseFarOffsetRadii;
	LastDirectorStage = EABTSM11FinaleCameraStage::PreLaunch;
	bM2DirectorFrozenEnabled = false;
	bM3DirectorFrozenEnabled = false;
	GetCameraComponent()->SetFieldOfView(
		static_cast<float>(BaselineFovDegrees));
}

void AABTSM11FinaleFlightCamera::ActivateFinaleAntiAliasingOverride()
{
	IConsoleVariable* AntiAliasingMethod =
		IConsoleManager::Get().FindConsoleVariable(
			TEXT("r.AntiAliasingMethod"));
	if (AntiAliasingMethod == nullptr)
	{
		UE_LOG(
			LogABTSRuntime,
			Warning,
			TEXT("[ABTS][M11-C][FlightCamera] AntiAliasingOverride unavailable: r.AntiAliasingMethod was not registered."));
		return;
	}

	PreviousAntiAliasingMethod = AntiAliasingMethod->GetInt();
	bFinaleAntiAliasingOverrideActive = true;
	EnsureFinaleAntiAliasingOverride();
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M11-C][FlightCamera] AntiAliasingOverride Active=1 Previous=%d Current=%d Required=FXAA"),
		PreviousAntiAliasingMethod,
		AntiAliasingMethod->GetInt());
}

void AABTSM11FinaleFlightCamera::EnsureFinaleAntiAliasingOverride() const
{
	if (!bFinaleAntiAliasingOverrideActive)
	{
		return;
	}
	IConsoleVariable* AntiAliasingMethod =
		IConsoleManager::Get().FindConsoleVariable(
			TEXT("r.AntiAliasingMethod"));
	if (AntiAliasingMethod != nullptr
		&& AntiAliasingMethod->GetInt() != static_cast<int32>(AAM_FXAA))
	{
		AntiAliasingMethod->SetWithCurrentPriority(
			static_cast<int32>(AAM_FXAA),
			FName(TEXT("ABTSM11FinaleCamera")),
			ECVF_SetByConsole,
			ECVF_SetByScalability);
	}
}

void AABTSM11FinaleFlightCamera::RestoreFinaleAntiAliasingOverride()
{
	if (!bFinaleAntiAliasingOverrideActive)
	{
		return;
	}
	IConsoleVariable* AntiAliasingMethod =
		IConsoleManager::Get().FindConsoleVariable(
			TEXT("r.AntiAliasingMethod"));
	if (AntiAliasingMethod != nullptr)
	{
		AntiAliasingMethod->SetWithCurrentPriority(
			PreviousAntiAliasingMethod,
			FName(TEXT("ABTSM11FinaleCameraRestore")),
			ECVF_SetByConsole,
			ECVF_SetByScalability);
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][M11-C][FlightCamera] AntiAliasingOverride Active=0 Restored=%d"),
			AntiAliasingMethod->GetInt());
	}
	bFinaleAntiAliasingOverrideActive = false;
}

bool AABTSM11FinaleFlightCamera::BuildAuthorityFrame(
	const FVector& TargetPosition,
	const FVector& TrajectoryTangent,
	const FVector& PreferredUp,
	FABTSM11FinaleFlightCameraFrame& OutFrame) const
{
	return ABTSM11FinaleFlightCameraMath::BuildDesiredFrame(
		TargetPosition,
		TrajectoryTangent,
		PreferredUp,
		LastAuthorityForward,
		LastTransportedUp,
		bAuthorityFollowActive,
		FollowDistanceCM,
		FollowHeightCM,
		LookAheadDistanceCM,
		LookTargetHeightCM,
		OutFrame);
}
