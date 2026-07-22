#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Context/LLMNPCContextTypes.h"
#include "LLMNPCSceneContextComponent.generated.h"

class AActor;

USTRUCT(BlueprintType)
struct FLLMNPCSceneTargetRegistration
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context")
	FString TargetRef;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context")
	TObjectPtr<AActor> TargetActor;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context")
	FName Category = TEXT("generic");

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context")
	TArray<FName> SemanticTags;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context")
	float Salience = 0.5f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context")
	bool bAvailable = true;
};

UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class LLMNPCACTIONLAYER_API ULLMNPCSceneContextComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="LLM NPC|Context|Scene", meta=(AutoCreateRefTerm="SemanticTags"))
	void RegisterSceneTarget(
		const FString& TargetRef,
		AActor* TargetActor,
		FName Category,
		const TArray<FName>& SemanticTags,
		float Salience
	);

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Context|Scene")
	void UnregisterSceneTarget(const FString& TargetRef);

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Context|Scene")
	void SetTargetAvailable(const FString& TargetRef, bool bAvailable);

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Context|Scene")
	void SetStateActive(FName State, bool bActive);

	UFUNCTION(BlueprintPure, Category="LLM NPC|Context|Scene")
	FLLMNPCSelectionContextSnapshot AppendToSnapshot(FLLMNPCSelectionContextSnapshot Snapshot) const;

	UFUNCTION(BlueprintPure, Category="LLM NPC|Context|Scene")
	bool IsTargetAvailable(const FString& TargetRef) const;

	UFUNCTION(BlueprintPure, Category="LLM NPC|Context|Scene")
	AActor* ResolveSceneTarget(const FString& TargetRef) const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Context|Scene")
	TArray<FName> ActiveStates;

private:
	UPROPERTY(Transient)
	TArray<FLLMNPCSceneTargetRegistration> RegisteredTargets;
};
