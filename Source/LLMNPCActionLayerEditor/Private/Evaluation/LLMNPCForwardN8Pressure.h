#pragma once

#include "CoreMinimal.h"

class FJsonObject;

struct FLLMNPCForwardN8PressureConfig
{
	FString ProfileName;
	int32 TargetActorCount = 0;
	int32 RoundCount = 0;
	double MinimumDurationSeconds = 0.0;
	float WarmupSeconds = 2.0f;
	float PlaybackTimeoutSeconds = 30.0f;
	float RecoveryDwellSeconds = 0.35f;
	int32 MinimumUniqueTemplateCount = 1;
	int32 MinimumFullLODCount = 0;
	int32 MinimumReducedLODCount = 0;
	int32 MinimumMinimalLODCount = 0;
	bool bFormalGate = false;
};

struct FLLMNPCForwardN8PressureActorRecord
{
	int32 ActorIndex = INDEX_NONE;
	FString ActorName;
	bool bSpawnedByRunner = false;
	float ViewerDistanceCm = 0.0f;
	int32 PlacementAttemptCount = 0;
	float FacingYawDegrees = 0.0f;
	FString DesiredLODLevel;
	TArray<FString> ObservedLODLevels;
	bool bPostProcessRequired = false;
	bool bPostProcessInstalled = false;
	bool bPostProcessReady = false;
	FString PostProcessError;
	int32 SubmittedRequestCount = 0;
	int32 AcceptedRequestCount = 0;
	int32 PlaybackObservedCount = 0;
	int32 CompletedRequestCount = 0;
	int32 PoseRecoveryFailureCount = 0;
	int32 MaxQueueCount = 0;
	int32 MaxActivePlanCount = 0;
	float MaxActionPoseResidual = 0.0f;
};

struct FLLMNPCForwardN8PressureRequestRecord
{
	int32 Sequence = 0;
	int32 Round = 0;
	int32 ActorIndex = INDEX_NONE;
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

struct FLLMNPCForwardN8PressureReport
{
	FString Status = TEXT("not_started");
	FString Outcome = TEXT("not_run");
	FString HumanReview = TEXT("pending");
	FString HumanReviewSource;
	FDateTime StartedAtUtc;
	FDateTime UpdatedAtUtc;
	FDateTime CompletedAtUtc;
	FDateTime HumanReviewedAtUtc;
	FName SkeletonProfileId = NAME_None;
	FLLMNPCForwardN8PressureConfig Config;
	double ElapsedSeconds = 0.0;
	int32 ManagedActorCount = 0;
	int32 SpawnedActorCount = 0;
	int32 DestroyedSpawnedActorCount = 0;
	int32 CompletedRoundCount = 0;
	int32 SubmittedRequestCount = 0;
	int32 AcceptedRequestCount = 0;
	int32 RejectedRequestCount = 0;
	int32 PlaybackObservedCount = 0;
	int32 CompletedRequestCount = 0;
	int32 PoseRecoveryFailureCount = 0;
	int32 ActorLossCount = 0;
	int32 PostProcessFailureCount = 0;
	int32 MaxPerActorQueueCount = 0;
	int32 MaxAggregateQueueCount = 0;
	int32 QueueNonZeroFrameCount = 0;
	int32 MaxRoundBoundaryAggregateQueueCount = 0;
	int32 FinalAggregateQueueCount = 0;
	int32 MaxAggregateActivePlanCount = 0;
	int32 FinalAggregateActivePlanCount = 0;
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
	FString BaselineReportFile;
	float BaselineP95FrameTimeMs = 0.0f;
	float P95FrameTimeDeltaMs = 0.0f;
	float P95FrameTimeRatio = 0.0f;
	bool bMachinePassed = false;
	TArray<float> FrameTimeSamplesMs;
	TArray<FLLMNPCForwardN8PressureActorRecord> Actors;
	TArray<FLLMNPCForwardN8PressureRequestRecord> Requests;
	TArray<FString> Errors;
	TArray<FString> Warnings;
};

namespace LLMNPCForwardN8Pressure
{
const TCHAR* GetSchemaVersion();
FLLMNPCForwardN8PressureConfig BuildSmokeConfig();
FLLMNPCForwardN8PressureConfig BuildTenNPCConfig();
FLLMNPCForwardN8PressureConfig BuildThirtyNPCLODConfig();
int32 GetExpectedRequestCount(const FLLMNPCForwardN8PressureConfig& Config);
bool ValidateConfig(
	const FLLMNPCForwardN8PressureConfig& Config,
	FString& OutError
);
void ObserveFrame(
	FLLMNPCForwardN8PressureReport& Report,
	float DeltaTimeSeconds,
	int32 AggregateQueueCount,
	int32 AggregateActivePlanCount,
	double UsedPhysicalMB
);
void Finalize(FLLMNPCForwardN8PressureReport& Report);
void ApplyHumanReview(
	FLLMNPCForwardN8PressureReport& Report,
	const FString& Review,
	const FString& Source
);
TSharedRef<FJsonObject> BuildJson(const FLLMNPCForwardN8PressureReport& Report);
}
