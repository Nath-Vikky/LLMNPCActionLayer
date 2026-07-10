#pragma once

#include "CoreMinimal.h"

class ULLMNPCMotionTemplate;

struct FLLMNPCParsedDraftInfo
{
	FString AssetName;
	FString SourceSequencePath;
	FString ReconstructionProfileHash;
};

class LLMNPCACTIONLAYEREDITOR_API FLLMNPCTemplateDraftImporter
{
public:
	static bool ParseDraftJson(
		const FString& DraftJson,
		ULLMNPCMotionTemplate& OutTemplate,
		FLLMNPCParsedDraftInfo& OutInfo,
		FString& OutError
	);
};
