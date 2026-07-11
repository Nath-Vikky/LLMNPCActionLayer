#pragma once

#include "CoreMinimal.h"
#include "LLMNPCMotionTypes.generated.h"

UENUM(BlueprintType)
enum class ELLMMotionTrackType : uint8
{
	Keyframes,
	Oscillator,
	Anchor,
	LookAt,
	IKReach,
	Hold,
	Spring
};

UENUM(BlueprintType)
enum class ELLMMotionValueType : uint8
{
	Float,
	Vector,
	Rotator,
	Transform
};

UENUM(BlueprintType)
enum class ELLMMotionEnvelope : uint8
{
	None,
	Smooth,
	EaseIn,
	EaseOut,
	EaseInOut
};

UENUM(BlueprintType)
enum class ELLMControlSolverType : uint8
{
	AdditiveRotation,
	TwoBoneIK,
	LookAt,
	FingerPoseBlend,
	LocalOffset
};

UENUM(BlueprintType)
enum class ELLMNPCMotionDebugSample : uint8
{
	Nod,
	Wave,
	InvalidUnknownControl
};

USTRUCT(BlueprintType)
struct FLLMMotionKeyFloat
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	float T = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	float V = 0.0f;
};

USTRUCT(BlueprintType)
struct FLLMMotionTrack
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	FName ControlId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	ELLMMotionTrackType TrackType = ELLMMotionTrackType::Keyframes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	ELLMMotionValueType ValueType = ELLMMotionValueType::Float;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion", meta=(ClampMin="0.0"))
	float StartTime = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion", meta=(ClampMin="0.0"))
	float EndTime = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	float Amplitude = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion", meta=(ClampMin="0.0"))
	float Frequency = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	float Phase = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	ELLMMotionEnvelope Envelope = ELLMMotionEnvelope::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Strength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Reach = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	FString TargetRef;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	FName Anchor = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	FVector Offset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	TArray<FLLMMotionKeyFloat> FloatKeys;
};

USTRUCT(BlueprintType)
struct FLLMMotionClip
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	FString ClipId = TEXT("clip_unnamed");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion", meta=(ClampMin="0.05", ClampMax="3.0"))
	float Duration = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion", meta=(ClampMin="0.0", ClampMax="1.0"))
	float BlendIn = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion", meta=(ClampMin="0.0", ClampMax="1.0"))
	float BlendOut = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Priority = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	bool bInterruptible = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	TArray<FLLMMotionTrack> Tracks;
};

USTRUCT(BlueprintType)
struct FLLMMotionPlan
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	FString Version = TEXT("1.0");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	FString Intent = TEXT("none");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	FLLMMotionClip Clip;
};

USTRUCT(BlueprintType)
struct FLLMProceduralPoseSnapshot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	float GlobalAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	float HeadPitch = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	float HeadYaw = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	float HeadRoll = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	float ChestPitch = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	float ChestYaw = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	float ChestRoll = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	float RightHandIKAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	FVector RightHandIKTargetCS = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	FVector RightHandLocalOffsetCS = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	FRotator RightUpperArmAdditiveRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	FRotator RightLowerArmAdditiveRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	FRotator RightHandAdditiveRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	FRotator LeftUpperArmAdditiveRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	FRotator LeftLowerArmAdditiveRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	FRotator LeftHandAdditiveRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	bool bLeftArmFKMirroredSource = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	FVector RightHandPalmTargetCS = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	float RightHandPalmAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	float LeftHandIKAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	FVector LeftHandIKTargetCS = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	FVector LeftHandLocalOffsetCS = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	FVector LeftHandPalmTargetCS = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	float LeftHandPalmAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	FVector GazeTargetCS = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	float GazeAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	float RightFingersOpen = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	float RightFingersPoint = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	float LeftFingersOpen = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	float LeftFingersPoint = 0.0f;
};

USTRUCT(BlueprintType)
struct FLLMNPCMotionDebugState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	FString LastRawMotionJson;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	FString LastAcceptedMotionJson;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	FString LastValidationError;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	FString ActiveClipId;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	float ActiveTime = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	float ActiveDuration = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	int32 QueueCount = 0;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	bool bHasActivePlan = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	int32 ActivePlanCount = 0;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	bool bMotionRequestInFlight = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	bool bPostProcessInstalled = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	FString LastPostProcessError;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	FLLMProceduralPoseSnapshot Snapshot;
};
