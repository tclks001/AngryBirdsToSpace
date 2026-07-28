// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM110FinaleTypes.h"

namespace
{
	bool IsM110FiniteVector(const FVector3d& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}
}

FVector FABTSM110FinaleLocalFrame::TransformLocalPosition(const FVector& LocalPositionCM) const
{
	return WorldTransform.TransformPositionNoScale(LocalPositionCM);
}

FVector FABTSM110FinaleLocalFrame::InverseTransformPosition(const FVector& WorldPositionCM) const
{
	return WorldTransform.InverseTransformPositionNoScale(WorldPositionCM);
}

bool FABTSM110FinaleLocalFrame::IsOrthonormal(const double Tolerance) const
{
	const FVector Forward = GetForward();
	const FVector Right = GetRight();
	const FVector Up = GetUp();
	return FMath::IsNearlyEqual(Forward.SizeSquared(), 1.0, Tolerance)
		&& FMath::IsNearlyEqual(Right.SizeSquared(), 1.0, Tolerance)
		&& FMath::IsNearlyEqual(Up.SizeSquared(), 1.0, Tolerance)
		&& FMath::Abs(FVector::DotProduct(Forward, Right)) <= Tolerance
		&& FMath::Abs(FVector::DotProduct(Forward, Up)) <= Tolerance
		&& FMath::Abs(FVector::DotProduct(Right, Up)) <= Tolerance
		&& FVector::DotProduct(FVector::CrossProduct(Forward, Right), Up) >= 1.0 - Tolerance;
}

bool FABTSM110FinaleLocalFrame::IsUsable(const double Tolerance) const
{
	if (!bValid
		|| LayoutVersion <= 0
		|| LaunchTaskId == INDEX_NONE
		|| AnchorCellId == INDEX_NONE
		|| SlotPairId == INDEX_NONE
		|| !IsOrthonormal(Tolerance))
	{
		return false;
	}

	const FVector PairDelta = RightSlotWorldLocation - LeftSlotWorldLocation;
	if (PairDelta.IsNearlyZero(Tolerance))
	{
		return false;
	}

	const FVector PairDirection = PairDelta.GetSafeNormal();
	return FMath::Abs(FVector::DotProduct(PairDirection, GetRight())) >= 1.0 - Tolerance
		&& GetOrigin().Equals((LeftSlotWorldLocation + RightSlotWorldLocation) * 0.5, Tolerance);
}

bool FABTSM110FinaleGravityBody::IsValid() const
{
	return Role >= EABTSM110FinaleGravityRole::Primary
		&& Role < EABTSM110FinaleGravityRole::Count
		&& IsM110FiniteVector(CenterCM)
		&& FMath::IsFinite(GravitationalParameterCM3PerSec2)
		&& GravitationalParameterCM3PerSec2 > 0.0
		&& FMath::IsFinite(CollisionRadiusCM)
		&& CollisionRadiusCM > 0.0;
}

FABTSM110FinaleGravityScenario::FABTSM110FinaleGravityScenario()
{
	for (int32 Index = 0; Index < BodyCount; ++Index)
	{
		Bodies[Index].Role = static_cast<EABTSM110FinaleGravityRole>(Index);
	}
}

bool FABTSM110FinaleGravityScenario::IsValid(FString* OutFailure) const
{
	const auto Reject = [OutFailure](const TCHAR* Reason)
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = Reason;
		}
		return false;
	};

	if (LayoutVersion <= 0)
	{
		return Reject(TEXT("InvalidLayoutVersion"));
	}
	if (ScenarioHash == 0)
	{
		return Reject(TEXT("MissingScenarioHash"));
	}

	for (int32 Index = 0; Index < BodyCount; ++Index)
	{
		const EABTSM110FinaleGravityRole ExpectedRole = static_cast<EABTSM110FinaleGravityRole>(Index);
		if (Bodies[Index].Role != ExpectedRole)
		{
			return Reject(TEXT("GravityRoleOrder"));
		}
		if (!Bodies[Index].IsValid())
		{
			return Reject(TEXT("InvalidGravityBody"));
		}
	}
	return true;
}

FVector3d FABTSM110FinaleGravityScenario::GetAccelerationAt(const FVector3d& PositionCM) const
{
	FVector3d AccelerationCMPerSec2 = FVector3d::ZeroVector;
	for (const FABTSM110FinaleGravityBody& Body : Bodies)
	{
		if (!Body.IsValid())
		{
			continue;
		}

		const FVector3d ToCenter = Body.CenterCM - PositionCM;
		const double DistanceCM = ToCenter.Length();
		if (DistanceCM <= UE_DOUBLE_SMALL_NUMBER)
		{
			continue;
		}

		// The integrator is expected to emit a collision event before entering a
		// body. Clamping here only prevents a singular result in diagnostic calls.
		const double SafeDistanceCM = FMath::Max(DistanceCM, Body.CollisionRadiusCM);
		AccelerationCMPerSec2 += ToCenter / DistanceCM
			* (Body.GravitationalParameterCM3PerSec2 / FMath::Square(SafeDistanceCM));
	}
	return AccelerationCMPerSec2;
}
