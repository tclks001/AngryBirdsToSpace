// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM11FinaleSystem.h"

#include "ABTSRuntime.h"
#include "Contracts/ABTSWorldGenerationContracts.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Rendering/ABTSStylizedRenderingTypes.h"
#include "Terrain/ABTSM3Planet.h"
#include "World/ABTSM11FinaleActors.h"

namespace
{
	class FFinaleFrameDiagnosticHash64
	{
	public:
		void AddUInt64(const uint64 Value)
		{
			for (int32 Shift = 0; Shift < 64; Shift += 8)
			{
				Hash ^= static_cast<uint8>((Value >> Shift) & 0xffull);
				Hash *= 1099511628211ull;
			}
		}

		void AddInt32(const int32 Value)
		{
			AddUInt64(static_cast<uint32>(Value));
		}

		void AddBool(const bool bValue)
		{
			AddUInt64(bValue ? 1ull : 0ull);
		}

		void AddDouble(const double Value)
		{
			AddUInt64(static_cast<uint64>(
				FMath::RoundToInt64(Value * 1000.0)));
		}

		void AddVector(const FVector& Value)
		{
			AddDouble(Value.X);
			AddDouble(Value.Y);
			AddDouble(Value.Z);
		}

		uint64 Get() const { return Hash; }

	private:
		uint64 Hash = 14695981039346656037ull;
	};

	bool RejectRuntimeBoundary(
		FString* OutFailure,
		const TCHAR* Reason)
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = Reason;
		}
		return false;
	}

	bool IsFinitePositive(const double Value)
	{
		return FMath::IsFinite(Value) && Value > 0.0;
	}

	bool IsFiniteFinaleBoundaryVector(const FVector3d& Value)
	{
		return FMath::IsFinite(Value.X)
			&& FMath::IsFinite(Value.Y)
			&& FMath::IsFinite(Value.Z);
	}

	bool LaunchInputsMatchExactly(
		const FABTSM11FinaleLaunchInput& A,
		const FABTSM11FinaleLaunchInput& B)
	{
		return A.YawDegrees == B.YawDegrees
			&& A.PitchDegrees == B.PitchDegrees
			&& A.Power == B.Power;
	}

	bool TrustRegionsMatchExactly(
		const FABTSM11PrefixTrustRegion& A,
		const FABTSM11PrefixTrustRegion& B)
	{
		return A.PrefixLevel == B.PrefixLevel
			&& LaunchInputsMatchExactly(A.Minimum, B.Minimum)
			&& LaunchInputsMatchExactly(A.Maximum, B.Maximum)
			&& A.CaptureMarginCells == B.CaptureMarginCells
			&& A.ReleaseMarginCells == B.ReleaseMarginCells
			&& A.RegionHash == B.RegionHash;
	}

	bool MatchesFrozenCertifiedManifest(
		const FABTSM11FinaleLayoutPreset& Candidate,
		const FABTSM11FinaleLayoutPreset& Frozen)
	{
		if (Candidate.PresetSourceHash != Frozen.PresetSourceHash
			|| Candidate.PresetHash != Frozen.PresetHash
			|| Candidate.CanonicalScenario.ScenarioHash
				!= Frozen.CanonicalScenario.ScenarioHash
			|| Candidate.ScanContractHash != Frozen.ScanContractHash
			|| Candidate.CertificationHash != Frozen.CertificationHash
			|| Candidate.NominalTrajectoryHash
				!= Frozen.NominalTrajectoryHash
			|| Candidate.PhysicalPlaybackContractVersion
				!= Frozen.PhysicalPlaybackContractVersion
			|| Candidate.PhysicalPlaybackTrajectoryHash
				!= Frozen.PhysicalPlaybackTrajectoryHash
			|| Candidate.CertifiedBundleHash != Frozen.CertifiedBundleHash)
		{
			return false;
		}
		for (int32 Index = 0;
			Index < FABTSM11FinaleLayoutPreset::AssistCount;
			++Index)
		{
			if (!TrustRegionsMatchExactly(
					Candidate.PrefixTrustRegions[Index],
					Frozen.PrefixTrustRegions[Index]))
			{
				return false;
			}
		}
		return true;
	}

	bool ValidateFinaleWorldCompatibility(
		const FABTSM11FinaleLayoutPreset& InPreset,
		const int32 GeneratorVersion,
		const double PrimaryRadiusCM,
		const FABTSM110FinaleLocalFrame& InFinaleFrame,
		FString* OutFailure)
	{
		if (GeneratorVersion != InPreset.CompatibleGeneratorVersion)
		{
			return RejectRuntimeBoundary(
				OutFailure,
				TEXT("IncompatibleGeneratorVersion"));
		}
		if (!InFinaleFrame.IsUsable())
		{
			return RejectRuntimeBoundary(
				OutFailure,
				TEXT("InvalidFinaleFrame"));
		}
		if (!InFinaleFrame.WorldTransform.GetScale3D().Equals(
				FVector::OneVector,
				1.0e-4))
		{
			return RejectRuntimeBoundary(
				OutFailure,
				TEXT("ScaledFinaleFrame"));
		}
		if (InFinaleFrame.LayoutVersion
			!= InPreset.CompatibleFrameLayoutVersion)
		{
			return RejectRuntimeBoundary(
				OutFailure,
				TEXT("IncompatibleFrameLayoutVersion"));
		}
		if (!IsFinitePositive(PrimaryRadiusCM)
			|| FMath::Abs(
				PrimaryRadiusCM - InPreset.ReferencePrimaryRadiusCM)
				> InPreset.PrimaryCompatibilityToleranceCM)
		{
			return RejectRuntimeBoundary(
				OutFailure,
				TEXT("IncompatiblePrimaryRadius"));
		}

		const FABTSM11GravityBodySpec& Primary =
			InPreset.CanonicalScenario.GetPrimary();
		const FVector3d ExpectedPrimaryCenter(
			0.0,
			0.0,
			-InPreset.ReferencePrimaryRadiusCM);
		if (!IsFiniteFinaleBoundaryVector(Primary.CenterCM)
			|| !Primary.CenterCM.Equals(
				ExpectedPrimaryCenter,
				InPreset.PrimaryCompatibilityToleranceCM)
			|| FMath::Abs(
				Primary.VisualRadiusCM
					- InPreset.ReferencePrimaryRadiusCM)
				> InPreset.PrimaryCompatibilityToleranceCM
			|| FMath::Abs(
				Primary.CollisionRadiusCM
					- InPreset.ReferencePrimaryRadiusCM)
				> InPreset.PrimaryCompatibilityToleranceCM)
		{
			return RejectRuntimeBoundary(
				OutFailure,
				TEXT("IncompatibleCanonicalPrimary"));
		}

		const double CanonicalLaunchRadiusCM =
			(InPreset.LaunchModel.PouchLocalPositionCM
				- Primary.CenterCM).Length();
		if (!IsFinitePositive(CanonicalLaunchRadiusCM)
			|| FMath::Abs(
				CanonicalLaunchRadiusCM
					- InPreset.ReferenceLaunchRadiusCM)
				> InPreset.PrimaryCompatibilityToleranceCM)
		{
			return RejectRuntimeBoundary(
				OutFailure,
				TEXT("IncompatibleLaunchRadius"));
		}
		return true;
	}
}

AABTSM11FinaleSystem::AABTSM11FinaleSystem()
{
	PrimaryActorTick.bCanEverTick = false;
	SetActorEnableCollision(false);

	AssistPlanetMeshes.SetNum(ExpectedAssistPresentationCount);
	AssistPlanetMeshes[0] = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT(
		"/Game/M11/Toon/Planets/Mars/SM_Mars.SM_Mars")));
	AssistPlanetMeshes[1] = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT(
		"/Game/M11/Toon/Planets/Jupiter/SM_Jupiter.SM_Jupiter")));
	AssistPlanetMeshes[2] = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT(
		"/Game/M11/Toon/Planets/Saturn/SM_Saturn.SM_Saturn")));
	AssistMeshReferenceRadiusCM = FVector(50.0);
	UFOMesh = TSoftObjectPtr<UStaticMesh>(
		FSoftObjectPath(TEXT("/Game/StaticMesh/UFO/SM_UFO.SM_UFO")));
}

bool AABTSM11FinaleSystem::InitializeFromPrimaryPlanet(
	const AABTSM3Planet& PrimaryPlanet)
{
	if (!PrimaryPlanet.IsPlanetReady())
	{
		return FailInitialization(TEXT("PrimaryPlanetNotReady"));
	}
	if (!PrimaryPlanet.PCGSummary.bAccepted)
	{
		return FailInitialization(TEXT("PrimaryTaskGraphNotAccepted"));
	}
	return InitializeFromRuntimeData(
		PrimaryPlanet.PCGSummary.GeneratorVersion,
		PrimaryPlanet.GetPlanetRadiusCM(),
		PrimaryPlanet.GetFinaleLaunchFrame());
}

bool AABTSM11FinaleSystem::InitializeFromWorldContract(
	const FABTSFinaleWorldContract& WorldContract)
{
	// Preserve the accepted reinitialization contract: a second call rejects
	// without destroying the already committed 3+1 presentation Actors.
	if (State != EABTSM11FinaleSystemState::Uninitialized)
	{
		return InitializeFromRuntimeData(
			WorldContract.Identity.GeneratorVersion,
			WorldContract.PrimaryRadiusCM,
			WorldContract.LaunchFrame);
	}
	if (WorldContract.Identity.ContractVersion
		!= FABTSGeneratedWorldIdentity::CurrentContractVersion)
	{
		return FailInitialization(TEXT("PrimaryWorldContractInvalid"));
	}
	if (!WorldContract.Identity.bSourceWorldAccepted)
	{
		return FailInitialization(TEXT("PrimaryTaskGraphNotAccepted"));
	}
	if (WorldContract.Identity.GenerationAttempt < 0)
	{
		return FailInitialization(TEXT("PrimaryWorldContractInvalid"));
	}
	return InitializeFromRuntimeData(
		WorldContract.Identity.GeneratorVersion,
		WorldContract.PrimaryRadiusCM,
		WorldContract.LaunchFrame);
}

uint64 AABTSM11FinaleSystem::ComputeFinaleFrameDiagnosticHash(
	const FABTSM110FinaleLocalFrame& InFinaleFrame)
{
	FFinaleFrameDiagnosticHash64 Hash;
	Hash.AddInt32(InFinaleFrame.LayoutVersion);
	Hash.AddInt32(InFinaleFrame.LaunchTaskId);
	Hash.AddInt32(InFinaleFrame.AnchorCellId);
	Hash.AddInt32(InFinaleFrame.SlotPairId);
	Hash.AddVector(InFinaleFrame.GetOrigin());
	Hash.AddVector(InFinaleFrame.GetForward());
	Hash.AddVector(InFinaleFrame.GetRight());
	Hash.AddVector(InFinaleFrame.GetUp());
	Hash.AddVector(InFinaleFrame.WorldTransform.GetScale3D());
	Hash.AddVector(InFinaleFrame.LeftSlotWorldLocation);
	Hash.AddVector(InFinaleFrame.RightSlotWorldLocation);
	Hash.AddBool(InFinaleFrame.bValid);
	return Hash.Get();
}

#if WITH_EDITOR

bool AABTSM11FinaleSystem::InitializeFromEditorCandidateRank(
	const int32 CandidateRank,
	const FABTSFinaleWorldContract& WorldContract)
{
	if (State != EABTSM11FinaleSystemState::Uninitialized)
	{
		UE_LOG(
			LogABTSRuntime,
			Warning,
			TEXT("[ABTS][M11-C-v2.1][Candidate] Reinitialization rejected State=%d."),
			static_cast<int32>(State));
		return false;
	}
	if (WorldContract.Identity.ContractVersion
			!= FABTSGeneratedWorldIdentity::CurrentContractVersion
		|| !WorldContract.Identity.bSourceWorldAccepted
		|| WorldContract.Identity.GenerationAttempt < 0)
	{
		return FailInitialization(TEXT("PrimaryWorldContractInvalid"));
	}

	FABTSM11FinaleLayoutPreset CandidatePreset;
	FABTSM11CandidateExperienceIdentity CandidateIdentity;
	FString Failure;
	if (!FABTSM11CandidateExperienceCatalog::BuildCandidate(
			CandidateRank,
			CandidatePreset,
			CandidateIdentity,
			&Failure))
	{
		return FailInitialization(
			FString::Printf(
				TEXT("EditorCandidateBuildRejected:%s"),
				*Failure));
	}

	FString PresetFailure;
	if (!CandidateIdentity.IsValid()
		|| CandidateIdentity.Rank != CandidateRank
		|| !CandidatePreset.IsValid(&PresetFailure)
		|| CandidatePreset.PresetSourceHash != 0
		|| CandidatePreset.PresetHash != 0
		|| CandidatePreset.ScanContractHash != 0
		|| CandidatePreset.CertificationHash != 0
		|| CandidatePreset.NominalTrajectoryHash != 0
		|| CandidatePreset.PhysicalPlaybackTrajectoryHash != 0
		|| CandidatePreset.CertifiedBundleHash != 0)
	{
		return FailInitialization(
			FString::Printf(
				TEXT("EditorCandidateBoundaryRejected:%s"),
				PresetFailure.IsEmpty()
					? TEXT("NonCertifiedIdentityViolation")
					: *PresetFailure));
	}
	if (!ValidateFinaleWorldCompatibility(
			CandidatePreset,
			WorldContract.Identity.GeneratorVersion,
			WorldContract.PrimaryRadiusCM,
			WorldContract.LaunchFrame,
			&Failure))
	{
		return FailInitialization(Failure);
	}

	LayoutPreset = MoveTemp(CandidatePreset);
	bEditorCandidateMode = true;
	EditorCandidateIdentity = CandidateIdentity;
	return CommitValidatedPreset(
		WorldContract.Identity.GeneratorVersion,
		WorldContract.LaunchFrame);
}

#endif

bool AABTSM11FinaleSystem::InitializeFromRuntimeData(
	const int32 GeneratorVersion,
	const double PrimaryRadiusCM,
	const FABTSM110FinaleLocalFrame& InFinaleFrame)
{
	return InitializeFromCertifiedPreset(
		FABTSM11FinaleLayoutPreset::MakeCertifiedV1(),
		GeneratorVersion,
		PrimaryRadiusCM,
		InFinaleFrame);
}

bool AABTSM11FinaleSystem::InitializeFromCertifiedPreset(
	const FABTSM11FinaleLayoutPreset& InPreset,
	const int32 GeneratorVersion,
	const double PrimaryRadiusCM,
	const FABTSM110FinaleLocalFrame& InFinaleFrame)
{
	if (State != EABTSM11FinaleSystemState::Uninitialized)
	{
		UE_LOG(
			LogABTSRuntime,
			Warning,
			TEXT("[ABTS][M11-B][FinaleSystem] Reinitialization rejected State=%d."),
			static_cast<int32>(State));
		return false;
	}

	bEditorCandidateMode = false;
	EditorCandidateIdentity =
		FABTSM11CandidateExperienceIdentity();
	LayoutPreset = InPreset;
	FString Failure;
	if (!ValidateRuntimeBoundary(
			LayoutPreset,
			GeneratorVersion,
			PrimaryRadiusCM,
			InFinaleFrame,
			&Failure))
	{
		return FailInitialization(Failure);
	}

	return CommitValidatedPreset(
		GeneratorVersion,
		InFinaleFrame);
}

bool AABTSM11FinaleSystem::CommitValidatedPreset(
	const int32 GeneratorVersion,
	const FABTSM110FinaleLocalFrame& InFinaleFrame)
{
	FinaleFrame = InFinaleFrame;
	FString Failure;
	if (!SpawnPresentationActorsAtomically(&Failure))
	{
		return FailInitialization(Failure);
	}

	State = EABTSM11FinaleSystemState::Ready;
	FailureReason.Reset();
	DrawCertificationDebugInPIE();
	const FVector LocalStart(
		LayoutPreset.LaunchModel.PouchLocalPositionCM);
	const FVector WorldStart =
		FinaleFrame.TransformLocalPosition(LocalStart);
	const int32 CandidateRank = bEditorCandidateMode
		? EditorCandidateIdentity.Rank
		: 0;
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M11][FinaleFrame] Authority=%s CandidateRank=%d LayoutVersion=%d FrameHash=0x%016llx LaunchTask=%d Anchor=%d Pair=%d LocalStart=%s WorldStart=%s Origin=%s Forward=%s Right=%s Up=%s"),
		bEditorCandidateMode ? TEXT("PreviewTest") : TEXT("Production"),
		CandidateRank,
		FinaleFrame.LayoutVersion,
		static_cast<unsigned long long>(
			ComputeFinaleFrameDiagnosticHash(FinaleFrame)),
		FinaleFrame.LaunchTaskId,
		FinaleFrame.AnchorCellId,
		FinaleFrame.SlotPairId,
		*LocalStart.ToCompactString(),
		*WorldStart.ToCompactString(),
		*FinaleFrame.GetOrigin().ToCompactString(),
		*FinaleFrame.GetForward().ToCompactString(),
		*FinaleFrame.GetRight().ToCompactString(),
		*FinaleFrame.GetUp().ToCompactString());
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M11-B][FinaleSystem] Ready Generator=%d FrameLayout=%d Pair=%d PresetHash=0x%016llx ScenarioHash=0x%08x BundleHash=0x%016llx Assists=%d UFO=%d Mode=%s%s%s"),
		GeneratorVersion,
		FinaleFrame.LayoutVersion,
		FinaleFrame.SlotPairId,
		static_cast<unsigned long long>(LayoutPreset.PresetHash),
		LayoutPreset.CanonicalScenario.ScenarioHash,
		static_cast<unsigned long long>(LayoutPreset.CertifiedBundleHash),
		GetSpawnedAssistActorCount(),
		HasSpawnedUFOActor() ? 1 : 0,
		bEditorCandidateMode
			? TEXT("EditorCandidate-UNCERTIFIED")
			: TEXT("CertifiedV1"),
		bEditorCandidateMode ? TEXT(" ") : TEXT(""),
		bEditorCandidateMode
			? *EditorCandidateIdentity.ToLogString()
			: TEXT(""));
	return true;
}

bool AABTSM11FinaleSystem::ValidateRuntimeBoundary(
	const FABTSM11FinaleLayoutPreset& InPreset,
	const int32 GeneratorVersion,
	const double PrimaryRadiusCM,
	const FABTSM110FinaleLocalFrame& InFinaleFrame,
	FString* OutFailure)
{
	FString PresetFailure;
	if (!InPreset.IsValid(&PresetFailure))
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = FString::Printf(
				TEXT("InvalidCertifiedPreset:%s"),
				*PresetFailure);
		}
		return false;
	}
	if (InPreset.PresetSourceHash == 0
		|| InPreset.PresetHash == 0
		|| InPreset.ScanContractHash == 0
		|| InPreset.CertificationHash == 0
		|| InPreset.NominalTrajectoryHash == 0
		|| InPreset.PhysicalPlaybackTrajectoryHash == 0
		|| InPreset.CertifiedBundleHash == 0)
	{
		return RejectRuntimeBoundary(
			OutFailure,
			TEXT("IncompleteCertificationIdentity"));
	}
	const FABTSM11FinaleLayoutPreset FrozenPreset =
		FABTSM11FinaleLayoutPreset::MakeCertifiedV1();
	if (!MatchesFrozenCertifiedManifest(InPreset, FrozenPreset))
	{
		return RejectRuntimeBoundary(
			OutFailure,
			TEXT("CertifiedBundleManifestMismatch"));
	}
	return ValidateFinaleWorldCompatibility(
		InPreset,
		GeneratorVersion,
		PrimaryRadiusCM,
		InFinaleFrame,
		OutFailure);
}

bool AABTSM11FinaleSystem::BuildRequest(
	const FABTSM11FinaleLaunchInput& Input,
	const uint8 EnabledAssistMask,
	FABTSM11TrajectoryRequest& OutRequest,
	FString* OutFailure) const
{
	if (!IsLayoutReady())
	{
		return RejectRuntimeBoundary(
			OutFailure,
			TEXT("FinaleLayoutNotReady"));
	}

	// Deliberately no Actor transform lookup here. The certified local preset is
	// the only authority for preview and later playback.
	return LayoutPreset.BuildRequest(
		Input,
		EnabledAssistMask,
		OutRequest,
		OutFailure);
}

bool AABTSM11FinaleSystem::BuildPhysicalPlaybackRequest(
	const FABTSM11FinaleLaunchInput& Input,
	FABTSM11TrajectoryRequest& OutRequest,
	FString* OutFailure) const
{
	if (!IsLayoutReady())
	{
		return RejectRuntimeBoundary(
			OutFailure,
			TEXT("FinaleLayoutNotReady"));
	}
	return LayoutPreset.BuildPhysicalPlaybackRequest(
		Input,
		0x7u,
		OutRequest,
		OutFailure);
}

int32 AABTSM11FinaleSystem::GetSpawnedAssistActorCount() const
{
	int32 Count = 0;
	for (const AABTSM11GravityBodyActor* Actor : GravityBodyActors)
	{
		Count += IsValid(Actor) ? 1 : 0;
	}
	return Count;
}

bool AABTSM11FinaleSystem::HasSpawnedUFOActor() const
{
	return IsValid(UFOActor.Get());
}

AABTSM11UFOActor* AABTSM11FinaleSystem::GetUFOActor() const
{
	return UFOActor.Get();
}

bool AABTSM11FinaleSystem::TryGetStylizedObjectClass(
	const AActor& RuntimePresentationActor,
	EABTSStylizedObjectClass& OutObjectClass) const
{
	OutObjectClass = EABTSStylizedObjectClass::None;
	if (!IsLayoutReady())
	{
		return false;
	}

	if (const AABTSM11GravityBodyActor* BodyActor =
		Cast<AABTSM11GravityBodyActor>(&RuntimePresentationActor))
	{
		if (!IsValid(BodyActor)
			|| BodyActor->GetOwner() != this
			|| !BodyActor->IsPresentationConfigured())
		{
			return false;
		}

		for (int32 AssistIndex = 1;
			AssistIndex <= FABTSM11GravityScenario::AssistCount;
			++AssistIndex)
		{
			const FABTSM11GravityBodySpec& Assist =
				LayoutPreset.CanonicalScenario.GetAssist(AssistIndex);
			if (GravityBodyActors.IsValidIndex(AssistIndex - 1)
				&& GravityBodyActors[AssistIndex - 1] == BodyActor
				&& BodyActor->GetStableBodyId() == Assist.BodyId
				&& BodyActor->GetGravityRole() == Assist.Role
				&& Assist.IsAssist())
			{
				OutObjectClass = EABTSStylizedObjectClass::FinalePlanet;
				return true;
			}
		}
		return false;
	}

	const AABTSM11UFOActor* TargetActor =
		Cast<AABTSM11UFOActor>(&RuntimePresentationActor);
	if (!IsValid(TargetActor)
		|| TargetActor != UFOActor.Get()
		|| TargetActor->GetOwner() != this
		|| !TargetActor->IsPresentationConfigured()
		|| TargetActor->GetStableTargetId()
			!= LayoutPreset.CanonicalScenario.Target.TargetId)
	{
		return false;
	}

	OutObjectClass = EABTSStylizedObjectClass::FinaleUFO;
	return true;
}

void AABTSM11FinaleSystem::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	DestroyPresentationActors();
	Super::EndPlay(EndPlayReason);
}

bool AABTSM11FinaleSystem::SpawnPresentationActorsAtomically(
	FString* OutFailure)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return RejectRuntimeBoundary(
			OutFailure,
			TEXT("MissingFinaleWorld"));
	}
	TArray<AABTSM11GravityBodyActor*> PendingBodies;
	PendingBodies.Reserve(ExpectedAssistPresentationCount);
	AABTSM11UFOActor* PendingUFO = nullptr;
	const auto RollBackPending = [&PendingBodies, &PendingUFO]()
	{
		for (AABTSM11GravityBodyActor* Actor : PendingBodies)
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}
		PendingBodies.Reset();
		if (IsValid(PendingUFO))
		{
			PendingUFO->Destroy();
			PendingUFO = nullptr;
		}
	};

	for (int32 AssistIndex = 1;
		AssistIndex <= FABTSM11GravityScenario::AssistCount;
		++AssistIndex)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = this;
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AABTSM11GravityBodyActor* Actor =
			World->SpawnActor<AABTSM11GravityBodyActor>(
				AABTSM11GravityBodyActor::StaticClass(),
				FTransform::Identity,
				SpawnParameters);
		if (Actor == nullptr)
		{
			RollBackPending();
			return RejectRuntimeBoundary(
				OutFailure,
				TEXT("AssistPresentationSpawnFailed"));
		}

		PendingBodies.Add(Actor);
		Actor->SetActorHiddenInGame(true);
		const int32 MeshIndex = AssistIndex - 1;
		const bool bConfigured = Actor->ConfigurePresentation(
			LayoutPreset.CanonicalScenario.GetAssist(AssistIndex),
			FinaleFrame,
			ResolveAssistMesh(MeshIndex),
			ResolveAssistMeshReferenceRadiusCM(MeshIndex));
		if (!bConfigured)
		{
			RollBackPending();
			return RejectRuntimeBoundary(
				OutFailure,
				TEXT("AssistPresentationConfigureFailed"));
		}
	}

	FActorSpawnParameters TargetSpawnParameters;
	TargetSpawnParameters.Owner = this;
	TargetSpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	PendingUFO = World->SpawnActor<AABTSM11UFOActor>(
		AABTSM11UFOActor::StaticClass(),
		FTransform::Identity,
		TargetSpawnParameters);
	if (PendingUFO == nullptr)
	{
		RollBackPending();
		return RejectRuntimeBoundary(
			OutFailure,
			TEXT("UFOPresentationSpawnFailed"));
	}
	PendingUFO->SetActorHiddenInGame(true);
	const bool bTargetConfigured = PendingUFO->ConfigurePresentation(
		LayoutPreset.CanonicalScenario.Target,
		FinaleFrame,
		ResolveUFOMesh(),
		UFOMeshReferenceRadiusCM,
		UFOVisualRadiusCM);
	if (!bTargetConfigured)
	{
		RollBackPending();
		return RejectRuntimeBoundary(
			OutFailure,
			TEXT("UFOPresentationConfigureFailed"));
	}

	GravityBodyActors.Reset(ExpectedAssistPresentationCount);
	for (AABTSM11GravityBodyActor* Actor : PendingBodies)
	{
		GravityBodyActors.Add(Actor);
		Actor->SetActorHiddenInGame(false);
	}
	UFOActor = PendingUFO;
	UFOActor->SetActorHiddenInGame(false);
	return GravityBodyActors.Num() == ExpectedAssistPresentationCount
		&& IsValid(UFOActor.Get());
}

void AABTSM11FinaleSystem::DrawCertificationDebugInPIE() const
{
#if WITH_EDITOR
	const UWorld* World = GetWorld();
	if (!bDrawCertificationDebugInPIE
		|| World == nullptr
		|| World->WorldType != EWorldType::PIE)
	{
		return;
	}

	const TStaticArray<FColor, FABTSM11GravityScenario::AssistCount>
		AssistColors = {
			FColor(220, 80, 60),
			FColor(235, 165, 45),
			FColor(235, 215, 75)};
	for (int32 AssistIndex = 1;
		AssistIndex <= FABTSM11GravityScenario::AssistCount;
		++AssistIndex)
	{
		const FABTSM11GravityBodySpec& Assist =
			LayoutPreset.CanonicalScenario.GetAssist(AssistIndex);
		const FVector Center = FinaleFrame.TransformLocalPosition(
			FVector(Assist.CenterCM));
		const FColor Color = AssistColors[AssistIndex - 1];
		DrawDebugSphere(
			World,
			Center,
			Assist.InfluenceRadiusCM,
			64,
			Color,
			true,
			-1.0f,
			0,
			2.0f);
		DrawDebugString(
			World,
			Center + FinaleFrame.GetUp() * Assist.InfluenceRadiusCM,
			FString::Printf(
				TEXT("M11 Assist%d Influence"),
				AssistIndex),
			nullptr,
			Color,
			-1.0f,
			true,
			1.0f);
	}

	const FABTSM11TargetSpec& Target =
		LayoutPreset.CanonicalScenario.Target;
	const FVector QualifiedCenter = FinaleFrame.TransformLocalPosition(
		FVector(Target.CenterCM));
	const FVector PhysicalCenter = FinaleFrame.TransformLocalPosition(
		FVector(Target.GetGeometricContactCenterCM()));
	DrawDebugSphere(
		World,
		QualifiedCenter,
		LayoutPreset.TargetApproachRadiusCM,
		64,
		FColor(60, 190, 255),
		true,
		-1.0f,
		0,
		1.5f);
	DrawDebugSphere(
		World,
		QualifiedCenter,
		Target.HitRadiusCM,
		64,
		FColor(80, 255, 130),
		true,
		-1.0f,
		0,
		2.5f);
	DrawDebugSphere(
		World,
		PhysicalCenter,
		Target.GetGeometricContactRadiusCM(),
		48,
		FColor(255, 80, 220),
		true,
		-1.0f,
		0,
		3.0f);
	DrawDebugString(
		World,
		QualifiedCenter
			+ FinaleFrame.GetUp() * LayoutPreset.TargetApproachRadiusCM,
		TEXT("M11 TargetApproach / Qualified Intercept"),
		nullptr,
		FColor(60, 190, 255),
		-1.0f,
		true,
		1.0f);
	DrawDebugString(
		World,
		PhysicalCenter
			+ FinaleFrame.GetUp()
				* Target.GetGeometricContactRadiusCM(),
		TEXT("M11 Physical UFO 800cm"),
		nullptr,
		FColor(255, 80, 220),
		-1.0f,
		true,
		1.0f);

	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M11-B][Debug] PIE overlay Assists=3 Approach=%.1f Intercept=%.1f Physical=%.1f"),
		LayoutPreset.TargetApproachRadiusCM,
		Target.HitRadiusCM,
		Target.GetGeometricContactRadiusCM());
#endif
}

void AABTSM11FinaleSystem::DestroyPresentationActors()
{
	for (AABTSM11GravityBodyActor* Actor : GravityBodyActors)
	{
		if (IsValid(Actor))
		{
			Actor->Destroy();
		}
	}
	GravityBodyActors.Reset();
	if (IsValid(UFOActor.Get()))
	{
		UFOActor->Destroy();
	}
	UFOActor = nullptr;
}

bool AABTSM11FinaleSystem::FailInitialization(const FString& Reason)
{
	DestroyPresentationActors();
	bEditorCandidateMode = false;
	EditorCandidateIdentity =
		FABTSM11CandidateExperienceIdentity();
	State = EABTSM11FinaleSystemState::Failed;
	FailureReason = Reason.IsEmpty()
		? TEXT("UnknownFinaleInitializationFailure")
		: Reason;
	UE_LOG(
		LogABTSRuntime,
		Error,
		TEXT("[ABTS][M11-B][FinaleSystem] Rejected Reason=%s"),
		*FailureReason);
	return false;
}

UStaticMesh* AABTSM11FinaleSystem::ResolveAssistMesh(
	const int32 AssistArrayIndex) const
{
	return AssistPlanetMeshes.IsValidIndex(AssistArrayIndex)
		&& !AssistPlanetMeshes[AssistArrayIndex].IsNull()
		? AssistPlanetMeshes[AssistArrayIndex].LoadSynchronous()
		: nullptr;
}

UStaticMesh* AABTSM11FinaleSystem::ResolveUFOMesh() const
{
	return UFOMesh.IsNull() ? nullptr : UFOMesh.LoadSynchronous();
}

double AABTSM11FinaleSystem::ResolveAssistMeshReferenceRadiusCM(
	const int32 AssistArrayIndex) const
{
	const double Radius = AssistArrayIndex == 0
		? AssistMeshReferenceRadiusCM.X
		: AssistArrayIndex == 1
			? AssistMeshReferenceRadiusCM.Y
			: AssistMeshReferenceRadiusCM.Z;
	return FMath::Max(Radius, 0.01);
}
