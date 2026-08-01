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
constexpr uint32 ForwardN7DEditorTestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;

const TCHAR* ForwardN7DMannyProfilePath =
	TEXT("/LLMNPCActionLayer/LLMNPC/SkeletonProfiles/SP_UE5_Manny_v1.SP_UE5_Manny_v1");

const FString ForwardN7DPresentRecipe = TEXT(R"JSON(
{
  "schema_version": "llmnpc.motion_recipe.v1",
  "recipe_id": "present_online_automation",
  "intent": "indicate",
  "duration": 1.6,
  "interruptible": true,
  "primitives": [
    {
      "primitive_id": "arm.present",
      "side": "right",
      "start": 0.0,
      "end": 1.6,
      "target_slot": "primary",
      "parameters": {
        "amplitude": 0.65,
        "height": 0.55
      }
    }
  ]
}
)JSON");

bool BuildForwardN7DEditorCapability(
	FLLMNPCSkeletonCapabilitySnapshot& OutCapability,
	FString& OutError
)
{
	ULLMNPCSkeletonProfile* Profile =
		LoadObject<ULLMNPCSkeletonProfile>(
			nullptr,
			ForwardN7DMannyProfilePath
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

FString BuildForwardN7DResponse(const FString& RecipeJson)
{
	return FString::Printf(
		TEXT(
			"{"
			"\"schema_version\":\"llmnpc.motion_recipe_authoring_response.v1\","
			"\"status\":\"recipe\","
			"\"recipe\":%s,"
			"\"catalog_draft\":{"
			"\"display_name\":\"Helpful Open-Palm Present\","
			"\"selection_summary\":\"Present the primary scene target with one helpful open palm.\","
			"\"visual_description\":\"The right arm extends toward the target with a comfortably bent elbow, an upward open palm, naturally extended fingers, and a smooth recovery.\","
			"\"suitable_when\":[\"introducing a person or object\",\"guiding attention toward a target\"],"
			"\"avoid_when\":[\"the selected hand is occupied\",\"no presentation target is registered\"]"
			"}"
			"}"
		),
		*RecipeJson
	);
}

FLLMNPCMotionRecipeDraftCatalogSpec BuildForwardN7DCatalogSpec()
{
	FLLMNPCMotionRecipeDraftCatalogSpec Spec;
	Spec.AssetName = TEXT("MT_ForwardN7D_Present_Automation");
	Spec.TemplateId =
		TEXT("gesture.present.manny.generated.automation");
	Spec.PublicActionId =
		TEXT("gesture.present.automation");
	Spec.PublicActionAssetName =
		TEXT("PA_ForwardN7D_Present_Automation");
	Spec.DisplayName = TEXT("Helpful Open-Palm Present");
	Spec.SelectionSummary =
		TEXT("Present the primary scene target with one helpful open palm.");
	Spec.VisualDescription =
		TEXT("The right arm extends toward the target with a comfortably bent elbow, an upward open palm, naturally extended fingers, and a smooth recovery.");
	Spec.SuitableWhen = {
		TEXT("introducing a person or object"),
		TEXT("guiding attention toward a target")
	};
	Spec.AvoidWhen = {
		TEXT("the selected hand is occupied"),
		TEXT("no presentation target is registered")
	};
	Spec.IntentTags = {TEXT("indicate")};
	Spec.EmotionTags = {TEXT("helpful")};
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
	Spec.SpatialRequirementTags = {TEXT("target_required")};
	Spec.SemanticEffectTags = {
		TEXT("indicate"),
		TEXT("direct_attention")
	};
	Spec.TargetCategoryTags = {TEXT("scene_target")};
	Spec.GestureFamily = TEXT("point");
	Spec.DefaultStyle = TEXT("friendly");
	Spec.SearchKeywords = {
		TEXT("present"),
		TEXT("open palm"),
		TEXT("introduce"),
		TEXT("show")
	};
	Spec.bCanRunWhileMoving = true;
	Spec.Expressiveness = 0.62f;
	Spec.Energy = 0.46f;
	Spec.SocialIntensity = 0.65f;
	return Spec;
}

void DeleteForwardN7DAutomationAssets()
{
	for (const FString& AssetPath : {
		FString(
			TEXT("/Game/LLMNPCAutomation/ForwardN7D/MT_ForwardN7D_Present_Automation.MT_ForwardN7D_Present_Automation")
		),
		FString(
			TEXT("/Game/LLMNPCAutomation/ForwardN7D/PA_ForwardN7D_Present_Automation.PA_ForwardN7D_Present_Automation")
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
			TEXT("MT_ForwardN7D_Present_Automation_online_*.json")
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
	FLLMNPCForwardN7DPresentAuthoringContractTest,
	"LLMNPCActionLayer.ForwardN7D.Editor.PresentAuthoringContract",
	ForwardN7DEditorTestFlags
)

bool FLLMNPCForwardN7DPresentAuthoringContractTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	FLLMNPCSkeletonCapabilitySnapshot Capability;
	FString Error;
	if (!BuildForwardN7DEditorCapability(Capability, Error))
	{
		AddError(Error);
		return false;
	}

	const FLLMNPCMotionRecipeAuthoringContract* Contract =
		FLLMNPCMotionRecipeAuthoringPrompt::FindContract(
			LLMNPCMotionRecipeAuthoring::
				ProceduralPresentAuthoringContractId
		);
	TestNotNull(
		TEXT("The Procedural Present contract is registered"),
		Contract
	);
	TestTrue(
		TEXT("gesture.present resolves to the Present contract"),
		FLLMNPCMotionRecipeAuthoringPrompt::
			FindContractForPublicAction(TEXT("gesture.present")) == Contract
	);
	if (!Contract)
	{
		return false;
	}
	TestTrue(
		TEXT("The Present contract requires only the primary target"),
		Contract->bTargetRequired &&
			Contract->AllowedTargetSlots.Num() == 1 &&
			Contract->AllowedTargetSlots.Contains(TEXT("primary"))
	);
	TestTrue(
		TEXT("Published Present variants may mirror to the free hand"),
		Contract->bAllowMirror
	);

	FLLMNPCMotionRecipeRequestContext RequestContext;
	RequestContext.AuthoringContractId = Contract->ContractId;
	FLLMNPCMotionRecipePromptPackage Prompt;
	TestTrue(
		*FString::Printf(
			TEXT("The Present Authoring Prompt builds: %s"),
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
		TEXT("The Prompt exposes the semantic Present contract"),
		Prompt.UserJson.Contains(TEXT("arm.present")) &&
			Prompt.UserJson.Contains(TEXT("target_required")) &&
			Prompt.UserJson.Contains(TEXT("primary"))
	);
	TestFalse(
		TEXT("The Prompt hides the private palm-up control"),
		Prompt.UserJson.Contains(TEXT("right_hand.palm_up")) ||
			Prompt.RecipeSchemaJson.Contains(TEXT("right_hand.palm_up"))
	);

	FLLMNPCMotionRecipeAuthoringResponse Response;
	TestTrue(
		TEXT("A valid Present response parses"),
		FLLMNPCMotionRecipeAuthoringPrompt::ParseResponse(
			BuildForwardN7DResponse(ForwardN7DPresentRecipe),
			Response,
			Error
		)
	);
	TestTrue(
		*FString::Printf(
			TEXT("The Present Recipe satisfies the contract: %s"),
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

	const FString MissingTargetRecipe =
		ForwardN7DPresentRecipe.Replace(
			TEXT("      \"target_slot\": \"primary\",\n"),
			TEXT("")
		);
	FLLMNPCMotionRecipeAuthoringResponse MissingTarget;
	TestTrue(
		TEXT("The structurally valid missing-target response parses"),
		FLLMNPCMotionRecipeAuthoringPrompt::ParseResponse(
			BuildForwardN7DResponse(MissingTargetRecipe),
			MissingTarget,
			Error
		)
	);
	TestFalse(
		TEXT("The Present contract rejects a missing target slot"),
		FLLMNPCMotionRecipeAuthoringPrompt::
			ValidateRecipeForCapability(
				MissingTarget,
				Capability,
				*Contract,
				Error
			)
	);
	TestTrue(
		TEXT("The missing-target rejection is explicit"),
		Error.Contains(TEXT("TARGET_SLOT_REQUIRED"))
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN7DRecipeDraftQualityTest,
	"LLMNPCActionLayer.ForwardN7D.Editor.RecipeDraftTargetQuality",
	ForwardN7DEditorTestFlags
)

bool FLLMNPCForwardN7DRecipeDraftQualityTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	AddExpectedError(
		TEXT("package was marked as deleted in editor"),
		EAutomationExpectedErrorFlags::Contains,
		2
	);
	DeleteForwardN7DAutomationAssets();

	FLLMNPCSkeletonCapabilitySnapshot Capability;
	FString Error;
	if (!BuildForwardN7DEditorCapability(Capability, Error))
	{
		AddError(Error);
		return false;
	}
	FLLMNPCMotionRecipeRequestContext RequestContext;
	RequestContext.AuthoringContractId =
		LLMNPCMotionRecipeAuthoring::
			ProceduralPresentAuthoringContractId;
	FLLMNPCMotionRecipePromptPackage Prompt;
	TestTrue(
		TEXT("The Present Authoring Prompt builds"),
		FLLMNPCMotionRecipeAuthoringPrompt::Build(
			TEXT("Present the primary target with a friendly right open palm."),
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
		BuildForwardN7DResponse(ForwardN7DPresentRecipe);
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
			ForwardN7DMannyProfilePath
		);
	TestNotNull(
		TEXT("Manny Profile loads for the Sandbox-to-Draft gate"),
		Profile
	);
	if (!Profile)
	{
		DeleteForwardN7DAutomationAssets();
		return false;
	}
	FLLMNPCAuthoringSandboxRequest SandboxRequest;
	SandboxRequest.RecipeJson = ForwardN7DPresentRecipe;
	SandboxRequest.SkeletonProfile = Profile;
	SandboxRequest.TargetBindings.Add(
		TEXT("primary"),
		FLLMNPCAuthoringSandbox::BuildCanonicalTargetRef(
			TEXT("primary")
		)
	);
	const FLLMNPCAuthoringSandboxPreflightResult SandboxPreflight =
		FLLMNPCAuthoringSandbox::RunFullPreflight(SandboxRequest);
	TestTrue(
		*FString::Printf(
			TEXT("The Present Sandbox preflight passes: %s"),
			*SandboxPreflight.ErrorMessage
		),
		SandboxPreflight.bPassed
	);
	if (!SandboxPreflight.bPassed)
	{
		DeleteForwardN7DAutomationAssets();
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
			ForwardN7DPresentRecipe,
			TEXT("ue5_manny.v1"),
			BuildForwardN7DCatalogSpec(),
			Evidence,
			TEXT("/Game/LLMNPCAutomation/ForwardN7D"),
			TEXT("/Game/LLMNPCAutomation/ForwardN7D")
		);
	TestTrue(
		*FString::Printf(
			TEXT("Present creates a Generated Draft: %s"),
			*Draft.Message
		),
		Draft.bSuccess
	);
	TestNotNull(TEXT("Generated Present Template is returned"), Draft.TemplateAsset.Get());
	TestNotNull(TEXT("Generated Present Public Action is returned"), Draft.PublicActionAsset.Get());
	if (!Draft.TemplateAsset || !Draft.PublicActionAsset)
	{
		DeleteForwardN7DAutomationAssets();
		return false;
	}

	TestTrue(
		TEXT("The Draft preserves the target and mirror contracts"),
		Draft.TemplateAsset->Metadata.bRequiresTarget &&
			Draft.PublicActionAsset->bRequiresTarget &&
			Draft.TemplateAsset->ModifierPolicy.bAllowMirror &&
			Draft.TemplateAsset->ModifierPolicy.bEnableDynamicTargetTracking
	);
	TestTrue(
		TEXT("The Draft contains explicit palm-up and open-hand controls"),
		Draft.TemplateAsset->ProceduralClip.Tracks.ContainsByPredicate(
			[](const FLLMMotionTrack& Track)
			{
				return Track.ControlId == TEXT("right_hand.palm_up");
			}
		) &&
			Draft.TemplateAsset->ProceduralClip.Tracks.ContainsByPredicate(
				[](const FLLMMotionTrack& Track)
				{
					return Track.ControlId == TEXT("right_fingers.open");
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
		TestEqual(
			TEXT("The stored Draft uses only the primary placeholder"),
			Track.TargetRef,
			FString(TEXT("primary"))
		);
	}

	const FLLMNPCAuthoringOperationResult Quality =
		Authoring->GenerateQualityReport(Draft.TemplateAsset, TEXT(""));
	TestTrue(
		*FString::Printf(
			TEXT("Present deterministic recompile passes Quality: %s"),
			*Quality.Message
		),
		Quality.bSuccess
	);
	FLLMMotionPlan PreviewPlan;
	Error.Reset();
	TestTrue(
		*FString::Printf(
			TEXT("The Present Draft compiles for preview: %s"),
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
		TestEqual(
			TEXT("Preview replaces every semantic placeholder"),
			Track.TargetRef,
			FString(TEXT("authoring_preview_target"))
		);
	}

	if (!Quality.OutputPath.IsEmpty())
	{
		IFileManager::Get().Delete(*Quality.OutputPath);
	}
	DeleteForwardN7DAutomationAssets();
	return true;
}

#endif
