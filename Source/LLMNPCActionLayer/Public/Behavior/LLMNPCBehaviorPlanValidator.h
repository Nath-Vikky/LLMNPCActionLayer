#pragma once

#include "CoreMinimal.h"
#include "Behavior/LLMNPCBehaviorTypes.h"
#include "Dialogue/LLMNPCDialogueTypes.h"

class AActor;
class ULLMNPCMotionTemplate;
class ULLMNPCSceneContextComponent;

class LLMNPCACTIONLAYER_API FLLMNPCBehaviorPlanValidator
{
public:
	static bool BuildPlan(
		const FLLMNPCModelTurnDecision& Decision,
		const ULLMNPCMotionTemplate* MotionTemplate,
		const FLLMNPCTemplateModifiers& Modifiers,
		const ULLMNPCSceneContextComponent* SceneContext,
		const AActor* OwnerActor,
		const FLLMNPCBehaviorPolicy& Policy,
		FLLMNPCBehaviorPlan& OutPlan,
		FString& OutError
	);

	static bool ValidatePlan(
		const FLLMNPCBehaviorPlan& Plan,
		const FLLMNPCBehaviorPolicy& Policy,
		FString& OutError
	);
};
