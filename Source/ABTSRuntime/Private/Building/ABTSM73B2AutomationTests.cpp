// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73BuildingTypes.h"
#include "Building/ABTSM73PostFailureValidator.h"
#include "Building/ABTSM73StabilityValidator.h"
#include "Building/ABTSM73StructureBuilder.h"
#include "Building/ABTSM73StructureData.h"
#include "Building/ABTSM73WeakPointPlanner.h"
#include "Building/ABTSM7MaterialProfileLibrary.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73StructuralWeaknessFailureTest,
	"ABTS.M73B2.StructuralWeaknessFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73StructuralWeaknessFailureTest::RunTest(const FString& Parameters)
{
	struct FTemplateCase
	{
		EABTSM73Silhouette Silhouette;
		EABTSM73StructuralWeaknessPattern ExpectedPattern;
		EABTSM73PredictedCollapseMode ExpectedCollapseMode;
		int32 ExpectedSupportCount;
		int32 WeaknessBudget;
		int32 ExpectedBrickCount;
	};
	const FTemplateCase Cases[] = {
		{EABTSM73Silhouette::SingleTower, EABTSM73StructuralWeaknessPattern::AsymmetricDualSupport,
			EABTSM73PredictedCollapseMode::Tip, 2, 19, 20},
		{EABTSM73Silhouette::Gatehouse, EABTSM73StructuralWeaknessPattern::CriticalCorner,
			EABTSM73PredictedCollapseMode::Tip, 4, 37, 38},
		{EABTSM73Silhouette::TwinTowerBridge, EABTSM73StructuralWeaknessPattern::OffsetSeam,
			EABTSM73PredictedCollapseMode::SlideAndTip, 3, 38, 39}
	};
	const TArray<FABTSM7MaterialProfile> Profiles = FABTSM7MaterialProfileLibrary::MakeDefaultProfiles();
	FABTSM73StructureBuilder Builder;
	FABTSM73PostFailureValidator FailureValidator;
	FABTSM73WeakPointPlanner Planner;
	FABTSM73StabilityValidator StabilityValidator;
	FABTSM73DifficultySettings Difficulty;
	const auto FindNode = [](const FABTSM73StructureData& Data, const int32 NodeId)
	{
		return Data.Bricks.FindByPredicate([NodeId](const FABTSM73BrickNode& Node)
		{
			return Node.NodeId == NodeId;
		});
	};
	const auto CountEdge = [](const FABTSM73StructureData& Data, const int32 LowerNodeId, const int32 UpperNodeId)
	{
		int32 Count = 0;
		for (const FABTSM73SupportEdge& Edge : Data.SupportEdges)
		{
			if (Edge.LowerNodeId == LowerNodeId
				&& Edge.UpperNodeId == UpperNodeId
				&& Edge.ContactAreaCM2 > 0.0f)
			{
				++Count;
			}
		}
		return Count;
	};

	for (const FTemplateCase& Case : Cases)
	{
		FABTSM73GenerationSettings Settings;
		Settings.Silhouette = Case.Silhouette;
		FABTSM73StructureData Data;
		FString Error;
		const bool bBuilt = Builder.Build(Settings, Data, Error);
		TestTrue(FString::Printf(TEXT("B2 silhouette %d builds: %s"), static_cast<int32>(Case.Silhouette), *Error), bBuilt);
		if (!bBuilt) continue;
		TestEqual(TEXT("Exactly one authored structural intent"), Data.StructuralWeaknessIntents.Num(), 1);
		if (Data.StructuralWeaknessIntents.Num() != 1) continue;
		const FABTSM73StructuralWeaknessIntent Intent = Data.StructuralWeaknessIntents[0];
		TestEqual(TEXT("Auto chooses the silhouette-specific template"), Intent.Pattern, Case.ExpectedPattern);
		TestEqual(TEXT("Template declares the intended collapse mode"), Intent.ExpectedCollapseMode, Case.ExpectedCollapseMode);
		TestEqual(TEXT("Template emits the expected direct-support count"), Intent.DirectSupportNodeIds.Num(), Case.ExpectedSupportCount);
		TestTrue(TEXT("Candidate and carrier are distinct nodes"), Intent.CandidateNodeId != Intent.CarrierNodeId);
		TestTrue(TEXT("Candidate is one of the declared direct supports"), Intent.DirectSupportNodeIds.Contains(Intent.CandidateNodeId));

		const FABTSM73BrickNode* Candidate = FindNode(Data, Intent.CandidateNodeId);
		const FABTSM73BrickNode* Carrier = FindNode(Data, Intent.CarrierNodeId);
		TestNotNull(TEXT("Authored candidate resolves"), Candidate);
		TestNotNull(TEXT("Authored carrier resolves"), Carrier);
		if (Candidate != nullptr)
		{
			TestEqual(TEXT("Candidate is a dedicated weak support"), Candidate->SemanticRole, EABTSM73BrickSemanticRole::WeakSupport);
			TestTrue(TEXT("Candidate is not an aligned ordinary deck"), Candidate->SemanticRole != EABTSM73BrickSemanticRole::Deck);
		}
		if (Carrier != nullptr)
		{
			TestEqual(TEXT("Carrier is one rigid brick"), Carrier->SemanticRole, EABTSM73BrickSemanticRole::Carrier);
		}
		TSet<int32> UniqueSupportIds;
		int32 WeakSupportCount = 0;
		for (const int32 SupportNodeId : Intent.DirectSupportNodeIds)
		{
			UniqueSupportIds.Add(SupportNodeId);
			const FABTSM73BrickNode* Support = FindNode(Data, SupportNodeId);
			TestNotNull(TEXT("Every direct support resolves"), Support);
			if (Support != nullptr)
			{
				WeakSupportCount += Support->SemanticRole == EABTSM73BrickSemanticRole::WeakSupport ? 1 : 0;
				TestTrue(TEXT("Every direct support is a support semantic"),
					Support->SemanticRole == EABTSM73BrickSemanticRole::WeakSupport
					|| Support->SemanticRole == EABTSM73BrickSemanticRole::Column);
				TestEqual(TEXT("Support and carrier share a storey"), Support->StoreyIndex, Carrier != nullptr ? Carrier->StoreyIndex : Support->StoreyIndex);
			}
			TestEqual(TEXT("Every direct support owns exactly one carrier edge"),
				CountEdge(Data, SupportNodeId, Intent.CarrierNodeId), 1);
		}
		TestEqual(TEXT("Direct-support ids are unique"), UniqueSupportIds.Num(), Case.ExpectedSupportCount);
		TestEqual(TEXT("Exactly one direct support is authored as weak"), WeakSupportCount, 1);
		int32 AllCarrierIncomingEdges = 0;
		for (const FABTSM73SupportEdge& Edge : Data.SupportEdges)
		{
			if (Edge.UpperNodeId == Intent.CarrierNodeId) ++AllCarrierIncomingEdges;
		}
		TestEqual(TEXT("Carrier has no undeclared support edge"), AllCarrierIncomingEdges, Case.ExpectedSupportCount);
		TestEqual(TEXT("Template emits two stone payloads"), Intent.PayloadNodeIds.Num(), 2);
		for (const int32 PayloadNodeId : Intent.PayloadNodeIds)
		{
			const FABTSM73BrickNode* Payload = FindNode(Data, PayloadNodeId);
			TestNotNull(TEXT("Every payload resolves"), Payload);
			if (Payload != nullptr)
			{
				TestEqual(TEXT("Payload semantic is preserved"), Payload->SemanticRole, EABTSM73BrickSemanticRole::Payload);
				TestEqual(TEXT("Payload uses Stone before planning"), Payload->Material, EABTSM7BuildingMaterial::Stone);
				TestEqual(TEXT("Payload is one course above the carrier"), Payload->StoreyIndex,
					Carrier != nullptr ? Carrier->StoreyIndex + 1 : Payload->StoreyIndex);
			}
			TestEqual(TEXT("Carrier owns exactly one edge to every payload"),
				CountEdge(Data, Intent.CarrierNodeId, PayloadNodeId), 1);
		}
		Error.Reset();
		TestTrue(FString::Printf(TEXT("B2 geometry is statically stable before planning: %s"), *Error),
			StabilityValidator.Validate(Settings, Data, Error));

		FABTSM73FailureProbeResult Probe;
		const bool bProbeValid = FailureValidator.EvaluateAuthoredIntent(Difficulty, Profiles, Data, Intent, Probe, Error);
		TestTrue(FString::Printf(TEXT("B2 failure probe accepts silhouette %d: %s"), static_cast<int32>(Case.Silhouette), *Error), bProbeValid);
		if (bProbeValid)
		{
			TestTrue(TEXT("Probe marks the authored intent valid"), Probe.bValid);
			TestFalse(TEXT("Accepted probe does not predict a vertical reseat"), Probe.bWouldReseat);
			TestEqual(TEXT("Probe candidate matches the intent"), Probe.CandidateNodeId, Intent.CandidateNodeId);
			TestEqual(TEXT("Probe carrier matches the intent"), Probe.CarrierNodeId, Intent.CarrierNodeId);
			TestEqual(TEXT("Probe pattern matches the intent"), Probe.Pattern, Intent.Pattern);
			TestEqual(TEXT("Probe collapse mode matches the intent"), Probe.CollapseMode, Intent.ExpectedCollapseMode);
			TestTrue(TEXT("Probe margins are finite"), FMath::IsFinite(Probe.InitialSupportMarginCM)
				&& FMath::IsFinite(Probe.TipMarginCM) && FMath::IsFinite(Probe.ReseatRisk));
			TestTrue(TEXT("Intact carrier COM has positive support margin"), Probe.InitialSupportMarginCM >= Difficulty.MinInitialSupportMarginCM);
			TestTrue(TEXT("Candidate removal puts COM beyond the tip boundary"), Probe.TipMarginCM >= Difficulty.MinTipMarginCM);
			TestTrue(TEXT("Vertical reseat risk stays inside the authored limit"), Probe.ReseatRisk <= Difficulty.MaxReseatRisk);
			TestEqual(TEXT("Failure affects carrier plus two payloads"), Probe.AffectedNodeIds.Num(), 3);
			TestTrue(TEXT("Affected set contains the carrier"), Probe.AffectedNodeIds.Contains(Intent.CarrierNodeId));
			TestFalse(TEXT("Affected set excludes the destroyed support"), Probe.AffectedNodeIds.Contains(Intent.CandidateNodeId));
			for (const int32 PayloadNodeId : Intent.PayloadNodeIds)
			{
				TestTrue(TEXT("Affected set contains every payload"), Probe.AffectedNodeIds.Contains(PayloadNodeId));
			}
			TestTrue(TEXT("Predicted tip follows the authored direction"),
				FVector::DotProduct(Probe.TipDirectionLocal.GetSafeNormal(), Intent.ExpectedTipDirectionLocal.GetSafeNormal()) >= 0.25f);
		}

		Error.Reset();
		const bool bPlanned = Planner.Plan(Difficulty, Profiles, FVector::ForwardVector, Settings.BuildingSeed, Data, Error);
		TestTrue(FString::Printf(TEXT("B2 planner accepts silhouette %d: %s"), static_cast<int32>(Case.Silhouette), *Error), bPlanned);
		if (!bPlanned) continue;
		TestEqual(TEXT("Planner emits exactly one weak-point record"), Data.WeakPoints.Num(), 1);
		TestEqual(TEXT("Planner retains exactly one final failure probe"), Data.FailureProbeResults.Num(), 1);
		if (Data.WeakPoints.Num() == 1 && Data.FailureProbeResults.Num() == 1)
		{
			const FABTSM73WeakPointRecord& Record = Data.WeakPoints[0];
			const FABTSM73FailureProbeResult& FinalProbe = Data.FailureProbeResults[0];
			TestEqual(TEXT("Final weak point is the authored candidate"), Record.NodeId, Intent.CandidateNodeId);
			TestEqual(TEXT("Final weak point remains a vertical support"), Record.Role, EABTSM73WeakPointRole::VerticalSupport);
			TestEqual(TEXT("Final weak point preserves the structural pattern"), Record.StructuralPattern, Case.ExpectedPattern);
			TestEqual(TEXT("Final weak point preserves collapse mode"), Record.CollapseMode, Case.ExpectedCollapseMode);
			TestTrue(TEXT("Final record and probe affected sets agree"), Record.AffectedNodeIds == FinalProbe.AffectedNodeIds);
			TestTrue(TEXT("Final record and probe initial margins agree"), FMath::IsNearlyEqual(Record.InitialSupportMarginCM, FinalProbe.InitialSupportMarginCM));
			TestTrue(TEXT("Final record and probe tip margins agree"), FMath::IsNearlyEqual(Record.TipMarginCM, FinalProbe.TipMarginCM));
			TestTrue(TEXT("Final record and probe reseat risks agree"), FMath::IsNearlyEqual(Record.ReseatRisk, FinalProbe.ReseatRisk));
			const FABTSM73BrickNode* FinalCandidate = FindNode(Data, Record.NodeId);
			TestNotNull(TEXT("Final weak-point candidate resolves"), FinalCandidate);
			if (FinalCandidate != nullptr)
			{
				TestEqual(TEXT("Final default weak support uses Glass"), FinalCandidate->Material, EABTSM7BuildingMaterial::Glass);
				TestTrue(TEXT("Weak-point flag reaches the final brick"), FinalCandidate->bWeakPoint);
			}
			AddInfo(FString::Printf(
				TEXT("B2 Silhouette=%d Pattern=%d WeakNode=%d InitialMargin=%.3f TipMargin=%.3f Reseat=%.3f Affected=%d"),
				static_cast<int32>(Case.Silhouette), static_cast<int32>(Record.StructuralPattern), Record.NodeId,
				Record.InitialSupportMarginCM, Record.TipMarginCM, Record.ReseatRisk, Record.AffectedNodeIds.Num()));
		}
		Error.Reset();
		TestTrue(FString::Printf(TEXT("B2 material plan remains statically stable: %s"), *Error),
			StabilityValidator.Validate(Settings, Data, Error));

		FABTSM73StructureData DeterministicData;
		Error.Reset();
		const bool bRebuilt = Builder.Build(Settings, DeterministicData, Error);
		TestTrue(FString::Printf(TEXT("B2 deterministic rebuild succeeds: %s"), *Error), bRebuilt);
		if (!bRebuilt || DeterministicData.StructuralWeaknessIntents.IsEmpty()) continue;
		const FABTSM73StructuralWeaknessIntent RebuiltIntent = DeterministicData.StructuralWeaknessIntents[0];
		TestEqual(TEXT("B2 deterministic brick count"), DeterministicData.Bricks.Num(), Data.Bricks.Num());
		TestEqual(TEXT("B2 deterministic candidate"), RebuiltIntent.CandidateNodeId, Intent.CandidateNodeId);
		TestEqual(TEXT("B2 deterministic carrier"), RebuiltIntent.CarrierNodeId, Intent.CarrierNodeId);
		TestEqual(TEXT("B2 deterministic bay"), RebuiltIntent.BayIndex, Intent.BayIndex);
		TestEqual(TEXT("B2 deterministic pattern"), RebuiltIntent.Pattern, Intent.Pattern);
		TestEqual(TEXT("B2 deterministic collapse mode"), RebuiltIntent.ExpectedCollapseMode, Intent.ExpectedCollapseMode);
		TestEqual(TEXT("B2 deterministic tip direction"), RebuiltIntent.ExpectedTipDirectionLocal, Intent.ExpectedTipDirectionLocal);
		TestTrue(TEXT("B2 deterministic support ids"), RebuiltIntent.DirectSupportNodeIds == Intent.DirectSupportNodeIds);
		TestTrue(TEXT("B2 deterministic payload ids"), RebuiltIntent.PayloadNodeIds == Intent.PayloadNodeIds);
		Error.Reset();
		const bool bReplanned = Planner.Plan(Difficulty, Profiles, FVector::ForwardVector,
			Settings.BuildingSeed, DeterministicData, Error);
		TestTrue(FString::Printf(TEXT("B2 deterministic replan succeeds: %s"), *Error), bReplanned);
		if (!bReplanned) continue;
		for (int32 Index = 0; Index < DeterministicData.Bricks.Num() && Data.Bricks.IsValidIndex(Index); ++Index)
		{
			TestEqual(TEXT("B2 deterministic node id"), DeterministicData.Bricks[Index].NodeId, Data.Bricks[Index].NodeId);
			TestEqual(TEXT("B2 deterministic center"), DeterministicData.Bricks[Index].LocalCenter, Data.Bricks[Index].LocalCenter);
			TestEqual(TEXT("B2 deterministic dimensions"), DeterministicData.Bricks[Index].DimensionsCM, Data.Bricks[Index].DimensionsCM);
			TestEqual(TEXT("B2 deterministic semantic"), DeterministicData.Bricks[Index].SemanticRole, Data.Bricks[Index].SemanticRole);
			TestEqual(TEXT("B2 deterministic material"), DeterministicData.Bricks[Index].Material, Data.Bricks[Index].Material);
			TestEqual(TEXT("B2 deterministic weak flag"), DeterministicData.Bricks[Index].bWeakPoint, Data.Bricks[Index].bWeakPoint);
		}
		TestEqual(TEXT("B2 deterministic weak-point count"), DeterministicData.WeakPoints.Num(), Data.WeakPoints.Num());
		if (!Data.WeakPoints.IsEmpty() && !DeterministicData.WeakPoints.IsEmpty())
		{
			TestEqual(TEXT("B2 deterministic weak node"), DeterministicData.WeakPoints[0].NodeId, Data.WeakPoints[0].NodeId);
			TestTrue(TEXT("B2 deterministic initial margin"), FMath::IsNearlyEqual(
				DeterministicData.WeakPoints[0].InitialSupportMarginCM, Data.WeakPoints[0].InitialSupportMarginCM));
			TestTrue(TEXT("B2 deterministic tip margin"), FMath::IsNearlyEqual(
				DeterministicData.WeakPoints[0].TipMarginCM, Data.WeakPoints[0].TipMarginCM));
			TestTrue(TEXT("B2 deterministic reseat risk"), FMath::IsNearlyEqual(
				DeterministicData.WeakPoints[0].ReseatRisk, Data.WeakPoints[0].ReseatRisk));
		}
	}

	for (const FTemplateCase& Case : Cases)
	{
		FABTSM73GenerationSettings BudgetSettings;
		BudgetSettings.Silhouette = Case.Silhouette;
		BudgetSettings.MaxBrickCount = Case.WeaknessBudget;
		FABTSM73StructureData BudgetData;
		FString BudgetError;
		TestFalse(TEXT("B2 post-generation budget overflow is rejected"), Builder.Build(BudgetSettings, BudgetData, BudgetError));
		TestEqual(TEXT("B2 post-generation budget diagnostic is stable"), BudgetError,
			FString::Printf(TEXT("BrickBudgetExceededWithWeakness:%d:%d"), Case.ExpectedBrickCount, Case.WeaknessBudget));
	}

	FABTSM73GenerationSettings DegenerateSettings;
	FABTSM73StructureData DegenerateData;
	FString DegenerateError;
	TestTrue(TEXT("Degenerate-hull negative fixture builds"), Builder.Build(DegenerateSettings, DegenerateData, DegenerateError));
	if (!DegenerateData.StructuralWeaknessIntents.IsEmpty())
	{
		const FABTSM73StructuralWeaknessIntent DegenerateIntent = DegenerateData.StructuralWeaknessIntents[0];
		int32 RemainingSupportId = INDEX_NONE;
		for (const int32 SupportNodeId : DegenerateIntent.DirectSupportNodeIds)
		{
			if (SupportNodeId != DegenerateIntent.CandidateNodeId)
			{
				RemainingSupportId = SupportNodeId;
				break;
			}
		}
		DegenerateData.SupportEdges.RemoveAll([&DegenerateIntent, RemainingSupportId](const FABTSM73SupportEdge& Edge)
		{
			return Edge.LowerNodeId == RemainingSupportId && Edge.UpperNodeId == DegenerateIntent.CarrierNodeId;
		});
		FABTSM73FailureProbeResult DegenerateProbe;
		TestFalse(TEXT("Missing remaining support hull is rejected"), FailureValidator.EvaluateAuthoredIntent(
			Difficulty, Profiles, DegenerateData, DegenerateIntent, DegenerateProbe, DegenerateError));
		TestEqual(TEXT("Degenerate remaining-hull diagnostic is stable"), DegenerateError,
			FString(TEXT("B2RemainingSupportHullDegenerate")));
	}

	FABTSM73StructureData DirectionData;
	FString DirectionError;
	TestTrue(TEXT("Direction-mismatch negative fixture builds"), Builder.Build(DegenerateSettings, DirectionData, DirectionError));
	if (!DirectionData.StructuralWeaknessIntents.IsEmpty())
	{
		FABTSM73StructuralWeaknessIntent ReversedIntent = DirectionData.StructuralWeaknessIntents[0];
		ReversedIntent.ExpectedTipDirectionLocal *= -1.0f;
		FABTSM73FailureProbeResult DirectionProbe;
		TestFalse(TEXT("Opposite authored tip direction is rejected"), FailureValidator.EvaluateAuthoredIntent(
			Difficulty, Profiles, DirectionData, ReversedIntent, DirectionProbe, DirectionError));
		TestTrue(TEXT("Tip-direction rejection remains diagnosable"), DirectionError.StartsWith(TEXT("B2TipDirectionMismatch:")));

		const TArray<int32> AlignedFallingSet = {ReversedIntent.CandidateNodeId};
		const float AlignedReseatRisk = FailureValidator.EstimateVerticalReseatRisk(
			Profiles, DirectionData, INDEX_NONE, AlignedFallingSet);
		TestTrue(TEXT("Vertical reseat estimator detects an aligned lower landing"),
			FMath::IsNearlyEqual(AlignedReseatRisk, 1.0f));
	}

	FABTSM73StructureData OptionalAuthoredData;
	FString OptionalAuthoredError;
	TestTrue(TEXT("Optional-authored fallback fixture builds"), Builder.Build(
		DegenerateSettings, OptionalAuthoredData, OptionalAuthoredError));
	if (!OptionalAuthoredData.StructuralWeaknessIntents.IsEmpty())
	{
		OptionalAuthoredData.StructuralWeaknessIntents[0].CandidateNodeId = 999999;
		FABTSM73DifficultySettings OptionalDifficulty;
		OptionalDifficulty.bRequireAuthoredStructuralWeakness = false;
		OptionalDifficulty.MinWeakCollapseRatio = 0.0f;
		OptionalDifficulty.MaxSingleWeakCollapseRatio = 1.0f;
		OptionalDifficulty.MinWeakPointExposure = 0.0f;
		OptionalDifficulty.bRejectOutsideDifficultyWindow = false;
		OptionalDifficulty.bReinforceNonWeakCriticalNodes = false;
		const bool bFallbackPlanned = Planner.Plan(OptionalDifficulty, Profiles, FVector::ForwardVector,
			DegenerateSettings.BuildingSeed, OptionalAuthoredData, OptionalAuthoredError);
		TestTrue(FString::Printf(TEXT("Invalid optional authored intent falls back to ordinary planning: %s"),
			*OptionalAuthoredError), bFallbackPlanned);
		if (bFallbackPlanned)
		{
			TestEqual(TEXT("Optional fallback still selects one weak point"), OptionalAuthoredData.WeakPoints.Num(), 1);
			TestTrue(TEXT("Invalid optional authored intent leaves no final B2 probe"),
				OptionalAuthoredData.FailureProbeResults.IsEmpty());
		}
	}

	FABTSM73GenerationSettings LegacySettings;
	LegacySettings.bGenerateStructuralWeakness = false;
	FABTSM73StructureData LegacyData;
	FString LegacyError;
	TestTrue(TEXT("Legacy aligned structure still builds when the experiment is disabled"), Builder.Build(LegacySettings, LegacyData, LegacyError));
	TestTrue(TEXT("Disabled authored weakness emits no intent"), LegacyData.StructuralWeaknessIntents.IsEmpty());
	TestFalse(TEXT("B2 requirement rejects the old aligned structure"), Planner.Plan(
		Difficulty, Profiles, FVector::ForwardVector, LegacySettings.BuildingSeed, LegacyData, LegacyError));
	TestEqual(TEXT("Legacy rejection reason is stable"), LegacyError, FString(TEXT("B2NoValidAuthoredWeakness")));
	TestTrue(TEXT("Legacy rejection leaves no failure probes"), LegacyData.FailureProbeResults.IsEmpty());
	TestTrue(TEXT("Legacy rejection leaves no weak points"), LegacyData.WeakPoints.IsEmpty());
	TestTrue(TEXT("Legacy rejection leaves no reinforcements"), LegacyData.ReinforcedNodeIds.IsEmpty());
	for (const FABTSM73BrickNode& Node : LegacyData.Bricks)
	{
		TestEqual(TEXT("Legacy rejection preserves original material"), Node.Material, Node.OriginalMaterial);
		TestFalse(TEXT("Legacy rejection leaves no weak flag"), Node.bWeakPoint);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73B2ParameterMatrixTest,
	"ABTS.M73B2.ParameterMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73B2ParameterMatrixTest::RunTest(const FString& Parameters)
{
	constexpr int32 ExpectedMatrixCaseCount = 192;
	constexpr float RequiredInitialSupportMarginCM = 2.0f;
	constexpr float RequiredTipMarginCM = 8.0f;
	constexpr float MaximumReseatRisk = 0.35f;
	const int32 Seeds[] = {7301, 7302, 7303, 7310};
	const EABTSM73Silhouette Silhouettes[] = {
		EABTSM73Silhouette::SingleTower,
		EABTSM73Silhouette::Gatehouse,
		EABTSM73Silhouette::TwinTowerBridge
	};
	const EABTSM7BuildingMaterial Materials[] = {
		EABTSM7BuildingMaterial::Wood,
		EABTSM7BuildingMaterial::Stone,
		EABTSM7BuildingMaterial::Iron,
		EABTSM7BuildingMaterial::Glass
	};
	const auto SilhouetteName = [](const EABTSM73Silhouette Silhouette)
	{
		switch (Silhouette)
		{
		case EABTSM73Silhouette::SingleTower: return TEXT("SingleTower");
		case EABTSM73Silhouette::Gatehouse: return TEXT("Gatehouse");
		case EABTSM73Silhouette::TwinTowerBridge: return TEXT("TwinTowerBridge");
		default: return TEXT("UnknownSilhouette");
		}
	};
	const auto MaterialName = [](const EABTSM7BuildingMaterial Material)
	{
		switch (Material)
		{
		case EABTSM7BuildingMaterial::Wood: return TEXT("Wood");
		case EABTSM7BuildingMaterial::Stone: return TEXT("Stone");
		case EABTSM7BuildingMaterial::Iron: return TEXT("Iron");
		case EABTSM7BuildingMaterial::Glass: return TEXT("Glass");
		default: return TEXT("UnknownMaterial");
		}
	};

	const TArray<FABTSM7MaterialProfile> Profiles = FABTSM7MaterialProfileLibrary::MakeDefaultProfiles();
	FABTSM73StructureBuilder Builder;
	FABTSM73WeakPointPlanner Planner;
	FABTSM73StabilityValidator StabilityValidator;
	FABTSM73DifficultySettings Difficulty;
	Difficulty.MinInitialSupportMarginCM = RequiredInitialSupportMarginCM;
	Difficulty.MinTipMarginCM = RequiredTipMarginCM;
	Difficulty.MaxReseatRisk = MaximumReseatRisk;
	// This matrix isolates structural generation/failure validity. Attack-facing
	// exposure and the final difficulty window are validated separately because
	// one fixed local attack vector cannot face all four authored weak corners.
	Difficulty.MinWeakPointExposure = 0.0f;
	Difficulty.bRejectOutsideDifficultyWindow = false;
	TMap<uint8, TSet<uint8>> ObservedDirectionQuadrantsByPattern;
	int32 ExecutedCaseCount = 0;

	for (const int32 Seed : Seeds)
	{
		for (const EABTSM73Silhouette Silhouette : Silhouettes)
		{
			for (const EABTSM7BuildingMaterial Material : Materials)
			{
				for (int32 Levels = 1; Levels <= 4; ++Levels)
				{
					++ExecutedCaseCount;
					const FString CaseLabel = FString::Printf(TEXT("Silhouette=%s Material=%s Levels=%d Seed=%d"),
						SilhouetteName(Silhouette), MaterialName(Material), Levels, Seed);
					FABTSM73GenerationSettings Settings;
					Settings.BuildingSeed = Seed;
					Settings.Silhouette = Silhouette;
					Settings.PrimaryMaterial = Material;
					Settings.Levels = Levels;
					FABTSM73StructureData Data;
					FString Error;

					const bool bBuilt = Builder.Build(Settings, Data, Error);
					TestTrue(FString::Printf(TEXT("%s Builder succeeds: %s"), *CaseLabel, *Error), bBuilt);
					if (!bBuilt) continue;
					TestEqual(FString::Printf(TEXT("%s Authored intent count"), *CaseLabel),
						Data.StructuralWeaknessIntents.Num(), 1);
					if (Data.StructuralWeaknessIntents.Num() == 1)
					{
						const FABTSM73StructuralWeaknessIntent& Intent = Data.StructuralWeaknessIntents[0];
						const FVector Direction = Intent.ExpectedTipDirectionLocal.GetSafeNormal();
						const uint8 DirectionCode = static_cast<uint8>(
							(Direction.X >= 0.0f ? 1u : 0u) | (Direction.Y >= 0.0f ? 2u : 0u));
						ObservedDirectionQuadrantsByPattern.FindOrAdd(static_cast<uint8>(Intent.Pattern)).Add(DirectionCode);
					}
					const bool bPlanned = Planner.Plan(Difficulty, Profiles, FVector::ForwardVector,
						Settings.BuildingSeed, Data, Error);
					TestTrue(FString::Printf(TEXT("%s Planner succeeds: %s"), *CaseLabel, *Error), bPlanned);
					if (!bPlanned) continue;
					const bool bStable = StabilityValidator.Validate(Settings, Data, Error);
					TestTrue(FString::Printf(TEXT("%s StabilityValidator succeeds: %s"), *CaseLabel, *Error), bStable);
					TestEqual(FString::Printf(TEXT("%s Final failure-probe count"), *CaseLabel),
						Data.FailureProbeResults.Num(), 1);
					if (Data.FailureProbeResults.Num() != 1) continue;

					const FABTSM73FailureProbeResult& Probe = Data.FailureProbeResults[0];
					TestTrue(FString::Printf(TEXT("%s Final probe is valid"), *CaseLabel), Probe.bValid);
					TestTrue(FString::Printf(TEXT("%s Probe values are finite"), *CaseLabel),
						FMath::IsFinite(Probe.InitialSupportMarginCM)
						&& FMath::IsFinite(Probe.TipMarginCM)
						&& FMath::IsFinite(Probe.ReseatRisk));
					TestTrue(FString::Printf(TEXT("%s InitialSupportMargin=%.3f Required>=%.3f"),
						*CaseLabel, Probe.InitialSupportMarginCM, RequiredInitialSupportMarginCM),
						Probe.InitialSupportMarginCM >= RequiredInitialSupportMarginCM);
					TestTrue(FString::Printf(TEXT("%s TipMargin=%.3f Required>=%.3f"),
						*CaseLabel, Probe.TipMarginCM, RequiredTipMarginCM),
						Probe.TipMarginCM >= RequiredTipMarginCM);
					TestTrue(FString::Printf(TEXT("%s ReseatRisk=%.3f Required<=%.3f"),
						*CaseLabel, Probe.ReseatRisk, MaximumReseatRisk),
						Probe.ReseatRisk <= MaximumReseatRisk);
				}
			}
		}
	}
	TestEqual(TEXT("B2 parameter matrix executes all 192 combinations"), ExecutedCaseCount, ExpectedMatrixCaseCount);
	for (const EABTSM73StructuralWeaknessPattern Pattern : {
		EABTSM73StructuralWeaknessPattern::AsymmetricDualSupport})
	{
		const TSet<uint8>* DirectionCodes = ObservedDirectionQuadrantsByPattern.Find(static_cast<uint8>(Pattern));
		TestEqual(FString::Printf(TEXT("Single-tower Pattern=%d observes all four tip-direction quadrants"), static_cast<int32>(Pattern)),
			DirectionCodes != nullptr ? DirectionCodes->Num() : 0, 4);
		for (uint8 DirectionCode = 0; DirectionCode < 4; ++DirectionCode)
		{
			TestTrue(FString::Printf(TEXT("Pattern=%d observes direction quadrant code=%d"),
				static_cast<int32>(Pattern), static_cast<int32>(DirectionCode)),
				DirectionCodes != nullptr && DirectionCodes->Contains(DirectionCode));
		}
	}
	return true;
}

#endif
