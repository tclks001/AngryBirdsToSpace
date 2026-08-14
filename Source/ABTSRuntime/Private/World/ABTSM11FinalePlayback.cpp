// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM11FinaleInteractionTypes.h"

namespace
{
	constexpr uint64 FNVOffset = 14695981039346656037ull;
	constexpr uint64 FNVPrime = 1099511628211ull;

	void HashBytes(uint64& Hash, const void* Data, const SIZE_T Size)
	{
		const uint8* Bytes = static_cast<const uint8*>(Data);
		for (SIZE_T Index = 0; Index < Size; ++Index)
		{
			Hash ^= Bytes[Index];
			Hash *= FNVPrime;
		}
	}

	void HashDouble(uint64& Hash, const double Value)
	{
		uint64 Bits = 0;
		static_assert(sizeof(Bits) == sizeof(Value));
		FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
		HashBytes(Hash, &Bits, sizeof(Bits));
	}

	uint64 ComputePlanHash(const FABTSM11PlaybackPlan& Plan)
	{
		uint64 Hash = FNVOffset;
		HashBytes(
			Hash,
			&Plan.ReleasedTrajectoryHash,
			sizeof(Plan.ReleasedTrajectoryHash));
		HashBytes(
			Hash,
			&Plan.PhysicalTrajectoryHash,
			sizeof(Plan.PhysicalTrajectoryHash));
		HashBytes(
			Hash,
			&Plan.TransferContractVersion,
			sizeof(Plan.TransferContractVersion));
		HashBytes(
			Hash,
			&Plan.bCandidateQualifiedIntercept,
			sizeof(Plan.bCandidateQualifiedIntercept));
		for (const FABTSM11PlaybackPoint& Point : Plan.Points)
		{
			HashDouble(Hash, Point.TimeSeconds);
			HashDouble(Hash, Point.PositionCM.X);
			HashDouble(Hash, Point.PositionCM.Y);
			HashDouble(Hash, Point.PositionCM.Z);
			HashDouble(Hash, Point.VelocityCMPerSec.X);
			HashDouble(Hash, Point.VelocityCMPerSec.Y);
			HashDouble(Hash, Point.VelocityCMPerSec.Z);
			const uint8 Kind = static_cast<uint8>(Point.SegmentKind);
			HashBytes(Hash, &Kind, sizeof(Kind));
		}
		return Hash;
	}

	void AppendResult(
		const FABTSM11TrajectoryResult& Result,
		const EABTSM11PlaybackSegmentKind Kind,
		TArray<FABTSM11PlaybackPoint>& OutPoints,
		const int32 FirstIndex = 0,
		const double TimeOffsetSeconds = 0.0)
	{
		const int32 SafeFirst = FMath::Clamp(
			FirstIndex,
			0,
			Result.Points.Num());
		OutPoints.Reserve(
			OutPoints.Num() + Result.Points.Num() - SafeFirst);
		for (int32 Index = SafeFirst; Index < Result.Points.Num(); ++Index)
		{
			const FABTSM11TrajectoryPoint& Source = Result.Points[Index];
			FABTSM11PlaybackPoint& Point = OutPoints.AddDefaulted_GetRef();
			Point.TimeSeconds =
				Source.TimeSeconds + TimeOffsetSeconds;
			Point.PositionCM = Source.PositionCM;
			Point.VelocityCMPerSec = Source.VelocityCMPerSec;
			Point.SegmentKind = Kind;
		}
	}

	FVector3d EstimateAcceleration(
		TConstArrayView<FABTSM11TrajectoryPoint> Points,
		const int32 Index)
	{
		if (Points.Num() < 2 || !Points.IsValidIndex(Index))
		{
			return FVector3d::ZeroVector;
		}
		const int32 Before = FMath::Max(0, Index - 1);
		const int32 After = FMath::Min(Points.Num() - 1, Index + 1);
		const double DeltaTime =
			Points[After].TimeSeconds - Points[Before].TimeSeconds;
		return DeltaTime > UE_DOUBLE_SMALL_NUMBER
			? (Points[After].VelocityCMPerSec
				- Points[Before].VelocityCMPerSec) / DeltaTime
			: FVector3d::ZeroVector;
	}

	struct FQuinticCurve
	{
		FVector3d C0 = FVector3d::ZeroVector;
		FVector3d C1 = FVector3d::ZeroVector;
		FVector3d C2 = FVector3d::ZeroVector;
		FVector3d C3 = FVector3d::ZeroVector;
		FVector3d C4 = FVector3d::ZeroVector;
		FVector3d C5 = FVector3d::ZeroVector;

		void Build(
			const FVector3d& P0,
			const FVector3d& V0,
			const FVector3d& A0,
			const FVector3d& P1,
			const FVector3d& V1,
			const FVector3d& A1,
			const double Duration)
		{
			C0 = P0;
			C1 = V0;
			C2 = A0 * 0.5;
			const double T2 = Duration * Duration;
			const double T3 = T2 * Duration;
			const double T4 = T3 * Duration;
			const double T5 = T4 * Duration;
			const FVector3d DP =
				P1 - (C0 + C1 * Duration + C2 * T2);
			const FVector3d DV =
				V1 - (C1 + C2 * (2.0 * Duration));
			const FVector3d DA = A1 - C2 * 2.0;
			C3 = DP * (10.0 / T3)
				- DV * (4.0 / T2)
				+ DA * (0.5 / Duration);
			C4 = DP * (-15.0 / T4)
				+ DV * (7.0 / T3)
				- DA / T2;
			C5 = DP * (6.0 / T5)
				- DV * (3.0 / T4)
				+ DA * (0.5 / T3);
		}

		FVector3d Position(const double T) const
		{
			return C0 + T * (C1 + T * (C2 + T * (C3 + T * (C4 + T * C5))));
		}

		FVector3d Velocity(const double T) const
		{
			return C1 + T * (C2 * 2.0 + T * (C3 * 3.0
				+ T * (C4 * 4.0 + T * C5 * 5.0)));
		}

		FVector3d Acceleration(const double T) const
		{
			return C2 * 2.0 + T * (C3 * 6.0
				+ T * (C4 * 12.0 + T * C5 * 20.0));
		}

		FVector3d Jerk(const double T) const
		{
			return C3 * 6.0 + T * (C4 * 24.0 + T * C5 * 60.0);
		}
	};

	struct FCircularContactGuidance
	{
		FVector3d CircleCenterCM = FVector3d::ZeroVector;
		FVector3d StartNormal = FVector3d::ZeroVector;
		FVector3d StartTangent = FVector3d::ZeroVector;
		FVector3d TargetCenterCM = FVector3d::ZeroVector;
		double RadiusCM = 0.0;
		double ContactSweepRadians = 0.0;

		FVector3d Position(const double SweepRadians) const
		{
			return CircleCenterCM
				- StartNormal * (RadiusCM * FMath::Cos(SweepRadians))
				+ StartTangent * (RadiusCM * FMath::Sin(SweepRadians));
		}

		FVector3d Tangent(const double SweepRadians) const
		{
			return StartNormal * FMath::Sin(SweepRadians)
				+ StartTangent * FMath::Cos(SweepRadians);
		}

		FVector3d Acceleration(
			const double SweepRadians,
			const double SpeedCMPerSec) const
		{
			return (CircleCenterCM - Position(SweepRadians))
				* (FMath::Square(SpeedCMPerSec) / FMath::Square(RadiusCM));
		}

		bool Build(
			const FVector3d& StartPositionCM,
			const FVector3d& StartVelocityCMPerSec,
			const FVector3d& InTargetCenterCM,
			const double ContactRadiusCM)
		{
			const double Speed = StartVelocityCMPerSec.Length();
			if (!FMath::IsFinite(Speed) || Speed <= 1.0
				|| !FMath::IsFinite(ContactRadiusCM)
				|| ContactRadiusCM <= 0.0)
			{
				return false;
			}

			StartTangent = StartVelocityCMPerSec / Speed;
			TargetCenterCM = InTargetCenterCM;
			const FVector3d ToTarget = TargetCenterCM - StartPositionCM;
			const double TargetDistance = ToTarget.Length();
			const double ForwardDistance = FVector3d::DotProduct(
				ToTarget,
				StartTangent);
			const FVector3d SideOffset =
				ToTarget - StartTangent * ForwardDistance;
			const double SideDistance = SideOffset.Length();
			if (!FMath::IsFinite(TargetDistance)
				|| TargetDistance <= ContactRadiusCM + 1.0e-3
				|| ForwardDistance <= 1.0
				|| SideDistance <= 1.0e-3)
			{
				return false;
			}

			StartNormal = SideOffset / SideDistance;
			RadiusCM = FMath::Square(TargetDistance)
				/ (2.0 * SideDistance);
			if (!FMath::IsFinite(RadiusCM) || RadiusCM <= ContactRadiusCM)
			{
				return false;
			}

			CircleCenterCM = StartPositionCM + StartNormal * RadiusCM;
			const double TargetSweep = 2.0 * FMath::Atan2(
				SideDistance,
				ForwardDistance);
			if (!FMath::IsFinite(TargetSweep)
				|| TargetSweep <= 1.0e-5
				|| TargetSweep >= PI - 1.0e-3)
			{
				return false;
			}

			double LowSweep = 0.0;
			double HighSweep = TargetSweep;
			for (int32 Iteration = 0; Iteration < 64; ++Iteration)
			{
				const double MiddleSweep = (LowSweep + HighSweep) * 0.5;
				if ((Position(MiddleSweep) - TargetCenterCM).Length()
					> ContactRadiusCM)
				{
					LowSweep = MiddleSweep;
				}
				else
				{
					HighSweep = MiddleSweep;
				}
			}
			ContactSweepRadians = HighSweep;
			return ContactSweepRadians > 1.0e-5
				&& FMath::IsNearlyEqual(
					(Position(ContactSweepRadians) - TargetCenterCM).Length(),
					ContactRadiusCM,
					1.0e-3);
		}
	};

	double MinimumBodyClearanceCM(
		const FABTSM11FinaleLayoutPreset& Preset,
		const FVector3d& Position)
	{
		double MinimumClearance = TNumericLimits<double>::Max();
		for (const FABTSM11GravityBodySpec& Body
			: Preset.CanonicalScenario.Bodies)
		{
			MinimumClearance = FMath::Min(
				MinimumClearance,
				(Position - Body.CenterCM).Length()
					- Body.CollisionRadiusCM);
		}
		return MinimumClearance;
	}

	bool BuildTransfer(
		const FABTSM11FinaleLayoutPreset& Preset,
		const FABTSM11TrajectoryResult& Released,
		const FABTSM11TrajectoryResult& Nominal,
		const FABTSM11TerminalTransferContract& Contract,
		TArray<FABTSM11PlaybackPoint>& InOutPoints,
		double& OutStartTime,
		double& OutEndTime,
		int32& OutNominalTailFirstIndex,
		double& OutNominalTailTimeOffsetSeconds,
		FString& OutFailure)
	{
		if (Released.Points.Num() < 2 || Nominal.Points.Num() < 2)
		{
			OutFailure = TEXT("TerminalTransferMissingTrajectory");
			return false;
		}
		const int32 SourceIndex = Released.Points.Num() - 1;
		const FABTSM11TrajectoryPoint& Source =
			Released.Points[SourceIndex];
		const FVector3d SourceAcceleration =
			EstimateAcceleration(Released.Points, SourceIndex);

		int32 NominalQualifiedIndex = INDEX_NONE;
		const FVector3d QualifiedCenter =
			Preset.CanonicalScenario.Target.CenterCM;
		const double QualifiedRadius =
			Preset.CanonicalScenario.Target.HitRadiusCM;
		for (int32 Index = 0; Index < Nominal.Points.Num(); ++Index)
		{
			if ((Nominal.Points[Index].PositionCM - QualifiedCenter)
					.Length()
				<= QualifiedRadius + 1.0e-3)
			{
				NominalQualifiedIndex = Index;
				break;
			}
		}
		if (NominalQualifiedIndex == INDEX_NONE
			|| NominalQualifiedIndex >= Nominal.Points.Num() - 1)
		{
			OutFailure =
				TEXT("TerminalTransferNominalQualifiedCrossingMissing");
			return false;
		}

		/*
		 * Solver time is authoritative inside each source trajectory, but the
		 * explicit post-F4 cinematic transfer has its own presentation
		 * duration. F4 samples reach the 16 km envelope at different absolute
		 * times; looking up the nominal tail at SourceTime + Duration rejects
		 * otherwise valid samples once that time is past the nominal contact.
		 *
		 * Join instead to the first frozen nominal state just beyond its own
		 * qualified crossing, then retime the untouched nominal suffix by one
		 * constant offset. This preserves ordering and velocities without
		 * pretending that two different launch inputs share an absolute clock.
		 */
		const int32 JoinIndex = FMath::Min(
			NominalQualifiedIndex + 1,
			Nominal.Points.Num() - 2);
		const FABTSM11TrajectoryPoint& Join =
			Nominal.Points[JoinIndex];
		double BestScore = TNumericLimits<double>::Max();
		double BestDuration = 0.0;
		double BestAcceleration = TNumericLimits<double>::Max();
		double BestJerk = TNumericLimits<double>::Max();
		double BestClearance = -TNumericLimits<double>::Max();
		for (double RequestedDuration = Contract.MinimumDurationSeconds;
			RequestedDuration <= Contract.MaximumDurationSeconds + 1.0e-9;
			RequestedDuration += Contract.DurationStepSeconds)
		{
			const double Duration = RequestedDuration;

			FQuinticCurve Curve;
			Curve.Build(
				Source.PositionCM,
				Source.VelocityCMPerSec,
				SourceAcceleration,
				Join.PositionCM,
				Join.VelocityCMPerSec,
				EstimateAcceleration(Nominal.Points, JoinIndex),
				Duration);

			double MaximumAcceleration = 0.0;
			double MaximumJerk = 0.0;
			double MinimumClearance = TNumericLimits<double>::Max();
			const int32 SampleCount = FMath::Max(
				2,
				FMath::CeilToInt(Duration / Contract.SampleStepSeconds));
			for (int32 SampleIndex = 1;
				SampleIndex <= SampleCount;
				++SampleIndex)
			{
				const double T = Duration
					* static_cast<double>(SampleIndex)
					/ static_cast<double>(SampleCount);
				MaximumAcceleration = FMath::Max(
					MaximumAcceleration,
					Curve.Acceleration(T).Length());
				MaximumJerk = FMath::Max(
					MaximumJerk,
					Curve.Jerk(T).Length());
				MinimumClearance = FMath::Min(
					MinimumClearance,
					MinimumBodyClearanceCM(
						Preset,
						Curve.Position(T)));
			}
			const bool bValid =
				MaximumAcceleration
					<= Contract.MaximumAccelerationCMPerSec2
				&& MaximumJerk <= Contract.MaximumJerkCMPerSec3
				&& MinimumClearance > Contract.BodyClearanceCM;
			const double ClearanceScore =
				MinimumClearance > Contract.BodyClearanceCM
				? 0.0
				: 1.0
					+ (Contract.BodyClearanceCM - MinimumClearance)
						/ FMath::Max(1.0, Contract.BodyClearanceCM);
			const double Score = FMath::Max3(
				MaximumAcceleration
					/ Contract.MaximumAccelerationCMPerSec2,
				MaximumJerk / Contract.MaximumJerkCMPerSec3,
				ClearanceScore);
			if (Score < BestScore)
			{
				BestScore = Score;
				BestDuration = Duration;
				BestAcceleration = MaximumAcceleration;
				BestJerk = MaximumJerk;
				BestClearance = MinimumClearance;
			}
			if (!bValid)
			{
				continue;
			}

			OutStartTime = Source.TimeSeconds;
			OutEndTime = Source.TimeSeconds + Duration;
			OutNominalTailFirstIndex = JoinIndex + 1;
			OutNominalTailTimeOffsetSeconds =
				OutEndTime - Join.TimeSeconds;
			InOutPoints.Reserve(InOutPoints.Num() + SampleCount);
			for (int32 SampleIndex = 1;
				SampleIndex <= SampleCount;
				++SampleIndex)
			{
				const double T = Duration
					* static_cast<double>(SampleIndex)
					/ static_cast<double>(SampleCount);
				FABTSM11PlaybackPoint& Point =
					InOutPoints.AddDefaulted_GetRef();
				Point.TimeSeconds = Source.TimeSeconds + T;
				Point.PositionCM = Curve.Position(T);
				Point.VelocityCMPerSec = Curve.Velocity(T);
				Point.SegmentKind =
					EABTSM11PlaybackSegmentKind::VisibleTerminalTransfer;
			}
			OutFailure.Reset();
			return true;
		}
		OutFailure = FString::Printf(
			TEXT("NoValidVisibleTerminalTransfer:BestDuration=%.3f MaxAccel=%.3f MaxJerk=%.3f MinClearance=%.3f"),
			BestDuration,
			BestAcceleration,
			BestJerk,
			BestClearance);
		return false;
	}

	bool BuildCandidateContactTransfer(
		const FABTSM11FinaleLayoutPreset& Preset,
		const FABTSM11TrajectoryResult& Released,
		const FABTSM11TerminalTransferContract& Contract,
		TArray<FABTSM11PlaybackPoint>& InOutPoints,
		double& OutStartTime,
		double& OutEndTime,
		FString& OutFailure)
	{
		if (Released.Points.Num() < 2 || !Contract.IsValid())
		{
			OutFailure = TEXT("CandidateContactTransferMissingTrajectory");
			return false;
		}
		const FABTSM11TargetSpec& Target = Preset.CanonicalScenario.Target;
		const FVector3d ContactCenter = Target.GetGeometricContactCenterCM();
		const double ContactRadius = Target.GetGeometricContactRadiusCM();
		if (!FMath::IsFinite(ContactRadius) || ContactRadius <= 0.0)
		{
			OutFailure = TEXT("CandidateContactTransferInvalidGeometry");
			return false;
		}

		/*
		 * The terminal presentation is also the authoritative playback path.
		 * Pick the latest usable state on the released F4 trajectory, construct
		 * the unique 3D circle tangent there and passing through the UFO centre,
		 * then stop at the first intersection with the physical contact sphere.
		 * A short quintic replaces the first part of that circle so acceleration
		 * changes continuously from the solver trajectory into constant-radius
		 * guidance. This prevents the old single-quintic hairpin while keeping
		 * position, velocity and acceleration continuous at both joins.
		 */
		constexpr double CandidateMaximumDurationSeconds = 12.0;
		constexpr double CandidateMaximumAccelerationCMPerSec2 = 60000.0;
		constexpr double CandidateMaximumJerkCMPerSec3 = 300000.0;
		constexpr double MaximumHeadingStepRadians = PI / 24.0;
		constexpr double MinimumSignedTurn = -1.0e-5;
		constexpr double MaximumLookbackSeconds = 6.0;
		constexpr double MinimumHandoffSpacingSeconds = 0.10;
		const double GuidanceSampleStepSeconds =
			Contract.SampleStepSeconds / 8.0;
		const double EntryFractions[] = { 0.20, 0.30, 0.40, 0.50 };
		const double DurationScales[] = { 1.0, 1.20, 1.50, 2.0 };

		double BestScore = TNumericLimits<double>::Max();
		FString BestDetail = TEXT("NoGeometricCandidate");
		double LastTestedTime = TNumericLimits<double>::Max();
		const double LatestTime = Released.Points.Last().TimeSeconds;
		for (int32 SourceIndex = Released.Points.Num() - 1;
			SourceIndex >= 1;
			--SourceIndex)
		{
			const FABTSM11TrajectoryPoint& Source =
				Released.Points[SourceIndex];
			if (LatestTime - Source.TimeSeconds > MaximumLookbackSeconds)
			{
				break;
			}
			if (SourceIndex != Released.Points.Num() - 1
				&& LastTestedTime - Source.TimeSeconds
					< MinimumHandoffSpacingSeconds)
			{
				continue;
			}
			LastTestedTime = Source.TimeSeconds;

			FCircularContactGuidance Circle;
			if (!Circle.Build(
				Source.PositionCM,
				Source.VelocityCMPerSec,
				ContactCenter,
				ContactRadius))
			{
				continue;
			}

			const double SourceSpeed = Source.VelocityCMPerSec.Length();
			const double AccelerationLimitedSpeed = FMath::Sqrt(
				CandidateMaximumAccelerationCMPerSec2 * Circle.RadiusCM * 0.80);
			const double GuidanceSpeed = FMath::Clamp(
				AccelerationLimitedSpeed,
				SourceSpeed * 0.50,
				SourceSpeed);
			const FVector3d SourceAcceleration = EstimateAcceleration(
				Released.Points,
				SourceIndex);
			const FVector3d CircleAxis = FVector3d::CrossProduct(
				Circle.StartTangent,
				Circle.StartNormal).GetSafeNormal();

			for (const double EntryFraction : EntryFractions)
			{
				const double EntrySweep = FMath::Min(
					Circle.ContactSweepRadians * EntryFraction,
					FMath::DegreesToRadians(15.0));
				if (EntrySweep <= 1.0e-5
					|| EntrySweep >= Circle.ContactSweepRadians - 1.0e-5)
				{
					continue;
				}

				const FVector3d EntryPosition = Circle.Position(EntrySweep);
				const FVector3d EntryVelocity =
					Circle.Tangent(EntrySweep) * GuidanceSpeed;
				const FVector3d EntryAcceleration = Circle.Acceleration(
					EntrySweep,
					GuidanceSpeed);
				const double IdealTransitionDuration =
					Circle.RadiusCM * EntrySweep * 2.0
					/ FMath::Max(1.0, SourceSpeed + GuidanceSpeed);

				for (const double DurationScale : DurationScales)
				{
					const double TransitionDuration = FMath::Max(
						GuidanceSampleStepSeconds * 2.0,
						IdealTransitionDuration * DurationScale);
					const double ArcDuration = Circle.RadiusCM
						* (Circle.ContactSweepRadians - EntrySweep)
						/ GuidanceSpeed;
					const double TotalDuration = TransitionDuration + ArcDuration;
					if (!FMath::IsFinite(TotalDuration)
						|| TotalDuration > CandidateMaximumDurationSeconds)
					{
						continue;
					}

					FQuinticCurve Transition;
					Transition.Build(
						Source.PositionCM,
						Source.VelocityCMPerSec,
						SourceAcceleration,
						EntryPosition,
						EntryVelocity,
						EntryAcceleration,
						TransitionDuration);

					TArray<FABTSM11PlaybackPoint> CandidatePoints;
					const int32 TransitionSampleCount = FMath::Max(
						2,
						FMath::CeilToInt(
							TransitionDuration / GuidanceSampleStepSeconds));
					const int32 ArcSampleCount = FMath::Max(
						2,
						FMath::CeilToInt(
							ArcDuration / GuidanceSampleStepSeconds));
					CandidatePoints.Reserve(
						TransitionSampleCount + ArcSampleCount);

					double MaximumAcceleration = 0.0;
					double MaximumJerk = 0.0;
					double MinimumClearance = TNumericLimits<double>::Max();
					double MinimumSpeed = TNumericLimits<double>::Max();
					double MaximumHeadingStep = 0.0;
					double MinimumObservedSignedTurn = 1.0;
					double PreviousTargetDistance =
						(Source.PositionCM - ContactCenter).Length();
					FVector3d PreviousVelocity = Source.VelocityCMPerSec;
					bool bValidShape = true;

					const auto AppendCheckedPoint = [&CandidatePoints,
						&Preset,
						&ContactCenter,
						&ContactRadius,
						&PreviousTargetDistance,
						&PreviousVelocity,
						&MaximumAcceleration,
						&MaximumJerk,
						&MinimumClearance,
						&MinimumSpeed,
						&MaximumHeadingStep,
						&MinimumObservedSignedTurn,
						&CircleAxis,
						&bValidShape](
							const double TimeSeconds,
							const FVector3d& Position,
							const FVector3d& Velocity,
							const FVector3d& Acceleration,
							const FVector3d& Jerk,
							const bool bFinal)
					{
						const double TargetDistance =
							(Position - ContactCenter).Length();
						const double Speed = Velocity.Length();
						const double PreviousSpeed = PreviousVelocity.Length();
						double HeadingStep = 0.0;
						double SignedTurn = 0.0;
						if (Speed > 1.0 && PreviousSpeed > 1.0)
						{
							const FVector3d PreviousDirection =
								PreviousVelocity / PreviousSpeed;
							const FVector3d Direction = Velocity / Speed;
							HeadingStep = FMath::Acos(FMath::Clamp(
								FVector3d::DotProduct(Direction, PreviousDirection),
								-1.0,
								1.0));
							SignedTurn = FVector3d::DotProduct(
								FVector3d::CrossProduct(
									PreviousDirection,
									Direction),
								CircleAxis);
						}
						MaximumAcceleration = FMath::Max(
							MaximumAcceleration,
							Acceleration.Length());
						MaximumJerk = FMath::Max(
							MaximumJerk,
							Jerk.Length());
						MinimumClearance = FMath::Min(
							MinimumClearance,
							MinimumBodyClearanceCM(Preset, Position));
						MinimumSpeed = FMath::Min(MinimumSpeed, Speed);
						MaximumHeadingStep = FMath::Max(
							MaximumHeadingStep,
							HeadingStep);
						MinimumObservedSignedTurn = FMath::Min(
							MinimumObservedSignedTurn,
							SignedTurn);
						bValidShape = bValidShape
							&& FMath::IsFinite(TargetDistance)
							&& FMath::IsFinite(Speed)
							&& SignedTurn >= MinimumSignedTurn
							&& TargetDistance <= PreviousTargetDistance + 1.0e-3
							&& (bFinal
								? FMath::IsNearlyEqual(
									TargetDistance,
									ContactRadius,
									1.0e-3)
								: TargetDistance > ContactRadius);
						FABTSM11PlaybackPoint& Point =
							CandidatePoints.AddDefaulted_GetRef();
						Point.TimeSeconds = TimeSeconds;
						Point.PositionCM = Position;
						Point.VelocityCMPerSec = Velocity;
						Point.SegmentKind =
							EABTSM11PlaybackSegmentKind::VisibleTerminalTransfer;
						PreviousTargetDistance = TargetDistance;
						PreviousVelocity = Velocity;
					};

					for (int32 SampleIndex = 1;
						SampleIndex <= TransitionSampleCount;
						++SampleIndex)
					{
						const double T = TransitionDuration
							* static_cast<double>(SampleIndex)
							/ static_cast<double>(TransitionSampleCount);
						AppendCheckedPoint(
							Source.TimeSeconds + T,
							Transition.Position(T),
							Transition.Velocity(T),
							Transition.Acceleration(T),
							Transition.Jerk(T),
							false);
					}

					const double AngularSpeed = GuidanceSpeed / Circle.RadiusCM;
					const double ArcJerkMagnitude = FMath::Pow(GuidanceSpeed, 3.0)
						/ FMath::Square(Circle.RadiusCM);
					for (int32 SampleIndex = 1;
						SampleIndex <= ArcSampleCount;
						++SampleIndex)
					{
						const double ArcTime = ArcDuration
							* static_cast<double>(SampleIndex)
							/ static_cast<double>(ArcSampleCount);
						const double Sweep = EntrySweep + AngularSpeed * ArcTime;
						const FVector3d Tangent = Circle.Tangent(Sweep);
						const FVector3d Acceleration = Circle.Acceleration(
							Sweep,
							GuidanceSpeed);
						AppendCheckedPoint(
							Source.TimeSeconds + TransitionDuration + ArcTime,
							Circle.Position(Sweep),
							Tangent * GuidanceSpeed,
							Acceleration,
							-Tangent * ArcJerkMagnitude,
							SampleIndex == ArcSampleCount);
					}

					const double ClearanceScore =
						MinimumClearance > Contract.BodyClearanceCM
							? 0.0
							: 1.0 + (Contract.BodyClearanceCM - MinimumClearance)
								/ FMath::Max(1.0, Contract.BodyClearanceCM);
					const double Score = FMath::Max3(
						MaximumAcceleration / CandidateMaximumAccelerationCMPerSec2,
						MaximumJerk / CandidateMaximumJerkCMPerSec3,
						ClearanceScore)
						+ (bValidShape ? 0.0 : 1000.0)
						+ (MaximumHeadingStep <= MaximumHeadingStepRadians
							? 0.0 : 1000.0);
					if (Score < BestScore)
					{
						BestScore = Score;
						BestDetail = FString::Printf(
							TEXT("Index=%d Radius=%.3f SweepDeg=%.3f Duration=%.3f MaxAccel=%.3f MaxJerk=%.3f MinClearance=%.3f MinSpeed=%.3f MaxHeadingDeg=%.3f MinSignedTurn=%.6f"),
							SourceIndex,
							Circle.RadiusCM,
							FMath::RadiansToDegrees(Circle.ContactSweepRadians),
							TotalDuration,
							MaximumAcceleration,
							MaximumJerk,
							MinimumClearance,
							MinimumSpeed,
							FMath::RadiansToDegrees(MaximumHeadingStep),
							MinimumObservedSignedTurn);
					}

					const bool bValid = bValidShape
						&& MinimumSpeed > 1.0
						&& MaximumHeadingStep <= MaximumHeadingStepRadians
						&& MaximumAcceleration
							<= CandidateMaximumAccelerationCMPerSec2
						&& MaximumJerk <= CandidateMaximumJerkCMPerSec3
						&& MinimumClearance > Contract.BodyClearanceCM;
					if (!bValid)
					{
						continue;
					}

					OutStartTime = Source.TimeSeconds;
					OutEndTime = Source.TimeSeconds + TotalDuration;
					InOutPoints.SetNum(SourceIndex + 1, EAllowShrinking::No);
					InOutPoints.Append(CandidatePoints);
					OutFailure.Reset();
					return true;
				}
			}
		}

		OutFailure = FString::Printf(
			TEXT("NoValidCandidateCircularContactTransfer:%s Score=%.6f"),
			*BestDetail,
			BestScore);
		return false;
	}

	bool SameInitialState(
		const FABTSM11TrajectoryResult& A,
		const FABTSM11TrajectoryResult& B)
	{
		return !A.Points.IsEmpty()
			&& !B.Points.IsEmpty()
			&& A.Points[0].PositionCM.Equals(
				B.Points[0].PositionCM,
				1.0e-8)
			&& A.Points[0].VelocityCMPerSec.Equals(
				B.Points[0].VelocityCMPerSec,
				1.0e-8);
	}

	bool SamePouchOriginAndTime(
		const FABTSM11TrajectoryResult& A,
		const FABTSM11TrajectoryResult& B)
	{
		return !A.Points.IsEmpty()
			&& !B.Points.IsEmpty()
			&& A.Points[0].TimeSeconds
				== B.Points[0].TimeSeconds
			&& A.Points[0].PositionCM.Equals(
				B.Points[0].PositionCM,
				1.0e-8);
	}
}

bool FABTSM11TerminalTransferContract::IsValid() const
{
	return ContractVersion == 1
		&& FMath::IsFinite(MinimumDurationSeconds)
		&& FMath::IsFinite(MaximumDurationSeconds)
		&& FMath::IsFinite(DurationStepSeconds)
		&& FMath::IsFinite(SampleStepSeconds)
		&& FMath::IsFinite(BodyClearanceCM)
		&& FMath::IsFinite(MaximumAccelerationCMPerSec2)
		&& FMath::IsFinite(MaximumJerkCMPerSec3)
		&& MinimumDurationSeconds > 0.0
		&& MaximumDurationSeconds >= MinimumDurationSeconds
		&& DurationStepSeconds > 0.0
		&& SampleStepSeconds > 0.0
		&& BodyClearanceCM >= 0.0
		&& MaximumAccelerationCMPerSec2 > 0.0
		&& MaximumJerkCMPerSec3 > 0.0;
}

void FABTSM11PlaybackPlan::Reset()
{
	Points.Reset();
	ReleasedTrajectoryHash = 0;
	PhysicalTrajectoryHash = 0;
	PlanHash = 0;
	TransferContractVersion = 0;
	DurationSeconds = 0.0;
	TransferStartTimeSeconds = -1.0;
	TransferEndTimeSeconds = -1.0;
	bQualifiedF4 = false;
	bCandidateQualifiedIntercept = false;
	bPhysicalTargetHit = false;
	bUsesVisibleTerminalTransfer = false;
	Failure.Reset();
}

bool FABTSM11PlaybackPlan::Build(
	const FABTSM11FinaleLayoutPreset& Preset,
	const FABTSM11TrajectoryResult& ReleasedQualifiedResult,
	const FABTSM11PrefixClassification& Classification,
	const FABTSM11TrajectoryResult* SameInputPhysicalResult,
	const FABTSM11TrajectoryResult* NominalPhysicalResult,
	const FABTSM11TerminalTransferContract& TransferContract)
{
	Reset();
	const auto Reject = [this](FString Reason)
	{
		Failure = MoveTemp(Reason);
		Points.Reset();
		return false;
	};
	if (!Preset.IsValid()
		|| !TransferContract.IsValid()
		|| ReleasedQualifiedResult.Points.Num() < 2
		|| ReleasedQualifiedResult.ValidationHash == 0)
	{
		return Reject(TEXT("InvalidReleasedTrajectory"));
	}

	ReleasedTrajectoryHash = ReleasedQualifiedResult.ValidationHash;
	bQualifiedF4 = Classification.IsF(4);
	if (!bQualifiedF4)
	{
		AppendResult(
			ReleasedQualifiedResult,
			EABTSM11PlaybackSegmentKind::PlayerAuthoritative,
			Points);
		DurationSeconds = Points.Last().TimeSeconds;
		PlanHash = ComputePlanHash(*this);
		return true;
	}
	if (!ReleasedQualifiedResult.DidHitTarget())
	{
		return Reject(TEXT("F4WithoutQualifiedTargetHit"));
	}

	if (SameInputPhysicalResult != nullptr
		&& SameInputPhysicalResult->DidHitTarget()
		&& SameInputPhysicalResult->ValidationHash != 0
		&& SameInitialState(
			ReleasedQualifiedResult,
			*SameInputPhysicalResult))
	{
		AppendResult(
			*SameInputPhysicalResult,
			EABTSM11PlaybackSegmentKind::PlayerAuthoritative,
			Points);
		PhysicalTrajectoryHash =
			SameInputPhysicalResult->ValidationHash;
		bPhysicalTargetHit = true;
		DurationSeconds = Points.Last().TimeSeconds;
		PlanHash = ComputePlanHash(*this);
		return true;
	}

	if (NominalPhysicalResult == nullptr
		|| !NominalPhysicalResult->DidHitTarget()
		|| NominalPhysicalResult->ValidationHash
			!= Preset.PhysicalPlaybackTrajectoryHash
		|| !SamePouchOriginAndTime(
			ReleasedQualifiedResult,
			*NominalPhysicalResult))
	{
		return Reject(TEXT("CertifiedNominalPhysicalPlaybackUnavailable"));
	}

	AppendResult(
		ReleasedQualifiedResult,
		EABTSM11PlaybackSegmentKind::PlayerAuthoritative,
		Points);
	int32 NominalTailFirstIndex = INDEX_NONE;
	double NominalTailTimeOffsetSeconds = 0.0;
	FString TransferFailure;
	if (!BuildTransfer(
		Preset,
		ReleasedQualifiedResult,
		*NominalPhysicalResult,
		TransferContract,
		Points,
		TransferStartTimeSeconds,
		TransferEndTimeSeconds,
		NominalTailFirstIndex,
		NominalTailTimeOffsetSeconds,
		TransferFailure))
	{
		return Reject(TransferFailure.IsEmpty()
			? TEXT("NoValidVisibleTerminalTransfer")
			: TransferFailure);
	}
	AppendResult(
		*NominalPhysicalResult,
		EABTSM11PlaybackSegmentKind::CertifiedNominalTail,
		Points,
		NominalTailFirstIndex,
		NominalTailTimeOffsetSeconds);
	if (Points.Num() < 3)
	{
		return Reject(TEXT("EmptyCertifiedNominalTail"));
	}

	PhysicalTrajectoryHash = NominalPhysicalResult->ValidationHash;
	TransferContractVersion = TransferContract.ContractVersion;
	bPhysicalTargetHit = true;
	bUsesVisibleTerminalTransfer = true;
	DurationSeconds = Points.Last().TimeSeconds;
	PlanHash = ComputePlanHash(*this);
	return true;
}

bool FABTSM11PlaybackPlan::BuildCandidateQualified(
	const FABTSM11FinaleLayoutPreset& Preset,
	const FABTSM11TrajectoryResult& ReleasedQualifiedResult,
	const FABTSM11PrefixClassification& Classification)
{
	Reset();
	const auto Reject = [this](FString Reason)
	{
		Failure = MoveTemp(Reason);
		Points.Reset();
		return false;
	};
	if (!Preset.IsValid()
		|| ReleasedQualifiedResult.Points.Num() < 2
		|| ReleasedQualifiedResult.ValidationHash == 0)
	{
		return Reject(TEXT("InvalidCandidateTrajectory"));
	}

	ReleasedTrajectoryHash = ReleasedQualifiedResult.ValidationHash;
	bQualifiedF4 = Classification.IsF(4);
	bCandidateQualifiedIntercept =
		bQualifiedF4 && ReleasedQualifiedResult.DidHitTarget();
	AppendResult(
		ReleasedQualifiedResult,
		EABTSM11PlaybackSegmentKind::PlayerAuthoritative,
		Points);
	DurationSeconds = Points.Last().TimeSeconds;
	PlanHash = ComputePlanHash(*this);
	return true;
}

bool FABTSM11PlaybackPlan::BuildCandidatePresentationContact(
	const FABTSM11FinaleLayoutPreset& Preset,
	const FABTSM11TrajectoryResult& ReleasedQualifiedResult,
	const FABTSM11PrefixClassification& Classification,
	const FABTSM11TerminalTransferContract& TransferContract)
{
	if (!BuildCandidateQualified(
		Preset,
		ReleasedQualifiedResult,
		Classification))
	{
		return false;
	}
	if (!bCandidateQualifiedIntercept || !TransferContract.IsValid())
	{
		Failure = TEXT("CandidatePresentationContactRequiresQualifiedF4");
		Points.Reset();
		return false;
	}

	FString TransferFailure;
	if (!BuildCandidateContactTransfer(
		Preset,
		ReleasedQualifiedResult,
		TransferContract,
		Points,
		TransferStartTimeSeconds,
		TransferEndTimeSeconds,
		TransferFailure))
	{
		Failure = TransferFailure.IsEmpty()
			? TEXT("NoValidCandidatePresentationContact")
			: MoveTemp(TransferFailure);
		Points.Reset();
		return false;
	}

	TransferContractVersion = TransferContract.ContractVersion;
	bPhysicalTargetHit = true;
	bUsesVisibleTerminalTransfer = true;
	DurationSeconds = Points.Last().TimeSeconds;
	PlanHash = ComputePlanHash(*this);
	Failure.Reset();
	return true;
}

bool FABTSM11PlaybackPlan::Sample(
	const double TimeSeconds,
	FVector3d& OutPositionCM,
	FVector3d& OutVelocityCMPerSec,
	EABTSM11PlaybackSegmentKind* OutSegmentKind) const
{
	if (Points.IsEmpty() || !FMath::IsFinite(TimeSeconds))
	{
		return false;
	}
	if (TimeSeconds <= Points[0].TimeSeconds)
	{
		OutPositionCM = Points[0].PositionCM;
		OutVelocityCMPerSec = Points[0].VelocityCMPerSec;
		if (OutSegmentKind != nullptr)
		{
			*OutSegmentKind = Points[0].SegmentKind;
		}
		return true;
	}
	if (TimeSeconds >= Points.Last().TimeSeconds)
	{
		OutPositionCM = Points.Last().PositionCM;
		OutVelocityCMPerSec = Points.Last().VelocityCMPerSec;
		if (OutSegmentKind != nullptr)
		{
			*OutSegmentKind = Points.Last().SegmentKind;
		}
		return true;
	}

	const int32 UpperIndex = Algo::UpperBoundBy(
		Points,
		TimeSeconds,
		[](const FABTSM11PlaybackPoint& Point)
		{
			return Point.TimeSeconds;
		});
	if (UpperIndex <= 0 || UpperIndex >= Points.Num())
	{
		return false;
	}
	const FABTSM11PlaybackPoint& A = Points[UpperIndex - 1];
	const FABTSM11PlaybackPoint& B = Points[UpperIndex];
	const double Duration = B.TimeSeconds - A.TimeSeconds;
	if (Duration <= UE_DOUBLE_SMALL_NUMBER)
	{
		OutPositionCM = B.PositionCM;
		OutVelocityCMPerSec = B.VelocityCMPerSec;
	}
	else
	{
		const double Alpha = FMath::Clamp(
			(TimeSeconds - A.TimeSeconds) / Duration,
			0.0,
			1.0);
		const double Alpha2 = Alpha * Alpha;
		const double Alpha3 = Alpha2 * Alpha;
		const double H00 = 2.0 * Alpha3 - 3.0 * Alpha2 + 1.0;
		const double H10 = Alpha3 - 2.0 * Alpha2 + Alpha;
		const double H01 = -2.0 * Alpha3 + 3.0 * Alpha2;
		const double H11 = Alpha3 - Alpha2;
		OutPositionCM = A.PositionCM * H00
			+ A.VelocityCMPerSec * (Duration * H10)
			+ B.PositionCM * H01
			+ B.VelocityCMPerSec * (Duration * H11);
		const double DH00 = 6.0 * Alpha2 - 6.0 * Alpha;
		const double DH10 = 3.0 * Alpha2 - 4.0 * Alpha + 1.0;
		const double DH01 = -DH00;
		const double DH11 = 3.0 * Alpha2 - 2.0 * Alpha;
		OutVelocityCMPerSec =
			(A.PositionCM * DH00 + B.PositionCM * DH01) / Duration
			+ A.VelocityCMPerSec * DH10
			+ B.VelocityCMPerSec * DH11;
	}
	if (OutSegmentKind != nullptr)
	{
		*OutSegmentKind = B.SegmentKind;
	}
	return true;
}
