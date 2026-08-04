// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Rendering profile identities shared by the T0 baseline and the future T1 implementation. */
enum class EABTSStylizedRenderProfile : uint8
{
	GroundDay = 0,
	SatelliteGuide,
	FinaleSpace
};

/** A semantic world anchor; no entry stores an authored absolute world transform. */
enum class EABTSToonVisualCaptureAnchor : uint8
{
	GroundStart = 0,
	SlingshotBuilding,
	SatelliteE5,
	FinaleLayout
};

enum class EABTSToonVisualCaptureMode : uint8
{
	Screenshots = 0,
	GPUProfile
};

/** Parsed, testable command-line contract for one explicit T0 run. */
struct ABTSRUNTIME_API FABTSToonVisualCaptureRunConfig
{
	bool bEnabled = false;
	EABTSToonVisualCaptureMode Mode =
		EABTSToonVisualCaptureMode::Screenshots;
	int32 ExpectedWorldSeed = 312503;
	int32 ExpectedResolutionX = 1920;
	int32 ExpectedResolutionY = 1080;
	int32 WarmupFrames = 8;
	int32 GPUProfileSamplesPerVariant = 3;
	double TimeoutSeconds = 180.0;
	bool bRequireExactResolution = true;
	bool bPauseWorldDuringCapture = true;
	bool bExitWhenComplete = false;
	FString OutputDirectory;
	FString BuildIdentity;

	/** Parses an arbitrary command line so the contract is covered by NullRHI tests. */
	static bool Parse(
		const TCHAR* CommandLine,
		FABTSToonVisualCaptureRunConfig& OutConfig,
		FString* OutFailure = nullptr);

	bool IsValid(FString* OutFailure = nullptr) const;
};

/** Stable catalogue entry. Off and On always reuse the same resolved pose. */
struct ABTSRUNTIME_API FABTSToonVisualCapturePointDefinition
{
	FName PointId = NAME_None;
	EABTSToonVisualCaptureAnchor Anchor =
		EABTSToonVisualCaptureAnchor::GroundStart;
	EABTSStylizedRenderProfile StyleProfile =
		EABTSStylizedRenderProfile::GroundDay;
	float FieldOfViewDegrees = 60.0f;
	int32 WarmupFrameOverride = INDEX_NONE;

	bool IsValid() const;
};

/** World-resolved pose and identity written into every screenshot/GPU record. */
struct ABTSRUNTIME_API FABTSToonResolvedCapturePoint
{
	FABTSToonVisualCapturePointDefinition Definition;
	FTransform CameraWorldTransform = FTransform::Identity;
	FVector LookAtWorld = FVector::ZeroVector;
	uint64 SemanticIdentityHash = 0;
	uint64 CameraPoseHash = 0;

	bool IsValid() const;
};

/** Pure-data helpers used by runtime capture and fast automation. */
class ABTSRUNTIME_API FABTSToonVisualCaptureMath
{
public:
	static TArray<FABTSToonVisualCapturePointDefinition>
		BuildDefaultCatalogue();

	static bool BuildLookAtCameraTransform(
		const FVector& CameraWorldLocation,
		const FVector& LookAtWorldLocation,
		const FVector& PreferredWorldUp,
		FTransform& OutCameraWorldTransform,
		FString* OutFailure = nullptr);

	static double ComputePerspectiveFitDistanceCM(
		double BoundingRadiusCM,
		double HorizontalFieldOfViewDegrees,
		double AspectRatio,
		double MarginScale = 1.15);

	static uint64 ComputeCatalogueHash(
		TConstArrayView<FABTSToonVisualCapturePointDefinition> Definitions);

	static uint64 ComputeCameraPoseHash(
		const FTransform& CameraWorldTransform,
		const FVector& LookAtWorld,
		float FieldOfViewDegrees);

	static const TCHAR* LexToString(EABTSStylizedRenderProfile Profile);
	static const TCHAR* LexToString(EABTSToonVisualCaptureAnchor Anchor);
	static const TCHAR* LexToString(EABTSToonVisualCaptureMode Mode);
};
