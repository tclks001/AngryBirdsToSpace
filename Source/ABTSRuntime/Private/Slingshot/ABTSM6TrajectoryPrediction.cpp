// Copyright Epic Games, Inc. All Rights Reserved.

#include "Slingshot/ABTSM6SlingshotSystem.h"

#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "Terrain/ABTSM3Planet.h"
#include "World/ABTSM51WorldActors.h"
#include "World/ABTSM9GravityQuery.h"

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
	const int32 VisualSteps = FMath::Clamp(TrajectorySampleCount, 8, 128);
	const bool bNeedsDistantLanding = !bPlanarTestMode && Tier == EABTSSlingshotTier::Reinforced;
	const int32 SimulationSteps = bNeedsDistantLanding
		? FMath::Max(VisualSteps, FMath::Clamp(ReinforcedLandingPredictionSampleCount, 54, 512))
		: VisualSteps;
	Candidate.WorldPoints.Reserve(SimulationSteps + 1);
	Candidate.WorldPoints.Add(Start);

	FVector Position = Start;
	FVector Velocity = InitialVelocity;
	const float StepSeconds = FMath::Clamp(TrajectoryStepSeconds, 0.01f, 0.25f);
	const FVector Center = bPlanarTestMode ? FVector::ZeroVector : Planet->GetPlanetCenterWorld();
	const float Mu = bPlanarTestMode
		? 0.0f
		: PrimarySurfaceGravityCMPerSec2 * FMath::Square(Planet->GetPlanetRadiusCM());
	const UPrimitiveComponent* BirdPhysicsBody = LaunchedBird->GetChaosPhysicsBody();
	const float BirdCollisionRadiusCM = LaunchedBird->GetSelectedMovementMode() == EABTSBirdMovementMode::ChaosRigidBody
		&& BirdPhysicsBody != nullptr
		? BirdPhysicsBody->Bounds.SphereRadius
		: (LaunchedBird->GetCapsuleComponent()
			? LaunchedBird->GetCapsuleComponent()->GetScaledCapsuleRadius()
			: 0.0f);
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
		Candidate.PredictedPathLengthCM += FVector::Distance(Position, NextPosition);

		if (!bPlanarTestMode)
		{
			const float NextClearanceCM = QueryBirdClearance(*Planet, NextPosition, BirdCollisionRadiusCM);
			if (!bDepartedSurface && NextClearanceCM >= DepartureClearanceCM)
			{
				bDepartedSurface = true;
			}
			if (bDepartedSurface && PreviousClearanceCM > 0.0f && NextClearanceCM <= 0.0f)
			{
				FVector OutsidePoint = Position;
				FVector InsidePoint = NextPosition;
				for (int32 Iteration = 0; Iteration < LandingBisectionIterations; ++Iteration)
				{
					const FVector MidPoint = (OutsidePoint + InsidePoint) * 0.5f;
					if (QueryBirdClearance(*Planet, MidPoint, BirdCollisionRadiusCM) > 0.0f)
					{
						OutsidePoint = MidPoint;
					}
					else
					{
						InsidePoint = MidPoint;
					}
				}

				Candidate.WorldPoints.Add(InsidePoint);
				const FVector LandingDirection = (InsidePoint - Center).GetSafeNormal();
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
					Candidate.LandingTimeSeconds = static_cast<float>(StepIndex + 1) * StepSeconds;
				}
				break;
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
