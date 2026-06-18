#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LLMNPCControlManifest.h"
#include "LLMNPCMotionTypes.h"
#include "LLMNPCMotionComponent.generated.h"

class UAnimInstance;
class ULLMNPCAPIClient;
class ULLMNPCMotionValidator;

UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class LLMNPCACTIONLAYER_API ULLMNPCMotionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULLMNPCMotionComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category="LLM NPC Motion")
	bool SubmitMotionPlanJson(const FString& JsonString);

	UFUNCTION(BlueprintCallable, Category="LLM NPC Motion")
	bool SubmitMotionPlan(FLLMMotionPlan Plan);

	UFUNCTION(BlueprintCallable, Category="LLM NPC Motion")
	void RequestMotionPlanFromContext(const FString& ContextJson);

	UFUNCTION(BlueprintCallable, Category="LLM NPC Motion")
	void RegisterTarget(const FString& TargetRef, AActor* TargetActor);

	UFUNCTION(BlueprintCallable, Category="LLM NPC Motion")
	void ClearTargets();

	UFUNCTION(BlueprintCallable, Category="LLM NPC Motion")
	void ClearQueue();

	UFUNCTION(BlueprintCallable, Category="LLM NPC Motion|Test")
	void TestNod();

	UFUNCTION(BlueprintCallable, Category="LLM NPC Motion|Test")
	void TestWave(AActor* TargetActor);

	UFUNCTION(BlueprintCallable, Category="LLM NPC Motion|Test")
	bool SubmitSampleMotionPlanJson(ELLMNPCMotionDebugSample Sample, AActor* TargetActor);

	UFUNCTION(BlueprintPure, Category="LLM NPC Motion|Test")
	FString BuildSampleMotionPlanJson(ELLMNPCMotionDebugSample Sample, const FString& TargetRef) const;

	UFUNCTION(BlueprintPure, Category="LLM NPC Motion")
	FLLMProceduralPoseSnapshot GetCurrentSnapshot() const;

	UFUNCTION(BlueprintPure, Category="LLM NPC Motion|Debug")
	FLLMNPCMotionDebugState GetDebugState() const;

	UFUNCTION(BlueprintPure, Category="LLM NPC Motion")
	int32 GetQueueCount() const { return Queue.Num(); }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Motion")
	TObjectPtr<ULLMNPCControlManifest> ControlManifest;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Motion")
	TSubclassOf<UAnimInstance> PostProcessAnimClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	bool bAutoInstallPostProcessAnimBP = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion", meta=(ClampMin="1", ClampMax="8"))
	int32 MaxQueueSize = 3;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	FString LastValidationError;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	FString LastRawMotionJson;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	FString LastAcceptedMotionJson;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	FString ActiveClipId;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	bool bMotionRequestInFlight = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	float ActiveTime = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	FLLMProceduralPoseSnapshot CurrentSnapshot;

private:
	UPROPERTY()
	TObjectPtr<ULLMNPCMotionValidator> Validator;

	UPROPERTY()
	TObjectPtr<ULLMNPCAPIClient> APIClient;

	UPROPERTY()
	TArray<FLLMMotionPlan> Queue;

	UPROPERTY()
	TMap<FString, TObjectPtr<AActor>> TargetMap;

	FLLMMotionPlan ActivePlan;
	bool bHasActivePlan = false;

private:
	bool StartNextPlan();
	void UpdateActivePlan(float DeltaTime);
	void InstallPostProcessAnimBP();
	USkeletalMeshComponent* GetOwnerMesh() const;

	static FLLMMotionPlan BuildNodMotionPlan();
	static FLLMMotionPlan BuildWaveMotionPlan(const FString& TargetRef);
	static FLLMMotionPlan BuildInvalidUnknownControlPlan();
	static FLLMMotionPlan BuildSampleMotionPlan(ELLMNPCMotionDebugSample Sample, const FString& TargetRef);
};
