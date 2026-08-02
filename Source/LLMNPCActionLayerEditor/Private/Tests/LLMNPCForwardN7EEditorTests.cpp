#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Authoring/LLMNPCMotionRecipeAuthoringPrompt.h"
#include "Authoring/LLMNPCTemplateAuthoringSubsystem.h"
#include "Capabilities/LLMNPCSkeletonCapabilityBuilder.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "Sandbox/LLMNPCAuthoringSandbox.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "Templates/LLMNPCPublicActionDefinition.h"

namespace
{
constexpr uint32 ForwardN7EEditorTestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;

const TCHAR* ForwardN7EMannyProfilePath =
	TEXT("/LLMNPCActionLayer/LLMNPC/SkeletonProfiles/SP_UE5_Manny_v1.SP_UE5_Manny_v1");

const FString ForwardN7EThumbsUpRecipe = TEXT(R"JSON(
{
  "schema_version": "llmnpc.motion_recipe.v1",
  "recipe_id": "thumbs_up_online_automation",
  "intent": "agree",
  "duration": 1.6,
  "interruptible": true,
  "primitives": [
    {
      "primitive_id": "hand.thumbs_up",
      "side": "right",
      "start": 0.0,
      "end": 1.6,
      "parameters": {
        "amplitude": 0.65,
        "height": 0.55
      }
    }
  ]
}
)JSON");

bool BuildForwardN7EEditorCapability(
	FLLMNPCSkeletonCapabilitySnapshot& OutCapability,
	FString& OutError
)
{
	ULLMNPCSkeletonProfile* Profile =
		LoadObject<ULLMNPCSkeletonProfile>(
			nullptr,
			ForwardN7EMannyProfilePath
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

FString BuildForwardN7EResponse(const FString& RecipeJson)
{
	return FString::Printf(
		TEXT(
			"{"
			"\"schema_version\":\"llmnpc.motion_recipe_authoring_response.v1\","
			"\"status\":\"recipe\","
			"\"recipe\":%s,"
			"\"catalog_draft\":{"
			"\"display_name\":\"Friendly Thumbs Up\","
			"\"selection_summary\":\"Give one clear thumbs-up to signal friendly approval or agreement.\","
			"\"visual_description\":\"The right hand rises near the upper chest with a bent elbow, outward-facing palm, upright thumb, naturally curled fingers, readable hold, and smooth recovery.\","
			"\"suitable_when\":[\"agreeing with a suggestion\",\"showing approval or encouragement\"],"
			"\"avoid_when\":[\"the selected hand is occupied\",\"the gesture would be culturally inappropriate\"]"
			"}"
			"}"
		),
		*RecipeJson
	);
}

FLLMNPCMotionRecipeDraftCatalogSpec BuildForwardN7ECatalogSpec()
{
	FLLMNPCMotionRecipeDraftCatalogSpec Spec;
	Spec.AssetName = TEXT("MT_ForwardN7E_ThumbsUp_Automation");
	Spec.TemplateId =
		TEXT("gesture.thumbs_up.manny.generated.automation");
	Spec.PublicActionId =
		TEXT("gesture.thumbs_up.automation");
	Spec.PublicActionAssetName =
		TEXT("PA_ForwardN7E_ThumbsUp_Automation");
	Spec.DisplayName = TEXT("Friendly Thumbs Up");
	Spec.SelectionSummary =
		TEXT("Give one clear thumbs-up to signal friendly approval or agreement.");
	Spec.VisualDescription =
		TEXT("The right hand rises near the upper chest with a bent elbow, outward-facing palm, upright thumb, naturally curled fingers, readable hold, and smooth recovery.");
	Spec.SuitableWhen = {
		TEXT("agreeing with a suggestion"),
		TEXT("showing approval or encouragement")
	};
	Spec.AvoidWhen = {
		TEXT("the selected hand is occupied"),
		TEXT("the gesture would be culturally inappropriate")
	};
	Spec.IntentTags = {TEXT("agree")};
	Spec.EmotionTags = {TEXT("friendly")};
	Spec.VariantStyleTags = {
		TEXT("neutral"),
		TEXT("friendly"),
		TEXT("subtle")
	};
	Spec.BodyRegionTags = {
		TEXT("one_arm"),
		TEXT("hand"),
		TEXT("fingers")
	};
	Spec.SemanticEffectTags = {
		TEXT("agree"),
		TEXT("affirm")
	};
	Spec.GestureFamily = TEXT("thumbs_up");
	Spec.DefaultStyle = TEXT("friendly");
	Spec.SearchKeywords = {
		TEXT("thumbs up"),
		TEXT("approve"),
		TEXT("agreement"),
		TEXT("good")
	};
	Spec.bCanRunWhileMoving = true;
	Spec.Expressiveness = 0.62f;
	Spec.Energy = 0.48f;
	Spec.SocialIntensity = 0.68f;
	return Spec;
}

void DeleteForwardN7EAutomationAssets()
{
	for (const FString& AssetPath : {
		FString(
			TEXT("/Game/LLMNPCAutomation/ForwardN7E/MT_ForwardN7E_ThumbsUp_Automation.MT_ForwardN7E_ThumbsUp_Automation")
		),
		FString(
			TEXT("/Game/LLMNPCAutomation/ForwardN7E/PA_ForwardN7E_ThumbsUp_Automation.PA_ForwardN7E_ThumbsUp_Automation")
		)
	})
	{
		const FString PackageName =
			FPackageName::ObjectPathToPackageName(AssetPath);
		if (FPackageName::DoesPackageExist(PackageName))
		{
			if (UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath))
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
			TEXT("MT_ForwardN7E_ThumbsUp_Automation_online_*.json")
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
	FLLMNPCForwardN7EThumbsUpAuthoringContractTest,
	"LLMNPCActionLayer.ForwardN7E.Editor.ThumbsUpAuthoringContract",
	ForwardN7EEditorTestFlags
)

bool FLLMNPCForwardN7EThumbsUpAuthoringContractTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	FLLMNPCSkeletonCapabilitySnapshot Capability;
	FString Error;
	if (!BuildForwardN7EEditorCapability(Capability, Error))
	{
		AddError(Error);
		return false;
	}

	const FLLMNPCMotionRecipeAuthoringContract* Contract =
		FLLMNPCMotionRecipeAuthoringPrompt::FindContract(
			LLMNPCMotionRecipeAuthoring::
				ProceduralThumbsUpAuthoringContractId
		);
	TestNotNull(
		TEXT("The Procedural Thumbs Up contract is registered"),
		Contract
	);
	TestTrue(
		TEXT("gesture.thumbs_up resolves to the Thumbs Up contract"),
		FLLMNPCMotionRecipeAuthoringPrompt::
			FindContractForPublicAction(TEXT("gesture.thumbs_up")) ==
			Contract
	);
	if (!Contract)
	{
		return false;
	}
	TestTrue(
		TEXT("Thumbs Up is target-independent and mirrorable"),
		!Contract->bTargetRequired &&
			Contract->AllowedTargetSlots.IsEmpty() &&
			Contract->bAllowMirror
	);

	FLLMNPCMotionRecipeRequestContext RequestContext;
	RequestContext.AuthoringContractId = Contract->ContractId;
	FLLMNPCMotionRecipePromptPackage Prompt;
	TestTrue(
		*FString::Printf(
			TEXT("The Thumbs Up Authoring Prompt builds: %s"),
			*Error
		),
		FLLMNPCMotionRecipeAuthoringPrompt::Build(
			Contract->DefaultDesiredAction,
			Capability,
			{},
			Prompt,
			Error,
			RequestContext
		)
	);
	TestTrue(
		TEXT("The Prompt exposes one semantic Thumbs Up primitive"),
		Prompt.UserJson.Contains(TEXT("hand.thumbs_up")) &&
			Prompt.UserJson.Contains(TEXT("agreement"))
	);
	TestFalse(
		TEXT("The Prompt hides private finger and bone controls"),
		Prompt.UserJson.Contains(TEXT("right_fingers.thumbs_up")) ||
			Prompt.RecipeSchemaJson.Contains(
				TEXT("right_fingers.thumbs_up")
			) ||
			Prompt.UserJson.Contains(TEXT("thumb_03_r"))
	);

	FLLMNPCMotionRecipeAuthoringResponse Response;
	TestTrue(
		TEXT("A valid Thumbs Up response parses"),
		FLLMNPCMotionRecipeAuthoringPrompt::ParseResponse(
			BuildForwardN7EResponse(ForwardN7EThumbsUpRecipe),
			Response,
			Error
		)
	);
	TestTrue(
		*FString::Printf(
			TEXT("The Thumbs Up Recipe satisfies the contract: %s"),
			*Error
		),
		FLLMNPCMotionRecipeAuthoringPrompt::
			ValidateRecipeForCapability(
				Response,
				Capability,
				*Contract,
				Error
			)
	);

	const FString TargetedRecipe =
		ForwardN7EThumbsUpRecipe.Replace(
			TEXT("      \"end\": 1.6,\n"),
			TEXT("      \"end\": 1.6,\n      \"target_slot\": \"primary\",\n")
		);
	FLLMNPCMotionRecipeAuthoringResponse TargetedResponse;
	TestTrue(
		TEXT("The structurally valid targeted response parses"),
		FLLMNPCMotionRecipeAuthoringPrompt::ParseResponse(
			BuildForwardN7EResponse(TargetedRecipe),
			TargetedResponse,
			Error
		)
	);
	TestFalse(
		TEXT("The Thumbs Up contract rejects a target slot"),
		FLLMNPCMotionRecipeAuthoringPrompt::
			ValidateRecipeForCapability(
				TargetedResponse,
				Capability,
				*Contract,
				Error
			)
	);
	TestTrue(
		TEXT("The target rejection is explicit"),
		Error.Contains(TEXT("TARGET_SLOT"))
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN7ERecipeDraftQualityTest,
	"LLMNPCActionLayer.ForwardN7E.Editor.RecipeDraftQuality",
	ForwardN7EEditorTestFlags
)

bool FLLMNPCForwardN7ERecipeDraftQualityTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	AddExpectedError(
		TEXT("package was marked as deleted in editor"),
		EAutomationExpectedErrorFlags::Contains,
		2
	);
	DeleteForwardN7EAutomationAssets();

	FLLMNPCSkeletonCapabilitySnapshot Capability;
	FString Error;
	if (!BuildForwardN7EEditorCapability(Capability, Error))
	{
		AddError(Error);
		return false;
	}
	FLLMNPCMotionRecipeRequestContext RequestContext;
	RequestContext.AuthoringContractId =
		LLMNPCMotionRecipeAuthoring::
			ProceduralThumbsUpAuthoringContractId;
	FLLMNPCMotionRecipePromptPackage Prompt;
	TestTrue(
		TEXT("The Thumbs Up Authoring Prompt builds"),
		FLLMNPCMotionRecipeAuthoringPrompt::Build(
			TEXT("Give one friendly right-hand thumbs-up."),
			Capability,
			{},
			Prompt,
			Error,
			RequestContext
		)
	);
	if (Prompt.PromptHash.IsEmpty())
	{
		return false;
	}

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
	Evidence.CapabilityModelViewJson = Prompt.CapabilityModelViewJson;
	Evidence.RawResponseJson =
		BuildForwardN7EResponse(ForwardN7EThumbsUpRecipe);
	Evidence.AuthoringContractId =
		Prompt.AuthoringContract.ContractId;
	Evidence.TriggerSource = Prompt.RequestContext.TriggerSource;
	Evidence.GeneratedAtUtc = FDateTime::UtcNow();
	Evidence.HttpStatus = 200;
	Evidence.AttemptCount = 1;
	Evidence.TotalLatencySeconds = 0.25f;
	Evidence.PromptTokens = 140;
	Evidence.CompletionTokens = 90;
	Evidence.TotalTokens = 230;

	ULLMNPCSkeletonProfile* Profile =
		LoadObject<ULLMNPCSkeletonProfile>(
			nullptr,
			ForwardN7EMannyProfilePath
		);
	TestNotNull(
		TEXT("Manny Profile loads for the Sandbox-to-Draft gate"),
		Profile
	);
	if (!Profile)
	{
		DeleteForwardN7EAutomationAssets();
		return false;
	}
	FLLMNPCAuthoringSandboxRequest SandboxRequest;
	SandboxRequest.RecipeJson = ForwardN7EThumbsUpRecipe;
	SandboxRequest.SkeletonProfile = Profile;
	const FLLMNPCAuthoringSandboxPreflightResult SandboxPreflight =
		FLLMNPCAuthoringSandbox::RunFullPreflight(SandboxRequest);
	TestTrue(
		*FString::Printf(
			TEXT("The Thumbs Up Sandbox preflight passes: %s"),
			*SandboxPreflight.ErrorMessage
		),
		SandboxPreflight.bPassed
	);
	if (!SandboxPreflight.bPassed)
	{
		DeleteForwardN7EAutomationAssets();
		return false;
	}
	Evidence.CompiledRecipeHash =
		SandboxPreflight.CompiledMetadata.CompiledRecipeHash;
	Evidence.KinematicReportHash =
		SandboxPreflight.KinematicReport.ReportHash;

	ULLMNPCTemplateAuthoringSubsystem* Authoring =
		NewObject<ULLMNPCTemplateAuthoringSubsystem>();
	const FLLMNPCAuthoringOperationResult Draft =
		Authoring->CreateMotionRecipeDraft(
			ForwardN7EThumbsUpRecipe,
			TEXT("ue5_manny.v1"),
			BuildForwardN7ECatalogSpec(),
			Evidence,
			TEXT("/Game/LLMNPCAutomation/ForwardN7E"),
			TEXT("/Game/LLMNPCAutomation/ForwardN7E")
		);
	TestTrue(
		*FString::Printf(
			TEXT("Thumbs Up creates a Generated Draft: %s"),
			*Draft.Message
		),
		Draft.bSuccess
	);
	TestNotNull(TEXT("Generated Thumbs Up Template is returned"), Draft.TemplateAsset.Get());
	TestNotNull(TEXT("Generated Thumbs Up Public Action is returned"), Draft.PublicActionAsset.Get());
	if (!Draft.TemplateAsset || !Draft.PublicActionAsset)
	{
		DeleteForwardN7EAutomationAssets();
		return false;
	}

	TestTrue(
		TEXT("The Draft remains target-independent and mirrorable"),
		!Draft.TemplateAsset->Metadata.bRequiresTarget &&
			!Draft.PublicActionAsset->bRequiresTarget &&
			Draft.TemplateAsset->ModifierPolicy.bAllowMirror &&
			!Draft.TemplateAsset->ModifierPolicy.bEnableDynamicTargetTracking
	);
	TestTrue(
		TEXT("The Draft contains IK and the dedicated Thumbs Up pose"),
		Draft.TemplateAsset->ProceduralClip.Tracks.ContainsByPredicate(
			[](const FLLMMotionTrack& Track)
			{
				return Track.ControlId == TEXT("right_hand.ik") &&
					Track.Anchor == TEXT("right_thumbs_up");
			}
		) &&
			Draft.TemplateAsset->ProceduralClip.Tracks.ContainsByPredicate(
				[](const FLLMMotionTrack& Track)
				{
					return Track.ControlId ==
						TEXT("right_fingers.thumbs_up");
				}
			)
	);
	TestTrue(
		TEXT("The Draft reserves only right-side execution channels"),
		Draft.TemplateAsset->Metadata.RequiredChannels.Contains(
			TEXT("right_arm_ik")
		) &&
			Draft.TemplateAsset->Metadata.RequiredChannels.Contains(
				TEXT("right_hand_pose")
			) &&
			!Draft.TemplateAsset->Metadata.RequiredChannels.Contains(
				TEXT("left_arm_ik")
			)
	);
	for (const FLLMMotionTrack& Track :
		Draft.TemplateAsset->ProceduralClip.Tracks)
	{
		TestTrue(
			TEXT("The stored Draft contains no target placeholder"),
			Track.TargetRef.IsEmpty()
		);
	}

	const FLLMNPCAuthoringOperationResult Quality =
		Authoring->GenerateQualityReport(Draft.TemplateAsset, TEXT(""));
	TestTrue(
		*FString::Printf(
			TEXT("Thumbs Up deterministic recompile passes Quality: %s"),
			*Quality.Message
		),
		Quality.bSuccess
	);
	FLLMMotionPlan PreviewPlan;
	Error.Reset();
	TestTrue(
		*FString::Printf(
			TEXT("The Thumbs Up Draft compiles for preview: %s"),
			*Error
		),
		ULLMNPCTemplateAuthoringSubsystem::CompileTemplateForPreview(
			*Draft.TemplateAsset,
			PreviewPlan,
			Error
		)
	);
	for (const FLLMMotionTrack& Track : PreviewPlan.Clip.Tracks)
	{
		TestTrue(
			TEXT("Preview stays target-independent"),
			Track.TargetRef.IsEmpty()
		);
	}

	if (!Quality.OutputPath.IsEmpty())
	{
		IFileManager::Get().Delete(*Quality.OutputPath);
	}
	DeleteForwardN7EAutomationAssets();
	return true;
}

#endif
