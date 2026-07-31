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
			|| Settings.BoxWeight
				+ Settings.PrismWeight
				+ Settings.PyramidWeight <= UE_SMALL_NUMBER
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
			EABTSM73DAG5BV2VolumeRole::Bridge));
	}

	FInitialPlan BuildInitialPlan(
		const FABTSM73DAG5BV2PreviewSettings& Settings,
		const EABTSM73DAG5BV2Archetype Archetype)
	{
		FInitialPlan Plan;
		const float W = Settings.TargetWidthCM;
		const float D = Settings.TargetDepthCM;
		const float H = Settings.TargetHeightCM;

		switch (Archetype)
		{
		case EABTSM73DAG5BV2Archetype::TerracedCitadel:
			Plan.Roots.Add(MakeScope(
				TEXT("Citadel/Main"),
				FVector(0.0, 0.0, H * 0.5),
				FVector(W * 0.58, D * 0.78, H),
				EABTSM73DAG5BV2VolumeRole::Body));
			Plan.Roots.Add(MakeScope(
				TEXT("Citadel/LeftAnnex"),
				FVector(-W * 0.39, 0.0, H * 0.24),
				FVector(W * 0.18, D * 0.92, H * 0.48),
				EABTSM73DAG5BV2VolumeRole::Annex));
			Plan.Roots.Add(MakeScope(
				TEXT("Citadel/RightAnnex"),
				FVector(W * 0.39, 0.0, H * 0.29),
				FVector(W * 0.18, D * 0.70, H * 0.58),
				EABTSM73DAG5BV2VolumeRole::Annex));
			break;

		case EABTSM73DAG5BV2Archetype::TwinTowerComplex:
			Plan.Roots.Add(MakeScope(
				TEXT("Twin/LeftTower"),
				FVector(-W * 0.27, 0.0, H * 0.50),
				FVector(W * 0.38, D * 0.72, H),
				EABTSM73DAG5BV2VolumeRole::Body));
			Plan.Roots.Add(MakeScope(
				TEXT("Twin/RightTower"),
				FVector(W * 0.27, 0.0, H * 0.43),
				FVector(W * 0.38, D * 0.88, H * 0.86),
				EABTSM73DAG5BV2VolumeRole::Body));
			AddBridge(
				Plan,
				TEXT("Twin/Bridge"),
				FVector(0.0, 0.0, H * 0.56),
				FVector(
					W * 0.46,
					D * 0.44,
					H * Settings.BridgeThicknessRatio));
			break;

		case EABTSM73DAG5BV2Archetype::BridgedArcology:
			Plan.Roots.Add(MakeScope(
				TEXT("Arcology/West"),
				FVector(-W * 0.34, 0.0, H * 0.45),
				FVector(W * 0.25, D * 0.62, H * 0.90),
				EABTSM73DAG5BV2VolumeRole::Body));
			Plan.Roots.Add(MakeScope(
				TEXT("Arcology/Core"),
				FVector(0.0, 0.0, H * 0.34),
				FVector(W * 0.25, D * 0.90, H * 0.68),
				EABTSM73DAG5BV2VolumeRole::Body));
			Plan.Roots.Add(MakeScope(
				TEXT("Arcology/East"),
				FVector(W * 0.34, 0.0, H * 0.50),
				FVector(W * 0.25, D * 0.68, H),
				EABTSM73DAG5BV2VolumeRole::Body));
			AddBridge(
				Plan,
				TEXT("Arcology/WestBridge"),
				FVector(-W * 0.17, 0.0, H * 0.54),
				FVector(
					W * 0.27,
					D * 0.40,
					H * Settings.BridgeThicknessRatio));
			AddBridge(
				Plan,
				TEXT("Arcology/EastBridge"),
				FVector(W * 0.17, 0.0, H * 0.66),
				FVector(
					W * 0.27,
					D * 0.36,
					H * Settings.BridgeThicknessRatio));
			break;

		case EABTSM73DAG5BV2Archetype::SpiredCampus:
		default:
			Plan.Roots.Add(MakeScope(
				TEXT("Campus/CentralSpire"),
				FVector(0.0, 0.0, H * 0.50),
				FVector(W * 0.38, D * 0.50, H),
				EABTSM73DAG5BV2VolumeRole::Body));
			Plan.Roots.Add(MakeScope(
				TEXT("Campus/WestWing"),
				FVector(-W * 0.34, 0.0, H * 0.23),
				FVector(W * 0.26, D * 0.82, H * 0.46),
				EABTSM73DAG5BV2VolumeRole::Annex));
			Plan.Roots.Add(MakeScope(
				TEXT("Campus/EastWing"),
				FVector(W * 0.34, 0.0, H * 0.28),
				FVector(W * 0.26, D * 0.68, H * 0.56),
				EABTSM73DAG5BV2VolumeRole::Annex));
			AddBridge(
				Plan,
				TEXT("Campus/WestLink"),
				FVector(-W * 0.18, 0.0, H * 0.35),
				FVector(
					W * 0.22,
					D * 0.32,
					H * Settings.BridgeThicknessRatio));
			AddBridge(
				Plan,
				TEXT("Campus/EastLink"),
				FVector(W * 0.18, 0.0, H * 0.42),
				FVector(
					W * 0.22,
					D * 0.28,
					H * Settings.BridgeThicknessRatio));
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
		if (Scope.Bounds.Min.Z <= 1.0
			&& Scope.Role != EABTSM73DAG5BV2VolumeRole::Bridge)
		{
			Volume.Role = EABTSM73DAG5BV2VolumeRole::Foundation;
		}
		else if (Scope.Depth >= Context.Settings->MaxGrammarDepth - 1
			&& Scope.Role != EABTSM73DAG5BV2VolumeRole::Bridge)
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
		if (LeafBudget >= 2 && CanSplitX(Scope, Settings))
		{
			Candidates.Add({
				EGrammarRule::SplitX,
				Settings.HorizontalSplitWeight});
		}
		if (LeafBudget >= 2 && CanSplitY(Scope, Settings))
		{
			Candidates.Add({
				EGrammarRule::SplitY,
				Settings.HorizontalSplitWeight * 0.75f});
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
					UpperSize.Y * (1.0f - Shrink * 0.75f),
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
			Bridge.Role = EABTSM73DAG5BV2VolumeRole::Bridge;
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
			if (A == PyramidMask)
			{
				return false;
			}
			if ((A == PrismXMask || A == PrismYMask)
				&& B != PyramidMask)
			{
				return false;
			}
			return true;
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
		const FABTSM73DAG5BV2PreviewSettings& Settings)
	{
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
			const float Weight = PrimitiveWeight(Bit, Settings);
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
			if (Index != BoxIndex
				&& !HasAbove[Index]
				&& (Domains[Index] & PyramidMask) != 0)
			{
				PyramidIndex = Index;
				break;
			}
		}
		for (int32 Index = 0; Index < Volumes.Num(); ++Index)
		{
			if (Index == BoxIndex
				|| Index == PyramidIndex
				|| HasAbove[Index])
			{
				continue;
			}
			const FVector Size = Volumes[Index].LocalBounds.GetSize();
			const uint8 Preferred =
				Size.X >= Size.Y ? PrismXMask : PrismYMask;
			if ((Domains[Index] & Preferred) != 0)
			{
				PrismIndex = Index;
				Domains[Index] = Preferred;
				break;
			}
		}
		if (BoxIndex == INDEX_NONE
			|| PyramidIndex == INDEX_NONE
			|| PrismIndex == INDEX_NONE)
		{
			return false;
		}
		Domains[BoxIndex] = BoxMask;
		Domains[PyramidIndex] = PyramidMask;
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

	const int32 RootBudget =
		Settings.MaxVolumeCount - Initial.FixedVolumes.Num();
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
		if (Volume.Role == EABTSM73DAG5BV2VolumeRole::Bridge)
		{
			const FVector Size = Volume.LocalBounds.GetSize();
			Domains[Index] = BoxMask
				| (Size.X >= Size.Y ? PrismXMask : PrismYMask);
		}
		else if (HasAbove[Index])
		{
			Domains[Index] = BoxMask | PrismXMask | PrismYMask;
		}
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
			|| OutResult.Summary.PrismCount == 0
			|| OutResult.Summary.PyramidCount == 0))
	{
		OutError = TEXT("DAG5BV2PrimitiveVarietyNotRealized");
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
