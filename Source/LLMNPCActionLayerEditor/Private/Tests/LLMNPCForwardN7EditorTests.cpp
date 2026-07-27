#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Authoring/LLMNPCMotionRecipeAuthoringPrompt.h"
#include "Authoring/LLMNPCTemplateAuthoringSubsystem.h"
#include "Capabilities/LLMNPCSkeletonCapabilityBuilder.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "Templates/LLMNPCMotionTemplate.h"

namespace
{
constexpr uint32 ForwardN7EditorTestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;

const FString ForwardN7Recipe = TEXT(R"JSON(
{
  "schema_version": "llmnpc.motion_recipe.v1",
  "recipe_id": "shrug_revision_automation",
  "intent": "express_uncertainty",
  "duration": 1.7,
  "interruptible": true,
  "primitives": [
    {
      "primitive_id": "shoulder.shrug",
      "start": 0.0,
      "end": 1.7,
      "parameters": {
        "amplitude": 0.68,
        "speed": 0.95,
        "torso_participation": 0.32,
        "arm_openness": 0.54,
        "palm_openness": 0.78,
        "asymmetry": 0.02
      }
    }
  ]
}
)JSON");

bool BuildForwardN7Capability(
	FLLMNPCSkeletonCapabilitySnapshot& OutCapability,
	FString& OutError
)
{
	ULLMNPCSkeletonProfile* Profile =
		LoadObject<ULLMNPCSkeletonProfile>(
			nullptr,
			TEXT("/LLMNPCActionLayer/LLMNPC/SkeletonProfiles/SP_UE5_Manny_v1.SP_UE5_Manny_v1")
		);
	if (!Profile)
	{
		OutError = TEXT("Manny Profile did not load.");
		return false;
	}
	const FLLMNPCSkeletonCapabilityBuildResult Result =
		FLLMNPCSkeletonCapabilityBuilder::Build(
			*Profile,
			nullptr,
			OutCapability
		);
	if (!Result.bSucceeded)
	{
		OutError = Result.Errors.IsEmpty()
			? TEXT("Capability build failed.")
			: Result.Errors[0];
		return false;
	}
	return true;
}

FString BuildForwardN7Response()
{
	return FString::Printf(
		TEXT(
			"{"
			"\"schema_version\":\"llmnpc.motion_recipe_authoring_response.v1\","
			"\"status\":\"recipe\","
			"\"recipe\":%s,"
			"\"catalog_draft\":{"
			"\"display_name\":\"Revised Manny Shrug\","
			"\"selection_summary\":\"Raise both shoulders with balanced arms and relaxed visible palms to express uncertainty.\","
			"\"visual_description\":\"Both shoulders rise together while the arms remain balanced, palms stay relaxed, and the pose returns smoothly to neutral.\","
			"\"suitable_when\":[\"answering with uncertainty\"],"
			"\"avoid_when\":[\"either arm must remain fixed\"]"
			"}"
			"}"
		),
		*ForwardN7Recipe
	);
}

FLLMNPCMotionRecipeDraftCatalogSpec BuildForwardN7CatalogSpec(
	const FString& AssetName,
	FName TemplateId
)
{
	FLLMNPCMotionRecipeDraftCatalogSpec Spec;
	Spec.AssetName = AssetName;
	Spec.TemplateId = TemplateId;
	Spec.PublicActionId =
		TEXT("gesture.shrug.forward_n7_automation");
	Spec.PublicActionAssetName =
		TEXT("PA_ForwardN7_Shrug_Automation");
	Spec.DisplayName = TEXT("Revised Manny Shrug");
	Spec.SelectionSummary =
		TEXT("Raise both shoulders with balanced arms and relaxed visible palms to express uncertainty.");
	Spec.VisualDescription =
		TEXT("Both shoulders rise together while the arms remain balanced, palms stay relaxed, and the pose returns smoothly to neutral.");
	Spec.SuitableWhen = {
		TEXT("answering with uncertainty")
	};
	Spec.AvoidWhen = {
		TEXT("either arm must remain fixed")
	};
	Spec.IntentTags = {TEXT("express_uncertainty")};
	Spec.EmotionTags = {TEXT("uncertain")};
	Spec.VariantStyleTags = {
		TEXT("neutral"),
		TEXT("uncertain")
	};
	Spec.BodyRegionTags = {
		TEXT("shoulders"),
		TEXT("upper_torso"),
		TEXT("two_arms"),
		TEXT("two_hands"),
		TEXT("fingers")
	};
	Spec.SpatialRequirementTags = {
		TEXT("target_independent")
	};
	Spec.SemanticEffectTags = {
		TEXT("express_uncertainty"),
		TEXT("noncommittal")
	};
	Spec.GestureFamily = TEXT("shrug");
	Spec.DefaultStyle = TEXT("uncertain");
	Spec.SearchKeywords = {
		TEXT("shrug"),
		TEXT("uncertain")
	};
	Spec.Expressiveness = 0.58f;
	Spec.Energy = 0.4f;
	Spec.SocialIntensity = 0.46f;
	return Spec;
}

FLLMNPCMotionRecipeGenerationEvidence BuildForwardN7Evidence(
	const FLLMNPCMotionRecipePromptPackage& Prompt
)
{
	FLLMNPCMotionRecipeGenerationEvidence Evidence;
	Evidence.RequestId = FGuid::NewGuid();
	Evidence.ProviderId =
		TEXT("deepseek_direct_editor_authoring");
	Evidence.ProviderModelId = TEXT("automation-model");
	Evidence.EndpointOrigin = TEXT("https://api.example.test");
	Evidence.NonSecretConfigHash =
		TEXT("automation-non-secret-config");
	Evidence.PromptVersion = Prompt.PromptVersion;
	Evidence.PromptHash = Prompt.PromptHash;
	Evidence.CapabilityHash = Prompt.CapabilityHash;
	Evidence.RegistryVersion = Prompt.RegistryVersion;
	Evidence.SystemPrompt = Prompt.SystemPrompt;
	Evidence.UserJson = Prompt.UserJson;
	Evidence.RecipeSchemaJson = Prompt.RecipeSchemaJson;
	Evidence.CapabilityModelViewJson =
		Prompt.CapabilityModelViewJson;
	Evidence.RawResponseJson = BuildForwardN7Response();
	Evidence.TriggerSource =
		Prompt.RequestContext.TriggerSource;
	Evidence.SourceTemplateId =
		Prompt.RequestContext.SourceTemplateId;
	Evidence.SourceRecipeHash =
		Prompt.RequestContext.SourceRecipeHash;
	Evidence.ReviewFeedback =
		Prompt.RequestContext.ReviewFeedback;
	Evidence.GeneratedAtUtc = FDateTime::UtcNow();
	Evidence.HttpStatus = 200;
	Evidence.AttemptCount = 1;
	Evidence.TotalLatencySeconds = 0.2f;
	Evidence.PromptTokens = 120;
	Evidence.CompletionTokens = 70;
	Evidence.TotalTokens = 190;
	return Evidence;
}

bool ReadForwardN7DraftSourcePath(
	const FString& ProvenanceJson,
	FString& OutPath
)
{
	TSharedPtr<FJsonObject> Provenance;
	const TSharedRef<TJsonReader<>> Reader =
		TJsonReaderFactory<>::Create(ProvenanceJson);
	if (
		!FJsonSerializer::Deserialize(Reader, Provenance) ||
		!Provenance.IsValid()
	)
	{
		return false;
	}
	const TSharedPtr<FJsonObject>* ImportRecord = nullptr;
	return
		Provenance->TryGetObjectField(
			TEXT("import_record"),
			ImportRecord
		) &&
		ImportRecord &&
		ImportRecord->IsValid() &&
		(*ImportRecord)->TryGetStringField(
			TEXT("draft_source_copy_path"),
			OutPath
		);
}

void DeleteForwardN7AutomationArtifacts()
{
	for (const FString& AssetPath : {
		FString(
			TEXT("/Game/LLMNPCAutomation/ForwardN7/MT_ForwardN7_Shrug_Parent.MT_ForwardN7_Shrug_Parent")
		),
		FString(
			TEXT("/Game/LLMNPCAutomation/ForwardN7/MT_ForwardN7_Shrug_Revision.MT_ForwardN7_Shrug_Revision")
		),
		FString(
			TEXT("/Game/LLMNPCAutomation/ForwardN7/PA_ForwardN7_Shrug_Automation.PA_ForwardN7_Shrug_Automation")
		)
	})
	{
		const FString PackageName =
			FPackageName::ObjectPathToPackageName(AssetPath);
		if (FPackageName::DoesPackageExist(PackageName))
		{
			if (
				UObject* Asset =
					LoadObject<UObject>(nullptr, *AssetPath)
			)
			{
				ObjectTools::DeleteObjectsUnchecked({Asset});
			}
		}
	}

	for (const FString& Directory : {
		ULLMNPCTemplateAuthoringSubsystem::GetDraftDirectory(),
		ULLMNPCTemplateAuthoringSubsystem::GetRejectedDirectory()
	})
	{
		TArray<FString> Files;
		IFileManager::Get().FindFiles(
			Files,
			*FPaths::Combine(
				Directory,
				TEXT("MT_ForwardN7_Shrug_*.json")
			),
			true,
			false
		);
		for (const FString& File : Files)
		{
			IFileManager::Get().Delete(
				*FPaths::Combine(Directory, File)
			);
		}
	}
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN7RejectedDraftRegenerationTest,
	"LLMNPCActionLayer.ForwardN7.Editor.RejectedDraftRegeneration",
	ForwardN7EditorTestFlags
)

bool FLLMNPCForwardN7RejectedDraftRegenerationTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	AddExpectedError(
		TEXT("package was marked as deleted in editor"),
		EAutomationExpectedErrorFlags::Contains,
		3
	);
	DeleteForwardN7AutomationArtifacts();

	FLLMNPCSkeletonCapabilitySnapshot Capability;
	FString Error;
	if (!BuildForwardN7Capability(Capability, Error))
	{
		AddError(Error);
		return false;
	}

	FLLMNPCMotionRecipePromptPackage ParentPrompt;
	TestTrue(
		*FString::Printf(
			TEXT("Manual Authoring Prompt builds: %s"),
			*Error
		),
		FLLMNPCMotionRecipeAuthoringPrompt::Build(
			TEXT("Create a natural balanced uncertainty shrug."),
			Capability,
			{},
			ParentPrompt,
			Error
		)
	);
	TestTrue(
		TEXT("Manual requests record their trigger source"),
		ParentPrompt.UserJson.Contains(
			TEXT("\"trigger_source\": \"ManualWorkbench\"")
		)
	);

	ULLMNPCTemplateAuthoringSubsystem* Authoring =
		NewObject<ULLMNPCTemplateAuthoringSubsystem>();
	const FLLMNPCAuthoringOperationResult ParentDraft =
		Authoring->CreateMotionRecipeDraft(
			ForwardN7Recipe,
			TEXT("ue5_manny.v1"),
			BuildForwardN7CatalogSpec(
				TEXT("MT_ForwardN7_Shrug_Parent"),
				TEXT("gesture.shrug.manny.forward_n7.parent")
			),
			BuildForwardN7Evidence(ParentPrompt),
			TEXT("/Game/LLMNPCAutomation/ForwardN7"),
			TEXT("/Game/LLMNPCAutomation/ForwardN7")
		);
	TestTrue(
		*FString::Printf(
			TEXT("Parent Generated Draft is created: %s"),
			*ParentDraft.Message
		),
		ParentDraft.bSuccess
	);
	if (!ParentDraft.TemplateAsset)
	{
		DeleteForwardN7AutomationArtifacts();
		return false;
	}

	const FString ReviewFeedback =
		TEXT("Keep both arms symmetric and prevent the right palm from folding inward.");
	const FLLMNPCAuthoringOperationResult Rejection =
		Authoring->RejectTemplate(
			ParentDraft.TemplateAsset,
			TEXT("automation-reviewer"),
			ReviewFeedback
		);
	TestTrue(TEXT("Parent Draft is rejected"), Rejection.bSuccess);

	FLLMNPCMotionRecipeRequestContext RevisionContext;
	RevisionContext.TriggerSource =
		LLMNPCMotionRecipeAuthoring::
			RegenerationTriggerSource;
	RevisionContext.SourceTemplateId =
		ParentDraft.TemplateAsset->Metadata.TemplateId;
	RevisionContext.SourceRecipeHash =
		ParentDraft.TemplateAsset->Metadata.SourceRecipeHash;
	RevisionContext.ReviewFeedback = ReviewFeedback;

	FLLMNPCMotionRecipePromptPackage RevisionPrompt;
	TestTrue(
		*FString::Printf(
			TEXT("Regeneration Prompt builds: %s"),
			*Error
		),
		FLLMNPCMotionRecipeAuthoringPrompt::Build(
			TEXT("Create a natural balanced uncertainty shrug."),
			Capability,
			{},
			RevisionPrompt,
			Error,
			RevisionContext
		)
	);
	TestTrue(
		TEXT("Regeneration request records bounded lineage"),
		RevisionPrompt.UserJson.Contains(
			TEXT("\"trigger_source\": \"RegenerateRejectedDraft\"")
		) &&
		RevisionPrompt.UserJson.Contains(
			ParentDraft.TemplateAsset->Metadata.TemplateId.ToString()
		) &&
		RevisionPrompt.UserJson.Contains(ReviewFeedback)
	);

	const FLLMNPCMotionRecipeDraftCatalogSpec RevisionSpec =
		BuildForwardN7CatalogSpec(
			TEXT("MT_ForwardN7_Shrug_Revision"),
			TEXT("gesture.shrug.manny.forward_n7.revision")
		);
	FLLMNPCMotionRecipeGenerationEvidence RevisionEvidence =
		BuildForwardN7Evidence(RevisionPrompt);

	ParentDraft.TemplateAsset->Metadata.ReviewState =
		ELLMNPCTemplateReviewState::Generated;
	const FLLMNPCAuthoringOperationResult InvalidParent =
		Authoring->CreateMotionRecipeDraft(
			ForwardN7Recipe,
			TEXT("ue5_manny.v1"),
			RevisionSpec,
			RevisionEvidence,
			TEXT("/Game/LLMNPCAutomation/ForwardN7"),
			TEXT("/Game/LLMNPCAutomation/ForwardN7")
		);
	TestFalse(
		TEXT("A non-Rejected parent cannot authorize a revision"),
		InvalidParent.bSuccess
	);
	TestEqual(
		TEXT("The parent-state rejection is stable"),
		InvalidParent.ErrorCode,
		FName(TEXT("LLMNPC_RECIPE_DRAFT_REVISION_PARENT_INVALID"))
	);

	ParentDraft.TemplateAsset->Metadata.ReviewState =
		ELLMNPCTemplateReviewState::Rejected;
	const FLLMNPCAuthoringOperationResult RevisionDraft =
		Authoring->CreateMotionRecipeDraft(
			ForwardN7Recipe,
			TEXT("ue5_manny.v1"),
			RevisionSpec,
			RevisionEvidence,
			TEXT("/Game/LLMNPCAutomation/ForwardN7"),
			TEXT("/Game/LLMNPCAutomation/ForwardN7")
		);
	TestTrue(
		*FString::Printf(
			TEXT("Rejected parent creates a separate revision: %s"),
			*RevisionDraft.Message
		),
		RevisionDraft.bSuccess
	);
	TestNotNull(
		TEXT("Revision Draft is returned"),
		RevisionDraft.TemplateAsset.Get()
	);
	if (!RevisionDraft.TemplateAsset)
	{
		DeleteForwardN7AutomationArtifacts();
		return false;
	}
	TestTrue(
		TEXT("Revision provenance records the trigger and parent"),
		RevisionDraft.TemplateAsset->SourceProvenanceJson.Contains(
			TEXT("RegenerateRejectedDraft")
		) &&
		RevisionDraft.TemplateAsset->SourceProvenanceJson.Contains(
			ParentDraft.TemplateAsset->Metadata.TemplateId.ToString()
		) &&
		RevisionDraft.TemplateAsset->SourceProvenanceJson.Contains(
			ReviewFeedback
		)
	);
	TestNotEqual(
		TEXT("Revision uses a new immutable Template ID"),
		RevisionDraft.TemplateAsset->Metadata.TemplateId,
		ParentDraft.TemplateAsset->Metadata.TemplateId
	);

	FString RevisionJobPath;
	TestTrue(
		TEXT("Revision provenance resolves its Authoring Job"),
		ReadForwardN7DraftSourcePath(
			RevisionDraft.TemplateAsset->SourceProvenanceJson,
			RevisionJobPath
		) &&
		FPaths::FileExists(RevisionJobPath)
	);
	FString RevisionJobJson;
	TestTrue(
		TEXT("Authoring Job records the regeneration trigger"),
		FFileHelper::LoadFileToString(
			RevisionJobJson,
			*RevisionJobPath
		) &&
		RevisionJobJson.Contains(
			TEXT("RegenerateRejectedDraft")
		)
	);

	const FLLMNPCAuthoringOperationResult Quality =
		Authoring->GenerateQualityReport(
			RevisionDraft.TemplateAsset,
			TEXT("")
		);
	TestTrue(
		*FString::Printf(
			TEXT("Revision passes deterministic Quality: %s"),
			*Quality.Message
		),
		Quality.bSuccess
	);

	if (!Quality.OutputPath.IsEmpty())
	{
		IFileManager::Get().Delete(*Quality.OutputPath);
	}
	DeleteForwardN7AutomationArtifacts();
	return true;
}

#endif
