#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Authoring/LLMNPCMotionRecipeAuthoringPrompt.h"
#include "Authoring/LLMNPCTemplateAuthoringSubsystem.h"
#include "Authoring/LLMNPCUEPIArtifactAdapter.h"
#include "Capabilities/LLMNPCSkeletonCapabilityBuilder.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "LLMNPCSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "MotionRecipe/LLMNPCMotionPrimitiveRegistry.h"
#include "ObjectTools.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "Templates/LLMNPCActionVocabulary.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "Templates/LLMNPCPublicActionDefinition.h"

namespace
{
constexpr uint32 ForwardN5EditorTestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;

const TCHAR* MannyProfilePath =
	TEXT("/LLMNPCActionLayer/LLMNPC/SkeletonProfiles/SP_UE5_Manny_v1.SP_UE5_Manny_v1");

const FString ValidShrugRecipe = TEXT(R"JSON(
{
  "schema_version": "llmnpc.motion_recipe.v1",
  "recipe_id": "shrug_automation",
  "intent": "express_uncertainty",
  "duration": 1.6,
  "interruptible": true,
  "primitives": [
    {
      "primitive_id": "shoulder.shrug",
      "start": 0.0,
      "end": 1.6,
      "parameters": {
        "amplitude": 0.72,
        "speed": 1.0,
        "torso_participation": 0.35,
        "arm_openness": 0.58,
        "palm_openness": 0.76,
        "asymmetry": 0.04
      }
    }
  ]
}
)JSON");

bool BuildForwardN5Capability(
	FLLMNPCSkeletonCapabilitySnapshot& OutCapability,
	FString& OutError
)
{
	ULLMNPCSkeletonProfile* Profile =
		LoadObject<ULLMNPCSkeletonProfile>(nullptr, MannyProfilePath);
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

FString BuildForwardN5Response(const FString& RecipeJson)
{
	return FString::Printf(
		TEXT(
			"{"
			"\"schema_version\":\"llmnpc.motion_recipe_authoring_response.v1\","
			"\"status\":\"recipe\","
			"\"recipe\":%s,"
			"\"catalog_draft\":{"
			"\"display_name\":\"Manny Uncertain Shrug\","
			"\"selection_summary\":\"Raise both shoulders with relaxed hands to express uncertainty without interrupting locomotion.\","
			"\"visual_description\":\"Both shoulders rise together with subtle chest support, slightly open elbows, relaxed hands, and a smooth return to neutral.\","
			"\"suitable_when\":[\"answering with uncertainty\",\"giving a noncommittal response\"],"
			"\"avoid_when\":[\"either arm must remain fixed\",\"formal stillness is required\"]"
			"}"
			"}"
		),
		*RecipeJson
	);
}

FLLMNPCMotionRecipeDraftCatalogSpec BuildForwardN5CatalogSpec()
{
	FLLMNPCMotionRecipeDraftCatalogSpec Spec;
	Spec.AssetName = TEXT("MT_ForwardN5_Shrug_Automation");
	Spec.TemplateId =
		TEXT("gesture.shrug.manny.generated.automation");
	Spec.PublicActionId = TEXT("gesture.shrug.automation");
	Spec.PublicActionAssetName =
		TEXT("PA_ForwardN5_Shrug_Automation");
	Spec.DisplayName = TEXT("Manny Uncertain Shrug");
	Spec.SelectionSummary =
		TEXT("Raise both shoulders with relaxed hands to express uncertainty without interrupting locomotion.");
	Spec.VisualDescription =
		TEXT("Both shoulders rise together with subtle chest support, slightly open elbows, relaxed hands, and a smooth return to neutral.");
	Spec.SuitableWhen = {
		TEXT("answering with uncertainty"),
		TEXT("giving a noncommittal response")
	};
	Spec.AvoidWhen = {
		TEXT("either arm must remain fixed"),
		TEXT("formal stillness is required")
	};
	Spec.IntentTags = {TEXT("express_uncertainty")};
	Spec.EmotionTags = {TEXT("uncertain")};
	Spec.VariantStyleTags = {
		TEXT("neutral"),
		TEXT("subtle"),
		TEXT("uncertain")
	};
	Spec.BodyRegionTags = {
		TEXT("shoulders"),
		TEXT("upper_torso"),
		TEXT("two_arms"),
		TEXT("two_hands"),
		TEXT("fingers")
	};
	Spec.SpatialRequirementTags = {TEXT("target_independent")};
	Spec.SemanticEffectTags = {
		TEXT("express_uncertainty"),
		TEXT("noncommittal")
	};
	Spec.GestureFamily = TEXT("shrug");
	Spec.DefaultStyle = TEXT("uncertain");
	Spec.SearchKeywords = {
		TEXT("shrug"),
		TEXT("uncertain"),
		TEXT("unsure")
	};
	Spec.Expressiveness = 0.6f;
	Spec.Energy = 0.42f;
	Spec.SocialIntensity = 0.48f;
	return Spec;
}

bool ReadDraftSourcePath(
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

void DeleteForwardN5AutomationAssets()
{
	for (const FString& AssetPath : {
		FString(
			TEXT("/Game/LLMNPCAutomation/ForwardN5/MT_ForwardN5_Shrug_Automation.MT_ForwardN5_Shrug_Automation")
		),
		FString(
			TEXT("/Game/LLMNPCAutomation/ForwardN5/PA_ForwardN5_Shrug_Automation.PA_ForwardN5_Shrug_Automation")
		)
	})
	{
		const FString PackageName =
			FPackageName::ObjectPathToPackageName(AssetPath);
		if (
			FPackageName::DoesPackageExist(PackageName)
		)
		{
			if (UObject* Asset =
				LoadObject<UObject>(nullptr, *AssetPath))
			{
				ObjectTools::DeleteObjectsUnchecked({Asset});
			}
		}
	}
	TArray<FString> JobFiles;
	IFileManager::Get().FindFiles(
		JobFiles,
		*FPaths::Combine(
			ULLMNPCTemplateAuthoringSubsystem::GetDraftDirectory(),
			TEXT("MT_ForwardN5_Shrug_Automation_online_*.json")
		),
		true,
		false
	);
	for (const FString& JobFile : JobFiles)
	{
		IFileManager::Get().Delete(
			*FPaths::Combine(
				ULLMNPCTemplateAuthoringSubsystem::GetDraftDirectory(),
				JobFile
			)
		);
	}
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN5SchemaArtifactTest,
	"LLMNPCActionLayer.ForwardN5.Editor.SchemaArtifact",
	ForwardN5EditorTestFlags
)

bool FLLMNPCForwardN5SchemaArtifactTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	const TSharedPtr<IPlugin> Plugin =
		IPluginManager::Get().FindPlugin(TEXT("LLMNPCActionLayer"));
	TestTrue(TEXT("LLMNPCActionLayer plugin resolves"), Plugin.IsValid());
	if (!Plugin.IsValid())
	{
		return false;
	}

	const FString ResourcePath = FPaths::Combine(
		Plugin->GetBaseDir(),
		TEXT("Resources"),
		TEXT("Schemas"),
		TEXT("llmnpc_motion_recipe_v1.schema.json")
	);
	FString ResourceJson;
	TestTrue(
		TEXT("Motion Recipe Schema resource loads"),
		FFileHelper::LoadFileToString(ResourceJson, *ResourcePath)
	);

	FString GeneratedJson;
	FString Error;
	TestTrue(
		*FString::Printf(
			TEXT("Registry generates the base Schema: %s"),
			*Error
		),
		FLLMNPCMotionPrimitiveRegistry::Get().BuildModelSchemaJson(
			nullptr,
			GeneratedJson,
			Error
		)
	);
	if (ResourceJson.IsEmpty() || GeneratedJson.IsEmpty())
	{
		return false;
	}

	TestEqual(
		TEXT("Checked-in Schema matches the current primitive registry"),
		FLLMNPCUEPIArtifactAdapter::HashJson(ResourceJson),
		FLLMNPCUEPIArtifactAdapter::HashJson(GeneratedJson)
	);
	TestTrue(
		TEXT("Editable plugin keeps Published Public Action source in the plugin"),
		FPaths::IsUnderDirectory(
			ULLMNPCTemplateAuthoringSubsystem::
				GetPublishedPublicActionSourceDirectory(),
			Plugin->GetBaseDir()
		)
	);
	for (const FString& RestrictedText : {
		FString(TEXT("\"solver_id\"")),
		FString(TEXT("compact_pose_index")),
		FString(TEXT("clavicle_l")),
		FString(TEXT("clavicle_r")),
		FString(TEXT("spine_03"))
	})
	{
		TestFalse(
			*FString::Printf(
				TEXT("Schema omits engine implementation detail: %s"),
				*RestrictedText
			),
			ResourceJson.Contains(
				RestrictedText,
				ESearchCase::IgnoreCase
			)
		);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN5AuthoringPromptTest,
	"LLMNPCActionLayer.ForwardN5.Editor.AuthoringPrompt",
	ForwardN5EditorTestFlags
)

bool FLLMNPCForwardN5AuthoringPromptTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	FLLMNPCSkeletonCapabilitySnapshot Capability;
	FString Error;
	TestTrue(
		*FString::Printf(
			TEXT("Manny Capability builds: %s"),
			*Error
		),
		BuildForwardN5Capability(Capability, Error)
	);
	if (Capability.CapabilityHash.IsEmpty())
	{
		return false;
	}

	const ULLMNPCMotionTemplate* Nod =
		LoadObject<ULLMNPCMotionTemplate>(
			nullptr,
			TEXT("/LLMNPCActionLayer/LLMNPC/MotionTemplates/Manny/MT_Nod_Manny_v1.MT_Nod_Manny_v1")
		);
	TArray<const ULLMNPCMotionTemplate*> Examples;
	if (Nod)
	{
		Examples.Add(Nod);
	}
	FLLMNPCMotionRecipePromptPackage Prompt;
	TestTrue(
		*FString::Printf(
			TEXT("Authoring Prompt builds: %s"),
			*Error
		),
		FLLMNPCMotionRecipeAuthoringPrompt::Build(
			TEXT("Express uncertainty with a bilateral shrug and relaxed hands."),
			Capability,
			Examples,
			Prompt,
			Error
		)
	);
	TestTrue(
		TEXT("Prompt pins the capability hash"),
		Prompt.UserJson.Contains(Capability.CapabilityHash)
	);
	TestTrue(
		TEXT("Prompt exposes shoulder.shrug"),
		Prompt.RecipeSchemaJson.Contains(TEXT("shoulder.shrug"))
	);
	for (const FString& RestrictedText : {
		FString(TEXT("clavicle_l")),
		FString(TEXT("clavicle_r")),
		FString(TEXT("spine_03")),
		FString(TEXT("compact_pose_index")),
		FString(TEXT("\"solver_id\""))
	})
	{
		TestFalse(
			*FString::Printf(
				TEXT("Model request omits restricted text: %s"),
				*RestrictedText
			),
			Prompt.UserJson.Contains(
				RestrictedText,
				ESearchCase::IgnoreCase
			)
		);
	}
	TestFalse(
		TEXT("Recipe Authoring does not reuse model_turn.v3"),
		Prompt.SystemPrompt.Contains(TEXT("model_turn"))
	);

	const FString ResponseJson =
		BuildForwardN5Response(ValidShrugRecipe);
	FLLMNPCMotionRecipeAuthoringResponse Response;
	TestTrue(
		*FString::Printf(
			TEXT("Strict Authoring response parses: %s"),
			*Error
		),
		FLLMNPCMotionRecipeAuthoringPrompt::ParseResponse(
			ResponseJson,
			Response,
			Error
		)
	);
	TestTrue(
		TEXT("Parsed response carries the Recipe"),
		Response.RecipeJson.Contains(TEXT("shoulder.shrug"))
	);
	TestTrue(
		*FString::Printf(
			TEXT("Valid Shrug Recipe passes the capability gate: %s"),
			*Error
		),
		FLLMNPCMotionRecipeAuthoringPrompt::
			ValidateRecipeForCapability(
				Response,
				Capability,
				{TEXT("shoulder.shrug")},
				TEXT("express_uncertainty"),
				1,
				Error
			)
	);
	const FString HoldPrimitiveResponse = ResponseJson.Replace(
		TEXT("\"primitive_id\": \"shoulder.shrug\""),
		TEXT("\"primitive_id\": \"hold\"")
	);
	FLLMNPCMotionRecipeAuthoringResponse HoldResponse;
	TestTrue(
		TEXT("The outer response contract can isolate an invalid Recipe"),
		FLLMNPCMotionRecipeAuthoringPrompt::ParseResponse(
			HoldPrimitiveResponse,
			HoldResponse,
			Error
		)
	);
	TestFalse(
		TEXT("The capability gate rejects a fabricated hold primitive"),
		FLLMNPCMotionRecipeAuthoringPrompt::
			ValidateRecipeForCapability(
				HoldResponse,
				Capability,
				{TEXT("shoulder.shrug")},
				TEXT("express_uncertainty"),
				1,
				Error
			)
	);
	TestTrue(
		TEXT("The hold rejection names the invalid primitive"),
		Error.Contains(TEXT("hold"))
	);
	const FString UnknownFieldResponse = ResponseJson.Replace(
		TEXT("\"status\":\"recipe\","),
		TEXT("\"status\":\"recipe\",\"solver_id\":\"forbidden\",")
	);
	TestFalse(
		TEXT("Unknown Authoring response fields are rejected"),
		FLLMNPCMotionRecipeAuthoringPrompt::ParseResponse(
			UnknownFieldResponse,
			Response,
			Error
		)
	);
	TestTrue(
		TEXT("The Authoring protocol supports an explicit unsupported result"),
		FLLMNPCMotionRecipeAuthoringPrompt::ParseResponse(
			TEXT("{\"schema_version\":\"llmnpc.motion_recipe_authoring_response.v1\",\"status\":\"unsupported\",\"reason\":\"The capability cannot express the requested action.\"}"),
			Response,
			Error
		) &&
		Response.bUnsupported
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN5RecipeDraftQualityTest,
	"LLMNPCActionLayer.ForwardN5.Editor.RecipeDraftQuality",
	ForwardN5EditorTestFlags
)

bool FLLMNPCForwardN5RecipeDraftQualityTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	AddExpectedError(
		TEXT("package was marked as deleted in editor"),
		EAutomationExpectedErrorFlags::Contains,
		2
	);
	DeleteForwardN5AutomationAssets();

	FLLMNPCSkeletonCapabilitySnapshot Capability;
	FString Error;
	if (!BuildForwardN5Capability(Capability, Error))
	{
		AddError(Error);
		return false;
	}
	FLLMNPCMotionRecipePromptPackage Prompt;
	TestTrue(
		TEXT("The automation Authoring Prompt builds"),
		FLLMNPCMotionRecipeAuthoringPrompt::Build(
			TEXT("Express uncertainty with a bilateral shrug and relaxed hands."),
			Capability,
			{},
			Prompt,
			Error
		)
	);
	if (Prompt.PromptHash.IsEmpty())
	{
		return false;
	}

	const FString ResponseJson =
		BuildForwardN5Response(ValidShrugRecipe);
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
	Evidence.RawResponseJson = ResponseJson;
	Evidence.GeneratedAtUtc = FDateTime::UtcNow();
	Evidence.HttpStatus = 200;
	Evidence.AttemptCount = 1;
	Evidence.TotalLatencySeconds = 0.25f;
	Evidence.PromptTokens = 120;
	Evidence.CompletionTokens = 80;
	Evidence.TotalTokens = 200;

	ULLMNPCTemplateAuthoringSubsystem* Authoring =
		NewObject<ULLMNPCTemplateAuthoringSubsystem>();
	const FLLMNPCAuthoringOperationResult Draft =
		Authoring->CreateMotionRecipeDraft(
			ValidShrugRecipe,
			TEXT("ue5_manny.v1"),
			BuildForwardN5CatalogSpec(),
			Evidence,
			TEXT("/Game/LLMNPCAutomation/ForwardN5"),
			TEXT("/Game/LLMNPCAutomation/ForwardN5")
		);
	TestTrue(
		*FString::Printf(
			TEXT("Recipe creates a Generated Draft: %s"),
			*Draft.Message
		),
		Draft.bSuccess
	);
	TestNotNull(
		TEXT("Generated Motion Template is returned"),
		Draft.TemplateAsset.Get()
	);
	TestNotNull(
		TEXT("Generated Public Action draft is returned"),
		Draft.PublicActionAsset.Get()
	);
	if (!Draft.TemplateAsset || !Draft.PublicActionAsset)
	{
		DeleteForwardN5AutomationAssets();
		return false;
	}
	TestEqual(
		TEXT("Template remains Generated"),
		Draft.TemplateAsset->Metadata.ReviewState,
		ELLMNPCTemplateReviewState::Generated
	);
	TestEqual(
		TEXT("Public Action remains Generated"),
		Draft.PublicActionAsset->ReviewState,
		ELLMNPCTemplateReviewState::Generated
	);
	TestTrue(
		TEXT("Recipe provenance records the online source"),
		Draft.TemplateAsset->SourceProvenanceJson.Contains(
			TEXT("\"source_type\": \"motion_recipe\"")
		)
	);

	FString JobPath;
	TestTrue(
		TEXT("Recipe provenance resolves the sanitized Job"),
		ReadDraftSourcePath(
			Draft.TemplateAsset->SourceProvenanceJson,
			JobPath
		) &&
		FPaths::FileExists(JobPath)
	);
	FString JobJson;
	if (FFileHelper::LoadFileToString(JobJson, *JobPath))
	{
		TestFalse(
			TEXT("Sanitized Job does not contain an API key field"),
			JobJson.Contains(
				TEXT("OPENAI_API_KEY"),
				ESearchCase::IgnoreCase
			)
		);
		TestFalse(
			TEXT("Sanitized Job does not contain an Authorization header"),
			JobJson.Contains(
				TEXT("Authorization"),
				ESearchCase::IgnoreCase
			)
		);
	}

	const FLLMNPCAuthoringOperationResult Quality =
		Authoring->GenerateQualityReport(
			Draft.TemplateAsset,
			TEXT("")
		);
	TestTrue(
		*FString::Printf(
			TEXT("Recipe quality passes without reconstruction input: %s"),
			*Quality.Message
		),
		Quality.bSuccess
	);
	TestTrue(
		TEXT("Quality report records deterministic Recipe recompilation"),
		Draft.TemplateAsset->ValidationReportJson.Contains(
			TEXT("motion_recipe_recompile")
		)
	);
	FLLMMotionPlan PreviewPlan;
	Error.Reset();
	TestTrue(
		*FString::Printf(
			TEXT("A passing Recipe Draft compiles for PIE preview: %s"),
			*Error
		),
		ULLMNPCTemplateAuthoringSubsystem::CompileTemplateForPreview(
			*Draft.TemplateAsset,
			PreviewPlan,
			Error
		)
	);
	TestFalse(
		TEXT("The compiled preview contains procedural tracks"),
		PreviewPlan.Clip.Tracks.IsEmpty()
	);

	Draft.TemplateAsset->ProceduralClip.Duration += 0.05f;
	const FLLMNPCAuthoringOperationResult TamperedQuality =
		Authoring->GenerateQualityReport(
			Draft.TemplateAsset,
			TEXT("")
		);
	TestFalse(
		TEXT("Quality rejects a hand-tampered compiled Clip"),
		TamperedQuality.bSuccess
	);
	TestTrue(
		TEXT("Tampered Clip failure is explicit"),
		Draft.TemplateAsset->ValidationReportJson.Contains(
			TEXT("LLMNPC_RECIPE_QUALITY_COMPILED_CLIP_MISMATCH")
		)
	);

	if (!Quality.OutputPath.IsEmpty())
	{
		IFileManager::Get().Delete(*Quality.OutputPath);
	}
	if (!JobPath.IsEmpty())
	{
		IFileManager::Get().Delete(*JobPath);
	}
	DeleteForwardN5AutomationAssets();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN5VocabularyTest,
	"LLMNPCActionLayer.ForwardN5.Editor.Vocabulary",
	ForwardN5EditorTestFlags
)

bool FLLMNPCForwardN5VocabularyTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	ULLMNPCActionVocabulary* Vocabulary =
		Settings
			? Settings->ActionVocabulary.LoadSynchronous()
			: nullptr;
	TestNotNull(TEXT("Action Vocabulary loads"), Vocabulary);
	if (!Vocabulary)
	{
		return false;
	}
	FString Error;
	TestTrue(
		*FString::Printf(TEXT("Action Vocabulary validates: %s"), *Error),
		Vocabulary->ValidateVocabulary(Error)
	);
	TestTrue(
		TEXT("shrug is a legal Gesture Family"),
		Vocabulary->IsTagAllowed(
			TEXT("shrug"),
			ELLMNPCActionVocabularyField::GestureFamily
		)
	);
	TestTrue(
		TEXT("express_uncertainty is a legal semantic effect"),
		Vocabulary->IsTagAllowed(
			TEXT("express_uncertainty"),
			ELLMNPCActionVocabularyField::SemanticEffect
		)
	);
	TestTrue(
		TEXT("shoulders is a legal body region"),
		Vocabulary->IsTagAllowed(
			TEXT("shoulders"),
			ELLMNPCActionVocabularyField::BodyRegion
		)
	);
	return true;
}

#endif
