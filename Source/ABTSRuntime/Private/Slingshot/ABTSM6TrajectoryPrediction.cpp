// Copyright Epic Games, Inc. All Rights Reserved.

#include "Slingshot/ABTSM6SlingshotSystem.h"

#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Physics/ABTSSweptCollision.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "Terrain/ABTSM3Planet.h"
#include "World/ABTSM51WorldActors.h"
#include "World/ABTSM9GravityQuery.h"
#include "World/ABTSM9Satellite.h"

namespace
{
	constexpr float PrimarySurfaceGravityCMPerSec2 = 980.0f;
	constexpr float DepartureClearanceCM = 5.0f;
	constexpr int32 LandingBisectionIterations = 8;

	float QueryBirdClearance(
		const AABTSM3Planet& Planet,
		const FVector& BirdCenter,
		const float BirdCollisionRadiusCM)
	{
		const FVector CenterOffset = BirdCenter - Planet.GetPlanetCenterWorld();
		if (CenterOffset.IsNearlyZero()) return -BIG_NUMBER;
		return CenterOffset.Size()
			- Planet.GetSurfaceRadiusAtDirection(CenterOffset.GetSafeNormal())
			- BirdCollisionRadiusCM;
	}

	float PointSegmentDistance(
		const FVector& Point,
		const FVector& SegmentStart,
		const FVector& SegmentEnd)
	{
		const FVector Segment = SegmentEnd - SegmentStart;
		const double LengthSquared = Segment.SizeSquared();
		if (LengthSquared <= UE_DOUBLE_SMALL_NUMBER)
		{
			return FVector::Distance(Point, SegmentStart);
		}
		const double Alpha = FMath::Clamp(
			FVector::DotProduct(Point - SegmentStart, Segment)
				/ LengthSquared,
			0.0,
			1.0);
		return FVector::Distance(
			Point,
			SegmentStart + Segment * Alpha);
	}
}

bool AABTSM6SlingshotSystem::CopyCurrentTrajectoryPreview(
	FABTSM6TrajectoryPreview& OutPreview) const
{
	OutPreview = FABTSM6TrajectoryPreview();
	if (LaunchState != EABTSM6LaunchState::Pulling
		|| !ActiveCord.IsValid()
		|| !bCurrentTrajectoryPreviewValid)
	{
		return false;
	}
	OutPreview = CurrentTrajectoryPreview;
	return true;
}

void AABTSM6SlingshotSystem::ClearCurrentTrajectoryPreview()
{
	ClearTrajectoryVisualInstances();
	bCurrentTrajectoryPreviewValid = false;
	CurrentTrajectoryPreview = FABTSM6TrajectoryPreview();
	LastTrajectoryPreviewStart = FVector::ZeroVector;
	LastTrajectoryPreviewVelocity = FVector::ZeroVector;
	LastTrajectoryPreviewTier = EABTSSlingshotTier::Simple;
	LastTrajectoryPreviewGravityHash = 0;
}

void AABTSM6SlingshotSystem::RebuildCurrentTrajectoryPreview()
{
	if (LaunchState != EABTSM6LaunchState::Pulling
		|| !ActiveCord.IsValid()
		|| !LaunchedBird.IsValid()
		|| (!bPlanarTestMode && !Planet.IsValid()))
	{
		ClearCurrentTrajectoryPreview();
		return;
	}

	const FVector Start = LaunchedBird->GetActorLocation();
	const FVector InitialVelocity = ComputeLaunchVelocity();
	const EABTSSlingshotTier Tier = ActiveCord->GetSlingshotTier();
	const uint64 GravityHash = bPlanarTestMode
		? 0
		: ABTSM9Gravity::GetSatelliteGravitySnapshotHash(
			GetWorld(),
			Planet->GetPlanetCenterWorld());
	if (bCurrentTrajectoryPreviewValid
		&& LastTrajectoryPreviewTier == Tier
		&& LastTrajectoryPreviewGravityHash == GravityHash
		&& Start.Equals(LastTrajectoryPreviewStart, 0.01f)
		&& InitialVelocity.Equals(LastTrajectoryPreviewVelocity, 0.01f))
	{
		return;
	}

	FABTSM6TrajectoryPreview Candidate;
	Candidate.SlingshotTier = Tier;
	Candidate.InitialWorldLocation = Start;
	Candidate.InitialWorldVelocity = InitialVelocity;
	Candidate.GravitySnapshotHash = static_cast<int64>(GravityHash);
	AABTSM9Satellite* PracticeSatellite = nullptr;
	AActor* PracticeTarget = nullptr;
	FVector PracticeTargetHalfExtentCM = FVector::ZeroVector;
	const bool bHasPracticeTarget =
		CopySatellitePracticeTarget(
			PracticeSatellite,
			PracticeTarget,
			PracticeTargetHalfExtentCM);
	const int32 VisualSteps = FMath::Clamp(TrajectorySampleCount, 8, 128);
	const bool bNeedsDistantLanding = !bPlanarTestMode && Tier == EABTSSlingshotTier::Reinforced;
	const float StepSeconds = bHasPracticeTarget
		? FMath::Clamp(SatellitePracticePredictionStepSeconds, 0.01f, 0.2f)
		: FMath::Clamp(TrajectoryStepSeconds, 0.01f, 0.25f);
	const int32 SimulationSteps = bHasPracticeTarget
		? FMath::Max(
			VisualSteps,
			FMath::CeilToInt(
				FMath::Clamp(
					SatellitePracticePredictionMaximumFlightSeconds,
					2.0f,
					60.0f)
				/ StepSeconds))
		: (bNeedsDistantLanding
			? FMath::Max(
				VisualSteps,
				FMath::Clamp(
					ReinforcedLandingPredictionSampleCount,
					54,
					512))
			: VisualSteps);
	Candidate.WorldPoints.Reserve(SimulationSteps + 1);
	Candidate.WorldPoints.Add(Start);

	FVector Position = Start;
	FVector Velocity = InitialVelocity;
	const FVector Center = bPlanarTestMode ? FVector::ZeroVector : Planet->GetPlanetCenterWorld();
	const float Mu = bPlanarTestMode
		? 0.0f
		: PrimarySurfaceGravityCMPerSec2 * FMath::Square(Planet->GetPlanetRadiusCM());
	const float BirdCollisionRadiusCM =
		LaunchedBird->GetSlingshotTrajectoryCollisionRadiusCM();
	TArray<FABTSM9SatelliteBodySnapshot> SatelliteBodies;
	if (!bPlanarTestMode)
	{
		ABTSM9Gravity::GatherSatelliteBodySnapshots(
			GetWorld(),
			SatelliteBodies);
	}
	float PreviousClearanceCM = bPlanarTestMode
		? BIG_NUMBER
		: QueryBirdClearance(*Planet, Position, BirdCollisionRadiusCM);
	bool bDepartedSurface = PreviousClearanceCM >= DepartureClearanceCM;

	for (int32 StepIndex = 0; StepIndex < SimulationSteps; ++StepIndex)
	{
		const FVector ToCenter = Center - Position;
		const float Radius = FMath::Max(ToCenter.Size(), 1.0f);
		const FVector PrimaryGravity = bPlanarTestMode
			? -PlanarUp * PrimarySurfaceGravityCMPerSec2
			: ToCenter / Radius * (Mu / FMath::Square(Radius));
		const FVector SatelliteGravity = bPlanarTestMode
			? FVector::ZeroVector
			: ABTSM9Gravity::GetSatelliteAcceleration(GetWorld(), Position);
		const FVector Acceleration = PrimaryGravity + SatelliteGravity
			- Velocity * FMath::Max(0.0f, GetResolvedFlightAirDragPerSecond());
		Velocity += Acceleration * StepSeconds;
		const FVector NextPosition = Position + Velocity * StepSeconds;

		float SatelliteBodyAlpha = BIG_NUMBER;
		int32 HitSatelliteIndex = INDEX_NONE;
		const bool bSatelliteBodyHit =
			ABTSM9Gravity::FindFirstSatelliteBodyHit(
				SatelliteBodies,
				Position,
				NextPosition,
				BirdCollisionRadiusCM,
				SatelliteBodyAlpha,
				HitSatelliteIndex);
		float SatelliteTargetAlpha = BIG_NUMBER;
		const bool bSatelliteTargetHit =
			bHasPracticeTarget
			&& PracticeSatellite != nullptr
			&& PracticeTarget != nullptr
			&& ABTSSweptCollision::SegmentExpandedOrientedBoxFirstAlpha(
				Position,
				NextPosition,
				PracticeTarget->GetActorTransform(),
				PracticeTargetHalfExtentCM,
				BirdCollisionRadiusCM,
				SatelliteTargetAlpha);
		float PrimaryAlpha = BIG_NUMBER;
		FVector PrimaryImpactLocation = FVector::ZeroVector;
		float NextClearanceCM = BIG_NUMBER;
		bool bPrimaryHit = false;
		if (!bPlanarTestMode)
		{
			NextClearanceCM =
				QueryBirdClearance(
					*Planet,
					NextPosition,
					BirdCollisionRadiusCM);
			if (bDepartedSurface
				&& PreviousClearanceCM > 0.0f
				&& NextClearanceCM <= 0.0f)
			{
				float OutsideAlpha = 0.0f;
				float InsideAlpha = 1.0f;
				for (int32 Iteration = 0;
					Iteration < LandingBisectionIterations;
					++Iteration)
				{
					const float MidAlpha =
						(OutsideAlpha + InsideAlpha) * 0.5f;
					const FVector MidPoint =
						FMath::Lerp(
							Position,
							NextPosition,
							MidAlpha);
					if (QueryBirdClearance(
						*Planet,
						MidPoint,
						BirdCollisionRadiusCM) > 0.0f)
					{
						OutsideAlpha = MidAlpha;
					}
					else
					{
						InsideAlpha = MidAlpha;
					}
				}
				PrimaryAlpha = InsideAlpha;
				PrimaryImpactLocation =
					FMath::Lerp(
						Position,
						NextPosition,
						PrimaryAlpha);
				bPrimaryHit = true;
			}
		}

		const float FirstAlpha = FMath::Min3(
			bSatelliteTargetHit ? SatelliteTargetAlpha : BIG_NUMBER,
			bSatelliteBodyHit ? SatelliteBodyAlpha : BIG_NUMBER,
			bPrimaryHit ? PrimaryAlpha : BIG_NUMBER);
		const bool bHasTerminalHit = FirstAlpha < BIG_NUMBER;
		const bool bTargetWins =
			bSatelliteTargetHit
			&& SatelliteTargetAlpha <= FirstAlpha + KINDA_SMALL_NUMBER;
		const bool bSatelliteBodyWins =
			!bTargetWins
			&& bSatelliteBodyHit
			&& SatelliteBodyAlpha <= FirstAlpha + KINDA_SMALL_NUMBER;
		const FVector SegmentEnd = bHasTerminalHit
			? FMath::Lerp(Position, NextPosition, FirstAlpha)
			: NextPosition;
		Candidate.PredictedPathLengthCM +=
			FVector::Distance(Position, SegmentEnd);

		for (int32 SatelliteIndex = 0;
			SatelliteIndex < SatelliteBodies.Num();
			++SatelliteIndex)
		{
			const FABTSM9SatelliteBodySnapshot& Body =
				SatelliteBodies[SatelliteIndex];
			const float ClearanceCM =
				PointSegmentDistance(
					Body.CenterWorld,
					Position,
					SegmentEnd)
				- Body.RadiusCM
				- BirdCollisionRadiusCM;
			if (ClearanceCM >= Candidate.ClosestSatelliteClearanceCM)
			{
				continue;
			}
			Candidate.ClosestSatelliteClearanceCM = ClearanceCM;
			Candidate.EncounterSatelliteCenterWorld = Body.CenterWorld;
			Candidate.EncounterSatelliteRadiusCM = Body.RadiusCM;
		}
		if (Candidate.EncounterSatelliteRadiusCM > 0.0f
			&& Candidate.ClosestSatelliteClearanceCM
				<= Candidate.EncounterSatelliteRadiusCM * 2.0f)
		{
			Candidate.bHasSatelliteEncounter = true;
		}

		if (bHasTerminalHit)
		{
			Candidate.WorldPoints.Add(SegmentEnd);
			Candidate.TerminalWorldLocation = SegmentEnd;
			Candidate.TerminalWorldVelocity = Velocity;
			if (bTargetWins)
			{
				Candidate.TerminalType =
					EABTSM6TrajectoryTerminalType::SatelliteE5;
				if (PracticeSatellite != nullptr)
				{
					Candidate.EncounterSatelliteCenterWorld =
						PracticeSatellite->GetPlanetCenterWorld();
					Candidate.EncounterSatelliteRadiusCM =
						PracticeSatellite->GetPlanetRadiusCM();
				}
				Candidate.bHasSatelliteEncounter = true;
			}
			else if (bSatelliteBodyWins)
			{
				Candidate.TerminalType =
					EABTSM6TrajectoryTerminalType::SatelliteBody;
				if (SatelliteBodies.IsValidIndex(HitSatelliteIndex))
				{
					Candidate.EncounterSatelliteCenterWorld =
						SatelliteBodies[HitSatelliteIndex].CenterWorld;
					Candidate.EncounterSatelliteRadiusCM =
						SatelliteBodies[HitSatelliteIndex].RadiusCM;
				}
				Candidate.bHasSatelliteEncounter = true;
			}
			else
			{
				Candidate.TerminalType =
					EABTSM6TrajectoryTerminalType::PrimarySurface;
				const FVector LandingDirection =
					(PrimaryImpactLocation - Center).GetSafeNormal();
				FVector SurfacePosition;
				FVector SurfaceNormal;
				float SurfaceRadiusCM = 0.0f;
				int32 SurfaceCellId = INDEX_NONE;
				if (Planet->QuerySurface(
					LandingDirection, SurfacePosition, SurfaceNormal, SurfaceRadiusCM, SurfaceCellId))
				{
					Candidate.bHasPrimarySurfaceLanding = true;
					Candidate.PrimarySurfaceLandingWorld = SurfacePosition;
					Candidate.PrimarySurfaceLandingVelocity = Velocity;
					Candidate.LandingCellId = SurfaceCellId;
					Candidate.LandingTimeSeconds =
						(static_cast<float>(StepIndex) + FirstAlpha)
						* StepSeconds;
				}
			}
			break;
		}

		if (!bPlanarTestMode)
		{
			if (!bDepartedSurface
				&& NextClearanceCM >= DepartureClearanceCM)
			{
				bDepartedSurface = true;
			}
			PreviousClearanceCM = NextClearanceCM;
		}

		Candidate.WorldPoints.Add(NextPosition);
		Position = NextPosition;
	}

	CurrentTrajectoryPreview = MoveTemp(Candidate);
	LastTrajectoryPreviewStart = Start;
	LastTrajectoryPreviewVelocity = InitialVelocity;
	LastTrajectoryPreviewTier = Tier;
	LastTrajectoryPreviewGravityHash = GravityHash;
	bCurrentTrajectoryPreviewValid = true;
}
