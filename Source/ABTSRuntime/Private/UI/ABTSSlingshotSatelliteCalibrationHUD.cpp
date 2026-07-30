// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/ABTSSlingshotSatelliteCalibrationHUD.h"

#include "Calibration/ABTSSlingshotSatelliteCalibrationRig.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Slingshot/ABTSM6SlingshotSystem.h"

namespace ABTSCalibrationHUDPrivate
{
	const TCHAR* TierLabel(const EABTSSlingshotTier Tier)
	{
		switch (Tier)
		{
		case EABTSSlingshotTier::Twig: return TEXT("Twig");
		case EABTSSlingshotTier::Simple: return TEXT("Simple");
		case EABTSSlingshotTier::Reinforced: return TEXT("Reinforced");
		case EABTSSlingshotTier::Space: return TEXT("Space");
		default: return TEXT("Unknown");
		}
	}
}

void AABTSSlingshotSatelliteCalibrationHUD::DrawHUD()
{
	Super::DrawHUD();
	if (Canvas == nullptr || GEngine == nullptr) return;
	AABTSSlingshotSatelliteCalibrationRig* Rig = FindCalibrationRig();
	if (Rig == nullptr) return;

	const float PanelWidth = FMath::Min(610.0f, Canvas->ClipX * 0.46f);
	const float PanelLeft = FMath::Max(0.0f, Canvas->ClipX - PanelWidth - 18.0f);
	const float PanelTop = 18.0f;
	const float PanelHeight = 294.0f;
	Canvas->K2_DrawTexture(
		Canvas->DefaultTexture,
		FVector2D(PanelLeft, PanelTop),
		FVector2D(PanelWidth, PanelHeight),
		FVector2D::ZeroVector,
		FVector2D::UnitVector,
		FLinearColor(0.01f, 0.018f, 0.032f, 0.90f));
	Canvas->K2_DrawBox(
		FVector2D(PanelLeft, PanelTop),
		FVector2D(PanelWidth, PanelHeight),
		2.0f,
		Rig->GetSweepSummary().bPassed
			? FLinearColor(0.2f, 0.95f, 0.55f, 0.95f)
			: FLinearColor(1.0f, 0.35f, 0.2f, 0.95f));

	float Y = PanelTop + 9.0f;
	const float X = PanelLeft + 12.0f;
	const float LineHeight = 19.0f;
	const auto DrawLine = [&](const FString& Text, const FLinearColor& Color)
	{
		DrawText(
			Text,
			Color,
			X,
			Y,
			GEngine->GetSmallFont(),
			0.86f,
			false);
		Y += LineHeight;
	};

	DrawLine(
		TEXT("SLINGSHOT / SATELLITE CALIBRATION"),
		FLinearColor(0.78f, 0.92f, 1.0f, 1.0f));
	DrawLine(
		FString::Printf(
			TEXT("LaunchProfileHash=%llu  BaselineGravitySnapshotHash=%llu"),
			Rig->GetLaunchProfileHash(),
			Rig->GetGravitySnapshotHash()),
		FLinearColor::White);
	DrawLine(
		FString::Printf(
			TEXT("SatellitePracticePresetHash=%llu  Gravity=%s"),
			Rig->GetSatellitePracticePresetHash(),
			Rig->IsSatelliteGravityEnabled() ? TEXT("ON") : TEXT("OFF")),
		FLinearColor::White);

	for (const FABTSM6ReachEnvelope& Envelope : Rig->GetReachEnvelopes())
	{
		DrawLine(
			FString::Printf(
				TEXT("%-10s Comfortable=%6.1fm (%0.3fR)  Maximum=%6.1fm (%0.3fR)"),
				ABTSCalibrationHUDPrivate::TierLabel(Envelope.Tier),
				Envelope.ComfortableReachCM / 100.0f,
				Envelope.ComfortableReachPrimaryRadiusRatio,
				Envelope.MaximumReachCM / 100.0f,
				Envelope.MaximumReachPrimaryRadiusRatio),
			FLinearColor(0.76f, 0.86f, 1.0f, 1.0f));
	}

	const FABTSCalibrationSweepSummary& Sweep = Rig->GetSweepSummary();
	DrawLine(
		FString::Printf(
			TEXT("Sweep=%s Samples=%d OnHits=%d GravityOnly=%d Island=%d"),
			Sweep.bPassed ? TEXT("PASS") : TEXT("FAIL"),
			Sweep.ReinforcedSampleCount,
			Sweep.ReinforcedGravityOnHits,
			Sweep.GravityDependentHits,
			Sweep.LargestSuccessIslandSamples),
		Sweep.bPassed
			? FLinearColor(0.25f, 1.0f, 0.52f, 1.0f)
			: FLinearColor(1.0f, 0.35f, 0.22f, 1.0f));
	DrawLine(
		FString::Printf(
			TEXT("Success Pull=%0.3f..%0.3f AimInPlane=%0.1f..%0.1fcm  SimpleFull=%d OutsidePull=%d"),
			Sweep.SuccessPullMinimum,
			Sweep.SuccessPullMaximum,
			Sweep.SuccessAimInPlaneMinimumCM,
			Sweep.SuccessAimInPlaneMaximumCM,
			Sweep.SimpleFullPowerHits,
			Sweep.ReinforcedOutsideCertifiedPullHits),
		FLinearColor::White);

	FABTSM6LaunchCalibrationTelemetry ActiveTelemetry;
	FVector ActiveBirdLocation;
	bool bHasActiveLaunch = false;
	for (TActorIterator<AABTSM6SlingshotSystem> It(GetWorld()); It; ++It)
	{
		if (It->CopyActiveCalibrationLaunchSample(
			ActiveTelemetry, ActiveBirdLocation))
		{
			bHasActiveLaunch = true;
		}
		break;
	}
	if (bHasActiveLaunch)
	{
		DrawLine(
			FString::Printf(
				TEXT("ACTIVE #%d %s Pull=%0.3f Aim=(%0.0f,%0.0f,%0.0f) V0=%0.1f"),
				ActiveTelemetry.Sequence,
				ABTSCalibrationHUDPrivate::TierLabel(ActiveTelemetry.Tier),
				ActiveTelemetry.PullAlpha,
				ActiveTelemetry.AimPlaneOffsetCM.X,
				ActiveTelemetry.AimPlaneOffsetCM.Y,
				ActiveTelemetry.AimPlaneOffsetCM.Z,
				ActiveTelemetry.InitialSpeedCMPerSec),
			FLinearColor(1.0f, 0.88f, 0.25f, 1.0f));
	}
	else if (Rig->HasLastLaunchTelemetry())
	{
		const FABTSM6LaunchCalibrationTelemetry& Last =
			Rig->GetLastLaunchTelemetry();
		DrawLine(
			FString::Printf(
				TEXT("LAST #%d %s Pull=%0.3f V0=%0.1f Target=%s Hit=%d"),
				Last.Sequence,
				ABTSCalibrationHUDPrivate::TierLabel(Last.Tier),
				Last.PullAlpha,
				Last.InitialSpeedCMPerSec,
				*Last.HitTargetId.ToString(),
				Last.bHitTarget ? 1 : 0),
			FLinearColor(1.0f, 0.88f, 0.25f, 1.0f));
		DrawLine(
			FString::Printf(
				TEXT("Arc=%0.1fm Apex=%0.1fm Path=%0.1fm Flight=%0.2fs SatelliteFirst=%d"),
				Last.ActualLandingArcLengthCM / 100.0f,
				Last.ApexAltitudeAbovePrimaryCM / 100.0f,
				Last.ActualPathLengthCM / 100.0f,
				Last.FlightTimeSeconds,
				Last.bHitSatelliteBodyFirst ? 1 : 0),
			FLinearColor::White);
	}
	else
	{
		DrawLine(
			TEXT("Launch one of the three labelled slingshots to record actual telemetry."),
			FLinearColor(0.9f, 0.9f, 0.9f, 1.0f));
	}
	DrawLine(
		TEXT("Console: abts.Calibration.SatelliteGravity 0 / 1  (-1 restores preset)"),
		FLinearColor(0.62f, 0.78f, 0.9f, 1.0f));
}

AABTSSlingshotSatelliteCalibrationRig*
AABTSSlingshotSatelliteCalibrationHUD::FindCalibrationRig()
{
	if (CalibrationRig.IsValid()) return CalibrationRig.Get();
	for (TActorIterator<AABTSSlingshotSatelliteCalibrationRig> It(GetWorld()); It; ++It)
	{
		CalibrationRig = *It;
		break;
	}
	return CalibrationRig.Get();
}
