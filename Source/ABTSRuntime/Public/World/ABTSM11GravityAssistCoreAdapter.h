// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "M11Core/ABTSM11CoreSolver.h"
#include "World/ABTSM11GravityAssistTypes.h"

/**
 * Lossless, field-by-field conversion seam between the portable M11 core and
 * the existing Unreal-facing contract. It is public so conformance tests can
 * prove adapter round trips without reaching private implementation details.
 */
namespace ABTSM11GravityAssistAdapter
{
	ABTSRUNTIME_API ABTS::M11Core::Vec3d ToCore(const FVector3d& Value);
	ABTSRUNTIME_API FVector3d FromCore(const ABTS::M11Core::Vec3d& Value);

	ABTSRUNTIME_API ABTS::M11Core::Color4f ToCore(const FLinearColor& Value);
	ABTSRUNTIME_API FLinearColor FromCore(const ABTS::M11Core::Color4f& Value);

	ABTSRUNTIME_API ABTS::M11Core::GravityBodySpec ToCore(
		const FABTSM11GravityBodySpec& Value);
	ABTSRUNTIME_API FABTSM11GravityBodySpec FromCore(
		const ABTS::M11Core::GravityBodySpec& Value);

	ABTSRUNTIME_API ABTS::M11Core::TargetSpec ToCore(
		const FABTSM11TargetSpec& Value);
	ABTSRUNTIME_API FABTSM11TargetSpec FromCore(
		const ABTS::M11Core::TargetSpec& Value);

	ABTSRUNTIME_API ABTS::M11Core::GravityScenario ToCore(
		const FABTSM11GravityScenario& Value);
	ABTSRUNTIME_API FABTSM11GravityScenario FromCore(
		const ABTS::M11Core::GravityScenario& Value);

	ABTSRUNTIME_API ABTS::M11Core::SolverConfig ToCore(
		const FABTSM11SolverConfig& Value);
	ABTSRUNTIME_API FABTSM11SolverConfig FromCore(
		const ABTS::M11Core::SolverConfig& Value);

	ABTSRUNTIME_API ABTS::M11Core::TrajectoryRequest ToCore(
		const FABTSM11TrajectoryRequest& Value);
	ABTSRUNTIME_API FABTSM11TrajectoryRequest FromCore(
		const ABTS::M11Core::TrajectoryRequest& Value);

	ABTSRUNTIME_API ABTS::M11Core::TrajectoryPoint ToCore(
		const FABTSM11TrajectoryPoint& Value);
	ABTSRUNTIME_API FABTSM11TrajectoryPoint FromCore(
		const ABTS::M11Core::TrajectoryPoint& Value);

	ABTSRUNTIME_API ABTS::M11Core::TrajectoryEvent ToCore(
		const FABTSM11TrajectoryEvent& Value);
	ABTSRUNTIME_API FABTSM11TrajectoryEvent FromCore(
		const ABTS::M11Core::TrajectoryEvent& Value);

	ABTSRUNTIME_API ABTS::M11Core::AssistPhaseDiagnostics ToCore(
		const FABTSM11AssistPhaseDiagnostics& Value);
	ABTSRUNTIME_API FABTSM11AssistPhaseDiagnostics FromCore(
		const ABTS::M11Core::AssistPhaseDiagnostics& Value);

	ABTSRUNTIME_API ABTS::M11Core::TrajectoryPacingDiagnostics ToCore(
		const FABTSM11TrajectoryPacingDiagnostics& Value);
	ABTSRUNTIME_API FABTSM11TrajectoryPacingDiagnostics FromCore(
		const ABTS::M11Core::TrajectoryPacingDiagnostics& Value);

	ABTSRUNTIME_API ABTS::M11Core::TrajectoryResult ToCore(
		const FABTSM11TrajectoryResult& Value);
	ABTSRUNTIME_API FABTSM11TrajectoryResult FromCore(
		const ABTS::M11Core::TrajectoryResult& Value);

	ABTSRUNTIME_API void FromCore(
		const ABTS::M11Core::TrajectoryResult& Value,
		FABTSM11TrajectoryResult& OutValue);
}
