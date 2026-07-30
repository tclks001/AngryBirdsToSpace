// Copyright Epic Games, Inc. All Rights Reserved.

#include "M11Search/ABTSM11CandidateSearch.h"
#include "M11Search/ABTSM11FrozenCandidateLayouts.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
	namespace fs = std::filesystem;
	using ABTS::M11Search::CandidateLayout;
	using ABTS::M11Search::CandidateSearch;
	using ABTS::M11Search::CandidateSearchContract;
	using ABTS::M11Search::FrozenCandidateIdentity;
	using ABTS::M11Search::InputEvaluation;
	using ABTS::M11Search::LaunchInput;

	struct Options
	{
		bool Merge = false;
		std::int32_t Rank = 3;
		fs::path Output;
		fs::path InputRoot;
		std::uint32_t Threads = 1;
		std::uint32_t ShardIndex = 0;
		std::uint32_t ShardCount = 1;
		double YawStep = 2.0;
		double PitchStep = 3.0;
		double PowerStep = 0.025;
		std::uint32_t CheckpointEvery = 256;
		bool Resume = false;
	};

	struct Grid
	{
		double MinYaw = -18.0;
		double MaxYaw = 18.0;
		double MinPitch = 0.0;
		double MaxPitch = 60.0;
		double MinPower = 0.0;
		double MaxPower = 1.0;
		double YawStep = 2.0;
		double PitchStep = 3.0;
		double PowerStep = 0.025;
		std::int32_t YawCount = 0;
		std::int32_t PitchCount = 0;
		std::int32_t PowerCount = 0;

		std::uint64_t Count() const
		{
			return static_cast<std::uint64_t>(YawCount)
				* static_cast<std::uint64_t>(PitchCount)
				* static_cast<std::uint64_t>(PowerCount);
		}
	};

	struct Sample
	{
		std::uint64_t GlobalIndex = 0;
		std::int32_t YawIndex = 0;
		std::int32_t PitchIndex = 0;
		std::int32_t PowerIndex = 0;
		std::uint8_t PrefixMask = 0;
		std::uint8_t Termination = 0;
		std::int32_t CompletedAssistCount = 0;
		std::int32_t TargetContactCount = 0;
		std::uint64_t ResultHash = 0;
	};

	bool ParseUnsigned(const std::string& Text, std::uint64_t& Out)
	{
		const char* Begin = Text.data();
		const char* End = Begin + Text.size();
		const auto Result = std::from_chars(Begin, End, Out);
		return Result.ec == std::errc() && Result.ptr == End;
	}

	bool ParseDouble(const std::string& Text, double& Out)
	{
		char* End = nullptr;
		Out = std::strtod(Text.c_str(), &End);
		return End == Text.c_str() + Text.size() && std::isfinite(Out);
	}

	bool ParseOptions(
		const int Argc,
		char** Argv,
		Options& Out,
		std::string& Failure)
	{
		if (Argc < 2)
		{
			Failure = "MissingCommand";
			return false;
		}
		const std::string Command = Argv[1];
		if (Command == "merge")
		{
			Out.Merge = true;
		}
		else if (Command != "preflight")
		{
			Failure = "UnknownCommand";
			return false;
		}
		for (int Index = 2; Index < Argc; ++Index)
		{
			const std::string Key = Argv[Index];
			if (Key == "--resume")
			{
				Out.Resume = true;
				continue;
			}
			if (Index + 1 >= Argc)
			{
				Failure = "MissingValue:" + Key;
				return false;
			}
			const std::string Value = Argv[++Index];
			std::uint64_t Unsigned = 0;
			double Number = 0.0;
			if (Key == "--output")
			{
				Out.Output = fs::path(Value);
			}
			else if (Key == "--input-root")
			{
				Out.InputRoot = fs::path(Value);
			}
			else if (Key == "--rank" && ParseUnsigned(Value, Unsigned))
			{
				Out.Rank = static_cast<std::int32_t>(Unsigned);
			}
			else if (Key == "--threads" && ParseUnsigned(Value, Unsigned))
			{
				Out.Threads = static_cast<std::uint32_t>(Unsigned);
			}
			else if (Key == "--shard-index" && ParseUnsigned(Value, Unsigned))
			{
				Out.ShardIndex = static_cast<std::uint32_t>(Unsigned);
			}
			else if (Key == "--shard-count" && ParseUnsigned(Value, Unsigned))
			{
				Out.ShardCount = static_cast<std::uint32_t>(Unsigned);
			}
			else if (Key == "--checkpoint-every"
				&& ParseUnsigned(Value, Unsigned))
			{
				Out.CheckpointEvery = static_cast<std::uint32_t>(Unsigned);
			}
			else if (Key == "--yaw-step" && ParseDouble(Value, Number))
			{
				Out.YawStep = Number;
			}
			else if (Key == "--pitch-step" && ParseDouble(Value, Number))
			{
				Out.PitchStep = Number;
			}
			else if (Key == "--power-step" && ParseDouble(Value, Number))
			{
				Out.PowerStep = Number;
			}
			else
			{
				Failure = "InvalidOption:" + Key;
				return false;
			}
		}
		if (Out.Output.empty() || !Out.Output.is_absolute()
			|| Out.Threads == 0 || Out.ShardCount == 0
			|| Out.ShardIndex >= Out.ShardCount
			|| Out.CheckpointEvery == 0)
		{
			Failure = "InvalidExecutionContract";
			return false;
		}
		if (Out.Merge && (Out.InputRoot.empty()
			|| !Out.InputRoot.is_absolute()))
		{
			Failure = "MergeRequiresAbsoluteInputRoot";
			return false;
		}
		return true;
	}

	bool MakeGrid(
		const CandidateLayout& Layout,
		const Options& OptionsValue,
		Grid& Out,
		std::string& Failure)
	{
		Out.MinYaw = Layout.Launch.MinimumYawDegrees;
		Out.MaxYaw = Layout.Launch.MaximumYawDegrees;
		Out.MinPitch = Layout.Launch.MinimumPitchDegrees;
		Out.MaxPitch = Layout.Launch.MaximumPitchDegrees;
		Out.MinPower = Layout.Launch.MinimumPower;
		Out.MaxPower = Layout.Launch.MaximumPower;
		Out.YawStep = OptionsValue.YawStep;
		Out.PitchStep = OptionsValue.PitchStep;
		Out.PowerStep = OptionsValue.PowerStep;
		const auto Count = [&Failure](
			const double Minimum,
			const double Maximum,
			const double Step,
			std::int32_t& OutCount)
		{
			if (!(Step > 0.0))
			{
				Failure = "NonPositiveGridStep";
				return false;
			}
			const double Cells = (Maximum - Minimum) / Step;
			const double Rounded = std::round(Cells);
			if (std::abs(Cells - Rounded) > 1.0e-9
				|| Rounded > std::numeric_limits<std::int32_t>::max() - 1)
			{
				Failure = "GridStepDoesNotDivideClosedDomain";
				return false;
			}
			OutCount = static_cast<std::int32_t>(Rounded) + 1;
			return true;
		};
		return Count(Out.MinYaw, Out.MaxYaw, Out.YawStep, Out.YawCount)
			&& Count(
				Out.MinPitch,
				Out.MaxPitch,
				Out.PitchStep,
				Out.PitchCount)
			&& Count(
				Out.MinPower,
				Out.MaxPower,
				Out.PowerStep,
				Out.PowerCount);
	}

	void DecodeIndex(
		const Grid& GridValue,
		const std::uint64_t GlobalIndex,
		Sample& Out)
	{
		const std::uint64_t PitchPower =
			static_cast<std::uint64_t>(GridValue.PitchCount)
			* static_cast<std::uint64_t>(GridValue.PowerCount);
		Out.GlobalIndex = GlobalIndex;
		Out.YawIndex = static_cast<std::int32_t>(
			GlobalIndex / PitchPower);
		const std::uint64_t Remainder = GlobalIndex % PitchPower;
		Out.PitchIndex = static_cast<std::int32_t>(
			Remainder / static_cast<std::uint64_t>(GridValue.PowerCount));
		Out.PowerIndex = static_cast<std::int32_t>(
			Remainder % static_cast<std::uint64_t>(GridValue.PowerCount));
	}

	LaunchInput InputFor(const Grid& GridValue, const Sample& SampleValue)
	{
		return LaunchInput{
			GridValue.MinYaw + SampleValue.YawIndex * GridValue.YawStep,
			GridValue.MinPitch
				+ SampleValue.PitchIndex * GridValue.PitchStep,
			GridValue.MinPower
				+ SampleValue.PowerIndex * GridValue.PowerStep};
	}

	std::string Hex64(const std::uint64_t Value)
	{
		std::ostringstream Stream;
		Stream << "0x" << std::hex << std::setw(16)
			<< std::setfill('0') << Value;
		return Stream.str();
	}

	bool WriteSummary(
		const fs::path& Path,
		const Options& OptionsValue,
		const FrozenCandidateIdentity& Identity,
		const Grid& GridValue,
		const std::vector<Sample>& Samples,
		const bool Complete,
		const std::string& Failure)
	{
		std::array<std::uint64_t, 4> Counts{};
		for (const Sample& SampleValue : Samples)
		{
			for (std::size_t Level = 0; Level < Counts.size(); ++Level)
			{
				if ((SampleValue.PrefixMask & (1u << Level)) != 0)
				{
					++Counts[Level];
				}
			}
		}
		std::ofstream Stream(Path, std::ios::binary | std::ios::trunc);
		if (!Stream)
		{
			return false;
		}
		Stream << std::setprecision(17)
			<< "{\n  \"schema\":\"abts.m11b.v2_2.preflight.v1\",\n"
			<< "  \"complete\":" << (Complete ? "true" : "false") << ",\n"
			<< "  \"candidateRank\":" << Identity.Rank << ",\n"
			<< "  \"candidateSourceHash\":\""
			<< Hex64(Identity.CandidateSourceHash) << "\",\n"
			<< "  \"shardIndex\":" << OptionsValue.ShardIndex << ",\n"
			<< "  \"shardCount\":" << OptionsValue.ShardCount << ",\n"
			<< "  \"grid\":{\"yawCount\":" << GridValue.YawCount
			<< ",\"pitchCount\":" << GridValue.PitchCount
			<< ",\"powerCount\":" << GridValue.PowerCount
			<< ",\"yawStep\":" << GridValue.YawStep
			<< ",\"pitchStep\":" << GridValue.PitchStep
			<< ",\"powerStep\":" << GridValue.PowerStep << "},\n"
			<< "  \"globalSampleCount\":" << GridValue.Count() << ",\n"
			<< "  \"completedShardSamples\":" << Samples.size() << ",\n"
			<< "  \"prefixCounts\":[" << Counts[0] << ',' << Counts[1]
			<< ',' << Counts[2] << ',' << Counts[3] << "],\n"
			<< "  \"failure\":\"" << Failure << "\"\n}\n";
		return Stream.good();
	}

	bool ReadSamples(
		const fs::path& Path,
		std::vector<Sample>& Out,
		std::string& Failure)
	{
		Out.clear();
		std::ifstream Stream(Path);
		if (!Stream)
		{
			return true;
		}
		std::string Line;
		std::getline(Stream, Line);
		while (std::getline(Stream, Line))
		{
			std::istringstream Row(Line);
			Sample Value;
			unsigned int Prefix = 0;
			unsigned int Termination = 0;
			std::string Hash;
			if (!(Row >> Value.GlobalIndex >> Value.YawIndex
				>> Value.PitchIndex >> Value.PowerIndex >> Prefix
				>> Termination >> Value.CompletedAssistCount
				>> Value.TargetContactCount >> Hash))
			{
				Failure = "MalformedSampleRow";
				return false;
			}
			Value.PrefixMask = static_cast<std::uint8_t>(Prefix);
			Value.Termination = static_cast<std::uint8_t>(Termination);
			const auto HashResult = std::from_chars(
				Hash.data() + 2,
				Hash.data() + Hash.size(),
				Value.ResultHash,
				16);
			if (Hash.size() != 18 || HashResult.ec != std::errc())
			{
				Failure = "MalformedSampleHash";
				return false;
			}
			Out.push_back(Value);
		}
		return true;
	}

	std::uint64_t AggregateSampleHash(const std::vector<Sample>& Samples)
	{
		std::uint64_t Hash = 14695981039346656037ull;
		const auto Add = [&Hash](const std::uint64_t Value)
		{
			for (std::uint32_t Byte = 0; Byte < 8; ++Byte)
			{
				Hash ^= (Value >> (Byte * 8)) & 0xffu;
				Hash *= 1099511628211ull;
			}
		};
		for (const Sample& Value : Samples)
		{
			Add(Value.GlobalIndex);
			Add(Value.PrefixMask);
			Add(Value.Termination);
			Add(Value.ResultHash);
		}
		return Hash;
	}

	std::array<std::uint64_t, 4> CountComponents(
		const Grid& GridValue,
		const std::vector<Sample>& Samples)
	{
		std::array<std::uint64_t, 4> Counts{};
		for (std::size_t Level = 0; Level < Counts.size(); ++Level)
		{
			std::vector<std::uint8_t> Visited(Samples.size(), 0);
			for (std::size_t Start = 0; Start < Samples.size(); ++Start)
			{
				if (Visited[Start] != 0
					|| (Samples[Start].PrefixMask & (1u << Level)) == 0)
				{
					continue;
				}
				++Counts[Level];
				std::vector<std::uint64_t> Open{Samples[Start].GlobalIndex};
				Visited[Start] = 1;
				while (!Open.empty())
				{
					const std::uint64_t Index = Open.back();
					Open.pop_back();
					Sample Decoded;
					DecodeIndex(GridValue, Index, Decoded);
					const std::array<std::int32_t, 6> DY{-1, 1, 0, 0, 0, 0};
					const std::array<std::int32_t, 6> DP{0, 0, -1, 1, 0, 0};
					const std::array<std::int32_t, 6> DW{0, 0, 0, 0, -1, 1};
					for (std::size_t Axis = 0; Axis < 6; ++Axis)
					{
						const std::int32_t Y = Decoded.YawIndex + DY[Axis];
						const std::int32_t P = Decoded.PitchIndex + DP[Axis];
						const std::int32_t W = Decoded.PowerIndex + DW[Axis];
						if (Y < 0 || Y >= GridValue.YawCount
							|| P < 0 || P >= GridValue.PitchCount
							|| W < 0 || W >= GridValue.PowerCount)
						{
							continue;
						}
						const std::uint64_t Neighbor =
							(static_cast<std::uint64_t>(Y)
								* GridValue.PitchCount
								+ static_cast<std::uint64_t>(P))
								* GridValue.PowerCount
							+ static_cast<std::uint64_t>(W);
						if (Visited[Neighbor] == 0
							&& (Samples[Neighbor].PrefixMask
								& (1u << Level)) != 0)
						{
							Visited[Neighbor] = 1;
							Open.push_back(Neighbor);
						}
					}
				}
			}
		}
		return Counts;
	}

	int RunMerge(const Options& OptionsValue)
	{
		CandidateLayout Layout;
		FrozenCandidateIdentity Identity;
		if (!ABTS::M11Search::BuildFrozenV4CandidateLayout(
				OptionsValue.Rank, Layout, &Identity))
		{
			std::cerr << "CandidateRankUnavailable\n";
			return 1;
		}
		Grid GridValue;
		std::string Failure;
		if (!MakeGrid(Layout, OptionsValue, GridValue, Failure))
		{
			std::cerr << Failure << '\n';
			return 1;
		}
		std::vector<Sample> Samples;
		for (std::uint32_t Shard = 0; Shard < OptionsValue.ShardCount; ++Shard)
		{
			std::ostringstream Name;
			Name << "shard_" << std::setw(4) << std::setfill('0') << Shard;
			std::vector<Sample> Part;
			if (!ReadSamples(
					OptionsValue.InputRoot / Name.str() / "samples.tsv",
					Part,
					Failure)
				|| Part.empty())
			{
				std::cerr << (Failure.empty() ? "MissingShardSamples" : Failure)
					<< ':' << Shard << '\n';
				return 1;
			}
			for (const Sample& Value : Part)
			{
				if (Value.GlobalIndex % OptionsValue.ShardCount != Shard)
				{
					std::cerr << "ShardOwnershipMismatch:" << Shard << '\n';
					return 1;
				}
			}
			Samples.insert(Samples.end(), Part.begin(), Part.end());
		}
		std::sort(Samples.begin(), Samples.end(), [](const Sample& A, const Sample& B)
		{
			return A.GlobalIndex < B.GlobalIndex;
		});
		if (Samples.size() != GridValue.Count())
		{
			std::cerr << "IncompleteCoverage\n";
			return 1;
		}
		std::array<std::uint64_t, 4> PrefixCounts{};
		std::uint64_t NestingViolations = 0;
		std::array<std::int32_t, 4> MinimumPowerIndex{
			GridValue.PowerCount, GridValue.PowerCount,
			GridValue.PowerCount, GridValue.PowerCount};
		std::array<std::int32_t, 4> MaximumPowerIndex{-1, -1, -1, -1};
		for (std::uint64_t Index = 0; Index < Samples.size(); ++Index)
		{
			const Sample& Value = Samples[Index];
			if (Value.GlobalIndex != Index)
			{
				std::cerr << "DuplicateOrMissingCanonicalIndex:" << Index << '\n';
				return 1;
			}
			if ((Value.PrefixMask & 8u) != 0 && (Value.PrefixMask & 7u) != 7u
				|| (Value.PrefixMask & 4u) != 0 && (Value.PrefixMask & 3u) != 3u
				|| (Value.PrefixMask & 2u) != 0 && (Value.PrefixMask & 1u) == 0)
			{
				++NestingViolations;
			}
			for (std::size_t Level = 0; Level < 4; ++Level)
			{
				if ((Value.PrefixMask & (1u << Level)) != 0)
				{
					++PrefixCounts[Level];
					MinimumPowerIndex[Level] = std::min(
						MinimumPowerIndex[Level], Value.PowerIndex);
					MaximumPowerIndex[Level] = std::max(
						MaximumPowerIndex[Level], Value.PowerIndex);
				}
			}
		}
		const auto Components = CountComponents(GridValue, Samples);
		std::error_code Error;
		fs::create_directories(OptionsValue.Output, Error);
		if (Error)
		{
			std::cerr << "OutputCreateFailed\n";
			return 1;
		}
		const fs::path SamplePath = OptionsValue.Output / "samples.tsv";
		std::ofstream SampleStream(SamplePath, std::ios::trunc);
		SampleStream << "global yaw pitch power prefix termination assists contacts hash\n";
		for (const Sample& Value : Samples)
		{
			SampleStream << Value.GlobalIndex << ' ' << Value.YawIndex << ' '
				<< Value.PitchIndex << ' ' << Value.PowerIndex << ' '
				<< static_cast<unsigned int>(Value.PrefixMask) << ' '
				<< static_cast<unsigned int>(Value.Termination) << ' '
				<< Value.CompletedAssistCount << ' ' << Value.TargetContactCount
				<< ' ' << Hex64(Value.ResultHash) << '\n';
		}
		const bool Passed = PrefixCounts[3] > 0
			&& Components[3] == 1 && NestingViolations == 0;
		std::ofstream Summary(
			OptionsValue.Output / "summary.json",
			std::ios::binary | std::ios::trunc);
		Summary << std::setprecision(17)
			<< "{\n  \"schema\":\"abts.m11b.v2_2.preflight_merged.v1\",\n"
			<< "  \"passed\":" << (Passed ? "true" : "false") << ",\n"
			<< "  \"candidateRank\":" << Identity.Rank << ",\n"
			<< "  \"candidateSourceHash\":\"" << Hex64(Identity.CandidateSourceHash)
			<< "\",\n  \"aggregateSampleHash\":\""
			<< Hex64(AggregateSampleHash(Samples)) << "\",\n"
			<< "  \"grid\":{\"yawCount\":" << GridValue.YawCount
			<< ",\"pitchCount\":" << GridValue.PitchCount
			<< ",\"powerCount\":" << GridValue.PowerCount << "},\n"
			<< "  \"sampleCount\":" << Samples.size() << ",\n"
			<< "  \"prefixCounts\":[" << PrefixCounts[0] << ','
			<< PrefixCounts[1] << ',' << PrefixCounts[2] << ','
			<< PrefixCounts[3] << "],\n  \"componentCounts\":["
			<< Components[0] << ',' << Components[1] << ','
			<< Components[2] << ',' << Components[3] << "],\n"
			<< "  \"minimumPowerIndices\":[" << MinimumPowerIndex[0] << ','
			<< MinimumPowerIndex[1] << ',' << MinimumPowerIndex[2] << ','
			<< MinimumPowerIndex[3] << "],\n  \"maximumPowerIndices\":["
			<< MaximumPowerIndex[0] << ',' << MaximumPowerIndex[1] << ','
			<< MaximumPowerIndex[2] << ',' << MaximumPowerIndex[3] << "],\n"
			<< "  \"nestingViolations\":" << NestingViolations << "\n}\n";
		std::cout << "[ABTS][M11-B-v2.2][Merge] Passed=" << Passed
			<< " Prefix=" << PrefixCounts[0] << ',' << PrefixCounts[1] << ','
			<< PrefixCounts[2] << ',' << PrefixCounts[3] << " Components="
			<< Components[0] << ',' << Components[1] << ',' << Components[2]
			<< ',' << Components[3] << '\n';
		return Passed ? 0 : 2;
	}

	int RunPreflight(const Options& OptionsValue)
	{
		CandidateLayout Layout;
		FrozenCandidateIdentity Identity;
		if (!ABTS::M11Search::BuildFrozenV4CandidateLayout(
				OptionsValue.Rank,
				Layout,
				&Identity))
		{
			std::cerr << "CandidateRankUnavailable\n";
			return 1;
		}
		const CandidateSearchContract Contract =
			CandidateSearchContract::MakeV2_1();
		if (ABTS::M11Search::ComputeCandidateSourceHash(Layout, Contract)
			!= Identity.CandidateSourceHash)
		{
			std::cerr << "CandidateSourceIdentityMismatch\n";
			return 1;
		}
		Grid GridValue;
		std::string Failure;
		if (!MakeGrid(Layout, OptionsValue, GridValue, Failure))
		{
			std::cerr << Failure << '\n';
			return 1;
		}
		std::error_code Error;
		fs::create_directories(OptionsValue.Output, Error);
		if (Error)
		{
			std::cerr << "OutputCreateFailed\n";
			return 1;
		}
		const fs::path SamplePath = OptionsValue.Output / "samples.tsv";
		std::vector<Sample> Existing;
		if (OptionsValue.Resume
			&& !ReadSamples(SamplePath, Existing, Failure))
		{
			std::cerr << Failure << '\n';
			return 1;
		}
		std::uint64_t NextGlobal = OptionsValue.ShardIndex;
		if (!Existing.empty())
		{
			NextGlobal = Existing.back().GlobalIndex
				+ OptionsValue.ShardCount;
		}
		std::ofstream SamplesOut(
			SamplePath,
			OptionsValue.Resume && !Existing.empty()
				? std::ios::app
				: std::ios::trunc);
		if (!SamplesOut)
		{
			std::cerr << "SampleOutputOpenFailed\n";
			return 1;
		}
		if (Existing.empty())
		{
			SamplesOut << "global yaw pitch power prefix termination "
				"assists contacts hash\n";
		}
		std::vector<Sample> All = Existing;
		const std::uint64_t Total = GridValue.Count();
		while (NextGlobal < Total)
		{
			std::vector<std::uint64_t> Indices;
			Indices.reserve(OptionsValue.CheckpointEvery);
			for (std::uint64_t Index = NextGlobal;
				Index < Total
					&& Indices.size() < OptionsValue.CheckpointEvery;
				Index += OptionsValue.ShardCount)
			{
				Indices.push_back(Index);
			}
			std::vector<Sample> Chunk(Indices.size());
			std::vector<std::string> Failures(Indices.size());
			std::atomic<std::size_t> Cursor{0};
			std::vector<std::thread> Workers;
			const std::size_t WorkerCount = std::min<std::size_t>(
				OptionsValue.Threads,
				Indices.size());
			for (std::size_t WorkerIndex = 0;
				WorkerIndex < WorkerCount;
				++WorkerIndex)
			{
				Workers.emplace_back([&]()
				{
					while (true)
					{
						const std::size_t Local = Cursor.fetch_add(1);
						if (Local >= Indices.size())
						{
							return;
						}
						Sample Value;
						DecodeIndex(GridValue, Indices[Local], Value);
						InputEvaluation Evaluation;
						if (!CandidateSearch::EvaluateInput(
								Layout,
								Contract,
								InputFor(GridValue, Value),
								0x7u,
								Evaluation,
								&Failures[Local]))
						{
							continue;
						}
						for (std::size_t Level = 0; Level < 4; ++Level)
						{
							if (Evaluation.PrefixMembership[Level])
							{
								Value.PrefixMask |=
									static_cast<std::uint8_t>(1u << Level);
							}
						}
						Value.Termination = static_cast<std::uint8_t>(
							Evaluation.Termination);
						Value.CompletedAssistCount =
							Evaluation.CompletedAssistCount;
						Value.TargetContactCount =
							Evaluation.TargetContactCount;
						Value.ResultHash = Evaluation.ResultHash;
						Chunk[Local] = Value;
					}
				});
			}
			for (std::thread& Worker : Workers)
			{
				Worker.join();
			}
			for (std::size_t Local = 0; Local < Chunk.size(); ++Local)
			{
				if (!Failures[Local].empty())
				{
					std::cerr << "SolveFailed:" << Indices[Local]
						<< ':' << Failures[Local] << '\n';
					return 1;
				}
				const Sample& Value = Chunk[Local];
				SamplesOut << Value.GlobalIndex << ' ' << Value.YawIndex
					<< ' ' << Value.PitchIndex << ' ' << Value.PowerIndex
					<< ' ' << static_cast<unsigned int>(Value.PrefixMask)
					<< ' ' << static_cast<unsigned int>(Value.Termination)
					<< ' ' << Value.CompletedAssistCount << ' '
					<< Value.TargetContactCount << ' '
					<< Hex64(Value.ResultHash) << '\n';
				All.push_back(Value);
			}
			SamplesOut.flush();
			NextGlobal = Indices.back() + OptionsValue.ShardCount;
			WriteSummary(
				OptionsValue.Output / "summary.json",
				OptionsValue,
				Identity,
				GridValue,
				All,
				NextGlobal >= Total,
				"");
			std::cout << "[ABTS][M11-B-v2.2][Preflight] Shard="
				<< OptionsValue.ShardIndex << '/' << OptionsValue.ShardCount
				<< " Completed=" << All.size() << '\n';
		}
		return 0;
	}
}

int main(const int Argc, char** Argv)
{
	Options OptionsValue;
	std::string Failure;
	if (!ParseOptions(Argc, Argv, OptionsValue, Failure))
	{
		std::cerr << Failure << '\n';
		return 1;
	}
	if (OptionsValue.Merge)
	{
		return RunMerge(OptionsValue);
	}
	return RunPreflight(OptionsValue);
}
