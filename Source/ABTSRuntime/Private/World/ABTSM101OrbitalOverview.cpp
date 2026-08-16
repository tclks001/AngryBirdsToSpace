// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM10ScoutMapSystem.h"

#include "ABTSRuntime.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Slingshot/ABTSM6Types.h"
#include "Terrain/ABTSM3Planet.h"
#include "World/ABTSM9Satellite.h"

namespace
{
	struct FOrbitalWorldBody
	{
		FVector Center = FVector::ZeroVector;
		float RadiusCM = 0.0f;
		bool bPrimary = false;
	};

	struct FOrbitalPlaneFrame
	{
		FVector Origin = FVector::ZeroVector;
		FVector AxisX = FVector::ForwardVector;
		FVector AxisY = FVector::RightVector;
		FVector Normal = FVector::UpVector;
	};

	FVector2D ProjectToPlane(const FVector& WorldPoint, const FOrbitalPlaneFrame& Frame)
	{
		const FVector Relative = WorldPoint - Frame.Origin;
		return FVector2D(
			FVector::DotProduct(Relative, Frame.AxisX),
			FVector::DotProduct(Relative, Frame.AxisY));
	}

	double Cross2D(const FVector2D& Origin, const FVector2D& A, const FVector2D& B)
	{
		return static_cast<double>(A.X - Origin.X) * static_cast<double>(B.Y - Origin.Y)
			- static_cast<double>(A.Y - Origin.Y) * static_cast<double>(B.X - Origin.X);
	}

	TArray<FVector2D> BuildConvexHull(const TArray<FVector2D>& Points)
	{
		TArray<FVector2D> Sorted = Points;
		Sorted.Sort([](const FVector2D& A, const FVector2D& B)
		{
			// TArray::Sort requires a strict weak ordering. Keep tolerance-based
			// duplicate removal below, outside the comparator.
			if (A.X != B.X) return A.X < B.X;
			return A.Y < B.Y;
		});
		for (int32 Index = Sorted.Num() - 1; Index > 0; --Index)
		{
			if (Sorted[Index].Equals(Sorted[Index - 1], 0.01f))
			{
				Sorted.RemoveAt(Index, 1, EAllowShrinking::No);
			}
		}
		if (Sorted.Num() <= 2) return Sorted;

		TArray<FVector2D> Hull;
		Hull.Reserve(Sorted.Num() * 2);
		for (const FVector2D& Point : Sorted)
		{
			while (Hull.Num() >= 2
				&& Cross2D(Hull[Hull.Num() - 2], Hull.Last(), Point) <= 0.0)
			{
				Hull.Pop(EAllowShrinking::No);
			}
			Hull.Add(Point);
		}
		const int32 LowerCount = Hull.Num();
		for (int32 Index = Sorted.Num() - 2; Index >= 0; --Index)
		{
			const FVector2D& Point = Sorted[Index];
			while (Hull.Num() > LowerCount
				&& Cross2D(Hull[Hull.Num() - 2], Hull.Last(), Point) <= 0.0)
			{
				Hull.Pop(EAllowShrinking::No);
			}
			Hull.Add(Point);
		}
		if (Hull.Num() > 1) Hull.Pop(EAllowShrinking::No);
		return Hull;
	}

	bool SolveSmallestCovarianceAxis(
		const TArray<FVector>& Points,
		FVector& OutCentroid,
		FVector& OutNormal)
	{
		OutCentroid = FVector::ZeroVector;
		OutNormal = FVector::ZeroVector;
		if (Points.Num() < 3) return false;

		TArray<double> Weights;
		Weights.Init(0.0, Points.Num());
		double TotalWeight = 0.0;
		for (int32 Index = 0; Index + 1 < Points.Num(); ++Index)
		{
			const double SegmentWeight = FMath::Max(
				static_cast<double>(FVector::Distance(Points[Index], Points[Index + 1])), 0.001);
			Weights[Index] += SegmentWeight * 0.5;
			Weights[Index + 1] += SegmentWeight * 0.5;
			TotalWeight += SegmentWeight;
		}
		if (TotalWeight <= UE_DOUBLE_SMALL_NUMBER) return false;

		FVector3d WeightedCenter = FVector3d::ZeroVector;
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			WeightedCenter += FVector3d(Points[Index]) * Weights[Index];
		}
		WeightedCenter /= TotalWeight;
		OutCentroid = FVector(WeightedCenter);

		double Matrix[3][3] = {};
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			const FVector3d Delta = FVector3d(Points[Index]) - WeightedCenter;
			const double Weight = Weights[Index];
			Matrix[0][0] += Weight * Delta.X * Delta.X;
			Matrix[0][1] += Weight * Delta.X * Delta.Y;
			Matrix[0][2] += Weight * Delta.X * Delta.Z;
			Matrix[1][1] += Weight * Delta.Y * Delta.Y;
			Matrix[1][2] += Weight * Delta.Y * Delta.Z;
			Matrix[2][2] += Weight * Delta.Z * Delta.Z;
		}
		Matrix[1][0] = Matrix[0][1];
		Matrix[2][0] = Matrix[0][2];
		Matrix[2][1] = Matrix[1][2];

		double Eigenvectors[3][3] =
		{
			{ 1.0, 0.0, 0.0 },
			{ 0.0, 1.0, 0.0 },
			{ 0.0, 0.0, 1.0 }
		};
		for (int32 Iteration = 0; Iteration < 16; ++Iteration)
		{
			int32 P = 0;
			int32 Q = 1;
			double Largest = FMath::Abs(Matrix[0][1]);
			if (FMath::Abs(Matrix[0][2]) > Largest)
			{
				P = 0;
				Q = 2;
				Largest = FMath::Abs(Matrix[0][2]);
			}
			if (FMath::Abs(Matrix[1][2]) > Largest)
			{
				P = 1;
				Q = 2;
				Largest = FMath::Abs(Matrix[1][2]);
			}
			const double Trace = FMath::Abs(Matrix[0][0])
				+ FMath::Abs(Matrix[1][1]) + FMath::Abs(Matrix[2][2]);
			if (Largest <= FMath::Max(Trace * 1.0e-12, 1.0e-8)) break;

			const double App = Matrix[P][P];
			const double Aqq = Matrix[Q][Q];
			const double Apq = Matrix[P][Q];
			const double Tau = (Aqq - App) / (2.0 * Apq);
			const double T = (Tau >= 0.0 ? 1.0 : -1.0)
				/ (FMath::Abs(Tau) + FMath::Sqrt(1.0 + Tau * Tau));
			const double C = 1.0 / FMath::Sqrt(1.0 + T * T);
			const double S = T * C;

			for (int32 K = 0; K < 3; ++K)
			{
				if (K == P || K == Q) continue;
				const double Akp = Matrix[K][P];
				const double Akq = Matrix[K][Q];
				Matrix[K][P] = Matrix[P][K] = C * Akp - S * Akq;
				Matrix[K][Q] = Matrix[Q][K] = S * Akp + C * Akq;
			}
			Matrix[P][P] = App - T * Apq;
			Matrix[Q][Q] = Aqq + T * Apq;
			Matrix[P][Q] = Matrix[Q][P] = 0.0;
			for (int32 K = 0; K < 3; ++K)
			{
				const double Vkp = Eigenvectors[K][P];
				const double Vkq = Eigenvectors[K][Q];
				Eigenvectors[K][P] = C * Vkp - S * Vkq;
				Eigenvectors[K][Q] = S * Vkp + C * Vkq;
			}
		}

		int32 Order[3] = { 0, 1, 2 };
		for (int32 A = 0; A < 2; ++A)
		{
			for (int32 B = A + 1; B < 3; ++B)
			{
				if (Matrix[Order[B]][Order[B]] < Matrix[Order[A]][Order[A]])
				{
					Swap(Order[A], Order[B]);
				}
			}
		}
		const double LargestEigenvalue = FMath::Max(Matrix[Order[2]][Order[2]], 0.0);
		const double MiddleEigenvalue = FMath::Max(Matrix[Order[1]][Order[1]], 0.0);
		const double SmallestEigenvalue = FMath::Max(Matrix[Order[0]][Order[0]], 0.0);
		if (LargestEigenvalue <= 1.0e-6 || MiddleEigenvalue <= LargestEigenvalue * 1.0e-6)
		{
			return false;
		}
		// If the two lowest eigenvalues are nearly equal, the best-fit normal is
		// not unique. Defer to the history/physical fallback instead of allowing
		// tiny aim changes to swap eigenvectors and rotate the diagram.
		if (MiddleEigenvalue - SmallestEigenvalue <= LargestEigenvalue * 1.0e-5)
		{
			return false;
		}
		const int32 SmallestIndex = Order[0];
		OutNormal = FVector(
			Eigenvectors[0][SmallestIndex],
			Eigenvectors[1][SmallestIndex],
			Eigenvectors[2][SmallestIndex]).GetSafeNormal();
		return !OutNormal.IsNearlyZero();
	}

	FVector BuildFallbackNormal(
		const FABTSM6TrajectoryPreview& Preview,
		const FVector& PrimaryCenter,
		const FVector& PreviousNormal)
	{
		const TArray<FVector>& Points = Preview.WorldPoints;
		const FVector Start = Points.IsEmpty() ? Preview.InitialWorldLocation : Points[0];
		const FVector End = Points.Num() > 1 ? Points.Last() : Start + Preview.InitialWorldVelocity;
		const FVector Chord = End - Start;
		if (!PreviousNormal.IsNearlyZero() && !Chord.IsNearlyZero())
		{
			const FVector Candidate = FVector::VectorPlaneProject(PreviousNormal, Chord).GetSafeNormal();
			if (!Candidate.IsNearlyZero()) return Candidate;
		}

		float GreatestDistanceSquared = 0.0f;
		FVector GreatestPoint = FVector::ZeroVector;
		const FVector ChordDirection = Chord.GetSafeNormal();
		for (const FVector& Point : Points)
		{
			const FVector FromStart = Point - Start;
			const FVector Perpendicular = FromStart - ChordDirection * FVector::DotProduct(FromStart, ChordDirection);
			if (Perpendicular.SizeSquared() > GreatestDistanceSquared)
			{
				GreatestDistanceSquared = Perpendicular.SizeSquared();
				GreatestPoint = Point;
			}
		}
		FVector Candidate = FVector::CrossProduct(Chord, GreatestPoint - Start).GetSafeNormal();
		if (!Candidate.IsNearlyZero()) return Candidate;

		Candidate = FVector::CrossProduct(Start - PrimaryCenter, Preview.InitialWorldVelocity).GetSafeNormal();
		if (!Candidate.IsNearlyZero()) return Candidate;

		const FVector Direction = !Chord.IsNearlyZero()
			? ChordDirection
			: Preview.InitialWorldVelocity.GetSafeNormal();
		const FVector WorldAxes[] = { FVector::UpVector, FVector::ForwardVector, FVector::RightVector };
		float BestCrossSquared = -1.0f;
		for (const FVector& Axis : WorldAxes)
		{
			const FVector Cross = FVector::CrossProduct(Direction, Axis);
			if (Cross.SizeSquared() > BestCrossSquared)
			{
				BestCrossSquared = Cross.SizeSquared();
				Candidate = Cross.GetSafeNormal();
			}
		}
		return Candidate.IsNearlyZero() ? FVector::UpVector : Candidate;
	}

	FVector ChooseTemporaryAxis(const FVector& Normal)
	{
		const FVector WorldAxes[] = { FVector::ForwardVector, FVector::RightVector, FVector::UpVector };
		FVector BestAxis = FVector::ZeroVector;
		float BestLengthSquared = -1.0f;
		for (const FVector& WorldAxis : WorldAxes)
		{
			const FVector Candidate = FVector::VectorPlaneProject(WorldAxis, Normal);
			if (Candidate.SizeSquared() > BestLengthSquared)
			{
				BestLengthSquared = Candidate.SizeSquared();
				BestAxis = Candidate;
			}
		}
		return BestAxis.GetSafeNormal();
	}

	bool IsSupportDirection(
		const TArray<FVector>& Points,
		const FVector& LaunchPoint,
		const FVector& Candidate,
		const float ToleranceCM)
	{
		for (const FVector& Point : Points)
		{
			if (FVector::DotProduct(Point - LaunchPoint, Candidate) < -ToleranceCM) return false;
		}
		return true;
	}

	FVector OrientAxisWithLaunchOnLeft(
		const TArray<FVector>& Points,
		const FVector& LaunchPoint,
		FVector Candidate)
	{
		float MinimumProjection = 0.0f;
		float MaximumProjection = 0.0f;
		for (const FVector& Point : Points)
		{
			const float Projection = FVector::DotProduct(Point - LaunchPoint, Candidate);
			MinimumProjection = FMath::Min(MinimumProjection, Projection);
			MaximumProjection = FMath::Max(MaximumProjection, Projection);
		}
		// The eventual horizontal framing center is (min + max) / 2. Flip the
		// axis when needed so the launch point at zero is always on its left.
		if (MinimumProjection + MaximumProjection < 0.0f) Candidate *= -1.0f;
		return Candidate;
	}

	FVector ChooseHorizontalAxis(
		const TArray<FVector>& Points,
		const FVector& PlaneOrigin,
		const FVector& Normal,
		const FVector& PreviousAxis)
	{
		const FVector TempX = ChooseTemporaryAxis(Normal);
		const FVector TempY = FVector::CrossProduct(Normal, TempX).GetSafeNormal();
		const FVector Launch = Points[0];
		TArray<FVector2D> TemporaryPoints;
		TemporaryPoints.Reserve(Points.Num());
		for (const FVector& Point : Points)
		{
			const FVector Relative = Point - PlaneOrigin;
			TemporaryPoints.Emplace(
				FVector::DotProduct(Relative, TempX),
				FVector::DotProduct(Relative, TempY));
		}
		const TArray<FVector2D> Hull = BuildConvexHull(TemporaryPoints);
		const float SpanTolerance = FMath::Max(1.0f, FVector::Distance(Points[0], Points.Last()) * 0.001f);

		if (!PreviousAxis.IsNearlyZero())
		{
			FVector Candidate = FVector::VectorPlaneProject(PreviousAxis, Normal).GetSafeNormal();
			if (!Candidate.IsNearlyZero() && IsSupportDirection(Points, Launch, Candidate, SpanTolerance))
			{
				return OrientAxisWithLaunchOnLeft(Points, Launch, Candidate);
			}
		}

		const FVector2D Launch2D = TemporaryPoints[0];
		int32 LaunchHullIndex = INDEX_NONE;
		for (int32 Index = 0; Index < Hull.Num(); ++Index)
		{
			if (Hull[Index].Equals(Launch2D, SpanTolerance))
			{
				LaunchHullIndex = Index;
				break;
			}
		}
		if (LaunchHullIndex != INDEX_NONE && Hull.Num() >= 3)
		{
			const FVector2D ToPrevious =
				(Hull[(LaunchHullIndex - 1 + Hull.Num()) % Hull.Num()] - Launch2D).GetSafeNormal();
			const FVector2D ToNext =
				(Hull[(LaunchHullIndex + 1) % Hull.Num()] - Launch2D).GetSafeNormal();
			const FVector2D InwardBisector = (ToPrevious + ToNext).GetSafeNormal();
			if (!InwardBisector.IsNearlyZero())
			{
				FVector Candidate = (TempX * InwardBisector.X + TempY * InwardBisector.Y).GetSafeNormal();
				if (IsSupportDirection(Points, Launch, Candidate, SpanTolerance))
				{
					return OrientAxisWithLaunchOnLeft(Points, Launch, Candidate);
				}
			}
		}

		FVector Candidate = FVector::VectorPlaneProject(PlaneOrigin - Launch, Normal).GetSafeNormal();
		if (Candidate.IsNearlyZero())
		{
			Candidate = FVector::VectorPlaneProject(Points.Last() - Launch, Normal).GetSafeNormal();
		}
		if (Candidate.IsNearlyZero()) Candidate = TempX;
		float MeanProjection = 0.0f;
		for (const FVector& Point : Points)
		{
			MeanProjection += FVector::DotProduct(Point - Launch, Candidate);
		}
		if (MeanProjection < 0.0f) Candidate *= -1.0f;
		return OrientAxisWithLaunchOnLeft(Points, Launch, Candidate);
	}

	bool IsOccludedByBody(
		const FVector& WorldPoint,
		const FVector& ViewNormal,
		const FOrbitalWorldBody& Body)
	{
		const FVector Relative = WorldPoint - Body.Center;
		const double B = FVector::DotProduct(Relative, ViewNormal);
		const double C = Relative.SizeSquared() - FMath::Square(static_cast<double>(Body.RadiusCM));
		const double Discriminant = B * B - C;
		if (Discriminant <= 0.0) return false;
		const double RayExitDistance = -B + FMath::Sqrt(Discriminant);
		return RayExitDistance > FMath::Max(1.0, static_cast<double>(Body.RadiusCM) * 0.001);
	}

	bool IsOccludedByAnyBody(
		const FVector& WorldPoint,
		const FVector& ViewNormal,
		const TArray<FOrbitalWorldBody>& Bodies)
	{
		for (const FOrbitalWorldBody& Body : Bodies)
		{
			if (IsOccludedByBody(WorldPoint, ViewNormal, Body)) return true;
		}
		return false;
	}

	using FTransitionRoots = TArray<double, TInlineAllocator<32>>;

	void AddQuadraticRoots(
		const double A,
		const double B,
		const double C,
		FTransitionRoots& InOutRoots)
	{
		if (FMath::Abs(A) <= 1.0e-12) return;
		const double Discriminant = B * B - 4.0 * A * C;
		if (Discriminant <= 0.0) return;
		const double Root = FMath::Sqrt(Discriminant);
		const double T0 = (-B - Root) / (2.0 * A);
		const double T1 = (-B + Root) / (2.0 * A);
		if (T0 > 1.0e-5 && T0 < 1.0 - 1.0e-5) InOutRoots.Add(T0);
		if (T1 > 1.0e-5 && T1 < 1.0 - 1.0e-5) InOutRoots.Add(T1);
	}

	void AddBodyTransitionRoots(
		const FVector& Start,
		const FVector& End,
		const FVector& ViewNormal,
		const FOrbitalWorldBody& Body,
		FTransitionRoots& InOutRoots)
	{
		const FVector RelativeStart = Start - Body.Center;
		const FVector Segment = End - Start;
		const FVector PerpendicularStart =
			RelativeStart - ViewNormal * FVector::DotProduct(RelativeStart, ViewNormal);
		const FVector PerpendicularSegment =
			Segment - ViewNormal * FVector::DotProduct(Segment, ViewNormal);
		AddQuadraticRoots(
			PerpendicularSegment.SizeSquared(),
			2.0 * FVector::DotProduct(PerpendicularStart, PerpendicularSegment),
			PerpendicularStart.SizeSquared() - FMath::Square(Body.RadiusCM),
			InOutRoots);
		AddQuadraticRoots(
			Segment.SizeSquared(),
			2.0 * FVector::DotProduct(RelativeStart, Segment),
			RelativeStart.SizeSquared() - FMath::Square(Body.RadiusCM),
			InOutRoots);
	}

	void AppendTrajectorySegmentIntervals(
		const FVector& WorldStart,
		const FVector& WorldEnd,
		const FVector2D& PlaneStart,
		const FVector2D& PlaneEnd,
		const FVector& ViewNormal,
		const TArray<FOrbitalWorldBody>& Bodies,
		TArray<FABTSM101OrbitalLineSegment>& OutSegments)
	{
		FTransitionRoots Roots;
		Roots.Reserve(Bodies.Num() * 4 + 2);
		Roots.Add(0.0);
		Roots.Add(1.0);
		for (const FOrbitalWorldBody& Body : Bodies)
		{
			AddBodyTransitionRoots(WorldStart, WorldEnd, ViewNormal, Body, Roots);
		}
		Roots.Sort();
		for (int32 Index = Roots.Num() - 1; Index > 0; --Index)
		{
			if (FMath::Abs(Roots[Index] - Roots[Index - 1]) <= 1.0e-5)
			{
				Roots.RemoveAt(Index, 1, EAllowShrinking::No);
			}
		}
		for (int32 Index = 0; Index + 1 < Roots.Num(); ++Index)
		{
			const double T0 = Roots[Index];
			const double T1 = Roots[Index + 1];
			if (T1 - T0 <= 1.0e-5) continue;
			const double MidT = (T0 + T1) * 0.5;
			const FVector MidPoint = FMath::Lerp(WorldStart, WorldEnd, static_cast<float>(MidT));
			FABTSM101OrbitalLineSegment& Segment = OutSegments.AddDefaulted_GetRef();
			Segment.Start = FMath::Lerp(PlaneStart, PlaneEnd, static_cast<float>(T0));
			Segment.End = FMath::Lerp(PlaneStart, PlaneEnd, static_cast<float>(T1));
			Segment.bDashed = IsOccludedByAnyBody(MidPoint, ViewNormal, Bodies);
		}
	}

	void AppendGridChord(
		const FVector& WorldStart,
		const FVector& WorldEnd,
		const FVector& BodyCenter,
		const FOrbitalPlaneFrame& Frame,
		TArray<FABTSM101OrbitalLineSegment>& OutSegments)
	{
		const float StartDepth = FVector::DotProduct(WorldStart - BodyCenter, Frame.Normal);
		const float EndDepth = FVector::DotProduct(WorldEnd - BodyCenter, Frame.Normal);
		const FVector2D PlaneStart = ProjectToPlane(WorldStart, Frame);
		const FVector2D PlaneEnd = ProjectToPlane(WorldEnd, Frame);
		if ((StartDepth >= 0.0f) == (EndDepth >= 0.0f))
		{
			FABTSM101OrbitalLineSegment& Segment = OutSegments.AddDefaulted_GetRef();
			Segment.Start = PlaneStart;
			Segment.End = PlaneEnd;
			Segment.bDashed = StartDepth < 0.0f;
			return;
		}
		const float SplitT = StartDepth / (StartDepth - EndDepth);
		const FVector2D SplitPoint = FMath::Lerp(PlaneStart, PlaneEnd, SplitT);
		FABTSM101OrbitalLineSegment& First = OutSegments.AddDefaulted_GetRef();
		First.Start = PlaneStart;
		First.End = SplitPoint;
		First.bDashed = StartDepth < 0.0f;
		FABTSM101OrbitalLineSegment& Second = OutSegments.AddDefaulted_GetRef();
		Second.Start = SplitPoint;
		Second.End = PlaneEnd;
		Second.bDashed = EndDepth < 0.0f;
	}

	void BuildPrimaryGrid(
		const FOrbitalWorldBody& Primary,
		const FOrbitalPlaneFrame& Frame,
		const FABTSM10ScoutMapSettings& Settings,
		TArray<FABTSM101OrbitalLineSegment>& OutSegments)
	{
		const float LatitudeStep = FMath::Clamp(Settings.OrbitalDiagramLatitudeStepDegrees, 15.0f, 90.0f);
		const float LongitudeStep = FMath::Clamp(Settings.OrbitalDiagramLongitudeStepDegrees, 15.0f, 90.0f);
		constexpr float CurveSampleStepDegrees = 10.0f;
		OutSegments.Reserve(OutSegments.Num() + 2048);

		const auto AppendLatitude = [&](const float Latitude)
		{
			const float LatitudeRadians = FMath::DegreesToRadians(Latitude);
			const float CosLatitude = FMath::Cos(LatitudeRadians);
			const float SinLatitude = FMath::Sin(LatitudeRadians);
			FVector PreviousPoint = FVector::ZeroVector;
			bool bHasPrevious = false;
			for (float Longitude = 0.0f; Longitude <= 360.0f + 0.1f; Longitude += CurveSampleStepDegrees)
			{
				const float LongitudeRadians = FMath::DegreesToRadians(Longitude);
				const FVector Point = Primary.Center + Primary.RadiusCM * FVector(
					CosLatitude * FMath::Cos(LongitudeRadians),
					CosLatitude * FMath::Sin(LongitudeRadians),
					SinLatitude);
				if (bHasPrevious)
				{
					AppendGridChord(PreviousPoint, Point, Primary.Center, Frame, OutSegments);
				}
				PreviousPoint = Point;
				bHasPrevious = true;
			}
		};

		// Absolute latitude is anchored at the world-space equator. Generate
		// symmetric positive/negative parallels so arbitrary editor step sizes
		// cannot shift the grid or omit latitude zero.
		AppendLatitude(0.0f);
		for (float Latitude = LatitudeStep; Latitude < 90.0f - 0.1f; Latitude += LatitudeStep)
		{
			AppendLatitude(Latitude);
			AppendLatitude(-Latitude);
		}

		for (float Longitude = 0.0f; Longitude < 360.0f - 0.1f; Longitude += LongitudeStep)
		{
			const float LongitudeRadians = FMath::DegreesToRadians(Longitude);
			FVector PreviousPoint = FVector::ZeroVector;
			bool bHasPrevious = false;
			for (float Latitude = -90.0f; Latitude <= 90.0f + 0.1f; Latitude += CurveSampleStepDegrees)
			{
				const float LatitudeRadians = FMath::DegreesToRadians(Latitude);
				const float CosLatitude = FMath::Cos(LatitudeRadians);
				const FVector Point = Primary.Center + Primary.RadiusCM * FVector(
					CosLatitude * FMath::Cos(LongitudeRadians),
					CosLatitude * FMath::Sin(LongitudeRadians),
					FMath::Sin(LatitudeRadians));
				if (bHasPrevious)
				{
					AppendGridChord(PreviousPoint, Point, Primary.Center, Frame, OutSegments);
				}
				PreviousPoint = Point;
				bHasPrevious = true;
			}
		}
	}

	bool SegmentIntersectsSphereBeforeEnd(
		const FVector& Start,
		const FVector& End,
		const FVector& SphereCenter,
		const float SphereRadius)
	{
		const FVector Segment = End - Start;
		const FVector RelativeStart = Start - SphereCenter;
		const double A = Segment.SizeSquared();
		if (A <= UE_DOUBLE_SMALL_NUMBER) return false;
		const double B = 2.0 * FVector::DotProduct(RelativeStart, Segment);
		const double C = RelativeStart.SizeSquared() - FMath::Square(static_cast<double>(SphereRadius));
		const double Discriminant = B * B - 4.0 * A * C;
		if (Discriminant <= 0.0) return false;
		const double NearT = (-B - FMath::Sqrt(Discriminant)) / (2.0 * A);
		return NearT > 1.0e-4 && NearT < 0.995;
	}

	bool IsLandingOutsideMainView(
		const UWorld* World,
		const AABTSM3Planet& Planet,
		const FABTSM6TrajectoryPreview& Preview,
		const FABTSM10ScoutMapSettings& Settings,
		const bool bAlreadyVisible)
	{
		if (World == nullptr || !Preview.bHasPrimarySurfaceLanding) return false;
		APlayerController* Controller = World->GetFirstPlayerController();
		if (Controller == nullptr) return false;
		int32 ViewWidth = 0;
		int32 ViewHeight = 0;
		Controller->GetViewportSize(ViewWidth, ViewHeight);
		FVector2D ScreenPosition;
		const bool bProjected = Controller->ProjectWorldLocationToScreen(
			Preview.PrimarySurfaceLandingWorld, ScreenPosition, false);
		const float BaseInset = FMath::Clamp(Settings.OrbitalDiagramViewportInsetRatio, 0.0f, 0.25f);
		const float Inset = FMath::Clamp(
			bAlreadyVisible ? BaseInset + 0.04f : BaseInset, 0.0f, 0.30f);
		const bool bInsideSafeFrame = bProjected && ViewWidth > 0 && ViewHeight > 0
			&& ScreenPosition.X >= static_cast<float>(ViewWidth) * Inset
			&& ScreenPosition.X <= static_cast<float>(ViewWidth) * (1.0f - Inset)
			&& ScreenPosition.Y >= static_cast<float>(ViewHeight) * Inset
			&& ScreenPosition.Y <= static_cast<float>(ViewHeight) * (1.0f - Inset);
		if (!bInsideSafeFrame) return true;

		const APlayerCameraManager* CameraManager = Controller->PlayerCameraManager;
		return CameraManager != nullptr
			&& SegmentIntersectsSphereBeforeEnd(
				CameraManager->GetCameraLocation(),
				Preview.PrimarySurfaceLandingWorld,
				Planet.GetPlanetCenterWorld(),
				Planet.GetPlanetRadiusCM());
	}
}

void AABTSM10ScoutMapSystem::UpdateOrbitalOverview()
{
	const bool bWasVisible = OrbitalOverviewSnapshot.bValid;
	if (GetNetMode() == NM_DedicatedServer
		|| !Settings.bShowOrbitalOverview
		|| !bScoutMapRevealed
		|| !Planet.IsValid())
	{
		ClearOrbitalOverview(bWasVisible);
		return;
	}

	FABTSM6TrajectoryPreview Preview;
	if (!CopyCurrentTrajectoryPreview(Preview)
		|| Preview.SlingshotTier != EABTSSlingshotTier::Reinforced
		|| Preview.WorldPoints.Num() < 2)
	{
		ClearOrbitalOverview(bWasVisible);
		return;
	}

	const float FirstShowThreshold = FMath::Max(1000.0f, Settings.OrbitalDiagramMinPathLengthCM);
	const float Hysteresis = FMath::Clamp(
		Settings.OrbitalDiagramPathLengthHysteresisCM, 0.0f, FirstShowThreshold - 1.0f);
	const float ActiveThreshold = bWasVisible
		? FirstShowThreshold - Hysteresis
		: FirstShowThreshold;
	const bool bLengthQualified = Preview.PredictedPathLengthCM >= ActiveThreshold;
	const bool bLandingOutsideView = IsLandingOutsideMainView(
		GetWorld(), *Planet.Get(), Preview, Settings, bWasVisible);
	if (!bLengthQualified && !bLandingOutsideView)
	{
		ClearOrbitalOverview(bWasVisible);
		return;
	}

	const bool bPredictionChanged =
		CachedOrbitalPreviewPointCount != Preview.WorldPoints.Num()
		|| CachedOrbitalPreviewGravityHash != Preview.GravitySnapshotHash
		|| !CachedOrbitalPreviewStart.Equals(Preview.InitialWorldLocation, 0.1f)
		|| !CachedOrbitalPreviewVelocity.Equals(Preview.InitialWorldVelocity, 0.1f)
		|| !FMath::IsNearlyEqual(
			CachedOrbitalPreviewPathLengthCM, Preview.PredictedPathLengthCM, 0.1f);
	if (!bPredictionChanged && bWasVisible) return;

	if (!BuildOrbitalOverviewSnapshot(Preview))
	{
		ClearOrbitalOverview(bWasVisible);
		return;
	}
	CachedOrbitalPreviewStart = Preview.InitialWorldLocation;
	CachedOrbitalPreviewVelocity = Preview.InitialWorldVelocity;
	CachedOrbitalPreviewPathLengthCM = Preview.PredictedPathLengthCM;
	CachedOrbitalPreviewPointCount = Preview.WorldPoints.Num();
	CachedOrbitalPreviewGravityHash = Preview.GravitySnapshotHash;
	if (!bWasVisible)
	{
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M10.1][OrbitalOverview] Visible Path=%.1f Points=%d Bodies=%d Trigger=%s"),
			Preview.PredictedPathLengthCM,
			Preview.WorldPoints.Num(),
			OrbitalOverviewSnapshot.Bodies.Num(),
			bLengthQualified ? TEXT("PathLength") : TEXT("MainView"));
	}
}

bool AABTSM10ScoutMapSystem::BuildOrbitalOverviewSnapshot(
	const FABTSM6TrajectoryPreview& Preview)
{
	if (!Planet.IsValid() || Preview.WorldPoints.Num() < 2) return false;

	FVector PlaneOrigin;
	FVector PlaneNormal;
	if (!SolveSmallestCovarianceAxis(Preview.WorldPoints, PlaneOrigin, PlaneNormal))
	{
		PlaneOrigin = FVector::ZeroVector;
		for (const FVector& Point : Preview.WorldPoints) PlaneOrigin += Point;
		PlaneOrigin /= static_cast<float>(Preview.WorldPoints.Num());
		PlaneNormal = BuildFallbackNormal(
			Preview, Planet->GetPlanetCenterWorld(), LastOrbitalPlaneNormal);
	}
	if (PlaneNormal.IsNearlyZero()) return false;

	if (!LastOrbitalPlaneNormal.IsNearlyZero())
	{
		if (FVector::DotProduct(PlaneNormal, LastOrbitalPlaneNormal) < 0.0f) PlaneNormal *= -1.0f;
	}
	const FVector HorizontalAxis = ChooseHorizontalAxis(
		Preview.WorldPoints, PlaneOrigin, PlaneNormal, LastOrbitalHorizontalAxis);
	if (HorizontalAxis.IsNearlyZero()) return false;
	FVector VerticalAxis = FVector::CrossProduct(PlaneNormal, HorizontalAxis).GetSafeNormal();
	if (!LastOrbitalPlaneNormal.IsNearlyZero() && !LastOrbitalHorizontalAxis.IsNearlyZero())
	{
		const FVector PreviousVertical =
			FVector::CrossProduct(LastOrbitalPlaneNormal, LastOrbitalHorizontalAxis).GetSafeNormal();
		if (!PreviousVertical.IsNearlyZero()
			&& FVector::DotProduct(VerticalAxis, PreviousVertical) < 0.0f)
		{
			PlaneNormal *= -1.0f;
			VerticalAxis *= -1.0f;
		}
	}
	else
	{
		// Establish a deterministic first-frame screen-up direction. Later
		// frames use the previous vertical axis and never switch reference axes.
		const FVector OrientationReferences[] =
		{
			FVector::UpVector,
			FVector::ForwardVector,
			FVector::RightVector
		};
		for (const FVector& Reference : OrientationReferences)
		{
			const float Alignment = FVector::DotProduct(VerticalAxis, Reference);
			if (FMath::Abs(Alignment) <= UE_KINDA_SMALL_NUMBER) continue;
			if (Alignment < 0.0f)
			{
				PlaneNormal *= -1.0f;
				VerticalAxis *= -1.0f;
			}
			break;
		}
	}

	const FOrbitalPlaneFrame Frame { PlaneOrigin, HorizontalAxis, VerticalAxis, PlaneNormal };
	TArray<FVector2D> ProjectedPoints;
	ProjectedPoints.Reserve(Preview.WorldPoints.Num());
	for (const FVector& Point : Preview.WorldPoints)
	{
		ProjectedPoints.Add(ProjectToPlane(Point, Frame));
	}
	const TArray<FVector2D> Hull = BuildConvexHull(ProjectedPoints);
	if (Hull.IsEmpty()) return false;
	FVector2D BoundsMin(TNumericLimits<float>::Max(), TNumericLimits<float>::Max());
	FVector2D BoundsMax(TNumericLimits<float>::Lowest(), TNumericLimits<float>::Lowest());
	for (const FVector2D& Point : Hull)
	{
		BoundsMin.X = FMath::Min(BoundsMin.X, Point.X);
		BoundsMin.Y = FMath::Min(BoundsMin.Y, Point.Y);
		BoundsMax.X = FMath::Max(BoundsMax.X, Point.X);
		BoundsMax.Y = FMath::Max(BoundsMax.Y, Point.Y);
	}
	const FVector2D ContentCenter = (BoundsMin + BoundsMax) * 0.5f;
	float ContentRadius = 0.0f;
	for (const FVector2D& Point : Hull)
	{
		ContentRadius = FMath::Max(ContentRadius, FVector2D::Distance(Point, ContentCenter));
	}
	if (ContentRadius <= 1.0f) return false;

	TArray<FOrbitalWorldBody> WorldBodies;
	WorldBodies.Reserve(4);
	FOrbitalWorldBody PrimaryBody;
	PrimaryBody.Center = Planet->GetPlanetCenterWorld();
	PrimaryBody.RadiusCM = FMath::Max(1.0f, Planet->GetPlanetRadiusCM());
	PrimaryBody.bPrimary = true;
	WorldBodies.Add(PrimaryBody);
	for (TActorIterator<AABTSM9Satellite> It(GetWorld()); It; ++It)
	{
		if (It->IsActorBeingDestroyed() || !It->IsPlanetReady()) continue;
		FOrbitalWorldBody& SatelliteBody = WorldBodies.AddDefaulted_GetRef();
		SatelliteBody.Center = It->GetPlanetCenterWorld();
		SatelliteBody.RadiusCM = FMath::Max(1.0f, It->GetPlanetRadiusCM());
		SatelliteBody.bPrimary = false;
	}

	FABTSM101OrbitalOverviewSnapshot Candidate;
	Candidate.ContentCenter = ContentCenter;
	Candidate.ContentRadiusCM = ContentRadius;
	Candidate.LaunchPoint = ProjectedPoints[0];
	Candidate.SourcePathLengthCM = Preview.PredictedPathLengthCM;
	Candidate.SourcePointCount = Preview.WorldPoints.Num();
	Candidate.Bodies.Reserve(WorldBodies.Num());
	for (const FOrbitalWorldBody& WorldBody : WorldBodies)
	{
		FABTSM101OrbitalBody& Body = Candidate.Bodies.AddDefaulted_GetRef();
		Body.Center = ProjectToPlane(WorldBody.Center, Frame);
		Body.RadiusCM = WorldBody.RadiusCM;
		Body.bPrimary = WorldBody.bPrimary;
	}

	Candidate.TrajectorySegments.Reserve(Preview.WorldPoints.Num() * 2);
	for (int32 Index = 0; Index + 1 < Preview.WorldPoints.Num(); ++Index)
	{
		AppendTrajectorySegmentIntervals(
			Preview.WorldPoints[Index],
			Preview.WorldPoints[Index + 1],
			ProjectedPoints[Index],
			ProjectedPoints[Index + 1],
			Frame.Normal,
			WorldBodies,
			Candidate.TrajectorySegments);
	}
	BuildPrimaryGrid(PrimaryBody, Frame, Settings, Candidate.PrimaryGridSegments);

	if (Preview.bHasPrimarySurfaceLanding)
	{
		const FVector LandingDirection =
			(Preview.PrimarySurfaceLandingWorld - PrimaryBody.Center).GetSafeNormal();
		if (!LandingDirection.IsNearlyZero())
		{
			Candidate.bHasLandingPoint = true;
			Candidate.LandingPoint = ProjectToPlane(
				PrimaryBody.Center + LandingDirection * PrimaryBody.RadiusCM, Frame);
			Candidate.ContentRadiusCM = FMath::Max(
				Candidate.ContentRadiusCM,
				FVector2D::Distance(Candidate.LandingPoint, Candidate.ContentCenter));
		}
	}
	Candidate.bValid = true;
	OrbitalOverviewSnapshot = MoveTemp(Candidate);
	LastOrbitalPlaneNormal = Frame.Normal;
	LastOrbitalHorizontalAxis = Frame.AxisX;
	return true;
}

void AABTSM10ScoutMapSystem::ClearOrbitalOverview(const bool bLogTransition)
{
	if (bLogTransition)
	{
		UE_LOG(LogABTSRuntime, Verbose, TEXT("[ABTS][M10.1][OrbitalOverview] Hidden"));
	}
	OrbitalOverviewSnapshot.Reset();
	CachedOrbitalPreviewStart = FVector::ZeroVector;
	CachedOrbitalPreviewVelocity = FVector::ZeroVector;
	CachedOrbitalPreviewPathLengthCM = -1.0f;
	CachedOrbitalPreviewPointCount = 0;
	CachedOrbitalPreviewGravityHash = 0;
	LastOrbitalPlaneNormal = FVector::ZeroVector;
	LastOrbitalHorizontalAxis = FVector::ZeroVector;
}
