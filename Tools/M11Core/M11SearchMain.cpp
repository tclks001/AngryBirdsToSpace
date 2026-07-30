// Copyright Epic Games, Inc. All Rights Reserved.

#include "ABTSM11CoreToolBuildIdentity.generated.h"
#include "M11Search/ABTSM11CandidateSearch.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using namespace ABTS::M11Core;
	using namespace ABTS::M11Search;
	namespace fs = std::filesystem;

	struct Options
	{
		bool SelfTest = false;
		bool DescribeContract = false;
		bool Merge = false;
		bool Json = false;
		bool Resume = false;
		fs::path InputRoot;
		fs::path OutputDirectory;
		std::uint64_t WorkItems = 0;
		std::uint32_t ShardIndex = 0;
		std::uint32_t ShardCount = 1;
		std::uint32_t Threads = 1;
		std::uint32_t TopK = 5;
		std::uint64_t Seed = 0x11b21001ull;
		std::uint64_t CheckpointEvery = 4;
	};

	bool ParseUnsigned(
		const std::string_view Text,
		std::uint64_t& OutValue)
	{
		if (Text.empty())
		{
			return false;
		}
		const char* Begin = Text.data();
		const char* End = Begin + Text.size();
		const auto Result = std::from_chars(Begin, End, OutValue);
		return Result.ec == std::errc() && Result.ptr == End;
	}

	bool ParseOptions(
		const int ArgumentCount,
		char** Arguments,
		Options& OutOptions,
		std::string& OutFailure)
	{
		if (ArgumentCount < 2)
		{
			OutFailure = "MissingCommand";
			return false;
		}
		const std::string Command = Arguments[1];
		if (Command == "--self-test")
		{
			OutOptions.SelfTest = true;
		}
		else if (Command == "--describe-contract")
		{
			OutOptions.DescribeContract = true;
		}
		else if (Command == "merge")
		{
			OutOptions.Merge = true;
		}
		else if (Command != "search")
		{
			OutFailure = "UnknownCommand";
			return false;
		}
		for (int Index = 2; Index < ArgumentCount; ++Index)
		{
			const std::string Argument = Arguments[Index];
			if (Argument == "--json")
			{
				OutOptions.Json = true;
				continue;
			}
			if (Argument == "--resume")
			{
				OutOptions.Resume = true;
				continue;
			}
			if (Index + 1 >= ArgumentCount)
			{
				OutFailure = "MissingOptionValue:" + Argument;
				return false;
			}
			const std::string Value = Arguments[++Index];
			std::uint64_t Parsed = 0;
			if (Argument == "--output")
			{
				OutOptions.OutputDirectory = fs::path(Value);
			}
			else if (Argument == "--input-root")
			{
				OutOptions.InputRoot = fs::path(Value);
			}
			else if (!ParseUnsigned(Value, Parsed))
			{
				OutFailure = "InvalidUnsignedOption:" + Argument;
				return false;
			}
			else if (Argument == "--work-items")
			{
				OutOptions.WorkItems = Parsed;
			}
			else if (Argument == "--shard-index"
				&& Parsed <= std::numeric_limits<std::uint32_t>::max())
			{
				OutOptions.ShardIndex =
					static_cast<std::uint32_t>(Parsed);
			}
			else if (Argument == "--shard-count"
				&& Parsed <= std::numeric_limits<std::uint32_t>::max())
			{
				OutOptions.ShardCount =
					static_cast<std::uint32_t>(Parsed);
			}
			else if (Argument == "--threads"
				&& Parsed <= std::numeric_limits<std::uint32_t>::max())
			{
				OutOptions.Threads =
					static_cast<std::uint32_t>(Parsed);
			}
			else if (Argument == "--top-k"
				&& Parsed <= std::numeric_limits<std::uint32_t>::max())
			{
				OutOptions.TopK =
					static_cast<std::uint32_t>(Parsed);
			}
			else if (Argument == "--seed")
			{
				OutOptions.Seed = Parsed;
			}
			else if (Argument == "--checkpoint-every")
			{
				OutOptions.CheckpointEvery = Parsed;
			}
			else
			{
				OutFailure = "UnknownOrOverflowedOption:" + Argument;
				return false;
			}
		}
		if (OutOptions.SelfTest || OutOptions.DescribeContract)
		{
			return true;
		}
		if (OutOptions.OutputDirectory.empty()
			|| !OutOptions.OutputDirectory.is_absolute()
			|| OutOptions.WorkItems == 0
			|| OutOptions.ShardCount == 0)
		{
			OutFailure = "SearchRequiresAbsoluteOutputAndPositiveCounts";
			return false;
		}
		if (OutOptions.Merge)
		{
			if (OutOptions.InputRoot.empty()
				|| !OutOptions.InputRoot.is_absolute())
			{
				OutFailure = "MergeRequiresAbsoluteInputRoot";
				return false;
			}
		}
		else if (OutOptions.CheckpointEvery == 0)
		{
			OutFailure = "SearchRequiresPositiveCheckpointInterval";
			return false;
		}
		return true;
	}

	std::string EscapeJson(const std::string_view Value)
	{
		std::ostringstream Result;
		for (const unsigned char Character : Value)
		{
			switch (Character)
			{
			case '"':
				Result << "\\\"";
				break;
			case '\\':
				Result << "\\\\";
				break;
			case '\b':
				Result << "\\b";
				break;
			case '\f':
				Result << "\\f";
				break;
			case '\n':
				Result << "\\n";
				break;
			case '\r':
				Result << "\\r";
				break;
			case '\t':
				Result << "\\t";
				break;
			default:
				if (Character < 0x20u)
				{
					Result << "\\u"
						<< std::hex << std::setw(4) << std::setfill('0')
						<< static_cast<unsigned int>(Character)
						<< std::dec << std::setfill(' ');
				}
				else
				{
					Result << static_cast<char>(Character);
				}
				break;
			}
		}
		return Result.str();
	}

	std::string Hex64(const std::uint64_t Value)
	{
		std::ostringstream Result;
		Result << "0x" << std::hex << std::setw(16)
			<< std::setfill('0') << Value;
		return Result.str();
	}

	std::uint64_t HashCanonicalJson(const std::string_view Json)
	{
		// Standard FNV-1a/64. The manifest records both this schema and the
		// canonical JSON bytes, so an auditor does not need project code to
		// verify the contract identity.
		std::uint64_t Hash = 14695981039346656037ull;
		for (const unsigned char Byte : Json)
		{
			Hash ^= static_cast<std::uint64_t>(Byte);
			Hash *= 1099511628211ull;
		}
		return Hash;
	}

	std::uint64_t HashFile(const fs::path& Path)
	{
		std::ifstream Stream(Path, std::ios::binary);
		std::uint64_t Hash = 1469598103934665603ull;
		char Buffer[65536];
		while (Stream)
		{
			Stream.read(Buffer, sizeof(Buffer));
			const std::streamsize Count = Stream.gcount();
			for (std::streamsize Index = 0; Index < Count; ++Index)
			{
				Hash ^= static_cast<std::uint8_t>(Buffer[Index]);
				Hash *= 1099511628211ull;
			}
		}
		return Hash;
	}

	bool WriteTextAtomically(
		const fs::path& Path,
		const std::string& Text,
		std::string& OutFailure)
	{
		const fs::path Temporary = Path.string() + ".tmp";
		{
			std::ofstream Stream(
				Temporary,
				std::ios::binary | std::ios::trunc);
			if (!Stream)
			{
				OutFailure = "OpenTemporaryFailed:" + Temporary.string();
				return false;
			}
			Stream.write(Text.data(), static_cast<std::streamsize>(Text.size()));
			Stream.flush();
			if (!Stream)
			{
				OutFailure = "WriteTemporaryFailed:" + Temporary.string();
				return false;
			}
		}
		std::error_code Error;
		if (fs::exists(Path, Error))
		{
			fs::remove(Path, Error);
			if (Error)
			{
				OutFailure = "RemoveOldAtomicTargetFailed:" + Error.message();
				return false;
			}
		}
		fs::rename(Temporary, Path, Error);
		if (Error)
		{
			OutFailure = "AtomicRenameFailed:" + Error.message();
			return false;
		}
		return true;
	}

	std::string EvaluationStateLine(const CandidateRecord& Candidate)
	{
		std::ostringstream Result;
		Result << Candidate.GlobalWorkIndex << '\t'
			<< static_cast<unsigned int>(Candidate.Status) << '\t'
			<< Hex64(Candidate.CandidateSourceHash) << '\t'
			<< Hex64(Candidate.NominalRequestHash) << '\t'
			<< Hex64(Candidate.NominalResultHash) << '\t'
			<< Hex64(Candidate.ScoreHash) << '\t'
			<< Candidate.SolverInvocationCount << '\t'
			<< Candidate.Rejection << '\n';
		return Result.str();
	}

	void WriteInputSetJson(
		std::ostream& Stream,
		const InputSetMetrics& Set,
		const std::size_t SetIndex)
	{
		Stream << "{\"set\":\"S" << (SetIndex + 1) << "\""
			<< ",\"fullDomainCount\":" << Set.FullDomainCount
			<< ",\"fullDomainRetentionRatio\":"
			<< Set.FullDomainRetentionRatio
			<< ",\"screenAimCount\":" << Set.ScreenAimCount
			<< ",\"screenAimRetentionRatio\":"
			<< Set.ScreenAimRetentionRatio
			<< ",\"screenAimRetentionCompliant\":"
			<< (Set.ScreenAimRetentionCompliant ? "true" : "false")
			<< ",\"conditionalProbeCount\":"
			<< Set.ConditionalProbeCount
			<< ",\"conditionalParentCount\":"
			<< Set.ConditionalParentCount
			<< ",\"conditionalMemberCount\":"
			<< Set.ConditionalMemberCount
			<< ",\"conditionalRetentionRatio\":"
			<< Set.ConditionalRetentionRatio
			<< ",\"screenAimHullEvidencePointCount\":"
			<< Set.ScreenAimHullEvidencePointCount
			<< ",\"screenAimHullAreaSquareDegrees\":"
			<< Set.ScreenAimHullAreaSquareDegrees
			<< ",\"screenAimHullYawSpanDegrees\":"
			<< Set.ScreenAimHullYawSpanDegrees
			<< ",\"screenAimHullPitchSpanDegrees\":"
			<< Set.ScreenAimHullPitchSpanDegrees
			<< ",\"screenAimHullNormalizedArea\":"
			<< Set.ScreenAimHullNormalizedArea
			<< ",\"screenAimHullCompactness\":"
			<< Set.ScreenAimHullCompactness
			<< ",\"screenAimHullContainsNominal\":"
			<< (Set.ScreenAimHullContainsNominal ? "true" : "false")
			<< ",\"screenAimHullCompliant\":"
			<< (Set.ScreenAimHullCompliant ? "true" : "false")
			<< ",\"screenAimHullYawPitch\":[";
		for (std::size_t PointIndex = 0;
			PointIndex < Set.ScreenAimHullYawPitch.size();
			++PointIndex)
		{
			if (PointIndex > 0)
			{
				Stream << ',';
			}
			Stream << '['
				<< Set.ScreenAimHullYawPitch[PointIndex].YawDegrees << ','
				<< Set.ScreenAimHullYawPitch[PointIndex].PitchDegrees << ']';
		}
		Stream << "]}";
	}

	std::string BuildSearchContractJson(
		const CandidateSearchContract& Contract);
	std::string BuildSamplingSemanticsJson(
		const CandidateSearchContract& Contract);

	std::string EvaluationJsonLine(
		const CandidateRecord& Candidate,
		const CandidateSearchContract& Contract)
	{
		const std::string ContractJson = BuildSearchContractJson(Contract);
		std::ostringstream Result;
		Result << std::setprecision(17)
			<< "{\"schema\":\"abts.m11b21.evaluation.v3\""
			<< ",\"searchSourceHashSha256\":\""
			<< ABTS::M11Core::ToolIdentity::SearchSourceHashSha256 << "\""
			<< ",\"contractHash\":\""
			<< Hex64(HashCanonicalJson(ContractJson)) << "\""
			<< ",\"globalWorkIndex\":" << Candidate.GlobalWorkIndex
			<< ",\"status\":\"" << ToString(Candidate.Status) << "\""
			<< ",\"candidateSourceHash\":\""
			<< Hex64(Candidate.CandidateSourceHash) << "\""
			<< ",\"nominalRequestHash\":\""
			<< Hex64(Candidate.NominalRequestHash) << "\""
			<< ",\"nominalResultHash\":\""
			<< Hex64(Candidate.NominalResultHash) << "\""
			<< ",\"scoreHash\":\"" << Hex64(Candidate.ScoreHash) << "\""
			<< ",\"solverInvocations\":" << Candidate.SolverInvocationCount
			<< ",\"totalFlightSeconds\":"
			<< Candidate.Metrics.TotalFlightTimeSeconds
			<< ",\"finalCoastSeconds\":"
			<< Candidate.Metrics.FinalCoastSeconds
			<< ",\"maximumCoastSeconds\":"
			<< Candidate.Metrics.MaximumCoastSeconds
			<< ",\"minimumLayoutTurnRadians\":"
			<< Candidate.Metrics.MinimumLayoutTurnRadians
			<< ",\"layoutTurnsRadians\":[";
		for (std::size_t Index = 0;
			Index < Candidate.Metrics.LayoutTurnsRadians.size();
			++Index)
		{
			if (Index > 0)
			{
				Result << ',';
			}
			Result << Candidate.Metrics.LayoutTurnsRadians[Index];
		}
		Result << ']'
			<< ",\"minimumTargetDistanceCM\":"
			<< Candidate.Metrics.MinimumTargetDistanceCM
			<< ",\"minimumReadableDeflectionRadians\":"
			<< Candidate.Metrics.MinimumReadableDeflectionRadians
			<< ",\"alternatingLateralTurnCount\":"
			<< Candidate.Metrics.AlternatingLateralTurnCount
			<< ",\"robustSurvivors\":"
			<< Candidate.Metrics.RobustSurvivorCount
			<< ",\"lowPowerCompletedAssistCount\":"
			<< Candidate.Metrics.LowPowerCompletedAssistCount
			<< ",\"assistDurations\":[";
		for (std::size_t Index = 0;
			Index < Candidate.Metrics.Assists.size();
			++Index)
		{
			if (Index > 0)
			{
				Result << ',';
			}
			Result << Candidate.Metrics.Assists[Index]
				.InfluenceDurationSeconds;
		}
		Result << "],\"assistDeflections\":[";
		for (std::size_t Index = 0;
			Index < Candidate.Metrics.Assists.size();
			++Index)
		{
			if (Index > 0)
			{
				Result << ',';
			}
			Result << Candidate.Metrics.Assists[Index]
				.ActualDeflectionRadians;
		}
		Result << "],\"assistSignedLateralTurns\":[";
		for (std::size_t Index = 0;
			Index < Candidate.Metrics.Assists.size();
			++Index)
		{
			if (Index > 0)
			{
				Result << ',';
			}
			Result << Candidate.Metrics.Assists[Index]
				.SignedLateralTurnRadians;
		}
		Result << "],\"assistTurnAxisProjection\":[";
		for (std::size_t Index = 0;
			Index < Candidate.Metrics.Assists.size();
			++Index)
		{
			if (Index > 0)
			{
				Result << ',';
			}
			Result << Candidate.Metrics.Assists[Index]
				.LateralTurnAxisProjection;
		}
		Result << "],\"assistEnergy\":[";
		for (std::size_t Index = 0;
			Index < Candidate.Metrics.Assists.size();
			++Index)
		{
			if (Index > 0)
			{
				Result << ',';
			}
			Result << Candidate.Metrics.Assists[Index]
				.AppliedEnergyGainCM2PerSec2;
		}
		Result << "],\"assistClearance\":[";
		for (std::size_t Index = 0;
			Index < Candidate.Metrics.Assists.size();
			++Index)
		{
			if (Index > 0)
			{
				Result << ',';
			}
			Result << Candidate.Metrics.Assists[Index]
				.CollisionClearanceCM;
		}
		Result << ']'
			<< ",\"fullDomainSampleCount\":"
			<< Candidate.Metrics.FullDomainSampleCount
			<< ",\"screenAimSampleCount\":"
			<< Candidate.Metrics.ScreenAimSampleCount
			<< ",\"solveFailures\":{\"fullDomain\":"
			<< Candidate.Metrics.FullDomainSolveFailureCount
			<< ",\"screenAim\":"
			<< Candidate.Metrics.ScreenAimSolveFailureCount
			<< ",\"conditional\":"
			<< Candidate.Metrics.ConditionalSolveFailureCount << '}'
			<< ",\"inputSets\":[";
		for (std::size_t Index = 0;
			Index < Candidate.Metrics.InputSets.size();
			++Index)
		{
			if (Index > 0)
			{
				Result << ',';
			}
			WriteInputSetJson(
				Result, Candidate.Metrics.InputSets[Index], Index);
		}
		Result << ']'
			<< ",\"softScores\":{\"prefixRetention\":"
			<< Candidate.Metrics.PrefixRetentionScore
			<< ",\"prefixHull\":" << Candidate.Metrics.PrefixHullScore
			<< ",\"deflectionReadability\":"
			<< Candidate.Metrics.DeflectionReadabilityScore
			<< ",\"alternation\":"
			<< Candidate.Metrics.AlternationScore
			<< ",\"pacing\":" << Candidate.Metrics.PacingScore
			<< ",\"total\":" << Candidate.Metrics.SoftScore << '}'
			<< ",\"rejection\":\""
			<< EscapeJson(Candidate.Rejection) << "\"}\n";
		return Result.str();
	}

	bool ParseHex64(const std::string_view Text, std::uint64_t& OutValue)
	{
		if (Text.size() != 18
			|| Text[0] != '0'
			|| (Text[1] != 'x' && Text[1] != 'X'))
		{
			return false;
		}
		const auto Result = std::from_chars(
			Text.data() + 2,
			Text.data() + Text.size(),
			OutValue,
			16);
		return Result.ec == std::errc()
			&& Result.ptr == Text.data() + Text.size();
	}

	std::vector<std::string> SplitTabs(const std::string& Line)
	{
		std::vector<std::string> Fields;
		std::size_t Start = 0;
		while (true)
		{
			const std::size_t End = Line.find('\t', Start);
			if (End == std::string::npos)
			{
				Fields.push_back(Line.substr(Start));
				break;
			}
			Fields.push_back(Line.substr(Start, End - Start));
			Start = End + 1;
		}
		return Fields;
	}

	bool ReadState(
		const fs::path& Path,
		std::vector<CandidateRecord>& OutRecords,
		std::string& OutFailure)
	{
		std::ifstream Stream(Path, std::ios::binary);
		if (!Stream)
		{
			OutFailure = "StateOpenFailed";
			return false;
		}
		std::string Line;
		while (std::getline(Stream, Line))
		{
			const std::vector<std::string> Fields = SplitTabs(Line);
			if (Fields.size() != 8)
			{
				OutFailure = "StateFieldCountMismatch";
				return false;
			}
			CandidateRecord Record;
			std::uint64_t Parsed = 0;
			if (!ParseUnsigned(Fields[0], Record.GlobalWorkIndex)
				|| !ParseUnsigned(Fields[1], Parsed)
				|| Parsed > static_cast<std::uint64_t>(
					EvaluationStatus::InternalError))
			{
				OutFailure = "StateIndexOrStatusInvalid";
				return false;
			}
			Record.Status = static_cast<EvaluationStatus>(Parsed);
			if (!ParseHex64(Fields[2], Record.CandidateSourceHash)
				|| !ParseHex64(Fields[3], Record.NominalRequestHash)
				|| !ParseHex64(Fields[4], Record.NominalResultHash)
				|| !ParseHex64(Fields[5], Record.ScoreHash)
				|| !ParseUnsigned(Fields[6], Parsed)
				|| Parsed > static_cast<std::uint64_t>(
					std::numeric_limits<std::int32_t>::max()))
			{
				OutFailure = "StateHashOrSolveCountInvalid";
				return false;
			}
			Record.SolverInvocationCount =
				static_cast<std::int32_t>(Parsed);
			Record.Rejection = Fields[7];
			OutRecords.push_back(std::move(Record));
		}
		if (!Stream.eof())
		{
			OutFailure = "StateReadFailed";
			return false;
		}
		return true;
	}

	std::optional<std::string> ExtractJsonString(
		const std::string& Json,
		const std::string& Key)
	{
		const std::string Prefix = "\"" + Key + "\":\"";
		const std::size_t Start = Json.find(Prefix);
		if (Start == std::string::npos)
		{
			return std::nullopt;
		}
		const std::size_t ValueStart = Start + Prefix.size();
		const std::size_t End = Json.find('"', ValueStart);
		if (End == std::string::npos)
		{
			return std::nullopt;
		}
		return Json.substr(ValueStart, End - ValueStart);
	}

	std::optional<std::uint64_t> ExtractJsonUnsigned(
		const std::string& Json,
		const std::string& Key)
	{
		const std::string Prefix = "\"" + Key + "\":";
		const std::size_t Start = Json.find(Prefix);
		if (Start == std::string::npos)
		{
			return std::nullopt;
		}
		const std::size_t ValueStart = Start + Prefix.size();
		std::size_t End = ValueStart;
		while (End < Json.size()
			&& Json[End] >= '0'
			&& Json[End] <= '9')
		{
			++End;
		}
		std::uint64_t Value = 0;
		if (End == ValueStart
			|| !ParseUnsigned(
				std::string_view(Json).substr(
					ValueStart, End - ValueStart),
				Value))
		{
			return std::nullopt;
		}
		return Value;
	}

	std::optional<double> ExtractJsonDouble(
		const std::string& Json,
		const std::string& Key)
	{
		const std::string Prefix = "\"" + Key + "\":";
		const std::size_t Start = Json.find(Prefix);
		if (Start == std::string::npos)
		{
			return std::nullopt;
		}
		const std::size_t ValueStart = Start + Prefix.size();
		std::size_t End = Json.find_first_of(",}", ValueStart);
		if (End == std::string::npos)
		{
			return std::nullopt;
		}
		while (End > ValueStart
			&& std::isspace(
				static_cast<unsigned char>(Json[End - 1])) != 0)
		{
			--End;
		}
		const std::string Text = Json.substr(ValueStart, End - ValueStart);
		char* ParseEnd = nullptr;
		const double Value = std::strtod(Text.c_str(), &ParseEnd);
		if (ParseEnd != Text.c_str() + Text.size()
			|| !std::isfinite(Value))
		{
			return std::nullopt;
		}
		return Value;
	}

	struct Checkpoint
	{
		std::uint64_t NextLocalOffset = 0;
		std::uint64_t StateBytes = 0;
		std::uint64_t StateHash = 0;
		std::uint64_t EvaluationBytes = 0;
		std::uint64_t EvaluationHash = 0;
		double ElapsedSeconds = 0.0;
	};

	std::string BuildCheckpointJson(
		const Options& OptionsValue,
		const CandidateSearchContract& Contract,
		const Checkpoint& Value)
	{
		const std::string ContractJson = BuildSearchContractJson(Contract);
		std::ostringstream Result;
		Result << std::setprecision(17)
			<< "{\n"
			<< "  \"schema\":\"abts.m11b21.checkpoint.v3\",\n"
			<< "  \"toolBuildVersion\":\""
			<< ABTS::M11Core::ToolIdentity::ToolBuildVersion << "\",\n"
			<< "  \"productionCoreSourceHashSha256\":\""
			<< ABTS::M11Core::ToolIdentity::
				ProductionCoreSourceHashSha256 << "\",\n"
			<< "  \"searchSourceHashSha256\":\""
			<< ABTS::M11Core::ToolIdentity::
				SearchSourceHashSha256 << "\",\n"
			<< "  \"contractHash\":\""
			<< Hex64(HashCanonicalJson(ContractJson)) << "\",\n"
			<< "  \"workItems\":" << OptionsValue.WorkItems << ",\n"
			<< "  \"shardIndex\":" << OptionsValue.ShardIndex << ",\n"
			<< "  \"shardCount\":" << OptionsValue.ShardCount << ",\n"
			<< "  \"seed\":" << OptionsValue.Seed << ",\n"
			<< "  \"nextLocalOffset\":" << Value.NextLocalOffset << ",\n"
			<< "  \"stateBytes\":" << Value.StateBytes << ",\n"
			<< "  \"stateHash\":\"" << Hex64(Value.StateHash) << "\",\n"
			<< "  \"evaluationBytes\":"
			<< Value.EvaluationBytes << ",\n"
			<< "  \"evaluationHash\":\""
			<< Hex64(Value.EvaluationHash) << "\",\n"
			<< "  \"elapsedSeconds\":" << Value.ElapsedSeconds << "\n"
			<< "}\n";
		return Result.str();
	}

	bool ReadCheckpoint(
		const fs::path& Path,
		const Options& OptionsValue,
		const CandidateSearchContract& Contract,
		Checkpoint& OutValue,
		std::string& OutFailure)
	{
		std::ifstream Stream(Path, std::ios::binary);
		if (!Stream)
		{
			OutFailure = "CheckpointOpenFailed";
			return false;
		}
		const std::string Json(
			(std::istreambuf_iterator<char>(Stream)),
			std::istreambuf_iterator<char>());
		const auto Schema = ExtractJsonString(Json, "schema");
		const auto Tool = ExtractJsonString(Json, "toolBuildVersion");
		const auto Core = ExtractJsonString(
			Json, "productionCoreSourceHashSha256");
		const auto Search = ExtractJsonString(
			Json, "searchSourceHashSha256");
		const auto ContractHash = ExtractJsonString(Json, "contractHash");
		const auto WorkItems = ExtractJsonUnsigned(Json, "workItems");
		const auto ShardIndex = ExtractJsonUnsigned(Json, "shardIndex");
		const auto ShardCount = ExtractJsonUnsigned(Json, "shardCount");
		const auto Seed = ExtractJsonUnsigned(Json, "seed");
		const auto Next = ExtractJsonUnsigned(Json, "nextLocalOffset");
		const auto StateBytes = ExtractJsonUnsigned(Json, "stateBytes");
		const auto StateHash = ExtractJsonString(Json, "stateHash");
		const auto EvaluationBytes =
			ExtractJsonUnsigned(Json, "evaluationBytes");
		const auto EvaluationHash =
			ExtractJsonString(Json, "evaluationHash");
		const auto Elapsed = ExtractJsonDouble(Json, "elapsedSeconds");
		const std::string ExpectedContractHash = Hex64(
			HashCanonicalJson(BuildSearchContractJson(Contract)));
		if (!Schema || !Tool || !Core || !Search || !ContractHash
			|| !WorkItems
			|| !ShardIndex || !ShardCount || !Seed || !Next
			|| !StateBytes || !StateHash || !EvaluationBytes
			|| !EvaluationHash || !Elapsed
			|| *Schema != "abts.m11b21.checkpoint.v3"
			|| *Tool
				!= ABTS::M11Core::ToolIdentity::ToolBuildVersion
			|| *Core
				!= ABTS::M11Core::ToolIdentity::
					ProductionCoreSourceHashSha256
			|| *Search
				!= ABTS::M11Core::ToolIdentity::
					SearchSourceHashSha256
			|| *ContractHash != ExpectedContractHash
			|| *WorkItems != OptionsValue.WorkItems
			|| *ShardIndex != OptionsValue.ShardIndex
			|| *ShardCount != OptionsValue.ShardCount
			|| *Seed != OptionsValue.Seed
			|| !ParseHex64(*StateHash, OutValue.StateHash)
			|| !ParseHex64(*EvaluationHash, OutValue.EvaluationHash))
		{
			OutFailure = "CheckpointIdentityMismatch";
			return false;
		}
		OutValue.NextLocalOffset = *Next;
		OutValue.StateBytes = *StateBytes;
		OutValue.EvaluationBytes = *EvaluationBytes;
		OutValue.ElapsedSeconds = *Elapsed;
		return true;
	}

	void WriteVectorJson(std::ostream& Stream, const Vec3d& Value)
	{
		Stream << '[' << Value.X << ',' << Value.Y << ',' << Value.Z << ']';
	}

	void WriteInputJson(std::ostream& Stream, const LaunchInput& Value)
	{
		Stream << "{\"yawDegrees\":" << Value.YawDegrees
			<< ",\"pitchDegrees\":" << Value.PitchDegrees
			<< ",\"power\":" << Value.Power << '}';
	}

	std::string BuildSearchContractJson(
		const CandidateSearchContract& Contract)
	{
		std::ostringstream Stream;
		Stream.imbue(std::locale::classic());
		Stream << std::setprecision(17)
			<< "{\"contractVersion\":" << Contract.ContractVersion
			<< ",\"algorithmVersion\":" << Contract.AlgorithmVersion
			<< ",\"searchSeed\":" << Contract.SearchSeed
			<< ",\"localTimeSampleCount\":"
			<< Contract.LocalTimeSampleCount
			<< ",\"localImpactSampleCount\":"
			<< Contract.LocalImpactSampleCount
			<< ",\"localRadialSampleCount\":"
			<< Contract.LocalRadialSampleCount
			<< ",\"localMomentumDirectionSampleCount\":"
			<< Contract.LocalMomentumDirectionSampleCount
			<< ",\"targetTimeSampleCount\":"
			<< Contract.TargetTimeSampleCount
			<< ",\"robustPreselectionWidth\":"
			<< Contract.RobustPreselectionWidth
			<< ",\"minimumRobustSurvivorCount\":"
			<< Contract.MinimumRobustSurvivorCount
			<< ",\"maximumTotalFlightTimeSeconds\":"
			<< Contract.MaximumTotalFlightTimeSeconds
			<< ",\"maximumCoastSeconds\":"
			<< Contract.MaximumCoastSeconds
			<< ",\"minimumInfluenceDurationSeconds\":"
			<< Contract.MinimumInfluenceDurationSeconds
			<< ",\"maximumInfluenceDurationSeconds\":"
			<< Contract.MaximumInfluenceDurationSeconds
			<< ",\"minimumDeflectionRadians\":"
			<< Contract.MinimumDeflectionRadians
			<< ",\"minimumEnergyGainCM2PerSec2\":"
			<< Contract.MinimumEnergyGainCM2PerSec2
			<< ",\"minimumCorridorQuality\":"
			<< Contract.MinimumCorridorQuality
			<< ",\"minimumLayoutTurnRadians\":"
			<< Contract.MinimumLayoutTurnRadians
			<< ",\"minimumLateralTurnAxisProjection\":"
			<< Contract.MinimumLateralTurnAxisProjection
			<< ",\"minimumBodyClearanceCM\":"
			<< Contract.MinimumBodyClearanceCM
			<< ",\"lowPowerProbe\":" << Contract.LowPowerProbe
			<< ",\"robustYawStepDegrees\":"
			<< Contract.RobustYawStepDegrees
			<< ",\"robustPitchStepDegrees\":"
			<< Contract.RobustPitchStepDegrees
			<< ",\"robustPowerStep\":" << Contract.RobustPowerStep
			<< ",\"monteCarloSeed\":" << Contract.MonteCarloSeed
			<< ",\"monteCarloSampleCount\":"
			<< Contract.MonteCarloSampleCount
			<< ",\"screenAimSeed\":" << Contract.ScreenAimSeed
			<< ",\"screenAimSampleCount\":"
			<< Contract.ScreenAimSampleCount
			<< ",\"minimumPrefixRetentionRatio\":"
			<< Contract.MinimumPrefixRetentionRatio
			<< ",\"maximumPrefixRetentionRatio\":"
			<< Contract.MaximumPrefixRetentionRatio
			<< ",\"fullScoreMinimumPrefixRetentionRatio\":"
			<< Contract.FullScoreMinimumPrefixRetentionRatio
			<< ",\"fullScoreMaximumPrefixRetentionRatio\":"
			<< Contract.FullScoreMaximumPrefixRetentionRatio
			<< ",\"conditionalProbeSamplesPerSet\":"
			<< Contract.ConditionalProbeSamplesPerSet
			<< ",\"conditionalYawHalfExtentDegrees\":"
			<< Contract.ConditionalYawHalfExtentDegrees
			<< ",\"conditionalPitchHalfExtentDegrees\":"
			<< Contract.ConditionalPitchHalfExtentDegrees
			<< ",\"conditionalPowerHalfExtent\":"
			<< Contract.ConditionalPowerHalfExtent
			<< ",\"minimumHullEvidenceCount\":"
			<< Contract.MinimumHullEvidenceCount
			<< ",\"minimumHullAreaSquareDegrees\":"
			<< Contract.MinimumHullAreaSquareDegrees
			<< ",\"minimumHullYawSpanDegrees\":"
			<< Contract.MinimumHullYawSpanDegrees
			<< ",\"minimumHullPitchSpanDegrees\":"
			<< Contract.MinimumHullPitchSpanDegrees
			<< ",\"firstEncounterMinimumSeconds\":"
			<< Contract.FirstEncounterMinimumSeconds
			<< ",\"firstEncounterMaximumSeconds\":"
			<< Contract.FirstEncounterMaximumSeconds
			<< ",\"interEncounterCoastMinimumSeconds\":"
			<< Contract.InterEncounterCoastMinimumSeconds
			<< ",\"interEncounterCoastMaximumSeconds\":"
			<< Contract.InterEncounterCoastMaximumSeconds
			<< ",\"targetCoastMinimumSeconds\":"
			<< Contract.TargetCoastMinimumSeconds
			<< ",\"targetCoastMaximumSeconds\":"
			<< Contract.TargetCoastMaximumSeconds
			<< ",\"minimumTargetHitRadiusCM\":"
			<< Contract.MinimumTargetHitRadiusCM
			<< ",\"maximumTargetHitRadiusCM\":"
			<< Contract.MaximumTargetHitRadiusCM
			<< ",\"targetCoverageMarginCM\":"
			<< Contract.TargetCoverageMarginCM
			<< ",\"minimumInfluenceRadiusCM\":"
			<< Contract.MinimumInfluenceRadiusCM
			<< ",\"maximumInfluenceRadiusCM\":"
			<< Contract.MaximumInfluenceRadiusCM
			<< ",\"minimumVirtualMomentumSpeedCMPerSec\":"
			<< Contract.MinimumVirtualMomentumSpeedCMPerSec
			<< ",\"maximumVirtualMomentumSpeedCMPerSec\":"
			<< Contract.MaximumVirtualMomentumSpeedCMPerSec
			<< ",\"minimumGravityScale\":"
			<< Contract.MinimumGravityScale
			<< ",\"maximumGravityScale\":"
			<< Contract.MaximumGravityScale
			<< ",\"minimumAlternatingLateralTurnCount\":"
			<< Contract.MinimumAlternatingLateralTurnCount
			<< ",\"requestedCandidateCount\":"
			<< Contract.RequestedCandidateCount
			<< ",\"minimumDiversityDistanceCM\":"
			<< Contract.MinimumDiversityDistanceCM << '}';
		return Stream.str();
	}

	std::string BuildSamplingSemanticsJson(
		const CandidateSearchContract& Contract)
	{
		std::ostringstream Stream;
		Stream.imbue(std::locale::classic());
		Stream << std::setprecision(17)
			<< "{\"screenAim\":{\"sampleCount\":"
			<< Contract.ScreenAimSampleCount
			<< ",\"seed\":" << Contract.ScreenAimSeed
			<< ",\"sequence\":\"fixed-seed-halton\""
			<< ",\"dimensions\":[\"yaw\",\"pitch\"]"
			<< ",\"power\":\"nominalInputPower\""
			<< ",\"authority\":\"candidate-ux-prefix-retention-and-hull\""
			<< ",\"acceptanceRatioRange\":["
			<< Contract.MinimumPrefixRetentionRatio << ','
			<< Contract.MaximumPrefixRetentionRatio << ']'
			<< ",\"fullScoreRatioRange\":["
			<< Contract.FullScoreMinimumPrefixRetentionRatio << ','
			<< Contract.FullScoreMaximumPrefixRetentionRatio << "]}"
			<< ",\"fullLaunchDomain\":{\"sampleCount\":"
			<< Contract.MonteCarloSampleCount
			<< ",\"seed\":" << Contract.MonteCarloSeed
			<< ",\"sequence\":\"fixed-seed-halton\""
			<< ",\"dimensions\":[\"yaw\",\"pitch\",\"power\"]"
			<< ",\"authority\":\"candidate-diagnostic-only\"}"
			<< ",\"conditional\":{\"samplesPerSet\":"
			<< Contract.ConditionalProbeSamplesPerSet
			<< ",\"authority\":\"local-evidence-only\""
			<< ",\"mergedIntoUnbiasedSets\":false}"
			<< ",\"exhaustiveCertification\":false}";
		return Stream.str();
	}

	std::string BuildContractDescriptorJson(
		const CandidateSearchContract& Contract)
	{
		const std::string ContractJson = BuildSearchContractJson(Contract);
		std::ostringstream Stream;
		Stream << "{\"schema\":\"abts.m11b21.contract_descriptor.v1\""
			<< ",\"authority\":\"ABTSM11SearchCLI\""
			<< ",\"contractHash\":\""
			<< Hex64(HashCanonicalJson(ContractJson)) << "\""
			<< ",\"contractHashSchema\":"
			<< "\"fnv1a64-canonical-contract-json-utf8-v1\""
			<< ",\"searchSourceHashSha256\":\""
			<< ABTS::M11Core::ToolIdentity::SearchSourceHashSha256
			<< "\",\"samplingSemantics\":"
			<< BuildSamplingSemanticsJson(Contract)
			<< ",\"searchContract\":" << ContractJson << '}';
		return Stream.str();
	}

	void WriteColorJson(
		std::ostream& Stream,
		const Color4f& Value)
	{
		Stream << '[' << Value.R << ',' << Value.G << ','
			<< Value.B << ',' << Value.A << ']';
	}

	void WriteBodyJson(std::ostream& Stream, const GravityBodySpec& Body)
	{
		Stream << "{\"bodyId\":" << Body.BodyId
			<< ",\"role\":" << static_cast<unsigned int>(Body.Role)
			<< ",\"centerCM\":";
		WriteVectorJson(Stream, Body.CenterCM);
		Stream << ",\"gravitationalParameterCM3PerSec2\":"
			<< Body.GravitationalParameterCM3PerSec2
			<< ",\"minimumEvaluationRadiusCM\":"
			<< Body.MinimumEvaluationRadiusCM
			<< ",\"visualRadiusCM\":" << Body.VisualRadiusCM
			<< ",\"collisionRadiusCM\":" << Body.CollisionRadiusCM
			<< ",\"maximumSimulationRadiusCM\":"
			<< Body.MaximumSimulationRadiusCM
			<< ",\"influenceRadiusCM\":" << Body.InfluenceRadiusCM
			<< ",\"assistReferenceRadiusCM\":"
			<< Body.AssistReferenceRadiusCM
			<< ",\"influenceBlendWidthCM\":"
			<< Body.InfluenceBlendWidthCM
			<< ",\"virtualOrbitalVelocityCMPerSec\":";
		WriteVectorJson(Stream, Body.VirtualOrbitalVelocityCMPerSec);
		Stream << ",\"bPlaneReferenceNormal\":";
		WriteVectorJson(Stream, Body.BPlaneReferenceNormal);
		Stream << ",\"bPlaneFallbackAxis\":";
		WriteVectorJson(Stream, Body.BPlaneFallbackAxis);
		Stream << ",\"bPlaneTargetTCM\":" << Body.BPlaneTargetTCM
			<< ",\"bPlaneTargetRCM\":" << Body.BPlaneTargetRCM
			<< ",\"bPlaneSigmaTCM\":" << Body.BPlaneSigmaTCM
			<< ",\"bPlaneSigmaRCM\":" << Body.BPlaneSigmaRCM
			<< ",\"bPlaneOuterChiSquared\":"
			<< Body.BPlaneOuterChiSquared
			<< ",\"allowedPassSide\":"
			<< static_cast<unsigned int>(Body.AllowedPassSideValue)
			<< ",\"minimumEnergyChangeCM2PerSec2\":"
			<< Body.MinimumEnergyChangeCM2PerSec2
			<< ",\"maximumEnergyChangeCM2PerSec2\":"
			<< Body.MaximumEnergyChangeCM2PerSec2
			<< ",\"debugColor\":";
		WriteColorJson(Stream, Body.DebugColor);
		Stream << '}';
	}

	void WriteLaunchModelJson(
		std::ostream& Stream,
		const LaunchModel& Launch)
	{
		Stream << "{\"version\":" << Launch.Version
			<< ",\"pouchLocalPositionCM\":";
		WriteVectorJson(Stream, Launch.PouchLocalPositionCM);
		Stream << ",\"minimumYawDegrees\":" << Launch.MinimumYawDegrees
			<< ",\"maximumYawDegrees\":" << Launch.MaximumYawDegrees
			<< ",\"minimumPitchDegrees\":"
			<< Launch.MinimumPitchDegrees
			<< ",\"maximumPitchDegrees\":"
			<< Launch.MaximumPitchDegrees
			<< ",\"minimumPower\":" << Launch.MinimumPower
			<< ",\"maximumPower\":" << Launch.MaximumPower
			<< ",\"minimumLaunchSpeedCMPerSec\":"
			<< Launch.MinimumLaunchSpeedCMPerSec
			<< ",\"maximumLaunchSpeedCMPerSec\":"
			<< Launch.MaximumLaunchSpeedCMPerSec
			<< ",\"maximumSimulationTimeSeconds\":"
			<< Launch.MaximumSimulationTimeSeconds << '}';
	}

	void WriteTargetJson(std::ostream& Stream, const TargetSpec& Target)
	{
		Stream << "{\"targetId\":" << Target.TargetId
			<< ",\"centerCM\":";
		WriteVectorJson(Stream, Target.CenterCM);
		Stream << ",\"hitRadiusCM\":" << Target.HitRadiusCM
			<< ",\"geometricContactRadiusCM\":"
			<< Target.GeometricContactRadiusCM
			<< ",\"useSeparateGeometricContactCenter\":"
			<< (Target.UseSeparateGeometricContactCenter
				? "true" : "false")
			<< ",\"geometricContactCenterCM\":";
		WriteVectorJson(Stream, Target.GeometricContactCenterCM);
		Stream << ",\"requiredQualifiedAssistCount\":"
			<< Target.RequiredQualifiedAssistCount
			<< ",\"minimumQualifyingCorridorQuality\":"
			<< Target.MinimumQualifyingCorridorQuality
			<< ",\"minimumQualifyingEnergyGainCM2PerSec2\":"
			<< Target.MinimumQualifyingEnergyGainCM2PerSec2
			<< ",\"requireAllowedPassSide\":"
			<< (Target.RequireAllowedPassSide ? "true" : "false")
			<< ",\"presentationForward\":";
		WriteVectorJson(Stream, Target.PresentationForward);
		Stream << '}';
	}

	void WriteScenarioJson(
		std::ostream& Stream,
		const GravityScenario& Scenario)
	{
		Stream << "{\"layoutVersion\":" << Scenario.LayoutVersion
			<< ",\"scenarioHash\":" << Scenario.ScenarioHash
			<< ",\"bodies\":[";
		for (std::size_t Index = 0;
			Index < Scenario.Bodies.size();
			++Index)
		{
			if (Index > 0)
			{
				Stream << ',';
			}
			WriteBodyJson(Stream, Scenario.Bodies[Index]);
		}
		Stream << "],\"target\":";
		WriteTargetJson(Stream, Scenario.Target);
		Stream << '}';
	}

	void WriteSolverJson(
		std::ostream& Stream,
		const SolverConfig& Solver)
	{
		Stream << "{\"solverVersion\":" << Solver.SolverVersion
			<< ",\"hashSchemaVersion\":" << Solver.HashSchemaVersion
			<< ",\"fixedTimeStepSeconds\":"
			<< Solver.FixedTimeStepSeconds
			<< ",\"maximumSimulationTimeSeconds\":"
			<< Solver.MaximumSimulationTimeSeconds
			<< ",\"maximumStepCount\":" << Solver.MaximumStepCount
			<< ",\"maximumSubdivisionDepth\":"
			<< Solver.MaximumSubdivisionDepth
			<< ",\"maximumCoastStepExpansionDepth\":"
			<< Solver.MaximumCoastStepExpansionDepth
			<< ",\"assistStepRadiusFraction\":"
			<< Solver.AssistStepRadiusFraction
			<< ",\"collisionStepRadiusFraction\":"
			<< Solver.CollisionStepRadiusFraction
			<< ",\"gravityTimescaleFraction\":"
			<< Solver.GravityTimescaleFraction
			<< ",\"positionErrorLimitCM\":"
			<< Solver.PositionErrorLimitCM
			<< ",\"rootBisectionIterations\":"
			<< Solver.RootBisectionIterations
			<< ",\"rootAlphaTolerance\":"
			<< Solver.RootAlphaTolerance
			<< ",\"bPlaneBasisMinimumLength\":"
			<< Solver.BPlaneBasisMinimumLength
			<< ",\"minimumVInfinityCMPerSec\":"
			<< Solver.MinimumVInfinityCMPerSec
			<< ",\"maximumNaturalDeflectionErrorRadians\":"
			<< Solver.MaximumNaturalDeflectionErrorRadians
			<< ",\"energyQualityPower\":"
			<< Solver.EnergyQualityPower
			<< ",\"energyRootEpsilonCM2PerSec2\":"
			<< Solver.EnergyRootEpsilonCM2PerSec2
			<< ",\"exitEnergyResidualToleranceCM2PerSec2\":"
			<< Solver.ExitEnergyResidualToleranceCM2PerSec2
			<< ",\"energyShootingIterationCount\":"
			<< Solver.EnergyShootingIterationCount
			<< ",\"naturalCloneMaximumTimeSeconds\":"
			<< Solver.NaturalCloneMaximumTimeSeconds
			<< ",\"naturalCloneMaximumStepCount\":"
			<< Solver.NaturalCloneMaximumStepCount
			<< ",\"enabledAssistMask\":"
			<< static_cast<unsigned int>(Solver.EnabledAssistMask) << '}';
	}

	void WriteLayoutJson(
		std::ostream& Stream,
		const CandidateLayout& Layout)
	{
		Stream << "{\"layoutVersion\":" << Layout.LayoutVersion
			<< ",\"launch\":";
		WriteLaunchModelJson(Stream, Layout.Launch);
		Stream << ",\"nominalInput\":";
		WriteInputJson(Stream, Layout.NominalInput);
		Stream << ",\"scenario\":";
		WriteScenarioJson(Stream, Layout.Scenario);
		Stream << ",\"solver\":";
		WriteSolverJson(Stream, Layout.Solver);
		Stream << '}';
	}

	struct CandidateManifestContext
	{
		std::string_view SelectionScope;
		std::uint64_t WorkItems = 0;
		std::uint32_t ShardIndex = 0;
		std::uint32_t ShardCount = 1;
		std::size_t SelectionRank = 0;
		std::size_t SelectedCount = 0;
		std::uint64_t EvaluationAggregateHash = 0;
		std::uint64_t CandidateAggregateHash = 0;
	};

	std::string BuildCandidateManifestJson(
		const CandidateRecord& Candidate,
		const CandidateSearchContract& Contract,
		const CandidateManifestContext& Context)
	{
		std::ostringstream Stream;
		Stream.imbue(std::locale::classic());
		Stream << std::setprecision(17)
			<< "{\n"
			<< "  \"schema\":\"abts.m11b21.candidate.v3\",\n"
			<< "  \"status\":\"Candidate\",\n"
			<< "  \"searchContractVersion\":"
			<< SearchContractVersion << ",\n"
			<< "  \"searchAlgorithmVersion\":"
			<< SearchAlgorithmVersion << ",\n"
			<< "  \"candidateManifestVersion\":"
			<< CandidateManifestVersion << ",\n"
			<< "  \"candidateId\":\"m11b21-"
			<< Hex64(Candidate.CandidateSourceHash).substr(2)
			<< "\",\n"
			<< "  \"globalWorkIndex\":"
			<< Candidate.GlobalWorkIndex << ",\n"
			<< "  \"candidateSourceHash\":\""
			<< Hex64(Candidate.CandidateSourceHash) << "\",\n"
			<< "  \"nominalRequestHash\":\""
			<< Hex64(Candidate.NominalRequestHash) << "\",\n"
			<< "  \"nominalResultHash\":\""
			<< Hex64(Candidate.NominalResultHash) << "\",\n"
			<< "  \"scoreHash\":\"" << Hex64(Candidate.ScoreHash)
			<< "\",\n"
			<< "  \"toolIdentity\":{\"toolBuildVersion\":\""
			<< ABTS::M11Core::ToolIdentity::ToolBuildVersion
			<< "\",\"sourceHashSchema\":\""
			<< ABTS::M11Core::ToolIdentity::SourceHashSchema
			<< "\",\"sourceHashSchemaVersion\":"
			<< ABTS::M11Core::ToolIdentity::SourceHashSchemaVersion
			<< ",\"productionCoreSourceHashSha256\":\""
			<< ABTS::M11Core::ToolIdentity::
				ProductionCoreSourceHashSha256
			<< "\",\"productionCoreSourceFileCount\":"
			<< ABTS::M11Core::ToolIdentity::
				ProductionCoreSourceFileCount
			<< ",\"searchSourceHashSha256\":\""
			<< ABTS::M11Core::ToolIdentity::SearchSourceHashSha256
			<< "\",\"searchSourceFileCount\":"
			<< ABTS::M11Core::ToolIdentity::SearchSourceFileCount
			<< ",\"compilerIdentity\":\""
			<< ABTS::M11Core::ToolIdentity::CompilerIdentity
			<< "\",\"architecture\":\""
			<< ABTS::M11Core::ToolIdentity::Architecture
			<< "\",\"cxxStandard\":\""
			<< ABTS::M11Core::ToolIdentity::CxxStandard
			<< "\",\"floatingPointMode\":\""
			<< ABTS::M11Core::ToolIdentity::FloatingPointMode
			<< "\",\"numericalCompileContract\":\""
			<< ABTS::M11Core::ToolIdentity::NumericalCompileContract
			<< "\"},\n"
			<< "  \"contractDescriptor\":"
			<< BuildContractDescriptorJson(Contract) << ",\n"
			<< "  \"selection\":{\"scope\":\""
			<< Context.SelectionScope
			<< "\",\"workItems\":" << Context.WorkItems
			<< ",\"shardIndex\":" << Context.ShardIndex
			<< ",\"shardCount\":" << Context.ShardCount
			<< ",\"rank\":" << Context.SelectionRank
			<< ",\"selectedCount\":" << Context.SelectedCount
			<< ",\"evaluationAggregateHash\":\""
			<< Hex64(Context.EvaluationAggregateHash)
			<< "\",\"candidateAggregateHash\":\""
			<< Hex64(Context.CandidateAggregateHash) << "\"},\n"
			<< "  \"certification\":{\"status\":\"not-certified\""
			<< ",\"certificationHash\":\"0x0000000000000000\""
			<< ",\"certifiedBundleHash\":\"0x0000000000000000\"},\n"
			<< "  \"layout\":";
		WriteLayoutJson(Stream, Candidate.Layout);
		Stream << ",\n  \"metrics\":{\"totalFlightTimeSeconds\":"
			<< Candidate.Metrics.TotalFlightTimeSeconds
			<< ",\"finalCoastSeconds\":"
			<< Candidate.Metrics.FinalCoastSeconds
			<< ",\"maximumCoastSeconds\":"
			<< Candidate.Metrics.MaximumCoastSeconds
			<< ",\"totalInfluenceDurationSeconds\":"
			<< Candidate.Metrics.TotalInfluenceDurationSeconds
			<< ",\"minimumLayoutTurnRadians\":"
			<< Candidate.Metrics.MinimumLayoutTurnRadians
			<< ",\"layoutTurnsRadians\":[";
		for (std::size_t Index = 0;
			Index < Candidate.Metrics.LayoutTurnsRadians.size();
			++Index)
		{
			if (Index > 0)
			{
				Stream << ',';
			}
			Stream << Candidate.Metrics.LayoutTurnsRadians[Index];
		}
		Stream << ']'
			<< ",\"minimumReadableDeflectionRadians\":"
			<< Candidate.Metrics.MinimumReadableDeflectionRadians
			<< ",\"alternatingLateralTurnCount\":"
			<< Candidate.Metrics.AlternatingLateralTurnCount
			<< ",\"robustSurvivorCount\":"
			<< Candidate.Metrics.RobustSurvivorCount
			<< ",\"lowPowerCompletedAssistCount\":"
			<< Candidate.Metrics.LowPowerCompletedAssistCount
			<< ",\"assists\":[";
		for (std::size_t Index = 0;
			Index < Candidate.Metrics.Assists.size();
			++Index)
		{
			if (Index > 0)
			{
				Stream << ',';
			}
			const AssistMetrics& Assist =
				Candidate.Metrics.Assists[Index];
			Stream << "{\"assistIndex\":" << (Index + 1)
				<< ",\"enterTimeSeconds\":" << Assist.EnterTimeSeconds
				<< ",\"closestTimeSeconds\":"
				<< Assist.ClosestTimeSeconds
				<< ",\"exitTimeSeconds\":" << Assist.ExitTimeSeconds
				<< ",\"coastBeforeEnterSeconds\":"
				<< Assist.CoastBeforeEnterSeconds
				<< ",\"influenceDurationSeconds\":"
				<< Assist.InfluenceDurationSeconds
				<< ",\"actualDeflectionRadians\":"
				<< Assist.ActualDeflectionRadians
				<< ",\"naturalDeflectionRadians\":"
				<< Assist.NaturalDeflectionRadians
				<< ",\"entrySpeedCMPerSec\":"
				<< Assist.EntrySpeedCMPerSec
				<< ",\"exitSpeedCMPerSec\":"
				<< Assist.ExitSpeedCMPerSec
				<< ",\"signedLateralTurnRadians\":"
				<< Assist.SignedLateralTurnRadians
				<< ",\"lateralTurnAxisProjection\":"
				<< Assist.LateralTurnAxisProjection
				<< ",\"corridorQuality\":" << Assist.CorridorQuality
				<< ",\"appliedEnergyGainCM2PerSec2\":"
				<< Assist.AppliedEnergyGainCM2PerSec2
				<< ",\"collisionClearanceCM\":"
				<< Assist.CollisionClearanceCM << '}';
		}
		Stream << "],\"candidateDomainAnalysis\":{"
			<< "\"samplingSemantics\":"
			<< BuildSamplingSemanticsJson(Contract)
			<< ",\"sampleCounts\":{\"fullLaunchDomain\":"
			<< Candidate.Metrics.FullDomainSampleCount
			<< ",\"screenAim\":"
			<< Candidate.Metrics.ScreenAimSampleCount
			<< "},\"solveFailureCounts\":{\"fullLaunchDomain\":"
			<< Candidate.Metrics.FullDomainSolveFailureCount
			<< ",\"screenAim\":"
			<< Candidate.Metrics.ScreenAimSolveFailureCount
			<< ",\"conditional\":"
			<< Candidate.Metrics.ConditionalSolveFailureCount
			<< "},\"inputSets\":[";
		for (std::size_t Index = 0;
			Index < Candidate.Metrics.InputSets.size();
			++Index)
		{
			if (Index > 0)
			{
				Stream << ',';
			}
			WriteInputSetJson(
				Stream, Candidate.Metrics.InputSets[Index], Index);
		}
		Stream << "]},\"softScores\":{\"prefixRetention\":"
			<< Candidate.Metrics.PrefixRetentionScore
			<< ",\"prefixHull\":" << Candidate.Metrics.PrefixHullScore
			<< ",\"deflectionReadability\":"
			<< Candidate.Metrics.DeflectionReadabilityScore
			<< ",\"alternation\":"
			<< Candidate.Metrics.AlternationScore
			<< ",\"pacing\":" << Candidate.Metrics.PacingScore
			<< ",\"total\":" << Candidate.Metrics.SoftScore
			<< "},\"ablations\":[";
		for (std::size_t Index = 0;
			Index < Candidate.Metrics.AblationMasks.size();
			++Index)
		{
			if (Index > 0)
			{
				Stream << ',';
			}
			Stream << "{\"mask\":"
				<< static_cast<unsigned int>(
					Candidate.Metrics.AblationMasks[Index])
				<< ",\"hitTarget\":"
				<< (Candidate.Metrics.AblationHitTarget[Index]
					? "true" : "false")
				<< ",\"resultHash\":\""
				<< Hex64(
					Candidate.Metrics.AblationResultHashes[Index])
				<< "\"}";
		}
		Stream << "]}\n}\n";
		return Stream.str();
	}

	std::uint64_t ShardWorkItemCount(const Options& OptionsValue)
	{
		if (OptionsValue.ShardIndex >= OptionsValue.WorkItems)
		{
			return 0;
		}
		return 1
			+ (OptionsValue.WorkItems - 1 - OptionsValue.ShardIndex)
				/ OptionsValue.ShardCount;
	}

	std::string ShardDirectoryName(const std::uint32_t ShardIndex)
	{
		std::ostringstream Result;
		Result << "shard_" << std::setw(4) << std::setfill('0')
			<< ShardIndex;
		return Result.str();
	}

	int RunDescribeContract(const Options& OptionsValue)
	{
		CandidateSearchContract Contract =
			CandidateSearchContract::MakeV2_1();
		Contract.SearchSeed = OptionsValue.Seed;
		std::string Failure;
		if (!Contract.IsValid(&Failure))
		{
			std::cerr << "DescribeContractInvalid:" << Failure << '\n';
			return 1;
		}
		std::cout << BuildContractDescriptorJson(Contract) << '\n';
		return 0;
	}

	int RunSelfTest(const bool Json)
	{
		CandidateSearchContract Contract =
			CandidateSearchContract::MakeV2_1();
		std::string Failure;
		CandidateRecord First;
		CandidateRecord Second;
		constexpr std::uint64_t DeterministicCandidateWorkIndex = 772ull;
		const bool FirstCompleted =
			CandidateSearch::EvaluateWorkItem(
				Contract,
				DeterministicCandidateWorkIndex,
				First,
				&Failure);
		const bool SecondCompleted =
			CandidateSearch::EvaluateWorkItem(
				Contract,
				DeterministicCandidateWorkIndex,
				Second,
				&Failure);
		const std::string ContractJson =
			BuildSearchContractJson(Contract);
		const std::string ContractDescriptor =
			BuildContractDescriptorJson(Contract);
		const bool ProvenanceValid =
			Contract.MonteCarloSampleCount == 5000
			&& Contract.ScreenAimSampleCount == 5000
			&& HashCanonicalJson(ContractJson) != 0
			&& ContractDescriptor.find("\"screenAim\"")
				!= std::string::npos
			&& ContractDescriptor.find("\"fullLaunchDomain\"")
				!= std::string::npos
			&& ContractDescriptor.find("\"searchContract\"")
				!= std::string::npos
			&& ContractDescriptor.find(
				"\"minimumLateralTurnAxisProjection\":0.25")
				!= std::string::npos
			&& ContractDescriptor.find("\"searchSourceHashSha256\"")
				!= std::string::npos;
		bool InputDomainMetricsValid =
			First.Metrics.FullDomainSampleCount
				== Contract.MonteCarloSampleCount
			&& First.Metrics.ScreenAimSampleCount
				== Contract.ScreenAimSampleCount
			&& First.Metrics.FullDomainSolveFailureCount == 0
			&& First.Metrics.ScreenAimSolveFailureCount == 0
			&& First.Metrics.ConditionalSolveFailureCount == 0;
		std::int32_t FullParentCount =
			First.Metrics.FullDomainSampleCount;
		std::int32_t ScreenParentCount =
			First.Metrics.ScreenAimSampleCount;
		for (std::size_t Index = 0;
			Index < First.Metrics.InputSets.size();
			++Index)
		{
			const InputSetMetrics& Set =
				First.Metrics.InputSets[Index];
			InputDomainMetricsValid = InputDomainMetricsValid
				&& Set.FullDomainCount >= 0
				&& Set.FullDomainCount <= FullParentCount
				&& Set.ScreenAimCount >= 0
				&& Set.ScreenAimCount <= ScreenParentCount
				&& Set.ConditionalMemberCount
					<= Set.ConditionalParentCount
				&& Set.ConditionalParentCount
					<= Set.ConditionalProbeCount
				&& std::isfinite(
					Set.ScreenAimHullAreaSquareDegrees)
				&& std::isfinite(
					Set.ScreenAimHullCompactness);
			if (Index < 3)
			{
				const double ExpectedScreenRatio =
					ScreenParentCount > 0
						? static_cast<double>(Set.ScreenAimCount)
							/ static_cast<double>(ScreenParentCount)
						: 0.0;
				InputDomainMetricsValid =
					InputDomainMetricsValid
					&& Set.ScreenAimRetentionCompliant
					&& Set.ScreenAimRetentionRatio
						== ExpectedScreenRatio
					&& Set.ScreenAimRetentionRatio
						>= Contract.MinimumPrefixRetentionRatio
					&& Set.ScreenAimRetentionRatio
						<= Contract.MaximumPrefixRetentionRatio
					&& Set.ScreenAimHullCompliant
					&& Set.ScreenAimHullContainsNominal
					&& Set.ScreenAimHullEvidencePointCount
						== Set.ScreenAimCount
					&& Set.ScreenAimHullEvidencePointCount
						>= Contract.MinimumHullEvidenceCount;
			}
			FullParentCount = Set.FullDomainCount;
			ScreenParentCount = Set.ScreenAimCount;
		}
		const bool Passed = Contract.IsValid()
			&& ProvenanceValid
			&& FirstCompleted
			&& SecondCompleted
			&& First.Status == EvaluationStatus::Accepted
			&& Second.Status == EvaluationStatus::Accepted
			&& InputDomainMetricsValid
			&& First.Status == Second.Status
			&& First.CandidateSourceHash
				== Second.CandidateSourceHash
			&& First.NominalResultHash
				== Second.NominalResultHash
			&& First.ScoreHash == Second.ScoreHash
			&& First.SolverInvocationCount
				== Second.SolverInvocationCount;
		if (Json)
		{
			std::cout
				<< "{\"schema\":\"abts.m11b21.self_test.v3\""
				<< ",\"passed\":" << (Passed ? "true" : "false")
				<< ",\"contractDescriptor\":" << ContractDescriptor
				<< ",\"workIndex\":"
				<< DeterministicCandidateWorkIndex
				<< ",\"status\":\"" << ToString(First.Status) << "\""
				<< ",\"candidateSourceHash\":\""
				<< Hex64(First.CandidateSourceHash) << "\""
				<< ",\"resultHash\":\""
				<< Hex64(First.NominalResultHash) << "\""
				<< ",\"scoreHash\":\"" << Hex64(First.ScoreHash)
				<< "\",\"solverInvocations\":"
				<< First.SolverInvocationCount
				<< ",\"rejection\":\""
				<< EscapeJson(First.Rejection) << "\""
				<< ",\"totalFlightSeconds\":"
				<< First.Metrics.TotalFlightTimeSeconds
				<< ",\"maximumCoastSeconds\":"
				<< First.Metrics.MaximumCoastSeconds
				<< ",\"minimumLayoutTurnRadians\":"
				<< First.Metrics.MinimumLayoutTurnRadians
				<< ",\"softScore\":" << First.Metrics.SoftScore
				<< ",\"inputSets\":[";
			for (std::size_t Index = 0;
				Index < First.Metrics.InputSets.size();
				++Index)
			{
				if (Index > 0)
				{
					std::cout << ',';
				}
				WriteInputSetJson(
					std::cout, First.Metrics.InputSets[Index], Index);
			}
			std::cout
				<< ']'
				<< ",\"assistDurations\":["
				<< First.Metrics.Assists[0].InfluenceDurationSeconds
				<< ',' << First.Metrics.Assists[1].InfluenceDurationSeconds
				<< ',' << First.Metrics.Assists[2].InfluenceDurationSeconds
				<< "],\"assistDeflections\":["
				<< First.Metrics.Assists[0].ActualDeflectionRadians
				<< ',' << First.Metrics.Assists[1].ActualDeflectionRadians
				<< ',' << First.Metrics.Assists[2].ActualDeflectionRadians
				<< "],\"assistEnergy\":["
				<< First.Metrics.Assists[0].AppliedEnergyGainCM2PerSec2
				<< ',' << First.Metrics.Assists[1].AppliedEnergyGainCM2PerSec2
				<< ',' << First.Metrics.Assists[2].AppliedEnergyGainCM2PerSec2
				<< "],\"assistClearance\":["
				<< First.Metrics.Assists[0].CollisionClearanceCM
				<< ',' << First.Metrics.Assists[1].CollisionClearanceCM
				<< ',' << First.Metrics.Assists[2].CollisionClearanceCM
				<< "]}\n";
		}
		else
		{
			std::cout << "[ABTS][M11-B-v2.1][SelfTest] Passed="
				<< (Passed ? 1 : 0)
				<< " Status=" << ToString(First.Status)
				<< " Source=" << Hex64(First.CandidateSourceHash)
				<< " Score=" << Hex64(First.ScoreHash) << '\n';
		}
		return Passed ? 0 : 1;
	}

	int RunSearch(const Options& OptionsValue)
	{
		std::error_code Error;
		if (!fs::exists(OptionsValue.OutputDirectory, Error))
		{
			fs::create_directories(OptionsValue.OutputDirectory, Error);
		}
		if (Error || !fs::is_directory(OptionsValue.OutputDirectory))
		{
			std::cerr << "OutputDirectoryUnavailable\n";
			return 1;
		}
		const fs::path StatePath =
			OptionsValue.OutputDirectory / "state.tsv";
		const fs::path EvaluationPath =
			OptionsValue.OutputDirectory / "evaluations.jsonl";
		const fs::path CheckpointPath =
			OptionsValue.OutputDirectory / "checkpoint.json";
		const fs::path SummaryPath =
			OptionsValue.OutputDirectory / "summary.json";
		const fs::path CandidateDirectory =
			OptionsValue.OutputDirectory / "candidates";
		fs::create_directories(CandidateDirectory, Error);
		if (Error)
		{
			std::cerr << "CandidateDirectoryUnavailable\n";
			return 1;
		}

		CandidateSearchContract Contract =
			CandidateSearchContract::MakeV2_1();
		Contract.SearchSeed = OptionsValue.Seed;
		std::string Failure;
		BatchRequest Validation;
		Validation.GlobalWorkItemCount = OptionsValue.WorkItems;
		Validation.ShardIndex = OptionsValue.ShardIndex;
		Validation.ShardCount = OptionsValue.ShardCount;
		Validation.ThreadCount = OptionsValue.Threads;
		Validation.RequestedTopCandidateCount = OptionsValue.TopK;
		if (!Contract.IsValid(&Failure)
			|| !Validation.IsValid(&Failure))
		{
			std::cerr << Failure << '\n';
			return 1;
		}

		std::vector<CandidateRecord> Records;
		Checkpoint Current;
		if (OptionsValue.Resume)
		{
			if (!ReadCheckpoint(
					CheckpointPath,
					OptionsValue,
					Contract,
					Current,
					Failure)
				|| !fs::exists(StatePath)
				|| !fs::exists(EvaluationPath)
				|| fs::file_size(StatePath) != Current.StateBytes
				|| fs::file_size(EvaluationPath)
					!= Current.EvaluationBytes
				|| HashFile(StatePath) != Current.StateHash
				|| HashFile(EvaluationPath)
					!= Current.EvaluationHash
				|| !ReadState(StatePath, Records, Failure)
				|| Records.size() != Current.NextLocalOffset)
			{
				std::cerr << "ResumeRejected:" << Failure << '\n';
				return 1;
			}
		}
		else
		{
			if (fs::exists(StatePath)
				|| fs::exists(EvaluationPath)
				|| fs::exists(CheckpointPath)
				|| fs::exists(SummaryPath))
			{
				std::cerr << "OutputAlreadyContainsSearchState\n";
				return 1;
			}
			std::ofstream(StatePath, std::ios::binary).close();
			std::ofstream(EvaluationPath, std::ios::binary).close();
			Current.StateHash = HashFile(StatePath);
			Current.EvaluationHash = HashFile(EvaluationPath);
		}

		const std::uint64_t ShardCount = ShardWorkItemCount(OptionsValue);
		if (Current.NextLocalOffset > ShardCount)
		{
			std::cerr << "CheckpointBeyondShard\n";
			return 1;
		}
		while (Current.NextLocalOffset < ShardCount)
		{
			const std::uint64_t Remaining =
				ShardCount - Current.NextLocalOffset;
			const std::uint64_t Count = std::min(
				Remaining, OptionsValue.CheckpointEvery);
			BatchRequest Batch = Validation;
			Batch.LocalBeginOffset = Current.NextLocalOffset;
			Batch.LocalWorkItemLimit = Count;
			BatchResult Result;
			if (!CandidateSearch::RunBatch(
				Contract, Batch, Result, &Failure))
			{
				std::cerr << "BatchFailed:" << Failure << '\n';
				return 1;
			}
			{
				std::ofstream State(
					StatePath,
					std::ios::binary | std::ios::app);
				std::ofstream Evaluation(
					EvaluationPath,
					std::ios::binary | std::ios::app);
				if (!State || !Evaluation)
				{
					std::cerr << "AppendOpenFailed\n";
					return 1;
				}
				for (const CandidateRecord& Record : Result.Evaluations)
				{
					State << EvaluationStateLine(Record);
					Evaluation << EvaluationJsonLine(Record, Contract);
					Records.push_back(Record);
				}
				State.flush();
				Evaluation.flush();
				if (!State || !Evaluation)
				{
					std::cerr << "AppendWriteFailed\n";
					return 1;
				}
			}
			Current.NextLocalOffset +=
				static_cast<std::uint64_t>(
					Result.Evaluations.size());
			Current.ElapsedSeconds += Result.WallClockSeconds;
			Current.StateBytes = fs::file_size(StatePath);
			Current.EvaluationBytes = fs::file_size(EvaluationPath);
			Current.StateHash = HashFile(StatePath);
			Current.EvaluationHash = HashFile(EvaluationPath);
			if (!WriteTextAtomically(
				CheckpointPath,
				BuildCheckpointJson(OptionsValue, Contract, Current),
				Failure))
			{
				std::cerr << "CheckpointWriteFailed:" << Failure << '\n';
				return 1;
			}
			std::cerr << "[ABTS][M11-B-v2.1][Shard] "
				<< OptionsValue.ShardIndex << '/'
				<< OptionsValue.ShardCount
				<< " Completed=" << Current.NextLocalOffset
				<< '/' << ShardCount
				<< " ChunkSeconds=" << Result.WallClockSeconds
				<< '\n';
		}

		for (CandidateRecord& Record : Records)
		{
			if (!Record.IsAccepted()
				|| Record.Layout.Scenario.ScenarioHash != 0)
			{
				continue;
			}
			CandidateRecord Replay;
			if (!CandidateSearch::EvaluateWorkItem(
					Contract,
					Record.GlobalWorkIndex,
					Replay,
					&Failure)
				|| !Replay.IsAccepted()
				|| Replay.CandidateSourceHash
					!= Record.CandidateSourceHash
				|| Replay.NominalRequestHash
					!= Record.NominalRequestHash
				|| Replay.NominalResultHash
					!= Record.NominalResultHash
				|| Replay.ScoreHash != Record.ScoreHash)
			{
				std::cerr << "AcceptedResumeReplayMismatch:"
					<< Record.GlobalWorkIndex << '\n';
				return 1;
			}
			Record = std::move(Replay);
		}
		const std::vector<CandidateRecord> Top =
			CandidateSearch::SelectTopCandidates(
				Contract, Records, OptionsValue.TopK);
		const std::uint64_t EvaluationAggregate =
			ComputeEvaluationAggregateHash(Records);
		const std::uint64_t CandidateAggregate =
			ComputeEvaluationAggregateHash(Top);
		for (std::size_t Index = 0; Index < Top.size(); ++Index)
		{
			const fs::path Manifest =
				CandidateDirectory
				/ ("candidate_" + std::to_string(Index + 1)
					+ "_" + std::to_string(
						Top[Index].GlobalWorkIndex)
					+ ".json");
			if (!WriteTextAtomically(
				Manifest,
				BuildCandidateManifestJson(
					Top[Index],
					Contract,
					CandidateManifestContext{
						"shard",
						OptionsValue.WorkItems,
						OptionsValue.ShardIndex,
						OptionsValue.ShardCount,
						Index + 1,
						Top.size(),
						EvaluationAggregate,
						CandidateAggregate}),
				Failure))
			{
				std::cerr << "ManifestWriteFailed:" << Failure << '\n';
				return 1;
			}
		}
		std::uint64_t SolverInvocations = 0;
		std::uint64_t AcceptedCount = 0;
		for (const CandidateRecord& Record : Records)
		{
			SolverInvocations += static_cast<std::uint64_t>(
				std::max(0, Record.SolverInvocationCount));
			AcceptedCount += Record.IsAccepted() ? 1u : 0u;
		}
		const double Throughput = Current.ElapsedSeconds > 0.0
			? static_cast<double>(SolverInvocations)
				/ Current.ElapsedSeconds
			: 0.0;
		std::ostringstream Summary;
		Summary << std::setprecision(17)
			<< "{\n"
			<< "  \"schema\":\"abts.m11b21.shard_summary.v3\",\n"
			<< "  \"passed\":true,\n"
			<< "  \"diagnostic\":\""
			<< (Top.empty()
				? "CompletedInsufficientCandidates"
				: "Completed")
			<< "\",\n"
			<< "  \"workItems\":" << OptionsValue.WorkItems << ",\n"
			<< "  \"shardIndex\":" << OptionsValue.ShardIndex << ",\n"
			<< "  \"shardCount\":" << OptionsValue.ShardCount << ",\n"
			<< "  \"evaluatedCount\":" << Records.size() << ",\n"
			<< "  \"acceptedCount\":" << AcceptedCount << ",\n"
			<< "  \"selectedCandidateCount\":" << Top.size() << ",\n"
			<< "  \"solverInvocationCount\":" << SolverInvocations
			<< ",\n"
			<< "  \"wallClockSeconds\":" << Current.ElapsedSeconds
			<< ",\n"
			<< "  \"solverInvocationsPerSecond\":" << Throughput
			<< ",\n"
			<< "  \"evaluationAggregateHash\":\""
			<< Hex64(EvaluationAggregate) << "\",\n"
			<< "  \"candidateAggregateHash\":\""
			<< Hex64(CandidateAggregate) << "\",\n"
			<< "  \"contractDescriptor\":"
			<< BuildContractDescriptorJson(Contract) << ",\n"
			<< "  \"productionCoreSourceHashSha256\":\""
			<< ABTS::M11Core::ToolIdentity::
				ProductionCoreSourceHashSha256 << "\",\n"
			<< "  \"searchSourceHashSha256\":\""
			<< ABTS::M11Core::ToolIdentity::SearchSourceHashSha256
			<< "\"\n"
			<< "}\n";
		if (!WriteTextAtomically(SummaryPath, Summary.str(), Failure))
		{
			std::cerr << "SummaryWriteFailed:" << Failure << '\n';
			return 1;
		}
		std::cout << Summary.str();
		return 0;
	}

	int RunMerge(const Options& OptionsValue)
	{
		std::error_code Error;
		if (!fs::exists(OptionsValue.InputRoot, Error)
			|| !fs::is_directory(OptionsValue.InputRoot, Error)
			|| Error)
		{
			std::cerr << "MergeInputRootUnavailable\n";
			return 1;
		}
		if (fs::exists(OptionsValue.OutputDirectory, Error))
		{
			if (Error
				|| !fs::is_directory(
					OptionsValue.OutputDirectory, Error)
				|| Error
				|| !fs::is_empty(OptionsValue.OutputDirectory, Error)
				|| Error)
			{
				std::cerr << "MergeOutputMustBeEmpty\n";
				return 1;
			}
		}
		else
		{
			fs::create_directories(OptionsValue.OutputDirectory, Error);
			if (Error)
			{
				std::cerr << "MergeOutputUnavailable\n";
				return 1;
			}
		}
		const fs::path CandidateDirectory =
			OptionsValue.OutputDirectory / "candidates";
		fs::create_directories(CandidateDirectory, Error);
		if (Error)
		{
			std::cerr << "MergeCandidateDirectoryUnavailable\n";
			return 1;
		}

		CandidateSearchContract Contract =
			CandidateSearchContract::MakeV2_1();
		Contract.SearchSeed = OptionsValue.Seed;
		std::string Failure;
		if (!Contract.IsValid(&Failure))
		{
			std::cerr << "MergeContractInvalid:" << Failure << '\n';
			return 1;
		}

		std::vector<CandidateRecord> Records;
		Records.reserve(static_cast<std::size_t>(OptionsValue.WorkItems));
		double CumulativeShardSeconds = 0.0;
		for (std::uint32_t ShardIndex = 0;
			ShardIndex < OptionsValue.ShardCount;
			++ShardIndex)
		{
			Options ShardOptions = OptionsValue;
			ShardOptions.Merge = false;
			ShardOptions.InputRoot.clear();
			ShardOptions.ShardIndex = ShardIndex;
			ShardOptions.OutputDirectory =
				OptionsValue.InputRoot
				/ ShardDirectoryName(ShardIndex);
			const fs::path StatePath =
				ShardOptions.OutputDirectory / "state.tsv";
			const fs::path EvaluationPath =
				ShardOptions.OutputDirectory / "evaluations.jsonl";
			const fs::path CheckpointPath =
				ShardOptions.OutputDirectory / "checkpoint.json";
			const fs::path SummaryPath =
				ShardOptions.OutputDirectory / "summary.json";
			Checkpoint ShardCheckpoint;
			std::vector<CandidateRecord> ShardRecords;
			const std::uint64_t ExpectedCount =
				ShardWorkItemCount(ShardOptions);
			if (!fs::exists(SummaryPath)
				|| !ReadCheckpoint(
					CheckpointPath,
					ShardOptions,
					Contract,
					ShardCheckpoint,
					Failure)
				|| ShardCheckpoint.NextLocalOffset != ExpectedCount
				|| !fs::exists(StatePath)
				|| !fs::exists(EvaluationPath)
				|| fs::file_size(StatePath)
					!= ShardCheckpoint.StateBytes
				|| fs::file_size(EvaluationPath)
					!= ShardCheckpoint.EvaluationBytes
				|| HashFile(StatePath) != ShardCheckpoint.StateHash
				|| HashFile(EvaluationPath)
					!= ShardCheckpoint.EvaluationHash
				|| !ReadState(StatePath, ShardRecords, Failure)
				|| ShardRecords.size() != ExpectedCount)
			{
				std::cerr << "MergeShardRejected:" << ShardIndex
					<< ':' << Failure << '\n';
				return 1;
			}
			for (std::size_t LocalIndex = 0;
				LocalIndex < ShardRecords.size();
				++LocalIndex)
			{
				const std::uint64_t ExpectedWorkIndex =
					static_cast<std::uint64_t>(ShardIndex)
					+ static_cast<std::uint64_t>(LocalIndex)
						* OptionsValue.ShardCount;
				if (ShardRecords[LocalIndex].GlobalWorkIndex
					!= ExpectedWorkIndex)
				{
					std::cerr << "MergeShardIndexSequenceMismatch:"
						<< ShardIndex << ':' << LocalIndex << '\n';
					return 1;
				}
			}
			CumulativeShardSeconds +=
				ShardCheckpoint.ElapsedSeconds;
			Records.insert(
				Records.end(),
				std::make_move_iterator(ShardRecords.begin()),
				std::make_move_iterator(ShardRecords.end()));
		}
		std::sort(
			Records.begin(),
			Records.end(),
			[](const CandidateRecord& Left, const CandidateRecord& Right)
			{
				return Left.GlobalWorkIndex < Right.GlobalWorkIndex;
			});
		if (Records.size() != OptionsValue.WorkItems)
		{
			std::cerr << "MergeGlobalRecordCountMismatch\n";
			return 1;
		}
		for (std::size_t Index = 0; Index < Records.size(); ++Index)
		{
			if (Records[Index].GlobalWorkIndex != Index)
			{
				std::cerr << "MergeGlobalIndexCoverageMismatch:"
					<< Index << '\n';
				return 1;
			}
		}

		std::uint64_t ReplaySolverInvocations = 0;
		for (CandidateRecord& Record : Records)
		{
			if (!Record.IsAccepted())
			{
				continue;
			}
			CandidateRecord Replay;
			if (!CandidateSearch::EvaluateWorkItem(
					Contract,
					Record.GlobalWorkIndex,
					Replay,
					&Failure)
				|| !Replay.IsAccepted()
				|| Replay.CandidateSourceHash
					!= Record.CandidateSourceHash
				|| Replay.NominalRequestHash
					!= Record.NominalRequestHash
				|| Replay.NominalResultHash
					!= Record.NominalResultHash
				|| Replay.ScoreHash != Record.ScoreHash)
			{
				std::cerr << "MergeAcceptedReplayMismatch:"
					<< Record.GlobalWorkIndex << '\n';
				return 1;
			}
			ReplaySolverInvocations += static_cast<std::uint64_t>(
				std::max(0, Replay.SolverInvocationCount));
			Record = std::move(Replay);
		}

		const std::vector<CandidateRecord> Top =
			CandidateSearch::SelectTopCandidates(
				Contract,
				Records,
				OptionsValue.TopK);
		const std::uint64_t EvaluationAggregate =
			ComputeEvaluationAggregateHash(Records);
		const std::uint64_t CandidateAggregate =
			ComputeEvaluationAggregateHash(Top);
		for (std::size_t Index = 0; Index < Top.size(); ++Index)
		{
			const fs::path Manifest =
				CandidateDirectory
					/ ("candidate_" + std::to_string(Index + 1)
						+ "_" + std::to_string(
							Top[Index].GlobalWorkIndex)
						+ ".json");
			if (!WriteTextAtomically(
					Manifest,
					BuildCandidateManifestJson(
						Top[Index],
						Contract,
						CandidateManifestContext{
							"global-merge",
							OptionsValue.WorkItems,
							0,
							OptionsValue.ShardCount,
							Index + 1,
							Top.size(),
							EvaluationAggregate,
							CandidateAggregate}),
					Failure))
			{
				std::cerr << "MergeManifestWriteFailed:"
					<< Failure << '\n';
				return 1;
			}
		}

		std::uint64_t SolverInvocations = 0;
		std::uint64_t AcceptedCount = 0;
		for (const CandidateRecord& Record : Records)
		{
			SolverInvocations += static_cast<std::uint64_t>(
				std::max(0, Record.SolverInvocationCount));
			AcceptedCount += Record.IsAccepted() ? 1u : 0u;
		}
		const double Throughput = CumulativeShardSeconds > 0.0
			? static_cast<double>(SolverInvocations)
				/ CumulativeShardSeconds
			: 0.0;
		std::ostringstream Summary;
		Summary << std::setprecision(17)
			<< "{\n"
			<< "  \"schema\":\"abts.m11b21.merge_summary.v3\",\n"
			<< "  \"passed\":true,\n"
			<< "  \"diagnostic\":\""
			<< (Top.empty()
				? "CompletedInsufficientCandidates"
				: "Completed")
			<< "\",\n"
			<< "  \"workItems\":" << OptionsValue.WorkItems << ",\n"
			<< "  \"shardCount\":" << OptionsValue.ShardCount << ",\n"
			<< "  \"evaluatedCount\":" << Records.size() << ",\n"
			<< "  \"acceptedCount\":" << AcceptedCount << ",\n"
			<< "  \"selectedCandidateCount\":" << Top.size() << ",\n"
			<< "  \"solverInvocationCount\":" << SolverInvocations
			<< ",\n"
			<< "  \"mergeReplaySolverInvocationCount\":"
			<< ReplaySolverInvocations << ",\n"
			<< "  \"cumulativeShardSeconds\":"
			<< CumulativeShardSeconds << ",\n"
			<< "  \"solverInvocationsPerCumulativeShardSecond\":"
			<< Throughput << ",\n"
			<< "  \"evaluationAggregateHash\":\""
			<< Hex64(EvaluationAggregate) << "\",\n"
			<< "  \"candidateAggregateHash\":\""
			<< Hex64(CandidateAggregate) << "\",\n"
			<< "  \"contractDescriptor\":"
			<< BuildContractDescriptorJson(Contract) << ",\n"
			<< "  \"productionCoreSourceHashSha256\":\""
			<< ABTS::M11Core::ToolIdentity::
				ProductionCoreSourceHashSha256 << "\",\n"
			<< "  \"searchSourceHashSha256\":\""
			<< ABTS::M11Core::ToolIdentity::SearchSourceHashSha256
			<< "\"\n"
			<< "}\n";
		const fs::path SummaryPath =
			OptionsValue.OutputDirectory / "summary.json";
		if (!WriteTextAtomically(
				SummaryPath, Summary.str(), Failure))
		{
			std::cerr << "MergeSummaryWriteFailed:" << Failure << '\n';
			return 1;
		}
		std::cout << Summary.str();
		return 0;
	}
}

int main(const int ArgumentCount, char** Arguments)
{
	Options Parsed;
	std::string Failure;
	if (!ParseOptions(ArgumentCount, Arguments, Parsed, Failure))
	{
		std::cerr
			<< "Usage:\n"
			<< "  ABTSM11SearchCLI --self-test [--json]\n"
			<< "  ABTSM11SearchCLI --describe-contract"
			<< " [--seed N] [--json]\n"
			<< "  ABTSM11SearchCLI search --output <absolute-dir>"
			<< " --work-items N [--shard-index I --shard-count N]"
			<< " [--threads N --top-k N --seed N]"
			<< " [--checkpoint-every N --resume] [--json]\n"
			<< "  ABTSM11SearchCLI merge --input-root <absolute-dir>"
			<< " --output <absolute-dir> --work-items N"
			<< " --shard-count N [--top-k N --seed N --json]\n"
			<< "Error: " << Failure << '\n';
		return 2;
	}
	return Parsed.SelfTest
		? RunSelfTest(Parsed.Json)
		: Parsed.DescribeContract
			? RunDescribeContract(Parsed)
			: Parsed.Merge
				? RunMerge(Parsed)
				: RunSearch(Parsed);
}
