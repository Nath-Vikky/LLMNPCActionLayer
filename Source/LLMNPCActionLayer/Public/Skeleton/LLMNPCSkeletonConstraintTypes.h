#pragma once

#include "CoreMinimal.h"
#include "LLMNPCSkeletonConstraintTypes.generated.h"

UENUM(BlueprintType)
enum class ELLMNPCCollisionProxyShape : uint8
{
	Sphere,
	Capsule
};

USTRUCT(BlueprintType)
struct FLLMNPCKinematicControlConstraint
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Constraints")
	FName ControlId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Constraints", meta=(ClampMin="0.0"))
	float MaxAngularSpeedDegreesPerSecond = 360.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Constraints", meta=(ClampMin="0.0"))
	float MaxAngularAccelerationDegreesPerSecondSquared = 1440.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Constraints", meta=(ClampMin="0.0"))
	float MaxAngularJerkDegreesPerSecondCubed = 7200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Constraints", meta=(ClampMin="0.0"))
	float MaxPositionSpeedCentimetersPerSecond = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Constraints", meta=(ClampMin="0.0"))
	float MaxPositionAccelerationCentimetersPerSecondSquared = 720.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Constraints", meta=(ClampMin="0.0"))
	float MaxPositionJerkCentimetersPerSecondCubed = 3600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Constraints", meta=(ClampMin="0.0"))
	float MaxNormalizedSpeedPerSecond = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Constraints", meta=(ClampMin="0.0"))
	float MaxNormalizedAccelerationPerSecondSquared = 480.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Constraints", meta=(ClampMin="0.0"))
	float MaxNormalizedJerkPerSecondCubed = 20000.0f;
};

USTRUCT(BlueprintType)
struct FLLMNPCCollisionProxyProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Collision")
	FName ProxyId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Collision")
	ELLMNPCCollisionProxyShape Shape = ELLMNPCCollisionProxyShape::Sphere;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Collision")
	FName AnchorBoneSemantic = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Collision")
	FVector LocalOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Collision", meta=(ClampMin="0.1"))
	float RadiusCentimeters = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Collision", meta=(ClampMin="0.0"))
	float HalfHeightCentimeters = 0.0f;
};

USTRUCT(BlueprintType)
struct FLLMNPCUpperBodyConstraintProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Constraints", meta=(ClampMin="0.0", ClampMax="180.0"))
	float MaxHeadLookAngleDegrees = 70.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Constraints", meta=(ClampMin="0.0", ClampMax="180.0"))
	float MaxChestAdditiveAngleDegrees = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Constraints")
	FVector HandReachBoundsMinCS = FVector(-55.0f, -95.0f, -70.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Constraints")
	FVector HandReachBoundsMaxCS = FVector(95.0f, 95.0f, 100.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Constraints")
	bool bKinematicBaselineApproved = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Constraints")
	FString ValidationBaselineVersion;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Constraints")
	FString ValidationBaselineHash;
};
