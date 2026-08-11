// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73DAG5BShapeGrammarV2.h"

#include "Algo/Sort.h"
#include "Misc/Crc.h"

namespace ABTSM73DAG5BV2
{
	enum class EGrammarRule : uint8
	{
		Terminal,
		Stack,
		SplitX,
		SplitY,
		Setback
	};

	enum class EAdjacency : uint8
	{
		Vertical,
		HorizontalX,
		HorizontalY
	};

	enum EPrimitiveMask : uint8
	{
		BoxMask = 1 << 0,
		PrismXMask = 1 << 1,
		PrismYMask = 1 << 2,
		PyramidMask = 1 << 3,
		AllMask = BoxMask | PrismXMask | PrismYMask | PyramidMask
	};

	struct FScope
	{
		FBox Bounds = FBox(EForceInit::ForceInit);
		EABTSM73DAG5BV2VolumeRole Role =
			EABTSM73DAG5BV2VolumeRole::Body;
		int32 Depth = 0;
		FString Path;
	};

	struct FGrammarContext
	{
		const FABTSM73DAG5BV2PreviewSettings* Settings = nullptr;
		FABTSM73DAG5BV2GenerationResult* Result = nullptr;
		FString* Error = nullptr;
		int32 GrammarSteps = 0;
	};

	struct FInitialPlan
	{
		TArray<FScope> Roots;
		TArray<FScope> FixedVolumes;
	};

	struct FAdjacencyEdge
	{
		int32 A = INDEX_NONE;
		int32 B = INDEX_NONE;
		EAdjacency Type = EAdjacency::Vertical;
	};

	struct FWFCContext
	{
		const FABTSM73DAG5BV2PreviewSettings* Settings = nullptr;
		const TArray<FABTSM73DAG5BV2Volume>* Volumes = nullptr;
		const TArray<FAdjacencyEdge>* Edges = nullptr;
		int32 PropagationOperations = 0;
		int32 BacktrackSteps = 0;
	};

	bool IsSpanRole(const EABTSM73DAG5BV2VolumeRole Role)
	{
		return Role == EABTSM73DAG5BV2VolumeRole::Bridge
			|| Role == EABTSM73DAG5BV2VolumeRole::SupportedSpan;
	}

	uint32 StableSeed(
		const FABTSM73DAG5BV2PreviewSettings& Settings,
		const FString& Path,
		const uint32 Salt)
	{
		const FString Canonical = FString::Printf(
			TEXT("V=%d|Seed=%d|Path=%s|Salt=%u"),
			Settings.GeneratorVersion,
			Settings.BuildingSeed,
			*Path,
			Salt);
		return FCrc::StrCrc32(*Canonical);
	}

	bool IsFinitePositive(const float Value)
	{
		return FMath::IsFinite(Value) && Value > 0.0f;
	}

	bool IsValidSettings(
		const FABTSM73DAG5BV2PreviewSettings& Settings,
		FString& OutError)
	{
		if (!IsFinitePositive(Settings.TargetWidthCM)
			|| !IsFinitePositive(Settings.TargetDepthCM)
			|| !IsFinitePositive(Settings.TargetHeightCM)
			|| !IsFinitePositive(Settings.MinVolumeSpanCM)
			|| Settings.MinGrammarDepth < 0
			|| Settings.MaxGrammarDepth < Settings.MinGrammarDepth
			|| Settings.MaxGrammarDepth > 6
			|| Settings.MaxVolumeCount < 8
			|| Settings.TargetVolumeCount < 3
			|| Settings.TargetVolumeCount > 256
			|| !FMath::IsFinite(Settings.StackWeight)
			|| !FMath::IsFinite(Settings.HorizontalSplitWeight)
			|| !FMath::IsFinite(Settings.SetbackWeight)
			|| !FMath::IsFinite(Settings.TerminalWeight)
			|| Settings.StackWeight < 0.0f
			|| Settings.HorizontalSplitWeight < 0.0f
			|| Settings.SetbackWeight < 0.0f
			|| Settings.TerminalWeight < 0.0f
			|| !FMath::IsFinite(Settings.SplitGapRatio)
			|| !FMath::IsFinite(Settings.SetbackRatio)
			|| !FMath::IsFinite(Settings.MaxOffsetRatio)
			|| !FMath::IsFinite(Settings.BridgeChance)
			|| !FMath::IsFinite(Settings.BridgeThicknessRatio)
			|| Settings.SplitGapRatio <= 0.0f
			|| Settings.SetbackRatio <= 0.0f
			|| Settings.MaxOffsetRatio < 0.0f
			|| Settings.BridgeChance < 0.0f
			|| Settings.BridgeChance > 1.0f
			|| Settings.BridgeThicknessRatio <= 0.0f
			|| !FMath::IsFinite(Settings.BoxWeight)
			|| !FMath::IsFinite(Settings.PrismWeight)
			|| !FMath::IsFinite(Settings.PyramidWeight)
			|| Settings.BoxWeight < 0.0f
			|| Settings.PrismWeight < 0.0f
			|| Settings.PyramidWeight < 0.0f
			|| !FMath::IsFinite(Settings.RoofMergeGapCM)
			|| Settings.RoofMergeGapCM < 0.0f
			|| !IsFinitePositive(Settings.RoofCourseHeightCM)
			|| Settings.MinimumRoofCourseCount < 2
			|| Settings.MaximumRoofCourseCount
				< Settings.MinimumRoofCourseCount
			|| Settings.MaximumRoofCourseCount > 64
			|| !IsFinitePositive(Settings.RoofHeightToShortSpanRatio)
			|| !IsFinitePositive(Settings.PyramidPreferredMaxAspectRatio)
			|| !IsFinitePositive(Settings.PrismPreferredMinAspectRatio)
			|| Settings.PyramidPreferredMaxAspectRatio < 1.0f
			|| Settings.PrismPreferredMinAspectRatio
				<= Settings.PyramidPreferredMaxAspectRatio
			|| !FMath::IsFinite(Settings.SingleTerminalRoofHeightCM)
			|| Settings.SingleTerminalRoofHeightCM <= 0.0f
			|| Settings.BoxWeight
				+ Settings.PrismWeight
				+ Settings.PyramidWeight <= UE_SMALL_NUMBER
			|| (Settings.bRequireSingleTerminalRoof
				&& (Settings.bRequirePrimitiveVariety
					|| Settings.PrismWeight + Settings.PyramidWeight
						<= UE_SMALL_NUMBER))
			|| Settings.MaxWFCPropagationOperations < 1
			|| Settings.MaxWFCBacktrackSteps < 0)
		{
			OutError = TEXT("DAG5BV2InvalidSettings");
			return false;
		}
		return true;
	}

	EABTSM73DAG5BV2Archetype ResolveArchetype(
		const FABTSM73DAG5BV2PreviewSettings& Settings)
	{
		if (Settings.Archetype != EABTSM73DAG5BV2Archetype::Auto)
		{
			return Settings.Archetype;
		}
		const uint32 Choice = StableSeed(Settings, TEXT("Archetype"), 0x51a7);
		return static_cast<EABTSM73DAG5BV2Archetype>(
			1 + Choice % 4);
	}

	FBox MakeBounds(
		const FVector& Center,
		const FVector& Size)
	{
		const FVector Half = Size * 0.5;
		return FBox(Center - Half, Center + Half);
	}

	FScope MakeScope(
		const FString& Path,
		const FVector& Center,
		const FVector& Size,
		const EABTSM73DAG5BV2VolumeRole Role,
		const int32 Depth = 0)
	{
		FScope Scope;
		Scope.Bounds = MakeBounds(Center, Size);
		Scope.Role = Role;
		Scope.Depth = Depth;
		Scope.Path = Path;
		return Scope;
	}

	void AddBridge(
		FInitialPlan& Plan,
		const FString& Path,
		const FVector& Center,
		const FVector& Size)
	{
		Plan.FixedVolumes.Add(MakeScope(
			Path,
			Center,
			Size,
			EABTSM73DAG5BV2VolumeRole::SupportedSpan));
	}

	FInitialPlan BuildInitialPlan(
		const FABTSM73DAG5BV2PreviewSettings& Settings,
		const EABTSM73DAG5BV2Archetype Archetype)
	{
		FInitialPlan Plan;
		const float W = Settings.TargetWidthCM;
		const float D = Settings.TargetDepthCM;
		const float H = Settings.TargetHeightCM;
		const int32 Milestone = Settings.ComplexityMilestoneTier;
		const bool bLegacy = Milestone < 0;

		switch (Archetype)
		{
		case EABTSM73DAG5BV2Archetype::TerracedCitadel:
			Plan.Roots.Add(MakeScope(
				TEXT("Citadel/Main"),
				FVector(0.0, 0.0, H * 0.5),
				FVector(W * 0.58, D * 0.78, H),
				EABTSM73DAG5BV2VolumeRole::Body));
			if (bLegacy || Milestone >= 1)
			{
				Plan.Roots.Add(MakeScope(
				TEXT("Citadel/LeftAnnex"),
				FVector(-W * 0.39, 0.0, H * 0.24),
				FVector(W * 0.18, D * 0.92, H * 0.48),
				EABTSM73DAG5BV2VolumeRole::Annex));
			}
			if (bLegacy || Milestone >= 2)
			{
				Plan.Roots.Add(MakeScope(
				TEXT("Citadel/RightAnnex"),
				FVector(W * 0.39, 0.0, H * 0.29),
				FVector(W * 0.18, D * 0.70, H * 0.58),
				EABTSM73DAG5BV2VolumeRole::Annex));
			}
			break;

		case EABTSM73DAG5BV2Archetype::TwinTowerComplex:
			Plan.Roots.Add(MakeScope(
				TEXT("Twin/LeftTower"),
				FVector(-W * 0.27, 0.0, H * 0.50),
				FVector(W * 0.38, D * 0.72, H),
				EABTSM73DAG5BV2VolumeRole::Body));
			if (bLegacy || Milestone >= 1)
			{
				Plan.Roots.Add(MakeScope(
				TEXT("Twin/RightTower"),
				FVector(W * 0.27, 0.0, H * 0.43),
				FVector(W * 0.38, D * 0.88, H * 0.86),
				EABTSM73DAG5BV2VolumeRole::Body));
			}
			if (bLegacy || Milestone >= 4)
			{
				AddBridge(
				Plan,
				TEXT("Twin/Bridge"),
				FVector(0.0, 0.0, H * 0.56),
				FVector(
					W * 0.46,
					D * 0.44,
					H * Settings.BridgeThicknessRatio));
			}
			break;

		case EABTSM73DAG5BV2Archetype::BridgedArcology:
			if (bLegacy || Milestone >= 1)
			{
				Plan.Roots.Add(MakeScope(
				TEXT("Arcology/West"),
				FVector(-W * 0.34, 0.0, H * 0.45),
				FVector(W * 0.25, D * 0.62, H * 0.90),
				EABTSM73DAG5BV2VolumeRole::Body));
			}
			Plan.Roots.Add(MakeScope(
				TEXT("Arcology/Core"),
				FVector(0.0, 0.0, H * 0.34),
				FVector(W * 0.25, D * 0.90, H * 0.68),
				EABTSM73DAG5BV2VolumeRole::Body));
			if (bLegacy || Milestone >= 2)
			{
				Plan.Roots.Add(MakeScope(
				TEXT("Arcology/East"),
				FVector(W * 0.34, 0.0, H * 0.50),
				FVector(W * 0.25, D * 0.68, H),
				EABTSM73DAG5BV2VolumeRole::Body));
			}
			if (bLegacy || Milestone >= 4)
			{
				AddBridge(
				Plan,
				TEXT("Arcology/WestBridge"),
				FVector(-W * 0.17, 0.0, H * 0.54),
				FVector(
					W * 0.27,
					D * 0.40,
					H * Settings.BridgeThicknessRatio));
			}
			if (bLegacy)
			{
				AddBridge(
				Plan,
				TEXT("Arcology/EastBridge"),
				FVector(W * 0.17, 0.0, H * 0.66),
				FVector(
					W * 0.27,
					D * 0.36,
					H * Settings.BridgeThicknessRatio));
			}
			break;

		case EABTSM73DAG5BV2Archetype::SpiredCampus:
		default:
			Plan.Roots.Add(MakeScope(
				TEXT("Campus/CentralSpire"),
				FVector(0.0, 0.0, H * 0.50),
				FVector(W * 0.38, D * 0.50, H),
				EABTSM73DAG5BV2VolumeRole::Body));
			if (bLegacy || Milestone >= 1)
			{
				Plan.Roots.Add(MakeScope(
				TEXT("Campus/WestWing"),
				FVector(-W * (bLegacy ? 0.34 : 0.36), 0.0, H * 0.23),
				FVector(W * 0.26, D * 0.82, H * 0.46),
				EABTSM73DAG5BV2VolumeRole::Annex));
			}
			if (bLegacy || Milestone >= 2)
			{
				Plan.Roots.Add(MakeScope(
				TEXT("Campus/EastWing"),
				FVector(W * (bLegacy ? 0.34 : 0.36), 0.0, H * 0.28),
				FVector(W * 0.26, D * 0.68, H * 0.56),
				EABTSM73DAG5BV2VolumeRole::Annex));
			}
			if (bLegacy || Milestone >= 4)
			{
				AddBridge(
				Plan,
				TEXT("Campus/WestLink"),
				FVector(-W * 0.18, 0.0, H * 0.35),
				FVector(
					W * 0.22,
					D * 0.32,
					H * Settings.BridgeThicknessRatio));
			}
			if (bLegacy)
			{
				AddBridge(
				Plan,
				TEXT("Campus/EastLink"),
				FVector(W * 0.18, 0.0, H * 0.42),
				FVector(
					W * 0.22,
					D * 0.28,
					H * Settings.BridgeThicknessRatio));
			}
			break;
		}
		return Plan;
	}

	bool EmitVolume(
		const FScope& Scope,
		FGrammarContext& Context)
	{
		if (Context.Result->Volumes.Num()
			>= Context.Settings->MaxVolumeCount)
		{
			*Context.Error = TEXT("DAG5BV2VolumeBudgetExceeded");
			return false;
		}
		FABTSM73DAG5BV2Volume& Volume =
			Context.Result->Volumes.AddDefaulted_GetRef();
		Volume.VolumeId = Context.Result->Volumes.Num() - 1;
		Volume.GrammarDepth = Scope.Depth;
		Volume.LocalBounds = Scope.Bounds;
		Volume.Role = Scope.Role;
		if (Scope.Bounds.Min.Z <= 1.0 && !IsSpanRole(Scope.Role))
		{
			Volume.Role = EABTSM73DAG5BV2VolumeRole::Foundation;
		}
		else if (Scope.Depth >= Context.Settings->MaxGrammarDepth - 1
			&& !IsSpanRole(Scope.Role))
		{
			Volume.Role = EABTSM73DAG5BV2VolumeRole::Crown;
		}
		Volume.DerivationPath = Scope.Path;
		return true;
	}

	bool CanStack(
		const FScope& Scope,
		const FABTSM73DAG5BV2PreviewSettings& Settings)
	{
		return Scope.Bounds.GetSize().Z
			>= Settings.MinVolumeSpanCM * 2.0f;
	}

	bool CanSplitX(
		const FScope& Scope,
		const FABTSM73DAG5BV2PreviewSettings& Settings)
	{
		const float Size = Scope.Bounds.GetSize().X;
		return Size * (1.0f - Settings.SplitGapRatio)
			>= Settings.MinVolumeSpanCM * 2.0f;
	}

	bool CanSplitY(
		const FScope& Scope,
		const FABTSM73DAG5BV2PreviewSettings& Settings)
	{
		const float Size = Scope.Bounds.GetSize().Y;
		return Size * (1.0f - Settings.SplitGapRatio)
			>= Settings.MinVolumeSpanCM * 2.0f;
	}

	EGrammarRule SelectRule(
		const FScope& Scope,
		const int32 LeafBudget,
		const FABTSM73DAG5BV2PreviewSettings& Settings)
	{
		if (Scope.Depth >= Settings.MaxGrammarDepth
			|| LeafBudget <= 1)
		{
			return EGrammarRule::Terminal;
		}

		// Beam-D1.5 macro milestones precede weighted detail. They make each
		// tier structurally legible for every seed instead of hoping that a
		// weighted rule happens to be selected.
		if (Settings.ComplexityMilestoneTier >= 0)
		{
			const int32 Tier = Settings.ComplexityMilestoneTier;
			if (Tier >= 3 && Scope.Depth == 0)
			{
				const bool bBalanceHorizontalAxes = Settings.Archetype
					== EABTSM73DAG5BV2Archetype::TerracedCitadel;
				// Terraced modules preserve their long dimension for Beam-A's
				// one-dimensional bay subdivision, so the semantic grammar splits
				// the short dimension first. Other archetypes retain legacy X-first.
				const bool bPreferX = !bBalanceHorizontalAxes
					|| Scope.Bounds.GetSize().X < Scope.Bounds.GetSize().Y;
				if (bPreferX && CanSplitX(Scope, Settings))
				{
					return EGrammarRule::SplitX;
				}
				if (!bPreferX && CanSplitY(Scope, Settings))
				{
					return EGrammarRule::SplitY;
				}
				if (CanSplitX(Scope, Settings)) return EGrammarRule::SplitX;
				if (CanSplitY(Scope, Settings)) return EGrammarRule::SplitY;
			}
			if (Tier >= 2 && Scope.Depth <= 1 && CanStack(Scope, Settings))
			{
				return EGrammarRule::Setback;
			}
			// The macro ladder ends after its explicit split/setback steps.  The old
			// unconditional Stack here kept every later high-tier scope on one linear
			// branch until MaxGrammarDepth, so a TargetVolumeCount of 72 could still
			// only produce roughly twenty final semantic volumes.  Easy tiers retain
			// one readable base stack; richer tiers now reach the weighted detail rules
			// below after their deterministic macro silhouette is established.
			const bool bPreserveSupportedSpanHierarchy = Settings.Archetype
				== EABTSM73DAG5BV2Archetype::BridgedArcology;
			if (((Tier < 2 && Scope.Depth == 0)
					|| bPreserveSupportedSpanHierarchy)
				&& CanStack(Scope, Settings))
			{
				return EGrammarRule::Stack;
			}
		}

		struct FWeightedRule
		{
			EGrammarRule Rule = EGrammarRule::Terminal;
			float Weight = 0.0f;
		};
		TArray<FWeightedRule, TInlineAllocator<5>> Candidates;
		if (CanStack(Scope, Settings))
		{
			Candidates.Add({EGrammarRule::Stack, Settings.StackWeight});
			Candidates.Add({EGrammarRule::Setback, Settings.SetbackWeight});
		}
		const bool bBalanceHorizontalAxes = Settings.Archetype
			== EABTSM73DAG5BV2Archetype::TerracedCitadel;
		const FVector HorizontalSize = Scope.Bounds.GetSize();
		const float SafeX = FMath::Max(HorizontalSize.X, 1.0f);
		const float SafeY = FMath::Max(HorizontalSize.Y, 1.0f);
		const float XAspectWeight = bBalanceHorizontalAxes
			? FMath::Clamp(SafeY / SafeX, 0.5f, 2.0f) : 1.0f;
		const float YAspectWeight = bBalanceHorizontalAxes
			? FMath::Clamp(SafeX / SafeY, 0.5f, 2.0f) : 0.75f;
		if (LeafBudget >= 2 && CanSplitX(Scope, Settings))
		{
			Candidates.Add({
				EGrammarRule::SplitX,
				Settings.HorizontalSplitWeight * XAspectWeight});
		}
		if (LeafBudget >= 2 && CanSplitY(Scope, Settings))
		{
			Candidates.Add({
				EGrammarRule::SplitY,
				Settings.HorizontalSplitWeight * YAspectWeight});
		}
		if (Scope.Depth >= Settings.MinGrammarDepth)
		{
			Candidates.Add({
				EGrammarRule::Terminal,
				Settings.TerminalWeight});
		}
		if (Candidates.IsEmpty())
		{
			return EGrammarRule::Terminal;
		}

		float TotalWeight = 0.0f;
		for (const FWeightedRule& Candidate : Candidates)
		{
			TotalWeight += FMath::Max(0.0f, Candidate.Weight);
		}
		if (TotalWeight <= UE_SMALL_NUMBER)
		{
			return EGrammarRule::Terminal;
		}

		FRandomStream Stream(
			static_cast<int32>(StableSeed(
				Settings,
				Scope.Path,
				0x9b31 + Scope.Depth)));
		float Pick = Stream.FRandRange(0.0f, TotalWeight);
		for (const FWeightedRule& Candidate : Candidates)
		{
			Pick -= FMath::Max(0.0f, Candidate.Weight);
			if (Pick <= 0.0f)
			{
				return Candidate.Rule;
			}
		}
		return Candidates.Last().Rule;
	}

	void AddTrace(
		const FScope& Scope,
		const TCHAR* Rule,
		FGrammarContext& Context)
	{
		Context.Result->GrammarTrace.Add(FString::Printf(
			TEXT("%s -> %s"),
			*Scope.Path,
			Rule));
		++Context.GrammarSteps;
	}

	bool ExpandScope(
		const FScope& Scope,
		const int32 LeafBudget,
		FGrammarContext& Context)
	{
		const FABTSM73DAG5BV2PreviewSettings& Settings =
			*Context.Settings;
		const EGrammarRule Rule =
			SelectRule(Scope, LeafBudget, Settings);
		if (Rule == EGrammarRule::Terminal)
		{
			AddTrace(Scope, TEXT("Terminal"), Context);
			return EmitVolume(Scope, Context);
		}

		FRandomStream Stream(
			static_cast<int32>(StableSeed(
				Settings,
				Scope.Path,
				0xa731 + Scope.Depth)));
		if (Rule == EGrammarRule::Stack
			|| Rule == EGrammarRule::Setback)
		{
			const bool bSetback = Rule == EGrammarRule::Setback;
			AddTrace(
				Scope,
				bSetback ? TEXT("Setback") : TEXT("Stack"),
				Context);
			const FVector Size = Scope.Bounds.GetSize();
			const float LowerRatio = Stream.FRandRange(0.38f, 0.56f);
			const float SplitZ =
				Scope.Bounds.Min.Z + Size.Z * LowerRatio;
			FScope Lower = Scope;
			Lower.Bounds.Max.Z = SplitZ;
			Lower.Depth = Scope.Depth + 1;
			Lower.Path = Scope.Path + TEXT("/Lower");
			if (!EmitVolume(Lower, Context))
			{
				return false;
			}

			FScope Upper = Scope;
			Upper.Bounds.Min.Z = SplitZ;
			Upper.Depth = Scope.Depth + 1;
			Upper.Path = Scope.Path
				+ (bSetback ? TEXT("/SetbackUpper") : TEXT("/Upper"));
			if (bSetback)
			{
				const FVector UpperSize = Upper.Bounds.GetSize();
				const float Shrink = FMath::Clamp(
					Settings.SetbackRatio,
					0.04f,
					0.35f);
				const float OffsetX = Stream.FRandRange(
					-Settings.MaxOffsetRatio,
					Settings.MaxOffsetRatio) * Size.X;
				const float OffsetY = Stream.FRandRange(
					-Settings.MaxOffsetRatio,
					Settings.MaxOffsetRatio) * Size.Y;
				FVector Center =
					Upper.Bounds.GetCenter()
					+ FVector(OffsetX, OffsetY, 0.0);
				const FVector NewSize(
					UpperSize.X * (1.0f - Shrink),
					UpperSize.Y * (1.0f - Shrink
						* (Settings.Archetype
							== EABTSM73DAG5BV2Archetype::TerracedCitadel
								? 1.0f : 0.75f)),
					UpperSize.Z);
				Center.X = FMath::Clamp(
					Center.X,
					Scope.Bounds.Min.X + NewSize.X * 0.5f,
					Scope.Bounds.Max.X - NewSize.X * 0.5f);
				Center.Y = FMath::Clamp(
					Center.Y,
					Scope.Bounds.Min.Y + NewSize.Y * 0.5f,
					Scope.Bounds.Max.Y - NewSize.Y * 0.5f);
				Upper.Bounds = MakeBounds(Center, NewSize);
				Upper.Bounds.Min.Z = SplitZ;
				Upper.Bounds.Max.Z = Scope.Bounds.Max.Z;
			}
			return ExpandScope(Upper, LeafBudget - 1, Context);
		}

		const bool bSplitX = Rule == EGrammarRule::SplitX;
		AddTrace(
			Scope,
			bSplitX ? TEXT("SplitX") : TEXT("SplitY"),
			Context);
		const int32 BridgeReserve =
			LeafBudget >= 3
			&& Stream.FRand() <= Settings.BridgeChance
				? 1
				: 0;
		const int32 ChildBudgetTotal = LeafBudget - BridgeReserve;
		const int32 FirstBudget = FMath::Max(1, ChildBudgetTotal / 2);
		const int32 SecondBudget =
			FMath::Max(1, ChildBudgetTotal - FirstBudget);
		const int32 Axis = bSplitX ? 0 : 1;
		const float MinAxis = Scope.Bounds.Min[Axis];
		const float MaxAxis = Scope.Bounds.Max[Axis];
		const float Span = MaxAxis - MinAxis;
		const float Gap = Span * Settings.SplitGapRatio;
		const float SplitRatio = Stream.FRandRange(0.42f, 0.58f);
		const float SplitCenter = MinAxis + Span * SplitRatio;

		FScope First = Scope;
		First.Bounds.Max[Axis] = SplitCenter - Gap * 0.5f;
		First.Depth = Scope.Depth + 1;
		First.Path = Scope.Path
			+ (bSplitX ? TEXT("/West") : TEXT("/South"));

		FScope Second = Scope;
		Second.Bounds.Min[Axis] = SplitCenter + Gap * 0.5f;
		Second.Depth = Scope.Depth + 1;
		Second.Path = Scope.Path
			+ (bSplitX ? TEXT("/East") : TEXT("/North"));

		if (!ExpandScope(First, FirstBudget, Context)
			|| !ExpandScope(Second, SecondBudget, Context))
		{
			return false;
		}

		if (BridgeReserve > 0)
		{
			FScope Bridge = Scope;
			const float Thickness = FMath::Max(
				Settings.MinVolumeSpanCM * 0.45f,
				Scope.Bounds.GetSize().Z
					* Settings.BridgeThicknessRatio);
			const float CenterZ = FMath::Lerp(
				Scope.Bounds.Min.Z,
				Scope.Bounds.Max.Z,
				Stream.FRandRange(0.48f, 0.72f));
			Bridge.Bounds.Min.Z = CenterZ - Thickness * 0.5f;
			Bridge.Bounds.Max.Z = CenterZ + Thickness * 0.5f;
			if (bSplitX)
			{
				Bridge.Bounds.Min.X =
					First.Bounds.Max.X - Gap * 0.25f;
				Bridge.Bounds.Max.X =
					Second.Bounds.Min.X + Gap * 0.25f;
				const float DepthInset =
					Scope.Bounds.GetSize().Y * 0.20f;
				Bridge.Bounds.Min.Y += DepthInset;
				Bridge.Bounds.Max.Y -= DepthInset;
			}
			else
			{
				Bridge.Bounds.Min.Y =
					First.Bounds.Max.Y - Gap * 0.25f;
				Bridge.Bounds.Max.Y =
					Second.Bounds.Min.Y + Gap * 0.25f;
				const float WidthInset =
					Scope.Bounds.GetSize().X * 0.20f;
				Bridge.Bounds.Min.X += WidthInset;
				Bridge.Bounds.Max.X -= WidthInset;
			}
			Bridge.Role = EABTSM73DAG5BV2VolumeRole::SupportedSpan;
			Bridge.Depth = Scope.Depth + 1;
			Bridge.Path = Scope.Path + TEXT("/Bridge");
			if (!EmitVolume(Bridge, Context))
			{
				return false;
			}
		}
		return true;
	}

	float OverlapLength(
		const float AMin,
		const float AMax,
		const float BMin,
		const float BMax)
	{
		return FMath::Max(
			0.0f,
			FMath::Min(AMax, BMax) - FMath::Max(AMin, BMin));
	}

	FString SemanticRootPath(const FString& Path)
	{
		TArray<FString> Parts;
		Path.ParseIntoArray(Parts, TEXT("/"), true);
		return Parts.Num() >= 2
			? Parts[0] + TEXT("/") + Parts[1]
			: Path;
	}

	bool ResolveSupportedSpans(
		const FABTSM73DAG5BV2PreviewSettings& Settings,
		TArray<FABTSM73DAG5BV2Volume>& Volumes,
		TArray<FString>& Trace)
	{
		TArray<bool> Keep;
		Keep.Init(true, Volumes.Num());
		for (FABTSM73DAG5BV2Volume& Span : Volumes)
		{
			if (!IsSpanRole(Span.Role))
			{
				continue;
			}
			const FVector Size = Span.LocalBounds.GetSize();
			const int32 Axis = Size.X >= Size.Y ? 0 : 1;
			const int32 Perpendicular = Axis == 0 ? 1 : 0;
			const double Center = Span.LocalBounds.GetCenter()[Axis];
			const double MinimumOpening = FMath::Max(
				1.0,
				static_cast<double>(Settings.MinVolumeSpanCM) * 0.25);
			int32 NegativeSupport = INDEX_NONE;
			int32 PositiveSupport = INDEX_NONE;
			double NegativeScore = -1.0;
			double PositiveScore = -1.0;
			for (const FABTSM73DAG5BV2Volume& Candidate : Volumes)
			{
				if (Candidate.VolumeId == Span.VolumeId
					|| IsSpanRole(Candidate.Role))
				{
					continue;
				}
				const double PerpendicularOverlap = OverlapLength(
					Span.LocalBounds.Min[Perpendicular],
					Span.LocalBounds.Max[Perpendicular],
					Candidate.LocalBounds.Min[Perpendicular],
					Candidate.LocalBounds.Max[Perpendicular]);
				const double VerticalOverlap = OverlapLength(
					Span.LocalBounds.Min.Z, Span.LocalBounds.Max.Z,
					Candidate.LocalBounds.Min.Z, Candidate.LocalBounds.Max.Z);
				const double LongitudinalOverlap = OverlapLength(
					Span.LocalBounds.Min[Axis], Span.LocalBounds.Max[Axis],
					Candidate.LocalBounds.Min[Axis], Candidate.LocalBounds.Max[Axis]);
				if (PerpendicularOverlap <= 1.0
					|| VerticalOverlap <= 1.0
					|| LongitudinalOverlap <= 1.0)
				{
					continue;
				}
				const double CandidateCenter =
					Candidate.LocalBounds.GetCenter()[Axis];
				const double Score = PerpendicularOverlap
					* VerticalOverlap * LongitudinalOverlap;
				if (CandidateCenter < Center
					&& Candidate.LocalBounds.Max[Axis]
						<= Center - MinimumOpening * 0.5
					&& Score > NegativeScore)
				{
					NegativeScore = Score;
					NegativeSupport = Candidate.VolumeId;
				}
				else if (CandidateCenter > Center
					&& Candidate.LocalBounds.Min[Axis]
						>= Center + MinimumOpening * 0.5
					&& Score > PositiveScore)
				{
					PositiveScore = Score;
					PositiveSupport = Candidate.VolumeId;
				}
			}
			const FString NegativeRoot = NegativeSupport != INDEX_NONE
				? SemanticRootPath(Volumes[NegativeSupport].DerivationPath)
				: FString();
			const FString PositiveRoot = PositiveSupport != INDEX_NONE
				? SemanticRootPath(Volumes[PositiveSupport].DerivationPath)
				: FString();
			double OpeningMin = NegativeSupport != INDEX_NONE
				? Volumes[NegativeSupport].LocalBounds.Max[Axis]
				: 0.0;
			double OpeningMax = PositiveSupport != INDEX_NONE
				? Volumes[PositiveSupport].LocalBounds.Min[Axis]
				: 0.0;
			if (NegativeSupport != INDEX_NONE
				&& PositiveSupport != INDEX_NONE
				&& NegativeRoot != PositiveRoot)
			{
				for (const FABTSM73DAG5BV2Volume& Candidate : Volumes)
				{
					const FString CandidateRoot =
						SemanticRootPath(Candidate.DerivationPath);
					const double PerpendicularOverlap = OverlapLength(
						Span.LocalBounds.Min[Perpendicular],
						Span.LocalBounds.Max[Perpendicular],
						Candidate.LocalBounds.Min[Perpendicular],
						Candidate.LocalBounds.Max[Perpendicular]);
					if (PerpendicularOverlap <= 1.0
						|| Candidate.LocalBounds.Min.Z
							>= Span.LocalBounds.Min.Z - 1.0)
					{
						continue;
					}
					if (CandidateRoot == NegativeRoot)
					{
						OpeningMin = FMath::Max(
							OpeningMin, Candidate.LocalBounds.Max[Axis]);
					}
					else if (CandidateRoot == PositiveRoot)
					{
						OpeningMax = FMath::Min(
							OpeningMax, Candidate.LocalBounds.Min[Axis]);
					}
				}
			}
			const bool bHasClearOpening =
				NegativeSupport != INDEX_NONE
				&& PositiveSupport != INDEX_NONE
				&& NegativeRoot != PositiveRoot
				&& OpeningMax - OpeningMin >= MinimumOpening;
			bool bUndercroftIsEmpty = bHasClearOpening;
			if (bUndercroftIsEmpty)
			{
				FBox Undercroft = Span.LocalBounds;
				Undercroft.Min[Axis] = OpeningMin;
				Undercroft.Max[Axis] = OpeningMax;
				Undercroft.Min.Z = 0.0;
				Undercroft.Max.Z = Span.LocalBounds.Min.Z;
				for (const FABTSM73DAG5BV2Volume& Candidate : Volumes)
				{
					const FString CandidateRoot =
						SemanticRootPath(Candidate.DerivationPath);
					if (Candidate.VolumeId == Span.VolumeId
						|| CandidateRoot == NegativeRoot
						|| CandidateRoot == PositiveRoot)
					{
						continue;
					}
					const double XOverlap = OverlapLength(
						Undercroft.Min.X, Undercroft.Max.X,
						Candidate.LocalBounds.Min.X,
						Candidate.LocalBounds.Max.X);
					const double YOverlap = OverlapLength(
						Undercroft.Min.Y, Undercroft.Max.Y,
						Candidate.LocalBounds.Min.Y,
						Candidate.LocalBounds.Max.Y);
					const double ZOverlap = OverlapLength(
						Undercroft.Min.Z, Undercroft.Max.Z,
						Candidate.LocalBounds.Min.Z,
						Candidate.LocalBounds.Max.Z);
					if (XOverlap > 1.0 && YOverlap > 1.0 && ZOverlap > 1.0)
					{
						bUndercroftIsEmpty = false;
						break;
					}
				}
			}
			if (NegativeSupport == INDEX_NONE
				|| PositiveSupport == INDEX_NONE
				|| NegativeSupport == PositiveSupport
				|| !bHasClearOpening
				|| !bUndercroftIsEmpty)
			{
				Keep[Span.VolumeId] = false;
				Trace.Add(Span.DerivationPath
					+ TEXT(" -> RejectSingleEndedSpan"));
				continue;
			}
			Span.Role = EABTSM73DAG5BV2VolumeRole::SupportedSpan;
			Span.NegativeSupportVolumeId = NegativeSupport;
			Span.PositiveSupportVolumeId = PositiveSupport;
			Span.SpanAxisIndex = Axis;
			Span.SpanOpeningMinCM = OpeningMin;
			Span.SpanOpeningMaxCM = OpeningMax;
		}

		TArray<int32> Remap;
		Remap.Init(INDEX_NONE, Volumes.Num());
		TArray<FABTSM73DAG5BV2Volume> Accepted;
		Accepted.Reserve(Volumes.Num());
		for (int32 OldId = 0; OldId < Volumes.Num(); ++OldId)
		{
			if (!Keep[OldId])
			{
				continue;
			}
			Remap[OldId] = Accepted.Num();
			FABTSM73DAG5BV2Volume Volume = Volumes[OldId];
			Volume.VolumeId = Accepted.Num();
			Accepted.Add(MoveTemp(Volume));
		}
		for (FABTSM73DAG5BV2Volume& Volume : Accepted)
		{
			if (Volume.Role != EABTSM73DAG5BV2VolumeRole::SupportedSpan)
			{
				continue;
			}
			if (!Remap.IsValidIndex(Volume.NegativeSupportVolumeId)
				|| !Remap.IsValidIndex(Volume.PositiveSupportVolumeId)
				|| Remap[Volume.NegativeSupportVolumeId] == INDEX_NONE
				|| Remap[Volume.PositiveSupportVolumeId] == INDEX_NONE)
			{
				return false;
			}
			Volume.NegativeSupportVolumeId =
				Remap[Volume.NegativeSupportVolumeId];
			Volume.PositiveSupportVolumeId =
				Remap[Volume.PositiveSupportVolumeId];
		}
		Volumes = MoveTemp(Accepted);
		return true;
	}

	double GroundAxisGap(
		const double AMin,
		const double AMax,
		const double BMin,
		const double BMax)
	{
		return FMath::Max(
			0.0,
			FMath::Max(AMin - BMax, BMin - AMax));
	}

	bool GroundFootprintsTouch(const FBox& A, const FBox& B)
	{
		const double XOverlap = OverlapLength(
			A.Min.X, A.Max.X, B.Min.X, B.Max.X);
		const double YOverlap = OverlapLength(
			A.Min.Y, A.Max.Y, B.Min.Y, B.Max.Y);
		const double XGap = GroundAxisGap(
			A.Min.X, A.Max.X, B.Min.X, B.Max.X);
		const double YGap = GroundAxisGap(
			A.Min.Y, A.Max.Y, B.Min.Y, B.Max.Y);
		return (XOverlap > 1.0 && YGap <= 1.0)
			|| (YOverlap > 1.0 && XGap <= 1.0)
			|| (XOverlap > 1.0 && YOverlap > 1.0);
	}

	int32 CountGroundFootprintComponents(
		const TArray<int32>& GroundIndices,
		const TArray<FABTSM73DAG5BV2Volume>& Volumes)
	{
		TArray<bool> Visited;
		Visited.Init(false, GroundIndices.Num());
		int32 ComponentCount = 0;
		for (int32 Start = 0; Start < GroundIndices.Num(); ++Start)
		{
			if (Visited[Start])
			{
				continue;
			}
			++ComponentCount;
			TArray<int32> Queue{Start};
			Visited[Start] = true;
			for (int32 Head = 0; Head < Queue.Num(); ++Head)
			{
				const FBox& Current =
					Volumes[GroundIndices[Queue[Head]]].LocalBounds;
				for (int32 Other = 0; Other < GroundIndices.Num(); ++Other)
				{
					if (!Visited[Other]
						&& GroundFootprintsTouch(
							Current,
							Volumes[GroundIndices[Other]].LocalBounds))
					{
						Visited[Other] = true;
						Queue.Add(Other);
					}
				}
			}
		}
		return ComponentCount;
	}

	double DeriveCoupledPodiumTopZ(const double MinimumGroundTop)
	{
		// A podium contains complete adjacent X/Y course pairs and leaves another
		// pair for the original branch above. Its height is derived from the
		// shortest grounded WFC branch, never from a fixed centimetre preset.
		const int32 AvailableCourses = FMath::FloorToInt(MinimumGroundTop / 36.0);
		const int32 MaximumPodiumCourses = FMath::Max(0, (AvailableCourses - 2) & ~1);
		const int32 HalfHeightCourses =
			(FMath::FloorToInt(MinimumGroundTop * 0.5 / 36.0)) & ~1;
		const int32 PodiumCourses = FMath::Min(
			MaximumPodiumCourses, FMath::Max(2, HalfHeightCourses));
		return PodiumCourses >= 2 ? PodiumCourses * 36.0 : 0.0;
	}

	struct FSemanticPodiumPlan
	{
		FString TraceRoot;
		TArray<FString> RootPaths;
		TArray<int32> GroundIndices;
		FBox Bounds = FBox(EForceInit::ForceInit);
		double LegacyTopZ = 0.0;
		double SemanticSeamZ = 0.0;
		double TopZ = 0.0;
		bool bUsesSemanticSeam = false;
		bool bSelectedCandidateStartsCrown = false;
		int32 AddedCellVolumeId = INDEX_NONE;
	};

	double QuantizePodiumTopAtOrBelow(const double SemanticSeamZ)
	{
		const int32 SeamCourse = FMath::FloorToInt(SemanticSeamZ / 36.0);
		return static_cast<double>(SeamCourse & ~1) * 36.0;
	}

	bool RootHasBodyContinuationAboveSemanticSeam(
		const FString& RootPath,
		const double SemanticSeamZ,
		const double QuantizedTopZ,
		const TArray<FABTSM73DAG5BV2Volume>& Volumes)
	{
		// A semantic seam may fall between 36 cm courses. The upper leaf will be
		// re-cut down to QuantizedTopZ, so require one real 36x36 Body column that
		// starts at/crosses the seam and remains for a complete X/Y course pair.
		for (const FABTSM73DAG5BV2Volume& Volume : Volumes)
		{
			if (IsSpanRole(Volume.Role)
				|| Volume.Role == EABTSM73DAG5BV2VolumeRole::Crown
				|| SemanticRootPath(Volume.DerivationPath) != RootPath
				|| Volume.LocalBounds.Min.Z > SemanticSeamZ + 1.0
				|| Volume.LocalBounds.Max.Z < QuantizedTopZ + 72.0 - 1.0)
			{
				continue;
			}
			const FVector Size = Volume.LocalBounds.GetSize();
			if (Size.X >= 36.0 - 1.0 && Size.Y >= 36.0 - 1.0)
			{
				return true;
			}
		}
		return false;
	}

	bool RootWouldConsumeCrownBelowSemanticSeam(
		const FString& RootPath,
		const double SemanticSeamZ,
		const TArray<FABTSM73DAG5BV2Volume>& Volumes)
	{
		return Volumes.ContainsByPredicate(
			[&RootPath, SemanticSeamZ](const FABTSM73DAG5BV2Volume& Volume)
			{
				return Volume.Role == EABTSM73DAG5BV2VolumeRole::Crown
					&& SemanticRootPath(Volume.DerivationPath) == RootPath
					&& Volume.LocalBounds.Min.Z < SemanticSeamZ - 1.0;
			});
	}

	double SelectHighestLegalSemanticPodiumTop(
		const FString& Scope,
		const TArray<FString>& RootPaths,
		const double LegacyTopZ,
		const TArray<FABTSM73DAG5BV2Volume>& Volumes,
		const bool bAppliedToEnvelope,
		TArray<FABTSM73DAG5BV2SemanticPodiumCandidateDiagnostic>&
			OutCandidateDiagnostics,
		TArray<FABTSM73DAG5BV2SemanticPodiumSelectionDiagnostic>&
			OutSelectionDiagnostics,
		double& OutSemanticSeamZ,
		bool& bOutUsesSemanticSeam,
		bool& bOutSelectedCandidateStartsCrown)
	{
		const int32 FirstCandidateIndex = OutCandidateDiagnostics.Num();
		TArray<double> SemanticSeams;
		double ProtectedSpanTopZ = TNumericLimits<double>::Max();
		for (const FABTSM73DAG5BV2Volume& Volume : Volumes)
		{
			if (Volume.Role == EABTSM73DAG5BV2VolumeRole::SupportedSpan)
			{
				const bool bIncidentRoot = RootPaths.ContainsByPredicate(
					[&Volume, &Volumes](const FString& RootPath)
					{
						const bool bNegative = Volumes.IsValidIndex(
							Volume.NegativeSupportVolumeId)
							&& SemanticRootPath(Volumes[
								Volume.NegativeSupportVolumeId].DerivationPath)
								== RootPath;
						const bool bPositive = Volumes.IsValidIndex(
							Volume.PositiveSupportVolumeId)
							&& SemanticRootPath(Volumes[
								Volume.PositiveSupportVolumeId].DerivationPath)
								== RootPath;
						return bNegative || bPositive;
					});
				if (bIncidentRoot)
				{
					ProtectedSpanTopZ = FMath::Min(
						ProtectedSpanTopZ, Volume.LocalBounds.Min.Z);
				}
				continue;
			}
			if (!RootPaths.Contains(SemanticRootPath(Volume.DerivationPath))
				|| Volume.LocalBounds.Min.Z <= LegacyTopZ + 1.0)
			{
				continue;
			}
			SemanticSeams.AddUnique(Volume.LocalBounds.Min.Z);
		}
		SemanticSeams.Sort([](const double A, const double B)
		{
			return A > B;
		});

		for (const double SemanticSeamZ : SemanticSeams)
		{
			bool bHasBodyStart = false;
			bool bHasCrownStart = false;
			for (const FABTSM73DAG5BV2Volume& Volume : Volumes)
			{
				if (!RootPaths.Contains(SemanticRootPath(Volume.DerivationPath))
					|| FMath::Abs(Volume.LocalBounds.Min.Z - SemanticSeamZ) > 1.0)
				{
					continue;
				}
				bHasCrownStart |= Volume.Role
					== EABTSM73DAG5BV2VolumeRole::Crown;
				bHasBodyStart |= Volume.Role
					!= EABTSM73DAG5BV2VolumeRole::Crown;
			}
			const bool bCandidateStartsCrown = bHasCrownStart && !bHasBodyStart;
			const double QuantizedTopZ = QuantizePodiumTopAtOrBelow(
				SemanticSeamZ - (bCandidateStartsCrown ? 72.0 : 0.0));
			FABTSM73DAG5BV2SemanticPodiumCandidateDiagnostic& Diagnostic =
				OutCandidateDiagnostics.AddDefaulted_GetRef();
			Diagnostic.Scope = Scope;
			Diagnostic.LegacyTopZ = LegacyTopZ;
			Diagnostic.SemanticSeamZ = SemanticSeamZ;
			Diagnostic.QuantizedTopZ = QuantizedTopZ;
			Diagnostic.ProtectedSpanTopZ = ProtectedSpanTopZ;
			Diagnostic.bCandidateStartsCrown = bCandidateStartsCrown;
			if (QuantizedTopZ <= LegacyTopZ + 1.0)
			{
				Diagnostic.RejectReason = TEXT("QuantizedNotAboveLegacy");
				continue;
			}
			if (SemanticSeamZ > ProtectedSpanTopZ + 1.0)
			{
				Diagnostic.RejectReason = TEXT("AboveIncidentSupportedSpanUnderside");
				continue;
			}
			for (const FString& RootPath : RootPaths)
			{
				if (RootWouldConsumeCrownBelowSemanticSeam(
					RootPath, SemanticSeamZ, Volumes))
				{
					Diagnostic.RejectReason = FString::Printf(
						TEXT("ConsumesCrown:%s"), *RootPath);
					break;
				}
				if (!RootHasBodyContinuationAboveSemanticSeam(
					RootPath, SemanticSeamZ, QuantizedTopZ, Volumes))
				{
					Diagnostic.RejectReason = FString::Printf(
						TEXT("MissingTwoCourseBodyContinuation:%s"), *RootPath);
					break;
				}
			}
			if (Diagnostic.RejectReason.IsEmpty())
			{
				Diagnostic.bAccepted = true;
				OutSemanticSeamZ = SemanticSeamZ;
				bOutUsesSemanticSeam = true;
				bOutSelectedCandidateStartsCrown = bCandidateStartsCrown;
				FABTSM73DAG5BV2SemanticPodiumSelectionDiagnostic& Selection =
					OutSelectionDiagnostics.AddDefaulted_GetRef();
				Selection.Scope = Scope;
				Selection.RootCount = RootPaths.Num();
				Selection.CandidateCount = OutCandidateDiagnostics.Num()
					- FirstCandidateIndex;
				Selection.LegacyTopZ = LegacyTopZ;
				Selection.SelectedSemanticSeamZ = SemanticSeamZ;
				Selection.SelectedTopZ = QuantizedTopZ;
				Selection.bUsesSemanticSeam = true;
				Selection.bSelectedCandidateStartsCrown =
					bCandidateStartsCrown;
				Selection.bAppliedToEnvelope = bAppliedToEnvelope;
				return QuantizedTopZ;
			}
		}

		OutSemanticSeamZ = LegacyTopZ;
		bOutUsesSemanticSeam = false;
		bOutSelectedCandidateStartsCrown = false;
		FABTSM73DAG5BV2SemanticPodiumSelectionDiagnostic& Selection =
			OutSelectionDiagnostics.AddDefaulted_GetRef();
		Selection.Scope = Scope;
		Selection.RootCount = RootPaths.Num();
		Selection.CandidateCount = OutCandidateDiagnostics.Num()
			- FirstCandidateIndex;
		Selection.LegacyTopZ = LegacyTopZ;
		Selection.SelectedSemanticSeamZ = LegacyTopZ;
		Selection.SelectedTopZ = LegacyTopZ;
		Selection.bAppliedToEnvelope = bAppliedToEnvelope;
		return LegacyTopZ;
	}

	bool ApplySemanticPodiumPlans(
		const FABTSM73DAG5BV2PreviewSettings& Settings,
		TArray<FSemanticPodiumPlan>& Plans,
		TArray<FABTSM73DAG5BV2Volume>& Volumes,
		TArray<FString>& Trace,
		FString& OutError)
	{
		if (Plans.IsEmpty())
		{
			return true;
		}

		TMap<FString, int32> RootToPlan;
		for (int32 PlanIndex = 0; PlanIndex < Plans.Num(); ++PlanIndex)
		{
			for (const FString& RootPath : Plans[PlanIndex].RootPaths)
			{
				if (RootToPlan.Contains(RootPath))
				{
					OutError = TEXT("DAG5BV2GroundCouplingRootAssignedTwice");
					return false;
				}
				RootToPlan.Add(RootPath, PlanIndex);
			}
		}

		TArray<bool> Keep;
		Keep.Init(true, Volumes.Num());
		TArray<FABTSM73DAG5BV2Volume> RecutVolumes = Volumes;
		for (int32 VolumeIndex = 0; VolumeIndex < RecutVolumes.Num(); ++VolumeIndex)
		{
			FABTSM73DAG5BV2Volume& Volume = RecutVolumes[VolumeIndex];
			if (IsSpanRole(Volume.Role))
			{
				continue;
			}
			const int32* PlanIndex = RootToPlan.Find(
				SemanticRootPath(Volume.DerivationPath));
			if (PlanIndex == nullptr || !Plans.IsValidIndex(*PlanIndex))
			{
				continue;
			}
			const FSemanticPodiumPlan& Plan = Plans[*PlanIndex];
			const double RecutSeamZ = Plan.bSelectedCandidateStartsCrown
				? Plan.TopZ : Plan.SemanticSeamZ;
			if (Plan.bUsesSemanticSeam
				&& Volume.LocalBounds.Max.Z <= RecutSeamZ + 1.0)
			{
				Keep[VolumeIndex] = false;
				continue;
			}
			if (Volume.LocalBounds.Min.Z < Plan.TopZ + 1.0
				|| (Plan.bUsesSemanticSeam
					&& Volume.LocalBounds.Min.Z <= RecutSeamZ + 1.0))
			{
				Volume.LocalBounds.Min.Z = Plan.TopZ;
			}
			if (Volume.LocalBounds.Max.Z - Volume.LocalBounds.Min.Z <= 1.0)
			{
				OutError = FString::Printf(
					TEXT("DAG5BV2GroundCouplingRecutVolumeInvalid:Volume=%d:Plan=%d:MinZ=%.3f:MaxZ=%.3f"),
					VolumeIndex, *PlanIndex, Volume.LocalBounds.Min.Z,
					Volume.LocalBounds.Max.Z);
				return false;
			}
		}

		TArray<int32> Remap;
		Remap.Init(INDEX_NONE, RecutVolumes.Num());
		TArray<FABTSM73DAG5BV2Volume> Accepted;
		Accepted.Reserve(RecutVolumes.Num() + Plans.Num());
		for (int32 OldId = 0; OldId < RecutVolumes.Num(); ++OldId)
		{
			if (!Keep[OldId])
			{
				continue;
			}
			Remap[OldId] = Accepted.Num();
			FABTSM73DAG5BV2Volume Volume = RecutVolumes[OldId];
			Volume.VolumeId = Accepted.Num();
			Accepted.Add(MoveTemp(Volume));
		}

		for (int32 PlanIndex = 0; PlanIndex < Plans.Num(); ++PlanIndex)
		{
			FSemanticPodiumPlan& Plan = Plans[PlanIndex];
			Plan.AddedCellVolumeId = Accepted.Num();
			FABTSM73DAG5BV2Volume& Cell = Accepted.AddDefaulted_GetRef();
			Cell.VolumeId = Plan.AddedCellVolumeId;
			Cell.LocalBounds = Plan.Bounds;
			Cell.LocalBounds.Min.Z = 0.0;
			Cell.LocalBounds.Max.Z = Plan.TopZ;
			Cell.Role = EABTSM73DAG5BV2VolumeRole::Foundation;
			Cell.Primitive = EABTSM73DAG5BV2Primitive::Box;
			Cell.DerivationPath = Plans.Num() == 1
				&& Plan.TraceRoot == TEXT("AllGroundRoots")
				? TEXT("CoupledGround/Cell/0")
				: FString::Printf(
					TEXT("CoupledGround/Root/%s/%d"),
					*Plan.TraceRoot, PlanIndex);
		}

		auto ResolveSupportId = [&Volumes, &Remap, &RootToPlan, &Plans](
			const int32 OldSupportId) -> int32
		{
			if (Remap.IsValidIndex(OldSupportId)
				&& Remap[OldSupportId] != INDEX_NONE)
			{
				return Remap[OldSupportId];
			}
			if (!Volumes.IsValidIndex(OldSupportId))
			{
				return INDEX_NONE;
			}
			const int32* PlanIndex = RootToPlan.Find(SemanticRootPath(
				Volumes[OldSupportId].DerivationPath));
			return PlanIndex != nullptr && Plans.IsValidIndex(*PlanIndex)
				? Plans[*PlanIndex].AddedCellVolumeId
				: INDEX_NONE;
		};
		for (FABTSM73DAG5BV2Volume& Volume : Accepted)
		{
			if (Volume.Role != EABTSM73DAG5BV2VolumeRole::SupportedSpan)
			{
				continue;
			}
			Volume.NegativeSupportVolumeId = ResolveSupportId(
				Volume.NegativeSupportVolumeId);
			Volume.PositiveSupportVolumeId = ResolveSupportId(
				Volume.PositiveSupportVolumeId);
			if (Volume.NegativeSupportVolumeId == INDEX_NONE
				|| Volume.PositiveSupportVolumeId == INDEX_NONE
				|| Volume.NegativeSupportVolumeId
					== Volume.PositiveSupportVolumeId)
			{
				OutError = FString::Printf(
					TEXT("DAG5BV2GroundCouplingSpanSupportRemapInvalid:Span=%d"),
					Volume.VolumeId);
				return false;
			}
		}

		if (Accepted.Num() > Settings.MaxVolumeCount)
		{
			OutError = TEXT("DAG5BV2GroundCouplingVolumeBudgetExceeded");
			return false;
		}
		for (const FSemanticPodiumPlan& Plan : Plans)
		{
			Trace.Add(FString::Printf(
				TEXT("%s -> CoupledGround SemanticSeam(%.3f) TopZ(%.3f) LegacyTopZ(%.3f) Selection(%s) Roots(%d)"),
				*Plan.TraceRoot, Plan.SemanticSeamZ, Plan.TopZ,
				Plan.LegacyTopZ,
				Plan.bUsesSemanticSeam ? TEXT("HighestLegal") : TEXT("LegacyFallback"),
				Plan.RootPaths.Num()));
		}
		Volumes = MoveTemp(Accepted);
		return true;
	}

	bool CoupleGroundLayer(
		const FABTSM73DAG5BV2PreviewSettings& Settings,
		TArray<FABTSM73DAG5BV2Volume>& Volumes,
		TArray<FString>& Trace,
		TArray<FABTSM73DAG5BV2SemanticPodiumCandidateDiagnostic>&
			OutCandidateDiagnostics,
		TArray<FABTSM73DAG5BV2SemanticPodiumSelectionDiagnostic>&
			OutSelectionDiagnostics,
		FString& OutError)
	{
		TArray<int32> GroundIndices;
		FBox GroundEnvelope(EForceInit::ForceInit);
		double MinimumGroundTop = TNumericLimits<double>::Max();
		for (int32 Index = 0; Index < Volumes.Num(); ++Index)
		{
			const FABTSM73DAG5BV2Volume& Volume = Volumes[Index];
			if (IsSpanRole(Volume.Role) || Volume.LocalBounds.Min.Z > 1.0)
			{
				continue;
			}
			GroundIndices.Add(Index);
			GroundEnvelope += Volume.LocalBounds;
			MinimumGroundTop = FMath::Min(
				MinimumGroundTop, Volume.LocalBounds.Max.Z);
		}
		const bool bHasSupportedSpan = Volumes.ContainsByPredicate(
			[](const FABTSM73DAG5BV2Volume& Volume)
			{
				return Volume.Role
					== EABTSM73DAG5BV2VolumeRole::SupportedSpan;
			});
		TArray<FSemanticPodiumPlan> Plans;
		if (bHasSupportedSpan)
		{
			struct FSemanticGroundGroup
			{
				FString RootPath;
				TArray<int32> Indices;
			};
			TArray<FSemanticGroundGroup> Groups;
			for (const int32 Index : GroundIndices)
			{
				const FString RootPath =
					SemanticRootPath(Volumes[Index].DerivationPath);
				FSemanticGroundGroup* Group = Groups.FindByPredicate(
					[&RootPath](const FSemanticGroundGroup& Candidate)
					{
						return Candidate.RootPath == RootPath;
					});
				if (Group == nullptr)
				{
					FSemanticGroundGroup& Added =
						Groups.AddDefaulted_GetRef();
					Added.RootPath = RootPath;
					Group = &Added;
				}
				Group->Indices.Add(Index);
			}
			Groups.Sort([](
				const FSemanticGroundGroup& A,
				const FSemanticGroundGroup& B)
			{
				return A.RootPath < B.RootPath;
			});

			for (const FSemanticGroundGroup& Group : Groups)
			{
				if (Group.Indices.Num() < 2
					|| CountGroundFootprintComponents(
						Group.Indices, Volumes) <= 1)
				{
					continue;
				}
				FSemanticPodiumPlan& Plan =
					Plans.AddDefaulted_GetRef();
				Plan.TraceRoot = Group.RootPath;
				Plan.RootPaths.Add(Group.RootPath);
				Plan.GroundIndices = Group.Indices;
				double GroupMinimumTop = TNumericLimits<double>::Max();
				for (const int32 Index : Group.Indices)
				{
					Plan.Bounds += Volumes[Index].LocalBounds;
					GroupMinimumTop = FMath::Min(
						GroupMinimumTop, Volumes[Index].LocalBounds.Max.Z);
				}
				Plan.LegacyTopZ = DeriveCoupledPodiumTopZ(GroupMinimumTop);
				Plan.TopZ = SelectHighestLegalSemanticPodiumTop(
					Plan.TraceRoot, Plan.RootPaths, Plan.LegacyTopZ, Volumes,
					true, OutCandidateDiagnostics, OutSelectionDiagnostics,
					Plan.SemanticSeamZ, Plan.bUsesSemanticSeam,
					Plan.bSelectedCandidateStartsCrown);
				Plan.Bounds.Min.Z = 0.0;
				Plan.Bounds.Max.Z = Plan.TopZ;
				if (!FMath::IsFinite(Plan.TopZ) || Plan.TopZ <= 1.0)
				{
					OutError = TEXT("DAG5BV2GroundCouplingHeightInvalid");
					return false;
				}
			}
			if (Plans.Num() > 1)
			{
				TArray<FString> CommonRoots;
				double CommonLegacyTopZ = 0.0;
				for (const FSemanticPodiumPlan& Plan : Plans)
				{
					for (const FString& RootPath : Plan.RootPaths)
					{
						CommonRoots.AddUnique(RootPath);
					}
					CommonLegacyTopZ = FMath::Max(
						CommonLegacyTopZ, Plan.LegacyTopZ);
				}
				CommonRoots.Sort();
				double DiagnosticSeamZ = CommonLegacyTopZ;
				bool bDiagnosticUsesSeam = false;
				bool bDiagnosticStartsCrown = false;
				SelectHighestLegalSemanticPodiumTop(
					TEXT("AllCoupledRootsCommonProbe"), CommonRoots,
					CommonLegacyTopZ, Volumes, false,
					OutCandidateDiagnostics, OutSelectionDiagnostics,
					DiagnosticSeamZ, bDiagnosticUsesSeam,
					bDiagnosticStartsCrown);
			}
			return ApplySemanticPodiumPlans(
				Settings, Plans, Volumes, Trace, OutError);
		}
		if (GroundIndices.Num() < 2
			|| CountGroundFootprintComponents(GroundIndices, Volumes) <= 1)
		{
			return true;
		}

		FSemanticPodiumPlan& Plan = Plans.AddDefaulted_GetRef();
		Plan.TraceRoot = TEXT("AllGroundRoots");
		Plan.GroundIndices = GroundIndices;
		Plan.Bounds = GroundEnvelope;
		for (const int32 Index : GroundIndices)
		{
			Plan.RootPaths.AddUnique(SemanticRootPath(
				Volumes[Index].DerivationPath));
		}
		Plan.RootPaths.Sort();
		Plan.LegacyTopZ = DeriveCoupledPodiumTopZ(MinimumGroundTop);
		Plan.TopZ = SelectHighestLegalSemanticPodiumTop(
			Plan.TraceRoot, Plan.RootPaths, Plan.LegacyTopZ, Volumes,
			true, OutCandidateDiagnostics, OutSelectionDiagnostics,
			Plan.SemanticSeamZ, Plan.bUsesSemanticSeam,
			Plan.bSelectedCandidateStartsCrown);
		Plan.Bounds.Min.Z = 0.0;
		Plan.Bounds.Max.Z = Plan.TopZ;
		if (!FMath::IsFinite(Plan.TopZ) || Plan.TopZ <= 1.0)
		{
			OutError = TEXT("DAG5BV2GroundCouplingHeightInvalid");
			return false;
		}
		return ApplySemanticPodiumPlans(
			Settings, Plans, Volumes, Trace, OutError);
	}

	void BuildAdjacency(
		const TArray<FABTSM73DAG5BV2Volume>& Volumes,
		TArray<FAdjacencyEdge>& OutEdges,
		TArray<bool>& OutHasAbove)
	{
		OutEdges.Reset();
		OutHasAbove.Init(false, Volumes.Num());
		for (int32 A = 0; A < Volumes.Num(); ++A)
		{
			for (int32 B = A + 1; B < Volumes.Num(); ++B)
			{
				const FBox& BoxA = Volumes[A].LocalBounds;
				const FBox& BoxB = Volumes[B].LocalBounds;
				const float XOverlap = OverlapLength(
					BoxA.Min.X,
					BoxA.Max.X,
					BoxB.Min.X,
					BoxB.Max.X);
				const float YOverlap = OverlapLength(
					BoxA.Min.Y,
					BoxA.Max.Y,
					BoxB.Min.Y,
					BoxB.Max.Y);
				const float ZOverlap = OverlapLength(
					BoxA.Min.Z,
					BoxA.Max.Z,
					BoxB.Min.Z,
					BoxB.Max.Z);
				const bool bAUnderB =
					FMath::Abs(BoxA.Max.Z - BoxB.Min.Z) <= 1.0f
					&& XOverlap > 1.0f
					&& YOverlap > 1.0f;
				const bool bBUnderA =
					FMath::Abs(BoxB.Max.Z - BoxA.Min.Z) <= 1.0f
					&& XOverlap > 1.0f
					&& YOverlap > 1.0f;
				if (bAUnderB || bBUnderA)
				{
					FAdjacencyEdge& Edge =
						OutEdges.AddDefaulted_GetRef();
					Edge.A = bAUnderB ? A : B;
					Edge.B = bAUnderB ? B : A;
					Edge.Type = EAdjacency::Vertical;
					OutHasAbove[Edge.A] = true;
					continue;
				}

				const float NearX = FMath::Min(
					FMath::Abs(BoxA.Max.X - BoxB.Min.X),
					FMath::Abs(BoxB.Max.X - BoxA.Min.X));
				const float NearY = FMath::Min(
					FMath::Abs(BoxA.Max.Y - BoxB.Min.Y),
					FMath::Abs(BoxB.Max.Y - BoxA.Min.Y));
				if (NearX <= 1.0f && YOverlap > 1.0f && ZOverlap > 1.0f)
				{
					OutEdges.Add({A, B, EAdjacency::HorizontalX});
				}
				else if (
					NearY <= 1.0f
					&& XOverlap > 1.0f
					&& ZOverlap > 1.0f)
				{
					OutEdges.Add({A, B, EAdjacency::HorizontalY});
				}
			}
		}
	}

	bool RoofLeavesAreNeighbors(
		const FABTSM73DAG5BV2Volume& A,
		const FABTSM73DAG5BV2Volume& B,
		const FABTSM73DAG5BV2PreviewSettings& Settings)
	{
		if (!FMath::IsNearlyEqual(
				A.LocalBounds.Min.Z, B.LocalBounds.Min.Z, 1.0)
			|| !FMath::IsNearlyEqual(
				A.LocalBounds.Max.Z, B.LocalBounds.Max.Z, 1.0))
		{
			return false;
		}
		const double XOverlap = OverlapLength(
			A.LocalBounds.Min.X, A.LocalBounds.Max.X,
			B.LocalBounds.Min.X, B.LocalBounds.Max.X);
		const double YOverlap = OverlapLength(
			A.LocalBounds.Min.Y, A.LocalBounds.Max.Y,
			B.LocalBounds.Min.Y, B.LocalBounds.Max.Y);
		const double XGap = FMath::Max(
			0.0,
			FMath::Max(
				A.LocalBounds.Min.X - B.LocalBounds.Max.X,
				B.LocalBounds.Min.X - A.LocalBounds.Max.X));
		const double YGap = FMath::Max(
			0.0,
			FMath::Max(
				A.LocalBounds.Min.Y - B.LocalBounds.Max.Y,
				B.LocalBounds.Min.Y - A.LocalBounds.Max.Y));
		return (YOverlap > 1.0 && XGap <= Settings.RoofMergeGapCM)
			|| (XOverlap > 1.0 && YGap <= Settings.RoofMergeGapCM);
	}

	bool AggregateRoofTerminals(
		const FABTSM73DAG5BV2PreviewSettings& Settings,
		TArray<FABTSM73DAG5BV2Volume>& Volumes,
		TArray<FString>& Trace,
		FABTSM73DAG5BV2PreviewSummary& Summary)
	{
		TArray<FAdjacencyEdge> InitialEdges;
		TArray<bool> HasAbove;
		BuildAdjacency(Volumes, InitialEdges, HasAbove);
		TArray<int32> Candidates;
		for (int32 Index = 0; Index < Volumes.Num(); ++Index)
		{
			if (!HasAbove[Index]
				&& !IsSpanRole(Volumes[Index].Role)
				&& Volumes[Index].Role
					!= EABTSM73DAG5BV2VolumeRole::Foundation)
			{
				Candidates.Add(Index);
			}
		}
		if (Candidates.IsEmpty())
		{
			return false;
		}

		TArray<bool> CandidateVisited;
		CandidateVisited.Init(false, Volumes.Num());
		TArray<bool> Keep;
		Keep.Init(true, Volumes.Num());
		TArray<int32> Replacement;
		Replacement.Init(INDEX_NONE, Volumes.Num());
		for (const int32 Start : Candidates)
		{
			if (CandidateVisited[Start])
			{
				continue;
			}
			TArray<int32> Component;
			TArray<int32> Queue;
			Queue.Add(Start);
			CandidateVisited[Start] = true;
			for (int32 QueueIndex = 0; QueueIndex < Queue.Num(); ++QueueIndex)
			{
				const int32 Current = Queue[QueueIndex];
				Component.Add(Current);
				for (const int32 Other : Candidates)
				{
					if (!CandidateVisited[Other]
						&& RoofLeavesAreNeighbors(
							Volumes[Current], Volumes[Other], Settings))
					{
						CandidateVisited[Other] = true;
						Queue.Add(Other);
					}
				}
			}

			Component.Sort();
			const int32 Representative = Component[0];
			FBox Envelope = Volumes[Representative].LocalBounds;
			double SourceArea = 0.0;
			for (const int32 Index : Component)
			{
				Envelope += Volumes[Index].LocalBounds;
				const FVector Size = Volumes[Index].LocalBounds.GetSize();
				SourceArea += Size.X * Size.Y;
			}
			const FVector EnvelopeSize = Envelope.GetSize();
			const double EnvelopeArea = EnvelopeSize.X * EnvelopeSize.Y;
			const bool bMerge = Settings.bMergeRoofTerminals
				&& Component.Num() > 1
				&& EnvelopeArea > UE_DOUBLE_SMALL_NUMBER
				&& SourceArea / EnvelopeArea >= 0.55;
			if (!bMerge)
			{
				for (const int32 Index : Component)
				{
					Volumes[Index].Role =
						EABTSM73DAG5BV2VolumeRole::Crown;
					++Summary.RoofTerminalCount;
				}
				continue;
			}

			FABTSM73DAG5BV2Volume& Roof = Volumes[Representative];
			Roof.LocalBounds = Envelope;
			Roof.Role = EABTSM73DAG5BV2VolumeRole::Crown;
			Roof.DerivationPath = SemanticRootPath(Roof.DerivationPath)
				+ FString::Printf(TEXT("/MergedRoof/%d"), Representative);
			for (int32 ComponentIndex = 1;
				ComponentIndex < Component.Num(); ++ComponentIndex)
			{
				const int32 Removed = Component[ComponentIndex];
				Keep[Removed] = false;
				Replacement[Removed] = Representative;
			}
			Summary.MergedRoofSourceCount += Component.Num();
			++Summary.RoofTerminalCount;
			Trace.Add(FString::Printf(
				TEXT("%s -> MergeRoofTerminals(%d)"),
				*Roof.DerivationPath,
				Component.Num()));
		}

		TArray<int32> Remap;
		Remap.Init(INDEX_NONE, Volumes.Num());
		TArray<FABTSM73DAG5BV2Volume> Accepted;
		Accepted.Reserve(Volumes.Num());
		for (int32 OldId = 0; OldId < Volumes.Num(); ++OldId)
		{
			if (!Keep[OldId])
			{
				continue;
			}
			Remap[OldId] = Accepted.Num();
			FABTSM73DAG5BV2Volume Volume = Volumes[OldId];
			Volume.VolumeId = Accepted.Num();
			Accepted.Add(MoveTemp(Volume));
		}
		for (int32 OldId = 0; OldId < Replacement.Num(); ++OldId)
		{
			if (Replacement[OldId] != INDEX_NONE)
			{
				Remap[OldId] = Remap[Replacement[OldId]];
			}
		}
		for (FABTSM73DAG5BV2Volume& Volume : Accepted)
		{
			if (Volume.Role != EABTSM73DAG5BV2VolumeRole::SupportedSpan)
			{
				continue;
			}
			if (!Remap.IsValidIndex(Volume.NegativeSupportVolumeId)
				|| !Remap.IsValidIndex(Volume.PositiveSupportVolumeId)
				|| Remap[Volume.NegativeSupportVolumeId] == INDEX_NONE
				|| Remap[Volume.PositiveSupportVolumeId] == INDEX_NONE)
			{
				return false;
			}
			Volume.NegativeSupportVolumeId =
				Remap[Volume.NegativeSupportVolumeId];
			Volume.PositiveSupportVolumeId =
				Remap[Volume.PositiveSupportVolumeId];
		}
		Volumes = MoveTemp(Accepted);

		TArray<FAdjacencyEdge> Edges;
		TArray<bool> IgnoredHasAbove;
		BuildAdjacency(Volumes, Edges, IgnoredHasAbove);
		TArray<int32> UpperCounts;
		UpperCounts.Init(0, Volumes.Num());
		for (const FAdjacencyEdge& Edge : Edges)
		{
			if (Edge.Type == EAdjacency::Vertical)
			{
				++UpperCounts[Edge.A];
			}
		}
		for (FABTSM73DAG5BV2Volume& Roof : Volumes)
		{
			if (Roof.Role != EABTSM73DAG5BV2VolumeRole::Crown)
			{
				continue;
			}
			TArray<int32> Supports;
			for (const FAdjacencyEdge& Edge : Edges)
			{
				if (Edge.Type == EAdjacency::Vertical
					&& Edge.B == Roof.VolumeId)
				{
					Supports.AddUnique(Edge.A);
				}
			}
			const FVector RoofSize = Roof.LocalBounds.GetSize();
			const double ShortSpan = FMath::Min(RoofSize.X, RoofSize.Y);
			const int32 DesiredCourses = FMath::Clamp(
				FMath::RoundToInt(
					ShortSpan * Settings.RoofHeightToShortSpanRatio
						/ Settings.RoofCourseHeightCM),
				Settings.MinimumRoofCourseCount,
				Settings.MaximumRoofCourseCount);
			const bool bHasExclusiveSupports = !Supports.IsEmpty()
				&& !Supports.ContainsByPredicate(
					[&UpperCounts](const int32 Support)
					{
						return UpperCounts[Support] != 1;
					});
			if (!bHasExclusiveSupports)
			{
				// A support shared by several semantic crowns cannot move to two
				// different roof bottoms. Preserve the contact plane and quantize
				// downward from the existing envelope top instead.
				const int32 QuantizedCourses = FMath::Clamp(
					FMath::FloorToInt(
						RoofSize.Z / Settings.RoofCourseHeightCM),
					2,
					Settings.MaximumRoofCourseCount);
				Roof.LocalBounds.Max.Z = Roof.LocalBounds.Min.Z
					+ QuantizedCourses * Settings.RoofCourseHeightCM;
				Trace.Add(FString::Printf(
					TEXT("%s -> SharedRoofCourses(%d)"),
					*Roof.DerivationPath,
					QuantizedCourses));
				continue;
			}
			int32 FeasibleCourses = Settings.MaximumRoofCourseCount;
			for (const int32 Support : Supports)
			{
				const double AvailableHeight = Roof.LocalBounds.Max.Z
					- Volumes[Support].LocalBounds.Min.Z
					- Settings.RoofCourseHeightCM * 2.0;
				FeasibleCourses = FMath::Min(
					FeasibleCourses,
					FMath::FloorToInt(
						AvailableHeight / Settings.RoofCourseHeightCM));
			}
			if (FeasibleCourses < 2)
			{
				continue;
			}
			const int32 CourseCount = FMath::Clamp(
				FMath::Min(DesiredCourses, FeasibleCourses),
				2,
				Settings.MaximumRoofCourseCount);
			const double NewBottom = Roof.LocalBounds.Max.Z
				- CourseCount * Settings.RoofCourseHeightCM;
			for (const int32 Support : Supports)
			{
				Volumes[Support].LocalBounds.Max.Z = NewBottom;
			}
			Roof.LocalBounds.Min.Z = NewBottom;
			Trace.Add(FString::Printf(
				TEXT("%s -> RoofCourses(%d)"),
				*Roof.DerivationPath,
				CourseCount));
		}
		return true;
	}

	uint8 PrimitiveToMask(
		const EABTSM73DAG5BV2Primitive Primitive)
	{
		switch (Primitive)
		{
		case EABTSM73DAG5BV2Primitive::TriangularPrismX:
			return PrismXMask;
		case EABTSM73DAG5BV2Primitive::TriangularPrismY:
			return PrismYMask;
		case EABTSM73DAG5BV2Primitive::Pyramid:
			return PyramidMask;
		case EABTSM73DAG5BV2Primitive::Box:
		default:
			return BoxMask;
		}
	}

	uint8 LongRidgePrismMask(const FABTSM73DAG5BV2Volume& Volume)
	{
		const FVector Size = Volume.LocalBounds.GetSize();
		// PrismX contracts X and therefore has a Y-aligned ridge. A long X
		// envelope must use PrismY so its ridge follows the long X axis.
		return Size.X >= Size.Y ? PrismYMask : PrismXMask;
	}

	uint8 AspectRoofDomain(const FABTSM73DAG5BV2Volume& Volume)
	{
		return PyramidMask | LongRidgePrismMask(Volume);
	}

	float RoofAspectBlend(
		const FABTSM73DAG5BV2Volume& Volume,
		const FABTSM73DAG5BV2PreviewSettings& Settings)
	{
		const FVector Size = Volume.LocalBounds.GetSize();
		const double ShortSpan = FMath::Max(1.0, FMath::Min(Size.X, Size.Y));
		const double Aspect = FMath::Max(Size.X, Size.Y) / ShortSpan;
		return FMath::Clamp(
			static_cast<float>(
				(Aspect - Settings.PyramidPreferredMaxAspectRatio)
				/ (Settings.PrismPreferredMinAspectRatio
					- Settings.PyramidPreferredMaxAspectRatio)),
			0.0f,
			1.0f);
	}

	EABTSM73DAG5BV2Primitive MaskToPrimitive(const uint8 Mask)
	{
		if ((Mask & PrismXMask) != 0)
		{
			return EABTSM73DAG5BV2Primitive::TriangularPrismX;
		}
		if ((Mask & PrismYMask) != 0)
		{
			return EABTSM73DAG5BV2Primitive::TriangularPrismY;
		}
		if ((Mask & PyramidMask) != 0)
		{
			return EABTSM73DAG5BV2Primitive::Pyramid;
		}
		return EABTSM73DAG5BV2Primitive::Box;
	}

	int32 BitCount(const uint8 Mask)
	{
		int32 Count = 0;
		for (uint8 Bit = 1; Bit <= PyramidMask; Bit <<= 1)
		{
			if ((Mask & Bit) != 0)
			{
				++Count;
			}
		}
		return Count;
	}

	bool Compatible(
		const uint8 A,
		const uint8 B,
		const EAdjacency Type)
	{
		if (Type == EAdjacency::Vertical)
		{
			// Prism and pyramid volumes are terminal roof shapes. Only a box
			// can be the lower endpoint of a vertical adjacency.
			return A == BoxMask;
		}
		if (A == BoxMask || B == BoxMask
			|| A == PyramidMask || B == PyramidMask)
		{
			return true;
		}
		return A == B;
	}

	bool Propagate(
		TArray<uint8>& Domains,
		FWFCContext& Context)
	{
		bool bChanged = true;
		while (bChanged)
		{
			bChanged = false;
			for (const FAdjacencyEdge& Edge : *Context.Edges)
			{
				for (int32 Direction = 0; Direction < 2; ++Direction)
				{
					if (++Context.PropagationOperations
						> Context.Settings->MaxWFCPropagationOperations)
					{
						return false;
					}
					const int32 Source =
						Direction == 0 ? Edge.A : Edge.B;
					const int32 Target =
						Direction == 0 ? Edge.B : Edge.A;
					const uint8 OldMask = Domains[Source];
					uint8 NewMask = 0;
					for (uint8 SourceBit = 1;
						SourceBit <= PyramidMask;
						SourceBit <<= 1)
					{
						if ((OldMask & SourceBit) == 0)
						{
							continue;
						}
						bool bSupported = false;
						for (uint8 TargetBit = 1;
							TargetBit <= PyramidMask;
							TargetBit <<= 1)
						{
							if ((Domains[Target] & TargetBit) == 0)
							{
								continue;
							}
							const bool bCompatible =
								Direction == 0
									? Compatible(
										SourceBit,
										TargetBit,
										Edge.Type)
									: Compatible(
										TargetBit,
										SourceBit,
										Edge.Type);
							if (bCompatible)
							{
								bSupported = true;
								break;
							}
						}
						if (bSupported)
						{
							NewMask |= SourceBit;
						}
					}
					if (NewMask == 0)
					{
						return false;
					}
					if (NewMask != OldMask)
					{
						Domains[Source] = NewMask;
						bChanged = true;
					}
				}
			}
		}
		return true;
	}

	float PrimitiveWeight(
		const uint8 Bit,
		const FABTSM73DAG5BV2PreviewSettings& Settings,
		const FABTSM73DAG5BV2Volume& Volume)
	{
		if (Volume.Role == EABTSM73DAG5BV2VolumeRole::Crown)
		{
			const float PrismBlend = RoofAspectBlend(Volume, Settings);
			if (Bit == PyramidMask)
			{
				return Settings.PyramidWeight
					* FMath::Lerp(2.4f, 0.20f, PrismBlend);
			}
			if (Bit == LongRidgePrismMask(Volume))
			{
				return Settings.PrismWeight
					* FMath::Lerp(0.20f, 2.4f, PrismBlend);
			}
			return 0.0f;
		}
		switch (Bit)
		{
		case PrismXMask:
		case PrismYMask:
			return Settings.PrismWeight * 0.5f;
		case PyramidMask:
			return Settings.PyramidWeight;
		case BoxMask:
		default:
			return Settings.BoxWeight;
		}
	}

	TArray<uint8, TInlineAllocator<4>> CandidateOrder(
		const uint8 Domain,
		const FABTSM73DAG5BV2PreviewSettings& Settings,
		const FABTSM73DAG5BV2Volume& Volume,
		const FString& Path,
		const int32 DecisionDepth)
	{
		struct FChoice
		{
			uint8 Bit = 0;
			float Score = 0.0f;
		};
		TArray<FChoice, TInlineAllocator<4>> Choices;
		FRandomStream Stream(
			static_cast<int32>(StableSeed(
				Settings,
				Path,
				0xc119 + DecisionDepth)));
		for (uint8 Bit = 1; Bit <= PyramidMask; Bit <<= 1)
		{
			if ((Domain & Bit) == 0)
			{
				continue;
			}
			const float Weight = PrimitiveWeight(Bit, Settings, Volume);
			if (Weight <= UE_SMALL_NUMBER)
			{
				continue;
			}
			const float RandomValue = FMath::Max(
				UE_SMALL_NUMBER,
				Stream.FRand());
			Choices.Add({
				Bit,
				-FMath::Loge(RandomValue) / Weight});
		}
		Choices.Sort([](const FChoice& A, const FChoice& B)
		{
			if (!FMath::IsNearlyEqual(A.Score, B.Score))
			{
				return A.Score < B.Score;
			}
			return A.Bit < B.Bit;
		});
		TArray<uint8, TInlineAllocator<4>> Ordered;
		for (const FChoice& Choice : Choices)
		{
			Ordered.Add(Choice.Bit);
		}
		return Ordered;
	}

	bool SolveWFC(
		TArray<uint8>& Domains,
		FWFCContext& Context,
		const int32 DecisionDepth)
	{
		if (!Propagate(Domains, Context))
		{
			return false;
		}
		int32 BestIndex = INDEX_NONE;
		int32 BestEntropy = MAX_int32;
		for (int32 Index = 0; Index < Domains.Num(); ++Index)
		{
			const int32 Entropy = BitCount(Domains[Index]);
			if (Entropy > 1 && Entropy < BestEntropy)
			{
				BestIndex = Index;
				BestEntropy = Entropy;
			}
		}
		if (BestIndex == INDEX_NONE)
		{
			return true;
		}

		const auto Ordered = CandidateOrder(
			Domains[BestIndex],
			*Context.Settings,
			(*Context.Volumes)[BestIndex],
			(*Context.Volumes)[BestIndex].DerivationPath,
			DecisionDepth);
		for (const uint8 Candidate : Ordered)
		{
			TArray<uint8> TrialDomains = Domains;
			TrialDomains[BestIndex] = Candidate;
			if (SolveWFC(TrialDomains, Context, DecisionDepth + 1))
			{
				Domains = MoveTemp(TrialDomains);
				return true;
			}
			if (++Context.BacktrackSteps
				> Context.Settings->MaxWFCBacktrackSteps)
			{
				return false;
			}
		}
		return false;
	}

	bool ApplyVarietyAnchors(
		const TArray<FABTSM73DAG5BV2Volume>& Volumes,
		const TArray<bool>& HasAbove,
		TArray<uint8>& Domains)
	{
		int32 BoxIndex = INDEX_NONE;
		int32 PyramidIndex = INDEX_NONE;
		int32 PrismIndex = INDEX_NONE;
		double MostSquareAspect = TNumericLimits<double>::Max();
		double MostElongatedAspect = 0.0;
		for (int32 Index = 0; Index < Volumes.Num(); ++Index)
		{
			if (BoxIndex == INDEX_NONE
				&& Volumes[Index].Role
					== EABTSM73DAG5BV2VolumeRole::Foundation
				&& (Domains[Index] & BoxMask) != 0)
			{
				BoxIndex = Index;
			}
		}
		for (int32 Index = 0; Index < Volumes.Num(); ++Index)
		{
			if (HasAbove[Index]
				|| Volumes[Index].Role
					!= EABTSM73DAG5BV2VolumeRole::Crown)
			{
				continue;
			}
			const FVector Size = Volumes[Index].LocalBounds.GetSize();
			const double ShortSpan = FMath::Max(
				1.0, FMath::Min(Size.X, Size.Y));
			const double Aspect = FMath::Max(Size.X, Size.Y) / ShortSpan;
			if ((Domains[Index] & PyramidMask) != 0
				&& Aspect < MostSquareAspect)
			{
				MostSquareAspect = Aspect;
				PyramidIndex = Index;
			}
			const uint8 Preferred = LongRidgePrismMask(Volumes[Index]);
			if ((Domains[Index] & Preferred) != 0
				&& Aspect > MostElongatedAspect)
			{
				MostElongatedAspect = Aspect;
				PrismIndex = Index;
			}
		}
		if (BoxIndex == INDEX_NONE || PyramidIndex == INDEX_NONE)
		{
			return false;
		}
		Domains[BoxIndex] = BoxMask;
		if (PrismIndex == PyramidIndex)
		{
			// One dominant roof cannot realize two shapes. Leave its aspect domain
			// intact and let deterministic weighted collapse choose the best fit.
			return true;
		}
		Domains[PyramidIndex] = PyramidMask;
		Domains[PrismIndex] = LongRidgePrismMask(Volumes[PrismIndex]);
		return true;
	}

	int32 FindSingleTerminalRoofIndex(
		const TArray<FABTSM73DAG5BV2Volume>& Volumes,
		const TArray<bool>& HasAbove)
	{
		int32 RoofIndex = INDEX_NONE;
		for (int32 Index = 0; Index < Volumes.Num(); ++Index)
		{
			const FABTSM73DAG5BV2Volume& Volume = Volumes[Index];
			if (HasAbove[Index]
				|| IsSpanRole(Volume.Role)
				|| Volume.Role != EABTSM73DAG5BV2VolumeRole::Crown)
			{
				continue;
			}

			if (RoofIndex == INDEX_NONE)
			{
				RoofIndex = Index;
				continue;
			}
			const FABTSM73DAG5BV2Volume& Current = Volumes[RoofIndex];
			const double CandidateTop = Volume.LocalBounds.Max.Z;
			const double CurrentTop = Current.LocalBounds.Max.Z;
			const double CandidateArea =
				Volume.LocalBounds.GetSize().X * Volume.LocalBounds.GetSize().Y;
			const double CurrentArea =
				Current.LocalBounds.GetSize().X * Current.LocalBounds.GetSize().Y;
			if (CandidateTop > CurrentTop + UE_KINDA_SMALL_NUMBER
				|| (FMath::IsNearlyEqual(CandidateTop, CurrentTop)
					&& (CandidateArea > CurrentArea + UE_KINDA_SMALL_NUMBER
						|| (FMath::IsNearlyEqual(CandidateArea, CurrentArea)
							&& Volume.DerivationPath
								< Current.DerivationPath))))
			{
				RoofIndex = Index;
			}
		}
		return RoofIndex;
	}

	bool ReallocateSingleTerminalRoofEnvelope(
		const FABTSM73DAG5BV2PreviewSettings& Settings,
		TArray<FABTSM73DAG5BV2Volume>& Volumes,
		const TArray<bool>& HasAbove)
	{
		const int32 RoofIndex = FindSingleTerminalRoofIndex(
			Volumes, HasAbove);
		if (RoofIndex == INDEX_NONE)
		{
			return false;
		}
		FABTSM73DAG5BV2Volume& Roof = Volumes[RoofIndex];
		const double OldBottomZ = Roof.LocalBounds.Min.Z;
		const double NewBottomZ = FMath::Max(
			OldBottomZ,
			Roof.LocalBounds.Max.Z - Settings.SingleTerminalRoofHeightCM);
		if (NewBottomZ <= OldBottomZ + UE_KINDA_SMALL_NUMBER)
		{
			return true;
		}

		bool bExtendedSupport = false;
		for (int32 Index = 0; Index < Volumes.Num(); ++Index)
		{
			if (Index == RoofIndex)
			{
				continue;
			}
			FABTSM73DAG5BV2Volume& Lower = Volumes[Index];
			const double XOverlap = FMath::Min(
				Lower.LocalBounds.Max.X, Roof.LocalBounds.Max.X) - FMath::Max(
				Lower.LocalBounds.Min.X, Roof.LocalBounds.Min.X);
			const double YOverlap = FMath::Min(
				Lower.LocalBounds.Max.Y, Roof.LocalBounds.Max.Y) - FMath::Max(
				Lower.LocalBounds.Min.Y, Roof.LocalBounds.Min.Y);
			if (FMath::Abs(Lower.LocalBounds.Max.Z - OldBottomZ) <= 1.0
				&& XOverlap > 1.0 && YOverlap > 1.0)
			{
				Lower.LocalBounds.Max.Z = NewBottomZ;
				bExtendedSupport = true;
			}
		}
		if (!bExtendedSupport)
		{
			return false;
		}
		Roof.LocalBounds.Min.Z = NewBottomZ;
		return true;
	}

	bool ApplySingleTerminalRoofAnchor(
		const FABTSM73DAG5BV2PreviewSettings& Settings,
		const TArray<FABTSM73DAG5BV2Volume>& Volumes,
		const TArray<bool>& HasAbove,
		TArray<uint8>& Domains)
	{
		const int32 RoofIndex = FindSingleTerminalRoofIndex(
			Volumes, HasAbove);
		if (RoofIndex == INDEX_NONE)
		{
			return false;
		}

		// Other exposed terminals remain flat Box volumes. The low tier gains
		// one readable crown, not full primitive variety or several expensive
		// roofs.
		for (int32 Index = 0; Index < Volumes.Num(); ++Index)
		{
			if (!HasAbove[Index] && !IsSpanRole(Volumes[Index].Role))
			{
				Domains[Index] = BoxMask;
			}
		}

		Domains[RoofIndex] = AspectRoofDomain(Volumes[RoofIndex]);
		return true;
	}

	FString GrammarCanonical(
		const EABTSM73DAG5BV2Archetype Archetype,
		const TArray<FABTSM73DAG5BV2Volume>& Volumes,
		const TArray<FString>& Trace)
	{
		FString Canonical = FString::Printf(
			TEXT("Archetype=%d"),
			static_cast<int32>(Archetype));
		for (const FString& Step : Trace)
		{
			Canonical += TEXT("|Rule=") + Step;
		}
		for (const FABTSM73DAG5BV2Volume& Volume : Volumes)
		{
			Canonical += FString::Printf(
				TEXT("|V=%d,%d,%d,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f"),
				Volume.VolumeId,
				Volume.GrammarDepth,
				static_cast<int32>(Volume.Role),
				*Volume.DerivationPath,
				Volume.LocalBounds.Min.X,
				Volume.LocalBounds.Min.Y,
				Volume.LocalBounds.Min.Z,
				Volume.LocalBounds.Max.X,
				Volume.LocalBounds.Max.Y,
				Volume.LocalBounds.Max.Z);
			if (Volume.Role == EABTSM73DAG5BV2VolumeRole::SupportedSpan)
			{
				Canonical += FString::Printf(
					TEXT(",S=%d,%d,%d,%.3f,%.3f"),
					Volume.NegativeSupportVolumeId,
					Volume.PositiveSupportVolumeId,
					Volume.SpanAxisIndex,
					Volume.SpanOpeningMinCM,
					Volume.SpanOpeningMaxCM);
			}
		}
		return Canonical;
	}

	FString WFCCanonical(
		const TArray<FABTSM73DAG5BV2Volume>& Volumes)
	{
		FString Canonical;
		for (const FABTSM73DAG5BV2Volume& Volume : Volumes)
		{
			Canonical += FString::Printf(
				TEXT("|V=%d,P=%d"),
				Volume.VolumeId,
				static_cast<int32>(Volume.Primitive));
		}
		return Canonical;
	}
}

bool FABTSM73DAG5BShapeGrammarV2::Generate(
	const FABTSM73DAG5BV2PreviewSettings& Settings,
	FABTSM73DAG5BV2GenerationResult& OutResult,
	FString& OutError) const
{
	using namespace ABTSM73DAG5BV2;
	OutResult = FABTSM73DAG5BV2GenerationResult();
	OutError.Reset();
	if (!IsValidSettings(Settings, OutError))
	{
		OutResult.Summary.RejectReason = OutError;
		return false;
	}

	const EABTSM73DAG5BV2Archetype Archetype =
		ResolveArchetype(Settings);
	OutResult.Summary.ResolvedArchetype = Archetype;
	FInitialPlan Initial = BuildInitialPlan(Settings, Archetype);
	if (Initial.Roots.IsEmpty()
		|| Initial.Roots.Num() + Initial.FixedVolumes.Num()
			> Settings.MaxVolumeCount)
	{
		OutError = TEXT("DAG5BV2InitialPlanExceedsBudget");
		OutResult.Summary.RejectReason = OutError;
		return false;
	}

	FGrammarContext GrammarContext;
	GrammarContext.Settings = &Settings;
	GrammarContext.Result = &OutResult;
	GrammarContext.Error = &OutError;
	for (const FScope& Fixed : Initial.FixedVolumes)
	{
		if (!EmitVolume(Fixed, GrammarContext))
		{
			OutResult.Summary.RejectReason = OutError;
			return false;
		}
		OutResult.GrammarTrace.Add(
			Fixed.Path + TEXT(" -> FixedBridge"));
		++GrammarContext.GrammarSteps;
	}

	const int32 RootBudget = FMath::Min(
		Settings.MaxVolumeCount,
		Settings.TargetVolumeCount) - Initial.FixedVolumes.Num();
	const int32 BaseBudget = RootBudget / Initial.Roots.Num();
	int32 Remainder = RootBudget % Initial.Roots.Num();
	for (const FScope& Root : Initial.Roots)
	{
		const int32 Budget =
			BaseBudget + (Remainder-- > 0 ? 1 : 0);
		if (!ExpandScope(Root, FMath::Max(1, Budget), GrammarContext))
		{
			OutResult.Summary.RejectReason = OutError;
			return false;
		}
	}
	if (!ResolveSupportedSpans(
		Settings, OutResult.Volumes, OutResult.GrammarTrace))
	{
		OutError = TEXT("DAG5BV2SupportedSpanResolutionFailed");
		OutResult.Summary.RejectReason = OutError;
		return false;
	}
	if (!CoupleGroundLayer(
		Settings,
		OutResult.Volumes,
		OutResult.GrammarTrace,
		OutResult.SemanticPodiumCandidateDiagnostics,
		OutResult.SemanticPodiumSelectionDiagnostics,
		OutError))
	{
		OutResult.Summary.RejectReason = OutError;
		return false;
	}
	if (!AggregateRoofTerminals(
		Settings,
		OutResult.Volumes,
		OutResult.GrammarTrace,
		OutResult.Summary))
	{
		OutError = TEXT("DAG5BV2RoofTerminalAggregationFailed");
		OutResult.Summary.RejectReason = OutError;
		return false;
	}

	if (OutResult.Volumes.Num() < 3)
	{
		OutError = TEXT("DAG5BV2InsufficientGrammarVolumes");
		OutResult.Summary.RejectReason = OutError;
		return false;
	}
	OutResult.Summary.GrammarStepCount = GrammarContext.GrammarSteps;
	OutResult.Summary.VolumeCount = OutResult.Volumes.Num();
	const FString GrammarText = GrammarCanonical(
		Archetype,
		OutResult.Volumes,
		OutResult.GrammarTrace);
	OutResult.Summary.GrammarHash =
		static_cast<int64>(FCrc::StrCrc32(*GrammarText));

	TArray<FAdjacencyEdge> Edges;
	TArray<bool> HasAbove;
	BuildAdjacency(OutResult.Volumes, Edges, HasAbove);
	TArray<uint8> Domains;
	Domains.Init(AllMask, OutResult.Volumes.Num());
	for (int32 Index = 0; Index < OutResult.Volumes.Num(); ++Index)
	{
		const FABTSM73DAG5BV2Volume& Volume =
			OutResult.Volumes[Index];
		if (HasAbove[Index])
		{
			Domains[Index] = BoxMask;
		}
		else if (IsSpanRole(Volume.Role))
		{
			Domains[Index] = BoxMask;
		}
		else if (Volume.Role == EABTSM73DAG5BV2VolumeRole::Crown)
		{
			Domains[Index] = AspectRoofDomain(Volume);
		}
		else
		{
			Domains[Index] = BoxMask;
		}
	}
	if (Settings.bRequireSingleTerminalRoof
		&& !ApplySingleTerminalRoofAnchor(
			Settings, OutResult.Volumes, HasAbove, Domains))
	{
		OutError = TEXT("DAG5BV2SingleTerminalRoofUnreachable");
		OutResult.Summary.RejectReason = OutError;
		return false;
	}
	if (Settings.bRequirePrimitiveVariety
		&& !ApplyVarietyAnchors(OutResult.Volumes, HasAbove, Domains))
	{
		OutError = TEXT("DAG5BV2PrimitiveVarietyUnreachable");
		OutResult.Summary.RejectReason = OutError;
		return false;
	}

	FWFCContext WFCContext;
	WFCContext.Settings = &Settings;
	WFCContext.Volumes = &OutResult.Volumes;
	WFCContext.Edges = &Edges;
	if (!SolveWFC(Domains, WFCContext, 0))
	{
		OutError = WFCContext.PropagationOperations
				> Settings.MaxWFCPropagationOperations
			? TEXT("DAG5BV2WFCPropagationBudgetExceeded")
			: WFCContext.BacktrackSteps
					> Settings.MaxWFCBacktrackSteps
				? TEXT("DAG5BV2WFCBacktrackBudgetExceeded")
				: TEXT("DAG5BV2WFCNoSolution");
		OutResult.Summary.RejectReason = OutError;
		return false;
	}

	for (int32 Index = 0; Index < OutResult.Volumes.Num(); ++Index)
	{
		FABTSM73DAG5BV2Volume& Volume = OutResult.Volumes[Index];
		Volume.Primitive = MaskToPrimitive(Domains[Index]);
		if (Volume.Role == EABTSM73DAG5BV2VolumeRole::SupportedSpan)
		{
			++OutResult.Summary.SupportedSpanCount;
		}
		if (HasAbove[Index]
			&& Volume.Primitive != EABTSM73DAG5BV2Primitive::Box)
		{
			OutError = TEXT("DAG5BV2RoofPrimitiveHasUpperVolume");
			OutResult.Summary.RejectReason = OutError;
			return false;
		}
		switch (Volume.Primitive)
		{
		case EABTSM73DAG5BV2Primitive::TriangularPrismX:
		case EABTSM73DAG5BV2Primitive::TriangularPrismY:
			++OutResult.Summary.PrismCount;
			break;
		case EABTSM73DAG5BV2Primitive::Pyramid:
			++OutResult.Summary.PyramidCount;
			break;
		case EABTSM73DAG5BV2Primitive::Box:
		default:
			++OutResult.Summary.BoxCount;
			break;
		}
	}
	if (Settings.bRequirePrimitiveVariety
		&& (OutResult.Summary.BoxCount == 0
			|| OutResult.Summary.PrismCount
				+ OutResult.Summary.PyramidCount == 0
			|| (OutResult.Summary.RoofTerminalCount >= 2
				&& (OutResult.Summary.PrismCount == 0
					|| OutResult.Summary.PyramidCount == 0))))
	{
		OutError = TEXT("DAG5BV2PrimitiveVarietyNotRealized");
		OutResult.Summary.RejectReason = OutError;
		return false;
	}
	if (Settings.bRequireSingleTerminalRoof
		&& OutResult.Summary.PrismCount
			+ OutResult.Summary.PyramidCount != 1)
	{
		OutError = TEXT("DAG5BV2SingleTerminalRoofNotRealized");
		OutResult.Summary.RejectReason = OutError;
		return false;
	}

	OutResult.Summary.WFCPropagationOperationCount =
		WFCContext.PropagationOperations;
	OutResult.Summary.WFCBacktrackStepCount =
		WFCContext.BacktrackSteps;
	const FString WFCText = WFCCanonical(OutResult.Volumes);
	OutResult.Summary.WFCHash =
		static_cast<int64>(FCrc::StrCrc32(*WFCText));
	const FString ResultText = FString::Printf(
		TEXT("Grammar=%lld|WFC=%lld|Volumes=%d"),
		OutResult.Summary.GrammarHash,
		OutResult.Summary.WFCHash,
		OutResult.Volumes.Num());
	OutResult.Summary.ResultHash =
		static_cast<int64>(FCrc::StrCrc32(*ResultText));
	OutResult.Summary.bAccepted = true;
	return true;
}
