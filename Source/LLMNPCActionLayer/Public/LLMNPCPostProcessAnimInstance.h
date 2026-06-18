#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "LLMNPCMotionTypes.h"
#include "LLMNPCPostProcessAnimInstance.generated.h"

class ULLMNPCMotionComponent;

UCLASS(Blueprintable, BlueprintType)
class LLMNPCACTIONLAYER_API ULLMNPCPostProcessAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	FLLMProceduralPoseSnapshot Snapshot;

private:
	UPROPERTY(Transient)
	TObjectPtr<ULLMNPCMotionComponent> MotionComponent;
};
