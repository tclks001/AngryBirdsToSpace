// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/ABTSM110FinaleTypes.h"
#include "World/ABTSM11CandidateExperienceCatalog.h"
#include "World/ABTSM11FinaleLayoutTypes.h"
#include "ABTSM11FinaleSystem.generated.h"

class AABTSM3Planet;
class AABTSM11GravityBodyActor;
class AABTSM11UFOActor;
class UStaticMesh;
struct FABTSFinaleWorldContract;

UENUM(BlueprintType)
enum class EABTSM11FinaleSystemState : uint8
{
	Uninitialized = 0,
	Ready,
	Failed
};

/**
 * M11-B runtime boundary between the generated M3 world and the certified,
 * finale-local trajectory data.
 *
 * The system keeps FABTSM11FinaleLayoutPreset in local coordinates. The M3
 * frame is used in one direction only to place visual Actors; Actor transforms
 * are never sampled when building a solver request.
 */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSM11FinaleSystem : public AActor
{
	GENERATED_BODY()

public:
	static constexpr int32 ExpectedAssistPresentationCount =
		FABTSM11GravityScenario::AssistCount;
	static constexpr int32 ExpectedTargetPresentationCount = 1;

	AABTSM11FinaleSystem();

	/**
	 * Legacy compatibility entry for existing callers and error-code parity.
	 * Production GameMode code uses InitializeFromWorldContract instead.
	 */
	bool InitializeFromPrimaryPlanet(const AABTSM3Planet& PrimaryPlanet);

	/**
	 * Preferred stable entry for parallel M11 development. It contains no M3
	 * TaskGraph arrays, M9 satellite Actor or mutable world-generation state.
	 */
	bool InitializeFromWorldContract(
		const FABTSFinaleWorldContract& WorldContract);

#if WITH_EDITOR
	/**
	 * Editor-PIE experience entry for a frozen M11-B v2.1 Candidate rank.
	 * It rebuilds the standard C++ work item and fails closed on any frozen
	 * identity mismatch. It is absent from non-Editor targets.
	 */
	bool InitializeFromEditorCandidateRank(
		int32 CandidateRank,
		const FABTSFinaleWorldContract& WorldContract);
#endif

	/**
	 * Narrow data entry used by production and automation alike.
	 * GeneratorVersion and PrimaryRadiusCM must come from the accepted M3
	 * world. The certified preset is always constructed internally.
	 */
	bool InitializeFromRuntimeData(
		int32 GeneratorVersion,
		double PrimaryRadiusCM,
		const FABTSM110FinaleLocalFrame& InFinaleFrame);

	/**
	 * Fail-closed compatibility gate. It deliberately accepts no Actor,
	 * satellite, mesh-bounds or world-coordinate gravity input.
	 */
	static bool ValidateRuntimeBoundary(
		const FABTSM11FinaleLayoutPreset& InPreset,
		int32 GeneratorVersion,
		double PrimaryRadiusCM,
		const FABTSM110FinaleLocalFrame& InFinaleFrame,
		FString* OutFailure = nullptr);

	/** Builds an authoritative M11-A request entirely in finale-local space. */
	bool BuildRequest(
		const FABTSM11FinaleLaunchInput& Input,
		uint8 EnabledAssistMask,
		FABTSM11TrajectoryRequest& OutRequest,
		FString* OutFailure = nullptr) const;

	/**
	 * Builds the production full-flight request to the physical UFO.
	 * Runtime playback always enables all three certified assist planets.
	 */
	bool BuildPhysicalPlaybackRequest(
		const FABTSM11FinaleLaunchInput& Input,
		FABTSM11TrajectoryRequest& OutRequest,
		FString* OutFailure = nullptr) const;

	UFUNCTION(BlueprintPure, Category = "ABTS|M11-B|Finale")
	bool IsLayoutReady() const
	{
		return State == EABTSM11FinaleSystemState::Ready;
	}

	UFUNCTION(BlueprintPure, Category = "ABTS|M11-B|Finale")
	EABTSM11FinaleSystemState GetSystemState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "ABTS|M11-B|Finale")
	FString GetFailureReason() const { return FailureReason; }

	UFUNCTION(BlueprintPure, Category = "ABTS|M11-B|Finale")
	int32 GetSpawnedAssistActorCount() const;

	UFUNCTION(BlueprintPure, Category = "ABTS|M11-B|Finale")
	bool HasSpawnedUFOActor() const;

	UFUNCTION(BlueprintPure, Category = "ABTS|M11-C|Candidate")
	bool IsEditorCandidateMode() const
	{
		return bEditorCandidateMode;
	}

	const FABTSM11CandidateExperienceIdentity&
		GetEditorCandidateIdentity() const
	{
		return EditorCandidateIdentity;
	}

	const FABTSM11FinaleLayoutPreset& GetLayoutPreset() const
	{
		return LayoutPreset;
	}

	const FABTSM110FinaleLocalFrame& GetFinaleFrame() const
	{
		return FinaleFrame;
	}

	const TArray<TObjectPtr<AABTSM11GravityBodyActor>>&
		GetGravityBodyActors() const
	{
		return GravityBodyActors;
	}

	AABTSM11UFOActor* GetUFOActor() const;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	friend class FABTSM11BRuntimePresentationTest;

	bool InitializeFromCertifiedPreset(
		const FABTSM11FinaleLayoutPreset& InPreset,
		int32 GeneratorVersion,
		double PrimaryRadiusCM,
		const FABTSM110FinaleLocalFrame& InFinaleFrame);
	bool CommitValidatedPreset(
		int32 GeneratorVersion,
		const FABTSM110FinaleLocalFrame& InFinaleFrame);
	bool SpawnPresentationActorsAtomically(FString* OutFailure);
	void DrawCertificationDebugInPIE() const;
	void DestroyPresentationActors();
	bool FailInitialization(const FString& Reason);

	UStaticMesh* ResolveAssistMesh(int32 AssistArrayIndex) const;
	UStaticMesh* ResolveUFOMesh() const;
	double ResolveAssistMeshReferenceRadiusCM(int32 AssistArrayIndex) const;

	/**
	 * Optional Mars/Jupiter/Saturn low-poly meshes in assist order.
	 * A null or unloaded entry uses the Actor's Engine sphere fallback.
	 */
	UPROPERTY(EditAnywhere, Category = "ABTS|M11-B|Presentation")
	TArray<TSoftObjectPtr<UStaticMesh>> AssistPlanetMeshes;

	/** Explicit unit-scale art radii; gameplay never reads mesh bounds. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M11-B|Presentation",
		meta = (ClampMin = "0.01", Units = "cm"))
	FVector AssistMeshReferenceRadiusCM = FVector(50.0);

	/** Defaults to the user-provided M11 UFO mesh and falls back to a sphere. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M11-B|Presentation")
	TSoftObjectPtr<UStaticMesh> UFOMesh;

	UPROPERTY(EditAnywhere, Category = "ABTS|M11-B|Presentation",
		meta = (ClampMin = "0.01", Units = "cm"))
	double UFOMeshReferenceRadiusCM = 50.0;

	/** Zero displays the analytic hit radius; positive values are art-only. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M11-B|Presentation",
		meta = (ClampMin = "0.0", Units = "cm"))
	double UFOVisualRadiusCM = 0.0;

	/**
	 * Editor-PIE-only persistent wire overlay for layout acceptance. It never
	 * creates collision, gravity bodies or solver input and is not drawn by
	 * commandlets or packaged builds.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-B|Debug")
	bool bDrawCertificationDebugInPIE = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "ABTS|M11-B|Runtime",
		meta = (AllowPrivateAccess = "true"))
	EABTSM11FinaleSystemState State =
		EABTSM11FinaleSystemState::Uninitialized;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "ABTS|M11-B|Runtime",
		meta = (AllowPrivateAccess = "true"))
	FString FailureReason;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "ABTS|M11-B|Runtime",
		meta = (AllowPrivateAccess = "true"))
	FABTSM110FinaleLocalFrame FinaleFrame;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AABTSM11GravityBodyActor>> GravityBodyActors;

	UPROPERTY(Transient)
	TObjectPtr<AABTSM11UFOActor> UFOActor;

	/** Plain immutable solver data; intentionally not derived from UPROPERTY Actors. */
	FABTSM11FinaleLayoutPreset LayoutPreset;

	/**
	 * Always false/empty in non-Editor targets and for the production v1
	 * Certified Bundle. Candidate identity never aliases certification hashes.
	 */
	bool bEditorCandidateMode = false;
	FABTSM11CandidateExperienceIdentity EditorCandidateIdentity;
};
