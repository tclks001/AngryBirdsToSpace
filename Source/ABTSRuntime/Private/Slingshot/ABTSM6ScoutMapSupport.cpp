// Copyright Epic Games, Inc. All Rights Reserved.

#include "Slingshot/ABTSM6SlingshotSystem.h"

#include "Slingshot/ABTSM6DestructibleProxy.h"

void AABTSM6SlingshotSystem::GatherLiveDestructibleProxies(
	TArray<AABTSM6DestructibleProxy*>& OutProxies) const
{
	OutProxies.Reset();
	OutProxies.Reserve(DynamicProxies.Num());
	for (const TWeakObjectPtr<AABTSM6DestructibleProxy>& WeakProxy : DynamicProxies)
	{
		AABTSM6DestructibleProxy* Proxy = WeakProxy.Get();
		if (Proxy != nullptr && !Proxy->IsActorBeingDestroyed()) OutProxies.Add(Proxy);
	}
}

