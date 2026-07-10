#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "LLMNPCMotionValidator.h"

#include <limits>

namespace
{
constexpr uint32 Phase1ValidatorTestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;

FLLMMotionPlan MakeSingleTrackPlan(FName ControlId, ELLMMotionTrackType TrackType)
{
	FLLMMotionPlan Plan;
	Plan.Intent = TEXT("phase1.validator");
	Plan.Clip.ClipId = TEXT("phase1.validator.clip");
	Plan.Clip.Duration = 1.0f;

	FLLMMotionTrack& Track = Plan.Clip.Tracks.AddDefaulted_GetRef();
	Track.ControlId = ControlId;
	Track.TrackType = TrackType;
	Track.StartTime = 0.0f;
	Track.EndTime = 1.0f;
	if (TrackType == ELLMMotionTrackType::Keyframes)
	{
		Track.FloatKeys = {{0.0f, 0.0f}, {1.0f, 10.0f}};
	}
	return Plan;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCValidatorTrustBoundaryTest,
	"LLMNPCActionLayer.Phase1.Validator.TrustBoundary",
	Phase1ValidatorTestFlags
)

bool FLLMNPCValidatorTrustBoundaryTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	const ULLMNPCMotionValidator* Validator = NewObject<ULLMNPCMotionValidator>();

	FLLMMotionPlan RuntimePlan = MakeSingleTrackPlan(
		TEXT("right_upperarm.pitch"),
		ELLMMotionTrackType::Keyframes
	);
	const FLLMMotionValidationResult RuntimeResult = Validator->ValidateAndClamp(
		RuntimePlan,
		ELLMNPCMotionValidationSource::RuntimeModel
	);
	TestFalse(TEXT("Runtime model cannot call direct FK controls"), RuntimeResult.bValid);
	TestTrue(
		TEXT("Direct FK rejection has a stable reason"),
		RuntimeResult.ErrorMessage.Contains(TEXT("LLMNPC_MOTION_CONTROL_INTERNAL_ONLY"))
	);

	FLLMMotionPlan TemplatePlan = MakeSingleTrackPlan(
		TEXT("right_upperarm.pitch"),
		ELLMMotionTrackType::Keyframes
	);
	const FLLMMotionValidationResult TemplateResult = Validator->ValidateAndClamp(
		TemplatePlan,
		ELLMNPCMotionValidationSource::PublishedTemplate
	);
	TestTrue(TEXT("Published templates may use approved direct FK controls"), TemplateResult.bValid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCValidatorMalformedDataTest,
	"LLMNPCActionLayer.Phase1.Validator.MalformedData",
	Phase1ValidatorTestFlags
)

bool FLLMNPCValidatorMalformedDataTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	const ULLMNPCMotionValidator* Validator = NewObject<ULLMNPCMotionValidator>();

	FLLMMotionPlan NonFinitePlan = MakeSingleTrackPlan(
		TEXT("head.pitch"),
		ELLMMotionTrackType::Oscillator
	);
	NonFinitePlan.Clip.Duration = std::numeric_limits<float>::quiet_NaN();
	TestFalse(
		TEXT("Non-finite clip data is rejected"),
		Validator->ValidateAndClamp(NonFinitePlan).bValid
	);

	FLLMMotionPlan ReversedTimePlan = MakeSingleTrackPlan(
		TEXT("head.pitch"),
		ELLMMotionTrackType::Oscillator
	);
	ReversedTimePlan.Clip.Tracks[0].StartTime = 0.8f;
	ReversedTimePlan.Clip.Tracks[0].EndTime = 0.2f;
	TestFalse(
		TEXT("Reversed track time is rejected"),
		Validator->ValidateAndClamp(ReversedTimePlan).bValid
	);

	FLLMMotionPlan MissingTargetPlan = MakeSingleTrackPlan(
		TEXT("gaze.target"),
		ELLMMotionTrackType::LookAt
	);
	TestFalse(
		TEXT("Targeted controls require a TargetRef"),
		Validator->ValidateAndClamp(MissingTargetPlan).bValid
	);

	FLLMMotionPlan UnsortedKeysPlan = MakeSingleTrackPlan(
		TEXT("head.pitch"),
		ELLMMotionTrackType::Keyframes
	);
	UnsortedKeysPlan.Clip.Tracks[0].FloatKeys = {
		{1.0f, 10.0f},
		{0.0f, 0.0f},
		{0.5f, 5.0f}
	};
	TestTrue(
		TEXT("Valid unsorted keys are normalized"),
		Validator->ValidateAndClamp(UnsortedKeysPlan).bValid
	);
	TestTrue(
		TEXT("Keys are sorted by time"),
		UnsortedKeysPlan.Clip.Tracks[0].FloatKeys[0].T <
		UnsortedKeysPlan.Clip.Tracks[0].FloatKeys[1].T
	);
	return true;
}

#endif
