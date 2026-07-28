// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/ABTSM11GravityAssistTypes.h"
#include "ABTSM11FinaleActors.generated.h"

class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Visual-only representation of one M11 assist body.
 *
 * The analytic body spec remains authoritative. This Actor never contributes
 * gravity, collision or a position back to the solver.
 */
UCLASS(NotBlueprintable)
class ABTSRUNTIME_API AABTSM11GravityBodyActor final : public AActor
{
	GENERATED_BODY()

public:
	AABTSM11GravityBodyActor();

	/**
	 * Projects a finale-local analytic body into the World.
	 *
	 * InMeshReferenceRadiusCM is the authored radius of the optional mesh at
	 * unit scale. It is used only to scale the presentation to
	 * BodySpec.VisualRadiusCM; mesh bounds never alter the analytic spec.
	 */
	bool ConfigurePresentation(
		const FABTSM11GravityBodySpec& BodySpec,
		const FABTSM110FinaleLocalFrame& FinaleFrame,
		UStaticMesh* InMesh = nullptr,
		double InMeshReferenceRadiusCM = 50.0);

	bool IsPresentationConfigured() const { return bPresentationConfigured; }
	int32 GetStableBodyId() const { return StableBodyId; }
	EABTSM110FinaleGravityRole GetGravityRole() const { return GravityRole; }
	USceneComponent* GetPresentationRoot() const { return SceneRoot; }
	UStaticMeshComponent* GetVisualMeshComponent() const { return VisualMesh; }

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M11-B|Presentation",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M11-B|Presentation",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> VisualMesh;

	int32 StableBodyId = INDEX_NONE;
	EABTSM110FinaleGravityRole GravityRole = EABTSM110FinaleGravityRole::Primary;
	bool bPresentationConfigured = false;
};

/**
 * Visual-only representation of M11's analytic UFO target.
 *
 * The mesh never supplies the authoritative hit geometry. M11-A's target
 * sphere remains the sole hit contract.
 */
UCLASS(NotBlueprintable)
class ABTSRUNTIME_API AABTSM11UFOActor final : public AActor
{
	GENERATED_BODY()

public:
	AABTSM11UFOActor();

	/**
	 * Projects a finale-local analytic target into the World.
	 *
	 * InVisualRadiusCM may override the presentation radius; zero uses the
	 * analytic hit radius as a conservative fallback. InMeshReferenceRadiusCM
	 * is an explicit art scale only, never inferred from mesh bounds.
	 */
	bool ConfigurePresentation(
		const FABTSM11TargetSpec& TargetSpec,
		const FABTSM110FinaleLocalFrame& FinaleFrame,
		UStaticMesh* InMesh = nullptr,
		double InMeshReferenceRadiusCM = 50.0,
		double InVisualRadiusCM = 0.0);

	bool IsPresentationConfigured() const { return bPresentationConfigured; }
	int32 GetStableTargetId() const { return StableTargetId; }
	USceneComponent* GetPresentationRoot() const { return SceneRoot; }
	UStaticMeshComponent* GetVisualMeshComponent() const { return VisualMesh; }

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M11-B|Presentation",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M11-B|Presentation",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> VisualMesh;

	int32 StableTargetId = INDEX_NONE;
	bool bPresentationConfigured = false;
};
