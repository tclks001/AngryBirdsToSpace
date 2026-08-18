// Copyright Epic Games, Inc. All Rights Reserved.

#include "Slingshot/ABTSM6SlingshotSystem.h"

#include "ABTSRuntime.h"
#include "Components/PrimitiveComponent.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "World/ABTSM9Satellite.h"
#include "World/ABTSM51WorldActors.h"

namespace
{
	constexpr int32 MaximumRecordedContactsPerLaunch = 64;

	struct FE1LandingFrame
	{
		FVector SatelliteCenter = FVector::ZeroVector;
		float SatelliteRadiusCM = 0.0f;
		FVector TargetActorLocation = FVector::ZeroVector;
		FVector TargetCenter = FVector::ZeroVector;
		FVector TargetExtent = FVector::ZeroVector;
		FVector TargetUp = FVector::UpVector;
		FVector TargetForward = FVector::ForwardVector;
		FVector TargetRight = FVector::RightVector;
		FBox TargetBounds = FBox(EForceInit::ForceInit);
		bool bValid = false;
	};

	bool BuildE1LandingFrame(
		const AABTSM9Satellite* Satellite,
		const AActor* Target,
		FE1LandingFrame& OutFrame)
	{
		OutFrame = FE1LandingFrame();
		if (!IsValid(Satellite) || !IsValid(Target))
		{
			return false;
		}

		OutFrame.SatelliteCenter = Satellite->GetPlanetCenterWorld();
		OutFrame.SatelliteRadiusCM =
			FMath::Max(1.0f, Satellite->GetPlanetRadiusCM());
		OutFrame.TargetActorLocation = Target->GetActorLocation();
		OutFrame.TargetBounds = Target->GetComponentsBoundingBox(true);
		OutFrame.TargetCenter = OutFrame.TargetBounds.IsValid
			? OutFrame.TargetBounds.GetCenter()
			: OutFrame.TargetActorLocation;
		OutFrame.TargetExtent = OutFrame.TargetBounds.IsValid
			? OutFrame.TargetBounds.GetExtent()
			: FVector::ZeroVector;
		OutFrame.TargetUp =
			(OutFrame.TargetCenter - OutFrame.SatelliteCenter)
			.GetSafeNormal();
		if (OutFrame.TargetUp.IsNearlyZero())
		{
			return false;
		}

		OutFrame.TargetForward = FVector::VectorPlaneProject(
			Target->GetActorForwardVector(),
			OutFrame.TargetUp).GetSafeNormal();
		if (OutFrame.TargetForward.IsNearlyZero())
		{
			OutFrame.TargetForward = FVector::VectorPlaneProject(
				FVector::ForwardVector,
				OutFrame.TargetUp).GetSafeNormal();
		}
		if (OutFrame.TargetForward.IsNearlyZero())
		{
			OutFrame.TargetForward = FVector::VectorPlaneProject(
				FVector::RightVector,
				OutFrame.TargetUp).GetSafeNormal();
		}
		OutFrame.TargetRight = FVector::CrossProduct(
			OutFrame.TargetUp,
			OutFrame.TargetForward).GetSafeNormal();
		OutFrame.bValid = !OutFrame.TargetForward.IsNearlyZero()
			&& !OutFrame.TargetRight.IsNearlyZero();
		return OutFrame.bValid;
	}

	FString CsvVector(const FVector& Value)
	{
		return FString::Printf(
			TEXT("%.3f,%.3f,%.3f"),
			Value.X,
			Value.Y,
			Value.Z);
	}

	float DistanceToBounds(const FBox& Bounds, const FVector& Point)
	{
		if (!Bounds.IsValid)
		{
			return -1.0f;
		}
		const FVector Closest(
			FMath::Clamp(Point.X, Bounds.Min.X, Bounds.Max.X),
			FMath::Clamp(Point.Y, Bounds.Min.Y, Bounds.Max.Y),
			FMath::Clamp(Point.Z, Bounds.Min.Z, Bounds.Max.Z));
		return FVector::Distance(Point, Closest);
	}
}

void AABTSM6SlingshotSystem::InitializeE1LandingDiagnostics()
{
#if UE_BUILD_SHIPPING
	bE1LandingDiagnosticEnabled = false;
	E1LandingDiagnosticPath.Reset();
#else
	const UWorld* World = GetWorld();
	bE1LandingDiagnosticEnabled = World != nullptr
		&& (World->WorldType == EWorldType::PIE
			|| FParse::Param(
				FCommandLine::Get(),
				TEXT("ABTSE1LandingTrace")));
	if (!bE1LandingDiagnosticEnabled)
	{
		return;
	}

	const FString FileName = FString::Printf(
		TEXT("E1LandingSamples-%s-%u-%u.csv"),
		*FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ")),
		FPlatformProcess::GetCurrentProcessId(),
		GetUniqueID());
	E1LandingDiagnosticPath = FPaths::Combine(
		FPaths::ProjectLogDir(),
		FileName);
	const FString Header =
		TEXT("Utc,World,Launch,ContactIndex,Phase,Bird,Tier,FlightSeconds,")
		TEXT("Pull,AimX,AimY,AimZ,ImpactActor,ImpactComponent,")
		TEXT("SatelliteBodyContact,DirectE1Contact,AnyE1Hit,")
		TEXT("PointX,PointY,PointZ,VelocityX,VelocityY,VelocityZ,")
		TEXT("NormalSpeed,SatelliteX,SatelliteY,SatelliteZ,SatelliteRadius,")
		TEXT("PointRadius,PointAltitude,E1ActorX,E1ActorY,E1ActorZ,")
		TEXT("E1CenterX,E1CenterY,E1CenterZ,E1ExtentX,E1ExtentY,E1ExtentZ,")
		TEXT("E1Altitude,DeltaForward,DeltaRight,DeltaRadial,")
		TEXT("AngularMissDeg,BoundsDistance\n");
	if (!FFileHelper::SaveStringToFile(
			Header,
			*E1LandingDiagnosticPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		bE1LandingDiagnosticEnabled = false;
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M6][E1LandingTrace] ArmFailed Path=%s"),
			*E1LandingDiagnosticPath);
		E1LandingDiagnosticPath.Reset();
		return;
	}
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M6][E1LandingTrace] Armed Path=%s PIE=%d"),
		*E1LandingDiagnosticPath,
		World->WorldType == EWorldType::PIE ? 1 : 0);
#endif
}

void AABTSM6SlingshotSystem::BeginE1LandingDiagnostic(
	const FVector& InitialVelocity)
{
	if (!bE1LandingDiagnosticEnabled)
	{
		return;
	}
	++E1LandingDiagnosticLaunchSequence;
	E1LandingDiagnosticContactIndex = 0;
	bE1LandingDiagnosticHitTarget = false;
	E1LandingDiagnosticInitialVelocity = InitialVelocity;
	AppendE1LandingDiagnosticRow(
		TEXT("Launch"),
		LaunchedBird.IsValid()
			? LaunchedBird->GetActorLocation()
			: PouchLocation,
		InitialVelocity,
		0.0f,
		nullptr,
		nullptr,
		false,
		false);
}

void AABTSM6SlingshotSystem::RecordE1LandingImpact(
	const FHitResult& Hit,
	const float NormalSpeedCMPerSec,
	const FVector& IncomingVelocity)
{
	if (!bE1LandingDiagnosticEnabled
		|| E1LandingDiagnosticLaunchSequence <= 0
		|| E1LandingDiagnosticContactIndex
			>= MaximumRecordedContactsPerLaunch)
	{
		return;
	}

	const bool bSatelliteBodyContact =
		Hit.GetActor() == SatellitePracticeBody.Get();
	const AActor* Target = SatellitePracticeTarget.Get();
	const FVector ImpactPoint = !Hit.ImpactPoint.IsNearlyZero()
		? FVector(Hit.ImpactPoint)
		: (LaunchedBird.IsValid()
			? LaunchedBird->GetActorLocation()
			: FVector(Hit.Location));
	const FBox TargetBounds = IsValid(Target)
		? Target->GetComponentsBoundingBox(true)
		: FBox(EForceInit::ForceInit);
	const bool bDirectE1Contact =
		Hit.GetActor() == Target
		|| (TargetBounds.IsValid
			&& DistanceToBounds(TargetBounds, ImpactPoint)
				<= 1.0f);
	if (!bSatelliteBodyContact && !bDirectE1Contact)
	{
		return;
	}

	++E1LandingDiagnosticContactIndex;
	bE1LandingDiagnosticHitTarget |= bDirectE1Contact;
	AppendE1LandingDiagnosticRow(
		TEXT("Contact"),
		ImpactPoint,
		IncomingVelocity,
		NormalSpeedCMPerSec,
		Hit.GetActor(),
		Hit.GetComponent(),
		bSatelliteBodyContact,
		bDirectE1Contact);
}

void AABTSM6SlingshotSystem::RecordE1LandingFinal(
	const FVector& LandingWorldLocation)
{
	if (!bE1LandingDiagnosticEnabled
		|| E1LandingDiagnosticLaunchSequence <= 0)
	{
		return;
	}
	const FVector FinalVelocity = LaunchedBird.IsValid()
		? LaunchedBird->GetSlingshotVelocity()
		: FVector::ZeroVector;
	AppendE1LandingDiagnosticRow(
		TEXT("Final"),
		LandingWorldLocation,
		FinalVelocity,
		0.0f,
		nullptr,
		nullptr,
		false,
		false);
}

void AABTSM6SlingshotSystem::AppendE1LandingDiagnosticRow(
	const TCHAR* Phase,
	const FVector& SampleWorldLocation,
	const FVector& SampleWorldVelocity,
	const float NormalSpeedCMPerSec,
	const AActor* ImpactActor,
	const UPrimitiveComponent* ImpactComponent,
	const bool bSatelliteBodyContact,
	const bool bDirectE1Contact)
{
	if (!bE1LandingDiagnosticEnabled
		|| E1LandingDiagnosticPath.IsEmpty())
	{
		return;
	}

	FE1LandingFrame Frame;
	const bool bFrameValid = BuildE1LandingFrame(
		SatellitePracticeBody.Get(),
		SatellitePracticeTarget.Get(),
		Frame);
	const FVector PointOffset = bFrameValid
		? SampleWorldLocation - Frame.SatelliteCenter
		: FVector::ZeroVector;
	const float PointRadius = bFrameValid
		? PointOffset.Size()
		: -1.0f;
	const FVector PointUp = bFrameValid
		? PointOffset.GetSafeNormal()
		: FVector::ZeroVector;
	const FVector TargetDelta = bFrameValid
		? SampleWorldLocation - Frame.TargetCenter
		: FVector::ZeroVector;
	const float AngularMissDegrees = bFrameValid
		&& !PointUp.IsNearlyZero()
		? FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
			FVector::DotProduct(PointUp, Frame.TargetUp),
			-1.0f,
			1.0f)))
		: -1.0f;
	const EABTSSlingshotTier Tier = ActiveCord.IsValid()
		? ActiveCord->GetSlingshotTier()
		: EABTSSlingshotTier::Simple;
	TArray<FString> Fields;
	Fields.Reserve(47);
	const auto AppendVectorFields =
		[&Fields](const FVector& Value)
		{
			Fields.Add(FString::Printf(TEXT("%.3f"), Value.X));
			Fields.Add(FString::Printf(TEXT("%.3f"), Value.Y));
			Fields.Add(FString::Printf(TEXT("%.3f"), Value.Z));
		};
	Fields.Add(FDateTime::UtcNow().ToIso8601());
	Fields.Add(GetNameSafe(GetWorld()));
	Fields.Add(FString::FromInt(E1LandingDiagnosticLaunchSequence));
	Fields.Add(FString::FromInt(E1LandingDiagnosticContactIndex));
	Fields.Add(Phase);
	Fields.Add(FString::FromInt(LaunchedBird.IsValid()
		? ABTSBirdIdToIndex(LaunchedBird->GetBirdId())
		: -1));
	Fields.Add(FString::FromInt(static_cast<int32>(Tier)));
	Fields.Add(FString::Printf(TEXT("%.3f"), FlightElapsedSeconds));
	Fields.Add(FString::Printf(TEXT("%.4f"), PullAlpha));
	Fields.Add(FString::Printf(TEXT("%.3f"), AimPlaneOffset.X));
	Fields.Add(FString::Printf(TEXT("%.3f"), AimPlaneOffset.Y));
	Fields.Add(FString::Printf(TEXT("%.3f"), AimPlaneOffset.Z));
	Fields.Add(GetNameSafe(ImpactActor));
	Fields.Add(GetNameSafe(ImpactComponent));
	Fields.Add(bSatelliteBodyContact ? TEXT("1") : TEXT("0"));
	Fields.Add(bDirectE1Contact ? TEXT("1") : TEXT("0"));
	Fields.Add(bE1LandingDiagnosticHitTarget ? TEXT("1") : TEXT("0"));
	AppendVectorFields(SampleWorldLocation);
	AppendVectorFields(SampleWorldVelocity);
	Fields.Add(FString::Printf(TEXT("%.3f"), NormalSpeedCMPerSec));
	AppendVectorFields(bFrameValid
		? Frame.SatelliteCenter
		: FVector::ZeroVector);
	Fields.Add(FString::Printf(
		TEXT("%.3f"),
		bFrameValid ? Frame.SatelliteRadiusCM : -1.0f));
	Fields.Add(FString::Printf(TEXT("%.3f"), PointRadius));
	Fields.Add(FString::Printf(
		TEXT("%.3f"),
		bFrameValid
			? PointRadius - Frame.SatelliteRadiusCM
			: -1.0f));
	AppendVectorFields(bFrameValid
		? Frame.TargetActorLocation
		: FVector::ZeroVector);
	AppendVectorFields(bFrameValid
		? Frame.TargetCenter
		: FVector::ZeroVector);
	AppendVectorFields(bFrameValid
		? Frame.TargetExtent
		: FVector::ZeroVector);
	Fields.Add(FString::Printf(
		TEXT("%.3f"),
		bFrameValid
			? FVector::Distance(
				Frame.TargetCenter,
				Frame.SatelliteCenter)
				- Frame.SatelliteRadiusCM
			: -1.0f));
	Fields.Add(FString::Printf(
		TEXT("%.3f"),
		bFrameValid
			? FVector::DotProduct(
				TargetDelta,
				Frame.TargetForward)
			: 0.0f));
	Fields.Add(FString::Printf(
		TEXT("%.3f"),
		bFrameValid
			? FVector::DotProduct(
				TargetDelta,
				Frame.TargetRight)
			: 0.0f));
	Fields.Add(FString::Printf(
		TEXT("%.3f"),
		bFrameValid
			? FVector::DotProduct(
				TargetDelta,
				Frame.TargetUp)
			: 0.0f));
	Fields.Add(FString::Printf(
		TEXT("%.3f"),
		AngularMissDegrees));
	Fields.Add(FString::Printf(
		TEXT("%.3f"),
		bFrameValid
			? DistanceToBounds(
				Frame.TargetBounds,
				SampleWorldLocation)
			: -1.0f));
	const FString Row = FString::Join(Fields, TEXT(",")) + TEXT("\n");
	if (!FFileHelper::SaveStringToFile(
		Row,
		*E1LandingDiagnosticPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
		&IFileManager::Get(),
		FILEWRITE_Append))
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M6][E1LandingTrace] AppendFailed Path=%s"),
			*E1LandingDiagnosticPath);
		bE1LandingDiagnosticEnabled = false;
	}
}

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM6E1LandingDiagnosticFrameTest,
	"ABTS.M6.E1LandingDiagnostics.Frame",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM6E1LandingDiagnosticFrameTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FVector SatelliteCenter(100.0, -200.0, 300.0);
	const float SatelliteRadius = 1000.0f;
	const FVector TargetUp =
		FVector(0.3, 0.4, 0.8660254).GetSafeNormal();
	const FVector TargetCenter =
		SatelliteCenter + TargetUp * (SatelliteRadius + 120.0f);
	const FVector RotationAxis = FVector::CrossProduct(
		TargetUp,
		FVector::UpVector).GetSafeNormal();
	const FVector SampleUp = FQuat(
		RotationAxis,
		FMath::DegreesToRadians(6.0f)).RotateVector(TargetUp);
	const FVector Sample =
		SatelliteCenter + SampleUp * (SatelliteRadius + 20.0f);
	const float AngularDegrees = FMath::RadiansToDegrees(
		FMath::Acos(FMath::Clamp(
			FVector::DotProduct(
				(Sample - SatelliteCenter).GetSafeNormal(),
				TargetUp),
			-1.0f,
			1.0f)));
	TestTrue(
		TEXT("Angular miss remains finite"),
		FMath::IsFinite(AngularDegrees));
	TestEqual(
		TEXT("Angular miss preserves the constructed six-degree offset"),
		AngularDegrees,
		6.0f,
		0.01f);
	TestEqual(
		TEXT("Sample altitude is measured from the satellite surface"),
		static_cast<double>(
			FVector::Distance(Sample, SatelliteCenter)
			- SatelliteRadius),
		20.0,
		0.01);
	TestTrue(
		TEXT("CSV vector uses three comma-separated coordinates"),
		CsvVector(FVector(1.0, 2.0, 3.0))
			== TEXT("1.000,2.000,3.000"));
	return true;
}

#endif
