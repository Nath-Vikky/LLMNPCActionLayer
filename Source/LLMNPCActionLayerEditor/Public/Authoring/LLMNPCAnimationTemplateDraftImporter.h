#pragma once

#include "CoreMinimal.h"

class UAnimationAsset;
class ULLMNPCMotionTemplate;

struct FLLMNPCParsedAnimationDraftInfo
{
	FString AssetName;
	FString SelectedAnimationAssetPath;
	FString SelectedAnimationAssetPackageHash;
	FString ReconstructionProfileHash;
};

class LLMNPCACTIONLAYEREDITOR_API FLLMNPCAnimationTemplateDraftImporter
{
public:
	static bool BuildAnimationAssetPackageHash(
		const UAnimationAsset& AnimationAsset,
		FString& OutPackageHash,
		FString& OutError
	);

	static bool ParseDraftJson(
		const FString& DraftJson,
		const UAnimationAsset& SelectedAnimationAsset,
		ULLMNPCMotionTemplate& OutTemplate,
		FLLMNPCParsedAnimationDraftInfo& OutInfo,
		FString& OutError
	);
};
