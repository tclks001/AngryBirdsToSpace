// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rendering/ABTSStylizedMaterialContract.h"
#include "UObject/GCObject.h"

class UMaterialInterface;
class UPrimitiveComponent;

/** One feature-owned stylized material candidate; it never mutates gameplay data. */
struct ABTSRUNTIME_API FABTSStylizedMaterialSlotBinding
{
	UPrimitiveComponent* Component = nullptr;
	int32 MaterialSlotIndex = INDEX_NONE;
	UMaterialInterface* StylizedMaterial = nullptr;
	EABTSStylizedMaterialFamily Family = EABTSStylizedMaterialFamily::None;

	bool IsValid() const;
};

/**
 * Integration-owned reversible slot override registry.
 *
 * It restores a material only while the currently assigned material is still
 * the exact interface it applied. External edits therefore fail closed instead
 * of being overwritten during Style Off or world teardown.
 */
class ABTSRUNTIME_API FABTSStylizedMaterialOverrideRegistry final
	: public FGCObject
{
public:
	FABTSStylizedMaterialOverrideRegistry();
	virtual ~FABTSStylizedMaterialOverrideRegistry() override;

	FABTSStylizedMaterialOverrideRegistry(
		const FABTSStylizedMaterialOverrideRegistry&) = delete;
	FABTSStylizedMaterialOverrideRegistry& operator=(
		const FABTSStylizedMaterialOverrideRegistry&) = delete;

	void Apply(
		TConstArrayView<FABTSStylizedMaterialSlotBinding> DesiredBindings,
		bool bStyleEnabled);
	void RestoreAll();

	int32 Num() const;
	int32 GetConflictCount() const;
	int32 GetRejectedBindingCount() const;

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override;

private:
	class FImpl;
	TUniquePtr<FImpl> Impl;
};
