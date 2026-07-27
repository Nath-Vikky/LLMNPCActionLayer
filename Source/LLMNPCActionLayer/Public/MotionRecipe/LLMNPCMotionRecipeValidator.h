#pragma once

#include "CoreMinimal.h"
#include "Capabilities/LLMNPCSkeletonCapabilityTypes.h"
#include "MotionRecipe/LLMNPCMotionPrimitiveRegistry.h"
#include "MotionRecipe/LLMNPCMotionRecipeTypes.h"

class LLMNPCACTIONLAYER_API FLLMNPCMotionRecipeValidator
{
public:
	static bool ValidateAndNormalize(
		FLLMNPCMotionRecipe& InOutRecipe,
		const FLLMNPCSkeletonCapabilitySnapshot& CapabilitySnapshot,
		const FLLMNPCMotionPrimitiveRegistry& Registry,
		const FLLMNPCMotionRecipeValidationContext& Context,
		FLLMNPCMotionRecipeValidationResult& OutResult
	);
};
