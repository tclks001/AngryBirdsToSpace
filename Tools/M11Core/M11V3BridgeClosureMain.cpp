// Copyright Epic Games, Inc. All Rights Reserved.

#include "M11Search/ABTSM11CandidateSearch.h"
#include "M11Search/ABTSM11FrozenCandidateLayouts.h"
#include "M11V3ConnectivityClosure.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
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
	using ABTS::M11V3::BridgeClosureEvidence;
	using ABTS::M11V3::BridgeClosurePolicy;
	using ABTS::M11V3::ClosureResult;
	using ABTS::M11V3::DiscoveryPlan;
	using ABTS::M11V3::GridShape;
	using ABTS::M11V3::PendingBridgeEdge;

	struct Options
	{
		bool SelfTest = false;
		std::int32_t Rank = 12;
		fs::path Samples;
		fs::path Output;
		std::uint32_t Threads = 1;
		double MinYaw = -18.0;
		double MinPitch = 0.0;
		double MinPower = 0.0;
		double YawStep = 1.0;
		double PitchStep = 1.5;
		double PowerStep = 0.0125;
		std::int32_t YawCount = 37;
		std::int32_t PitchCount = 41;
		std::int32_t PowerCount = 81;
	};

	struct CoarseSample
	{
		std::uint64_t GlobalIndex = 0;
		std::int32_t Yaw = 0;
		std::int32_t Pitch = 0;
		std::int32_t Power = 0;
		std::uint8_t PrefixMask = 0;
	};

	struct FineSample
	{
		std::int32_t FineYaw = 0;
		std::int32_t FinePitch = 0;
		std::int32_t FinePower = 0;
		bool IsF4 = false;
		std::uint64_t ResultHash = 0;
	};

	struct BridgeRegion
	{
		std::int32_t MinYaw = 0;
		std::int32_t MaxYaw = 0;
		std::int32_t MinPitch = 0;
		std::int32_t MaxPitch = 0;
		std::int32_t MinPower = 0;
		std::int32_t MaxPower = 0;
		std::int32_t YawCount = 0;
		std::int32_t PitchCount = 0;
		std::int32_t PowerCount = 0;

		std::int32_t Flatten(
			const std::int32_t Yaw,
			const std::int32_t Pitch,
			const std::int32_t Power) const
		{
			return (Yaw - MinYaw) + YawCount
				* ((Pitch - MinPitch) + PitchCount * (Power - MinPower));
		}
	};

	constexpr std::uint64_t FnvOffset = 1469598103934665603ull;
	constexpr std::uint64_t FnvPrime = 1099511628211ull;

	struct Hash
	{
		std::uint64_t Value = FnvOffset;

		template <typename T>
		void Add(const T& Pod)
		{
			static_assert(std::is_trivially_copyable_v<T>);
			static_assert(std::endian::native == std::endian::little);
			const auto* Bytes = reinterpret_cast<const std::uint8_t*>(&Pod);
			for (std::size_t Index = 0; Index < sizeof(T); ++Index)
			{
				Value ^= Bytes[Index];
				Value *= FnvPrime;
			}
		}
	};

	std::string Hex64(const std::uint64_t Value)
	{
		std::ostringstream Stream;
		Stream << "0x" << std::hex << std::setw(16) << std::setfill('0') << Value;
		return Stream.str();
	}

	bool ParseUnsigned(const std::string& Text, std::uint64_t& Out)
	{
		const auto Result = std::from_chars(
			Text.data(), Text.data() + Text.size(), Out);
		return Result.ec == std::errc() && Result.ptr == Text.data() + Text.size();
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
		if (Command == "self-test")
		{
			Out.SelfTest = true;
		}
		else if (Command != "close")
		{
			Failure = "UnknownCommand";
			return false;
		}
		for (int Index = 2; Index < Argc; ++Index)
		{
			const std::string Key = Argv[Index];
			if (Index + 1 >= Argc)
			{
				Failure = "MissingValue:" + Key;
				return false;
			}
			const std::string Value = Argv[++Index];
			std::uint64_t Unsigned = 0;
			double Number = 0.0;
			if (Key == "--samples") Out.Samples = Value;
			else if (Key == "--output") Out.Output = Value;
			else if (Key == "--rank" && ParseUnsigned(Value, Unsigned))
				Out.Rank = static_cast<std::int32_t>(Unsigned);
			else if (Key == "--threads" && ParseUnsigned(Value, Unsigned))
				Out.Threads = static_cast<std::uint32_t>(Unsigned);
			else if (Key == "--yaw-count" && ParseUnsigned(Value, Unsigned))
				Out.YawCount = static_cast<std::int32_t>(Unsigned);
			else if (Key == "--pitch-count" && ParseUnsigned(Value, Unsigned))
				Out.PitchCount = static_cast<std::int32_t>(Unsigned);
			else if (Key == "--power-count" && ParseUnsigned(Value, Unsigned))
				Out.PowerCount = static_cast<std::int32_t>(Unsigned);
			else if (Key == "--min-yaw" && ParseDouble(Value, Number)) Out.MinYaw = Number;
			else if (Key == "--min-pitch" && ParseDouble(Value, Number)) Out.MinPitch = Number;
			else if (Key == "--min-power" && ParseDouble(Value, Number)) Out.MinPower = Number;
			else if (Key == "--yaw-step" && ParseDouble(Value, Number)) Out.YawStep = Number;
			else if (Key == "--pitch-step" && ParseDouble(Value, Number)) Out.PitchStep = Number;
			else if (Key == "--power-step" && ParseDouble(Value, Number)) Out.PowerStep = Number;
			else
			{
				Failure = "InvalidOption:" + Key;
				return false;
			}
		}
		if (!Out.SelfTest && (Out.Samples.empty() || Out.Output.empty()
			|| Out.Threads == 0 || Out.YawStep <= 0.0 || Out.PitchStep <= 0.0
			|| Out.PowerStep <= 0.0))
		{
			Failure = "InvalidCloseOptions";
			return false;
		}
		return true;
	}

	bool ReadCoarseSamples(
		const Options& OptionsValue,
		std::vector<CoarseSample>& Out,
		std::string& Failure)
	{
		std::ifstream Stream(OptionsValue.Samples);
		if (!Stream)
		{
			Failure = "SamplesOpenFailed";
			return false;
		}
		std::string Line;
		std::getline(Stream, Line);
		while (std::getline(Stream, Line))
		{
			std::istringstream Row(Line);
			CoarseSample Value;
			unsigned int Prefix = 0;
			unsigned int Termination = 0;
			std::int32_t Assists = 0;
			std::int32_t Contacts = 0;
			std::string ResultHash;
			if (!(Row >> Value.GlobalIndex >> Value.Yaw >> Value.Pitch >> Value.Power
				>> Prefix >> Termination >> Assists >> Contacts >> ResultHash))
			{
				Failure = "MalformedSampleRow";
				return false;
			}
			Value.PrefixMask = static_cast<std::uint8_t>(Prefix);
			Out.push_back(Value);
		}
		const std::uint64_t Expected = static_cast<std::uint64_t>(OptionsValue.YawCount)
			* OptionsValue.PitchCount * OptionsValue.PowerCount;
		if (Out.size() != Expected)
		{
			Failure = "CoarseSampleCountMismatch";
			return false;
		}
		std::sort(Out.begin(), Out.end(), [](const CoarseSample& A, const CoarseSample& B)
		{
			return A.GlobalIndex < B.GlobalIndex;
		});
		for (std::uint64_t Index = 0; Index < Out.size(); ++Index)
		{
			const CoarseSample& Value = Out[Index];
			const std::uint64_t ExpectedGlobal =
				(static_cast<std::uint64_t>(Value.Yaw) * OptionsValue.PitchCount
					+ Value.Pitch) * OptionsValue.PowerCount + Value.Power;
			if (Value.GlobalIndex != Index || Value.GlobalIndex != ExpectedGlobal)
			{
				Failure = "CoarseCanonicalIndexMismatch";
				return false;
			}
		}
		return true;
	}

	BridgeRegion MakeRegion(
		const PendingBridgeEdge& Edge,
		const GridShape& Shape,
		const BridgeClosurePolicy& Policy,
		const Options& OptionsValue,
		const std::int32_t Divisions)
	{
		std::int32_t AY = 0, AP = 0, AW = 0;
		std::int32_t BY = 0, BP = 0, BW = 0;
		ABTS::M11V3::Unflatten(Edge.SampleA, Shape, AY, AP, AW);
		ABTS::M11V3::Unflatten(Edge.SampleB, Shape, BY, BP, BW);
		const std::int32_t HaloYaw = Policy.RegionHaloFinalCells
			* static_cast<std::int32_t>(std::ceil(
				Policy.FinalYawPrecisionDegrees
				/ (OptionsValue.YawStep / Divisions)));
		const std::int32_t HaloPitch = Policy.RegionHaloFinalCells
			* static_cast<std::int32_t>(std::ceil(
				Policy.FinalPitchPrecisionDegrees
				/ (OptionsValue.PitchStep / Divisions)));
		const std::int32_t HaloPower = Policy.RegionHaloFinalCells
			* static_cast<std::int32_t>(std::ceil(
				Policy.FinalPowerPrecision
				/ (OptionsValue.PowerStep / Divisions)));
		BridgeRegion Region;
		Region.MinYaw = std::max(0, std::min(AY, BY) * Divisions - HaloYaw);
		Region.MaxYaw = std::min((Shape.YawCount - 1) * Divisions,
			std::max(AY, BY) * Divisions + HaloYaw);
		Region.MinPitch = std::max(0, std::min(AP, BP) * Divisions - HaloPitch);
		Region.MaxPitch = std::min((Shape.PitchCount - 1) * Divisions,
			std::max(AP, BP) * Divisions + HaloPitch);
		Region.MinPower = std::max(0, std::min(AW, BW) * Divisions - HaloPower);
		Region.MaxPower = std::min((Shape.PowerCount - 1) * Divisions,
			std::max(AW, BW) * Divisions + HaloPower);
		Region.YawCount = Region.MaxYaw - Region.MinYaw + 1;
		Region.PitchCount = Region.MaxPitch - Region.MinPitch + 1;
		Region.PowerCount = Region.MaxPower - Region.MinPower + 1;
		return Region;
	}

	bool EvaluateRegion(
		const Options& OptionsValue,
		const CandidateLayout& Layout,
		const CandidateSearchContract& Contract,
		const BridgeRegion& Region,
		const std::int32_t Divisions,
		std::vector<FineSample>& Out,
		std::string& Failure)
	{
		const std::int32_t Count = Region.YawCount * Region.PitchCount
			* Region.PowerCount;
		Out.resize(Count);
		std::atomic<std::int32_t> Next{0};
		std::atomic<bool> Failed{false};
		std::vector<std::string> ThreadFailures(OptionsValue.Threads);
		std::vector<std::thread> Workers;
		Workers.reserve(OptionsValue.Threads);
		for (std::uint32_t Worker = 0; Worker < OptionsValue.Threads; ++Worker)
		{
			Workers.emplace_back([&, Worker]()
			{
				while (!Failed.load(std::memory_order_relaxed))
				{
					const std::int32_t Index = Next.fetch_add(1);
					if (Index >= Count) break;
					const std::int32_t LocalYaw = Index % Region.YawCount;
					const std::int32_t Remainder = Index / Region.YawCount;
					const std::int32_t LocalPitch = Remainder % Region.PitchCount;
					const std::int32_t LocalPower = Remainder / Region.PitchCount;
					FineSample& Sample = Out[Index];
					Sample.FineYaw = Region.MinYaw + LocalYaw;
					Sample.FinePitch = Region.MinPitch + LocalPitch;
					Sample.FinePower = Region.MinPower + LocalPower;
					const LaunchInput Input{
						OptionsValue.MinYaw + Sample.FineYaw * OptionsValue.YawStep / Divisions,
						OptionsValue.MinPitch + Sample.FinePitch * OptionsValue.PitchStep / Divisions,
						OptionsValue.MinPower + Sample.FinePower * OptionsValue.PowerStep / Divisions};
					InputEvaluation Evaluation;
					if (!CandidateSearch::EvaluateInput(
						Layout, Contract, Input, 0x7u, Evaluation, &ThreadFailures[Worker]))
					{
						Failed.store(true);
						break;
					}
					Sample.IsF4 = Evaluation.PrefixMembership[3]
						&& Evaluation.HasOrderedTerminalHit;
					Sample.ResultHash = Evaluation.ResultHash;
				}
			});
		}
		for (std::thread& Worker : Workers) Worker.join();
		if (Failed.load())
		{
			for (const std::string& Item : ThreadFailures)
			{
				if (!Item.empty())
				{
					Failure = "BridgeSolveFailed:" + Item;
					break;
				}
			}
			return false;
		}
		return true;
	}

	BridgeClosureEvidence BuildEvidence(
		const PendingBridgeEdge& Edge,
		const GridShape& Shape,
		const BridgeClosurePolicy& Policy,
		const BridgeRegion& Region,
		const std::int32_t Divisions,
		const std::vector<FineSample>& Samples,
		std::vector<std::int32_t>& OutPath)
	{
		BridgeClosureEvidence Evidence;
		Evidence.EdgeHash = Edge.EdgeHash;
		Evidence.PolicyHash = Policy.ComputePolicyHash();
		Evidence.RecursionDepth = Policy.MaximumRecursionDepth;
		Evidence.SampleCount = static_cast<std::int32_t>(Samples.size());
		Evidence.ReachedYawPrecisionDegrees = Policy.FinalYawPrecisionDegrees;
		Evidence.ReachedPitchPrecisionDegrees = Policy.FinalPitchPrecisionDegrees;
		Evidence.ReachedPowerPrecision = Policy.FinalPowerPrecision;
		Evidence.ReachedFinalPrecision = false;
		std::int32_t AY = 0, AP = 0, AW = 0;
		std::int32_t BY = 0, BP = 0, BW = 0;
		ABTS::M11V3::Unflatten(Edge.SampleA, Shape, AY, AP, AW);
		ABTS::M11V3::Unflatten(Edge.SampleB, Shape, BY, BP, BW);
		const std::int32_t Start = Region.Flatten(AY * Divisions, AP * Divisions, AW * Divisions);
		const std::int32_t Goal = Region.Flatten(BY * Divisions, BP * Divisions, BW * Divisions);

		Hash VisitHash;
		VisitHash.Add(static_cast<std::uint32_t>(0x11b3e101u));
		VisitHash.Add(Edge.EdgeHash);
		for (const FineSample& Sample : Samples)
		{
			VisitHash.Add(Sample.FineYaw);
			VisitHash.Add(Sample.FinePitch);
			VisitHash.Add(Sample.FinePower);
			VisitHash.Add(Sample.ResultHash);
			VisitHash.Add(static_cast<std::uint8_t>(Sample.IsF4 ? 1u : 0u));
		}
		Evidence.VisitOrderHash = VisitHash.Value;

		std::vector<std::int32_t> Parent(Samples.size(), -1);
		std::queue<std::int32_t> Open;
		if (Samples[Start].IsF4)
		{
			Parent[Start] = Start;
			Open.push(Start);
		}
		constexpr std::int32_t DY[6] = {-1, 1, 0, 0, 0, 0};
		constexpr std::int32_t DP[6] = {0, 0, -1, 1, 0, 0};
		constexpr std::int32_t DW[6] = {0, 0, 0, 0, -1, 1};
		while (!Open.empty() && Parent[Goal] < 0)
		{
			const std::int32_t Current = Open.front();
			Open.pop();
			const FineSample& Point = Samples[Current];
			for (std::int32_t Direction = 0; Direction < 6; ++Direction)
			{
				const std::int32_t Yaw = Point.FineYaw + DY[Direction];
				const std::int32_t Pitch = Point.FinePitch + DP[Direction];
				const std::int32_t Power = Point.FinePower + DW[Direction];
				if (Yaw < Region.MinYaw || Yaw > Region.MaxYaw
					|| Pitch < Region.MinPitch || Pitch > Region.MaxPitch
					|| Power < Region.MinPower || Power > Region.MaxPower)
				{
					continue;
				}
				const std::int32_t Neighbor = Region.Flatten(Yaw, Pitch, Power);
				if (Parent[Neighbor] < 0 && Samples[Neighbor].IsF4)
				{
					Parent[Neighbor] = Current;
					Open.push(Neighbor);
				}
			}
		}
		if (Parent[Goal] >= 0)
		{
			for (std::int32_t Current = Goal;; Current = Parent[Current])
			{
				OutPath.push_back(Current);
				if (Current == Start) break;
			}
			std::reverse(OutPath.begin(), OutPath.end());
		}
		Evidence.PathSampleCount = static_cast<std::int32_t>(OutPath.size());
		Evidence.ProvenContinuousF4Path = OutPath.size() >= 2;
		Hash PathHash;
		PathHash.Add(static_cast<std::uint32_t>(0x11b3e102u));
		PathHash.Add(Edge.EdgeHash);
		PathHash.Add(Evidence.PathSampleCount);
		for (const std::int32_t Index : OutPath)
		{
			PathHash.Add(Index);
			PathHash.Add(Samples[Index].ResultHash);
		}
		Evidence.ContinuousPathHash = PathHash.Value;
		Evidence.EvidenceHash = Evidence.ComputeEvidenceHash();
		return Evidence;
	}

	bool WritePlan(const fs::path& Path, const DiscoveryPlan& Plan)
	{
		std::ofstream Stream(Path, std::ios::binary | std::ios::trunc);
		Stream << "{\n  \"schema\":\"abts.m11b.v3.connectivity_discovery.v1\",\n"
			<< "  \"prefixLevel\":" << Plan.PrefixLevel << ",\n"
			<< "  \"shape\":[" << Plan.Shape.YawCount << ',' << Plan.Shape.PitchCount
			<< ',' << Plan.Shape.PowerCount << "],\n"
			<< "  \"activeSampleCount\":" << Plan.ActiveSampleCount << ",\n"
			<< "  \"faceComponentCount\":" << Plan.FaceComponentCount << ",\n"
			<< "  \"discoveryComponentCount\":" << Plan.DiscoveryComponentCount << ",\n"
			<< "  \"activeMaskHash\":\"" << Hex64(Plan.ActiveMaskHash) << "\",\n"
			<< "  \"planHash\":\"" << Hex64(Plan.PlanHash) << "\",\n"
			<< "  \"requiredBridgeEdges\":[\n";
		for (std::size_t Index = 0; Index < Plan.RequiredBridgeEdges.size(); ++Index)
		{
			const PendingBridgeEdge& Edge = Plan.RequiredBridgeEdges[Index];
			Stream << "    {\"sampleA\":" << Edge.SampleA << ",\"sampleB\":"
				<< Edge.SampleB << ",\"faceComponentA\":" << Edge.FaceComponentA
				<< ",\"faceComponentB\":" << Edge.FaceComponentB
				<< ",\"changedAxisMask\":" << static_cast<unsigned int>(Edge.ChangedAxisMask)
				<< ",\"edgeHash\":\"" << Hex64(Edge.EdgeHash) << "\"}"
				<< (Index + 1 == Plan.RequiredBridgeEdges.size() ? "\n" : ",\n");
		}
		Stream << "  ]\n}\n";
		return Stream.good();
	}

	bool WriteBridge(
		const fs::path& Root,
		const std::size_t BridgeIndex,
		const PendingBridgeEdge& Edge,
		const BridgeRegion& Region,
		const std::vector<FineSample>& Samples,
		const std::vector<std::int32_t>& Path,
		const BridgeClosureEvidence& Evidence)
	{
		std::ostringstream DirectoryName;
		DirectoryName << "bridge_" << std::setw(4) << std::setfill('0') << BridgeIndex;
		const fs::path Directory = Root / DirectoryName.str();
		std::error_code Error;
		fs::create_directories(Directory, Error);
		if (Error) return false;
		std::vector<std::uint8_t> IsPath(Samples.size(), 0);
		for (const std::int32_t Index : Path) IsPath[Index] = 1;
		std::ofstream Tsv(Directory / "samples.tsv", std::ios::trunc);
		Tsv << "index fine_yaw fine_pitch fine_power f4 path result_hash\n";
		for (std::size_t Index = 0; Index < Samples.size(); ++Index)
		{
			const FineSample& Sample = Samples[Index];
			Tsv << Index << ' ' << Sample.FineYaw << ' ' << Sample.FinePitch << ' '
				<< Sample.FinePower << ' ' << Sample.IsF4 << ' '
				<< static_cast<unsigned int>(IsPath[Index]) << ' '
				<< Hex64(Sample.ResultHash) << '\n';
		}
		std::ofstream Json(Directory / "evidence.json", std::ios::binary | std::ios::trunc);
		Json << std::setprecision(17)
			<< "{\n  \"schema\":\"abts.m11b.v3.bridge_closure_evidence.v1\",\n"
			<< "  \"edgeHash\":\"" << Hex64(Evidence.EdgeHash) << "\",\n"
			<< "  \"policyHash\":\"" << Hex64(Evidence.PolicyHash) << "\",\n"
			<< "  \"sampleA\":" << Edge.SampleA << ",\n  \"sampleB\":" << Edge.SampleB << ",\n"
			<< "  \"regionFineBounds\":[[" << Region.MinYaw << ',' << Region.MaxYaw
			<< "],[" << Region.MinPitch << ',' << Region.MaxPitch << "],["
			<< Region.MinPower << ',' << Region.MaxPower << "]],\n"
			<< "  \"recursionDepth\":" << Evidence.RecursionDepth << ",\n"
			<< "  \"sampleCount\":" << Evidence.SampleCount << ",\n"
			<< "  \"pathSampleCount\":" << Evidence.PathSampleCount << ",\n"
			<< "  \"reachedPrecision\":[" << Evidence.ReachedYawPrecisionDegrees << ','
			<< Evidence.ReachedPitchPrecisionDegrees << ',' << Evidence.ReachedPowerPrecision << "],\n"
			<< "  \"visitOrderHash\":\"" << Hex64(Evidence.VisitOrderHash) << "\",\n"
			<< "  \"continuousPathHash\":\"" << Hex64(Evidence.ContinuousPathHash) << "\",\n"
			<< "  \"reachedFinalPrecision\":" << (Evidence.ReachedFinalPrecision ? "true" : "false") << ",\n"
			<< "  \"provenContinuousF4Path\":" << (Evidence.ProvenContinuousF4Path ? "true" : "false") << ",\n"
			<< "  \"evidenceHash\":\"" << Hex64(Evidence.EvidenceHash) << "\"\n}\n";
		return Tsv.good() && Json.good();
	}

	int RunSelfTest()
	{
		std::string Failure;
		const bool Passed = ABTS::M11V3::RunContractSelfTest(&Failure);
		const GridShape Shape{3, 3, 1};
		std::vector<std::uint8_t> Mask(9, 0);
		Mask[0] = Mask[4] = Mask[8] = 1;
		DiscoveryPlan Plan;
		const bool PlanBuilt = ABTS::M11V3::BuildDiscoveryPlan18(
			Mask, Shape, 4, Plan, nullptr);
		BridgeClosurePolicy Policy;
		std::cout << "{\"schema\":\"abts.m11b.v3.portable_contract_self_test.v1\","
			<< "\"passed\":" << (Passed && PlanBuilt ? "true" : "false")
			<< ",\"planHash\":\"" << Hex64(Plan.PlanHash)
			<< "\",\"policyHash\":\"" << Hex64(Policy.ComputePolicyHash())
			<< "\",\"failure\":\"" << Failure << "\"}\n";
		return Passed && PlanBuilt ? 0 : 1;
	}

	int RunClose(const Options& OptionsValue)
	{
		CandidateLayout Layout;
		FrozenCandidateIdentity Identity;
		if (!ABTS::M11Search::BuildFrozenV4CandidateLayout(
			OptionsValue.Rank, Layout, &Identity))
		{
			std::cerr << "CandidateRankUnavailable\n";
			return 1;
		}
		const CandidateSearchContract Contract = CandidateSearchContract::MakeV2_1();
		if (ABTS::M11Search::ComputeCandidateSourceHash(Layout, Contract)
			!= Identity.CandidateSourceHash)
		{
			std::cerr << "CandidateSourceIdentityMismatch\n";
			return 1;
		}
		const GridShape Shape{
			OptionsValue.YawCount, OptionsValue.PitchCount, OptionsValue.PowerCount};
		if (!Shape.IsValid())
		{
			std::cerr << "InvalidGridShape\n";
			return 1;
		}
		std::vector<CoarseSample> CoarseSamples;
		std::string Failure;
		if (!ReadCoarseSamples(OptionsValue, CoarseSamples, Failure))
		{
			std::cerr << Failure << '\n';
			return 1;
		}
		std::vector<std::uint8_t> ActiveMask(Shape.GetSampleCount(), 0);
		for (const CoarseSample& Sample : CoarseSamples)
		{
			const std::int32_t ContractIndex = ABTS::M11V3::Flatten(
				Sample.Yaw, Sample.Pitch, Sample.Power, Shape);
			ActiveMask[ContractIndex] = (Sample.PrefixMask & 8u) != 0 ? 1u : 0u;
		}
		DiscoveryPlan Plan;
		if (!ABTS::M11V3::BuildDiscoveryPlan18(
			ActiveMask, Shape, 4, Plan, &Failure))
		{
			std::cerr << Failure << '\n';
			return 1;
		}
		std::error_code Error;
		fs::create_directories(OptionsValue.Output, Error);
		if (Error || !WritePlan(OptionsValue.Output / "connectivity_plan.json", Plan))
		{
			std::cerr << "PlanWriteFailed\n";
			return 1;
		}
		const BridgeClosurePolicy Policy;
		constexpr std::int32_t Divisions = 1 << 3;
		const bool ReachedPrecision = OptionsValue.YawStep / Divisions
			<= Policy.FinalYawPrecisionDegrees + 1.0e-12
			&& OptionsValue.PitchStep / Divisions
				<= Policy.FinalPitchPrecisionDegrees + 1.0e-12
			&& OptionsValue.PowerStep / Divisions
				<= Policy.FinalPowerPrecision + 1.0e-12;
		std::vector<BridgeClosureEvidence> Evidence;
		for (std::size_t Index = 0; Index < Plan.RequiredBridgeEdges.size(); ++Index)
		{
			const PendingBridgeEdge& Edge = Plan.RequiredBridgeEdges[Index];
			const BridgeRegion Region = MakeRegion(
				Edge, Shape, Policy, OptionsValue, Divisions);
			std::vector<FineSample> FineSamples;
			if (!EvaluateRegion(
				OptionsValue, Layout, Contract, Region, Divisions, FineSamples, Failure))
			{
				std::cerr << Failure << '\n';
				return 1;
			}
			std::vector<std::int32_t> Path;
			BridgeClosureEvidence Item = BuildEvidence(
				Edge, Shape, Policy, Region, Divisions, FineSamples, Path);
			Item.ReachedFinalPrecision = ReachedPrecision;
			Item.EvidenceHash = Item.ComputeEvidenceHash();
			if (!WriteBridge(
				OptionsValue.Output, Index, Edge, Region, FineSamples, Path, Item))
			{
				std::cerr << "BridgeEvidenceWriteFailed\n";
				return 1;
			}
			Evidence.push_back(Item);
			std::cout << "[ABTS][M11-B-v3][Bridge] " << (Index + 1) << '/'
				<< Plan.RequiredBridgeEdges.size() << " Edge=" << Hex64(Edge.EdgeHash)
				<< " Samples=" << Item.SampleCount << " Path=" << Item.PathSampleCount
				<< " Passed=" << Item.ProvenContinuousF4Path << '\n';
		}

		ClosureResult Result;
		const bool Passed = ABTS::M11V3::CloseWithEvidence(
			Plan, Policy, Evidence, Result, &Failure);
		std::ofstream Summary(
			OptionsValue.Output / "closure_result.json",
			std::ios::binary | std::ios::trunc);
		Summary << "{\n  \"schema\":\"abts.m11b.v3.connectivity_closure_result.v1\",\n"
			<< "  \"passed\":" << (Result.Passed ? "true" : "false") << ",\n"
			<< "  \"candidateRank\":" << Identity.Rank << ",\n"
			<< "  \"candidateSourceHash\":\"" << Hex64(Identity.CandidateSourceHash) << "\",\n"
			<< "  \"planHash\":\"" << Hex64(Result.PlanHash) << "\",\n"
			<< "  \"policyHash\":\"" << Hex64(Result.PolicyHash) << "\",\n"
			<< "  \"requiredBridgeCount\":" << Result.RequiredBridgeCount << ",\n"
			<< "  \"provenBridgeCount\":" << Result.ProvenBridgeCount << ",\n"
			<< "  \"finalComponentCount\":" << Result.FinalComponentCount << ",\n"
			<< "  \"bridgeEvidenceAggregateHash\":\""
			<< Hex64(Result.BridgeEvidenceAggregateHash) << "\",\n"
			<< "  \"evidenceComplete\":" << (Result.EvidenceComplete ? "true" : "false") << ",\n"
			<< "  \"resultHash\":\"" << Hex64(Result.ResultHash) << "\",\n"
			<< "  \"failure\":\"" << Result.Failure << "\"\n}\n";
		if (!Summary.good())
		{
			std::cerr << "ClosureResultWriteFailed\n";
			return 1;
		}
		std::cout << "[ABTS][M11-B-v3][Closure] Passed=" << Passed
			<< " Face=" << Plan.FaceComponentCount
			<< " Discovery=" << Plan.DiscoveryComponentCount
			<< " Proven=" << Result.ProvenBridgeCount << '/'
			<< Result.RequiredBridgeCount << " Final=" << Result.FinalComponentCount
			<< " Failure=" << Result.Failure << '\n';
		return Passed ? 0 : 2;
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
	return OptionsValue.SelfTest ? RunSelfTest() : RunClose(OptionsValue);
}
