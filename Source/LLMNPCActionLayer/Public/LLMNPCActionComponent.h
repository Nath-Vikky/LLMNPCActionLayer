#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LLMNPCActionManifest.h"
#include "LLMNPCActionTypes.h"
#include "LLMNPCActionComponent.generated.h"

class ULLMNPCActionLLMClient;
class ULLMNPCActionValidator;

UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class LLMNPCACTIONLAYER_API ULLMNPCActionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULLMNPCActionComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction
	) override;

	UFUNCTION(BlueprintCallable, Category="LLM NPC Action")
	bool SubmitActionPlanJson(const FString& JsonString);

	UFUNCTION(BlueprintCallable, Category="LLM NPC Action")
	bool SubmitActionPlan(const FLLMNPCActionPlan& Plan);

	UFUNCTION(BlueprintCallable, Category="LLM NPC Action")
	void RequestActionPlanFromContext(const FString& ContextJson);

	UFUNCTION(BlueprintCallable, Category="LLM NPC Action")
	void RegisterTargetRef(const FString& TargetRef, AActor* TargetActor);

	UFUNCTION(BlueprintCallable, Category="LLM NPC Action")
	void UnregisterTargetRef(const FString& TargetRef);

	UFUNCTION(BlueprintCallable, Category="LLM NPC Action")
	void ClearActionQueue();

	UFUNCTION(BlueprintCallable, Category="LLM NPC Action")
	void StopActiveAction();

	UFUNCTION(BlueprintCallable, Category="LLM NPC Action|Test")
	void TestWave(AActor* TargetActor);

	UFUNCTION(BlueprintCallable, Category="LLM NPC Action|Test")
	void TestPoint(AActor* TargetActor);

	UFUNCTION(BlueprintCallable, Category="LLM NPC Action|Test")
	void TestNod(AActor* TargetActor);

	UFUNCTION(BlueprintPure, Category="LLM NPC Action")
	const FLLMNPCRuntimeGestureState& GetRuntimeGestureState() const
	{
		return RuntimeState;
	}

	UFUNCTION(BlueprintPure, Category="LLM NPC Action")
	int32 GetQueuedActionCount() const
	{
		return ActionQueue.Num();
	}

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Action|Validation")
	TObjectPtr<ULLMNPCActionManifest> ActionManifest;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Action|Runtime", meta=(ClampMin="1", ClampMax="16"))
	int32 MaxQueuedActions = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Action|Runtime")
	bool bUpperBodyBusy = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Action|Runtime")
	bool bRightHandBusy = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Action|Runtime")
	bool bLeftHandBusy = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Action|Runtime")
	bool bCanUseFullBodyActions = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Action")
	FLLMNPCRuntimeGestureState RuntimeState;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Action|Debug")
	FString LastAcceptedPlanJson;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Action|Debug")
	FString LastRejectedReason;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Action|Debug")
	bool bLLMRequestInFlight = false;

private:
	UPROPERTY()
	TArray<FLLMNPCActionRequest> ActionQueue;

	UPROPERTY()
	TMap<FName, TObjectPtr<AActor>> RegisteredTargets;

	UPROPERTY()
	TObjectPtr<AActor> CurrentTargetActor = nullptr;

	UPROPERTY()
	TObjectPtr<ULLMNPCActionValidator> Validator;

	UPROPERTY()
	TObjectPtr<ULLMNPCActionLLMClient> LLMClient;

	FLLMNPCActionRequest ActiveAction;
	float ActiveElapsed = 0.0f;
	bool bActionActive = false;

private:
	bool StartNextAction();
	bool ResolveTarget(const FString& TargetRef, AActor*& OutTarget) const;
	bool CanAcceptAction(const FLLMNPCActionRequest& Action, FString& OutReason) const;

	void UpdateActiveAction(float DeltaTime);
	void ClearRuntimeState();

	void UpdateGaze(float NormalizedTime);
	void UpdateWave(float NormalizedTime);
	void UpdatePoint(float NormalizedTime);
	void UpdateNod(float NormalizedTime);

	bool ShouldUseRightHand() const;
	bool ShouldUseLeftHand() const;
	float GetEmotionAmplitudeScale() const;
	float GetEmotionSpeedScale() const;
	float GetEmotionChestYawScale() const;

	USkeletalMeshComponent* GetOwnerMesh() const;
	FVector GetSafeBoneLocation(const FName BoneOrSocketName, const FVector& Fallback) const;
};
