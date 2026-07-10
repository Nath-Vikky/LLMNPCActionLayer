#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "LLMNPCMotionComponent.h"
#include "LLMNPCMotionValidator.h"
#include "Templates/LLMNPCMotionTemplate.h"

namespace
{
constexpr uint32 Phase1SchedulerTestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;

FLLMMotionPlan MakeSchedulerPlan(
	const TCHAR* ClipId,
	FName ControlId,
	float Priority,
	bool bInterruptible
)
{
	FLLMMotionPlan Plan;
	Plan.Intent = TEXT("phase1.scheduler");
	Plan.Clip.ClipId = ClipId;
	Plan.Clip.Duration = 2.0f;
	Plan.Clip.BlendIn = 0.0f;
	Plan.Clip.BlendOut = 0.0f;
	Plan.Clip.Priority = Priority;
	Plan.Clip.bInterruptible = bInterruptible;

	FLLMMotionTrack& Track = Plan.Clip.Tracks.AddDefaulted_GetRef();
	Track.ControlId = ControlId;
	Track.TrackType = ELLMMotionTrackType::Hold;
	Track.ValueType = ELLMMotionValueType::Float;
	Track.StartTime = 0.0f;
	Track.EndTime = Plan.Clip.Duration;
	Track.Amplitude = 10.0f;
	Track.Envelope = ELLMMotionEnvelope::None;
	return Plan;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCMotionSchedulerTest,
	"LLMNPCActionLayer.Phase1.Scheduler.ChannelPriorityAndInterruption",
	Phase1SchedulerTestFlags
)

bool FLLMNPCMotionSchedulerTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	const auto SubmitInternal = [](ULLMNPCMotionComponent& Component, FLLMMotionPlan Plan)
	{
		return Component.SubmitMotionPlanWithSource(
			MoveTemp(Plan),
			ELLMNPCMotionValidationSource::InternalDebug
		);
	};

	ULLMNPCMotionComponent* Concurrent = NewObject<ULLMNPCMotionComponent>();
	TestTrue(
		TEXT("Head motion enters the scheduler"),
		SubmitInternal(*Concurrent, MakeSchedulerPlan(TEXT("head"), TEXT("head.pitch"), 0.4f, true))
	);
	TestTrue(
		TEXT("Arm motion enters the scheduler"),
		SubmitInternal(*Concurrent, MakeSchedulerPlan(TEXT("arm"), TEXT("right_upperarm.pitch"), 0.4f, true))
	);
	Concurrent->StartEligiblePlans();
	Concurrent->UpdateActivePlans(0.1f);
	TestEqual(TEXT("Independent head and arm channels run concurrently"), Concurrent->ActiveMotions.Num(), 2);
	TestTrue(TEXT("Concurrent head channel contributes to the merged pose"), Concurrent->CurrentSnapshot.HeadPitch > 0.0f);
	TestTrue(
		TEXT("Concurrent arm channel contributes to the merged pose"),
		Concurrent->CurrentSnapshot.RightUpperArmAdditiveRotation.Pitch > 0.0f
	);

	ULLMNPCMotionComponent* Interruptible = NewObject<ULLMNPCMotionComponent>();
	TestTrue(
		TEXT("Low-priority motion is accepted"),
		SubmitInternal(*Interruptible, MakeSchedulerPlan(TEXT("low"), TEXT("head.pitch"), 0.2f, true))
	);
	Interruptible->StartEligiblePlans();
	TestTrue(
		TEXT("High-priority motion is accepted"),
		SubmitInternal(*Interruptible, MakeSchedulerPlan(TEXT("high"), TEXT("head.yaw"), 0.8f, true))
	);
	Interruptible->StartEligiblePlans();
	TestEqual(TEXT("A higher-priority conflict replaces the active motion"), Interruptible->ActiveMotions.Num(), 1);
	if (!Interruptible->ActiveMotions.IsEmpty())
	{
		TestEqual(
			TEXT("The high-priority clip becomes active"),
			Interruptible->ActiveMotions[0].Request.Plan.Clip.ClipId,
			FString(TEXT("high"))
		);
	}
	TestEqual(TEXT("The successful interruption leaves no queued request"), Interruptible->Queue.Num(), 0);

	ULLMNPCMotionComponent* Protected = NewObject<ULLMNPCMotionComponent>();
	TestTrue(
		TEXT("Protected motion is accepted"),
		SubmitInternal(*Protected, MakeSchedulerPlan(TEXT("protected"), TEXT("head.pitch"), 0.2f, false))
	);
	Protected->StartEligiblePlans();
	TestTrue(
		TEXT("Conflicting request is accepted into the queue"),
		SubmitInternal(*Protected, MakeSchedulerPlan(TEXT("waiting"), TEXT("head.roll"), 1.0f, true))
	);
	Protected->StartEligiblePlans();
	TestEqual(TEXT("A non-interruptible clip remains active"), Protected->ActiveMotions.Num(), 1);
	TestEqual(TEXT("The conflicting clip waits in the queue"), Protected->Queue.Num(), 1);
	if (!Protected->ActiveMotions.IsEmpty())
	{
		TestEqual(
			TEXT("The protected clip was not replaced"),
			Protected->ActiveMotions[0].Request.Plan.Clip.ClipId,
			FString(TEXT("protected"))
		);
	}

	if (!Protected->Queue.IsEmpty())
	{
		Protected->Queue[0].QueuedAtSeconds -= Protected->MaxQueueWaitSeconds + 1.0;
	}
	Protected->StartEligiblePlans();
	TestEqual(TEXT("An expired blocked request is removed"), Protected->Queue.Num(), 0);

	ULLMNPCMotionComponent* Cooldown = NewObject<ULLMNPCMotionComponent>();
	ULLMNPCMotionTemplate* CooldownTemplate = NewObject<ULLMNPCMotionTemplate>();
	CooldownTemplate->Metadata.TemplateId = TEXT("phase1.scheduler.cooldown");
	CooldownTemplate->Metadata.CooldownSeconds = 5.0f;
	TestTrue(
		TEXT("First cooldown template request is accepted"),
		Cooldown->SubmitMotionPlanWithSource(
			MakeSchedulerPlan(TEXT("cooldown.first"), TEXT("head.pitch"), 0.4f, true),
			ELLMNPCMotionValidationSource::PublishedTemplate,
			CooldownTemplate
		)
	);
	TestTrue(
		TEXT("A duplicate queued before the first start is initially accepted"),
		Cooldown->SubmitMotionPlanWithSource(
			MakeSchedulerPlan(TEXT("cooldown.second"), TEXT("head.pitch"), 0.4f, true),
			ELLMNPCMotionValidationSource::PublishedTemplate,
			CooldownTemplate
		)
	);
	Cooldown->StartEligiblePlans();
	Cooldown->UpdateActivePlans(3.0f);
	Cooldown->StartEligiblePlans();
	TestEqual(TEXT("The duplicate waits after the first clip completes"), Cooldown->ActiveMotions.Num(), 0);
	TestEqual(TEXT("Start-time cooldown keeps the duplicate queued"), Cooldown->Queue.Num(), 1);
	Cooldown->LastTemplateStartTimes[CooldownTemplate->Metadata.TemplateId] -= 6.0;
	Cooldown->StartEligiblePlans();
	TestEqual(TEXT("The queued duplicate starts after cooldown"), Cooldown->ActiveMotions.Num(), 1);
	return true;
}

#endif
