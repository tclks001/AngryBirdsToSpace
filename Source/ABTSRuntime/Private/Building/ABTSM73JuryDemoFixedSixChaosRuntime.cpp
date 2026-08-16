// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73StableBuildingActor.h"

#include "ABTSM7PenetrationValidator.h"
#include "ABTSRuntime.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Building/ABTSM7BuildingModule.h"
#include "Engine/World.h"
#include "Game/ABTSM7GameMode.h"
#include "PBDRigidsSolver.h"
#include "Components/StaticMeshComponent.h"
#include "HAL/PlatformTime.h"
#include "Misc/Crc.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "Physics/Experimental/PhysScene_Chaos.h"

namespace
{
	constexpr float FixedSixGravityCMPerSec2 = 980.0f;
	constexpr float FixedSixMinimumObservationSeconds = 1.25f;
	constexpr float FixedSixStableHoldSeconds = 0.45f;
	constexpr float FixedSixMaximumObservationSeconds = 6.0f;
	constexpr float FixedSixSimulationDeltaSeconds = 1.0f / 60.0f;
	constexpr float FixedSixMaximumLinearSpeedCMPerSec = 4.0f;
	constexpr float FixedSixMaximumAngularSpeedDegreesPerSec = 1.5f;
	constexpr float FixedSixMaximumPlanarDriftCM = 4.0f;
	constexpr float FixedSixMaximumSettlementCM = 6.0f;
	constexpr float FixedSixMaximumRotationDegrees = 2.0f;
	// Startup contact resolution is a transient non-destruction screen.  Keep
	// the final spatial, quiet and sleep gates strict, while allowing the small
	// overshoot measured by the exact frozen-pad fixture to settle naturally.
	constexpr float FixedSixMaximumPeakPlanarDriftCM = 6.0f;
	constexpr float FixedSixMaximumPeakSettlementCM = 6.0f;
	constexpr float FixedSixMaximumPeakRotationDegrees = 3.0f;
	constexpr float JuryDemoFrozenTangentSupportThicknessCM = 100.0f;
	// Release destruction keeps a dense local collapse while bounding the
	// solver island. The full frozen body set remains the static/certification
	// identity; only real first-hit gameplay uses this activation budget.
	constexpr int32 FixedSixGameplayMaximumActiveBodies = 128;

	uint32 ComputeProductionCandidateHash(
		const FABTSM73JuryDemoFixedSixStaticEntry& Entry,
		const FABTSM7SiteUniformGravityPolicy& GravityPolicy,
		const uint32 BodyProfileHash,
		const uint32 WorldProfileHash,
		const int32 VisibleModuleCount,
		const int32 PhysicsBodyCount)
	{
		const FString Canonical = FString::Printf(
		TEXT("JuryDemoFixedSixProductionChaos:v3:ResearchCandidate=v11")
			TEXT(":Entry=%s:Complexity=%d:Seed=%d:Contract=%d:Layout=%llu")
			TEXT(":Placement=%llu:Descriptor=%llu:Static=%llu:Production=%llu")
			TEXT(":Device=%llu:Registration=%llu:Visible=%d:Bodies=%d")
			TEXT(":AssemblySchema=%d:Assembly=%llu:GravityAuthority=%s")
			TEXT(":GravityIdentity=%llu:GravityPolicy=%u:BodyProfile=%u:WorldProfile=%u")
			TEXT(":GravityModel=SiteUniformTangentGravity")
			TEXT(":GravityWakePolicy=NonInvalidatingForceSkipSleepingBodiesResumeOnExplicitWake")
			TEXT(":MassExpectedPolicy=PerBodySetupCalculateMass")
			TEXT(":PenetrationPolicy=FrozenPendingModulesOnlyReadOnly")
			TEXT(":SimulationClock=ScopedProductionFixedStep")
			TEXT(":BatchExecution=StableSerialWithinBatch:E1ToE6")
			TEXT(":ActivationBarrier=PostActivationFrameBoundary:OuterDTMicro=%d")
			TEXT(":SupportPolicy=FrozenSiteLocalTangentPadAllSix")
			TEXT(":SupportCollisionAuthority=PadBlocksDeveloperObstacle")
			TEXT(":ProductionTerrainDeveloperObstacleResponse=IgnoreUntilEndPlay")
			TEXT(":DynamicModuleObjectType=DeveloperObstacleAfterPhysicsActor")
			TEXT(":ChaosShapeFilterIdentity=DeveloperObstacleVerifiedBeforeObservation")
			TEXT(":SupportPadHalfXMilli=%d:SupportPadHalfYMilli=%d:SupportThicknessMilli=%d")
			TEXT(":SolverEnhancedDeterminism=1")
			TEXT(":MinMS=%d:HoldMS=%d:MaxMS=%d:LinearMilli=%d:AngularMilli=%d")
			TEXT(":FinalDriftMilli=%d:FinalSettlementMilli=%d:FinalRotationMilli=%d")
			TEXT(":PeakDriftMilli=%d:PeakSettlementMilli=%d:PeakRotationMilli=%d"),
			*Entry.ManifestEntryId.ToString(),
			static_cast<int32>(Entry.DemoBuilding),
			Entry.DeterministicSeed,
			Entry.SourceContractVersion,
			Entry.SourceLayoutHash,
			Entry.SourcePlacementHash,
			Entry.DescriptorHash,
			Entry.StaticGeometryHash,
			Entry.ProductionIdentityHash,
			Entry.DeviceAssemblyHash,
			Entry.RegistrationResultHash,
			VisibleModuleCount,
			PhysicsBodyCount,
			Entry.PhysicsAssemblySchemaVersion,
			Entry.PhysicsAssemblyHash,
			*Entry.GravityAuthorityId.ToString(),
			Entry.GravityIdentityHash,
			GravityPolicy.ComputeCrc32(),
			BodyProfileHash,
			WorldProfileHash,
			FMath::RoundToInt(FixedSixSimulationDeltaSeconds * 1000000.0f),
			FMath::RoundToInt(Entry.PadHalfExtentCM.X * 1000.0f),
			FMath::RoundToInt(Entry.PadHalfExtentCM.Y * 1000.0f),
			FMath::RoundToInt(JuryDemoFrozenTangentSupportThicknessCM * 1000.0f),
			FMath::RoundToInt(FixedSixMinimumObservationSeconds * 1000.0f),
			FMath::RoundToInt(FixedSixStableHoldSeconds * 1000.0f),
			FMath::RoundToInt(FixedSixMaximumObservationSeconds * 1000.0f),
			FMath::RoundToInt(FixedSixMaximumLinearSpeedCMPerSec * 1000.0f),
			FMath::RoundToInt(FixedSixMaximumAngularSpeedDegreesPerSec * 1000.0f),
			FMath::RoundToInt(FixedSixMaximumPlanarDriftCM * 1000.0f),
			FMath::RoundToInt(FixedSixMaximumSettlementCM * 1000.0f),
			FMath::RoundToInt(FixedSixMaximumRotationDegrees * 1000.0f),
			FMath::RoundToInt(FixedSixMaximumPeakPlanarDriftCM * 1000.0f),
			FMath::RoundToInt(FixedSixMaximumPeakSettlementCM * 1000.0f),
			FMath::RoundToInt(FixedSixMaximumPeakRotationDegrees * 1000.0f));
		return FCrc::StrCrc32(*Canonical);
	}

	uint32 ComputeProductionResultHash(
		const FABTSM73JuryDemoFixedSixChaosResult& Result)
	{
		const FString Canonical = FString::Printf(
			TEXT("JuryDemoFixedSixProductionChaosResult:v1:Candidate=%u:Accepted=%d")
			TEXT(":ReachedQuiet=%d:EndedQuiet=%d:FirstQuietMS=%d")
			TEXT(":FinalDriftMilli=%d:FinalSettlementMilli=%d:FinalRotationMilli=%d")
			TEXT(":FinalLinearMilli=%d:FinalAngularMilli=%d:FinalAwake=%d")
			TEXT(":PeakDriftMilli=%d:PeakSettlementMilli=%d:PeakRotationMilli=%d"),
			Result.CandidateHash,
			Result.bAccepted ? 1 : 0,
			Result.bReachedQuiet ? 1 : 0,
			Result.bEndedQuiet ? 1 : 0,
			FMath::RoundToInt(Result.FirstQuietSeconds * 1000.0f),
			FMath::RoundToInt(Result.FinalPlanarDriftCM * 1000.0f),
			FMath::RoundToInt(Result.FinalSettlementCM * 1000.0f),
			FMath::RoundToInt(Result.FinalRotationDegrees * 1000.0f),
			FMath::RoundToInt(Result.FinalLinearSpeedCMPerSec * 1000.0f),
			FMath::RoundToInt(Result.FinalAngularSpeedDegreesPerSec * 1000.0f),
			Result.FinalAwakeBodyCount,
			FMath::RoundToInt(Result.PeakPlanarDriftCM * 1000.0f),
			FMath::RoundToInt(Result.PeakSettlementCM * 1000.0f),
			FMath::RoundToInt(Result.PeakRotationDegrees * 1000.0f));
		return FCrc::StrCrc32(*Canonical);
	}
}

bool AABTSM73StableBuildingActor::PrepareJuryDemoFixedSixChaosValidation(
	const float GravityAccelerationCMPerSec2,
	FString& OutError)
{
	OutError.Reset();
	if (!IsJuryDemoFixedSixStaticRegistrationAccepted()
		|| bJuryDemoFixedSixChaosPrepared
		|| bJuryDemoFixedSixChaosRunning
		|| !RuntimeMaterialSystem.IsValid()
		|| !JuryDemoFixedSixStaticEntry.IsSet())
	{
		OutError = TEXT("FixedSixChaosPreparationStateInvalid");
		return false;
	}
	if (!FMath::IsNearlyEqual(
		GravityAccelerationCMPerSec2, FixedSixGravityCMPerSec2, 1.0e-3f))
	{
		OutError = TEXT("FixedSixChaosGravityIdentityMismatch");
		return false;
	}

	FABTSM73JuryDemoFixedSixStaticEntry& Entry =
		JuryDemoFixedSixStaticEntry.GetValue();
	FPhysScene* PhysicsScene = GetWorld() != nullptr
		? GetWorld()->GetPhysicsScene()
		: nullptr;
	Chaos::FPhysicsSolver* PhysicsSolver = PhysicsScene != nullptr
		? PhysicsScene->GetSolver()
		: nullptr;
	if (PhysicsSolver == nullptr || !PhysicsSolver->IsDetemerministic())
	{
		OutError = TEXT("FixedSixChaosEnhancedDeterminismMissing");
		RejectRuntimeStructure(OutError);
		return false;
	}
	if (!ValidateJuryDemoFixedSixFrozenTangentSupport(Entry, OutError))
	{
		RejectRuntimeStructure(OutError);
		return false;
	}
	const double PreparationStartSeconds = FPlatformTime::Seconds();
	double PreviousPhaseSeconds = PreparationStartSeconds;
	const auto LogPreparationPhase = [this, &Entry, PreparationStartSeconds,
		&PreviousPhaseSeconds](const TCHAR* Phase)
	{
		const double NowSeconds = FPlatformTime::Seconds();
		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][M7][FixedSixProductionChaos][PrepareTiming]")
			TEXT(" Entry=%s Complexity=E%d Phase=%s SegmentMS=%.3f TotalMS=%.3f"),
			*Entry.ManifestEntryId.ToString(),
			static_cast<int32>(Entry.DemoBuilding), Phase,
			(NowSeconds - PreviousPhaseSeconds) * 1000.0,
			(NowSeconds - PreparationStartSeconds) * 1000.0);
		PreviousPhaseSeconds = NowSeconds;
	};
	LogPreparationPhase(TEXT("Begin"));
	FABTSM7SiteUniformGravityPolicy GravityPolicy;
	if (!CopyJuryDemoSiteUniformGravityPolicy(
		GravityAccelerationCMPerSec2, GravityPolicy))
	{
		OutError = TEXT("FixedSixChaosSiteUniformPolicyInvalid");
		return false;
	}

	const int32 AuxiliaryModuleCount = Entry.Devices.Num() + Entry.Caps.Num();
	if (RuntimeModules.Num() != AuxiliaryModuleCount)
	{
		OutError = TEXT("FixedSixChaosAuxiliaryModuleCountMismatch");
		return false;
	}
	TArray<TWeakObjectPtr<AABTSM7BuildingModule>> AuxiliaryModules =
		RuntimeModules;
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& WeakModule :
		AuxiliaryModules)
	{
		if (!WeakModule.IsValid())
		{
			OutError = TEXT("FixedSixChaosAuxiliaryModuleMissing");
			return false;
		}
	}

	IdleValidationState = EABTSM73IdleValidationState::Running;
	ClearBrickPreviews();
	JuryDemoFixedSixStaticBrickInstanceCount = 0;
	RuntimeModules.Reset(Entry.Bricks.Num() + AuxiliaryModuleCount);
	TArray<double> ExpectedBrickMassKG;
	ExpectedBrickMassKG.Reserve(Entry.Bricks.Num());
	double TotalActualMassKG = 0.0;
	double TotalExpectedMassKG = 0.0;
	int32 MassMismatchCount = 0;
	int32 InvalidMassBodyCount = 0;

	for (int32 BrickIndex = 0; BrickIndex < Entry.Bricks.Num(); ++BrickIndex)
	{
		const FABTSM73BeamD1BrickBinding& Brick = Entry.Bricks[BrickIndex];
		AABTSM7BuildingModule* Module =
			RuntimeMaterialSystem->SpawnStaticBrickModule(
				Brick.BrickSpec,
				Brick.LocalTransform * GetActorTransform());
		if (Module == nullptr)
		{
			OutError = FString::Printf(
				TEXT("FixedSixChaosBrickSpawnFailed:%d"), BrickIndex);
			RejectRuntimeStructure(OutError);
			return false;
		}
		Module->ConfigureDamageLifecycleOwner(
			this, Brick.BrickId, false);
		RuntimeModules.Add(Module);
	}
	RuntimeModules.Append(AuxiliaryModules);
	LogPreparationPhase(TEXT("SpawnComplete"));

	for (int32 ModuleIndex = 0; ModuleIndex < RuntimeModules.Num(); ++ModuleIndex)
	{
		AABTSM7BuildingModule* Module = RuntimeModules[ModuleIndex].Get();
		UStaticMeshComponent* Mesh =
			Module != nullptr ? Module->GetMeshComponent() : nullptr;
		FBodyInstance* Body = Mesh != nullptr ? Mesh->GetBodyInstance() : nullptr;
		UBodySetup* BodySetup = Body != nullptr ? Body->GetBodySetup() : nullptr;
		if (Module == nullptr || Mesh == nullptr || Body == nullptr
			|| BodySetup == nullptr)
		{
			++InvalidMassBodyCount;
			continue;
		}
		const double ActualMassKG = Body->GetBodyMass();
		const double ExpectedMassKG = BodySetup->CalculateMass(Mesh);
		const double ToleranceKG = FMath::Max(0.01, ExpectedMassKG * 0.001);
		if (!FMath::IsFinite(ActualMassKG) || !FMath::IsFinite(ExpectedMassKG)
			|| ActualMassKG <= 0.0 || ExpectedMassKG <= 0.0
			|| !FMath::IsNearlyEqual(
				ActualMassKG, ExpectedMassKG, ToleranceKG))
		{
			++MassMismatchCount;
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M7][FixedSixProductionChaos][BodyMassRejected]")
				TEXT(" Entry=%s Index=%d ActualKG=%.6f ExpectedKG=%.6f ToleranceKG=%.6f"),
				*Entry.ManifestEntryId.ToString(), ModuleIndex,
				ActualMassKG, ExpectedMassKG, ToleranceKG);
		}
		if (ModuleIndex < Entry.Bricks.Num())
		{
			ExpectedBrickMassKG.Add(ExpectedMassKG);
		}
		TotalActualMassKG += ActualMassKG;
		TotalExpectedMassKG += ExpectedMassKG;
	}
	if (InvalidMassBodyCount > 0 || MassMismatchCount > 0
		|| ExpectedBrickMassKG.Num() != Entry.Bricks.Num())
	{
		OutError = FString::Printf(
			TEXT("FixedSixChaosBodyMassInvalid:Missing=%d:Mismatch=%d"),
			InvalidMassBodyCount, MassMismatchCount);
		RejectRuntimeStructure(OutError);
		return false;
	}
	LogPreparationPhase(TEXT("MassComplete"));

	TArray<AABTSM7BuildingModule*> VisibleModules;
	VisibleModules.Reserve(RuntimeModules.Num());
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& WeakModule :
		RuntimeModules)
	{
		VisibleModules.Add(WeakModule.Get());
	}
	const FABTSM7PenetrationValidationStats Penetration =
		RuntimeMaterialSystem->ValidatePendingModuleInterpenetration(VisibleModules);
	if (Penetration.DetectedPairCount != 0
		|| Penetration.RepairCount != 0
		|| Penetration.LargeErrorPairCount != 0
		|| Penetration.RemainingSmallPairCount != 0)
	{
		OutError = FString::Printf(
			TEXT("FixedSixChaosPenetrationInvalid:Pairs=%d:Repairs=%d:Large=%d:Small=%d:Depth=%.4f"),
			Penetration.DetectedPairCount, Penetration.RepairCount,
			Penetration.LargeErrorPairCount,
			Penetration.RemainingSmallPairCount,
			Penetration.MaximumDetectedDepthCM);
		RejectRuntimeStructure(OutError);
		return false;
	}
	LogPreparationPhase(TEXT("PenetrationComplete"));

	JuryDemoFixedSixChaosPhysicsModules.Reset();
	if (Entry.PhysicsAssemblyHash != 0)
	{
		for (const FABTSM73BuildingFreezeV3PhysicsCluster& Cluster :
			Entry.PhysicsClusters)
		{
			if (!RuntimeModules.IsValidIndex(Cluster.RootBrickId))
			{
				OutError = TEXT("FixedSixChaosCompoundRootInvalid");
				RejectRuntimeStructure(OutError);
				return false;
			}
			AABTSM7BuildingModule* CompoundRootModule =
				RuntimeModules[Cluster.RootBrickId].Get();
			double ExpectedCompoundMassKG = 0.0;
			for (const int32 BrickId : Cluster.BrickIds)
			{
				if (!RuntimeModules.IsValidIndex(BrickId)
					|| !ExpectedBrickMassKG.IsValidIndex(BrickId))
				{
					OutError = TEXT("FixedSixChaosCompoundMemberInvalid");
					RejectRuntimeStructure(OutError);
					return false;
				}
				ExpectedCompoundMassKG += ExpectedBrickMassKG[BrickId];
				if (BrickId != Cluster.RootBrickId
					&& !CompoundRootModule->TryWeldStaticChild(
						*RuntimeModules[BrickId].Get()))
				{
					OutError = FString::Printf(
						TEXT("FixedSixChaosCompoundWeldFailed:%d:%d"),
						Cluster.ClusterId, BrickId);
					RejectRuntimeStructure(OutError);
					return false;
				}
			}
			const double ActualCompoundMassKG =
				CompoundRootModule->GetMeshComponent()->GetMass();
			const double CompoundToleranceKG =
				FMath::Max(0.05, ExpectedCompoundMassKG * 0.001);
			if (!FMath::IsNearlyEqual(
				ActualCompoundMassKG,
				ExpectedCompoundMassKG,
				CompoundToleranceKG))
			{
				OutError = FString::Printf(
					TEXT("FixedSixChaosCompoundMassMismatch:%d:Actual=%.6f:Expected=%.6f"),
					Cluster.ClusterId, ActualCompoundMassKG,
					ExpectedCompoundMassKG);
				RejectRuntimeStructure(OutError);
				return false;
			}
			JuryDemoFixedSixChaosPhysicsModules.Add(CompoundRootModule);
		}
		for (int32 ModuleIndex = Entry.Bricks.Num();
			ModuleIndex < RuntimeModules.Num(); ++ModuleIndex)
		{
			JuryDemoFixedSixChaosPhysicsModules.Add(
				RuntimeModules[ModuleIndex]);
		}
	}
	else
	{
		JuryDemoFixedSixChaosPhysicsModules = RuntimeModules;
	}

	const int32 ExpectedPhysicsBodyCount = Entry.PhysicsBodyCount > 0
		? Entry.PhysicsBodyCount
		: RuntimeModules.Num();
	if (JuryDemoFixedSixChaosPhysicsModules.Num()
		!= ExpectedPhysicsBodyCount)
	{
		OutError = FString::Printf(
			TEXT("FixedSixChaosBodyCountMismatch:Actual=%d:Expected=%d"),
			JuryDemoFixedSixChaosPhysicsModules.Num(),
			ExpectedPhysicsBodyCount);
		RejectRuntimeStructure(OutError);
		return false;
	}
	LogPreparationPhase(TEXT("AssemblyComplete"));

	JuryDemoFixedSixChaosInitialTransforms.Reset(RuntimeModules.Num());
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& WeakModule :
		RuntimeModules)
	{
		AABTSM7BuildingModule* Module = WeakModule.Get();
		JuryDemoFixedSixChaosInitialTransforms.Add(
			Module->GetActorTransform());
		Module->SetContactDamageGraceSeconds(
			FixedSixMaximumObservationSeconds + 1.0f);
	}

	const FABTSM7ChaosBodyProfile BodyProfile =
		FABTSM7ChaosBodyProfile::Production();
	const FABTSM7ChaosWorldProfile WorldProfile =
		FABTSM7ChaosWorldProfile::CaptureProduction();
	JuryDemoFixedSixChaosResult = FABTSM73JuryDemoFixedSixChaosResult();
	JuryDemoFixedSixChaosResult.ManifestEntryId = Entry.ManifestEntryId;
	JuryDemoFixedSixChaosResult.ComplexityId = Entry.DemoBuilding;
	JuryDemoFixedSixChaosResult.DeterministicSeed = Entry.DeterministicSeed;
	JuryDemoFixedSixChaosResult.VisibleModuleCount = RuntimeModules.Num();
	JuryDemoFixedSixChaosResult.PhysicsBodyCount =
		JuryDemoFixedSixChaosPhysicsModules.Num();
	JuryDemoFixedSixChaosResult.PhysicsAssemblyHash =
		Entry.PhysicsAssemblyHash;
	JuryDemoFixedSixChaosResult.CandidateHash = ComputeProductionCandidateHash(
		Entry, GravityPolicy, BodyProfile.ComputeCrc32(),
		WorldProfile.ComputeCrc32(), RuntimeModules.Num(),
		JuryDemoFixedSixChaosPhysicsModules.Num());
	JuryDemoFixedSixChaosSiteUp = GravityPolicy.SiteUp;
	bJuryDemoFixedSixChaosPrepared = true;

	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M7][FixedSixProductionChaos][Prepared]")
		TEXT(" Entry=%s Complexity=E%d Seed=%d Visible=%d Bodies=%d")
		TEXT(" Assembly=%llu Candidate=%u BodyHash=%u WorldHash=%u")
		TEXT(" GravityPolicy=%u SiteUp=%s MassActualKG=%.6f")
		TEXT(" MassExpectedKG=%.6f MassGate=Accepted PenetrationGate=Accepted")
		TEXT(" EnhancedDeterminism=1"),
		*Entry.ManifestEntryId.ToString(),
		static_cast<int32>(Entry.DemoBuilding),
		Entry.DeterministicSeed,
		RuntimeModules.Num(), JuryDemoFixedSixChaosPhysicsModules.Num(),
		Entry.PhysicsAssemblyHash,
		JuryDemoFixedSixChaosResult.CandidateHash,
		BodyProfile.ComputeCrc32(), WorldProfile.ComputeCrc32(),
		GravityPolicy.ComputeCrc32(),
		*GravityPolicy.SiteUp.ToCompactString(),
		TotalActualMassKG, TotalExpectedMassKG);
	return true;
}

bool AABTSM73StableBuildingActor::
ActivatePreparedJuryDemoFixedSixChaosValidation(
	FString& OutError,
	const FVector* GameplayImpactWorld)
{
	OutError.Reset();
	if (!bJuryDemoFixedSixChaosPrepared
		|| bJuryDemoFixedSixChaosRunning
		|| !RuntimeMaterialSystem.IsValid()
		|| !JuryDemoFixedSixStaticEntry.IsSet())
	{
		OutError = TEXT("FixedSixChaosActivationStateInvalid");
		return false;
	}
	TArray<AABTSM7BuildingModule*> PhysicsModules;
	PhysicsModules.Reserve(JuryDemoFixedSixChaosPhysicsModules.Num());
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& WeakModule :
		JuryDemoFixedSixChaosPhysicsModules)
	{
		AABTSM7BuildingModule* Module = WeakModule.Get();
		if (Module == nullptr || Module->IsDynamic())
		{
			OutError = TEXT("FixedSixChaosPreparedBodyInvalid");
			return false;
		}
		PhysicsModules.Add(Module);
	}
	const int32 CertifiedPhysicsBodyCount = PhysicsModules.Num();
	if (GameplayImpactWorld != nullptr
		&& PhysicsModules.Num() > FixedSixGameplayMaximumActiveBodies)
	{
		const FVector ImpactWorld = *GameplayImpactWorld;
		PhysicsModules.StableSort([&ImpactWorld](
			const AABTSM7BuildingModule& A,
			const AABTSM7BuildingModule& B)
		{
			const double DistanceA = FVector::DistSquared(
				A.GetActorLocation(), ImpactWorld);
			const double DistanceB = FVector::DistSquared(
				B.GetActorLocation(), ImpactWorld);
			if (!FMath::IsNearlyEqual(DistanceA, DistanceB, 0.001))
			{
				return DistanceA < DistanceB;
			}
			return A.GetDamageLifecycleBrickId()
				< B.GetDamageLifecycleBrickId();
		});
		PhysicsModules.SetNum(FixedSixGameplayMaximumActiveBodies);
	}
	JuryDemoFixedSixActivePhysicsBodyCount = PhysicsModules.Num();
	const FABTSM73JuryDemoFixedSixStaticEntry& Entry =
		JuryDemoFixedSixStaticEntry.GetValue();
	int32 E1CrystalTargetCount = 0;
	FABTSM73E1DestructibleModuleTargetSet E1TargetSet;
	if (Entry.ManifestEntryId == FName(TEXT("E1ColumnBreak")))
	{
		for (const TWeakObjectPtr<AABTSM7BuildingModule>& WeakModule :
			RuntimeModules)
		{
			AABTSM7BuildingModule* Module = WeakModule.Get();
			UStaticMeshComponent* Mesh = Module != nullptr
				? Module->GetMeshComponent()
				: nullptr;
			if (Module == nullptr
				|| Module->GetDamageLifecycleOwner() != this
				|| Module->GetOwner() != RuntimeMaterialSystem.Get()
				|| Mesh == nullptr
				|| Mesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
			{
				OutError = TEXT("E1DamageLifecycleRealModuleIdentityInvalid");
				return false;
			}
			E1CrystalTargetCount += Module->IsCrystalLifecycleTarget() ? 1 : 0;
		}
		if (E1CrystalTargetCount != 1)
		{
			OutError = FString::Printf(
				TEXT("E1DamageLifecycleCrystalTargetCountInvalid:%d"),
				E1CrystalTargetCount);
			return false;
		}
		if (!CopyJuryDemoE1DestructibleModuleTargetSet(E1TargetSet))
		{
			OutError = TEXT("E1DamageLifecycleOrderedTargetSetInvalid");
			return false;
		}
	}
	if (!RuntimeMaterialSystem->BeginSiteUniformLaunchPhysics(
		PhysicsModules,
		Entry.WorldTransform.GetLocation(),
		Entry.SupportCenterWorldCM,
		FixedSixGravityCMPerSec2,
		FixedSixMaximumObservationSeconds + 1.0f,
		/*bPenetrationPrevalidated=*/true))
	{
		OutError = TEXT("FixedSixChaosSiteUniformActivationRejected");
		return false;
	}
	FABTSM7SiteUniformGravityPolicy ExpectedPolicy;
	if (!CopyJuryDemoSiteUniformGravityPolicy(
		FixedSixGravityCMPerSec2, ExpectedPolicy)
		|| RuntimeMaterialSystem->GetLastSiteUniformGravityPolicyHash()
			!= ExpectedPolicy.ComputeCrc32()
		|| !RuntimeMaterialSystem->GetLastSiteUniformGravityUp().Equals(
			ExpectedPolicy.SiteUp, 1.0e-6))
	{
		OutError = TEXT("FixedSixChaosActivatedGravityIdentityMismatch");
		return false;
	}

	JuryDemoFixedSixChaosResult.InternalSeconds = 0.0f;
	JuryDemoFixedSixChaosQuietSeconds = 0.0f;
	JuryDemoFixedSixChaosWallStartSeconds = FPlatformTime::Seconds();
	JuryDemoFixedSixChaosActivationFrame = GFrameCounter;
	bJuryDemoFixedSixChaosRunning = true;
	if (Entry.ManifestEntryId == FName(TEXT("E1ColumnBreak")))
	{
		JuryDemoE1DamageLifecycleState.RecordChaosActivated();
		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][M7][E1DamageLifecycle]")
			TEXT(" Event=ChaosActivated RealModules=%d TargetBricks=%d")
			TEXT(" CrystalTargets=%d TargetGeometry=%u")
			TEXT(" OrderedDescriptorUnion=1 ProxyTargets=0 Accepted=0"),
			RuntimeModules.Num(),
			E1TargetSet.OrderedBrickTargets.Num(), E1CrystalTargetCount,
			E1TargetSet.ComputeOrderedGeometryHash());
	}
	SetActorTickEnabled(true);
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M7][FixedSixProductionChaos][Activated]")
		TEXT(" Entry=%s Candidate=%u Visible=%d Bodies=%d CertifiedBodies=%d GameplayBudgeted=%d")
		TEXT(" GravityModel=SiteUniformTangentGravity SiteUp=%s")
		TEXT(" SimulationClock=ScopedProductionFixedStep SimulationHz=60")
		TEXT(" M6RadialReactivation=Forbidden Accepted=1"),
		*Entry.ManifestEntryId.ToString(),
		JuryDemoFixedSixChaosResult.CandidateHash,
		JuryDemoFixedSixChaosResult.VisibleModuleCount,
		JuryDemoFixedSixActivePhysicsBodyCount,
		CertifiedPhysicsBodyCount,
		GameplayImpactWorld != nullptr
			&& JuryDemoFixedSixActivePhysicsBodyCount < CertifiedPhysicsBodyCount
			? 1 : 0,
		*ExpectedPolicy.SiteUp.ToCompactString());
	return true;
}

bool AABTSM73StableBuildingActor::MarkPreparedJuryDemoFixedSixChaosDeferred(
    FString& OutError)
{
	OutError.Reset();
	if (!bJuryDemoFixedSixChaosPrepared || bJuryDemoFixedSixChaosRunning
		|| bJuryDemoFixedSixChaosDeferredUntilFirstHit
		|| !JuryDemoFixedSixStaticEntry.IsSet())
	{
		OutError = TEXT("FixedSixChaosDeferredStateInvalid");
		return false;
	}
	bJuryDemoFixedSixChaosDeferredUntilFirstHit = true;
	bJuryDemoFixedSixChaosDeferredActivated = false;
	// Startup is static-ready, not a substitute for a real-time Chaos certificate.
	IdleValidationState = EABTSM73IdleValidationState::Accepted;
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M7][FixedSixDeferredChaos][StaticReady]")
		TEXT(" Entry=%s Candidate=%u ChaosDeferredUntilFirstHit=1")
		TEXT(" StartupChaosCertified=0"),
		*JuryDemoFixedSixStaticEntry->ManifestEntryId.ToString(),
		JuryDemoFixedSixChaosResult.CandidateHash);
	return true;
}

bool AABTSM73StableBuildingActor::
ActivateDeferredJuryDemoFixedSixChaosForFirstHit(
	const AABTSM7BuildingModule& TriggerModule, FString& OutError)
{
	OutError.Reset();
	if (bJuryDemoFixedSixChaosDeferredActivated)
	{
		return true;
	}
	if (!bJuryDemoFixedSixChaosDeferredUntilFirstHit
		|| bJuryDemoFixedSixChaosDeferredActivationInProgress
		|| TriggerModule.GetDamageLifecycleOwner() != this
		|| !RuntimeModules.ContainsByPredicate([&TriggerModule](
			const TWeakObjectPtr<AABTSM7BuildingModule>& Candidate)
			{ return Candidate.Get() == &TriggerModule; }))
	{
		OutError = TEXT("FixedSixDeferredFirstHitIdentityRejected");
		return false;
	}
	bJuryDemoFixedSixChaosDeferredActivationInProgress = true;
	if (UWorld* World = GetWorld())
	{
		AABTSM7GameMode* GameMode = Cast<AABTSM7GameMode>(
			World->GetAuthGameMode());
		if (GameMode == nullptr
			|| !GameMode->
				RestoreJuryDemoFixedSixTerrainCollisionForDeferredFirstHit(
					OutError))
		{
			if (OutError.IsEmpty())
			{
				OutError = TEXT("FixedSixDeferredTerrainCollisionAuthorityMissing");
			}
			bJuryDemoFixedSixChaosDeferredActivationInProgress = false;
			return false;
		}
	}
	const FVector GameplayImpactWorld = TriggerModule.GetActorLocation();
	if (!ActivatePreparedJuryDemoFixedSixChaosValidation(
		OutError, &GameplayImpactWorld))
	{
		bJuryDemoFixedSixChaosDeferredActivationInProgress = false;
		return false;
	}
	// This is gameplay promotion, not the old startup stability observation.
	bJuryDemoFixedSixChaosRunning = false;
	SetActorTickEnabled(false);
	bJuryDemoFixedSixChaosDeferredUntilFirstHit = false;
	bJuryDemoFixedSixChaosDeferredActivated = true;
	bJuryDemoFixedSixChaosDeferredActivationInProgress = false;
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M7][FixedSixDeferredChaos][FirstHitActivated]")
		TEXT(" Entry=%s Trigger=%s AllBodies=%d PhysicsActorDeveloperObstacle=1")
		TEXT(" TerrainBuildingResponse=Block PadsBuildingResponse=Block")
		TEXT(" CCD=1 SiteUniformGravity=1 DamageTransaction=Continue"),
		*JuryDemoFixedSixStaticEntry->ManifestEntryId.ToString(),
		*TriggerModule.GetName(), JuryDemoFixedSixActivePhysicsBodyCount);
	return true;
}

void AABTSM73StableBuildingActor::TickJuryDemoFixedSixChaosValidation(
	const float DeltaSeconds)
{
	// Promotion is triggered from a timer late in UWorld::Tick. A newly enabled
	// actor can still receive the already-started frame's old DeltaSeconds even
	// though its bodies have not had a Chaos step. Start evidence at the first
	// complete post-activation frame; every sampled frame remains exact 60 Hz.
	if (GFrameCounter == JuryDemoFixedSixChaosActivationFrame)
	{
		return;
	}
	if (!FMath::IsFinite(DeltaSeconds)
		|| !FMath::IsNearlyEqual(
			DeltaSeconds, FixedSixSimulationDeltaSeconds, 0.00001f))
	{
		RejectJuryDemoFixedSixChaosValidation(FString::Printf(
			TEXT("FixedSixChaosSimulationStepMismatch:Actual=%.9f:Expected=%.9f"),
			DeltaSeconds, FixedSixSimulationDeltaSeconds));
		return;
	}
	const float EffectiveDeltaSeconds = FixedSixSimulationDeltaSeconds;
	JuryDemoFixedSixChaosResult.InternalSeconds += EffectiveDeltaSeconds;
	JuryDemoFixedSixChaosResult.FinalPlanarDriftCM = 0.0f;
	JuryDemoFixedSixChaosResult.FinalSettlementCM = 0.0f;
	JuryDemoFixedSixChaosResult.FinalRotationDegrees = 0.0f;
	JuryDemoFixedSixChaosResult.FinalLinearSpeedCMPerSec = 0.0f;
	JuryDemoFixedSixChaosResult.FinalAngularSpeedDegreesPerSec = 0.0f;
	JuryDemoFixedSixChaosResult.FinalAwakeBodyCount = 0;
	bool bEveryBodyQuiet = true;

	for (int32 ModuleIndex = 0; ModuleIndex < RuntimeModules.Num();
		++ModuleIndex)
	{
		AABTSM7BuildingModule* Module = RuntimeModules[ModuleIndex].Get();
		UStaticMeshComponent* Mesh =
			Module != nullptr ? Module->GetMeshComponent() : nullptr;
		if (Module == nullptr || Mesh == nullptr
			|| !JuryDemoFixedSixChaosInitialTransforms.IsValidIndex(ModuleIndex))
		{
			RejectJuryDemoFixedSixChaosValidation(
				FString::Printf(TEXT("FixedSixChaosModuleLost:%d"), ModuleIndex));
			return;
		}

		const FTransform& Initial =
			JuryDemoFixedSixChaosInitialTransforms[ModuleIndex];
		const FVector Delta =
			Module->GetActorLocation() - Initial.GetLocation();
		const float PlanarDriftCM = FVector::VectorPlaneProject(
			Delta, JuryDemoFixedSixChaosSiteUp).Size();
		const float SettlementCM = FMath::Abs(
			FVector::DotProduct(Delta, JuryDemoFixedSixChaosSiteUp));
		const float RotationDegrees = FMath::RadiansToDegrees(
			Initial.GetRotation().AngularDistance(Module->GetActorQuat()));
		const float LinearSpeedCMPerSec =
			Mesh->GetPhysicsLinearVelocity().Size();
		const float AngularSpeedDegreesPerSec =
			Mesh->GetPhysicsAngularVelocityInDegrees().Size();
		JuryDemoFixedSixChaosResult.FinalPlanarDriftCM = FMath::Max(
			JuryDemoFixedSixChaosResult.FinalPlanarDriftCM,
			PlanarDriftCM);
		JuryDemoFixedSixChaosResult.FinalSettlementCM = FMath::Max(
			JuryDemoFixedSixChaosResult.FinalSettlementCM,
			SettlementCM);
		JuryDemoFixedSixChaosResult.FinalRotationDegrees = FMath::Max(
			JuryDemoFixedSixChaosResult.FinalRotationDegrees,
			RotationDegrees);
		JuryDemoFixedSixChaosResult.FinalLinearSpeedCMPerSec = FMath::Max(
			JuryDemoFixedSixChaosResult.FinalLinearSpeedCMPerSec,
			LinearSpeedCMPerSec);
		JuryDemoFixedSixChaosResult.FinalAngularSpeedDegreesPerSec =
			FMath::Max(
				JuryDemoFixedSixChaosResult.FinalAngularSpeedDegreesPerSec,
				AngularSpeedDegreesPerSec);
		if (!Module->IsCompoundChild())
		{
			const FBodyInstance* Body = Mesh->GetBodyInstance();
			JuryDemoFixedSixChaosResult.FinalAwakeBodyCount +=
				Body != nullptr && Body->IsInstanceAwake() ? 1 : 0;
		}
		bEveryBodyQuiet = bEveryBodyQuiet
			&& LinearSpeedCMPerSec <= FixedSixMaximumLinearSpeedCMPerSec
			&& AngularSpeedDegreesPerSec
				<= FixedSixMaximumAngularSpeedDegreesPerSec;
	}

	JuryDemoFixedSixChaosResult.PeakPlanarDriftCM = FMath::Max(
		JuryDemoFixedSixChaosResult.PeakPlanarDriftCM,
		JuryDemoFixedSixChaosResult.FinalPlanarDriftCM);
	JuryDemoFixedSixChaosResult.PeakSettlementCM = FMath::Max(
		JuryDemoFixedSixChaosResult.PeakSettlementCM,
		JuryDemoFixedSixChaosResult.FinalSettlementCM);
	JuryDemoFixedSixChaosResult.PeakRotationDegrees = FMath::Max(
		JuryDemoFixedSixChaosResult.PeakRotationDegrees,
		JuryDemoFixedSixChaosResult.FinalRotationDegrees);
	if (JuryDemoFixedSixChaosResult.InternalSeconds
			>= FixedSixMinimumObservationSeconds
		&& bEveryBodyQuiet)
	{
		JuryDemoFixedSixChaosQuietSeconds += EffectiveDeltaSeconds;
	}
	else
	{
		JuryDemoFixedSixChaosQuietSeconds = 0.0f;
	}
	if (!JuryDemoFixedSixChaosResult.bReachedQuiet
		&& JuryDemoFixedSixChaosQuietSeconds
			>= FixedSixStableHoldSeconds)
	{
		JuryDemoFixedSixChaosResult.bReachedQuiet = true;
		JuryDemoFixedSixChaosResult.FirstQuietSeconds =
			JuryDemoFixedSixChaosResult.InternalSeconds;
	}
	if (JuryDemoFixedSixChaosResult.InternalSeconds
		>= FixedSixMaximumObservationSeconds)
	{
		FinishJuryDemoFixedSixChaosValidation();
	}
}

void AABTSM73StableBuildingActor::
FinishJuryDemoFixedSixChaosValidation()
{
	JuryDemoFixedSixChaosResult.bEndedQuiet =
		JuryDemoFixedSixChaosQuietSeconds >= FixedSixStableHoldSeconds;
	JuryDemoFixedSixChaosResult.WallSeconds =
		FPlatformTime::Seconds() - JuryDemoFixedSixChaosWallStartSeconds;
	JuryDemoFixedSixChaosResult.bAccepted =
		JuryDemoFixedSixChaosResult.bReachedQuiet
		&& JuryDemoFixedSixChaosResult.bEndedQuiet
		&& JuryDemoFixedSixChaosResult.FinalAwakeBodyCount == 0
		&& JuryDemoFixedSixChaosResult.FinalPlanarDriftCM
			<= FixedSixMaximumPlanarDriftCM
		&& JuryDemoFixedSixChaosResult.FinalSettlementCM
			<= FixedSixMaximumSettlementCM
		&& JuryDemoFixedSixChaosResult.FinalRotationDegrees
			<= FixedSixMaximumRotationDegrees
		&& JuryDemoFixedSixChaosResult.PeakPlanarDriftCM
			<= FixedSixMaximumPeakPlanarDriftCM
		&& JuryDemoFixedSixChaosResult.PeakSettlementCM
			<= FixedSixMaximumPeakSettlementCM
		&& JuryDemoFixedSixChaosResult.PeakRotationDegrees
			<= FixedSixMaximumPeakRotationDegrees;
	JuryDemoFixedSixChaosResult.ResultHash =
		ComputeProductionResultHash(JuryDemoFixedSixChaosResult);
	bJuryDemoFixedSixChaosRunning = false;
	bJuryDemoFixedSixChaosPrepared = false;
	IdleValidationState = JuryDemoFixedSixChaosResult.bAccepted
		? EABTSM73IdleValidationState::Accepted
		: EABTSM73IdleValidationState::Rejected;
	GenerationSummary.bAccepted = JuryDemoFixedSixChaosResult.bAccepted;
	if (!JuryDemoFixedSixChaosResult.bAccepted)
	{
		GenerationSummary.RejectReason = FString::Printf(
			TEXT("FixedSixChaosHardGateRejected:Quiet=%d/%d:Awake=%d:Final=%.3f/%.3f/%.3f:Peak=%.3f/%.3f/%.3f"),
			JuryDemoFixedSixChaosResult.bReachedQuiet ? 1 : 0,
			JuryDemoFixedSixChaosResult.bEndedQuiet ? 1 : 0,
			JuryDemoFixedSixChaosResult.FinalAwakeBodyCount,
			JuryDemoFixedSixChaosResult.FinalPlanarDriftCM,
			JuryDemoFixedSixChaosResult.FinalSettlementCM,
			JuryDemoFixedSixChaosResult.FinalRotationDegrees,
			JuryDemoFixedSixChaosResult.PeakPlanarDriftCM,
			JuryDemoFixedSixChaosResult.PeakSettlementCM,
			JuryDemoFixedSixChaosResult.PeakRotationDegrees);
	}
	SetActorTickEnabled(false);
	const FString ResultMessage = FString::Printf(
		TEXT("[ABTS][M7][FixedSixProductionChaos][Result]")
		TEXT(" Entry=%s Complexity=E%d Seed=%d Candidate=%u Result=%u")
		TEXT(" Accepted=%d ReachedQuiet=%d EndedQuiet=%d FinalAwake=%d")
		TEXT(" Final=%.3f/%.3f/%.3f Peak=%.3f/%.3f/%.3f")
		TEXT(" FinalLinear=%.3f FinalAngular=%.3f FirstQuiet=%.3f")
		TEXT(" Internal=%.3f Wall=%.3f Visible=%d Bodies=%d Assembly=%llu")
		TEXT(" FinalThresholds=4cm/6cm/2deg PeakStartupThresholds=6cm/6cm/3deg Freeze=0"),
		*JuryDemoFixedSixChaosResult.ManifestEntryId.ToString(),
		static_cast<int32>(JuryDemoFixedSixChaosResult.ComplexityId),
		JuryDemoFixedSixChaosResult.DeterministicSeed,
		JuryDemoFixedSixChaosResult.CandidateHash,
		JuryDemoFixedSixChaosResult.ResultHash,
		JuryDemoFixedSixChaosResult.bAccepted ? 1 : 0,
		JuryDemoFixedSixChaosResult.bReachedQuiet ? 1 : 0,
		JuryDemoFixedSixChaosResult.bEndedQuiet ? 1 : 0,
		JuryDemoFixedSixChaosResult.FinalAwakeBodyCount,
		JuryDemoFixedSixChaosResult.FinalPlanarDriftCM,
		JuryDemoFixedSixChaosResult.FinalSettlementCM,
		JuryDemoFixedSixChaosResult.FinalRotationDegrees,
		JuryDemoFixedSixChaosResult.PeakPlanarDriftCM,
		JuryDemoFixedSixChaosResult.PeakSettlementCM,
		JuryDemoFixedSixChaosResult.PeakRotationDegrees,
		JuryDemoFixedSixChaosResult.FinalLinearSpeedCMPerSec,
		JuryDemoFixedSixChaosResult.FinalAngularSpeedDegreesPerSec,
		JuryDemoFixedSixChaosResult.FirstQuietSeconds,
		JuryDemoFixedSixChaosResult.InternalSeconds,
		JuryDemoFixedSixChaosResult.WallSeconds,
		JuryDemoFixedSixChaosResult.VisibleModuleCount,
		JuryDemoFixedSixChaosResult.PhysicsBodyCount,
		JuryDemoFixedSixChaosResult.PhysicsAssemblyHash);
	if (JuryDemoFixedSixChaosResult.bAccepted)
	{
		for (const TWeakObjectPtr<AABTSM7BuildingModule>& WeakModule :
			JuryDemoFixedSixChaosPhysicsModules)
		{
			if (AABTSM7BuildingModule* Module = WeakModule.Get())
			{
				Module->SetContactDamageGraceSeconds(0.0f);
			}
		}
		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][M7][FixedSixProductionChaos]")
			TEXT(" Event=ContactDamageArmed Entry=%s Bodies=%d")
			TEXT(" GravityIdentityPreserved=1"),
			*JuryDemoFixedSixChaosResult.ManifestEntryId.ToString(),
			JuryDemoFixedSixChaosPhysicsModules.Num());
		UE_LOG(LogABTSRuntime, Display, TEXT("%s"), *ResultMessage);
	}
	else
	{
		UE_LOG(LogABTSRuntime, Error, TEXT("%s"), *ResultMessage);
	}
}

void AABTSM73StableBuildingActor::RejectJuryDemoFixedSixChaosValidation(
	const FString& Reason)
{
	bJuryDemoFixedSixChaosPrepared = false;
	bJuryDemoFixedSixChaosRunning = false;
	bJuryDemoFixedSixChaosDeferredUntilFirstHit = false;
	bJuryDemoFixedSixChaosDeferredActivationInProgress = false;
	bJuryDemoFixedSixChaosDeferredActivated = false;
	RejectRuntimeStructure(Reason.IsEmpty()
		? FString(TEXT("FixedSixChaosRejected"))
		: Reason);
	UE_LOG(LogABTSRuntime, Error,
		TEXT("[ABTS][M7][FixedSixProductionChaos][Rejected]")
		TEXT(" Actor=%s Reason=%s"),
		*GetName(), *Reason);
}

bool AABTSM73StableBuildingActor::CopyJuryDemoFixedSixChaosResult(
	FABTSM73JuryDemoFixedSixChaosResult& OutResult) const
{
	OutResult = JuryDemoFixedSixChaosResult;
	return JuryDemoFixedSixChaosResult.ResultHash != 0
		&& (IdleValidationState == EABTSM73IdleValidationState::Accepted
			|| IdleValidationState == EABTSM73IdleValidationState::Rejected);
}
