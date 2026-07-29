// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM11GravityAssistTypes.h"

#include "World/ABTSM11GravityAssistCoreAdapter.h"

namespace ABTSM11GravityAssistTypesFacadeDetail
{
	bool CopyFailure(
		const bool Success,
		const std::string& Failure,
		FString* OutFailure)
	{
		if (!Success && OutFailure != nullptr)
		{
			*OutFailure = UTF8_TO_TCHAR(Failure.c_str());
		}
		return Success;
	}
}

bool FABTSM11GravityBodySpec::IsAssist() const
{
	return ABTSM11GravityAssistAdapter::ToCore(*this).IsAssist();
}

int32 FABTSM11GravityBodySpec::GetAssistIndex() const
{
	return ABTSM11GravityAssistAdapter::ToCore(
		*this).GetAssistIndex();
}

bool FABTSM11GravityBodySpec::IsValid(FString* OutFailure) const
{
	std::string Failure;
	return ABTSM11GravityAssistTypesFacadeDetail::CopyFailure(
		ABTSM11GravityAssistAdapter::ToCore(*this).IsValid(&Failure),
		Failure,
		OutFailure);
}

double FABTSM11TargetSpec::GetGeometricContactRadiusCM() const
{
	return ABTSM11GravityAssistAdapter::ToCore(
		*this).GetGeometricContactRadiusCM();
}

FVector3d FABTSM11TargetSpec::GetGeometricContactCenterCM() const
{
	return ABTSM11GravityAssistAdapter::FromCore(
		ABTSM11GravityAssistAdapter::ToCore(
			*this).GetGeometricContactCenterCM());
}

bool FABTSM11TargetSpec::IsValid(FString* OutFailure) const
{
	std::string Failure;
	return ABTSM11GravityAssistTypesFacadeDetail::CopyFailure(
		ABTSM11GravityAssistAdapter::ToCore(*this).IsValid(&Failure),
		Failure,
		OutFailure);
}

FABTSM11GravityScenario::FABTSM11GravityScenario()
{
	for (int32 BodyIndex = 0; BodyIndex < BodyCount; ++BodyIndex)
	{
		Bodies[BodyIndex].Role =
			static_cast<EABTSM110FinaleGravityRole>(BodyIndex);
	}
}

bool FABTSM11GravityScenario::IsValid(FString* OutFailure) const
{
	std::string Failure;
	return ABTSM11GravityAssistTypesFacadeDetail::CopyFailure(
		ABTSM11GravityAssistAdapter::ToCore(*this).IsValid(&Failure),
		Failure,
		OutFailure);
}

bool FABTSM11SolverConfig::IsGameplayAssistEnabled(
	const int32 AssistIndex) const
{
	return ABTSM11GravityAssistAdapter::ToCore(
		*this).IsGameplayAssistEnabled(AssistIndex);
}

FABTSM11SolverConfig FABTSM11SolverConfig::MakeV2()
{
	return ABTSM11GravityAssistAdapter::FromCore(
		ABTS::M11Core::SolverConfig::MakeV2());
}

bool FABTSM11SolverConfig::IsValid(FString* OutFailure) const
{
	std::string Failure;
	return ABTSM11GravityAssistTypesFacadeDetail::CopyFailure(
		ABTSM11GravityAssistAdapter::ToCore(*this).IsValid(&Failure),
		Failure,
		OutFailure);
}

bool FABTSM11TrajectoryRequest::IsValid(FString* OutFailure) const
{
	std::string Failure;
	return ABTSM11GravityAssistTypesFacadeDetail::CopyFailure(
		ABTSM11GravityAssistAdapter::ToCore(*this).IsValid(&Failure),
		Failure,
		OutFailure);
}

void FABTSM11TrajectoryResult::Reset()
{
	*this = FABTSM11TrajectoryResult();
}

const FABTSM11TrajectoryEvent*
FABTSM11TrajectoryResult::FindFirstEvent(
	const EABTSM11TrajectoryEventType Type) const
{
	return Events.FindByPredicate(
		[Type](const FABTSM11TrajectoryEvent& Event)
		{
			return Event.Type == Type;
		});
}

const FABTSM11TrajectoryEvent*
FABTSM11TrajectoryResult::FindAssistEvent(
	const EABTSM11TrajectoryEventType Type,
	const int32 AssistIndex) const
{
	return Events.FindByPredicate(
		[Type, AssistIndex](const FABTSM11TrajectoryEvent& Event)
		{
			return Event.Type == Type
				&& Event.AssistIndex == AssistIndex;
		});
}

FABTSM11TrajectoryPacingDiagnostics::
	FABTSM11TrajectoryPacingDiagnostics()
{
	for (FABTSM11AssistPhaseDiagnostics& Assist : Assists)
	{
		Assist = FABTSM11AssistPhaseDiagnostics();
	}
}

bool FABTSM11TrajectoryResult::BuildPacingDiagnostics(
	FABTSM11TrajectoryPacingDiagnostics& OutDiagnostics,
	FString* OutFailure) const
{
	ABTS::M11Core::TrajectoryPacingDiagnostics CoreDiagnostics;
	std::string Failure;
	const bool Success =
		ABTSM11GravityAssistAdapter::ToCore(*this)
			.BuildPacingDiagnostics(CoreDiagnostics, &Failure);
	OutDiagnostics =
		ABTSM11GravityAssistAdapter::FromCore(CoreDiagnostics);
	return ABTSM11GravityAssistTypesFacadeDetail::CopyFailure(
		Success,
		Failure,
		OutFailure);
}
