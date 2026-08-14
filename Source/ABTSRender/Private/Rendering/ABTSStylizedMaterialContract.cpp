// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/ABTSStylizedMaterialContract.h"

#include "Misc/Crc.h"

namespace ABTSStylizedMaterialContractPrivate
{
	constexpr int32 ContractVersion = 1;

	const FName StyleEnabledParameter(TEXT("ABTS_StyleEnabled"));
	const FName BaseColorTintParameter(TEXT("ABTS_BaseColorTint"));
	const FName RoughnessFloorParameter(TEXT("ABTS_RoughnessFloor"));
	const FName RoughnessScaleParameter(TEXT("ABTS_RoughnessScale"));
	const FName SpecularScaleParameter(TEXT("ABTS_SpecularScale"));
	const FName MetallicScaleParameter(TEXT("ABTS_MetallicScale"));
	const FName RimStrengthParameter(TEXT("ABTS_RimStrength"));
	const FName RimPowerParameter(TEXT("ABTS_RimPower"));

	// Append-only signature. Changing an existing token is a contract migration.
	constexpr const TCHAR* ContractSignature =
		TEXT("ABTS.T3A0.MaterialContract.v1|")
		TEXT("Families=M3Surface,M3BackgroundProp,CuteBirdBody,CuteBirdFace,")
		TEXT("SlingshotOrganic,SlingshotMetal,M7Wood,M7Stone,M7Steel,M7Glass,")
		TEXT("FinalePlanet,FinaleUFO|")
		TEXT("Parameters=ABTS_StyleEnabled,ABTS_BaseColorTint,")
		TEXT("ABTS_RoughnessFloor,ABTS_RoughnessScale,ABTS_SpecularScale,")
		TEXT("ABTS_MetallicScale,ABTS_RimStrength,ABTS_RimPower");
}

bool FABTSStylizedSurfaceParameters::IsValid() const
{
	return BaseColorTint.R >= 0.0f
		&& BaseColorTint.G >= 0.0f
		&& BaseColorTint.B >= 0.0f
		&& BaseColorTint.A >= 0.0f
		&& RoughnessFloor >= 0.0f
		&& RoughnessFloor <= 1.0f
		&& RoughnessScale >= 0.0f
		&& RoughnessScale <= 2.0f
		&& SpecularScale >= 0.0f
		&& SpecularScale <= 2.0f
		&& MetallicScale >= 0.0f
		&& MetallicScale <= 1.0f
		&& RimStrength >= 0.0f
		&& RimStrength <= 1.0f
		&& RimPower >= 1.0f
		&& RimPower <= 32.0f;
}

int32 FABTSStylizedMaterialContract::GetVersion()
{
	return ABTSStylizedMaterialContractPrivate::ContractVersion;
}

uint32 FABTSStylizedMaterialContract::GetContractHash()
{
	return FCrc::StrCrc32(
		ABTSStylizedMaterialContractPrivate::ContractSignature);
}

bool FABTSStylizedMaterialContract::IsFamilyValid(
	const EABTSStylizedMaterialFamily Family)
{
	return Family > EABTSStylizedMaterialFamily::None
		&& Family <= EABTSStylizedMaterialFamily::FinaleUFO;
}

EABTSStylizedMaterialOwner FABTSStylizedMaterialContract::ResolveOwner(
	const EABTSStylizedMaterialFamily Family)
{
	switch (Family)
	{
	case EABTSStylizedMaterialFamily::M3Surface:
	case EABTSStylizedMaterialFamily::M3BackgroundProp:
		return EABTSStylizedMaterialOwner::M3;
	case EABTSStylizedMaterialFamily::M7Wood:
	case EABTSStylizedMaterialFamily::M7Stone:
	case EABTSStylizedMaterialFamily::M7Steel:
	case EABTSStylizedMaterialFamily::M7Glass:
		return EABTSStylizedMaterialOwner::M7;
	case EABTSStylizedMaterialFamily::FinalePlanet:
	case EABTSStylizedMaterialFamily::FinaleUFO:
		return EABTSStylizedMaterialOwner::M11;
	case EABTSStylizedMaterialFamily::CuteBirdBody:
	case EABTSStylizedMaterialFamily::CuteBirdFace:
	case EABTSStylizedMaterialFamily::SlingshotOrganic:
	case EABTSStylizedMaterialFamily::SlingshotMetal:
	case EABTSStylizedMaterialFamily::None:
	default:
		return EABTSStylizedMaterialOwner::Integration;
	}
}

EABTSStylizedMaterialAdoptionMode
FABTSStylizedMaterialContract::ResolveAdoptionMode(
	const EABTSStylizedMaterialFamily Family)
{
	return Family == EABTSStylizedMaterialFamily::M3Surface
		? EABTSStylizedMaterialAdoptionMode::InPlaceStyleParameter
		: EABTSStylizedMaterialAdoptionMode::ReversibleSlotOverride;
}

FABTSStylizedSurfaceParameters
FABTSStylizedMaterialContract::ResolveDefaultParameters(
	const EABTSStylizedMaterialFamily Family)
{
	FABTSStylizedSurfaceParameters Parameters;
	switch (Family)
	{
	case EABTSStylizedMaterialFamily::M3Surface:
		// The continuous planetary surface must not read as a second sun near
		// the horizon. Its shape remains readable from diffuse light, shadows
		// and the terrain outline, so the stylized branch is deliberately matte.
		Parameters.RoughnessFloor = 1.0f;
		Parameters.SpecularScale = 0.0f;
		break;
	case EABTSStylizedMaterialFamily::M3BackgroundProp:
		Parameters.RoughnessFloor = 0.76f;
		Parameters.SpecularScale = 0.22f;
		break;
	case EABTSStylizedMaterialFamily::CuteBirdBody:
		Parameters.RoughnessFloor = 0.68f;
		Parameters.SpecularScale = 0.28f;
		Parameters.RimStrength = 0.12f;
		Parameters.RimPower = 5.0f;
		break;
	case EABTSStylizedMaterialFamily::CuteBirdFace:
		Parameters.RoughnessFloor = 0.72f;
		Parameters.SpecularScale = 0.20f;
		break;
	case EABTSStylizedMaterialFamily::SlingshotOrganic:
	case EABTSStylizedMaterialFamily::M7Wood:
	case EABTSStylizedMaterialFamily::M7Stone:
		Parameters.RoughnessFloor = 0.78f;
		Parameters.SpecularScale = 0.20f;
		break;
	case EABTSStylizedMaterialFamily::SlingshotMetal:
	case EABTSStylizedMaterialFamily::M7Steel:
		Parameters.RoughnessFloor = 0.36f;
		Parameters.SpecularScale = 0.58f;
		Parameters.MetallicScale = 0.92f;
		Parameters.RimStrength = 0.10f;
		Parameters.RimPower = 8.0f;
		break;
	case EABTSStylizedMaterialFamily::M7Glass:
		Parameters.RoughnessFloor = 0.18f;
		Parameters.SpecularScale = 0.72f;
		Parameters.RimStrength = 0.18f;
		Parameters.RimPower = 6.0f;
		break;
	case EABTSStylizedMaterialFamily::FinalePlanet:
		Parameters.RoughnessFloor = 0.66f;
		Parameters.SpecularScale = 0.28f;
		Parameters.RimStrength = 0.20f;
		Parameters.RimPower = 4.0f;
		break;
	case EABTSStylizedMaterialFamily::FinaleUFO:
		Parameters.RoughnessFloor = 0.30f;
		Parameters.SpecularScale = 0.64f;
		Parameters.MetallicScale = 0.95f;
		Parameters.RimStrength = 0.22f;
		Parameters.RimPower = 7.0f;
		break;
	case EABTSStylizedMaterialFamily::None:
	default:
		break;
	}
	return Parameters;
}

bool FABTSStylizedMaterialContract::RequiresOpacityPreservation(
	const EABTSStylizedMaterialFamily Family)
{
	return Family == EABTSStylizedMaterialFamily::M7Glass;
}

const TCHAR* FABTSStylizedMaterialContract::LexToString(
	const EABTSStylizedMaterialFamily Family)
{
	switch (Family)
	{
	case EABTSStylizedMaterialFamily::M3Surface: return TEXT("M3Surface");
	case EABTSStylizedMaterialFamily::M3BackgroundProp: return TEXT("M3BackgroundProp");
	case EABTSStylizedMaterialFamily::CuteBirdBody: return TEXT("CuteBirdBody");
	case EABTSStylizedMaterialFamily::CuteBirdFace: return TEXT("CuteBirdFace");
	case EABTSStylizedMaterialFamily::SlingshotOrganic: return TEXT("SlingshotOrganic");
	case EABTSStylizedMaterialFamily::SlingshotMetal: return TEXT("SlingshotMetal");
	case EABTSStylizedMaterialFamily::M7Wood: return TEXT("M7Wood");
	case EABTSStylizedMaterialFamily::M7Stone: return TEXT("M7Stone");
	case EABTSStylizedMaterialFamily::M7Steel: return TEXT("M7Steel");
	case EABTSStylizedMaterialFamily::M7Glass: return TEXT("M7Glass");
	case EABTSStylizedMaterialFamily::FinalePlanet: return TEXT("FinalePlanet");
	case EABTSStylizedMaterialFamily::FinaleUFO: return TEXT("FinaleUFO");
	case EABTSStylizedMaterialFamily::None:
	default: return TEXT("None");
	}
}

const TCHAR* FABTSStylizedMaterialContract::LexToString(
	const EABTSStylizedMaterialOwner Owner)
{
	switch (Owner)
	{
	case EABTSStylizedMaterialOwner::M3: return TEXT("M3");
	case EABTSStylizedMaterialOwner::M7: return TEXT("M7");
	case EABTSStylizedMaterialOwner::M11: return TEXT("M11");
	case EABTSStylizedMaterialOwner::Integration:
	default: return TEXT("Integration");
	}
}

const TCHAR* FABTSStylizedMaterialContract::LexToString(
	const EABTSStylizedMaterialAdoptionMode Mode)
{
	return Mode == EABTSStylizedMaterialAdoptionMode::InPlaceStyleParameter
		? TEXT("InPlaceStyleParameter")
		: TEXT("ReversibleSlotOverride");
}

const FName& FABTSStylizedMaterialContract::GetStyleEnabledParameterName()
{
	return ABTSStylizedMaterialContractPrivate::StyleEnabledParameter;
}

const FName& FABTSStylizedMaterialContract::GetBaseColorTintParameterName()
{
	return ABTSStylizedMaterialContractPrivate::BaseColorTintParameter;
}

const FName& FABTSStylizedMaterialContract::GetRoughnessFloorParameterName()
{
	return ABTSStylizedMaterialContractPrivate::RoughnessFloorParameter;
}

const FName& FABTSStylizedMaterialContract::GetRoughnessScaleParameterName()
{
	return ABTSStylizedMaterialContractPrivate::RoughnessScaleParameter;
}

const FName& FABTSStylizedMaterialContract::GetSpecularScaleParameterName()
{
	return ABTSStylizedMaterialContractPrivate::SpecularScaleParameter;
}

const FName& FABTSStylizedMaterialContract::GetMetallicScaleParameterName()
{
	return ABTSStylizedMaterialContractPrivate::MetallicScaleParameter;
}

const FName& FABTSStylizedMaterialContract::GetRimStrengthParameterName()
{
	return ABTSStylizedMaterialContractPrivate::RimStrengthParameter;
}

const FName& FABTSStylizedMaterialContract::GetRimPowerParameterName()
{
	return ABTSStylizedMaterialContractPrivate::RimPowerParameter;
}
