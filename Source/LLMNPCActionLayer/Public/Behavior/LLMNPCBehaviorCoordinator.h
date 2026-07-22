#pragma once

#include "CoreMinimal.h"
#include "Behavior/LLMNPCBehaviorTypes.h"
#include "Dialogue/LLMNPCDialogueTypes.h"
#include "TimerManager.h"
#include "UObject/Object.h"
#include "LLMNPCBehaviorCoordinator.generated.h"

class AActor;
class AAIController;
class ULLMNPCMotionComponent;
class ULLMNPCSceneContextComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FLLMNPCBehaviorFinishedEvent,
	const FLLMNPCBehaviorDebugState&,
	Result
);

UCLASS(BlueprintType)
class LLMNPCACTIONLAYER_API ULLMNPCBehaviorCoordinator : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(
		ULLMNPCMotionComponent* InMotionComponent,
		ULLMNPCSceneContextComponent* InSceneContext
	);

	void Shutdown();

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Behavior")
	FLLMNPCBehaviorExecutionResult ExecuteModelDecision(FLLMNPCModelTurnDecision Decision);

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Behavior")
	void CancelBehavior(FName Reason = NAME_None);

	UFUNCTION(BlueprintPure, Category="LLM NPC|Behavior")
	bool IsBehaviorActive() const;

	UFUNCTION(BlueprintPure, Category="LLM NPC|Behavior|Debug")
	FLLMNPCBehaviorDebugState GetDebugState() const { return DebugState; }

	UPROPERTY(BlueprintAssignable, Category="LLM NPC|Behavior")
	FLLMNPCBehaviorFinishedEvent OnBehaviorFinished;

	virtual UWorld* GetWorld() const override;
	virtual void BeginDestroy() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<ULLMNPCMotionComponent> MotionComponent;

	UPROPERTY(Transient)
	TObjectPtr<ULLMNPCSceneContextComponent> SceneContext;

	UPROPERTY(Transient)
	TObjectPtr<AAIController> ActiveController;

	UPROPERTY(Transient)
	TObjectPtr<AActor> ActiveTarget;

	UPROPERTY(Transient)
	FLLMNPCBehaviorPlan ActivePlan;

	UPROPERTY(Transient)
	FLLMNPCBehaviorDebugState DebugState;

	FLLMNPCBehaviorPolicy ActivePolicy;
	FTimerHandle BehaviorTickHandle;
	FDelegateHandle MoveFinishedHandle;
	double PlanStartedAtSeconds = 0.0;
	double StepStartedAtSeconds = 0.0;
	double LastTickAtSeconds = 0.0;
	uint32 ActiveMoveRequestId = 0;
	bool bActionExecutedInPlan = false;

	FLLMNPCBehaviorPolicy BuildPolicy() const;
	bool StartPlan(const FLLMNPCBehaviorPlan& Plan, FString& OutError);
	void ExecuteCurrentStep();
	void BeginMove(const FLLMNPCBehaviorStep& Step);
	void BeginFacing(const FLLMNPCBehaviorStep& Step);
	void TickBehavior();
	void AdvanceStep();
	void CompletePlan();
	void FailPlan(FName ErrorCode, const FString& ErrorMessage = FString());
	AActor* ResolveTarget(const FString& TargetRef) const;
	void UnbindMoveDelegate();
};
