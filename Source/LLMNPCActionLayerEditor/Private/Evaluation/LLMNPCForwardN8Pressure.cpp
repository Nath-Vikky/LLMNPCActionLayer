#include "Evaluation/LLMNPCForwardN8Pressure.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
void AddPressureUnique(TArray<FString>& Values, const FString& Value)
{
	if (!Value.IsEmpty())
	{
		Values.AddUnique(Value);
	}
}

TArray<TSharedPtr<FJsonValue>> PressureStringsToJson(const TArray<FString>& Values)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	Result.Reserve(Values.Num());
	for (const FString& Value : Values)
	{
		Result.Add(MakeShared<FJsonValueString>(Value));
	}
	return Result;
}

int32 CountActorsObservingLOD(
	const TArray<FLLMNPCForwardN8PressureActorRecord>& Actors,
	const FString& LODLevel
)
{
	int32 Count = 0;
	for (const FLLMNPCForwardN8PressureActorRecord& Actor : Actors)
	{
		if (Actor.ObservedLODLevels.Contains(LODLevel))
		{
			++Count;
		}
	}
	return Count;
}
}

namespace LLMNPCForwardN8Pressure
{
const TCHAR* GetSchemaVersion()
{
	return TEXT("llmnpc.forward_n8_pressure.v1");
}

FLLMNPCForwardN8PressureConfig BuildSmokeConfig()
{
	FLLMNPCForwardN8PressureConfig Config;
	Config.ProfileName = TEXT("smoke_3_npc");
	Config.TargetActorCount = 3;
	Config.RoundCount = 2;
	Config.MinimumDurationSeconds = 10.0;
	Config.WarmupSeconds = 1.0f;
	Config.MinimumUniqueTemplateCount = 2;
	Config.MinimumFullLODCount = 2;
	return Config;
}

FLLMNPCForwardN8PressureConfig BuildTenNPCConfig()
{
	FLLMNPCForwardN8PressureConfig Config;
	Config.ProfileName = TEXT("local_10_npc");
	Config.TargetActorCount = 10;
	Config.RoundCount = 5;
	Config.MinimumDurationSeconds = 60.0;
	Config.WarmupSeconds = 2.0f;
	Config.MinimumUniqueTemplateCount = 4;
	Config.MinimumFullLODCount = 8;
	Config.bFormalGate = true;
	return Config;
}

FLLMNPCForwardN8PressureConfig BuildThirtyNPCLODConfig()
{
	FLLMNPCForwardN8PressureConfig Config;
	Config.ProfileName = TEXT("lod_30_npc");
	Config.TargetActorCount = 30;
	Config.RoundCount = 4;
	Config.MinimumDurationSeconds = 90.0;
	Config.WarmupSeconds = 2.0f;
	Config.MinimumUniqueTemplateCount = 6;
	Config.MinimumFullLODCount = 8;
	Config.MinimumReducedLODCount = 8;
	Config.MinimumMinimalLODCount = 8;
	Config.bFormalGate = true;
	return Config;
}

int32 GetExpectedRequestCount(const FLLMNPCForwardN8PressureConfig& Config)
{
	return Config.TargetActorCount * Config.RoundCount;
}

bool ValidateConfig(
	const FLLMNPCForwardN8PressureConfig& Config,
	FString& OutError
)
{
	OutError.Reset();
	if (Config.ProfileName.TrimStartAndEnd().IsEmpty())
	{
		OutError = TEXT("LLMNPC_N8_PRESSURE_PROFILE_EMPTY");
	}
	else if (Config.TargetActorCount <= 0 || Config.TargetActorCount > 30)
	{
		OutError = TEXT("LLMNPC_N8_PRESSURE_ACTOR_COUNT_INVALID");
	}
	else if (Config.RoundCount <= 0)
	{
		OutError = TEXT("LLMNPC_N8_PRESSURE_ROUND_COUNT_INVALID");
	}
	else if (!FMath::IsFinite(Config.MinimumDurationSeconds) || Config.MinimumDurationSeconds <= 0.0)
	{
		OutError = TEXT("LLMNPC_N8_PRESSURE_DURATION_INVALID");
	}
	else if (!FMath::IsFinite(Config.WarmupSeconds) || Config.WarmupSeconds < 0.5f)
	{
		OutError = TEXT("LLMNPC_N8_PRESSURE_WARMUP_INVALID");
	}
	else if (!FMath::IsFinite(Config.PlaybackTimeoutSeconds) || Config.PlaybackTimeoutSeconds <= 0.0f)
	{
		OutError = TEXT("LLMNPC_N8_PRESSURE_PLAYBACK_TIMEOUT_INVALID");
	}
	else if (!FMath::IsFinite(Config.RecoveryDwellSeconds) || Config.RecoveryDwellSeconds < 0.1f)
	{
		OutError = TEXT("LLMNPC_N8_PRESSURE_RECOVERY_DWELL_INVALID");
	}
	else if (
		Config.MinimumUniqueTemplateCount <= 0 ||
		Config.MinimumUniqueTemplateCount > GetExpectedRequestCount(Config)
	)
	{
		OutError = TEXT("LLMNPC_N8_PRESSURE_TEMPLATE_COVERAGE_INVALID");
	}
	else if (
		Config.MinimumFullLODCount < 0 ||
		Config.MinimumReducedLODCount < 0 ||
		Config.MinimumMinimalLODCount < 0 ||
		Config.MinimumFullLODCount > Config.TargetActorCount ||
		Config.MinimumReducedLODCount > Config.TargetActorCount ||
		Config.MinimumMinimalLODCount > Config.TargetActorCount
	)
	{
		OutError = TEXT("LLMNPC_N8_PRESSURE_LOD_COVERAGE_INVALID");
	}
	else if (
		Config.bFormalGate &&
		Config.TargetActorCount < 10
	)
	{
		OutError = TEXT("LLMNPC_N8_PRESSURE_FORMAL_ACTOR_BUDGET_TOO_SMALL");
	}
	else if (
		Config.bFormalGate &&
		Config.TargetActorCount < 30 &&
		(Config.RoundCount < 5 || Config.MinimumDurationSeconds < 60.0)
	)
	{
		OutError = TEXT("LLMNPC_N8_PRESSURE_TEN_NPC_BUDGET_TOO_SMALL");
	}
	else if (
		Config.bFormalGate &&
		Config.TargetActorCount >= 30 &&
		(
			Config.RoundCount < 4 ||
			Config.MinimumDurationSeconds < 90.0 ||
			Config.MinimumFullLODCount < 8 ||
			Config.MinimumReducedLODCount < 8 ||
			Config.MinimumMinimalLODCount < 8
		)
	)
	{
		OutError = TEXT("LLMNPC_N8_PRESSURE_THIRTY_NPC_BUDGET_TOO_SMALL");
	}
	return OutError.IsEmpty();
}

void ObserveFrame(
	FLLMNPCForwardN8PressureReport& Report,
	float DeltaTimeSeconds,
	int32 AggregateQueueCount,
	int32 AggregateActivePlanCount,
	double UsedPhysicalMB
)
{
	if (!FMath::IsFinite(DeltaTimeSeconds) || DeltaTimeSeconds < 0.0f)
	{
		AddPressureUnique(Report.Errors, TEXT("LLMNPC_N8_PRESSURE_FRAME_DELTA_INVALID"));
		return;
	}

	const float FrameTimeMs = DeltaTimeSeconds * 1000.0f;
	++Report.FrameSampleCount;
	Report.FrameTimeSumMs += FrameTimeMs;
	Report.FrameTimeSamplesMs.Add(FrameTimeMs);
	Report.MaxFrameTimeMs = FMath::Max(Report.MaxFrameTimeMs, FrameTimeMs);
	Report.Hitch50MsCount += FrameTimeMs >= 50.0f ? 1 : 0;
	Report.Hitch100MsCount += FrameTimeMs >= 100.0f ? 1 : 0;
	Report.MaxAggregateQueueCount = FMath::Max(
		Report.MaxAggregateQueueCount,
		AggregateQueueCount
	);
	Report.QueueNonZeroFrameCount += AggregateQueueCount > 0 ? 1 : 0;
	Report.MaxAggregateActivePlanCount = FMath::Max(
		Report.MaxAggregateActivePlanCount,
		AggregateActivePlanCount
	);
	Report.PeakUsedPhysicalMB = FMath::Max(
		Report.PeakUsedPhysicalMB,
		UsedPhysicalMB
	);
}

void Finalize(FLLMNPCForwardN8PressureReport& Report)
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
	if (Report.BaselineP95FrameTimeMs > 0.0f)
	{
		Report.P95FrameTimeDeltaMs =
			Report.P95FrameTimeMs - Report.BaselineP95FrameTimeMs;
		Report.P95FrameTimeRatio =
			Report.P95FrameTimeMs / Report.BaselineP95FrameTimeMs;
	}

	TSet<FName> UniqueTemplates;
	for (const FLLMNPCForwardN8PressureRequestRecord& Request : Report.Requests)
	{
		if (Request.bAccepted)
		{
			UniqueTemplates.Add(Request.TemplateId);
		}
	}
	const int32 FullLODCount = CountActorsObservingLOD(Report.Actors, TEXT("Full"));
	const int32 ReducedLODCount = CountActorsObservingLOD(Report.Actors, TEXT("Reduced"));
	const int32 MinimalLODCount = CountActorsObservingLOD(Report.Actors, TEXT("Minimal"));
	const int32 ExpectedRequestCount = GetExpectedRequestCount(Report.Config);
	bool bRequiredPostProcessUnavailable = false;
	for (const FLLMNPCForwardN8PressureActorRecord& Actor : Report.Actors)
	{
		if (
			Actor.bPostProcessRequired &&
			(!Actor.bPostProcessInstalled || !Actor.bPostProcessReady)
		)
		{
			bRequiredPostProcessUnavailable = true;
			break;
		}
	}

	if (Report.Status != TEXT("complete"))
	{
		AddPressureUnique(Report.Errors, TEXT("LLMNPC_N8_PRESSURE_NOT_COMPLETE"));
	}
	if (Report.ElapsedSeconds + KINDA_SMALL_NUMBER < Report.Config.MinimumDurationSeconds)
	{
		AddPressureUnique(Report.Errors, TEXT("LLMNPC_N8_PRESSURE_DURATION_INCOMPLETE"));
	}
	if (Report.ManagedActorCount != Report.Config.TargetActorCount)
	{
		AddPressureUnique(Report.Errors, TEXT("LLMNPC_N8_PRESSURE_ACTOR_COUNT_MISMATCH"));
	}
	if (Report.SpawnedActorCount != FMath::Max(Report.Config.TargetActorCount - 1, 0))
	{
		AddPressureUnique(Report.Errors, TEXT("LLMNPC_N8_PRESSURE_SPAWN_COUNT_MISMATCH"));
	}
	if (Report.DestroyedSpawnedActorCount != Report.SpawnedActorCount)
	{
		AddPressureUnique(Report.Errors, TEXT("LLMNPC_N8_PRESSURE_CLEANUP_INCOMPLETE"));
	}
	if (Report.CompletedRoundCount != Report.Config.RoundCount)
	{
		AddPressureUnique(Report.Errors, TEXT("LLMNPC_N8_PRESSURE_ROUND_COUNT_MISMATCH"));
	}
	if (
		Report.SubmittedRequestCount != ExpectedRequestCount ||
		Report.Requests.Num() != ExpectedRequestCount
	)
	{
		AddPressureUnique(Report.Errors, TEXT("LLMNPC_N8_PRESSURE_REQUEST_COUNT_MISMATCH"));
	}
	if (
		Report.AcceptedRequestCount != Report.SubmittedRequestCount ||
		Report.RejectedRequestCount != 0
	)
	{
		AddPressureUnique(Report.Errors, TEXT("LLMNPC_N8_PRESSURE_SUBMISSION_REJECTED"));
	}
	if (Report.PlaybackObservedCount != Report.AcceptedRequestCount)
	{
		AddPressureUnique(Report.Errors, TEXT("LLMNPC_N8_PRESSURE_PLAYBACK_NOT_OBSERVED"));
	}
	if (Report.CompletedRequestCount != Report.AcceptedRequestCount)
	{
		AddPressureUnique(Report.Errors, TEXT("LLMNPC_N8_PRESSURE_PLAYBACK_INCOMPLETE"));
	}
	if (Report.PoseRecoveryFailureCount != 0)
	{
		AddPressureUnique(Report.Errors, TEXT("LLMNPC_N8_PRESSURE_POSE_NOT_RECOVERED"));
	}
	if (Report.ActorLossCount != 0)
	{
		AddPressureUnique(Report.Errors, TEXT("LLMNPC_N8_PRESSURE_ACTOR_LOST"));
	}
	if (Report.PostProcessFailureCount != 0 || bRequiredPostProcessUnavailable)
	{
		AddPressureUnique(Report.Errors, TEXT("LLMNPC_N8_PRESSURE_POST_PROCESS_UNAVAILABLE"));
	}
	if (UniqueTemplates.Num() < Report.Config.MinimumUniqueTemplateCount)
	{
		AddPressureUnique(Report.Errors, TEXT("LLMNPC_N8_PRESSURE_TEMPLATE_COVERAGE_INCOMPLETE"));
	}
	if (
		FullLODCount < Report.Config.MinimumFullLODCount ||
		ReducedLODCount < Report.Config.MinimumReducedLODCount ||
		MinimalLODCount < Report.Config.MinimumMinimalLODCount
	)
	{
		AddPressureUnique(Report.Errors, TEXT("LLMNPC_N8_PRESSURE_LOD_COVERAGE_INCOMPLETE"));
	}
	if (Report.MaxPerActorQueueCount > 1)
	{
		AddPressureUnique(Report.Errors, TEXT("LLMNPC_N8_PRESSURE_QUEUE_GROWTH_DETECTED"));
	}
	if (
		Report.MaxRoundBoundaryAggregateQueueCount != 0 ||
		Report.FinalAggregateQueueCount != 0
	)
	{
		AddPressureUnique(Report.Errors, TEXT("LLMNPC_N8_PRESSURE_QUEUE_NOT_DRAINED"));
	}
	if (Report.FinalAggregateActivePlanCount != 0)
	{
		AddPressureUnique(Report.Errors, TEXT("LLMNPC_N8_PRESSURE_PLAYBACK_INCOMPLETE"));
	}
	if (Report.FrameSampleCount <= 0)
	{
		AddPressureUnique(Report.Errors, TEXT("LLMNPC_N8_PRESSURE_FRAME_SAMPLES_EMPTY"));
	}
	if (Report.UsedPhysicalDeltaMB > 512.0)
	{
		AddPressureUnique(
			Report.Warnings,
			TEXT("Process physical memory grew by more than 512 MB; inspect Unreal Insights before release.")
		);
	}
	if (Report.BaselineP95FrameTimeMs <= 0.0f)
	{
		AddPressureUnique(
			Report.Warnings,
			TEXT("No accepted single-NPC P95 baseline was available for comparison.")
		);
	}

	Report.bMachinePassed = Report.Errors.IsEmpty();
	Report.Outcome = Report.bMachinePassed ? TEXT("passed") : TEXT("failed");
	Report.FrameTimeSamplesMs.Reset();
}

void ApplyHumanReview(
	FLLMNPCForwardN8PressureReport& Report,
	const FString& Review,
	const FString& Source
)
{
	Report.HumanReview = Review;
	Report.HumanReviewSource = Source;
	Report.HumanReviewedAtUtc = FDateTime::UtcNow();
	Report.UpdatedAtUtc = Report.HumanReviewedAtUtc;
}

TSharedRef<FJsonObject> BuildJson(const FLLMNPCForwardN8PressureReport& Report)
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
	Root->SetStringField(TEXT("skeleton_profile_id"), Report.SkeletonProfileId.ToString());

	TSharedRef<FJsonObject> Config = MakeShared<FJsonObject>();
	Config->SetStringField(TEXT("profile"), Report.Config.ProfileName);
	Config->SetNumberField(TEXT("target_actor_count"), Report.Config.TargetActorCount);
	Config->SetNumberField(TEXT("round_count"), Report.Config.RoundCount);
	Config->SetNumberField(TEXT("minimum_duration_seconds"), Report.Config.MinimumDurationSeconds);
	Config->SetNumberField(TEXT("warmup_seconds"), Report.Config.WarmupSeconds);
	Config->SetNumberField(TEXT("playback_timeout_seconds"), Report.Config.PlaybackTimeoutSeconds);
	Config->SetNumberField(TEXT("recovery_dwell_seconds"), Report.Config.RecoveryDwellSeconds);
	Config->SetNumberField(TEXT("minimum_unique_template_count"), Report.Config.MinimumUniqueTemplateCount);
	Config->SetNumberField(TEXT("minimum_full_lod_count"), Report.Config.MinimumFullLODCount);
	Config->SetNumberField(TEXT("minimum_reduced_lod_count"), Report.Config.MinimumReducedLODCount);
	Config->SetNumberField(TEXT("minimum_minimal_lod_count"), Report.Config.MinimumMinimalLODCount);
	Config->SetBoolField(TEXT("formal_gate"), Report.Config.bFormalGate);
	Root->SetObjectField(TEXT("config"), Config);

	int32 PostProcessRequiredActorCount = 0;
	int32 PostProcessInstalledActorCount = 0;
	int32 PoseDriverReadyActorCount = 0;
	int32 MaxPlacementAttemptCount = 0;
	int32 TransientlyQueuedActorCount = 0;
	for (const FLLMNPCForwardN8PressureActorRecord& Actor : Report.Actors)
	{
		PostProcessRequiredActorCount += Actor.bPostProcessRequired ? 1 : 0;
		PostProcessInstalledActorCount += Actor.bPostProcessInstalled ? 1 : 0;
		PoseDriverReadyActorCount += Actor.bPostProcessReady ? 1 : 0;
		MaxPlacementAttemptCount = FMath::Max(
			MaxPlacementAttemptCount,
			Actor.PlacementAttemptCount
		);
		TransientlyQueuedActorCount += Actor.MaxQueueCount > 0 ? 1 : 0;
	}

	TSharedRef<FJsonObject> Metrics = MakeShared<FJsonObject>();
	Metrics->SetNumberField(TEXT("elapsed_seconds"), Report.ElapsedSeconds);
	Metrics->SetNumberField(TEXT("managed_actor_count"), Report.ManagedActorCount);
	Metrics->SetNumberField(TEXT("spawned_actor_count"), Report.SpawnedActorCount);
	Metrics->SetNumberField(TEXT("destroyed_spawned_actor_count"), Report.DestroyedSpawnedActorCount);
	Metrics->SetNumberField(TEXT("completed_round_count"), Report.CompletedRoundCount);
	Metrics->SetNumberField(TEXT("submitted_request_count"), Report.SubmittedRequestCount);
	Metrics->SetNumberField(TEXT("accepted_request_count"), Report.AcceptedRequestCount);
	Metrics->SetNumberField(TEXT("rejected_request_count"), Report.RejectedRequestCount);
	Metrics->SetNumberField(TEXT("playback_observed_count"), Report.PlaybackObservedCount);
	Metrics->SetNumberField(TEXT("completed_request_count"), Report.CompletedRequestCount);
	Metrics->SetNumberField(TEXT("pose_recovery_failure_count"), Report.PoseRecoveryFailureCount);
	Metrics->SetNumberField(TEXT("actor_loss_count"), Report.ActorLossCount);
	Metrics->SetNumberField(TEXT("post_process_failure_count"), Report.PostProcessFailureCount);
	Metrics->SetNumberField(TEXT("post_process_required_actor_count"), PostProcessRequiredActorCount);
	Metrics->SetNumberField(TEXT("post_process_installed_actor_count"), PostProcessInstalledActorCount);
	Metrics->SetNumberField(TEXT("pose_driver_ready_actor_count"), PoseDriverReadyActorCount);
	Metrics->SetNumberField(TEXT("max_placement_attempt_count"), MaxPlacementAttemptCount);
	Metrics->SetNumberField(TEXT("max_per_actor_queue_count"), Report.MaxPerActorQueueCount);
	Metrics->SetNumberField(TEXT("max_aggregate_queue_count"), Report.MaxAggregateQueueCount);
	Metrics->SetNumberField(TEXT("queue_non_zero_frame_count"), Report.QueueNonZeroFrameCount);
	Metrics->SetNumberField(TEXT("transiently_queued_actor_count"), TransientlyQueuedActorCount);
	Metrics->SetNumberField(
		TEXT("max_round_boundary_aggregate_queue_count"),
		Report.MaxRoundBoundaryAggregateQueueCount
	);
	Metrics->SetNumberField(TEXT("final_aggregate_queue_count"), Report.FinalAggregateQueueCount);
	Metrics->SetNumberField(TEXT("max_aggregate_active_plan_count"), Report.MaxAggregateActivePlanCount);
	Metrics->SetNumberField(
		TEXT("final_aggregate_active_plan_count"),
		Report.FinalAggregateActivePlanCount
	);
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
	Metrics->SetStringField(TEXT("baseline_report_file"), Report.BaselineReportFile);
	Metrics->SetNumberField(TEXT("baseline_p95_frame_time_ms"), Report.BaselineP95FrameTimeMs);
	Metrics->SetNumberField(TEXT("p95_frame_time_delta_ms"), Report.P95FrameTimeDeltaMs);
	Metrics->SetNumberField(TEXT("p95_frame_time_ratio"), Report.P95FrameTimeRatio);
	Root->SetObjectField(TEXT("metrics"), Metrics);

	TArray<TSharedPtr<FJsonValue>> Actors;
	Actors.Reserve(Report.Actors.Num());
	for (const FLLMNPCForwardN8PressureActorRecord& Actor : Report.Actors)
	{
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("actor_index"), Actor.ActorIndex);
		Entry->SetStringField(TEXT("actor_name"), Actor.ActorName);
		Entry->SetBoolField(TEXT("spawned_by_runner"), Actor.bSpawnedByRunner);
		Entry->SetNumberField(TEXT("viewer_distance_cm"), Actor.ViewerDistanceCm);
		Entry->SetNumberField(TEXT("placement_attempt_count"), Actor.PlacementAttemptCount);
		Entry->SetNumberField(TEXT("facing_yaw_degrees"), Actor.FacingYawDegrees);
		Entry->SetStringField(TEXT("desired_lod_level"), Actor.DesiredLODLevel);
		Entry->SetArrayField(TEXT("observed_lod_levels"), PressureStringsToJson(Actor.ObservedLODLevels));
		Entry->SetBoolField(TEXT("post_process_required"), Actor.bPostProcessRequired);
		Entry->SetBoolField(TEXT("post_process_installed"), Actor.bPostProcessInstalled);
		Entry->SetBoolField(TEXT("post_process_ready"), Actor.bPostProcessReady);
		Entry->SetStringField(TEXT("post_process_error"), Actor.PostProcessError);
		Entry->SetNumberField(TEXT("submitted_request_count"), Actor.SubmittedRequestCount);
		Entry->SetNumberField(TEXT("accepted_request_count"), Actor.AcceptedRequestCount);
		Entry->SetNumberField(TEXT("playback_observed_count"), Actor.PlaybackObservedCount);
		Entry->SetNumberField(TEXT("completed_request_count"), Actor.CompletedRequestCount);
		Entry->SetNumberField(TEXT("pose_recovery_failure_count"), Actor.PoseRecoveryFailureCount);
		Entry->SetNumberField(TEXT("max_queue_count"), Actor.MaxQueueCount);
		Entry->SetNumberField(TEXT("max_active_plan_count"), Actor.MaxActivePlanCount);
		Entry->SetNumberField(TEXT("max_action_pose_residual"), Actor.MaxActionPoseResidual);
		Actors.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Root->SetArrayField(TEXT("actors"), Actors);

	TArray<TSharedPtr<FJsonValue>> Requests;
	Requests.Reserve(Report.Requests.Num());
	for (const FLLMNPCForwardN8PressureRequestRecord& Request : Report.Requests)
	{
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("sequence"), Request.Sequence);
		Entry->SetNumberField(TEXT("round"), Request.Round);
		Entry->SetNumberField(TEXT("actor_index"), Request.ActorIndex);
		Entry->SetStringField(TEXT("template_id"), Request.TemplateId.ToString());
		Entry->SetNumberField(TEXT("submitted_at_seconds"), Request.SubmittedAtSeconds);
		Entry->SetNumberField(TEXT("completed_at_seconds"), Request.CompletedAtSeconds);
		Entry->SetNumberField(TEXT("playback_wait_seconds"), Request.PlaybackWaitSeconds);
		Entry->SetNumberField(TEXT("amplitude"), Request.Amplitude);
		Entry->SetNumberField(TEXT("speed_scale"), Request.SpeedScale);
		Entry->SetNumberField(TEXT("duration_scale"), Request.DurationScale);
		Entry->SetStringField(TEXT("style"), Request.Style.ToString());
		Entry->SetBoolField(TEXT("mirror"), Request.bMirror);
		Entry->SetBoolField(TEXT("accepted"), Request.bAccepted);
		Entry->SetBoolField(TEXT("playback_observed"), Request.bPlaybackObserved);
		Entry->SetBoolField(TEXT("playback_completed"), Request.bPlaybackCompleted);
		Entry->SetBoolField(TEXT("pose_recovered"), Request.bPoseRecovered);
		Entry->SetStringField(TEXT("error"), Request.Error);
		Requests.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Root->SetArrayField(TEXT("requests"), Requests);
	Root->SetArrayField(TEXT("errors"), PressureStringsToJson(Report.Errors));
	Root->SetArrayField(TEXT("warnings"), PressureStringsToJson(Report.Warnings));
	return Root;
}
}
