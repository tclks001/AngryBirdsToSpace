// Copyright Epic Games, Inc. All Rights Reserved.

#include "M11Search/ABTSM11FrozenCandidateLayouts.h"

namespace ABTS::M11Search
{
	namespace
	{
#include "ABTSM11FrozenV4CandidateData.inl"

		void ApplyRank3UpstreamCandidate353(CandidateLayout& Layout)
		{
			Layout.Scenario.Target.CenterCM +=
				M11Core::Vec3d(2045.340, 2022.718, -8885.799);
			Layout.Scenario.Target.GeometricContactCenterCM +=
				M11Core::Vec3d(2045.340, 2022.718, -8885.799);
			Layout.Scenario.Target.HitRadiusCM = 12000.0;

			M11Core::GravityBodySpec& Assist2 = Layout.Scenario.Bodies[2];
			Assist2.CenterCM +=
				M11Core::Vec3d(-1344.726, 1739.712, -1105.200);
			Assist2.BPlaneTargetTCM += -48.730;
			Assist2.BPlaneTargetRCM += -1327.573;
			Assist2.BPlaneSigmaTCM *= 0.857348;
			Assist2.BPlaneSigmaRCM *= 0.857348;
			Assist2.VirtualOrbitalVelocityCMPerSec +=
				M11Core::Vec3d(80.096, -225.197, 77.338);

			M11Core::GravityBodySpec& Assist3 = Layout.Scenario.Bodies[3];
			Assist3.CenterCM +=
				M11Core::Vec3d(1864.062, -1883.951, -345.280);
			Assist3.BPlaneTargetTCM += 731.050;
			Assist3.BPlaneTargetRCM += 1622.652;
			Assist3.BPlaneSigmaTCM *= 1.193424;
			Assist3.BPlaneSigmaRCM *= 1.193424;
			Assist3.VirtualOrbitalVelocityCMPerSec +=
				M11Core::Vec3d(-955.286, 1384.235, -1302.886);
		}

		void ApplyRank3F3ExpansionCandidate21(CandidateLayout& Layout)
		{
			Layout.Scenario.Target.CenterCM +=
				M11Core::Vec3d(2375.775, 2202.028, -8743.864);
			Layout.Scenario.Target.GeometricContactCenterCM +=
				M11Core::Vec3d(2375.775, 2202.028, -8743.864);
			Layout.Scenario.Target.HitRadiusCM = 6000.0;

			M11Core::GravityBodySpec& Assist2 = Layout.Scenario.Bodies[2];
			Assist2.CenterCM +=
				M11Core::Vec3d(-1344.726, 1739.712, -1105.200);
			Assist2.BPlaneTargetTCM += -48.730;
			Assist2.BPlaneTargetRCM += -1327.573;
			Assist2.BPlaneSigmaTCM *= 0.857348;
			Assist2.BPlaneSigmaRCM *= 0.857348;
			Assist2.VirtualOrbitalVelocityCMPerSec +=
				M11Core::Vec3d(80.096, -225.197, 77.338);

			M11Core::GravityBodySpec& Assist3 = Layout.Scenario.Bodies[3];
			Assist3.CenterCM +=
				M11Core::Vec3d(2117.187, -1860.906, -88.000);
			Assist3.BPlaneTargetTCM += 86.735;
			Assist3.BPlaneTargetRCM += 2102.742;
			Assist3.BPlaneSigmaTCM *= 1.133051;
			Assist3.BPlaneSigmaRCM *= 1.133051;
			Assist3.VirtualOrbitalVelocityCMPerSec +=
				M11Core::Vec3d(-881.321, 1399.806, -1318.121);
		}

		void TranslateConstellationAwayFromPouch(
			CandidateLayout& Layout,
			const double DistanceCM)
		{
			const M11Core::Vec3d Direction =
				(Layout.Scenario.Bodies[1].CenterCM
					- Layout.Launch.PouchLocalPositionCM).GetSafeNormal();
			const M11Core::Vec3d Offset = Direction * DistanceCM;
			for (std::size_t BodyIndex = 1;
				BodyIndex < Layout.Scenario.Bodies.size(); ++BodyIndex)
			{
				Layout.Scenario.Bodies[BodyIndex].CenterCM += Offset;
			}
			Layout.Scenario.Target.CenterCM += Offset;
			Layout.Scenario.Target.GeometricContactCenterCM += Offset;
		}

		constexpr std::array<FrozenCandidateIdentity, 7> Identities = {{
			{3, 20ull, 0xed74ffaf0de8028full, 0x19a6a15736704d7bull,
				0x791c9a64b195b0d4ull, 0x938f4825be418ebeull},
			{4, 20ull, 0xf22ad256fd791e07ull, 0xa8fdff5512fc4743ull,
				0xbf710eb5c1e114c1ull, 0xfee62a58f2e1dfb7ull},
			{5, 30ull, 0xcdc6e41075d99493ull, 0xfb9a637bf71a38dfull,
				0xa7695a10b44f8281ull, 0x4689059277f93880ull},
			{6, 30ull, 0x80d274a67e1e9944ull, 0x3e64212a606348f0ull,
				0x9de084d9f77c9ee7ull, 0xf8b1ff45fa8f1adfull},
			// Derived research Candidate 353. The final field freezes its
			// half-cell aggregate evidence hash; it is not a certified score.
			{7, 353ull, 0xb3e0f00ca35d499aull, 0x48ffe272661916b2ull,
				0xe7c6c093e3cc9533ull, 0x0baef62a673e8e55ull},
			// Candidate 353 F3-expansion research successor. The score field
			// carries its strict half-cell aggregate evidence hash.
			{8, 21ull, 0x617687274ed0c29aull, 0xa2a41077916aadb2ull,
				0xaac8ba98079011fdull, 0xb77f6d2f3f954005ull},
			// Rank 8 with the entire three-assist/UFO constellation moved
			// 100 cm farther from the pouch. Relative celestial geometry is
			// unchanged; the score field carries its refined-grid evidence hash.
			{9, 22ull, 0x166f0aa067d54328ull, 0x11e775a2b20e0b64ull,
				0x22675cdfb00406d5ull, 0xa9bd918ee812d572ull},
		}};
	}

	bool BuildFrozenV4CandidateLayout(
		const std::int32_t Rank,
		CandidateLayout& OutLayout,
		FrozenCandidateIdentity* OutIdentity)
	{
		OutLayout = CandidateLayout();
		if (OutIdentity != nullptr)
		{
			*OutIdentity = FrozenCandidateIdentity();
		}
		const std::int32_t SourceRank =
			Rank == 7 || Rank == 8 || Rank == 9 ? 3 : Rank;
		if (!BuildFrozenV4Layout(SourceRank, OutLayout))
		{
			return false;
		}
		if (Rank == 7)
		{
			ApplyRank3UpstreamCandidate353(OutLayout);
		}
		else if (Rank == 8)
		{
			ApplyRank3F3ExpansionCandidate21(OutLayout);
		}
		else if (Rank == 9)
		{
			ApplyRank3F3ExpansionCandidate21(OutLayout);
			TranslateConstellationAwayFromPouch(OutLayout, 100.0);
		}
		for (const FrozenCandidateIdentity& Identity : Identities)
		{
			if (Identity.Rank == Rank)
			{
				if (OutIdentity != nullptr)
				{
					*OutIdentity = Identity;
				}
				return true;
			}
		}
		OutLayout = CandidateLayout();
		return false;
	}
}
