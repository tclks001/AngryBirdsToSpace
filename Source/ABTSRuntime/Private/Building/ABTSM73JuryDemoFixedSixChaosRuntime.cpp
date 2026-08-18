// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73StableBuildingActor.h"

#include "ABTSM7PenetrationValidator.h"
#include "ABTSRuntime.h"
#include "Building/ABTSM73JuryDemoFixedSixRegistration.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Building/ABTSM7BuildingModule.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/ABTSM7GameMode.h"
#include "PBDRigidsSolver.h"
#include "Components/StaticMeshComponent.h"
#include "HAL/PlatformTime.h"
#include "Misc/Crc.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Terrain/ABTSM3Planet.h"
#include "World/ABTSCollisionChannels.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#endif

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

	bool IsFrozenAxisAlignedBrick(const FABTSM73BeamD1BrickBinding& Brick)
	{
		const FQuat Rotation = Brick.LocalTransform.GetRotation();
		if (!Rotation.IsNormalized())
		{
			return false;
		}
		const FVector Axes[] =
		{
			Rotation.RotateVector(FVector::XAxisVector),
			Rotation.RotateVector(FVector::YAxisVector),
			Rotation.RotateVector(FVector::ZAxisVector)
		};
		bool bUsedPrincipalAxis[3] = {false, false, false};
		for (const FVector& Axis : Axes)
		{
			const FVector Absolute = Axis.GetAbs();
			int32 PrincipalAxis = 0;
			if (Absolute.Y > Absolute.X && Absolute.Y >= Absolute.Z)
			{
				PrincipalAxis = 1;
			}
			else if (Absolute.Z > Absolute.X && Absolute.Z > Absolute.Y)
			{
				PrincipalAxis = 2;
			}
			if (Absolute[PrincipalAxis] < 0.9999f
				|| bUsedPrincipalAxis[PrincipalAxis])
			{
				return false;
			}
			bUsedPrincipalAxis[PrincipalAxis] = true;
		}
		return true;
	}

	bool VerifyFrozenAxisAlignedSupportConnectivity(
		const TConstArrayView<FABTSM73BeamD1BrickBinding> Bricks,
		FString& OutError)
	{
		OutError.Reset();
		if (Bricks.IsEmpty())
		{
			OutError = TEXT("FixedSixSupportClosureEmptyFrozenGraph");
			return false;
		}
		constexpr double ContactToleranceCM = 1.5;
		constexpr double MinimumOverlapCM = 1.0;
		double MinimumBottomZ = DBL_MAX;
		for (const FABTSM73BeamD1BrickBinding& Brick : Bricks)
		{
			if (!Brick.LocalBounds.IsValid || !IsFrozenAxisAlignedBrick(Brick))
			{
				OutError = FString::Printf(
					TEXT("FixedSixSupportClosureNonAxisAlignedOrInvalidBrick:%d"),
					Brick.BrickId);
				return false;
			}
			MinimumBottomZ = FMath::Min(MinimumBottomZ, Brick.LocalBounds.Min.Z);
		}
		TArray<TArray<int32>> Children;
		Children.SetNum(Bricks.Num());
		TArray<int32> Ground;
		for (int32 UpperId = 0; UpperId < Bricks.Num(); ++UpperId)
		{
			const FBox& Upper = Bricks[UpperId].LocalBounds;
			if (Upper.Min.Z <= MinimumBottomZ + ContactToleranceCM)
			{
				Ground.Add(UpperId);
			}
			for (int32 LowerId = 0; LowerId < Bricks.Num(); ++LowerId)
			{
				if (LowerId == UpperId) continue;
				const FBox& Lower = Bricks[LowerId].LocalBounds;
				const double OverlapX = FMath::Min(Lower.Max.X, Upper.Max.X)
					- FMath::Max(Lower.Min.X, Upper.Min.X);
				const double OverlapY = FMath::Min(Lower.Max.Y, Upper.Max.Y)
					- FMath::Max(Lower.Min.Y, Upper.Min.Y);
				if (FMath::Abs(Lower.Max.Z - Upper.Min.Z) <= ContactToleranceCM
					&& OverlapX > MinimumOverlapCM && OverlapY > MinimumOverlapCM)
				{
					Children[LowerId].Add(UpperId);
				}
			}
		}
		TBitArray<> Reachable(false, Bricks.Num());
		TArray<int32> Queue;
		for (const int32 GroundId : Ground)
		{
			Reachable[GroundId] = true;
			Queue.Add(GroundId);
		}
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			for (const int32 ChildId : Children[Queue[Head]])
			{
				if (!Reachable[ChildId])
				{
					Reachable[ChildId] = true;
					Queue.Add(ChildId);
				}
			}
		}
		if (Reachable.CountSetBits() != Bricks.Num())
		{
			OutError = FString::Printf(
				TEXT("FixedSixSupportClosureFrozenDisconnected:Reachable=%d:Bricks=%d"),
				Reachable.CountSetBits(), Bricks.Num());
			return false;
		}
		return true;
	}

	bool CanPublishExactIndependentSupportClosure(const int32 ExistingBodies,
		const int32 NewBodies, const int32 MaximumBodies)
	{
		return ExistingBodies >= 0 && NewBodies > 0 && MaximumBodies > 0
			&& ExistingBodies <= MaximumBodies
			&& NewBodies <= MaximumBodies - ExistingBodies;
	}

	/**
	 * A damage epoch stores frozen graph identity, never a live actor pointer:
	 * a successful BreakModule can retire the source actor before Tick resolves
	 * the coalesced transaction.  Keep this helper deliberately small and pure
	 * so the same input has one stable epoch key in every process.
	 */
	bool MergeStableDamageEpochBrickIds(TArray<int32>& InOutBrickIds,
		const TConstArrayView<int32> IncomingBrickIds, const int32 BrickCount)
	{
		if (BrickCount <= 0 || IncomingBrickIds.IsEmpty())
		{
			return false;
		}
		for (const int32 BrickId : IncomingBrickIds)
		{
			if (BrickId < 0 || BrickId >= BrickCount)
			{
				return false;
			}
		}
		InOutBrickIds.Append(IncomingBrickIds);
		InOutBrickIds.Sort();
		for (int32 Index = InOutBrickIds.Num() - 1; Index > 0; --Index)
		{
			if (InOutBrickIds[Index] == InOutBrickIds[Index - 1])
			{
				InOutBrickIds.RemoveAt(Index, 1, EAllowShrinking::No);
			}
		}
		return true;
	}

	bool BuildOverflowKinematicPublicationPlan(const int32 ExistingBodies,
		const int32 ClosureBodies, const int32 MaximumBodies,
		const int32 RequestedReserveBodies, int32& OutDynamicBodies,
		int32& OutQueuedBodies)
	{
		OutDynamicBodies = 0;
		OutQueuedBodies = 0;
		const int32 AvailableBodies = FMath::Max(0, MaximumBodies - ExistingBodies);
		if (ClosureBodies <= 0 || AvailableBodies <= 0) return false;
		const int32 Reserve = ClosureBodies > AvailableBodies
			? FMath::Min(RequestedReserveBodies,
				FMath::Max(0, AvailableBodies - 1)) : 0;
		OutDynamicBodies = FMath::Min(ClosureBodies,
			FMath::Max(1, AvailableBodies - Reserve));
		OutQueuedBodies = ClosureBodies - OutDynamicBodies;
		return OutDynamicBodies > 0 && OutDynamicBodies + OutQueuedBodies
			== ClosureBodies && ExistingBodies + OutDynamicBodies <= MaximumBodies;
	}

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

	// M6 has already accepted this fixed-six actor as an exact static-ready
	// participant. Preparing the deferred first-hit representation must not
	// regress that published startup state to Running: a late E1 binding would
	// otherwise reopen the WorldReady gate. Any failure below remains fail-closed
	// through RejectRuntimeStructure/RejectJuryDemoFixedSixChaosValidation.
	if (IdleValidationState != EABTSM73IdleValidationState::Accepted)
	{
		OutError = TEXT("FixedSixChaosPreparationIdleValidationNotAccepted");
		return false;
	}
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

	// The frozen PhysicsAssembly remains an immutable static certificate, but it
	// is not a destruction representation. Welding here would make all children
	// follow one root and destroys per-brick force/damage semantics before the
	// player ever hits the building.
	JuryDemoFixedSixChaosPhysicsModules = RuntimeModules;
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& WeakModule :
		JuryDemoFixedSixChaosPhysicsModules)
	{
		if (const AABTSM7BuildingModule* Module = WeakModule.Get(); Module == nullptr
			|| Module->IsCompoundChild())
		{
			OutError = TEXT("FixedSixExactDestructionBodyIdentityInvalid");
			RejectRuntimeStructure(OutError);
			return false;
		}
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

bool AABTSM73StableBuildingActor::BuildJuryDemoFixedSixSupportClosure(
	const TConstArrayView<int32> SeedBrickIds,
	TArray<AABTSM7BuildingModule*>& OutPhysicsModules,
	TArray<int32>* OutAffectedBrickIds,
	FString& OutError) const
{
	OutPhysicsModules.Reset();
	if (OutAffectedBrickIds != nullptr) OutAffectedBrickIds->Reset();
	OutError.Reset();
	if (!JuryDemoFixedSixStaticEntry.IsSet())
	{
		OutError = TEXT("FixedSixSupportClosureEntryMissing");
		return false;
	}
	const FABTSM73JuryDemoFixedSixStaticEntry& Entry =
		JuryDemoFixedSixStaticEntry.GetValue();
	const int32 BrickCount = Entry.Bricks.Num();
	if (BrickCount <= 0 || RuntimeModules.Num() < BrickCount || SeedBrickIds.IsEmpty())
	{
		OutError = TEXT("FixedSixSupportClosureSeedSetInvalid");
		return false;
	}
	FString FrozenConnectivityError;
	if (!VerifyFrozenAxisAlignedSupportConnectivity(
		Entry.Bricks, FrozenConnectivityError))
	{
		OutError = FrozenConnectivityError;
		return false;
	}

	TBitArray<> Removed(false, BrickCount);
	for (const int32 BrickId : SeedBrickIds)
	{
		if (!Entry.Bricks.IsValidIndex(BrickId))
		{
			OutError = FString::Printf(TEXT("FixedSixSupportClosureSeedInvalid:%d"), BrickId);
			return false;
		}
		Removed[BrickId] = true;
	}

	constexpr double ContactToleranceCM = 1.5;
	constexpr double MinimumOverlapCM = 1.0;
	double MinimumBottomZ = DBL_MAX;
	for (const FABTSM73BeamD1BrickBinding& Brick : Entry.Bricks)
	{
		if (!Brick.LocalBounds.IsValid)
		{
			OutError = TEXT("FixedSixSupportClosureBoundsInvalid");
			return false;
		}
		MinimumBottomZ = FMath::Min(MinimumBottomZ, Brick.LocalBounds.Min.Z);
	}
	TArray<TArray<int32>> SupportChildren;
	SupportChildren.SetNum(BrickCount);
	TArray<int32> GroundBrickIds;
	int32 SupportEdgeCount = 0;
	for (int32 UpperId = 0; UpperId < BrickCount; ++UpperId)
	{
		const FBox& Upper = Entry.Bricks[UpperId].LocalBounds;
		if (Upper.Min.Z <= MinimumBottomZ + ContactToleranceCM)
		{
			GroundBrickIds.Add(UpperId);
		}
		for (int32 LowerId = 0; LowerId < BrickCount; ++LowerId)
		{
			if (LowerId == UpperId) continue;
			const FBox& Lower = Entry.Bricks[LowerId].LocalBounds;
			if (FMath::Abs(Lower.Max.Z - Upper.Min.Z) > ContactToleranceCM) continue;
			const double OverlapX = FMath::Min(Lower.Max.X, Upper.Max.X)
				- FMath::Max(Lower.Min.X, Upper.Min.X);
			const double OverlapY = FMath::Min(Lower.Max.Y, Upper.Max.Y)
				- FMath::Max(Lower.Min.Y, Upper.Min.Y);
			if (OverlapX > MinimumOverlapCM && OverlapY > MinimumOverlapCM)
			{
				SupportChildren[LowerId].Add(UpperId);
				++SupportEdgeCount;
			}
		}
	}
	const auto GatherGroundReachable = [&SupportChildren, &GroundBrickIds, BrickCount](
		const TBitArray<>& RemovedBricks, TBitArray<>& OutReachable)
	{
		OutReachable.Init(false, BrickCount);
		TArray<int32> Queue;
		for (const int32 GroundId : GroundBrickIds)
		{
			if (!RemovedBricks[GroundId]) { OutReachable[GroundId] = true; Queue.Add(GroundId); }
		}
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			for (const int32 ChildId : SupportChildren[Queue[Head]])
			{
				if (!RemovedBricks[ChildId] && !OutReachable[ChildId])
				{
					OutReachable[ChildId] = true;
					Queue.Add(ChildId);
				}
			}
		}
	};
	TBitArray<> BaselineRemoved(false, BrickCount);
	TBitArray<> BaselineReachable;
	GatherGroundReachable(BaselineRemoved, BaselineReachable);
	if (GroundBrickIds.IsEmpty() || SupportEdgeCount <= 0
		|| BaselineReachable.CountSetBits() != BrickCount)
	{
		OutError = FString::Printf(TEXT("FixedSixSupportClosureBaselineInvalid:Ground=%d:Edges=%d:Reachable=%d:Bricks=%d"),
			GroundBrickIds.Num(), SupportEdgeCount, BaselineReachable.CountSetBits(), BrickCount);
		return false;
	}
	for (int32 BrickId = 0; BrickId < BrickCount; ++BrickId)
	{
		const AABTSM7BuildingModule* Module = RuntimeModules[BrickId].Get();
		Removed[BrickId] |= Module == nullptr || Module->IsBroken()
			|| Module->IsRecycled() || Module->IsDynamic()
			|| Module->IsOverflowKinematic()
			|| JuryDemoFixedSixRemovedSupportBrickIds.Contains(BrickId);
	}
	TBitArray<> Reachable;
	GatherGroundReachable(Removed, Reachable);
	for (int32 BrickId = 0; BrickId < BrickCount; ++BrickId)
	{
		const bool bAffected = Removed[BrickId] || !Reachable[BrickId];
		if (bAffected && OutAffectedBrickIds != nullptr) OutAffectedBrickIds->Add(BrickId);
		if (bAffected)
		{
			if (AABTSM7BuildingModule* Module = RuntimeModules[BrickId].Get();
				Module != nullptr && !Module->IsBroken() && !Module->IsRecycled()
				&& !Module->IsDynamic() && !Module->IsOverflowKinematic())
			{
				OutPhysicsModules.Add(Module);
			}
		}
	}
	UE_LOG(LogABTSRuntime, Display,
	TEXT("[ABTS][M7][FixedSixSupportClosure][Derived] Scope=SupportClosure.Derive Entry=%s Seeds=%d Ground=%d Edges=%d BaselineReachable=%d Affected=%d Active=%d StaticFloating=0 ExactIndependentBricks=1"),
		*Entry.ManifestEntryId.ToString(), SeedBrickIds.Num(), GroundBrickIds.Num(),
		SupportEdgeCount, BaselineReachable.CountSetBits(),
		OutAffectedBrickIds != nullptr ? OutAffectedBrickIds->Num() : OutPhysicsModules.Num(),
		OutPhysicsModules.Num());
	return true;
}

bool AABTSM73StableBuildingActor::
ActivatePreparedJuryDemoFixedSixChaosValidation(
	FString& OutError,
	const FVector* GameplayImpactWorld,
	const TArray<AABTSM7BuildingModule*>* GameplayPhysicsModules)
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
	const FABTSM73JuryDemoFixedSixStaticEntry& Entry =
		JuryDemoFixedSixStaticEntry.GetValue();
	TArray<AABTSM7BuildingModule*> PhysicsModules;
	PhysicsModules.Reserve(JuryDemoFixedSixChaosPhysicsModules.Num());
	int32 CertifiedPhysicsBodyCount = 0;
	if (GameplayPhysicsModules != nullptr)
	{
		// A destructive epoch is intentionally allowed to run after a previous
		// seed has broken and destroyed one of the original prepared Actors.
		// Certify only this publication subset against the immutable descriptor
		// and the stable RuntimeModules ledger, never against the historical weak
		// snapshot of every body prepared at startup.
		TSet<AABTSM7BuildingModule*> UniqueGameplayModules;
		for (AABTSM7BuildingModule* Module : *GameplayPhysicsModules)
		{
			const int32 BrickId = Module != nullptr
				? Module->GetDamageLifecycleBrickId() : INDEX_NONE;
			if (!IsValid(Module) || Module->IsDynamic() || Module->IsBroken()
				|| Module->IsRecycled() || !Entry.Bricks.IsValidIndex(BrickId)
				|| !RuntimeModules.IsValidIndex(BrickId)
				|| RuntimeModules[BrickId].Get() != Module
				|| UniqueGameplayModules.Contains(Module))
			{
				OutError = TEXT("FixedSixGameplaySupportClosureIdentityInvalid");
				return false;
			}
			UniqueGameplayModules.Add(Module);
			PhysicsModules.Add(Module);
		}
		CertifiedPhysicsBodyCount = Entry.Bricks.Num();
		if (PhysicsModules.IsEmpty()
			|| PhysicsModules.Num() > FixedSixGameplayMaximumActiveBodies)
		{
			OutError = FString::Printf(
				TEXT("FixedSixGameplaySupportClosureBudgetRejected:%d"),
				PhysicsModules.Num());
			return false;
		}
	}
	else if (GameplayImpactWorld != nullptr)
	{
		OutError = TEXT("FixedSixGameplaySupportClosureRequired");
		return false;
	}
	else
	{
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
		CertifiedPhysicsBodyCount = PhysicsModules.Num();
	}
	JuryDemoFixedSixActivePhysicsBodyCount = PhysicsModules.Num();
	int32 E1CrystalTargetCount = 0;
	AABTSM7BuildingModule* E1CrystalModule = nullptr;
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
			if (Module->IsCrystalLifecycleTarget())
			{
				++E1CrystalTargetCount;
				E1CrystalModule = Module;
			}
		}
		if (E1CrystalTargetCount != 1 || E1CrystalModule == nullptr)
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
	const FABTSM7ChaosBodyProfile DestructionBodyProfile =
		FABTSM7ChaosBodyProfile::DestructionCandidate();
	if (!RuntimeMaterialSystem->BeginSiteUniformLaunchPhysics(
		PhysicsModules,
		Entry.WorldTransform.GetLocation(),
		Entry.SupportCenterWorldCM,
		FixedSixGravityCMPerSec2,
		FixedSixMaximumObservationSeconds + 1.0f,
		/*bPenetrationPrevalidated=*/true,
		GameplayImpactWorld != nullptr ? &DestructionBodyProfile : nullptr))
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
		if (!RuntimeMaterialSystem->OnMaterialRecovered.IsBound())
		{
			OutError = TEXT("E1CrystalImmediateRecoveryRouteMissing");
			return false;
		}
		NotifyJuryDemoE1ModuleDamage(
			*E1CrystalModule,
			EABTSM73E1DamageCause::ModuleContact,
			true,
			0.0f);
		const FString CrystalName = E1CrystalModule->GetName();
		const FVector CrystalLocation = E1CrystalModule->GetActorLocation();
		if (!E1CrystalModule->BreakModule())
		{
			OutError = TEXT("E1CrystalImmediateBreakRejected");
			return false;
		}
		RuntimeMaterialSystem->OnMaterialRecovered.Broadcast(
			EABTSM7BuildingMaterial::Crystal,
			1);
		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][M7][E1CrystalChaosReward]")
			TEXT(" Consumed=1 Module=%s CrystalCoreQuantity=1")
			TEXT(" Trigger=E1ChaosActivated Location=%s"),
			*CrystalName,
			*CrystalLocation.ToCompactString());
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
		|| !JuryDemoFixedSixStaticEntry.IsSet()
		|| IdleValidationState != EABTSM73IdleValidationState::Accepted)
	{
		OutError = TEXT("FixedSixChaosDeferredStateInvalid");
		return false;
	}
	if (bJuryDemoFixedSixChaosDeferredUntilFirstHit)
	{
		// A harmless batch retry must retain the already-published static-ready
		// authority instead of turning it into a fresh pending/rejected gate.
		return true;
	}
	bJuryDemoFixedSixChaosDeferredUntilFirstHit = true;
	bJuryDemoFixedSixChaosDeferredActivated = false;
	// This only arms real first-hit promotion; it must not start Chaos early.
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
	const AABTSM7BuildingModule& TriggerModule, FString& OutError,
	const float ImpactRadiusCM)
{
	return ActivateJuryDemoFixedSixImpactSupportClosure(
		TriggerModule, ImpactRadiusCM, OutError);
}

bool AABTSM73StableBuildingActor::
ActivateJuryDemoFixedSixImpactSupportClosure(
	const AABTSM7BuildingModule& TriggerModule,
	const float ImpactRadiusCM,
	FString& OutError)
{
	TArray<AABTSM7BuildingModule*> SingleSeed =
		{const_cast<AABTSM7BuildingModule*>(&TriggerModule)};
	TArray<int32> SeedBrickIds;
	if (!ResolveJuryDemoFixedSixImpactSeedBrickIds(
		SingleSeed, ImpactRadiusCM, SeedBrickIds, OutError))
	{
		return false;
	}
	return ActivateJuryDemoFixedSixImpactSupportClosureTransaction(
		SeedBrickIds, ++JuryDemoFixedSixDamageEpoch, OutError);
}

bool AABTSM73StableBuildingActor::QueueJuryDemoFixedSixDamageSeed(
	AABTSM7BuildingModule& TriggerModule, const float ImpactRadiusCM,
	const FVector& ImpactVelocityCMPerSec,
	FString& OutError)
{
	OutError.Reset();
	if (!bJuryDemoFixedSixChaosPrepared
		|| TriggerModule.GetDamageLifecycleOwner() != this
		|| TriggerModule.IsBroken() || TriggerModule.IsRecycled())
	{
		OutError = TEXT("FixedSixDamageEpochSeedIdentityRejected");
		return false;
	}
	if (TriggerModule.IsDynamic() || TriggerModule.IsOverflowKinematic())
	{
		// The direct material path still applies its accumulated damage/impulse
		// to this already-independent brick.  It must not recursively derive a
		// frozen graph merely because a dense contact island reported it again.
		return true;
	}
	const int32 TriggerBrickId = TriggerModule.GetDamageLifecycleBrickId();
	if (!JuryDemoFixedSixStaticEntry.IsSet()
		|| !JuryDemoFixedSixStaticEntry->Bricks.IsValidIndex(TriggerBrickId)
		|| !RuntimeModules.IsValidIndex(TriggerBrickId)
		|| RuntimeModules[TriggerBrickId].Get() != &TriggerModule)
	{
		OutError = TEXT("FixedSixDamageEpochSeedLedgerMismatch");
		return false;
	}

	// A direct brick hit already has an immutable BrickId.  Never scan every
	// module merely to rediscover it: black-bird processing calls this once per
	// touched brick after it has queued the building-wide radial set, and an
	// O(N) rediscovery here was the remaining source of the multi-second hitch.
	// A positive-radius device seed still resolves once when it is the first
	// event in the epoch; subsequent same-frame seeds simply merge by BrickId.
	if (ImpactRadiusCM <= 0.0f || !JuryDemoFixedSixQueuedDamageSeedBrickIds.IsEmpty())
	{
		const int32 SeedBrickId = TriggerBrickId;
		return QueueJuryDemoFixedSixDamageBrickIds(
			MakeArrayView(&SeedBrickId, 1), TriggerBrickId,
			ImpactVelocityCMPerSec, OutError);
	}

	TArray<AABTSM7BuildingModule*> TriggerModules = {&TriggerModule};
	TArray<int32> SeedBrickIds;
	if (!ResolveJuryDemoFixedSixImpactSeedBrickIds(
		TriggerModules, ImpactRadiusCM, SeedBrickIds, OutError))
	{
		return false;
	}
	return QueueJuryDemoFixedSixDamageBrickIds(SeedBrickIds, TriggerBrickId,
		ImpactVelocityCMPerSec, OutError);
}

bool AABTSM73StableBuildingActor::QueueJuryDemoFixedSixRadialDamage(
	const FVector& OriginWorldCM, const float ImpactRadiusCM, FString& OutError)
{
	OutError.Reset();
	if (!bJuryDemoFixedSixChaosPrepared || !JuryDemoFixedSixStaticEntry.IsSet()
		|| !FMath::IsFinite(OriginWorldCM.X) || !FMath::IsFinite(OriginWorldCM.Y)
		|| !FMath::IsFinite(OriginWorldCM.Z))
	{
		OutError = TEXT("FixedSixDamageEpochRadialIdentityRejected");
		return false;
	}
	const float RadiusCM = FMath::Max(0.0f, ImpactRadiusCM);
	TArray<int32> SeedBrickIds;
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& WeakModule : RuntimeModules)
	{
		const AABTSM7BuildingModule* Module = WeakModule.Get();
		const int32 BrickId = Module != nullptr
			? Module->GetDamageLifecycleBrickId() : INDEX_NONE;
		if (Module != nullptr && !Module->IsBroken() && !Module->IsRecycled()
			&& JuryDemoFixedSixStaticEntry->Bricks.IsValidIndex(BrickId)
			&& FVector::DistSquared(Module->GetActorLocation(), OriginWorldCM)
			<= FMath::Square(RadiusCM))
		{
			SeedBrickIds.Add(BrickId);
		}
	}
	if (SeedBrickIds.IsEmpty())
	{
		OutError = TEXT("FixedSixDamageEpochRadialNoLiveBrick");
		return false;
	}
	return QueueJuryDemoFixedSixDamageBrickIds(SeedBrickIds, INDEX_NONE,
		FVector::ZeroVector, OutError);
}

bool AABTSM73StableBuildingActor::QueueJuryDemoFixedSixTopologyMutation(
	AABTSM7BuildingModule& MutatedModule, FString& OutError)
{
	OutError.Reset();
	if (!bJuryDemoFixedSixChaosPrepared
		|| MutatedModule.GetDamageLifecycleOwner() != this
		|| MutatedModule.IsRecycled())
	{
		OutError = TEXT("FixedSixDamageEpochTopologyIdentityRejected");
		return false;
	}
	const int32 MutatedBrickId = MutatedModule.GetDamageLifecycleBrickId();
	if (!JuryDemoFixedSixStaticEntry.IsSet()
		|| !JuryDemoFixedSixStaticEntry->Bricks.IsValidIndex(MutatedBrickId)
		|| !RuntimeModules.IsValidIndex(MutatedBrickId)
		|| RuntimeModules[MutatedBrickId].Get() != &MutatedModule)
	{
		OutError = TEXT("FixedSixDamageEpochTopologyLedgerMismatch");
		return false;
	}
	// Capture the stable ID before BreakModule mutates the actor.  Multiple
	// breaks in this frame merge into the same deterministic transaction.
	return QueueJuryDemoFixedSixDamageBrickIds(
		MakeArrayView(&MutatedBrickId, 1), INDEX_NONE,
		FVector::ZeroVector, OutError);
}

bool AABTSM73StableBuildingActor::
IsJuryDemoFixedSixRegisteredRuntimeModule(
	const AABTSM7BuildingModule& Module,
	const AABTSM7BuildingMaterialSystem& ExpectedMaterialSystem) const
{
	if (!bJuryDemoFixedSixChaosPrepared
		|| Module.GetDamageLifecycleOwner() != this
		|| RuntimeMaterialSystem.Get() != &ExpectedMaterialSystem
		|| Module.IsBroken() || Module.IsRecycled())
	{
		return false;
	}
	const int32 BrickId = Module.GetDamageLifecycleBrickId();
	return JuryDemoFixedSixStaticEntry.IsSet()
		&& JuryDemoFixedSixStaticEntry->Bricks.IsValidIndex(BrickId)
		&& RuntimeModules.IsValidIndex(BrickId)
		&& RuntimeModules[BrickId].Get() == &Module;
}

bool AABTSM73StableBuildingActor::ResolveJuryDemoFixedSixImpactSeedBrickIds(
	const TConstArrayView<AABTSM7BuildingModule*> TriggerModules,
	const float ImpactRadiusCM, TArray<int32>& OutSeedBrickIds,
	FString& OutError) const
{
	OutSeedBrickIds.Reset();
	OutError.Reset();
	if (!JuryDemoFixedSixStaticEntry.IsSet() || TriggerModules.IsEmpty())
	{
		OutError = TEXT("FixedSixDamageEpochSeedIdentityRejected");
		return false;
	}
	const FABTSM73JuryDemoFixedSixStaticEntry& Entry =
		JuryDemoFixedSixStaticEntry.GetValue();
	const float RadiusCM = FMath::Max(0.0f, ImpactRadiusCM);
	for (const AABTSM7BuildingModule* TriggerModule : TriggerModules)
	{
		if (TriggerModule == nullptr
			|| TriggerModule->GetDamageLifecycleOwner() != this)
		{
			OutError = TEXT("FixedSixDamageEpochSeedIdentityRejected");
			return false;
		}
		const FVector TriggerLocation = TriggerModule->GetActorLocation();
		int32 NearestBrickId = INDEX_NONE;
		float NearestDistanceSquared = TNumericLimits<float>::Max();
		for (const TWeakObjectPtr<AABTSM7BuildingModule>& WeakModule : RuntimeModules)
		{
			const AABTSM7BuildingModule* Candidate = WeakModule.Get();
			const int32 BrickId = Candidate != nullptr
				? Candidate->GetDamageLifecycleBrickId() : INDEX_NONE;
			if (Candidate == nullptr || Candidate->IsBroken() || Candidate->IsRecycled()
				|| !Entry.Bricks.IsValidIndex(BrickId))
			{
				continue;
			}
			const float DistanceSquared = FVector::DistSquared(
				Candidate->GetActorLocation(), TriggerLocation);
			if (DistanceSquared < NearestDistanceSquared
				|| (FMath::IsNearlyEqual(DistanceSquared, NearestDistanceSquared)
					&& (NearestBrickId == INDEX_NONE || BrickId < NearestBrickId)))
			{
				NearestBrickId = BrickId;
				NearestDistanceSquared = DistanceSquared;
			}
			if (BrickId == TriggerModule->GetDamageLifecycleBrickId()
				|| (RadiusCM > 0.0f && DistanceSquared <= FMath::Square(RadiusCM)))
			{
				OutSeedBrickIds.Add(BrickId);
			}
		}
		if (OutSeedBrickIds.IsEmpty() && NearestBrickId != INDEX_NONE)
		{
			OutSeedBrickIds.Add(NearestBrickId);
		}
	}
	OutSeedBrickIds.Sort();
	for (int32 Index = OutSeedBrickIds.Num() - 1; Index > 0; --Index)
	{
		if (OutSeedBrickIds[Index] == OutSeedBrickIds[Index - 1])
		{
			OutSeedBrickIds.RemoveAt(Index, 1, EAllowShrinking::No);
		}
	}
	if (OutSeedBrickIds.IsEmpty())
	{
		OutError = TEXT("FixedSixDamageEpochSeedNoLiveBrick");
		return false;
	}
	return true;
}

bool AABTSM73StableBuildingActor::QueueJuryDemoFixedSixDamageBrickIds(
	const TConstArrayView<int32> SeedBrickIds,
	const int32 ImpulseBrickId,
	const FVector& ImpulseVelocityCMPerSec,
	FString& OutError)
{
	OutError.Reset();
	if (!JuryDemoFixedSixStaticEntry.IsSet() || SeedBrickIds.IsEmpty())
	{
		OutError = TEXT("FixedSixDamageEpochSeedSetInvalid");
		return false;
	}
	if (!MergeStableDamageEpochBrickIds(JuryDemoFixedSixQueuedDamageSeedBrickIds,
		SeedBrickIds, JuryDemoFixedSixStaticEntry->Bricks.Num()))
	{
		OutError = TEXT("FixedSixDamageEpochSeedInvalid");
		return false;
	}
	if (ImpulseBrickId != INDEX_NONE && !ImpulseVelocityCMPerSec.IsNearlyZero())
	{
		if (!JuryDemoFixedSixStaticEntry->Bricks.IsValidIndex(ImpulseBrickId))
		{
			OutError = TEXT("FixedSixDamageEpochImpulseIdentityInvalid");
			return false;
		}
		JuryDemoFixedSixQueuedImpulseVelocityByBrickId.FindOrAdd(ImpulseBrickId)
			+= ImpulseVelocityCMPerSec;
	}
	SetActorTickEnabled(true);
	return true;
}

void AABTSM73StableBuildingActor::ResolveQueuedJuryDemoFixedSixDamageTransaction()
{
	if (JuryDemoFixedSixQueuedDamageSeedBrickIds.IsEmpty()) return;
	TArray<int32> SeedBrickIds = MoveTemp(JuryDemoFixedSixQueuedDamageSeedBrickIds);
	SeedBrickIds.Sort();
	for (int32 Index = SeedBrickIds.Num() - 1; Index > 0; --Index)
	{
		if (SeedBrickIds[Index] == SeedBrickIds[Index - 1])
		{
			SeedBrickIds.RemoveAt(Index, 1, EAllowShrinking::No);
		}
	}
	FString TransactionError;
	const uint64 Epoch = ++JuryDemoFixedSixDamageEpoch;
	const double BeginSeconds = FPlatformTime::Seconds();
	if (!ActivateJuryDemoFixedSixImpactSupportClosureTransaction(
		SeedBrickIds, Epoch, TransactionError))
	{
		JuryDemoFixedSixQueuedImpulseVelocityByBrickId.Reset();
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M7][DamageEpoch][Rejected] Entry=%s Epoch=%llu Seeds=%d Reason=%s"),
			JuryDemoFixedSixStaticEntry.IsSet()
				? *JuryDemoFixedSixStaticEntry->ManifestEntryId.ToString() : TEXT("None"),
			Epoch, SeedBrickIds.Num(), *TransactionError);
		return;
	}
	JuryDemoFixedSixQueuedImpulseVelocityByBrickId.Reset();
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M7][DamageEpoch][Resolved] Entry=%s Epoch=%llu Seeds=%d TotalMS=%.3f ClosureOnce=1"),
		*JuryDemoFixedSixStaticEntry->ManifestEntryId.ToString(), Epoch,
		SeedBrickIds.Num(), (FPlatformTime::Seconds() - BeginSeconds) * 1000.0);
}

bool AABTSM73StableBuildingActor::
ActivateJuryDemoFixedSixImpactSupportClosureTransaction(
	const TConstArrayView<int32> TriggerBrickIds,
	const uint64 DamageEpoch,
	FString& OutError)
{
	OutError.Reset();
	if (!bJuryDemoFixedSixChaosPrepared
		|| bJuryDemoFixedSixChaosDeferredActivationInProgress
		|| TriggerBrickIds.IsEmpty())
	{
		OutError = TEXT("FixedSixDeferredFirstHitIdentityRejected");
		return false;
	}
	const FABTSM73JuryDemoFixedSixStaticEntry& Entry =
		JuryDemoFixedSixStaticEntry.GetValue();
	for (const int32 BrickId : TriggerBrickIds)
	{
		if (!Entry.Bricks.IsValidIndex(BrickId))
		{
			OutError = TEXT("FixedSixDamageEpochSeedIdentityRejected");
			return false;
		}
	}
	const double DeriveBeginSeconds = FPlatformTime::Seconds();
	TArray<AABTSM7BuildingModule*> SupportClosureModules;
	TArray<int32> AffectedBrickIds;
	if (!BuildJuryDemoFixedSixSupportClosure(TriggerBrickIds, SupportClosureModules,
		&AffectedBrickIds, OutError))
	{
		return false;
	}
	const double DeriveMilliseconds = (FPlatformTime::Seconds()
		- DeriveBeginSeconds) * 1000.0;
	const bool bWasDeferredActivated = bJuryDemoFixedSixChaosDeferredActivated;
	if (SupportClosureModules.IsEmpty())
	{
		for (const int32 BrickId : AffectedBrickIds)
		{
			JuryDemoFixedSixRemovedSupportBrickIds.Add(BrickId);
			if (RuntimeModules.IsValidIndex(BrickId))
			{
				if (AABTSM7BuildingModule* Affected = RuntimeModules[BrickId].Get();
					Affected != nullptr && Affected->IsDynamic())
				{
					Affected->ReactivatePreservingSiteUniformGravity(FVector::ZeroVector);
				}
			}
		}
		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][M7][DamageEpoch][Summary]")
			TEXT(" Entry=%s Epoch=%llu Derivations=1 ClosureMS=%.3f Unsupported=0 Published=0 UnpublishedUnsupported=0 QueueDrop=0 ActivateMS=0.000 ImpulseBricks=0 Reason=NoLiveStaticBody"),
			*JuryDemoFixedSixStaticEntry->ManifestEntryId.ToString(),
			DamageEpoch, DeriveMilliseconds);
		return true;
	}
	int32 ActualActiveBodyCount = 0;
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& WeakModule : RuntimeModules)
	{
		if (const AABTSM7BuildingModule* Module = WeakModule.Get();
			Module != nullptr && Module->IsDynamic())
		{
			++ActualActiveBodyCount;
		}
	}
	if (JuryDemoFixedSixActivePhysicsBodyCount != ActualActiveBodyCount)
	{
		UE_LOG(LogABTSRuntime, Warning,
			TEXT("[ABTS][M7][DamageEpoch][LedgerRebased] Entry=%s Epoch=%llu Ledger=%d Actual=%d"),
			*JuryDemoFixedSixStaticEntry->ManifestEntryId.ToString(), DamageEpoch,
			JuryDemoFixedSixActivePhysicsBodyCount, ActualActiveBodyCount);
	}
	JuryDemoFixedSixActivePhysicsBodyCount = ActualActiveBodyCount;

	TSet<int32> SeedBrickIds;
	for (const int32 BrickId : TriggerBrickIds)
	{
		SeedBrickIds.Add(BrickId);
	}
	SupportClosureModules.Sort([&SeedBrickIds](
		const AABTSM7BuildingModule& Left,
		const AABTSM7BuildingModule& Right)
	{
		const bool bLeftIsSeed = SeedBrickIds.Contains(
			Left.GetDamageLifecycleBrickId());
		const bool bRightIsSeed = SeedBrickIds.Contains(
			Right.GetDamageLifecycleBrickId());
		if (bLeftIsSeed != bRightIsSeed) return bLeftIsSeed;
		return Left.GetDamageLifecycleBrickId() < Right.GetDamageLifecycleBrickId();
	});
	int32 ImmediateBodyCount = 0;
	int32 OverflowBodyCount = 0;
	if (!BuildOverflowKinematicPublicationPlan(ActualActiveBodyCount,
		SupportClosureModules.Num(), FixedSixGameplayMaximumActiveBodies,
		JuryDemoFixedSixOverflowEmergencyReserveBodies, ImmediateBodyCount,
		OverflowBodyCount))
	{
		// Body capacity changes only the exact-Chaos subset. Every affected
		// brick still publishes into the visible independent analytic fallback.
		ImmediateBodyCount = 0;
		OverflowBodyCount = SupportClosureModules.Num();
	}
	TArray<AABTSM7BuildingModule*> PhysicsModules;
	TArray<AABTSM7BuildingModule*> OverflowModules;
	PhysicsModules.Append(SupportClosureModules.GetData(), ImmediateBodyCount);
	for (int32 ModuleIndex = ImmediateBodyCount;
		ModuleIndex < SupportClosureModules.Num(); ++ModuleIndex)
	{
		OverflowModules.Add(SupportClosureModules[ModuleIndex]);
	}
	if (OverflowModules.Num() != OverflowBodyCount)
	{
		OutError = TEXT("FixedSixDamageEpochPublicationPlanDrift");
		return false;
	}
	const double PublishBeginSeconds = FPlatformTime::Seconds();
	bJuryDemoFixedSixChaosDeferredActivationInProgress = true;
	if (!bJuryDemoFixedSixChaosDeferredActivated)
	{
		if (!bJuryDemoFixedSixChaosDeferredUntilFirstHit)
		{
			bJuryDemoFixedSixChaosDeferredActivationInProgress = false;
			OutError = TEXT("FixedSixDeferredFirstHitStateInvalid");
			return false;
		}
		if (UWorld* World = GetWorld())
		{
			AABTSM7GameMode* GameMode = Cast<AABTSM7GameMode>(World->GetAuthGameMode());
			if (GameMode == nullptr || !GameMode->
				RestoreJuryDemoFixedSixTerrainCollisionForDeferredFirstHit(OutError))
			{
				bJuryDemoFixedSixChaosDeferredActivationInProgress = false;
				if (OutError.IsEmpty()) OutError = TEXT("FixedSixDeferredTerrainCollisionAuthorityMissing");
				return false;
			}
		}
		FVector GameplayImpactWorld = GetActorLocation();
		if (RuntimeModules.IsValidIndex(TriggerBrickIds[0]))
		{
			if (const AABTSM7BuildingModule* TriggerModule =
				RuntimeModules[TriggerBrickIds[0]].Get())
			{
				GameplayImpactWorld = TriggerModule->GetActorLocation();
			}
		}
		if (!ActivatePreparedJuryDemoFixedSixChaosValidation(
			OutError, &GameplayImpactWorld, &PhysicsModules))
		{
			bJuryDemoFixedSixChaosDeferredActivationInProgress = false;
			return false;
		}
		bJuryDemoFixedSixChaosRunning = false;
		bJuryDemoFixedSixChaosDeferredUntilFirstHit = false;
		bJuryDemoFixedSixChaosDeferredActivated = true;
	}
	else
	{
		const FABTSM7ChaosBodyProfile DestructionProfile =
			FABTSM7ChaosBodyProfile::DestructionCandidate();
		if (!RuntimeMaterialSystem->BeginSiteUniformLaunchPhysics(
			PhysicsModules,
			JuryDemoFixedSixStaticEntry->WorldTransform.GetLocation(),
			JuryDemoFixedSixStaticEntry->SupportCenterWorldCM,
			FixedSixGravityCMPerSec2,
			FixedSixMaximumObservationSeconds + 1.0f,
			/*bPenetrationPrevalidated=*/true,
			&DestructionProfile))
		{
			bJuryDemoFixedSixChaosDeferredActivationInProgress = false;
			OutError = TEXT("FixedSixSupportClosureAdditionalActivationRejected");
			return false;
		}
	}
	if (bWasDeferredActivated)
	{
		JuryDemoFixedSixActivePhysicsBodyCount += PhysicsModules.Num();
	}
	for (AABTSM7BuildingModule* Module : OverflowModules)
	{
		if (Module == nullptr) continue;
		// Ordinary support loss has no authored radial impulse.  Only the
		// later explicit bird/barrel path contributes velocity or damage.
		Module->BeginOverflowKinematic(FVector::ZeroVector,
			FVector::ZeroVector, JuryDemoFixedSixChaosSiteUp,
			FixedSixGravityCMPerSec2);
		JuryDemoFixedSixOverflowKinematicModules.Add(Module);
	}
	JuryDemoFixedSixOverflowKinematicModules.Sort([](
		const TWeakObjectPtr<AABTSM7BuildingModule>& Left,
		const TWeakObjectPtr<AABTSM7BuildingModule>& Right)
	{
		const AABTSM7BuildingModule* LeftModule = Left.Get();
		const AABTSM7BuildingModule* RightModule = Right.Get();
		return LeftModule != nullptr && RightModule != nullptr
			? LeftModule->GetDamageLifecycleBrickId()
				< RightModule->GetDamageLifecycleBrickId()
			: LeftModule != nullptr;
	});
	TArray<int32> ImpulseBrickIds;
	JuryDemoFixedSixQueuedImpulseVelocityByBrickId.GetKeys(ImpulseBrickIds);
	ImpulseBrickIds.Sort();
	int32 AppliedImpulseBrickCount = 0;
	for (const int32 BrickId : ImpulseBrickIds)
	{
		const FVector* Impulse =
			JuryDemoFixedSixQueuedImpulseVelocityByBrickId.Find(BrickId);
		if (Impulse == nullptr || Impulse->IsNearlyZero()
			|| !RuntimeModules.IsValidIndex(BrickId))
		{
			continue;
		}
		AABTSM7BuildingModule* Module = RuntimeModules[BrickId].Get();
		if (Module == nullptr || Module->IsBroken() || Module->IsRecycled())
		{
			// Inner blast bricks are deliberately destroyed before publication.
			continue;
		}
		if (Module->IsDynamic())
		{
			Module->ApplyDynamicImpactImpulse(*Impulse);
			++AppliedImpulseBrickCount;
		}
		else if (Module->IsOverflowKinematic())
		{
			Module->AddOverflowKinematicImpact(*Impulse,
				Impulse->GetSafeNormal() * 60.0f, /*DamageGain=*/0.0f);
			++AppliedImpulseBrickCount;
		}
		else
		{
			OutError = TEXT("FixedSixDamageEpochImpulsePublicationMissing");
			bJuryDemoFixedSixChaosDeferredActivationInProgress = false;
			return false;
		}
	}
	for (const int32 BrickId : AffectedBrickIds)
	{
		JuryDemoFixedSixRemovedSupportBrickIds.Add(BrickId);
		// A freshly derived topology invalidates any prior low-velocity sleep.
		// Already-independent bricks are not republished, but must re-enter
		// visible motion when their frozen support path is removed this epoch.
		if (RuntimeModules.IsValidIndex(BrickId))
		{
			if (AABTSM7BuildingModule* Affected = RuntimeModules[BrickId].Get();
				Affected != nullptr && Affected->IsDynamic())
			{
				Affected->ReactivatePreservingSiteUniformGravity(FVector::ZeroVector);
			}
		}
	}
	int32 PostActivationActualActiveBodyCount = 0;
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& WeakModule : RuntimeModules)
	{
		if (const AABTSM7BuildingModule* Module = WeakModule.Get();
			Module != nullptr && Module->IsDynamic())
		{
			++PostActivationActualActiveBodyCount;
		}
	}
	if (PostActivationActualActiveBodyCount > FixedSixGameplayMaximumActiveBodies)
	{
		OutError = TEXT("FixedSixDamageEpochActiveBodyLimitInvariantBroken");
		bJuryDemoFixedSixChaosDeferredActivationInProgress = false;
		return false;
	}
	JuryDemoFixedSixActivePhysicsBodyCount = PostActivationActualActiveBodyCount;
	bJuryDemoFixedSixChaosDeferredActivationInProgress = false;
	const int32 NewlyUnsupportedCount = SupportClosureModules.Num();
	const int32 PublishedCount = PhysicsModules.Num() + OverflowModules.Num();
	const int32 UnpublishedUnsupportedCount = FMath::Max(0,
		NewlyUnsupportedCount - PublishedCount);
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M7][FixedSixDeferredChaos][FirstHitActivated]")
	TEXT(" Entry=%s Epoch=%llu TriggerBrick=%d ActiveBodies=%d NewBodies=%d QueueBodies=%d Affected=%d DeriveMS=%.3f PublishMS=%.3f OverflowQueue=1 IndependentBrickBodies=1")
		TEXT(" TerrainBuildingResponse=Block PadsBuildingResponse=Block")
		TEXT(" CCD=1 SiteUniformGravity=1 DamageTransaction=Continue"),
		*JuryDemoFixedSixStaticEntry->ManifestEntryId.ToString(),
		DamageEpoch, TriggerBrickIds[0], JuryDemoFixedSixActivePhysicsBodyCount,
		PhysicsModules.Num(), OverflowModules.Num(), AffectedBrickIds.Num(),
		DeriveMilliseconds, (FPlatformTime::Seconds() - PublishBeginSeconds) * 1000.0);
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M7][DamageEpoch][Summary]")
		TEXT(" Entry=%s Epoch=%llu Derivations=1 ClosureMS=%.3f Unsupported=%d Published=%d UnpublishedUnsupported=%d QueueDrop=0 ActivateMS=%.3f ImpulseBricks=%d"),
		*JuryDemoFixedSixStaticEntry->ManifestEntryId.ToString(), DamageEpoch,
		DeriveMilliseconds, NewlyUnsupportedCount, PublishedCount,
		UnpublishedUnsupportedCount,
		(FPlatformTime::Seconds() - PublishBeginSeconds) * 1000.0,
		AppliedImpulseBrickCount);
	if (UnpublishedUnsupportedCount != 0)
	{
		OutError = TEXT("FixedSixDamageEpochUnpublishedUnsupportedInvariantBroken");
		return false;
	}
	SetActorTickEnabled(!JuryDemoFixedSixOverflowKinematicModules.IsEmpty());
	return true;
}

bool AABTSM73StableBuildingActor::PromoteJuryDemoFixedSixOverflowModule(
	AABTSM7BuildingModule& Module, const bool bDirectImpact, FString& OutError)
{
	OutError.Reset();
	if (!Module.IsOverflowKinematic()
		|| !JuryDemoFixedSixOverflowKinematicModules.ContainsByPredicate(
			[&Module](const TWeakObjectPtr<AABTSM7BuildingModule>& Candidate)
			{ return Candidate.Get() == &Module; })
		|| !RuntimeMaterialSystem.IsValid() || !JuryDemoFixedSixStaticEntry.IsSet())
	{
		OutError = TEXT("FixedSixOverflowPromotionIdentityInvalid");
		return false;
	}
	int32 ActualActive = 0;
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& Weak : RuntimeModules)
	{
		if (const AABTSM7BuildingModule* Candidate = Weak.Get();
			Candidate != nullptr && Candidate->IsDynamic())
		{
			++ActualActive;
		}
	}
	if (ActualActive >= FixedSixGameplayMaximumActiveBodies)
	{
		OutError = TEXT("FixedSixOverflowPromotionNoBodySlot");
		return false;
	}
	const FVector LinearVelocity = Module.GetOverflowKinematicLinearVelocity();
	const FVector AngularVelocity = Module.GetOverflowKinematicAngularVelocityDegrees();
	const FABTSM7ChaosBodyProfile DestructionProfile =
		FABTSM7ChaosBodyProfile::DestructionCandidate();
	TArray<AABTSM7BuildingModule*> Singleton = {&Module};
	if (!RuntimeMaterialSystem->BeginSiteUniformLaunchPhysics(Singleton,
		JuryDemoFixedSixStaticEntry->WorldTransform.GetLocation(),
		JuryDemoFixedSixStaticEntry->SupportCenterWorldCM,
		FixedSixGravityCMPerSec2, FixedSixMaximumObservationSeconds + 1.0f,
		/*bPenetrationPrevalidated=*/false, &DestructionProfile))
	{
		OutError = TEXT("FixedSixOverflowPromotionLaunchRejected");
		return false;
	}
	if (UStaticMeshComponent* Mesh = Module.GetMeshComponent())
	{
		Mesh->SetPhysicsLinearVelocity(LinearVelocity);
		Mesh->SetPhysicsAngularVelocityInDegrees(AngularVelocity);
		Mesh->WakeAllRigidBodies();
	}
	for (int32 Index = JuryDemoFixedSixOverflowKinematicModules.Num() - 1;
		Index >= 0; --Index)
	{
		if (JuryDemoFixedSixOverflowKinematicModules[Index].Get() == &Module)
		{
			JuryDemoFixedSixOverflowKinematicModules.RemoveAt(Index);
		}
	}
	JuryDemoFixedSixActivePhysicsBodyCount = ActualActive + 1;
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M7][OverflowKinematicQueue][Promoted]")
		TEXT(" Entry=%s Brick=%d Direct=%d Active=%d Queue=%d CCD=1"),
		*JuryDemoFixedSixStaticEntry->ManifestEntryId.ToString(),
		Module.GetDamageLifecycleBrickId(), bDirectImpact ? 1 : 0,
		JuryDemoFixedSixActivePhysicsBodyCount,
		JuryDemoFixedSixOverflowKinematicModules.Num());
	return true;
}

bool AABTSM73StableBuildingActor::PromoteJuryDemoFixedSixOverflowForDirectImpact(
	AABTSM7BuildingModule& Module, FString& OutError)
{
	return PromoteJuryDemoFixedSixOverflowModule(Module, true, OutError);
}

bool AABTSM73StableBuildingActor::AdoptJuryDemoFixedSixDynamicAsOverflow(
	AABTSM7BuildingModule& Module, FString& OutError)
{
	OutError.Reset();
	if (!bJuryDemoFixedSixChaosPrepared || Module.GetDamageLifecycleOwner() != this
		|| !Module.IsDynamic() || Module.IsBroken() || Module.IsRecycled())
	{
		OutError = TEXT("FixedSixWalkReturnOverflowIdentityInvalid");
		return false;
	}
	UStaticMeshComponent* Mesh = Module.GetMeshComponent();
	const FVector LinearVelocity = Mesh != nullptr
		? Mesh->GetPhysicsLinearVelocity() : FVector::ZeroVector;
	const FVector AngularVelocity = Mesh != nullptr
		? Mesh->GetPhysicsAngularVelocityInDegrees() : FVector::ZeroVector;
	Module.BeginOverflowKinematic(LinearVelocity, AngularVelocity,
		JuryDemoFixedSixChaosSiteUp, FixedSixGravityCMPerSec2);
	if (!JuryDemoFixedSixOverflowKinematicModules.ContainsByPredicate(
		[&Module](const TWeakObjectPtr<AABTSM7BuildingModule>& Candidate)
		{ return Candidate.Get() == &Module; }))
	{
		JuryDemoFixedSixOverflowKinematicModules.Add(&Module);
		JuryDemoFixedSixOverflowKinematicModules.Sort([](
			const TWeakObjectPtr<AABTSM7BuildingModule>& Left,
			const TWeakObjectPtr<AABTSM7BuildingModule>& Right)
		{
			const AABTSM7BuildingModule* LeftModule = Left.Get();
			const AABTSM7BuildingModule* RightModule = Right.Get();
			return LeftModule != nullptr && RightModule != nullptr
				? LeftModule->GetDamageLifecycleBrickId()
					< RightModule->GetDamageLifecycleBrickId()
				: LeftModule != nullptr;
		});
	}
	JuryDemoFixedSixActivePhysicsBodyCount = FMath::Max(0,
		JuryDemoFixedSixActivePhysicsBodyCount - 1);
	SetActorTickEnabled(true);
	return true;
}

void AABTSM73StableBuildingActor::TickJuryDemoFixedSixOverflowKinematic(
	const float DeltaSeconds)
{
	if (JuryDemoFixedSixOverflowKinematicModules.IsEmpty()) return;
	JuryDemoFixedSixOverflowAccumulatorSeconds += FMath::Max(0.0f, DeltaSeconds);
	while (JuryDemoFixedSixOverflowAccumulatorSeconds >= FixedSixSimulationDeltaSeconds)
	{
		JuryDemoFixedSixOverflowAccumulatorSeconds -= FixedSixSimulationDeltaSeconds;
		// Promotion and BreakModule deliberately remove their stable BrickId from
		// the live queue.  Iterate a deterministic snapshot so the same fixed
		// step never mutates the array being traversed (and therefore cannot
		// skip a later brick or trip UE's ranged-for mutation guard).
		const TArray<TWeakObjectPtr<AABTSM7BuildingModule>> StepModules =
			JuryDemoFixedSixOverflowKinematicModules;
		for (const TWeakObjectPtr<AABTSM7BuildingModule>& Weak : StepModules)
		{
			AABTSM7BuildingModule* Module = Weak.Get();
			if (Module == nullptr || !Module->IsOverflowKinematic()) continue;
			if (Module->HasOverflowPendingBreak())
			{
				const EABTSM7ModuleKind Kind = Module->GetModuleKind();
				const EABTSM7BuildingMaterial Material = Module->GetBuildingMaterial();
				if (Module->BreakModule() && Kind == EABTSM7ModuleKind::Brick
					&& RuntimeMaterialSystem.IsValid())
				{
					RuntimeMaterialSystem->OnMaterialRecovered.Broadcast(Material, 1);
				}
				continue;
			}
			UWorld* World = GetWorld();
			if (World == nullptr) continue;
			FCollisionQueryParams Params(
				SCENE_QUERY_STAT(M7OverflowKinematicGroundProbe), false, Module);
			TArray<FHitResult> Hits;
			UStaticMeshComponent* Mesh = Module->GetMeshComponent();
			const FVector Start = Module->GetActorLocation();
			const FVector End = Module->PredictOverflowKinematicLocation(
				FixedSixSimulationDeltaSeconds);
			const FVector HalfExtent = Mesh != nullptr
				? Mesh->Bounds.BoxExtent.GetAbs() : FVector(18.0f);
			const FCollisionShape Shape = FCollisionShape::MakeBox(HalfExtent);
			World->SweepMultiByChannel(Hits, Start, End, Module->GetActorQuat(),
				ABTSDeveloperObstacleChannel, Shape, Params);
			const FVector* GroundLocation = nullptr;
			FVector ResolvedGroundLocation = FVector::ZeroVector;
			bool bDynamicContact = false;
			for (const FHitResult& Hit : Hits)
			{
				const bool bTerrain = Cast<AABTSM3Planet>(Hit.GetActor()) != nullptr;
				const bool bFrozenSupport =
					IsJuryDemoFixedSixGroundSupportPrimitive(Hit.GetComponent());
				const AABTSM7BuildingModule* HitModule =
					Cast<AABTSM7BuildingModule>(Hit.GetActor());
				const bool bSettledBrickSupport = HitModule != nullptr
					&& HitModule != Module && HitModule->IsOverflowKinematicSettled();
				if (bTerrain || bFrozenSupport || bSettledBrickSupport)
				{
					ResolvedGroundLocation = Hit.Location;
					GroundLocation = &ResolvedGroundLocation;
					break;
				}
				if (HitModule != nullptr)
				{
					bDynamicContact |= HitModule->IsDynamic();
				}
			}
			if (bDynamicContact)
			{
				FString PromotionError;
				if (PromoteJuryDemoFixedSixOverflowModule(*Module, false,
					PromotionError))
				{
					continue;
				}
			}
			Module->TickOverflowKinematic(FixedSixSimulationDeltaSeconds,
				GroundLocation);
		}
		for (int32 Index = JuryDemoFixedSixOverflowKinematicModules.Num() - 1;
			Index >= 0; --Index)
		{
			const TWeakObjectPtr<AABTSM7BuildingModule>& Weak =
				JuryDemoFixedSixOverflowKinematicModules[Index];
			if (!Weak.IsValid() || !Weak->IsOverflowKinematic())
			{
				JuryDemoFixedSixOverflowKinematicModules.RemoveAt(Index);
				continue;
			}
			if (AABTSM7BuildingModule* Module = Weak.Get();
				Module != nullptr && Module->IsOverflowKinematicSettled())
			{
				Module->FreezeSettledOverflowKinematic();
				JuryDemoFixedSixOverflowKinematicModules.RemoveAt(Index);
			}
		}
	}
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

#if WITH_DEV_AUTOMATION_TESTS

bool AABTSM73StableBuildingActor::
ArmJuryDemoFixedSixDamageEpochTickForAutomation(FString& OutError)
{
	OutError.Reset();
	if (!bJuryDemoFixedSixChaosPrepared
		|| bJuryDemoFixedSixChaosDeferredActivationInProgress
		|| bJuryDemoFixedSixChaosDeferredActivated)
	{
		OutError = TEXT("FixedSixDamageEpochAutomationArmStateInvalid");
		return false;
	}
	// This is deliberately narrower than the production transition: fixture
	// worlds have no M7 GameMode/terrain collision override, but Tick must still
	// execute the exact same support-closure publication transaction.
	bJuryDemoFixedSixChaosDeferredUntilFirstHit = false;
	bJuryDemoFixedSixChaosDeferredActivated = true;
	return true;
}

namespace ABTSM7DamageEpochAutomation
{
	class FFixedSixRuntimeTestWorld final : public FTestWorldWrapper
	{
	public:
		bool Create()
		{
			if (GEngine == nullptr)
			{
				ReportFailure(TEXT("GEngine unavailable"));
				return false;
			}
			UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
			UWorld::InitializationValues Values;
			Values.AllowAudioPlayback(false)
				.RequiresHitProxies(false)
				.CreatePhysicsScene(true)
				.ShouldSimulatePhysics(false)
				.EnableTraceCollision(true)
				.CreateNavigation(false)
				.CreateAISystem(false)
				.CreateFXSystem(false);
			TestWorld = UWorld::CreateWorld(EWorldType::Game, false,
				TEXT("ABTSM7FixedSixDamageEpochRuntimeWorld"), nullptr, true,
				ERHIFeatureLevel::Num, &Values);
			if (TestWorld == nullptr)
			{
				ReportFailure(TEXT("Failed to create Fixed-Six runtime test world"));
				return false;
			}
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.OwningGameInstance = GameInstance;
			Context.SetCurrentWorld(TestWorld);
			TestWorld->SetGameInstance(GameInstance);
			GameInstance->Init();
			return true;
		}
	};

	bool BuildPreparedFixedSixRuntime(
		FAutomationTestBase& Test,
		FFixedSixRuntimeTestWorld& WorldWrapper,
		AABTSM7BuildingMaterialSystem*& OutMaterialSystem,
		AABTSM73StableBuildingActor*& OutBuilding,
		AABTSM7BuildingModule*& OutModule)
	{
		OutMaterialSystem = nullptr;
		OutBuilding = nullptr;
		OutModule = nullptr;
		if (!WorldWrapper.Create())
		{
			WorldWrapper.ForwardErrorMessages(&Test);
			return false;
		}
		UWorld* World = WorldWrapper.GetTestWorld();
		if (!Test.TestNotNull(TEXT("Fixed-Six runtime world"), World)) return false;
		FPhysScene* PhysicsScene = World->GetPhysicsScene();
		Chaos::FPhysicsSolver* PhysicsSolver = PhysicsScene != nullptr
			? PhysicsScene->GetSolver()
			: nullptr;
		if (!Test.TestNotNull(TEXT("Fixed-Six runtime deterministic solver"),
			PhysicsSolver)) return false;
		// Production activation owns this switch through GameMode.  The isolated
		// automation world has no GameMode, so make the same solver contract
		// explicit solely for the duration of this runtime seam.
		PhysicsSolver->SetIsDeterministic(true);
		if (!Test.TestTrue(TEXT("Fixed-Six runtime enables deterministic solver"),
			PhysicsSolver->IsDetemerministic())) return false;

		AABTSM3Planet* Planet = World->SpawnActor<AABTSM3Planet>();
		if (!Test.TestNotNull(TEXT("Fixed-Six runtime planet"), Planet)) return false;
		Planet->WorldSeed = FABTSJuryDemoFixedSixContract::FrozenWorldSeed;
		// Keep the production terrain resolution: reducing SurfaceSubdivision
		// changes the frozen site's collision samples and correctly fails M3's
		// clearance gate before this M7-only runtime seam can be constructed.
		if (!Test.TestTrue(TEXT("Fixed-Six runtime planet rebuild"),
			Planet->RebuildPlanet())) return false;

		FABTSBuildingGenerationContract Contract;
		if (!Test.TestTrue(TEXT("Fixed-Six runtime exports source contract"),
			Planet->TryExportBuildingGenerationContract(Contract))) return false;
		FABTSM73JuryDemoFixedSixStaticPlan Plan;
		FString Error;
		if (!Test.TestTrue(TEXT("Fixed-Six runtime resolves exact static plan"),
			FABTSM73JuryDemoFixedSixRegistration::BuildStaticPlan(
				Contract, Plan, Error)))
		{
			Test.AddError(Error);
			return false;
		}
		OutMaterialSystem = World->SpawnActor<AABTSM7BuildingMaterialSystem>();
		if (!Test.TestNotNull(TEXT("Fixed-Six runtime material system"),
			OutMaterialSystem)) return false;
		TArray<TWeakObjectPtr<AABTSM73StableBuildingActor>> Actors;
		if (!Test.TestTrue(TEXT("Fixed-Six runtime static actor registration"),
			FABTSM73JuryDemoFixedSixRegistration::SpawnStaticActors(
				*World, *OutMaterialSystem, AABTSM73StableBuildingActor::StaticClass(),
				MoveTemp(Plan), Actors, Error)))
		{
			Test.AddError(Error);
			return false;
		}
		OutBuilding = Actors.IsValidIndex(0) ? Actors[0].Get() : nullptr;
		if (!Test.TestNotNull(TEXT("Fixed-Six runtime first building"),
			OutBuilding)) return false;
		if (!Test.TestTrue(TEXT("Fixed-Six runtime prepares exact brick modules"),
			OutBuilding->PrepareJuryDemoFixedSixChaosValidation(
				FixedSixGravityCMPerSec2, Error)))
		{
			Test.AddError(Error);
			return false;
		}
		for (TActorIterator<AABTSM7BuildingModule> It(World); It; ++It)
		{
			AABTSM7BuildingModule* Candidate = *It;
			if (Candidate != nullptr
				&& OutBuilding->IsJuryDemoFixedSixRegisteredRuntimeModule(
					*Candidate, *OutMaterialSystem))
			{
				OutModule = Candidate;
				break;
			}
		}
		return Test.TestNotNull(TEXT("Fixed-Six runtime ledger resolves a live brick"),
			OutModule);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM7FixedSixDisconnectedSupportTest,
	"ABTS.M7.FixedSixSupportClosure.DisconnectedSupportRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM7FixedSixDisconnectedSupportTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const auto MakeBrick = [](const int32 BrickId, const FVector& Center,
		const FQuat& Rotation = FQuat::Identity)
	{
		FABTSM73BeamD1BrickBinding Brick;
		Brick.BrickId = BrickId;
		Brick.LocalTransform = FTransform(Rotation, Center);
		Brick.LocalBounds = FBox(Center - FVector(18.0f),
			Center + FVector(18.0f));
		return Brick;
	};
	TArray<FABTSM73BeamD1BrickBinding> Disconnected;
	Disconnected.Add(MakeBrick(0, FVector(0.0f, 0.0f, 18.0f)));
	// The second brick has a 36 cm unsupported gap above the grounded one.
	Disconnected.Add(MakeBrick(1, FVector(0.0f, 0.0f, 90.0f)));
	FString Error;
	TestFalse(TEXT("Frozen disconnected support graph fails closed"),
		VerifyFrozenAxisAlignedSupportConnectivity(Disconnected, Error));
	TestTrue(TEXT("Disconnected graph records its exact rejection"),
		Error.Contains(TEXT("FrozenDisconnected")));

	TArray<FABTSM73BeamD1BrickBinding> Rotated;
	Rotated.Add(MakeBrick(0, FVector(0.0f, 0.0f, 18.0f),
		FQuat(FVector::UpVector, FMath::DegreesToRadians(45.0f))));
	Error.Reset();
	TestFalse(TEXT("Non-axis-aligned frozen OBB never uses an AABB support edge"),
		VerifyFrozenAxisAlignedSupportConnectivity(Rotated, Error));
	TestTrue(TEXT("Rotation rejection is explicit"),
		Error.Contains(TEXT("NonAxisAligned")));
	TestTrue(TEXT("Exact independent closure publishes within the body cap"),
		CanPublishExactIndependentSupportClosure(127, 1, 128));
	TestFalse(TEXT("Over-cap closure rejects atomically without a recycle fallback"),
		CanPublishExactIndependentSupportClosure(1, 128, 128));
	int32 Dynamic129 = 0;
	int32 Queue129 = 0;
	TestTrue(TEXT("129-brick closure receives a visible per-brick overflow plan"),
		BuildOverflowKinematicPublicationPlan(0, 129, 128, 16,
			Dynamic129, Queue129));
	TestEqual(TEXT("129 closure preserves all affected identities"),
		Dynamic129 + Queue129, 129);
	TestTrue(TEXT("129 closure keeps exact Chaos bodies within the cap"),
		Dynamic129 <= 128);
	TestTrue(TEXT("129 closure queues rather than recycles the overflow brick"),
		Queue129 > 0);
	int32 Dynamic256 = 0;
	int32 Queue256 = 0;
	TestTrue(TEXT("256-brick closure receives a deterministic overflow plan"),
		BuildOverflowKinematicPublicationPlan(0, 256, 128, 16,
			Dynamic256, Queue256));
	TestEqual(TEXT("256 closure preserves all affected identities"),
		Dynamic256 + Queue256, 256);
	TestTrue(TEXT("256 closure never exceeds the exact Chaos cap"),
		Dynamic256 <= 128);
	TestEqual(TEXT("Overflow publication reserves emergency exact slots"),
		Dynamic256, 112);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM7FixedSixDamageEpochLedgerTest,
	"ABTS.M7.FixedSixDamageEpoch.BlackbirdTopologyLedger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM7FixedSixDamageEpochLedgerTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TArray<int32> EpochBrickIds;
	const TArray<int32> BlackbirdRadialIds = {8, 3, 8, 1};
	TestTrue(TEXT("One black-bird event merges every touched BrickId into one epoch"),
		MergeStableDamageEpochBrickIds(EpochBrickIds, BlackbirdRadialIds, 12));
	TestEqual(TEXT("Radial ledger has one entry per stable BrickId"),
		EpochBrickIds.Num(), 3);
	TestEqual(TEXT("Radial ledger is stable-sorted at its first entry"),
		EpochBrickIds[0], 1);
	TestEqual(TEXT("Radial ledger is stable-sorted at its last entry"),
		EpochBrickIds.Last(), 8);

	const TArray<int32> ThroughHoleTopologyIds = {3, 7, 3};
	TestTrue(TEXT("A same-frame through-hole topology mutation coalesces"),
		MergeStableDamageEpochBrickIds(EpochBrickIds, ThroughHoleTopologyIds, 12));
	TestEqual(TEXT("Repeated break callback cannot create a second closure seed"),
		EpochBrickIds.Num(), 4);
	TestEqual(TEXT("Topology seed remains deterministic after radial merge"),
		EpochBrickIds[2], 7);

	const TArray<int32> InvalidIds = {9, 12};
	TestFalse(TEXT("Out-of-range topology identity fails closed"),
		MergeStableDamageEpochBrickIds(EpochBrickIds, InvalidIds, 12));
	TestEqual(TEXT("Rejected identity cannot mutate an accepted epoch ledger"),
		EpochBrickIds.Num(), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM7FixedSixDamageEpochRadialRuntimeTest,
	"ABTS.M7.FixedSixDamageEpoch.RadialRuntimeLedger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM7FixedSixDamageEpochRadialRuntimeTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM7DamageEpochAutomation;
	(void)Parameters;
	FFixedSixRuntimeTestWorld WorldWrapper;
	AABTSM7BuildingMaterialSystem* MaterialSystem = nullptr;
	AABTSM73StableBuildingActor* Building = nullptr;
	AABTSM7BuildingModule* Module = nullptr;
	if (!BuildPreparedFixedSixRuntime(*this, WorldWrapper, MaterialSystem,
		Building, Module))
	{
		return false;
	}
	FString ArmError;
	if (!TestTrue(TEXT("Runtime fixture arms the real Tick publication seam"),
		Building->ArmJuryDemoFixedSixDamageEpochTickForAutomation(ArmError)))
	{
		AddError(ArmError);
		return false;
	}
	TestEqual(TEXT("Runtime radial test starts with no pending epoch seeds"),
		Building->GetJuryDemoFixedSixQueuedDamageSeedCountForValidation(), 0);
	// Fixed-Six gameplay identity is DamageLifecycleOwner + the immutable
	// BrickId ledger.  Deliberately make the transient Actor owner the building
	// (not the MaterialSystem) to catch a regression to the old owner==this
	// filter in ApplyRadialBlast.
	Module->SetOwner(Building);
	TestTrue(TEXT("Runtime brick remains ledger-owned after Actor-owner change"),
		Building->IsJuryDemoFixedSixRegisteredRuntimeModule(*Module,
			*MaterialSystem));
	// The production launch path correctly retains MaterialSystem Actor
	// ownership; restore it before executing the real Tick publication seam.
	Module->SetOwner(MaterialSystem);
	MaterialSystem->ApplyRadialBlast(Module->GetActorLocation(),
		72.0f, 760.0f, 900.0f);
	TestTrue(TEXT("Runtime black-bird radial effect publishes a building epoch"),
		Building->GetJuryDemoFixedSixQueuedDamageSeedCountForValidation() > 0);
	TestTrue(TEXT("Runtime black-bird radial effect damages the registered brick"),
		Module->IsBroken());
	TestEqual(TEXT("Runtime radial discovery queues but does not synchronously derive"),
		Building->GetJuryDemoFixedSixDamageEpochForValidation(), uint64(0));
	// This is the production actor Tick, not a queue-only plan assertion.
	Building->Tick(1.0f / 60.0f);
	TestEqual(TEXT("One black-bird event resolves exactly one building epoch"),
		Building->GetJuryDemoFixedSixDamageEpochForValidation(), uint64(1));
	TestEqual(TEXT("Resolved epoch leaves no queued seed behind"),
		Building->GetJuryDemoFixedSixQueuedDamageSeedCountForValidation(), 0);
	int32 IndependentMotionCount = 0;
	int32 NonZeroVelocityCount = 0;
	for (TActorIterator<AABTSM7BuildingModule> It(WorldWrapper.GetTestWorld()); It; ++It)
	{
		AABTSM7BuildingModule* Candidate = *It;
		if (Candidate == nullptr || Candidate->GetDamageLifecycleOwner() != Building
			|| Candidate->IsBroken() || Candidate->IsRecycled())
		{
			continue;
		}
		if (Candidate->IsDynamic() || Candidate->IsOverflowKinematic())
		{
			++IndependentMotionCount;
			const FVector Velocity = Candidate->IsOverflowKinematic()
				? Candidate->GetOverflowKinematicLinearVelocity()
				: Candidate->GetMeshComponent() != nullptr
					? Candidate->GetMeshComponent()->GetPhysicsLinearVelocity()
					: FVector::ZeroVector;
			NonZeroVelocityCount += Velocity.SizeSquared() > 1.0f ? 1 : 0;
		}
	}
	TestTrue(TEXT("Radial closure publishes visible independent bricks"),
		IndependentMotionCount > 0);
	TestTrue(TEXT("Outer black-bird ring retains non-zero published momentum"),
		NonZeroVelocityCount > 0);
	return true;
}

#endif
