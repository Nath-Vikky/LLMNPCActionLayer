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
	ForwardN1ShoulderShrug,
	ForwardN1HandRelaxed,
	ForwardN1HandCurl,
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
struct FLLMNPCResolvedAxisBasis
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	FVector PitchAxis = FVector::RightVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	FVector YawAxis = FVector::UpVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	FVector RollAxis = FVector::ForwardVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	FRotator MinAdditiveRotation = FRotator(-45.0f, -60.0f, -45.0f);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	FRotator MaxAdditiveRotation = FRotator(45.0f, 60.0f, 45.0f);
};

USTRUCT(BlueprintType)
struct FLLMNPCPoseBoneBindings
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	FName ProfileId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	bool bApplyAxisCalibration = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	FVector ComponentForwardDirectionCS = FVector::RightVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	FVector ComponentUpDirectionCS = FVector::UpVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	FName Head = TEXT("head");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	FName Chest = TEXT("spine_03");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	FName RightShoulder = TEXT("clavicle_r");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	FName RightUpperArm = TEXT("upperarm_r");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	FName RightLowerArm = TEXT("lowerarm_r");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	FName RightHand = TEXT("hand_r");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	FName LeftShoulder = TEXT("clavicle_l");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	FName LeftUpperArm = TEXT("upperarm_l");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	FName LeftLowerArm = TEXT("lowerarm_l");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	FName LeftHand = TEXT("hand_l");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	FVector RightArmIKPoleDirectionCS = FVector::BackwardVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	float RightArmIKMaxReachScale = 0.98f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	FVector LeftArmIKPoleDirectionCS = FVector::ForwardVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	float LeftArmIKMaxReachScale = 0.98f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	FLLMNPCResolvedAxisBasis HeadAxis;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	FLLMNPCResolvedAxisBasis ChestAxis;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	FLLMNPCResolvedAxisBasis RightShoulderAxis;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	FLLMNPCResolvedAxisBasis RightUpperArmAxis;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	FLLMNPCResolvedAxisBasis RightLowerArmAxis;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	FLLMNPCResolvedAxisBasis RightHandAxis;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	FLLMNPCResolvedAxisBasis LeftShoulderAxis;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	FLLMNPCResolvedAxisBasis LeftUpperArmAxis;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	FLLMNPCResolvedAxisBasis LeftLowerArmAxis;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	FLLMNPCResolvedAxisBasis LeftHandAxis;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	TArray<FName> RightFingerBones = {
		TEXT("thumb_01_r"), TEXT("thumb_02_r"), TEXT("thumb_03_r"),
		TEXT("index_01_r"), TEXT("index_02_r"), TEXT("index_03_r"),
		TEXT("middle_01_r"), TEXT("middle_02_r"), TEXT("middle_03_r"),
		TEXT("ring_01_r"), TEXT("ring_02_r"), TEXT("ring_03_r"),
		TEXT("pinky_01_r"), TEXT("pinky_02_r"), TEXT("pinky_03_r")
	};

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	TArray<FName> LeftFingerBones = {
		TEXT("thumb_01_l"), TEXT("thumb_02_l"), TEXT("thumb_03_l"),
		TEXT("index_01_l"), TEXT("index_02_l"), TEXT("index_03_l"),
		TEXT("middle_01_l"), TEXT("middle_02_l"), TEXT("middle_03_l"),
		TEXT("ring_01_l"), TEXT("ring_02_l"), TEXT("ring_03_l"),
		TEXT("pinky_01_l"), TEXT("pinky_02_l"), TEXT("pinky_03_l")
	};

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	TArray<FRotator> RightFingerOpenRotations = {
		FRotator(-5.0f, 2.0f, 6.0f), FRotator(-2.0f, 2.0f, 2.0f), FRotator(0.0f, 1.0f, 0.0f),
		FRotator(0.0f, 7.0f, 0.0f), FRotator(0.0f, 5.0f, 0.0f), FRotator(0.0f, 2.0f, 0.0f),
		FRotator(0.0f, 6.0f, 0.0f), FRotator(0.0f, 5.0f, 0.0f), FRotator(0.0f, 2.0f, 0.0f),
		FRotator(0.0f, 6.0f, 1.0f), FRotator(0.0f, 5.0f, 0.0f), FRotator(0.0f, 2.0f, 0.0f),
		FRotator(0.0f, 6.0f, 2.0f), FRotator(0.0f, 5.0f, 0.0f), FRotator(0.0f, 2.0f, 0.0f)
	};

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	TArray<FRotator> RightFingerPointRotations = {
		FRotator(12.0f, -18.0f, -8.0f), FRotator(8.0f, -16.0f, -4.0f), FRotator(2.0f, -8.0f, 0.0f),
		FRotator(0.0f, -3.0f, 0.0f), FRotator(0.0f, -2.0f, 0.0f), FRotator(0.0f, -1.0f, 0.0f),
		FRotator(0.0f, -44.0f, 0.0f), FRotator(0.0f, -54.0f, 0.0f), FRotator(0.0f, -28.0f, 0.0f),
		FRotator(0.0f, -46.0f, 2.0f), FRotator(0.0f, -56.0f, 0.0f), FRotator(0.0f, -30.0f, 0.0f),
		FRotator(2.0f, -48.0f, 4.0f), FRotator(2.0f, -58.0f, 0.0f), FRotator(0.0f, -32.0f, 0.0f)
	};

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	TArray<FRotator> RightFingerRelaxedRotations = {
		FRotator(-3.0f, 1.0f, 3.0f), FRotator(-1.0f, 1.0f, 1.0f), FRotator(0.0f, 0.5f, 0.0f),
		FRotator(0.0f, 4.0f, 0.0f), FRotator(0.0f, 3.0f, 0.0f), FRotator(0.0f, 1.0f, 0.0f),
		FRotator(0.0f, 3.0f, 0.0f), FRotator(0.0f, 2.5f, 0.0f), FRotator(0.0f, 1.0f, 0.0f),
		FRotator(0.0f, 2.5f, 0.5f), FRotator(0.0f, 2.0f, 0.0f), FRotator(0.0f, 1.0f, 0.0f),
		FRotator(0.0f, 2.0f, 1.0f), FRotator(0.0f, 1.5f, 0.0f), FRotator(0.0f, 0.5f, 0.0f)
	};

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	TArray<FRotator> RightFingerCurlRotations = {
		FRotator(18.0f, -22.0f, -10.0f), FRotator(12.0f, -28.0f, -5.0f), FRotator(5.0f, -18.0f, 0.0f),
		FRotator(0.0f, -42.0f, 0.0f), FRotator(0.0f, -58.0f, 0.0f), FRotator(0.0f, -34.0f, 0.0f),
		FRotator(0.0f, -46.0f, 0.0f), FRotator(0.0f, -62.0f, 0.0f), FRotator(0.0f, -38.0f, 0.0f),
		FRotator(0.0f, -48.0f, 2.0f), FRotator(0.0f, -64.0f, 0.0f), FRotator(0.0f, -40.0f, 0.0f),
		FRotator(2.0f, -50.0f, 4.0f), FRotator(2.0f, -66.0f, 0.0f), FRotator(0.0f, -42.0f, 0.0f)
	};

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	TArray<FRotator> LeftFingerOpenRotations = {
		FRotator(-5.0f, -2.0f, -6.0f), FRotator(-2.0f, -2.0f, -2.0f), FRotator(0.0f, -1.0f, 0.0f),
		FRotator(0.0f, -7.0f, 0.0f), FRotator(0.0f, -5.0f, 0.0f), FRotator(0.0f, -2.0f, 0.0f),
		FRotator(0.0f, -6.0f, 0.0f), FRotator(0.0f, -5.0f, 0.0f), FRotator(0.0f, -2.0f, 0.0f),
		FRotator(0.0f, -6.0f, -1.0f), FRotator(0.0f, -5.0f, 0.0f), FRotator(0.0f, -2.0f, 0.0f),
		FRotator(0.0f, -6.0f, -2.0f), FRotator(0.0f, -5.0f, 0.0f), FRotator(0.0f, -2.0f, 0.0f)
	};

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	TArray<FRotator> LeftFingerPointRotations = {
		FRotator(12.0f, 18.0f, 8.0f), FRotator(8.0f, 16.0f, 4.0f), FRotator(2.0f, 8.0f, 0.0f),
		FRotator(0.0f, 3.0f, 0.0f), FRotator(0.0f, 2.0f, 0.0f), FRotator(0.0f, 1.0f, 0.0f),
		FRotator(0.0f, 44.0f, 0.0f), FRotator(0.0f, 54.0f, 0.0f), FRotator(0.0f, 28.0f, 0.0f),
		FRotator(0.0f, 46.0f, -2.0f), FRotator(0.0f, 56.0f, 0.0f), FRotator(0.0f, 30.0f, 0.0f),
		FRotator(2.0f, 48.0f, -4.0f), FRotator(2.0f, 58.0f, 0.0f), FRotator(0.0f, 32.0f, 0.0f)
	};

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	TArray<FRotator> LeftFingerRelaxedRotations = {
		FRotator(-3.0f, -1.0f, -3.0f), FRotator(-1.0f, -1.0f, -1.0f), FRotator(0.0f, -0.5f, 0.0f),
		FRotator(0.0f, -4.0f, 0.0f), FRotator(0.0f, -3.0f, 0.0f), FRotator(0.0f, -1.0f, 0.0f),
		FRotator(0.0f, -3.0f, 0.0f), FRotator(0.0f, -2.5f, 0.0f), FRotator(0.0f, -1.0f, 0.0f),
		FRotator(0.0f, -2.5f, -0.5f), FRotator(0.0f, -2.0f, 0.0f), FRotator(0.0f, -1.0f, 0.0f),
		FRotator(0.0f, -2.0f, -1.0f), FRotator(0.0f, -1.5f, 0.0f), FRotator(0.0f, -0.5f, 0.0f)
	};

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	TArray<FRotator> LeftFingerCurlRotations = {
		FRotator(18.0f, 22.0f, 10.0f), FRotator(12.0f, 28.0f, 5.0f), FRotator(5.0f, 18.0f, 0.0f),
		FRotator(0.0f, 42.0f, 0.0f), FRotator(0.0f, 58.0f, 0.0f), FRotator(0.0f, 34.0f, 0.0f),
		FRotator(0.0f, 46.0f, 0.0f), FRotator(0.0f, 62.0f, 0.0f), FRotator(0.0f, 38.0f, 0.0f),
		FRotator(0.0f, 48.0f, -2.0f), FRotator(0.0f, 64.0f, 0.0f), FRotator(0.0f, 40.0f, 0.0f),
		FRotator(2.0f, 50.0f, -4.0f), FRotator(2.0f, 66.0f, 0.0f), FRotator(0.0f, 42.0f, 0.0f)
	};
};

USTRUCT(BlueprintType)
struct FLLMProceduralPoseSnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Skeleton")
	FLLMNPCPoseBoneBindings BoneBindings;

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
	FRotator RightShoulderAdditiveRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	FRotator LeftShoulderAdditiveRotation = FRotator::ZeroRotator;

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
	float RightFingersRelaxed = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	float RightFingersCurl = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	float LeftFingersOpen = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	float LeftFingersPoint = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	float LeftFingersRelaxed = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	float LeftFingersCurl = 0.0f;
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
	bool bAnimationAssetPlaying = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	FString AnimationPlaybackState;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	FName ActiveAnimationTemplateId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	FName ActiveAnimationSlot = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	float ActiveAnimationPlayRate = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	FString LastAnimationError;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	FString MotionLODLevel;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	float MotionLODUpdateIntervalSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	int32 ReplicatedMotionSequence = 0;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	FName LastRequestedTemplateId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	FName LastResolvedTemplateId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	FName ActiveSourceTemplateId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	FString LastTargetRef;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	float RequestedAmplitude = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	float RequestedSpeedScale = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	float RequestedDurationScale = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	FName RequestedStyle = TEXT("neutral");

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	bool bRequestedMirror = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	int32 RequestedRandomSeed = 0;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	float ResolvedAmplitude = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	float ResolvedSpeedScale = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	float ResolvedDurationScale = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	FName ResolvedStyle = TEXT("neutral");

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	bool bResolvedMirror = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	int32 ResolvedRandomSeed = 0;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	bool bModifiersClamped = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	FString ModifierResolutionTrace;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	TArray<FName> ActiveChannels;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	FString LastValidationSource;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	bool bLastSubmissionAccepted = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	FLLMProceduralPoseSnapshot Snapshot;
};
