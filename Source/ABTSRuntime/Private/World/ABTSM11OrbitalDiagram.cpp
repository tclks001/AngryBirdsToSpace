// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM11FinaleInteractionTypes.h"

namespace
{
	struct FProjectionBasis
	{
		FVector3d Mean = FVector3d::ZeroVector;
		FVector3d X = FVector3d::ForwardVector;
		FVector3d Y = FVector3d::RightVector;
		FVector3d Normal = FVector3d::UpVector;
		FVector2d ContentCenter = FVector2d::ZeroVector;
		double FitRadius = 1.0;
	};

	FVector3d MatrixColumn(const double Matrix[3][3], const int32 Column)
	{
		return FVector3d(
			Matrix[0][Column],
			Matrix[1][Column],
			Matrix[2][Column]);
	}

	void JacobiEigenvectors(
		double Matrix[3][3],
		double Eigenvectors[3][3])
	{
		for (int32 Row = 0; Row < 3; ++Row)
		{
			for (int32 Column = 0; Column < 3; ++Column)
			{
				Eigenvectors[Row][Column] = Row == Column ? 1.0 : 0.0;
			}
		}
		for (int32 Iteration = 0; Iteration < 24; ++Iteration)
		{
			int32 P = 0;
			int32 Q = 1;
			double Largest = FMath::Abs(Matrix[P][Q]);
			for (int32 Row = 0; Row < 3; ++Row)
			{
				for (int32 Column = Row + 1; Column < 3; ++Column)
				{
					const double Candidate =
						FMath::Abs(Matrix[Row][Column]);
					if (Candidate > Largest)
					{
						Largest = Candidate;
						P = Row;
						Q = Column;
					}
				}
			}
			if (Largest <= 1.0e-10)
			{
				break;
			}

			const double Angle = 0.5 * FMath::Atan2(
				2.0 * Matrix[P][Q],
				Matrix[Q][Q] - Matrix[P][P]);
			const double C = FMath::Cos(Angle);
			const double S = FMath::Sin(Angle);
			const double PP = Matrix[P][P];
			const double QQ = Matrix[Q][Q];
			const double PQ = Matrix[P][Q];
			Matrix[P][P] =
				C * C * PP - 2.0 * S * C * PQ + S * S * QQ;
			Matrix[Q][Q] =
				S * S * PP + 2.0 * S * C * PQ + C * C * QQ;
			Matrix[P][Q] = 0.0;
			Matrix[Q][P] = 0.0;
			for (int32 K = 0; K < 3; ++K)
			{
				if (K == P || K == Q)
				{
					continue;
				}
				const double KP = Matrix[K][P];
				const double KQ = Matrix[K][Q];
				Matrix[K][P] = Matrix[P][K] = C * KP - S * KQ;
				Matrix[K][Q] = Matrix[Q][K] = S * KP + C * KQ;
			}
			for (int32 Row = 0; Row < 3; ++Row)
			{
				const double VP = Eigenvectors[Row][P];
				const double VQ = Eigenvectors[Row][Q];
				Eigenvectors[Row][P] = C * VP - S * VQ;
				Eigenvectors[Row][Q] = S * VP + C * VQ;
			}
		}
	}

	FVector2d ProjectRaw(
		const FProjectionBasis& Basis,
		const FVector3d& Position)
	{
		const FVector3d Relative = Position - Basis.Mean;
		return FVector2d(
			Relative.Dot(Basis.X),
			Relative.Dot(Basis.Y));
	}

	FVector2d NormalizeProjection(
		const FProjectionBasis& Basis,
		const FVector3d& Position)
	{
		return (ProjectRaw(Basis, Position) - Basis.ContentCenter)
			/ Basis.FitRadius;
	}

	double ProjectDepth(
		const FProjectionBasis& Basis,
		const FVector3d& Position)
	{
		return (Position - Basis.Mean).Dot(Basis.Normal);
	}

	bool BuildBasis(
		TConstArrayView<FABTSM11PlaybackPoint> Points,
		const FABTSM110FinaleLocalFrame& FinaleFrame,
		FProjectionBasis& OutBasis)
	{
		if (Points.Num() < 2)
		{
			return false;
		}
		for (const FABTSM11PlaybackPoint& Point : Points)
		{
			OutBasis.Mean += Point.PositionCM;
		}
		OutBasis.Mean /= static_cast<double>(Points.Num());

		double Covariance[3][3] = {};
		for (const FABTSM11PlaybackPoint& Point : Points)
		{
			const FVector3d D = Point.PositionCM - OutBasis.Mean;
			const double V[3] = {D.X, D.Y, D.Z};
			for (int32 Row = 0; Row < 3; ++Row)
			{
				for (int32 Column = Row; Column < 3; ++Column)
				{
					Covariance[Row][Column] += V[Row] * V[Column];
				}
			}
		}
		for (int32 Row = 0; Row < 3; ++Row)
		{
			for (int32 Column = Row; Column < 3; ++Column)
			{
				Covariance[Row][Column] /= Points.Num();
				Covariance[Column][Row] = Covariance[Row][Column];
			}
		}
		double Eigenvectors[3][3] = {};
		JacobiEigenvectors(Covariance, Eigenvectors);
		int32 Smallest = 0;
		for (int32 Index = 1; Index < 3; ++Index)
		{
			if (Covariance[Index][Index]
				< Covariance[Smallest][Smallest])
			{
				Smallest = Index;
			}
		}
		OutBasis.Normal =
			MatrixColumn(Eigenvectors, Smallest).GetSafeNormal();
		if (OutBasis.Normal.IsNearlyZero())
		{
			OutBasis.Normal = FVector3d::UpVector;
		}

		const FVector WorldUpLocal =
			FinaleFrame.WorldTransform.InverseTransformVectorNoScale(
				FVector::UpVector);
		if (OutBasis.Normal.Dot(FVector3d(WorldUpLocal)) < 0.0)
		{
			OutBasis.Normal *= -1.0;
		}

		const FVector3d Start = Points[0].PositionCM;
		double FarthestDistanceSquared = 0.0;
		FVector3d ForwardCandidate = FVector3d::ZeroVector;
		for (const FABTSM11PlaybackPoint& Point : Points)
		{
			FVector3d Delta = Point.PositionCM - Start;
			Delta -= OutBasis.Normal * Delta.Dot(OutBasis.Normal);
			const double DistanceSquared = Delta.SquaredLength();
			if (DistanceSquared > FarthestDistanceSquared)
			{
				FarthestDistanceSquared = DistanceSquared;
				ForwardCandidate = Delta;
			}
		}
		OutBasis.X = ForwardCandidate.GetSafeNormal();
		if (OutBasis.X.IsNearlyZero())
		{
			OutBasis.X = FVector3d::ForwardVector
				- OutBasis.Normal
					* OutBasis.Normal.Dot(FVector3d::ForwardVector);
			OutBasis.X.Normalize();
		}
		OutBasis.Y = OutBasis.Normal.Cross(OutBasis.X).GetSafeNormal();

		auto ComputeBounds = [&Points, &OutBasis](
			FVector2d& OutMinimum,
			FVector2d& OutMaximum)
		{
			OutMinimum = FVector2d(
				TNumericLimits<double>::Max(),
				TNumericLimits<double>::Max());
			OutMaximum = FVector2d(
				TNumericLimits<double>::Lowest(),
				TNumericLimits<double>::Lowest());
			for (const FABTSM11PlaybackPoint& Point : Points)
			{
				const FVector2d Projected =
					ProjectRaw(OutBasis, Point.PositionCM);
				OutMinimum.X = FMath::Min(OutMinimum.X, Projected.X);
				OutMinimum.Y = FMath::Min(OutMinimum.Y, Projected.Y);
				OutMaximum.X = FMath::Max(OutMaximum.X, Projected.X);
				OutMaximum.Y = FMath::Max(OutMaximum.Y, Projected.Y);
			}
		};

		FVector2d Minimum;
		FVector2d Maximum;
		ComputeBounds(Minimum, Maximum);
		OutBasis.ContentCenter = (Minimum + Maximum) * 0.5;
		if (ProjectRaw(OutBasis, Start).X > OutBasis.ContentCenter.X)
		{
			OutBasis.X *= -1.0;
			OutBasis.Y =
				OutBasis.Normal.Cross(OutBasis.X).GetSafeNormal();
			ComputeBounds(Minimum, Maximum);
			OutBasis.ContentCenter = (Minimum + Maximum) * 0.5;
		}

		double Radius = 0.0;
		for (const FABTSM11PlaybackPoint& Point : Points)
		{
			Radius = FMath::Max(
				Radius,
				(ProjectRaw(OutBasis, Point.PositionCM)
					- OutBasis.ContentCenter).Length());
		}
		// Keep all trajectory samples inside 86% of the circular panel.
		OutBasis.FitRadius = FMath::Max(1.0, Radius / 0.86);
		return true;
	}

	void AddCircleRoots(
		const FVector2d& Start,
		const FVector2d& End,
		const FVector2d& Center,
		const double Radius,
		TArray<double>& InOutAlphas)
	{
		const FVector2d D = End - Start;
		const FVector2d F = Start - Center;
		const double A = D.SquaredLength();
		if (A <= UE_DOUBLE_SMALL_NUMBER || Radius <= 0.0)
		{
			return;
		}
		const double B = 2.0 * F.Dot(D);
		const double C = F.SquaredLength() - Radius * Radius;
		const double Discriminant = B * B - 4.0 * A * C;
		if (Discriminant <= 0.0)
		{
			return;
		}
		const double Root = FMath::Sqrt(Discriminant);
		const double Alpha0 = (-B - Root) / (2.0 * A);
		const double Alpha1 = (-B + Root) / (2.0 * A);
		if (Alpha0 > 1.0e-8 && Alpha0 < 1.0 - 1.0e-8)
		{
			InOutAlphas.Add(Alpha0);
		}
		if (Alpha1 > 1.0e-8 && Alpha1 < 1.0 - 1.0e-8)
		{
			InOutAlphas.Add(Alpha1);
		}
	}

	bool IsHiddenByAnyBody(
		const FProjectionBasis& Basis,
		const FABTSM11FinaleLayoutPreset& Preset,
		const FVector3d& Position)
	{
		const FVector2d Projected = ProjectRaw(Basis, Position);
		const double Depth = ProjectDepth(Basis, Position);
		for (const FABTSM11GravityBodySpec& Body
			: Preset.CanonicalScenario.Bodies)
		{
			const FVector2d BodyProjected =
				ProjectRaw(Basis, Body.CenterCM);
			const double RadialSquared =
				(Projected - BodyProjected).SquaredLength();
			const double RadiusSquared =
				FMath::Square(Body.VisualRadiusCM);
			if (RadialSquared >= RadiusSquared)
			{
				continue;
			}
			const double FrontSurfaceDepth =
				ProjectDepth(Basis, Body.CenterCM)
				+ FMath::Sqrt(RadiusSquared - RadialSquared);
			if (Depth < FrontSurfaceDepth)
			{
				return true;
			}
		}
		return false;
	}

	TArray<int32> BuildDecimatedIndices(
		TConstArrayView<FABTSM11PlaybackPoint> Points,
		const int32 MaximumCount)
	{
		TArray<int32> Indices;
		if (Points.IsEmpty())
		{
			return Indices;
		}
		const int32 SafeMaximum = FMath::Max(2, MaximumCount);
		const int32 Stride = FMath::Max(
			1,
			FMath::CeilToInt(
				static_cast<double>(Points.Num())
				/ static_cast<double>(SafeMaximum)));
		for (int32 Index = 0; Index < Points.Num(); Index += Stride)
		{
			Indices.Add(Index);
		}
		Indices.Add(Points.Num() - 1);
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			if (Points[Index].SegmentKind
				!= Points[Index - 1].SegmentKind)
			{
				Indices.Add(Index - 1);
				Indices.Add(Index);
			}
		}
		Indices.Sort();
		for (int32 Index = Indices.Num() - 1; Index > 0; --Index)
		{
			if (Indices[Index] == Indices[Index - 1])
			{
				Indices.RemoveAt(Index, 1, EAllowShrinking::No);
			}
		}
		return Indices;
	}

	FVector3d WorldToFinaleLocal(
		const FABTSM110FinaleLocalFrame& Frame,
		const FVector& WorldPosition)
	{
		return FVector3d(Frame.InverseTransformPosition(WorldPosition));
	}

	void AddGridPolylineSegment(
		const FProjectionBasis& Basis,
		const FVector3d& A,
		const FVector3d& B,
		const double VisibilityA,
		const double VisibilityB,
		TArray<FABTSM11DiagramGridSegment>& OutSegments)
	{
		const auto Add = [&Basis, &OutSegments](
			const FVector3d& Start,
			const FVector3d& End,
			const bool bHidden)
		{
			FABTSM11DiagramGridSegment& Segment =
				OutSegments.AddDefaulted_GetRef();
			Segment.Start = NormalizeProjection(Basis, Start);
			Segment.End = NormalizeProjection(Basis, End);
			Segment.bHiddenHemisphere = bHidden;
		};
		const bool bHiddenA = VisibilityA < 0.0;
		const bool bHiddenB = VisibilityB < 0.0;
		if (bHiddenA == bHiddenB)
		{
			Add(A, B, bHiddenA);
			return;
		}
		const double Alpha = FMath::Clamp(
			VisibilityA / (VisibilityA - VisibilityB),
			0.0,
			1.0);
		const FVector3d Split = FMath::Lerp(A, B, Alpha);
		Add(A, Split, bHiddenA);
		Add(Split, B, bHiddenB);
	}

	void BuildPrimaryGrid(
		const FProjectionBasis& Basis,
		const FABTSM11FinaleLayoutPreset& Preset,
		const FABTSM110FinaleLocalFrame& Frame,
		TArray<FABTSM11DiagramGridSegment>& OutSegments)
	{
		const FABTSM11GravityBodySpec& Primary =
			Preset.CanonicalScenario.GetPrimary();
		const FVector WorldCenter = Frame.TransformLocalPosition(
			FVector(Primary.CenterCM));
		const double Radius = Primary.VisualRadiusCM;
		const FVector3d ViewNormalWorld =
			FVector3d(
				Frame.WorldTransform.TransformVectorNoScale(
					FVector(Basis.Normal))).GetSafeNormal();

		auto AddWorldPolyline = [
			&Basis,
			&Frame,
			&WorldCenter,
			&ViewNormalWorld,
			&OutSegments](
				const TArray<FVector>& WorldPoints)
		{
			for (int32 Index = 1; Index < WorldPoints.Num(); ++Index)
			{
				const FVector3d A =
					WorldToFinaleLocal(Frame, WorldPoints[Index - 1]);
				const FVector3d B =
					WorldToFinaleLocal(Frame, WorldPoints[Index]);
				const double VisibilityA =
					FVector3d(WorldPoints[Index - 1] - WorldCenter)
						.Dot(ViewNormalWorld);
				const double VisibilityB =
					FVector3d(WorldPoints[Index] - WorldCenter)
						.Dot(ViewNormalWorld);
				AddGridPolylineSegment(
					Basis,
					A,
					B,
					VisibilityA,
					VisibilityB,
					OutSegments);
			}
		};

		for (int32 LatitudeDegrees = -60;
			LatitudeDegrees <= 60;
			LatitudeDegrees += 30)
		{
			const double Latitude = FMath::DegreesToRadians(
				static_cast<double>(LatitudeDegrees));
			TArray<FVector> Points;
			for (int32 Step = 0; Step <= 48; ++Step)
			{
				const double Longitude =
					2.0 * PI * static_cast<double>(Step) / 48.0;
				Points.Add(WorldCenter + FVector(
					Radius * FMath::Cos(Latitude)
						* FMath::Cos(Longitude),
					Radius * FMath::Cos(Latitude)
						* FMath::Sin(Longitude),
					Radius * FMath::Sin(Latitude)));
			}
			AddWorldPolyline(Points);
		}
		for (int32 LongitudeDegrees = 0;
			LongitudeDegrees < 180;
			LongitudeDegrees += 30)
		{
			const double Longitude = FMath::DegreesToRadians(
				static_cast<double>(LongitudeDegrees));
			TArray<FVector> Points;
			for (int32 Step = 0; Step <= 48; ++Step)
			{
				const double Latitude =
					-0.5 * PI + PI * static_cast<double>(Step) / 48.0;
				Points.Add(WorldCenter + FVector(
					Radius * FMath::Cos(Latitude)
						* FMath::Cos(Longitude),
					Radius * FMath::Cos(Latitude)
						* FMath::Sin(Longitude),
					Radius * FMath::Sin(Latitude)));
			}
			AddWorldPolyline(Points);
		}
	}
}

bool FABTSM11OrbitalDiagramBuilder::Build(
	const FABTSM11FinaleLayoutPreset& Preset,
	const FABTSM110FinaleLocalFrame& FinaleFrame,
	const TConstArrayView<FABTSM11PlaybackPoint> PlaybackPoints,
	const uint64 SourceTrajectoryHash,
	FABTSM11OrbitalDiagramSnapshot& OutSnapshot,
	const int32 MaximumTrajectoryPointCount)
{
	OutSnapshot = FABTSM11OrbitalDiagramSnapshot();
	if (!Preset.IsValid()
		|| !FinaleFrame.IsUsable()
		|| PlaybackPoints.Num() < 2
		|| SourceTrajectoryHash == 0)
	{
		return false;
	}

	FProjectionBasis Basis;
	if (!BuildBasis(PlaybackPoints, FinaleFrame, Basis))
	{
		return false;
	}
	OutSnapshot.PlaneOriginCM =
		Basis.Mean
		+ Basis.X * Basis.ContentCenter.X
		+ Basis.Y * Basis.ContentCenter.Y;
	OutSnapshot.PlaneAxisX = Basis.X;
	OutSnapshot.PlaneAxisY = Basis.Y;
	OutSnapshot.PlaneNormal = Basis.Normal;
	OutSnapshot.FitRadiusCM = Basis.FitRadius;
	OutSnapshot.SourceTrajectoryHash = SourceTrajectoryHash;

	for (int32 BodyIndex = 0;
		BodyIndex < FABTSM11GravityScenario::BodyCount;
		++BodyIndex)
	{
		const FABTSM11GravityBodySpec& SourceBody =
			Preset.CanonicalScenario.Bodies[BodyIndex];
		FABTSM11DiagramBody& Body = OutSnapshot.Bodies[BodyIndex];
		Body.BodyId = SourceBody.BodyId;
		Body.Role = SourceBody.Role;
		Body.Center = NormalizeProjection(Basis, SourceBody.CenterCM);
		Body.VisualRadius =
			SourceBody.VisualRadiusCM / Basis.FitRadius;
		Body.CollisionRadius =
			SourceBody.CollisionRadiusCM / Basis.FitRadius;
		Body.InfluenceRadius =
			SourceBody.InfluenceRadiusCM / Basis.FitRadius;
		Body.Color = SourceBody.DebugColor;
	}
	const FABTSM11TargetSpec& Target =
		Preset.CanonicalScenario.Target;
	OutSnapshot.UFOCenter = NormalizeProjection(
		Basis,
		Target.GetGeometricContactCenterCM());
	OutSnapshot.UFORadius =
		Target.GetGeometricContactRadiusCM() / Basis.FitRadius;
	BuildPrimaryGrid(
		Basis,
		Preset,
		FinaleFrame,
		OutSnapshot.PrimaryGrid);

	const TArray<int32> Indices = BuildDecimatedIndices(
		PlaybackPoints,
		MaximumTrajectoryPointCount);
	for (int32 PairIndex = 1; PairIndex < Indices.Num(); ++PairIndex)
	{
		const FABTSM11PlaybackPoint& A =
			PlaybackPoints[Indices[PairIndex - 1]];
		const FABTSM11PlaybackPoint& B =
			PlaybackPoints[Indices[PairIndex]];
		const FVector2d ProjectedA = ProjectRaw(Basis, A.PositionCM);
		const FVector2d ProjectedB = ProjectRaw(Basis, B.PositionCM);
		TArray<double> Alphas;
		Alphas.Add(0.0);
		Alphas.Add(1.0);
		for (const FABTSM11GravityBodySpec& Body
			: Preset.CanonicalScenario.Bodies)
		{
			AddCircleRoots(
				ProjectedA,
				ProjectedB,
				ProjectRaw(Basis, Body.CenterCM),
				Body.VisualRadiusCM,
				Alphas);
		}
		Alphas.Sort();
		for (int32 AlphaIndex = 1;
			AlphaIndex < Alphas.Num();
			++AlphaIndex)
		{
			const double Alpha0 = Alphas[AlphaIndex - 1];
			const double Alpha1 = Alphas[AlphaIndex];
			if (Alpha1 - Alpha0 <= 1.0e-9)
			{
				continue;
			}
			const FVector3d Start = FMath::Lerp(
				A.PositionCM,
				B.PositionCM,
				Alpha0);
			const FVector3d End = FMath::Lerp(
				A.PositionCM,
				B.PositionCM,
				Alpha1);
			const FVector3d Mid = FMath::Lerp(
				A.PositionCM,
				B.PositionCM,
				(Alpha0 + Alpha1) * 0.5);
			const bool bHidden =
				IsHiddenByAnyBody(Basis, Preset, Mid);
			const EABTSM11PlaybackSegmentKind Kind =
				Alpha1 < 0.5 ? A.SegmentKind : B.SegmentKind;
			FABTSM11DiagramPoint& StartPoint =
				OutSnapshot.Trajectory.AddDefaulted_GetRef();
			StartPoint.Position = NormalizeProjection(Basis, Start);
			StartPoint.bHiddenByBody = bHidden;
			StartPoint.SegmentKind = Kind;
			FABTSM11DiagramPoint& EndPoint =
				OutSnapshot.Trajectory.AddDefaulted_GetRef();
			EndPoint.Position = NormalizeProjection(Basis, End);
			EndPoint.bHiddenByBody = bHidden;
			EndPoint.SegmentKind = Kind;
		}
	}

	OutSnapshot.bValid = OutSnapshot.Trajectory.Num() >= 2;
	return OutSnapshot.bValid;
}
