// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/ABTSM11FinaleHUDData.h"

#include "World/ABTSM11FinaleLayoutCertification.h"
#include "World/ABTSM11GravityAssistSolver.h"

namespace
{
	bool IsFiniteFinaleHudVector(const FVector3d& Value)
	{
		return FMath::IsFinite(Value.X)
			&& FMath::IsFinite(Value.Y)
			&& FMath::IsFinite(Value.Z);
	}

	double AxisMinimum(
		const FABTSM11FinaleLaunchModel& Model,
		const EABTSM11FinaleControlAxis Axis)
	{
		switch (Axis)
		{
		case EABTSM11FinaleControlAxis::Yaw:
			return Model.MinimumYawDegrees;
		case EABTSM11FinaleControlAxis::Pitch:
			return Model.MinimumPitchDegrees;
		case EABTSM11FinaleControlAxis::Power:
			return Model.MinimumPower;
		default:
			return 0.0;
		}
	}

	double AxisMaximum(
		const FABTSM11FinaleLaunchModel& Model,
		const EABTSM11FinaleControlAxis Axis)
	{
		switch (Axis)
		{
		case EABTSM11FinaleControlAxis::Yaw:
			return Model.MaximumYawDegrees;
		case EABTSM11FinaleControlAxis::Pitch:
			return Model.MaximumPitchDegrees;
		case EABTSM11FinaleControlAxis::Power:
			return Model.MaximumPower;
		default:
			return 0.0;
		}
	}

	double GetAxisValue(
		const FABTSM11FinaleLaunchInput& Input,
		const EABTSM11FinaleControlAxis Axis)
	{
		switch (Axis)
		{
		case EABTSM11FinaleControlAxis::Yaw:
			return Input.YawDegrees;
		case EABTSM11FinaleControlAxis::Pitch:
			return Input.PitchDegrees;
		case EABTSM11FinaleControlAxis::Power:
			return Input.Power;
		default:
			return 0.0;
		}
	}

	void SetAxisValue(
		FABTSM11FinaleLaunchInput& Input,
		const EABTSM11FinaleControlAxis Axis,
		const double Value)
	{
		switch (Axis)
		{
		case EABTSM11FinaleControlAxis::Yaw:
			Input.YawDegrees = Value;
			break;
		case EABTSM11FinaleControlAxis::Pitch:
			Input.PitchDegrees = Value;
			break;
		case EABTSM11FinaleControlAxis::Power:
			Input.Power = Value;
			break;
		default:
			break;
		}
	}

	bool RejectF4Guidance(FString* OutFailure, const FString& Reason)
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = Reason;
		}
		return false;
	}

	struct FF4GuidanceSample
	{
		FIntVector Offset = FIntVector::ZeroValue;
		FABTSM11FinaleLaunchInput Input;
		bool bF4 = false;
	};

	void VisitF4GuidanceNeighbors(
		const FIntVector& Center,
		TFunctionRef<void(const FIntVector&)> Visitor)
	{
		for (int32 YawOffset = -1; YawOffset <= 1; ++YawOffset)
		{
			for (int32 PitchOffset = -1; PitchOffset <= 1; ++PitchOffset)
			{
				for (int32 PowerOffset = -1; PowerOffset <= 1; ++PowerOffset)
				{
					if (YawOffset == 0
						&& PitchOffset == 0
						&& PowerOffset == 0)
					{
						continue;
					}
					Visitor(Center + FIntVector(
						YawOffset,
						PitchOffset,
						PowerOffset));
				}
			}
		}
	}

	EABTSM11TrajectorySemanticLeg CoastLegForAssist(
		const int32 AssistIndex)
	{
		switch (AssistIndex)
		{
		case 1: return EABTSM11TrajectorySemanticLeg::LaunchToAssist1;
		case 2: return EABTSM11TrajectorySemanticLeg::Assist1ToAssist2;
		case 3: return EABTSM11TrajectorySemanticLeg::Assist2ToAssist3;
		default: return EABTSM11TrajectorySemanticLeg::Invalid;
		}
	}

	EABTSM11TrajectorySemanticLeg EncounterLegForAssist(
		const int32 AssistIndex)
	{
		switch (AssistIndex)
		{
		case 1: return EABTSM11TrajectorySemanticLeg::Assist1Encounter;
		case 2: return EABTSM11TrajectorySemanticLeg::Assist2Encounter;
		case 3: return EABTSM11TrajectorySemanticLeg::Assist3Encounter;
		default: return EABTSM11TrajectorySemanticLeg::Invalid;
		}
	}

	bool IsEncounterLeg(const EABTSM11TrajectorySemanticLeg Leg)
	{
		return Leg == EABTSM11TrajectorySemanticLeg::Assist1Encounter
			|| Leg == EABTSM11TrajectorySemanticLeg::Assist2Encounter
			|| Leg == EABTSM11TrajectorySemanticLeg::Assist3Encounter;
	}

	int32 FindNearestPointIndex(
		const TConstArrayView<FABTSM11OrbitalScenePoint> Points,
		const double TimeSeconds)
	{
		if (Points.IsEmpty())
		{
			return INDEX_NONE;
		}
		int32 BestIndex = 0;
		double BestDifference = FMath::Abs(Points[0].TimeSeconds - TimeSeconds);
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			const double Difference =
				FMath::Abs(Points[Index].TimeSeconds - TimeSeconds);
			if (Difference < BestDifference)
			{
				BestDifference = Difference;
				BestIndex = Index;
			}
		}
		return BestIndex;
	}

	int32 FindClosestPointIndex(
		const TConstArrayView<FABTSM11OrbitalScenePoint> Points,
		const FVector3d& Center,
		const int32 MinimumIndex,
		const int32 MaximumIndex)
	{
		if (Points.IsEmpty())
		{
			return INDEX_NONE;
		}
		const int32 Start = FMath::Clamp(MinimumIndex, 0, Points.Num() - 1);
		const int32 End = FMath::Clamp(MaximumIndex, Start, Points.Num() - 1);
		int32 BestIndex = Start;
		double BestDistanceSquared =
			(Points[Start].PositionCM - Center).SquaredLength();
		for (int32 Index = Start + 1; Index <= End; ++Index)
		{
			const double DistanceSquared =
				(Points[Index].PositionCM - Center).SquaredLength();
			if (DistanceSquared < BestDistanceSquared)
			{
				BestDistanceSquared = DistanceSquared;
				BestIndex = Index;
			}
		}
		return BestIndex;
	}

	bool FindArcBracket(
		const TConstArrayView<FABTSM11OrbitalScenePoint> Points,
		const int32 StartIndex,
		const int32 EndIndex,
		const double TargetArcLength,
		int32& OutPointA,
		int32& OutPointB,
		double& OutAlpha)
	{
		if (Points.IsEmpty()
			|| StartIndex < 0
			|| EndIndex >= Points.Num()
			|| StartIndex > EndIndex)
		{
			return false;
		}
		if (StartIndex == EndIndex)
		{
			OutPointA = StartIndex;
			OutPointB = StartIndex;
			OutAlpha = 0.0;
			return true;
		}
		for (int32 Index = StartIndex + 1; Index <= EndIndex; ++Index)
		{
			if (Points[Index].ArcLengthCM + UE_DOUBLE_SMALL_NUMBER
				< TargetArcLength)
			{
				continue;
			}
			OutPointA = Index - 1;
			OutPointB = Index;
			const double Span = Points[Index].ArcLengthCM
				- Points[Index - 1].ArcLengthCM;
			OutAlpha = Span > UE_DOUBLE_SMALL_NUMBER
				? FMath::Clamp(
					(TargetArcLength - Points[Index - 1].ArcLengthCM) / Span,
					0.0,
					1.0)
				: 0.0;
			return true;
		}
		OutPointA = EndIndex;
		OutPointB = EndIndex;
		OutAlpha = 0.0;
		return true;
	}

	bool SampleScene(
		const FABTSM11OrbitalSceneSnapshot& Scene,
		const EABTSM11TrajectorySemanticLeg Leg,
		const double Phase,
		FVector3d& OutPosition,
		FVector3d& OutVelocity,
		double& OutTime)
	{
		int32 PointA = INDEX_NONE;
		int32 PointB = INDEX_NONE;
		double Alpha = 0.0;
		if (!Scene.SemanticMap.ResolvePoint(
				Leg,
				Phase,
				Scene.Trajectory,
				PointA,
				PointB,
				Alpha))
		{
			return false;
		}
		const FABTSM11OrbitalScenePoint& A = Scene.Trajectory[PointA];
		const FABTSM11OrbitalScenePoint& B = Scene.Trajectory[PointB];
		OutPosition = FMath::Lerp(A.PositionCM, B.PositionCM, Alpha);
		OutVelocity = FMath::Lerp(A.VelocityCMPerSec, B.VelocityCMPerSec, Alpha);
		OutTime = FMath::Lerp(A.TimeSeconds, B.TimeSeconds, Alpha);
		return IsFiniteFinaleHudVector(OutPosition)
			&& IsFiniteFinaleHudVector(OutVelocity)
			&& FMath::IsFinite(OutTime);
	}

	const FABTSM11TrajectorySemanticSegment* FindSegmentAtTime(
		const FABTSM11TrajectorySemanticMap& Map,
		const TConstArrayView<FABTSM11OrbitalScenePoint> Points,
		const double TimeSeconds)
	{
		const FABTSM11TrajectorySemanticSegment* Best = nullptr;
		for (const FABTSM11TrajectorySemanticSegment& Segment : Map.Segments)
		{
			if (!Segment.IsValid(Points.Num()))
			{
				continue;
			}
			const double Start = Points[Segment.StartPointIndex].TimeSeconds;
			const double End = Points[Segment.EndPointIndex].TimeSeconds;
			if (TimeSeconds + 1.0e-9 < Start || TimeSeconds - 1.0e-9 > End)
			{
				continue;
			}
			if (Best == nullptr || Segment.bEncounter)
			{
				Best = &Segment;
			}
		}
		return Best;
	}

	bool IsHiddenByBody(
		const FABTSM11OrbitalSceneSnapshot& Scene,
		const FABTSM11OverviewViewState& View,
		const FVector3d& Position)
	{
		const FVector2d Projected = View.Project(Position);
		const double Depth = View.ProjectDepth(Position);
		for (const FABTSM11OrbitalSceneBody& Body : Scene.Bodies)
		{
			const FVector2d BodyProjected = View.Project(Body.CenterCM);
			const double Radius = Body.VisualRadiusCM
				* View.Zoom / View.ProjectionScaleCM;
			const double RadialSquared =
				(Projected - BodyProjected).SquaredLength();
			if (RadialSquared >= Radius * Radius)
			{
				continue;
			}
			const double FrontDepth = View.ProjectDepth(Body.CenterCM)
				+ FMath::Sqrt(
					FMath::Max(0.0, FMath::Square(Body.VisualRadiusCM)
						- RadialSquared
							* FMath::Square(View.ProjectionScaleCM / View.Zoom)));
			if (Depth < FrontDepth)
			{
				return true;
			}
		}
		return false;
	}

	double PointSegmentDistanceSquared(
		const FVector2d& Point,
		const FVector2d& Start,
		const FVector2d& End,
		double& OutAlpha)
	{
		const FVector2d Delta = End - Start;
		const double LengthSquared = Delta.SquaredLength();
		OutAlpha = LengthSquared > UE_DOUBLE_SMALL_NUMBER
			? FMath::Clamp((Point - Start).Dot(Delta) / LengthSquared, 0.0, 1.0)
			: 0.0;
		return (Point - FMath::Lerp(Start, End, OutAlpha)).SquaredLength();
	}

	bool BuildFrozenView(
		const FABTSM11OrbitalSceneSnapshot& Scene,
		const EABTSM11TrajectorySemanticLeg Leg,
		const double Phase,
		const int32 ContextBodyIndex,
		const bool bContextIsTarget,
		const FVector3d& FinaleLocalUp,
		const FVector3d& PreferredViewForward,
		FABTSM11FrozenPipView& OutView,
		FVector3d& OutPosition,
		FVector3d& OutVelocity,
		double& OutTime)
	{
		OutView = FABTSM11FrozenPipView();
		if (!SampleScene(Scene, Leg, Phase, OutPosition, OutVelocity, OutTime))
		{
			return false;
		}
		FVector3d ContextCenter;
		FVector3d ContextVelocity;
		double VisualRadius = 0.0;
		double InfluenceRadius = 0.0;
		if (!Scene.GetContextGeometry(
				ContextBodyIndex,
				bContextIsTarget,
				ContextCenter,
				ContextVelocity,
				VisualRadius,
				InfluenceRadius))
		{
			return false;
		}

		const FVector3d Radial = (OutPosition - ContextCenter).GetSafeNormal();
		const FVector3d RelativeVelocity = OutVelocity - ContextVelocity;
		FVector3d Forward = Radial.Cross(RelativeVelocity).GetSafeNormal();
		const FVector3d Preferred = PreferredViewForward.GetSafeNormal();
		if (Forward.IsNearlyZero())
		{
			Forward = Preferred;
		}
		if (Forward.IsNearlyZero())
		{
			Forward = FinaleLocalUp.GetSafeNormal();
		}
		if (Forward.IsNearlyZero())
		{
			return false;
		}
		if (!Preferred.IsNearlyZero() && Forward.Dot(Preferred) < 0.0)
		{
			Forward *= -1.0;
		}
		FVector3d Up = FinaleLocalUp
			- Forward * FinaleLocalUp.Dot(Forward);
		Up.Normalize();
		if (Up.IsNearlyZero())
		{
			Up = RelativeVelocity
				- Forward * RelativeVelocity.Dot(Forward);
			Up.Normalize();
		}
		if (Up.IsNearlyZero())
		{
			Up = FVector3d::UpVector
				- Forward * FVector3d::UpVector.Dot(Forward);
			Up.Normalize();
		}
		if (Up.IsNearlyZero())
		{
			return false;
		}
		const FVector3d Right = Up.Cross(Forward).GetSafeNormal();
		if (Right.IsNearlyZero())
		{
			return false;
		}

		OutView.ContextCenterCM = ContextCenter;
		OutView.ViewCenterCM = (ContextCenter + OutPosition) * 0.5;
		OutView.ViewForward = Forward;
		OutView.ViewUp = Up;
		OutView.ViewRight = Right;
		OutView.HalfExtentCM = FMath::Max3(
			1000.0,
			VisualRadius * 2.25,
			(OutPosition - ContextCenter).Length() * 0.65 + VisualRadius);
		OutView.bValid = FMath::IsFinite(OutView.HalfExtentCM)
			&& OutView.HalfExtentCM > 0.0;
		return OutView.bValid;
	}
}

bool FABTSM11F4GuidanceSearchConfig::IsValid() const
{
	return FMath::IsFinite(MinimumYawStepDegrees)
		&& MinimumYawStepDegrees > 0.0
		&& FMath::IsFinite(MinimumPitchStepDegrees)
		&& MinimumPitchStepDegrees > 0.0
		&& FMath::IsFinite(MinimumPowerStep)
		&& MinimumPowerStep > 0.0
		&& MaximumSampleCount >= 27;
}

bool FABTSM11F4GuidanceTarget::IsValid(
	const FABTSM11FinaleLaunchModel& LaunchModel) const
{
	return bValid
		&& Input.IsFinite()
		&& LaunchModel.Contains(Input)
		&& FMath::IsFinite(YawToleranceDegrees)
		&& YawToleranceDegrees > 0.0
		&& FMath::IsFinite(PitchToleranceDegrees)
		&& PitchToleranceDegrees > 0.0
		&& FMath::IsFinite(PowerTolerance)
		&& PowerTolerance > 0.0
		&& SampleCount > 0
		&& F4SampleCount > 0
		&& F4SampleCount <= SampleCount;
}

EABTSM11F4GuidanceDirection FABTSM11F4GuidanceTarget::GetDirection(
	const FABTSM11FinaleLaunchInput& Current,
	const EABTSM11FinaleControlAxis Axis) const
{
	double Tolerance = 0.0;
	switch (Axis)
	{
	case EABTSM11FinaleControlAxis::Yaw:
		Tolerance = YawToleranceDegrees;
		break;
	case EABTSM11FinaleControlAxis::Pitch:
		Tolerance = PitchToleranceDegrees;
		break;
	case EABTSM11FinaleControlAxis::Power:
		Tolerance = PowerTolerance;
		break;
	default:
		return EABTSM11F4GuidanceDirection::Aligned;
	}
	const double Delta = GetAxisValue(Input, Axis)
		- GetAxisValue(Current, Axis);
	if (!bValid || !FMath::IsFinite(Delta) || FMath::Abs(Delta) <= Tolerance)
	{
		return EABTSM11F4GuidanceDirection::Aligned;
	}
	return Delta > 0.0
		? EABTSM11F4GuidanceDirection::Increase
		: EABTSM11F4GuidanceDirection::Decrease;
}

bool FABTSM11F4GuidanceBuilder::Build(
	const FABTSM11FinaleLayoutPreset& Preset,
	FABTSM11F4GuidanceTarget& OutTarget,
	FString* OutFailure,
	const FABTSM11F4GuidanceSearchConfig& Config)
{
	OutTarget = FABTSM11F4GuidanceTarget();
	if (!Config.IsValid())
	{
		return RejectF4Guidance(OutFailure, TEXT("InvalidSearchConfig"));
	}
	FString PresetFailure;
	if (!Preset.IsValid(&PresetFailure))
	{
		return RejectF4Guidance(
			OutFailure,
			FString::Printf(TEXT("InvalidPreset:%s"), *PresetFailure));
	}
	const double YawStep = FMath::Max(
		Preset.ScanContract.FinalYawPrecisionDegrees,
		Config.MinimumYawStepDegrees);
	const double PitchStep = FMath::Max(
		Preset.ScanContract.FinalPitchPrecisionDegrees,
		Config.MinimumPitchStepDegrees);
	const double PowerStep = FMath::Max(
		Preset.ScanContract.FinalPowerPrecision,
		Config.MinimumPowerStep);
	if (!FMath::IsFinite(YawStep) || YawStep <= 0.0
		|| !FMath::IsFinite(PitchStep) || PitchStep <= 0.0
		|| !FMath::IsFinite(PowerStep) || PowerStep <= 0.0)
	{
		return RejectF4Guidance(OutFailure, TEXT("InvalidResolvedStep"));
	}

	TArray<FIntVector> Open;
	TSet<FIntVector> Enqueued;
	TMap<FIntVector, int32> SampleByOffset;
	TArray<FF4GuidanceSample> Samples;
	TArray<int32> F4Indices;
	Open.Reserve(Config.MaximumSampleCount);
	Samples.Reserve(Config.MaximumSampleCount);
	Enqueued.Reserve(Config.MaximumSampleCount);
	SampleByOffset.Reserve(Config.MaximumSampleCount);
	Open.Add(FIntVector::ZeroValue);
	Enqueued.Add(FIntVector::ZeroValue);

	int32 OpenIndex = 0;
	bool bF4SeedFound = false;
	while (OpenIndex < Open.Num()
		&& Samples.Num() < Config.MaximumSampleCount)
	{
		const FIntVector Offset = Open[OpenIndex++];
		FABTSM11FinaleLaunchInput Input = Preset.NominalInput;
		Input.YawDegrees += static_cast<double>(Offset.X) * YawStep;
		Input.PitchDegrees += static_cast<double>(Offset.Y) * PitchStep;
		Input.Power += static_cast<double>(Offset.Z) * PowerStep;
		if (!Preset.LaunchModel.Contains(Input))
		{
			continue;
		}

		FF4GuidanceSample Sample;
		Sample.Offset = Offset;
		Sample.Input = Input;
		FABTSM11TrajectoryRequest Request;
		FABTSM11TrajectoryResult Result;
		FString SampleFailure;
		const bool bSolved = Preset.BuildRequest(
			Input,
			0x7u,
			Request,
			&SampleFailure)
			&& FABTSM11GravityAssistSolver::Solve(
				Request,
				Result,
				&SampleFailure);
		Sample.bF4 = bSolved
			&& FABTSM11PrefixClassifier::Classify(
				Preset,
				Result,
				0x7u).IsF(4);
		const int32 SampleIndex = Samples.Add(MoveTemp(Sample));
		SampleByOffset.Add(Offset, SampleIndex);
		if (!Samples[SampleIndex].bF4)
		{
			if (!bF4SeedFound)
			{
				VisitF4GuidanceNeighbors(
					Offset,
					[&Open, &Enqueued](const FIntVector& Neighbor)
					{
						if (!Enqueued.Contains(Neighbor))
						{
							Enqueued.Add(Neighbor);
							Open.Add(Neighbor);
						}
					});
			}
			continue;
		}

		if (!bF4SeedFound)
		{
			// Discard the remaining discovery shell. From this point onward only
			// the verified F4 component is allowed to grow the frontier.
			bF4SeedFound = true;
			Open.Reset();
			Enqueued.Reset();
			OpenIndex = 0;
		}
		F4Indices.Add(SampleIndex);
		VisitF4GuidanceNeighbors(
			Offset,
			[&Open, &Enqueued, &SampleByOffset](
				const FIntVector& Neighbor)
			{
				if (!SampleByOffset.Contains(Neighbor)
					&& !Enqueued.Contains(Neighbor))
				{
					Enqueued.Add(Neighbor);
					Open.Add(Neighbor);
				}
			});
	}

	if (F4Indices.IsEmpty())
	{
		return RejectF4Guidance(OutFailure, TEXT("NoF4Samples"));
	}

	FVector3d OffsetCentroid = FVector3d::ZeroVector;
	for (const int32 Index : F4Indices)
	{
		const FIntVector& Offset = Samples[Index].Offset;
		OffsetCentroid += FVector3d(Offset.X, Offset.Y, Offset.Z);
	}
	OffsetCentroid /= static_cast<double>(F4Indices.Num());

	int32 BestIndex = F4Indices[0];
	int32 BestF4NeighborCount = -1;
	double BestCentroidDistanceSquared = TNumericLimits<double>::Max();
	for (const int32 Index : F4Indices)
	{
		const FIntVector& Offset = Samples[Index].Offset;
		int32 F4NeighborCount = 0;
		VisitF4GuidanceNeighbors(
			Offset,
			[&SampleByOffset, &Samples, &F4NeighborCount](
				const FIntVector& Neighbor)
			{
				const int32* NeighborIndex = SampleByOffset.Find(Neighbor);
				if (NeighborIndex != nullptr
					&& Samples[*NeighborIndex].bF4)
				{
					++F4NeighborCount;
				}
			});
		const FVector3d OffsetVector(Offset.X, Offset.Y, Offset.Z);
		const double CentroidDistanceSquared =
			(OffsetVector - OffsetCentroid).SquaredLength();
		if (F4NeighborCount > BestF4NeighborCount
			|| (F4NeighborCount == BestF4NeighborCount
				&& CentroidDistanceSquared
					< BestCentroidDistanceSquared))
		{
			BestIndex = Index;
			BestF4NeighborCount = F4NeighborCount;
			BestCentroidDistanceSquared = CentroidDistanceSquared;
		}
	}

	OutTarget.Input = Samples[BestIndex].Input;
	OutTarget.YawToleranceDegrees = YawStep * 0.5;
	OutTarget.PitchToleranceDegrees = PitchStep * 0.5;
	OutTarget.PowerTolerance = PowerStep * 0.5;
	OutTarget.SampleCount = Samples.Num();
	OutTarget.F4SampleCount = F4Indices.Num();
	OutTarget.bSearchTruncated = OpenIndex < Open.Num();
	OutTarget.bValid = true;
	if (!OutTarget.IsValid(Preset.LaunchModel))
	{
		OutTarget = FABTSM11F4GuidanceTarget();
		return RejectF4Guidance(OutFailure, TEXT("ResolvedTargetInvalid"));
	}
	return true;
}

bool FABTSM11FinaleControlPanelConfig::IsValid() const
{
	return FMath::IsFinite(FullRangeDragPixels)
		&& FullRangeDragPixels > 0.0
		&& FMath::IsFinite(WheelFullRangeFraction)
		&& WheelFullRangeFraction > 0.0;
}

double FABTSM11FinaleControlPanelConfig::GetGearScale(
	const EABTSM11ControlSpeedGear Gear) const
{
	switch (Gear)
	{
	case EABTSM11ControlSpeedGear::Coarse: return 1.0;
	case EABTSM11ControlSpeedGear::Fine: return 0.1;
	case EABTSM11ControlSpeedGear::UltraFine: return 0.01;
	default: return 0.0;
	}
}

bool FABTSM11FinaleControlPanelState::Initialize(
	const FABTSM11FinaleLaunchModel& InLaunchModel,
	const FABTSM11FinaleLaunchInput& InInitialInput,
	const FABTSM11FinaleControlPanelConfig& InConfig)
{
	bInitialized = InLaunchModel.IsValid()
		&& InLaunchModel.Contains(InInitialInput)
		&& InConfig.IsValid();
	if (!bInitialized)
	{
		return false;
	}
	LaunchModel = InLaunchModel;
	InitialInput = InInitialInput;
	Input = InInitialInput;
	Config = InConfig;
	SpeedGear = EABTSM11ControlSpeedGear::Coarse;
	return true;
}

void FABTSM11FinaleControlPanelState::SetSpeedGear(
	const EABTSM11ControlSpeedGear InGear)
{
	if (Config.GetGearScale(InGear) > 0.0)
	{
		SpeedGear = InGear;
	}
}

bool FABTSM11FinaleControlPanelState::ApplyDragPixels(
	const EABTSM11FinaleControlAxis Axis,
	const double PixelDelta)
{
	return bInitialized
		&& FMath::IsFinite(PixelDelta)
		&& ApplyNormalizedDelta(
			Axis,
			PixelDelta / Config.FullRangeDragPixels
				* Config.GetGearScale(SpeedGear));
}

bool FABTSM11FinaleControlPanelState::ApplyWheelSteps(
	const EABTSM11FinaleControlAxis Axis,
	const double WheelSteps)
{
	return bInitialized
		&& FMath::IsFinite(WheelSteps)
		&& ApplyNormalizedDelta(
			Axis,
			WheelSteps * Config.WheelFullRangeFraction
				* Config.GetGearScale(SpeedGear));
}

bool FABTSM11FinaleControlPanelState::ResetAxis(
	const EABTSM11FinaleControlAxis Axis)
{
	if (!bInitialized)
	{
		return false;
	}
	const double Before = GetAxisValue(Input, Axis);
	SetAxisValue(Input, Axis, GetAxisValue(InitialInput, Axis));
	return Before != GetAxisValue(Input, Axis);
}

void FABTSM11FinaleControlPanelState::ResetAll()
{
	if (bInitialized)
	{
		Input = InitialInput;
	}
}

bool FABTSM11FinaleControlPanelState::ApplyNormalizedDelta(
	const EABTSM11FinaleControlAxis Axis,
	const double Delta)
{
	if (!FMath::IsFinite(Delta))
	{
		return false;
	}
	const double Before = GetAxisValue(Input, Axis);
	const double Range = AxisMaximum(LaunchModel, Axis)
		- AxisMinimum(LaunchModel, Axis);
	SetAxisValue(Input, Axis, Before + Delta * Range);
	ClampInput();
	return Before != GetAxisValue(Input, Axis);
}

void FABTSM11FinaleControlPanelState::ClampInput()
{
	Input.YawDegrees = FMath::Clamp(
		Input.YawDegrees,
		LaunchModel.MinimumYawDegrees,
		LaunchModel.MaximumYawDegrees);
	Input.PitchDegrees = FMath::Clamp(
		Input.PitchDegrees,
		LaunchModel.MinimumPitchDegrees,
		LaunchModel.MaximumPitchDegrees);
	Input.Power = FMath::Clamp(
		Input.Power,
		LaunchModel.MinimumPower,
		LaunchModel.MaximumPower);
}

bool FABTSM11FinaleHudCaptureState::TryBegin(
	const EABTSM11FinaleHudCapture RequestedCapture)
{
	if (RequestedCapture == EABTSM11FinaleHudCapture::None
		|| Capture != EABTSM11FinaleHudCapture::None)
	{
		return false;
	}
	Capture = RequestedCapture;
	bFocusLossCancellation = false;
	return true;
}

bool FABTSM11FinaleHudCaptureState::End(
	const EABTSM11FinaleHudCapture ExpectedCapture)
{
	if (Capture != ExpectedCapture
		|| ExpectedCapture == EABTSM11FinaleHudCapture::None)
	{
		return false;
	}
	Capture = EABTSM11FinaleHudCapture::None;
	return true;
}

void FABTSM11FinaleHudCaptureState::CancelForFocusLoss()
{
	bFocusLossCancellation = Capture != EABTSM11FinaleHudCapture::None;
	Capture = EABTSM11FinaleHudCapture::None;
}

bool FABTSM11FinaleHudCaptureState::CanLaunch() const
{
	return Capture == EABTSM11FinaleHudCapture::None;
}

bool FABTSM11FinaleHudCaptureState::TryBeginLaunch()
{
	return CanLaunch()
		&& TryBegin(EABTSM11FinaleHudCapture::LaunchButton);
}

bool ABTSM11ShouldCommitFinaleHudLaunch(
	const EABTSM11FinaleHudCapture Capture,
	const bool bReleasedInsideLaunchButton,
	const bool bIsAiming)
{
	return Capture == EABTSM11FinaleHudCapture::LaunchButton
		&& bReleasedInsideLaunchButton
		&& bIsAiming;
}

bool ABTSM11ShouldRefreshFinaleHudTargetCapture(
	const bool bHasFrozenProbe,
	const bool bCaptureInitialized,
	const bool bAutomaticTargetChanged,
	const bool bExplicitProbeMutation)
{
	return bExplicitProbeMutation
		|| (!bHasFrozenProbe
			&& (!bCaptureInitialized || bAutomaticTargetChanged));
}

FVector2D ABTSM11MapViewportPointToHudCanvas(
	const FVector2D& ViewportPoint,
	const FVector2D& PlayerViewOrigin,
	const FVector2D& PlayerViewSize,
	const FVector2D& HudCanvasSize)
{
	if (PlayerViewSize.X <= KINDA_SMALL_NUMBER
		|| PlayerViewSize.Y <= KINDA_SMALL_NUMBER
		|| HudCanvasSize.X <= KINDA_SMALL_NUMBER
		|| HudCanvasSize.Y <= KINDA_SMALL_NUMBER)
	{
		return ViewportPoint;
	}
	const FVector2D PlayerViewPoint = ViewportPoint - PlayerViewOrigin;
	return FVector2D(
		PlayerViewPoint.X * HudCanvasSize.X / PlayerViewSize.X,
		PlayerViewPoint.Y * HudCanvasSize.Y / PlayerViewSize.Y);
}

bool FABTSM11TrajectorySemanticSegment::IsValid(
	const int32 PointCount) const
{
	return Leg != EABTSM11TrajectorySemanticLeg::Invalid
		&& StartPointIndex >= 0
		&& StartPointIndex < PointCount
		&& EndPointIndex >= StartPointIndex
		&& EndPointIndex < PointCount
		&& ClosestPointIndex >= StartPointIndex
		&& ClosestPointIndex <= EndPointIndex;
}

const FABTSM11TrajectorySemanticSegment* FABTSM11TrajectorySemanticMap::Find(
	const EABTSM11TrajectorySemanticLeg Leg) const
{
	return Segments.FindByPredicate(
		[Leg](const FABTSM11TrajectorySemanticSegment& Segment)
		{
			return Segment.Leg == Leg;
		});
}

bool FABTSM11TrajectorySemanticMap::ResolvePoint(
	const EABTSM11TrajectorySemanticLeg Leg,
	const double PhaseWithinLeg,
	const TConstArrayView<FABTSM11OrbitalScenePoint> Points,
	int32& OutPointA,
	int32& OutPointB,
	double& OutAlpha) const
{
	const FABTSM11TrajectorySemanticSegment* Segment = Find(Leg);
	if (Segment == nullptr || !Segment->IsValid(Points.Num()))
	{
		return false;
	}
	const double Phase = FMath::Clamp(PhaseWithinLeg, 0.0, 1.0);
	double TargetArc = 0.0;
	if (Segment->bEncounter)
	{
		if (Phase <= 0.5)
		{
			TargetArc = FMath::Lerp(
				Points[Segment->StartPointIndex].ArcLengthCM,
				Points[Segment->ClosestPointIndex].ArcLengthCM,
				Phase * 2.0);
		}
		else
		{
			TargetArc = FMath::Lerp(
				Points[Segment->ClosestPointIndex].ArcLengthCM,
				Points[Segment->EndPointIndex].ArcLengthCM,
				(Phase - 0.5) * 2.0);
		}
	}
	else
	{
		TargetArc = FMath::Lerp(
			Points[Segment->StartPointIndex].ArcLengthCM,
			Points[Segment->EndPointIndex].ArcLengthCM,
			Phase);
	}
	return FindArcBracket(
		Points,
		Segment->StartPointIndex,
		Segment->EndPointIndex,
		TargetArc,
		OutPointA,
		OutPointB,
		OutAlpha);
}

double FABTSM11TrajectorySemanticMap::ComputePhase(
	const FABTSM11TrajectorySemanticSegment& Segment,
	const double TimeSeconds,
	const TConstArrayView<FABTSM11OrbitalScenePoint> Points) const
{
	if (!Segment.IsValid(Points.Num()))
	{
		return 0.0;
	}
	const int32 PointIndex = FindNearestPointIndex(Points, TimeSeconds);
	const double Arc = Points[FMath::Clamp(
		PointIndex,
		Segment.StartPointIndex,
		Segment.EndPointIndex)].ArcLengthCM;
	const double StartArc = Points[Segment.StartPointIndex].ArcLengthCM;
	const double ClosestArc = Points[Segment.ClosestPointIndex].ArcLengthCM;
	const double EndArc = Points[Segment.EndPointIndex].ArcLengthCM;
	if (!Segment.bEncounter)
	{
		return EndArc > StartArc
			? FMath::Clamp((Arc - StartArc) / (EndArc - StartArc), 0.0, 1.0)
			: 0.0;
	}
	if (Arc <= ClosestArc)
	{
		return ClosestArc > StartArc
			? 0.5 * FMath::Clamp(
				(Arc - StartArc) / (ClosestArc - StartArc),
				0.0,
				1.0)
			: 0.5;
	}
	return EndArc > ClosestArc
		? 0.5 + 0.5 * FMath::Clamp(
			(Arc - ClosestArc) / (EndArc - ClosestArc),
			0.0,
			1.0)
		: 0.5;
}

bool FABTSM11OrbitalSceneSnapshot::GetContextGeometry(
	const int32 BodyIndex,
	const bool bTarget,
	FVector3d& OutCenterCM,
	FVector3d& OutVelocityCMPerSec,
	double& OutVisualRadiusCM,
	double& OutInfluenceRadiusCM) const
{
	if (bTarget)
	{
		OutCenterCM = TargetCenterCM;
		OutVelocityCMPerSec = FVector3d::ZeroVector;
		OutVisualRadiusCM = TargetRadiusCM;
		OutInfluenceRadiusCM = TargetRadiusCM;
		return bValid
			&& TargetRadiusCM > 0.0
			&& IsFiniteFinaleHudVector(TargetCenterCM);
	}
	if (!bValid || BodyIndex < 0 || BodyIndex >= Bodies.Num())
	{
		return false;
	}
	const FABTSM11OrbitalSceneBody& Body = Bodies[BodyIndex];
	OutCenterCM = Body.CenterCM;
	OutVelocityCMPerSec = Body.VirtualVelocityCMPerSec;
	OutVisualRadiusCM = Body.VisualRadiusCM;
	OutInfluenceRadiusCM = Body.InfluenceRadiusCM;
	return Body.BodyIndex == BodyIndex;
}

bool FABTSM11OrbitalSceneBuilder::Build(
	const FABTSM11FinaleLayoutPreset& Preset,
	const FABTSM11TrajectoryResult& Result,
	FABTSM11OrbitalSceneSnapshot& OutSnapshot,
	const int32 MaximumTrajectoryPointCount)
{
	OutSnapshot = FABTSM11OrbitalSceneSnapshot();
	if (!Preset.IsValid()
		|| Result.ValidationHash == 0
		|| Result.Points.Num() < 2
		|| MaximumTrajectoryPointCount < 2)
	{
		return false;
	}

	TArray<double> FullArcLengths;
	FullArcLengths.SetNumZeroed(Result.Points.Num());
	for (int32 Index = 1; Index < Result.Points.Num(); ++Index)
	{
		FullArcLengths[Index] = FullArcLengths[Index - 1]
			+ (Result.Points[Index].PositionCM
				- Result.Points[Index - 1].PositionCM).Length();
	}

	TArray<int32> SourceIndices;
	const int32 Stride = FMath::Max(
		1,
		FMath::CeilToInt(
			static_cast<double>(Result.Points.Num())
			/ static_cast<double>(MaximumTrajectoryPointCount)));
	for (int32 Index = 0; Index < Result.Points.Num(); Index += Stride)
	{
		SourceIndices.Add(Index);
	}
	SourceIndices.Add(Result.Points.Num() - 1);
	for (const FABTSM11TrajectoryEvent& Event : Result.Events)
	{
		int32 NearestIndex = 0;
		double Difference = FMath::Abs(
			Result.Points[0].TimeSeconds - Event.TimeSeconds);
		for (int32 Index = 1; Index < Result.Points.Num(); ++Index)
		{
			const double Candidate = FMath::Abs(
				Result.Points[Index].TimeSeconds - Event.TimeSeconds);
			if (Candidate < Difference)
			{
				Difference = Candidate;
				NearestIndex = Index;
			}
		}
		SourceIndices.Add(NearestIndex);
	}
	SourceIndices.Sort();
	for (int32 Index = SourceIndices.Num() - 1; Index > 0; --Index)
	{
		if (SourceIndices[Index] == SourceIndices[Index - 1])
		{
			SourceIndices.RemoveAt(Index, 1, EAllowShrinking::No);
		}
	}

	for (const int32 SourceIndex : SourceIndices)
	{
		const FABTSM11TrajectoryPoint& Source = Result.Points[SourceIndex];
		if (!FMath::IsFinite(Source.TimeSeconds)
			|| !IsFiniteFinaleHudVector(Source.PositionCM)
			|| !IsFiniteFinaleHudVector(Source.VelocityCMPerSec))
		{
			return false;
		}
		FABTSM11OrbitalScenePoint& Point =
			OutSnapshot.Trajectory.AddDefaulted_GetRef();
		Point.TimeSeconds = Source.TimeSeconds;
		Point.ArcLengthCM = FullArcLengths[SourceIndex];
		Point.PositionCM = Source.PositionCM;
		Point.VelocityCMPerSec = Source.VelocityCMPerSec;
	}

	for (int32 BodyIndex = 0;
		BodyIndex < FABTSM11GravityScenario::BodyCount;
		++BodyIndex)
	{
		const FABTSM11GravityBodySpec& Source =
			Preset.CanonicalScenario.Bodies[BodyIndex];
		FABTSM11OrbitalSceneBody& Body = OutSnapshot.Bodies[BodyIndex];
		Body.BodyIndex = BodyIndex;
		Body.BodyId = Source.BodyId;
		Body.Role = Source.Role;
		Body.CenterCM = Source.CenterCM;
		Body.VirtualVelocityCMPerSec = Source.VirtualOrbitalVelocityCMPerSec;
		Body.VisualRadiusCM = Source.VisualRadiusCM;
		Body.CollisionRadiusCM = Source.CollisionRadiusCM;
		Body.InfluenceRadiusCM = Source.InfluenceRadiusCM;
	}
	OutSnapshot.TargetCenterCM =
		Preset.CanonicalScenario.Target.GetGeometricContactCenterCM();
	OutSnapshot.TargetRadiusCM =
		Preset.CanonicalScenario.Target.GetGeometricContactRadiusCM();
	OutSnapshot.SourceTrajectoryHash = Result.ValidationHash;

	const int32 LastIndex = OutSnapshot.Trajectory.Num() - 1;
	int32 PreviousEndIndex = 0;
	for (int32 AssistIndex = 1;
		AssistIndex <= FABTSM11GravityScenario::AssistCount;
		++AssistIndex)
	{
		const FABTSM11TrajectoryEvent* Enter = Result.FindAssistEvent(
			EABTSM11TrajectoryEventType::AssistEnter,
			AssistIndex);
		if (Enter == nullptr)
		{
			FABTSM11TrajectorySemanticSegment& Coast =
				OutSnapshot.SemanticMap.Segments.AddDefaulted_GetRef();
			Coast.Leg = CoastLegForAssist(AssistIndex);
			Coast.StartPointIndex = PreviousEndIndex;
			Coast.ClosestPointIndex = PreviousEndIndex;
			Coast.EndPointIndex = LastIndex;
			Coast.ContextBodyIndex = AssistIndex;
			break;
		}
		const int32 EnterIndex = FindNearestPointIndex(
			OutSnapshot.Trajectory,
			Enter->TimeSeconds);
		FABTSM11TrajectorySemanticSegment& Coast =
			OutSnapshot.SemanticMap.Segments.AddDefaulted_GetRef();
		Coast.Leg = CoastLegForAssist(AssistIndex);
		Coast.StartPointIndex = PreviousEndIndex;
		Coast.ClosestPointIndex = PreviousEndIndex;
		Coast.EndPointIndex = FMath::Max(PreviousEndIndex, EnterIndex);
		Coast.ContextBodyIndex = AssistIndex;

		const FABTSM11TrajectoryEvent* Closest = Result.FindAssistEvent(
			EABTSM11TrajectoryEventType::ClosestApproach,
			AssistIndex);
		const FABTSM11TrajectoryEvent* Exit = Result.FindAssistEvent(
			EABTSM11TrajectoryEventType::AssistExit,
			AssistIndex);
		const int32 RawExitIndex = Exit != nullptr
			? FindNearestPointIndex(OutSnapshot.Trajectory, Exit->TimeSeconds)
			: LastIndex;
		const int32 ExitIndex = FMath::Max(EnterIndex, RawExitIndex);
		int32 ClosestIndex = Closest != nullptr
			? FindNearestPointIndex(OutSnapshot.Trajectory, Closest->TimeSeconds)
			: FindClosestPointIndex(
				OutSnapshot.Trajectory,
				OutSnapshot.Bodies[AssistIndex].CenterCM,
				EnterIndex,
				ExitIndex);
		ClosestIndex = FMath::Clamp(ClosestIndex, EnterIndex, ExitIndex);
		FABTSM11TrajectorySemanticSegment& Encounter =
			OutSnapshot.SemanticMap.Segments.AddDefaulted_GetRef();
		Encounter.Leg = EncounterLegForAssist(AssistIndex);
		Encounter.StartPointIndex = EnterIndex;
		Encounter.ClosestPointIndex = ClosestIndex;
		Encounter.EndPointIndex = FMath::Max(EnterIndex, ExitIndex);
		Encounter.ContextBodyIndex = AssistIndex;
		Encounter.bEncounter = true;
		PreviousEndIndex = Encounter.EndPointIndex;
		if (Exit == nullptr)
		{
			break;
		}
	}

	if (OutSnapshot.SemanticMap.Find(
			EABTSM11TrajectorySemanticLeg::Assist3Encounter) != nullptr)
	{
		int32 TargetApproachIndex = INDEX_NONE;
		const double ApproachRadius = FMath::Max(
			Preset.TargetApproachRadiusCM,
			OutSnapshot.TargetRadiusCM);
		for (int32 Index = PreviousEndIndex;
			Index < OutSnapshot.Trajectory.Num();
			++Index)
		{
			if ((OutSnapshot.Trajectory[Index].PositionCM
				- OutSnapshot.TargetCenterCM).Length() <= ApproachRadius)
			{
				TargetApproachIndex = Index;
				break;
			}
		}
		FABTSM11TrajectorySemanticSegment& Coast =
			OutSnapshot.SemanticMap.Segments.AddDefaulted_GetRef();
		Coast.Leg = EABTSM11TrajectorySemanticLeg::Assist3ToTarget;
		Coast.StartPointIndex = PreviousEndIndex;
		Coast.ClosestPointIndex = PreviousEndIndex;
		Coast.EndPointIndex = TargetApproachIndex != INDEX_NONE
			? TargetApproachIndex
			: LastIndex;
		Coast.bContextIsTarget = true;
		if (TargetApproachIndex != INDEX_NONE)
		{
			FABTSM11TrajectorySemanticSegment& Approach =
				OutSnapshot.SemanticMap.Segments.AddDefaulted_GetRef();
			Approach.Leg = EABTSM11TrajectorySemanticLeg::TargetApproach;
			Approach.StartPointIndex = TargetApproachIndex;
			Approach.ClosestPointIndex = FindClosestPointIndex(
				OutSnapshot.Trajectory,
				OutSnapshot.TargetCenterCM,
				TargetApproachIndex,
				LastIndex);
			Approach.EndPointIndex = LastIndex;
			Approach.bContextIsTarget = true;
			Approach.bEncounter = true;
		}
	}

	OutSnapshot.bValid = OutSnapshot.Trajectory.Num() >= 2
		&& !OutSnapshot.SemanticMap.Segments.IsEmpty();
	return OutSnapshot.bValid;
}

bool FABTSM11OverviewViewState::Initialize(
	const FVector3d& InCenterCM,
	const FVector3d& InAxisX,
	const FVector3d& InAxisY,
	const double InProjectionScaleCM)
{
	ProjectionCenterCM = InCenterCM;
	AxisX = InAxisX.GetSafeNormal();
	ViewForward = AxisX.Cross(InAxisY).GetSafeNormal();
	AxisY = ViewForward.Cross(AxisX).GetSafeNormal();
	ProjectionScaleCM = InProjectionScaleCM;
	Zoom = 1.0;
	bValid = IsFiniteFinaleHudVector(ProjectionCenterCM)
		&& !AxisX.IsNearlyZero()
		&& !AxisY.IsNearlyZero()
		&& !ViewForward.IsNearlyZero()
		&& FMath::IsFinite(ProjectionScaleCM)
		&& ProjectionScaleCM > 0.0;
	return bValid;
}

bool FABTSM11OverviewViewState::InitializeFromScene(
	const FABTSM11OrbitalSceneSnapshot& Scene,
	const FVector3d& InAxisX,
	const FVector3d& InAxisY)
{
	if (!Scene.bValid || Scene.Trajectory.Num() < 2)
	{
		return false;
	}

	FVector3d Minimum(
		TNumericLimits<double>::Max(),
		TNumericLimits<double>::Max(),
		TNumericLimits<double>::Max());
	FVector3d Maximum(
		TNumericLimits<double>::Lowest(),
		TNumericLimits<double>::Lowest(),
		TNumericLimits<double>::Lowest());
	const auto IncludeSphere = [&Minimum, &Maximum](
		const FVector3d& Center,
		const double Radius)
	{
		if (!IsFiniteFinaleHudVector(Center)
			|| !FMath::IsFinite(Radius)
			|| Radius < 0.0)
		{
			return false;
		}
		const FVector3d Extent(Radius, Radius, Radius);
		Minimum.X = FMath::Min(Minimum.X, Center.X - Extent.X);
		Minimum.Y = FMath::Min(Minimum.Y, Center.Y - Extent.Y);
		Minimum.Z = FMath::Min(Minimum.Z, Center.Z - Extent.Z);
		Maximum.X = FMath::Max(Maximum.X, Center.X + Extent.X);
		Maximum.Y = FMath::Max(Maximum.Y, Center.Y + Extent.Y);
		Maximum.Z = FMath::Max(Maximum.Z, Center.Z + Extent.Z);
		return true;
	};

	for (const FABTSM11OrbitalScenePoint& Point : Scene.Trajectory)
	{
		if (!IncludeSphere(Point.PositionCM, 0.0))
		{
			return false;
		}
	}
	// The primary is deliberately excluded. M10.1-C/M11 permit the overview
	// to crop the main planet; the three causal assists and target define the
	// terminal route volume that the player manipulates.
	for (int32 BodyIndex = 1; BodyIndex < Scene.Bodies.Num(); ++BodyIndex)
	{
		const FABTSM11OrbitalSceneBody& Body = Scene.Bodies[BodyIndex];
		if (!IncludeSphere(
			Body.CenterCM,
			FMath::Max(0.0, Body.VisualRadiusCM)))
		{
			return false;
		}
	}
	if (!IncludeSphere(
		Scene.TargetCenterCM,
		FMath::Max(0.0, Scene.TargetRadiusCM)))
	{
		return false;
	}

	const FVector3d BoundingSphereCenter = (Minimum + Maximum) * 0.5;
	double BoundingSphereRadius = 1.0;
	for (const FABTSM11OrbitalScenePoint& Point : Scene.Trajectory)
	{
		BoundingSphereRadius = FMath::Max(
			BoundingSphereRadius,
			(Point.PositionCM - BoundingSphereCenter).Length());
	}
	for (int32 BodyIndex = 1; BodyIndex < Scene.Bodies.Num(); ++BodyIndex)
	{
		const FABTSM11OrbitalSceneBody& Body = Scene.Bodies[BodyIndex];
		BoundingSphereRadius = FMath::Max(
			BoundingSphereRadius,
			(Body.CenterCM - BoundingSphereCenter).Length()
				+ FMath::Max(0.0, Body.VisualRadiusCM));
	}
	BoundingSphereRadius = FMath::Max(
		BoundingSphereRadius,
		(Scene.TargetCenterCM - BoundingSphereCenter).Length()
			+ FMath::Max(0.0, Scene.TargetRadiusCM));

	// Keep the full route volume inside 80% of the circular overview.
	return Initialize(
		BoundingSphereCenter,
		InAxisX,
		InAxisY,
		BoundingSphereRadius / 0.80);
}

bool FABTSM11OverviewViewState::ApplyPanNormalized(
	const FVector2d& ScreenDelta)
{
	if (!bValid
		|| !FMath::IsFinite(ScreenDelta.X)
		|| !FMath::IsFinite(ScreenDelta.Y)
		|| Zoom <= UE_DOUBLE_SMALL_NUMBER)
	{
		return false;
	}
	if (ScreenDelta.IsNearlyZero())
	{
		return false;
	}

	// Screen Y points down while projected AxisY points up. Move the pivot in
	// the opposite projected direction so the visible content follows the
	// left-button drag exactly.
	const FVector3d WorldDelta =
		AxisX * (-ScreenDelta.X * ProjectionScaleCM / Zoom)
		+ AxisY * (ScreenDelta.Y * ProjectionScaleCM / Zoom);
	const FVector3d Candidate = ProjectionCenterCM + WorldDelta;
	if (!IsFiniteFinaleHudVector(Candidate))
	{
		return false;
	}
	ProjectionCenterCM = Candidate;
	return true;
}

bool FABTSM11OverviewViewState::ApplyOrbitRotation(
	const double YawDegrees,
	const double PitchDegrees)
{
	if (!bValid
		|| !FMath::IsFinite(YawDegrees)
		|| !FMath::IsFinite(PitchDegrees))
	{
		return false;
	}
	if (FMath::IsNearlyZero(YawDegrees)
		&& FMath::IsNearlyZero(PitchDegrees))
	{
		return false;
	}

	// Trackball-style local orbit. Horizontal drag rotates around the current
	// screen Up; vertical drag then rotates around the yawed screen Right.
	// No world Up is reintroduced, so composed drags naturally accumulate
	// roll without exposing a direct roll control.
	const FQuat4d YawRotation(
		AxisY,
		FMath::DegreesToRadians(YawDegrees));
	FVector3d RotatedRight = YawRotation.RotateVector(AxisX);
	FVector3d RotatedUp = YawRotation.RotateVector(AxisY);
	FVector3d RotatedForward = YawRotation.RotateVector(ViewForward);
	const FQuat4d PitchRotation(
		RotatedRight.GetSafeNormal(),
		FMath::DegreesToRadians(PitchDegrees));
	RotatedRight = PitchRotation.RotateVector(RotatedRight);
	RotatedUp = PitchRotation.RotateVector(RotatedUp);
	RotatedForward = PitchRotation.RotateVector(RotatedForward);
	if (!IsFiniteFinaleHudVector(RotatedRight)
		|| !IsFiniteFinaleHudVector(RotatedUp)
		|| !IsFiniteFinaleHudVector(RotatedForward))
	{
		return false;
	}

	ViewForward = RotatedForward.GetSafeNormal();
	AxisX = RotatedRight.GetSafeNormal();
	AxisY = ViewForward.Cross(AxisX).GetSafeNormal();
	AxisX = AxisY.Cross(ViewForward).GetSafeNormal();
	if (AxisX.IsNearlyZero()
		|| AxisY.IsNearlyZero()
		|| ViewForward.IsNearlyZero())
	{
		return false;
	}
	return true;
}

bool FABTSM11OverviewViewState::ApplyZoom(
	const double ZoomMultiplier,
	const double MinimumZoom,
	const double MaximumZoom)
{
	if (!bValid
		|| !FMath::IsFinite(ZoomMultiplier)
		|| ZoomMultiplier <= 0.0
		|| !FMath::IsFinite(MinimumZoom)
		|| !FMath::IsFinite(MaximumZoom)
		|| MinimumZoom <= 0.0
		|| MaximumZoom < MinimumZoom)
	{
		return false;
	}
	const double Before = Zoom;
	Zoom = FMath::Clamp(Zoom * ZoomMultiplier, MinimumZoom, MaximumZoom);
	return Zoom != Before;
}

FVector2d FABTSM11OverviewViewState::Project(
	const FVector3d& PositionCM) const
{
	if (!bValid)
	{
		return FVector2d::ZeroVector;
	}
	const FVector3d Relative = PositionCM - ProjectionCenterCM;
	return FVector2d(Relative.Dot(AxisX), Relative.Dot(AxisY))
		* (Zoom / ProjectionScaleCM);
}

double FABTSM11OverviewViewState::ProjectDepth(
	const FVector3d& PositionCM) const
{
	return bValid
		? (PositionCM - ProjectionCenterCM).Dot(ViewForward)
		: 0.0;
}

bool FABTSM11OverviewProjector::Build(
	const FABTSM11OrbitalSceneSnapshot& Scene,
	const FABTSM11OverviewViewState& View,
	FABTSM11OverviewProjection& OutProjection)
{
	OutProjection = FABTSM11OverviewProjection();
	if (!Scene.bValid || !View.bValid || Scene.Trajectory.Num() < 2)
	{
		return false;
	}
	for (int32 BodyIndex = 0; BodyIndex < Scene.Bodies.Num(); ++BodyIndex)
	{
		const FABTSM11OrbitalSceneBody& Body = Scene.Bodies[BodyIndex];
		FABTSM11OverviewProjectedBody& Projected =
			OutProjection.Bodies[BodyIndex];
		Projected.BodyIndex = BodyIndex;
		Projected.Center = View.Project(Body.CenterCM);
		Projected.VisualRadius = Body.VisualRadiusCM
			* View.Zoom / View.ProjectionScaleCM;
	}
	OutProjection.TargetCenter = View.Project(Scene.TargetCenterCM);
	OutProjection.TargetRadius = Scene.TargetRadiusCM
		* View.Zoom / View.ProjectionScaleCM;
	for (const FABTSM11OrbitalScenePoint& Point : Scene.Trajectory)
	{
		FABTSM11OverviewProjectedPoint& Projected =
			OutProjection.Trajectory.AddDefaulted_GetRef();
		Projected.Position = View.Project(Point.PositionCM);
		Projected.Depth = View.ProjectDepth(Point.PositionCM);
		Projected.TimeSeconds = Point.TimeSeconds;
		Projected.ArcLengthCM = Point.ArcLengthCM;
		Projected.bHiddenByBody = IsHiddenByBody(Scene, View, Point.PositionCM);
	}
	for (int32 Index = 1; Index < Scene.Trajectory.Num(); ++Index)
	{
		const double MidTime = FMath::Lerp(
			Scene.Trajectory[Index - 1].TimeSeconds,
			Scene.Trajectory[Index].TimeSeconds,
			0.5);
		const FABTSM11TrajectorySemanticSegment* Segment =
			FindSegmentAtTime(Scene.SemanticMap, Scene.Trajectory, MidTime);
		if (Segment == nullptr)
		{
			continue;
		}
		FABTSM11OverviewHitProxy& Proxy =
			OutProjection.HitProxies.AddDefaulted_GetRef();
		Proxy.Start = OutProjection.Trajectory[Index - 1].Position;
		Proxy.End = OutProjection.Trajectory[Index].Position;
		Proxy.StartTimeSeconds = Scene.Trajectory[Index - 1].TimeSeconds;
		Proxy.EndTimeSeconds = Scene.Trajectory[Index].TimeSeconds;
		Proxy.Leg = Segment->Leg;
		Proxy.StartPhase = Scene.SemanticMap.ComputePhase(
			*Segment,
			Proxy.StartTimeSeconds,
			Scene.Trajectory);
		Proxy.EndPhase = Scene.SemanticMap.ComputePhase(
			*Segment,
			Proxy.EndTimeSeconds,
			Scene.Trajectory);
		Proxy.bHiddenByBody =
			OutProjection.Trajectory[Index - 1].bHiddenByBody
			&& OutProjection.Trajectory[Index].bHiddenByBody;
	}
	OutProjection.SourceTrajectoryHash = Scene.SourceTrajectoryHash;
	OutProjection.bValid = !OutProjection.HitProxies.IsEmpty();
	return OutProjection.bValid;
}

bool ABTSM11HitTestOverviewTrajectory(
	const FABTSM11OverviewProjection& Projection,
	const FVector2d& MousePixels,
	const FVector2d& PanelCenterPixels,
	const double PanelRadiusPixels,
	const double HitRadiusPixels,
	FABTSM11TrajectoryHit& OutHit,
	const EABTSM11TrajectorySemanticLeg PreferredLeg)
{
	OutHit = FABTSM11TrajectoryHit();
	if (!Projection.bValid
		|| PanelRadiusPixels <= 0.0
		|| HitRadiusPixels <= 0.0
		|| (MousePixels - PanelCenterPixels).Length()
			> PanelRadiusPixels + HitRadiusPixels)
	{
		return false;
	}
	for (const FABTSM11OverviewHitProxy& Proxy : Projection.HitProxies)
	{
		const FVector2d Start = PanelCenterPixels
			+ Proxy.Start * PanelRadiusPixels;
		const FVector2d End = PanelCenterPixels
			+ Proxy.End * PanelRadiusPixels;
		double Alpha = 0.0;
		const double DistanceSquared = PointSegmentDistanceSquared(
			MousePixels,
			Start,
			End,
			Alpha);
		if (DistanceSquared > HitRadiusPixels * HitRadiusPixels)
		{
			continue;
		}
		const double Distance = FMath::Sqrt(DistanceSquared);
		const bool bDistanceTie = OutHit.bValid
			&& FMath::Abs(Distance - OutHit.PixelDistance) <= 0.5;
		const bool bPreferred = Proxy.Leg == PreferredLeg;
		const bool bExistingPreferred = OutHit.Leg == PreferredLeg;
		const bool bBetterTie = bDistanceTie
			&& ((OutHit.bHiddenByBody && !Proxy.bHiddenByBody)
				|| (OutHit.bHiddenByBody == Proxy.bHiddenByBody
					&& bPreferred && !bExistingPreferred)
				|| (OutHit.bHiddenByBody == Proxy.bHiddenByBody
					&& bPreferred == bExistingPreferred
					&& FMath::Lerp(
						Proxy.StartTimeSeconds,
						Proxy.EndTimeSeconds,
						Alpha) < OutHit.TimeSeconds));
		if (!OutHit.bValid
			|| Distance < OutHit.PixelDistance - 0.5
			|| bBetterTie)
		{
			OutHit.Leg = Proxy.Leg;
			OutHit.PhaseWithinLeg = FMath::Lerp(
				Proxy.StartPhase,
				Proxy.EndPhase,
				Alpha);
			OutHit.TimeSeconds = FMath::Lerp(
				Proxy.StartTimeSeconds,
				Proxy.EndTimeSeconds,
				Alpha);
			OutHit.PixelDistance = Distance;
			OutHit.bHiddenByBody = Proxy.bHiddenByBody;
			OutHit.bValid = true;
		}
	}
	return OutHit.bValid;
}

FVector2d FABTSM11FrozenPipView::Project(
	const FVector3d& PositionCM) const
{
	if (!bValid || HalfExtentCM <= 0.0)
	{
		return FVector2d::ZeroVector;
	}
	const FVector3d Relative = PositionCM - ViewCenterCM;
	return FVector2d(Relative.Dot(ViewRight), Relative.Dot(ViewUp))
		/ HalfExtentCM;
}

bool FABTSM11TrajectoryProbeBuilder::Create(
	const FABTSM11OrbitalSceneSnapshot& ReferenceScene,
	const FABTSM11TrajectoryHit& Hit,
	const FVector3d& FinaleLocalUp,
	const FVector3d& PreferredViewForward,
	FABTSM11TrajectoryProbe& OutProbe)
{
	OutProbe = FABTSM11TrajectoryProbe();
	if (!ReferenceScene.bValid || !Hit.bValid)
	{
		return false;
	}
	const FABTSM11TrajectorySemanticSegment* Segment =
		ReferenceScene.SemanticMap.Find(Hit.Leg);
	if (Segment == nullptr || !Segment->IsValid(ReferenceScene.Trajectory.Num()))
	{
		return false;
	}
	OutProbe.ReferenceResultHash = ReferenceScene.SourceTrajectoryHash;
	OutProbe.Leg = Hit.Leg;
	OutProbe.PhaseWithinLeg = FMath::Clamp(Hit.PhaseWithinLeg, 0.0, 1.0);
	OutProbe.ContextBodyIndex = Segment->ContextBodyIndex;
	OutProbe.bContextIsTarget = Segment->bContextIsTarget;
	FVector3d Velocity;
	if (!BuildFrozenView(
			ReferenceScene,
			OutProbe.Leg,
			OutProbe.PhaseWithinLeg,
			OutProbe.ContextBodyIndex,
			OutProbe.bContextIsTarget,
			FinaleLocalUp,
			PreferredViewForward,
			OutProbe.FrozenPipView,
			OutProbe.ReferenceLocalPosition,
			Velocity,
			OutProbe.ReferenceSolverTime))
	{
		return false;
	}
	OutProbe.ReferenceTangent = Velocity.GetSafeNormal();
	OutProbe.bValid = !OutProbe.ReferenceTangent.IsNearlyZero();
	return OutProbe.bValid;
}

bool FABTSM11TrajectoryProbeBuilder::Rebase(
	const FABTSM11OrbitalSceneSnapshot& Scene,
	const FABTSM11TrajectoryProbe& ExistingProbe,
	const FVector3d& FinaleLocalUp,
	FABTSM11TrajectoryProbe& OutProbe)
{
	FABTSM11ProbeProjection Resolved;
	if (!FABTSM11TrajectoryProbeResolver::Resolve(
			Scene,
			ExistingProbe,
			Resolved))
	{
		return false;
	}
	FABTSM11TrajectoryHit Hit;
	Hit.Leg = ExistingProbe.Leg;
	Hit.PhaseWithinLeg = ExistingProbe.PhaseWithinLeg;
	Hit.TimeSeconds = Resolved.TimeSeconds;
	Hit.bValid = true;
	return Create(
		Scene,
		Hit,
		FinaleLocalUp,
		ExistingProbe.FrozenPipView.ViewForward,
		OutProbe);
}

bool FABTSM11TrajectoryProbeResolver::Resolve(
	const FABTSM11OrbitalSceneSnapshot& Scene,
	const FABTSM11TrajectoryProbe& Probe,
	FABTSM11ProbeProjection& OutProjection)
{
	OutProjection = FABTSM11ProbeProjection();
	if (!Scene.bValid || !Probe.bValid || !Probe.FrozenPipView.bValid)
	{
		return false;
	}
	FVector3d ContextCenter;
	FVector3d ContextVelocity;
	double VisualRadius = 0.0;
	double InfluenceRadius = 0.0;
	if (!Scene.GetContextGeometry(
			Probe.ContextBodyIndex,
			Probe.bContextIsTarget,
			ContextCenter,
			ContextVelocity,
			VisualRadius,
			InfluenceRadius))
	{
		return false;
	}
	if (SampleScene(
			Scene,
			Probe.Leg,
			Probe.PhaseWithinLeg,
			OutProjection.PositionCM,
			OutProjection.VelocityCMPerSec,
			OutProjection.TimeSeconds))
	{
		OutProjection.Status = EABTSM11ProbeRemapStatus::ExactSemanticLeg;
	}
	else if (IsEncounterLeg(Probe.Leg))
	{
		const int32 ClosestIndex = FindClosestPointIndex(
			Scene.Trajectory,
			ContextCenter,
			0,
			Scene.Trajectory.Num() - 1);
		if (ClosestIndex == INDEX_NONE)
		{
			return false;
		}
		const FABTSM11OrbitalScenePoint& Closest =
			Scene.Trajectory[ClosestIndex];
		OutProjection.PositionCM = Closest.PositionCM;
		OutProjection.VelocityCMPerSec = Closest.VelocityCMPerSec;
		OutProjection.TimeSeconds = Closest.TimeSeconds;
		OutProjection.Status = EABTSM11ProbeRemapStatus::ClosestMissFallback;
	}
	else
	{
		const FABTSM11OrbitalScenePoint& Last = Scene.Trajectory.Last();
		OutProjection.PositionCM = Last.PositionCM;
		OutProjection.VelocityCMPerSec = Last.VelocityCMPerSec;
		OutProjection.TimeSeconds = Last.TimeSeconds;
		OutProjection.Status = EABTSM11ProbeRemapStatus::TrajectoryEndedBeforeLeg;
	}
	OutProjection.PipPosition =
		Probe.FrozenPipView.Project(OutProjection.PositionCM);
	OutProjection.ContextDistanceCM =
		(OutProjection.PositionCM - ContextCenter).Length();
	OutProjection.bValid = IsFiniteFinaleHudVector(OutProjection.PositionCM)
		&& IsFiniteFinaleHudVector(OutProjection.VelocityCMPerSec)
		&& FMath::IsFinite(OutProjection.TimeSeconds)
		&& FMath::IsFinite(OutProjection.ContextDistanceCM);
	return OutProjection.bValid;
}

bool ABTSM11BuildPipEdgeIndicator(
	const FVector2d& PointUV,
	const double MarginUV,
	FABTSM11PipEdgeIndicator& OutIndicator)
{
	OutIndicator = FABTSM11PipEdgeIndicator();
	if (!FMath::IsFinite(PointUV.X)
		|| !FMath::IsFinite(PointUV.Y)
		|| !FMath::IsFinite(MarginUV)
		|| MarginUV < 0.0
		|| MarginUV >= 0.5)
	{
		return false;
	}

	const double Minimum = MarginUV;
	const double Maximum = 1.0 - MarginUV;
	if (PointUV.X >= Minimum && PointUV.X <= Maximum
		&& PointUV.Y >= Minimum && PointUV.Y <= Maximum)
	{
		return true;
	}

	const FVector2d Center(0.5, 0.5);
	const FVector2d Ray = PointUV - Center;
	const double RayLength = Ray.Length();
	if (RayLength <= UE_DOUBLE_SMALL_NUMBER)
	{
		return false;
	}

	double Scale = TNumericLimits<double>::Max();
	if (Ray.X > UE_DOUBLE_SMALL_NUMBER)
	{
		Scale = FMath::Min(Scale, (Maximum - Center.X) / Ray.X);
	}
	else if (Ray.X < -UE_DOUBLE_SMALL_NUMBER)
	{
		Scale = FMath::Min(Scale, (Minimum - Center.X) / Ray.X);
	}
	if (Ray.Y > UE_DOUBLE_SMALL_NUMBER)
	{
		Scale = FMath::Min(Scale, (Maximum - Center.Y) / Ray.Y);
	}
	else if (Ray.Y < -UE_DOUBLE_SMALL_NUMBER)
	{
		Scale = FMath::Min(Scale, (Minimum - Center.Y) / Ray.Y);
	}
	if (!FMath::IsFinite(Scale) || Scale < 0.0)
	{
		return false;
	}

	OutIndicator.AnchorUV = Center + Ray * Scale;
	OutIndicator.DirectionUV = Ray / RayLength;
	OutIndicator.OvershootUV = (PointUV - OutIndicator.AnchorUV).Length();
	OutIndicator.bVisible = true;
	return true;
}
