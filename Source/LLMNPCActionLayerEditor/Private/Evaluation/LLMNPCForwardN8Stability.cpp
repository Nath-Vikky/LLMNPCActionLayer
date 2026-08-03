#include "Evaluation/LLMNPCForwardN8Stability.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
void AddUnique(TArray<FString>& Values, const FString& Value)
{
	if (!Value.IsEmpty())
	{
		Values.AddUnique(Value);
	}
}

float MaxAbsRotator(const FRotator& Rotation)
{
	return FMath::Max3(
		FMath::Abs(static_cast<float>(Rotation.Pitch)),
		FMath::Abs(static_cast<float>(Rotation.Yaw)),
		FMath::Abs(static_cast<float>(Rotation.Roll))
	);
}

TArray<TSharedPtr<FJsonValue>> StringsToJson(const TArray<FString>& Values)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	Result.Reserve(Values.Num());
	for (const FString& Value : Values)
	{
		Result.Add(MakeShared<FJsonValueString>(Value));
	}
	return Result;
}
}

namespace LLMNPCForwardN8Stability
{
const TCHAR* GetSchemaVersion()
{
	return TEXT("llmnpc.forward_n8_stability.v1");
}

FLLMNPCForwardN8StabilityConfig BuildSmokeConfig()
{
	FLLMNPCForwardN8StabilityConfig Config;
	Config.ProfileName = TEXT("smoke");
	Config.TargetRequestCount = 8;
	Config.MinimumDurationSeconds = 20.0;
	Config.PlaybackTimeoutSeconds = 30.0f;
	Config.RecoveryDwellSeconds = 0.35f;
	Config.MinimumUniqueTemplateCount = 4;
	return Config;
}

FLLMNPCForwardN8StabilityConfig BuildFormalConfig()
{
	FLLMNPCForwardN8StabilityConfig Config;
	Config.ProfileName = TEXT("formal_30m_500");
	Config.TargetRequestCount = 500;
	Config.MinimumDurationSeconds = 30.0 * 60.0;
	Config.PlaybackTimeoutSeconds = 30.0f;
	Config.RecoveryDwellSeconds = 0.35f;
	Config.MinimumUniqueTemplateCount = 6;
	Config.bFormalGate = true;
	return Config;
}

bool ValidateConfig(
	const FLLMNPCForwardN8StabilityConfig& Config,
	FString& OutError
)
{
	OutError.Reset();
	if (Config.ProfileName.TrimStartAndEnd().IsEmpty())
	{
		OutError = TEXT("LLMNPC_N8_STABILITY_PROFILE_EMPTY");
	}
	else if (Config.TargetRequestCount <= 0)
	{
		OutError = TEXT("LLMNPC_N8_STABILITY_REQUEST_TARGET_INVALID");
	}
	else if (!FMath::IsFinite(Config.MinimumDurationSeconds) || Config.MinimumDurationSeconds <= 0.0)
	{
		OutError = TEXT("LLMNPC_N8_STABILITY_DURATION_INVALID");
	}
	else if (!FMath::IsFinite(Config.PlaybackTimeoutSeconds) || Config.PlaybackTimeoutSeconds <= 0.0f)
	{
		OutError = TEXT("LLMNPC_N8_STABILITY_PLAYBACK_TIMEOUT_INVALID");
	}
	else if (!FMath::IsFinite(Config.RecoveryDwellSeconds) || Config.RecoveryDwellSeconds < 0.1f)
	{
		OutError = TEXT("LLMNPC_N8_STABILITY_RECOVERY_DWELL_INVALID");
	}
	else if (Config.MinimumUniqueTemplateCount <= 0 || Config.MinimumUniqueTemplateCount > Config.TargetRequestCount)
	{
		OutError = TEXT("LLMNPC_N8_STABILITY_TEMPLATE_COVERAGE_INVALID");
	}
	else if (
		Config.bFormalGate &&
		(
			Config.TargetRequestCount < 500 ||
			Config.MinimumDurationSeconds < 1800.0
		)
	)
	{
		OutError = TEXT("LLMNPC_N8_STABILITY_FORMAL_BUDGET_TOO_SMALL");
	}
	return OutError.IsEmpty();
}

bool IsPlaybackBusy(const FLLMNPCMotionDebugState& Debug)
{
	return Debug.bHasActivePlan ||
		Debug.ActivePlanCount > 0 ||
		Debug.QueueCount > 0 ||
		Debug.bMotionRequestInFlight ||
		Debug.bAnimationAssetPlaying;
}

float MeasureActionPoseResidual(const FLLMProceduralPoseSnapshot& Snapshot)
{
	float Residual = 0.0f;
	auto Include = [&Residual](float Value)
	{
		Residual = FMath::Max(Residual, FMath::Abs(Value));
	};

	Include(MaxAbsRotator(Snapshot.RightShoulderAdditiveRotation));
	Include(MaxAbsRotator(Snapshot.LeftShoulderAdditiveRotation));
	Include(MaxAbsRotator(Snapshot.RightUpperArmAdditiveRotation));
	Include(MaxAbsRotator(Snapshot.RightLowerArmAdditiveRotation));
	Include(MaxAbsRotator(Snapshot.RightHandAdditiveRotation));
	Include(MaxAbsRotator(Snapshot.LeftUpperArmAdditiveRotation));
	Include(MaxAbsRotator(Snapshot.LeftLowerArmAdditiveRotation));
	Include(MaxAbsRotator(Snapshot.LeftHandAdditiveRotation));
	Include(Snapshot.RightHandIKAlpha);
	Include(Snapshot.LeftHandIKAlpha);
	Include(Snapshot.RightHandPalmAlpha);
	Include(Snapshot.LeftHandPalmAlpha);
	Include(Snapshot.RightHandPalmFacingAlpha);
	Include(Snapshot.LeftHandPalmFacingAlpha);
	Include(Snapshot.RightFingersOpen);
	Include(Snapshot.RightFingersContact);
	Include(Snapshot.RightFingersPoint);
	Include(Snapshot.RightFingersRelaxed);
	Include(Snapshot.RightFingersCurl);
	Include(Snapshot.RightFingersThumbsUp);
	Include(Snapshot.LeftFingersOpen);
	Include(Snapshot.LeftFingersContact);
	Include(Snapshot.LeftFingersPoint);
	Include(Snapshot.LeftFingersRelaxed);
	Include(Snapshot.LeftFingersCurl);
	Include(Snapshot.LeftFingersThumbsUp);
	return Residual;
}

void ObserveFrame(
	FLLMNPCForwardN8StabilityReport& Report,
	float DeltaTimeSeconds,
	const FLLMNPCMotionDebugState& Debug,
	double UsedPhysicalMB
)
{
	if (!FMath::IsFinite(DeltaTimeSeconds) || DeltaTimeSeconds < 0.0f)
	{
		AddUnique(Report.Errors, TEXT("LLMNPC_N8_STABILITY_FRAME_DELTA_INVALID"));
		return;
	}

	const float FrameTimeMs = DeltaTimeSeconds * 1000.0f;
	++Report.FrameSampleCount;
	Report.FrameTimeSumMs += FrameTimeMs;
	Report.FrameTimeSamplesMs.Add(FrameTimeMs);
	Report.MaxFrameTimeMs = FMath::Max(Report.MaxFrameTimeMs, FrameTimeMs);
	Report.Hitch50MsCount += FrameTimeMs >= 50.0f ? 1 : 0;
	Report.Hitch100MsCount += FrameTimeMs >= 100.0f ? 1 : 0;
	Report.MaxQueueCount = FMath::Max(Report.MaxQueueCount, Debug.QueueCount);
	Report.MaxActivePlanCount = FMath::Max(
		Report.MaxActivePlanCount,
		Debug.ActivePlanCount
	);
	Report.PeakUsedPhysicalMB = FMath::Max(
		Report.PeakUsedPhysicalMB,
		UsedPhysicalMB
	);
}

void Finalize(FLLMNPCForwardN8StabilityReport& Report)
{
	Report.AverageFrameTimeMs = Report.FrameSampleCount > 0
		? static_cast<float>(Report.FrameTimeSumMs / Report.FrameSampleCount)
		: 0.0f;
	if (!Report.FrameTimeSamplesMs.IsEmpty())
	{
		TArray<float> Sorted = Report.FrameTimeSamplesMs;
		Sorted.Sort();
		const int32 P95Index = FMath::Clamp(
			FMath::CeilToInt(Sorted.Num() * 0.95f) - 1,
			0,
			Sorted.Num() - 1
		);
		Report.P95FrameTimeMs = Sorted[P95Index];
	}
	Report.UsedPhysicalDeltaMB =
		Report.EndUsedPhysicalMB - Report.StartUsedPhysicalMB;

	TSet<FName> UniqueTemplates;
	for (const FLLMNPCForwardN8StabilityRequestRecord& Request : Report.Requests)
	{
		if (Request.bAccepted)
		{
			UniqueTemplates.Add(Request.TemplateId);
		}
	}

	if (Report.Status != TEXT("complete"))
	{
		AddUnique(Report.Errors, TEXT("LLMNPC_N8_STABILITY_NOT_COMPLETE"));
	}
	if (Report.ElapsedSeconds + KINDA_SMALL_NUMBER < Report.Config.MinimumDurationSeconds)
	{
		AddUnique(Report.Errors, TEXT("LLMNPC_N8_STABILITY_DURATION_INCOMPLETE"));
	}
	if (Report.SubmittedRequestCount != Report.Config.TargetRequestCount)
	{
		AddUnique(Report.Errors, TEXT("LLMNPC_N8_STABILITY_REQUEST_COUNT_MISMATCH"));
	}
	if (
		Report.AcceptedRequestCount != Report.SubmittedRequestCount ||
		Report.RejectedRequestCount != 0
	)
	{
		AddUnique(Report.Errors, TEXT("LLMNPC_N8_STABILITY_SUBMISSION_REJECTED"));
	}
	if (Report.PlaybackObservedCount != Report.AcceptedRequestCount)
	{
		AddUnique(Report.Errors, TEXT("LLMNPC_N8_STABILITY_PLAYBACK_NOT_OBSERVED"));
	}
	if (Report.CompletedRequestCount != Report.AcceptedRequestCount)
	{
		AddUnique(Report.Errors, TEXT("LLMNPC_N8_STABILITY_PLAYBACK_INCOMPLETE"));
	}
	if (Report.PoseRecoveryFailureCount != 0)
	{
		AddUnique(Report.Errors, TEXT("LLMNPC_N8_STABILITY_POSE_NOT_RECOVERED"));
	}
	if (Report.ActorLossCount != 0)
	{
		AddUnique(Report.Errors, TEXT("LLMNPC_N8_STABILITY_ACTOR_LOST"));
	}
	if (Report.PostProcessLossCount != 0)
	{
		AddUnique(Report.Errors, TEXT("LLMNPC_N8_STABILITY_POST_PROCESS_LOST"));
	}
	if (UniqueTemplates.Num() < Report.Config.MinimumUniqueTemplateCount)
	{
		AddUnique(Report.Errors, TEXT("LLMNPC_N8_STABILITY_TEMPLATE_COVERAGE_INCOMPLETE"));
	}
	if (Report.FrameSampleCount <= 0)
	{
		AddUnique(Report.Errors, TEXT("LLMNPC_N8_STABILITY_FRAME_SAMPLES_EMPTY"));
	}
	if (Report.UsedPhysicalDeltaMB > 512.0)
	{
		AddUnique(
			Report.Warnings,
			TEXT("Process physical memory grew by more than 512 MB; inspect Unreal Insights before release.")
		);
	}

	Report.bMachinePassed = Report.Errors.IsEmpty();
	Report.Outcome = Report.bMachinePassed ? TEXT("passed") : TEXT("failed");
	Report.FrameTimeSamplesMs.Reset();
}

void ApplyHumanReview(
	FLLMNPCForwardN8StabilityReport& Report,
	const FString& Review,
	const FString& Source
)
{
	Report.HumanReview = Review;
	Report.HumanReviewSource = Source;
	Report.HumanReviewedAtUtc = FDateTime::UtcNow();
	Report.UpdatedAtUtc = Report.HumanReviewedAtUtc;
}

TSharedRef<FJsonObject> BuildJson(const FLLMNPCForwardN8StabilityReport& Report)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("schema_version"), GetSchemaVersion());
	Root->SetStringField(TEXT("status"), Report.Status);
	Root->SetStringField(TEXT("outcome"), Report.Outcome);
	Root->SetBoolField(TEXT("machine_passed"), Report.bMachinePassed);
	Root->SetStringField(TEXT("human_review"), Report.HumanReview);
	Root->SetStringField(TEXT("human_review_source"), Report.HumanReviewSource);
	Root->SetStringField(TEXT("started_at_utc"), Report.StartedAtUtc.ToIso8601());
	Root->SetStringField(TEXT("updated_at_utc"), Report.UpdatedAtUtc.ToIso8601());
	Root->SetStringField(TEXT("completed_at_utc"), Report.CompletedAtUtc.ToIso8601());
	Root->SetStringField(TEXT("human_reviewed_at_utc"), Report.HumanReviewedAtUtc.ToIso8601());
	Root->SetStringField(TEXT("actor"), Report.ActorName);
	Root->SetStringField(TEXT("skeleton_profile_id"), Report.SkeletonProfileId.ToString());

	TSharedRef<FJsonObject> Config = MakeShared<FJsonObject>();
	Config->SetStringField(TEXT("profile"), Report.Config.ProfileName);
	Config->SetNumberField(TEXT("target_request_count"), Report.Config.TargetRequestCount);
	Config->SetNumberField(TEXT("minimum_duration_seconds"), Report.Config.MinimumDurationSeconds);
	Config->SetNumberField(TEXT("playback_timeout_seconds"), Report.Config.PlaybackTimeoutSeconds);
	Config->SetNumberField(TEXT("recovery_dwell_seconds"), Report.Config.RecoveryDwellSeconds);
	Config->SetNumberField(TEXT("minimum_unique_template_count"), Report.Config.MinimumUniqueTemplateCount);
	Config->SetBoolField(TEXT("formal_gate"), Report.Config.bFormalGate);
	Root->SetObjectField(TEXT("config"), Config);

	TSharedRef<FJsonObject> Metrics = MakeShared<FJsonObject>();
	Metrics->SetNumberField(TEXT("elapsed_seconds"), Report.ElapsedSeconds);
	Metrics->SetNumberField(TEXT("submitted_request_count"), Report.SubmittedRequestCount);
	Metrics->SetNumberField(TEXT("accepted_request_count"), Report.AcceptedRequestCount);
	Metrics->SetNumberField(TEXT("rejected_request_count"), Report.RejectedRequestCount);
	Metrics->SetNumberField(TEXT("playback_observed_count"), Report.PlaybackObservedCount);
	Metrics->SetNumberField(TEXT("completed_request_count"), Report.CompletedRequestCount);
	Metrics->SetNumberField(TEXT("pose_recovery_failure_count"), Report.PoseRecoveryFailureCount);
	Metrics->SetNumberField(TEXT("actor_loss_count"), Report.ActorLossCount);
	Metrics->SetNumberField(TEXT("post_process_loss_count"), Report.PostProcessLossCount);
	Metrics->SetNumberField(TEXT("max_queue_count"), Report.MaxQueueCount);
	Metrics->SetNumberField(TEXT("max_active_plan_count"), Report.MaxActivePlanCount);
	Metrics->SetNumberField(TEXT("frame_sample_count"), Report.FrameSampleCount);
	Metrics->SetNumberField(TEXT("average_frame_time_ms"), Report.AverageFrameTimeMs);
	Metrics->SetNumberField(TEXT("p95_frame_time_ms"), Report.P95FrameTimeMs);
	Metrics->SetNumberField(TEXT("max_frame_time_ms"), Report.MaxFrameTimeMs);
	Metrics->SetNumberField(TEXT("hitch_50ms_count"), Report.Hitch50MsCount);
	Metrics->SetNumberField(TEXT("hitch_100ms_count"), Report.Hitch100MsCount);
	Metrics->SetNumberField(TEXT("start_used_physical_mb"), Report.StartUsedPhysicalMB);
	Metrics->SetNumberField(TEXT("peak_used_physical_mb"), Report.PeakUsedPhysicalMB);
	Metrics->SetNumberField(TEXT("end_used_physical_mb"), Report.EndUsedPhysicalMB);
	Metrics->SetNumberField(TEXT("used_physical_delta_mb"), Report.UsedPhysicalDeltaMB);
	Metrics->SetNumberField(TEXT("max_action_pose_residual"), Report.MaxActionPoseResidual);
	Root->SetObjectField(TEXT("metrics"), Metrics);

	TArray<TSharedPtr<FJsonValue>> Requests;
	Requests.Reserve(Report.Requests.Num());
	for (const FLLMNPCForwardN8StabilityRequestRecord& Request : Report.Requests)
	{
		TSharedRef<FJsonObject> Event = MakeShared<FJsonObject>();
		Event->SetNumberField(TEXT("sequence"), Request.Sequence);
		Event->SetStringField(TEXT("template_id"), Request.TemplateId.ToString());
		Event->SetNumberField(TEXT("submitted_at_seconds"), Request.SubmittedAtSeconds);
		Event->SetNumberField(TEXT("completed_at_seconds"), Request.CompletedAtSeconds);
		Event->SetNumberField(TEXT("playback_wait_seconds"), Request.PlaybackWaitSeconds);
		Event->SetNumberField(TEXT("amplitude"), Request.Amplitude);
		Event->SetNumberField(TEXT("speed_scale"), Request.SpeedScale);
		Event->SetNumberField(TEXT("duration_scale"), Request.DurationScale);
		Event->SetStringField(TEXT("style"), Request.Style.ToString());
		Event->SetBoolField(TEXT("mirror"), Request.bMirror);
		Event->SetBoolField(TEXT("accepted"), Request.bAccepted);
		Event->SetBoolField(TEXT("playback_observed"), Request.bPlaybackObserved);
		Event->SetBoolField(TEXT("playback_completed"), Request.bPlaybackCompleted);
		Event->SetBoolField(TEXT("pose_recovered"), Request.bPoseRecovered);
		Event->SetStringField(TEXT("error"), Request.Error);
		Requests.Add(MakeShared<FJsonValueObject>(Event));
	}
	Root->SetArrayField(TEXT("requests"), Requests);
	Root->SetArrayField(TEXT("errors"), StringsToJson(Report.Errors));
	Root->SetArrayField(TEXT("warnings"), StringsToJson(Report.Warnings));
	return Root;
}
}
