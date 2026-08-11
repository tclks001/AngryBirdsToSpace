// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM11FinaleFormation.h"

#include "Algo/BinarySearch.h"

namespace
{
	bool IsFiniteFormationVector(const FVector& Value)
	{
		return !Value.ContainsNaN()
			&& FMath::IsFinite(Value.X)
			&& FMath::IsFinite(Value.Y)
			&& FMath::IsFinite(Value.Z);
	}
}

void FABTSM11FinaleFormationPath::Reset()
{
	Nodes.Reset();
	TotalArcLengthCM = 0.0;
}

bool FABTSM11FinaleFormationPath::Build(
	const FABTSM11PlaybackPlan& Plan,
	FString* OutFailure)
{
	Reset();
	if (Plan.Points.Num() < 2)
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = TEXT("FormationPlaybackPlanTooShort");
		}
		return false;
	}
	const auto AddNode = [&](const double TimeSeconds) -> bool
	{
		FVector3d Position;
		FVector3d Velocity;
		if (!Plan.Sample(TimeSeconds, Position, Velocity)
			|| Position.ContainsNaN()
			|| Velocity.ContainsNaN())
		{
			return false;
		}
		FABTSM11FinaleFormationPathNode Node;
		Node.TimeSeconds = TimeSeconds;
		Node.PositionCM = Position;
		Node.VelocityCMPerSec = Velocity;
		if (!Nodes.IsEmpty())
		{
			Node.ArcLengthCM = Nodes.Last().ArcLengthCM
				+ FVector3d::Distance(Nodes.Last().PositionCM, Position);
		}
		Nodes.Add(Node);
		return true;
	};
	if (!AddNode(Plan.Points[0].TimeSeconds))
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = TEXT("FormationPlaybackStartRejected");
		}
		return false;
	}
	for (int32 PointIndex = 1; PointIndex < Plan.Points.Num(); ++PointIndex)
	{
		const double StartTime = Plan.Points[PointIndex - 1].TimeSeconds;
		const double EndTime = Plan.Points[PointIndex].TimeSeconds;
		if (!FMath::IsFinite(StartTime)
			|| !FMath::IsFinite(EndTime)
			|| EndTime <= StartTime)
		{
			Reset();
			if (OutFailure != nullptr)
			{
				*OutFailure = TEXT("FormationPlaybackTimeOrderRejected");
			}
			return false;
		}
		for (int32 Subsample = 1;
			Subsample <= SubsamplesPerPlaybackSegment;
			++Subsample)
		{
			const double Alpha = static_cast<double>(Subsample)
				/ static_cast<double>(SubsamplesPerPlaybackSegment);
			if (!AddNode(FMath::Lerp(StartTime, EndTime, Alpha)))
			{
				Reset();
				if (OutFailure != nullptr)
				{
					*OutFailure = TEXT("FormationPlaybackSampleRejected");
				}
				return false;
			}
		}
	}
	TotalArcLengthCM = Nodes.Last().ArcLengthCM;
	if (!FMath::IsFinite(TotalArcLengthCM)
		|| TotalArcLengthCM <= UE_DOUBLE_SMALL_NUMBER)
	{
		Reset();
		if (OutFailure != nullptr)
		{
			*OutFailure = TEXT("FormationPlaybackArcLengthRejected");
		}
		return false;
	}
	return true;
}

bool FABTSM11FinaleFormationPath::ResolveArcLengthAtTime(
	const double TimeSeconds,
	double& OutArcLengthCM) const
{
	if (Nodes.IsEmpty() || !FMath::IsFinite(TimeSeconds))
	{
		return false;
	}
	if (TimeSeconds <= Nodes[0].TimeSeconds)
	{
		OutArcLengthCM = 0.0;
		return true;
	}
	if (TimeSeconds >= Nodes.Last().TimeSeconds)
	{
		OutArcLengthCM = TotalArcLengthCM;
		return true;
	}
	const int32 UpperIndex = Algo::UpperBoundBy(
		Nodes,
		TimeSeconds,
		[](const FABTSM11FinaleFormationPathNode& Node)
		{
			return Node.TimeSeconds;
		});
	if (UpperIndex <= 0 || UpperIndex >= Nodes.Num())
	{
		return false;
	}
	const FABTSM11FinaleFormationPathNode& A = Nodes[UpperIndex - 1];
	const FABTSM11FinaleFormationPathNode& B = Nodes[UpperIndex];
	const double Alpha = (TimeSeconds - A.TimeSeconds)
		/ (B.TimeSeconds - A.TimeSeconds);
	OutArcLengthCM = FMath::Lerp(A.ArcLengthCM, B.ArcLengthCM, Alpha);
	return FMath::IsFinite(OutArcLengthCM);
}

bool FABTSM11FinaleFormationPath::SampleAtArcLength(
	const double ArcLengthCM,
	FVector3d& OutPositionCM,
	FVector3d& OutVelocityCMPerSec) const
{
	if (Nodes.IsEmpty() || !FMath::IsFinite(ArcLengthCM))
	{
		return false;
	}
	const double ClampedArc = FMath::Clamp(
		ArcLengthCM,
		0.0,
		TotalArcLengthCM);
	if (ClampedArc <= 0.0)
	{
		OutPositionCM = Nodes[0].PositionCM;
		OutVelocityCMPerSec = Nodes[0].VelocityCMPerSec;
		return true;
	}
	if (ClampedArc >= TotalArcLengthCM)
	{
		OutPositionCM = Nodes.Last().PositionCM;
		OutVelocityCMPerSec = Nodes.Last().VelocityCMPerSec;
		return true;
	}
	const int32 UpperIndex = Algo::UpperBoundBy(
		Nodes,
		ClampedArc,
		[](const FABTSM11FinaleFormationPathNode& Node)
		{
			return Node.ArcLengthCM;
		});
	if (UpperIndex <= 0 || UpperIndex >= Nodes.Num())
	{
		return false;
	}
	const FABTSM11FinaleFormationPathNode& A = Nodes[UpperIndex - 1];
	const FABTSM11FinaleFormationPathNode& B = Nodes[UpperIndex];
	const double Span = B.ArcLengthCM - A.ArcLengthCM;
	const double Alpha = Span > UE_DOUBLE_SMALL_NUMBER
		? (ClampedArc - A.ArcLengthCM) / Span
		: 1.0;
	OutPositionCM = FMath::Lerp(A.PositionCM, B.PositionCM, Alpha);
	OutVelocityCMPerSec = FMath::Lerp(
		A.VelocityCMPerSec,
		B.VelocityCMPerSec,
		Alpha);
	if (OutVelocityCMPerSec.SquaredLength() <= UE_DOUBLE_SMALL_NUMBER)
	{
		OutVelocityCMPerSec = B.PositionCM - A.PositionCM;
	}
	return !OutPositionCM.ContainsNaN()
		&& !OutVelocityCMPerSec.ContainsNaN();
}

bool ABTSM11FinaleFormationMath::BuildVelocityViewRotation(
	const FVector& WorldVelocity,
	const FVector& ViewUp,
	const FVector& ViewRight,
	const FQuat& PreviousActorRotation,
	FQuat& OutActorRotation)
{
	if (!IsFiniteFormationVector(WorldVelocity)
		|| !IsFiniteFormationVector(ViewUp)
		|| !IsFiniteFormationVector(ViewRight)
		|| PreviousActorRotation.ContainsNaN())
	{
		return false;
	}
	const FVector Forward = WorldVelocity.GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		return false;
	}
	FVector PresentationUp = FVector::VectorPlaneProject(
		ViewUp,
		Forward).GetSafeNormal();
	if (PresentationUp.IsNearlyZero())
	{
		PresentationUp = FVector::VectorPlaneProject(
			PreviousActorRotation.GetUpVector(),
			Forward).GetSafeNormal();
	}
	if (PresentationUp.IsNearlyZero())
	{
		const FVector PresentationRight = FVector::VectorPlaneProject(
			ViewRight,
			Forward).GetSafeNormal();
		if (PresentationRight.IsNearlyZero())
		{
			return false;
		}
		PresentationUp = FVector::CrossProduct(
			Forward,
			PresentationRight).GetSafeNormal();
	}
	const FQuat Resolved = FRotationMatrix::MakeFromXZ(
		Forward,
		PresentationUp).ToQuat().GetNormalized();
	if (Resolved.ContainsNaN())
	{
		return false;
	}
	OutActorRotation = Resolved;
	return true;
}
