#pragma once

#include "CoreMinimal.h"
#include "Capabilities/LLMNPCSkeletonCapabilityTypes.h"
#include "MotionRecipe/LLMNPCMotionRecipeTypes.h"

class LLMNPCACTIONLAYER_API FLLMNPCMotionPrimitiveRegistry
{
public:
	static const FLLMNPCMotionPrimitiveRegistry& Get();

	const FString& GetRegistryVersion() const;
	const TArray<FLLMNPCMotionPrimitiveDefinition>& GetDefinitions() const;
	const FLLMNPCMotionPrimitiveDefinition* Find(FName PrimitiveId) const;

	void ResolveChannels(
		const FLLMNPCMotionPrimitiveDefinition& Definition,
		FName Side,
		TArray<FName>& OutChannels
	) const;

	bool BuildModelSchemaJson(
		const FLLMNPCSkeletonCapabilitySnapshot* CapabilitySnapshot,
		FString& OutJson,
		FString& OutError
	) const;

private:
	FLLMNPCMotionPrimitiveRegistry();

	bool IsDefinitionSupported(
		const FLLMNPCMotionPrimitiveDefinition& Definition,
		const FLLMNPCSkeletonCapabilitySnapshot& CapabilitySnapshot
	) const;

	FString RegistryVersion = LLMNPCMotionRecipe::RegistryVersion;
	TArray<FLLMNPCMotionPrimitiveDefinition> Definitions;
};
