#pragma once

#include "CoreMinimal.h"

class ULLMNPCMotionTemplate;
struct FLLMNPCSkeletonCapabilitySnapshot;

namespace LLMNPCMotionRecipeAuthoring
{
inline constexpr const TCHAR* PromptVersion =
	TEXT("llmnpc.motion_recipe_authoring_prompt.v2");
inline constexpr const TCHAR* ResponseSchemaVersion =
	TEXT("llmnpc.motion_recipe_authoring_response.v1");
inline constexpr const TCHAR* JobSchemaVersion =
	TEXT("llmnpc.motion_recipe_authoring_job.v1");
}

struct FLLMNPCMotionRecipePromptPackage
{
	FString PromptVersion = LLMNPCMotionRecipeAuthoring::PromptVersion;
	FString SystemPrompt;
	FString UserJson;
	FString RecipeSchemaJson;
	FString CapabilityModelViewJson;
	FString PromptHash;
	FString CapabilityHash;
	FString RegistryVersion;
	int32 SimilarTemplateCount = 0;
};

struct FLLMNPCMotionRecipeCatalogDraft
{
	FString DisplayName;
	FString SelectionSummary;
	FString VisualDescription;
	TArray<FString> SuitableWhen;
	TArray<FString> AvoidWhen;
};

struct FLLMNPCMotionRecipeAuthoringResponse
{
	bool bUnsupported = false;
	FString UnsupportedReason;
	FString RecipeJson;
	FLLMNPCMotionRecipeCatalogDraft CatalogDraft;
};

class FLLMNPCMotionRecipeAuthoringPrompt
{
public:
	static bool Build(
		const FString& DesiredAction,
		const FLLMNPCSkeletonCapabilitySnapshot& CapabilitySnapshot,
		const TArray<const ULLMNPCMotionTemplate*>& PublishedExamples,
		FLLMNPCMotionRecipePromptPackage& OutPackage,
		FString& OutError
	);

	static bool ParseResponse(
		const FString& ResponseJson,
		FLLMNPCMotionRecipeAuthoringResponse& OutResponse,
		FString& OutError
	);

	static bool ValidateRecipeForCapability(
		const FLLMNPCMotionRecipeAuthoringResponse& Response,
		const FLLMNPCSkeletonCapabilitySnapshot& CapabilitySnapshot,
		const TSet<FName>& AllowedPrimitiveIds,
		FName RequiredIntent,
		int32 MaxPrimitiveCount,
		FString& OutError
	);
};
