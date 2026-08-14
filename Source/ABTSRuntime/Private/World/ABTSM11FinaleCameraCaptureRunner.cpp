// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM11FinaleCameraCaptureRunner.h"

#include "ABTSRuntime.h"
#include "Camera/ABTSM11FinaleCameraDirector.h"
#include "Camera/ABTSM11FinaleFlightCamera.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "HighResScreenshot.h"
#include "Kismet/GameplayStatics.h"
#include "Inventory/ABTSInventoryTypes.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "Misc/CommandLine.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Rendering/ABTSStylizedRenderingControl.h"
#include "Rendering/ABTSStylizedRenderingTypes.h"
#include "Rendering/ABTSStylizedSceneCaptureRegistry.h"
#include "ImageUtils.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Slingshot/ABTSSlingshotVisualTypes.h"
#include "World/ABTSM11FinaleInteractionSystem.h"
#include "World/ABTSM11FinaleActors.h"
#include "World/ABTSM11CandidateExperienceCatalog.h"
#include "World/ABTSM11FinaleSystem.h"
#include "World/ABTSM51WorldActors.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace ABTSM11FinaleCameraCaptureRunnerPrivate
{
	struct FProjectedSphereObservation
	{
		FVector2D Screen = FVector2D::ZeroVector;
		double DepthCM = 0.0;
		double PixelRadius = 0.0;
		double VisibleRatio = 0.0;
	};

	bool ProjectObservationSphere(
		const FMinimalViewInfo& View,
		const FIntPoint FrameSize,
		const FVector& WorldCenter,
		const double WorldRadiusCM,
		FProjectedSphereObservation& OutObservation)
	{
		OutObservation = FProjectedSphereObservation();
		if (FrameSize.X <= 0 || FrameSize.Y <= 0
			|| !FMath::IsFinite(WorldRadiusCM) || WorldRadiusCM <= 0.0
			|| !FMath::IsFinite(View.FOV) || View.FOV <= 0.0f
			|| View.FOV >= 179.0f)
		{
			return false;
		}
		const FRotationMatrix CameraBasis(View.Rotation);
		const FVector Relative = WorldCenter - View.Location;
		const double CameraX = FVector::DotProduct(
			Relative,
			CameraBasis.GetUnitAxis(EAxis::X));
		const double CameraY = FVector::DotProduct(
			Relative,
			CameraBasis.GetUnitAxis(EAxis::Y));
		const double CameraZ = FVector::DotProduct(
			Relative,
			CameraBasis.GetUnitAxis(EAxis::Z));
		OutObservation.DepthCM = CameraX;
		if (!FMath::IsFinite(CameraX) || CameraX <= 1.0)
		{
			return true;
		}
		const double TanHalfHorizontal = FMath::Tan(
			FMath::DegreesToRadians(static_cast<double>(View.FOV)) * 0.5);
		const double Aspect = static_cast<double>(FrameSize.X)
			/ static_cast<double>(FrameSize.Y);
		const double TanHalfVertical = TanHalfHorizontal / Aspect;
		if (!FMath::IsFinite(TanHalfHorizontal)
			|| !FMath::IsFinite(TanHalfVertical)
			|| TanHalfHorizontal <= 0.0 || TanHalfVertical <= 0.0)
		{
			return false;
		}
		const double NdcX = CameraY / (CameraX * TanHalfHorizontal);
		const double NdcY = CameraZ / (CameraX * TanHalfVertical);
		OutObservation.Screen.X = (NdcX * 0.5 + 0.5) * FrameSize.X;
		OutObservation.Screen.Y = (0.5 - NdcY * 0.5) * FrameSize.Y;
		const double Distance = Relative.Size();
		const double TangentDistance = FMath::Sqrt(FMath::Max(
			1.0,
			Distance * Distance - WorldRadiusCM * WorldRadiusCM));
		const double HorizontalFocalPixels = FrameSize.X
			/ (2.0 * TanHalfHorizontal);
		OutObservation.PixelRadius = FMath::Clamp(
			HorizontalFocalPixels * WorldRadiusCM / TangentDistance,
			0.0,
			static_cast<double>(FMath::Max(FrameSize.X, FrameSize.Y)) * 4.0);
		const double Diameter = OutObservation.PixelRadius * 2.0;
		if (Diameter <= UE_DOUBLE_SMALL_NUMBER)
		{
			return true;
		}
		const double VisibleWidth = FMath::Max(
			0.0,
			FMath::Min(
				static_cast<double>(FrameSize.X),
				OutObservation.Screen.X + OutObservation.PixelRadius)
			- FMath::Max(0.0, OutObservation.Screen.X - OutObservation.PixelRadius));
		const double VisibleHeight = FMath::Max(
			0.0,
			FMath::Min(
				static_cast<double>(FrameSize.Y),
				OutObservation.Screen.Y + OutObservation.PixelRadius)
			- FMath::Max(0.0, OutObservation.Screen.Y - OutObservation.PixelRadius));
		OutObservation.VisibleRatio = FMath::Clamp(
			VisibleWidth * VisibleHeight / (Diameter * Diameter),
			0.0,
			1.0);
		return true;
	}

	FABTSM11FinaleCameraStageSelection ResolveObservationTarget(
		const EABTSM11FinaleInteractionState InteractionState,
		const double PlaybackSeconds,
		const FABTSM11TrajectoryResult* Result,
		const bool bUseM3ShotPlan = false,
		const FABTSM11FinaleCameraShotSettings* M3ShotSettings = nullptr)
	{
		return ABTSM11FinaleCameraDirector::ResolveStage(
			InteractionState == EABTSM11FinaleInteractionState::Launched,
			InteractionState == EABTSM11FinaleInteractionState::TargetHit,
			PlaybackSeconds,
			Result,
			bUseM3ShotPlan,
			M3ShotSettings);
	}

	bool WriteBytes(IFileHandle& File, const void* Data, const int64 Size)
	{
		return Size >= 0 && File.Write(static_cast<const uint8*>(Data), Size);
	}

	bool WriteFourCC(IFileHandle& File, const ANSICHAR (&Value)[5])
	{
		return WriteBytes(File, Value, 4);
	}

	bool WriteUInt16(IFileHandle& File, const uint16 Value)
	{
		const uint8 Bytes[2] = {
			static_cast<uint8>(Value),
			static_cast<uint8>(Value >> 8)};
		return WriteBytes(File, Bytes, UE_ARRAY_COUNT(Bytes));
	}

	bool WriteUInt32(IFileHandle& File, const uint32 Value)
	{
		const uint8 Bytes[4] = {
			static_cast<uint8>(Value),
			static_cast<uint8>(Value >> 8),
			static_cast<uint8>(Value >> 16),
			static_cast<uint8>(Value >> 24)};
		return WriteBytes(File, Bytes, UE_ARRAY_COUNT(Bytes));
	}

	int64 BeginChunk(IFileHandle& File, const ANSICHAR (&FourCC)[5])
	{
		if (!WriteFourCC(File, FourCC))
		{
			return INDEX_NONE;
		}
		const int64 SizeOffset = File.Tell();
		return WriteUInt32(File, 0) ? SizeOffset : INDEX_NONE;
	}

	bool EndChunk(IFileHandle& File, const int64 SizeOffset)
	{
		if (SizeOffset < 0)
		{
			return false;
		}
		const int64 EndBeforePadding = File.Tell();
		const int64 PayloadSize = EndBeforePadding - SizeOffset - 4;
		if (PayloadSize < 0 || PayloadSize > MAX_uint32)
		{
			return false;
		}
		if ((PayloadSize & 1) != 0)
		{
			const uint8 Padding = 0;
			if (!WriteBytes(File, &Padding, 1))
			{
				return false;
			}
		}
		const int64 EndAfterPadding = File.Tell();
		return File.Seek(SizeOffset)
			&& WriteUInt32(File, static_cast<uint32>(PayloadSize))
			&& File.Seek(EndAfterPadding);
	}

	bool RejectFinaleCameraCaptureConfig(
		FString* OutFailure,
		const FString& Reason)
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = Reason;
		}
		return false;
	}

	bool ParseBoolOption(
		const TCHAR* CommandLine,
		const TCHAR* Key,
		bool DefaultValue,
		bool& OutValue,
		FString* OutFailure)
	{
		int32 Value = DefaultValue ? 1 : 0;
		if (FParse::Value(CommandLine, Key, Value)
			&& Value != 0
			&& Value != 1)
		{
			return RejectFinaleCameraCaptureConfig(
				OutFailure,
				FString::Printf(TEXT("%s must be 0 or 1."), Key));
		}
		OutValue = Value != 0;
		return true;
	}

	FString Hex64(const uint64 Value)
	{
		return FString::Printf(TEXT("0x%016llX"), Value);
	}

	struct FM5OrthogonalityDigests
	{
		uint64 StageSequenceHash = 0;
		uint64 CameraNumericsHash = 0;
		int32 FlightFrameCount = 0;

		bool IsValid() const
		{
			return StageSequenceHash != 0 && CameraNumericsHash != 0
				&& FlightFrameCount > 0;
		}
	};

	constexpr uint64 M5FnvOffset = 14695981039346656037ull;
	constexpr uint64 M5FnvPrime = 1099511628211ull;

	void HashM5Byte(uint64& InOutHash, const uint8 Value)
	{
		InOutHash ^= Value;
		InOutHash *= M5FnvPrime;
	}

	void HashM5UInt64(uint64& InOutHash, const uint64 Value)
	{
		for (int32 Shift = 0; Shift < 64; Shift += 8)
		{
			HashM5Byte(
				InOutHash,
				static_cast<uint8>((Value >> Shift) & 0xffull));
		}
	}

	void HashM5Bool(uint64& InOutHash, const bool bValue)
	{
		HashM5Byte(InOutHash, bValue ? 1 : 0);
	}

	void HashM5String(uint64& InOutHash, const FString& Value)
	{
		const FTCHARToUTF8 Utf8(*Value);
		HashM5UInt64(InOutHash, static_cast<uint64>(Utf8.Length()));
		for (int32 Index = 0; Index < Utf8.Length(); ++Index)
		{
			HashM5Byte(InOutHash, static_cast<uint8>(Utf8.Get()[Index]));
		}
	}

	void HashM5Quantized(
		uint64& InOutHash,
		const double Value,
		const double Scale)
	{
		const int64 Quantized = FMath::IsFinite(Value)
			&& FMath::IsFinite(Scale) && Scale > 0.0
			? FMath::RoundToInt64(Value * Scale)
			: MIN_int64;
		HashM5UInt64(InOutHash, static_cast<uint64>(Quantized));
	}

	FM5OrthogonalityDigests ComputeM5OrthogonalityDigests(
		const TArray<FABTSM11FinaleCameraObservationSample>& Samples)
	{
		FM5OrthogonalityDigests Result;
		for (const FABTSM11FinaleCameraObservationSample& Sample : Samples)
		{
			Result.FlightFrameCount +=
				Sample.InteractionState == TEXT("Launched")
					|| Sample.InteractionState == TEXT("TargetHit")
				? 1
				: 0;
		}
		if (Result.FlightFrameCount <= 0)
		{
			return Result;
		}
		Result.StageSequenceHash = M5FnvOffset;
		Result.CameraNumericsHash = M5FnvOffset;
		HashM5String(Result.StageSequenceHash, TEXT("ABTS.M11.M5.Stage.v1"));
		HashM5String(Result.CameraNumericsHash, TEXT("ABTS.M11.M5.Camera.v1"));
		HashM5UInt64(Result.StageSequenceHash, Result.FlightFrameCount);
		HashM5UInt64(Result.CameraNumericsHash, Result.FlightFrameCount);
		int32 FlightFrameIndex = 0;
		for (const FABTSM11FinaleCameraObservationSample& Sample : Samples)
		{
			if (Sample.InteractionState != TEXT("Launched")
				&& Sample.InteractionState != TEXT("TargetHit"))
			{
				continue;
			}
			HashM5UInt64(Result.StageSequenceHash, FlightFrameIndex);
			HashM5Quantized(
				Result.StageSequenceHash, Sample.PlaybackSeconds, 1.0e9);
			HashM5String(Result.StageSequenceHash, Sample.InteractionState);
			HashM5String(Result.StageSequenceHash, Sample.Stage);
			HashM5String(Result.StageSequenceHash, Sample.CurrentTarget);
			HashM5String(Result.StageSequenceHash, Sample.FramingTarget);
			HashM5String(Result.StageSequenceHash, Sample.StageReason);
			HashM5String(Result.StageSequenceHash, Sample.EndpointAuthority);
			HashM5Quantized(
				Result.StageSequenceHash, Sample.StageProgress, 1.0e9);
			HashM5Quantized(
				Result.StageSequenceHash,
				Sample.StageDurationSeconds,
				1.0e9);
			HashM5String(Result.StageSequenceHash, Sample.ShotPhase);
			HashM5String(Result.StageSequenceHash, Sample.ShotReason);
			HashM5Quantized(
				Result.StageSequenceHash, Sample.ShotProgress, 1.0e9);
			HashM5Quantized(
				Result.StageSequenceHash,
				Sample.ShotDurationSeconds,
				1.0e9);
			HashM5Quantized(
				Result.StageSequenceHash, Sample.ShotEndSlope, 1.0e9);
			HashM5String(Result.StageSequenceHash, Sample.DirectorMode);
			HashM5Bool(
				Result.StageSequenceHash,
				Sample.bDirectorM2FrozenEnabled);
			HashM5Bool(
				Result.StageSequenceHash,
				Sample.bDirectorM3FrozenEnabled);
			HashM5Quantized(
				Result.StageSequenceHash,
				Sample.DirectorBlendAlpha,
				1.0e9);

			HashM5UInt64(Result.CameraNumericsHash, FlightFrameIndex);
			HashM5Quantized(
				Result.CameraNumericsHash, Sample.PlaybackSeconds, 1.0e9);
			HashM5Quantized(
				Result.CameraNumericsHash, Sample.CameraWorld.X, 1.0e3);
			HashM5Quantized(
				Result.CameraNumericsHash, Sample.CameraWorld.Y, 1.0e3);
			HashM5Quantized(
				Result.CameraNumericsHash, Sample.CameraWorld.Z, 1.0e3);
			HashM5Quantized(
				Result.CameraNumericsHash,
				Sample.CameraRotation.Pitch,
				1.0e6);
			HashM5Quantized(
				Result.CameraNumericsHash,
				Sample.CameraRotation.Yaw,
				1.0e6);
			HashM5Quantized(
				Result.CameraNumericsHash,
				Sample.CameraRotation.Roll,
				1.0e6);
			HashM5Quantized(
				Result.CameraNumericsHash, Sample.FovDegrees, 1.0e6);
			++FlightFrameIndex;
		}
		return Result;
	}

	const TCHAR* StateLabel(const EABTSM11FinaleInteractionState State)
	{
		switch (State)
		{
		case EABTSM11FinaleInteractionState::Locked:
			return TEXT("Locked");
		case EABTSM11FinaleInteractionState::Ready:
			return TEXT("Ready");
		case EABTSM11FinaleInteractionState::Aiming:
			return TEXT("Aiming");
		case EABTSM11FinaleInteractionState::ReleasePending:
			return TEXT("ReleasePending");
		case EABTSM11FinaleInteractionState::Launched:
			return TEXT("Launched");
		case EABTSM11FinaleInteractionState::TargetHit:
			return TEXT("TargetHit");
		case EABTSM11FinaleInteractionState::Failed:
			return TEXT("Failed");
		case EABTSM11FinaleInteractionState::Recovering:
			return TEXT("Recovering");
		default:
			return TEXT("Unknown");
		}
	}

	const TCHAR* EnvironmentStageLabel(
		const EABTSM11FinaleEnvironmentStage Stage)
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

	const TCHAR* StylizedProfileLabel(const EABTSStylizedRenderProfile Profile)
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

	EABTSStylizedViewClass ResolveCaptureViewClass(
		const FABTSM11FinaleCameraCaptureConfig& Config)
	{
		return Config.bMirrorMainWorldEnvironment
			? EABTSStylizedViewClass::FinaleGameplayMirrorCapture
			: EABTSStylizedViewClass::FinaleCinematicCapture;
	}
}

bool FABTSM11FinaleCameraCaptureConfig::Parse(
	const TCHAR* CommandLine,
	FABTSM11FinaleCameraCaptureConfig& OutConfig,
	FString* OutFailure)
{
	using namespace ABTSM11FinaleCameraCaptureRunnerPrivate;
	OutConfig = FABTSM11FinaleCameraCaptureConfig();
	if (CommandLine == nullptr)
	{
		return RejectFinaleCameraCaptureConfig(
			OutFailure,
			TEXT("Command line is null."));
	}
	OutConfig.bEnabled = FParse::Param(
		CommandLine,
		TEXT("ABTSM11CameraCapture"));
	if (!OutConfig.bEnabled)
	{
		return true;
	}

	FParse::Value(
		CommandLine,
		TEXT("ABTSM11CaptureRank="),
		OutConfig.CandidateRank);
	const bool bHasYaw = FParse::Value(
		CommandLine,
		TEXT("ABTSM11CaptureYaw="),
		OutConfig.CustomLaunchInput.YawDegrees);
	const bool bHasPitch = FParse::Value(
		CommandLine,
		TEXT("ABTSM11CapturePitch="),
		OutConfig.CustomLaunchInput.PitchDegrees);
	const bool bHasPower = FParse::Value(
		CommandLine,
		TEXT("ABTSM11CapturePower="),
		OutConfig.CustomLaunchInput.Power);
	if ((bHasYaw || bHasPitch || bHasPower)
		&& !(bHasYaw && bHasPitch && bHasPower))
	{
		return RejectFinaleCameraCaptureConfig(
			OutFailure,
			TEXT("ABTSM11CaptureYaw/Pitch/Power must be supplied together."));
	}
	OutConfig.bCustomLaunchInput = bHasYaw && bHasPitch && bHasPower;
	if (!ParseBoolOption(
		CommandLine,
		TEXT("ABTSM11CaptureStylized="),
		false,
		OutConfig.bStylized,
		OutFailure)
		|| !ParseBoolOption(
			CommandLine,
			TEXT("ABTSM11CaptureTelemetryOnly="),
			false,
			OutConfig.bTelemetryOnly,
			OutFailure)
		|| !ParseBoolOption(
			CommandLine,
			TEXT("ABTSM11CaptureHudScreenshot="),
			false,
			OutConfig.bHudScreenshotOnly,
			OutFailure)
		|| !ParseBoolOption(
			CommandLine,
			TEXT("ABTSM11CaptureDirectorM2="),
			false,
			OutConfig.bDirectorM2,
			OutFailure)
		|| !ParseBoolOption(
			CommandLine,
			TEXT("ABTSM11CaptureDirectorM3="),
			false,
			OutConfig.bDirectorM3,
			OutFailure)
		|| !ParseBoolOption(
			CommandLine,
			TEXT("ABTSM11CaptureMirrorMainWorld="),
			false,
			OutConfig.bMirrorMainWorldEnvironment,
			OutFailure)
		|| !ParseBoolOption(
			CommandLine,
			TEXT("ABTSM11CaptureAutoExit="),
			true,
			OutConfig.bAutoExit,
			OutFailure))
	{
		return false;
	}
	FParse::Value(
		CommandLine,
		TEXT("ABTSM11CaptureWarmupFrames="),
		OutConfig.WarmupFrames);
	FParse::Value(
		CommandLine,
		TEXT("ABTSM11CaptureTerminalHoldFrames="),
		OutConfig.TerminalHoldFrames);
	FParse::Value(
		CommandLine,
		TEXT("MovieFrameRate="),
		OutConfig.FrameRate);
	FParse::Value(
		CommandLine,
		TEXT("MovieQuality="),
		OutConfig.JpegQuality);
	FParse::Value(CommandLine, TEXT("ResX="), OutConfig.CaptureWidth);
	FParse::Value(CommandLine, TEXT("ResY="), OutConfig.CaptureHeight);
	FParse::Value(
		CommandLine,
		TEXT("ABTSM11CaptureTimeoutSeconds="),
		OutConfig.TimeoutSeconds);
	FParse::Value(
		CommandLine,
		TEXT("MovieFolder="),
		OutConfig.OutputDirectory);
	FParse::Value(
		CommandLine,
		TEXT("MovieName="),
		OutConfig.MovieName);
	FParse::Value(
		CommandLine,
		TEXT("MovieFormat="),
		OutConfig.MovieFormat);
	if (!OutConfig.OutputDirectory.IsEmpty())
	{
		OutConfig.OutputDirectory = FPaths::ConvertRelativePathToFull(
			OutConfig.OutputDirectory);
		FPaths::NormalizeDirectoryName(OutConfig.OutputDirectory);
	}
	return OutConfig.IsValid(OutFailure);
}

bool FABTSM11FinaleCameraCaptureConfig::IsValid(
	FString* OutFailure) const
{
	using namespace ABTSM11FinaleCameraCaptureRunnerPrivate;
	if (!bEnabled)
	{
		return true;
	}
	if (CandidateRank < 0
		|| CandidateRank > FABTSM11CandidateExperienceCatalog::LastCandidateRank)
	{
		return RejectFinaleCameraCaptureConfig(
			OutFailure,
			FString::Printf(
				TEXT("ABTSM11CaptureRank must be in [0, %d]."),
				FABTSM11CandidateExperienceCatalog::LastCandidateRank));
	}
	if (bDirectorM2 && bDirectorM3)
	{
		return RejectFinaleCameraCaptureConfig(
			OutFailure,
			TEXT("M2 and M3 camera director modes are mutually exclusive."));
	}
	if (bHudScreenshotOnly && bTelemetryOnly)
	{
		return RejectFinaleCameraCaptureConfig(
			OutFailure,
			TEXT("HUD screenshot and telemetry-only modes are mutually exclusive."));
	}
	if (bCustomLaunchInput && !CustomLaunchInput.IsFinite())
	{
		return RejectFinaleCameraCaptureConfig(
			OutFailure,
			TEXT("Custom capture launch input must be finite."));
	}
	if (WarmupFrames < 0 || WarmupFrames > 600
		|| TerminalHoldFrames < 0 || TerminalHoldFrames > 600)
	{
		return RejectFinaleCameraCaptureConfig(
			OutFailure,
			TEXT("Capture frame counts must be in [0, 600]."));
	}
	if (FrameRate < 1 || FrameRate > 120
		|| JpegQuality < 1 || JpegQuality > 100)
	{
		return RejectFinaleCameraCaptureConfig(
			OutFailure,
			TEXT("MovieFrameRate must be in [1, 120] and MovieQuality in [1, 100]."));
	}
	if (CaptureWidth < 320 || CaptureWidth > 7680
		|| CaptureHeight < 180 || CaptureHeight > 4320)
	{
		return RejectFinaleCameraCaptureConfig(
			OutFailure,
			TEXT("ResX/ResY must describe a capture in [320x180, 7680x4320]."));
	}
	if (!FMath::IsFinite(TimeoutSeconds)
		|| TimeoutSeconds < 10.0
		|| TimeoutSeconds > 1800.0)
	{
		return RejectFinaleCameraCaptureConfig(
			OutFailure,
			TEXT("ABTSM11CaptureTimeoutSeconds must be in [10, 1800]."));
	}
	if (OutputDirectory.IsEmpty() || FPaths::IsRelative(OutputDirectory))
	{
		return RejectFinaleCameraCaptureConfig(
			OutFailure,
			TEXT("-MovieFolder must be an absolute path."));
	}
	if (MovieName.IsEmpty()
		|| MovieName.Contains(TEXT("{"))
		|| MovieName.Contains(TEXT("}"))
		|| MovieName.Contains(TEXT("/"))
		|| MovieName.Contains(TEXT("\\")))
	{
		return RejectFinaleCameraCaptureConfig(
			OutFailure,
			TEXT("-MovieName must be a plain filename without format tokens."));
	}
	if (!MovieFormat.Equals(TEXT("JPG"), ESearchCase::IgnoreCase))
	{
		return RejectFinaleCameraCaptureConfig(
			OutFailure,
			TEXT("-MovieFormat=JPG is required; native startup AVI is not accepted because its finalized frame count is not reliable in unattended capture."));
	}
	return true;
}

FString FABTSM11FinaleCameraCaptureConfig::GetExpectedVideoPath() const
{
	return FPaths::Combine(OutputDirectory, MovieName + TEXT(".avi"));
}

FString FABTSM11FinaleCameraCaptureConfig::GetFrameWildcard() const
{
	return FPaths::Combine(OutputDirectory, MovieName + TEXT(".*.jpg"));
}

int32 FABTSM11FinaleCameraCaptureConfig::GetObservedFrameCount() const
{
	TArray<FString> FrameFiles;
	IFileManager::Get().FindFiles(FrameFiles, *GetFrameWildcard(), true, false);
	return FrameFiles.Num();
}

FString FABTSM11FinaleCameraCaptureConfig::GetManifestPath() const
{
	return FPaths::Combine(
		OutputDirectory,
		MovieName + TEXT(".manifest.json"));
}

FString FABTSM11FinaleCameraCaptureConfig::GetObservationCsvPath() const
{
	return FPaths::Combine(
		OutputDirectory,
		MovieName + TEXT(".camera-observations.csv"));
}

FString FABTSM11FinaleCameraCaptureConfig::GetHudScreenshotPath() const
{
	return FPaths::Combine(OutputDirectory, MovieName + TEXT(".png"));
}

AABTSM11FinaleCameraCaptureRunner::AABTSM11FinaleCameraCaptureRunner()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	RecordingCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(
		TEXT("RecordingCapture"));
	SetRootComponent(RecordingCapture);
	RecordingCapture->bCaptureEveryFrame = false;
	RecordingCapture->bCaptureOnMovement = false;
	RecordingCapture->bAlwaysPersistRenderingState = true;
	RecordingCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
}

bool AABTSM11FinaleCameraCaptureRunner::Initialize(
	const FABTSM11FinaleCameraCaptureConfig& InConfig,
	AABTSM11FinaleSystem& InFinaleSystem,
	AABTSM11FinaleInteractionSystem& InInteractionSystem)
{
	FString ConfigFailure;
	if (Phase != EABTSM11FinaleCameraCapturePhase::Inactive
		|| !InConfig.IsValid(&ConfigFailure)
		|| !InConfig.bEnabled
		|| GetWorld() == nullptr
		|| GetWorld()->WorldType != EWorldType::Game
		|| !InFinaleSystem.IsLayoutReady()
		|| InFinaleSystem.IsEditorCandidateMode()
			!= (InConfig.CandidateRank != 0)
		|| (InFinaleSystem.IsEditorCandidateMode()
			&& InFinaleSystem.GetEditorCandidateIdentity().Rank
				!= InConfig.CandidateRank))
	{
		FailureReason = ConfigFailure.IsEmpty()
			? TEXT("CaptureInitializationContractRejected")
			: ConfigFailure;
		return false;
	}

	Config = InConfig;
	if (!Config.bAutoExit)
	{
		FailureReason = TEXT("StandaloneCaptureRequiresAutoExit");
		return false;
	}
	if (!IFileManager::Get().MakeDirectory(*Config.OutputDirectory, true)
		|| Config.GetObservedFrameCount() > 0
		|| IFileManager::Get().FileExists(*Config.GetExpectedVideoPath())
		|| IFileManager::Get().FileExists(*Config.GetHudScreenshotPath())
		|| IFileManager::Get().FileExists(*Config.GetManifestPath())
		|| IFileManager::Get().FileExists(*Config.GetObservationCsvPath()))
	{
		FailureReason = TEXT("CaptureOutputMustBeWritableAndUnique");
		return false;
	}
	FinaleSystem = &InFinaleSystem;
	InteractionSystem = &InInteractionSystem;
	StartUtc = FDateTime::UtcNow();
	StartPlatformSeconds = FPlatformTime::Seconds();
	RemainingWarmupFrames = Config.WarmupFrames;
	ObservationSamples.Reset();
	bStylizedRuntimeStateMaintained = true;
	StylizedRuntimeStateFailureFrame = INDEX_NONE;
	bHasPreviousCameraObservation = false;
	bObservationCsvWritten = false;
	ABTSM11FinaleCameraDirector::SetM2Enabled(Config.bDirectorM2);
	ABTSM11FinaleCameraDirector::SetM3Enabled(Config.bDirectorM3);
	FABTSStylizedRenderingControl::SetEnabled(Config.bStylized);
	FABTSStylizedRenderingControl::SetProfile(
		Config.bMirrorMainWorldEnvironment
			? EABTSStylizedRenderProfile::GroundDay
			: EABTSStylizedRenderProfile::FinaleSpace);
	FApp::SetUseFixedTimeStep(true);
	FApp::SetFixedDeltaTime(1.0 / static_cast<double>(Config.FrameRate));
	const FABTSStylizedViewPolicy CaptureViewPolicy =
		FABTSStylizedRenderingContract::ResolveViewPolicy(
			ABTSM11FinaleCameraCaptureRunnerPrivate::ResolveCaptureViewClass(Config),
			FABTSStylizedRenderingControl::GetProfile());
	if (!Config.bTelemetryOnly && !Config.bHudScreenshotOnly)
	{
		RecordingRenderTarget = NewObject<UTextureRenderTarget2D>(this);
		if (!IsValid(RecordingRenderTarget) || !IsValid(RecordingCapture))
		{
			FailureReason = TEXT("RecordingRenderTargetAllocationFailed");
			return false;
		}
		RecordingRenderTarget->ClearColor = FLinearColor::Black;
		RecordingRenderTarget->InitCustomFormat(
			Config.CaptureWidth,
			Config.CaptureHeight,
			PF_B8G8R8A8,
			false);
		RecordingRenderTarget->UpdateResourceImmediate(true);
		RecordingCapture->TextureTarget = RecordingRenderTarget;
		bStylizedViewRegistered = FABTSStylizedSceneCaptureRegistry::Register(
			*RecordingCapture,
			ABTSM11FinaleCameraCaptureRunnerPrivate::ResolveCaptureViewClass(Config));
	}
	if ((!Config.bTelemetryOnly
			&& !Config.bHudScreenshotOnly
			&& !bStylizedViewRegistered)
		|| !CaptureViewPolicy.IsValid())
	{
		FailureReason = Config.bTelemetryOnly
			? TEXT("TelemetryStylizedViewPolicyInvalid")
			: TEXT("RecordingStylizedViewRegistrationFailed");
		return false;
	}
	CapturedFrameSize = FIntPoint(
		Config.CaptureWidth,
		Config.CaptureHeight);
	Phase = EABTSM11FinaleCameraCapturePhase::WarmingRenderMode;

	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M11][CameraCapture] Initialized Contract=%d WorldType=%d Mode=%s Format=%s Rank=%d Authority=%s Stylized=%d TelemetryOnly=%d DirectorM2=%d DirectorM3=%d MirrorMainWorld=%d RenderVersion=%d ViewClass=%s PolicyTone=%d PolicyOutline=%d PolicySelective=%d WarmupFrames=%d Frames=%s Video=%s"),
		FABTSM11FinaleCameraCaptureConfig::ContractVersion,
		static_cast<int32>(GetWorld()->WorldType),
		Config.bHudScreenshotOnly
			? TEXT("GameViewportHudScreenshot")
			: Config.bTelemetryOnly
			? TEXT("RendererIndependentTelemetry")
			: TEXT("StandaloneSceneCaptureFrameCapture"),
		*Config.MovieFormat,
		Config.CandidateRank,
		Config.CandidateRank == 0 ? TEXT("Certified") : TEXT("UNCERTIFIED"),
		Config.bStylized ? 1 : 0,
		Config.bTelemetryOnly ? 1 : 0,
		Config.bDirectorM2 ? 1 : 0,
		Config.bDirectorM3 ? 1 : 0,
		Config.bMirrorMainWorldEnvironment ? 1 : 0,
		FABTSStylizedRenderingControl::GetImplementationVersion(),
		Config.bMirrorMainWorldEnvironment
			? TEXT("FinaleGameplayMirrorCapture")
			: TEXT("FinaleCinematicCapture"),
		CaptureViewPolicy.bApplyTone ? 1 : 0,
		CaptureViewPolicy.bApplyOutline ? 1 : 0,
		CaptureViewPolicy.bAllowSelectiveStencil ? 1 : 0,
		RemainingWarmupFrames,
		*Config.GetFrameWildcard(),
		*Config.GetExpectedVideoPath());
	return true;
}

void AABTSM11FinaleCameraCaptureRunner::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	if (bStylizedViewRegistered && IsValid(RecordingCapture))
	{
		FABTSStylizedSceneCaptureRegistry::Unregister(*RecordingCapture);
		bStylizedViewRegistered = false;
	}
	Super::EndPlay(EndPlayReason);
}

void AABTSM11FinaleCameraCaptureRunner::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Phase == EABTSM11FinaleCameraCapturePhase::Inactive
		|| Phase == EABTSM11FinaleCameraCapturePhase::Terminal)
	{
		return;
	}
	if (!HasExpectedStylizedRuntimeState())
	{
		FailForStylizedRuntimeStateDrift();
		return;
	}
	if (bMovieCaptureStarted
		&& !bMovieCaptureStopped
		&& !CaptureCurrentFrame())
	{
		Finish(false, FailureReason.IsEmpty()
			? TEXT("SceneCaptureFrameCaptureFailed")
			: FailureReason);
		return;
	}

	const double Elapsed =
		FPlatformTime::Seconds() - StartPlatformSeconds;
	if (Phase != EABTSM11FinaleCameraCapturePhase::Finalizing
		&& Elapsed > Config.TimeoutSeconds)
	{
		if (bMovieCaptureStarted && !bMovieCaptureStopped)
		{
			bPendingFinalizeSuccess = false;
			PendingFinalizeReason = TEXT("CaptureTimeout");
			StopRecording();
		}
		else
		{
			Finish(false, TEXT("CaptureTimeout"));
		}
		return;
	}

	if (!IsValid(FinaleSystem) || !IsValid(InteractionSystem))
	{
		Finish(false, TEXT("CaptureDependenciesLost"));
		return;
	}

	switch (Phase)
	{
	case EABTSM11FinaleCameraCapturePhase::WarmingRenderMode:
		if (RemainingWarmupFrames > 0)
		{
			--RemainingWarmupFrames;
			return;
		}
		bMovieCaptureStarted = !Config.bHudScreenshotOnly;
		Phase = EABTSM11FinaleCameraCapturePhase::WaitingForDependencies;
		break;

	case EABTSM11FinaleCameraCapturePhase::WaitingForDependencies:
		// Preserve an observable "recording started, then launch" order and
		// prove the post-render callback is producing real files before input.
		if (!Config.bHudScreenshotOnly && CapturedFrameCount < 2)
		{
			break;
		}
		FailureReason.Reset();
		if (TryBeginNominalAttempt())
		{
			RemainingHudSettleFrames = Config.TerminalHoldFrames;
			Phase = Config.bHudScreenshotOnly
				? EABTSM11FinaleCameraCapturePhase::WaitingForHudScreenshot
				: EABTSM11FinaleCameraCapturePhase::WaitingForLaunch;
		}
		else if (!FailureReason.IsEmpty())
		{
			Finish(false, FailureReason);
		}
		break;

	case EABTSM11FinaleCameraCapturePhase::WaitingForHudScreenshot:
		if (InteractionSystem->GetInteractionState()
			== EABTSM11FinaleInteractionState::Failed)
		{
			Finish(false, TEXT("HudPreviewAttemptFailed:")
				+ InteractionSystem->GetRuntimeFailure());
			break;
		}
		if (!InteractionSystem->IsAiming())
		{
			break;
		}
		if (!bHudScreenshotRequested)
		{
			if (RemainingHudSettleFrames > 0
				|| InteractionSystem->GetTargetPreviewPrediction() == nullptr)
			{
				RemainingHudSettleFrames = FMath::Max(0, RemainingHudSettleFrames - 1);
				break;
			}
			if (FScreenshotRequest::IsScreenshotRequested())
			{
				Finish(false, TEXT("AnotherScreenshotRequestIsActive"));
				break;
			}
			// Preserve the real HUD while excluding transient startup diagnostics
			// from the visual-design artifact.
			GAreScreenMessagesEnabled = false;
			FScreenshotRequest::RequestScreenshot(
				Config.GetHudScreenshotPath(),
				true,
				false,
				false,
				FIntRect(),
				true);
			bHudScreenshotRequested = FScreenshotRequest::IsScreenshotRequested();
			if (!bHudScreenshotRequested)
			{
				Finish(false, TEXT("HudScreenshotRequestRejected"));
			}
			break;
		}
		if (!FScreenshotRequest::IsScreenshotRequested()
			&& IFileManager::Get().FileExists(*Config.GetHudScreenshotPath()))
		{
			Finish(true, TEXT("HudScreenshotComplete"));
		}
		break;

	case EABTSM11FinaleCameraCapturePhase::WaitingForLaunch:
		if (InteractionSystem->GetInteractionState()
			== EABTSM11FinaleInteractionState::Failed)
		{
			Finish(
				false,
				TEXT("NominalAttemptFailedBeforeRecording:" )
					+ InteractionSystem->GetRuntimeFailure());
			break;
		}
		if (InteractionSystem->GetInteractionState()
				== EABTSM11FinaleInteractionState::Launched
			&& IsValid(InteractionSystem->GetFlightCamera())
			&& InteractionSystem->GetFlightCamera()
				->IsAuthorityFollowActive())
		{
			if (!TryStartRecording())
			{
				if (!FailureReason.IsEmpty())
				{
					Finish(false, FailureReason);
				}
			}
		}
		break;

	case EABTSM11FinaleCameraCapturePhase::Recording:
		if (InteractionSystem->GetInteractionState()
			== EABTSM11FinaleInteractionState::TargetHit)
		{
			RemainingTerminalHoldFrames = Config.TerminalHoldFrames;
			Phase = EABTSM11FinaleCameraCapturePhase::HoldingTerminalFrame;
		}
		else if (InteractionSystem->GetInteractionState()
			== EABTSM11FinaleInteractionState::Failed)
		{
			bPendingFinalizeSuccess = false;
			PendingFinalizeReason = TEXT("PlaybackFailed:")
				+ InteractionSystem->GetRuntimeFailure();
			StopRecording();
		}
		break;

	case EABTSM11FinaleCameraCapturePhase::HoldingTerminalFrame:
		if (RemainingTerminalHoldFrames > 0)
		{
			--RemainingTerminalHoldFrames;
			return;
		}
		bPendingFinalizeSuccess = true;
		PendingFinalizeReason = TEXT("TargetHit");
		StopRecording();
		break;

	default:
		break;
	}
}

bool AABTSM11FinaleCameraCaptureRunner::HasExpectedStylizedRuntimeState() const
{
	const EABTSStylizedRenderProfile ExpectedProfile =
		Config.bMirrorMainWorldEnvironment
			? EABTSStylizedRenderProfile::GroundDay
			: EABTSStylizedRenderProfile::FinaleSpace;
	return FABTSStylizedRenderingControl::IsEnabled() == Config.bStylized
		&& FABTSStylizedRenderingControl::GetProfile()
			== ExpectedProfile;
}

void AABTSM11FinaleCameraCaptureRunner::FailForStylizedRuntimeStateDrift()
{
	bStylizedRuntimeStateMaintained = false;
	StylizedRuntimeStateFailureFrame = CapturedFrameCount;
	const FString Reason = FString::Printf(
		TEXT("StylizedRuntimeStateDrift:Frame=%d ExpectedEnabled=%d ActualEnabled=%d ExpectedProfile=%s ActualProfile=%d"),
		CapturedFrameCount,
		Config.bStylized ? 1 : 0,
		FABTSStylizedRenderingControl::IsEnabled() ? 1 : 0,
		Config.bMirrorMainWorldEnvironment ? TEXT("GroundDay") : TEXT("FinaleSpace"),
		static_cast<int32>(FABTSStylizedRenderingControl::GetProfile()));
	UE_LOG(
		LogABTSRuntime,
		Error,
		TEXT("[ABTS][M11][CameraCapture] %s"),
		*Reason);
	if (bMovieCaptureStarted && !bMovieCaptureStopped)
	{
		bPendingFinalizeSuccess = false;
		PendingFinalizeReason = Reason;
		StopRecording();
	}
	else
	{
		Finish(false, Reason);
	}
}

bool AABTSM11FinaleCameraCaptureRunner::TryBeginNominalAttempt()
{
	UWorld* World = GetWorld();
	APlayerController* Controller =
		UGameplayStatics::GetPlayerController(World, 0);
	if (!IsValid(Controller)
		|| InteractionSystem->GetInteractionState()
			!= EABTSM11FinaleInteractionState::Ready)
	{
		return false;
	}

	AABTSM51SlingshotCord* MatchingCord = nullptr;
	if (!TryResolveOrCreateCaptureCord(MatchingCord))
	{
		return false;
	}
	const FABTSM11FinaleLaunchInput Input = Config.bCustomLaunchInput
		? Config.CustomLaunchInput
		: FinaleSystem->GetLayoutPreset().NominalInput;
	if (!FinaleSystem->GetLayoutPreset().LaunchModel.Contains(Input))
	{
		FailureReason = TEXT("CaptureLaunchInputOutsidePresetDomain");
		return false;
	}
	const bool bAttemptAccepted = Config.bHudScreenshotOnly
		? InteractionSystem->TryEnterCaptureAim(
			*MatchingCord,
			*Controller,
			Input)
		: InteractionSystem->TryLaunchCaptureAttempt(
			*MatchingCord,
			*Controller,
			Input);
	if (!bAttemptAccepted)
	{
		FailureReason = TEXT("CaptureAttemptRejected");
		return false;
	}

	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M11][CameraCapture] AttemptQueued Mode=%s Rank=%d Yaw=%.9f Pitch=%.9f Power=%.9f"),
		Config.bHudScreenshotOnly
			? TEXT("HudAiming")
			: Config.bCustomLaunchInput ? TEXT("CustomF4") : TEXT("Nominal"),
		Config.CandidateRank,
		Input.YawDegrees,
		Input.PitchDegrees,
		Input.Power);
	return true;
}

bool AABTSM11FinaleCameraCaptureRunner::TryResolveOrCreateCaptureCord(
	AABTSM51SlingshotCord*& OutCord)
{
	OutCord = nullptr;
	UWorld* World = GetWorld();
	if (World == nullptr || !IsValid(FinaleSystem))
	{
		FailureReason = TEXT("CaptureFixtureWorldUnavailable");
		return false;
	}

	const FABTSM110FinaleLocalFrame& Frame =
		FinaleSystem->GetFinaleFrame();
	int32 MatchingCordCount = 0;
	for (TActorIterator<AABTSM51SlingshotCord> It(World); It; ++It)
	{
		if (It->IsFinaleSpaceSlingshot()
			&& It->GetFinaleSlotPairId() == Frame.SlotPairId)
		{
			OutCord = *It;
			++MatchingCordCount;
		}
	}
	if (MatchingCordCount == 1 && IsValid(OutCord))
	{
		return true;
	}
	if (MatchingCordCount > 1)
	{
		FailureReason = FString::Printf(
			TEXT("ExpectedAtMostOneMatchingFinaleCord:Found=%d"),
			MatchingCordCount);
		return false;
	}

	AABTSM51SlingshotDirtHole* LeftSlot = nullptr;
	AABTSM51SlingshotDirtHole* RightSlot = nullptr;
	int32 LeftSlotCount = 0;
	int32 RightSlotCount = 0;
	for (TActorIterator<AABTSM51SlingshotDirtHole> It(World); It; ++It)
	{
		if (!It->IsFinaleSpaceSlot()
			|| It->GetSlotPairId() != Frame.SlotPairId)
		{
			continue;
		}
		if (It->GetSlotSide() == EABTSSlingshotSlotSide::Left)
		{
			LeftSlot = *It;
			++LeftSlotCount;
		}
		else if (It->GetSlotSide() == EABTSSlingshotSlotSide::Right)
		{
			RightSlot = *It;
			++RightSlotCount;
		}
	}
	if (LeftSlotCount == 0 || RightSlotCount == 0)
	{
		// The M5.1 system publishes the slots before WorldReady. If the
		// capture actor ticks first, wait for the unique pair to appear.
		return false;
	}
	if (LeftSlotCount != 1 || RightSlotCount != 1
		|| !IsValid(LeftSlot) || !IsValid(RightSlot))
	{
		FailureReason = FString::Printf(
			TEXT("ExpectedUniqueFinaleSlotPair:Left=%d Right=%d"),
			LeftSlotCount,
			RightSlotCount);
		return false;
	}
	if (LeftSlot->IsOccupied() || RightSlot->IsOccupied())
	{
		FailureReason = TEXT("FinaleSlotOccupiedWithoutMatchingCord");
		return false;
	}

	const FABTSSlingshotVisualPreset VisualPreset =
		ABTSMakeDefaultSlingshotVisualPreset(EABTSSlingshotTier::Space);
	const FVector Up = Frame.GetUp().GetSafeNormal();
	const FVector Forward = Frame.GetForward().GetSafeNormal();
	if (Up.IsNearlyZero() || Forward.IsNearlyZero())
	{
		FailureReason = TEXT("CaptureFixtureFrameAxesInvalid");
		return false;
	}
	const FQuat StakeRotation =
		FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat();
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const auto SpawnStake =
		[World, &SpawnParameters, &VisualPreset, &Up, &StakeRotation,
			&Frame](AABTSM51SlingshotDirtHole& Slot)
			-> AABTSM51SlingshotStake*
	{
		const FTransform Transform(
			StakeRotation,
			Slot.GetActorLocation()
				+ Up * (VisualPreset.StakeHeightCM * 0.5f));
		AABTSM51SlingshotStake* Stake =
			World->SpawnActor<AABTSM51SlingshotStake>(
				AABTSM51SlingshotStake::StaticClass(),
				Transform,
				SpawnParameters);
		if (!IsValid(Stake))
		{
			return nullptr;
		}
		Stake->InitializeStake(
			EABTSItemId::SpaceStake,
			Slot.GetCellId(),
			Up);
		Stake->SetInstalledSlotIdentity(
			EABTSSlingshotSlotKind::FinaleSpace,
			Frame.SlotPairId,
			Slot.GetSlotSide());
		Slot.SetOccupiedStake(Stake);
		return Stake;
	};

	AABTSM51SlingshotStake* LeftStake = SpawnStake(*LeftSlot);
	AABTSM51SlingshotStake* RightStake = SpawnStake(*RightSlot);
	if (!IsValid(LeftStake) || !IsValid(RightStake))
	{
		LeftSlot->SetOccupiedStake(nullptr);
		RightSlot->SetOccupiedStake(nullptr);
		if (IsValid(LeftStake))
		{
			LeftStake->Destroy();
		}
		if (IsValid(RightStake))
		{
			RightStake->Destroy();
		}
		FailureReason = TEXT("CaptureFixtureStakeSpawnFailed");
		return false;
	}

	AABTSM51SlingshotCord* Cord =
		World->SpawnActor<AABTSM51SlingshotCord>(
			AABTSM51SlingshotCord::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	if (!IsValid(Cord))
	{
		LeftSlot->SetOccupiedStake(nullptr);
		RightSlot->SetOccupiedStake(nullptr);
		LeftStake->Destroy();
		RightStake->Destroy();
		FailureReason = TEXT("CaptureFixtureCordSpawnFailed");
		return false;
	}
	Cord->InitializeCordWithTier(
		LeftStake,
		RightStake,
		LeftStake->GetVisualTopWorldLocation(),
		RightStake->GetVisualTopWorldLocation(),
		EABTSSlingshotTier::Space);
	LeftStake->SetHasCord(true);
	RightStake->SetHasCord(true);
	if (!Cord->IsFinaleSpaceSlingshot()
		|| Cord->GetFinaleSlotPairId() != Frame.SlotPairId)
	{
		Cord->Destroy();
		LeftSlot->SetOccupiedStake(nullptr);
		RightSlot->SetOccupiedStake(nullptr);
		LeftStake->Destroy();
		RightStake->Destroy();
		FailureReason = TEXT("CaptureFixtureIdentityRejected");
		return false;
	}

	CaptureFixtureLeftStake = LeftStake;
	CaptureFixtureRightStake = RightStake;
	CaptureFixtureCord = Cord;
	bCaptureFixtureCreated = true;
	OutCord = Cord;
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M11][CameraCapture] CaptureFixtureCreated Pair=%d LeftCell=%d RightCell=%d Endpoints=(%s,%s)"),
		Frame.SlotPairId,
		LeftSlot->GetCellId(),
		RightSlot->GetCellId(),
		*Cord->GetEndpointA().ToCompactString(),
		*Cord->GetEndpointB().ToCompactString());
	return true;
}

bool AABTSM11FinaleCameraCaptureRunner::TryStartRecording()
{
	// Frame recording began before the nominal attempt. Reaching this point
	// proves that the old flight camera has taken the view; no GUI, PIE command,
	// or legacy MovieScene FrameGrabber is involved.
	Phase = EABTSM11FinaleCameraCapturePhase::Recording;
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M11][CameraCapture] StandaloneRecordingObserved Rank=%d Stylized=%d Frames=%d Camera=%s Plan=0x%016llX"),
		Config.CandidateRank,
		Config.bStylized ? 1 : 0,
		CapturedFrameCount,
		*GetNameSafe(InteractionSystem->GetFlightCamera()),
		InteractionSystem->GetReleasedPlaybackPlan().PlanHash);
	return true;
}

bool AABTSM11FinaleCameraCaptureRunner::CaptureCurrentFrame()
{
	APlayerCameraManager* CameraManager =
		UGameplayStatics::GetPlayerCameraManager(GetWorld(), 0);
	if (!IsValid(CameraManager)
		|| (!Config.bTelemetryOnly
			&& (!IsValid(RecordingCapture)
				|| !IsValid(RecordingRenderTarget))))
	{
		FailureReason = TEXT("RecordingCameraDependenciesUnavailable");
		return false;
	}
	const FMinimalViewInfo& View = CameraManager->GetCameraCacheView();
	if (!RecordCameraObservation(View))
	{
		return false;
	}
	if (Config.bTelemetryOnly)
	{
		CapturedFrameSize = FIntPoint(
			Config.CaptureWidth,
			Config.CaptureHeight);
		++CapturedFrameCount;
		return true;
	}
	RecordingCapture->SetWorldLocationAndRotation(
		View.Location,
		View.Rotation);
	RecordingCapture->FOVAngle = View.FOV;
	RecordingCapture->PostProcessSettings = View.PostProcessSettings;
	RecordingCapture->PostProcessBlendWeight = View.PostProcessBlendWeight;
	RecordingCapture->CaptureScene();

	FTextureRenderTargetResource* Resource =
		RecordingRenderTarget->GameThread_GetRenderTargetResource();
	TArray<FColor> Pixels;
	FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
	ReadFlags.SetLinearToGamma(false);
	const FIntPoint Size(Config.CaptureWidth, Config.CaptureHeight);
	if (Resource == nullptr
		|| !Resource->ReadPixels(Pixels, ReadFlags)
		|| Pixels.Num() != Size.X * Size.Y)
	{
		FailureReason = TEXT("RecordingRenderTargetReadFailed");
		return false;
	}

	const FString FramePath = FPaths::Combine(
		Config.OutputDirectory,
		FString::Printf(
			TEXT("%s.%06d.jpg"),
			*Config.MovieName,
			CapturedFrameCount));
	const FImageView Image(
		Pixels.GetData(),
		Size.X,
		Size.Y,
		EGammaSpace::sRGB);
	if (!FImageUtils::SaveImageByExtension(
		*FramePath,
		Image,
		Config.JpegQuality))
	{
		FailureReason = TEXT("JpegFrameWriteFailed");
		return false;
	}

	CapturedFrameSize = Size;
	++CapturedFrameCount;
	return true;
}

bool AABTSM11FinaleCameraCaptureRunner::RecordCameraObservation(
	const FMinimalViewInfo& View)
{
	using namespace ABTSM11FinaleCameraCaptureRunnerPrivate;
	if (!IsValid(FinaleSystem) || !IsValid(InteractionSystem))
	{
		FailureReason = TEXT("CameraObservationDependenciesUnavailable");
		return false;
	}
	AABTSM25BirdCharacter* Bird = InteractionSystem->GetAttemptBird();
	const EABTSM11FinaleInteractionState InteractionState =
		InteractionSystem->GetInteractionState();
	const bool bBirdRequired =
		InteractionState == EABTSM11FinaleInteractionState::Launched
		|| InteractionState == EABTSM11FinaleInteractionState::TargetHit;
	if (bBirdRequired && !IsValid(Bird))
	{
		FailureReason = TEXT("CameraObservationBirdUnavailable");
		return false;
	}
	const double PlaybackSeconds =
		InteractionSystem->GetPlaybackElapsedSeconds();
	const AABTSM11FinaleFlightCamera* FlightCamera =
		InteractionSystem->GetFlightCamera();
	const FABTSM11FinaleCameraShotSettings PresentationShotSettings =
		IsValid(FlightCamera)
			? FlightCamera->GetM3ShotSettings()
			: FABTSM11FinaleCameraShotSettings();
	FABTSM11FinaleCameraShotSettings M3ShotSettings;
	if (!PresentationShotSettings.BuildPlaybackClockSettings(
		InteractionSystem->GetPlaybackPresentationTimeScale(),
		M3ShotSettings))
	{
		FailureReason = TEXT("CameraObservationShotClockInvalid");
		return false;
	}
	const FABTSM11TrajectoryResult* ObservationPrediction =
		InteractionSystem->GetCurrentPrediction();
	FABTSM11FinaleCameraStageSelection ObservationTarget =
		ResolveObservationTarget(
			InteractionState,
			PlaybackSeconds,
			ObservationPrediction,
			Config.bDirectorM3,
			&M3ShotSettings);
	const FABTSM11PlaybackPlan& PlaybackPlan =
		InteractionSystem->GetReleasedPlaybackPlan();
	ObservationTarget.EndpointAuthority = PlaybackPlan.bPhysicalTargetHit
		? EABTSM11FinaleCameraEndpointAuthority::PhysicalContact
		: PlaybackPlan.bCandidateQualifiedIntercept
			? EABTSM11FinaleCameraEndpointAuthority::CandidateQualified
			: EABTSM11FinaleCameraEndpointAuthority::None;
	if (ObservationTarget.Stage == EABTSM11FinaleCameraStage::FinalApproach
		|| ObservationTarget.Stage == EABTSM11FinaleCameraStage::Terminal)
	{
		const FABTSM11TrajectoryEvent* Assist3Exit =
			ObservationPrediction != nullptr
				? ObservationPrediction->FindAssistEvent(
					EABTSM11TrajectoryEventType::AssistExit,
					FABTSM11GravityScenario::AssistCount)
				: nullptr;
		if (Assist3Exit == nullptr
			|| !ABTSM11FinaleCameraDirector::ApplyM4TerminalTimeline(
				PlaybackSeconds,
				Assist3Exit->TimeSeconds,
				PlaybackPlan.DurationSeconds,
				ObservationTarget))
		{
			FailureReason = TEXT("CameraObservationTerminalTimelineInvalid");
			return false;
		}
		ObservationTarget.Reason = FString::Printf(
			TEXT("%sEndpoint"),
			ABTSM11FinaleCameraDirector::EndpointAuthorityLabel(
				ObservationTarget.EndpointAuthority));
	}
	if (ObservationTarget.Stage
		== EABTSM11FinaleCameraStage::Unavailable)
	{
		FailureReason = ObservationTarget.Reason;
		return false;
	}

	FVector BirdCenter = FVector::ZeroVector;
	double BirdRadiusCM = 0.0;
	if (IsValid(Bird))
	{
		const USkeletalMeshComponent* BirdVisual = Bird->GetBirdVisual();
		if (IsValid(BirdVisual))
		{
			BirdCenter = BirdVisual->Bounds.Origin;
			BirdRadiusCM = BirdVisual->Bounds.SphereRadius;
		}
		if (!FMath::IsFinite(BirdRadiusCM) || BirdRadiusCM <= 1.0)
		{
			BirdCenter = Bird->GetActorLocation();
			BirdRadiusCM = 60.0;
		}
	}

	const TArray<TObjectPtr<AABTSM25BirdCharacter>>& FormationBirds =
		InteractionSystem->GetAttemptFormationBirds();
	if (bBirdRequired
		&& FormationBirds.Num()
			!= FABTSM11FinaleCameraObservationSample::M6FormationMemberCount)
	{
		FailureReason = TEXT("CameraObservationM6FormationUnavailable");
		return false;
	}

	const FABTSM11FinaleLayoutPreset& Preset =
		FinaleSystem->GetLayoutPreset();
	const FABTSM110FinaleLocalFrame& Frame = FinaleSystem->GetFinaleFrame();
	FVector TargetCenter = FVector::ZeroVector;
	double TargetRadiusCM = 0.0;
	if (ObservationTarget.bTargetIsUFO)
	{
		const FABTSM11TargetSpec& Target = Preset.CanonicalScenario.Target;
		TargetCenter = Frame.TransformLocalPosition(
			FVector(Target.GetGeometricContactCenterCM()));
		TargetRadiusCM = Target.GetGeometricContactRadiusCM();
	}
	else
	{
		const int32 ObservedAssistIndex = Config.bDirectorM3
			? ObservationTarget.FramingAssistIndex
			: ObservationTarget.AssistIndex;
		const FABTSM11GravityBodySpec& Body =
			Preset.CanonicalScenario.GetAssist(ObservedAssistIndex);
		TargetCenter = Frame.TransformLocalPosition(FVector(Body.CenterCM));
		TargetRadiusCM = Body.VisualRadiusCM;
	}
	if (!FMath::IsFinite(TargetRadiusCM) || TargetRadiusCM <= 1.0)
	{
		FailureReason = TEXT("CameraObservationTargetRadiusInvalid");
		return false;
	}

	const FIntPoint Size(Config.CaptureWidth, Config.CaptureHeight);
	FProjectedSphereObservation BirdProjection;
	FProjectedSphereObservation TargetProjection;
	FProjectedSphereObservation BridgeOutgoingProjection;
	FProjectedSphereObservation BridgeIncomingProjection;
	TStaticArray<FProjectedSphereObservation,
		FABTSM11FinaleCameraObservationSample::M6FormationMemberCount>
		FormationProjections;
	if ((IsValid(Bird) && !ProjectObservationSphere(
				View,
				Size,
				BirdCenter,
				BirdRadiusCM,
				BirdProjection))
		|| !ProjectObservationSphere(
			View,
			Size,
			TargetCenter,
			TargetRadiusCM,
			TargetProjection))
	{
		FailureReason = TEXT("CameraObservationProjectionInvalid");
		return false;
	}
	for (int32 Index = 0; Index < FormationBirds.Num(); ++Index)
	{
		AABTSM25BirdCharacter* FormationBird = FormationBirds[Index];
		if (!IsValid(FormationBird))
		{
			FailureReason = TEXT("CameraObservationM6FormationMemberMissing");
			return false;
		}
		FVector Center = FormationBird->GetActorLocation();
		double RadiusCM = 60.0;
		if (const USkeletalMeshComponent* Visual = FormationBird->GetBirdVisual())
		{
			Center = Visual->Bounds.Origin;
			RadiusCM = FMath::Max(
				1.0,
				static_cast<double>(Visual->Bounds.SphereRadius));
		}
		if (!ProjectObservationSphere(
			View,
			Size,
			Center,
			RadiusCM,
			FormationProjections[Index]))
		{
			FailureReason = TEXT("CameraObservationM6FormationProjectionInvalid");
			return false;
		}
	}
	if (ObservationTarget.IsM3InterBodyTransition())
	{
		const FABTSM11GravityBodySpec& OutgoingBody =
			Preset.CanonicalScenario.GetAssist(
				ObservationTarget.OutgoingAssistIndex);
		const FABTSM11GravityBodySpec& IncomingBody =
			Preset.CanonicalScenario.GetAssist(
				ObservationTarget.IncomingAssistIndex);
		if (!ProjectObservationSphere(
				View,
				Size,
				Frame.TransformLocalPosition(FVector(OutgoingBody.CenterCM)),
				OutgoingBody.VisualRadiusCM,
				BridgeOutgoingProjection)
			|| !ProjectObservationSphere(
				View,
				Size,
				Frame.TransformLocalPosition(FVector(IncomingBody.CenterCM)),
				IncomingBody.VisualRadiusCM,
				BridgeIncomingProjection))
		{
			FailureReason = TEXT("CameraObservationBridgeProjectionInvalid");
			return false;
		}
	}
	else if (ObservationTarget.IsM4TerminalTransition())
	{
		const FABTSM11GravityBodySpec& OutgoingBody =
			Preset.CanonicalScenario.GetAssist(
				FABTSM11GravityScenario::AssistCount);
		const FABTSM11TargetSpec& IncomingTarget =
			Preset.CanonicalScenario.Target;
		if (!ProjectObservationSphere(
			View,
			Size,
			Frame.TransformLocalPosition(FVector(OutgoingBody.CenterCM)),
			OutgoingBody.VisualRadiusCM,
			BridgeOutgoingProjection)
			|| !ProjectObservationSphere(
				View,
				Size,
				Frame.TransformLocalPosition(FVector(
					IncomingTarget.GetGeometricContactCenterCM())),
				IncomingTarget.GetGeometricContactRadiusCM(),
				BridgeIncomingProjection))
		{
			FailureReason = TEXT("CameraObservationTerminalProjectionInvalid");
			return false;
		}
	}

	FABTSM11FinaleCameraObservationSample Sample;
	Sample.FrameIndex = CapturedFrameCount;
	Sample.CaptureSeconds = static_cast<double>(CapturedFrameCount)
		/ static_cast<double>(Config.FrameRate);
	Sample.PlaybackSeconds = PlaybackSeconds;
	Sample.InteractionState = StateLabel(InteractionState);
	Sample.EnvironmentStage = EnvironmentStageLabel(
		InteractionSystem->GetFinaleEnvironmentStage());
	FABTSStylizedEnvironmentParameters ActiveEnvironment;
	Sample.EnvironmentProfile =
		FABTSStylizedRenderingControl::TryGetEnvironmentParametersOnAnyThread(
			ActiveEnvironment)
			? StylizedProfileLabel(ActiveEnvironment.Profile)
			: TEXT("Unavailable");
	Sample.Stage = ABTSM11FinaleCameraDirector::StageLabel(
		ObservationTarget.Stage);
	Sample.CurrentTarget = ObservationTarget.TargetLabel;
	Sample.FramingTarget = ObservationTarget.FramingTargetLabel;
	Sample.StageReason = ObservationTarget.Reason;
	Sample.EndpointAuthority =
		ABTSM11FinaleCameraDirector::EndpointAuthorityLabel(
			ObservationTarget.EndpointAuthority);
	Sample.StageProgress = ObservationTarget.StageProgress;
	Sample.StageDurationSeconds = ObservationTarget.StageDurationSeconds;
	Sample.ShotPhase = ABTSM11FinaleCameraDirector::ShotPhaseLabel(
		ObservationTarget.ShotPhase);
	Sample.ShotReason = ObservationTarget.ShotReason;
	Sample.ShotProgress = ObservationTarget.ShotProgress;
	Sample.ShotDurationSeconds = ObservationTarget.ShotDurationSeconds;
	Sample.ShotEndSlope = ObservationTarget.ShotEndSlope;
	if (IsValid(FlightCamera))
	{
		Sample.bDirectorM2FrozenEnabled =
			FlightCamera->IsM2DirectorFrozenEnabled();
		Sample.bDirectorM3FrozenEnabled =
			FlightCamera->IsM3DirectorFrozenEnabled();
		Sample.DirectorBlendAlpha = FlightCamera->GetLastM2BlendAlpha();
	}
	Sample.DirectorMode = Sample.bDirectorM3FrozenEnabled
		? TEXT("M3MultiAssist")
		: Sample.bDirectorM2FrozenEnabled
			? TEXT("M2Assist1")
			: TEXT("Legacy");
	Sample.BirdWorld = BirdCenter;
	Sample.BirdScreen = BirdProjection.Screen;
	Sample.BirdDepthCM = BirdProjection.DepthCM;
	Sample.BirdPixelRadius = BirdProjection.PixelRadius;
	Sample.BirdVisibleRatio = BirdProjection.VisibleRatio;
	Sample.TargetWorld = TargetCenter;
	Sample.TargetScreen = TargetProjection.Screen;
	Sample.TargetDepthCM = TargetProjection.DepthCM;
	Sample.TargetPixelRadius = TargetProjection.PixelRadius;
	Sample.TargetVisibleRatio = TargetProjection.VisibleRatio;
	if (ObservationTarget.IsM3InterBodyTransition())
	{
		Sample.BridgeOutgoingTarget = FString::Printf(
			TEXT("Assist%d"), ObservationTarget.OutgoingAssistIndex);
		Sample.BridgeOutgoingScreen = BridgeOutgoingProjection.Screen;
		Sample.BridgeOutgoingPixelRadius =
			BridgeOutgoingProjection.PixelRadius;
		Sample.BridgeOutgoingVisibleRatio =
			BridgeOutgoingProjection.VisibleRatio;
		Sample.BridgeIncomingTarget = FString::Printf(
			TEXT("Assist%d"), ObservationTarget.IncomingAssistIndex);
		Sample.BridgeIncomingScreen = BridgeIncomingProjection.Screen;
		Sample.BridgeIncomingPixelRadius =
			BridgeIncomingProjection.PixelRadius;
		Sample.BridgeIncomingVisibleRatio =
			BridgeIncomingProjection.VisibleRatio;
	}
	else if (ObservationTarget.IsM4TerminalTransition())
	{
		Sample.BridgeOutgoingTarget = TEXT("Assist3");
		Sample.BridgeOutgoingScreen = BridgeOutgoingProjection.Screen;
		Sample.BridgeOutgoingPixelRadius =
			BridgeOutgoingProjection.PixelRadius;
		Sample.BridgeOutgoingVisibleRatio =
			BridgeOutgoingProjection.VisibleRatio;
		Sample.BridgeIncomingTarget = TEXT("UFO");
		Sample.BridgeIncomingScreen = BridgeIncomingProjection.Screen;
		Sample.BridgeIncomingPixelRadius =
			BridgeIncomingProjection.PixelRadius;
		Sample.BridgeIncomingVisibleRatio =
			BridgeIncomingProjection.VisibleRatio;
	}
	else
	{
		Sample.BridgeOutgoingTarget = TEXT("None");
		Sample.BridgeIncomingTarget = TEXT("None");
	}
	Sample.CameraWorld = View.Location;
	Sample.CameraRotation = View.Rotation;
	Sample.CameraToBirdCM = IsValid(Bird)
		? FVector::Distance(View.Location, BirdCenter)
		: 0.0;
	Sample.CameraToTargetCM = FVector::Distance(View.Location, TargetCenter);
	Sample.FovDegrees = View.FOV;
	Sample.FormationExpectedSpacingCM =
		InteractionSystem->GetFinaleFormationSpacingCM();
	Sample.bFormationFullyDeployed =
		InteractionSystem->IsFinaleFormationFullyDeployed();
	Sample.bFormationPrimaryAnchored = FormationBirds.Num()
		== FABTSM11FinaleCameraObservationSample::M6FormationMemberCount
		&& FormationBirds[0] == Bird;
	Sample.bFormationOrderStable = Sample.bFormationPrimaryAnchored;
	int32 PreviousFollowerBirdId = INDEX_NONE;
	for (int32 Index = 0; Index < FormationBirds.Num(); ++Index)
	{
		AABTSM25BirdCharacter* FormationBird = FormationBirds[Index];
		auto& Member = Sample.FormationMembers[Index];
		Member.BirdId = static_cast<int32>(FormationBird->GetBirdId());
		Member.ActorName = FormationBird->GetName();
		Member.World = FormationBird->GetActorLocation();
		Member.Screen = FormationProjections[Index].Screen;
		Member.DepthCM = FormationProjections[Index].DepthCM;
		Member.PixelRadius = FormationProjections[Index].PixelRadius;
		Member.VisibleRatio = FormationProjections[Index].VisibleRatio;
		if (Index > 0)
		{
			Sample.bFormationOrderStable &= PreviousFollowerBirdId == INDEX_NONE
				|| Member.BirdId > PreviousFollowerBirdId;
			PreviousFollowerBirdId = Member.BirdId;
		}
	}
	const TArray<double>& AdjacentArcSpacing =
		InteractionSystem->GetFormationAdjacentArcSpacingCM();
	for (int32 Index = 0;
		Index < Sample.FormationAdjacentArcSpacingCM.Num();
		++Index)
	{
		Sample.FormationAdjacentArcSpacingCM[Index] =
			AdjacentArcSpacing.IsValidIndex(Index)
				? AdjacentArcSpacing[Index]
				: 0.0;
	}
	if (bHasPreviousCameraObservation)
	{
		Sample.CameraPositionDeltaCM = FVector::Distance(
			PreviousObservedCameraLocation,
			View.Location);
		Sample.CameraRotationDeltaDegrees = FMath::RadiansToDegrees(
			PreviousObservedCameraRotation.Quaternion().AngularDistance(
				View.Rotation.Quaternion()));
		Sample.FovDeltaDegrees = FMath::Abs(
			View.FOV - PreviousObservedFovDegrees);
	}
	PreviousObservedCameraLocation = View.Location;
	PreviousObservedCameraRotation = View.Rotation;
	PreviousObservedFovDegrees = View.FOV;
	bHasPreviousCameraObservation = true;
	ObservationSamples.Add(MoveTemp(Sample));
	return true;
}

bool AABTSM11FinaleCameraCaptureRunner::WriteObservationCsv()
{
	if (ObservationSamples.Num() != CapturedFrameCount
		|| ObservationSamples.IsEmpty())
	{
		FailureReason = TEXT("CameraObservationFrameCountMismatch");
		return false;
	}
	FString Csv = TEXT(
		"schemaVersion,frameIndex,captureSeconds,playbackSeconds,interactionState,environmentStage,environmentProfile,stage,currentTarget,framingTarget,stageReason,endpointAuthority,stageProgress,stageDurationSeconds,"
		"shotPhase,shotReason,shotProgress,shotDurationSeconds,shotEndSlope,"
		"directorMode,directorM2FrozenEnabled,directorM3FrozenEnabled,directorBlendAlpha,"
		"birdWorldX,birdWorldY,birdWorldZ,birdScreenX,birdScreenY,birdDepthCM,birdPixelRadius,birdVisibleRatio,"
		"targetWorldX,targetWorldY,targetWorldZ,targetScreenX,targetScreenY,targetDepthCM,targetPixelRadius,targetVisibleRatio,"
		"bridgeOutgoingTarget,bridgeOutgoingScreenX,bridgeOutgoingScreenY,bridgeOutgoingPixelRadius,bridgeOutgoingVisibleRatio,"
		"bridgeIncomingTarget,bridgeIncomingScreenX,bridgeIncomingScreenY,bridgeIncomingPixelRadius,bridgeIncomingVisibleRatio,"
		"cameraWorldX,cameraWorldY,cameraWorldZ,cameraPitch,cameraYaw,cameraRoll,cameraToBirdCM,cameraToTargetCM,fovDegrees,"
		"cameraPositionDeltaCM,cameraRotationDeltaDegrees,fovDeltaDegrees,"
		"formation0BirdId,formation0Actor,formation0WorldX,formation0WorldY,formation0WorldZ,formation0ScreenX,formation0ScreenY,formation0DepthCM,formation0PixelRadius,formation0VisibleRatio,"
		"formation1BirdId,formation1Actor,formation1WorldX,formation1WorldY,formation1WorldZ,formation1ScreenX,formation1ScreenY,formation1DepthCM,formation1PixelRadius,formation1VisibleRatio,"
		"formation2BirdId,formation2Actor,formation2WorldX,formation2WorldY,formation2WorldZ,formation2ScreenX,formation2ScreenY,formation2DepthCM,formation2PixelRadius,formation2VisibleRatio,"
		"formation3BirdId,formation3Actor,formation3WorldX,formation3WorldY,formation3WorldZ,formation3ScreenX,formation3ScreenY,formation3DepthCM,formation3PixelRadius,formation3VisibleRatio,"
		"formationSpacing01CM,formationSpacing12CM,formationSpacing23CM,formationExpectedSpacingCM,formationOrderStable,formationPrimaryAnchored,formationFullyDeployed\n");
	Csv.Reserve(ObservationSamples.Num() * 1280);
	for (const FABTSM11FinaleCameraObservationSample& Sample
		: ObservationSamples)
	{
		Csv += FString::Printf(
			TEXT("9,%d,%.9f,%.9f,%s,%s,%s,%s,%s,%s,%s,%s,%.9f,%.9f,%s,%s,%.9f,%.9f,%.9f,%s,%d,%d,%.9f,")
			TEXT("%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.9f,")
			TEXT("%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.9f,")
			TEXT("%s,%.6f,%.6f,%.6f,%.9f,%s,%.6f,%.6f,%.6f,%.9f,")
			TEXT("%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,")
			TEXT("%.6f,%.6f,%.6f"),
			Sample.FrameIndex,
			Sample.CaptureSeconds,
			Sample.PlaybackSeconds,
			*Sample.InteractionState,
			*Sample.EnvironmentStage,
			*Sample.EnvironmentProfile,
			*Sample.Stage,
			*Sample.CurrentTarget,
			*Sample.FramingTarget,
			*Sample.StageReason,
			*Sample.EndpointAuthority,
			Sample.StageProgress,
			Sample.StageDurationSeconds,
			*Sample.ShotPhase,
			*Sample.ShotReason,
			Sample.ShotProgress,
			Sample.ShotDurationSeconds,
			Sample.ShotEndSlope,
			*Sample.DirectorMode,
			Sample.bDirectorM2FrozenEnabled ? 1 : 0,
			Sample.bDirectorM3FrozenEnabled ? 1 : 0,
			Sample.DirectorBlendAlpha,
			Sample.BirdWorld.X,
			Sample.BirdWorld.Y,
			Sample.BirdWorld.Z,
			Sample.BirdScreen.X,
			Sample.BirdScreen.Y,
			Sample.BirdDepthCM,
			Sample.BirdPixelRadius,
			Sample.BirdVisibleRatio,
			Sample.TargetWorld.X,
			Sample.TargetWorld.Y,
			Sample.TargetWorld.Z,
			Sample.TargetScreen.X,
			Sample.TargetScreen.Y,
			Sample.TargetDepthCM,
			Sample.TargetPixelRadius,
			Sample.TargetVisibleRatio,
			*Sample.BridgeOutgoingTarget,
			Sample.BridgeOutgoingScreen.X,
			Sample.BridgeOutgoingScreen.Y,
			Sample.BridgeOutgoingPixelRadius,
			Sample.BridgeOutgoingVisibleRatio,
			*Sample.BridgeIncomingTarget,
			Sample.BridgeIncomingScreen.X,
			Sample.BridgeIncomingScreen.Y,
			Sample.BridgeIncomingPixelRadius,
			Sample.BridgeIncomingVisibleRatio,
			Sample.CameraWorld.X,
			Sample.CameraWorld.Y,
			Sample.CameraWorld.Z,
			Sample.CameraRotation.Pitch,
			Sample.CameraRotation.Yaw,
			Sample.CameraRotation.Roll,
			Sample.CameraToBirdCM,
			Sample.CameraToTargetCM,
			Sample.FovDegrees,
			Sample.CameraPositionDeltaCM,
			Sample.CameraRotationDeltaDegrees,
			Sample.FovDeltaDegrees);
		for (const auto& Member : Sample.FormationMembers)
		{
			Csv += FString::Printf(
				TEXT(",%d,%s,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.9f"),
				Member.BirdId,
				*Member.ActorName,
				Member.World.X,
				Member.World.Y,
				Member.World.Z,
				Member.Screen.X,
				Member.Screen.Y,
				Member.DepthCM,
				Member.PixelRadius,
				Member.VisibleRatio);
		}
		Csv += FString::Printf(
			TEXT(",%.6f,%.6f,%.6f,%.6f,%d,%d,%d\n"),
			Sample.FormationAdjacentArcSpacingCM[0],
			Sample.FormationAdjacentArcSpacingCM[1],
			Sample.FormationAdjacentArcSpacingCM[2],
			Sample.FormationExpectedSpacingCM,
			Sample.bFormationOrderStable ? 1 : 0,
			Sample.bFormationPrimaryAnchored ? 1 : 0,
			Sample.bFormationFullyDeployed ? 1 : 0);
	}
	bObservationCsvWritten = FFileHelper::SaveStringToFile(
		Csv,
		*Config.GetObservationCsvPath(),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	if (!bObservationCsvWritten)
	{
		FailureReason = TEXT("CameraObservationCsvWriteFailed");
	}
	return bObservationCsvWritten;
}

bool AABTSM11FinaleCameraCaptureRunner::MuxCapturedFramesToAvi()
{
	using namespace ABTSM11FinaleCameraCaptureRunnerPrivate;
	TArray<FString> FrameNames;
	IFileManager::Get().FindFiles(
		FrameNames,
		*Config.GetFrameWildcard(),
		true,
		false);
	FrameNames.Sort();
	if (FrameNames.Num() != CapturedFrameCount
		|| CapturedFrameCount <= 0
		|| CapturedFrameSize.X <= 0
		|| CapturedFrameSize.Y <= 0)
	{
		FailureReason = TEXT("FrameSequenceCountMismatchBeforeMux");
		return false;
	}

	uint32 MaxFrameBytes = 0;
	for (int32 Index = 0; Index < FrameNames.Num(); ++Index)
	{
		const FString ExpectedName = FString::Printf(
			TEXT("%s.%06d.jpg"),
			*Config.MovieName,
			Index);
		const int64 Size = IFileManager::Get().FileSize(
			*FPaths::Combine(Config.OutputDirectory, FrameNames[Index]));
		if (FrameNames[Index] != ExpectedName
			|| Size <= 4
			|| Size > MAX_uint32)
		{
			FailureReason = TEXT("FrameSequenceIdentityInvalidBeforeMux");
			return false;
		}
		MaxFrameBytes = FMath::Max(MaxFrameBytes, static_cast<uint32>(Size));
	}

	const FString VideoPath = Config.GetExpectedVideoPath();
	IPlatformFile& PlatformFile =
		FPlatformFileManager::Get().GetPlatformFile();
	TUniquePtr<IFileHandle> File(PlatformFile.OpenWrite(*VideoPath));
	if (!File)
	{
		FailureReason = TEXT("AviOpenWriteFailed");
		return false;
	}
	const auto FailMux = [this, &File, &PlatformFile, &VideoPath](
		const TCHAR* Reason)
	{
		FailureReason = Reason;
		File.Reset();
		PlatformFile.DeleteFile(*VideoPath);
		return false;
	};

	const int64 Riff = BeginChunk(*File, "RIFF");
	if (Riff < 0 || !WriteFourCC(*File, "AVI "))
	{
		return FailMux(TEXT("AviRiffHeaderWriteFailed"));
	}
	const int64 HeaderList = BeginChunk(*File, "LIST");
	if (HeaderList < 0 || !WriteFourCC(*File, "hdrl"))
	{
		return FailMux(TEXT("AviHeaderListWriteFailed"));
	}
	const int64 MainHeader = BeginChunk(*File, "avih");
	const uint32 TotalFrames = static_cast<uint32>(CapturedFrameCount);
	const uint32 Width = static_cast<uint32>(CapturedFrameSize.X);
	const uint32 Height = static_cast<uint32>(CapturedFrameSize.Y);
	if (MainHeader < 0
		|| !WriteUInt32(*File, static_cast<uint32>(FMath::RoundToInt(1000000.0 / Config.FrameRate)))
		|| !WriteUInt32(*File, MaxFrameBytes * static_cast<uint32>(Config.FrameRate))
		|| !WriteUInt32(*File, 0) || !WriteUInt32(*File, 0x10)
		|| !WriteUInt32(*File, TotalFrames) || !WriteUInt32(*File, 0)
		|| !WriteUInt32(*File, 1) || !WriteUInt32(*File, MaxFrameBytes)
		|| !WriteUInt32(*File, Width) || !WriteUInt32(*File, Height)
		|| !WriteUInt32(*File, 0) || !WriteUInt32(*File, 0)
		|| !WriteUInt32(*File, 0) || !WriteUInt32(*File, 0)
		|| !EndChunk(*File, MainHeader))
	{
		return FailMux(TEXT("AviMainHeaderWriteFailed"));
	}
	const int64 StreamList = BeginChunk(*File, "LIST");
	const int64 StreamHeader = StreamList >= 0
		&& WriteFourCC(*File, "strl")
		? BeginChunk(*File, "strh")
		: INDEX_NONE;
	if (StreamHeader < 0
		|| !WriteFourCC(*File, "vids") || !WriteFourCC(*File, "MJPG")
		|| !WriteUInt32(*File, 0) || !WriteUInt16(*File, 0)
		|| !WriteUInt16(*File, 0) || !WriteUInt32(*File, 0)
		|| !WriteUInt32(*File, 1) || !WriteUInt32(*File, Config.FrameRate)
		|| !WriteUInt32(*File, 0) || !WriteUInt32(*File, TotalFrames)
		|| !WriteUInt32(*File, MaxFrameBytes) || !WriteUInt32(*File, MAX_uint32)
		|| !WriteUInt32(*File, 0) || !WriteUInt16(*File, 0)
		|| !WriteUInt16(*File, 0) || !WriteUInt16(*File, static_cast<uint16>(Width))
		|| !WriteUInt16(*File, static_cast<uint16>(Height))
		|| !EndChunk(*File, StreamHeader))
	{
		return FailMux(TEXT("AviStreamHeaderWriteFailed"));
	}
	const int64 StreamFormat = BeginChunk(*File, "strf");
	if (StreamFormat < 0
		|| !WriteUInt32(*File, 40) || !WriteUInt32(*File, Width)
		|| !WriteUInt32(*File, Height) || !WriteUInt16(*File, 1)
		|| !WriteUInt16(*File, 24) || !WriteFourCC(*File, "MJPG")
		|| !WriteUInt32(*File, Width * Height * 3)
		|| !WriteUInt32(*File, 0) || !WriteUInt32(*File, 0)
		|| !WriteUInt32(*File, 0) || !WriteUInt32(*File, 0)
		|| !EndChunk(*File, StreamFormat)
		|| !EndChunk(*File, StreamList)
		|| !EndChunk(*File, HeaderList))
	{
		return FailMux(TEXT("AviStreamFormatWriteFailed"));
	}

	const int64 MovieList = BeginChunk(*File, "LIST");
	if (MovieList < 0 || !WriteFourCC(*File, "movi"))
	{
		return FailMux(TEXT("AviMovieListWriteFailed"));
	}
	const int64 MovieTypeOffset = MovieList + 4;
	TArray<uint32> ChunkOffsets;
	TArray<uint32> ChunkSizes;
	ChunkOffsets.Reserve(CapturedFrameCount);
	ChunkSizes.Reserve(CapturedFrameCount);
	for (const FString& FrameName : FrameNames)
	{
		TArray<uint8> FrameBytes;
		if (!FFileHelper::LoadFileToArray(
			FrameBytes,
			*FPaths::Combine(Config.OutputDirectory, FrameName))
			|| FrameBytes.Num() < 4
			|| FrameBytes[0] != 0xFF || FrameBytes[1] != 0xD8
			|| FrameBytes[FrameBytes.Num() - 2] != 0xFF
			|| FrameBytes[FrameBytes.Num() - 1] != 0xD9)
		{
			return FailMux(TEXT("AviJpegFrameReadFailed"));
		}
		const int64 ChunkStart = File->Tell();
		const int64 FrameChunk = BeginChunk(*File, "00dc");
		if (FrameChunk < 0
			|| !WriteBytes(*File, FrameBytes.GetData(), FrameBytes.Num())
			|| !EndChunk(*File, FrameChunk))
		{
			return FailMux(TEXT("AviJpegFrameWriteFailed"));
		}
		ChunkOffsets.Add(static_cast<uint32>(ChunkStart - MovieTypeOffset));
		ChunkSizes.Add(static_cast<uint32>(FrameBytes.Num()));
	}
	if (!EndChunk(*File, MovieList))
	{
		return FailMux(TEXT("AviMovieListFinalizeFailed"));
	}
	const int64 IndexChunk = BeginChunk(*File, "idx1");
	for (int32 Index = 0; Index < ChunkOffsets.Num(); ++Index)
	{
		if (!WriteFourCC(*File, "00dc") || !WriteUInt32(*File, 0x10)
			|| !WriteUInt32(*File, ChunkOffsets[Index])
			|| !WriteUInt32(*File, ChunkSizes[Index]))
		{
			return FailMux(TEXT("AviIndexWriteFailed"));
		}
	}
	if (IndexChunk < 0 || !EndChunk(*File, IndexChunk) || !EndChunk(*File, Riff))
	{
		return FailMux(TEXT("AviFinalizeFailed"));
	}
	File.Reset();
	if (IFileManager::Get().FileSize(*VideoPath) <= 4096)
	{
		return FailMux(TEXT("AviFinalSizeInvalid"));
	}
	return true;
}

void AABTSM11FinaleCameraCaptureRunner::StopRecording()
{
	if (!bMovieCaptureStarted || bMovieCaptureStopped)
	{
		Finish(false, TEXT("StopMovieCaptureWithoutActiveCapture"));
		return;
	}
	EndUtc = FDateTime::UtcNow();
	bMovieCaptureStopped = true;
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M11][CameraCapture] RecordingStopped PendingSuccess=%d Reason=%s"),
		bPendingFinalizeSuccess ? 1 : 0,
		*PendingFinalizeReason);
	if (!WriteObservationCsv())
	{
		Finish(false, FailureReason);
		return;
	}
	if (!Config.bTelemetryOnly && !MuxCapturedFramesToAvi())
	{
		Finish(false, FailureReason);
		return;
	}
	Finish(bPendingFinalizeSuccess, PendingFinalizeReason);
}

void AABTSM11FinaleCameraCaptureRunner::Finish(
	const bool bSuccess,
	const FString& Reason)
{
	if (Phase == EABTSM11FinaleCameraCapturePhase::Terminal)
	{
		return;
	}
	EndUtc = FDateTime::UtcNow();
	FailureReason = bSuccess ? FString() : Reason;
	Phase = EABTSM11FinaleCameraCapturePhase::Terminal;
	SetActorTickEnabled(false);
	const bool bManifestWritten = WriteManifest(bSuccess, Reason);
	if (bStylizedViewRegistered && IsValid(RecordingCapture))
	{
		FABTSStylizedSceneCaptureRegistry::Unregister(*RecordingCapture);
		bStylizedViewRegistered = false;
	}

	const FString Summary = FString::Printf(
		TEXT("[ABTS][M11][CameraCapture] Complete Success=%d Rank=%d Stylized=%d TelemetryOnly=%d HudScreenshot=%d State=%s Frames=%d Manifest=%d Reason=%s Artifact=%s"),
		bSuccess ? 1 : 0,
		Config.CandidateRank,
		Config.bStylized ? 1 : 0,
		Config.bTelemetryOnly ? 1 : 0,
		Config.bHudScreenshotOnly ? 1 : 0,
		IsValid(InteractionSystem)
			? ABTSM11FinaleCameraCaptureRunnerPrivate::StateLabel(
				InteractionSystem->GetInteractionState())
			: TEXT("Unavailable"),
		CapturedFrameCount,
		bManifestWritten ? 1 : 0,
		*Reason,
		Config.bHudScreenshotOnly
			? *Config.GetHudScreenshotPath()
			: Config.bTelemetryOnly
				? TEXT("None")
				: *Config.GetExpectedVideoPath());
	if (bSuccess && bManifestWritten)
	{
		UE_LOG(LogABTSRuntime, Log, TEXT("%s"), *Summary);
	}
	else
	{
		UE_LOG(LogABTSRuntime, Error, TEXT("%s"), *Summary);
	}

	if (Config.bAutoExit)
	{
		FPlatformMisc::RequestExitWithStatus(
			false,
			bSuccess && bManifestWritten ? 0 : 2);
	}
}

bool AABTSM11FinaleCameraCaptureRunner::WriteManifest(
	const bool bSuccess,
	const FString& Reason) const
{
	using namespace ABTSM11FinaleCameraCaptureRunnerPrivate;
	if (!IFileManager::Get().MakeDirectory(
		*Config.OutputDirectory,
		true))
	{
		return false;
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(
		TEXT("contractVersion"),
		FABTSM11FinaleCameraCaptureConfig::ContractVersion);
	Root->SetStringField(
		TEXT("status"),
		bSuccess
			? TEXT("Complete")
			: TEXT("Failed"));
	Root->SetStringField(
		TEXT("captureFinalization"),
		Config.bHudScreenshotOnly
			? TEXT("GameViewportHudScreenshotComplete")
			: Config.bTelemetryOnly
			? TEXT("TelemetryCsvAndManifestComplete")
			: TEXT("SynchronousFramesAndAviMuxComplete"));
	Root->SetStringField(TEXT("reason"), Reason);
	Root->SetStringField(TEXT("startUtc"), StartUtc.ToIso8601());
	Root->SetStringField(TEXT("endUtc"), EndUtc.ToIso8601());
	Root->SetNumberField(TEXT("rank"), Config.CandidateRank);
	Root->SetStringField(
		TEXT("launchInputMode"),
		Config.bCustomLaunchInput ? TEXT("CustomF4") : TEXT("Nominal"));
	const FABTSM11FinaleLaunchInput ManifestInput = Config.bCustomLaunchInput
		? Config.CustomLaunchInput
		: IsValid(FinaleSystem)
			? FinaleSystem->GetLayoutPreset().NominalInput
			: FABTSM11FinaleLaunchInput();
	Root->SetNumberField(TEXT("launchYawDegrees"), ManifestInput.YawDegrees);
	Root->SetNumberField(TEXT("launchPitchDegrees"), ManifestInput.PitchDegrees);
	Root->SetNumberField(TEXT("launchPower"), ManifestInput.Power);
	Root->SetStringField(
		TEXT("authority"),
		Config.CandidateRank == 0 ? TEXT("Certified") : TEXT("UNCERTIFIED"));
	Root->SetBoolField(TEXT("stylizedEnabled"), Config.bStylized);
	Root->SetBoolField(TEXT("telemetryOnly"), Config.bTelemetryOnly);
	Root->SetBoolField(TEXT("hudScreenshotOnly"), Config.bHudScreenshotOnly);
	Root->SetStringField(TEXT("hudScreenshotPath"),
		Config.bHudScreenshotOnly ? Config.GetHudScreenshotPath() : TEXT(""));
	Root->SetBoolField(
		TEXT("visualRecordingProduced"),
		!Config.bTelemetryOnly && !Config.bHudScreenshotOnly);
	Root->SetBoolField(TEXT("cameraDirectorM2Requested"), Config.bDirectorM2);
	Root->SetBoolField(TEXT("cameraDirectorM3Requested"), Config.bDirectorM3);
	Root->SetBoolField(
		TEXT("mirrorMainWorldEnvironment"),
		Config.bMirrorMainWorldEnvironment);
	Root->SetBoolField(
		TEXT("m7AdaptiveShotCompression"),
		IsValid(InteractionSystem)
			&& InteractionSystem->GetReleasedCameraShotPlan()
				.bUsesAdaptiveCompression);
	Root->SetStringField(
		TEXT("m7CameraPlanTrajectoryHash"),
		IsValid(InteractionSystem)
			? Hex64(InteractionSystem->GetReleasedCameraShotPlan()
				.ReleasedTrajectoryHash)
			: Hex64(0));
	Root->SetStringField(
		TEXT("cameraDirectorMode"),
		Config.bDirectorM3
			? TEXT("M3MultiAssist")
			: Config.bDirectorM2 ? TEXT("M2Assist1") : TEXT("Legacy"));
	Root->SetBoolField(
		TEXT("captureFixtureCreated"),
		bCaptureFixtureCreated);
	Root->SetStringField(
		TEXT("stylizedProfile"),
		Config.bMirrorMainWorldEnvironment
			? TEXT("LiveMainWorldStage")
			: TEXT("FinaleSpace"));
	Root->SetStringField(
		TEXT("stylizedViewClass"),
		Config.bMirrorMainWorldEnvironment
			? TEXT("FinaleGameplayMirrorCapture")
			: TEXT("FinaleCinematicCapture"));
	const FABTSStylizedViewPolicy CaptureViewPolicy =
		FABTSStylizedRenderingContract::ResolveViewPolicy(
			ABTSM11FinaleCameraCaptureRunnerPrivate::ResolveCaptureViewClass(Config),
			FABTSStylizedRenderingControl::GetProfile());
	Root->SetBoolField(
		TEXT("stylizedViewRegistered"),
		bStylizedViewRegistered);
	Root->SetBoolField(
		TEXT("stylizedViewPolicyValid"),
		CaptureViewPolicy.IsValid());
	Root->SetBoolField(
		TEXT("stylizedRuntimeStateMaintained"),
		bStylizedRuntimeStateMaintained);
	Root->SetNumberField(
		TEXT("stylizedRuntimeStateFailureFrame"),
		StylizedRuntimeStateFailureFrame);
	Root->SetBoolField(
		TEXT("stylizedTonePolicyEnabled"),
		Config.bStylized && CaptureViewPolicy.bApplyTone);
	Root->SetBoolField(
		TEXT("stylizedOutlinePolicyEnabled"),
		Config.bStylized && CaptureViewPolicy.bApplyOutline);
	Root->SetBoolField(
		TEXT("stylizedSelectiveStencilPolicyEnabled"),
		Config.bStylized && CaptureViewPolicy.bAllowSelectiveStencil);
	Root->SetStringField(
		TEXT("captureProtocol"),
		Config.bHudScreenshotOnly
			? TEXT("GameViewportPNGWithHUD")
			: Config.bTelemetryOnly
			? TEXT("RendererIndependentTelemetry")
			: TEXT("SceneCaptureJPG+MJPEGAVI"));
	Root->SetNumberField(
		TEXT("stylizedImplementationVersion"),
		FABTSStylizedRenderingControl::GetImplementationVersion());
	Root->SetStringField(
		TEXT("videoPath"),
		Config.bTelemetryOnly || Config.bHudScreenshotOnly
			? FString() : Config.GetExpectedVideoPath());
	Root->SetStringField(
		TEXT("frameWildcard"),
		Config.bTelemetryOnly || Config.bHudScreenshotOnly
			? FString() : Config.GetFrameWildcard());
	Root->SetStringField(
		TEXT("cameraObservationPath"),
		Config.GetObservationCsvPath());
	Root->SetNumberField(TEXT("cameraObservationSchemaVersion"), 9);
	Root->SetNumberField(
		TEXT("cameraObservationCount"),
		ObservationSamples.Num());
	Root->SetBoolField(
		TEXT("cameraObservationCsvWritten"),
		bObservationCsvWritten);
	Root->SetStringField(
		TEXT("cameraObservationAssessment"),
		Config.bHudScreenshotOnly
			? TEXT("NotApplicableToHudScreenshot")
			: Config.bTelemetryOnly
			? TEXT("NumericalOrthogonalityEvidence")
			: TEXT("OfflineDiagnosticRequired"));
	const FM5OrthogonalityDigests M5Digests =
		ComputeM5OrthogonalityDigests(ObservationSamples);
	Root->SetStringField(
		TEXT("m5EvidenceMode"),
		Config.bHudScreenshotOnly
			? TEXT("HudVisualDiagnostic")
			: Config.bTelemetryOnly
			? TEXT("NumericalTelemetry")
			: Config.bStylized
				? TEXT("StylizedVisualRecording")
				: TEXT("LegacyVisualDiagnostic"));
	Root->SetBoolField(
		TEXT("m5VisualAcceptanceEligible"),
		!Config.bTelemetryOnly && !Config.bHudScreenshotOnly && Config.bStylized);
	Root->SetBoolField(
		TEXT("m5StylizedExcludedFromNumericalHashes"),
		true);
	Root->SetStringField(
		TEXT("m5DigestQuantization"),
		TEXT("StageTimeProgress=1e-9;CameraCM=1e-3;RotationFov=1e-6"));
	Root->SetStringField(
		TEXT("m5StageSequenceHash"),
		Hex64(M5Digests.StageSequenceHash));
	Root->SetStringField(
		TEXT("m5CameraNumericsHash"),
		Hex64(M5Digests.CameraNumericsHash));
	Root->SetNumberField(
		TEXT("m5HashedFlightFrameCount"),
		M5Digests.FlightFrameCount);
	Root->SetBoolField(TEXT("m5NumericalDigestValid"), M5Digests.IsValid());
	Root->SetNumberField(
		TEXT("frameCountObserved"),
		CapturedFrameCount);
	Root->SetNumberField(TEXT("frameRate"), Config.FrameRate);
	Root->SetNumberField(TEXT("frameWidth"), CapturedFrameSize.X);
	Root->SetNumberField(TEXT("frameHeight"), CapturedFrameSize.Y);
	Root->SetNumberField(TEXT("jpegQuality"), Config.JpegQuality);
	Root->SetBoolField(TEXT("videoPostprocessRequired"), false);
	Root->SetNumberField(
		TEXT("videoBytesObserved"),
		Config.bTelemetryOnly || Config.bHudScreenshotOnly
			? 0.0
			: static_cast<double>(IFileManager::Get().FileSize(
				*Config.GetExpectedVideoPath())));

	int32 FlightFrameCount = 0;
	int32 BirdLostFrameCount = 0;
	int32 TargetLostFrameCount = 0;
	int32 EmptyCompositionFrameCount = 0;
	int32 CameraPositionJumpCount = 0;
	int32 CameraRotationJumpCount = 0;
	int32 FovJumpCount = 0;
	int32 StageTransitionCount = 0;
	int32 DirectorM2BlendFrameCount = 0;
	int32 DirectorM2LeakFrameCount = 0;
	int32 M4TerminalFrameCount = 0;
	int32 M4AcquireFrameCount = 0;
	int32 M4AcquireNoTargetFrameCount = 0;
	int32 M4BirdLostFrameCount = 0;
	int32 M4TargetLostFrameCount = 0;
	int32 M4EndpointMissingFrameCount = 0;
	int32 M4PositionJumpCount = 0;
	int32 M4RotationJumpCount = 0;
	int32 M4FovJumpCount = 0;
	int32 M6FormationLostFrameCount = 0;
	int32 M6FormationOrderMismatchFrameCount = 0;
	int32 M6FormationPrimaryMismatchFrameCount = 0;
	int32 M6FormationSpacingMismatchFrameCount = 0;
	int32 M6FormationFullyDeployedFrameCount = 0;
	double M6MinimumAdjacentSpacingCM = TNumericLimits<double>::Max();
	double M4FinalBirdToUFODistanceCM = TNumericLimits<double>::Max();
	int32 CurrentBirdLostRun = 0;
	int32 CurrentTargetLostRun = 0;
	int32 CurrentEmptyRun = 0;
	int32 LongestBirdLostRun = 0;
	int32 LongestTargetLostRun = 0;
	int32 LongestEmptyRun = 0;
	double MaximumCameraPositionDeltaCM = 0.0;
	double MaximumCameraRotationDeltaDegrees = 0.0;
	double MaximumFovDeltaDegrees = 0.0;
	double MaximumDirectorM2BlendAlpha = 0.0;
	FString PreviousStage;
	FString PreviousTarget;
	for (const FABTSM11FinaleCameraObservationSample& Sample
		: ObservationSamples)
	{
		const bool bFlightFrame = Sample.InteractionState == TEXT("Launched")
			|| Sample.InteractionState == TEXT("TargetHit");
		if (!bFlightFrame)
		{
			continue;
		}
		++FlightFrameCount;
		const bool bDirectorBlended =
			Sample.DirectorBlendAlpha > UE_DOUBLE_SMALL_NUMBER;
		const bool bM2Window = Sample.CurrentTarget == TEXT("Assist1")
			&& (Sample.Stage == TEXT("CruiseToBody")
				|| Sample.Stage == TEXT("Approach")
				|| Sample.Stage == TEXT("Periapsis"));
		const bool bM3Window =
			(Sample.FramingTarget.StartsWith(TEXT("Assist"))
				&& (Sample.Stage == TEXT("CruiseToBody")
				|| Sample.Stage == TEXT("Handoff")
				|| Sample.Stage == TEXT("Approach")
				|| Sample.Stage == TEXT("Periapsis")))
			|| Sample.Stage == TEXT("FinalApproach")
			|| Sample.Stage == TEXT("Terminal");
		const bool bExpectedDirectorWindow = Config.bDirectorM3
			? bM3Window
			: bM2Window;
		DirectorM2BlendFrameCount += bDirectorBlended ? 1 : 0;
		DirectorM2LeakFrameCount +=
			bDirectorBlended && !bExpectedDirectorWindow ? 1 : 0;
		MaximumDirectorM2BlendAlpha = FMath::Max(
			MaximumDirectorM2BlendAlpha,
			Sample.DirectorBlendAlpha);
		const bool bBirdLost = Sample.BirdVisibleRatio < 0.5;
		const bool bTargetLost = Sample.TargetVisibleRatio <= 0.01
			|| Sample.TargetPixelRadius < 4.0;
		const bool bEmpty = bBirdLost && bTargetLost;
		M6FormationOrderMismatchFrameCount +=
			Sample.bFormationOrderStable ? 0 : 1;
		M6FormationPrimaryMismatchFrameCount +=
			Sample.bFormationPrimaryAnchored ? 0 : 1;
		bool bAnyFormationBirdLost = false;
		for (const auto& Member : Sample.FormationMembers)
		{
			bAnyFormationBirdLost |= Member.VisibleRatio < 0.5;
		}
		M6FormationLostFrameCount += bAnyFormationBirdLost ? 1 : 0;
		if (Sample.bFormationFullyDeployed)
		{
			++M6FormationFullyDeployedFrameCount;
			for (const double SpacingCM
				: Sample.FormationAdjacentArcSpacingCM)
			{
				M6MinimumAdjacentSpacingCM = FMath::Min(
					M6MinimumAdjacentSpacingCM,
					SpacingCM);
				M6FormationSpacingMismatchFrameCount +=
					SpacingCM + 1.0e-3
						< Sample.FormationExpectedSpacingCM * 0.95
					? 1 : 0;
			}
		}
		const bool bM4TerminalFrame =
			Sample.Stage == TEXT("FinalApproach")
			|| Sample.Stage == TEXT("Terminal");
		if (Sample.ShotPhase == TEXT("TerminalAcquire"))
		{
			++M4AcquireFrameCount;
			const bool bOutgoingVisible =
				Sample.BridgeOutgoingVisibleRatio > 0.01
				&& Sample.BridgeOutgoingPixelRadius >= 4.0;
			const bool bIncomingVisible =
				Sample.BridgeIncomingVisibleRatio > 0.01
				&& Sample.BridgeIncomingPixelRadius >= 4.0;
			M4AcquireNoTargetFrameCount +=
				!bOutgoingVisible && !bIncomingVisible ? 1 : 0;
		}
		if (bM4TerminalFrame)
		{
			const bool bM4BirdUnreadable =
				bBirdLost || Sample.BirdPixelRadius < 1.0;
			++M4TerminalFrameCount;
			M4BirdLostFrameCount += bM4BirdUnreadable ? 1 : 0;
			M4TargetLostFrameCount += bTargetLost ? 1 : 0;
			M4EndpointMissingFrameCount +=
				Sample.EndpointAuthority == TEXT("None") ? 1 : 0;
			M4PositionJumpCount +=
				Sample.CameraPositionDeltaCM > 5000.0 ? 1 : 0;
			M4RotationJumpCount +=
				Sample.CameraRotationDeltaDegrees > 15.0 ? 1 : 0;
			M4FovJumpCount += Sample.FovDeltaDegrees > 2.0 ? 1 : 0;
			M4FinalBirdToUFODistanceCM =
				(Sample.BirdWorld - Sample.TargetWorld).Length();
		}
		BirdLostFrameCount += bBirdLost ? 1 : 0;
		TargetLostFrameCount += bTargetLost ? 1 : 0;
		EmptyCompositionFrameCount += bEmpty ? 1 : 0;
		CurrentBirdLostRun = bBirdLost ? CurrentBirdLostRun + 1 : 0;
		CurrentTargetLostRun = bTargetLost ? CurrentTargetLostRun + 1 : 0;
		CurrentEmptyRun = bEmpty ? CurrentEmptyRun + 1 : 0;
		LongestBirdLostRun = FMath::Max(
			LongestBirdLostRun, CurrentBirdLostRun);
		LongestTargetLostRun = FMath::Max(
			LongestTargetLostRun, CurrentTargetLostRun);
		LongestEmptyRun = FMath::Max(LongestEmptyRun, CurrentEmptyRun);
		CameraPositionJumpCount +=
			Sample.CameraPositionDeltaCM > 5000.0 ? 1 : 0;
		CameraRotationJumpCount +=
			Sample.CameraRotationDeltaDegrees > 15.0 ? 1 : 0;
		FovJumpCount += Sample.FovDeltaDegrees > 2.0 ? 1 : 0;
		MaximumCameraPositionDeltaCM = FMath::Max(
			MaximumCameraPositionDeltaCM,
			Sample.CameraPositionDeltaCM);
		MaximumCameraRotationDeltaDegrees = FMath::Max(
			MaximumCameraRotationDeltaDegrees,
			Sample.CameraRotationDeltaDegrees);
		MaximumFovDeltaDegrees = FMath::Max(
			MaximumFovDeltaDegrees,
			Sample.FovDeltaDegrees);
		if (!PreviousStage.IsEmpty()
			&& (Sample.Stage != PreviousStage
				|| Sample.CurrentTarget != PreviousTarget))
		{
			++StageTransitionCount;
		}
		PreviousStage = Sample.Stage;
		PreviousTarget = Sample.CurrentTarget;
	}
	Root->SetNumberField(TEXT("cameraObservationFlightFrames"), FlightFrameCount);
	Root->SetNumberField(TEXT("cameraObservationBirdLostFrames"), BirdLostFrameCount);
	Root->SetNumberField(TEXT("cameraObservationTargetLostFrames"), TargetLostFrameCount);
	Root->SetNumberField(
		TEXT("cameraObservationEmptyCompositionFrames"),
		EmptyCompositionFrameCount);
	Root->SetNumberField(
		TEXT("cameraObservationLongestBirdLostRun"),
		LongestBirdLostRun);
	Root->SetNumberField(
		TEXT("cameraObservationLongestTargetLostRun"),
		LongestTargetLostRun);
	Root->SetNumberField(
		TEXT("cameraObservationLongestEmptyRun"),
		LongestEmptyRun);
	Root->SetNumberField(
		TEXT("cameraObservationCameraPositionJumpFrames"),
		CameraPositionJumpCount);
	Root->SetNumberField(
		TEXT("cameraObservationCameraRotationJumpFrames"),
		CameraRotationJumpCount);
	Root->SetNumberField(
		TEXT("cameraObservationFovJumpFrames"),
		FovJumpCount);
	Root->SetNumberField(
		TEXT("cameraObservationStageTransitions"),
		StageTransitionCount);
	Root->SetNumberField(
		TEXT("cameraDirectorM2BlendFrames"),
		Config.bDirectorM2 ? DirectorM2BlendFrameCount : 0);
	Root->SetNumberField(
		TEXT("cameraDirectorM2LeakFrames"),
		Config.bDirectorM2 ? DirectorM2LeakFrameCount : 0);
	Root->SetNumberField(
		TEXT("cameraDirectorM2MaximumBlendAlpha"),
		Config.bDirectorM2 ? MaximumDirectorM2BlendAlpha : 0.0);
	Root->SetNumberField(
		TEXT("cameraDirectorM3BlendFrames"),
		Config.bDirectorM3 ? DirectorM2BlendFrameCount : 0);
	Root->SetNumberField(
		TEXT("cameraDirectorM3LeakFrames"),
		Config.bDirectorM3 ? DirectorM2LeakFrameCount : 0);
	Root->SetNumberField(
		TEXT("cameraDirectorM3MaximumBlendAlpha"),
		Config.bDirectorM3 ? MaximumDirectorM2BlendAlpha : 0.0);
	Root->SetNumberField(
		TEXT("cameraObservationMaximumPositionDeltaCM"),
		MaximumCameraPositionDeltaCM);
	Root->SetNumberField(
		TEXT("cameraObservationMaximumRotationDeltaDegrees"),
		MaximumCameraRotationDeltaDegrees);
	Root->SetNumberField(
		TEXT("cameraObservationMaximumFovDeltaDegrees"),
		MaximumFovDeltaDegrees);
	Root->SetNumberField(TEXT("m4TerminalFrameCount"), M4TerminalFrameCount);
	Root->SetNumberField(TEXT("m4AcquireFrameCount"), M4AcquireFrameCount);
	Root->SetNumberField(
		TEXT("m4AcquireNoTargetFrames"),
		M4AcquireNoTargetFrameCount);
	Root->SetNumberField(TEXT("m4BirdLostFrames"), M4BirdLostFrameCount);
	Root->SetNumberField(TEXT("m4UFOLostFrames"), M4TargetLostFrameCount);
	Root->SetNumberField(
		TEXT("m4EndpointMissingFrames"),
		M4EndpointMissingFrameCount);
	Root->SetNumberField(TEXT("m4PositionJumpFrames"), M4PositionJumpCount);
	Root->SetNumberField(TEXT("m4RotationJumpFrames"), M4RotationJumpCount);
	Root->SetNumberField(TEXT("m4FovJumpFrames"), M4FovJumpCount);
	Root->SetNumberField(
		TEXT("m6FormationLostFrames"),
		M6FormationLostFrameCount);
	Root->SetNumberField(
		TEXT("m6FormationOrderMismatchFrames"),
		M6FormationOrderMismatchFrameCount);
	Root->SetNumberField(
		TEXT("m6FormationPrimaryMismatchFrames"),
		M6FormationPrimaryMismatchFrameCount);
	Root->SetNumberField(
		TEXT("m6FormationSpacingMismatchCount"),
		M6FormationSpacingMismatchFrameCount);
	Root->SetNumberField(
		TEXT("m6FormationFullyDeployedFrames"),
		M6FormationFullyDeployedFrameCount);
	Root->SetNumberField(
		TEXT("m6FormationMinimumAdjacentSpacingCM"),
		FMath::IsFinite(M6MinimumAdjacentSpacingCM)
			? M6MinimumAdjacentSpacingCM : 0.0);
	Root->SetBoolField(
		TEXT("m6FormationPassed"),
		FlightFrameCount > 0
			&& M6FormationFullyDeployedFrameCount > 0
			&& M6FormationLostFrameCount == 0
			&& M6FormationOrderMismatchFrameCount == 0
			&& M6FormationPrimaryMismatchFrameCount == 0
			&& M6FormationSpacingMismatchFrameCount == 0);
	double M4PhysicalContactRadiusCM = 0.0;
	if (IsValid(FinaleSystem))
	{
		M4PhysicalContactRadiusCM = FinaleSystem->GetLayoutPreset()
			.CanonicalScenario.Target.GetGeometricContactRadiusCM();
	}
	const bool bM4PlanHasPhysicalContact = IsValid(InteractionSystem)
		&& InteractionSystem->GetReleasedPlaybackPlan().bPhysicalTargetHit;
	if (IsValid(FinaleSystem) && IsValid(InteractionSystem))
	{
		const FABTSM11PlaybackPlan& ContactPlan =
			InteractionSystem->GetReleasedPlaybackPlan();
		if (!ContactPlan.Points.IsEmpty())
		{
			const FABTSM110FinaleLocalFrame& Frame =
				FinaleSystem->GetFinaleFrame();
			const FVector AuthorityBirdWorld = Frame.TransformLocalPosition(
				FVector(ContactPlan.Points.Last().PositionCM));
			const FVector PhysicalTargetWorld = Frame.TransformLocalPosition(
				FVector(FinaleSystem->GetLayoutPreset()
					.CanonicalScenario.Target
					.GetGeometricContactCenterCM()));
			M4FinalBirdToUFODistanceCM =
				(AuthorityBirdWorld - PhysicalTargetWorld).Length();
		}
	}
	const bool bM4PhysicalContactPassed = bM4PlanHasPhysicalContact
		&& FMath::IsFinite(M4FinalBirdToUFODistanceCM)
		&& M4PhysicalContactRadiusCM > 0.0
		&& FMath::Abs(
			M4FinalBirdToUFODistanceCM - M4PhysicalContactRadiusCM)
			<= FMath::Max(1.0, M4PhysicalContactRadiusCM * 0.01);
	Root->SetNumberField(
		TEXT("m4FinalBirdToUFODistanceCM"),
		FMath::IsFinite(M4FinalBirdToUFODistanceCM)
			? M4FinalBirdToUFODistanceCM
			: -1.0);
	Root->SetNumberField(
		TEXT("m4PhysicalContactRadiusCM"),
		M4PhysicalContactRadiusCM);
	Root->SetBoolField(
		TEXT("m4PhysicalContactPassed"),
		bM4PhysicalContactPassed);
	Root->SetBoolField(
		TEXT("m4TerminalClosurePassed"),
		M4TerminalFrameCount > 0
			&& M4AcquireFrameCount > 0
			&& M4AcquireNoTargetFrameCount == 0
			&& M4BirdLostFrameCount == 0
			&& M4TargetLostFrameCount == 0
			&& M4EndpointMissingFrameCount == 0
			&& M4PositionJumpCount == 0
			&& M4RotationJumpCount == 0
			&& M4FovJumpCount == 0
			&& bM4PhysicalContactPassed);

	if (IsValid(FinaleSystem))
	{
		const FABTSM11FinaleLayoutPreset& Preset =
			FinaleSystem->GetLayoutPreset();
		Root->SetStringField(TEXT("presetHash"), Hex64(Preset.PresetHash));
		Root->SetStringField(
			TEXT("certifiedBundleHash"),
			Hex64(Preset.CertifiedBundleHash));
		if (FinaleSystem->IsEditorCandidateMode())
		{
			const FABTSM11CandidateExperienceIdentity& Identity =
				FinaleSystem->GetEditorCandidateIdentity();
			Root->SetStringField(
				TEXT("candidateSourceHash"),
				Hex64(Identity.CandidateSourceHash));
			Root->SetStringField(
				TEXT("candidateResultHash"),
				Hex64(Identity.NominalResultHash));
		}
	}
	if (IsValid(InteractionSystem))
	{
		const FABTSM11PlaybackPlan& Plan =
			InteractionSystem->GetReleasedPlaybackPlan();
		Root->SetStringField(
			TEXT("interactionState"),
			StateLabel(InteractionSystem->GetInteractionState()));
		Root->SetStringField(
			TEXT("releasedTrajectoryHash"),
			Hex64(Plan.ReleasedTrajectoryHash));
		Root->SetStringField(TEXT("playbackPlanHash"), Hex64(Plan.PlanHash));
		Root->SetBoolField(
			TEXT("candidateQualifiedIntercept"),
			Plan.bCandidateQualifiedIntercept);
		Root->SetBoolField(TEXT("physicalTargetHit"), Plan.bPhysicalTargetHit);
		Root->SetBoolField(
			TEXT("visibleTerminalTransfer"),
			Plan.bUsesVisibleTerminalTransfer);
		Root->SetNumberField(
			TEXT("terminalTransferStartSeconds"),
			Plan.TransferStartTimeSeconds);
		Root->SetNumberField(
			TEXT("terminalTransferEndSeconds"),
			Plan.TransferEndTimeSeconds);
		Root->SetStringField(
			TEXT("cameraEndpointAuthority"),
			Plan.bPhysicalTargetHit
				? TEXT("PhysicalContact")
				: Plan.bCandidateQualifiedIntercept
					? TEXT("CandidateQualified")
					: TEXT("None"));
	}

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer =
		TJsonWriterFactory<>::Create(&Json);
	return FJsonSerializer::Serialize(Root, Writer)
		&& FFileHelper::SaveStringToFile(
			Json,
			*Config.GetManifestPath(),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11FinaleCameraCaptureConfigTest,
	"ABTS.M11C.CameraCapture.Config",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM11FinaleCameraCaptureConfigTest::RunTest(
	const FString& Parameters)
{
	TestEqual(
		TEXT("Capture contract version includes GameViewport HUD screenshots v18"),
		FABTSM11FinaleCameraCaptureConfig::ContractVersion,
		18);

	FABTSM11FinaleCameraCaptureConfig Config;
	FString Failure;
	TestTrue(
		TEXT("Disabled command line parses without side effects"),
		FABTSM11FinaleCameraCaptureConfig::Parse(
			TEXT("-unattended"),
			Config,
			&Failure));
	TestFalse(TEXT("Disabled command line remains disabled"), Config.bEnabled);

	const FString Output = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("M11CaptureTest")));
	const FString ValidCommandLine = FString::Printf(
		TEXT("-ABTSM11CameraCapture -ABTSM11CaptureRank=11 ")
		TEXT("-ABTSM11CaptureStylized=1 -ABTSM11CaptureDirectorM2=1 ")
		TEXT("-ABTSM11CaptureAutoExit=0 ")
		TEXT("-MovieFolder=\"%s\" -MovieName=Rank11_Stylized ")
		TEXT("-MovieFormat=JPG"),
		*Output);
	TestTrue(
		TEXT("Rank 11 stylized capture config parses"),
		FABTSM11FinaleCameraCaptureConfig::Parse(
			*ValidCommandLine,
			Config,
			&Failure));
	TestTrue(TEXT("Capture enabled"), Config.bEnabled);
	TestEqual(TEXT("Rank preserved"), Config.CandidateRank, 11);
	TestTrue(TEXT("Stylized preserved"), Config.bStylized);
	TestFalse(TEXT("Visual capture is not telemetry-only"), Config.bTelemetryOnly);
	TestTrue(TEXT("M2 director preserved"), Config.bDirectorM2);
	TestFalse(TEXT("M3 director remains disabled"), Config.bDirectorM3);
	TestFalse(TEXT("Auto-exit override preserved"), Config.bAutoExit);
	TestEqual(
		TEXT("JPG capture protocol preserved"),
		Config.MovieFormat,
		FString(TEXT("JPG")));
	TestEqual(TEXT("Frame rate preserved"), Config.FrameRate, 30);
	TestEqual(
		TEXT("Muxed AVI filename is deterministic"),
		FPaths::GetCleanFilename(Config.GetExpectedVideoPath()),
		FString(TEXT("Rank11_Stylized.avi")));
	TestEqual(
		TEXT("Camera observation filename is deterministic"),
		FPaths::GetCleanFilename(Config.GetObservationCsvPath()),
		FString(TEXT("Rank11_Stylized.camera-observations.csv")));

	const FString TelemetryCommandLine = FString::Printf(
		TEXT("-ABTSM11CameraCapture -ABTSM11CaptureRank=0 ")
		TEXT("-ABTSM11CaptureStylized=0 ")
		TEXT("-ABTSM11CaptureTelemetryOnly=1 ")
		TEXT("-ABTSM11CaptureDirectorM3=1 ")
		TEXT("-MovieFolder=\"%s\" -MovieName=Rank0_Numerical ")
		TEXT("-MovieFormat=JPG"),
		*Output);
	TestTrue(
		TEXT("Rank 0 non-stylized telemetry config parses"),
		FABTSM11FinaleCameraCaptureConfig::Parse(
			*TelemetryCommandLine,
			Config,
			&Failure));
	TestEqual(TEXT("Telemetry Rank preserved"), Config.CandidateRank, 0);
	TestFalse(TEXT("Telemetry style is disabled"), Config.bStylized);
	TestTrue(TEXT("Telemetry-only mode preserved"), Config.bTelemetryOnly);
	TestTrue(TEXT("Telemetry M3 director preserved"), Config.bDirectorM3);

	const FString Rank12MirrorCommandLine = FString::Printf(
		TEXT("-ABTSM11CameraCapture -ABTSM11CaptureRank=12 ")
		TEXT("-ABTSM11CaptureStylized=1 -ABTSM11CaptureDirectorM3=1 ")
		TEXT("-ABTSM11CaptureMirrorMainWorld=1 ")
		TEXT("-MovieFolder=\"%s\" -MovieName=Rank12_PIE_Mirror ")
		TEXT("-MovieFormat=JPG"),
		*Output);
	TestTrue(
		TEXT("Rank 12 PIE-equivalent MainWorld mirror parses"),
		FABTSM11FinaleCameraCaptureConfig::Parse(
			*Rank12MirrorCommandLine,
			Config,
			&Failure));
	TestEqual(TEXT("Rank12 preserved"), Config.CandidateRank, 12);
	TestTrue(TEXT("Rank12 uses M3 director"), Config.bDirectorM3);
	TestTrue(
		TEXT("Rank12 mirrors the live MainWorld environment"),
		Config.bMirrorMainWorldEnvironment);

	const FString HudCommandLine = FString::Printf(
		TEXT("-ABTSM11CameraCapture -ABTSM11CaptureRank=11 ")
		TEXT("-ABTSM11CaptureStylized=1 -ABTSM11CaptureHudScreenshot=1 ")
		TEXT("-MovieFolder=\"%s\" -MovieName=Rank11_Hud ")
		TEXT("-MovieFormat=JPG"),
		*Output);
	TestTrue(
		TEXT("GameViewport HUD screenshot config parses"),
		FABTSM11FinaleCameraCaptureConfig::Parse(
			*HudCommandLine,
			Config,
			&Failure));
	TestTrue(TEXT("HUD screenshot mode preserved"), Config.bHudScreenshotOnly);
	TestFalse(TEXT("HUD screenshot is not telemetry-only"), Config.bTelemetryOnly);
	TestEqual(
		TEXT("HUD screenshot filename is deterministic"),
		FPaths::GetCleanFilename(Config.GetHudScreenshotPath()),
		FString(TEXT("Rank11_Hud.png")));

	const FString CustomCommandLine = FString::Printf(
		TEXT("-ABTSM11CameraCapture -ABTSM11CaptureRank=11 ")
		TEXT("-ABTSM11CaptureYaw=-1.5 -ABTSM11CapturePitch=26.5 ")
		TEXT("-ABTSM11CapturePower=0.99 -MovieFolder=\"%s\" ")
		TEXT("-MovieName=CustomF4 -MovieFormat=JPG"),
		*Output);
	TestTrue(
		TEXT("Complete custom launch triplet parses"),
		FABTSM11FinaleCameraCaptureConfig::Parse(
			*CustomCommandLine, Config, &Failure));
	TestTrue(TEXT("Custom launch mode preserved"), Config.bCustomLaunchInput);
	TestEqual(TEXT("Custom yaw preserved"),
		Config.CustomLaunchInput.YawDegrees, -1.5, 1.0e-12);
	TestEqual(TEXT("Custom pitch preserved"),
		Config.CustomLaunchInput.PitchDegrees, 26.5, 1.0e-12);
	TestEqual(TEXT("Custom power preserved"),
		Config.CustomLaunchInput.Power, 0.99, 1.0e-12);
	TestFalse(
		TEXT("Partial custom launch triplet fails closed"),
		FABTSM11FinaleCameraCaptureConfig::Parse(
			TEXT("-ABTSM11CameraCapture -ABTSM11CaptureYaw=-1.5 ")
			TEXT("-MovieFolder=C:/Capture -MovieName=PartialInput ")
			TEXT("-MovieFormat=JPG"), Config, &Failure));

	FABTSM11FinaleCameraObservationSample DigestSample;
	DigestSample.FrameIndex = 7;
	DigestSample.PlaybackSeconds = 1.25;
	DigestSample.InteractionState = TEXT("Launched");
	DigestSample.Stage = TEXT("Approach");
	DigestSample.CurrentTarget = TEXT("Assist1");
	DigestSample.FramingTarget = TEXT("Assist1");
	DigestSample.StageReason = TEXT("Assist1Approach");
	DigestSample.EndpointAuthority = TEXT("PhysicalContact");
	DigestSample.StageProgress = 0.25;
	DigestSample.StageDurationSeconds = 2.0;
	DigestSample.ShotPhase = TEXT("Authority");
	DigestSample.ShotReason = TEXT("LucyAuthority");
	DigestSample.ShotProgress = 0.25;
	DigestSample.ShotDurationSeconds = 2.0;
	DigestSample.DirectorMode = TEXT("M3MultiAssist");
	DigestSample.bDirectorM3FrozenEnabled = true;
	DigestSample.DirectorBlendAlpha = 1.0;
	DigestSample.CameraWorld = FVector(100.125, -20.5, 30.75);
	DigestSample.CameraRotation = FRotator(5.0, 10.0, -2.0);
	DigestSample.FovDegrees = 55.0;
	TArray<FABTSM11FinaleCameraObservationSample> DigestSamples;
	DigestSamples.Add(DigestSample);
	const auto BaselineDigest =
		ABTSM11FinaleCameraCaptureRunnerPrivate::
			ComputeM5OrthogonalityDigests(DigestSamples);
	const auto RepeatDigest =
		ABTSM11FinaleCameraCaptureRunnerPrivate::
			ComputeM5OrthogonalityDigests(DigestSamples);
	TestTrue(TEXT("M5 baseline digest is valid"), BaselineDigest.IsValid());
	TestEqual(
		TEXT("M5 stage digest is deterministic"),
		BaselineDigest.StageSequenceHash,
		RepeatDigest.StageSequenceHash);
	TestEqual(
		TEXT("M5 camera digest is deterministic"),
		BaselineDigest.CameraNumericsHash,
		RepeatDigest.CameraNumericsHash);
	FABTSM11FinaleCameraObservationSample PreflightSample = DigestSample;
	PreflightSample.FrameIndex = 1;
	PreflightSample.InteractionState = TEXT("Ready");
	PreflightSample.Stage = TEXT("Unavailable");
	PreflightSample.CameraWorld = FVector(999.0, 999.0, 999.0);
	DigestSamples.Insert(PreflightSample, 0);
	const auto PreflightDigest =
		ABTSM11FinaleCameraCaptureRunnerPrivate::
			ComputeM5OrthogonalityDigests(DigestSamples);
	TestEqual(
		TEXT("Preflight timing does not alter stage digest"),
		BaselineDigest.StageSequenceHash,
		PreflightDigest.StageSequenceHash);
	TestEqual(
		TEXT("Preflight timing does not alter camera digest"),
		BaselineDigest.CameraNumericsHash,
		PreflightDigest.CameraNumericsHash);
	TestEqual(
		TEXT("Only launched/terminal frames are hashed"),
		PreflightDigest.FlightFrameCount,
		1);
	DigestSamples.RemoveAt(0);
	DigestSamples[0].CameraWorld.X += 0.002;
	const auto CameraMutationDigest =
		ABTSM11FinaleCameraCaptureRunnerPrivate::
			ComputeM5OrthogonalityDigests(DigestSamples);
	TestEqual(
		TEXT("Camera mutation does not alter stage digest"),
		BaselineDigest.StageSequenceHash,
		CameraMutationDigest.StageSequenceHash);
	TestNotEqual(
		TEXT("Camera mutation alters camera digest"),
		BaselineDigest.CameraNumericsHash,
		CameraMutationDigest.CameraNumericsHash);
	DigestSamples[0] = DigestSample;
	DigestSamples[0].ShotPhase = TEXT("OutgoingHold");
	const auto StageMutationDigest =
		ABTSM11FinaleCameraCaptureRunnerPrivate::
			ComputeM5OrthogonalityDigests(DigestSamples);
	TestNotEqual(
		TEXT("Stage mutation alters stage digest"),
		BaselineDigest.StageSequenceHash,
		StageMutationDigest.StageSequenceHash);
	TestEqual(
		TEXT("Stage mutation does not alter camera digest"),
		BaselineDigest.CameraNumericsHash,
		StageMutationDigest.CameraNumericsHash);

	FMinimalViewInfo TestView;
	TestView.Location = FVector::ZeroVector;
	TestView.Rotation = FRotator::ZeroRotator;
	TestView.FOV = 90.0f;
	ABTSM11FinaleCameraCaptureRunnerPrivate::FProjectedSphereObservation
		Projection;
	TestTrue(
		TEXT("M1 projection accepts a finite front-facing sphere"),
		ABTSM11FinaleCameraCaptureRunnerPrivate::ProjectObservationSphere(
			TestView,
			FIntPoint(1000, 500),
			FVector(1000.0, 0.0, 0.0),
			100.0,
			Projection));
	TestEqual(
		TEXT("Centered sphere projects to horizontal center"),
		Projection.Screen.X,
		500.0,
		0.001);
	TestEqual(
		TEXT("Centered sphere projects to vertical center"),
		Projection.Screen.Y,
		250.0,
		0.001);
	TestEqual(
		TEXT("Centered sphere is fully visible"),
		Projection.VisibleRatio,
		1.0,
		0.001);

	FABTSM11TrajectoryResult EventResult;
	EventResult.ValidationHash = 1;
	EventResult.CompletedAssistCount = FABTSM11GravityScenario::AssistCount;
	for (int32 AssistIndex = 1;
		AssistIndex <= FABTSM11GravityScenario::AssistCount;
		++AssistIndex)
	{
		for (int32 EventIndex = 0; EventIndex < 3; ++EventIndex)
		{
			FABTSM11TrajectoryEvent& Event = EventResult.Events.AddDefaulted_GetRef();
			Event.AssistIndex = AssistIndex;
			Event.Type = static_cast<EABTSM11TrajectoryEventType>(EventIndex);
			Event.TimeSeconds = AssistIndex * 10.0 + EventIndex * 2.0;
		}
	}
	const auto Cruise =
		ABTSM11FinaleCameraCaptureRunnerPrivate::ResolveObservationTarget(
			EABTSM11FinaleInteractionState::Launched,
			5.0,
			&EventResult);
	TestEqual(TEXT("Pre-enter observes Assist1"), Cruise.AssistIndex, 1);
	TestEqual(
		TEXT("Pre-enter stage is CruiseToBody"),
		static_cast<uint8>(Cruise.Stage),
		static_cast<uint8>(
			EABTSM11FinaleCameraStage::CruiseToBody));
	const auto Handoff =
		ABTSM11FinaleCameraCaptureRunnerPrivate::ResolveObservationTarget(
			EABTSM11FinaleInteractionState::Launched,
			15.0,
			&EventResult);
	TestEqual(TEXT("Post-exit observes Assist2"), Handoff.AssistIndex, 2);
	TestEqual(
		TEXT("Post-exit stage is Handoff"),
		static_cast<uint8>(Handoff.Stage),
		static_cast<uint8>(EABTSM11FinaleCameraStage::Handoff));
	const auto M3EarlyHandoff =
		ABTSM11FinaleCameraCaptureRunnerPrivate::ResolveObservationTarget(
			EABTSM11FinaleInteractionState::Launched,
			14.25,
			&EventResult,
			true);
	TestEqual(
		TEXT("M3 CurrentBody switches at the Handoff boundary"),
		M3EarlyHandoff.AssistIndex,
		2);
	TestEqual(
		TEXT("M3 early Handoff still frames the outgoing body"),
		M3EarlyHandoff.FramingAssistIndex,
		1);
	TestEqual(
		TEXT("M3 early Handoff is an explicit outgoing hold"),
		static_cast<uint8>(M3EarlyHandoff.ShotPhase),
		static_cast<uint8>(
			EABTSM11FinaleCameraShotPhase::OutgoingHold));
	const auto M3LateHandoff =
		ABTSM11FinaleCameraCaptureRunnerPrivate::ResolveObservationTarget(
			EABTSM11FinaleInteractionState::Launched,
			15.0,
			&EventResult,
			true);
	TestEqual(
		TEXT("M3 CurrentBody switches inside Handoff"),
		M3LateHandoff.AssistIndex,
		2);
	TestEqual(
		TEXT("M3 dual-body bridge selects the incoming body before AssistEnter"),
		M3LateHandoff.FramingAssistIndex,
		2);
	TestEqual(
		TEXT("M3 Handoff has an explicit dual-body bridge phase"),
		static_cast<uint8>(M3LateHandoff.ShotPhase),
		static_cast<uint8>(
			EABTSM11FinaleCameraShotPhase::DualBodyBridge));
	TestEqual(
		TEXT("M3 bridge identifies the outgoing body"),
		M3LateHandoff.OutgoingAssistIndex,
		1);
	TestEqual(
		TEXT("M3 bridge identifies the incoming body"),
		M3LateHandoff.IncomingAssistIndex,
		2);
	TestEqual(
		TEXT("M3 bridge exposes both bodies to observation"),
		M3LateHandoff.FramingTargetLabel,
		FString(TEXT("Assist1+Assist2")));

	TestFalse(
		TEXT("Out-of-range Rank fails closed"),
		FABTSM11FinaleCameraCaptureConfig::Parse(
			TEXT("-ABTSM11CameraCapture -ABTSM11CaptureRank=13 ")
			TEXT("-MovieFolder=C:/Capture -MovieName=BadRank ")
			TEXT("-MovieFormat=JPG"),
			Config,
			&Failure));
	TestFalse(
		TEXT("Non-boolean M2 director option fails closed"),
		FABTSM11FinaleCameraCaptureConfig::Parse(
			TEXT("-ABTSM11CameraCapture -ABTSM11CaptureDirectorM2=2 ")
			TEXT("-MovieFolder=C:/Capture -MovieName=BadDirectorMode ")
			TEXT("-MovieFormat=JPG"),
			Config,
			&Failure));
	TestFalse(
		TEXT("Non-boolean telemetry option fails closed"),
		FABTSM11FinaleCameraCaptureConfig::Parse(
			TEXT("-ABTSM11CameraCapture ")
			TEXT("-ABTSM11CaptureTelemetryOnly=2 ")
			TEXT("-MovieFolder=C:/Capture ")
			TEXT("-MovieName=BadTelemetry -MovieFormat=JPG"),
			Config,
			&Failure));
	TestFalse(
		TEXT("M2 and M3 modes fail closed when both requested"),
		FABTSM11FinaleCameraCaptureConfig::Parse(
			TEXT("-ABTSM11CameraCapture -ABTSM11CaptureDirectorM2=1 ")
			TEXT("-ABTSM11CaptureDirectorM3=1 -MovieFolder=C:/Capture ")
			TEXT("-MovieName=AmbiguousDirector -MovieFormat=JPG"),
			Config,
			&Failure));
	TestFalse(
		TEXT("Missing output folder fails closed"),
		FABTSM11FinaleCameraCaptureConfig::Parse(
			TEXT("-ABTSM11CameraCapture -MovieName=MissingFolder ")
			TEXT("-MovieFormat=JPG"),
			Config,
			&Failure));
	TestFalse(
		TEXT("Movie format tokens fail closed"),
		FABTSM11FinaleCameraCaptureConfig::Parse(
			TEXT("-ABTSM11CameraCapture -MovieFolder=C:/Capture ")
			TEXT("-MovieName={world} -MovieFormat=JPG"),
			Config,
			&Failure));
	TestFalse(
		TEXT("Native AVI capture fails closed"),
		FABTSM11FinaleCameraCaptureConfig::Parse(
			TEXT("-ABTSM11CameraCapture -MovieFolder=C:/Capture ")
			TEXT("-MovieName=NativeAvi"),
			Config,
			&Failure));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11FinaleHudScreenshotConfigTest,
	"ABTS.M11D.HUD.Unit.CaptureConfig",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM11FinaleHudScreenshotConfigTest::RunTest(
	const FString& Parameters)
{
	const FString Output = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("M11HudCaptureTest")));
	const FString CommandLine = FString::Printf(
		TEXT("-ABTSM11CameraCapture -ABTSM11CaptureRank=11 ")
		TEXT("-ABTSM11CaptureStylized=1 -ABTSM11CaptureHudScreenshot=1 ")
		TEXT("-MovieFolder=\"%s\" -MovieName=M11D_Hud ")
		TEXT("-MovieFormat=JPG"),
		*Output);
	FABTSM11FinaleCameraCaptureConfig Config;
	FString Failure;
	TestTrue(TEXT("HUD screenshot config parses independently"),
		FABTSM11FinaleCameraCaptureConfig::Parse(
			*CommandLine, Config, &Failure));
	TestTrue(TEXT("HUD screenshot mode is enabled"), Config.bHudScreenshotOnly);
	TestFalse(TEXT("HUD screenshot mode does not launch telemetry"),
		Config.bTelemetryOnly);
	TestEqual(TEXT("HUD screenshot uses Contract v18"),
		FABTSM11FinaleCameraCaptureConfig::ContractVersion, 18);
	TestEqual(TEXT("HUD PNG path is deterministic"),
		FPaths::GetCleanFilename(Config.GetHudScreenshotPath()),
		FString(TEXT("M11D_Hud.png")));

	TestFalse(TEXT("HUD and telemetry modes fail closed together"),
		FABTSM11FinaleCameraCaptureConfig::Parse(
			TEXT("-ABTSM11CameraCapture -ABTSM11CaptureHudScreenshot=1 ")
			TEXT("-ABTSM11CaptureTelemetryOnly=1 -MovieFolder=C:/Capture ")
			TEXT("-MovieName=AmbiguousHud -MovieFormat=JPG"),
			Config,
			&Failure));
	return true;
}

#endif
