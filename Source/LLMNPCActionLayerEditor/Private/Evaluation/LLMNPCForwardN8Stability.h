#pragma once

#include "CoreMinimal.h"
#include "LLMNPCMotionTypes.h"

class FJsonObject;

struct FLLMNPCForwardN8StabilityConfig
{
	FString ProfileName;
	int32 TargetRequestCount = 0;
	double MinimumDurationSeconds = 0.0;
	float PlaybackTimeoutSeconds = 30.0f;
	float RecoveryDwellSeconds = 0.35f;
	int32 MinimumUniqueTemplateCount = 1;
	bool bFormalGate = false;
};

struct FLLMNPCForwardN8StabilityRequestRecord
{
	int32 Sequence = 0;
	FName TemplateId = NAME_None;
	float SubmittedAtSeconds = 0.0f;
	float CompletedAtSeconds = 0.0f;
	float PlaybackWaitSeconds = 0.0f;
	float Amplitude = 1.0f;
	float SpeedScale = 1.0f;
	float DurationScale = 1.0f;
	FName Style = TEXT("neutral");
	bool bMirror = false;
	bool bAccepted = false;
	bool bPlaybackObserved = false;
	bool bPlaybackCompleted = false;
	bool bPoseRecovered = false;
	FString Error;
};

struct FLLMNPCForwardN8StabilityReport
{
	FString Status = TEXT("not_started");
	FString Outcome = TEXT("not_run");
	FString HumanReview = TEXT("pending");
	FString HumanReviewSource;
	FDateTime StartedAtUtc;
	FDateTime UpdatedAtUtc;
	FDateTime CompletedAtUtc;
	FDateTime HumanReviewedAtUtc;
	FString ActorName;
	FName SkeletonProfileId = NAME_None;
	FLLMNPCForwardN8StabilityConfig Config;
	double ElapsedSeconds = 0.0;
	int32 SubmittedRequestCount = 0;
	int32 AcceptedRequestCount = 0;
	int32 RejectedRequestCount = 0;
	int32 PlaybackObservedCount = 0;
	int32 CompletedRequestCount = 0;
	int32 PoseRecoveryFailureCount = 0;
	int32 ActorLossCount = 0;
	int32 PostProcessLossCount = 0;
	int32 MaxQueueCount = 0;
	int32 MaxActivePlanCount = 0;
	int32 FrameSampleCount = 0;
	int32 Hitch50MsCount = 0;
	int32 Hitch100MsCount = 0;
	double FrameTimeSumMs = 0.0;
	float AverageFrameTimeMs = 0.0f;
	float P95FrameTimeMs = 0.0f;
	float MaxFrameTimeMs = 0.0f;
	double StartUsedPhysicalMB = 0.0;
	double PeakUsedPhysicalMB = 0.0;
	double EndUsedPhysicalMB = 0.0;
	double UsedPhysicalDeltaMB = 0.0;
	float MaxActionPoseResidual = 0.0f;
	bool bMachinePassed = false;
	TArray<float> FrameTimeSamplesMs;
	TArray<FLLMNPCForwardN8StabilityRequestRecord> Requests;
	TArray<FString> Errors;
	TArray<FString> Warnings;
};

namespace LLMNPCForwardN8Stability
{
const TCHAR* GetSchemaVersion();
FLLMNPCForwardN8StabilityConfig BuildSmokeConfig();
FLLMNPCForwardN8StabilityConfig BuildFormalConfig();
bool ValidateConfig(
	const FLLMNPCForwardN8StabilityConfig& Config,
	FString& OutError
);
bool IsPlaybackBusy(const FLLMNPCMotionDebugState& Debug);
float MeasureActionPoseResidual(const FLLMProceduralPoseSnapshot& Snapshot);
void ObserveFrame(
	FLLMNPCForwardN8StabilityReport& Report,
	float DeltaTimeSeconds,
	const FLLMNPCMotionDebugState& Debug,
	double UsedPhysicalMB
);
void Finalize(FLLMNPCForwardN8StabilityReport& Report);
void ApplyHumanReview(
	FLLMNPCForwardN8StabilityReport& Report,
	const FString& Review,
	const FString& Source
);
TSharedRef<FJsonObject> BuildJson(const FLLMNPCForwardN8StabilityReport& Report);
}
