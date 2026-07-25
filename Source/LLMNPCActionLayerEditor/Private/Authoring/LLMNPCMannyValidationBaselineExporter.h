#pragma once

#include "CoreMinimal.h"

class ULLMNPCSkeletonProfile;
struct FLLMNPCSkeletonCapabilitySnapshot;

class FLLMNPCMannyValidationBaselineExporter
{
public:
	static bool Export(
		const ULLMNPCSkeletonProfile& Profile,
		const FLLMNPCSkeletonCapabilitySnapshot& Capability,
		const FString& OutputFilename,
		FString& OutError,
		FString* OutBaselineHash = nullptr
	);

	static bool CalibrateThresholdsFromPublishedTemplates(
		ULLMNPCSkeletonProfile& Profile,
		float HeadroomMultiplier,
		FString& OutError
	);
};
