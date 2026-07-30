// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/ABTSSweptCollision.h"

bool ABTSSweptCollision::SegmentSphereFirstAlpha(
	const FVector& Start,
	const FVector& End,
	const FVector& Center,
	const float RadiusCM,
	float& OutAlpha)
{
	OutAlpha = BIG_NUMBER;
	const double SafeRadius = FMath::Max(0.0, static_cast<double>(RadiusCM));
	const FVector Offset = Start - Center;
	if (Offset.SizeSquared() <= FMath::Square(SafeRadius))
	{
		OutAlpha = 0.0f;
		return true;
	}
	const FVector Segment = End - Start;
	const double A = Segment.SizeSquared();
	if (A <= UE_DOUBLE_SMALL_NUMBER) return false;
	const double B = 2.0 * FVector::DotProduct(Offset, Segment);
	const double C = Offset.SizeSquared() - FMath::Square(SafeRadius);
	const double Discriminant = B * B - 4.0 * A * C;
	if (Discriminant < 0.0) return false;
	const double Root = FMath::Sqrt(Discriminant);
	const double Alpha0 = (-B - Root) / (2.0 * A);
	const double Alpha1 = (-B + Root) / (2.0 * A);
	const double Alpha = Alpha0 >= 0.0 && Alpha0 <= 1.0
		? Alpha0
		: Alpha1 >= 0.0 && Alpha1 <= 1.0 ? Alpha1 : -1.0;
	if (Alpha < 0.0) return false;
	OutAlpha = static_cast<float>(Alpha);
	return true;
}

bool ABTSSweptCollision::SegmentExpandedOrientedBoxFirstAlpha(
	const FVector& Start,
	const FVector& End,
	const FTransform& BoxWorldTransform,
	const FVector& BoxHalfExtentCM,
	const float SweptSphereRadiusCM,
	float& OutAlpha)
{
	OutAlpha = BIG_NUMBER;
	const FTransform BoxNoScale(
		BoxWorldTransform.GetRotation(),
		BoxWorldTransform.GetLocation());
	const FVector LocalStart = BoxNoScale.InverseTransformPosition(Start);
	const FVector LocalEnd = BoxNoScale.InverseTransformPosition(End);
	const FVector Delta = LocalEnd - LocalStart;
	const FVector Extent = BoxHalfExtentCM.GetAbs();
	const double SphereRadius =
		FMath::Max(
			0.0,
			static_cast<double>(SweptSphereRadiusCM));
	const auto PointDistanceSquaredToBox =
		[&Extent](const FVector& Point)
		{
			const FVector Outside(
				FMath::Max(
					FMath::Abs(Point.X) - Extent.X,
					0.0),
				FMath::Max(
					FMath::Abs(Point.Y) - Extent.Y,
					0.0),
				FMath::Max(
					FMath::Abs(Point.Z) - Extent.Z,
					0.0));
			return Outside.SizeSquared();
		};
	const double RadiusSquared = FMath::Square(SphereRadius);
	if (PointDistanceSquaredToBox(LocalStart)
		<= RadiusSquared)
	{
		OutAlpha = 0.0f;
		return true;
	}
	if (Delta.SizeSquared() <= UE_DOUBLE_SMALL_NUMBER)
	{
		return false;
	}

	// Outside-axis membership changes only when the point crosses a box face.
	// Within each interval, squared Euclidean distance to the box is one
	// quadratic, so solving its earliest radius root is exact.
	TArray<double, TInlineAllocator<8>> Breakpoints;
	Breakpoints.Add(0.0);
	Breakpoints.Add(1.0);
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const double DeltaValue = Delta[Axis];
		if (FMath::Abs(DeltaValue)
			<= UE_DOUBLE_SMALL_NUMBER)
		{
			continue;
		}
		for (const double Boundary :
			{
				-static_cast<double>(Extent[Axis]),
				static_cast<double>(Extent[Axis])
			})
		{
			const double Alpha =
				(Boundary - LocalStart[Axis])
				/ DeltaValue;
			if (Alpha > 0.0 && Alpha < 1.0)
			{
				Breakpoints.Add(Alpha);
			}
		}
	}
	Breakpoints.Sort();
	constexpr double AlphaTolerance = 1.0e-9;
	for (int32 IntervalIndex = 0;
		IntervalIndex + 1 < Breakpoints.Num();
		++IntervalIndex)
	{
		const double IntervalStart =
			Breakpoints[IntervalIndex];
		const double IntervalEnd =
			Breakpoints[IntervalIndex + 1];
		if (IntervalEnd
			<= IntervalStart + AlphaTolerance)
		{
			continue;
		}
		const FVector IntervalStartPoint =
			LocalStart + Delta * IntervalStart;
		if (PointDistanceSquaredToBox(IntervalStartPoint)
			<= RadiusSquared)
		{
			OutAlpha =
				static_cast<float>(IntervalStart);
			return true;
		}
		const double MidAlpha =
			(IntervalStart + IntervalEnd) * 0.5;
		const FVector MidPoint =
			LocalStart + Delta * MidAlpha;
		double A = 0.0;
		double B = 0.0;
		double C = -RadiusSquared;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			double Boundary = 0.0;
			if (MidPoint[Axis] < -Extent[Axis])
			{
				Boundary = -Extent[Axis];
			}
			else if (MidPoint[Axis] > Extent[Axis])
			{
				Boundary = Extent[Axis];
			}
			else
			{
				continue;
			}
			const double Constant =
				LocalStart[Axis] - Boundary;
			const double AxisDelta = Delta[Axis];
			A += AxisDelta * AxisDelta;
			B += 2.0 * AxisDelta * Constant;
			C += Constant * Constant;
		}

		double CandidateAlpha = BIG_NUMBER;
		if (A <= UE_DOUBLE_SMALL_NUMBER)
		{
			if (FMath::Abs(B)
				> UE_DOUBLE_SMALL_NUMBER)
			{
				CandidateAlpha = -C / B;
			}
		}
		else
		{
			const double Discriminant =
				B * B - 4.0 * A * C;
			if (Discriminant >= 0.0)
			{
				const double Root =
					FMath::Sqrt(
						FMath::Max(
							0.0,
							Discriminant));
				const double Root0 =
					(-B - Root) / (2.0 * A);
				const double Root1 =
					(-B + Root) / (2.0 * A);
				if (Root0
						>= IntervalStart - AlphaTolerance
					&& Root0
						<= IntervalEnd + AlphaTolerance)
				{
					CandidateAlpha = Root0;
				}
				else if (Root1
						>= IntervalStart - AlphaTolerance
					&& Root1
						<= IntervalEnd + AlphaTolerance)
				{
					CandidateAlpha = Root1;
				}
			}
		}
		if (CandidateAlpha < BIG_NUMBER)
		{
			CandidateAlpha =
				FMath::Clamp(
					CandidateAlpha,
					IntervalStart,
					IntervalEnd);
			const FVector CandidatePoint =
				LocalStart + Delta * CandidateAlpha;
			if (PointDistanceSquaredToBox(CandidatePoint)
				<= RadiusSquared + 1.0e-5)
			{
				OutAlpha =
					static_cast<float>(CandidateAlpha);
				return true;
			}
		}
		const FVector IntervalEndPoint =
			LocalStart + Delta * IntervalEnd;
		if (PointDistanceSquaredToBox(IntervalEndPoint)
			<= RadiusSquared)
		{
			OutAlpha =
				static_cast<float>(IntervalEnd);
			return true;
		}
	}
	return false;
}

float ABTSSweptCollision::SegmentExpandedOrientedBoxMinimumClearance(
	const FVector& Start,
	const FVector& End,
	const FTransform& BoxWorldTransform,
	const FVector& BoxHalfExtentCM,
	const float SweptSphereRadiusCM,
	float* OutClosestAlpha)
{
	const FTransform BoxNoScale(
		BoxWorldTransform.GetRotation(),
		BoxWorldTransform.GetLocation());
	const FVector LocalStart = BoxNoScale.InverseTransformPosition(Start);
	const FVector LocalEnd = BoxNoScale.InverseTransformPosition(End);
	const FVector Delta = LocalEnd - LocalStart;
	const FVector Extent = BoxHalfExtentCM.GetAbs();
	const auto PointDistanceSquaredToBox =
		[&Extent](const FVector& Point)
		{
			const FVector Outside(
				FMath::Max(FMath::Abs(Point.X) - Extent.X, 0.0),
				FMath::Max(FMath::Abs(Point.Y) - Extent.Y, 0.0),
				FMath::Max(FMath::Abs(Point.Z) - Extent.Z, 0.0));
			return Outside.SizeSquared();
		};

	double MinimumDistanceSquared =
		PointDistanceSquaredToBox(LocalStart);
	double ClosestAlpha = 0.0;
	if (Delta.SizeSquared() > UE_DOUBLE_SMALL_NUMBER)
	{
		TArray<double, TInlineAllocator<8>> Breakpoints;
		Breakpoints.Add(0.0);
		Breakpoints.Add(1.0);
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const double DeltaValue = Delta[Axis];
			if (FMath::Abs(DeltaValue) <= UE_DOUBLE_SMALL_NUMBER)
			{
				continue;
			}
			for (const double Boundary :
				{
					-static_cast<double>(Extent[Axis]),
					static_cast<double>(Extent[Axis])
				})
			{
				const double Alpha =
					(Boundary - LocalStart[Axis]) / DeltaValue;
				if (Alpha > 0.0 && Alpha < 1.0)
				{
					Breakpoints.Add(Alpha);
				}
			}
		}
		Breakpoints.Sort();
		constexpr double AlphaTolerance = 1.0e-9;
		const auto ConsiderAlpha =
			[&](const double CandidateAlpha)
			{
				const double ClampedAlpha =
					FMath::Clamp(CandidateAlpha, 0.0, 1.0);
				const double DistanceSquared =
					PointDistanceSquaredToBox(
						LocalStart + Delta * ClampedAlpha);
				if (DistanceSquared
						< MinimumDistanceSquared - UE_DOUBLE_SMALL_NUMBER
					|| (FMath::IsNearlyEqual(
							DistanceSquared,
							MinimumDistanceSquared,
							UE_DOUBLE_SMALL_NUMBER)
						&& ClampedAlpha < ClosestAlpha))
				{
					MinimumDistanceSquared = DistanceSquared;
					ClosestAlpha = ClampedAlpha;
				}
			};
		for (int32 IntervalIndex = 0;
			IntervalIndex + 1 < Breakpoints.Num();
			++IntervalIndex)
		{
			const double IntervalStart = Breakpoints[IntervalIndex];
			const double IntervalEnd = Breakpoints[IntervalIndex + 1];
			if (IntervalEnd <= IntervalStart + AlphaTolerance)
			{
				continue;
			}
			ConsiderAlpha(IntervalStart);
			ConsiderAlpha(IntervalEnd);
			const double MidAlpha =
				(IntervalStart + IntervalEnd) * 0.5;
			const FVector MidPoint =
				LocalStart + Delta * MidAlpha;
			double A = 0.0;
			double B = 0.0;
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				double Boundary = 0.0;
				if (MidPoint[Axis] < -Extent[Axis])
				{
					Boundary = -Extent[Axis];
				}
				else if (MidPoint[Axis] > Extent[Axis])
				{
					Boundary = Extent[Axis];
				}
				else
				{
					continue;
				}
				const double Constant =
					LocalStart[Axis] - Boundary;
				const double AxisDelta = Delta[Axis];
				A += AxisDelta * AxisDelta;
				B += 2.0 * AxisDelta * Constant;
			}
			if (A > UE_DOUBLE_SMALL_NUMBER)
			{
				ConsiderAlpha(
					FMath::Clamp(
						-B / (2.0 * A),
						IntervalStart,
						IntervalEnd));
			}
		}
	}
	if (OutClosestAlpha)
	{
		*OutClosestAlpha = static_cast<float>(ClosestAlpha);
	}
	return static_cast<float>(
		FMath::Sqrt(FMath::Max(0.0, MinimumDistanceSquared))
		- FMath::Max(
			0.0,
			static_cast<double>(SweptSphereRadiusCM)));
}

float ABTSSweptCollision::PointExpandedOrientedBoxClearance(
	const FVector& Point,
	const FTransform& BoxWorldTransform,
	const FVector& BoxHalfExtentCM,
	const float SweptSphereRadiusCM)
{
	const FTransform BoxNoScale(
		BoxWorldTransform.GetRotation(),
		BoxWorldTransform.GetLocation());
	const FVector Local = BoxNoScale.InverseTransformPosition(Point);
	const FVector Extent = BoxHalfExtentCM.GetAbs();
	const FVector BoxDistance =
		Local.GetAbs() - Extent;
	const FVector Outside(
		FMath::Max(BoxDistance.X, 0.0),
		FMath::Max(BoxDistance.Y, 0.0),
		FMath::Max(BoxDistance.Z, 0.0));
	const double Inside =
		FMath::Min(
			FMath::Max3(
				BoxDistance.X,
				BoxDistance.Y,
				BoxDistance.Z),
			0.0);
	return static_cast<float>(
		Outside.Size()
		+ Inside
		- FMath::Max(
			0.0,
			static_cast<double>(SweptSphereRadiusCM)));
}
