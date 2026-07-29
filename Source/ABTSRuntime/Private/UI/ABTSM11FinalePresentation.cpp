// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/ABTSM11FinalePresentation.h"

namespace
{
	bool IsFiniteM11PresentationVector(const FVector3d& Vector)
	{
		return FMath::IsFinite(Vector.X)
			&& FMath::IsFinite(Vector.Y)
			&& FMath::IsFinite(Vector.Z);
	}

	bool IsFiniteM11PresentationVector(const FVector2D& Vector)
	{
		return FMath::IsFinite(Vector.X)
			&& FMath::IsFinite(Vector.Y);
	}

	FVector3d ResolveM11TargetCenter(
		const FABTSM11FinaleLayoutPreset& Preset,
		const EABTSM11PreviewTarget Target)
	{
		switch (Target)
		{
		case EABTSM11PreviewTarget::Assist1:
			return Preset.CanonicalScenario.GetAssist(1).CenterCM;
		case EABTSM11PreviewTarget::Assist2:
			return Preset.CanonicalScenario.GetAssist(2).CenterCM;
		case EABTSM11PreviewTarget::Assist3:
			return Preset.CanonicalScenario.GetAssist(3).CenterCM;
		default:
			return Preset.CanonicalScenario.Target
				.GetGeometricContactCenterCM();
		}
	}

	FVector3d ResolveM11PreviousTargetCenter(
		const FABTSM11FinaleLayoutPreset& Preset,
		const EABTSM11PreviewTarget Target)
	{
		switch (Target)
		{
		case EABTSM11PreviewTarget::Assist1:
			return Preset.LaunchModel.PouchLocalPositionCM;
		case EABTSM11PreviewTarget::Assist2:
			return Preset.CanonicalScenario.GetAssist(1).CenterCM;
		case EABTSM11PreviewTarget::Assist3:
			return Preset.CanonicalScenario.GetAssist(2).CenterCM;
		default:
			return Preset.CanonicalScenario.GetAssist(3).CenterCM;
		}
	}

	double ResolveM11TargetVisualRadius(
		const FABTSM11FinaleLayoutPreset& Preset,
		const EABTSM11PreviewTarget Target)
	{
		const int32 AssistIndex = static_cast<int32>(Target) + 1;
		if (AssistIndex >= 1
			&& AssistIndex <= FABTSM11GravityScenario::AssistCount)
		{
			return Preset.CanonicalScenario
				.GetAssist(AssistIndex).VisualRadiusCM;
		}
		return Preset.CanonicalScenario.Target
			.GetGeometricContactRadiusCM();
	}

	double ResolveM11TargetFramingRadius(
		const FABTSM11FinaleLayoutPreset& Preset,
		const EABTSM11PreviewTarget Target,
		const double VisualRadiusCM)
	{
		const int32 AssistIndex = static_cast<int32>(Target) + 1;
		const double ContractRadiusCM =
			AssistIndex >= 1
				&& AssistIndex
					<= FABTSM11GravityScenario::AssistCount
			? Preset.CanonicalScenario.GetAssist(
				AssistIndex).InfluenceRadiusCM
			: FMath::Max(
				Preset.TargetApproachRadiusCM,
				Preset.CanonicalScenario.Target.HitRadiusCM);
		return FMath::Max(
			FMath::Max(1.0, VisualRadiusCM) * 4.0,
			FMath::Max(1.0, ContractRadiusCM) * 1.10);
	}

	bool ProjectM11PipPoint(
		const FABTSM11TargetPipView& View,
		const FVector3d& PositionCM,
		FVector2D& OutUV)
	{
		const FVector3d Relative = PositionCM - View.CameraLocationCM;
		const double Depth = FVector3d::DotProduct(
			Relative,
			View.Forward);
		if (!FMath::IsFinite(Depth) || Depth <= 1.0e-6)
		{
			return false;
		}
		const double TanHalfHorizontal = FMath::Tan(
			FMath::DegreesToRadians(View.HorizontalFOVDegrees * 0.5));
		const double TanHalfVertical =
			TanHalfHorizontal / View.AspectRatio;
		if (TanHalfHorizontal <= 1.0e-9
			|| TanHalfVertical <= 1.0e-9)
		{
			return false;
		}
		const double NdcX = FVector3d::DotProduct(
			Relative,
			View.Right) / (Depth * TanHalfHorizontal);
		const double NdcY = FVector3d::DotProduct(
			Relative,
			View.Up) / (Depth * TanHalfVertical);
		OutUV = FVector2D(
			static_cast<float>(0.5 + NdcX * 0.5),
			static_cast<float>(0.5 - NdcY * 0.5));
		return IsFiniteM11PresentationVector(OutUV);
	}

	float DistanceToM11ViewportEdge(
		const FVector2D& Point,
		const FVector2D& ViewportSize)
	{
		return FMath::Min(
			FMath::Min(Point.X, ViewportSize.X - Point.X),
			FMath::Min(Point.Y, ViewportSize.Y - Point.Y));
	}

	FVector2D ResolveM11WedgeDirection(
		const FVector2D& RawPosition,
		const FVector2D& ViewportSize)
	{
		FVector2D Direction =
			RawPosition - ViewportSize * 0.5f;
		if (!Direction.Normalize())
		{
			Direction = FVector2D(0.0f, 1.0f);
		}
		return Direction;
	}

	FVector2D ResolveM11WedgeAnchor(
		const FVector2D& Direction,
		const FVector2D& ViewportSize,
		const float Margin)
	{
		const FVector2D Center = ViewportSize * 0.5f;
		const FVector2D HalfExtent(
			FMath::Max(1.0f, Center.X - Margin),
			FMath::Max(1.0f, Center.Y - Margin));
		const float XScale = FMath::Abs(Direction.X) > KINDA_SMALL_NUMBER
			? HalfExtent.X / FMath::Abs(Direction.X)
			: TNumericLimits<float>::Max();
		const float YScale = FMath::Abs(Direction.Y) > KINDA_SMALL_NUMBER
			? HalfExtent.Y / FMath::Abs(Direction.Y)
			: TNumericLimits<float>::Max();
		return Center + Direction * FMath::Min(XScale, YScale);
	}
}

bool ABTSM11BuildTargetPipView(
	const FABTSM11FinaleLayoutPreset& Preset,
	const FABTSM11PreviewSelection& Selection,
	const int32 RenderWidth,
	const int32 RenderHeight,
	FABTSM11TargetPipView& OutView)
{
	OutView = FABTSM11TargetPipView();
	if (RenderWidth <= 0
		|| RenderHeight <= 0)
	{
		return false;
	}
	const FVector3d TargetCenter =
		ResolveM11TargetCenter(Preset, Selection.Target);
	const FVector3d PreviousCenter =
		ResolveM11PreviousTargetCenter(Preset, Selection.Target);
	FVector3d Forward = (TargetCenter - PreviousCenter).GetSafeNormal();
	if (!IsFiniteM11PresentationVector(TargetCenter)
		|| !IsFiniteM11PresentationVector(PreviousCenter)
		|| Forward.IsNearlyZero())
	{
		return false;
	}

	const FVector3d ConstantUp = FVector3d::UpVector;
	FVector3d Right = FVector3d::CrossProduct(
		ConstantUp,
		Forward).GetSafeNormal();
	if (Right.IsNearlyZero())
	{
		Right = FVector3d::CrossProduct(
			FVector3d::RightVector,
			Forward).GetSafeNormal();
	}
	if (Right.IsNearlyZero())
	{
		return false;
	}
	const FVector3d Up = FVector3d::CrossProduct(
		Forward,
		Right).GetSafeNormal();
	const double VisualRadiusCM = FMath::Max(
		1.0,
		ResolveM11TargetVisualRadius(Preset, Selection.Target));
	const double FramingRadiusCM = ResolveM11TargetFramingRadius(
		Preset,
		Selection.Target,
		VisualRadiusCM);
	const double AspectRatio =
		static_cast<double>(RenderWidth)
		/ static_cast<double>(RenderHeight);
	const double TanHalfVertical = FMath::Tan(
		FMath::DegreesToRadians(
			ABTSM11FinaleTargetPreviewFOVDegrees * 0.5))
		/ AspectRatio;
	if (!FMath::IsFinite(TanHalfVertical)
		|| TanHalfVertical <= 1.0e-9)
	{
		return false;
	}
	const double CameraDistanceCM = FMath::Max(
		2500.0,
		FramingRadiusCM / (TanHalfVertical * 0.80));

	OutView.TargetCenterCM = TargetCenter;
	OutView.PreviousTargetCenterCM = PreviousCenter;
	OutView.CameraLocationCM =
		TargetCenter - Forward * CameraDistanceCM;
	OutView.Forward = Forward;
	OutView.Right = Right;
	OutView.Up = Up;
	OutView.HorizontalFOVDegrees =
		ABTSM11FinaleTargetPreviewFOVDegrees;
	OutView.AspectRatio = AspectRatio;
	OutView.FramingRadiusCM = FramingRadiusCM;
	OutView.CameraDistanceCM = CameraDistanceCM;
	OutView.bValid =
		IsFiniteM11PresentationVector(OutView.CameraLocationCM)
		&& IsFiniteM11PresentationVector(OutView.Up)
		&& FMath::IsFinite(OutView.CameraDistanceCM);
	return OutView.bValid;
}

bool ABTSM11BuildTargetPipTrajectory(
	const FABTSM11TargetPipView& View,
	const FABTSM11PreviewSelection& Selection,
	const FABTSM11TrajectoryResult& CurrentPrediction,
	FABTSM11TargetPipTrajectory& OutTrajectory,
	const int32 MaximumPointCount)
{
	OutTrajectory.Reset();
	if (!View.bValid
		|| CurrentPrediction.ValidationHash == 0
		|| CurrentPrediction.Points.IsEmpty()
		|| MaximumPointCount < 3)
	{
		return false;
	}

	int32 ClosestIndex = INDEX_NONE;
	double ClosestDistanceSquared = TNumericLimits<double>::Max();
	for (int32 Index = 0;
		Index < CurrentPrediction.Points.Num();
		++Index)
	{
		const double DistanceSquared =
			(CurrentPrediction.Points[Index].PositionCM
				- View.TargetCenterCM).SquaredLength();
		if (FMath::IsFinite(DistanceSquared)
			&& DistanceSquared < ClosestDistanceSquared)
		{
			ClosestDistanceSquared = DistanceSquared;
			ClosestIndex = Index;
		}
	}
	if (ClosestIndex == INDEX_NONE)
	{
		return false;
	}

	const double WindowArcLengthCM =
		FMath::Max(1000.0, View.FramingRadiusCM * 1.30);
	int32 StartIndex = ClosestIndex;
	double ArcLengthCM = 0.0;
	while (StartIndex > 0 && ArcLengthCM < WindowArcLengthCM)
	{
		ArcLengthCM += (
			CurrentPrediction.Points[StartIndex].PositionCM
			- CurrentPrediction.Points[StartIndex - 1].PositionCM)
				.Length();
		--StartIndex;
	}
	int32 EndIndex = ClosestIndex;
	ArcLengthCM = 0.0;
	while (EndIndex + 1 < CurrentPrediction.Points.Num()
		&& ArcLengthCM < WindowArcLengthCM)
	{
		ArcLengthCM += (
			CurrentPrediction.Points[EndIndex + 1].PositionCM
			- CurrentPrediction.Points[EndIndex].PositionCM)
				.Length();
		++EndIndex;
	}

	const int32 RangeCount = EndIndex - StartIndex + 1;
	const int32 OutputCount = FMath::Min(
		MaximumPointCount,
		RangeCount);
	TArray<int32> SourceIndices;
	SourceIndices.Reserve(OutputCount + 1);
	if (OutputCount == 1)
	{
		SourceIndices.Add(ClosestIndex);
	}
	else
	{
		for (int32 Slot = 0; Slot < OutputCount; ++Slot)
		{
			const double Alpha = static_cast<double>(Slot)
				/ static_cast<double>(OutputCount - 1);
			SourceIndices.Add(FMath::RoundToInt(
				FMath::Lerp(
					static_cast<double>(StartIndex),
					static_cast<double>(EndIndex),
					Alpha)));
		}
	}
	if (!SourceIndices.Contains(ClosestIndex))
	{
		int32 Replacement = 0;
		int32 BestDistance = TNumericLimits<int32>::Max();
		for (int32 Index = 1;
			Index + 1 < SourceIndices.Num();
			++Index)
		{
			const int32 Distance =
				FMath::Abs(SourceIndices[Index] - ClosestIndex);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Replacement = Index;
			}
		}
		SourceIndices[Replacement] = ClosestIndex;
		SourceIndices.Sort();
	}

	OutTrajectory.Points.Reserve(SourceIndices.Num());
	for (const int32 SourceIndex : SourceIndices)
	{
		FABTSM11TargetPipTrajectoryPoint& Projected =
			OutTrajectory.Points.AddDefaulted_GetRef();
		Projected.bInFront = ProjectM11PipPoint(
			View,
			CurrentPrediction.Points[SourceIndex].PositionCM,
			Projected.UV);
		Projected.bClosestApproach =
			SourceIndex == ClosestIndex;
	}
	OutTrajectory.SourceTrajectoryHash =
		CurrentPrediction.ValidationHash;
	OutTrajectory.Target = Selection.Target;
	OutTrajectory.ClosestSourcePointIndex = ClosestIndex;
	OutTrajectory.bValid = OutTrajectory.Points.Num() >= 2;
	return OutTrajectory.bValid;
}

bool ABTSM11ClipPipLineToRect(
	FVector2D& InOutStart,
	FVector2D& InOutEnd,
	const float Inset)
{
	const float SafeInset = FMath::Clamp(Inset, 0.0f, 0.49f);
	const FVector2D Minimum(SafeInset, SafeInset);
	const FVector2D Maximum(1.0f - SafeInset, 1.0f - SafeInset);
	const FVector2D Delta = InOutEnd - InOutStart;
	float TMinimum = 0.0f;
	float TMaximum = 1.0f;
	const auto ClipAxis =
		[&TMinimum, &TMaximum](
			const float Origin,
			const float Direction,
			const float AxisMinimum,
			const float AxisMaximum)
	{
		if (FMath::Abs(Direction) <= SMALL_NUMBER)
		{
			return Origin >= AxisMinimum && Origin <= AxisMaximum;
		}
		float T0 = (AxisMinimum - Origin) / Direction;
		float T1 = (AxisMaximum - Origin) / Direction;
		if (T0 > T1)
		{
			Swap(T0, T1);
		}
		TMinimum = FMath::Max(TMinimum, T0);
		TMaximum = FMath::Min(TMaximum, T1);
		return TMinimum <= TMaximum;
	};
	if (!ClipAxis(InOutStart.X, Delta.X, Minimum.X, Maximum.X)
		|| !ClipAxis(InOutStart.Y, Delta.Y, Minimum.Y, Maximum.Y))
	{
		return false;
	}
	const FVector2D OriginalStart = InOutStart;
	InOutStart = OriginalStart + Delta * TMinimum;
	InOutEnd = OriginalStart + Delta * TMaximum;
	return IsFiniteM11PresentationVector(InOutStart)
		&& IsFiniteM11PresentationVector(InOutEnd);
}

bool FABTSM11TargetWedgeConfig::IsValid() const
{
	return FMath::IsFinite(AnchorMarginPixels)
		&& AnchorMarginPixels >= 0.0f
		&& FMath::IsFinite(ShowEdgeDistancePixels)
		&& ShowEdgeDistancePixels >= AnchorMarginPixels
		&& FMath::IsFinite(HideEdgeDistancePixels)
		&& HideEdgeDistancePixels > ShowEdgeDistancePixels
		&& FMath::IsFinite(ShowHoldSeconds)
		&& ShowHoldSeconds >= 0.0
		&& FMath::IsFinite(HideHoldSeconds)
		&& HideHoldSeconds >= 0.0;
}

FABTSM11TargetWedgeProjection ABTSM11ProjectTargetForWedge(
	const FVector3d& TargetWorldPosition,
	const FVector3d& CameraWorldPosition,
	const FVector3d& CameraForward,
	const FVector3d& CameraRight,
	const FVector3d& CameraUp,
	const double HorizontalFOVDegrees,
	const FVector2D& ViewportSize)
{
	FABTSM11TargetWedgeProjection Projection;
	if (!IsFiniteM11PresentationVector(TargetWorldPosition)
		|| !IsFiniteM11PresentationVector(CameraWorldPosition)
		|| !IsFiniteM11PresentationVector(CameraForward)
		|| !IsFiniteM11PresentationVector(CameraRight)
		|| !IsFiniteM11PresentationVector(CameraUp)
		|| !IsFiniteM11PresentationVector(ViewportSize)
		|| ViewportSize.X <= 1.0f
		|| ViewportSize.Y <= 1.0f
		|| !FMath::IsFinite(HorizontalFOVDegrees)
		|| HorizontalFOVDegrees <= 1.0
		|| HorizontalFOVDegrees >= 179.0)
	{
		return Projection;
	}
	const FVector3d Forward = CameraForward.GetSafeNormal();
	const FVector3d Right = CameraRight.GetSafeNormal();
	const FVector3d Up = CameraUp.GetSafeNormal();
	if (Forward.IsNearlyZero()
		|| Right.IsNearlyZero()
		|| Up.IsNearlyZero())
	{
		return Projection;
	}

	const FVector3d Relative =
		TargetWorldPosition - CameraWorldPosition;
	const double Depth = FVector3d::DotProduct(Relative, Forward);
	Projection.bInFront = Depth > 1.0e-6;
	const FVector2D Center = ViewportSize * 0.5f;
	if (Projection.bInFront)
	{
		const double AspectRatio =
			static_cast<double>(ViewportSize.X)
			/ static_cast<double>(ViewportSize.Y);
		const double TanHalfHorizontal = FMath::Tan(
			FMath::DegreesToRadians(HorizontalFOVDegrees * 0.5));
		const double TanHalfVertical =
			TanHalfHorizontal / AspectRatio;
		const double NdcX =
			FVector3d::DotProduct(Relative, Right)
			/ (Depth * TanHalfHorizontal);
		const double NdcY =
			FVector3d::DotProduct(Relative, Up)
			/ (Depth * TanHalfVertical);
		Projection.RawScreenPosition = FVector2D(
			static_cast<float>(Center.X + NdcX * Center.X),
			static_cast<float>(Center.Y - NdcY * Center.Y));
	}
	else
	{
		FVector2D Direction(
			static_cast<float>(
				FVector3d::DotProduct(Relative, Right)),
			static_cast<float>(
				-FVector3d::DotProduct(Relative, Up)));
		if (!Direction.Normalize())
		{
			Direction = FVector2D(0.0f, 1.0f);
		}
		Projection.RawScreenPosition =
			Center + Direction * ViewportSize.GetMax() * 2.0f;
	}
	Projection.bFinite = IsFiniteM11PresentationVector(
		Projection.RawScreenPosition);
	return Projection;
}

void FABTSM11TargetWedgeTracker::Reset()
{
	*this = FABTSM11TargetWedgeTracker();
}

FABTSM11TargetWedgeOutput FABTSM11TargetWedgeTracker::Update(
	const double DeltaSeconds,
	const EABTSM11PreviewTarget Target,
	const FABTSM11TargetWedgeProjection& Projection,
	const FVector2D& ViewportSize,
	const FABTSM11TargetWedgeConfig& Config)
{
	FABTSM11TargetWedgeOutput Output;
	Output.Target = Target;
	if (!Projection.bFinite
		|| !IsFiniteM11PresentationVector(ViewportSize)
		|| ViewportSize.X <= 1.0f
		|| ViewportSize.Y <= 1.0f
		|| !Config.IsValid())
	{
		Reset();
		return Output;
	}

	const float EdgeDistance = Projection.bInFront
		? DistanceToM11ViewportEdge(
			Projection.RawScreenPosition,
			ViewportSize)
		: -TNumericLimits<float>::Max();
	const bool bComfortablyVisible =
		Projection.bInFront
		&& EdgeDistance >= Config.HideEdgeDistancePixels;
	const bool bOutsideSafeView =
		!Projection.bInFront
		|| EdgeDistance <= Config.ShowEdgeDistancePixels;
	if (!bInitialized || Target != LatchedTarget)
	{
		LatchedTarget = Target;
		bVisible = !bComfortablyVisible;
		bPendingVisible = bVisible;
		PendingSeconds = 0.0;
		bInitialized = true;
	}
	else
	{
		bool bDesiredVisible = bVisible;
		if (bVisible && bComfortablyVisible)
		{
			bDesiredVisible = false;
		}
		else if (!bVisible && bOutsideSafeView)
		{
			bDesiredVisible = true;
		}
		if (bDesiredVisible == bVisible)
		{
			PendingSeconds = 0.0;
			bPendingVisible = bVisible;
		}
		else
		{
			if (bPendingVisible != bDesiredVisible)
			{
				bPendingVisible = bDesiredVisible;
				PendingSeconds = 0.0;
			}
			PendingSeconds += FMath::Max(0.0, DeltaSeconds);
			const double RequiredHold = bDesiredVisible
				? Config.ShowHoldSeconds
				: Config.HideHoldSeconds;
			if (PendingSeconds >= RequiredHold)
			{
				bVisible = bDesiredVisible;
				PendingSeconds = 0.0;
			}
		}
	}

	Output.Direction = ResolveM11WedgeDirection(
		Projection.RawScreenPosition,
		ViewportSize);
	Output.Anchor = ResolveM11WedgeAnchor(
		Output.Direction,
		ViewportSize,
		Config.AnchorMarginPixels);
	Output.bVisible = bVisible;
	return Output;
}
