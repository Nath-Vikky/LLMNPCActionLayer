#pragma once

#include "CoreMinimal.h"
#include "LLMNPCActionTypes.generated.h"

UENUM(BlueprintType)
enum class ELLMNPCHand : uint8
{
	Auto,
	Left,
	Right,
	Both
};

UENUM(BlueprintType)
enum class ELLMNPCBodyMask : uint8
{
	UpperBody,
	FullBody
};

UENUM(BlueprintType)
enum class ELLMNPCEmotion : uint8
{
	Neutral,
	Friendly,
	Urgent,
	Angry,
	Shy,
	Confused,
	Sad
};

USTRUCT(BlueprintType)
struct FLLMNPCActionRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Action")
	FString ActionId = TEXT("gesture.wave");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Action")
	FString TargetRef = TEXT("player");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Action")
	ELLMNPCHand Hand = ELLMNPCHand::Auto;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Action")
	ELLMNPCBodyMask BodyMask = ELLMNPCBodyMask::UpperBody;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Action")
	ELLMNPCEmotion Emotion = ELLMNPCEmotion::Neutral;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Action", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Amplitude = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Action", meta=(ClampMin="0.1", ClampMax="2.0"))
	float Speed = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Action", meta=(ClampMin="0.2", ClampMax="5.0"))
	float Duration = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Action", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Height = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Action", meta=(ClampMin="1", ClampMax="8"))
	int32 Beats = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Action")
	bool bInterruptible = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Action", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Priority = 0.5f;
};

USTRUCT(BlueprintType)
struct FLLMNPCActionPlan
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Action")
	FString Version = TEXT("1.0");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Action")
	FString Intent = TEXT("none");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Action")
	TArray<FLLMNPCActionRequest> Actions;
};

USTRUCT(BlueprintType)
struct FLLMNPCRuntimeGestureState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Action")
	bool bHasActiveGesture = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Action")
	FName ActiveActionId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Action")
	FName ActiveTargetRef = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Action")
	float GestureAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Action")
	float RightHandIKAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Action")
	float LeftHandIKAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Action")
	FVector RightHandIKTargetWS = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Action")
	FVector LeftHandIKTargetWS = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Action")
	FVector GazeTargetWS = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Action")
	float HeadPitchOffset = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Action")
	float HeadYawOffset = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Action")
	float ChestYawOffset = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Action")
	float FingerPointAlphaRight = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Action")
	float FingerOpenAlphaRight = 0.0f;
};
