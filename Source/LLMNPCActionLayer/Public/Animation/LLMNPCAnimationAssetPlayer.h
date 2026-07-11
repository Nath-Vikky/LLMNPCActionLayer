#pragma once

#include "CoreMinimal.h"
#include "Templates/LLMNPCTemplateCompiler.h"
#include "UObject/Object.h"
#include "LLMNPCAnimationAssetPlayer.generated.h"

class AActor;
class UAnimInstance;
class UAnimMontage;
class UAnimationAsset;
class ULLMNPCMotionTemplate;
class USkeletalMeshComponent;

UENUM(BlueprintType)
enum class ELLMNPCAnimationPlaybackState : uint8
{
	Idle,
	Playing,
	Completed,
	Interrupted,
	TimedOut,
	Failed
};

USTRUCT(BlueprintType)
struct FLLMNPCAnimationPlaybackDebugState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Animation")
	ELLMNPCAnimationPlaybackState State = ELLMNPCAnimationPlaybackState::Idle;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Animation")
	FName TemplateId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Animation")
	FName SlotName = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Animation")
	float PlayRate = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Animation")
	FString ErrorCode;
};

UCLASS(BlueprintType)
class LLMNPCACTIONLAYER_API ULLMNPCAnimationAssetPlayer : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(AActor* InOwnerActor);
	void Shutdown();

	bool Play(
		const ULLMNPCMotionTemplate& MotionTemplate,
		const FLLMNPCTemplateModifiers& Modifiers,
		FString& OutError
	);

	void Stop(bool bInterrupted = true);
	bool IsPlaying() const { return State == ELLMNPCAnimationPlaybackState::Playing; }
	bool CanInterrupt() const { return bActiveInterruptible; }
	bool ConflictsWith(const TArray<FName>& Channels) const;
	const TArray<FName>& GetActiveChannels() const { return ActiveChannels; }

	UFUNCTION(BlueprintPure, Category="LLM NPC|Animation")
	FLLMNPCAnimationPlaybackDebugState GetDebugState() const;

	static bool ValidatePlaybackRequest(
		const ULLMNPCMotionTemplate& MotionTemplate,
		const FLLMNPCTemplateModifiers& Modifiers,
		UAnimationAsset*& OutAnimationAsset,
		float& OutPlayRate,
		FString& OutError
	);

protected:
	virtual void BeginDestroy() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<AActor> OwnerActor;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> MeshComponent;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveMontage;

	ELLMNPCAnimationPlaybackState State = ELLMNPCAnimationPlaybackState::Idle;
	FName ActiveTemplateId = NAME_None;
	FName ActiveSlotName = NAME_None;
	float ActivePlayRate = 1.0f;
	float ActiveBlendOutSeconds = 0.15f;
	bool bActiveInterruptible = false;
	TArray<FName> ActiveChannels;
	FString LastErrorCode;
	FTimerHandle TimeoutHandle;

	USkeletalMeshComponent* ResolveMesh() const;
	void HandleMontageEnded(UAnimMontage* Montage, bool bInterrupted);
	void HandleTimeout();
	void ClearActivePlayback(bool bPreserveTerminalState);
};
