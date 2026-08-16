// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/ABTSStylizedRenderingWorldSubsystem.h"

#include "Components/ExponentialHeightFogComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RotationMatrix.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProceduralMeshComponent.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Building/ABTSM7BuildingModule.h"
#include "Building/ABTSM7StylizedRenderingAdapter.h"
#include "Camera/ABTSM101LandingPreviewCamera.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "Party/ABTSBirdParty.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "Presentation/ABTSOpeningCinematicPreview.h"
#include "Rendering/ABTSSharedStylizedMaterialAdapter.h"
#include "Rendering/ABTSStylizedRenderingControl.h"
#include "Rendering/ABTST4LowPolyCloudPrototype.h"
#include "Rendering/ABTSStylizedMaterialContract.h"
#include "Rendering/ABTSStylizedMaterialOverrideRegistry.h"
#include "Rendering/ABTSStylizedRenderingTypes.h"
#include "Rendering/ABTSStylizedSceneCaptureRegistry.h"
#include "Slingshot/ABTSM6SlingshotSystem.h"
#include "Terrain/ABTSM3Planet.h"
#include "Terrain/ABTSM3StylizedMaterialAdapter.h"
#include "Terrain/ABTSM3StylizedSemanticAdapter.h"
#include "World/ABTSM10ScoutMapSystem.h"
#include "World/ABTSM11FinaleActors.h"
#include "World/ABTSM11FinaleInteractionSystem.h"
#include "World/ABTSM11FinaleSystem.h"
#include "World/ABTSM11StylizedMaterialAdapter.h"
#include "PCG/ABTSM3MonthlySatellitePracticeRuntime.h"
#include "World/ABTSM9Satellite.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/StaticMeshComponent.h"
#include "Misc/AutomationTest.h"
#endif

namespace ABTSStylizedRenderingWorldSubsystemPrivate
{
	constexpr float RefreshIntervalSeconds = 0.10f;
	constexpr float ContinuousAtmosphereTraceSampleCountScale = 2.0f;

	struct FFinaleEnvironmentSourceCandidate
	{
		TWeakObjectPtr<AABTSM11FinaleInteractionSystem> Source;
		FString StableKey;
		uint64 SourceIdentityHash = 0;
		EABTSM11FinaleEnvironmentStage Stage =
			EABTSM11FinaleEnvironmentStage::GroundLaunch;
		bool bInitialized = false;
		bool bEnvironmentRelevant = false;
	};

	struct FResolvedFinaleEnvironmentSource
	{
		TWeakObjectPtr<AABTSM11FinaleInteractionSystem> Source;
		EABTSM11FinaleEnvironmentStage Stage =
			EABTSM11FinaleEnvironmentStage::GroundLaunch;
		uint64 SourceIdentityHash = 0;
		uint64 CandidateSetHash = 0;
		int32 RelevantSourceCount = 0;
		bool bConflict = false;

		bool HasUniqueSource() const
		{
			return RelevantSourceCount == 1
				&& !bConflict
				&& SourceIdentityHash != 0;
		}
	};

	FResolvedFinaleEnvironmentSource ResolveFinaleEnvironmentCandidates(
		TArray<FFinaleEnvironmentSourceCandidate> Candidates)
	{
		FResolvedFinaleEnvironmentSource Result;
		Candidates.RemoveAll([](const FFinaleEnvironmentSourceCandidate& Candidate)
		{
			return !Candidate.bInitialized
				|| !Candidate.bEnvironmentRelevant
				|| Candidate.SourceIdentityHash == 0;
		});
		Candidates.Sort([](
			const FFinaleEnvironmentSourceCandidate& Left,
			const FFinaleEnvironmentSourceCandidate& Right)
		{
			if (Left.StableKey != Right.StableKey)
			{
				return Left.StableKey < Right.StableKey;
			}
			return Left.SourceIdentityHash < Right.SourceIdentityHash;
		});
		Result.RelevantSourceCount = Candidates.Num();
		for (const FFinaleEnvironmentSourceCandidate& Candidate : Candidates)
		{
			Result.CandidateSetHash = HashCombineFast(
				Result.CandidateSetHash,
				Candidate.SourceIdentityHash);
			Result.CandidateSetHash = HashCombineFast(
				Result.CandidateSetHash,
				GetTypeHash(static_cast<uint8>(Candidate.Stage)));
		}
		if (Candidates.Num() == 1)
		{
			Result.Source = Candidates[0].Source;
			Result.Stage = Candidates[0].Stage;
			Result.SourceIdentityHash = Candidates[0].SourceIdentityHash;
		}
		else if (Candidates.Num() > 1)
		{
			// Ambiguous sources are never merged.  A stale DeepSpace actor must
			// not override a live ground source merely because iterator order or
			// stage priority happens to favour it.
			Result.bConflict = true;
			Result.Stage = EABTSM11FinaleEnvironmentStage::GroundLaunch;
		}
		return Result;
	}

	FResolvedFinaleEnvironmentSource ResolveFinaleEnvironmentSource(
		UWorld& World)
	{
		TArray<FFinaleEnvironmentSourceCandidate> Candidates;
		for (TActorIterator<AABTSM11FinaleInteractionSystem> It(&World); It; ++It)
		{
			AABTSM11FinaleInteractionSystem* Interaction = *It;
			if (!IsValid(Interaction) || Interaction->IsActorBeingDestroyed())
			{
				continue;
			}
			const AABTSM11FinaleSystem* FinaleSystem =
				Interaction->GetFinaleSystem();
			FFinaleEnvironmentSourceCandidate& Candidate =
				Candidates.AddDefaulted_GetRef();
			Candidate.Source = Interaction;
			Candidate.StableKey = Interaction->GetPathName();
			Candidate.SourceIdentityHash = HashCombineFast(
				GetTypeHash(Candidate.StableKey),
				GetTypeHash(GetPathNameSafe(FinaleSystem)));
			if (Candidate.SourceIdentityHash == 0)
			{
				Candidate.SourceIdentityHash = 1;
			}
			Candidate.Stage = Interaction->GetFinaleEnvironmentStage();
			Candidate.bInitialized = IsValid(FinaleSystem)
				&& Interaction->GetInteractionState()
					!= EABTSM11FinaleInteractionState::Locked;
			Candidate.bEnvironmentRelevant = Candidate.bInitialized
				&& (Interaction->IsFinaleActive()
					|| Candidate.Stage
						!= EABTSM11FinaleEnvironmentStage::GroundLaunch);
		}
		return ResolveFinaleEnvironmentCandidates(MoveTemp(Candidates));
	}

	bool DoesFinaleEnvironmentStageRequireSpace(
		const EABTSM11FinaleEnvironmentStage Stage,
		const bool bAtmosphereTransitionComplete = false)
	{
		return Stage == EABTSM11FinaleEnvironmentStage::DeepSpace
			|| (Stage == EABTSM11FinaleEnvironmentStage::AtmosphereTransition
				&& bAtmosphereTransitionComplete);
	}

	float ResolveFinaleHighAltitudeSpaceBlend(
		UWorld& World,
		const FABTSToonEnvironmentSnapshot& Environment)
	{
		APlayerController* Controller = World.GetFirstPlayerController();
		APlayerCameraManager* CameraManager = IsValid(Controller)
			? Controller->PlayerCameraManager
			: nullptr;
		if (!Environment.IsValid() || !IsValid(CameraManager))
		{
			return 0.0f;
		}
		const FABTSStylizedEnvironmentParameters Parameters =
			FABTSStylizedRenderingControl::BuildEnvironmentParameters(
				Environment.PlanetCenterWorld,
				Environment.PlanetRadiusCM,
				Environment.SunDirectionToSunWorld,
				EABTSStylizedRenderProfile::GroundDay);
		const float CameraAltitudeCM = static_cast<float>(FMath::Max(
			FVector::Distance(
				CameraManager->GetCameraLocation(),
				Environment.PlanetCenterWorld)
				- Environment.PlanetRadiusCM,
			0.0));
		return FABTSStylizedRenderingControl::ComputeHighAltitudeSpaceBlend(
			CameraAltitudeCM,
			Parameters.HighAltitudeTransitionStartCM,
			Parameters.HighAltitudeTransitionEndCM);
	}

	const TCHAR* LexToString(const EABTSM11FinaleEnvironmentStage Stage)
	{
		switch (Stage)
		{
		case EABTSM11FinaleEnvironmentStage::GroundLaunch:
			return TEXT("GroundLaunch");
		case EABTSM11FinaleEnvironmentStage::AtmosphereTransition:
			return TEXT("AtmosphereTransition");
		case EABTSM11FinaleEnvironmentStage::DeepSpace:
			return TEXT("DeepSpace");
		case EABTSM11FinaleEnvironmentStage::Recovering:
			return TEXT("Recovering");
		default:
			return TEXT("Unknown");
		}
	}

	UABTSStylizedRenderingWorldSubsystem* ResolveTuningSubsystem(UWorld* World)
	{
		return World != nullptr
			? World->GetSubsystem<UABTSStylizedRenderingWorldSubsystem>()
			: nullptr;
	}

	void LogCloudTuningUsage(const TCHAR* Command)
	{
		UE_LOG(
			LogABTSRuntime,
			Warning,
			TEXT("[ABTS][Rendering][T4-A2.4][CloudTuning] Command=%s Accepted=0 Usage=\"ABTS.Toon.CloudField.SetDistribution <ClusterCount:1..64> <Mean:1..64> <Variance:0..1024> <Seed:uint32>\""),
			Command);
	}

	void SetCloudDistributionCommand(
		const TArray<FString>& Args,
		UWorld* World)
	{
		int32 ClusterCount = 0;
		float Mean = 0.0f;
		float Variance = 0.0f;
		uint32 Seed = 0;
		if (Args.Num() != 4
			|| !LexTryParseString(ClusterCount, *Args[0])
			|| !LexTryParseString(Mean, *Args[1])
			|| !LexTryParseString(Variance, *Args[2])
			|| !LexTryParseString(Seed, *Args[3]))
		{
			LogCloudTuningUsage(TEXT("SetDistribution"));
			return;
		}
		UABTSStylizedRenderingWorldSubsystem* Subsystem =
			ResolveTuningSubsystem(World);
		FString Failure;
		if (Subsystem == nullptr
			|| !Subsystem->ApplyCloudFieldTuningOverride(
				ClusterCount, Mean, Variance, Seed, Failure))
		{
			UE_LOG(LogABTSRuntime, Warning,
				TEXT("[ABTS][Rendering][T4-A2.4][CloudTuning] Command=SetDistribution Accepted=0 Reason=%s"),
				Failure.IsEmpty() ? TEXT("SubsystemUnavailable") : *Failure);
		}
	}

	void SetCloudClusterCountCommand(const TArray<FString>& Args, UWorld* World)
	{
		int32 ClusterCount = 0;
		UABTSStylizedRenderingWorldSubsystem* Subsystem =
			ResolveTuningSubsystem(World);
		if (Args.Num() != 1 || !LexTryParseString(ClusterCount, *Args[0])
			|| Subsystem == nullptr)
		{
			LogCloudTuningUsage(TEXT("SetClusterCount"));
			return;
		}
		const FABTST4CloudFieldTuningState Current =
			Subsystem->GetCloudFieldTuningState();
		FString Failure;
		if (!Subsystem->ApplyCloudFieldTuningOverride(
			ClusterCount,
			Current.Distribution.CloudsPerClusterMean,
			Current.Distribution.CloudsPerClusterVariance,
			Current.Seed,
			Failure))
		{
			UE_LOG(LogABTSRuntime, Warning,
				TEXT("[ABTS][Rendering][T4-A2.4][CloudTuning] Command=SetClusterCount Accepted=0 Reason=%s"),
				*Failure);
		}
	}

	void SetCloudMeanCommand(const TArray<FString>& Args, UWorld* World)
	{
		float Mean = 0.0f;
		UABTSStylizedRenderingWorldSubsystem* Subsystem =
			ResolveTuningSubsystem(World);
		if (Args.Num() != 1 || !LexTryParseString(Mean, *Args[0])
			|| Subsystem == nullptr)
		{
			LogCloudTuningUsage(TEXT("SetMean"));
			return;
		}
		const FABTST4CloudFieldTuningState Current =
			Subsystem->GetCloudFieldTuningState();
		FString Failure;
		if (!Subsystem->ApplyCloudFieldTuningOverride(
			Current.Distribution.ClusterCount,
			Mean,
			Current.Distribution.CloudsPerClusterVariance,
			Current.Seed,
			Failure))
		{
			UE_LOG(LogABTSRuntime, Warning,
				TEXT("[ABTS][Rendering][T4-A2.4][CloudTuning] Command=SetMean Accepted=0 Reason=%s"),
				*Failure);
		}
	}

	void SetCloudVarianceCommand(const TArray<FString>& Args, UWorld* World)
	{
		float Variance = 0.0f;
		UABTSStylizedRenderingWorldSubsystem* Subsystem =
			ResolveTuningSubsystem(World);
		if (Args.Num() != 1 || !LexTryParseString(Variance, *Args[0])
			|| Subsystem == nullptr)
		{
			LogCloudTuningUsage(TEXT("SetVariance"));
			return;
		}
		const FABTST4CloudFieldTuningState Current =
			Subsystem->GetCloudFieldTuningState();
		FString Failure;
		if (!Subsystem->ApplyCloudFieldTuningOverride(
			Current.Distribution.ClusterCount,
			Current.Distribution.CloudsPerClusterMean,
			Variance,
			Current.Seed,
			Failure))
		{
			UE_LOG(LogABTSRuntime, Warning,
				TEXT("[ABTS][Rendering][T4-A2.4][CloudTuning] Command=SetVariance Accepted=0 Reason=%s"),
				*Failure);
		}
	}

	void SetCloudSeedCommand(const TArray<FString>& Args, UWorld* World)
	{
		uint32 Seed = 0;
		UABTSStylizedRenderingWorldSubsystem* Subsystem =
			ResolveTuningSubsystem(World);
		if (Args.Num() != 1 || !LexTryParseString(Seed, *Args[0])
			|| Subsystem == nullptr)
		{
			LogCloudTuningUsage(TEXT("SetSeed"));
			return;
		}
		const FABTST4CloudFieldTuningState Current =
			Subsystem->GetCloudFieldTuningState();
		FString Failure;
		if (!Subsystem->ApplyCloudFieldTuningOverride(
			Current.Distribution.ClusterCount,
			Current.Distribution.CloudsPerClusterMean,
			Current.Distribution.CloudsPerClusterVariance,
			Seed,
			Failure))
		{
			UE_LOG(LogABTSRuntime, Warning,
				TEXT("[ABTS][Rendering][T4-A2.4][CloudTuning] Command=SetSeed Accepted=0 Reason=%s"),
				*Failure);
		}
	}

	void ClearCloudDistributionCommand(const TArray<FString>&, UWorld* World)
	{
		if (UABTSStylizedRenderingWorldSubsystem* Subsystem =
			ResolveTuningSubsystem(World))
		{
			Subsystem->ClearCloudFieldTuningOverride();
		}
	}

	void CloudDistributionStatusCommand(const TArray<FString>&, UWorld* World)
	{
		if (UABTSStylizedRenderingWorldSubsystem* Subsystem =
			ResolveTuningSubsystem(World))
		{
			const FABTST4CloudFieldTuningState State =
				Subsystem->GetCloudFieldTuningState();
			const TArray<int32> Counts = FABTST4LowPolyCloudPrototype::
				BuildGlobalClusterMemberCounts(State.Seed, State.Distribution);
			FString CountText;
			int32 BackgroundLogicalClouds = 0;
			for (const int32 Count : Counts)
			{
				BackgroundLogicalClouds += Count;
				CountText += CountText.IsEmpty()
					? FString::FromInt(Count)
					: FString::Printf(TEXT(",%d"), Count);
			}
			UE_LOG(LogABTSRuntime, Log,
				TEXT("[ABTS][Rendering][T4-A2.4][CloudTuning] Status Override=%d ClusterCount=%d Mean=%.3f Variance=%.3f Seed=%u BackgroundLogicalClouds=%d TotalLogicalClouds=%d Cloudlets=%d Members=[%s]"),
				State.bOverrideActive ? 1 : 0,
				State.Distribution.ClusterCount,
				State.Distribution.CloudsPerClusterMean,
				State.Distribution.CloudsPerClusterVariance,
				State.Seed,
				BackgroundLogicalClouds,
				BackgroundLogicalClouds
					+ FABTST4LowPolyCloudPrototype::
						TerminatorMegaClusterIslandCount,
				(BackgroundLogicalClouds
					+ FABTST4LowPolyCloudPrototype::
						TerminatorMegaClusterIslandCount)
					* FABTST4LowPolyCloudPrototype::CloudletsPerIsland,
				*CountText);
		}
	}

	void CloudOverviewCommand(const TArray<FString>&, UWorld* World)
	{
		UABTSStylizedRenderingWorldSubsystem* Subsystem =
			ResolveTuningSubsystem(World);
		FString Failure;
		if (Subsystem == nullptr || !Subsystem->EnterCloudFieldOverview(Failure))
		{
			UE_LOG(LogABTSRuntime, Warning,
				TEXT("[ABTS][Rendering][T4-A2.4][CloudOverview] Active=0 Reason=%s"),
				Failure.IsEmpty() ? TEXT("SubsystemUnavailable") : *Failure);
		}
	}

	void CloudOverviewRestoreCommand(const TArray<FString>&, UWorld* World)
	{
		UABTSStylizedRenderingWorldSubsystem* Subsystem =
			ResolveTuningSubsystem(World);
		FString Failure;
		if (Subsystem == nullptr || !Subsystem->ExitCloudFieldOverview(Failure))
		{
			UE_LOG(LogABTSRuntime, Warning,
				TEXT("[ABTS][Rendering][T4-A2.4][CloudOverview] Restore=0 Reason=%s"),
				Failure.IsEmpty() ? TEXT("SubsystemUnavailable") : *Failure);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs GSetCloudDistributionCommand(
		TEXT("ABTS.Toon.CloudField.SetDistribution"),
		TEXT("PIE only. Args: cluster-count mean variance seed."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
			&SetCloudDistributionCommand));
	FAutoConsoleCommandWithWorldAndArgs GSetCloudClusterCountCommand(
		TEXT("ABTS.Toon.CloudField.SetClusterCount"),
		TEXT("PIE only. Changes the explicit global weather-cluster count and rebuilds."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
			&SetCloudClusterCountCommand));
	FAutoConsoleCommandWithWorldAndArgs GSetCloudMeanCommand(
		TEXT("ABTS.Toon.CloudField.SetMean"),
		TEXT("PIE only. Changes cloud members-per-cluster mean and rebuilds."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetCloudMeanCommand));
	FAutoConsoleCommandWithWorldAndArgs GSetCloudVarianceCommand(
		TEXT("ABTS.Toon.CloudField.SetVariance"),
		TEXT("PIE only. Changes cloud members-per-cluster variance and rebuilds."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
			&SetCloudVarianceCommand));
	FAutoConsoleCommandWithWorldAndArgs GSetCloudSeedCommand(
		TEXT("ABTS.Toon.CloudField.SetSeed"),
		TEXT("PIE only. Changes the deterministic cloud-field seed and rebuilds."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetCloudSeedCommand));
	FAutoConsoleCommandWithWorldAndArgs GClearCloudDistributionCommand(
		TEXT("ABTS.Toon.CloudField.ClearDistribution"),
		TEXT("PIE only. Restores the frozen production cloud layout."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
			&ClearCloudDistributionCommand));
	FAutoConsoleCommandWithWorldAndArgs GCloudDistributionStatusCommand(
		TEXT("ABTS.Toon.CloudField.Status"),
		TEXT("Logs active A2.4 distribution and deterministic member counts."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
			&CloudDistributionStatusCommand));
	FAutoConsoleCommandWithWorldAndArgs GCloudOverviewCommand(
		TEXT("ABTS.Toon.CloudField.Overview"),
		TEXT("PIE only. Frames the whole planet and cloud field from radial north."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&CloudOverviewCommand));
	FAutoConsoleCommandWithWorldAndArgs GCloudOverviewRestoreCommand(
		TEXT("ABTS.Toon.CloudField.RestoreView"),
		TEXT("PIE only. Restores the view target saved by CloudField.Overview."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
			&CloudOverviewRestoreCommand));

	/**
	 * UE's sky defaults target an Earth-scale atmosphere. ABTS uses a five-
	 * kilometre planet with roughly three kilometres of atmosphere, so the
	 * variable ray-march count remains near its two-sample minimum across most
	 * gameplay paths. Conversely, forcing the full-resolution sky path exposes
	 * large camera-space integration tiles at UE's 0.1 km minimum planet radius.
	 * Use a higher-resolution, fixed-sample SkyView LUT for the background, keep
	 * full-precision supporting LUTs, and restore every process-wide CVar after
	 * the last stylized world. SDR sky quantization is handled by the final
	 * stylized Tone pass, where the actual background/depth identity is known;
	 * it must not be applied here as a process-wide mesh/PIP override.
	 */
	class FContinuousAtmosphereOverride
	{
	public:
		static bool Acquire(FString& OutFailure)
		{
			OutFailure.Reset();
			if (ReferenceCount > 0)
			{
				++ReferenceCount;
				return true;
			}

			Overrides = {
				{ TEXT("r.SkyAtmosphere.FastSkyLUT"), 1.0f },
				{ TEXT("r.SkyAtmosphere.FastSkyLUT.Width"), 384.0f },
				{ TEXT("r.SkyAtmosphere.FastSkyLUT.Height"), 208.0f },
				{ TEXT("r.SkyAtmosphere.FastSkyLUT.SampleCountMin"), 16.0f },
				{ TEXT("r.SkyAtmosphere.FastSkyLUT.SampleCountMax"), 32.0f },
				{ TEXT("r.SkyAtmosphere.FastSkyLUT.DistanceToSampleCountMax"), 0.01f },
				{ TEXT("r.SkyAtmosphere.AerialPerspectiveLUT.FastApplyOnOpaque"), 0.0f },
				{ TEXT("r.SkyAtmosphere.SampleCountMin"), 16.0f },
				{ TEXT("r.SkyAtmosphere.SampleCountMax"), 32.0f },
				{ TEXT("r.SkyAtmosphere.DistanceToSampleCountMax"), 0.01f },
				{ TEXT("r.SkyAtmosphere.LUT32"), 1.0f },
				{ TEXT("r.SkyAtmosphere.TransmittanceLUT.SampleCount"), 32.0f },
				{ TEXT("r.SkyAtmosphere.MultiScatteringLUT.HighQuality"), 1.0f }
			};
			FString BandingExperiment;
			FParse::Value(
				FCommandLine::Get(),
				TEXT("ABTSToonSkyBandingExperiment="),
				BandingExperiment);
			if (!BandingExperiment.IsEmpty()
				&& !BandingExperiment.Equals(
					TEXT("Control"),
					ESearchCase::IgnoreCase))
			{
				auto SetDesiredValue = [](const TCHAR* Name, const float Value)
				{
					FCVarOverride* Override = Overrides.FindByPredicate(
						[Name](const FCVarOverride& Candidate)
						{
							return FCString::Stricmp(Candidate.Name, Name) == 0;
						});
					check(Override != nullptr);
					Override->DesiredValue = Value;
				};

				if (BandingExperiment.Equals(
					TEXT("HighResolution"),
					ESearchCase::IgnoreCase))
				{
					SetDesiredValue(
						TEXT("r.SkyAtmosphere.FastSkyLUT.Width"),
						768.0f);
					SetDesiredValue(
						TEXT("r.SkyAtmosphere.FastSkyLUT.Height"),
						416.0f);
				}
				else if (BandingExperiment.Equals(
					TEXT("HighSamples"),
					ESearchCase::IgnoreCase))
				{
					SetDesiredValue(
						TEXT("r.SkyAtmosphere.FastSkyLUT.SampleCountMin"),
						64.0f);
					SetDesiredValue(
						TEXT("r.SkyAtmosphere.FastSkyLUT.SampleCountMax"),
						128.0f);
				}
				else if (BandingExperiment.Equals(
					TEXT("PerPixel"),
					ESearchCase::IgnoreCase))
				{
					SetDesiredValue(
						TEXT("r.SkyAtmosphere.FastSkyLUT"),
						0.0f);
					SetDesiredValue(
						TEXT("r.SkyAtmosphere.SampleCountMin"),
						64.0f);
					SetDesiredValue(
						TEXT("r.SkyAtmosphere.SampleCountMax"),
						128.0f);
				}
				else
				{
					Overrides.Reset();
					OutFailure = FString::Printf(
						TEXT("Unknown ABTSToonSkyBandingExperiment: %s"),
						*BandingExperiment);
					return false;
				}
			}
			for (FCVarOverride& Override : Overrides)
			{
				Override.Variable = IConsoleManager::Get().FindConsoleVariable(
					Override.Name);
				if (Override.Variable == nullptr)
				{
					RestoreAppliedOverrides();
					OutFailure = FString::Printf(
						TEXT("Required UE 5.8 SkyAtmosphere CVar is unavailable: %s"),
						Override.Name);
					return false;
				}
				Override.OriginalValue = Override.Variable->GetString();
				Override.bApplied = true;
				Override.Variable->SetWithCurrentPriority(Override.DesiredValue);
				if (!FMath::IsNearlyEqual(
					Override.Variable->GetFloat(),
					Override.DesiredValue,
					1.0e-4f))
				{
					const FString RejectedName(Override.Name);
					RestoreAppliedOverrides();
					OutFailure = FString::Printf(
						TEXT("Continuous SkyAtmosphere value was not retained: %s"),
						*RejectedName);
					return false;
				}
			}
			ReferenceCount = 1;
			return true;
		}

		static void Release()
		{
			if (ReferenceCount <= 0)
			{
				return;
			}
			--ReferenceCount;
			if (ReferenceCount > 0)
			{
				return;
			}

			RestoreAppliedOverrides();
		}

		static FString DescribeEffectiveValues()
		{
			FString Result;
			for (const FCVarOverride& Override : Overrides)
			{
				if (Override.Variable != nullptr)
				{
					if (!Result.IsEmpty())
					{
						Result += TEXT(" ");
					}
					Result += FString::Printf(
						TEXT("%s=%s"),
						Override.Name,
						*Override.Variable->GetString());
				}
			}
			return Result;
		}

	private:
		struct FCVarOverride
		{
			const TCHAR* Name = nullptr;
			float DesiredValue = 0.0f;
			IConsoleVariable* Variable = nullptr;
			FString OriginalValue;
			bool bApplied = false;
		};

		static void RestoreAppliedOverrides()
		{
			for (int32 Index = Overrides.Num() - 1; Index >= 0; --Index)
			{
				FCVarOverride& Override = Overrides[Index];
				if (Override.bApplied && Override.Variable != nullptr)
				{
					Override.Variable->SetWithCurrentPriority(
						*Override.OriginalValue);
				}
			}
			Overrides.Reset();
		}

		inline static int32 ReferenceCount = 0;
		inline static TArray<FCVarOverride> Overrides;
	};

	struct FPrimitiveSavedState
	{
		bool bRenderCustomDepth = false;
		int32 StencilValue = 0;
		ERendererStencilMask StencilMask = ERendererStencilMask::ERSM_Default;
		uint8 AppliedStencilValue = 0;
	};

	void GatherActorPrimitives(
		const AActor& Actor,
		TArray<UPrimitiveComponent*>& OutPrimitives)
	{
		TInlineComponentArray<UPrimitiveComponent*> Components;
		Actor.GetComponents(Components);
		for (UPrimitiveComponent* Component : Components)
		{
			if (IsValid(Component) && Component->IsRegistered())
			{
				OutPrimitives.AddUnique(Component);
			}
		}
	}
}

class UABTSStylizedRenderingWorldSubsystem::FPrimitiveOverrideRegistry
{
public:
	using FPrimitiveSavedState =
		ABTSStylizedRenderingWorldSubsystemPrivate::FPrimitiveSavedState;

	void Apply(
		const TMap<TWeakObjectPtr<UPrimitiveComponent>,
			EABTSStylizedObjectClass>& Desired)
	{
		for (auto It = ConflictingComponents.CreateIterator(); It; ++It)
		{
			if (!It->IsValid() || !Desired.Contains(*It))
			{
				It.RemoveCurrent();
			}
		}

		for (auto It = SavedStates.CreateIterator(); It; ++It)
		{
			UPrimitiveComponent* Component = It.Key().Get();
			if (!IsValid(Component))
			{
				It.RemoveCurrent();
				continue;
			}
			if (!Desired.Contains(It.Key()))
			{
				RestoreIfStillOwned(*Component, It.Value());
				It.RemoveCurrent();
			}
		}

		for (const TPair<TWeakObjectPtr<UPrimitiveComponent>,
			EABTSStylizedObjectClass>& Pair : Desired)
		{
			UPrimitiveComponent* Component = Pair.Key.Get();
			const uint8 StencilValue =
				FABTSStylizedRenderingContract::ResolveStencilValueForRenderer(
					Pair.Value);
			if (!IsValid(Component) || StencilValue == 0)
			{
				continue;
			}

			FPrimitiveSavedState* Existing = SavedStates.Find(Pair.Key);
			if (Existing == nullptr)
			{
				// Do not steal an unrelated gameplay/debug stencil producer.
				if (Component->bRenderCustomDepth
					&& Component->CustomDepthStencilValue != StencilValue)
				{
					ConflictingComponents.Add(Pair.Key);
					continue;
				}
				ConflictingComponents.Remove(Pair.Key);
				FPrimitiveSavedState Saved;
				Saved.bRenderCustomDepth = Component->bRenderCustomDepth;
				Saved.StencilValue = Component->CustomDepthStencilValue;
				Saved.StencilMask = Component->CustomDepthStencilWriteMask;
				Saved.AppliedStencilValue = StencilValue;
				Existing = &SavedStates.Add(Pair.Key, Saved);
			}
			Existing->AppliedStencilValue = StencilValue;
			Component->SetCustomDepthStencilWriteMask(
				ERendererStencilMask::ERSM_Default);
			Component->SetCustomDepthStencilValue(StencilValue);
			Component->SetRenderCustomDepth(true);
		}
	}

	void RestoreAll()
	{
		for (TPair<TWeakObjectPtr<UPrimitiveComponent>, FPrimitiveSavedState>& Pair
			: SavedStates)
		{
			if (UPrimitiveComponent* Component = Pair.Key.Get())
			{
				RestoreIfStillOwned(*Component, Pair.Value);
			}
		}
		SavedStates.Reset();
		ConflictingComponents.Reset();
	}

	int32 Num() const { return SavedStates.Num(); }
	int32 GetConflictCount() const { return ConflictingComponents.Num(); }

private:
	static void RestoreIfStillOwned(
		UPrimitiveComponent& Component,
		const FPrimitiveSavedState& Saved)
	{
		if (!Component.bRenderCustomDepth
			|| Component.CustomDepthStencilValue
				!= Saved.AppliedStencilValue)
		{
			return;
		}
		Component.SetCustomDepthStencilWriteMask(Saved.StencilMask);
		Component.SetCustomDepthStencilValue(Saved.StencilValue);
		Component.SetRenderCustomDepth(Saved.bRenderCustomDepth);
	}

	TMap<TWeakObjectPtr<UPrimitiveComponent>, FPrimitiveSavedState> SavedStates;
	TSet<TWeakObjectPtr<UPrimitiveComponent>> ConflictingComponents;
};

/**
 * Reversible, runtime-only presentation of the accepted spherical environment.
 * It deliberately edits the existing map actors instead of creating authored
 * assets, and restores every field when stylization is disabled.
 */
class FABTSToonEnvironmentPresentationState
{
public:
	bool Apply(
		UWorld& World,
		const FABTSToonEnvironmentSnapshot& Snapshot,
		const FABTSStylizedEnvironmentParameters& Parameters,
		FString& OutFailure)
	{
		OutFailure.Reset();
		if (!Snapshot.IsValid() || !Parameters.IsValid())
		{
			OutFailure = TEXT("The accepted environment snapshot is invalid.");
			return false;
		}
		const FABTSStylizedEnvironmentProfilePolicy ProfilePolicy =
			FABTSStylizedRenderingControl::GetEnvironmentProfilePolicy(
				Parameters.Profile);
		if (!ProfilePolicy.IsValid()
			|| (Parameters.bCloudsEnabled != 0u)
				!= ProfilePolicy.bLowPolyCloudsVisible)
		{
			OutFailure = TEXT("The formal environment profile policy is invalid or disagrees with its cloud parameters.");
			return false;
		}

		TArray<ASkyAtmosphere*> Atmospheres;
		for (TActorIterator<ASkyAtmosphere> It(&World); It; ++It)
		{
			if (IsValid(*It) && IsValid(It->GetComponent()))
			{
				Atmospheres.Add(*It);
			}
		}
		if (Atmospheres.Num() != 1)
		{
			OutFailure = FString::Printf(
				TEXT("Expected exactly one SkyAtmosphere, found %d."),
				Atmospheres.Num());
			return false;
		}
		TArray<AVolumetricCloud*> CloudActors;
		for (TActorIterator<AVolumetricCloud> It(&World); It; ++It)
		{
			if (IsValid(*It)
				&& IsValid(It->FindComponentByClass<UVolumetricCloudComponent>()))
			{
				CloudActors.Add(*It);
			}
		}
		if (CloudActors.Num() != 1)
		{
			OutFailure = FString::Printf(
				TEXT("Expected exactly one VolumetricCloud, found %d."),
				CloudActors.Num());
			return false;
		}

		ASkyAtmosphere* AtmosphereActor = Atmospheres[0];
		USkyAtmosphereComponent* Atmosphere = AtmosphereActor->GetComponent();
		UVolumetricCloudComponent* Cloud =
			CloudActors[0]->FindComponentByClass<UVolumetricCloudComponent>();
		if (!bOriginalCaptured)
		{
			CaptureOriginal(World, *AtmosphereActor, *Atmosphere, *Cloud);
		}
		else if (SavedAtmosphereActor.Get() != AtmosphereActor
			|| SavedAtmosphereComponent.Get() != Atmosphere
			|| SavedCloudComponent.Get() != Cloud)
		{
			OutFailure = TEXT("The authoritative SkyAtmosphere changed during the run.");
			return false;
		}

		if (!bContinuousAtmosphereOverrideAcquired)
		{
			if (!ABTSStylizedRenderingWorldSubsystemPrivate::
				FContinuousAtmosphereOverride::Acquire(OutFailure))
			{
				return false;
			}
			bContinuousAtmosphereOverrideAcquired = true;
		}

		AtmosphereActor->SetActorLocation(
			Snapshot.PlanetCenterWorld,
			false,
			nullptr,
			ETeleportType::TeleportPhysics);
		Atmosphere->TransformMode =
			ESkyAtmosphereTransformMode::PlanetCenterAtComponentTransform;
		Atmosphere->SetBottomRadius(FMath::Max(
			static_cast<float>(Snapshot.PlanetRadiusCM / 100000.0),
			0.1f));
		const float AtmosphereHeightKM = FMath::Max(
			Parameters.AtmosphereHeightCM / 100000.0f,
			0.1f);
		Atmosphere->SetAtmosphereHeight(AtmosphereHeightKM);
		Atmosphere->TraceSampleCountScale =
			ABTSStylizedRenderingWorldSubsystemPrivate::
				ContinuousAtmosphereTraceSampleCountScale;
		// The physically tiny ABTS planet produces much less optical path energy
		// than UE's Earth-scale defaults.  Compensate atmosphere radiance only;
		// object albedo, sun, clouds and HDR stars keep their independent exposure.
		const FLinearColor SkyLuminanceFactor =
			Parameters.Profile == EABTSStylizedRenderProfile::GroundDay
				? FLinearColor(22.0f, 24.0f, 24.0f, 1.0f)
				: FLinearColor::White;
		Atmosphere->SetSkyLuminanceFactor(SkyLuminanceFactor);
		Atmosphere->SetMultiScatteringFactor(0.75f);
		Atmosphere->SetRayleighScatteringScale(0.65f);
		Atmosphere->SetRayleighExponentialDistribution(
			FMath::Max(AtmosphereHeightKM * 0.22f, 0.001f));
		Atmosphere->SetMieScatteringScale(0.35f);
		Atmosphere->SetMieAnisotropy(0.72f);
		Atmosphere->SetMieExponentialDistribution(
			FMath::Max(AtmosphereHeightKM * 0.08f, 0.001f));
		Atmosphere->SetHeightFogContribution(0.0f);
		Atmosphere->SetAerialPerspectiveStartDepth(0.001f);
		Atmosphere->SetVisibility(
			ProfilePolicy.bSkyAtmosphereVisible,
			true);
		Atmosphere->MarkRenderStateDirty();

		for (const FFogSavedState& Fog : SavedFogs)
		{
			if (UExponentialHeightFogComponent* Component = Fog.Component.Get())
			{
				Component->SetVisibility(
					ProfilePolicy.bHeightFogVisible && Fog.bVisible,
					true);
			}
		}

		// The stock Earth-scale volumetric material was proven to have no usable
		// interval at ABTS scale: it is transparent at authored density and a
		// uniform grey veil once optical depth is compensated. Hide it while the
		// reversible stylized presentation owns the world. T4-A2R0 supplies three
		// bounded, deterministic low-poly cloud islands instead of a global shell.
		Cloud->SetVisibility(false, true);
		bLowPolyCloudPrototypeEnabled =
			ProfilePolicy.bLowPolyCloudsVisible;
		bApplied = true;
		return true;
	}

	void Restore()
	{
		if (!bOriginalCaptured)
		{
			return;
		}
		if (bContinuousAtmosphereOverrideAcquired)
		{
			ABTSStylizedRenderingWorldSubsystemPrivate::
				FContinuousAtmosphereOverride::Release();
			bContinuousAtmosphereOverrideAcquired = false;
		}
		if (ASkyAtmosphere* Actor = SavedAtmosphereActor.Get())
		{
			Actor->SetActorLocation(
				OriginalActorLocation,
				false,
				nullptr,
				ETeleportType::TeleportPhysics);
		}
		if (USkyAtmosphereComponent* Atmosphere =
			SavedAtmosphereComponent.Get())
		{
			Atmosphere->TransformMode = OriginalTransformMode;
			Atmosphere->SetBottomRadius(OriginalBottomRadius);
			Atmosphere->SetAtmosphereHeight(OriginalAtmosphereHeight);
			Atmosphere->TraceSampleCountScale = OriginalTraceSampleCountScale;
			Atmosphere->SetSkyLuminanceFactor(OriginalSkyLuminanceFactor);
			Atmosphere->SetMultiScatteringFactor(
				OriginalMultiScatteringFactor);
			Atmosphere->SetRayleighScatteringScale(
				OriginalRayleighScatteringScale);
			Atmosphere->SetRayleighExponentialDistribution(
				OriginalRayleighExponentialDistribution);
			Atmosphere->SetMieScatteringScale(OriginalMieScatteringScale);
			Atmosphere->SetMieAnisotropy(OriginalMieAnisotropy);
			Atmosphere->SetMieExponentialDistribution(
				OriginalMieExponentialDistribution);
			Atmosphere->SetHeightFogContribution(
				OriginalHeightFogContribution);
			Atmosphere->SetAerialPerspectiveStartDepth(
				OriginalAerialPerspectiveStartDepth);
			Atmosphere->SetVisibility(bOriginalAtmosphereVisible, true);
			Atmosphere->MarkRenderStateDirty();
		}
		for (const FFogSavedState& Fog : SavedFogs)
		{
			if (UExponentialHeightFogComponent* Component = Fog.Component.Get())
			{
				Component->SetVisibility(Fog.bVisible, true);
			}
		}
		if (UVolumetricCloudComponent* Cloud = SavedCloudComponent.Get())
		{
			Cloud->SetLayerBottomAltitude(OriginalCloudLayerBottomAltitude);
			Cloud->SetLayerHeight(OriginalCloudLayerHeight);
			Cloud->SetTracingStartMaxDistance(OriginalCloudTracingStartMaxDistance);
			Cloud->SetTracingStartDistanceFromCamera(
				OriginalCloudTracingStartDistanceFromCamera);
			Cloud->TracingMaxDistanceMode = OriginalCloudTracingMaxDistanceMode;
			Cloud->SetTracingMaxDistance(OriginalCloudTracingMaxDistance);
			Cloud->SetPlanetRadius(OriginalCloudPlanetRadius);
			Cloud->SetbUsePerSampleAtmosphericLightTransmittance(
				bOriginalCloudPerSampleTransmittance);
			Cloud->SetSkyLightCloudBottomOcclusion(
				OriginalCloudBottomOcclusion);
			Cloud->SetViewSampleCountScale(OriginalCloudViewSampleScale);
			Cloud->SetReflectionViewSampleCountScale(
				OriginalCloudReflectionSampleScale);
			Cloud->SetShadowViewSampleCountScale(OriginalCloudShadowSampleScale);
			Cloud->SetShadowReflectionViewSampleCountScale(
				OriginalCloudShadowReflectionSampleScale);
			Cloud->SetShadowTracingDistance(OriginalCloudShadowTracingDistance);
			Cloud->SetStopTracingTransmittanceThreshold(
				OriginalCloudStopTracingThreshold);
			Cloud->SetMaterial(OriginalCloudMaterial.Get());
			Cloud->SetVisibility(bOriginalCloudVisible, true);
		}
		bLowPolyCloudPrototypeEnabled = false;
		bApplied = false;
	}

	bool IsApplied() const { return bApplied; }
	int32 GetFogCount() const { return SavedFogs.Num(); }
	bool IsCloudApplied() const
	{
		return bApplied && bLowPolyCloudPrototypeEnabled;
	}

private:
	struct FFogSavedState
	{
		TWeakObjectPtr<UExponentialHeightFogComponent> Component;
		bool bVisible = true;
	};

	void CaptureOriginal(
		UWorld& World,
		ASkyAtmosphere& Actor,
		USkyAtmosphereComponent& Atmosphere,
		UVolumetricCloudComponent& Cloud)
	{
		SavedAtmosphereActor = &Actor;
		SavedAtmosphereComponent = &Atmosphere;
		OriginalActorLocation = Actor.GetActorLocation();
		OriginalTransformMode = Atmosphere.TransformMode;
		OriginalBottomRadius = Atmosphere.BottomRadius;
		OriginalAtmosphereHeight = Atmosphere.AtmosphereHeight;
		OriginalTraceSampleCountScale = Atmosphere.TraceSampleCountScale;
		OriginalSkyLuminanceFactor = Atmosphere.SkyLuminanceFactor;
		OriginalMultiScatteringFactor = Atmosphere.MultiScatteringFactor;
		OriginalRayleighScatteringScale = Atmosphere.RayleighScatteringScale;
		OriginalRayleighExponentialDistribution =
			Atmosphere.RayleighExponentialDistribution;
		OriginalMieScatteringScale = Atmosphere.MieScatteringScale;
		OriginalMieAnisotropy = Atmosphere.MieAnisotropy;
		OriginalMieExponentialDistribution =
			Atmosphere.MieExponentialDistribution;
		OriginalHeightFogContribution = Atmosphere.HeightFogContribution;
		OriginalAerialPerspectiveStartDepth =
			Atmosphere.AerialPerspectiveStartDepth;
		bOriginalAtmosphereVisible = Atmosphere.IsVisible();
		SavedCloudComponent = &Cloud;
		OriginalCloudLayerBottomAltitude = Cloud.LayerBottomAltitude;
		OriginalCloudLayerHeight = Cloud.LayerHeight;
		OriginalCloudTracingStartMaxDistance = Cloud.TracingStartMaxDistance;
		OriginalCloudTracingStartDistanceFromCamera =
			Cloud.TracingStartDistanceFromCamera;
		OriginalCloudTracingMaxDistanceMode = Cloud.TracingMaxDistanceMode;
		OriginalCloudTracingMaxDistance = Cloud.TracingMaxDistance;
		OriginalCloudPlanetRadius = Cloud.PlanetRadius;
		OriginalCloudMaterial = Cloud.GetMaterial();
		bOriginalCloudPerSampleTransmittance =
			Cloud.bUsePerSampleAtmosphericLightTransmittance != 0;
		OriginalCloudBottomOcclusion = Cloud.SkyLightCloudBottomOcclusion;
		OriginalCloudViewSampleScale = Cloud.ViewSampleCountScale;
		OriginalCloudReflectionSampleScale =
			Cloud.ReflectionViewSampleCountScaleValue;
		OriginalCloudShadowSampleScale = Cloud.ShadowViewSampleCountScale;
		OriginalCloudShadowReflectionSampleScale =
			Cloud.ShadowReflectionViewSampleCountScaleValue;
		OriginalCloudShadowTracingDistance = Cloud.ShadowTracingDistance;
		OriginalCloudStopTracingThreshold =
			Cloud.StopTracingTransmittanceThreshold;
		bOriginalCloudVisible = Cloud.IsVisible();

		SavedFogs.Reset();
		for (TActorIterator<AExponentialHeightFog> It(&World); It; ++It)
		{
			if (UExponentialHeightFogComponent* Fog =
				IsValid(*It) ? It->GetComponent() : nullptr)
			{
				FFogSavedState Saved;
				Saved.Component = Fog;
				Saved.bVisible = Fog->IsVisible();
				SavedFogs.Add(Saved);
			}
		}
		bOriginalCaptured = true;
	}

	TWeakObjectPtr<ASkyAtmosphere> SavedAtmosphereActor;
	TWeakObjectPtr<USkyAtmosphereComponent> SavedAtmosphereComponent;
	TWeakObjectPtr<UVolumetricCloudComponent> SavedCloudComponent;
	TArray<FFogSavedState> SavedFogs;
	FVector OriginalActorLocation = FVector::ZeroVector;
	ESkyAtmosphereTransformMode OriginalTransformMode =
		ESkyAtmosphereTransformMode::PlanetTopAtAbsoluteWorldOrigin;
	float OriginalBottomRadius = 0.0f;
	float OriginalAtmosphereHeight = 0.0f;
	float OriginalTraceSampleCountScale = 1.0f;
	FLinearColor OriginalSkyLuminanceFactor = FLinearColor::White;
	float OriginalMultiScatteringFactor = 0.0f;
	float OriginalRayleighScatteringScale = 0.0f;
	float OriginalRayleighExponentialDistribution = 0.0f;
	float OriginalMieScatteringScale = 0.0f;
	float OriginalMieAnisotropy = 0.0f;
	float OriginalMieExponentialDistribution = 0.0f;
	float OriginalHeightFogContribution = 0.0f;
	float OriginalAerialPerspectiveStartDepth = 0.0f;
	bool bOriginalAtmosphereVisible = true;
	float OriginalCloudLayerBottomAltitude = 0.0f;
	float OriginalCloudLayerHeight = 0.0f;
	float OriginalCloudTracingStartMaxDistance = 0.0f;
	float OriginalCloudTracingStartDistanceFromCamera = 0.0f;
	EVolumetricCloudTracingMaxDistanceMode OriginalCloudTracingMaxDistanceMode =
		EVolumetricCloudTracingMaxDistanceMode::DistanceFromCloudLayerEntryPoint;
	float OriginalCloudTracingMaxDistance = 0.0f;
	float OriginalCloudPlanetRadius = 0.0f;
	TWeakObjectPtr<UMaterialInterface> OriginalCloudMaterial;
	float OriginalCloudBottomOcclusion = 0.0f;
	float OriginalCloudViewSampleScale = 1.0f;
	float OriginalCloudReflectionSampleScale = 1.0f;
	float OriginalCloudShadowSampleScale = 1.0f;
	float OriginalCloudShadowReflectionSampleScale = 1.0f;
	float OriginalCloudShadowTracingDistance = 0.0f;
	float OriginalCloudStopTracingThreshold = 0.0f;
	bool bOriginalCloudPerSampleTransmittance = false;
	bool bOriginalCloudVisible = true;
	bool bOriginalCaptured = false;
	bool bApplied = false;
	bool bContinuousAtmosphereOverrideAcquired = false;
	bool bLowPolyCloudPrototypeEnabled = false;
};

UABTSStylizedRenderingWorldSubsystem::UABTSStylizedRenderingWorldSubsystem() = default;

UABTSStylizedRenderingWorldSubsystem::UABTSStylizedRenderingWorldSubsystem(
	FVTableHelper& Helper)
	: Super(Helper)
{
}

UABTSStylizedRenderingWorldSubsystem::~UABTSStylizedRenderingWorldSubsystem() = default;

bool UABTSStylizedRenderingWorldSubsystem::ShouldCreateSubsystem(
	UObject* Outer) const
{
	return Super::ShouldCreateSubsystem(Outer);
}

bool UABTSStylizedRenderingWorldSubsystem::DoesSupportWorldType(
	const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UABTSStylizedRenderingWorldSubsystem::Initialize(
	FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	PrimitiveRegistry = MakeUnique<FPrimitiveOverrideRegistry>();
	MaterialRegistry = MakeUnique<FABTSStylizedMaterialOverrideRegistry>();
	EnvironmentPresentation =
		MakeUnique<FABTSToonEnvironmentPresentationState>();
	PreloadedSharedMaterials.Reset();
	bSharedMaterialPreloadReady = false;
	bM7SemanticAdapterReady = false;
	bM7MaterialSetReady = false;
	EnvironmentSnapshot = FABTSToonEnvironmentSnapshot();
	bEnvironmentSnapshotReady = false;
	bWorldTearingDown = false;
	bEnvironmentOwnershipActive = false;
	EnvironmentOwnershipGeneration = 0;
	EnvironmentRecoveryGeneration = 0;
	EnvironmentSourceGeneration = 0;
	ActiveFinaleEnvironmentSource.Reset();
	ActiveFinaleEnvironmentSourceHash = 0;
	LastEnvironmentDiagnosticHash = 0;
	CloudFieldTuningState = FABTST4CloudFieldTuningState();
	WorldBeginTearDownHandle = FWorldDelegates::OnWorldBeginTearDown.AddUObject(
		this,
		&UABTSStylizedRenderingWorldSubsystem::HandleWorldBeginTearDown);
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(
		this,
		&UABTSStylizedRenderingWorldSubsystem::HandleWorldCleanup);
}

void UABTSStylizedRenderingWorldSubsystem::Deinitialize()
{
	bWorldTearingDown = true;
	UnbindWorldLifecycleDelegates();
	ReleaseEnvironmentOwnership(TEXT("SubsystemDeinitialize"), true);
	if (AActor* OverviewCamera = CloudFieldOverviewCamera.Get())
	{
		OverviewCamera->Destroy();
	}
	CloudFieldOverviewCamera.Reset();
	CloudFieldOverviewPreviousViewTarget.Reset();
	if (EnvironmentPresentation)
	{
		EnvironmentPresentation.Reset();
	}
	if (MaterialRegistry)
	{
		MaterialRegistry->RestoreAll();
		MaterialRegistry.Reset();
	}
	if (PrimitiveRegistry)
	{
		PrimitiveRegistry->RestoreAll();
		PrimitiveRegistry.Reset();
	}
	PreloadedSharedMaterials.Reset();
	bSharedMaterialPreloadReady = false;
	bM7SemanticAdapterReady = false;
	bM7MaterialSetReady = false;
	EnvironmentSnapshot = FABTSToonEnvironmentSnapshot();
	bEnvironmentSnapshotReady = false;
	LastEnvironmentDiagnosticHash = 0;
	Super::Deinitialize();
}

void UABTSStylizedRenderingWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bWorldTearingDown = false;
	BindWorldLifecycleDelegates(InWorld);
	PreloadSharedMaterials();
	bWorldBeganPlay = true;
	RefreshNow();
}

void UABTSStylizedRenderingWorldSubsystem::BindWorldLifecycleDelegates(
	UWorld& World)
{
	if (!ActorSpawnedHandle.IsValid())
	{
		ActorSpawnedHandle = World.AddOnActorSpawnedHandler(
			FOnActorSpawned::FDelegate::CreateUObject(
				this,
				&UABTSStylizedRenderingWorldSubsystem::HandleActorSpawned));
	}
	if (!ActorDestroyedHandle.IsValid())
	{
		ActorDestroyedHandle = World.AddOnActorDestroyedHandler(
			FOnActorDestroyed::FDelegate::CreateUObject(
				this,
				&UABTSStylizedRenderingWorldSubsystem::HandleActorDestroyed));
	}
}

void UABTSStylizedRenderingWorldSubsystem::UnbindWorldLifecycleDelegates()
{
	if (UWorld* World = GetWorld())
	{
		if (ActorSpawnedHandle.IsValid())
		{
			World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
		}
		if (ActorDestroyedHandle.IsValid())
		{
			World->RemoveOnActorDestroyedHandler(ActorDestroyedHandle);
		}
	}
	ActorSpawnedHandle.Reset();
	ActorDestroyedHandle.Reset();
	if (WorldBeginTearDownHandle.IsValid())
	{
		FWorldDelegates::OnWorldBeginTearDown.Remove(
			WorldBeginTearDownHandle);
		WorldBeginTearDownHandle.Reset();
	}
	if (WorldCleanupHandle.IsValid())
	{
		FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
		WorldCleanupHandle.Reset();
	}
}

void UABTSStylizedRenderingWorldSubsystem::HandleActorSpawned(AActor* Actor)
{
	if (!bWorldTearingDown
		&& IsValid(Actor)
		&& (Actor->IsA<AABTSM11FinaleInteractionSystem>()
			|| Actor->IsA<AABTSM7BuildingMaterialSystem>()
			|| Actor->IsA<AABTSM7BuildingModule>()))
	{
		// Finished spawning precedes the M11 Initialize call.  Mark the lease
		// dirty now; the bounded fallback poll acquires it once initialization
		// publishes a valid FinaleSystem and active state.
		RefreshAccumulatorSeconds =
			ABTSStylizedRenderingWorldSubsystemPrivate::RefreshIntervalSeconds;
	}
}

void UABTSStylizedRenderingWorldSubsystem::UnregisterCapturesOwnedBy(
	const AActor* Owner)
{
	if (Owner == nullptr)
	{
		return;
	}
	for (auto It = RegisteredCaptures.CreateIterator(); It; ++It)
	{
		USceneCaptureComponent2D* Capture = It->Get();
		if (IsValid(Capture) && Capture->GetOwner() == Owner)
		{
			FABTSStylizedSceneCaptureRegistry::Unregister(*Capture);
			It.RemoveCurrent();
		}
	}
}

void UABTSStylizedRenderingWorldSubsystem::HandleActorDestroyed(AActor* Actor)
{
	if (Actor == nullptr)
	{
		return;
	}
	UnregisterCapturesOwnedBy(Actor);
	if (!Actor->IsA<AABTSM11FinaleInteractionSystem>())
	{
		return;
	}

	ReleaseEnvironmentOwnership(TEXT("M11SourceDestroyed"), false);
	if (!bWorldTearingDown)
	{
		// IsActorBeingDestroyed is rejected by the resolver, so this immediate
		// refresh cannot reacquire the dying source.  A surviving unique source
		// is acquired atomically; otherwise GroundDay is restored fail closed.
		RefreshNow();
	}
}

void UABTSStylizedRenderingWorldSubsystem::HandleWorldBeginTearDown(
	UWorld* World)
{
	if (World != GetWorld())
	{
		return;
	}
	bWorldTearingDown = true;
	bWorldBeganPlay = false;
	ReleaseEnvironmentOwnership(TEXT("WorldBeginTearDown"), true);
}

void UABTSStylizedRenderingWorldSubsystem::HandleWorldCleanup(
	UWorld* World,
	const bool bSessionEnded,
	const bool bCleanupResources)
{
	(void)bSessionEnded;
	(void)bCleanupResources;
	if (World != GetWorld())
	{
		return;
	}
	bWorldTearingDown = true;
	bWorldBeganPlay = false;
	ReleaseEnvironmentOwnership(TEXT("WorldCleanup"), true);
}

void UABTSStylizedRenderingWorldSubsystem::ReleaseEnvironmentOwnership(
	const TCHAR* Reason,
	const bool bUnregisterAllCaptures)
{
	const bool bHadOwnership = bEnvironmentOwnershipActive
		|| bEnvironmentSnapshotReady
		|| ActiveFinaleEnvironmentSource.IsValid()
		|| ActiveFinaleEnvironmentSourceHash != 0;
	FABTSStylizedRenderingControl::ClearEnvironmentParameters();
	DestroyLowPolyCloudPrototype();
	if (EnvironmentPresentation)
	{
		EnvironmentPresentation->Restore();
	}
	if (bUnregisterAllCaptures)
	{
		for (const TWeakObjectPtr<USceneCaptureComponent2D>& Capture
			: RegisteredCaptures)
		{
			if (Capture.IsValid())
			{
				FABTSStylizedSceneCaptureRegistry::Unregister(*Capture.Get());
			}
		}
		RegisteredCaptures.Reset();
	}
	EnvironmentSnapshot = FABTSToonEnvironmentSnapshot();
	bEnvironmentSnapshotReady = false;
	ActiveFinaleEnvironmentSource.Reset();
	ActiveFinaleEnvironmentSourceHash = 0;
	bEnvironmentOwnershipActive = false;
	LastEnvironmentDiagnosticHash = 0;
	if (bHadOwnership
		&& EnvironmentRecoveryGeneration != EnvironmentOwnershipGeneration)
	{
		EnvironmentRecoveryGeneration = EnvironmentOwnershipGeneration;
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][Rendering][T4-A3.3][Recovery] Restored=1 Reason=%s OwnershipGeneration=%u SourceGeneration=%u CapturesRemaining=%d"),
			Reason != nullptr ? Reason : TEXT("Unknown"),
			EnvironmentOwnershipGeneration,
			EnvironmentSourceGeneration,
			RegisteredCaptures.Num());
	}
}

void UABTSStylizedRenderingWorldSubsystem::PreloadSharedMaterials()
{
	const double StartSeconds = FPlatformTime::Seconds();
	PreloadedSharedMaterials.Reset();
	bSharedMaterialPreloadReady = false;

	TArray<UMaterialInterface*> LoadedMaterials;
	int32 FailureCount = 0;
	const int32 LoadedCount =
		FABTSSharedStylizedMaterialAdapter::PreloadCatalogMaterials(
			LoadedMaterials,
			FailureCount);
	int32 CompleteCount = 0;
	for (UMaterialInterface* Material : LoadedMaterials)
	{
		if (!IsValid(Material))
		{
			++FailureCount;
			continue;
		}

		// In Editor this blocks until any missing shader map is complete. That
		// deliberately moves the one-time fallback-material window from the first
		// slingshot click to world startup. Cooked builds retain the same strong
		// reference preload without requiring runtime shader compilation.
		Material->EnsureIsComplete();
		if (!Material->IsCompiling())
		{
			++CompleteCount;
		}
		PreloadedSharedMaterials.Add(Material);
	}

	const int32 CatalogCount =
		FABTSSharedStylizedMaterialAdapter::GetCatalogEntryCount();
	bSharedMaterialPreloadReady =
		FailureCount == 0
		&& LoadedCount == CatalogCount
		&& CompleteCount == CatalogCount
		&& PreloadedSharedMaterials.Num() == CatalogCount;
	const double ElapsedMilliseconds =
		(FPlatformTime::Seconds() - StartSeconds) * 1000.0;
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][Rendering][T3-A2][Preload] Catalog=%d Loaded=%d Complete=%d Failed=%d Ready=%d ElapsedMS=%.2f"),
		CatalogCount,
		LoadedCount,
		CompleteCount,
		FailureCount,
		bSharedMaterialPreloadReady ? 1 : 0,
		ElapsedMilliseconds);
}

void UABTSStylizedRenderingWorldSubsystem::Tick(const float DeltaTime)
{
	if (bWorldTearingDown)
	{
		return;
	}
	UpdateCloudTraversalVisibility(DeltaTime, false);
	const bool bStyleEnabled = FABTSStylizedRenderingControl::IsEnabled();
	if (bStyleEnabled != bLastObservedStyleEnabled)
	{
		RefreshAccumulatorSeconds = 0.0f;
		RefreshNow();
		return;
	}
	RefreshAccumulatorSeconds += FMath::Max(0.0f, DeltaTime);
	if (RefreshAccumulatorSeconds >=
		ABTSStylizedRenderingWorldSubsystemPrivate::RefreshIntervalSeconds)
	{
		RefreshAccumulatorSeconds = 0.0f;
		RefreshNow();
	}
}

TStatId UABTSStylizedRenderingWorldSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(
		UABTSStylizedRenderingWorldSubsystem,
		STATGROUP_Tickables);
}

bool UABTSStylizedRenderingWorldSubsystem::IsTickable() const
{
	return bWorldBeganPlay && GetWorld() != nullptr;
}

int32 UABTSStylizedRenderingWorldSubsystem::GetRegisteredPrimitiveCount() const
{
	return PrimitiveRegistry ? PrimitiveRegistry->Num() : 0;
}

int32 UABTSStylizedRenderingWorldSubsystem::GetRegisteredMaterialSlotCount() const
{
	return MaterialRegistry ? MaterialRegistry->Num() : 0;
}

FABTST4CloudFieldTuningState
UABTSStylizedRenderingWorldSubsystem::GetCloudFieldTuningState() const
{
	FABTST4CloudFieldTuningState Result = CloudFieldTuningState;
	if (Result.Seed == 0 && bEnvironmentSnapshotReady)
	{
		const FABTSStylizedEnvironmentParameters Parameters =
			FABTSStylizedRenderingControl::BuildEnvironmentParameters(
				EnvironmentSnapshot.PlanetCenterWorld,
				EnvironmentSnapshot.PlanetRadiusCM,
				EnvironmentSnapshot.SunDirectionToSunWorld,
				EnvironmentSnapshot.Profile);
		Result.Seed = Parameters.StarSeed ^ 0xC10DF13Du;
	}
	return Result;
}

bool UABTSStylizedRenderingWorldSubsystem::ApplyCloudFieldTuningOverride(
	const int32 ClusterCount,
	const float CloudsPerClusterMean,
	const float CloudsPerClusterVariance,
	const uint32 Seed,
	FString& OutFailure)
{
	OutFailure.Reset();
	UWorld* World = GetWorld();
	FABTST4CloudClusterDistributionParameters Distribution;
	Distribution.ClusterCount = ClusterCount;
	Distribution.CloudsPerClusterMean = CloudsPerClusterMean;
	Distribution.CloudsPerClusterVariance = CloudsPerClusterVariance;
	if (World == nullptr || World->WorldType != EWorldType::PIE)
	{
		OutFailure = TEXT("Cloud distribution tuning is available only in PIE.");
		return false;
	}
	if (!Distribution.IsValid() || Seed == 0)
	{
		OutFailure = TEXT("ClusterCount must be [1,64], mean [1,64], variance [0,1024], and seed non-zero.");
		return false;
	}
	const TArray<int32> MemberCounts =
		FABTST4LowPolyCloudPrototype::BuildGlobalClusterMemberCounts(
			Seed, Distribution);
	if (MemberCounts.IsEmpty())
	{
		OutFailure = FString::Printf(
			TEXT("The sampled distribution is empty or exceeds the %d-background-cloud / %d-cloudlet PIE safety budget. Reduce cluster count, mean or variance."),
			FABTST4LowPolyCloudPrototype::MaxGlobalIslandCount,
			(FABTST4LowPolyCloudPrototype::MaxGlobalIslandCount
				+ FABTST4LowPolyCloudPrototype::
					TerminatorMegaClusterIslandCount)
				* FABTST4LowPolyCloudPrototype::CloudletsPerIsland);
		return false;
	}
	int32 BackgroundLogicalClouds = 0;
	for (const int32 Count : MemberCounts)
	{
		BackgroundLogicalClouds += Count;
	}

	const FABTST4CloudFieldTuningState PreviousState = CloudFieldTuningState;
	CloudFieldTuningState.Distribution = Distribution;
	CloudFieldTuningState.Seed = Seed;
	CloudFieldTuningState.bOverrideActive = true;
	DestroyLowPolyCloudPrototype();
	RefreshNow();
	if (!LowPolyCloudPrototypeActor.IsValid())
	{
		CloudFieldTuningState = PreviousState;
		DestroyLowPolyCloudPrototype();
		RefreshNow();
		OutFailure = TEXT("The tuned cloud layout failed the runtime cloud contract.");
		return false;
	}

	FString MemberText;
	for (const int32 Count : MemberCounts)
	{
		MemberText += MemberText.IsEmpty()
			? FString::FromInt(Count)
			: FString::Printf(TEXT(",%d"), Count);
	}
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][Rendering][T4-A2.4][CloudTuning] Accepted=1 ClusterCount=%d Mean=%.3f Variance=%.3f Seed=%u BackgroundLogicalClouds=%d TotalLogicalClouds=%d Cloudlets=%d Members=[%s] LayoutHash=0x%016llX"),
		ClusterCount,
		CloudsPerClusterMean,
		CloudsPerClusterVariance,
		Seed,
		BackgroundLogicalClouds,
		LowPolyLogicalCloudCount,
		LowPolyLogicalCloudCount
			* FABTST4LowPolyCloudPrototype::CloudletsPerIsland,
		*MemberText,
		static_cast<unsigned long long>(LowPolyLogicalCloudLayoutHash));
	return true;
}

void UABTSStylizedRenderingWorldSubsystem::ClearCloudFieldTuningOverride()
{
	UWorld* World = GetWorld();
	if (World == nullptr || World->WorldType != EWorldType::PIE)
	{
		return;
	}
	CloudFieldTuningState = FABTST4CloudFieldTuningState();
	DestroyLowPolyCloudPrototype();
	RefreshNow();
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][Rendering][T4-A2.4][CloudTuning] Cleared=1 ProductionLayoutRestored=1"));
}

bool UABTSStylizedRenderingWorldSubsystem::EnterCloudFieldOverview(
	FString& OutFailure)
{
	OutFailure.Reset();
	UWorld* World = GetWorld();
	if (World == nullptr || World->WorldType != EWorldType::PIE)
	{
		OutFailure = TEXT("Cloud overview is available only in PIE.");
		return false;
	}
	if (!bEnvironmentSnapshotReady)
	{
		RefreshNow();
	}
	if (!bEnvironmentSnapshotReady || !EnvironmentSnapshot.IsValid())
	{
		OutFailure = TEXT("A valid primary-planet environment snapshot is unavailable.");
		return false;
	}
	APlayerController* Controller = World->GetFirstPlayerController();
	if (!IsValid(Controller))
	{
		OutFailure = TEXT("The local PIE player controller is unavailable.");
		return false;
	}

	ACameraActor* OverviewCamera = CloudFieldOverviewCamera.Get();
	if (!IsValid(OverviewCamera))
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = MakeUniqueObjectName(
			World, ACameraActor::StaticClass(), TEXT("ABTST4CloudFieldOverviewCamera"));
		SpawnParameters.ObjectFlags |= RF_Transient;
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		OverviewCamera = World->SpawnActor<ACameraActor>(
			ACameraActor::StaticClass(), FTransform::Identity, SpawnParameters);
		if (!IsValid(OverviewCamera))
		{
			OutFailure = TEXT("Unable to spawn the transient overview camera.");
			return false;
		}
		CloudFieldOverviewCamera = OverviewCamera;
	}
	if (Controller->GetViewTarget() != OverviewCamera)
	{
		CloudFieldOverviewPreviousViewTarget = Controller->GetViewTarget();
	}

	const FABTSStylizedEnvironmentParameters Parameters =
		FABTSStylizedRenderingControl::BuildEnvironmentParameters(
			EnvironmentSnapshot.PlanetCenterWorld,
			EnvironmentSnapshot.PlanetRadiusCM,
			EnvironmentSnapshot.SunDirectionToSunWorld,
			EnvironmentSnapshot.Profile);
	int32 ViewportX = 0;
	int32 ViewportY = 0;
	Controller->GetViewportSize(ViewportX, ViewportY);
	const double Aspect = ViewportX > 0 && ViewportY > 0
		? static_cast<double>(ViewportX) / static_cast<double>(ViewportY)
		: 16.0 / 9.0;
	constexpr double HorizontalFovDegrees = 52.0;
	const double HalfHorizontal = FMath::DegreesToRadians(
		HorizontalFovDegrees * 0.5);
	const double HalfVertical = FMath::Atan(
		FMath::Tan(HalfHorizontal) / FMath::Max(Aspect, 0.1));
	const double FramingRadiusCM = EnvironmentSnapshot.PlanetRadiusCM
		+ Parameters.CloudBaseAltitudeCM
		+ Parameters.CloudLayerHeightCM * 3.0
		+ EnvironmentSnapshot.PlanetRadiusCM * 0.16;
	const double CameraDistanceCM = FramingRadiusCM
		/ FMath::Sin(FMath::Min(HalfHorizontal, HalfVertical)) * 1.08;
	const FVector RadialNorth = FVector::UpVector;
	const FVector CameraLocation = EnvironmentSnapshot.PlanetCenterWorld
		+ RadialNorth * CameraDistanceCM;
	const FVector ViewDirection = -RadialNorth;
	const FRotator CameraRotation = FRotationMatrix::MakeFromXZ(
		ViewDirection, FVector::ForwardVector).Rotator();
	OverviewCamera->SetActorLocationAndRotation(
		CameraLocation, CameraRotation, false, nullptr,
		ETeleportType::TeleportPhysics);
	OverviewCamera->GetCameraComponent()->SetFieldOfView(
		static_cast<float>(HorizontalFovDegrees));
	Controller->SetViewTarget(OverviewCamera);
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][Rendering][T4-A2.4][CloudOverview] Active=1 Center=%s RadiusCM=%.2f FramingRadiusCM=%.2f DistanceCM=%.2f FOV=%.2f Aspect=%.3f"),
		*EnvironmentSnapshot.PlanetCenterWorld.ToCompactString(),
		EnvironmentSnapshot.PlanetRadiusCM,
		FramingRadiusCM,
		CameraDistanceCM,
		HorizontalFovDegrees,
		Aspect);
	return true;
}

bool UABTSStylizedRenderingWorldSubsystem::ExitCloudFieldOverview(
	FString& OutFailure)
{
	OutFailure.Reset();
	UWorld* World = GetWorld();
	APlayerController* Controller = World != nullptr
		? World->GetFirstPlayerController() : nullptr;
	if (!IsValid(Controller))
	{
		OutFailure = TEXT("The local player controller is unavailable.");
		return false;
	}
	AActor* PreviousViewTarget = CloudFieldOverviewPreviousViewTarget.Get();
	if (!IsValid(PreviousViewTarget))
	{
		OutFailure = TEXT("No valid pre-overview view target was saved.");
		return false;
	}
	Controller->SetViewTarget(PreviousViewTarget);
	if (AActor* OverviewCamera = CloudFieldOverviewCamera.Get())
	{
		OverviewCamera->Destroy();
	}
	CloudFieldOverviewCamera.Reset();
	CloudFieldOverviewPreviousViewTarget.Reset();
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][Rendering][T4-A2.4][CloudOverview] Active=0 Restored=%s"),
		*GetNameSafe(PreviousViewTarget));
	return true;
}

void UABTSStylizedRenderingWorldSubsystem::RefreshNow()
{
	using namespace ABTSStylizedRenderingWorldSubsystemPrivate;
	UWorld* World = GetWorld();
	if (bWorldTearingDown
		|| World == nullptr
		|| PrimitiveRegistry == nullptr
		|| MaterialRegistry == nullptr)
	{
		return;
	}
	bLastObservedStyleEnabled = FABTSStylizedRenderingControl::IsEnabled();
	if (!bLastObservedStyleEnabled)
	{
		ReleaseEnvironmentOwnership(TEXT("StyleDisabled"), false);
	}
	else
	{
		const FResolvedFinaleEnvironmentSource FinaleSource =
			ResolveFinaleEnvironmentSource(*World);
		if (FinaleSource.HasUniqueSource())
		{
			if (ActiveFinaleEnvironmentSource.Get()
					!= FinaleSource.Source.Get()
				|| ActiveFinaleEnvironmentSourceHash
					!= FinaleSource.SourceIdentityHash)
			{
				ActiveFinaleEnvironmentSource = FinaleSource.Source;
				ActiveFinaleEnvironmentSourceHash =
					FinaleSource.SourceIdentityHash;
				++EnvironmentSourceGeneration;
			}
		}
		else
		{
			ActiveFinaleEnvironmentSource.Reset();
			ActiveFinaleEnvironmentSourceHash = 0;
		}

		const EABTSM11FinaleEnvironmentStage FinaleEnvironmentStage =
			FinaleSource.Stage;
		const EABTSStylizedRenderProfile ConfiguredProfile =
			FABTSStylizedRenderingControl::GetProfile();
		FABTSToonEnvironmentSnapshot BaselineEnvironment;
		FString BaselineEnvironmentFailure;
		const bool bBaselineEnvironmentReady =
			FABTSToonEnvironmentResolver::ResolveWorldSnapshot(
				*World,
				ConfiguredProfile,
				BaselineEnvironment,
				&BaselineEnvironmentFailure);
		const float FinaleHighAltitudeSpaceBlend =
			bBaselineEnvironmentReady
				? ResolveFinaleHighAltitudeSpaceBlend(
					*World,
					BaselineEnvironment)
				: 0.0f;
		const bool bAtmosphereTransitionComplete =
			FinaleHighAltitudeSpaceBlend >= 1.0f - KINDA_SMALL_NUMBER;
		const bool bFinaleDeepSpace = DoesFinaleEnvironmentStageRequireSpace(
			FinaleEnvironmentStage,
			bAtmosphereTransitionComplete);
		const EABTSStylizedRenderProfile ActiveProfile =
			FABTSStylizedRenderingContract::ResolveMainWorldProfile(
				bFinaleDeepSpace,
				ConfiguredProfile);

		FABTSToonEnvironmentSnapshot ResolvedEnvironment = BaselineEnvironment;
		FString EnvironmentFailure = BaselineEnvironmentFailure;
		bEnvironmentSnapshotReady = bBaselineEnvironmentReady;
		if (bBaselineEnvironmentReady
			&& ActiveProfile != BaselineEnvironment.Profile)
		{
			bEnvironmentSnapshotReady =
				FABTSToonEnvironmentResolver::ResolveWorldSnapshot(
					*World,
					ActiveProfile,
					ResolvedEnvironment,
					&EnvironmentFailure);
		}
		EnvironmentSnapshot = bEnvironmentSnapshotReady
			? ResolvedEnvironment
			: FABTSToonEnvironmentSnapshot();
		uint64 EnvironmentDiagnosticHash = bEnvironmentSnapshotReady
			? EnvironmentSnapshot.IdentityHash
			: GetTypeHash(EnvironmentFailure);
		EnvironmentDiagnosticHash = HashCombineFast(
			EnvironmentDiagnosticHash,
			GetTypeHash(bEnvironmentSnapshotReady));
		EnvironmentDiagnosticHash = HashCombineFast(
			EnvironmentDiagnosticHash,
			GetTypeHash(static_cast<uint8>(FinaleEnvironmentStage)));
		EnvironmentDiagnosticHash = HashCombineFast(
			EnvironmentDiagnosticHash,
			GetTypeHash(bAtmosphereTransitionComplete));
		EnvironmentDiagnosticHash = HashCombineFast(
			EnvironmentDiagnosticHash,
			FinaleSource.CandidateSetHash);
		EnvironmentDiagnosticHash = HashCombineFast(
			EnvironmentDiagnosticHash,
			GetTypeHash(FinaleSource.bConflict));
		if (EnvironmentDiagnosticHash != LastEnvironmentDiagnosticHash)
		{
			LastEnvironmentDiagnosticHash = EnvironmentDiagnosticHash;
			if (bEnvironmentSnapshotReady)
			{
				UE_LOG(
					LogABTSRuntime,
					Log,
					TEXT("[ABTS][Rendering][T4-A3.3][Environment] Ready=1 Version=%d Profile=%d FinaleStage=%s AtmosphereSpaceBlend=%.3f TransitionComplete=%d Source=%s SourceGeneration=%u SourceHash=0x%016llX RelevantSources=%d Conflict=%d FailClosed=%d Seed=%d Generator=%d Attempt=%d Center=%s RadiusCM=%.2f SunToSun=%s SnapshotHash=0x%016llX"),
					EnvironmentSnapshot.Version,
					static_cast<int32>(EnvironmentSnapshot.Profile),
					LexToString(FinaleEnvironmentStage),
					FinaleHighAltitudeSpaceBlend,
					bAtmosphereTransitionComplete ? 1 : 0,
					FinaleSource.HasUniqueSource()
						? *GetNameSafe(FinaleSource.Source.Get())
						: FinaleSource.bConflict
							? TEXT("AmbiguousM11Sources")
							: TEXT("ConfiguredGroundContext"),
					EnvironmentSourceGeneration,
					static_cast<unsigned long long>(
						FinaleSource.SourceIdentityHash),
					FinaleSource.RelevantSourceCount,
					FinaleSource.bConflict ? 1 : 0,
					FinaleSource.bConflict ? 1 : 0,
					EnvironmentSnapshot.WorldSeed,
					EnvironmentSnapshot.GeneratorVersion,
					EnvironmentSnapshot.GenerationAttempt,
					*EnvironmentSnapshot.PlanetCenterWorld.ToCompactString(),
					EnvironmentSnapshot.PlanetRadiusCM,
					*EnvironmentSnapshot.SunDirectionToSunWorld.ToCompactString(),
					static_cast<unsigned long long>(
						EnvironmentSnapshot.IdentityHash));
			}
			else
			{
				UE_LOG(
					LogABTSRuntime,
					Verbose,
					TEXT("[ABTS][Rendering][T4-A3.3][Environment] Ready=0 RelevantSources=%d Conflict=%d Reason=%s"),
					FinaleSource.RelevantSourceCount,
					FinaleSource.bConflict ? 1 : 0,
					EnvironmentFailure.IsEmpty()
						? TEXT("Unknown")
						: *EnvironmentFailure);
			}
		}
		RefreshEnvironmentPresentation();
	}

	TMap<TWeakObjectPtr<UPrimitiveComponent>, EABTSStylizedObjectClass> Desired;
	int32 M3SemanticCount = 0;
	int32 M7SemanticCount = 0;
	int32 M7MaterialSlotCount = 0;
	int32 M11SemanticCount = 0;
	int32 PlayerSemanticCount = 0;
	int32 SlingshotSemanticCount = 0;
	int32 M3SurfaceStyleCount = 0;
	int32 M3BackgroundMaterialCount = 0;
	int32 M11FinaleMaterialCount = 0;
	int32 SharedBirdMaterialCount = 0;
	int32 SharedSlingshotMaterialCount = 0;
	TArray<FABTSStylizedMaterialSlotBinding> DesiredMaterialBindings;
	TArray<AABTSM7BuildingMaterialSystem*> M7Authorities;
	for (TActorIterator<AABTSM7BuildingMaterialSystem> It(World); It; ++It)
	{
		if (IsValid(*It) && !It->IsActorBeingDestroyed())
		{
			M7Authorities.Add(*It);
		}
	}
	bM7SemanticAdapterReady = M7Authorities.Num() == 1;
	FABTSM7StylizedMaterialSet M7MaterialSet;
	FString M7MaterialFailure;
	bM7MaterialSetReady = bM7SemanticAdapterReady
		&& FABTSM7StylizedRenderingAdapter::TryLoadMaterialSet(
			M7MaterialSet,
			&M7MaterialFailure);

	auto AddPrimitive = [&Desired](
		UPrimitiveComponent* Component,
		const EABTSStylizedObjectClass ObjectClass)
	{
		if (IsValid(Component)
			&& FABTSStylizedRenderingContract::RequiresSelectiveStencil(
				ObjectClass))
		{
			Desired.Add(Component, ObjectClass);
		}
	};
	auto AddActor = [&AddPrimitive](
		const AActor& Actor,
		const EABTSStylizedObjectClass ObjectClass)
	{
		TArray<UPrimitiveComponent*> Primitives;
		GatherActorPrimitives(Actor, Primitives);
		for (UPrimitiveComponent* Primitive : Primitives)
		{
			AddPrimitive(Primitive, ObjectClass);
		}
	};

	// M3 surface parameters must be refreshed outside the Style-On-only
	// semantic pass so a 1 -> 0 transition restores the same TerrainMID. Tree
	// and rock slots remain read-only publications consumed by the Integration
	// registry, which owns exact source-material restoration.
	for (TActorIterator<AABTSM3Planet> It(World); It; ++It)
	{
		if (It->ApplyStylizedSurfaceStyle(bLastObservedStyleEnabled))
		{
			++M3SurfaceStyleCount;
		}
		TArray<FABTSStylizedMaterialSlotBinding> M3MaterialBindings;
		FABTSM3StylizedMaterialAdapter::GatherBackgroundPropMaterialBindings(
			**It,
			M3MaterialBindings);
		M3BackgroundMaterialCount += M3MaterialBindings.Num();
		DesiredMaterialBindings.Append(MoveTemp(M3MaterialBindings));
	}

	if (bLastObservedStyleEnabled)
	{
		if (bM7SemanticAdapterReady)
		{
			TArray<FABTSM7StylizedSemanticBinding> Bindings;
			FABTSM7StylizedRenderingAdapter::GatherSemanticBindings(
				*M7Authorities[0],
				Bindings);
			M7SemanticCount = Bindings.Num();
			for (const FABTSM7StylizedSemanticBinding& Binding : Bindings)
			{
				AddPrimitive(
					const_cast<UPrimitiveComponent*>(Binding.Component),
					Binding.ObjectClass);
			}
			if (bM7MaterialSetReady)
			{
				TArray<FABTSStylizedMaterialSlotBinding> M7MaterialBindings;
				FABTSM7StylizedAdapterReadiness M7Readiness;
				FABTSM7StylizedRenderingAdapter::GatherMaterialBindings(
					*M7Authorities[0],
					M7MaterialSet,
					M7MaterialBindings,
					&M7Readiness);
				bM7SemanticAdapterReady = M7Readiness.bSemanticReady;
				bM7MaterialSetReady = M7Readiness.bMaterialSetReady;
				M7MaterialSlotCount = M7Readiness.PublishedSlotCount;
				DesiredMaterialBindings.Append(MoveTemp(M7MaterialBindings));
			}
		}
		for (TActorIterator<AABTSM3Planet> It(World); It; ++It)
		{
			TArray<FABTSM3StylizedSemanticBinding> Bindings;
			FABTSM3StylizedSemanticAdapter::GatherPrimaryPlanetSemantics(
				**It,
				Bindings);
			M3SemanticCount += Bindings.Num();
			for (const FABTSM3StylizedSemanticBinding& Binding : Bindings)
			{
				AddPrimitive(
					const_cast<UPrimitiveComponent*>(Binding.Component),
					Binding.ObjectClass);
			}
		}
		for (TActorIterator<AABTSM9Satellite> It(World); It; ++It)
		{
			TArray<FABTSM3StylizedSemanticBinding> Bindings;
			FABTSM3StylizedSemanticAdapter::GatherSatelliteSemantics(**It, Bindings);
			M3SemanticCount += Bindings.Num();
			for (const FABTSM3StylizedSemanticBinding& Binding : Bindings)
			{
				if (Binding.Actor)
				{
					AddActor(*Binding.Actor, Binding.ObjectClass);
				}
			}
		}
		for (TActorIterator<AABTSM3MonthlySatellitePracticeRuntime> It(World);
			It;
			++It)
		{
			TArray<FABTSM3StylizedSemanticBinding> Bindings;
			FABTSM3StylizedSemanticAdapter::GatherMonthlyPracticeSemantics(
				**It,
				Bindings);
			M3SemanticCount += Bindings.Num();
			for (const FABTSM3StylizedSemanticBinding& Binding : Bindings)
			{
				if (Binding.Actor)
				{
					AddActor(*Binding.Actor, Binding.ObjectClass);
				}
			}
		}

		for (TActorIterator<AABTSBirdParty> It(World); It; ++It)
		{
			for (AABTSM25BirdCharacter* Bird : It->GetPartyMembers())
			{
				if (IsValid(Bird))
				{
					AddActor(*Bird, EABTSStylizedObjectClass::PlayerBird);
					if (bSharedMaterialPreloadReady)
					{
						SharedBirdMaterialCount +=
							FABTSSharedStylizedMaterialAdapter::GatherActorBindings(
								*Bird,
								DesiredMaterialBindings);
					}
					++PlayerSemanticCount;
				}
			}
		}
		for (TActorIterator<AABTSOpeningCinematicPreview> It(World); It; ++It)
		{
			if (bSharedMaterialPreloadReady)
			{
				SharedBirdMaterialCount +=
					FABTSSharedStylizedMaterialAdapter::GatherActorBindings(
						**It,
						DesiredMaterialBindings);
			}
		}

		for (TActorIterator<AABTSM6SlingshotSystem> It(World); It; ++It)
		{
			TArray<UPrimitiveComponent*> Primitives;
			It->GatherActiveSlingshotPrimitives(Primitives);
			for (UPrimitiveComponent* Primitive : Primitives)
			{
				AddPrimitive(Primitive, EABTSStylizedObjectClass::Slingshot);
			}
			if (bSharedMaterialPreloadReady)
			{
				SharedSlingshotMaterialCount +=
					FABTSSharedStylizedMaterialAdapter::GatherPrimitiveBindings(
						Primitives,
						DesiredMaterialBindings);
			}
			SlingshotSemanticCount += Primitives.Num();
		}

		for (TActorIterator<AABTSM11FinaleSystem> It(World); It; ++It)
		{
			for (AABTSM11GravityBodyActor* Actor : It->GetGravityBodyActors())
			{
				if (!IsValid(Actor))
				{
					continue;
				}
				EABTSStylizedObjectClass ObjectClass =
					EABTSStylizedObjectClass::None;
				if (It->TryGetStylizedObjectClass(*Actor, ObjectClass))
				{
					AddActor(*Actor, ObjectClass);
					++M11SemanticCount;
				}
			}
			if (AABTSM11UFOActor* UFO = It->GetUFOActor())
			{
				EABTSStylizedObjectClass ObjectClass =
					EABTSStylizedObjectClass::None;
				if (It->TryGetStylizedObjectClass(*UFO, ObjectClass))
				{
					AddActor(*UFO, ObjectClass);
					++M11SemanticCount;
				}
			}

			if (It->IsLayoutReady())
			{
				TArray<FABTSStylizedMaterialSlotBinding> M11MaterialBindings;
				FABTSM11StylizedMaterialAdapter::CollectBindings(
					**It,
					M11MaterialBindings);
				M11FinaleMaterialCount += M11MaterialBindings.Num();
				DesiredMaterialBindings.Append(MoveTemp(M11MaterialBindings));
			}
		}
	}
	PrimitiveRegistry->Apply(Desired);

	MaterialRegistry->Apply(
		DesiredMaterialBindings,
		FABTSStylizedRenderingControl::IsEnabled());

	TMap<TWeakObjectPtr<USceneCaptureComponent2D>, EABTSStylizedViewClass>
		DesiredCaptures;
	for (TActorIterator<AABTSM10ScoutMapSystem> It(World); It; ++It)
	{
		AABTSM101LandingPreviewCamera* Camera = It->GetLandingPreviewCamera();
		if (!IsValid(Camera) || !Camera->IsPreviewActive())
		{
			continue;
		}
		USceneCaptureComponent2D* Capture = Camera->GetSceneCaptureComponent();
		if (!IsValid(Capture))
		{
			continue;
		}
		if (Camera->GetPreviewSubject()
			== EABTSM101PreviewSubject::PrimaryLanding)
		{
			DesiredCaptures.Add(
				Capture,
				EABTSStylizedViewClass::GroundLandingPreview);
		}
		else if (Camera->GetPreviewSubject()
			== EABTSM101PreviewSubject::SatelliteLanding)
		{
			DesiredCaptures.Add(
				Capture,
				EABTSStylizedViewClass::SatelliteLandingPreview);
		}
	}
	for (TActorIterator<AABTSM11FinaleInteractionSystem> It(World); It; ++It)
	{
		if (USceneCaptureComponent2D* Capture =
			It->GetFinaleRemotePreviewCaptureComponent())
		{
			DesiredCaptures.Add(
				Capture,
				It->GetFinaleRemotePreviewStylizedViewClass());
		}
	}

	for (auto It = RegisteredCaptures.CreateIterator(); It; ++It)
	{
		USceneCaptureComponent2D* Capture = It->Get();
		if (!IsValid(Capture) || !DesiredCaptures.Contains(*It))
		{
			if (IsValid(Capture))
			{
				FABTSStylizedSceneCaptureRegistry::Unregister(*Capture);
			}
			It.RemoveCurrent();
		}
	}
	for (const TPair<TWeakObjectPtr<USceneCaptureComponent2D>,
		EABTSStylizedViewClass>& Pair : DesiredCaptures)
	{
		if (USceneCaptureComponent2D* Capture = Pair.Key.Get())
		{
			if (FABTSStylizedSceneCaptureRegistry::Register(*Capture, Pair.Value))
			{
				RegisteredCaptures.Add(Capture);
			}
		}
	}

	uint64 DiagnosticSummaryHash = GetTypeHash(M3SemanticCount);
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(M7SemanticCount));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(M7MaterialSlotCount));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(bM7SemanticAdapterReady));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(bM7MaterialSetReady));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(M11SemanticCount));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(PlayerSemanticCount));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(SlingshotSemanticCount));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(PrimitiveRegistry->Num()));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(RegisteredCaptures.Num()));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(PrimitiveRegistry->GetConflictCount()));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(MaterialRegistry->Num()));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(MaterialRegistry->GetConflictCount()));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(MaterialRegistry->GetRejectedBindingCount()));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(M3SurfaceStyleCount));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(M3BackgroundMaterialCount));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(M11FinaleMaterialCount));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(SharedBirdMaterialCount));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(SharedSlingshotMaterialCount));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(bSharedMaterialPreloadReady));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(PreloadedSharedMaterials.Num()));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(FABTSSharedStylizedMaterialAdapter::GetCatalogHash()));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(FABTSStylizedMaterialContract::GetContractHash()));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(FABTSStylizedRenderingControl::IsEnabled()));
	if (LastDiagnosticSummaryHash != DiagnosticSummaryHash)
	{
		LastDiagnosticSummaryHash = DiagnosticSummaryHash;
		if (bM7SemanticAdapterReady && !bM7MaterialSetReady)
		{
			UE_LOG(
				LogABTSRuntime,
				Warning,
				TEXT("[ABTS][Rendering][T3-B] M7MaterialSetReady=0 Reason=%s"),
				M7MaterialFailure.IsEmpty()
					? TEXT("IncompleteFixedCandidateSet")
					: *M7MaterialFailure);
		}
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][Rendering][T2-B2] M3Semantics=%d M7AdapterReady=%d M7Semantics=%d M7MaterialSetReady=%d M7MaterialSlots=%d M7Authorities=%d M11Semantics=%d Birds=%d SlingshotPrimitives=%d SelectiveProducers=%d PreviewViews=%d Conflicts=%d Style=%d"),
			M3SemanticCount,
			bM7SemanticAdapterReady ? 1 : 0,
			M7SemanticCount,
			bM7MaterialSetReady ? 1 : 0,
			M7MaterialSlotCount,
			M7Authorities.Num(),
			M11SemanticCount,
			PlayerSemanticCount,
			SlingshotSemanticCount,
			PrimitiveRegistry->Num(),
			RegisteredCaptures.Num(),
			PrimitiveRegistry->GetConflictCount(),
			FABTSStylizedRenderingControl::IsEnabled() ? 1 : 0);
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][Rendering][T3-A0] MaterialSlots=%d MaterialConflicts=%d MaterialRejected=%d MaterialContractVersion=%d MaterialContractHash=%u Style=%d"),
			MaterialRegistry->Num(),
			MaterialRegistry->GetConflictCount(),
			MaterialRegistry->GetRejectedBindingCount(),
			FABTSStylizedMaterialContract::GetVersion(),
			FABTSStylizedMaterialContract::GetContractHash(),
			FABTSStylizedRenderingControl::IsEnabled() ? 1 : 0);
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][Rendering][T3-A1] SurfaceStyles=%d BackgroundMaterialSlots=%d AppliedSlots=%d Conflicts=%d Rejected=%d Style=%d"),
			M3SurfaceStyleCount,
			M3BackgroundMaterialCount,
			MaterialRegistry->Num(),
			MaterialRegistry->GetConflictCount(),
			MaterialRegistry->GetRejectedBindingCount(),
			bLastObservedStyleEnabled ? 1 : 0);
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][Rendering][T3-A3] FinaleMaterialSlots=%d AppliedSlots=%d Conflicts=%d Rejected=%d Style=%d"),
			M11FinaleMaterialCount,
			MaterialRegistry->Num(),
			MaterialRegistry->GetConflictCount(),
			MaterialRegistry->GetRejectedBindingCount(),
			bLastObservedStyleEnabled ? 1 : 0);
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][Rendering][T3-A2] BirdMaterialSlots=%d SlingshotMaterialSlots=%d SharedCatalogEntries=%d SharedCatalogHash=%u Preloaded=%d PreloadReady=%d AppliedSlots=%d Conflicts=%d Rejected=%d Style=%d"),
			SharedBirdMaterialCount,
			SharedSlingshotMaterialCount,
			FABTSSharedStylizedMaterialAdapter::GetCatalogEntryCount(),
			FABTSSharedStylizedMaterialAdapter::GetCatalogHash(),
			PreloadedSharedMaterials.Num(),
			bSharedMaterialPreloadReady ? 1 : 0,
			MaterialRegistry->Num(),
			MaterialRegistry->GetConflictCount(),
			MaterialRegistry->GetRejectedBindingCount(),
			FABTSStylizedRenderingControl::IsEnabled() ? 1 : 0);
	}
}

void UABTSStylizedRenderingWorldSubsystem::RefreshCloudTraversalNow(
	const bool bForceImmediate)
{
	UpdateCloudTraversalVisibility(0.0f, bForceImmediate);
}

void UABTSStylizedRenderingWorldSubsystem::UpdateCloudTraversalVisibility(
	const float DeltaTime,
	const bool bForceImmediate)
{
	UWorld* World = GetWorld();
	if (World == nullptr
		|| !LowPolyCloudPrototypeActor.IsValid()
		|| LowPolyCloudDefinitions.IsEmpty()
		|| LowPolyCloudMaterials.Num() != 1
		|| LowPolyCloudTraversalStrengths.Num() != 1)
	{
		return;
	}

	APlayerController* Controller = World->GetFirstPlayerController();
	APlayerCameraManager* CameraManager = IsValid(Controller)
		? Controller->PlayerCameraManager
		: nullptr;
	AABTSBirdParty* Party = nullptr;
	for (TActorIterator<AABTSBirdParty> It(World); It; ++It)
	{
		if (It->IsPartyReady())
		{
			Party = *It;
			break;
		}
	}
	AABTSM25BirdCharacter* ControlledBird = IsValid(Party)
		? Party->GetControlledBird()
		: nullptr;
	if (!IsValid(CameraManager) || !IsValid(ControlledBird))
	{
		LowPolyCloudTraversalStrengths[0] = 0.0f;
		if (UMaterialInstanceDynamic* Material =
			LowPolyCloudMaterials[0].Get())
		{
			Material->SetScalarParameterValue(
				TEXT("ABTS_CloudTraversalActive"), 0.0f);
			Material->SetScalarParameterValue(
				TEXT("ABTS_CloudTraversalProtectionActive"), 0.0f);
		}
		return;
	}

	// Protect each rendered bird independently.  A single capped party sphere
	// used to lose the formation endpoints, while actor/component bounds could
	// be offset from the Chaos-driven skeletal mesh that is actually visible.
	TArray<AABTSM25BirdCharacter*> ProtectedBirds;
	ProtectedBirds.Reserve(4);
	ProtectedBirds.Add(ControlledBird);
	for (AABTSM25BirdCharacter* Bird : Party->GetPartyMembers())
	{
		if (IsValid(Bird) && ProtectedBirds.Num() < 4)
		{
			ProtectedBirds.AddUnique(Bird);
		}
	}
	TArray<FSphere> BirdSpheres;
	BirdSpheres.Reserve(4);
	FBox BirdBounds(EForceInit::ForceInit);
	float MaxMotionPaddingCM = 0.0f;
	for (AABTSM25BirdCharacter* Bird : ProtectedBirds)
	{
		USkeletalMeshComponent* Visual = Bird->GetBirdVisual();
		const FVector Center = IsValid(Visual)
			? Visual->Bounds.Origin
			: Bird->GetActorLocation();
		const float VisualRadiusCM = IsValid(Visual)
			? FMath::Clamp(static_cast<float>(Visual->Bounds.SphereRadius),
				65.0f, 190.0f)
			: 100.0f;
		const float BirdSpeedCMPS = FMath::Max(
			static_cast<float>(Bird->GetVelocity().Size()),
			static_cast<float>(Bird->GetSlingshotVelocity().Size()));
		// Two-frame conservative lead covers subsystem/visual tick ordering and
		// high-speed slingshot motion without opening one giant party-sized hole.
		const float MotionPaddingCM = FMath::Min(
			BirdSpeedCMPS * FMath::Max(DeltaTime, 1.0f / 60.0f) * 2.0f,
			260.0f);
		MaxMotionPaddingCM = FMath::Max(MaxMotionPaddingCM, MotionPaddingCM);
		const float RadiusCM = VisualRadiusCM + 70.0f + MotionPaddingCM;
		BirdSpheres.Emplace(Center, RadiusCM);
		const FVector Extent(RadiusCM);
		BirdBounds += Center - Extent;
		BirdBounds += Center + Extent;
	}
	check(BirdSpheres.Num() > 0);
	const FVector BirdWorld = BirdBounds.GetCenter();
	float BirdRadiusCM = 120.0f;
	for (const FSphere& Sphere : BirdSpheres)
	{
		BirdRadiusCM = FMath::Max(
			BirdRadiusCM,
			static_cast<float>(FVector::Distance(BirdWorld, Sphere.Center)
				+ Sphere.W));
	}
	const FVector CameraWorld = CameraManager->GetCameraLocation();
	// High-speed slingshot travel is ordinary continuous camera motion. The old
	// 1400 cm displacement heuristic classified it as a cut and snapped the
	// traversal aperture between binary states.
	const bool bCameraCut = bForceImmediate
		|| !bHasPreviousCloudTraversalCamera
		|| CameraManager->bGameCameraCutThisFrame;
	bHasPreviousCloudTraversalCamera = true;

	uint64 DiagnosticHash = 1469598103934665603ull;
	int32 ActiveCloudCount = 0;
	float SharedTargetStrength = 0.0f;
	bool bSharedProtectionActive = false;
	const float CameraRadiusCM = FMath::Clamp(
		BirdRadiusCM * 1.20f, 250.0f, 500.0f);
	const float CorridorRadiusCM = FMath::Clamp(
		BirdRadiusCM * 0.72f, 145.0f, 310.0f);
	for (int32 Index = 0; Index < LowPolyCloudDefinitions.Num(); ++Index)
	{
		const FABTST4CloudTraversalRelation Relation =
			FABTST4LowPolyCloudPrototype::EvaluateTraversalRelation(
				LowPolyCloudDefinitions[Index],
				CameraWorld,
				BirdWorld,
				BirdRadiusCM);
		SharedTargetStrength = FMath::Max(
			SharedTargetStrength, Relation.TraversalWeight);
		bSharedProtectionActive |= Relation.bTraversalActive;
		if (Relation.bTraversalActive)
		{
			++ActiveCloudCount;
		}
		DiagnosticHash = HashCombineFast(
			DiagnosticHash,
			GetTypeHash(
				(Index + 1) * 17
				+ (Relation.bCameraInside ? 1 : 0)
				+ (Relation.bBirdInside ? 2 : 0)
				+ (Relation.bCloudBetweenCameraAndBird ? 4 : 0)));
	}

	UMaterialInstanceDynamic* SharedMaterial = LowPolyCloudMaterials[0].Get();
	if (!IsValid(SharedMaterial))
	{
		return;
	}
	float& SharedStrength = LowPolyCloudTraversalStrengths[0];
	if (bCameraCut)
	{
		SharedStrength = SharedTargetStrength;
	}
	else
	{
		const float Speed = SharedTargetStrength > SharedStrength ? 10.0f : 6.0f;
		SharedStrength = FMath::FInterpTo(
			SharedStrength,
			SharedTargetStrength,
			FMath::Max(0.0f, DeltaTime),
			Speed);
	}
	SharedMaterial->SetScalarParameterValue(
		TEXT("ABTS_CloudTraversalActive"), SharedStrength);
	SharedMaterial->SetScalarParameterValue(
		TEXT("ABTS_CloudTraversalProtectionActive"),
		bSharedProtectionActive ? 1.0f : 0.0f);
	SharedMaterial->SetVectorParameterValue(
		TEXT("ABTS_CloudTraversalCameraWorld"),
		FLinearColor(CameraWorld.X, CameraWorld.Y, CameraWorld.Z, 0.0f));
	SharedMaterial->SetVectorParameterValue(
		TEXT("ABTS_CloudTraversalBirdWorld"),
		FLinearColor(BirdWorld.X, BirdWorld.Y, BirdWorld.Z, 0.0f));
	SharedMaterial->SetScalarParameterValue(
		TEXT("ABTS_CloudTraversalCameraRadiusCM"), CameraRadiusCM);
	SharedMaterial->SetScalarParameterValue(
		TEXT("ABTS_CloudTraversalBirdRadiusCM"), BirdRadiusCM + 65.0f);
	SharedMaterial->SetScalarParameterValue(
		TEXT("ABTS_CloudTraversalBirdCount"),
		static_cast<float>(BirdSpheres.Num()));
	for (int32 BirdIndex = 0; BirdIndex < 4; ++BirdIndex)
	{
		const FSphere Sphere = BirdSpheres.IsValidIndex(BirdIndex)
			? BirdSpheres[BirdIndex]
			: FSphere(FVector::ZeroVector, 1.0);
		SharedMaterial->SetVectorParameterValue(
			*FString::Printf(
				TEXT("ABTS_CloudTraversalBirdSphere%d"), BirdIndex),
			FLinearColor(
				Sphere.Center.X,
				Sphere.Center.Y,
				Sphere.Center.Z,
				Sphere.W));
	}
	SharedMaterial->SetScalarParameterValue(
		TEXT("ABTS_CloudTraversalCorridorRadiusCM"), CorridorRadiusCM);
	SharedMaterial->SetScalarParameterValue(
		TEXT("ABTS_CloudTraversalFeatherCM"), 95.0f);
	SharedMaterial->SetScalarParameterValue(
		TEXT("ABTS_CloudTraversalRetainedCoverage"), 0.82f);
	SharedMaterial->SetScalarParameterValue(
		TEXT("ABTS_CloudTraversalMaskFrequency"), 0.012f);
	if (DiagnosticHash != LastCloudTraversalDiagnosticHash)
	{
		LastCloudTraversalDiagnosticHash = DiagnosticHash;
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][Rendering][T4-A2.3.1][Traversal] ActiveClouds=%d Camera=%s PartyCenter=%s PartyRadiusCM=%.1f ProtectedBirds=%d MaxMotionPaddingCM=%.1f Veil=Removed PixelAnimationTSR=1 CameraCut=%d RelationHash=0x%016llX"),
			ActiveCloudCount,
			*CameraWorld.ToCompactString(),
			*BirdWorld.ToCompactString(),
			BirdRadiusCM,
			BirdSpheres.Num(),
			MaxMotionPaddingCM,
			bCameraCut ? 1 : 0,
			static_cast<unsigned long long>(DiagnosticHash));
	}
}

void UABTSStylizedRenderingWorldSubsystem::RefreshEnvironmentPresentation()
{
	if (!EnvironmentPresentation)
	{
		ReleaseEnvironmentOwnership(
			TEXT("EnvironmentPresentationUnavailable"),
			false);
		return;
	}

	if (!FABTSStylizedRenderingControl::IsEnabled()
		|| !bEnvironmentSnapshotReady)
	{
		ReleaseEnvironmentOwnership(
			FABTSStylizedRenderingControl::IsEnabled()
				? TEXT("EnvironmentSnapshotUnavailable")
				: TEXT("StyleDisabled"),
			false);
		return;
	}

	const FABTSStylizedEnvironmentParameters Parameters =
		FABTSStylizedRenderingControl::BuildEnvironmentParameters(
			EnvironmentSnapshot.PlanetCenterWorld,
			EnvironmentSnapshot.PlanetRadiusCM,
			EnvironmentSnapshot.SunDirectionToSunWorld,
			EnvironmentSnapshot.Profile);
	FString Failure;
	UWorld* World = GetWorld();
	const bool bWasApplied = EnvironmentPresentation->IsApplied();
	if (World == nullptr
		|| !EnvironmentPresentation->Apply(
			*World,
			EnvironmentSnapshot,
			Parameters,
			Failure))
	{
		ReleaseEnvironmentOwnership(
			TEXT("EnvironmentPresentationApplyFailed"),
			false);
		UE_LOG(
			LogABTSRuntime,
			Warning,
			TEXT("[ABTS][Rendering][T4-A2][Environment] Applied=0 Reason=%s"),
			Failure.IsEmpty() ? TEXT("WorldUnavailable") : *Failure);
		return;
	}
	if (!RefreshLowPolyCloudPrototype(Parameters, Failure))
	{
		ReleaseEnvironmentOwnership(
			TEXT("CloudPrototypeApplyFailed"),
			false);
		UE_LOG(
			LogABTSRuntime,
			Warning,
			TEXT("[ABTS][Rendering][T4-A2R1A][CloudPrototype] Applied=0 Reason=%s"),
			Failure.IsEmpty() ? TEXT("Unknown") : *Failure);
		return;
	}

	FABTSStylizedRenderingControl::SetEnvironmentParameters(Parameters);
	if (!bEnvironmentOwnershipActive)
	{
		bEnvironmentOwnershipActive = true;
		++EnvironmentOwnershipGeneration;
	}
	if (!bWasApplied)
	{
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][Rendering][T4-A1][AtmosphereQuality] %s TraceScale=%.2f"),
			*ABTSStylizedRenderingWorldSubsystemPrivate::
				FContinuousAtmosphereOverride::DescribeEffectiveValues(),
			ABTSStylizedRenderingWorldSubsystemPrivate::
				ContinuousAtmosphereTraceSampleCountScale);
	}
	if (!bWasApplied && EnvironmentPresentation->IsCloudApplied())
	{
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][Rendering][T4-A2.4][CloudQuality] Route=SingleHISMBatchedIslandFields LogicalClouds=%d BackgroundClouds=%d TerminatorMegaClouds=%d WeatherSystems=%d MacroClusters=%d Cloudlets=%d Body=%d Crown=%d Edge=%d HISMComponents=1 MaterialBatches=1 GlobalCoverage=1 BackgroundSunIndependentPlacement=1 TerminatorMegaSunRelative=1 TerminatorMegaConnected=1 SizeVariation=1 FusionDiagnostics=5 SphericalConformal=1 DetachedEdges=0 CustomDataFloats=%d Deterministic=1 Material=MaskedUnlit ViewInvariantIslandField=1 ViewInvariantVolumeGradient=1 CameraDependentLighting=0 ContinuousMacroNormal=1 GradientCoherenceGuard=1 GradientJunctionGate=1 PlanarCoreClosure=1 UndersideField=1 CriticalPointFallback=IslandUp ThreeBandColor=1 SunwardWhitening=1 ThinDensityWhitening=1 ViewIndependentWhitening=1 LocalSolarHeight=1 NightWhiteningGate=1 NightBrightness=%.2f DayBlend=[%.2f,%.2f] GenericObjectToneBypass=1 MacroNormalStrength=0.84 PixelLocalNormalWeight=0 PixelInstanceVariation=0 VertexNoiseWPO=1 PixelAnimationTSR=1 CloudCompositeStencil=%d CloudToCloudOutlineSuppression=1 CloudToWorldOutlinePreserved=1 BoundedTraversal=1 SharedSpatialTraversalMask=1 CameraSphere=1 PerBirdVisualSpheres=4 ImmediateHardProtection=1 CameraBirdCorridor=1 StablePlanarNoiseCoverage=1 RetainedCoverage=0.82 MaskFrequency=0.012 FullScreenVeil=Removed FullTranslucency=0 TraversalAffectsLighting=0 LogicalHash=%llu NativeActorHidden=1 Collision=0 Shadows=0 LayoutHash=%llu BaseCM=%.1f HeightCM=%.1f"),
			LowPolyLogicalCloudCount,
			LowPolyLogicalCloudCount
				- FABTST4LowPolyCloudPrototype::TerminatorMegaClusterIslandCount,
			FABTST4LowPolyCloudPrototype::TerminatorMegaClusterIslandCount,
			FABTST4LowPolyCloudPrototype::WeatherSystemCount,
			LowPolyLogicalCloudCount
				* FABTST4LowPolyCloudPrototype::MacroClusterCountPerIsland,
			LowPolyLogicalCloudCount
				* FABTST4LowPolyCloudPrototype::CloudletsPerIsland,
			LowPolyLogicalCloudCount
				* FABTST4LowPolyCloudPrototype::BodyCloudletsPerIsland,
			LowPolyLogicalCloudCount
				* FABTST4LowPolyCloudPrototype::CrownCloudletsPerIsland,
			LowPolyLogicalCloudCount
				* FABTST4LowPolyCloudPrototype::EdgeCloudletsPerIsland,
			FABTST4LowPolyCloudPrototype::CloudletCustomDataFloatCount,
			FABTST4LowPolyCloudPrototype::NightBrightness,
			FABTST4LowPolyCloudPrototype::DaylightBlendMinSolarHeight,
			FABTST4LowPolyCloudPrototype::DaylightBlendMaxSolarHeight,
			static_cast<int32>(FABTSStylizedRenderingContract::
				ResolveCloudCompositeStencilValueForRenderer()),
			static_cast<unsigned long long>(LowPolyLogicalCloudLayoutHash),
			static_cast<unsigned long long>(LowPolyCloudLayoutHash),
			Parameters.CloudBaseAltitudeCM,
			Parameters.CloudLayerHeightCM);
	}
	UE_LOG(
		LogABTSRuntime,
		VeryVerbose,
		TEXT("[ABTS][Rendering][T4-A2][Environment] Applied=1 Profile=%d RadiusCM=%.2f AtmosphereHeightCM=%.2f FogHidden=%d Cloud=%d StarSeed=%u"),
		static_cast<int32>(Parameters.Profile),
		Parameters.PlanetRadiusCM,
		Parameters.AtmosphereHeightCM,
		EnvironmentPresentation->GetFogCount(),
		EnvironmentPresentation->IsCloudApplied() ? 1 : 0,
		Parameters.StarSeed);
}

bool UABTSStylizedRenderingWorldSubsystem::RefreshLowPolyCloudPrototype(
	const FABTSStylizedEnvironmentParameters& Parameters,
	FString& OutFailure)
{
	OutFailure.Reset();
	if (Parameters.bCloudsEnabled == 0u)
	{
		DestroyLowPolyCloudPrototype();
		return true;
	}
	if (FParse::Param(
		FCommandLine::Get(),
		TEXT("ABTSToonT4A2DisableClouds")))
	{
		// A2.4 performance baseline: EnvironmentPresentation has already hidden
		// the native volumetric cloud, so this produces a true no-cloud scene
		// without changing the accepted production profile or serialized assets.
		DestroyLowPolyCloudPrototype();
		return true;
	}
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		OutFailure = TEXT("Cloud prototype world is unavailable.");
		return false;
	}
	const FABTST4CloudFieldTuningState EffectiveTuning =
		GetCloudFieldTuningState();
	const uint32 EffectiveCloudSeed = EffectiveTuning.bOverrideActive
		? EffectiveTuning.Seed
		: Parameters.StarSeed ^ 0xC10DF13Du;
	const TArray<int32> BackgroundClusterMemberCounts =
		FABTST4LowPolyCloudPrototype::BuildGlobalClusterMemberCounts(
			EffectiveCloudSeed, EffectiveTuning.Distribution);
	int32 ExpectedBackgroundClouds = 0;
	for (const int32 MemberCount : BackgroundClusterMemberCounts)
	{
		ExpectedBackgroundClouds += MemberCount;
	}
	if (BackgroundClusterMemberCounts.IsEmpty()
		|| ExpectedBackgroundClouds <= 0
		|| ExpectedBackgroundClouds
			> FABTST4LowPolyCloudPrototype::MaxGlobalIslandCount)
	{
		OutFailure = TEXT("Cloud distribution is empty or exceeds the PIE safety budget.");
		return false;
	}
	const int32 ExpectedLogicalClouds = ExpectedBackgroundClouds
		+ FABTST4LowPolyCloudPrototype::TerminatorMegaClusterIslandCount;
	const TArray<FABTST4LowPolyCloudIslandDefinition> Definitions =
		FABTST4LowPolyCloudPrototype::BuildDefinitions(
			Parameters.PlanetCenterWorld,
			Parameters.PlanetRadiusCM,
			EffectiveCloudSeed,
			FVector(Parameters.SunDirectionToSunWorld),
			Parameters.CloudBaseAltitudeCM,
			Parameters.CloudLayerHeightCM,
			EffectiveTuning.Distribution);
	if (Definitions.Num() != ExpectedLogicalClouds)
	{
		OutFailure = FString::Printf(
			TEXT("Global cloud field produced %d logical clouds; expected %d (%d background + %d terminator)."),
			Definitions.Num(),
			ExpectedLogicalClouds,
			ExpectedBackgroundClouds,
			FABTST4LowPolyCloudPrototype::TerminatorMegaClusterIslandCount);
		return false;
	}
	const uint64 DesiredLogicalCloudHash =
		FABTST4LowPolyCloudPrototype::ComputeLogicalCloudLayoutHash(Definitions);
	if (DesiredLogicalCloudHash == 0)
	{
		OutFailure = TEXT("Logical cloud identity layout is invalid.");
		return false;
	}
	if (!FABTST4LowPolyCloudPrototype::
		AreBackgroundWeatherClusterEnvelopesConnected(Definitions))
	{
		OutFailure = TEXT("One or more background weather clusters have a disconnected visible envelope.");
		return false;
	}
	if (FABTST4LowPolyCloudPrototype::CountTerminatorMegaClusterClouds(
		Definitions)
		!= FABTST4LowPolyCloudPrototype::TerminatorMegaClusterIslandCount)
	{
		OutFailure = TEXT("Terminator mega-cluster member count is incomplete.");
		return false;
	}
	if (!FABTST4LowPolyCloudPrototype::
		IsTerminatorMegaClusterEnvelopeConnected(Definitions))
	{
		OutFailure = TEXT("Terminator mega-cluster envelope is disconnected.");
		return false;
	}
	const double TerminatorMegaSpanDegrees = FABTST4LowPolyCloudPrototype::
		ComputeTerminatorMegaClusterAngularSpanDegrees(Definitions);
	if (TerminatorMegaSpanDegrees < 27.0 || TerminatorMegaSpanDegrees > 33.0)
	{
		OutFailure = FString::Printf(
			TEXT("Terminator mega-cluster span %.2f degrees is outside [27, 33]."),
			TerminatorMegaSpanDegrees);
		return false;
	}
	const uint64 DesiredLayoutHash =
		FABTST4LowPolyCloudPrototype::ComputeLayoutHash(Definitions);
	uint64 DesiredCloudletHash = DesiredLayoutHash;
	DesiredCloudletHash ^= DesiredLogicalCloudHash + 0x9e3779b97f4a7c15ull
		+ (DesiredCloudletHash << 6) + (DesiredCloudletHash >> 2);
	TArray<TArray<FABTST4InstancedCloudletDefinition>> IslandCloudlets;
	IslandCloudlets.SetNum(Definitions.Num());
	int32 TotalCloudlets = 0;
	for (int32 DefinitionIndex = 0;
		DefinitionIndex < Definitions.Num();
		++DefinitionIndex)
	{
		FString CloudletFailure;
		if (!FABTST4LowPolyCloudPrototype::BuildInstancedCloudlets(
			Definitions[DefinitionIndex],
			IslandCloudlets[DefinitionIndex],
			&CloudletFailure))
		{
			OutFailure = FString::Printf(
				TEXT("Cloud island %d instance layout failed: %s"),
				Definitions[DefinitionIndex].IslandIndex,
				*CloudletFailure);
			return false;
		}
		const uint64 IslandHash =
			FABTST4LowPolyCloudPrototype::ComputeCloudletLayoutHash(
				IslandCloudlets[DefinitionIndex]);
		DesiredCloudletHash ^= IslandHash + 0x9e3779b97f4a7c15ull
			+ (DesiredCloudletHash << 6) + (DesiredCloudletHash >> 2);
		TotalCloudlets += IslandCloudlets[DefinitionIndex].Num();
	}
	const int32 ExpectedCloudlets = ExpectedLogicalClouds
		* FABTST4LowPolyCloudPrototype::CloudletsPerIsland;
	if (TotalCloudlets != ExpectedCloudlets)
	{
		OutFailure = FString::Printf(
			TEXT("Cloudlet budget mismatch: expected %d, produced %d."),
			ExpectedCloudlets,
			TotalCloudlets);
		return false;
	}
	if (LowPolyCloudPrototypeActor.IsValid()
		&& LowPolyCloudLayoutHash == DesiredCloudletHash)
	{
		LowPolyLogicalCloudLayoutHash = DesiredLogicalCloudHash;
		LowPolyLogicalCloudCount = Definitions.Num();
		return true;
	}
	DestroyLowPolyCloudPrototype();

	UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/Game/Toon/Environment/Cloud/M_ABTS_Toon_Cloudlet.M_ABTS_Toon_Cloudlet"));
	if (!IsValid(BaseMaterial))
	{
		OutFailure = TEXT("T4-A2R1-B cloudlet material is unavailable.");
		return false;
	}
	float MaterialMacroLightingVersion = 0.0f;
	float MaterialNightBrightness = 0.0f;
	float MaterialDaylightBlendMin = 0.0f;
	float MaterialDaylightBlendMax = 0.0f;
	float MaterialTraversalRetainedCoverage = 0.0f;
	float MaterialTraversalMaskFrequency = 0.0f;
	float MaterialTraversalProtectionActive = -1.0f;
	float MaterialTraversalBirdCount = -1.0f;
	FLinearColor MaterialPlanetCenter = FLinearColor::Transparent;
	const bool bMaterialContractValid =
		BaseMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(TEXT("ABTS_CloudMacroLightingVersion")),
			MaterialMacroLightingVersion)
		&& BaseMaterial->GetVectorParameterValue(
			FMaterialParameterInfo(TEXT("ABTS_CloudPlanetCenter")),
			MaterialPlanetCenter)
		&& BaseMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(TEXT("ABTS_CloudNightBrightness")),
			MaterialNightBrightness)
		&& BaseMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(
				TEXT("ABTS_CloudDaylightBlendMinSolarHeight")),
			MaterialDaylightBlendMin)
		&& BaseMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(
				TEXT("ABTS_CloudDaylightBlendMaxSolarHeight")),
			MaterialDaylightBlendMax)
		&& BaseMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(
				TEXT("ABTS_CloudTraversalRetainedCoverage")),
			MaterialTraversalRetainedCoverage)
		&& BaseMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(
				TEXT("ABTS_CloudTraversalMaskFrequency")),
			MaterialTraversalMaskFrequency)
		&& BaseMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(
				TEXT("ABTS_CloudTraversalProtectionActive")),
			MaterialTraversalProtectionActive)
		&& BaseMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(TEXT("ABTS_CloudTraversalBirdCount")),
			MaterialTraversalBirdCount)
		&& BaseMaterial->HasPixelAnimation()
		&& FMath::IsNearlyEqual(MaterialMacroLightingVersion, 12.0f)
		&& FMath::IsNearlyEqual(MaterialTraversalRetainedCoverage, 0.82f)
		&& FMath::IsNearlyEqual(MaterialTraversalMaskFrequency, 0.012f)
		&& FMath::IsNearlyZero(MaterialTraversalProtectionActive)
		&& FMath::IsNearlyZero(MaterialTraversalBirdCount)
		&& FMath::IsNearlyEqual(
			MaterialNightBrightness,
			FABTST4LowPolyCloudPrototype::NightBrightness)
		&& FMath::IsNearlyEqual(
			MaterialDaylightBlendMin,
			FABTST4LowPolyCloudPrototype::DaylightBlendMinSolarHeight)
		&& FMath::IsNearlyEqual(
			MaterialDaylightBlendMax,
			FABTST4LowPolyCloudPrototype::DaylightBlendMaxSolarHeight);
	if (!bMaterialContractValid)
	{
		OutFailure = FString::Printf(
			TEXT("T4-A2.3.1 cloud material contract mismatch (Version=%.2f Night=%.2f Blend=[%.2f,%.2f] RetainedCoverage=%.2f MaskFrequency=%.3f PixelAnimation=%d Protection=%.1f BirdCount=%.1f)."),
			MaterialMacroLightingVersion,
			MaterialNightBrightness,
			MaterialDaylightBlendMin,
			MaterialDaylightBlendMax,
			MaterialTraversalRetainedCoverage,
			MaterialTraversalMaskFrequency,
			BaseMaterial->HasPixelAnimation() ? 1 : 0,
			MaterialTraversalProtectionActive,
			MaterialTraversalBirdCount);
		return false;
	}
	UStaticMesh* CloudletMesh = LoadObject<UStaticMesh>(
		nullptr,
		TEXT("/Game/Toon/Environment/Cloud/SM_ABTS_Toon_Cloudlet.SM_ABTS_Toon_Cloudlet"));
	if (!IsValid(CloudletMesh))
	{
		OutFailure = TEXT("T4-A2R1-B cloudlet mesh is unavailable.");
		return false;
	}
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = MakeUniqueObjectName(
		World,
		AActor::StaticClass(),
		TEXT("ABTST4InstancedCloudPrototype"));
	SpawnParameters.ObjectFlags |= RF_Transient;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Actor = World->SpawnActor<AActor>(
		AActor::StaticClass(),
		FTransform(FQuat::Identity, Parameters.PlanetCenterWorld),
		SpawnParameters);
	if (!IsValid(Actor))
	{
		OutFailure = TEXT("Unable to spawn the transient cloud prototype actor.");
		return false;
	}
	Actor->SetActorEnableCollision(false);
	USceneComponent* Root = NewObject<USceneComponent>(
		Actor,
		TEXT("CloudPrototypeRoot"),
		RF_Transient);
	if (!IsValid(Root))
	{
		Actor->Destroy();
		OutFailure = TEXT("Unable to allocate the cloud prototype root.");
		return false;
	}
	Actor->AddInstanceComponent(Root);
	Actor->SetRootComponent(Root);
	Root->RegisterComponent();
	Actor->SetActorLocation(
		Parameters.PlanetCenterWorld,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	LowPolyCloudDefinitions.Reset(Definitions.Num());
	LowPolyCloudMaterials.Reset(1);
	LowPolyCloudTraversalStrengths.Reset(1);

	UHierarchicalInstancedStaticMeshComponent* Component =
		NewObject<UHierarchicalInstancedStaticMeshComponent>(
			Actor,
			TEXT("CloudFieldInstances"),
			RF_Transient);
	if (!IsValid(Component))
	{
		Actor->Destroy();
		OutFailure = TEXT("Unable to allocate the batched cloud-field component.");
		return false;
	}
	Actor->AddInstanceComponent(Component);
	Component->SetupAttachment(Root);
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetStaticMesh(CloudletMesh);
	Component->SetNumCustomDataFloats(
		FABTST4LowPolyCloudPrototype::CloudletCustomDataFloatCount);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetGenerateOverlapEvents(false);
	Component->SetCanEverAffectNavigation(false);
	Component->SetCastShadow(false);
	Component->bCastDynamicShadow = false;
	Component->bCastStaticShadow = false;
	Component->SetCustomDepthStencilWriteMask(
		ERendererStencilMask::ERSM_Default);
	Component->SetCustomDepthStencilValue(
		FABTSStylizedRenderingContract::
			ResolveCloudCompositeStencilValueForRenderer());
	Component->SetRenderCustomDepth(true);

	auto SetVectorData = [](
		TArray<float>& Data,
		const int32 FirstIndex,
		const FVector& Value)
	{
		Data[FirstIndex] = static_cast<float>(Value.X);
		Data[FirstIndex + 1] = static_cast<float>(Value.Y);
		Data[FirstIndex + 2] = static_cast<float>(Value.Z);
	};
	for (int32 DefinitionIndex = 0;
		DefinitionIndex < Definitions.Num();
		++DefinitionIndex)
	{
		const FABTST4LowPolyCloudIslandDefinition& Definition =
			Definitions[DefinitionIndex];
		const TArray<FABTST4CloudMacroClusterDefinition> MacroClusters =
			FABTST4LowPolyCloudPrototype::BuildMacroClusters(Definition);
		if (MacroClusters.Num()
			!= FABTST4LowPolyCloudPrototype::MacroClusterCountPerIsland)
		{
			Actor->Destroy();
			OutFailure = TEXT("A2.4 batched cloud macro-field data are incomplete.");
			return false;
		}
		for (const FABTST4InstancedCloudletDefinition& Cloudlet
			: IslandCloudlets[DefinitionIndex])
		{
			const int32 InstanceIndex = Component->AddInstance(
				Cloudlet.TransformRelativeToPlanet, false);
			if (InstanceIndex == INDEX_NONE)
			{
				Actor->Destroy();
				OutFailure = TEXT("Unable to add a cloudlet instance.");
				return false;
			}
			TArray<float> CustomData;
			CustomData.SetNumZeroed(
				FABTST4LowPolyCloudPrototype::CloudletCustomDataFloatCount);
			CustomData[0] = Cloudlet.Seed01;
			CustomData[1] = Cloudlet.NormalizedHeight;
			CustomData[2] = Cloudlet.FakeOcclusion;
			CustomData[3] = Cloudlet.SizeTier;
			CustomData[4] = static_cast<float>(Cloudlet.Layer) / 2.0f;
			SetVectorData(
				CustomData,
				FABTST4LowPolyCloudPrototype::
					CloudletIslandCenterCustomDataIndex,
				Definition.CenterWorld);
			SetVectorData(
				CustomData,
				FABTST4LowPolyCloudPrototype::
					CloudletIslandAxisXCustomDataIndex,
				Definition.TangentX);
			SetVectorData(
				CustomData,
				FABTST4LowPolyCloudPrototype::
					CloudletIslandAxisYCustomDataIndex,
				Definition.TangentY);
			SetVectorData(
				CustomData,
				FABTST4LowPolyCloudPrototype::
					CloudletIslandUpCustomDataIndex,
				Definition.RadialUp);
			SetVectorData(
				CustomData,
				FABTST4LowPolyCloudPrototype::
					CloudletIslandExtentsCustomDataIndex,
				Definition.ExtentsCM);
			for (const FABTST4CloudMacroClusterDefinition& Cluster
				: MacroClusters)
			{
				const int32 FirstIndex =
					FABTST4LowPolyCloudPrototype::CloudletMacroCustomDataIndex
					+ Cluster.ClusterIndex
						* FABTST4LowPolyCloudPrototype::
							CloudletMacroCustomDataStride;
				CustomData[FirstIndex] =
					static_cast<float>(Cluster.NormalizedCenter.X);
				CustomData[FirstIndex + 1] =
					static_cast<float>(Cluster.NormalizedCenter.Y);
				CustomData[FirstIndex + 2] =
					static_cast<float>(Cluster.NormalizedRadii.X);
				CustomData[FirstIndex + 3] =
					static_cast<float>(Cluster.NormalizedRadii.Y);
				CustomData[FirstIndex + 4] =
					FMath::Cos(Cluster.OrientationRadians);
				CustomData[FirstIndex + 5] =
					FMath::Sin(Cluster.OrientationRadians);
				CustomData[FirstIndex + 6] = Cluster.HeightBias;
			}
			CustomData[FABTST4LowPolyCloudPrototype::
				CloudletColorVariantCustomDataIndex] =
				Definition.IslandIndex == 1 ? 1.0f : 0.0f;
			if (!Component->SetCustomData(
				InstanceIndex,
				MakeArrayView(CustomData),
				false))
			{
				Actor->Destroy();
				OutFailure = TEXT("Unable to assign batched cloud instance data.");
				return false;
			}
		}
		LowPolyCloudDefinitions.Add(Definition);
	}

	UMaterialInstanceDynamic* Material =
		UMaterialInstanceDynamic::Create(BaseMaterial, Component);
	if (!IsValid(Material))
	{
		Actor->Destroy();
		OutFailure = TEXT("Unable to allocate the A2.4 batched cloud material.");
		return false;
	}
	const FVector SunDirection =
		FVector(Parameters.SunDirectionToSunWorld).GetSafeNormal();
	Material->SetVectorParameterValue(
			TEXT("ABTS_CloudSunDirection"),
			FLinearColor(
				SunDirection.X,
				SunDirection.Y,
				SunDirection.Z,
				0.0f));
		Material->SetVectorParameterValue(
			TEXT("ABTS_CloudPlanetCenter"),
			FLinearColor(
				Parameters.PlanetCenterWorld.X,
				Parameters.PlanetCenterWorld.Y,
				Parameters.PlanetCenterWorld.Z,
				0.0f));
		Material->SetScalarParameterValue(
			TEXT("ABTS_CloudNightBrightness"),
			FABTST4LowPolyCloudPrototype::NightBrightness);
		Material->SetScalarParameterValue(
			TEXT("ABTS_CloudDaylightBlendMinSolarHeight"),
			FABTST4LowPolyCloudPrototype::DaylightBlendMinSolarHeight);
		Material->SetScalarParameterValue(
			TEXT("ABTS_CloudDaylightBlendMaxSolarHeight"),
			FABTST4LowPolyCloudPrototype::DaylightBlendMaxSolarHeight);
		Material->SetVectorParameterValue(
			TEXT("ABTS_CloudLightColor"),
			FLinearColor(0.92f, 0.93f, 0.96f, 1.0f));
		Material->SetVectorParameterValue(
			TEXT("ABTS_CloudBodyColor"),
			FLinearColor(0.45f, 0.53f, 0.64f, 1.0f));
		Material->SetVectorParameterValue(
			TEXT("ABTS_CloudShadowColor"),
			FLinearColor(0.18f, 0.24f, 0.34f, 1.0f));
		Material->SetScalarParameterValue(
			TEXT("ABTS_CloudSunWhiteStrength"), 0.70f);
		Material->SetScalarParameterValue(
			TEXT("ABTS_CloudThinWhiteStrength"), 0.58f);
		Material->SetScalarParameterValue(
			TEXT("ABTS_CloudThinDensityStart"), 0.30f);
		Material->SetScalarParameterValue(
			TEXT("ABTS_CloudThinDensityEnd"), 0.78f);
		Material->SetScalarParameterValue(
			TEXT("ABTS_CloudGradientConfidenceStart"), 0.10f);
		Material->SetScalarParameterValue(
			TEXT("ABTS_CloudGradientConfidenceEnd"), 0.34f);
		Material->SetScalarParameterValue(
			TEXT("ABTS_CloudTraversalActive"), 0.0f);
		Material->SetScalarParameterValue(
			TEXT("ABTS_CloudTraversalProtectionActive"), 0.0f);
		Material->SetScalarParameterValue(
			TEXT("ABTS_CloudTraversalBirdCount"), 0.0f);
		for (int32 BirdIndex = 0; BirdIndex < 4; ++BirdIndex)
		{
			Material->SetVectorParameterValue(
				*FString::Printf(
					TEXT("ABTS_CloudTraversalBirdSphere%d"), BirdIndex),
				FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));
		}
		Material->SetScalarParameterValue(
			TEXT("ABTS_CloudTraversalCameraRadiusCM"), 280.0f);
		Material->SetScalarParameterValue(
			TEXT("ABTS_CloudTraversalBirdRadiusCM"), 220.0f);
		Material->SetScalarParameterValue(
			TEXT("ABTS_CloudTraversalCorridorRadiusCM"), 150.0f);
		Material->SetScalarParameterValue(
			TEXT("ABTS_CloudTraversalFeatherCM"), 95.0f);
		Material->SetScalarParameterValue(
			TEXT("ABTS_CloudTraversalRetainedCoverage"), 0.82f);
		Material->SetScalarParameterValue(
			TEXT("ABTS_CloudTraversalMaskFrequency"), 0.012f);
	Component->SetMaterial(0, Material);
	Component->RegisterComponent();
	Component->MarkRenderStateDirty();
	LowPolyCloudMaterials.Add(Material);
	LowPolyCloudTraversalStrengths.Add(0.0f);
	LowPolyCloudPrototypeActor = Actor;
	LowPolyCloudLayoutHash = DesiredCloudletHash;
	LowPolyLogicalCloudLayoutHash = DesiredLogicalCloudHash;
	LowPolyLogicalCloudCount = Definitions.Num();
	UpdateCloudTraversalVisibility(0.0f, true);
	return true;
}

void UABTSStylizedRenderingWorldSubsystem::DestroyLowPolyCloudPrototype()
{
	if (AActor* Actor = LowPolyCloudPrototypeActor.Get())
	{
		Actor->Destroy();
	}
	LowPolyCloudPrototypeActor.Reset();
	LowPolyCloudLayoutHash = 0;
	LowPolyLogicalCloudLayoutHash = 0;
	LowPolyLogicalCloudCount = 0;
	LowPolyCloudDefinitions.Reset();
	LowPolyCloudMaterials.Reset();
	LowPolyCloudTraversalStrengths.Reset();
	bHasPreviousCloudTraversalCamera = false;
	LastCloudTraversalDiagnosticHash = 0;
}

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT2B1PrimitiveRegistryTest,
	"ABTS.Rendering.Toon.T2B1.PrimitiveRegistry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT2B1PrimitiveRegistryTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	UStaticMeshComponent* Component =
		NewObject<UStaticMeshComponent>(GetTransientPackage());
	TestNotNull(TEXT("Transient primitive is available"), Component);
	if (Component == nullptr)
	{
		return false;
	}
	Component->SetRenderCustomDepth(false);
	Component->SetCustomDepthStencilValue(19);

	UABTSStylizedRenderingWorldSubsystem::FPrimitiveOverrideRegistry Registry;
	TMap<TWeakObjectPtr<UPrimitiveComponent>, EABTSStylizedObjectClass> Desired;
	Desired.Add(Component, EABTSStylizedObjectClass::PlayerBird);
	Registry.Apply(Desired);
	TestTrue(TEXT("Selective producer is enabled"), Component->bRenderCustomDepth != 0);
	TestEqual(
		TEXT("Semantic class resolves only through Integration allocation"),
		Component->CustomDepthStencilValue,
		static_cast<int32>(
			FABTSStylizedRenderingContract::ResolveStencilValueForRenderer(
				EABTSStylizedObjectClass::PlayerBird)));
	TestEqual(TEXT("One producer is tracked"), Registry.Num(), 1);

	Desired.Reset();
	Registry.Apply(Desired);
	TestFalse(
		TEXT("Style-off/absence restores the original producer switch"),
		Component->bRenderCustomDepth != 0);
	TestEqual(
		TEXT("Style-off/absence restores the original stencil value"),
		Component->CustomDepthStencilValue,
		19);
	TestEqual(TEXT("No producer remains tracked"), Registry.Num(), 0);

	Component->SetRenderCustomDepth(true);
	Component->SetCustomDepthStencilValue(99);
	Desired.Add(Component, EABTSStylizedObjectClass::FinaleUFO);
	Registry.Apply(Desired);
	TestEqual(
		TEXT("Foreign stencil producers are never stolen"),
		Component->CustomDepthStencilValue,
		99);
	TestEqual(TEXT("Foreign producer is not tracked"), Registry.Num(), 0);
	Registry.Apply(Desired);
	TestEqual(TEXT("Conflict fails closed once"), Registry.GetConflictCount(), 1);
	Registry.RestoreAll();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT4A1ContinuousAtmosphereOverrideTest,
	"ABTS.Rendering.Toon.T4A1.ContinuousAtmosphereOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT4A1ContinuousAtmosphereOverrideTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	IConsoleVariable* FastSky = IConsoleManager::Get().FindConsoleVariable(
		TEXT("r.SkyAtmosphere.FastSkyLUT"));
	IConsoleVariable* FastSkyWidth = IConsoleManager::Get().FindConsoleVariable(
		TEXT("r.SkyAtmosphere.FastSkyLUT.Width"));
	IConsoleVariable* FastSkyHeight = IConsoleManager::Get().FindConsoleVariable(
		TEXT("r.SkyAtmosphere.FastSkyLUT.Height"));
	IConsoleVariable* FastSkySampleMin = IConsoleManager::Get().FindConsoleVariable(
		TEXT("r.SkyAtmosphere.FastSkyLUT.SampleCountMin"));
	IConsoleVariable* FastSkySampleMax = IConsoleManager::Get().FindConsoleVariable(
		TEXT("r.SkyAtmosphere.FastSkyLUT.SampleCountMax"));
	IConsoleVariable* FastSkySampleDistance =
		IConsoleManager::Get().FindConsoleVariable(
			TEXT("r.SkyAtmosphere.FastSkyLUT.DistanceToSampleCountMax"));
	IConsoleVariable* FastAerial = IConsoleManager::Get().FindConsoleVariable(
		TEXT("r.SkyAtmosphere.AerialPerspectiveLUT.FastApplyOnOpaque"));
	IConsoleVariable* SampleMin = IConsoleManager::Get().FindConsoleVariable(
		TEXT("r.SkyAtmosphere.SampleCountMin"));
	IConsoleVariable* SampleMax = IConsoleManager::Get().FindConsoleVariable(
		TEXT("r.SkyAtmosphere.SampleCountMax"));
	IConsoleVariable* SampleDistance = IConsoleManager::Get().FindConsoleVariable(
		TEXT("r.SkyAtmosphere.DistanceToSampleCountMax"));
	IConsoleVariable* LUT32 = IConsoleManager::Get().FindConsoleVariable(
		TEXT("r.SkyAtmosphere.LUT32"));
	TestNotNull(TEXT("Fast sky CVar exists in UE 5.8"), FastSky);
	TestNotNull(TEXT("Fast sky width CVar exists in UE 5.8"), FastSkyWidth);
	TestNotNull(TEXT("Fast sky height CVar exists in UE 5.8"), FastSkyHeight);
	TestNotNull(TEXT("Fast sky minimum sample CVar exists in UE 5.8"),
		FastSkySampleMin);
	TestNotNull(TEXT("Fast sky maximum sample CVar exists in UE 5.8"),
		FastSkySampleMax);
	TestNotNull(TEXT("Fast sky sample distance CVar exists in UE 5.8"),
		FastSkySampleDistance);
	TestNotNull(TEXT("Fast aerial CVar exists in UE 5.8"), FastAerial);
	TestNotNull(TEXT("Full sky minimum sample CVar exists in UE 5.8"), SampleMin);
	TestNotNull(TEXT("Full sky maximum sample CVar exists in UE 5.8"), SampleMax);
	TestNotNull(TEXT("Full sky sample distance CVar exists in UE 5.8"), SampleDistance);
	TestNotNull(TEXT("Full precision LUT CVar exists in UE 5.8"), LUT32);
	if (FastSky == nullptr || FastSkyWidth == nullptr || FastSkyHeight == nullptr
		|| FastSkySampleMin == nullptr || FastSkySampleMax == nullptr
		|| FastSkySampleDistance == nullptr || FastAerial == nullptr
		|| SampleMin == nullptr
		|| SampleMax == nullptr || SampleDistance == nullptr || LUT32 == nullptr)
	{
		return false;
	}

	const int32 OriginalFastSky = FastSky->GetInt();
	const float OriginalFastSkyWidth = FastSkyWidth->GetFloat();
	const float OriginalFastSkyHeight = FastSkyHeight->GetFloat();
	const float OriginalFastSkySampleMin = FastSkySampleMin->GetFloat();
	const float OriginalFastSkySampleMax = FastSkySampleMax->GetFloat();
	const float OriginalFastSkySampleDistance = FastSkySampleDistance->GetFloat();
	const int32 OriginalFastAerial = FastAerial->GetInt();
	const float OriginalSampleMin = SampleMin->GetFloat();
	const float OriginalSampleMax = SampleMax->GetFloat();
	const float OriginalSampleDistance = SampleDistance->GetFloat();
	const int32 OriginalLUT32 = LUT32->GetInt();
	FString Failure;
	TestTrue(
		TEXT("First stylized world acquires the continuous atmosphere override"),
		ABTSStylizedRenderingWorldSubsystemPrivate::
			FContinuousAtmosphereOverride::Acquire(Failure));
	TestTrue(TEXT("Acquire failure remains empty"), Failure.IsEmpty());
	TestEqual(TEXT("Fast sky LUT is enabled while owned"), FastSky->GetInt(), 1);
	TestTrue(TEXT("SkyView LUT width is doubled"),
		FMath::IsNearlyEqual(FastSkyWidth->GetFloat(), 384.0f));
	TestTrue(TEXT("SkyView LUT height is doubled"),
		FMath::IsNearlyEqual(FastSkyHeight->GetFloat(), 208.0f));
	TestTrue(TEXT("SkyView LUT has a 16-sample floor"),
		FMath::IsNearlyEqual(FastSkySampleMin->GetFloat(), 16.0f));
	TestTrue(TEXT("SkyView LUT is capped at 32 samples"),
		FMath::IsNearlyEqual(FastSkySampleMax->GetFloat(), 32.0f));
	TestTrue(TEXT("Tiny-planet SkyView rays reach the cap after ten metres"),
		FMath::IsNearlyEqual(FastSkySampleDistance->GetFloat(), 0.01f));
	TestEqual(TEXT("Fast aerial LUT is disabled while owned"), FastAerial->GetInt(), 0);
	TestTrue(TEXT("Full ray march has a 16-sample floor"),
		FMath::IsNearlyEqual(SampleMin->GetFloat(), 16.0f));
	TestTrue(TEXT("Full ray march is capped at 32 samples"),
		FMath::IsNearlyEqual(SampleMax->GetFloat(), 32.0f));
	TestTrue(TEXT("Tiny-planet rays reach the cap after ten metres"),
		FMath::IsNearlyEqual(SampleDistance->GetFloat(), 0.01f));
	TestEqual(TEXT("Atmosphere LUTs use full precision while owned"),
		LUT32->GetInt(), 1);

	Failure.Reset();
	TestTrue(
		TEXT("A second game world shares the same process override"),
		ABTSStylizedRenderingWorldSubsystemPrivate::
			FContinuousAtmosphereOverride::Acquire(Failure));
	ABTSStylizedRenderingWorldSubsystemPrivate::
		FContinuousAtmosphereOverride::Release();
	TestEqual(TEXT("One remaining owner keeps fast sky enabled"), FastSky->GetInt(), 1);
	TestEqual(TEXT("One remaining owner keeps fast aerial disabled"), FastAerial->GetInt(), 0);

	ABTSStylizedRenderingWorldSubsystemPrivate::
		FContinuousAtmosphereOverride::Release();
	TestEqual(TEXT("Final release restores the original fast sky value"),
		FastSky->GetInt(), OriginalFastSky);
	TestTrue(TEXT("Final release restores the original SkyView LUT width"),
		FMath::IsNearlyEqual(FastSkyWidth->GetFloat(), OriginalFastSkyWidth));
	TestTrue(TEXT("Final release restores the original SkyView LUT height"),
		FMath::IsNearlyEqual(FastSkyHeight->GetFloat(), OriginalFastSkyHeight));
	TestTrue(TEXT("Final release restores the original SkyView minimum samples"),
		FMath::IsNearlyEqual(
			FastSkySampleMin->GetFloat(), OriginalFastSkySampleMin));
	TestTrue(TEXT("Final release restores the original SkyView maximum samples"),
		FMath::IsNearlyEqual(
			FastSkySampleMax->GetFloat(), OriginalFastSkySampleMax));
	TestTrue(TEXT("Final release restores the original SkyView sample distance"),
		FMath::IsNearlyEqual(
			FastSkySampleDistance->GetFloat(), OriginalFastSkySampleDistance));
	TestEqual(TEXT("Final release restores the original fast aerial value"),
		FastAerial->GetInt(), OriginalFastAerial);
	TestTrue(TEXT("Final release restores the original minimum samples"),
		FMath::IsNearlyEqual(SampleMin->GetFloat(), OriginalSampleMin));
	TestTrue(TEXT("Final release restores the original maximum samples"),
		FMath::IsNearlyEqual(SampleMax->GetFloat(), OriginalSampleMax));
	TestTrue(TEXT("Final release restores the original sample distance"),
		FMath::IsNearlyEqual(
			SampleDistance->GetFloat(), OriginalSampleDistance));
	TestEqual(TEXT("Final release restores the original LUT precision"),
		LUT32->GetInt(), OriginalLUT32);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT4A2R0LowPolyCloudContractTest,
	"ABTS.Rendering.Toon.T4A2R0.LowPolyCloudContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT4A2R0LowPolyCloudContractTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FABTSStylizedEnvironmentParameters First =
		FABTSStylizedRenderingControl::BuildEnvironmentParameters(
			FVector::ZeroVector,
			10000.0,
			FVector::UpVector,
			EABTSStylizedRenderProfile::GroundDay);
	const FABTSStylizedEnvironmentParameters Second =
		FABTSStylizedRenderingControl::BuildEnvironmentParameters(
			FVector::ZeroVector,
			10000.0,
			FVector::UpVector,
			EABTSStylizedRenderProfile::GroundDay);
	TestTrue(TEXT("Low-poly cloud envelope validates"), First.IsValid());
	TestEqual(TEXT("Cloud islands are enabled only by the GroundDay profile"),
		First.bCloudsEnabled, 1u);
	TestTrue(TEXT("Cloud base is radial and above the planet"),
		First.CloudBaseAltitudeCM > 0.0f);
	TestTrue(TEXT("Cloud island envelope has finite radial separation"),
		First.CloudLayerHeightCM > 0.0f);
	TestTrue(TEXT("Cloud opacity is normalized"),
		First.CloudDensity > 0.0f && First.CloudDensity <= 1.0f);
	TestTrue(TEXT("Cloud coverage is normalized"),
		First.CloudCoverage >= 0.0f && First.CloudCoverage <= 1.0f);
	TestTrue(TEXT("Cloud profile is deterministic"),
		FMath::IsNearlyEqual(
			First.CloudBaseAltitudeCM, Second.CloudBaseAltitudeCM)
		&& FMath::IsNearlyEqual(
			First.CloudLayerHeightCM, Second.CloudLayerHeightCM)
		&& FMath::IsNearlyEqual(
			First.CloudGlobalScaleKM, Second.CloudGlobalScaleKM)
		&& FMath::IsNearlyEqual(First.CloudCoverage, Second.CloudCoverage)
		&& FMath::IsNearlyEqual(First.CloudDensity, Second.CloudDensity));

	const TArray<FABTST4LowPolyCloudIslandDefinition> FirstLayout =
		FABTST4LowPolyCloudPrototype::BuildDefinitions(
			First.PlanetCenterWorld,
			First.PlanetRadiusCM,
			First.StarSeed ^ 0xC10DF13Du,
			FVector(First.SunDirectionToSunWorld),
			First.CloudBaseAltitudeCM,
			First.CloudLayerHeightCM);
	const TArray<FABTST4LowPolyCloudIslandDefinition> SecondLayout =
		FABTST4LowPolyCloudPrototype::BuildDefinitions(
			Second.PlanetCenterWorld,
			Second.PlanetRadiusCM,
			Second.StarSeed ^ 0xC10DF13Du,
			FVector(Second.SunDirectionToSunWorld),
			Second.CloudBaseAltitudeCM,
			Second.CloudLayerHeightCM);
	TestEqual(TEXT("A2.4 freezes the deterministic production cloud count"),
		FirstLayout.Num(), FABTST4LowPolyCloudPrototype::IslandCount);
	const uint64 FirstHash =
		FABTST4LowPolyCloudPrototype::ComputeLayoutHash(FirstLayout);
	TestTrue(TEXT("Cloud layout hash is non-zero"), FirstHash != 0);
	TestEqual(TEXT("Cloud layout is deterministic"), FirstHash,
		FABTST4LowPolyCloudPrototype::ComputeLayoutHash(SecondLayout));
	for (const FABTST4LowPolyCloudIslandDefinition& Definition : FirstLayout)
	{
		FABTST4LowPolyCloudMeshData Mesh;
		FString Failure;
		TestTrue(TEXT("Every cloud island produces a closed mesh"),
			FABTST4LowPolyCloudPrototype::BuildClosedMesh(
				Definition, Mesh, &Failure));
		TestTrue(TEXT("Every cloud mesh validates"), Mesh.IsValid());
		TestTrue(TEXT("Every cloud mesh has a deterministic geometry identity"),
			Mesh.GeometryHash != 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT4A2R1AInstancedCloudletContractTest,
	"ABTS.Rendering.Toon.T4A2R1A.InstancedCloudletContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT4A2R1AInstancedCloudletContractTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FABTSStylizedEnvironmentParameters Environment =
		FABTSStylizedRenderingControl::BuildEnvironmentParameters(
			FVector(120.0, -340.0, 560.0),
			10000.0,
			FVector(0.3, -0.6, 0.7).GetSafeNormal(),
			EABTSStylizedRenderProfile::GroundDay);
	const TArray<FABTST4LowPolyCloudIslandDefinition> Layout =
		FABTST4LowPolyCloudPrototype::BuildDefinitions(
			Environment.PlanetCenterWorld,
			Environment.PlanetRadiusCM,
			Environment.StarSeed ^ 0xC10DF13Du,
			FVector(Environment.SunDirectionToSunWorld),
			Environment.CloudBaseAltitudeCM,
			Environment.CloudLayerHeightCM);
	TestEqual(TEXT("R1-A produces the frozen global cloud field"),
		Layout.Num(), FABTST4LowPolyCloudPrototype::IslandCount);

	int32 TotalCloudlets = 0;
	uint64 CombinedHashA = 0xA2C1A11Aull;
	uint64 CombinedHashB = 0xA2C1A11Aull;
	for (const FABTST4LowPolyCloudIslandDefinition& Island : Layout)
	{
		TArray<FABTST4InstancedCloudletDefinition> First;
		TArray<FABTST4InstancedCloudletDefinition> Second;
		FString Failure;
		TestTrue(TEXT("Cloud island builds an instanced cloudlet population"),
			FABTST4LowPolyCloudPrototype::BuildInstancedCloudlets(
				Island, First, &Failure));
		TestTrue(TEXT("Repeated cloudlet generation succeeds"),
			FABTST4LowPolyCloudPrototype::BuildInstancedCloudlets(
				Island, Second, &Failure));
		TestTrue(TEXT("Every cloud island has more than one shared-mesh instance"),
			First.Num() > 1);
		const uint64 FirstHash =
			FABTST4LowPolyCloudPrototype::ComputeCloudletLayoutHash(First);
		const uint64 SecondHash =
			FABTST4LowPolyCloudPrototype::ComputeCloudletLayoutHash(Second);
		TestTrue(TEXT("Cloudlet population hash is non-zero"), FirstHash != 0);
		TestEqual(TEXT("Cloudlet transforms and custom data are deterministic"),
			FirstHash, SecondHash);
		CombinedHashA ^= FirstHash + 0x9e3779b97f4a7c15ull
			+ (CombinedHashA << 6) + (CombinedHashA >> 2);
		CombinedHashB ^= SecondHash + 0x9e3779b97f4a7c15ull
			+ (CombinedHashB << 6) + (CombinedHashB >> 2);
		TotalCloudlets += First.Num();
		for (const FABTST4InstancedCloudletDefinition& Cloudlet : First)
		{
			TestTrue(TEXT("Every cloudlet validates"), Cloudlet.IsValid());
		}
	}
	TestEqual(TEXT("R1-A freezes the total instance budget"),
		TotalCloudlets, FABTST4LowPolyCloudPrototype::TotalCloudletCount);
	TestEqual(TEXT("Combined cloudlet identity is repeatable"),
		CombinedHashA, CombinedHashB);
	TestEqual(TEXT("A2.4 batches exact logical-cloud fields into one HISM"),
		FABTST4LowPolyCloudPrototype::CloudletCustomDataFloatCount, 63);
	TestEqual(TEXT("A2.4 preserves the five geometry channels at the head"),
		FABTST4LowPolyCloudPrototype::CloudletBaseCustomDataFloatCount, 5);
	TestEqual(TEXT("A2.4 stores the island field after geometry data"),
		FABTST4LowPolyCloudPrototype::CloudletIslandCenterCustomDataIndex, 5);
	TestEqual(TEXT("A2.4 stores six exact macro fields after island extents"),
		FABTST4LowPolyCloudPrototype::CloudletMacroCustomDataIndex, 20);
	TestEqual(TEXT("A2.4 appends the legacy colour variant after macro fields"),
		FABTST4LowPolyCloudPrototype::CloudletColorVariantCustomDataIndex, 62);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT4A2R1BCloudAssetContractTest,
	"ABTS.Rendering.Toon.T4A2R1B.CloudAssetContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT4A2R1BCloudAssetContractTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	UMaterial* CloudMaterial = LoadObject<UMaterial>(
		nullptr,
		TEXT("/Game/Toon/Environment/Cloud/M_ABTS_Toon_Cloudlet.M_ABTS_Toon_Cloudlet"));
	UStaticMesh* CloudMesh = LoadObject<UStaticMesh>(
		nullptr,
		TEXT("/Game/Toon/Environment/Cloud/SM_ABTS_Toon_Cloudlet.SM_ABTS_Toon_Cloudlet"));
	TestNotNull(TEXT("R1-B cloudlet material is loadable"), CloudMaterial);
	TestNotNull(TEXT("R1-B cloudlet mesh is loadable"), CloudMesh);
	if (!IsValid(CloudMaterial) || !IsValid(CloudMesh))
	{
		return false;
	}
	TestTrue(
		TEXT("R1-B cloudlet material is exclusively Unlit"),
		CloudMaterial->GetShadingModels().HasOnlyShadingModel(MSM_Unlit));
	TestEqual(
		TEXT("A2.3 cloudlet material uses bounded masked visibility"),
		static_cast<int32>(CloudMaterial->GetBlendMode()),
		static_cast<int32>(BLEND_Masked));
	TestTrue(
		TEXT("R1-B cloudlet material is compiled for static meshes"),
		CloudMaterial->GetUsageByFlag(MATUSAGE_StaticMesh));
	TestTrue(
		TEXT("R1-B cloudlet material is compiled for instanced static meshes"),
		CloudMaterial->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes));
	TestTrue(
		TEXT("A2.3.1 masked/WPO cloud pixels reject stale TSR history"),
		CloudMaterial->HasPixelAnimation());
	float MacroLightingVersion = 0.0f;
	TestTrue(
		TEXT("A2.3 material exposes its bounded-traversal version"),
		CloudMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(TEXT("ABTS_CloudMacroLightingVersion")),
			MacroLightingVersion));
	TestEqual(
		TEXT("A2.3.1 per-bird/TSR material version is current"),
		MacroLightingVersion,
		12.0f);
	float TraversalActive = -1.0f;
	float TraversalProtectionActive = -1.0f;
	float TraversalBirdCount = -1.0f;
	float TraversalCameraRadius = 0.0f;
	float TraversalBirdRadius = 0.0f;
	float TraversalCorridorRadius = 0.0f;
	float TraversalRetainedCoverage = 0.0f;
	float TraversalMaskFrequency = 0.0f;
	TestTrue(TEXT("A2.3 material exposes a fail-closed traversal switch"),
		CloudMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(TEXT("ABTS_CloudTraversalActive")),
			TraversalActive));
	TestEqual(TEXT("Traversal is disabled outside a diagnosed relation"),
		TraversalActive, 0.0f);
	TestTrue(TEXT("A2.3.1 exposes an immediate hard-protection switch"),
		CloudMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(
				TEXT("ABTS_CloudTraversalProtectionActive")),
			TraversalProtectionActive));
	TestEqual(TEXT("Hard protection fails closed outside traversal"),
		TraversalProtectionActive, 0.0f);
	TestTrue(TEXT("A2.3.1 exposes a fixed-capacity per-bird sphere count"),
		CloudMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(TEXT("ABTS_CloudTraversalBirdCount")),
			TraversalBirdCount));
	TestEqual(TEXT("No stale bird spheres are active by default"),
		TraversalBirdCount, 0.0f);
	for (int32 BirdIndex = 0; BirdIndex < 4; ++BirdIndex)
	{
		FLinearColor BirdSphere = FLinearColor::Transparent;
		TestTrue(
			*FString::Printf(TEXT("Bird sphere %d is material-addressable"), BirdIndex),
			CloudMaterial->GetVectorParameterValue(
				FMaterialParameterInfo(*FString::Printf(
					TEXT("ABTS_CloudTraversalBirdSphere%d"), BirdIndex)),
				BirdSphere));
		TestTrue(
			*FString::Printf(TEXT("Bird sphere %d has a fail-safe radius"), BirdIndex),
			BirdSphere.A >= 1.0f);
	}
	TestTrue(TEXT("A2.3 exposes a camera clearing radius"),
		CloudMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(TEXT("ABTS_CloudTraversalCameraRadiusCM")),
			TraversalCameraRadius));
	TestTrue(TEXT("A2.3 exposes a bird clearing radius"),
		CloudMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(TEXT("ABTS_CloudTraversalBirdRadiusCM")),
			TraversalBirdRadius));
	TestTrue(TEXT("A2.3 exposes a camera-to-bird corridor radius"),
		CloudMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(TEXT("ABTS_CloudTraversalCorridorRadiusCM")),
			TraversalCorridorRadius));
	TestTrue(TEXT("Every A2.3 clearing radius is positive and bounded"),
		TraversalCameraRadius >= 200.0f
			&& TraversalBirdRadius >= 120.0f
			&& TraversalCorridorRadius >= 100.0f
			&& TraversalCameraRadius <= 600.0f);
	TestTrue(TEXT("A2.3.1 exposes a bounded retained cloud coverage"),
		CloudMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(
				TEXT("ABTS_CloudTraversalRetainedCoverage")),
			TraversalRetainedCoverage));
	TestTrue(TEXT("A2.3.1 retains visible cloud without becoming opaque"),
		TraversalRetainedCoverage >= 0.76f
			&& TraversalRetainedCoverage <= 0.88f);
	TestTrue(TEXT("A2.3.1 exposes a stable planar cloud-mask scale"),
		CloudMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(
				TEXT("ABTS_CloudTraversalMaskFrequency")),
			TraversalMaskFrequency));
	TestTrue(TEXT("A2.3.1 mask cells remain cloud-like rather than pixel-sized"),
		TraversalMaskFrequency >= 0.008f
			&& TraversalMaskFrequency <= 0.020f);
	FLinearColor PlanetCenterParameter = FLinearColor::Transparent;
	TestTrue(
		TEXT("A2.2 material consumes the accepted planet centre per pixel"),
		CloudMaterial->GetVectorParameterValue(
			FMaterialParameterInfo(TEXT("ABTS_CloudPlanetCenter")),
			PlanetCenterParameter));
	float NightBrightness = 0.0f;
	TestTrue(
		TEXT("A2.2 material exposes a bounded night-cloud brightness"),
		CloudMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(TEXT("ABTS_CloudNightBrightness")),
			NightBrightness));
	TestTrue(TEXT("Night clouds remain readable without retaining daytime white"),
		NightBrightness >= 0.35f && NightBrightness <= 0.65f);
	float DaylightBlendMinSolarHeight = 0.0f;
	float DaylightBlendMaxSolarHeight = 0.0f;
	TestTrue(
		TEXT("A2.2 material exposes the night-to-day blend start"),
		CloudMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(
				TEXT("ABTS_CloudDaylightBlendMinSolarHeight")),
			DaylightBlendMinSolarHeight));
	TestTrue(
		TEXT("A2.2 material exposes the night-to-day blend end"),
		CloudMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(
				TEXT("ABTS_CloudDaylightBlendMaxSolarHeight")),
			DaylightBlendMaxSolarHeight));
	TestEqual(TEXT("The material and CPU oracle share the blend start"),
		DaylightBlendMinSolarHeight,
		FABTST4LowPolyCloudPrototype::DaylightBlendMinSolarHeight);
	TestEqual(TEXT("The material and CPU oracle share the blend end"),
		DaylightBlendMaxSolarHeight,
		FABTST4LowPolyCloudPrototype::DaylightBlendMaxSolarHeight);
	FLinearColor CloudLightColor = FLinearColor::Transparent;
	TestTrue(
		TEXT("R1-C2-B3-B material exposes a neutral white light band"),
		CloudMaterial->GetVectorParameterValue(
			FMaterialParameterInfo(TEXT("ABTS_CloudLightColor")),
			CloudLightColor));
	TestTrue(
		TEXT("R1-C2-B3-B light band is bright enough to read as sunlit white"),
		CloudLightColor.GetLuminance() > 0.80f);
	FLinearColor CloudBodyColor = FLinearColor::Transparent;
	TestTrue(
		TEXT("R1-C2-B3-B material exposes a distinct body colour band"),
		CloudMaterial->GetVectorParameterValue(
			FMaterialParameterInfo(TEXT("ABTS_CloudBodyColor")),
			CloudBodyColor));
	TestTrue(
		TEXT("R1-C2-B3-B body colour remains below the cloud-top band"),
		CloudBodyColor.GetLuminance() < CloudLightColor.GetLuminance());
	float SunWhiteStrength = 0.0f;
	float ThinWhiteStrength = 0.0f;
	TestTrue(
		TEXT("R1-C2-B3-B exposes sunward whitening"),
		CloudMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(TEXT("ABTS_CloudSunWhiteStrength")),
			SunWhiteStrength));
	TestTrue(
		TEXT("R1-C2-B3-B exposes thin-density whitening"),
		CloudMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(TEXT("ABTS_CloudThinWhiteStrength")),
			ThinWhiteStrength));
	TestTrue(
		TEXT("R1-C2-B3-B sunward whitening is visually material"),
		SunWhiteStrength >= 0.60f);
	TestTrue(
		TEXT("R1-C2-B3-B thin-density whitening is visually material"),
		ThinWhiteStrength >= 0.50f);
	float GradientConfidenceStart = 0.0f;
	float GradientConfidenceEnd = 0.0f;
	TestTrue(
		TEXT("R1-C2-B3-B1 exposes a gradient-confidence start"),
		CloudMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(TEXT("ABTS_CloudGradientConfidenceStart")),
			GradientConfidenceStart));
	TestTrue(
		TEXT("R1-C2-B3-B1 exposes a gradient-confidence end"),
		CloudMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(TEXT("ABTS_CloudGradientConfidenceEnd")),
			GradientConfidenceEnd));
	TestTrue(
		TEXT("R1-C2-B3-B1 keeps a non-zero critical-point fallback band"),
		GradientConfidenceStart >= 0.05f
			&& GradientConfidenceEnd >= GradientConfidenceStart + 0.15f);
	float RetiredPerInstancePixelParameter = 0.0f;
	TestFalse(
		TEXT("R1-C2-B3-B keeps local-normal detail out of pixel lighting"),
		CloudMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(TEXT("ABTS_CloudBodyDetailWeight")),
			RetiredPerInstancePixelParameter));
	TestFalse(
		TEXT("R1-C2-B3-B keeps instance variation out of pixel lighting"),
		CloudMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(TEXT("ABTS_CloudInstanceVariationStrength")),
			RetiredPerInstancePixelParameter));
	const FVector PositiveBounds = CloudMesh->GetPositiveBoundsExtension();
	const FVector NegativeBounds = CloudMesh->GetNegativeBoundsExtension();
	TestTrue(
		TEXT("R1-B cloudlet mesh reserves positive WPO bounds"),
		PositiveBounds.GetMin() >= 18.0);
	TestTrue(
		TEXT("R1-B cloudlet mesh reserves negative WPO bounds"),
		NegativeBounds.GetMin() >= 18.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT4A2R1C2A4SeededAmorphousFootprintContractTest,
	"ABTS.Rendering.Toon.T4A2R1C2A4.SeededAmorphousFootprintContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT4A2R1C2A4SeededAmorphousFootprintContractTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FABTSStylizedEnvironmentParameters Environment =
		FABTSStylizedRenderingControl::BuildEnvironmentParameters(
			FVector(120.0, -340.0, 560.0),
			10000.0,
			FVector(0.3, -0.6, 0.7).GetSafeNormal(),
			EABTSStylizedRenderProfile::GroundDay);
	const TArray<FABTST4LowPolyCloudIslandDefinition> Layout =
		FABTST4LowPolyCloudPrototype::BuildDefinitions(
			Environment.PlanetCenterWorld,
			Environment.PlanetRadiusCM,
			Environment.StarSeed ^ 0xC10DF13Du,
			FVector(Environment.SunDirectionToSunWorld),
			Environment.CloudBaseAltitudeCM,
			Environment.CloudLayerHeightCM);
	TestEqual(TEXT("R1-C2-A4 retains the global cloud population"),
		Layout.Num(), FABTST4LowPolyCloudPrototype::IslandCount);

	int32 TotalBody = 0;
	int32 TotalCrown = 0;
	int32 TotalEdge = 0;
	int32 TotalMacroClusters = 0;
	for (const FABTST4LowPolyCloudIslandDefinition& Island : Layout)
	{
		const double HorizontalEnvelopeAspect = FMath::Max(
			Island.ExtentsCM.X, Island.ExtentsCM.Y) / FMath::Min(
				Island.ExtentsCM.X, Island.ExtentsCM.Y);
		TestTrue(TEXT("Cloud island horizontal envelope has no dominant axis"),
			HorizontalEnvelopeAspect <= 1.08);
		const TArray<FABTST4CloudMacroClusterDefinition> MacroClusters =
			FABTST4LowPolyCloudPrototype::BuildMacroClusters(Island);
		const TArray<FABTST4CloudMacroClusterDefinition> RepeatedClusters =
			FABTST4LowPolyCloudPrototype::BuildMacroClusters(Island);
		TestEqual(TEXT("Every island owns six deterministic macro clusters"),
			MacroClusters.Num(),
			FABTST4LowPolyCloudPrototype::MacroClusterCountPerIsland);
		TestEqual(TEXT("Repeated macro-cluster generation keeps its count"),
			RepeatedClusters.Num(), MacroClusters.Num());
		for (int32 ClusterIndex = 0;
			ClusterIndex < MacroClusters.Num(); ++ClusterIndex)
		{
			TestTrue(TEXT("Every macro cluster validates"),
				MacroClusters[ClusterIndex].IsValid());
			TestEqual(TEXT("Macro cluster identity is deterministic"),
				MacroClusters[ClusterIndex].IdentityHash,
				RepeatedClusters[ClusterIndex].IdentityHash);
		}
		if (MacroClusters.Num()
			== FABTST4LowPolyCloudPrototype::MacroClusterCountPerIsland)
		{
			TestTrue(TEXT("Seeded core remains close to the island centre"),
				MacroClusters[0].NormalizedCenter.Size() <= 0.10);
			TArray<double> OuterAngles;
			OuterAngles.Reserve(MacroClusters.Num() - 1);
			double MinimumOuterDistance = TNumericLimits<double>::Max();
			double MaximumOuterDistance = 0.0;
			double MinimumEquivalentRadius = TNumericLimits<double>::Max();
			double MaximumEquivalentRadius = 0.0;
			for (int32 ClusterIndex = 1;
				ClusterIndex < MacroClusters.Num(); ++ClusterIndex)
			{
				const FABTST4CloudMacroClusterDefinition& Cluster =
					MacroClusters[ClusterIndex];
				double Angle = FMath::Atan2(
					Cluster.NormalizedCenter.Y,
					Cluster.NormalizedCenter.X);
				if (Angle < 0.0)
				{
					Angle += UE_TWO_PI;
				}
				OuterAngles.Add(Angle);
				const double Distance = Cluster.NormalizedCenter.Size();
				MinimumOuterDistance = FMath::Min(
					MinimumOuterDistance, Distance);
				MaximumOuterDistance = FMath::Max(
					MaximumOuterDistance, Distance);
				const double EquivalentRadius = FMath::Sqrt(
					Cluster.NormalizedRadii.X * Cluster.NormalizedRadii.Y);
				MinimumEquivalentRadius = FMath::Min(
					MinimumEquivalentRadius, EquivalentRadius);
				MaximumEquivalentRadius = FMath::Max(
					MaximumEquivalentRadius, EquivalentRadius);
			}
			OuterAngles.Sort();
			double MinimumAngularGap = TNumericLimits<double>::Max();
			double MaximumAngularGap = 0.0;
			for (int32 AngleIndex = 0;
				AngleIndex < OuterAngles.Num(); ++AngleIndex)
			{
				const double NextAngle = AngleIndex + 1 < OuterAngles.Num()
					? OuterAngles[AngleIndex + 1]
					: OuterAngles[0] + UE_TWO_PI;
				const double Gap = NextAngle - OuterAngles[AngleIndex];
				MinimumAngularGap = FMath::Min(MinimumAngularGap, Gap);
				MaximumAngularGap = FMath::Max(MaximumAngularGap, Gap);
			}
			TestTrue(TEXT("Outer lobe distances vary enough to avoid a regular ring"),
				MaximumOuterDistance - MinimumOuterDistance >= 0.045);
			TestTrue(TEXT("Outer lobe angular gaps vary enough to avoid a regular polygon"),
				MaximumAngularGap - MinimumAngularGap >= 0.10);
			TestTrue(TEXT("Outer lobe sizes vary enough to avoid repeated corner puffs"),
				MaximumEquivalentRadius - MinimumEquivalentRadius >= 0.025);
		}
		TotalMacroClusters += MacroClusters.Num();

		TArray<FABTST4InstancedCloudletDefinition> Cloudlets;
		TArray<FABTST4InstancedCloudletDefinition> RepeatedCloudlets;
		FString Failure;
		TestTrue(TEXT("Curved clustered cloudlet population builds"),
			FABTST4LowPolyCloudPrototype::BuildInstancedCloudlets(
				Island, Cloudlets, &Failure));
		TestTrue(TEXT("Repeated curved cloudlet population builds"),
			FABTST4LowPolyCloudPrototype::BuildInstancedCloudlets(
				Island, RepeatedCloudlets, &Failure));
		TestEqual(TEXT("R1-C2-A4 layout identity is deterministic"),
			FABTST4LowPolyCloudPrototype::ComputeCloudletLayoutHash(Cloudlets),
			FABTST4LowPolyCloudPrototype::ComputeCloudletLayoutHash(
				RepeatedCloudlets));
		int32 BodyCount = 0;
		int32 CrownCount = 0;
		int32 EdgeCount = 0;
		double BodyHeightSum = 0.0;
		double CrownHeightSum = 0.0;
		for (const FABTST4InstancedCloudletDefinition& Cloudlet : Cloudlets)
		{
			TestTrue(TEXT("Cloudlet keeps a valid macro-cluster membership"),
				MacroClusters.IsValidIndex(Cloudlet.MacroClusterIndex));
			const FVector TranslationUp =
				Cloudlet.TransformRelativeToPlanet.GetLocation().GetSafeNormal();
			const FVector RotationUp =
				Cloudlet.TransformRelativeToPlanet.GetRotation().GetAxisZ();
			TestTrue(TEXT("Cloudlet radial up follows curved shell position"),
				FVector::DotProduct(TranslationUp, Cloudlet.RadialUp) > 0.9999);
			TestTrue(TEXT("Cloudlet local Z follows curved radial up"),
				FVector::DotProduct(RotationUp, Cloudlet.RadialUp) > 0.9999);
			switch (Cloudlet.Layer)
			{
			case EABTST4CloudletLayer::Body:
				++BodyCount;
				BodyHeightSum += Cloudlet.NormalizedHeight;
				TestTrue(TEXT("Body cloudlets form a flattened coverage layer"),
					Cloudlet.TransformRelativeToPlanet.GetScale3D().Z
					< FMath::Max(
						Cloudlet.TransformRelativeToPlanet.GetScale3D().X,
						Cloudlet.TransformRelativeToPlanet.GetScale3D().Y));
				break;
			case EABTST4CloudletLayer::Crown:
				++CrownCount;
				CrownHeightSum += Cloudlet.NormalizedHeight;
				break;
			case EABTST4CloudletLayer::Edge:
				++EdgeCount;
				{
					bool bAttachedToBody = false;
					for (const FABTST4InstancedCloudletDefinition& Body : Cloudlets)
					{
						if (Body.Layer != EABTST4CloudletLayer::Body
							|| Body.MacroClusterIndex
								!= Cloudlet.MacroClusterIndex)
						{
							continue;
						}
						const FVector2D Delta =
							Cloudlet.NormalizedPlanarCenter
							- Body.NormalizedPlanarCenter;
						const double CosYaw = FMath::Cos(
							Body.PlanarOrientationRadians);
						const double SinYaw = FMath::Sin(
							Body.PlanarOrientationRadians);
						const double LocalX =
							Delta.X * CosYaw + Delta.Y * SinYaw;
						const double LocalY =
							-Delta.X * SinYaw + Delta.Y * CosYaw;
						const double ExpandedDistance = FMath::Square(
							LocalX / (Body.NormalizedPlanarRadii.X * 1.08))
							+ FMath::Square(
								LocalY / (Body.NormalizedPlanarRadii.Y * 1.08));
						if (ExpandedDistance <= 1.0)
						{
							bAttachedToBody = true;
							break;
						}
					}
					TestTrue(TEXT("Edge cloudlet remains attached to its body cluster"),
						bAttachedToBody);
				}
				break;
			default:
				AddError(TEXT("Unknown cloudlet layer."));
				break;
			}
		}
		TestEqual(TEXT("Body budget is frozen per island"), BodyCount,
			FABTST4LowPolyCloudPrototype::GetCloudletLayerCount(
				Island.IslandIndex, EABTST4CloudletLayer::Body));
		TestEqual(TEXT("Crown budget is frozen per island"), CrownCount,
			FABTST4LowPolyCloudPrototype::GetCloudletLayerCount(
				Island.IslandIndex, EABTST4CloudletLayer::Crown));
		TestEqual(TEXT("Edge budget is frozen per island"), EdgeCount,
			FABTST4LowPolyCloudPrototype::GetCloudletLayerCount(
				Island.IslandIndex, EABTST4CloudletLayer::Edge));
		TestTrue(TEXT("Crown layer is higher than body coverage"),
			CrownCount > 0 && BodyCount > 0
			&& CrownHeightSum / CrownCount > BodyHeightSum / BodyCount);

		// Measure the actual cloudlet union rather than only the authored island
		// extents. A support-width sweep catches a chain of clusters or locally
		// stretched edge cloudlets even when the outer envelope looks square.
		double MinimumProjectedWidthCM = TNumericLimits<double>::Max();
		double MaximumProjectedWidthCM = 0.0;
		constexpr int32 AzimuthSampleCount = 24;
		for (int32 AzimuthIndex = 0;
			AzimuthIndex < AzimuthSampleCount; ++AzimuthIndex)
		{
			const double Angle = UE_TWO_PI
				* static_cast<double>(AzimuthIndex)
				/ static_cast<double>(AzimuthSampleCount);
			const FVector2D Direction(FMath::Cos(Angle), FMath::Sin(Angle));
			double MinimumSupportCM = TNumericLimits<double>::Max();
			double MaximumSupportCM = -TNumericLimits<double>::Max();
			for (const FABTST4InstancedCloudletDefinition& Cloudlet : Cloudlets)
			{
				const FVector2D CenterCM(
					Cloudlet.NormalizedPlanarCenter.X * Island.ExtentsCM.X,
					Cloudlet.NormalizedPlanarCenter.Y * Island.ExtentsCM.Y);
				const FVector2D RadiiCM(
					Cloudlet.NormalizedPlanarRadii.X * Island.ExtentsCM.X,
					Cloudlet.NormalizedPlanarRadii.Y * Island.ExtentsCM.Y);
				const double Orientation = Cloudlet.PlanarOrientationRadians;
				const FVector2D AxisX(FMath::Cos(Orientation), FMath::Sin(Orientation));
				const FVector2D AxisY(-FMath::Sin(Orientation), FMath::Cos(Orientation));
				const double RadiusSupportCM = FMath::Sqrt(
					FMath::Square(RadiiCM.X * FVector2D::DotProduct(Direction, AxisX))
					+ FMath::Square(RadiiCM.Y * FVector2D::DotProduct(Direction, AxisY)));
				const double CenterSupportCM = FVector2D::DotProduct(
					Direction, CenterCM);
				MinimumSupportCM = FMath::Min(
					MinimumSupportCM, CenterSupportCM - RadiusSupportCM);
				MaximumSupportCM = FMath::Max(
					MaximumSupportCM, CenterSupportCM + RadiusSupportCM);
			}
			const double ProjectedWidthCM = MaximumSupportCM - MinimumSupportCM;
			MinimumProjectedWidthCM = FMath::Min(
				MinimumProjectedWidthCM, ProjectedWidthCM);
			MaximumProjectedWidthCM = FMath::Max(
				MaximumProjectedWidthCM, ProjectedWidthCM);
		}
		const double AzimuthalFootprintIsotropy = MaximumProjectedWidthCM > 0.0
			? MinimumProjectedWidthCM / MaximumProjectedWidthCM
			: 0.0;
		TestTrue(
			FString::Printf(
				TEXT("Cloudlet union remains broad from every sampled azimuth (Island=%d Isotropy=%.4f MinWidthCM=%.2f MaxWidthCM=%.2f)"),
				Island.IslandIndex,
				AzimuthalFootprintIsotropy,
				MinimumProjectedWidthCM,
				MaximumProjectedWidthCM),
			AzimuthalFootprintIsotropy >= 0.80);

		constexpr int32 CoverageGrid = 65;
		int32 EnclosingSamples = 0;
		int32 MaskSamples = 0;
		int32 CoveredSamples = 0;
		for (int32 YIndex = 0; YIndex < CoverageGrid; ++YIndex)
		{
			const double Y = FMath::Lerp(
				-1.0, 1.0,
				static_cast<double>(YIndex) / (CoverageGrid - 1));
			for (int32 XIndex = 0; XIndex < CoverageGrid; ++XIndex)
			{
				const double X = FMath::Lerp(
					-1.0, 1.0,
					static_cast<double>(XIndex) / (CoverageGrid - 1));
				if (X * X + Y * Y > 1.0)
				{
					continue;
				}
				++EnclosingSamples;
				bool bInsideMacroMaskCore = false;
				for (const FABTST4CloudMacroClusterDefinition& Cluster
					: MacroClusters)
				{
					const FVector2D Delta = FVector2D(X, Y)
						- Cluster.NormalizedCenter;
					const double CosYaw = FMath::Cos(Cluster.OrientationRadians);
					const double SinYaw = FMath::Sin(Cluster.OrientationRadians);
					const double LocalX = Delta.X * CosYaw + Delta.Y * SinYaw;
					const double LocalY = -Delta.X * SinYaw + Delta.Y * CosYaw;
					const double ClusterDistance = FMath::Square(
						LocalX / Cluster.NormalizedRadii.X)
						+ FMath::Square(LocalY / Cluster.NormalizedRadii.Y);
					if (ClusterDistance <= FMath::Square(0.76))
					{
						bInsideMacroMaskCore = true;
						break;
					}
				}
				if (!bInsideMacroMaskCore)
				{
					continue;
				}
				++MaskSamples;
				bool bCovered = false;
				for (const FABTST4InstancedCloudletDefinition& Cloudlet : Cloudlets)
				{
					if (Cloudlet.Layer != EABTST4CloudletLayer::Body)
					{
						continue;
					}
					const FVector2D Delta = FVector2D(X, Y)
						- Cloudlet.NormalizedPlanarCenter;
					const double CosYaw = FMath::Cos(
						Cloudlet.PlanarOrientationRadians);
					const double SinYaw = FMath::Sin(
						Cloudlet.PlanarOrientationRadians);
					const double LocalX = Delta.X * CosYaw + Delta.Y * SinYaw;
					const double LocalY = -Delta.X * SinYaw + Delta.Y * CosYaw;
					const double EllipseDistance = FMath::Square(
						LocalX / Cloudlet.NormalizedPlanarRadii.X)
						+ FMath::Square(
							LocalY / Cloudlet.NormalizedPlanarRadii.Y);
					if (EllipseDistance <= 1.0)
					{
						bCovered = true;
						break;
					}
				}
				CoveredSamples += bCovered ? 1 : 0;
			}
		}
		const double Coverage = MaskSamples > 0
			? static_cast<double>(CoveredSamples) / MaskSamples
			: 0.0;
		const double MaskOccupancy = EnclosingSamples > 0
			? static_cast<double>(MaskSamples) / EnclosingSamples
			: 0.0;
		TestTrue(TEXT("Body covers at least 98 percent of intended macro mask"),
			Coverage >= 0.98);
		TestTrue(TEXT("Macro mask preserves deliberate non-convex negative space"),
			MaskOccupancy >= 0.12 && MaskOccupancy <= 0.62);
		TotalBody += BodyCount;
		TotalCrown += CrownCount;
		TotalEdge += EdgeCount;
	}
	TestEqual(TEXT("R1-C2-A4 total macro-cluster budget"),
		TotalMacroClusters,
		FABTST4LowPolyCloudPrototype::IslandCount
			* FABTST4LowPolyCloudPrototype::MacroClusterCountPerIsland);
	TestEqual(TEXT("Global field total body budget"),
		TotalBody,
		FABTST4LowPolyCloudPrototype::TotalBodyCloudletCount);
	TestEqual(TEXT("Global field total crown budget"),
		TotalCrown,
		FABTST4LowPolyCloudPrototype::TotalCrownCloudletCount);
	TestEqual(TEXT("Global field total edge budget"),
		TotalEdge,
		FABTST4LowPolyCloudPrototype::TotalEdgeCloudletCount);
	TestEqual(TEXT("A2.4 freezes the production instanced-cloudlet GPU budget"),
		TotalBody + TotalCrown + TotalEdge,
		FABTST4LowPolyCloudPrototype::TotalCloudletCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT4A3M11EnvironmentStageRoutingTest,
	"ABTS.Rendering.Toon.T4A3_2.M11EnvironmentStageRouting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT4A3M11EnvironmentStageRoutingTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSStylizedRenderingWorldSubsystemPrivate;
	(void)Parameters;
	const EABTSStylizedRenderProfile Baseline =
		EABTSStylizedRenderProfile::GroundDay;
	const auto ResolveStageProfile = [Baseline](
		const EABTSM11FinaleEnvironmentStage Stage)
	{
		return FABTSStylizedRenderingContract::ResolveMainWorldProfile(
			DoesFinaleEnvironmentStageRequireSpace(Stage),
			Baseline);
	};

	TestEqual(
		TEXT("Ground launch preserves the configured surface environment"),
		ResolveStageProfile(EABTSM11FinaleEnvironmentStage::GroundLaunch),
		EABTSStylizedRenderProfile::GroundDay);
	TestEqual(
		TEXT("Atmospheric launch preserves GroundDay until the continuous altitude blend completes"),
		ResolveStageProfile(
			EABTSM11FinaleEnvironmentStage::AtmosphereTransition),
		EABTSStylizedRenderProfile::GroundDay);
	TestTrue(
		TEXT("Incomplete atmosphere transition does not force space"),
		!DoesFinaleEnvironmentStageRequireSpace(
			EABTSM11FinaleEnvironmentStage::AtmosphereTransition,
			false));
	TestTrue(
		TEXT("Completed altitude blend promotes atmosphere transition to space before Assist1"),
		DoesFinaleEnvironmentStageRequireSpace(
			EABTSM11FinaleEnvironmentStage::AtmosphereTransition,
			true));
	TestEqual(
		TEXT("Only the explicit deep-space stage selects FinaleSpace"),
		ResolveStageProfile(EABTSM11FinaleEnvironmentStage::DeepSpace),
		EABTSStylizedRenderProfile::FinaleSpace);
	TestEqual(
		TEXT("Failure recovery returns to the configured surface environment"),
		ResolveStageProfile(EABTSM11FinaleEnvironmentStage::Recovering),
		EABTSStylizedRenderProfile::GroundDay);
	TestFalse(
		TEXT("Recovering never forces FinaleSpace"),
		DoesFinaleEnvironmentStageRequireSpace(
			EABTSM11FinaleEnvironmentStage::Recovering));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT4A3EnvironmentLifecycleContractTest,
	"ABTS.Rendering.Toon.T4A3_3.EnvironmentLifecycleContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT4A3EnvironmentLifecycleContractTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSStylizedRenderingWorldSubsystemPrivate;
	(void)Parameters;
	const auto MakeCandidate = [](
		const TCHAR* Key,
		const uint64 Identity,
		const EABTSM11FinaleEnvironmentStage Stage,
		const bool bInitialized = true,
		const bool bRelevant = true)
	{
		FFinaleEnvironmentSourceCandidate Candidate;
		Candidate.StableKey = Key;
		Candidate.SourceIdentityHash = Identity;
		Candidate.Stage = Stage;
		Candidate.bInitialized = bInitialized;
		Candidate.bEnvironmentRelevant = bRelevant;
		return Candidate;
	};

	const FResolvedFinaleEnvironmentSource Baseline =
		ResolveFinaleEnvironmentCandidates({});
	TestEqual(TEXT("No M11 source resolves to the safe ground stage"),
		Baseline.Stage,
		EABTSM11FinaleEnvironmentStage::GroundLaunch);
	TestFalse(TEXT("No source is not a conflict"), Baseline.bConflict);
	TestEqual(TEXT("No source has zero lease identity"),
		Baseline.SourceIdentityHash, uint64(0));

	const FResolvedFinaleEnvironmentSource UniqueDeepSpace =
		ResolveFinaleEnvironmentCandidates({MakeCandidate(
			TEXT("/World/FinaleA"),
			0xA301ull,
			EABTSM11FinaleEnvironmentStage::DeepSpace)});
	TestTrue(TEXT("One relevant initialized source acquires the lease"),
		UniqueDeepSpace.HasUniqueSource());
	TestEqual(TEXT("Unique source publishes its exact stage"),
		UniqueDeepSpace.Stage,
		EABTSM11FinaleEnvironmentStage::DeepSpace);
	TestEqual(TEXT("Unique source identity is preserved"),
		UniqueDeepSpace.SourceIdentityHash, uint64(0xA301ull));

	const FResolvedFinaleEnvironmentSource UninitializedIgnored =
		ResolveFinaleEnvironmentCandidates({MakeCandidate(
			TEXT("/World/Incomplete"),
			0xA302ull,
			EABTSM11FinaleEnvironmentStage::DeepSpace,
			false,
			true)});
	TestEqual(TEXT("An uninitialized actor cannot own environment routing"),
		UninitializedIgnored.RelevantSourceCount, 0);
	TestEqual(TEXT("Uninitialized evidence fails closed to ground"),
		UninitializedIgnored.Stage,
		EABTSM11FinaleEnvironmentStage::GroundLaunch);

	const FFinaleEnvironmentSourceCandidate Ground = MakeCandidate(
		TEXT("/World/FinaleGround"),
		0xA303ull,
		EABTSM11FinaleEnvironmentStage::GroundLaunch);
	const FFinaleEnvironmentSourceCandidate Space = MakeCandidate(
		TEXT("/World/FinaleSpace"),
		0xA304ull,
		EABTSM11FinaleEnvironmentStage::DeepSpace);
	const FResolvedFinaleEnvironmentSource ConflictForward =
		ResolveFinaleEnvironmentCandidates({Ground, Space});
	const FResolvedFinaleEnvironmentSource ConflictReverse =
		ResolveFinaleEnvironmentCandidates({Space, Ground});
	TestTrue(TEXT("Two relevant sources are an explicit conflict"),
		ConflictForward.bConflict);
	TestEqual(TEXT("Conflicting sources fail closed to ground"),
		ConflictForward.Stage,
		EABTSM11FinaleEnvironmentStage::GroundLaunch);
	TestEqual(TEXT("A conflict never leaks a winning source identity"),
		ConflictForward.SourceIdentityHash, uint64(0));
	TestEqual(TEXT("Conflict evidence hash is iterator-order independent"),
		ConflictForward.CandidateSetHash,
		ConflictReverse.CandidateSetHash);

	const FResolvedFinaleEnvironmentSource AfterSourceDestroyed =
		ResolveFinaleEnvironmentCandidates({MakeCandidate(
			TEXT("/World/FinaleSpace"),
			0xA304ull,
			EABTSM11FinaleEnvironmentStage::DeepSpace,
			true,
			false)});
	TestEqual(TEXT("Destroyed or inactive source releases to ground baseline"),
		AfterSourceDestroyed.Stage,
		EABTSM11FinaleEnvironmentStage::GroundLaunch);
	TestFalse(TEXT("Released source cannot remain leased"),
		AfterSourceDestroyed.HasUniqueSource());
	return true;
}

#endif
