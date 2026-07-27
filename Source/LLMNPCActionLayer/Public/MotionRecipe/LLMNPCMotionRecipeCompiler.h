#pragma once

#include "CoreMinimal.h"
#include "Capabilities/LLMNPCSkeletonCapabilityTypes.h"
#include "LLMNPCMotionTypes.h"
#include "MotionRecipe/LLMNPCMotionPrimitiveRegistry.h"
#include "MotionRecipe/LLMNPCMotionRecipeTypes.h"

class ULLMNPCControlManifest;

struct LLMNPCACTIONLAYER_API FLLMNPCMotionRecipeCompileContext
{
	FLLMNPCMotionRecipeValidationContext ValidationContext;
	TMap<FName, FString> TargetBindings;
	const ULLMNPCControlManifest* ControlManifest = nullptr;
	float Priority = 0.75f;
};

class LLMNPCACTIONLAYER_API FLLMNPCMotionRecipeCompiler
{
public:
	static bool Compile(
		const FLLMNPCMotionRecipe& Recipe,
		const FLLMNPCSkeletonCapabilitySnapshot& CapabilitySnapshot,
		const FLLMNPCMotionPrimitiveRegistry& Registry,
		const FLLMNPCMotionRecipeCompileContext& Context,
		FLLMMotionPlan& OutPlan,
		FLLMNPCCompiledRecipeMetadata& OutMetadata,
		FString& OutError
	);
};
