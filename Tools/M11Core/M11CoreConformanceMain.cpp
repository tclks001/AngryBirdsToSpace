// Copyright Epic Games, Inc. All Rights Reserved.

#include "M11Core/ABTSM11CoreConformance.h"
#include "ABTSM11CoreToolBuildIdentity.generated.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

namespace
{
	std::string EscapeJsonString(const std::string& Value)
	{
		std::string Result;
		for (const unsigned char Character : Value)
		{
			switch (Character)
			{
			case '\\':
				Result += "\\\\";
				break;
			case '"':
				Result += "\\\"";
				break;
			case '\n':
				Result += "\\n";
				break;
			case '\r':
				Result += "\\r";
				break;
			case '\t':
				Result += "\\t";
				break;
			default:
				if (Character < 0x20)
				{
					char Buffer[7] = {};
					std::snprintf(
						Buffer,
						sizeof(Buffer),
						"\\u%04x",
						static_cast<unsigned int>(Character));
					Result += Buffer;
				}
				else
				{
					Result += static_cast<char>(Character);
				}
				break;
			}
		}
		return Result;
	}

	std::string ResolveExecutablePath(const char* ArgumentZero)
	{
		if (ArgumentZero == nullptr || ArgumentZero[0] == '\0')
		{
			return {};
		}

		std::error_code Error;
		std::filesystem::path Result =
			std::filesystem::absolute(ArgumentZero, Error);
		if (Error)
		{
			return ArgumentZero;
		}
		const std::filesystem::path Canonical =
			std::filesystem::weakly_canonical(Result, Error);
		return (Error ? Result : Canonical).string();
	}

	std::string BuildReplayCommand(const std::string& ExecutablePath)
	{
		std::string EscapedPath;
		for (const char Character : ExecutablePath)
		{
			if (Character == '"')
			{
				EscapedPath += "\\\"";
			}
			else
			{
				EscapedPath += Character;
			}
		}
		return "& \"" + EscapedPath + "\" --json";
	}
}

int main(const int ArgumentCount, const char* const* Arguments)
{
	ABTS::M11Core::Testing::ConformanceReport Report;
	std::string Failure;
	const bool bPassed =
		ABTS::M11Core::Testing::RunPortableConformance(Report, &Failure);
	const char* Diagnostic =
		bPassed ? Report.Diagnostic.c_str() : Failure.c_str();
	bool bJson = false;
	for (int ArgumentIndex = 1;
		ArgumentIndex < ArgumentCount;
		++ArgumentIndex)
	{
		bJson =
			bJson
			|| (Arguments[ArgumentIndex] != nullptr
				&& std::strcmp(
					Arguments[ArgumentIndex],
					"--json") == 0);
	}
	const std::string ExecutablePath =
		ResolveExecutablePath(
			ArgumentCount > 0 ? Arguments[0] : nullptr);
	const std::string ReplayCommand =
		BuildReplayCommand(ExecutablePath);

	if (bJson)
	{
		const std::string EscapedDiagnostic =
			EscapeJsonString(Diagnostic);
		const std::string EscapedExecutablePath =
			EscapeJsonString(ExecutablePath);
		const std::string EscapedReplayCommand =
			EscapeJsonString(ReplayCommand);
		std::printf(
			"{\"schema\":\"abts.m11_core.conformance.v2\","
			"\"passed\":%s,\"contract\":%d,"
			"\"sourceHash\":{\"schema\":\"%s\",\"version\":%d},"
			"\"tool\":{\"buildVersion\":\"%s\","
			"\"conformanceToolSourceHashSha256\":\"%s\","
			"\"conformanceToolSourceFileCount\":%llu,"
			"\"executablePath\":\"%s\","
			"\"replayCommand\":\"%s\"},"
			"\"compiler\":{\"id\":\"%s\",\"version\":\"%s\","
			"\"vcToolsVersion\":\"%s\",\"identity\":\"%s\","
			"\"architecture\":\"%s\","
			"\"cxxStandard\":\"%s\","
			"\"floatingPointMode\":\"%s\","
			"\"numericalCompileContract\":\"%s\"},"
			"\"core\":{\"productionCoreSourceHashSha256\":\"%s\","
			"\"productionCoreSourceFileCount\":%llu},"
			"\"v1\":{\"hash\":\"0x%016llx\",\"points\":%d,"
			"\"events\":%d,\"termination\":%d},"
			"\"v2\":{\"hash\":\"0x%016llx\",\"points\":%d,"
			"\"events\":%d,\"termination\":%d},",
			bPassed ? "true" : "false",
			Report.ContractVersion,
			ABTS::M11Core::ToolIdentity::SourceHashSchema,
			ABTS::M11Core::ToolIdentity::SourceHashSchemaVersion,
			ABTS::M11Core::ToolIdentity::ToolBuildVersion,
			ABTS::M11Core::ToolIdentity::
				ConformanceToolSourceHashSha256,
			static_cast<unsigned long long>(
				ABTS::M11Core::ToolIdentity::
					ConformanceToolSourceFileCount),
			EscapedExecutablePath.c_str(),
			EscapedReplayCommand.c_str(),
			ABTS::M11Core::ToolIdentity::CompilerId,
			ABTS::M11Core::ToolIdentity::CompilerVersion,
			ABTS::M11Core::ToolIdentity::VCToolsVersion,
			ABTS::M11Core::ToolIdentity::CompilerIdentity,
			ABTS::M11Core::ToolIdentity::Architecture,
			ABTS::M11Core::ToolIdentity::CxxStandard,
			ABTS::M11Core::ToolIdentity::FloatingPointMode,
			ABTS::M11Core::ToolIdentity::NumericalCompileContract,
			ABTS::M11Core::ToolIdentity::
				ProductionCoreSourceHashSha256,
			static_cast<unsigned long long>(
				ABTS::M11Core::ToolIdentity::
					ProductionCoreSourceFileCount),
			static_cast<unsigned long long>(Report.V1ValidationHash),
			Report.V1PointCount,
			Report.V1EventCount,
			static_cast<int>(Report.V1Termination),
			static_cast<unsigned long long>(Report.V2ValidationHash),
			Report.V2PointCount,
			Report.V2EventCount,
			static_cast<int>(Report.V2Termination));

		std::printf("\"cases\":[");
		for (std::size_t CaseIndex = 0;
			CaseIndex < Report.Cases.size();
			++CaseIndex)
		{
			const ABTS::M11Core::Testing::CorpusCaseReport& Case =
				Report.Cases[CaseIndex];
			const std::string EscapedName =
				EscapeJsonString(Case.Name);
			const std::string EscapedCaseDiagnostic =
				EscapeJsonString(Case.Diagnostic);
			std::printf(
				"%s{\"id\":%u,\"name\":\"%s\","
				"\"input\":{\"requestIdentity\":\"0x%016llx\"},"
				"\"result\":{\"hash\":\"0x%016llx\","
				"\"termination\":%d,\"points\":%d,\"events\":%d,"
				"\"completedAssistCount\":%d},"
				"\"expectedOutcomeMatch\":%s,"
				"\"repeatedResultMatch\":%s,"
				"\"parallelResultMatch\":%s,"
				"\"passed\":%s,\"diagnostic\":\"%s\"}",
				CaseIndex == 0 ? "" : ",",
				static_cast<unsigned int>(Case.Id),
				EscapedName.c_str(),
				static_cast<unsigned long long>(
					Case.RequestIdentity),
				static_cast<unsigned long long>(Case.ResultHash),
				static_cast<int>(Case.Termination),
				Case.PointCount,
				Case.EventCount,
				Case.CompletedAssistCount,
				Case.ExpectedOutcomeMatch ? "true" : "false",
				Case.RepeatedResultMatch ? "true" : "false",
				Case.ParallelResultMatch ? "true" : "false",
				Case.Passed ? "true" : "false",
				EscapedCaseDiagnostic.c_str());
		}
		std::printf(
			"],\"corpusAggregateHash\":\"0x%016llx\","
			"\"repeatedResultsMatch\":%s,"
			"\"parallelResultsMatch\":%s,"
			"\"allCaseExpectationsMatch\":%s,"
			"\"invalidInputFailsClosed\":%s,"
			"\"diagnostic\":\"%s\"}\n",
			static_cast<unsigned long long>(
				Report.CorpusAggregateHash),
			Report.RepeatedResultsMatch ? "true" : "false",
			Report.ParallelResultsMatch ? "true" : "false",
			Report.AllCaseExpectationsMatch ? "true" : "false",
			Report.InvalidInputFailsClosed ? "true" : "false",
			EscapedDiagnostic.c_str());
		return bPassed ? 0 : 1;
	}

	std::printf(
		"[ABTS][M11-A-v2.1][PortableConformance] "
		"Passed=%d Contract=%d "
		"V1Hash=0x%016llx V1Points=%d V1Events=%d V1Termination=%d "
		"V2Hash=0x%016llx V2Points=%d V2Events=%d V2Termination=%d "
		"CorpusAggregateHash=0x%016llx Repeated=%d Parallel=%d "
		"AllCaseExpectations=%d InvalidFailsClosed=%d "
		"ToolBuildVersion=%s "
		"CompilerIdentity=%s Architecture=%s CxxStandard=%s "
		"FloatingPointMode=%s NumericalContract=%s "
		"ProductionCoreSourceHash=%s ConformanceToolSourceHash=%s "
		"Executable=%s Replay=%s "
		"Diagnostic=%s\n",
		bPassed ? 1 : 0,
		Report.ContractVersion,
		static_cast<unsigned long long>(Report.V1ValidationHash),
		Report.V1PointCount,
		Report.V1EventCount,
		static_cast<int>(Report.V1Termination),
		static_cast<unsigned long long>(Report.V2ValidationHash),
		Report.V2PointCount,
		Report.V2EventCount,
		static_cast<int>(Report.V2Termination),
		static_cast<unsigned long long>(Report.CorpusAggregateHash),
		Report.RepeatedResultsMatch ? 1 : 0,
		Report.ParallelResultsMatch ? 1 : 0,
		Report.AllCaseExpectationsMatch ? 1 : 0,
		Report.InvalidInputFailsClosed ? 1 : 0,
		ABTS::M11Core::ToolIdentity::ToolBuildVersion,
		ABTS::M11Core::ToolIdentity::CompilerIdentity,
		ABTS::M11Core::ToolIdentity::Architecture,
		ABTS::M11Core::ToolIdentity::CxxStandard,
		ABTS::M11Core::ToolIdentity::FloatingPointMode,
		ABTS::M11Core::ToolIdentity::NumericalCompileContract,
		ABTS::M11Core::ToolIdentity::
			ProductionCoreSourceHashSha256,
		ABTS::M11Core::ToolIdentity::
			ConformanceToolSourceHashSha256,
		ExecutablePath.c_str(),
		ReplayCommand.c_str(),
		Diagnostic);
	for (const ABTS::M11Core::Testing::CorpusCaseReport& Case :
		Report.Cases)
	{
		std::printf(
			"[ABTS][M11-A-v2.1][PortableConformanceCase] "
			"Id=%u Name=%s RequestIdentity=0x%016llx "
			"ResultHash=0x%016llx Termination=%d Points=%d "
			"Events=%d CompletedAssists=%d Expected=%d "
			"Repeated=%d Parallel=%d Passed=%d Diagnostic=%s\n",
			static_cast<unsigned int>(Case.Id),
			Case.Name.c_str(),
			static_cast<unsigned long long>(Case.RequestIdentity),
			static_cast<unsigned long long>(Case.ResultHash),
			static_cast<int>(Case.Termination),
			Case.PointCount,
			Case.EventCount,
			Case.CompletedAssistCount,
			Case.ExpectedOutcomeMatch ? 1 : 0,
			Case.RepeatedResultMatch ? 1 : 0,
			Case.ParallelResultMatch ? 1 : 0,
			Case.Passed ? 1 : 0,
			Case.Diagnostic.c_str());
	}

	return bPassed ? 0 : 1;
}
