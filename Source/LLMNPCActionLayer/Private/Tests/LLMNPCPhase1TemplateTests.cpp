#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Engine/GameInstance.h"
#include "LLMNPCMotionSampler.h"
#include "LLMNPCMotionValidator.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "Templates/LLMNPCTemplateCompiler.h"
#include "Templates/LLMNPCTemplateLibrarySubsystem.h"

namespace
{
constexpr uint32 Phase1TemplateTestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;

const FLLMMotionTrack* FindTrack(const FLLMMotionClip& Clip, FName ControlId)
{
	return Clip.Tracks.FindByPredicate(
		[ControlId](const FLLMMotionTrack& Track)
		{
			return Track.ControlId == ControlId;
		}
	);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCTemplateLibraryTest,
	"LLMNPCActionLayer.Phase1.Templates.Library",
	Phase1TemplateTestFlags
)

bool FLLMNPCTemplateLibraryTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);

	UGameInstance* TestGameInstance = NewObject<UGameInstance>();
	ULLMNPCTemplateLibrarySubsystem* Library =
		NewObject<ULLMNPCTemplateLibrarySubsystem>(TestGameInstance);
	Library->RefreshLibrary();
	TestTrue(TEXT("The five built-in Published templates are indexed"), Library->GetPublishedTemplateCount() >= 5);
	TestTrue(TEXT("The Phase 1 template scan has no errors"), Library->GetScanErrors().IsEmpty());

	const ULLMNPCMotionTemplate* FaithfulWave = Library->FindPublishedTemplate(
		TEXT("gesture.wave.right.manny.fk.v1")
	);
	TestNotNull(TEXT("The faithful FK wave is addressable by exact ID"), FaithfulWave);
	if (FaithfulWave)
	{
		TestTrue(
			TEXT("The manually reviewed FK Wave backs runtime model selection"),
			FaithfulWave->Metadata.bAllowRuntimeModelSelection
		);
	}

	const ULLMNPCMotionTemplate* ModelWave = Library->FindPublishedVariant(
		TEXT("gesture.wave.right"),
		TEXT("ue5_manny.v1")
	);
	TestNotNull(TEXT("The public wave action resolves for Manny"), ModelWave);
	if (ModelWave)
	{
		TestEqual(
			TEXT("The public Wave resolves to the manually reviewed FK implementation"),
			ModelWave->Metadata.TemplateId,
			FName(TEXT("gesture.wave.right.manny.fk.v1"))
		);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCTemplateCompilerBaselineTest,
	"LLMNPCActionLayer.Phase1.Templates.CompilerBaseline",
	Phase1TemplateTestFlags
)

bool FLLMNPCTemplateCompilerBaselineTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);

	const ULLMNPCSkeletonProfile* Profile = LoadObject<ULLMNPCSkeletonProfile>(
		nullptr,
		TEXT("/LLMNPCActionLayer/LLMNPC/SkeletonProfiles/SP_UE5_Manny_v1.SP_UE5_Manny_v1")
	);
	const ULLMNPCMotionTemplate* NodTemplate = LoadObject<ULLMNPCMotionTemplate>(
		nullptr,
		TEXT("/LLMNPCActionLayer/LLMNPC/MotionTemplates/Manny/MT_Nod_Manny_v1.MT_Nod_Manny_v1")
	);
	const ULLMNPCMotionTemplate* WaveTemplate = LoadObject<ULLMNPCMotionTemplate>(
		nullptr,
		TEXT("/LLMNPCActionLayer/LLMNPC/MotionTemplates/Manny/MT_Wave_Right_Manny_FK_v1.MT_Wave_Right_Manny_FK_v1")
	);
	const ULLMNPCMotionTemplate* ProceduralWaveTemplate = LoadObject<ULLMNPCMotionTemplate>(
		nullptr,
		TEXT("/LLMNPCActionLayer/LLMNPC/MotionTemplates/Manny/MT_Wave_Right_Manny_Procedural_v1.MT_Wave_Right_Manny_Procedural_v1")
	);
	TestNotNull(TEXT("The compiler has a Manny profile"), Profile);
	TestNotNull(TEXT("The compiler has a nod template"), NodTemplate);
	TestNotNull(TEXT("The compiler has a faithful wave template"), WaveTemplate);
	TestNotNull(TEXT("The compiler has a procedural wave template"), ProceduralWaveTemplate);
	if (!Profile || !NodTemplate || !WaveTemplate || !ProceduralWaveTemplate)
	{
		return false;
	}

	FLLMNPCTemplateModifiers Modifiers;
	FLLMMotionPlan NodPlan;
	FString CompileError;
	TestTrue(
		TEXT("The Published nod template compiles"),
		FLLMNPCTemplateCompiler::Compile(
			*NodTemplate,
			Modifiers,
			*Profile,
			NodPlan,
			CompileError
		)
	);
	if (!CompileError.IsEmpty())
	{
		AddError(CompileError);
	}

	const TMap<FString, TObjectPtr<AActor>> EmptyTargets;
	FLLMProceduralPoseSnapshot NodSnapshot;
	FLLMNPCMotionSampler::SampleClip(
		NodPlan.Clip,
		nullptr,
		nullptr,
		EmptyTargets,
		0.14625f,
		NodSnapshot
	);
	TestTrue(
		TEXT("The nod template preserves the Phase 0 baseline"),
		FMath::IsNearlyEqual(NodSnapshot.HeadPitch, 3.826834f, 0.001f)
	);

	FLLMMotionPlan WavePlan;
	CompileError.Reset();
	TestTrue(
		TEXT("The Published faithful wave template compiles"),
		FLLMNPCTemplateCompiler::Compile(
			*WaveTemplate,
			Modifiers,
			*Profile,
			WavePlan,
			CompileError
		)
	);
	if (!CompileError.IsEmpty())
	{
		AddError(CompileError);
	}

	ULLMNPCMotionValidator* Validator = NewObject<ULLMNPCMotionValidator>();
	TestTrue(
		TEXT("The compiled faithful wave passes Published Template validation"),
		Validator->ValidateAndClamp(
			WavePlan,
			ELLMNPCMotionValidationSource::PublishedTemplate
		).bValid
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
		TEXT("The wave template preserves the hand rotation baseline"),
		WaveSnapshot.RightHandAdditiveRotation.Equals(
			FRotator(-10.0f, 16.4f, 57.5f),
			0.001f
		)
	);
	TestEqual(TEXT("The wave template preserves the open hand baseline"), WaveSnapshot.RightFingersOpen, 1.0f);

	const FLLMMotionTrack* ProceduralAnchor = FindTrack(
		ProceduralWaveTemplate->ProceduralClip,
		TEXT("right_hand.ik")
	);
	TestNotNull(TEXT("The procedural wave retains its right-hand IK anchor"), ProceduralAnchor);
	if (ProceduralAnchor)
	{
		TestEqual(
			TEXT("The procedural wave uses the Waving-calibrated semantic anchor"),
			ProceduralAnchor->Anchor,
			FName(TEXT("right_wave"))
		);
		TestTrue(
			TEXT("The procedural wave does not stack an uncalibrated anchor offset"),
			ProceduralAnchor->Offset.IsNearlyZero()
		);
	}
	TestNotNull(
		TEXT("The procedural wave oscillates along Manny's lateral axis"),
		FindTrack(ProceduralWaveTemplate->ProceduralClip, TEXT("right_hand.local_offset.x"))
	);
	TestNull(
		TEXT("The procedural wave no longer oscillates along Manny's forward axis"),
		FindTrack(ProceduralWaveTemplate->ProceduralClip, TEXT("right_hand.local_offset.y"))
	);

	FLLMMotionPlan ProceduralWavePlan;
	CompileError.Reset();
	TestTrue(
		TEXT("The calibrated procedural wave template compiles"),
		FLLMNPCTemplateCompiler::Compile(
			*ProceduralWaveTemplate,
			Modifiers,
			*Profile,
			ProceduralWavePlan,
			CompileError
		)
	);
	if (!CompileError.IsEmpty())
	{
		AddError(CompileError);
	}
	FLLMProceduralPoseSnapshot ProceduralWaveSnapshot;
	FLLMNPCMotionSampler::SampleClip(
		ProceduralWavePlan.Clip,
		nullptr,
		nullptr,
		EmptyTargets,
		0.88f,
		ProceduralWaveSnapshot
	);
	TestTrue(
		TEXT("The procedural wave carries the reviewed wrist orientation"),
		ProceduralWaveSnapshot.RightHandAdditiveRotation.Equals(
			FRotator(-10.0f, 16.4f, 57.5f),
			0.001f
		)
	);
	TestEqual(
		TEXT("The calibrated procedural wave keeps the hand open"),
		ProceduralWaveSnapshot.RightFingersOpen,
		1.0f
	);
	return true;
}

#endif
