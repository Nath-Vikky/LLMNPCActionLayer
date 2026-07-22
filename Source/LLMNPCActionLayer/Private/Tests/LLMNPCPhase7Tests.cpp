#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Engine/SkeletalMesh.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "Templates/LLMNPCTemplateCompiler.h"

namespace
{
constexpr uint32 Phase7TestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;

ULLMNPCMotionTemplate* MakeCompatibleProceduralTemplate()
{
	ULLMNPCMotionTemplate* Template = NewObject<ULLMNPCMotionTemplate>();
	Template->Metadata.TemplateId = TEXT("gesture.phase7.compatible.v1");
	Template->Metadata.PublicActionId = TEXT("gesture.phase7.compatible");
	Template->Metadata.SkeletonProfileId = TEXT("source_humanoid.v1");
	Template->Metadata.CompatibleSkeletonProfileIds = {TEXT("target_humanoid.v1")};
	Template->Metadata.ReviewState = ELLMNPCTemplateReviewState::Published;
	Template->SourceProvenanceJson = TEXT("{\"source\":\"automation\"}");
	Template->ValidationReportJson = TEXT("{\"status\":\"pass\"}");
	Template->ProceduralClip.Duration = 1.0f;
	FLLMMotionTrack& Track = Template->ProceduralClip.Tracks.AddDefaulted_GetRef();
	Track.ControlId = TEXT("head.pitch");
	Track.TrackType = ELLMMotionTrackType::Hold;
	Track.StartTime = 0.0f;
	Track.EndTime = 1.0f;
	Track.Amplitude = 10.0f;
	return Template;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCPhase7ProfileBindingTest,
	"LLMNPCActionLayer.Phase7.SkeletonProfiles.RuntimeBindings",
	Phase7TestFlags
)

bool FLLMNPCPhase7ProfileBindingTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	ULLMNPCSkeletonProfile* Profile = NewObject<ULLMNPCSkeletonProfile>();
	Profile->ProfileId = TEXT("custom_humanoid.v1");
	Profile->bApplyAxisCalibrationAtRuntime = true;
	Profile->SemanticBoneMap = {
		{TEXT("head"), TEXT("CustomHead")},
		{TEXT("chest"), TEXT("CustomChest")},
		{TEXT("upperarm_right"), TEXT("CustomRightArm")},
		{TEXT("lowerarm_right"), TEXT("CustomRightForeArm")},
		{TEXT("hand_right"), TEXT("CustomRightHand")},
		{TEXT("upperarm_left"), TEXT("CustomLeftArm")},
		{TEXT("lowerarm_left"), TEXT("CustomLeftForeArm")},
		{TEXT("hand_left"), TEXT("CustomLeftHand")},
		{TEXT("index_01_right"), TEXT("CustomRightIndex1")}
	};
	FLLMNPCFingerPoseProfile& OpenPose = Profile->FingerPoses.AddDefaulted_GetRef();
	OpenPose.PoseId = TEXT("open");
	OpenPose.SemanticBoneRotations.Add(TEXT("index_01_right"), FRotator(1.0f, 2.0f, 3.0f));

	const FLLMNPCPoseBoneBindings Bindings = Profile->BuildPoseBoneBindings();
	TestEqual(TEXT("The profile ID reaches the animation snapshot"), Bindings.ProfileId, Profile->ProfileId);
	TestTrue(TEXT("Runtime axis calibration is explicit"), Bindings.bApplyAxisCalibration);
	TestEqual(TEXT("The custom head replaces the Manny fallback"), Bindings.Head, FName(TEXT("CustomHead")));
	TestEqual(
		TEXT("A custom finger binding reaches the fixed animation-thread layout"),
		Bindings.RightFingerBones[3],
		FName(TEXT("CustomRightIndex1"))
	);
	TestTrue(
		TEXT("Profile finger calibration replaces the default open pose"),
		Bindings.RightFingerOpenRotations[3].Equals(FRotator(1.0f, 2.0f, 3.0f))
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCPhase7TemplateCompatibilityTest,
	"LLMNPCActionLayer.Phase7.SkeletonProfiles.TemplateCompatibility",
	Phase7TestFlags
)

bool FLLMNPCPhase7TemplateCompatibilityTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	ULLMNPCMotionTemplate* Template = MakeCompatibleProceduralTemplate();
	TestTrue(
		TEXT("The template advertises its reviewed compatible profile"),
		Template->SupportsSkeletonProfile(TEXT("target_humanoid.v1"))
	);
	TestFalse(
		TEXT("Unreviewed profiles remain unavailable"),
		Template->SupportsSkeletonProfile(TEXT("unknown_humanoid.v1"))
	);

	ULLMNPCSkeletonProfile* TargetProfile = NewObject<ULLMNPCSkeletonProfile>();
	TargetProfile->ProfileId = TEXT("target_humanoid.v1");
	FLLMNPCTemplateModifiers Modifiers;
	FLLMMotionPlan Plan;
	FString Error;
	TestTrue(
		TEXT("The compiler accepts an explicitly compatible profile"),
		FLLMNPCTemplateCompiler::Compile(*Template, Modifiers, *TargetProfile, Plan, Error)
	);
	if (!Error.IsEmpty())
	{
		AddError(Error);
	}
	Template->Metadata.CompatibleSkeletonProfileIds.Add(TEXT("target_humanoid.v1"));
	TestFalse(TEXT("Duplicate compatibility declarations are rejected"), Template->ValidateTemplate(Error));
	TestEqual(
		TEXT("Duplicate compatibility has a stable error"),
		Error,
		FString(TEXT("LLMNPC_TEMPLATE_COMPATIBLE_SKELETON_PROFILE_INVALID"))
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCPhase7MannyQuinnCompatibilityTest,
	"LLMNPCActionLayer.Phase7.SkeletonProfiles.MannyQuinnCompatibility",
	Phase7TestFlags
)

bool FLLMNPCPhase7MannyQuinnCompatibilityTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	const ULLMNPCSkeletonProfile* Profile = LoadObject<ULLMNPCSkeletonProfile>(
		nullptr,
		TEXT("/LLMNPCActionLayer/LLMNPC/SkeletonProfiles/SP_UE5_Manny_v1.SP_UE5_Manny_v1")
	);
	const USkeletalMesh* Manny = LoadObject<USkeletalMesh>(
		nullptr,
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny.SKM_Manny")
	);
	const USkeletalMesh* Quinn = LoadObject<USkeletalMesh>(
		nullptr,
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn.SKM_Quinn")
	);
	TestNotNull(TEXT("The Manny Skeleton Profile loads"), Profile);
	TestNotNull(TEXT("The Manny mesh loads"), Manny);
	TestNotNull(TEXT("The Quinn mesh loads"), Quinn);
	if (!Profile || !Manny || !Quinn)
	{
		return false;
	}

	TestEqual(TEXT("Manny and Quinn share the UE5 mannequin Skeleton"), Manny->GetSkeleton(), Quinn->GetSkeleton());
	TestTrue(TEXT("The Manny profile accepts Manny"), Profile->IsCompatibleSkeleton(Manny->GetSkeleton()));
	TestTrue(TEXT("The same profile accepts Quinn"), Profile->IsCompatibleSkeleton(Quinn->GetSkeleton()));

	const FLLMNPCSkeletonProfileQualityReport Report = Profile->BuildQualityReport();
	TestTrue(TEXT("The shipped profile passes the Phase 7 quality gate"), Report.bPassed);
	TestTrue(TEXT("All core procedural controls are mapped"), FMath::IsNearlyEqual(Report.CoreBoneCoverage, 1.0f));
	TestTrue(
		TEXT("Shipped finger calibration coverage is measured"),
		Report.FingerPoseCoverage > 0.0f && Report.FingerPoseCoverage <= 1.0f
	);
	TestTrue(TEXT("The Skeleton signature is current"), Report.bSignatureCurrent);
	return true;
}

#endif
