#include "LLMNPCPostProcessAnimInstance.h"

#include "LLMNPCMotionComponent.h"

void ULLMNPCPostProcessAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	if (AActor* Owner = GetOwningActor())
	{
		MotionComponent = Owner->FindComponentByClass<ULLMNPCMotionComponent>();
	}
}

void ULLMNPCPostProcessAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!MotionComponent)
	{
		if (AActor* Owner = GetOwningActor())
		{
			MotionComponent = Owner->FindComponentByClass<ULLMNPCMotionComponent>();
		}
	}

	Snapshot = MotionComponent ? MotionComponent->GetCurrentSnapshot() : FLLMProceduralPoseSnapshot();
}
