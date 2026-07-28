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
