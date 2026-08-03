#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Evaluation/LLMNPCForwardN8Pressure.h"
#include "Online/LLMNPCOnlineReportSanitizer.h"

namespace
{
constexpr uint32 ForwardN8BEditorTestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;

FLLMNPCForwardN8PressureReport BuildPassingReport(
	const FLLMNPCForwardN8PressureConfig& Config
)
{
	FLLMNPCForwardN8PressureReport Report;
	Report.Status = TEXT("complete");
	Report.Config = Config;
	Report.ElapsedSeconds = Config.MinimumDurationSeconds;
	Report.ManagedActorCount = Config.TargetActorCount;
	Report.SpawnedActorCount = FMath::Max(Config.TargetActorCount - 1, 0);
	Report.DestroyedSpawnedActorCount = Report.SpawnedActorCount;
	Report.CompletedRoundCount = Config.RoundCount;
	Report.StartUsedPhysicalMB = 1000.0;
	Report.PeakUsedPhysicalMB = 1020.0;
	Report.EndUsedPhysicalMB = 1012.0;
	Report.BaselineReportFile = TEXT("stability_baseline_human_passed.json");
	Report.BaselineP95FrameTimeMs = 16.0f;

	for (int32 ActorIndex = 0; ActorIndex < Config.TargetActorCount; ++ActorIndex)
	{
		FLLMNPCForwardN8PressureActorRecord& Actor =
			Report.Actors.AddDefaulted_GetRef();
		Actor.ActorIndex = ActorIndex;
		Actor.ActorName = FString::Printf(TEXT("PressureNPC_%02d"), ActorIndex);
		Actor.bSpawnedByRunner = ActorIndex > 0;
		Actor.PlacementAttemptCount = ActorIndex > 0 ? 1 : 0;
		Actor.bPostProcessRequired = false;
		Actor.bPostProcessInstalled = false;
		Actor.bPostProcessReady = true;
		Actor.SubmittedRequestCount = Config.RoundCount;
		Actor.AcceptedRequestCount = Config.RoundCount;
		Actor.PlaybackObservedCount = Config.RoundCount;
		Actor.CompletedRequestCount = Config.RoundCount;
		if (Config.TargetActorCount >= 30)
		{
			const int32 LODIndex = ActorIndex % 3;
			Actor.DesiredLODLevel = LODIndex == 0
				? TEXT("Full")
				: (LODIndex == 1 ? TEXT("Reduced") : TEXT("Minimal"));
			Actor.ObservedLODLevels.Add(Actor.DesiredLODLevel);
		}
		else
		{
			Actor.DesiredLODLevel = TEXT("Full");
			Actor.ObservedLODLevels.Add(TEXT("Full"));
		}
	}

	const int32 ExpectedRequestCount =
		LLMNPCForwardN8Pressure::GetExpectedRequestCount(Config);
	Report.SubmittedRequestCount = ExpectedRequestCount;
	Report.AcceptedRequestCount = ExpectedRequestCount;
	Report.PlaybackObservedCount = ExpectedRequestCount;
	Report.CompletedRequestCount = ExpectedRequestCount;
	for (int32 Sequence = 0; Sequence < ExpectedRequestCount; ++Sequence)
	{
		FLLMNPCForwardN8PressureRequestRecord& Request =
			Report.Requests.AddDefaulted_GetRef();
		Request.Sequence = Sequence + 1;
		Request.Round = Sequence / Config.TargetActorCount + 1;
		Request.ActorIndex = Sequence % Config.TargetActorCount;
		Request.TemplateId = FName(*FString::Printf(
			TEXT("gesture.pressure.%d"),
			Sequence % Config.MinimumUniqueTemplateCount
		));
		Request.bAccepted = true;
		Request.bPlaybackObserved = true;
		Request.bPlaybackCompleted = true;
		Request.bPoseRecovered = true;
	}
	LLMNPCForwardN8Pressure::ObserveFrame(
		Report,
		1.0f / 60.0f,
		0,
		Config.TargetActorCount,
		1010.0
	);
	LLMNPCForwardN8Pressure::ObserveFrame(
		Report,
		1.0f / 30.0f,
		0,
		Config.TargetActorCount,
		1020.0
	);
	return Report;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN8BProfileContractTest,
	"LLMNPCActionLayer.ForwardN8B.Editor.ProfileContract",
	ForwardN8BEditorTestFlags
)

bool FLLMNPCForwardN8BProfileContractTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	const FLLMNPCForwardN8PressureConfig Smoke =
		LLMNPCForwardN8Pressure::BuildSmokeConfig();
	const FLLMNPCForwardN8PressureConfig Ten =
		LLMNPCForwardN8Pressure::BuildTenNPCConfig();
	const FLLMNPCForwardN8PressureConfig Thirty =
		LLMNPCForwardN8Pressure::BuildThirtyNPCLODConfig();
	FString Error;
	TestTrue(TEXT("The pressure smoke profile is valid"), LLMNPCForwardN8Pressure::ValidateConfig(Smoke, Error));
	TestEqual(TEXT("Smoke owns three NPCs"), Smoke.TargetActorCount, 3);
	TestTrue(TEXT("The 10 NPC profile is valid"), LLMNPCForwardN8Pressure::ValidateConfig(Ten, Error));
	TestEqual(TEXT("The 10 NPC profile submits 50 requests"), LLMNPCForwardN8Pressure::GetExpectedRequestCount(Ten), 50);
	TestTrue(TEXT("The 30 NPC LOD profile is valid"), LLMNPCForwardN8Pressure::ValidateConfig(Thirty, Error));
	TestEqual(TEXT("The 30 NPC profile submits 120 requests"), LLMNPCForwardN8Pressure::GetExpectedRequestCount(Thirty), 120);
	TestEqual(TEXT("The 30 NPC gate requires Full coverage"), Thirty.MinimumFullLODCount, 8);
	TestEqual(TEXT("The 30 NPC gate requires Reduced coverage"), Thirty.MinimumReducedLODCount, 8);
	TestEqual(TEXT("The 30 NPC gate requires Minimal coverage"), Thirty.MinimumMinimalLODCount, 8);

	FLLMNPCForwardN8PressureConfig Undersized = Ten;
	Undersized.MinimumDurationSeconds = 59.0;
	TestFalse(
		TEXT("The formal 10 NPC duration cannot be reduced"),
		LLMNPCForwardN8Pressure::ValidateConfig(Undersized, Error)
	);
	TestEqual(
		TEXT("The undersized budget has a stable error"),
		Error,
		FString(TEXT("LLMNPC_N8_PRESSURE_TEN_NPC_BUDGET_TOO_SMALL"))
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN8BTenNPCVerdictTest,
	"LLMNPCActionLayer.ForwardN8B.Editor.TenNPCVerdict",
	ForwardN8BEditorTestFlags
)

bool FLLMNPCForwardN8BTenNPCVerdictTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	FLLMNPCForwardN8PressureReport Passing = BuildPassingReport(
		LLMNPCForwardN8Pressure::BuildTenNPCConfig()
	);
	LLMNPCForwardN8Pressure::Finalize(Passing);
	TestTrue(TEXT("A complete 10 NPC report passes"), Passing.bMachinePassed);
	TestFalse(
		TEXT("The main-AnimGraph Manny path does not require a component post-process override"),
		Passing.Actors[0].bPostProcessRequired
	);
	TestEqual(TEXT("The pressure outcome is explicit"), Passing.Outcome, FString(TEXT("passed")));
	TestTrue(TEXT("P95 frame time is calculated"), Passing.P95FrameTimeMs > 0.0f);
	TestTrue(TEXT("The single-NPC P95 ratio is calculated"), Passing.P95FrameTimeRatio > 0.0f);

	FLLMNPCForwardN8PressureReport Rejected = BuildPassingReport(
		LLMNPCForwardN8Pressure::BuildTenNPCConfig()
	);
	Rejected.AcceptedRequestCount -= 1;
	Rejected.RejectedRequestCount = 1;
	Rejected.PlaybackObservedCount -= 1;
	Rejected.CompletedRequestCount -= 1;
	LLMNPCForwardN8Pressure::Finalize(Rejected);
	TestFalse(TEXT("A rejected pressure request fails"), Rejected.bMachinePassed);
	TestTrue(
		TEXT("The pressure rejection is attributable"),
		Rejected.Errors.Contains(TEXT("LLMNPC_N8_PRESSURE_SUBMISSION_REJECTED"))
	);

	FLLMNPCForwardN8PressureReport Cleanup = BuildPassingReport(
		LLMNPCForwardN8Pressure::BuildTenNPCConfig()
	);
	Cleanup.DestroyedSpawnedActorCount -= 1;
	LLMNPCForwardN8Pressure::Finalize(Cleanup);
	TestFalse(TEXT("Incomplete spawned-actor cleanup fails"), Cleanup.bMachinePassed);
	TestTrue(
		TEXT("Cleanup failure is attributable"),
		Cleanup.Errors.Contains(TEXT("LLMNPC_N8_PRESSURE_CLEANUP_INCOMPLETE"))
	);

	FLLMNPCForwardN8PressureReport PostProcessLost = BuildPassingReport(
		LLMNPCForwardN8Pressure::BuildTenNPCConfig()
	);
	PostProcessLost.Actors[0].bPostProcessRequired = true;
	PostProcessLost.Actors[0].bPostProcessInstalled = false;
	PostProcessLost.Actors[0].bPostProcessReady = false;
	LLMNPCForwardN8Pressure::Finalize(PostProcessLost);
	TestFalse(TEXT("A required post-process loss fails"), PostProcessLost.bMachinePassed);
	TestTrue(
		TEXT("The post-process loss is attributable"),
		PostProcessLost.Errors.Contains(TEXT("LLMNPC_N8_PRESSURE_POST_PROCESS_UNAVAILABLE"))
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN8BThirtyNPCLODVerdictTest,
	"LLMNPCActionLayer.ForwardN8B.Editor.ThirtyNPCLODVerdict",
	ForwardN8BEditorTestFlags
)

bool FLLMNPCForwardN8BThirtyNPCLODVerdictTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	FLLMNPCForwardN8PressureReport Passing = BuildPassingReport(
		LLMNPCForwardN8Pressure::BuildThirtyNPCLODConfig()
	);
	LLMNPCForwardN8Pressure::Finalize(Passing);
	TestTrue(TEXT("A complete 30 NPC LOD report passes"), Passing.bMachinePassed);

	FLLMNPCForwardN8PressureReport MissingMinimal = BuildPassingReport(
		LLMNPCForwardN8Pressure::BuildThirtyNPCLODConfig()
	);
	for (FLLMNPCForwardN8PressureActorRecord& Actor : MissingMinimal.Actors)
	{
		Actor.ObservedLODLevels.Remove(TEXT("Minimal"));
	}
	LLMNPCForwardN8Pressure::Finalize(MissingMinimal);
	TestFalse(TEXT("Missing Minimal LOD coverage fails"), MissingMinimal.bMachinePassed);
	TestTrue(
		TEXT("LOD coverage failure is attributable"),
		MissingMinimal.Errors.Contains(TEXT("LLMNPC_N8_PRESSURE_LOD_COVERAGE_INCOMPLETE"))
	);

	FLLMNPCForwardN8PressureReport TransientlyQueued = BuildPassingReport(
		LLMNPCForwardN8Pressure::BuildThirtyNPCLODConfig()
	);
	TransientlyQueued.MaxPerActorQueueCount = 1;
	TransientlyQueued.MaxAggregateQueueCount = 6;
	TransientlyQueued.QueueNonZeroFrameCount = 8;
	TransientlyQueued.Actors[3].MaxQueueCount = 1;
	LLMNPCForwardN8Pressure::Finalize(TransientlyQueued);
	TestTrue(
		TEXT("A single pending dispatch per Minimal LOD actor is allowed when every barrier drains"),
		TransientlyQueued.bMachinePassed
	);

	FLLMNPCForwardN8PressureReport GrowingQueue = BuildPassingReport(
		LLMNPCForwardN8Pressure::BuildThirtyNPCLODConfig()
	);
	GrowingQueue.MaxPerActorQueueCount = 2;
	GrowingQueue.MaxAggregateQueueCount = 2;
	GrowingQueue.Actors[3].MaxQueueCount = 2;
	LLMNPCForwardN8Pressure::Finalize(GrowingQueue);
	TestFalse(TEXT("A per-actor queue depth above one fails"), GrowingQueue.bMachinePassed);
	TestTrue(
		TEXT("Queue growth remains attributable"),
		GrowingQueue.Errors.Contains(TEXT("LLMNPC_N8_PRESSURE_QUEUE_GROWTH_DETECTED"))
	);

	FLLMNPCForwardN8PressureReport UndrainedQueue = BuildPassingReport(
		LLMNPCForwardN8Pressure::BuildThirtyNPCLODConfig()
	);
	UndrainedQueue.FinalAggregateQueueCount = 1;
	LLMNPCForwardN8Pressure::Finalize(UndrainedQueue);
	TestFalse(TEXT("An undrained final queue fails"), UndrainedQueue.bMachinePassed);
	TestTrue(
		TEXT("An undrained queue has a stable error"),
		UndrainedQueue.Errors.Contains(TEXT("LLMNPC_N8_PRESSURE_QUEUE_NOT_DRAINED"))
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN8BReportTest,
	"LLMNPCActionLayer.ForwardN8B.Editor.Report",
	ForwardN8BEditorTestFlags
)

bool FLLMNPCForwardN8BReportTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	FLLMNPCForwardN8PressureReport Report = BuildPassingReport(
		LLMNPCForwardN8Pressure::BuildTenNPCConfig()
	);
	Report.Actors[0].ActorName = TEXT("Bearer secret-value-that-must-not-survive");
	LLMNPCForwardN8Pressure::Finalize(Report);
	LLMNPCForwardN8Pressure::ApplyHumanReview(
		Report,
		TEXT("passed"),
		TEXT("editor_user")
	);
	FString Json;
	TestTrue(
		TEXT("The pressure report is sanitized and serialized"),
		FLLMNPCOnlineReportSanitizer::SanitizeAndSerialize(
			LLMNPCForwardN8Pressure::BuildJson(Report),
			Json
		)
	);
	TestFalse(TEXT("The credential-like canary is removed"), Json.Contains(TEXT("secret-value")));
	TestTrue(TEXT("The pressure schema is retained"), Json.Contains(TEXT("llmnpc.forward_n8_pressure.v1")));
	TestTrue(TEXT("The baseline source is retained"), Json.Contains(TEXT("stability_baseline_human_passed.json")));
	TestTrue(TEXT("The review source is retained"), Json.Contains(TEXT("editor_user")));
	TestTrue(TEXT("The optional post-process contract is explicit"), Json.Contains(TEXT("post_process_required")));
	TestTrue(TEXT("The final queue drain is explicit"), Json.Contains(TEXT("final_aggregate_queue_count")));
	return true;
}

#endif
