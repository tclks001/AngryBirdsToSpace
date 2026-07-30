// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM11GravityAssistSolver.h"

#include "World/ABTSM11GravityAssistCoreAdapter.h"

bool FABTSM11GravityAssistSolver::Solve(
	const FABTSM11TrajectoryRequest& Request,
	FABTSM11TrajectoryResult& OutResult,
	FString* OutFailure)
{
	const ABTS::M11Core::TrajectoryRequest CoreRequest =
		ABTSM11GravityAssistAdapter::ToCore(Request);
	ABTS::M11Core::TrajectoryResult CoreResult;
	std::string Failure;
	const bool Success = ABTS::M11Core::GravityAssistSolver::Solve(
		CoreRequest,
		CoreResult,
		&Failure);
	ABTSM11GravityAssistAdapter::FromCore(CoreResult, OutResult);
	if (!Success && OutFailure != nullptr)
	{
		*OutFailure = UTF8_TO_TCHAR(Failure.c_str());
	}
	return Success;
}

double FABTSM11GravityAssistSolver::ComputePrimarySpecificEnergy(
	const FABTSM11GravityBodySpec& Primary,
	const FVector3d& PositionCM,
	const FVector3d& VelocityCMPerSec)
{
	return ABTS::M11Core::GravityAssistSolver::
		ComputePrimarySpecificEnergy(
			ABTSM11GravityAssistAdapter::ToCore(Primary),
			ABTSM11GravityAssistAdapter::ToCore(PositionCM),
			ABTSM11GravityAssistAdapter::ToCore(VelocityCMPerSec));
}

bool FABTSM11GravityAssistSolver::SweptSphereFirstHit(
	const FVector3d& SegmentStartCM,
	const FVector3d& SegmentEndCM,
	const FVector3d& SphereCenterCM,
	const double SphereRadiusCM,
	double& OutAlpha)
{
	return ABTS::M11Core::GravityAssistSolver::SweptSphereFirstHit(
		ABTSM11GravityAssistAdapter::ToCore(SegmentStartCM),
		ABTSM11GravityAssistAdapter::ToCore(SegmentEndCM),
		ABTSM11GravityAssistAdapter::ToCore(SphereCenterCM),
		SphereRadiusCM,
		OutAlpha);
}
