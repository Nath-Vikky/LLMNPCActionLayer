#pragma once

#include "CoreMinimal.h"
#include "Context/LLMNPCContextTypes.h"
#include "Context/LLMNPCExecutionContextTypes.h"
#include "Context/LLMNPCResolvedMotionModifiers.h"

class ULLMNPCModifierMappingProfile;
class ULLMNPCMotionTemplate;
class ULLMNPCSkeletonProfile;
struct FLLMMotionPlan;

class LLMNPCACTIONLAYER_API FLLMNPCContextModifierResolver
{
public:
	static bool Resolve(
		const ULLMNPCMotionTemplate& MotionTemplate,
		const FLLMNPCTemplateModifiers& RequestedModifiers,
		const FLLMNPCSelectionContextSnapshot& SelectionContext,
		const FLLMNPCExecutionContextSnapshot& ExecutionContext,
		const ULLMNPCModifierMappingProfile* MappingProfile,
		const ULLMNPCSkeletonProfile* SkeletonProfile,
		FLLMNPCResolvedMotionModifiers& OutModifiers,
		FLLMNPCModifierResolutionTrace& OutTrace,
		FString& OutError
	);

	static void ApplyToCompiledPlan(
		const ULLMNPCMotionTemplate& MotionTemplate,
		const FLLMNPCResolvedMotionModifiers& Modifiers,
		FLLMMotionPlan& InOutPlan
	);

	static bool UsesRightArm(const ULLMNPCMotionTemplate& MotionTemplate);
	static bool UsesLeftArm(const ULLMNPCMotionTemplate& MotionTemplate);
	static bool UsesFineHandMotion(const ULLMNPCMotionTemplate& MotionTemplate);
};
