// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "ABTSRuntime.h"
#include "ABTSM73BeamC3V2MassiveXYCribPrototype.h"
#include "ABTSM7MaterialProfileLibrary.h"
#include "ABTSM7PenetrationValidator.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Building/ABTSM7BuildingModule.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Crc.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsConstraintActor.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Tests/AutomationCommon.h"
#include "TestStage/ABTSM71TestStageActors.h"

namespace ABTSM73BeamC3V2ChaosTests
{
	constexpr float FixedDeltaSeconds = 1.0f / 30.0f;
	constexpr float MinimumObservationSeconds = 1.25f;
	constexpr float StableHoldSeconds = 0.45f;
	constexpr float MaximumObservationSeconds = 6.0f;
	constexpr float MaximumLinearSpeedCMPerSec = 4.0f;
	constexpr float MaximumAngularSpeedDegreesPerSec = 1.5f;
	constexpr float MaximumPlanarDriftCM = 4.0f;
	constexpr float MaximumSettlementCM = 6.0f;
	constexpr float MaximumRotationDegrees = 2.0f;
	constexpr int32 PositionSolverIterations = 32;
	constexpr int32 VelocitySolverIterations = 8;
	constexpr float GravityCMPerSec2 = 980.0f;
	constexpr float PerturbationVelocityCMPerSec = 8.0f;
	constexpr int32 GravityProbeTickCount = 8;
	constexpr float MinimumGravityProbeDropCM = 10.0f;
	constexpr float MinimumGravityProbeDownwardSpeedCMPerSec = 50.0f;
	constexpr float BoundsToleranceCM = 0.5f;
	constexpr float ExpectedWoodLogMassKG = 417.871f;
	constexpr float ExpectedWoodLogMassToleranceKG = 4.2f;
	constexpr float MinimumOutwardResponseDisplacementCM = 0.05f;
	constexpr float MinimumOutwardResponseSpeedCMPerSec = 1.0f;
	constexpr uint32 ExpectedMassiveXYCribGeometryCrc32 = 3576735518u;

	int32 QuantizeFixtureValue(const float Value)
	{
		return FMath::RoundToInt(Value * 1000.0f);
	}

	bool ResolveOutwardTopRailPerturbation(
		const ABTSM73BeamC3V2::FMassiveXYCribResult& Prototype,
		int32& OutBrickIndex,
		FVector& OutVelocityChange)
	{
		using namespace ABTSM73BeamC3V2;
		OutBrickIndex = INDEX_NONE;
		OutVelocityChange = FVector::ZeroVector;
		int32 HighestCourse = INDEX_NONE;
		for (int32 BrickIndex = 0;
			BrickIndex < Prototype.Bricks.Num();
			++BrickIndex)
		{
			const FMassiveXYCribBrick& Brick = Prototype.Bricks[BrickIndex];
			if (Brick.RailIndex == 0 && Brick.CourseIndex > HighestCourse)
			{
				HighestCourse = Brick.CourseIndex;
				OutBrickIndex = BrickIndex;
			}
		}
		if (!Prototype.Bricks.IsValidIndex(OutBrickIndex))
		{
			return false;
		}

		const FMassiveXYCribBrick& Brick = Prototype.Bricks[OutBrickIndex];
		const FVector OutwardAxis = Brick.Axis == ECourseAxis::X
			? FVector(0.0f, FMath::Sign(Brick.CenterCM.Y), 0.0f)
			: FVector(FMath::Sign(Brick.CenterCM.X), 0.0f, 0.0f);
		if (OutwardAxis.IsNearlyZero())
		{
			return false;
		}
		OutVelocityChange = OutwardAxis * PerturbationVelocityCMPerSec;
		return true;
	}

	uint32 ComputePhysicsFixtureCrc32(
		const ABTSM73BeamC3V2::FMassiveXYCribSettings& Settings,
		const ABTSM73BeamC3V2::FMassiveXYCribResult& Prototype,
		const FABTSM7MaterialProfile& MaterialProfile,
		const UPhysicalMaterial& EffectiveMaterial,
		const bool bApplyPerturbation,
		const int32 PerturbedBrickIndex,
		const FVector& PerturbationVelocityChange)
	{
		const float DamageGraceSeconds = MaximumObservationSeconds
			* (bApplyPerturbation ? 2.0f : 1.0f) + 1.0f;
		const FString Canonical = FString::Printf(
			TEXT("BeamC3V2PhysicsFixture:v4:G=%u:M=%d:FloorM=%d:DF=%d:SF=%d:R=%d:D=%d:MP=%d:FO=%d,FM=%d:RO=%d,RM=%d:DT=%d:Min=%d:Hold=%d:Max=%d:Lin=%d:Ang=%d:Drift=%d:Settle=%d:Rot=%d:Solver=%d,%d:Gravity=%d:Probe=%d,%d,%d:BoundsTol=%d:Mass=%d,%d:DamageGrace=%d:Perturb=%d,Brick=%d,DV=%d,%d,%d:Response=%d,%d"),
			Prototype.GeometryCrc32,
			static_cast<int32>(Settings.Material),
			static_cast<int32>(Settings.Material),
			QuantizeFixtureValue(MaterialProfile.DynamicFriction),
			QuantizeFixtureValue(MaterialProfile.StaticFriction),
			QuantizeFixtureValue(MaterialProfile.Restitution),
			QuantizeFixtureValue(MaterialProfile.DensityGPerCubicCM),
			QuantizeFixtureValue(EffectiveMaterial.RaiseMassToPower),
			EffectiveMaterial.bOverrideFrictionCombineMode ? 1 : 0,
			static_cast<int32>(EffectiveMaterial.FrictionCombineMode),
			EffectiveMaterial.bOverrideRestitutionCombineMode ? 1 : 0,
			static_cast<int32>(EffectiveMaterial.RestitutionCombineMode),
			QuantizeFixtureValue(FixedDeltaSeconds),
			QuantizeFixtureValue(MinimumObservationSeconds),
			QuantizeFixtureValue(StableHoldSeconds),
			QuantizeFixtureValue(MaximumObservationSeconds),
			QuantizeFixtureValue(MaximumLinearSpeedCMPerSec),
			QuantizeFixtureValue(MaximumAngularSpeedDegreesPerSec),
			QuantizeFixtureValue(MaximumPlanarDriftCM),
			QuantizeFixtureValue(MaximumSettlementCM),
			QuantizeFixtureValue(MaximumRotationDegrees),
			PositionSolverIterations,
			VelocitySolverIterations,
			QuantizeFixtureValue(GravityCMPerSec2),
			GravityProbeTickCount,
			QuantizeFixtureValue(MinimumGravityProbeDropCM),
			QuantizeFixtureValue(MinimumGravityProbeDownwardSpeedCMPerSec),
			QuantizeFixtureValue(BoundsToleranceCM),
			QuantizeFixtureValue(ExpectedWoodLogMassKG),
			QuantizeFixtureValue(ExpectedWoodLogMassToleranceKG),
			QuantizeFixtureValue(DamageGraceSeconds),
			bApplyPerturbation ? 1 : 0,
			PerturbedBrickIndex,
			QuantizeFixtureValue(PerturbationVelocityChange.X),
			QuantizeFixtureValue(PerturbationVelocityChange.Y),
			QuantizeFixtureValue(PerturbationVelocityChange.Z),
			QuantizeFixtureValue(MinimumOutwardResponseDisplacementCM),
			QuantizeFixtureValue(MinimumOutwardResponseSpeedCMPerSec));
		return FCrc::StrCrc32(*Canonical);
	}

	class FBeamC3V2PhysicsWorld final : public FTestWorldWrapper
	{
	public:
		bool CreatePhysicsWorld()
		{
			if (TestWorld != nullptr)
			{
				ReportFailure(TEXT("Beam-C3 V2 physics world already exists"));
				return false;
			}
			if (GEngine == nullptr)
			{
				ReportFailure(TEXT("GEngine is unavailable"));
				return false;
			}

			UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
			UWorld::InitializationValues InitializationValues;
			InitializationValues
				.AllowAudioPlayback(false)
				.RequiresHitProxies(false)
				.CreatePhysicsScene(true)
				.ShouldSimulatePhysics(true)
				.EnableTraceCollision(true)
				.CreateNavigation(false)
				.CreateAISystem(false)
				.CreateFXSystem(false)
				.SetDefaultGameMode(AGameModeBase::StaticClass());
			TestWorld = UWorld::CreateWorld(
				EWorldType::Game,
				false,
				TEXT("ABTSM73BeamC3V2PhysicsTestWorld"),
				nullptr,
				true,
				ERHIFeatureLevel::Num,
				&InitializationValues);
			if (TestWorld == nullptr)
			{
				ReportFailure(TEXT("Failed to create Beam-C3 V2 physics world"));
				return false;
			}

			TestWorld->SetShouldTick(false);
			FWorldContext& WorldContext =
				GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext.OwningGameInstance = GameInstance;
			WorldContext.SetCurrentWorld(TestWorld);
			TestWorld->SetGameInstance(GameInstance);
			GameInstance->Init();
			return true;
		}
	};

	struct FObservationResult
	{
		bool bReachedQuietWindow = false;
		bool bEndedInQuietWindow = false;
		float FirstQuietWindowSeconds = 0.0f;
		float ElapsedSeconds = 0.0f;
		float FinalMaximumPlanarDriftCM = 0.0f;
		float FinalMaximumSettlementCM = 0.0f;
		float FinalMaximumRotationDegrees = 0.0f;
		float FinalMaximumLinearSpeedCMPerSec = 0.0f;
		float FinalMaximumAngularSpeedDegreesPerSec = 0.0f;
		float PeakPlanarDriftCM = 0.0f;
		float PeakSettlementCM = 0.0f;
		float PeakRotationDegrees = 0.0f;
		float FirstResponseOutwardDisplacementCM = 0.0f;
		float FirstResponseOutwardVelocityCMPerSec = 0.0f;
	};

	bool ObserveUntilQuiet(
		FAutomationTestBase& Test,
		FBeamC3V2PhysicsWorld& WorldWrapper,
		const TArray<AABTSM7BuildingModule*>& Modules,
		const TArray<FTransform>& Baselines,
		const TCHAR* Phase,
		const int32 ResponseModuleIndex,
		const FVector& ResponseDirection,
		FObservationResult& OutResult)
	{
		OutResult = FObservationResult();
		if (Modules.IsEmpty() || Baselines.Num() != Modules.Num())
		{
			Test.AddError(FString::Printf(
				TEXT("%s has invalid observation inputs: Modules=%d Baselines=%d"),
				Phase,
				Modules.Num(),
				Baselines.Num()));
			return false;
		}
		float QuietSeconds = 0.0f;
		const int32 MaximumTicks = FMath::CeilToInt(
			MaximumObservationSeconds / FixedDeltaSeconds);
		for (int32 TickIndex = 0; TickIndex < MaximumTicks; ++TickIndex)
		{
			if (!WorldWrapper.TickTestWorld(FixedDeltaSeconds))
			{
				WorldWrapper.ForwardErrorMessages(&Test);
				return false;
			}

			OutResult.ElapsedSeconds += FixedDeltaSeconds;
			OutResult.FinalMaximumPlanarDriftCM = 0.0f;
			OutResult.FinalMaximumSettlementCM = 0.0f;
			OutResult.FinalMaximumRotationDegrees = 0.0f;
			OutResult.FinalMaximumLinearSpeedCMPerSec = 0.0f;
			OutResult.FinalMaximumAngularSpeedDegreesPerSec = 0.0f;
			bool bEveryBodyQuiet = true;
			for (int32 ModuleIndex = 0;
				ModuleIndex < Modules.Num();
				++ModuleIndex)
			{
				const AABTSM7BuildingModule* Module = Modules[ModuleIndex];
				const UStaticMeshComponent* Mesh =
					Module != nullptr ? Module->GetMeshComponent() : nullptr;
				if (Module == nullptr || Mesh == nullptr)
				{
					Test.AddError(FString::Printf(
						TEXT("%s lost module %d during Chaos"),
						Phase,
						ModuleIndex));
					return false;
				}

				const FVector Delta = Module->GetActorLocation()
					- Baselines[ModuleIndex].GetLocation();
				const float PlanarDriftCM = FVector(Delta.X, Delta.Y, 0.0f).Size();
				const float SettlementCM = FMath::Abs(Delta.Z);
				const float RotationDegrees = FMath::RadiansToDegrees(
					Baselines[ModuleIndex].GetRotation().AngularDistance(
						Module->GetActorQuat()));
				const float LinearSpeedCMPerSec =
					Mesh->GetPhysicsLinearVelocity().Size();
				if (TickIndex == 0 && ModuleIndex == ResponseModuleIndex)
				{
					const FVector UnitResponseDirection =
						ResponseDirection.GetSafeNormal();
					OutResult.FirstResponseOutwardDisplacementCM =
						FVector::DotProduct(Delta, UnitResponseDirection);
					OutResult.FirstResponseOutwardVelocityCMPerSec =
						FVector::DotProduct(
							Mesh->GetPhysicsLinearVelocity(),
							UnitResponseDirection);
				}
				const float AngularSpeedDegreesPerSec =
					Mesh->GetPhysicsAngularVelocityInDegrees().Size();
				OutResult.FinalMaximumPlanarDriftCM = FMath::Max(
					OutResult.FinalMaximumPlanarDriftCM, PlanarDriftCM);
				OutResult.FinalMaximumSettlementCM = FMath::Max(
					OutResult.FinalMaximumSettlementCM, SettlementCM);
				OutResult.FinalMaximumRotationDegrees = FMath::Max(
					OutResult.FinalMaximumRotationDegrees, RotationDegrees);
				OutResult.FinalMaximumLinearSpeedCMPerSec = FMath::Max(
					OutResult.FinalMaximumLinearSpeedCMPerSec,
					LinearSpeedCMPerSec);
				OutResult.FinalMaximumAngularSpeedDegreesPerSec = FMath::Max(
					OutResult.FinalMaximumAngularSpeedDegreesPerSec,
					AngularSpeedDegreesPerSec);
				bEveryBodyQuiet = bEveryBodyQuiet
					&& LinearSpeedCMPerSec <= MaximumLinearSpeedCMPerSec
					&& AngularSpeedDegreesPerSec
						<= MaximumAngularSpeedDegreesPerSec;
			}

			OutResult.PeakPlanarDriftCM = FMath::Max(
				OutResult.PeakPlanarDriftCM,
				OutResult.FinalMaximumPlanarDriftCM);
			OutResult.PeakSettlementCM = FMath::Max(
				OutResult.PeakSettlementCM,
				OutResult.FinalMaximumSettlementCM);
			OutResult.PeakRotationDegrees = FMath::Max(
				OutResult.PeakRotationDegrees,
				OutResult.FinalMaximumRotationDegrees);
			if (OutResult.ElapsedSeconds >= MinimumObservationSeconds
				&& bEveryBodyQuiet)
			{
				QuietSeconds += FixedDeltaSeconds;
			}
			else
			{
				QuietSeconds = 0.0f;
			}
			if (!OutResult.bReachedQuietWindow
				&& QuietSeconds >= StableHoldSeconds)
			{
				OutResult.bReachedQuietWindow = true;
				OutResult.FirstQuietWindowSeconds = OutResult.ElapsedSeconds;
			}
		}
		OutResult.bEndedInQuietWindow = QuietSeconds >= StableHoldSeconds;

		Test.TestTrue(*FString::Printf(
			TEXT("%s reaches a real quiet window during the fixed %.1f s observation"),
			Phase,
			MaximumObservationSeconds),
			OutResult.bReachedQuietWindow);
		Test.TestTrue(*FString::Printf(
			TEXT("%s ends with a continuous %.2f s quiet window without Freeze"),
			Phase,
			StableHoldSeconds),
			OutResult.bEndedInQuietWindow);
		Test.TestTrue(*FString::Printf(
			TEXT("%s final planar drift %.3f cm <= %.1f cm"),
			Phase,
			OutResult.FinalMaximumPlanarDriftCM,
			MaximumPlanarDriftCM),
			OutResult.FinalMaximumPlanarDriftCM <= MaximumPlanarDriftCM);
		Test.TestTrue(*FString::Printf(
			TEXT("%s final settlement %.3f cm <= %.1f cm"),
			Phase,
			OutResult.FinalMaximumSettlementCM,
			MaximumSettlementCM),
			OutResult.FinalMaximumSettlementCM <= MaximumSettlementCM);
		Test.TestTrue(*FString::Printf(
			TEXT("%s final rotation %.3f deg <= %.1f deg"),
			Phase,
			OutResult.FinalMaximumRotationDegrees,
			MaximumRotationDegrees),
			OutResult.FinalMaximumRotationDegrees <= MaximumRotationDegrees);
		Test.TestTrue(*FString::Printf(
			TEXT("%s peak planar drift %.3f cm <= %.1f cm"),
			Phase,
			OutResult.PeakPlanarDriftCM,
			MaximumPlanarDriftCM),
			OutResult.PeakPlanarDriftCM <= MaximumPlanarDriftCM);
		Test.TestTrue(*FString::Printf(
			TEXT("%s peak settlement %.3f cm <= %.1f cm"),
			Phase,
			OutResult.PeakSettlementCM,
			MaximumSettlementCM),
			OutResult.PeakSettlementCM <= MaximumSettlementCM);
		Test.TestTrue(*FString::Printf(
			TEXT("%s peak rotation %.3f deg <= %.1f deg"),
			Phase,
			OutResult.PeakRotationDegrees,
			MaximumRotationDegrees),
			OutResult.PeakRotationDegrees <= MaximumRotationDegrees);
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M7.3-BeamC3V2][ChaosScreen][%s] ReachedQuiet=%d EndedQuiet=%d FirstQuiet=%.2f Seconds=%.2f FinalDrift=%.3f FinalSettlement=%.3f FinalRotation=%.3f FinalLinear=%.3f FinalAngular=%.3f PeakDrift=%.3f PeakSettlement=%.3f PeakRotation=%.3f FirstResponseDisplacement=%.3f FirstResponseVelocity=%.3f"),
			Phase,
			OutResult.bReachedQuietWindow ? 1 : 0,
			OutResult.bEndedInQuietWindow ? 1 : 0,
			OutResult.FirstQuietWindowSeconds,
			OutResult.ElapsedSeconds,
			OutResult.FinalMaximumPlanarDriftCM,
			OutResult.FinalMaximumSettlementCM,
			OutResult.FinalMaximumRotationDegrees,
			OutResult.FinalMaximumLinearSpeedCMPerSec,
			OutResult.FinalMaximumAngularSpeedDegreesPerSec,
			OutResult.PeakPlanarDriftCM,
			OutResult.PeakSettlementCM,
			OutResult.PeakRotationDegrees,
			OutResult.FirstResponseOutwardDisplacementCM,
			OutResult.FirstResponseOutwardVelocityCMPerSec);
		return !Test.HasAnyErrors();
	}

	bool RunMassiveXYCribChaosScreen(
		FAutomationTestBase& Test,
		const bool bApplyPerturbation)
	{
		using namespace ABTSM73BeamC3V2;
		const double StartSeconds = FPlatformTime::Seconds();
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M7.3-BeamC3V2][ChaosScreen][Start] Perturbation=%d"),
			bApplyPerturbation ? 1 : 0);

		FBeamC3V2PhysicsWorld WorldWrapper;
		if (!WorldWrapper.CreatePhysicsWorld())
		{
			WorldWrapper.ForwardErrorMessages(&Test);
			return false;
		}
		UWorld* World = WorldWrapper.GetTestWorld();
		if (!Test.TestNotNull(TEXT("Beam-C3 V2 physics world"), World))
		{
			return false;
		}
		Test.TestNotNull(TEXT("Beam-C3 V2 physics scene"),
			World->GetPhysicsScene());

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AABTSM71PhysicsTestStage* Stage =
			World->SpawnActorDeferred<AABTSM71PhysicsTestStage>(
				AABTSM71PhysicsTestStage::StaticClass(),
				FTransform::Identity,
				nullptr,
				nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Stage != nullptr)
		{
			UGameplayStatics::FinishSpawningActor(Stage, FTransform::Identity);
		}
		AABTSM7BuildingMaterialSystem* MaterialSystem =
			World->SpawnActor<AABTSM7BuildingMaterialSystem>(
				AABTSM7BuildingMaterialSystem::StaticClass(),
				FTransform::Identity,
				SpawnParameters);
		if (!Test.TestNotNull(TEXT("Planar physics stage"), Stage)
			|| !Test.TestNotNull(TEXT("M7 material system"), MaterialSystem))
		{
			return false;
		}
		if (!WorldWrapper.BeginPlayInTestWorld())
		{
			WorldWrapper.ForwardErrorMessages(&Test);
			return false;
		}

		FMassiveXYCribSettings Settings;
		Test.TestEqual(TEXT("Stage-0 material authority is explicitly Wood"),
			Settings.Material, EABTSM7BuildingMaterial::Wood);
		TArray<FABTSM7MaterialProfile> Profiles;
		MaterialSystem->CopyMaterialProfiles(Profiles);
		const FABTSM7MaterialProfile* WoodProfile =
			FABTSM7MaterialProfileLibrary::FindProfile(
				Profiles, Settings.Material);
		if (!Test.TestNotNull(TEXT("Authoritative Wood profile"), WoodProfile))
		{
			return false;
		}
		UPhysicalMaterial* FloorPhysicalMaterial = NewObject<UPhysicalMaterial>(
			Stage, TEXT("BeamC3V2WoodFloorPhysicalMaterial"), RF_Transient);
		FloorPhysicalMaterial->Friction = WoodProfile->DynamicFriction;
		FloorPhysicalMaterial->StaticFriction = WoodProfile->StaticFriction;
		FloorPhysicalMaterial->Restitution = WoodProfile->Restitution;
		FloorPhysicalMaterial->Density = WoodProfile->DensityGPerCubicCM;
		FloorPhysicalMaterial->bOverrideFrictionCombineMode = true;
		FloorPhysicalMaterial->FrictionCombineMode =
			EFrictionCombineMode::Average;
		FloorPhysicalMaterial->bOverrideRestitutionCombineMode = true;
		FloorPhysicalMaterial->RestitutionCombineMode =
			EFrictionCombineMode::Average;
		Stage->GetFloorComponent()->SetPhysMaterialOverride(FloorPhysicalMaterial);
		FBodyInstance* FloorBody =
			Stage->GetFloorComponent()->GetBodyInstance();
		if (!Test.TestNotNull(TEXT("Stage floor BodyInstance"), FloorBody))
		{
			return false;
		}
		Test.TestTrue(TEXT("Floor uses the requested effective PhysicalMaterial"),
			FloorBody->GetSimplePhysicalMaterial() == FloorPhysicalMaterial);
		Test.TestTrue(TEXT("Floor collision participates in physics"),
			CollisionEnabledHasPhysics(
				Stage->GetFloorComponent()->GetCollisionEnabled()));
		Test.TestEqual(TEXT("Fixture records UE mass-power behavior"),
			FloorPhysicalMaterial->RaiseMassToPower, 0.75f);
		Test.TestTrue(TEXT("Floor friction combine override is enabled"),
			FloorPhysicalMaterial->bOverrideFrictionCombineMode);
		Test.TestEqual(TEXT("Floor friction combine is Average"),
			FloorPhysicalMaterial->FrictionCombineMode,
			EFrictionCombineMode::Average);
		Test.TestTrue(TEXT("Floor restitution combine override is enabled"),
			FloorPhysicalMaterial->bOverrideRestitutionCombineMode);
		Test.TestEqual(TEXT("Floor restitution combine is Average"),
			FloorPhysicalMaterial->RestitutionCombineMode,
			EFrictionCombineMode::Average);

		FABTSM7BrickSpec ProbeSpec;
		ProbeSpec.Material = Settings.Material;
		ProbeSpec.DimensionsCM = FVector(100.0f);
		AABTSM7BuildingModule* GravityProbe =
			MaterialSystem->SpawnBrickModule(
				ProbeSpec,
				FTransform(FVector(7000.0f, 7000.0f, 1500.0f)));
		if (!Test.TestNotNull(TEXT("Custom-gravity probe"), GravityProbe)
			|| !Test.TestNotNull(TEXT("Custom-gravity probe mesh"),
				GravityProbe != nullptr
					? GravityProbe->GetMeshComponent() : nullptr))
		{
			return false;
		}
		GravityProbe->ConfigureChaosSolverIterations(
			PositionSolverIterations, VelocitySolverIterations);
		GravityProbe->SetContactDamageGraceSeconds(MaximumObservationSeconds);
		GravityProbe->ActivateDynamicPlanar(
			FVector::ZeroVector, FVector::UpVector, GravityCMPerSec2);
		FBodyInstance* GravityProbeBody =
			GravityProbe->GetMeshComponent()->GetBodyInstance();
		if (!Test.TestNotNull(TEXT("Custom-gravity probe BodyInstance"),
			GravityProbeBody))
		{
			return false;
		}
		Test.TestTrue(TEXT("Custom-gravity probe is dynamic"),
			GravityProbe->IsDynamic());
		Test.TestTrue(TEXT("Custom-gravity probe Actor Tick is enabled"),
			GravityProbe->IsActorTickEnabled());
		Test.TestFalse(TEXT("Probe disables built-in gravity"),
			GravityProbeBody->bEnableGravity != 0);
		const float ProbeInitialZ = GravityProbe->GetActorLocation().Z;
		for (int32 TickIndex = 0;
			TickIndex < GravityProbeTickCount;
			++TickIndex)
		{
			if (!WorldWrapper.TickTestWorld(FixedDeltaSeconds))
			{
				WorldWrapper.ForwardErrorMessages(&Test);
				return false;
			}
		}
		const float ProbeDropCM =
			ProbeInitialZ - GravityProbe->GetActorLocation().Z;
		const float ProbeVerticalSpeedCMPerSec =
			GravityProbe->GetMeshComponent()->GetPhysicsLinearVelocity().Z;
		Test.TestTrue(*FString::Printf(
			TEXT("Custom gravity drops the probe %.2f cm"), ProbeDropCM),
			ProbeDropCM > MinimumGravityProbeDropCM);
		Test.TestTrue(*FString::Printf(
			TEXT("Custom gravity gives downward velocity %.2f cm/s"),
			ProbeVerticalSpeedCMPerSec),
			ProbeVerticalSpeedCMPerSec
				< -MinimumGravityProbeDownwardSpeedCMPerSec);
		GravityProbe->Destroy();
		if (!WorldWrapper.TickTestWorld(FixedDeltaSeconds))
		{
			WorldWrapper.ForwardErrorMessages(&Test);
			return false;
		}

		FMassiveXYCribResult Prototype;
		FString Error;
		if (!Test.TestTrue(TEXT("Massive XY crib prototype builds"),
			FMassiveXYCribPrototype::Build(Settings, Prototype, Error)))
		{
			Test.AddError(Error);
			return false;
		}
		Test.TestEqual(TEXT("Chaos fixture owns six realized XY pairs"),
			Prototype.PairCount, 6);
		Test.TestEqual(TEXT("Chaos fixture owns twelve realized courses"),
			Prototype.CourseCount, 12);
		Test.TestEqual(TEXT("Chaos fixture owns exactly 24 Bricks"),
			Prototype.Bricks.Num(), 24);
		Test.TestEqual(TEXT("Chaos fixture owns 44 adjacent-course contacts"),
			Prototype.AdjacentCourseContactCount, 44);
		Test.TestEqual(TEXT("Chaos fixture owns the 1296 cm realized height"),
			Prototype.RealizedBodyHeightCM, 1296.0f);
		Test.TestEqual(TEXT("Chaos fixture has the frozen realized-geometry CRC32"),
			Prototype.GeometryCrc32,
			ExpectedMassiveXYCribGeometryCrc32);
		FBox OverallBounds(EForceInit::ForceInit);
		FBox GroundSupportBounds(EForceInit::ForceInit);
		for (const FMassiveXYCribBrick& Brick : Prototype.Bricks)
		{
			OverallBounds += Brick.Bounds;
			if (Brick.CourseIndex == 0)
			{
				GroundSupportBounds += Brick.Bounds;
			}
		}
		Test.TestTrue(TEXT("Chaos fixture overall bounds are 432x432x1296"),
			OverallBounds.GetSize().Equals(
				FVector(432.0f, 432.0f, 1296.0f), KINDA_SMALL_NUMBER));
		Test.TestTrue(TEXT("Chaos fixture ground bearing envelope is 432x396"),
			GroundSupportBounds.GetSize().Equals(
				FVector(432.0f, 396.0f, 108.0f), KINDA_SMALL_NUMBER));

		int32 PerturbedBrickIndex = INDEX_NONE;
		FVector PerturbationVelocityChange = FVector::ZeroVector;
		if (bApplyPerturbation
			&& !Test.TestTrue(TEXT("A top free rail has a fixed outward perturbation"),
				ResolveOutwardTopRailPerturbation(
					Prototype,
					PerturbedBrickIndex,
					PerturbationVelocityChange)))
		{
			return false;
		}
		const uint32 PhysicsFixtureCrc32 = ComputePhysicsFixtureCrc32(
			Settings,
			Prototype,
			*WoodProfile,
			*FloorPhysicalMaterial,
			bApplyPerturbation,
			PerturbedBrickIndex,
			PerturbationVelocityChange);
		Test.TestTrue(TEXT("Physics fixture CRC32 is non-zero"),
			PhysicsFixtureCrc32 != 0);
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M7.3-BeamC3V2][ChaosScreen][Identity] GeometryCrc32=%u FixtureCrc32=%u Material=%d Section=%.0f Length=%.0f Height=%.0f Bricks=%d FPS=%.0f Solver=%d/%d Observation=%.1f PerturbationBrick=%d DeltaV=(%.1f,%.1f,%.1f)"),
			Prototype.GeometryCrc32,
			PhysicsFixtureCrc32,
			static_cast<int32>(Settings.Material),
			FMassiveXYCribSettings::LogSectionCM,
			FMassiveXYCribSettings::LogLengthCM,
			Prototype.RealizedBodyHeightCM,
			Prototype.Bricks.Num(),
			1.0f / FixedDeltaSeconds,
			PositionSolverIterations,
			VelocitySolverIterations,
			MaximumObservationSeconds,
			PerturbedBrickIndex,
			PerturbationVelocityChange.X,
			PerturbationVelocityChange.Y,
			PerturbationVelocityChange.Z);

		TArray<AABTSM7BuildingModule*> Modules;
		Modules.Reserve(Prototype.Bricks.Num());
		const float DamageGraceSeconds = MaximumObservationSeconds
			* (bApplyPerturbation ? 2.0f : 1.0f) + 1.0f;
		for (const FMassiveXYCribBrick& Brick : Prototype.Bricks)
		{
			AABTSM7BuildingModule* Module = MaterialSystem->SpawnBrickModule(
				Brick.MakeBrickSpec(Settings.Material),
				FTransform(Brick.CenterCM));
			if (!Test.TestNotNull(TEXT("Every prototype Brick spawns"), Module))
			{
				return false;
			}
			Module->ConfigureChaosSolverIterations(
				PositionSolverIterations, VelocitySolverIterations);
			Module->SetContactDamageGraceSeconds(DamageGraceSeconds);
			Modules.Add(Module);
		}
		Test.TestEqual(TEXT("One independent module per prototype Brick"),
			Modules.Num(), Prototype.Bricks.Num());

		const FABTSM7PenetrationValidationStats Penetration =
			MaterialSystem->ValidateAndRepairPendingModules(Modules);
		Test.TestEqual(TEXT("Exact crib starts without detected penetration"),
			Penetration.DetectedPairCount, 0);
		Test.TestEqual(TEXT("Exact crib requires no penetration repair"),
			Penetration.RepairCount, 0);
		Test.TestEqual(TEXT("Exact crib has no large penetration error"),
			Penetration.LargeErrorPairCount, 0);
		Test.TestEqual(TEXT("Exact crib leaves no small penetration error"),
			Penetration.RemainingSmallPairCount, 0);
		if (Penetration.DetectedPairCount != 0
			|| Penetration.RepairCount != 0
			|| Penetration.LargeErrorPairCount != 0
			|| Penetration.RemainingSmallPairCount != 0)
		{
			return false;
		}

		int32 ConstraintActorCount = 0;
		for (TActorIterator<APhysicsConstraintActor> It(World); It; ++It)
		{
			++ConstraintActorCount;
		}
		Test.TestEqual(TEXT("Fixture creates no PhysicsConstraint Actor"),
			ConstraintActorCount, 0);
		TSet<const FBodyInstance*> UniqueBodies;
		TArray<FTransform> InitialTransforms;
		InitialTransforms.Reserve(Modules.Num());
		for (int32 ModuleIndex = 0;
			ModuleIndex < Modules.Num();
			++ModuleIndex)
		{
			AABTSM7BuildingModule* Module = Modules[ModuleIndex];
			if (!Test.TestNotNull(TEXT("Prototype module remains valid"), Module)
				|| !Test.TestNotNull(TEXT("Prototype module root component"),
					Module != nullptr ? Module->GetRootComponent() : nullptr)
				|| !Test.TestNotNull(TEXT("Prototype module mesh component"),
					Module != nullptr ? Module->GetMeshComponent() : nullptr))
			{
				return false;
			}
			UStaticMeshComponent* Mesh = Module->GetMeshComponent();
			FBodyInstance* BodyInstance = Mesh->GetBodyInstance();
			if (!Test.TestNotNull(TEXT("Prototype module BodyInstance"),
				BodyInstance))
			{
				return false;
			}
			const FMassiveXYCribBrick& ExpectedBrick =
				Prototype.Bricks[ModuleIndex];
			Test.TestTrue(TEXT("Brick Actor has no Attach Parent"),
				Module->GetAttachParentActor() == nullptr);
			Test.TestTrue(TEXT("Brick root component has no Attach Parent"),
				Module->GetRootComponent()->GetAttachParent() == nullptr);
			Test.TestTrue(TEXT("Brick has no PhysicsConstraint component"),
				Module->FindComponentByClass<UPhysicsConstraintComponent>()
					== nullptr);
			Test.TestTrue(TEXT("Rendered Brick matches prototype bounds"),
				Mesh->CalcBounds(Mesh->GetComponentTransform()).GetBox().Equals(
					ExpectedBrick.Bounds, BoundsToleranceCM));
			UniqueBodies.Add(BodyInstance);
			InitialTransforms.Add(Module->GetActorTransform());
			Module->ActivateDynamicPlanar(
				FVector::ZeroVector, FVector::UpVector, GravityCMPerSec2);
			if (!Test.TestTrue(TEXT("Brick physics actor is valid"),
				BodyInstance->IsValidBodyInstance()))
			{
				return false;
			}
			Test.TestTrue(TEXT("Brick is marked dynamic"), Module->IsDynamic());
			Test.TestTrue(TEXT("Brick Actor Tick is enabled for custom gravity"),
				Module->IsActorTickEnabled());
			Test.TestFalse(TEXT("Brick disables built-in gravity"),
				BodyInstance->bEnableGravity != 0);
			Test.TestTrue(TEXT("Brick collision participates in simulation"),
				CollisionEnabledHasPhysics(Mesh->GetCollisionEnabled()));
			Test.TestTrue(TEXT("Brick remains an active independent rigid body"),
				Mesh->IsSimulatingPhysics());
			Test.TestFalse(TEXT("Brick does not override mass"),
				BodyInstance->bOverrideMass != 0);
			Test.TestEqual(TEXT("Brick mass scale remains one"),
				BodyInstance->MassScale, 1.0f);
			Test.TestEqual(TEXT("Per-body position solver override is effective"),
				BodyInstance->GetPositionSolverIterationCount(),
				PositionSolverIterations);
			Test.TestEqual(TEXT("Per-body velocity solver override is effective"),
				BodyInstance->GetVelocitySolverIterationCount(),
				VelocitySolverIterations);
			Test.TestTrue(TEXT("Chaos Brick matches prototype world bounds"),
				BodyInstance->GetBodyBounds().Equals(
					ExpectedBrick.Bounds, BoundsToleranceCM));
			Test.TestTrue(TEXT("Chaos local shape has authored dimensions"),
				BodyInstance->GetBodyBoundsLocal().GetSize().Equals(
					ExpectedBrick.DimensionsCM, BoundsToleranceCM));
			UPhysicalMaterial* PhysicalMaterial =
				BodyInstance->GetSimplePhysicalMaterial();
			if (Test.TestNotNull(TEXT("Brick has a real PhysicalMaterial"),
				PhysicalMaterial))
			{
				Test.TestEqual(TEXT("Brick dynamic friction matches Wood"),
					PhysicalMaterial->Friction, WoodProfile->DynamicFriction);
				Test.TestEqual(TEXT("Brick static friction matches Wood"),
					PhysicalMaterial->StaticFriction, WoodProfile->StaticFriction);
				Test.TestEqual(TEXT("Brick restitution matches Wood"),
					PhysicalMaterial->Restitution, WoodProfile->Restitution);
				Test.TestEqual(TEXT("Brick density matches Wood"),
					PhysicalMaterial->Density, WoodProfile->DensityGPerCubicCM);
				Test.TestEqual(TEXT("Brick friction combine is explicit Average"),
					PhysicalMaterial->FrictionCombineMode,
					EFrictionCombineMode::Average);
				Test.TestTrue(TEXT("Brick friction combine override is enabled"),
					PhysicalMaterial->bOverrideFrictionCombineMode);
				Test.TestEqual(TEXT("Brick restitution combine is explicit Average"),
					PhysicalMaterial->RestitutionCombineMode,
					EFrictionCombineMode::Average);
				Test.TestTrue(TEXT("Brick restitution combine override is enabled"),
					PhysicalMaterial->bOverrideRestitutionCombineMode);
				Test.TestEqual(TEXT("Brick mass power matches recorded UE behavior"),
					PhysicalMaterial->RaiseMassToPower, 0.75f);
			}
			UBodySetup* BodySetup = BodyInstance->GetBodySetup();
			if (!Test.TestNotNull(TEXT("Brick BodySetup"), BodySetup))
			{
				return false;
			}
			const float ActualMassKG = BodyInstance->GetBodyMass();
			const float CalculatedMassKG = BodySetup->CalculateMass(Mesh);
			const float CalculatedMassToleranceKG =
				FMath::Max(0.05f, CalculatedMassKG * 0.01f);
			Test.TestTrue(TEXT("Generated Brick mass is finite"),
				FMath::IsFinite(ActualMassKG));
			Test.TestTrue(*FString::Printf(
				TEXT("Body mass %.3f kg matches generated %.3f kg"),
				ActualMassKG,
				CalculatedMassKG),
				FMath::IsNearlyEqual(
					ActualMassKG,
					CalculatedMassKG,
					CalculatedMassToleranceKG));
			Test.TestTrue(*FString::Printf(
				TEXT("Wood log UE mass %.3f kg matches frozen fixture %.3f kg"),
				ActualMassKG,
				ExpectedWoodLogMassKG),
				FMath::IsNearlyEqual(
					ActualMassKG,
					ExpectedWoodLogMassKG,
					ExpectedWoodLogMassToleranceKG));
		}
		Test.TestEqual(TEXT("Every Brick owns a distinct BodyInstance"),
			UniqueBodies.Num(), Modules.Num());

		FObservationResult IdleObservation;
		if (!ObserveUntilQuiet(
			Test,
			WorldWrapper,
			Modules,
			InitialTransforms,
			TEXT("Idle"),
			INDEX_NONE,
			FVector::ZeroVector,
			IdleObservation))
		{
			return false;
		}

		if (bApplyPerturbation)
		{
			TArray<FTransform> PerturbationBaselines;
			PerturbationBaselines.Reserve(Modules.Num());
			for (AABTSM7BuildingModule* Module : Modules)
			{
				PerturbationBaselines.Add(Module->GetActorTransform());
			}

			if (!Test.TestTrue(TEXT("Registered perturbation Brick is in range"),
				Modules.IsValidIndex(PerturbedBrickIndex)))
			{
				return false;
			}
			UStaticMeshComponent* PerturbedMesh =
				Modules[PerturbedBrickIndex]->GetMeshComponent();
			if (!Test.TestNotNull(TEXT("Registered perturbation mesh"),
				PerturbedMesh))
			{
				return false;
			}
			const FVector VelocityBefore =
				PerturbedMesh->GetPhysicsLinearVelocity();
			Test.TestTrue(TEXT("Outward perturbation starts from the terminal quiet state"),
				VelocityBefore.Size() <= MaximumLinearSpeedCMPerSec);
			FBodyInstance* PerturbedBody = PerturbedMesh->GetBodyInstance();
			if (!Test.TestNotNull(TEXT("Registered perturbation BodyInstance"),
				PerturbedBody))
			{
				return false;
			}
			PerturbedBody->WakeInstance();
			Test.TestTrue(TEXT("Registered perturbation body is awake"),
				PerturbedBody->IsInstanceAwake());
			PerturbedMesh->AddImpulse(
				PerturbationVelocityChange, NAME_None, true);
			const FVector QueuedVelocityReadback =
				PerturbedMesh->GetPhysicsLinearVelocity();
			UE_LOG(LogABTSRuntime, Log,
				TEXT("[ABTS][M7.3-BeamC3V2][ChaosScreen][PerturbationQueued] Brick=%d Course=%d Axis=%d Rail=%d VelocityBefore=(%.3f,%.3f,%.3f) RequestedDeltaV=(%.3f,%.3f,%.3f) ImmediateReadback=(%.3f,%.3f,%.3f)"),
				PerturbedBrickIndex,
				Prototype.Bricks[PerturbedBrickIndex].CourseIndex,
				static_cast<int32>(
					Prototype.Bricks[PerturbedBrickIndex].Axis),
				Prototype.Bricks[PerturbedBrickIndex].RailIndex,
				VelocityBefore.X,
				VelocityBefore.Y,
				VelocityBefore.Z,
				PerturbationVelocityChange.X,
				PerturbationVelocityChange.Y,
				PerturbationVelocityChange.Z,
				QueuedVelocityReadback.X,
				QueuedVelocityReadback.Y,
				QueuedVelocityReadback.Z);

			FObservationResult PerturbationObservation;
			const FString PerturbationPhase = FString::Printf(
				TEXT("OutwardTopRailPerturbation%.0fCMPerSec"),
				PerturbationVelocityCMPerSec);
			if (!ObserveUntilQuiet(
				Test,
				WorldWrapper,
				Modules,
				PerturbationBaselines,
				*PerturbationPhase,
				PerturbedBrickIndex,
				PerturbationVelocityChange,
				PerturbationObservation))
			{
				return false;
			}
			Test.TestTrue(*FString::Printf(
				TEXT("First physics frame moves the selected rail outward %.3f cm"),
				PerturbationObservation.FirstResponseOutwardDisplacementCM),
				PerturbationObservation.FirstResponseOutwardDisplacementCM
					> MinimumOutwardResponseDisplacementCM);
			Test.TestTrue(*FString::Printf(
				TEXT("First physics frame gives the selected rail outward speed %.3f cm/s"),
				PerturbationObservation.FirstResponseOutwardVelocityCMPerSec),
				PerturbationObservation.FirstResponseOutwardVelocityCMPerSec
					> MinimumOutwardResponseSpeedCMPerSec);
			if (Test.HasAnyErrors())
			{
				return false;
			}
		}

		for (AABTSM7BuildingModule* Module : Modules)
		{
			if (!Test.TestTrue(TEXT("Every Brick survives the complete screen"),
				IsValid(Module))
				|| !Test.TestNotNull(TEXT("Every surviving Brick retains its mesh"),
					IsValid(Module) ? Module->GetMeshComponent() : nullptr))
			{
				return false;
			}
			Test.TestTrue(TEXT("Screen never freezes accepted Brick bodies"),
				Module->GetMeshComponent()->IsSimulatingPhysics());
		}
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M7.3-BeamC3V2][ChaosScreen][Complete] Perturbation=%d GeometryCrc32=%u FixtureCrc32=%u Bricks=%d Seconds=%.3f"),
			bApplyPerturbation ? 1 : 0,
			Prototype.GeometryCrc32,
			PhysicsFixtureCrc32,
			Modules.Num(),
			FPlatformTime::Seconds() - StartSeconds);
		WorldWrapper.ForwardErrorMessages(&Test);
		return !Test.HasAnyErrors();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3V2MassiveXYCribChaosIdleTest,
	"ABTS.M73DAG.BeamC3V2.MassiveXYCrib.ChaosIdle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3V2MassiveXYCribChaosIdleTest::RunTest(
	const FString& Parameters)
{
	return ABTSM73BeamC3V2ChaosTests::RunMassiveXYCribChaosScreen(
		*this, false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3V2MassiveXYCribChaosPerturbationTest,
	"ABTS.M73DAG.BeamC3V2.MassiveXYCrib.ChaosPerturbation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3V2MassiveXYCribChaosPerturbationTest::RunTest(
	const FString& Parameters)
{
	return ABTSM73BeamC3V2ChaosTests::RunMassiveXYCribChaosScreen(
		*this, true);
}

#endif
