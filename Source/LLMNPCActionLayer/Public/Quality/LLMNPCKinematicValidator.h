#pragma once

#include "CoreMinimal.h"
#include "LLMNPCMotionTypes.h"
#include "LLMNPCKinematicValidator.generated.h"

class ULLMNPCControlManifest;
class ULLMNPCSkeletonProfile;

UENUM(BlueprintType)
enum class ELLMNPCKinematicIssueSeverity : uint8
{
	Diagnostic,
	Warning,
	Error
};

USTRUCT(BlueprintType)
struct FLLMNPCKinematicValidationIssue
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Quality")
	FString Code;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Quality")
	ELLMNPCKinematicIssueSeverity Severity = ELLMNPCKinematicIssueSeverity::Diagnostic;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Quality")
	FString FieldPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Quality")
	float SampleTimeSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Quality")
	double ObservedValue = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Quality")
	double LimitValue = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Quality")
	FString Message;
};

USTRUCT(BlueprintType)
struct FLLMNPCKinematicValidationSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Quality", meta=(ClampMin="1.0", ClampMax="240.0"))
	float SampleRateHz = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Quality", meta=(ClampMin="8", ClampMax="2048"))
	int32 MaxSamples = 512;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Quality", meta=(ClampMin="0.0"))
	float EndPoseTolerance = 0.01f;
};

USTRUCT(BlueprintType)
struct FLLMNPCKinematicTrackMetrics
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Quality")
	FName ControlId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Quality")
	double MaxAbsoluteValue = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Quality")
	double MaxAbsoluteSpeed = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Quality")
	double MaxAbsoluteAcceleration = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Quality")
	double MaxAbsoluteJerk = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Quality")
	double StartAbsoluteValue = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Quality")
	double EndAbsoluteValue = 0.0;
};

USTRUCT(BlueprintType)
struct FLLMNPCKinematicQualityReport
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Quality")
	FString SchemaVersion = TEXT("llmnpc.kinematic_quality_report.v1");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Quality")
	FName ProfileId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Quality")
	FString CapabilityHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Quality")
	FString PlanHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Quality")
	FString ReportHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Quality")
	float SampleRateHz = 60.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Quality")
	int32 SampleCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Quality")
	bool bBaselineApproved = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Quality")
	bool bPassed = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Quality")
	bool bBlockingFailure = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Quality")
	TArray<FLLMNPCKinematicTrackMetrics> TrackMetrics;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Quality")
	TArray<FLLMNPCKinematicValidationIssue> Issues;
};

class LLMNPCACTIONLAYER_API FLLMNPCKinematicValidator
{
public:
	static FLLMNPCKinematicQualityReport ValidatePlan(
		const FLLMMotionPlan& Plan,
		const ULLMNPCSkeletonProfile& Profile,
		const ULLMNPCControlManifest* ControlManifest,
		const FString& CapabilityHash,
		const FLLMNPCKinematicValidationSettings& Settings =
			FLLMNPCKinematicValidationSettings()
	);
};
