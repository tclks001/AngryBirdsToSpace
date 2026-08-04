// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Building/ABTSM73BeamAGenerator.h"
#include "Building/ABTSM73BeamBGenerator.h"
#include "Building/ABTSM73DAG5BShapeGrammarV2.h"
#include "Misc/AutomationTest.h"

namespace ABTSM73BeamBTests
{
	bool GenerateUpstream(
		const FABTSM73BeamBPreviewSettings& Settings,
		FABTSM73DAG5BV2GenerationResult& OutSilhouette,
		FABTSM73BeamAGenerationResult& OutBeamA,
		FString& OutError)
	{
		FABTSM73DAG5BShapeGrammarV2 ShapeGenerator;
		if (!ShapeGenerator.Generate(Settings.BeamA.Silhouette,
			OutSilhouette, OutError))
		{
			return false;
		}
		FABTSM73BeamAGenerator BeamAGenerator;
		return BeamAGenerator.Generate(Settings.BeamA, OutSilhouette,
			OutBeamA, OutError);
	}

	bool Generate(
		const FABTSM73BeamBPreviewSettings& Settings,
		FABTSM73BeamBGenerationResult& OutResult,
		FString& OutError)
	{
		FABTSM73DAG5BV2GenerationResult Silhouette;
		FABTSM73BeamAGenerationResult BeamA;
		if (!GenerateUpstream(Settings, Silhouette, BeamA, OutError))
		{
			return false;
		}
		FABTSM73BeamBGenerator Generator;
		return Generator.Generate(Settings, Silhouette, BeamA,
			OutResult, OutError);
	}

	FABTSM73BeamBPreviewSettings SettingsForSeed(const int32 Seed)
	{
		FABTSM73BeamBPreviewSettings Settings;
		Settings.BeamA.Silhouette.BuildingSeed = Seed;
		Settings.BeamA.Silhouette.Archetype =
			EABTSM73DAG5BV2Archetype::BridgedArcology;
		Settings.BeamA.Silhouette.MinGrammarDepth = 2;
		Settings.BeamA.Silhouette.MaxGrammarDepth = 4;
		Settings.BeamA.TargetBaySpanCM = 480.0f;
		Settings.GrammarDepth = 2;
		return Settings;
	}

	FBox MemberBounds(
		const FABTSM73BeamAMember& Member,
		const FABTSM73BeamAGenerationResult& Result,
		const double CrossSection)
	{
		if (!Result.Joints.IsValidIndex(Member.JointA)
			|| !Result.Joints.IsValidIndex(Member.JointB))
		{
			return FBox(EForceInit::ForceInit);
		}
		const FVector Center =
			(Result.Joints[Member.JointA].LocalPosition
				+ Result.Joints[Member.JointB].LocalPosition) * 0.5;
		FVector Extent(CrossSection * 0.5);
		const int32 AxisIndex = static_cast<int32>(Member.Axis);
		if (AxisIndex >= 0 && AxisIndex <= 2)
		{
			Extent[AxisIndex] = Member.LengthCM * 0.5;
		}
		return FBox(Center - Extent, Center + Extent);
	}

	FString SemanticModulePath(const FString& Path)
	{
		TArray<FString> Parts;
		Path.ParseIntoArray(Parts, TEXT("/"), true);
		return Parts.Num() >= 2
			? Parts[0] + TEXT("/") + Parts[1]
			: Path;
	}

	bool HasPositiveVolumePenetration(
		const FABTSM73BeamAGenerationResult& Result,
		const FABTSM73BeamBPreviewSettings& Settings)
	{
		TArray<FBox> Bounds;
		for (const FABTSM73BeamAMember& Member : Result.Members)
		{
			Bounds.Add(MemberBounds(Member, Result,
				Settings.BeamA.BlockCrossSectionCM));
		}
		const double Tolerance = Settings.BeamA.JointMergeToleranceCM;
		for (int32 A = 0; A < Bounds.Num(); ++A)
		{
			for (int32 B = A + 1; B < Bounds.Num(); ++B)
			{
				if (FMath::Min(Bounds[A].Max.X, Bounds[B].Max.X)
						- FMath::Max(Bounds[A].Min.X, Bounds[B].Min.X) > Tolerance
					&& FMath::Min(Bounds[A].Max.Y, Bounds[B].Max.Y)
						- FMath::Max(Bounds[A].Min.Y, Bounds[B].Min.Y) > Tolerance
					&& FMath::Min(Bounds[A].Max.Z, Bounds[B].Max.Z)
						- FMath::Max(Bounds[A].Min.Z, Bounds[B].Min.Z) > Tolerance)
				{
					return true;
				}
			}
		}
		return false;
	}

	bool EveryMemberReachesGround(
		const FABTSM73BeamAGenerationResult& Result,
		const FABTSM73BeamBPreviewSettings& Settings)
	{
		TArray<bool> Reachable;
		Reachable.Init(false, Result.Members.Num());
		TArray<int32> Queue;
		for (const FABTSM73BeamAMember& Member : Result.Members)
		{
			if (MemberBounds(Member, Result,
				Settings.BeamA.BlockCrossSectionCM).Min.Z
				<= Settings.BeamA.JointMergeToleranceCM)
			{
				Reachable[Member.MemberId] = true;
				Queue.Add(Member.MemberId);
			}
		}
		for (int32 QueueIndex = 0; QueueIndex < Queue.Num(); ++QueueIndex)
		{
			const int32 LowerId = Queue[QueueIndex];
			for (const FABTSM73BeamABearingContact& Contact :
				Result.BearingContacts)
			{
				if (Contact.LowerMemberId == LowerId
					&& Reachable.IsValidIndex(Contact.UpperMemberId)
					&& !Reachable[Contact.UpperMemberId])
				{
					Reachable[Contact.UpperMemberId] = true;
					Queue.Add(Contact.UpperMemberId);
				}
			}
		}
		return !Reachable.Contains(false);
	}

	const FABTSM73DAG5BV2Volume* FindVolumeForBay(
		const FABTSM73DAG5BV2GenerationResult& Silhouette,
		const FABTSM73BeamABay& Bay)
	{
		return Silhouette.Volumes.FindByPredicate(
			[&Bay](const FABTSM73DAG5BV2Volume& Volume)
			{
				return Volume.VolumeId == Bay.SourceVolumeId;
			});
	}

	FBox IndependentRoofEnvelope(
		const FBox& Bounds,
		const EABTSM73DAG5BV2Primitive Primitive,
		const double Alpha,
		const double CrossSection)
	{
		FBox Envelope = Bounds;
		const FVector Center = Bounds.GetCenter();
		const FVector Size = Bounds.GetSize();
		const double MinimumHalfSpan = CrossSection * 0.55;
		if (Primitive == EABTSM73DAG5BV2Primitive::Pyramid
			|| Primitive == EABTSM73DAG5BV2Primitive::TriangularPrismX)
		{
			const double HalfSpan = FMath::Max(MinimumHalfSpan,
				Size.X * 0.5 * (1.0 - FMath::Clamp(Alpha, 0.0, 1.0)));
			Envelope.Min.X = Center.X - HalfSpan;
			Envelope.Max.X = Center.X + HalfSpan;
		}
		if (Primitive == EABTSM73DAG5BV2Primitive::Pyramid
			|| Primitive == EABTSM73DAG5BV2Primitive::TriangularPrismY)
		{
			const double HalfSpan = FMath::Max(MinimumHalfSpan,
				Size.Y * 0.5 * (1.0 - FMath::Clamp(Alpha, 0.0, 1.0)));
			Envelope.Min.Y = Center.Y - HalfSpan;
			Envelope.Max.Y = Center.Y + HalfSpan;
		}
		return Envelope;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamBDeterminismTest,
	"ABTS.M73DAG.BeamB.Determinism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamBDeterminismTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamBTests;
	const FABTSM73BeamBPreviewSettings Settings = SettingsForSeed(735201);
	FABTSM73BeamBGenerationResult A;
	FABTSM73BeamBGenerationResult B;
	FString Error;
	TestTrue(TEXT("First generation succeeds"), Generate(Settings, A, Error));
	TestTrue(TEXT("Second generation succeeds"), Generate(Settings, B, Error));
	TestEqual(TEXT("Motif hash is deterministic"),
		A.Summary.MotifWFCHash, B.Summary.MotifWFCHash);
	TestEqual(TEXT("Grammar hash is deterministic"),
		A.Summary.GraphGrammarHash, B.Summary.GraphGrammarHash);
	TestEqual(TEXT("Result hash is deterministic"),
		A.Summary.ResultHash, B.Summary.ResultHash);
	TestEqual(TEXT("Placement count is deterministic"),
		A.Placements.Num(), B.Placements.Num());
	TestEqual(TEXT("Member count is deterministic"),
		A.PlannedMembers.Num(), B.PlannedMembers.Num());
	TestEqual(TEXT("Closed member count is deterministic"),
		A.ClosedAssembly.Members.Num(), B.ClosedAssembly.Members.Num());
	TestEqual(TEXT("Closed bearing count is deterministic"),
		A.ClosedAssembly.BearingContacts.Num(),
		B.ClosedAssembly.BearingContacts.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamBGlobalAssemblyClosureTest,
	"ABTS.M73DAG.BeamB.GlobalAssemblyClosure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamBGlobalAssemblyClosureTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamBTests;
	for (int32 Value = static_cast<int32>(
		EABTSM73DAG5BV2Archetype::TerracedCitadel);
		Value <= static_cast<int32>(
			EABTSM73DAG5BV2Archetype::SpiredCampus); ++Value)
	{
		FABTSM73BeamBPreviewSettings Settings = SettingsForSeed(
			940000 + Value * 211);
		Settings.BeamA.Silhouette.Archetype =
			static_cast<EABTSM73DAG5BV2Archetype>(Value);
		FABTSM73BeamBGenerationResult Result;
		FString Error;
		const bool bGenerated = Generate(Settings, Result, Error);
		TestTrue(FString::Printf(TEXT("Archetype %d closes: %s"),
			Value, *Error), bGenerated);
		if (!bGenerated)
		{
			continue;
		}
		TestEqual(TEXT("Summary reports no penetration"),
			Result.Summary.RemainingPenetrationCount, 0);
		TestEqual(TEXT("Summary reports no unsupported member"),
			Result.Summary.UnsupportedMemberCount, 0);
		TestFalse(TEXT("Independent AABB audit finds no penetration"),
			HasPositiveVolumePenetration(Result.ClosedAssembly, Settings));
		TestTrue(TEXT("Independent bearing audit reaches every member"),
			EveryMemberReachesGround(Result.ClosedAssembly, Settings));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamBNoDiagonalTest,
	"ABTS.M73DAG.BeamB.NoDiagonalMembers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamBNoDiagonalTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamBTests;
	FABTSM73BeamBPreviewSettings Settings = SettingsForSeed(735201);
	Settings.bAllowBracedBay = true;
	Settings.bAllowCantilever = true;
	FABTSM73BeamBGenerationResult Result;
	FString Error;
	TestTrue(TEXT("Generation succeeds even with legacy flag enabled"),
		Generate(Settings, Result, Error));
	TestEqual(TEXT("Summary has no diagonal members"),
		Result.Summary.DiagonalMemberCount, 0);
	for (const FABTSM73BeamBPlacement& Placement : Result.Placements)
	{
		TestNotEqual(TEXT("Braced motif is outside active WFC domain"),
			Placement.Motif, EABTSM73BeamBMotif::BracedBay);
		TestNotEqual(TEXT("Cantilever motif is outside active WFC domain"),
			Placement.Motif, EABTSM73BeamBMotif::CantileverBay);
	}
	for (const FABTSM73BeamBPlannedMember& Member : Result.PlannedMembers)
	{
		TestNotEqual(TEXT("Plan contains no diagonal"), Member.Axis,
			EABTSM73BeamAFrameAxis::Diagonal);
	}
	for (const FABTSM73BeamAMember& Member : Result.ClosedAssembly.Members)
	{
		TestNotEqual(TEXT("Closed assembly contains no diagonal"), Member.Axis,
			EABTSM73BeamAFrameAxis::Diagonal);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamBMotifCoverageTest,
	"ABTS.M73DAG.BeamB.MotifCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamBMotifCoverageTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamBTests;
	TSet<EABTSM73BeamBMotif> Seen;
	bool bSawBridgeVolume = false;
	bool bBridgeForced = true;
	const EABTSM73DAG5BV2Archetype Archetypes[] = {
		EABTSM73DAG5BV2Archetype::TerracedCitadel,
		EABTSM73DAG5BV2Archetype::TwinTowerComplex,
		EABTSM73DAG5BV2Archetype::BridgedArcology,
		EABTSM73DAG5BV2Archetype::SpiredCampus};
	for (const EABTSM73DAG5BV2Archetype Archetype : Archetypes)
	{
			const int32 Value = static_cast<int32>(Archetype);
			FABTSM73BeamBPreviewSettings Settings = SettingsForSeed(
				940000 + Value * 211);
			Settings.BeamA.Silhouette.Archetype = Archetype;
			FABTSM73DAG5BV2GenerationResult Silhouette;
			FABTSM73BeamAGenerationResult BeamA;
			FABTSM73BeamBGenerationResult Result;
			FString Error;
			if (!GenerateUpstream(Settings, Silhouette, BeamA, Error))
			{
				AddError(FString::Printf(TEXT("Upstream failed: %s"), *Error));
				return false;
			}
			FABTSM73BeamBGenerator Generator;
			if (!Generator.Generate(Settings, Silhouette, BeamA, Result, Error))
			{
				AddError(FString::Printf(TEXT("Beam-B failed: %s"), *Error));
				return false;
			}
			for (const FABTSM73BeamBPlacement& Placement : Result.Placements)
			{
				Seen.Add(Placement.Motif);
				const FABTSM73BeamABay& Bay = BeamA.Bays[Placement.BayId];
				const FABTSM73DAG5BV2Volume* Volume =
					Silhouette.Volumes.FindByPredicate(
						[&](const FABTSM73DAG5BV2Volume& Candidate)
						{
							return Candidate.VolumeId == Bay.SourceVolumeId;
						});
				if (Volume != nullptr
					&& Volume->Role
						== EABTSM73DAG5BV2VolumeRole::SupportedSpan)
				{
					bSawBridgeVolume = true;
					bBridgeForced &= Placement.Motif
						== EABTSM73BeamBMotif::BridgeBay;
				}
			}
	}
	TestTrue(TEXT("Seed matrix covers at least six motif families"),
		Seen.Num() >= 6);
	TestTrue(TEXT("Matrix contains a supported span volume"),
		bSawBridgeVolume);
	TestTrue(TEXT("Supported span volumes force BridgeBay"), bBridgeForced);
	TestFalse(TEXT("CantileverBay is excluded from the active domain"),
		Seen.Contains(EABTSM73BeamBMotif::CantileverBay));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamBSupportedSpanVoidTest,
	"ABTS.M73DAG.BeamB.SupportedSpanVoid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamBSupportedSpanVoidTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamBTests;
	FABTSM73BeamBPreviewSettings Settings = SettingsForSeed(940422);
	Settings.BeamA.Silhouette.Archetype =
		EABTSM73DAG5BV2Archetype::BridgedArcology;
	Settings.bAllowCantilever = true;
	FABTSM73BeamBGenerationResult Result;
	FString Error;
	if (!Generate(Settings, Result, Error))
	{
		AddError(FString::Printf(TEXT("Generation failed: %s"), *Error));
		return false;
	}
	TestTrue(TEXT("Supported span reserves an undercroft"),
		!Result.ClosedAssembly.ReservedSupportVoids.IsEmpty());
	for (const FABTSM73BeamBPlacement& Placement : Result.Placements)
	{
		TestNotEqual(TEXT("Legacy cantilever is never selected"),
			Placement.Motif, EABTSM73BeamBMotif::CantileverBay);
	}

	const double Tolerance = Settings.BeamA.JointMergeToleranceCM;
	for (const FABTSM73BeamAMember& Member : Result.ClosedAssembly.Members)
	{
		if (Member.Axis != EABTSM73BeamAFrameAxis::Z)
		{
			continue;
		}
		const FBox Bounds = MemberBounds(Member, Result.ClosedAssembly,
			Settings.BeamA.BlockCrossSectionCM);
		const FVector Station = Bounds.GetCenter();
		for (const FABTSM73BeamASupportVoid& SupportVoid :
			Result.ClosedAssembly.ReservedSupportVoids)
		{
			const FBox& Void = SupportVoid.Bounds;
			const bool bOverlapsHeight = Bounds.Max.Z > Void.Min.Z + Tolerance
				&& Bounds.Min.Z < Void.Max.Z - Tolerance;
			const bool bInsideFootprint = Station.X > Void.Min.X + Tolerance
				&& Station.X < Void.Max.X - Tolerance
				&& Station.Y > Void.Min.Y + Tolerance
				&& Station.Y < Void.Max.Y - Tolerance;
			if (bOverlapsHeight && bInsideFootprint)
			{
				AddError(FString::Printf(
					TEXT("Z member %d at %s enters void %s..%s axis %d"),
					Member.MemberId,
					*Station.ToString(),
					*Void.Min.ToCompactString(),
					*Void.Max.ToCompactString(),
					SupportVoid.SpanAxisIndex));
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamBBridgeEndpointBearingTest,
	"ABTS.M73DAG.BeamB.BridgeEndpointBearing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamBBridgeEndpointBearingTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamBTests;
	FABTSM73BeamBPreviewSettings Settings = SettingsForSeed(940422);
	Settings.BeamA.Silhouette.Archetype =
		EABTSM73DAG5BV2Archetype::BridgedArcology;
	FABTSM73DAG5BV2GenerationResult Silhouette;
	FABTSM73BeamAGenerationResult BeamA;
	FABTSM73BeamBGenerationResult Result;
	FString Error;
	if (!GenerateUpstream(Settings, Silhouette, BeamA, Error))
	{
		AddError(FString::Printf(TEXT("Upstream failed: %s"), *Error));
		return false;
	}
	FABTSM73BeamBGenerator Generator;
	if (!Generator.Generate(Settings, Silhouette, BeamA, Result, Error))
	{
		AddError(FString::Printf(TEXT("Beam-B failed: %s"), *Error));
		return false;
	}

	int32 SupportedSpanCount = 0;
	for (const FABTSM73DAG5BV2Volume& Volume : Silhouette.Volumes)
	{
		SupportedSpanCount += Volume.Role
			== EABTSM73DAG5BV2VolumeRole::SupportedSpan ? 1 : 0;
	}
	TestTrue(TEXT("Fixture contains a supported span"), SupportedSpanCount > 0);
	TestEqual(TEXT("Every supported span has two endpoint ledgers"),
		Result.BridgeEndpoints.Num(), SupportedSpanCount * 2);
	int32 ExpectedBridgeSeatCount = 0;
	for (const FABTSM73BeamAMember& Member : Result.ClosedAssembly.Members)
	{
		ExpectedBridgeSeatCount += Member.Role
			== EABTSM73BeamAMemberRole::BridgeSeat ? 1 : 0;
	}
	TestEqual(TEXT("Summary reports every closed bridge seat and corbel"),
		Result.Summary.BridgeSeatMemberCount, ExpectedBridgeSeatCount);
	TestEqual(TEXT("Every endpoint has a closed bearing"),
		Result.Summary.BridgeEndpointBearingCount,
		Result.BridgeEndpoints.Num());
	int32 ExpectedRailEndpointCount = 0;
	for (const FABTSM73BeamBBridgeEndpoint& Endpoint : Result.BridgeEndpoints)
	{
		TestTrue(TEXT("Every bridge endpoint declares its load rails"),
			Endpoint.RailStationsCM.Num() >= 2);
		ExpectedRailEndpointCount += Endpoint.RailStationsCM.Num();
	}
	TestEqual(TEXT("Every declared bridge rail endpoint bears on its seat"),
		Result.Summary.BridgeRailEndpointBearingCount,
		ExpectedRailEndpointCount);
	TestEqual(TEXT("No bridge rail endpoint bearing is missing"),
		Result.Summary.BridgeRailEndpointBearingViolationCount, 0);
	TestEqual(TEXT("No endpoint bearing contract is missing"),
		Result.Summary.BridgeEndpointBearingViolationCount, 0);
	TestEqual(TEXT("Bridge Assembly receives no ground rescue post"),
		Result.Summary.BridgeGroundRescuePostCount, 0);

	TMap<int32, TArray<int32>> AssembliesBySourceVolume;
	TMap<int32, TArray<int32>> OwnersByMember;
	for (const FABTSM73BeamAAssembly& Assembly :
		Result.ClosedAssembly.Assemblies)
	{
		if (Result.ClosedAssembly.Bays.IsValidIndex(Assembly.BayId))
		{
			AssembliesBySourceVolume.FindOrAdd(
				Result.ClosedAssembly.Bays[Assembly.BayId].SourceVolumeId)
				.AddUnique(Assembly.AssemblyId);
		}
		for (const int32 MemberId : Assembly.MemberIds)
		{
			OwnersByMember.FindOrAdd(MemberId).AddUnique(Assembly.AssemblyId);
		}
	}
	const double EndpointTolerance =
		Settings.BeamA.JointMergeToleranceCM;
	for (const FABTSM73BeamBBridgeEndpoint& Endpoint :
		Result.BridgeEndpoints)
	{
		TestNotEqual(TEXT("Endpoint support differs from span"),
			Endpoint.SupportVolumeId, Endpoint.SpanVolumeId);
		const FABTSM73DAG5BV2Volume* DeclaredSupport =
			Silhouette.Volumes.FindByPredicate(
				[&Endpoint](const FABTSM73DAG5BV2Volume& Volume)
				{
					return Volume.VolumeId
						== Endpoint.DeclaredSupportVolumeId;
				});
		const FABTSM73DAG5BV2Volume* SeatSupport =
			Silhouette.Volumes.FindByPredicate(
				[&Endpoint](const FABTSM73DAG5BV2Volume& Volume)
				{
					return Volume.VolumeId == Endpoint.SupportVolumeId;
				});
		TestTrue(TEXT("Declared endpoint support remains valid"),
			DeclaredSupport != nullptr);
		TestTrue(TEXT("Seat support Volume remains valid"),
			SeatSupport != nullptr);
		if (DeclaredSupport != nullptr && SeatSupport != nullptr)
		{
			TestEqual(TEXT("Bridge seat stays in the declared semantic module"),
				SemanticModulePath(SeatSupport->DerivationPath),
				SemanticModulePath(DeclaredSupport->DerivationPath));
		}
		TestTrue(TEXT("Seat member identity remains valid"),
			Result.PlannedMembers.IsValidIndex(Endpoint.SeatPlannedMemberId));
		if (Result.PlannedMembers.IsValidIndex(Endpoint.SeatPlannedMemberId))
		{
			TestEqual(TEXT("Endpoint plan uses BridgeSeat role"),
				Result.PlannedMembers[Endpoint.SeatPlannedMemberId].Role,
				EABTSM73BeamAMemberRole::BridgeSeat);
			TestEqual(TEXT("BridgeSeat is owned by the selected support Bay"),
				Result.PlannedMembers[Endpoint.SeatPlannedMemberId].BayId,
				Endpoint.SupportBayId);
		}
		const TArray<int32>* SpanAssemblies =
			AssembliesBySourceVolume.Find(Endpoint.SpanVolumeId);
		const TArray<int32>* SupportAssemblies =
			AssembliesBySourceVolume.Find(Endpoint.SupportVolumeId);
		const FABTSM73DAG5BV2Volume* Span =
			Silhouette.Volumes.FindByPredicate(
				[&Endpoint](const FABTSM73DAG5BV2Volume& Volume)
				{
					return Volume.VolumeId == Endpoint.SpanVolumeId;
				});
		bool bFoundPhysicalBearing = false;
		if (SpanAssemblies != nullptr && SupportAssemblies != nullptr
			&& Span != nullptr)
		{
			const FABTSM73BeamABay* BridgeBay =
				Result.ClosedAssembly.Bays.IsValidIndex(Endpoint.BridgeBayId)
					? &Result.ClosedAssembly.Bays[Endpoint.BridgeBayId]
					: nullptr;
			const double EndpointMinAxis = BridgeBay != nullptr
				? FMath::Min(Endpoint.BearingPlaneCM,
					BridgeBay->LocalBounds.Min[Span->SpanAxisIndex])
					- Settings.BeamA.BlockCrossSectionCM - EndpointTolerance
				: 0.0;
			const double EndpointMaxAxis = BridgeBay != nullptr
				? FMath::Max(Endpoint.BearingPlaneCM,
					BridgeBay->LocalBounds.Max[Span->SpanAxisIndex])
					+ Settings.BeamA.BlockCrossSectionCM + EndpointTolerance
				: -1.0;
			for (const FABTSM73BeamABearingContact& Contact :
				Result.ClosedAssembly.BearingContacts)
			{
				if (Contact.ContactAreaCM2 <= 0.0f
					|| BridgeBay == nullptr
					|| Contact.LocalPosition[Span->SpanAxisIndex]
						< EndpointMinAxis
					|| Contact.LocalPosition[Span->SpanAxisIndex]
						> EndpointMaxAxis)
				{
					continue;
				}
				const TArray<int32>* LowerOwners =
					OwnersByMember.Find(Contact.LowerMemberId);
				const TArray<int32>* UpperOwners =
					OwnersByMember.Find(Contact.UpperMemberId);
				const bool bSupportOwner = LowerOwners != nullptr
					&& LowerOwners->ContainsByPredicate(
						[SupportAssemblies](const int32 AssemblyId)
						{
							return SupportAssemblies->Contains(AssemblyId);
						});
				const bool bSpanOwner = UpperOwners != nullptr
					&& UpperOwners->ContainsByPredicate(
						[SpanAssemblies](const int32 AssemblyId)
						{
							return SpanAssemblies->Contains(AssemblyId);
						});
				if (bSupportOwner && bSpanOwner)
				{
					bFoundPhysicalBearing = true;
					break;
				}
			}
			TArray<bool> EndpointReachable;
			EndpointReachable.Init(false,
				Result.ClosedAssembly.Members.Num());
			TArray<int32> Queue;
			for (const FABTSM73BeamAMember& Member :
				Result.ClosedAssembly.Members)
			{
				const TArray<int32>* Owners =
					OwnersByMember.Find(Member.MemberId);
				const bool bSupportOwner = Owners != nullptr
					&& Owners->ContainsByPredicate(
						[SupportAssemblies](const int32 AssemblyId)
						{
							return SupportAssemblies->Contains(AssemblyId);
						});
				const FBox Bounds = MemberBounds(
					Member, Result.ClosedAssembly,
					Settings.BeamA.BlockCrossSectionCM);
				if (Member.Role != EABTSM73BeamAMemberRole::BridgeSeat
					|| !bSupportOwner
					|| Endpoint.BearingPlaneCM
						< Bounds.Min[Span->SpanAxisIndex]
							- Settings.BeamA.BlockCrossSectionCM - EndpointTolerance
					|| Endpoint.BearingPlaneCM
						> Bounds.Max[Span->SpanAxisIndex]
							+ Settings.BeamA.BlockCrossSectionCM + EndpointTolerance)
				{
					continue;
				}
				EndpointReachable[Member.MemberId] = true;
				Queue.Add(Member.MemberId);
			}
			for (int32 QueueIndex = 0;
				QueueIndex < Queue.Num() && !bFoundPhysicalBearing;
				++QueueIndex)
			{
				for (const FABTSM73BeamABearingContact& Contact :
					Result.ClosedAssembly.BearingContacts)
				{
					if (Contact.LowerMemberId != Queue[QueueIndex]
						|| Contact.ContactAreaCM2 <= 0.0f
						|| BridgeBay == nullptr
						|| Contact.LocalPosition[Span->SpanAxisIndex]
							< EndpointMinAxis
						|| Contact.LocalPosition[Span->SpanAxisIndex]
							> EndpointMaxAxis)
					{
						continue;
					}
					const TArray<int32>* UpperOwners =
						OwnersByMember.Find(Contact.UpperMemberId);
					const bool bSpanOwner = UpperOwners != nullptr
						&& UpperOwners->ContainsByPredicate(
							[SpanAssemblies](const int32 AssemblyId)
							{
								return SpanAssemblies->Contains(AssemblyId);
							});
					if (bSpanOwner)
					{
						bFoundPhysicalBearing = true;
						break;
					}
					if (EndpointReachable.IsValidIndex(Contact.UpperMemberId)
						&& !EndpointReachable[Contact.UpperMemberId])
					{
						EndpointReachable[Contact.UpperMemberId] = true;
						Queue.Add(Contact.UpperMemberId);
					}
				}
			}
		}
		TestTrue(TEXT("Bridge rail bears on its designated support ledger"),
			bFoundPhysicalBearing);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamBDefaultBridgedArcologyRailBearingTest,
	"ABTS.M73DAG.BeamB.DefaultBridgedArcologyRailBearing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamBDefaultBridgedArcologyRailBearingTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamBTests;
	FABTSM73BeamBPreviewSettings Settings = SettingsForSeed(735201);
	FABTSM73BeamBGenerationResult Result;
	FString Error;
	if (!Generate(Settings, Result, Error))
	{
		AddError(FString::Printf(
			TEXT("Default Bridged Arcology failed: %s"), *Error));
		return false;
	}

	int32 ExpectedRailEndpointCount = 0;
	for (const FABTSM73BeamBBridgeEndpoint& Endpoint : Result.BridgeEndpoints)
	{
		TestTrue(TEXT("Default endpoint declares paired load rails"),
			Endpoint.RailStationsCM.Num() >= 2);
		ExpectedRailEndpointCount += Endpoint.RailStationsCM.Num();
	}
	TestTrue(TEXT("Default fixture contains bridge endpoints"),
		!Result.BridgeEndpoints.IsEmpty());
	TestEqual(TEXT("Default fixture closes every bridge endpoint"),
		Result.Summary.BridgeEndpointBearingCount,
		Result.BridgeEndpoints.Num());
	TestEqual(TEXT("Default fixture carries every load-rail endpoint"),
		Result.Summary.BridgeRailEndpointBearingCount,
		ExpectedRailEndpointCount);
	TestEqual(TEXT("Default fixture has no rail-bearing violation"),
		Result.Summary.BridgeRailEndpointBearingViolationCount, 0);
	TestEqual(TEXT("Default fixture has no endpoint-bearing violation"),
		Result.Summary.BridgeEndpointBearingViolationCount, 0);
	TestTrue(TEXT("Default fixture audits every separated suspended beam"),
		Result.Summary.BridgeSuspendedBeamTargetCount >= 4);
	TestEqual(TEXT("Default fixture supports every audited suspended beam"),
		Result.Summary.BridgeSuspendedBeamSupportedCount,
		Result.Summary.BridgeSuspendedBeamTargetCount);
	TestEqual(TEXT("Default fixture has no suspended-beam support violation"),
		Result.Summary.BridgeSuspendedBeamSupportViolationCount, 0);
	TestEqual(TEXT("Default fixture has no bridge ground rescue post"),
		Result.Summary.BridgeGroundRescuePostCount, 0);
	int32 ShortCorbelCount = 0;
	int32 UpperPostCount = 0;
	for (const FABTSM73BeamAMember& Member : Result.ClosedAssembly.Members)
	{
		if (Member.Role == EABTSM73BeamAMemberRole::BridgeSeat
			&& Member.Axis == EABTSM73BeamAFrameAxis::Z)
		{
			++ShortCorbelCount;
		}
		if (Member.Role != EABTSM73BeamAMemberRole::BridgePost)
		{
			continue;
		}
		++UpperPostCount;
		TestEqual(TEXT("Bridge upper support is a Z post"),
			Member.Axis, EABTSM73BeamAFrameAxis::Z);
		bool bRailBelow = false;
		bool bModuleAbove = false;
		for (const FABTSM73BeamABearingContact& Contact :
			Result.ClosedAssembly.BearingContacts)
		{
			if (Contact.UpperMemberId == Member.MemberId
				&& Result.ClosedAssembly.Members.IsValidIndex(
					Contact.LowerMemberId)
				&& (Result.ClosedAssembly.Members[Contact.LowerMemberId].Role
						== EABTSM73BeamAMemberRole::BridgeRail
					|| Result.ClosedAssembly.Members[Contact.LowerMemberId].Role
						== EABTSM73BeamAMemberRole::BridgeSeat))
			{
				bRailBelow = true;
			}
			if (Contact.LowerMemberId == Member.MemberId
				&& Result.ClosedAssembly.Members.IsValidIndex(
					Contact.UpperMemberId))
			{
				const FABTSM73BeamAMember& Upper =
					Result.ClosedAssembly.Members[Contact.UpperMemberId];
				bModuleAbove = Upper.Axis != EABTSM73BeamAFrameAxis::Z
					&& Upper.Role != EABTSM73BeamAMemberRole::BridgePost;
			}
		}
		TestTrue(TEXT("Bridge upper post bears on a bridge rail or bearer"),
			bRailBelow);
		TestTrue(TEXT("Bridge upper post carries an endpoint module"),
			bModuleAbove);
	}
	TestTrue(TEXT("Default fixture materializes local short bridge corbels"),
		ShortCorbelCount > 0);
	TestTrue(TEXT("Default fixture closes every separated elevated beam with Z posts"),
		UpperPostCount >= 4);
	TestEqual(TEXT("Bridge upper-post summary matches materialized posts"),
		Result.Summary.BridgeUpperPostMemberCount, UpperPostCount);
	TestTrue(TEXT("Added suspended-beam posts preserve ground reachability"),
		EveryMemberReachesGround(Result.ClosedAssembly, Settings));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamBPortCompatibilityTest,
	"ABTS.M73DAG.BeamB.PortCompatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamBPortCompatibilityTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamBTests;
	for (int32 Value = static_cast<int32>(
		EABTSM73DAG5BV2Archetype::TerracedCitadel);
		Value <= static_cast<int32>(
			EABTSM73DAG5BV2Archetype::SpiredCampus); ++Value)
	{
		FABTSM73BeamBPreviewSettings Settings = SettingsForSeed(
			940000 + Value * 211);
		Settings.BeamA.Silhouette.Archetype =
			static_cast<EABTSM73DAG5BV2Archetype>(Value);
		FABTSM73BeamBGenerationResult Result;
		FString Error;
		TestTrue(FString::Printf(TEXT("Archetype %d accepts: %s"),
			Value, *Error), Generate(Settings, Result, Error));
		TestEqual(TEXT("No port violations"),
			Result.Summary.PortViolationCount, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamBMotifTerminalCoverageTest,
	"ABTS.M73DAG.BeamB.MotifTerminalCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamBMotifTerminalCoverageTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamBTests;
	const FABTSM73BeamBPreviewSettings Settings = SettingsForSeed(735201);
	FABTSM73DAG5BV2GenerationResult Silhouette;
	FABTSM73BeamAGenerationResult BeamA;
	FABTSM73BeamBGenerationResult Result;
	FString Error;
	if (!GenerateUpstream(Settings, Silhouette, BeamA, Error))
	{
		AddError(FString::Printf(TEXT("Upstream failed: %s"), *Error));
		return false;
	}
	FABTSM73BeamBGenerator Generator;
	if (!Generator.Generate(Settings, Silhouette, BeamA, Result, Error))
	{
		AddError(FString::Printf(TEXT("Beam-B failed: %s"), *Error));
		return false;
	}

	const double Tolerance = Settings.BeamA.JointMergeToleranceCM;
	int32 AuditedMemberCount = 0;
	for (const FABTSM73BeamBPlannedMember& Member : Result.PlannedMembers)
	{
		const bool bAuditedMotif =
			Member.Motif == EABTSM73BeamBMotif::PostAndLintel
			|| Member.Motif == EABTSM73BeamBMotif::PortalFrame
			|| Member.Motif == EABTSM73BeamBMotif::CrossBeam
			|| Member.Motif == EABTSM73BeamBMotif::TwoLayerCrib
			|| Member.Motif == EABTSM73BeamBMotif::TransferFrame
			|| Member.Motif == EABTSM73BeamBMotif::BracedBay;
		const bool bAuditedRole =
			Member.Role == EABTSM73BeamAMemberRole::PrimaryBeam
			|| Member.Role == EABTSM73BeamAMemberRole::SecondaryBeam;
		if (!bAuditedMotif || !bAuditedRole
			|| Member.Axis == EABTSM73BeamAFrameAxis::Z
			|| !BeamA.Bays.IsValidIndex(Member.BayId))
		{
			continue;
		}

		++AuditedMemberCount;
		const FABTSM73BeamABay& Bay = BeamA.Bays[Member.BayId];
		const int32 AxisIndex = static_cast<int32>(Member.Axis);
		const double MemberMin = FMath::Min(
			Member.LocalStart[AxisIndex], Member.LocalEnd[AxisIndex]);
		const double MemberMax = FMath::Max(
			Member.LocalStart[AxisIndex], Member.LocalEnd[AxisIndex]);
		TestTrue(FString::Printf(
			TEXT("Bay %d member %d reaches its negative terminal"),
			Member.BayId, Member.PlannedMemberId),
			FMath::IsNearlyEqual(MemberMin,
				Bay.LocalBounds.Min[AxisIndex], Tolerance));
		TestTrue(FString::Printf(
			TEXT("Bay %d member %d reaches its positive terminal"),
			Member.BayId, Member.PlannedMemberId),
			FMath::IsNearlyEqual(MemberMax,
				Bay.LocalBounds.Max[AxisIndex], Tolerance));
	}
	TestTrue(TEXT("Default fixture audits full-span motif beams"),
		AuditedMemberCount > 0);
	TestEqual(TEXT("Full terminal coverage preserves bounds acceptance"),
		Result.Summary.OutOfBoundsMemberCount, 0);
	TestEqual(TEXT("Full terminal coverage closes without penetration"),
		Result.Summary.RemainingPenetrationCount, 0);
	TestEqual(TEXT("Full terminal coverage closes without unsupported members"),
		Result.Summary.UnsupportedMemberCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamBGrammarDepthTest,
	"ABTS.M73DAG.BeamB.GrammarDepthAddsTopology",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamBGrammarDepthTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamBTests;
	FABTSM73BeamBPreviewSettings ShallowSettings = SettingsForSeed(735201);
	ShallowSettings.GrammarDepth = 1;
	FABTSM73BeamBPreviewSettings DeepSettings = ShallowSettings;
	DeepSettings.GrammarDepth = 4;
	FABTSM73DAG5BV2GenerationResult Silhouette;
	FABTSM73BeamAGenerationResult BeamA;
	FString Error;
	TestTrue(TEXT("Upstream accepts"), GenerateUpstream(
		ShallowSettings, Silhouette, BeamA, Error));
	FABTSM73BeamBGenerator Generator;
	FABTSM73BeamBGenerationResult Shallow;
	FABTSM73BeamBGenerationResult Deep;
	TestTrue(TEXT("Shallow accepts"), Generator.Generate(
		ShallowSettings, Silhouette, BeamA, Shallow, Error));
	TestTrue(TEXT("Deep accepts"), Generator.Generate(
		DeepSettings, Silhouette, BeamA, Deep, Error));
	TestEqual(TEXT("Motif collapse remains stable across grammar depth"),
		Shallow.Summary.MotifWFCHash, Deep.Summary.MotifWFCHash);
	TestTrue(TEXT("Deep grammar adds rule steps"),
		Deep.GrammarSteps.Num() > Shallow.GrammarSteps.Num());
	TestTrue(TEXT("Deep grammar adds planned topology"),
		Deep.PlannedMembers.Num() > Shallow.PlannedMembers.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamBBoundsBudgetTest,
	"ABTS.M73DAG.BeamB.BoundsAndBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamBBoundsBudgetTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamBTests;
	FABTSM73BeamBPreviewSettings Settings = SettingsForSeed(735201);
	FABTSM73BeamBGenerationResult Accepted;
	FString Error;
	TestTrue(TEXT("Normal settings accept"), Generate(Settings, Accepted, Error));
	TestEqual(TEXT("No member leaves its Bay"),
		Accepted.Summary.OutOfBoundsMemberCount, 0);

	FABTSM73DAG5BV2GenerationResult Silhouette;
	FABTSM73BeamAGenerationResult BeamA;
	TestTrue(TEXT("Upstream accepts"), GenerateUpstream(
		Settings, Silhouette, BeamA, Error));
	Settings.MaxPlannedMemberCount = 4;
	FABTSM73BeamBGenerationResult Rejected;
	FABTSM73BeamBGenerator Generator;
	TestFalse(TEXT("Insufficient member budget rejects"), Generator.Generate(
		Settings, Silhouette, BeamA, Rejected, Error));
	TestEqual(TEXT("Budget failure is stable"), Error,
		FString(TEXT("BeamBPlannedMemberBudgetExceeded")));
	TestTrue(TEXT("Rejected result does not leak a partial graph"),
		Rejected.PlannedMembers.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamBInvalidSettingsTest,
	"ABTS.M73DAG.BeamB.InvalidSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamBInvalidSettingsTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamBTests;
	FABTSM73BeamBPreviewSettings Settings = SettingsForSeed(735201);
	FABTSM73DAG5BV2GenerationResult Silhouette;
	FABTSM73BeamAGenerationResult BeamA;
	FString Error;
	TestTrue(TEXT("Upstream accepts"), GenerateUpstream(
		Settings, Silhouette, BeamA, Error));
	Settings.GrammarDepth = 0;
	FABTSM73BeamBGenerationResult Result;
	FABTSM73BeamBGenerator Generator;
	TestFalse(TEXT("Invalid depth rejects"), Generator.Generate(
		Settings, Silhouette, BeamA, Result, Error));
	TestEqual(TEXT("Invalid reason is stable"), Error,
		FString(TEXT("BeamBInvalidSettings")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamBSemanticRoofFittingTest,
	"ABTS.M73DAG.BeamB.SemanticRoofFitting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamBSemanticRoofFittingTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamBTests;
	int32 NonBoxBayCount = 0;
	int32 RoofCourseCount = 0;
	int32 IndependentViolationCount = 0;
	int32 DirectBearingViolationCount = 0;
	int32 VisiblyTaperedBayCount = 0;
	for (int32 Value = static_cast<int32>(
		EABTSM73DAG5BV2Archetype::TerracedCitadel);
		Value <= static_cast<int32>(
			EABTSM73DAG5BV2Archetype::SpiredCampus); ++Value)
	{
		FABTSM73BeamBPreviewSettings Settings = SettingsForSeed(
			940000 + Value * 211);
		Settings.BeamA.Silhouette.Archetype =
			static_cast<EABTSM73DAG5BV2Archetype>(Value);
		FABTSM73DAG5BV2GenerationResult Silhouette;
		FABTSM73BeamAGenerationResult BeamA;
		FABTSM73BeamBGenerationResult Result;
		FString Error;
		if (!GenerateUpstream(Settings, Silhouette, BeamA, Error))
		{
			AddError(FString::Printf(TEXT("Upstream failed: %s"), *Error));
			return false;
		}
		FABTSM73BeamBGenerator Generator;
		if (!Generator.Generate(Settings, Silhouette, BeamA, Result, Error))
		{
			AddError(FString::Printf(TEXT("Beam-B failed: %s"), *Error));
			return false;
		}
		TestEqual(TEXT("Runtime semantic envelope audit accepts"),
			Result.Summary.SemanticEnvelopeViolationCount, 0);
		const double CrossSection = Settings.BeamA.BlockCrossSectionCM;
		const double Tolerance = Settings.BeamA.JointMergeToleranceCM;
		TestEqual(TEXT("Closed assembly has no unsupported members"),
			Result.ClosedAssembly.Summary.UnsupportedMemberCount, 0);
		TestEqual(TEXT("Closed assembly has no penetrations"),
			Result.ClosedAssembly.Summary.RemainingPenetrationCount, 0);
		for (const FABTSM73BeamABay& Bay : Result.ClosedAssembly.Bays)
		{
			const FABTSM73DAG5BV2Volume* Volume =
				FindVolumeForBay(Silhouette, Bay);
			if (Volume == nullptr
				|| Volume->Primitive == EABTSM73DAG5BV2Primitive::Box)
			{
				continue;
			}
			++NonBoxBayCount;
			const int32 CourseCount = FMath::Max(2, FMath::FloorToInt(
				Bay.LocalBounds.GetSize().Z / CrossSection));
			double LowestZ = TNumericLimits<double>::Max();
			double HighestZ = TNumericLimits<double>::Lowest();
			FBox LowestBounds(EForceInit::ForceInit);
			FBox HighestBounds(EForceInit::ForceInit);
			TArray<TPair<double, FBox>> CourseMembers;
			for (const FABTSM73BeamBPlannedMember& Member :
				Result.PlannedMembers)
			{
				if (Member.BayId != Bay.BayId
					|| Member.Role != EABTSM73BeamAMemberRole::RoofCourse)
				{
					continue;
				}
				++RoofCourseCount;
				const FVector Center =
					(Member.LocalStart + Member.LocalEnd) * 0.5;
				FVector Extent(CrossSection * 0.5);
				Extent[static_cast<int32>(Member.Axis)] =
					(Member.LocalEnd - Member.LocalStart).Size() * 0.5;
				const FBox Actual(Center - Extent, Center + Extent);
				const double CenterZ = Actual.GetCenter().Z;
				CourseMembers.Emplace(CenterZ, Actual);
				LowestZ = FMath::Min(LowestZ, CenterZ);
				HighestZ = FMath::Max(HighestZ, CenterZ);
				const int32 CourseIndex = FMath::Clamp(FMath::RoundToInt(
					(CenterZ - Bay.LocalBounds.Min.Z - CrossSection * 0.5)
					/ CrossSection), 0, CourseCount - 1);
				FBox Expected = IndependentRoofEnvelope(
					Bay.LocalBounds, Volume->Primitive,
					static_cast<double>(CourseIndex) / CourseCount,
					CrossSection).ExpandBy(Tolerance);
				if (CourseIndex > 0)
				{
					const FBox LowerEnvelope = IndependentRoofEnvelope(
						Bay.LocalBounds,
						Volume->Primitive,
						static_cast<double>(CourseIndex - 1) / CourseCount,
						CrossSection).ExpandBy(Tolerance);
					const int32 AxisIndex = static_cast<int32>(Member.Axis);
					Expected.Min[AxisIndex] = LowerEnvelope.Min[AxisIndex];
					Expected.Max[AxisIndex] = LowerEnvelope.Max[AxisIndex];
				}
				if (Actual.Min.X < Expected.Min.X
					|| Actual.Max.X > Expected.Max.X
					|| Actual.Min.Y < Expected.Min.Y
					|| Actual.Max.Y > Expected.Max.Y)
				{
					++IndependentViolationCount;
				}
			}
			for (const TPair<double, FBox>& CourseMember : CourseMembers)
			{
				if (FMath::IsNearlyEqual(CourseMember.Key, LowestZ, Tolerance))
				{
					LowestBounds += CourseMember.Value;
				}
				if (FMath::IsNearlyEqual(CourseMember.Key, HighestZ, Tolerance))
				{
					HighestBounds += CourseMember.Value;
				}
				if (CourseMember.Key <= LowestZ + Tolerance)
				{
					continue;
				}
				const bool bDirectlyBearsOnLowerCourse =
					CourseMembers.ContainsByPredicate(
						[&CourseMember, CrossSection, Tolerance](
							const TPair<double, FBox>& Lower)
						{
							return FMath::IsNearlyEqual(
								Lower.Key,
								CourseMember.Key - CrossSection,
								Tolerance)
								&& FMath::Min(Lower.Value.Max.X,
									CourseMember.Value.Max.X)
									- FMath::Max(Lower.Value.Min.X,
										CourseMember.Value.Min.X) > Tolerance
								&& FMath::Min(Lower.Value.Max.Y,
									CourseMember.Value.Max.Y)
									- FMath::Max(Lower.Value.Min.Y,
										CourseMember.Value.Min.Y) > Tolerance;
						});
				if (!bDirectlyBearsOnLowerCourse)
				{
					++DirectBearingViolationCount;
				}
			}
			if (LowestBounds.IsValid && HighestBounds.IsValid
				&& HighestZ > LowestZ + Tolerance)
			{
				const bool bTapersX =
					Volume->Primitive == EABTSM73DAG5BV2Primitive::Pyramid
					|| Volume->Primitive
						== EABTSM73DAG5BV2Primitive::TriangularPrismX;
				const bool bTapersY =
					Volume->Primitive == EABTSM73DAG5BV2Primitive::Pyramid
					|| Volume->Primitive
						== EABTSM73DAG5BV2Primitive::TriangularPrismY;
				if ((!bTapersX || HighestBounds.GetSize().X
						< LowestBounds.GetSize().X - Tolerance)
					&& (!bTapersY || HighestBounds.GetSize().Y
						< LowestBounds.GetSize().Y - Tolerance))
				{
					++VisiblyTaperedBayCount;
				}
			}
		}
	}
	TestTrue(TEXT("Matrix contains semantic non-box Bays"), NonBoxBayCount > 0);
	TestTrue(TEXT("Planned assembly preserves RoofCourse roles"),
		RoofCourseCount > 0);
	TestEqual(TEXT("Independent envelope audit finds no violations"),
		IndependentViolationCount, 0);
	TestEqual(TEXT("Every upper roof block directly bears on the lower course"),
		DirectBearingViolationCount, 0);
	TestTrue(TEXT("At least one closed roof visibly tapers"),
		VisiblyTaperedBayCount > 0);
	return true;
}

#endif
