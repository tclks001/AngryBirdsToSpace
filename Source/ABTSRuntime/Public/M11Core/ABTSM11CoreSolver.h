// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "M11Core/ABTSM11CoreTypes.h"

#include <string>

namespace ABTS::M11Core
{
	class GravityAssistSolver final
	{
	public:
		[[nodiscard]] static bool Solve(
			const TrajectoryRequest& Request,
			TrajectoryResult& OutResult,
			std::string* OutFailure = nullptr);

		[[nodiscard]] static double ComputePrimarySpecificEnergy(
			const GravityBodySpec& Primary,
			const Vec3d& PositionCM,
			const Vec3d& VelocityCMPerSec);

		[[nodiscard]] static bool SweptSphereFirstHit(
			const Vec3d& SegmentStartCM,
			const Vec3d& SegmentEndCM,
			const Vec3d& SphereCenterCM,
			double SphereRadiusCM,
			double& OutAlpha);
	};
}
