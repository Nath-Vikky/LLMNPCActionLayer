#pragma once

#include "CoreMinimal.h"
#include "Capabilities/LLMNPCSkeletonCapabilityTypes.h"

class ULLMNPCControlManifest;
class ULLMNPCSkeletonProfile;

class LLMNPCACTIONLAYER_API FLLMNPCSkeletonCapabilityBuilder
{
public:
	static FLLMNPCSkeletonCapabilityBuildResult Build(
		const ULLMNPCSkeletonProfile& Profile,
		const ULLMNPCControlManifest* ControlManifest,
		FLLMNPCSkeletonCapabilitySnapshot& OutSnapshot
	);

	static FLLMNPCSkeletonCapabilityBuildResult BuildAtTime(
		const ULLMNPCSkeletonProfile& Profile,
		const ULLMNPCControlManifest* ControlManifest,
		const FDateTime& GeneratedAtUtc,
		FLLMNPCSkeletonCapabilitySnapshot& OutSnapshot
	);

	static bool BuildModelViewJson(
		const FLLMNPCSkeletonCapabilitySnapshot& Snapshot,
		FString& OutJson,
		FString& OutError
	);

	static bool ModelViewContainsRestrictedFields(
		const FString& ModelViewJson,
		FString& OutRestrictedField
	);
};
