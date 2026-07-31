// Copyright Epic Games, Inc. All Rights Reserved.

#include "M11Search/ABTSM11FrozenCandidateLayouts.h"

namespace ABTS::M11Search
{
	namespace
	{
#include "ABTSM11FrozenV4CandidateData.inl"

		constexpr std::array<FrozenCandidateIdentity, 4> Identities = {{
			{3, 20ull, 0xed74ffaf0de8028full, 0x19a6a15736704d7bull,
				0x791c9a64b195b0d4ull, 0x938f4825be418ebeull},
			{4, 20ull, 0xf22ad256fd791e07ull, 0xa8fdff5512fc4743ull,
				0xbf710eb5c1e114c1ull, 0xfee62a58f2e1dfb7ull},
			{5, 30ull, 0xcdc6e41075d99493ull, 0xfb9a637bf71a38dfull,
				0xa7695a10b44f8281ull, 0x4689059277f93880ull},
			{6, 30ull, 0x80d274a67e1e9944ull, 0x3e64212a606348f0ull,
				0x9de084d9f77c9ee7ull, 0xf8b1ff45fa8f1adfull},
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
		if (!BuildFrozenV4Layout(Rank, OutLayout))
		{
			return false;
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
