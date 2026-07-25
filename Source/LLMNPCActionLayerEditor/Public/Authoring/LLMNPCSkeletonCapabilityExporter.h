#pragma once

#include "CoreMinimal.h"
#include "Capabilities/LLMNPCSkeletonCapabilityTypes.h"

class ULLMNPCControlManifest;
class ULLMNPCSkeletonProfile;

class LLMNPCACTIONLAYEREDITOR_API FLLMNPCSkeletonCapabilityExporter
{
public:
	static bool ExportModelView(
		const ULLMNPCSkeletonProfile& Profile,
		const ULLMNPCControlManifest* ControlManifest,
		const FString& OutputFilename,
		FLLMNPCSkeletonCapabilitySnapshot& OutSnapshot,
		FString& OutError
	);
};
