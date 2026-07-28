// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM11FinaleInteractionTypes.h"

namespace
{
	FABTSM11FinaleLaunchInput ClampInput(
		const FABTSM11FinaleLaunchInput& Input,
		const FABTSM11FinaleLaunchInput& Minimum,
		const FABTSM11FinaleLaunchInput& Maximum)
	{
		FABTSM11FinaleLaunchInput Result = Input;
		Result.YawDegrees = FMath::Clamp(
			Result.YawDegrees,
			Minimum.YawDegrees,
			Maximum.YawDegrees);
		Result.PitchDegrees = FMath::Clamp(
			Result.PitchDegrees,
			Minimum.PitchDegrees,
			Maximum.PitchDegrees);
		Result.Power = FMath::Clamp(
			Result.Power,
			Minimum.Power,
			Maximum.Power);
		return Result;
	}

	EABTSM11PreviewTarget DesiredPreviewTarget(
		const FABTSM11PrefixClassification& Classification)
	{
		if ((Classification.ValidAssistMask & 0x7u) == 0x7u)
		{
			return EABTSM11PreviewTarget::UFO;
		}
		if ((Classification.ValidAssistMask & 0x3u) == 0x3u)
		{
			return EABTSM11PreviewTarget::Assist3;
		}
		if ((Classification.ValidAssistMask & 0x1u) != 0)
		{
			return EABTSM11PreviewTarget::Assist2;
		}
		return EABTSM11PreviewTarget::Assist1;
	}

	int32 TargetAssistIndex(const EABTSM11PreviewTarget Target)
	{
		switch (Target)
		{
		case EABTSM11PreviewTarget::Assist1: return 1;
		case EABTSM11PreviewTarget::Assist2: return 2;
		case EABTSM11PreviewTarget::Assist3: return 3;
		default: return 0;
		}
	}

	FABTSM11PreviewSelection BuildSelection(
		const EABTSM11PreviewTarget Target,
		const FABTSM11FinaleLayoutPreset& Preset,
		const FABTSM11TrajectoryResult& Result)
	{
		FABTSM11PreviewSelection Selection;
		Selection.Target = Target;
		const int32 AssistIndex = TargetAssistIndex(Target);
		if (AssistIndex > 0)
		{
			const FABTSM11GravityBodySpec& Body =
				Preset.CanonicalScenario.GetAssist(AssistIndex);
			Selection.TargetCenterCM = Body.CenterCM;
			Selection.TargetRadiusCM = Body.InfluenceRadiusCM;
			Selection.bEnteredTargetRegion =
				Result.FindAssistEvent(
					EABTSM11TrajectoryEventType::AssistEnter,
					AssistIndex) != nullptr;
		}
		else
		{
			const FABTSM11TargetSpec& TargetSpec =
				Preset.CanonicalScenario.Target;
			Selection.TargetCenterCM =
				TargetSpec.GetGeometricContactCenterCM();
			Selection.TargetRadiusCM =
				TargetSpec.GetGeometricContactRadiusCM();
			Selection.bEnteredTargetRegion =
				Result.FindFirstEvent(
					EABTSM11TrajectoryEventType::TargetContact) != nullptr;
		}

		const FABTSM11TrajectoryPoint* Closest = nullptr;
		for (const FABTSM11TrajectoryPoint& Point : Result.Points)
		{
			const double Distance =
				(Point.PositionCM - Selection.TargetCenterCM).Length();
			if (Distance < Selection.ClosestDistanceCM)
			{
				Selection.ClosestDistanceCM = Distance;
				Closest = &Point;
			}
		}
		if (Closest != nullptr)
		{
			Selection.ClosestTrajectoryPositionCM = Closest->PositionCM;
			Selection.IncomingDirection =
				Closest->VelocityCMPerSec.GetSafeNormal();
			if (Selection.IncomingDirection.IsNearlyZero())
			{
				Selection.IncomingDirection =
					(Selection.TargetCenterCM - Closest->PositionCM)
						.GetSafeNormal();
			}
		}
		return Selection;
	}
}

void FABTSM11PrimaryReleaseGate::Enter(const bool bEntryButtonDown)
{
	bWaitingForEntryRelease = bEntryButtonDown;
	bLaunchArmed = false;
}

void FABTSM11PrimaryReleaseGate::Reset()
{
	bWaitingForEntryRelease = false;
	bLaunchArmed = false;
}

void FABTSM11PrimaryReleaseGate::UpdateEntryButtonState(
	const bool bButtonDown)
{
	if (bWaitingForEntryRelease && !bButtonDown)
	{
		bWaitingForEntryRelease = false;
	}
}

void FABTSM11PrimaryReleaseGate::OnPrimaryPressed(
	const bool bIsAiming)
{
	bLaunchArmed = bIsAiming && !bWaitingForEntryRelease;
}

bool FABTSM11PrimaryReleaseGate::OnPrimaryReleased(
	const bool bIsAiming)
{
	if (bWaitingForEntryRelease)
	{
		bWaitingForEntryRelease = false;
		bLaunchArmed = false;
		return false;
	}
	const bool bShouldLaunch = bIsAiming && bLaunchArmed;
	bLaunchArmed = false;
	return bShouldLaunch;
}

bool FABTSM11PrefixStabilizerConfig::IsValid() const
{
	return FMath::IsFinite(NearSensitivityScale)
		&& NearSensitivityScale > 0.0
		&& NearSensitivityScale <= 1.0
		&& FMath::IsFinite(CaptureConfirmationSeconds)
		&& CaptureConfirmationSeconds >= 0.0
		&& FMath::IsFinite(ReleaseConfirmationSeconds)
		&& ReleaseConfirmationSeconds >= 0.0;
}

bool FABTSM11PrefixStabilizer::Initialize(
	const FABTSM11FinaleLayoutPreset& Preset,
	const FABTSM11FinaleLaunchInput& InitialInput,
	const FABTSM11PrefixStabilizerConfig& InConfig)
{
	if (!Preset.IsValid()
		|| !Preset.LaunchModel.Contains(InitialInput)
		|| !InConfig.IsValid())
	{
		return false;
	}
	LaunchModel = Preset.LaunchModel;
	ScanContract = Preset.ScanContract;
	TrustRegions = Preset.PrefixTrustRegions;
	Config = InConfig;
	bInitialized = true;
	Reset(InitialInput);
	return true;
}

void FABTSM11PrefixStabilizer::Reset(
	const FABTSM11FinaleLaunchInput& Input)
{
	DesiredInput = Input;
	ClampToLaunchDomain(DesiredInput);
	ControlledInput = DesiredInput;
	StablePrefixLevel = 0;
	NearPrefixLevel = 0;
	CaptureSeconds = 0.0;
	ReleaseSeconds = 0.0;
}

void FABTSM11PrefixStabilizer::CancelProtection()
{
	StablePrefixLevel = 0;
	NearPrefixLevel = 0;
	CaptureSeconds = 0.0;
	ReleaseSeconds = 0.0;
	ControlledInput = DesiredInput;
	ClampToLaunchDomain(ControlledInput);
}

void FABTSM11PrefixStabilizer::ApplyInputDelta(
	const double YawDeltaDegrees,
	const double PitchDeltaDegrees,
	const double PowerDelta)
{
	if (!bInitialized)
	{
		return;
	}
	const double Scale = GetSensitivityScale();
	DesiredInput.YawDegrees += YawDeltaDegrees * Scale;
	DesiredInput.PitchDegrees += PitchDeltaDegrees * Scale;
	DesiredInput.Power += PowerDelta * Scale;
	ClampToLaunchDomain(DesiredInput);
	RefreshControlledInput();
}

void FABTSM11PrefixStabilizer::Update(
	const double DeltaSeconds,
	const FABTSM11PrefixClassification& Classification)
{
	if (!bInitialized || DeltaSeconds < 0.0)
	{
		return;
	}

	if (StablePrefixLevel > 0)
	{
		const FABTSM11PrefixTrustRegion& StableRegion =
			TrustRegions[StablePrefixLevel - 1];
		const bool bOutsideRelease = !IsInsideExpandedRegion(
			DesiredInput,
			StableRegion,
			StableRegion.ReleaseMarginCells);
		const bool bLostAuthoritativePrefix =
			!Classification.IsF(StablePrefixLevel);
		if (bOutsideRelease || bLostAuthoritativePrefix)
		{
			ReleaseSeconds += DeltaSeconds;
			if (ReleaseSeconds >= Config.ReleaseConfirmationSeconds)
			{
				StablePrefixLevel = 0;
				NearPrefixLevel = 0;
				CaptureSeconds = 0.0;
				ReleaseSeconds = 0.0;
			}
		}
		else
		{
			ReleaseSeconds = 0.0;
		}
	}

	const int32 NextPrefix = StablePrefixLevel + 1;
	if (NextPrefix <= 3)
	{
		const FABTSM11PrefixTrustRegion& Region =
			TrustRegions[NextPrefix - 1];
		const bool bNear = IsInsideExpandedRegion(
			ControlledInput,
			Region,
			Region.CaptureMarginCells);
		if (bNear)
		{
			if (NearPrefixLevel != NextPrefix)
			{
				NearPrefixLevel = NextPrefix;
				CaptureSeconds = 0.0;
			}
			if (Region.Contains(ControlledInput)
				&& Classification.IsF(NextPrefix))
			{
				CaptureSeconds += DeltaSeconds;
				if (CaptureSeconds >= Config.CaptureConfirmationSeconds)
				{
					StablePrefixLevel = NextPrefix;
					NearPrefixLevel = 0;
					CaptureSeconds = 0.0;
					ReleaseSeconds = 0.0;
				}
			}
			else
			{
				CaptureSeconds = 0.0;
			}
		}
		else
		{
			NearPrefixLevel = 0;
			CaptureSeconds = 0.0;
		}
	}
	else
	{
		NearPrefixLevel = 0;
		CaptureSeconds = 0.0;
	}
	RefreshControlledInput();
}

EABTSM11PrefixStabilizerPhase
FABTSM11PrefixStabilizer::GetPhase() const
{
	if (NearPrefixLevel > 0)
	{
		return EABTSM11PrefixStabilizerPhase::Near;
	}
	return StablePrefixLevel > 0
		? EABTSM11PrefixStabilizerPhase::Stable
		: EABTSM11PrefixStabilizerPhase::Free;
}

double FABTSM11PrefixStabilizer::GetSensitivityScale() const
{
	return GetPhase() == EABTSM11PrefixStabilizerPhase::Free
		? 1.0
		: Config.NearSensitivityScale;
}

bool FABTSM11PrefixStabilizer::IsInsideExpandedRegion(
	const FABTSM11FinaleLaunchInput& Input,
	const FABTSM11PrefixTrustRegion& Region,
	const double MarginCells) const
{
	const double SafeMargin = FMath::Max(0.0, MarginCells);
	return Input.YawDegrees >= Region.Minimum.YawDegrees
			- SafeMargin * ScanContract.FinalYawPrecisionDegrees
		&& Input.YawDegrees <= Region.Maximum.YawDegrees
			+ SafeMargin * ScanContract.FinalYawPrecisionDegrees
		&& Input.PitchDegrees >= Region.Minimum.PitchDegrees
			- SafeMargin * ScanContract.FinalPitchPrecisionDegrees
		&& Input.PitchDegrees <= Region.Maximum.PitchDegrees
			+ SafeMargin * ScanContract.FinalPitchPrecisionDegrees
		&& Input.Power >= Region.Minimum.Power
			- SafeMargin * ScanContract.FinalPowerPrecision
		&& Input.Power <= Region.Maximum.Power
			+ SafeMargin * ScanContract.FinalPowerPrecision;
}

void FABTSM11PrefixStabilizer::ClampToLaunchDomain(
	FABTSM11FinaleLaunchInput& Input) const
{
	const FABTSM11FinaleLaunchInput Minimum{
		LaunchModel.MinimumYawDegrees,
		LaunchModel.MinimumPitchDegrees,
		LaunchModel.MinimumPower};
	const FABTSM11FinaleLaunchInput Maximum{
		LaunchModel.MaximumYawDegrees,
		LaunchModel.MaximumPitchDegrees,
		LaunchModel.MaximumPower};
	Input = ClampInput(Input, Minimum, Maximum);
}

void FABTSM11PrefixStabilizer::RefreshControlledInput()
{
	ControlledInput = DesiredInput;
	if (StablePrefixLevel > 0)
	{
		const FABTSM11PrefixTrustRegion& Region =
			TrustRegions[StablePrefixLevel - 1];
		ControlledInput = ClampInput(
			ControlledInput,
			Region.Minimum,
			Region.Maximum);
	}
	ClampToLaunchDomain(ControlledInput);
}

void FABTSM11PreviewTargetSelector::Reset()
{
	LatchedTarget = EABTSM11PreviewTarget::Assist1;
	PendingTarget = LatchedTarget;
	PendingSeconds = 0.0;
	bInitialized = false;
}

FABTSM11PreviewSelection FABTSM11PreviewTargetSelector::Update(
	const double DeltaSeconds,
	const FABTSM11FinaleLayoutPreset& Preset,
	const FABTSM11TrajectoryResult& Result,
	const FABTSM11PrefixClassification& Classification)
{
	const EABTSM11PreviewTarget Desired =
		DesiredPreviewTarget(Classification);
	if (!bInitialized)
	{
		LatchedTarget = Desired;
		PendingTarget = Desired;
		PendingSeconds = 0.0;
		bInitialized = true;
	}
	else if (Desired == LatchedTarget)
	{
		PendingTarget = Desired;
		PendingSeconds = 0.0;
	}
	else
	{
		if (PendingTarget != Desired)
		{
			PendingTarget = Desired;
			PendingSeconds = 0.0;
		}
		PendingSeconds += FMath::Max(0.0, DeltaSeconds);
		const bool bAdvancing =
			static_cast<uint8>(Desired) > static_cast<uint8>(LatchedTarget);
		const double RequiredHold = bAdvancing ? 0.20 : 0.35;
		if (PendingSeconds >= RequiredHold)
		{
			LatchedTarget = Desired;
			PendingSeconds = 0.0;
		}
	}
	return BuildSelection(LatchedTarget, Preset, Result);
}

bool FABTSM11FailurePresentationConfig::IsValid() const
{
	return FMath::IsFinite(ReadableHoldSeconds)
		&& ReadableHoldSeconds >= 0.0
		&& FMath::IsFinite(FadeToBlackSeconds)
		&& FadeToBlackSeconds > 0.0
		&& FMath::IsFinite(BlackHoldSeconds)
		&& BlackHoldSeconds >= 0.0
		&& FMath::IsFinite(FadeFromBlackSeconds)
		&& FadeFromBlackSeconds > 0.0;
}

bool FABTSM11FailurePresentationTimeline::Begin(
	const FABTSM11FailurePresentationConfig& InConfig)
{
	Reset();
	if (!InConfig.IsValid())
	{
		return false;
	}
	Config = InConfig;
	bStarted = true;
	return true;
}

void FABTSM11FailurePresentationTimeline::Reset()
{
	ElapsedSeconds = 0.0;
	bStarted = false;
	bRestoreWorldIssued = false;
}

void FABTSM11FailurePresentationTimeline::Advance(
	const double DeltaSeconds,
	bool& bOutShouldRestoreWorld)
{
	bOutShouldRestoreWorld = false;
	if (!bStarted
		|| !FMath::IsFinite(DeltaSeconds)
		|| DeltaSeconds < 0.0
		|| IsComplete())
	{
		return;
	}
	const double RestoreTime =
		Config.ReadableHoldSeconds
		+ Config.FadeToBlackSeconds;
	const double ProposedElapsed = ElapsedSeconds + DeltaSeconds;
	if (!bRestoreWorldIssued && ProposedElapsed >= RestoreTime)
	{
		// Clamp the crossing frame to full black. This guarantees at least
		// one rendered black restoration frame even after a large hitch,
		// rather than skipping directly to Ready with the failed world shown.
		ElapsedSeconds = RestoreTime;
		bRestoreWorldIssued = true;
		bOutShouldRestoreWorld = true;
		return;
	}
	ElapsedSeconds = ProposedElapsed;
}

bool FABTSM11FailurePresentationTimeline::IsActive() const
{
	return bStarted && !IsComplete();
}

bool FABTSM11FailurePresentationTimeline::IsComplete() const
{
	if (!bStarted)
	{
		return false;
	}
	const double CompleteTime =
		Config.ReadableHoldSeconds
		+ Config.FadeToBlackSeconds
		+ Config.BlackHoldSeconds
		+ Config.FadeFromBlackSeconds;
	return ElapsedSeconds >= CompleteTime;
}

double FABTSM11FailurePresentationTimeline::GetBlackoutAlpha() const
{
	if (!bStarted)
	{
		return 0.0;
	}
	const double FadeInStart = Config.ReadableHoldSeconds;
	const double BlackStart = FadeInStart + Config.FadeToBlackSeconds;
	const double FadeOutStart = BlackStart + Config.BlackHoldSeconds;
	if (ElapsedSeconds <= FadeInStart)
	{
		return 0.0;
	}
	if (ElapsedSeconds < BlackStart)
	{
		return FMath::Clamp(
			(ElapsedSeconds - FadeInStart)
				/ Config.FadeToBlackSeconds,
			0.0,
			1.0);
	}
	if (ElapsedSeconds <= FadeOutStart)
	{
		return 1.0;
	}
	return FMath::Clamp(
		1.0 - (ElapsedSeconds - FadeOutStart)
			/ Config.FadeFromBlackSeconds,
		0.0,
		1.0);
}

EABTSM11FailurePresentationPhase
FABTSM11FailurePresentationTimeline::GetPhase() const
{
	if (!bStarted)
	{
		return EABTSM11FailurePresentationPhase::Inactive;
	}
	const double FadeInStart = Config.ReadableHoldSeconds;
	const double BlackStart = FadeInStart + Config.FadeToBlackSeconds;
	const double FadeOutStart = BlackStart + Config.BlackHoldSeconds;
	const double CompleteTime =
		FadeOutStart + Config.FadeFromBlackSeconds;
	if (ElapsedSeconds < FadeInStart)
	{
		return EABTSM11FailurePresentationPhase::Hold;
	}
	if (ElapsedSeconds < BlackStart)
	{
		return EABTSM11FailurePresentationPhase::FadeToBlack;
	}
	if (ElapsedSeconds < FadeOutStart)
	{
		return EABTSM11FailurePresentationPhase::BlackHold;
	}
	if (ElapsedSeconds < CompleteTime)
	{
		return EABTSM11FailurePresentationPhase::FadeFromBlack;
	}
	return EABTSM11FailurePresentationPhase::Complete;
}

FVector3d ABTSM11ComputeAimPouchLocalPosition(
	const FABTSM11FinaleLaunchModel& LaunchModel,
	const FABTSM11FinaleLaunchInput& Input,
	const double MinimumPullDistanceCM,
	const double MaximumPullDistanceCM,
	const double MaximumPitchDropCM)
{
	if (!LaunchModel.Contains(Input)
		|| !FMath::IsFinite(MinimumPullDistanceCM)
		|| !FMath::IsFinite(MaximumPullDistanceCM)
		|| !FMath::IsFinite(MaximumPitchDropCM)
		|| MinimumPullDistanceCM < 0.0
		|| MaximumPullDistanceCM < MinimumPullDistanceCM
		|| MaximumPitchDropCM < 0.0)
	{
		return LaunchModel.PouchLocalPositionCM;
	}
	const FVector3d Direction = LaunchModel.MapDirection(Input);
	FVector3d Horizontal(Direction.X, Direction.Y, 0.0);
	Horizontal = Horizontal.GetSafeNormal();
	if (Horizontal.IsNearlyZero())
	{
		Horizontal = FVector3d::ForwardVector;
	}
	const double PowerAlpha = FMath::Clamp(
		(Input.Power - LaunchModel.MinimumPower)
			/ (LaunchModel.MaximumPower
				- LaunchModel.MinimumPower),
		0.0,
		1.0);
	const double PitchAlpha = FMath::Clamp(
		(Input.PitchDegrees - LaunchModel.MinimumPitchDegrees)
			/ (LaunchModel.MaximumPitchDegrees
				- LaunchModel.MinimumPitchDegrees),
		0.0,
		1.0);
	return LaunchModel.PouchLocalPositionCM
		- Horizontal * FMath::Lerp(
			MinimumPullDistanceCM,
			MaximumPullDistanceCM,
			PowerAlpha)
		- FVector3d::UpVector * (MaximumPitchDropCM * PitchAlpha);
}

double ABTSM11ResolveFailurePresentationEndTime(
	const FABTSM11FinaleLayoutPreset& Preset,
	const FABTSM11TrajectoryResult& Result,
	const FABTSM11PlaybackPlan& Plan,
	const double BirdClearanceCM)
{
	if (Result.Termination
			!= EABTSM11TrajectoryTermination::BodyCollision
		|| Plan.Points.Num() < 2
		|| !FMath::IsFinite(BirdClearanceCM)
		|| BirdClearanceCM < 0.0)
	{
		return Plan.DurationSeconds;
	}
	const FABTSM11TrajectoryEvent* Collision =
		Result.FindFirstEvent(EABTSM11TrajectoryEventType::BodyCollision);
	if (Collision == nullptr)
	{
		return Plan.DurationSeconds;
	}
	const FABTSM11GravityBodySpec* HitBody = nullptr;
	for (const FABTSM11GravityBodySpec& Body
		: Preset.CanonicalScenario.Bodies)
	{
		if (Body.BodyId == Collision->BodyId)
		{
			HitBody = &Body;
			break;
		}
	}
	if (HitBody == nullptr)
	{
		return Plan.DurationSeconds;
	}
	const double SafeRadius = FMath::Max(
		HitBody->VisualRadiusCM,
		HitBody->CollisionRadiusCM) + BirdClearanceCM;
	const double SafeRadiusSquared = FMath::Square(SafeRadius);
	const auto SampleSegmentPosition = [](
		const FABTSM11PlaybackPoint& Start,
		const FABTSM11PlaybackPoint& End,
		const double Alpha)
	{
		const double Duration =
			End.TimeSeconds - Start.TimeSeconds;
		if (Duration <= UE_DOUBLE_SMALL_NUMBER)
		{
			return End.PositionCM;
		}
		const double Alpha2 = Alpha * Alpha;
		const double Alpha3 = Alpha2 * Alpha;
		const double H00 = 2.0 * Alpha3 - 3.0 * Alpha2 + 1.0;
		const double H10 = Alpha3 - 2.0 * Alpha2 + Alpha;
		const double H01 = -2.0 * Alpha3 + 3.0 * Alpha2;
		const double H11 = Alpha3 - Alpha2;
		return Start.PositionCM * H00
			+ Start.VelocityCMPerSec * (Duration * H10)
			+ End.PositionCM * H01
			+ End.VelocityCMPerSec * (Duration * H11);
	};
	for (int32 Index = 1; Index < Plan.Points.Num(); ++Index)
	{
		const FABTSM11PlaybackPoint& Before = Plan.Points[Index - 1];
		const FABTSM11PlaybackPoint& After = Plan.Points[Index];
		if ((Before.PositionCM - HitBody->CenterCM).SquaredLength()
			<= SafeRadiusSquared)
		{
			return Before.TimeSeconds;
		}
		constexpr int32 SegmentProbeCount = 8;
		double PreviousAlpha = 0.0;
		bool bFoundEntryBracket = false;
		double EntryAlpha = 1.0;
		for (int32 Probe = 1; Probe <= SegmentProbeCount; ++Probe)
		{
			const double Alpha =
				static_cast<double>(Probe)
				/ static_cast<double>(SegmentProbeCount);
			const FVector3d Position =
				SampleSegmentPosition(Before, After, Alpha);
			if ((Position - HitBody->CenterCM).SquaredLength()
				<= SafeRadiusSquared)
			{
				bFoundEntryBracket = true;
				EntryAlpha = Alpha;
				break;
			}
			PreviousAlpha = Alpha;
		}
		if (!bFoundEntryBracket)
		{
			continue;
		}
		double OutsideAlpha = PreviousAlpha;
		double InsideAlpha = EntryAlpha;
		for (int32 Iteration = 0; Iteration < 48; ++Iteration)
		{
			const double MidAlpha =
				(OutsideAlpha + InsideAlpha) * 0.5;
			const FVector3d MidPosition =
				SampleSegmentPosition(Before, After, MidAlpha);
			if ((MidPosition - HitBody->CenterCM).SquaredLength()
				<= SafeRadiusSquared)
			{
				InsideAlpha = MidAlpha;
			}
			else
			{
				OutsideAlpha = MidAlpha;
			}
		}
		return FMath::Lerp(
			Before.TimeSeconds,
			After.TimeSeconds,
			OutsideAlpha);
	}
	return Plan.DurationSeconds;
}

bool ABTSM11ClipDiagramSegmentToUnitCircle(
	FVector2d& InOutStart,
	FVector2d& InOutEnd)
{
	const FVector2d Direction = InOutEnd - InOutStart;
	const double A = Direction.SquaredLength();
	if (A <= UE_DOUBLE_SMALL_NUMBER)
	{
		return InOutStart.SquaredLength() <= 1.0;
	}
	const double B = 2.0 * InOutStart.Dot(Direction);
	const double C = InOutStart.SquaredLength() - 1.0;
	const double Discriminant = B * B - 4.0 * A * C;
	double MinimumAlpha = 0.0;
	double MaximumAlpha = 1.0;
	if (C > 0.0)
	{
		if (Discriminant < 0.0)
		{
			return false;
		}
		const double Root = FMath::Sqrt(Discriminant);
		const double T0 = (-B - Root) / (2.0 * A);
		const double T1 = (-B + Root) / (2.0 * A);
		MinimumAlpha = FMath::Max(MinimumAlpha, FMath::Min(T0, T1));
		MaximumAlpha = FMath::Min(MaximumAlpha, FMath::Max(T0, T1));
		if (MaximumAlpha < MinimumAlpha)
		{
			return false;
		}
	}
	else if (InOutEnd.SquaredLength() > 1.0)
	{
		if (Discriminant < 0.0)
		{
			return false;
		}
		const double Root = FMath::Sqrt(Discriminant);
		MaximumAlpha = FMath::Clamp(
			FMath::Max(
				(-B - Root) / (2.0 * A),
				(-B + Root) / (2.0 * A)),
			0.0,
			1.0);
	}
	const FVector2d OriginalStart = InOutStart;
	InOutStart = OriginalStart + Direction * MinimumAlpha;
	InOutEnd = OriginalStart + Direction * MaximumAlpha;
	return true;
}

EABTSM11FailureReason ABTSM11ClassifyFailure(
	const FABTSM11TrajectoryResult& Result,
	const FABTSM11PrefixClassification& Classification)
{
	if (Classification.IsF(4))
	{
		return EABTSM11FailureReason::None;
	}
	switch (Result.Termination)
	{
	case EABTSM11TrajectoryTermination::BodyCollision:
		return EABTSM11FailureReason::BodyCollision;
	case EABTSM11TrajectoryTermination::WrongOrder:
		return EABTSM11FailureReason::WrongOrder;
	case EABTSM11TrajectoryTermination::SolarCaptured:
		return EABTSM11FailureReason::SolarCaptured;
	case EABTSM11TrajectoryTermination::OutOfBounds:
		return EABTSM11FailureReason::OutOfBounds;
	case EABTSM11TrajectoryTermination::Timeout:
		return EABTSM11FailureReason::Timeout;
	case EABTSM11TrajectoryTermination::AssistInvalidHyperbola:
	case EABTSM11TrajectoryTermination::PlanetCaptured:
	case EABTSM11TrajectoryTermination::AssistInvalidBPlaneBasis:
	case EABTSM11TrajectoryTermination::AssistSolveFailed:
	case EABTSM11TrajectoryTermination::InvalidInput:
		return EABTSM11FailureReason::SolverFailure;
	default:
		break;
	}
	if (!Classification.IsF(1))
	{
		return Result.FindAssistEvent(
			EABTSM11TrajectoryEventType::AssistEnter,
			1) == nullptr
			? EABTSM11FailureReason::MissAssist1
			: EABTSM11FailureReason::InvalidAssist1;
	}
	if (!Classification.IsF(2))
	{
		return Result.FindAssistEvent(
			EABTSM11TrajectoryEventType::AssistEnter,
			2) == nullptr
			? EABTSM11FailureReason::MissAssist2
			: EABTSM11FailureReason::InvalidAssist2;
	}
	if (!Classification.IsF(3))
	{
		return Result.FindAssistEvent(
			EABTSM11TrajectoryEventType::AssistEnter,
			3) == nullptr
			? EABTSM11FailureReason::MissAssist3
			: EABTSM11FailureReason::InvalidAssist3;
	}
	return EABTSM11FailureReason::MissUFO;
}

const TCHAR* ABTSM11FailureReasonLabel(
	const EABTSM11FailureReason Reason)
{
	switch (Reason)
	{
	case EABTSM11FailureReason::None: return TEXT("PATH QUALIFIED");
	case EABTSM11FailureReason::MissAssist1: return TEXT("MISSES PLANET 1");
	case EABTSM11FailureReason::InvalidAssist1: return TEXT("PLANET 1 PASS IS NOT USABLE");
	case EABTSM11FailureReason::MissAssist2: return TEXT("PLANET 1 EXIT MISSES PLANET 2");
	case EABTSM11FailureReason::InvalidAssist2: return TEXT("PLANET 2 PASS IS NOT USABLE");
	case EABTSM11FailureReason::MissAssist3: return TEXT("PLANET 2 EXIT MISSES PLANET 3");
	case EABTSM11FailureReason::InvalidAssist3: return TEXT("PLANET 3 PASS IS NOT USABLE");
	case EABTSM11FailureReason::BodyCollision: return TEXT("TRAJECTORY COLLIDES WITH A BODY");
	case EABTSM11FailureReason::WrongOrder: return TEXT("PLANETS ARE ENCOUNTERED OUT OF ORDER");
	case EABTSM11FailureReason::SolarCaptured: return TEXT("ENERGY REMAINS SUN-BOUND");
	case EABTSM11FailureReason::OutOfBounds: return TEXT("TRAJECTORY LEAVES THE FINALE DOMAIN");
	case EABTSM11FailureReason::Timeout: return TEXT("TRAJECTORY REMAINS BOUND");
	case EABTSM11FailureReason::MissUFO: return TEXT("THREE ASSISTS COMPLETE; UFO IS MISSED");
	default: return TEXT("TRAJECTORY CANNOT BE RESOLVED");
	}
}
