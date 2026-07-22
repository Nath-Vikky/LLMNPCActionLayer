#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Authoring/LLMNPCTemplateAuthoringSubsystem.h"
#include "Authoring/LLMNPCTemplateDraftImporter.h"
#include "Authoring/LLMNPCUEPIArtifactAdapter.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "ObjectTools.h"
#include "Templates/LLMNPCMotionTemplate.h"

namespace
{
constexpr uint32 Phase3TestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;

FString ExamplePath(const TCHAR* FileName)
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("LLMNPCActionLayer"));
	return Plugin.IsValid()
		? FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources/AuthoringExamples"), FileName)
		: FString();
}

bool LoadExample(const TCHAR* FileName, FString& OutJson)
{
	return FFileHelper::LoadFileToString(OutJson, *ExamplePath(FileName));
}

bool PrepareDraft(
	FString& OutDraftJson,
	FLLMNPCUEPIReconstructionSummary& OutSummary,
	FString& OutProfileJson,
	FString& OutContextJson,
	FString& OutError
)
{
	if (
		!LoadExample(TEXT("DT_Wave_Right_Manny_UEPI_v2.json"), OutDraftJson) ||
		!LoadExample(TEXT("Waving_reconstruction_excerpt.json"), OutProfileJson)
	)
	{
		OutError = TEXT("Phase 3 example files could not be read.");
		return false;
	}
	if (!FLLMNPCUEPIArtifactAdapter::ParseReconstructionProfile(
		OutProfileJson,
		OutSummary,
		OutContextJson,
		OutError
	))
	{
		return false;
	}
	OutDraftJson.ReplaceInline(
		TEXT("sha1:dd58d7d67de5761e19098d6b32ac624f1f998055"),
		*OutSummary.ProfileContentHash
	);
	FString FullPoseJson;
	if (!LoadExample(TEXT("Waving_full_pose_excerpt.json"), FullPoseJson))
	{
		OutError = TEXT("The Full Pose fixture could not be read.");
		return false;
	}
	OutDraftJson.ReplaceInline(
		TEXT("sha1:7aec2ea8cfc356c97b9f6e60316c6ecd09a8097f"),
		*FLLMNPCUEPIArtifactAdapter::HashJson(FullPoseJson)
	);
	return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCUEPIArtifactAdapterTest,
	"LLMNPCActionLayer.Phase3.Authoring.UEPIArtifactAdapter",
	Phase3TestFlags
)

bool FLLMNPCUEPIArtifactAdapterTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	FString ProfileJson;
	TestTrue(TEXT("The Reconstruction Profile fixture loads"), LoadExample(TEXT("Waving_reconstruction_excerpt.json"), ProfileJson));

	FLLMNPCUEPIReconstructionSummary Summary;
	FString ContextJson;
	FString Error;
	TestTrue(
		TEXT("The UEPI Reconstruction Profile parses"),
		FLLMNPCUEPIArtifactAdapter::ParseReconstructionProfile(ProfileJson, Summary, ContextJson, Error)
	);
	TestEqual(TEXT("The adapter retains the source sequence"), Summary.SequencePath, FString(TEXT("/Game/LLMNPC/Animation/Waving.Waving")));
	TestEqual(TEXT("One excerpted driver curve is counted"), Summary.DriverCurveCount, 1);
	TestEqual(TEXT("Three excerpted driver keys are counted"), Summary.DriverKeyCount, 3);
	TestTrue(TEXT("The authoring context retains driver evidence"), ContextJson.Contains(TEXT("driver_track_curves")));
	TestTrue(TEXT("The authoring context forbids runtime bone output"), ContextJson.Contains(TEXT("runtime_bone_output_forbidden")));
	TestFalse(TEXT("The context does not embed Full Pose samples"), ContextJson.Contains(TEXT("\"samples\"")));

	FString FullPoseJson;
	TestTrue(TEXT("The Full Pose fixture loads"), LoadExample(TEXT("Waving_full_pose_excerpt.json"), FullPoseJson));
	TestTrue(
		TEXT("The matching Full Pose artifact validates on demand"),
		FLLMNPCUEPIArtifactAdapter::ValidateFullPoseArtifact(FullPoseJson, Summary, Error)
	);

	const FString InvalidTimes = ProfileJson.Replace(
		TEXT("\"normalized_time\": 1.0,\n          \"local_transform\""),
		TEXT("\"normalized_time\": 2.0,\n          \"local_transform\"")
	);
	TestFalse(
		TEXT("Out-of-range driver times are rejected"),
		FLLMNPCUEPIArtifactAdapter::ParseReconstructionProfile(InvalidTimes, Summary, ContextJson, Error)
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCTemplateDraftParserTest,
	"LLMNPCActionLayer.Phase3.Authoring.StrictDraftParser",
	Phase3TestFlags
)

bool FLLMNPCTemplateDraftParserTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	FString DraftJson;
	FString ProfileJson;
	FString ContextJson;
	FString Error;
	FLLMNPCUEPIReconstructionSummary Summary;
	TestTrue(TEXT("The Phase 3 Draft fixture is prepared"), PrepareDraft(DraftJson, Summary, ProfileJson, ContextJson, Error));

	ULLMNPCMotionTemplate* Parsed = NewObject<ULLMNPCMotionTemplate>();
	FLLMNPCParsedDraftInfo Info;
	TestTrue(TEXT("A valid Draft parses"), FLLMNPCTemplateDraftImporter::ParseDraftJson(DraftJson, *Parsed, Info, Error));
	TestEqual(TEXT("Imported Draft state is forced to Generated"), Parsed->Metadata.ReviewState, ELLMNPCTemplateReviewState::Generated);
	TestFalse(TEXT("A Generated Draft is not Published"), Parsed->IsPublished());
	TestEqual(TEXT("The Draft contains five bounded semantic tracks"), Parsed->ProceduralClip.Tracks.Num(), 5);
	TestEqual(TEXT("The source sequence is retained"), Info.SourceSequencePath, Summary.SequencePath);

	const FString CompatibleDraft = DraftJson.Replace(
		TEXT("\"skeleton_profile_id\": \"ue5_manny.v1\","),
		TEXT("\"skeleton_profile_id\": \"ue5_manny.v1\",\n  \"compatible_skeleton_profile_ids\": [\"custom_humanoid.v1\"],")
	);
	ULLMNPCMotionTemplate* CompatibleTemplate = NewObject<ULLMNPCMotionTemplate>();
	TestTrue(
		TEXT("A Draft may declare an explicit reviewed Profile compatibility list"),
		FLLMNPCTemplateDraftImporter::ParseDraftJson(CompatibleDraft, *CompatibleTemplate, Info, Error)
	);
	TestTrue(
		TEXT("The compatibility list reaches template metadata"),
		CompatibleTemplate->Metadata.CompatibleSkeletonProfileIds.Contains(TEXT("custom_humanoid.v1"))
	);

	const FString SelfPublished = DraftJson.Replace(
		TEXT("\"review_state\": \"generated\""),
		TEXT("\"review_state\": \"published\"")
	);
	TestFalse(
		TEXT("A Draft cannot self-declare Published"),
		FLLMNPCTemplateDraftImporter::ParseDraftJson(SelfPublished, *NewObject<ULLMNPCMotionTemplate>(), Info, Error)
	);
	TestEqual(TEXT("Self-publication has a stable rejection"), Error, FString(TEXT("LLMNPC_DRAFT_REVIEW_STATE_FORBIDDEN")));

	const FString RawBoneControl = DraftJson.Replace(TEXT("right_upperarm.pitch"), TEXT("upperarm_r"));
	ULLMNPCMotionTemplate* RawBoneTemplate = NewObject<ULLMNPCMotionTemplate>();
	TestTrue(
		TEXT("Raw bone-looking IDs are parsed only as untrusted Draft data"),
		FLLMNPCTemplateDraftImporter::ParseDraftJson(RawBoneControl, *RawBoneTemplate, Info, Error)
	);
	ULLMNPCTemplateAuthoringSubsystem* Authoring = NewObject<ULLMNPCTemplateAuthoringSubsystem>();
	const FLLMNPCAuthoringOperationResult Quality = Authoring->GenerateQualityReport(
		RawBoneTemplate,
		ExamplePath(TEXT("Waving_reconstruction_excerpt.json"))
	);
	TestFalse(TEXT("Unknown controls fail the quality gate"), Quality.bSuccess);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCTemplateReviewGateTest,
	"LLMNPCActionLayer.Phase3.Review.HumanApprovalGate",
	Phase3TestFlags
)

bool FLLMNPCTemplateReviewGateTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	FString DraftJson;
	FString ProfileJson;
	FString ContextJson;
	FString Error;
	FLLMNPCUEPIReconstructionSummary Summary;
	TestTrue(TEXT("The review fixture is prepared"), PrepareDraft(DraftJson, Summary, ProfileJson, ContextJson, Error));

	ULLMNPCMotionTemplate* Template = NewObject<ULLMNPCMotionTemplate>();
	FLLMNPCParsedDraftInfo Info;
	TestTrue(TEXT("The review Draft parses"), FLLMNPCTemplateDraftImporter::ParseDraftJson(DraftJson, *Template, Info, Error));
	ULLMNPCTemplateAuthoringSubsystem* Authoring = NewObject<ULLMNPCTemplateAuthoringSubsystem>();

	TestFalse(TEXT("Generated templates cannot publish"), Authoring->CanPublishTemplate(Template, Error));
	TestFalse(
		TEXT("Approval cannot skip Previewed"),
		Authoring->ApproveTemplate(Template, TEXT("automation"), TEXT("reviewed")).bSuccess
	);

	const FLLMNPCAuthoringOperationResult Quality = Authoring->GenerateQualityReport(
		Template,
		ExamplePath(TEXT("Waving_reconstruction_excerpt.json")),
		ExamplePath(TEXT("Waving_full_pose_excerpt.json"))
	);
	TestTrue(*FString::Printf(TEXT("Quality report passes: %s"), *Quality.Message), Quality.bSuccess);
	TestTrue(
		TEXT("A passing quality report still does not publish"),
		!Authoring->CanPublishTemplate(Template, Error)
	);
	TestTrue(
		TEXT("Explicit preview notes advance to Previewed"),
		Authoring->MarkTemplatePreviewed(Template, TEXT("Wave silhouette and hand path checked in PIE.")).bSuccess
	);
	TestEqual(TEXT("The state is Previewed"), Template->Metadata.ReviewState, ELLMNPCTemplateReviewState::Previewed);
	TestTrue(
		TEXT("A named reviewer can approve a Previewed template"),
		Authoring->ApproveTemplate(Template, TEXT("phase3-automation-reviewer"), TEXT("Approved for gate verification.")).bSuccess
	);
	TestEqual(TEXT("The state is HumanApproved"), Template->Metadata.ReviewState, ELLMNPCTemplateReviewState::HumanApproved);
	TestTrue(TEXT("HumanApproved plus current report opens the publish gate"), Authoring->CanPublishTemplate(Template, Error));

	Template->ProceduralClip.Tracks[0].FloatKeys[1].V += 1.0f;
	TestFalse(TEXT("Editing motion after review makes the quality report stale"), Authoring->CanPublishTemplate(Template, Error));
	TestEqual(TEXT("Stale reports have a stable reason"), Error, FString(TEXT("LLMNPC_AUTHORING_QUALITY_REPORT_STALE")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCTemplateImporterAssetTest,
	"LLMNPCActionLayer.Phase3.Authoring.EditorImporter",
	Phase3TestFlags
)

bool FLLMNPCTemplateImporterAssetTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	const FString AssetPath = TEXT("/Game/LLMNPCAutomation/Phase3/DT_Phase3_Import_Automation.DT_Phase3_Import_Automation");
	if (ULLMNPCMotionTemplate* Existing = LoadObject<ULLMNPCMotionTemplate>(nullptr, *AssetPath))
	{
		ObjectTools::DeleteObjectsUnchecked({ Existing });
	}

	FString DraftJson;
	FString ProfileJson;
	FString ContextJson;
	FString Error;
	FLLMNPCUEPIReconstructionSummary Summary;
	TestTrue(TEXT("The importer fixture is prepared"), PrepareDraft(DraftJson, Summary, ProfileJson, ContextJson, Error));
	DraftJson.ReplaceInline(TEXT("DT_Wave_Right_Manny_UEPI_v2"), TEXT("DT_Phase3_Import_Automation"));
	DraftJson.ReplaceInline(TEXT("gesture.wave.right.manny.uepi.v2"), TEXT("gesture.wave.right.manny.phase3_import_test.v1"));

	ULLMNPCTemplateAuthoringSubsystem* Authoring = NewObject<ULLMNPCTemplateAuthoringSubsystem>();
	const FLLMNPCAuthoringOperationResult ImportResult = Authoring->ImportDraftJson(
		DraftJson,
		TEXT("/Game/LLMNPCAutomation/Phase3")
	);
	TestTrue(*FString::Printf(TEXT("Draft asset imports: %s"), *ImportResult.Message), ImportResult.bSuccess);
	TestNotNull(TEXT("The importer returns the new asset"), ImportResult.TemplateAsset.Get());
	if (ImportResult.TemplateAsset)
	{
		TestEqual(
			TEXT("Imported assets cannot bypass Generated"),
			ImportResult.TemplateAsset->Metadata.ReviewState,
			ELLMNPCTemplateReviewState::Generated
		);
		TestFalse(TEXT("Imported assets are not runtime Published"), ImportResult.TemplateAsset->IsPublished());
		TestEqual(
			TEXT("The automation asset is removed after verification"),
			ObjectTools::DeleteObjectsUnchecked({ ImportResult.TemplateAsset.Get() }),
			1
		);
	}
	return true;
}

#endif
