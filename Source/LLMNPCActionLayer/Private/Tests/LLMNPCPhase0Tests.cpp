#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "LLMNPCControlManifest.h"
#include "LLMNPCMotionComponent.h"
#include "LLMNPCMotionSampler.h"
#include "LLMNPCMotionValidator.h"
#include "Providers/LLMNPCMockProvider.h"

#include "JsonObjectConverter.h"

namespace
{
constexpr uint32 Phase0TestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCBuiltInManifestSmokeTest,
	"LLMNPCActionLayer.Phase0.BuiltInManifest",
	Phase0TestFlags
)

bool FLLMNPCBuiltInManifestSmokeTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);

	const FLLMControlDefinition* HeadPitch =
		ULLMNPCControlManifest::FindBuiltInControl(TEXT("head.pitch"));
	TestNotNull(TEXT("The built-in head.pitch control exists"), HeadPitch);
	if (HeadPitch)
	{
		TestTrue(
			TEXT("head.pitch accepts oscillator tracks"),
			HeadPitch->AllowedTrackTypes.Contains(ELLMMotionTrackType::Oscillator)
		);
	}

	TestNull(
		TEXT("Unknown controls are not present in the built-in manifest"),
		ULLMNPCControlManifest::FindBuiltInControl(TEXT("phase0.unknown"))
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCMotionValidatorSmokeTest,
	"LLMNPCActionLayer.Phase0.MotionValidator",
	Phase0TestFlags
)

bool FLLMNPCMotionValidatorSmokeTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);

	ULLMNPCMotionValidator* Validator = NewObject<ULLMNPCMotionValidator>();
	FLLMMotionPlan ValidPlan;
	ValidPlan.Intent = TEXT("phase0.nod");
	ValidPlan.Clip.ClipId = TEXT("phase0.nod.clip");
	ValidPlan.Clip.Duration = 0.9f;

	FLLMMotionTrack& Track = ValidPlan.Clip.Tracks.AddDefaulted_GetRef();
	Track.ControlId = TEXT("head.pitch");
	Track.TrackType = ELLMMotionTrackType::Oscillator;
	Track.StartTime = 0.05f;
	Track.EndTime = 0.82f;
	Track.Amplitude = 10.0f;
	Track.Frequency = 2.0f;

	const FLLMMotionValidationResult ValidResult = Validator->ValidateAndClamp(ValidPlan);
	TestTrue(TEXT("A minimal nod motion plan is accepted"), ValidResult.bValid);

	ValidPlan.Clip.Tracks[0].ControlId = TEXT("phase0.unknown");
	const FLLMMotionValidationResult InvalidResult = Validator->ValidateAndClamp(ValidPlan);
	TestFalse(TEXT("An unknown control is rejected"), InvalidResult.bValid);
	TestTrue(
		TEXT("The rejection identifies the unknown control"),
		InvalidResult.ErrorMessage.Contains(TEXT("LLMNPC_MOTION_CONTROL_UNKNOWN"))
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCMockProviderSmokeTest,
	"LLMNPCActionLayer.Phase0.MockProvider",
	Phase0TestFlags
)

bool FLLMNPCMockProviderSmokeTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);

	FLLMNPCMockProvider Provider;
	FLLMNPCModelTurnRequest Request;
	Request.RequestId = FGuid::NewGuid();
	Request.ContextJson = TEXT("{}");

	bool bCallbackReceived = false;
	Provider.SendTurn(
		Request,
		[this, &Request, &bCallbackReceived](const FLLMNPCModelTurnResult& Result)
		{
			bCallbackReceived = true;
			TestEqual(TEXT("The result keeps the request ID"), Result.RequestId, Request.RequestId);
			TestFalse(TEXT("The empty Phase 0 provider does not report success"), Result.bSuccess);
			TestEqual(
				TEXT("The empty provider returns a stable error code"),
				Result.ErrorCode,
				FName(TEXT("LLMNPC_MOCK_NOT_CONFIGURED"))
			);
		}
	);

	TestTrue(TEXT("The mock provider completes deterministically"), bCallbackReceived);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCMotionSnapshotBaselineTest,
	"LLMNPCActionLayer.Phase0.MotionSnapshotBaseline",
	Phase0TestFlags
)

bool FLLMNPCMotionSnapshotBaselineTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);

	const ULLMNPCMotionComponent* Component = NewObject<ULLMNPCMotionComponent>();
	const TMap<FString, TObjectPtr<AActor>> EmptyTargets;

	FLLMMotionPlan NodPlan;
	const FString NodJson = Component->BuildSampleMotionPlanJson(
		ELLMNPCMotionDebugSample::Nod,
		FString()
	);
	TestTrue(
		TEXT("The nod baseline plan can be parsed"),
		FJsonObjectConverter::JsonObjectStringToUStruct(NodJson, &NodPlan, 0, 0)
	);

	FLLMProceduralPoseSnapshot NodSnapshot;
	FLLMNPCMotionSampler::SampleClip(
		NodPlan.Clip,
		nullptr,
		nullptr,
		EmptyTargets,
		0.14625f,
		NodSnapshot
	);
	TestEqual(TEXT("Nod baseline global alpha"), NodSnapshot.GlobalAlpha, 1.0f);
	TestTrue(
		TEXT("Nod baseline head pitch"),
		FMath::IsNearlyEqual(NodSnapshot.HeadPitch, 3.826834f, 0.001f)
	);

	FLLMMotionPlan WavePlan;
	const FString WaveJson = Component->BuildSampleMotionPlanJson(
		ELLMNPCMotionDebugSample::Wave,
		FString()
	);
	TestTrue(
		TEXT("The wave baseline plan can be parsed"),
		FJsonObjectConverter::JsonObjectStringToUStruct(WaveJson, &WavePlan, 0, 0)
	);

	FLLMProceduralPoseSnapshot WaveSnapshot;
	FLLMNPCMotionSampler::SampleClip(
		WavePlan.Clip,
		nullptr,
		nullptr,
		EmptyTargets,
		0.88f,
		WaveSnapshot
	);
	TestTrue(
		TEXT("Wave baseline upper arm rotation"),
		WaveSnapshot.RightUpperArmAdditiveRotation.Equals(FRotator(-5.5f, -3.1f, -80.0f), 0.001f)
	);
	TestTrue(
		TEXT("Wave baseline lower arm rotation"),
		WaveSnapshot.RightLowerArmAdditiveRotation.Equals(FRotator(-3.3f, 91.4f, -3.4f), 0.001f)
	);
	TestTrue(
		TEXT("Wave baseline hand rotation"),
		WaveSnapshot.RightHandAdditiveRotation.Equals(FRotator(-10.0f, 16.4f, 57.5f), 0.001f)
	);
	TestEqual(TEXT("Wave baseline open fingers"), WaveSnapshot.RightFingersOpen, 1.0f);
	return true;
}

#endif
