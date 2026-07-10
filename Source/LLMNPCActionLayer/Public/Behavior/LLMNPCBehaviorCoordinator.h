#pragma once

#include "CoreMinimal.h"
#include "Dialogue/LLMNPCDialogueTypes.h"
#include "UObject/Object.h"
#include "LLMNPCBehaviorCoordinator.generated.h"

class ULLMNPCMotionComponent;

USTRUCT(BlueprintType)
struct FLLMNPCBehaviorExecutionResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Behavior")
	bool bAccepted = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Behavior")
	bool bActionExecuted = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Behavior")
	FName ResolvedTemplateId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Behavior")
	FName ErrorCode = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Behavior")
	FString ErrorMessage;
};

UCLASS(BlueprintType)
class LLMNPCACTIONLAYER_API ULLMNPCBehaviorCoordinator : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(ULLMNPCMotionComponent* InMotionComponent);

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Behavior")
	FLLMNPCBehaviorExecutionResult ExecuteModelDecision(FLLMNPCModelTurnDecision Decision);

private:
	UPROPERTY(Transient)
	TObjectPtr<ULLMNPCMotionComponent> MotionComponent;
};
