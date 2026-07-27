#pragma once

#include "CoreMinimal.h"
#include "MotionRecipe/LLMNPCMotionRecipeTypes.h"

class LLMNPCACTIONLAYER_API FLLMNPCMotionRecipeParser
{
public:
	static bool Parse(
		const FString& JsonString,
		FLLMNPCMotionRecipe& OutRecipe,
		FString& OutError
	);
};
