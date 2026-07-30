// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Crafting/ABTSCraftingTypes.h"
#include "Inventory/ABTSInventoryTypes.h"
#include "Party/ABTSBirdTypes.h"
#include "PCG/ABTSM3MonthlySlingshotField.h"
#include "Slingshot/ABTSSlingshotTypes.h"
#include "ABTSM3MonthlyWitness.generated.h"

UENUM(BlueprintType)
enum class EABTSM3WitnessAuthority : uint8
{
	None = 0,
	Fixture = 1,
	Integration = 2
};

UENUM(BlueprintType)
enum class EABTSM3MonthlyWitnessRejectReason : uint8
{
	None = 0,
	NotEvaluated = 1,
	InvalidConfig = 2,
	InvalidSource = 3,
	ProviderUnavailable = 4,
	ProviderIdentityMismatch = 5,
	ProfileCatalogMismatch = 6,
	CandidateJoinMismatch = 7,
	ProfileMismatch = 8,
	GeometryInvalid = 9,
	SearchBudgetExceeded = 10,
	PositiveWitnessNotFound = 11,
	PriorDomainIncomplete = 12,
	M9EvidenceMissing = 13,
	ProgressionInvalid = 14,
	BridgeEvidenceInvalid = 15,
	HashMismatch = 16,
	NoAcceptedCandidate = 17
};

UENUM(BlueprintType)
enum class EABTSM3TrajectoryTermination : uint8
{
	None = 0,
	TargetHit = 1,
	WorldHit = 2,
	TimeLimit = 3,
	Invalid = 4
};

UENUM(BlueprintType)
enum class EABTSM3PriorTierCertificateState : uint8
{
	NotRequired = 0,
	CompleteInfeasible = 1,
	Incomplete = 2
};

UENUM(BlueprintType)
enum class EABTSM3FlowStepKind : uint8
{
	Recipe = 0,
	EncounterReward = 1,
	BridgeGate = 2,
	FinaleEntry = 3,
	InitialState = 4
};

UENUM(BlueprintType)
enum class EABTSM3BranchUtilityState : uint8
{
	NotRequired = 0,
	Accepted = 1,
	Rejected = 2
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlyWitnessConfig
{
	GENERATED_BODY()

	FABTSM3MonthlyWitnessConfig();

	/** Disabled until Integration supplies a production authority adapter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	bool bBuildGameplayFinalize = false;

	/** Diagnostic only; excluded from deterministic identity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	bool bEmitWitnessLogs = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness",
		meta = (ClampMin = "1", ClampMax = "8192"))
	int32 MaxWitnessEvaluationsPerEncounter = 8192;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness",
		meta = (ClampMin = "2", ClampMax = "16"))
	int32 PullAlphaSampleCount = 7;

	/** Odd square grid, clipped to the unit aim disk. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness",
		meta = (ClampMin = "3", ClampMax = "9"))
	int32 AimAxisSampleCount = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness",
		meta = (ClampMin = "0", Units = "cm"))
	int32 MinimumForbiddenClearanceCM = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness",
		meta = (ClampMin = "1", Units = "cm"))
	int32 MinimumM9AblationMissCM = 100;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness",
		meta = (ClampMin = "1", ClampMax = "8"))
	int32 MaximumRetainedCandidates = 3;

	/** E1..E6 current gameplay tiers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Schedule")
	TArray<EABTSSlingshotTier> EncounterTiers;

	/** E1..E6 order indices requiring a complete prior-tier certificate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Schedule")
	TArray<int32> PriorTierRequiredEncounterOrders;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Schedule")
	EABTSSlingshotTier PriorTier = EABTSSlingshotTier::Simple;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Schedule")
	int32 M9PracticeEncounterOrder = 4;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Version")
	int32 WitnessPlannerVersion = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Version")
	int32 FlowValidatorVersion = 1;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3WitnessServiceIdentity
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Provider")
	EABTSM3WitnessAuthority Authority = EABTSM3WitnessAuthority::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Provider")
	int32 ServiceSchemaVersion = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Provider")
	int64 SolverHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Provider")
	int64 GeometryHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Provider")
	int64 GravitySnapshotHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Provider")
	int64 ProfileCatalogHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Provider")
	int64 ProgressionCatalogHash = 0;

	/** Frozen v1 Simple/Reinforced eligible-bird catalog identity. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Provider")
	int64 BirdCatalogHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Provider")
	bool bCertified = false;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3WitnessAttackFace
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Profile")
	FName FaceId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Profile",
		meta = (Units = "cm"))
	FVector LocalCenterCM = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Profile")
	FVector LocalNormal = FVector::ForwardVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Profile",
		meta = (Units = "cm"))
	float RadiusCM = 100.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Profile",
		meta = (Units = "cm/s"))
	float MinimumImpactSpeedCMPerSec = 1.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Profile")
	bool bRequiresBird = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Profile")
	EABTSBirdId RequiredBird = EABTSBirdId::Red;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Profile")
	int64 FaceHash = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3WitnessProfileDescriptor
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Profile")
	FName ProfileId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Profile",
		meta = (Units = "cm"))
	FVector BoundsExtentCM = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Profile")
	TArray<FABTSM3WitnessAttackFace> AttackFaces;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Profile")
	int64 DescriptorHash = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3WitnessProfileCatalog
{
	GENERATED_BODY()

	/** Hash of the complete descriptor catalog supplied to R4. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Profile")
	int64 FullCatalogHash = 0;

	/** R3 Bounds/PVS catalog identity this snapshot is compatible with. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Profile")
	int64 SpatialSourceCatalogHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Profile")
	TArray<FABTSM3WitnessProfileDescriptor> Descriptors;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3WitnessSlotGeometry
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Geometry")
	int32 CellId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Geometry",
		meta = (Units = "cm"))
	FVector CordSocketWorldCM = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3WitnessForbiddenSphere
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Geometry")
	FName VolumeId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Geometry",
		meta = (Units = "cm"))
	FVector CenterWorldCM = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Geometry",
		meta = (Units = "cm"))
	float RadiusCM = 0.0f;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3ResolvedWitnessGeometry
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Geometry")
	int32 EncounterId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Geometry")
	int32 EncounterOrder = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Geometry")
	TArray<FABTSM3WitnessSlotGeometry> Slots;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Geometry")
	FTransform TargetWorldTransform = FTransform::Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Geometry")
	TArray<FABTSM3WitnessForbiddenSphere> ForbiddenSpheres;

	/** Non-empty only for E5; identifies the practice satellite collision volume. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Geometry")
	FName M9SatelliteForbiddenVolumeId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Geometry")
	int64 GeometryHash = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3WitnessLaunchInput
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Launch")
	int32 EncounterId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Launch")
	int32 EncounterOrder = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Launch")
	int32 SlotACellId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Launch")
	int32 SlotBCellId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Launch")
	EABTSSlingshotTier Tier = EABTSSlingshotTier::Simple;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Launch")
	EABTSBirdId Bird = EABTSBirdId::Red;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Launch")
	int32 LaunchSideSign = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Launch")
	int32 PullAlphaQ = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Launch")
	int32 AimRightQ = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Launch")
	int32 AimUpQ = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Launch")
	bool bEnableSatelliteGravity = true;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3WitnessTrajectoryRequest
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Trajectory")
	int32 SourceRouteCandidateId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Trajectory")
	FABTSM3WitnessLaunchInput LaunchInput;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Trajectory",
		meta = (Units = "cm"))
	FVector SlotAWorldCM = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Trajectory",
		meta = (Units = "cm"))
	FVector SlotBWorldCM = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Trajectory",
		meta = (Units = "cm"))
	FVector TargetCenterWorldCM = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Trajectory",
		meta = (Units = "cm"))
	float TargetRadiusCM = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Trajectory")
	int64 SolverHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Trajectory")
	int64 GravitySnapshotHash = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3WitnessTrajectorySample
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Trajectory",
		meta = (Units = "s"))
	float TimeSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Trajectory",
		meta = (Units = "cm"))
	FVector PositionWorldCM = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Trajectory",
		meta = (Units = "cm/s"))
	FVector VelocityWorldCMPerSec = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3WitnessTrajectoryResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Trajectory")
	TArray<FABTSM3WitnessTrajectorySample> Samples;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Trajectory")
	EABTSM3TrajectoryTermination Termination =
		EABTSM3TrajectoryTermination::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Trajectory",
		meta = (Units = "cm"))
	FVector LandingWorldCM = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Trajectory")
	int32 M9QueryCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Trajectory")
	int32 M9NonZeroAccelerationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Trajectory",
		meta = (Units = "cm/s^2"))
	float PeakM9AccelerationCMPerSecSq = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Trajectory")
	int64 SolverHashEcho = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Trajectory")
	int64 GravitySnapshotHashEcho = 0;
};

/** Complete same-input M9-off counterfactual persisted by the E5 witness. */
USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3M9AblationEvidence
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|M9")
	FABTSM3WitnessLaunchInput LaunchInput;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|M9")
	TArray<FABTSM3WitnessTrajectorySample> Samples;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|M9")
	EABTSM3TrajectoryTermination Termination =
		EABTSM3TrajectoryTermination::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|M9",
		meta = (Units = "cm"))
	FVector LandingWorldCM = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|M9",
		meta = (Units = "cm"))
	int32 TargetMissCM = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|M9")
	int32 M9QueryCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|M9")
	int32 M9NonZeroAccelerationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|M9",
		meta = (Units = "cm/s^2"))
	float PeakM9AccelerationCMPerSecSq = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|M9")
	int64 SolverHashEcho = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|M9")
	int64 GravitySnapshotHashEcho = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|M9")
	int64 EvidenceHash = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3WitnessItemAmount
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	EABTSItemId ItemId = EABTSItemId::Branch;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	int32 Quantity = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3WitnessRecipe
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	FName RecipeId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	EABTSCraftingStationType RequiredStation =
		EABTSCraftingStationType::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	TArray<FABTSM3WitnessItemAmount> Inputs;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	TArray<FABTSM3WitnessItemAmount> Outputs;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	TArray<EABTSM3ProgressKey> RequiredKeys;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	TArray<EABTSM3ProgressKey> GrantedKeys;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3WitnessEncounterReward
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	int32 EncounterId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	int32 EncounterOrder = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	TArray<FABTSM3WitnessItemAmount> Items;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	TArray<EABTSM3ProgressKey> RequiredKeys;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	TArray<EABTSM3ProgressKey> GrantedKeys;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3BridgeGateEvidence
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	int32 SourceRouteCandidateId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	FName BarrierId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	int32 GateCellId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	int32 PreBridgeCellId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	int32 PostBridgeCellId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	bool bBlockedBeforeBridge = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	bool bReachableAfterBridge = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	bool bNoBypassBeforeBridge = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	int64 EvidenceHash = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3WitnessProgressionSnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	int64 CatalogHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	bool bWorkbenchStationAvailable = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	bool bFurnaceStationAvailable = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	TArray<FABTSM3WitnessItemAmount> InitialItems;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	TArray<FABTSM3WitnessRecipe> Recipes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	TArray<FABTSM3WitnessEncounterReward> EncounterRewards;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	FABTSM3BridgeGateEvidence BridgeEvidence;
};

/** Read-only Integration seam. Tests may provide Fixture authority; runtime never creates one implicitly. */
class ABTSRUNTIME_API IABTSM3MonthlyWitnessServices
{
public:
	virtual ~IABTSM3MonthlyWitnessServices() = default;

	virtual bool GetIdentity(
		FABTSM3WitnessServiceIdentity& OutIdentity,
		FString& OutFailure) const = 0;

	virtual bool GetProfileCatalog(
		FABTSM3WitnessProfileCatalog& OutCatalog,
		FString& OutFailure) const = 0;

	virtual bool ResolveEncounterGeometry(
		int32 SourceRouteCandidateId,
		const FABTSM3MonthlySpatialEncounter& Encounter,
		const FABTSM3MonthlySlingshotField& Field,
		FABTSM3ResolvedWitnessGeometry& OutGeometry,
		FString& OutFailure) const = 0;

	virtual bool GetEligibleBirds(
		EABTSSlingshotTier Tier,
		TArray<EABTSBirdId>& OutBirds,
		FString& OutFailure) const = 0;

	virtual bool EvaluateTrajectory(
		const FABTSM3WitnessTrajectoryRequest& Request,
		FABTSM3WitnessTrajectoryResult& OutResult,
		FString& OutFailure) const = 0;

	virtual bool GetProgressionSnapshot(
		int32 SourceRouteCandidateId,
		FABTSM3WitnessProgressionSnapshot& OutSnapshot,
		FString& OutFailure) const = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3BallisticWitness
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	FABTSM3WitnessLaunchInput LaunchInput;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	FName ProfileId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	FName AttackFaceId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int64 ResolvedGeometryHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int64 ProfileDescriptorHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int64 AttackFaceHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	TArray<FABTSM3WitnessTrajectorySample> Samples;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	EABTSM3TrajectoryTermination Termination =
		EABTSM3TrajectoryTermination::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness",
		meta = (Units = "cm"))
	FVector PredictedImpactWorldCM = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness",
		meta = (Units = "cm"))
	int32 MinimumClearanceCM = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness",
		meta = (Units = "cm"))
	int32 SlotDistanceCM = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness",
		meta = (Units = "cm"))
	int32 M9AblationMissCM = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	FABTSM3M9AblationEvidence M9AblationEvidence;

	/** Exact E5 practice-satellite forbidden volume; default elsewhere. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	FABTSM3WitnessForbiddenSphere M9SatelliteForbiddenSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int32 SearchEvaluationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int32 M9QueryCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int32 M9NonZeroAccelerationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness",
		meta = (Units = "cm/s^2"))
	float PeakM9AccelerationCMPerSecSq = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int64 WitnessHash = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3PriorTierInfeasibilityCertificate
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	EABTSM3PriorTierCertificateState State =
		EABTSM3PriorTierCertificateState::NotRequired;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	EABTSSlingshotTier Tier = EABTSSlingshotTier::Simple;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int32 PlannedInputCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int32 CompletedInputCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness",
		meta = (Units = "cm"))
	int32 ClosestMissCM = 0;

	/** Exact eligible-bird domain used to sign this full-domain certificate. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int64 EligibleBirdCatalogHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int64 InputDomainHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int64 ResolvedGeometryHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int64 ProfileDescriptorHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int64 AttackFaceHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int64 SolverHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int64 GravitySnapshotHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int64 CertificateHash = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlyEncounterGameplay
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int32 EncounterId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int32 EncounterOrder = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	FName ResolvedProfileId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int64 ProfileDescriptorHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	FName AttackFaceId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	FABTSM3BallisticWitness PositiveWitness;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	FABTSM3PriorTierInfeasibilityCertificate PriorTierCertificate;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int32 TotalTrajectoryEvaluations = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int64 EncounterGameplayHash = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3WitnessFlowStep
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	int32 StepIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	EABTSM3FlowStepKind Kind = EABTSM3FlowStepKind::Recipe;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	FName StepId = NAME_None;

	/** Stable R-3 identity for EncounterReward; INDEX_NONE otherwise. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	int32 EncounterId = INDEX_NONE;

	/** E1..E6 order for EncounterReward; INDEX_NONE otherwise. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	int32 EncounterOrder = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	TArray<EABTSM3ProgressKey> RequiredKeys;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	TArray<EABTSM3ProgressKey> GrantedKeys;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	EABTSCraftingStationType RequiredStation =
		EABTSCraftingStationType::None;

	/** Sorted signed inventory delta applied by this step. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	TArray<FABTSM3WitnessItemAmount> ItemDeltas;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	int64 LedgerHashAfterStep = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlyFlowClosure
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	bool bWorkbenchStationAvailable = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	bool bFurnaceStationAvailable = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	TArray<FABTSM3WitnessFlowStep> Steps;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	TArray<FABTSM3WitnessItemAmount> FinalItems;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	TArray<EABTSM3ProgressKey> FinalKeys;

	/** Exact candidate-addressed bridge proof consumed by this closure. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	FABTSM3BridgeGateEvidence BridgeEvidence;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	bool bBridgeBlockedBefore = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	bool bBridgeReachableAfter = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	bool bBridgeNoBypass = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	int32 BranchCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	EABTSM3BranchUtilityState BranchUtility =
		EABTSM3BranchUtilityState::NotRequired;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	bool bFlowValid = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness|Flow")
	int64 FlowHash = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlyGameplayCandidate
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int32 SourceRouteCandidateId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int64 SourceSpatialCandidateHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int64 SourceSlingshotFieldCandidateHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	TArray<FABTSM3MonthlyEncounterGameplay> Encounters;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	FABTSM3MonthlyFlowClosure FlowClosure;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int32 GameplayScore = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int32 SpatialScore = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int32 RouteScore = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	bool bHardPass = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	EABTSM3MonthlyWitnessRejectReason RejectReason =
		EABTSM3MonthlyWitnessRejectReason::NotEvaluated;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int64 CandidateHash = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlyWitnessResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int32 SchemaVersion = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int32 GeneratorVersion = 3;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int32 LayoutPolicyVersion = 2;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int32 WorldSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int64 SourceSpatialResultHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int64 SourceSlingshotFieldResultHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int64 ConfigHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	FABTSM3WitnessServiceIdentity ServiceIdentity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	EABTSM3WitnessAuthority Authority = EABTSM3WitnessAuthority::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	bool bGameplayFinalizeValid = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	bool bExternalInputsCertified = false;

	/** R4 never publishes the complete monthly world; R6/Integration owns promotion. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	bool bMonthlyWorldAccepted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	EABTSM3MonthlyWitnessRejectReason RejectReason =
		EABTSM3MonthlyWitnessRejectReason::NotEvaluated;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int32 AttemptedCandidateCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int32 HardPassCandidateCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int32 SelectedCandidateId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	TArray<FABTSM3MonthlyGameplayCandidate> RetainedCandidates;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int64 GameplayLayoutHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	int64 ResultHash = 0;
};

class ABTSRUNTIME_API FABTSM3MonthlyWitnessBuilder
{
public:
	static constexpr int32 SchemaVersion = 1;
	static constexpr int32 GeneratorVersion = 3;
	static constexpr int32 MonthlyLayoutPolicyVersion = 2;
	static constexpr int32 RequiredEncounterCount = 6;
	static constexpr int32 MaximumEvaluationBudget = 8192;

	static bool Build(
		int32 WorldSeed,
		const FABTSM3MonthlyWitnessConfig& Config,
		const FABTSM3MonthlySpatialResult& SpatialResult,
		const FABTSM3MonthlySlingshotFieldResult& FieldResult,
		const IABTSM3MonthlyWitnessServices* Services,
		FABTSM3MonthlyWitnessResult& OutResult,
		FString& OutFailure);

	static bool Validate(
		const FABTSM3MonthlyWitnessConfig& Config,
		const FABTSM3MonthlySpatialResult& SpatialResult,
		const FABTSM3MonthlySlingshotFieldResult& FieldResult,
		const FABTSM3MonthlyWitnessResult& Result,
		EABTSM3MonthlyWitnessRejectReason& OutReason,
		FString& OutFailure);

	static uint64 ComputeConfigHash(
		const FABTSM3MonthlyWitnessConfig& Config);

	static uint64 ComputeAttackFaceHash(
		const FABTSM3WitnessAttackFace& Face);

	static uint64 ComputeProfileDescriptorHash(
		const FABTSM3WitnessProfileDescriptor& Descriptor);

	static uint64 ComputeProfileCatalogHash(
		const FABTSM3WitnessProfileCatalog& Catalog);

	static uint64 ComputeResolvedGeometryHash(
		const FABTSM3ResolvedWitnessGeometry& Geometry);

	static uint64 ComputeBridgeEvidenceHash(
		const FABTSM3BridgeGateEvidence& Evidence);

	static uint64 ComputeProgressionCatalogHash(
		const FABTSM3WitnessProgressionSnapshot& Snapshot);

	/** Exact bird-domain identity required by the v1 witness planner. */
	static uint64 ComputeV1BirdCatalogHash();

	static uint64 ComputeFlowClosureHash(
		const FABTSM3MonthlyFlowClosure& Flow);

	static uint64 ComputeCandidateHash(
		const FABTSM3MonthlyGameplayCandidate& Candidate);

	static uint64 ComputeGameplayLayoutHash(
		const FABTSM3MonthlyWitnessResult& Result);

	static uint64 ComputeResultHash(
		const FABTSM3MonthlyWitnessResult& Result);

	static void LogSummary(
		const FABTSM3MonthlyWitnessResult& Result);

	static const TCHAR* GetRejectReasonName(
		EABTSM3MonthlyWitnessRejectReason Reason);
};
