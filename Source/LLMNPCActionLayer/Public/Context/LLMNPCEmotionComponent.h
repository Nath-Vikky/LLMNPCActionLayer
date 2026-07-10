#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Context/LLMNPCContextTypes.h"
#include "LLMNPCEmotionComponent.generated.h"

UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class LLMNPCACTIONLAYER_API ULLMNPCEmotionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULLMNPCEmotionComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Context|Emotion")
	void SetEmotion(FName Emotion, float Intensity, float Valence = 0.0f, float Arousal = 0.0f);

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Context|Emotion")
	void ResetEmotion();

	UFUNCTION(BlueprintPure, Category="LLM NPC|Context|Emotion")
	FLLMNPCEmotionSnapshot GetEmotionSnapshot() const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Context|Emotion")
	FName PrimaryEmotion = TEXT("neutral");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Context|Emotion", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Intensity = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Context|Emotion", meta=(ClampMin="-1.0", ClampMax="1.0"))
	float Valence = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Context|Emotion", meta=(ClampMin="-1.0", ClampMax="1.0"))
	float Arousal = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Context|Emotion", meta=(ClampMin="0.0"))
	float DecayPerSecond = 0.08f;
};
