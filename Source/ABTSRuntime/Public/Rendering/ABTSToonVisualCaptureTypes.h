// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rendering/ABTSStylizedRenderingControl.h"
#include "Rendering/ABTSStylizedRenderProfile.h"

/** A semantic world anchor; no entry stores an authored absolute world transform. */
enum class EABTSToonVisualCaptureAnchor : uint8
{
	GroundStart = 0,
	SlingshotBuilding,
	SatelliteE5,
	FinaleLayout,
	EnvironmentGroundDay,
	EnvironmentGroundDawn,
	EnvironmentTerminatorSky,
	EnvironmentBrightSkyBanding,
	EnvironmentTerminatorSunwardSky,
	EnvironmentTerminatorAntiSunwardSky,
	EnvironmentGroundNight,
	EnvironmentBacklitBirdParty,
	EnvironmentHighAltitude,
	CloudR0Ground,
	CloudR0Side,
	CloudR0Above,
	CloudR0FlyThrough,
	/** Orthogonal side view; append-only diagnostic for azimuthal cloud shape. */
	CloudR0SideOrthogonal,
	/** Ground-height camera looking diagonally into the cloud underside/side. */
	CloudR0GroundObliqueUp,
	/** Ground-height camera directly below the island looking radially upward. */
	CloudR0GroundZenith,
	/** A2.2 orbital composition proving deterministic global coverage. */
	CloudFieldGlobal,
	/** A2.2 neighbouring clouds visually fuse without an internal outline. */
	CloudFieldFusion,
	/** A2.2 composition proving cloud size and silhouette variety. */
	CloudFieldVariety,
	/** A2.2 deep-night cloud response with daytime whitening fully gated. */
	CloudFieldNight,
	/** A2.2 connected approximately 30-degree cluster spanning the terminator. */
	CloudFieldTerminatorMega,
	/** A2.3 bird/party body lies inside a cloud while the camera stays outside. */
	CloudTraversalBirdInside,
	/** A2.3 camera lies inside a cloud while the bird stays outside. */
	CloudTraversalCameraInside,
	/** A2.3 one cloud lies between an outside camera and outside bird. */
	CloudTraversalBetween,
	/** A2.3 camera and bird share the bounded interior of one cloud. */
	CloudTraversalBothInside
};

enum class EABTSToonVisualCaptureSuite : uint8
{
	ToonT0 = 0,
	ToonT4A0,
	ToonT4A1,
	ToonT4A2
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
	EABTSToonVisualCaptureSuite Suite =
		EABTSToonVisualCaptureSuite::ToonT0;
	EABTSToonVisualCaptureMode Mode =
		EABTSToonVisualCaptureMode::Screenshots;
	int32 ExpectedWorldSeed = 312503;
	int32 ExpectedResolutionX = 1920;
	int32 ExpectedResolutionY = 1080;
	int32 WarmupFrames = 8;
	int32 GPUProfileSamplesPerVariant = 3;
	/** Zero preserves legacy suites; A2.4 formal runs always provide 50/75/100. */
	int32 ExpectedScreenPercentage = 0;
	double TimeoutSeconds = 180.0;
	bool bRequireExactResolution = true;
	bool bPauseWorldDuringCapture = true;
	bool bExitWhenComplete = false;
	/** A2.4-only same-pose GPU baseline. Native clouds remain suppressed. */
	bool bDisableLowPolyCloudsForPerformanceBaseline = false;
	/** Optional fail-closed subsets; empty means the complete catalogue. */
	TArray<FName> RequestedPointIds;
	TArray<FName> RequestedVariantIds;
	FString OutputDirectory;
	FString BuildIdentity;

	/** Parses an arbitrary command line so the contract is covered by NullRHI tests. */
	static bool Parse(
		const TCHAR* CommandLine,
		FABTSToonVisualCaptureRunConfig& OutConfig,
		FString* OutFailure = nullptr);

	bool IsValid(FString* OutFailure = nullptr) const;
};

/** One deterministic T4 layer-isolation state. */
struct ABTSRUNTIME_API FABTSToonDiagnosticVariantDefinition
{
	FName VariantId = NAME_None;
	bool bStyleEnabled = false;
	EABTSStylizedDiagnosticPassMask PassMask =
		EABTSStylizedDiagnosticPassMask::ToneAndOutline;
	bool bShadowsEnabled = true;

	bool IsValid() const;
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
	uint64 EnvironmentSnapshotHash = 0;
	bool bRelocateBirdPartyForDiagnostic = false;
	FVector DiagnosticBirdPartyCenterWorld = FVector::ZeroVector;
	FVector DiagnosticBirdPartyUp = FVector::UpVector;

	bool IsValid() const;
};

/** Pure-data helpers used by runtime capture and fast automation. */
class ABTSRUNTIME_API FABTSToonVisualCaptureMath
{
public:
	static TArray<FABTSToonVisualCapturePointDefinition>
		BuildDefaultCatalogue();
	static TArray<FABTSToonVisualCapturePointDefinition>
		BuildT4A0Catalogue();
	static TArray<FABTSToonVisualCapturePointDefinition>
		BuildT4A1Catalogue();
	static TArray<FABTSToonVisualCapturePointDefinition>
		BuildT4A2Catalogue();
	static TArray<FABTSToonDiagnosticVariantDefinition>
		BuildVariantCatalogue(EABTSToonVisualCaptureSuite Suite);

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
	static uint64 ComputeVariantCatalogueHash(
		TConstArrayView<FABTSToonDiagnosticVariantDefinition> Definitions);

	static uint64 ComputeCameraPoseHash(
		const FTransform& CameraWorldTransform,
		const FVector& LookAtWorld,
		float FieldOfViewDegrees);

	static const TCHAR* LexToString(EABTSStylizedRenderProfile Profile);
	static const TCHAR* LexToString(EABTSToonVisualCaptureSuite Suite);
	static const TCHAR* LexToString(EABTSToonVisualCaptureAnchor Anchor);
	static const TCHAR* LexToString(EABTSToonVisualCaptureMode Mode);
};
