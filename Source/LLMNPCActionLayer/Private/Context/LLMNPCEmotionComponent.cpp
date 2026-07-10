#include "Context/LLMNPCEmotionComponent.h"

ULLMNPCEmotionComponent::ULLMNPCEmotionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void ULLMNPCEmotionComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction
)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (PrimaryEmotion != TEXT("neutral") && DecayPerSecond > 0.0f)
	{
		Intensity = FMath::Max(0.0f, Intensity - DecayPerSecond * DeltaTime);
		if (Intensity <= KINDA_SMALL_NUMBER)
		{
			ResetEmotion();
		}
	}
}

void ULLMNPCEmotionComponent::SetEmotion(FName Emotion, float NewIntensity, float NewValence, float NewArousal)
{
	PrimaryEmotion = Emotion.IsNone() ? FName(TEXT("neutral")) : Emotion;
	Intensity = FMath::Clamp(NewIntensity, 0.0f, 1.0f);
	Valence = FMath::Clamp(NewValence, -1.0f, 1.0f);
	Arousal = FMath::Clamp(NewArousal, -1.0f, 1.0f);
}

void ULLMNPCEmotionComponent::ResetEmotion()
{
	PrimaryEmotion = TEXT("neutral");
	Intensity = 0.0f;
	Valence = 0.0f;
	Arousal = 0.0f;
}

FLLMNPCEmotionSnapshot ULLMNPCEmotionComponent::GetEmotionSnapshot() const
{
	FLLMNPCEmotionSnapshot Snapshot;
	Snapshot.PrimaryEmotion = PrimaryEmotion;
	Snapshot.Intensity = FMath::Clamp(Intensity, 0.0f, 1.0f);
	Snapshot.Valence = FMath::Clamp(Valence, -1.0f, 1.0f);
	Snapshot.Arousal = FMath::Clamp(Arousal, -1.0f, 1.0f);
	return Snapshot;
}
