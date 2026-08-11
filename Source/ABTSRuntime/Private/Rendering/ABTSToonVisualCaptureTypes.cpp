// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/ABTSToonVisualCaptureTypes.h"

#include "Math/RotationMatrix.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace ABTSToonCaptureTypes
{
	constexpr uint64 FnvOffset = 14695981039346656037ull;
	constexpr uint64 FnvPrime = 1099511628211ull;

	void HashBytes(uint64& InOutHash, const void* Data, int32 ByteCount)
	{
		const uint8* Bytes = static_cast<const uint8*>(Data);
		for (int32 Index = 0; Index < ByteCount; ++Index)
		{
			InOutHash ^= Bytes[Index];
			InOutHash *= FnvPrime;
		}
	}

	template <typename ValueType>
	void HashValue(uint64& InOutHash, const ValueType& Value)
	{
		HashBytes(InOutHash, &Value, sizeof(ValueType));
	}

	void HashString(uint64& InOutHash, const FString& Value)
	{
		FTCHARToUTF8 UTF8(*Value);
		HashBytes(InOutHash, UTF8.Get(), UTF8.Length());
		const uint8 Terminator = 0;
		HashValue(InOutHash, Terminator);
	}

	int64 Quantize(double Value, double Scale)
	{
		return FMath::RoundToInt64(Value * Scale);
	}

	bool Fail(FString* OutFailure, const TCHAR* Reason)
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = Reason;
		}
		return false;
	}
}

bool FABTSToonVisualCaptureRunConfig::Parse(
	const TCHAR* CommandLine,
	FABTSToonVisualCaptureRunConfig& OutConfig,
	FString* OutFailure)
{
	OutConfig = FABTSToonVisualCaptureRunConfig();
	if (CommandLine == nullptr)
	{
		return ABTSToonCaptureTypes::Fail(
			OutFailure,
			TEXT("Command line is null."));
	}

	FString Suite;
	const bool bNamedSuite =
		FParse::Value(CommandLine, TEXT("ABTSVisualCaptureSuite="), Suite);
	OutConfig.bEnabled =
		FParse::Param(CommandLine, TEXT("ABTSToonT0Capture"))
		|| FParse::Param(CommandLine, TEXT("ABTSToonT4A0Capture"))
		|| FParse::Param(CommandLine, TEXT("ABTSToonT4A1Capture"))
		|| FParse::Param(CommandLine, TEXT("ABTSToonT4A2Capture"))
		|| (bNamedSuite
			&& (Suite.Equals(TEXT("ToonT0"), ESearchCase::IgnoreCase)
				|| Suite.Equals(TEXT("ToonT4A0"), ESearchCase::IgnoreCase)
				|| Suite.Equals(TEXT("ToonT4A1"), ESearchCase::IgnoreCase)
				|| Suite.Equals(TEXT("ToonT4A2"), ESearchCase::IgnoreCase)));
	if (!OutConfig.bEnabled)
	{
		return true;
	}
	const bool bT4A2Requested =
		FParse::Param(CommandLine, TEXT("ABTSToonT4A2Capture"))
		|| (bNamedSuite
			&& Suite.Equals(TEXT("ToonT4A2"), ESearchCase::IgnoreCase));
	const bool bT4A1Requested =
		FParse::Param(CommandLine, TEXT("ABTSToonT4A1Capture"))
		|| (bNamedSuite
			&& Suite.Equals(TEXT("ToonT4A1"), ESearchCase::IgnoreCase));
	const bool bT4A0Requested =
		FParse::Param(CommandLine, TEXT("ABTSToonT4A0Capture"))
		|| (bNamedSuite
			&& Suite.Equals(TEXT("ToonT4A0"), ESearchCase::IgnoreCase));
	OutConfig.Suite = bT4A2Requested
		? EABTSToonVisualCaptureSuite::ToonT4A2
		: bT4A1Requested
			? EABTSToonVisualCaptureSuite::ToonT4A1
		: bT4A0Requested
			? EABTSToonVisualCaptureSuite::ToonT4A0
			: EABTSToonVisualCaptureSuite::ToonT0;

	FString ModeText;
	if (FParse::Value(CommandLine, TEXT("ABTSToonT0Mode="), ModeText))
	{
		if (ModeText.Equals(TEXT("Screenshots"), ESearchCase::IgnoreCase))
		{
			OutConfig.Mode = EABTSToonVisualCaptureMode::Screenshots;
		}
		else if (ModeText.Equals(TEXT("GPU"), ESearchCase::IgnoreCase)
			|| ModeText.Equals(TEXT("GPUProfile"), ESearchCase::IgnoreCase))
		{
			OutConfig.Mode = EABTSToonVisualCaptureMode::GPUProfile;
		}
		else
		{
			return ABTSToonCaptureTypes::Fail(
				OutFailure,
				TEXT("ABTSToonT0Mode must be Screenshots or GPU."));
		}
	}

	FParse::Value(
		CommandLine,
		TEXT("ABTSToonT0ExpectedSeed="),
		OutConfig.ExpectedWorldSeed);
	FParse::Value(
		CommandLine,
		TEXT("ABTSToonT0ResX="),
		OutConfig.ExpectedResolutionX);
	FParse::Value(
		CommandLine,
		TEXT("ABTSToonT0ResY="),
		OutConfig.ExpectedResolutionY);
	FParse::Value(
		CommandLine,
		TEXT("ABTSToonT0WarmupFrames="),
		OutConfig.WarmupFrames);
	FParse::Value(
		CommandLine,
		TEXT("ABTSToonT0GPUSamples="),
		OutConfig.GPUProfileSamplesPerVariant);
	FParse::Value(
		CommandLine,
		TEXT("ABTSToonT0TimeoutSeconds="),
		OutConfig.TimeoutSeconds);
	FParse::Value(
		CommandLine,
		TEXT("ABTSToonT0Output="),
		OutConfig.OutputDirectory);
	FParse::Value(
		CommandLine,
		TEXT("ABTSToonT0BuildId="),
		OutConfig.BuildIdentity);

	OutConfig.bRequireExactResolution =
		!FParse::Param(CommandLine, TEXT("ABTSToonT0AllowAnyResolution"));
	OutConfig.bPauseWorldDuringCapture =
		!FParse::Param(CommandLine, TEXT("ABTSToonT0KeepWorldRunning"));
	OutConfig.bExitWhenComplete =
		FParse::Param(CommandLine, TEXT("ABTSToonT0ExitWhenDone"));

	return OutConfig.IsValid(OutFailure);
}

bool FABTSToonVisualCaptureRunConfig::IsValid(FString* OutFailure) const
{
	if (!bEnabled)
	{
		return true;
	}
	if (ExpectedWorldSeed <= 0)
	{
		return ABTSToonCaptureTypes::Fail(
			OutFailure,
			TEXT("ExpectedWorldSeed must be positive."));
	}
	if (ExpectedResolutionX < 320 || ExpectedResolutionY < 180)
	{
		return ABTSToonCaptureTypes::Fail(
			OutFailure,
			TEXT("Capture resolution is below 320x180."));
	}
	if (WarmupFrames < 0 || WarmupFrames > 600)
	{
		return ABTSToonCaptureTypes::Fail(
			OutFailure,
			TEXT("WarmupFrames must be in [0, 600]."));
	}
	if (GPUProfileSamplesPerVariant < 1
		|| GPUProfileSamplesPerVariant > 10)
	{
		return ABTSToonCaptureTypes::Fail(
			OutFailure,
			TEXT("GPUProfileSamplesPerVariant must be in [1, 10]."));
	}
	if (!FMath::IsFinite(TimeoutSeconds)
		|| TimeoutSeconds < 5.0
		|| TimeoutSeconds > 1800.0)
	{
		return ABTSToonCaptureTypes::Fail(
			OutFailure,
			TEXT("TimeoutSeconds must be finite and in [5, 1800]."));
	}
	if (BuildIdentity.TrimStartAndEnd().IsEmpty())
	{
		return ABTSToonCaptureTypes::Fail(
			OutFailure,
			TEXT("ABTSToonT0BuildId is required for source/binary evidence identity."));
	}
	if (Suite == EABTSToonVisualCaptureSuite::ToonT4A0
		&& Mode != EABTSToonVisualCaptureMode::Screenshots)
	{
		return ABTSToonCaptureTypes::Fail(
			OutFailure,
			TEXT("ToonT4A0 is a screenshot isolation suite; GPU profiling begins in T4-A1."));
	}
	return true;
}

bool FABTSToonDiagnosticVariantDefinition::IsValid() const
{
	const int32 Mask = static_cast<int32>(PassMask);
	return !VariantId.IsNone()
		&& Mask >= static_cast<int32>(EABTSStylizedDiagnosticPassMask::None)
		&& Mask <= static_cast<int32>(
			EABTSStylizedDiagnosticPassMask::ToneAndOutline)
		&& (bStyleEnabled
			|| PassMask == EABTSStylizedDiagnosticPassMask::None);
}

bool FABTSToonVisualCapturePointDefinition::IsValid() const
{
	return !PointId.IsNone()
		&& FMath::IsFinite(FieldOfViewDegrees)
		&& FieldOfViewDegrees >= 10.0f
		&& FieldOfViewDegrees <= 150.0f
		&& WarmupFrameOverride >= INDEX_NONE;
}

bool FABTSToonResolvedCapturePoint::IsValid() const
{
	return Definition.IsValid()
		&& !CameraWorldTransform.ContainsNaN()
		&& !LookAtWorld.ContainsNaN()
		&& SemanticIdentityHash != 0
		&& CameraPoseHash != 0
		&& EnvironmentSnapshotHash != 0;
}

TArray<FABTSToonVisualCapturePointDefinition>
FABTSToonVisualCaptureMath::BuildDefaultCatalogue()
{
	TArray<FABTSToonVisualCapturePointDefinition> Result;
	Result.Reserve(4);

	FABTSToonVisualCapturePointDefinition Ground;
	Ground.PointId = TEXT("GroundStart");
	Ground.Anchor = EABTSToonVisualCaptureAnchor::GroundStart;
	Ground.StyleProfile = EABTSStylizedRenderProfile::GroundDay;
	Ground.FieldOfViewDegrees = 72.0f;
	Result.Add(Ground);

	FABTSToonVisualCapturePointDefinition Building;
	Building.PointId = TEXT("SlingshotBuilding");
	Building.Anchor = EABTSToonVisualCaptureAnchor::SlingshotBuilding;
	Building.StyleProfile = EABTSStylizedRenderProfile::GroundDay;
	Building.FieldOfViewDegrees = 60.0f;
	Result.Add(Building);

	FABTSToonVisualCapturePointDefinition Satellite;
	Satellite.PointId = TEXT("SatelliteE5");
	Satellite.Anchor = EABTSToonVisualCaptureAnchor::SatelliteE5;
	Satellite.StyleProfile = EABTSStylizedRenderProfile::SatelliteGuide;
	Satellite.FieldOfViewDegrees = 52.0f;
	Result.Add(Satellite);

	FABTSToonVisualCapturePointDefinition Finale;
	Finale.PointId = TEXT("FinaleLayout");
	Finale.Anchor = EABTSToonVisualCaptureAnchor::FinaleLayout;
	Finale.StyleProfile = EABTSStylizedRenderProfile::FinaleSpace;
	Finale.FieldOfViewDegrees = 50.0f;
	Result.Add(Finale);

	return Result;
}

TArray<FABTSToonVisualCapturePointDefinition>
FABTSToonVisualCaptureMath::BuildT4A0Catalogue()
{
	TArray<FABTSToonVisualCapturePointDefinition> Result;
	Result.Reserve(5);

	auto Add = [&Result](
		const TCHAR* Id,
		const EABTSToonVisualCaptureAnchor Anchor,
		const EABTSStylizedRenderProfile Profile,
		const float Fov)
	{
		FABTSToonVisualCapturePointDefinition Point;
		Point.PointId = Id;
		Point.Anchor = Anchor;
		Point.StyleProfile = Profile;
		Point.FieldOfViewDegrees = Fov;
		Result.Add(Point);
	};

	Add(TEXT("GroundDay"),
		EABTSToonVisualCaptureAnchor::EnvironmentGroundDay,
		EABTSStylizedRenderProfile::GroundDay,
		68.0f);
	Add(TEXT("GroundDawn"),
		EABTSToonVisualCaptureAnchor::EnvironmentGroundDawn,
		EABTSStylizedRenderProfile::GroundDay,
		68.0f);
	Add(TEXT("GroundNight"),
		EABTSToonVisualCaptureAnchor::EnvironmentGroundNight,
		EABTSStylizedRenderProfile::GroundDay,
		68.0f);
	Add(TEXT("HighAltitude"),
		EABTSToonVisualCaptureAnchor::EnvironmentHighAltitude,
		EABTSStylizedRenderProfile::SatelliteGuide,
		54.0f);
	Add(TEXT("FinaleSpace"),
		EABTSToonVisualCaptureAnchor::FinaleLayout,
		EABTSStylizedRenderProfile::FinaleSpace,
		50.0f);
	return Result;
}

TArray<FABTSToonVisualCapturePointDefinition>
FABTSToonVisualCaptureMath::BuildT4A1Catalogue()
{
	TArray<FABTSToonVisualCapturePointDefinition> Result =
		BuildT4A0Catalogue();
	FABTSToonVisualCapturePointDefinition TerminatorSky;
	TerminatorSky.PointId = TEXT("TerminatorSky");
	TerminatorSky.Anchor =
		EABTSToonVisualCaptureAnchor::EnvironmentTerminatorSky;
	TerminatorSky.StyleProfile = EABTSStylizedRenderProfile::GroundDay;
	TerminatorSky.FieldOfViewDegrees = 52.0f;
	Result.Insert(TerminatorSky, 2);

	FABTSToonVisualCapturePointDefinition BrightSkyBanding;
	BrightSkyBanding.PointId = TEXT("BrightSkyBanding");
	BrightSkyBanding.Anchor =
		EABTSToonVisualCaptureAnchor::EnvironmentBrightSkyBanding;
	BrightSkyBanding.StyleProfile = EABTSStylizedRenderProfile::GroundDay;
	BrightSkyBanding.FieldOfViewDegrees = 52.0f;
	Result.Insert(BrightSkyBanding, 3);

	FABTSToonVisualCapturePointDefinition TerminatorSunwardSky;
	TerminatorSunwardSky.PointId = TEXT("TerminatorSunwardSky");
	TerminatorSunwardSky.Anchor =
		EABTSToonVisualCaptureAnchor::EnvironmentTerminatorSunwardSky;
	TerminatorSunwardSky.StyleProfile = EABTSStylizedRenderProfile::GroundDay;
	TerminatorSunwardSky.FieldOfViewDegrees = 52.0f;
	Result.Insert(TerminatorSunwardSky, 4);

	FABTSToonVisualCapturePointDefinition TerminatorAntiSunwardSky;
	TerminatorAntiSunwardSky.PointId = TEXT("TerminatorAntiSunwardSky");
	TerminatorAntiSunwardSky.Anchor =
		EABTSToonVisualCaptureAnchor::EnvironmentTerminatorAntiSunwardSky;
	TerminatorAntiSunwardSky.StyleProfile = EABTSStylizedRenderProfile::GroundDay;
	TerminatorAntiSunwardSky.FieldOfViewDegrees = 52.0f;
	Result.Insert(TerminatorAntiSunwardSky, 5);

	FABTSToonVisualCapturePointDefinition BacklitParty;
	BacklitParty.PointId = TEXT("BacklitBirdParty");
	BacklitParty.Anchor =
		EABTSToonVisualCaptureAnchor::EnvironmentBacklitBirdParty;
	BacklitParty.StyleProfile = EABTSStylizedRenderProfile::GroundDay;
	BacklitParty.FieldOfViewDegrees = 56.0f;
	Result.Insert(BacklitParty, 7);
	return Result;
}

TArray<FABTSToonVisualCapturePointDefinition>
FABTSToonVisualCaptureMath::BuildT4A2Catalogue()
{
	// Preserve all accepted A1 atmosphere poses, then add the seven accepted
	// A2.1 cloud views and five A2.2 global/night/terminator compositions.
	// The orthogonal side pair prevents a long-axis-only cloud from
	// passing the visual gate; the two ground-up views make the gameplay-facing
	// underside and lighting continuity first-class evidence.
	TArray<FABTSToonVisualCapturePointDefinition> Result =
		BuildT4A1Catalogue();
	auto AddCloudPoint = [&Result](
		const TCHAR* Id,
		const EABTSToonVisualCaptureAnchor Anchor,
		const float Fov)
	{
		FABTSToonVisualCapturePointDefinition Point;
		Point.PointId = Id;
		Point.Anchor = Anchor;
		Point.StyleProfile = EABTSStylizedRenderProfile::GroundDay;
		Point.FieldOfViewDegrees = Fov;
		Point.WarmupFrameOverride = 12;
		Result.Add(Point);
	};
	AddCloudPoint(TEXT("CloudR0Ground"),
		EABTSToonVisualCaptureAnchor::CloudR0Ground, 62.0f);
	AddCloudPoint(TEXT("CloudR0Side"),
		EABTSToonVisualCaptureAnchor::CloudR0Side, 58.0f);
	AddCloudPoint(TEXT("CloudR0SideOrthogonal"),
		EABTSToonVisualCaptureAnchor::CloudR0SideOrthogonal, 58.0f);
	AddCloudPoint(TEXT("CloudR0Above"),
		EABTSToonVisualCaptureAnchor::CloudR0Above, 58.0f);
	AddCloudPoint(TEXT("CloudR0FlyThrough"),
		EABTSToonVisualCaptureAnchor::CloudR0FlyThrough, 68.0f);
	AddCloudPoint(TEXT("CloudR0GroundObliqueUp"),
		EABTSToonVisualCaptureAnchor::CloudR0GroundObliqueUp, 68.0f);
	AddCloudPoint(TEXT("CloudR0GroundZenith"),
		EABTSToonVisualCaptureAnchor::CloudR0GroundZenith, 76.0f);
	AddCloudPoint(TEXT("CloudFieldGlobal"),
		EABTSToonVisualCaptureAnchor::CloudFieldGlobal, 52.0f);
	AddCloudPoint(TEXT("CloudFieldFusion"),
		EABTSToonVisualCaptureAnchor::CloudFieldFusion, 44.0f);
	AddCloudPoint(TEXT("CloudFieldVariety"),
		EABTSToonVisualCaptureAnchor::CloudFieldVariety, 50.0f);
	AddCloudPoint(TEXT("CloudFieldNight"),
		EABTSToonVisualCaptureAnchor::CloudFieldNight, 48.0f);
	AddCloudPoint(TEXT("CloudFieldTerminatorMega"),
		EABTSToonVisualCaptureAnchor::CloudFieldTerminatorMega, 58.0f);
	return Result;
}

TArray<FABTSToonDiagnosticVariantDefinition>
FABTSToonVisualCaptureMath::BuildVariantCatalogue(
	const EABTSToonVisualCaptureSuite Suite)
{
	TArray<FABTSToonDiagnosticVariantDefinition> Result;
	auto Add = [&Result](
		const TCHAR* Id,
		const bool bStyleEnabled,
		const EABTSStylizedDiagnosticPassMask Mask,
		const bool bShadowsEnabled)
	{
		FABTSToonDiagnosticVariantDefinition Variant;
		Variant.VariantId = Id;
		Variant.bStyleEnabled = bStyleEnabled;
		Variant.PassMask = Mask;
		Variant.bShadowsEnabled = bShadowsEnabled;
		Result.Add(Variant);
	};

	if (Suite == EABTSToonVisualCaptureSuite::ToonT0)
	{
		Result.Reserve(2);
		Add(TEXT("StyleOff"), false,
			EABTSStylizedDiagnosticPassMask::None, true);
		Add(TEXT("StyleOn"), true,
			EABTSStylizedDiagnosticPassMask::ToneAndOutline, true);
		return Result;
	}
	if (Suite == EABTSToonVisualCaptureSuite::ToonT4A1
		|| Suite == EABTSToonVisualCaptureSuite::ToonT4A2)
	{
		Result.Reserve(2);
		Add(TEXT("StyleOff"), false,
			EABTSStylizedDiagnosticPassMask::None, true);
		Add(TEXT("StyleOn"), true,
			EABTSStylizedDiagnosticPassMask::ToneAndOutline, true);
		return Result;
	}

	Result.Reserve(6);
	Add(TEXT("StyleOff"), false,
		EABTSStylizedDiagnosticPassMask::None, true);
	Add(TEXT("ToneOnly"), true,
		EABTSStylizedDiagnosticPassMask::Tone, true);
	Add(TEXT("OutlineOnly"), true,
		EABTSStylizedDiagnosticPassMask::Outline, true);
	Add(TEXT("ToneOutline"), true,
		EABTSStylizedDiagnosticPassMask::ToneAndOutline, true);
	Add(TEXT("ShadowOff"), true,
		EABTSStylizedDiagnosticPassMask::None, false);
	Add(TEXT("LightingOnly"), true,
		EABTSStylizedDiagnosticPassMask::None, true);
	return Result;
}

bool FABTSToonVisualCaptureMath::BuildLookAtCameraTransform(
	const FVector& CameraWorldLocation,
	const FVector& LookAtWorldLocation,
	const FVector& PreferredWorldUp,
	FTransform& OutCameraWorldTransform,
	FString* OutFailure)
{
	OutCameraWorldTransform = FTransform::Identity;
	if (CameraWorldLocation.ContainsNaN()
		|| LookAtWorldLocation.ContainsNaN()
		|| PreferredWorldUp.ContainsNaN())
	{
		return ABTSToonCaptureTypes::Fail(
			OutFailure,
			TEXT("Look-at input contains NaN."));
	}

	const FVector Forward =
		(LookAtWorldLocation - CameraWorldLocation).GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		return ABTSToonCaptureTypes::Fail(
			OutFailure,
			TEXT("Camera and look-at locations are coincident."));
	}

	FVector Up = FVector::VectorPlaneProject(
		PreferredWorldUp.GetSafeNormal(),
		Forward).GetSafeNormal();
	if (Up.IsNearlyZero())
	{
		FVector FallbackRight;
		Forward.FindBestAxisVectors(Up, FallbackRight);
		Up = FVector::VectorPlaneProject(Up, Forward).GetSafeNormal();
	}
	if (Up.IsNearlyZero())
	{
		return ABTSToonCaptureTypes::Fail(
			OutFailure,
			TEXT("Unable to construct a stable camera up vector."));
	}

	const FQuat Rotation = FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat();
	if (Rotation.ContainsNaN())
	{
		return ABTSToonCaptureTypes::Fail(
			OutFailure,
			TEXT("Look-at rotation contains NaN."));
	}

	OutCameraWorldTransform = FTransform(Rotation, CameraWorldLocation);
	return true;
}

double FABTSToonVisualCaptureMath::ComputePerspectiveFitDistanceCM(
	double BoundingRadiusCM,
	double HorizontalFieldOfViewDegrees,
	double AspectRatio,
	double MarginScale)
{
	if (!FMath::IsFinite(BoundingRadiusCM)
		|| !FMath::IsFinite(HorizontalFieldOfViewDegrees)
		|| !FMath::IsFinite(AspectRatio)
		|| !FMath::IsFinite(MarginScale)
		|| BoundingRadiusCM <= 0.0
		|| HorizontalFieldOfViewDegrees <= 1.0
		|| HorizontalFieldOfViewDegrees >= 179.0
		|| AspectRatio <= UE_SMALL_NUMBER
		|| MarginScale < 1.0)
	{
		return 0.0;
	}

	const double HorizontalHalfRadians =
		FMath::DegreesToRadians(HorizontalFieldOfViewDegrees * 0.5);
	const double VerticalHalfRadians =
		FMath::Atan(FMath::Tan(HorizontalHalfRadians) / AspectRatio);
	const double LimitingHalfRadians =
		FMath::Min(HorizontalHalfRadians, VerticalHalfRadians);
	return BoundingRadiusCM * MarginScale
		/ FMath::Max(FMath::Sin(LimitingHalfRadians), UE_SMALL_NUMBER);
}

uint64 FABTSToonVisualCaptureMath::ComputeCatalogueHash(
	TConstArrayView<FABTSToonVisualCapturePointDefinition> Definitions)
{
	uint64 Hash = ABTSToonCaptureTypes::FnvOffset;
	const int32 Count = Definitions.Num();
	ABTSToonCaptureTypes::HashValue(Hash, Count);
	for (const FABTSToonVisualCapturePointDefinition& Definition : Definitions)
	{
		ABTSToonCaptureTypes::HashString(Hash, Definition.PointId.ToString());
		const uint8 Anchor = static_cast<uint8>(Definition.Anchor);
		const uint8 Profile = static_cast<uint8>(Definition.StyleProfile);
		const int32 Fov = ABTSToonCaptureTypes::Quantize(
			Definition.FieldOfViewDegrees,
			1000.0);
		ABTSToonCaptureTypes::HashValue(Hash, Anchor);
		ABTSToonCaptureTypes::HashValue(Hash, Profile);
		ABTSToonCaptureTypes::HashValue(Hash, Fov);
		ABTSToonCaptureTypes::HashValue(
			Hash,
			Definition.WarmupFrameOverride);
	}
	return Hash;
}

uint64 FABTSToonVisualCaptureMath::ComputeVariantCatalogueHash(
	TConstArrayView<FABTSToonDiagnosticVariantDefinition> Definitions)
{
	uint64 Hash = ABTSToonCaptureTypes::FnvOffset;
	const int32 Count = Definitions.Num();
	ABTSToonCaptureTypes::HashValue(Hash, Count);
	for (const FABTSToonDiagnosticVariantDefinition& Definition : Definitions)
	{
		ABTSToonCaptureTypes::HashString(Hash, Definition.VariantId.ToString());
		ABTSToonCaptureTypes::HashValue(Hash, Definition.bStyleEnabled);
		const uint8 Mask = static_cast<uint8>(Definition.PassMask);
		ABTSToonCaptureTypes::HashValue(Hash, Mask);
		ABTSToonCaptureTypes::HashValue(Hash, Definition.bShadowsEnabled);
	}
	return Hash;
}

uint64 FABTSToonVisualCaptureMath::ComputeCameraPoseHash(
	const FTransform& CameraWorldTransform,
	const FVector& LookAtWorld,
	float FieldOfViewDegrees)
{
	uint64 Hash = ABTSToonCaptureTypes::FnvOffset;
	const FVector Location = CameraWorldTransform.GetLocation();
	const FQuat Rotation = CameraWorldTransform.GetRotation().GetNormalized();
	const int64 Values[] = {
		ABTSToonCaptureTypes::Quantize(Location.X, 10.0),
		ABTSToonCaptureTypes::Quantize(Location.Y, 10.0),
		ABTSToonCaptureTypes::Quantize(Location.Z, 10.0),
		ABTSToonCaptureTypes::Quantize(Rotation.X, 1000000.0),
		ABTSToonCaptureTypes::Quantize(Rotation.Y, 1000000.0),
		ABTSToonCaptureTypes::Quantize(Rotation.Z, 1000000.0),
		ABTSToonCaptureTypes::Quantize(Rotation.W, 1000000.0),
		ABTSToonCaptureTypes::Quantize(LookAtWorld.X, 10.0),
		ABTSToonCaptureTypes::Quantize(LookAtWorld.Y, 10.0),
		ABTSToonCaptureTypes::Quantize(LookAtWorld.Z, 10.0),
		ABTSToonCaptureTypes::Quantize(FieldOfViewDegrees, 1000.0)
	};
	for (const int64 Value : Values)
	{
		ABTSToonCaptureTypes::HashValue(Hash, Value);
	}
	return Hash;
}

const TCHAR* FABTSToonVisualCaptureMath::LexToString(
	EABTSStylizedRenderProfile Profile)
{
	switch (Profile)
	{
	case EABTSStylizedRenderProfile::GroundDay:
		return TEXT("GroundDay");
	case EABTSStylizedRenderProfile::SatelliteGuide:
		return TEXT("SatelliteGuide");
	case EABTSStylizedRenderProfile::FinaleSpace:
		return TEXT("FinaleSpace");
	default:
		return TEXT("Unknown");
	}
}

const TCHAR* FABTSToonVisualCaptureMath::LexToString(
	EABTSToonVisualCaptureSuite Suite)
{
	switch (Suite)
	{
	case EABTSToonVisualCaptureSuite::ToonT4A2:
		return TEXT("ToonT4A2");
	case EABTSToonVisualCaptureSuite::ToonT4A1:
		return TEXT("ToonT4A1");
	case EABTSToonVisualCaptureSuite::ToonT4A0:
		return TEXT("ToonT4A0");
	case EABTSToonVisualCaptureSuite::ToonT0:
	default:
		return TEXT("ToonT0");
	}
}

const TCHAR* FABTSToonVisualCaptureMath::LexToString(
	EABTSToonVisualCaptureAnchor Anchor)
{
	switch (Anchor)
	{
	case EABTSToonVisualCaptureAnchor::GroundStart:
		return TEXT("GroundStart");
	case EABTSToonVisualCaptureAnchor::SlingshotBuilding:
		return TEXT("SlingshotBuilding");
	case EABTSToonVisualCaptureAnchor::SatelliteE5:
		return TEXT("SatelliteE5");
	case EABTSToonVisualCaptureAnchor::FinaleLayout:
		return TEXT("FinaleLayout");
	case EABTSToonVisualCaptureAnchor::EnvironmentGroundDay:
		return TEXT("EnvironmentGroundDay");
	case EABTSToonVisualCaptureAnchor::EnvironmentGroundDawn:
		return TEXT("EnvironmentGroundDawn");
	case EABTSToonVisualCaptureAnchor::EnvironmentTerminatorSky:
		return TEXT("EnvironmentTerminatorSky");
	case EABTSToonVisualCaptureAnchor::EnvironmentBrightSkyBanding:
		return TEXT("EnvironmentBrightSkyBanding");
	case EABTSToonVisualCaptureAnchor::EnvironmentTerminatorSunwardSky:
		return TEXT("EnvironmentTerminatorSunwardSky");
	case EABTSToonVisualCaptureAnchor::EnvironmentTerminatorAntiSunwardSky:
		return TEXT("EnvironmentTerminatorAntiSunwardSky");
	case EABTSToonVisualCaptureAnchor::EnvironmentGroundNight:
		return TEXT("EnvironmentGroundNight");
	case EABTSToonVisualCaptureAnchor::EnvironmentBacklitBirdParty:
		return TEXT("EnvironmentBacklitBirdParty");
	case EABTSToonVisualCaptureAnchor::EnvironmentHighAltitude:
		return TEXT("EnvironmentHighAltitude");
	case EABTSToonVisualCaptureAnchor::CloudR0Ground:
		return TEXT("CloudR0Ground");
	case EABTSToonVisualCaptureAnchor::CloudR0Side:
		return TEXT("CloudR0Side");
	case EABTSToonVisualCaptureAnchor::CloudR0Above:
		return TEXT("CloudR0Above");
	case EABTSToonVisualCaptureAnchor::CloudR0FlyThrough:
		return TEXT("CloudR0FlyThrough");
	case EABTSToonVisualCaptureAnchor::CloudR0SideOrthogonal:
		return TEXT("CloudR0SideOrthogonal");
	case EABTSToonVisualCaptureAnchor::CloudR0GroundObliqueUp:
		return TEXT("CloudR0GroundObliqueUp");
	case EABTSToonVisualCaptureAnchor::CloudR0GroundZenith:
		return TEXT("CloudR0GroundZenith");
	case EABTSToonVisualCaptureAnchor::CloudFieldGlobal:
		return TEXT("CloudFieldGlobal");
	case EABTSToonVisualCaptureAnchor::CloudFieldFusion:
		return TEXT("CloudFieldFusion");
	case EABTSToonVisualCaptureAnchor::CloudFieldVariety:
		return TEXT("CloudFieldVariety");
	case EABTSToonVisualCaptureAnchor::CloudFieldNight:
		return TEXT("CloudFieldNight");
	case EABTSToonVisualCaptureAnchor::CloudFieldTerminatorMega:
		return TEXT("CloudFieldTerminatorMega");
	default:
		return TEXT("Unknown");
	}
}

const TCHAR* FABTSToonVisualCaptureMath::LexToString(
	EABTSToonVisualCaptureMode Mode)
{
	return Mode == EABTSToonVisualCaptureMode::GPUProfile
		? TEXT("GPUProfile")
		: TEXT("Screenshots");
}
