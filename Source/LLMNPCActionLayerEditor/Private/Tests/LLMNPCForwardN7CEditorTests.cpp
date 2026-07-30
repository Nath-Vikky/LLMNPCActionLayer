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
#include "Sandbox/LLMNPCAuthoringSandbox.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "Templates/LLMNPCPublicActionDefinition.h"

namespace
{
constexpr uint32 ForwardN7CEditorTestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;

const TCHAR* ForwardN7CMannyProfilePath =
	TEXT("/LLMNPCActionLayer/LLMNPC/SkeletonProfiles/SP_UE5_Manny_v1.SP_UE5_Manny_v1");

const FString ForwardN7CBeckonRecipe = TEXT(R"JSON(
{
  "schema_version": "llmnpc.motion_recipe.v1",
  "recipe_id": "beckon_online_automation",
  "intent": "attract_attention",
  "duration": 1.8,
  "interruptible": true,
  "primitives": [
    {
      "primitive_id": "hand.beckon",
      "side": "right",
      "start": 0.0,
      "end": 1.8,
      "target_slot": "primary",
      "parameters": {
        "amplitude": 0.7,
        "speed": 1.0,
        "cycles": 2,
        "curl_amount": 0.72,
        "reach": 0.58,
        "height": 0.55
      }
    }
  ]
}
)JSON");

bool BuildForwardN7CCapability(
	FLLMNPCSkeletonCapabilitySnapshot& OutCapability,
	FString& OutError
)
{
	ULLMNPCSkeletonProfile* Profile =
		LoadObject<ULLMNPCSkeletonProfile>(
			nullptr,
			ForwardN7CMannyProfilePath
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

FString BuildForwardN7CResponse(const FString& RecipeJson)
{
	return FString::Printf(
		TEXT(
			"{"
			"\"schema_version\":\"llmnpc.motion_recipe_authoring_response.v1\","
			"\"status\":\"recipe\","
			"\"recipe\":%s,"
			"\"catalog_draft\":{"
			"\"display_name\":\"Friendly Manny Beckon\","
			"\"selection_summary\":\"Invite the primary scene target closer with one friendly palm-up hand.\","
			"\"visual_description\":\"The right hand reaches toward the target with a constrained palm-up wrist, curls the relaxed fingers inward twice, then returns smoothly to neutral.\","
			"\"suitable_when\":[\"inviting a nearby person closer\",\"directing a companion to approach\"],"
			"\"avoid_when\":[\"the selected hand is occupied\",\"no social target is registered\"]"
			"}"
			"}"
		),
		*RecipeJson
	);
}

FLLMNPCMotionRecipeDraftCatalogSpec BuildForwardN7CCatalogSpec()
{
	FLLMNPCMotionRecipeDraftCatalogSpec Spec;
	Spec.AssetName = TEXT("MT_ForwardN7C_Beckon_Automation");
	Spec.TemplateId =
		TEXT("gesture.beckon.manny.generated.automation");
	Spec.PublicActionId =
		TEXT("gesture.beckon.automation");
	Spec.PublicActionAssetName =
		TEXT("PA_ForwardN7C_Beckon_Automation");
	Spec.DisplayName = TEXT("Friendly Manny Beckon");
	Spec.SelectionSummary =
		TEXT("Invite the primary scene target closer with one friendly palm-up hand.");
	Spec.VisualDescription =
		TEXT("The right hand reaches toward the target with a constrained palm-up wrist, curls the relaxed fingers inward twice, then returns smoothly to neutral.");
	Spec.SuitableWhen = {
		TEXT("inviting a nearby person closer"),
		TEXT("directing a companion to approach")
	};
	Spec.AvoidWhen = {
		TEXT("the selected hand is occupied"),
		TEXT("no social target is registered")
	};
	Spec.IntentTags = {TEXT("attract_attention")};
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
	Spec.SpatialRequirementTags = {TEXT("target_required")};
	Spec.SemanticEffectTags = {
		TEXT("attract_attention"),
		TEXT("direct_attention")
	};
	Spec.TargetCategoryTags = {TEXT("scene_target")};
	Spec.GestureFamily = TEXT("wave");
	Spec.DefaultStyle = TEXT("friendly");
	Spec.SearchKeywords = {
		TEXT("beckon"),
		TEXT("come closer"),
		TEXT("invite"),
		TEXT("approach")
	};
	Spec.bCanRunWhileMoving = true;
	Spec.Expressiveness = 0.68f;
	Spec.Energy = 0.58f;
	Spec.SocialIntensity = 0.72f;
	return Spec;
}

bool ReadForwardN7CDraftSourcePath(
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

void DeleteForwardN7CAutomationAssets()
{
	for (const FString& AssetPath : {
		FString(
			TEXT("/Game/LLMNPCAutomation/ForwardN7C/MT_ForwardN7C_Beckon_Automation.MT_ForwardN7C_Beckon_Automation")
		),
		FString(
			TEXT("/Game/LLMNPCAutomation/ForwardN7C/PA_ForwardN7C_Beckon_Automation.PA_ForwardN7C_Beckon_Automation")
		)
	})
	{
		const FString PackageName =
			FPackageName::ObjectPathToPackageName(AssetPath);
		if (FPackageName::DoesPackageExist(PackageName))
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
			TEXT("MT_ForwardN7C_Beckon_Automation_online_*.json")
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
	FLLMNPCForwardN7CBeckonAuthoringContractTest,
	"LLMNPCActionLayer.ForwardN7C.Editor.BeckonAuthoringContract",
	ForwardN7CEditorTestFlags
)

bool FLLMNPCForwardN7CBeckonAuthoringContractTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	FLLMNPCSkeletonCapabilitySnapshot Capability;
	FString Error;
	if (!BuildForwardN7CCapability(Capability, Error))
	{
		AddError(Error);
		return false;
	}

	const FLLMNPCMotionRecipeAuthoringContract* Contract =
		FLLMNPCMotionRecipeAuthoringPrompt::FindContract(
			LLMNPCMotionRecipeAuthoring::
				ProceduralBeckonAuthoringContractId
		);
	TestNotNull(
		TEXT("The Procedural Beckon contract is registered"),
		Contract
	);
	TestTrue(
		TEXT("gesture.beckon resolves to its contract"),
		FLLMNPCMotionRecipeAuthoringPrompt::
			FindContractForPublicAction(
				TEXT("gesture.beckon")
			) == Contract
	);
	if (!Contract)
	{
		return false;
	}
	TestTrue(
		TEXT("The contract requires only the primary semantic target"),
		Contract->bTargetRequired &&
			Contract->AllowedTargetSlots.Num() == 1 &&
			Contract->AllowedTargetSlots.Contains(
				TEXT("primary")
			)
	);
	TestTrue(
		TEXT("Published Beckon variants may mirror to the free hand"),
		Contract->bAllowMirror
	);

	FLLMNPCMotionRecipeRequestContext RequestContext;
	RequestContext.AuthoringContractId = Contract->ContractId;
	FLLMNPCMotionRecipePromptPackage Prompt;
	TestTrue(
		*FString::Printf(
			TEXT("The target-aware Authoring Prompt builds: %s"),
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
	TestEqual(
		TEXT("The Prompt uses the N7-C protocol"),
		Prompt.PromptVersion,
		FString(LLMNPCMotionRecipeAuthoring::PromptVersion)
	);
	TestTrue(
		TEXT("The Prompt Schema exposes hand.beckon"),
		Prompt.RecipeSchemaJson.Contains(TEXT("hand.beckon"))
	);

	TSharedPtr<FJsonObject> RequestObject;
	const TSharedRef<TJsonReader<>> Reader =
		TJsonReaderFactory<>::Create(Prompt.UserJson);
	TestTrue(
		TEXT("The request evidence is valid JSON"),
		FJsonSerializer::Deserialize(Reader, RequestObject) &&
			RequestObject.IsValid()
	);
	FString TargetContract;
	const TSharedPtr<FJsonObject>* Constraints = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* AllowedTargets =
		nullptr;
	bool bTargetRequired = false;
	const bool bHasTargetContract =
		RequestObject.IsValid() &&
		RequestObject->TryGetStringField(
			TEXT("target_contract"),
			TargetContract
		) &&
		RequestObject->TryGetObjectField(
			TEXT("authoring_constraints"),
			Constraints
		) &&
		Constraints &&
		Constraints->IsValid() &&
		(*Constraints)->TryGetBoolField(
			TEXT("target_required"),
			bTargetRequired
		) &&
		(*Constraints)->TryGetArrayField(
			TEXT("allowed_target_slots"),
			AllowedTargets
		);
	FString AllowedTarget;
	TestTrue(
		TEXT("The structured request exposes only target slot primary"),
		bHasTargetContract &&
			bTargetRequired &&
			TargetContract.Contains(TEXT("primary")) &&
			AllowedTargets &&
			AllowedTargets->Num() == 1 &&
			(*AllowedTargets)[0]->TryGetString(AllowedTarget) &&
			AllowedTarget == TEXT("primary")
	);
	TestTrue(
		TEXT("The System Prompt forbids Actor names and invented targets"),
		Prompt.SystemPrompt.Contains(
			TEXT("semantic target slots")
		) &&
			Prompt.SystemPrompt.Contains(
				TEXT("invented target")
			)
	);

	FLLMNPCMotionRecipeAuthoringResponse Response;
	TestTrue(
		TEXT("The strict Beckon response parses"),
		FLLMNPCMotionRecipeAuthoringPrompt::ParseResponse(
			BuildForwardN7CResponse(
				ForwardN7CBeckonRecipe
			),
			Response,
			Error
		)
	);
	TestTrue(
		*FString::Printf(
			TEXT("The primary-target Recipe passes its contract: %s"),
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

	FLLMNPCMotionRecipeAuthoringResponse UnknownTarget;
	TestTrue(
		TEXT("An unknown-target response still parses structurally"),
		FLLMNPCMotionRecipeAuthoringPrompt::ParseResponse(
			BuildForwardN7CResponse(
				ForwardN7CBeckonRecipe.Replace(
					TEXT("\"target_slot\": \"primary\""),
					TEXT("\"target_slot\": \"secondary\"")
				)
			),
			UnknownTarget,
			Error
		)
	);
	TestFalse(
		TEXT("The contract rejects an invented target slot"),
		FLLMNPCMotionRecipeAuthoringPrompt::
			ValidateRecipeForCapability(
				UnknownTarget,
				Capability,
				*Contract,
				Error
			)
	);
	TestTrue(
		TEXT("The target-slot rejection is explicit"),
		Error.Contains(TEXT("TARGET_SLOT_UNKNOWN"))
	);

	FLLMNPCMotionRecipeAuthoringResponse MissingTarget;
	TestTrue(
		TEXT("A targetless response still parses structurally"),
		FLLMNPCMotionRecipeAuthoringPrompt::ParseResponse(
			BuildForwardN7CResponse(
				ForwardN7CBeckonRecipe.Replace(
					TEXT("      \"target_slot\": \"primary\",\n"),
					TEXT("")
				)
			),
			MissingTarget,
			Error
		)
	);
	TestFalse(
		TEXT("The contract rejects a targetless Beckon"),
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

	const FLLMNPCMotionRecipeAuthoringContract* ClapContract =
		FLLMNPCMotionRecipeAuthoringPrompt::FindContract(
			LLMNPCMotionRecipeAuthoring::
				ProceduralClapAuthoringContractId
		);
	TestTrue(
		TEXT("The legacy Clap contract remains target-independent"),
		ClapContract &&
			!ClapContract->bTargetRequired &&
			ClapContract->AllowedTargetSlots.IsEmpty()
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN7CRecipeDraftQualityTest,
	"LLMNPCActionLayer.ForwardN7C.Editor.RecipeDraftTargetQuality",
	ForwardN7CEditorTestFlags
)

bool FLLMNPCForwardN7CRecipeDraftQualityTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	AddExpectedError(
		TEXT("package was marked as deleted in editor"),
		EAutomationExpectedErrorFlags::Contains,
		2
	);
	DeleteForwardN7CAutomationAssets();

	FLLMNPCSkeletonCapabilitySnapshot Capability;
	FString Error;
	if (!BuildForwardN7CCapability(Capability, Error))
	{
		AddError(Error);
		return false;
	}
	FLLMNPCMotionRecipeRequestContext RequestContext;
	RequestContext.AuthoringContractId =
		LLMNPCMotionRecipeAuthoring::
			ProceduralBeckonAuthoringContractId;
	FLLMNPCMotionRecipePromptPackage Prompt;
	TestTrue(
		TEXT("The Beckon Authoring Prompt builds"),
		FLLMNPCMotionRecipeAuthoringPrompt::Build(
			TEXT("Invite the primary scene target closer with two friendly right-hand beckons."),
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
	Evidence.CapabilityModelViewJson =
		Prompt.CapabilityModelViewJson;
	Evidence.RawResponseJson =
		BuildForwardN7CResponse(ForwardN7CBeckonRecipe);
	Evidence.AuthoringContractId =
		Prompt.AuthoringContract.ContractId;
	Evidence.TriggerSource =
		Prompt.RequestContext.TriggerSource;
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
			ForwardN7CMannyProfilePath
		);
	TestNotNull(
		TEXT("Manny Profile loads for the Sandbox-to-Draft gate"),
		Profile
	);
	if (!Profile)
	{
		DeleteForwardN7CAutomationAssets();
		return false;
	}
	FLLMNPCAuthoringSandboxRequest SandboxRequest;
	SandboxRequest.RecipeJson = ForwardN7CBeckonRecipe;
	SandboxRequest.SkeletonProfile = Profile;
	SandboxRequest.TargetBindings.Add(
		TEXT("primary"),
		FLLMNPCAuthoringSandbox::BuildCanonicalTargetRef(
			TEXT("primary")
		)
	);
	const FLLMNPCAuthoringSandboxPreflightResult SandboxPreflight =
		FLLMNPCAuthoringSandbox::RunFullPreflight(
			SandboxRequest
		);
	TestTrue(
		*FString::Printf(
			TEXT("The canonical targeted Sandbox preflight passes: %s"),
			*SandboxPreflight.ErrorMessage
		),
		SandboxPreflight.bPassed
	);
	if (!SandboxPreflight.bPassed)
	{
		DeleteForwardN7CAutomationAssets();
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
			ForwardN7CBeckonRecipe,
			TEXT("ue5_manny.v1"),
			BuildForwardN7CCatalogSpec(),
			Evidence,
			TEXT("/Game/LLMNPCAutomation/ForwardN7C"),
			TEXT("/Game/LLMNPCAutomation/ForwardN7C")
		);
	TestTrue(
		*FString::Printf(
			TEXT("Targeted Recipe creates a Generated Draft: %s"),
			*Draft.Message
		),
		Draft.bSuccess
	);
	TestNotNull(
		TEXT("Generated Beckon Template is returned"),
		Draft.TemplateAsset.Get()
	);
	TestNotNull(
		TEXT("Generated target-aware Public Action is returned"),
		Draft.PublicActionAsset.Get()
	);
	if (!Draft.TemplateAsset || !Draft.PublicActionAsset)
	{
		DeleteForwardN7CAutomationAssets();
		return false;
	}

	TestTrue(
		TEXT("The Draft and Public Action preserve the target contract"),
		Draft.TemplateAsset->Metadata.bRequiresTarget &&
			Draft.PublicActionAsset->bRequiresTarget &&
			Draft.TemplateAsset->Metadata.TargetCategoryTags.Contains(
				TEXT("scene_target")
			) &&
			Draft.PublicActionAsset->TargetCategoryTags.Contains(
				TEXT("scene_target")
			)
	);
	TestEqual(
		TEXT("The Draft preserves the Sandbox-approved kinematic identity"),
		Draft.TemplateAsset->Metadata.KinematicReportHash,
		SandboxPreflight.KinematicReport.ReportHash
	);
	TestTrue(
		TEXT("Published execution policy enables bounded mirror and dynamic tracking"),
		Draft.TemplateAsset->ModifierPolicy.bAllowMirror &&
			Draft.TemplateAsset->ModifierPolicy.
				bEnableDynamicTargetTracking &&
			Draft.TemplateAsset->ModifierPolicy.
				bEnableObstacleAdaptation &&
			Draft.TemplateAsset->ModifierPolicy.TargetLossPolicy ==
				ELLMNPCTargetLossPolicy::FadeOut
	);
	TestTrue(
		TEXT("Right-hand Beckon blocks its occupied hand but not a free left hand"),
		Draft.TemplateAsset->Metadata.BlockedStates.Contains(
			TEXT("right_hand_busy")
		) &&
			!Draft.TemplateAsset->Metadata.BlockedStates.Contains(
				TEXT("left_hand_busy")
			) &&
			Draft.TemplateAsset->Metadata.BlockedStates.Contains(
				TEXT("two_hand_interaction")
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
			TEXT("The stored Draft uses only the semantic primary placeholder"),
			Track.TargetRef,
			FString(TEXT("primary"))
		);
	}

	const FLLMNPCAuthoringOperationResult Quality =
		Authoring->GenerateQualityReport(
			Draft.TemplateAsset,
			TEXT("")
		);
	TestTrue(
		*FString::Printf(
			TEXT("Target-aware deterministic recompile passes Quality: %s"),
			*Quality.Message
		),
		Quality.bSuccess
	);
	TestTrue(
		TEXT("Quality records deterministic Recipe recompilation"),
		Draft.TemplateAsset->ValidationReportJson.Contains(
			TEXT("motion_recipe_recompile")
		)
	);

	FLLMMotionPlan PreviewPlan;
	Error.Reset();
	TestTrue(
		*FString::Printf(
			TEXT("The targeted Draft compiles for PIE preview: %s"),
			*Error
		),
		ULLMNPCTemplateAuthoringSubsystem::
			CompileTemplateForPreview(
				*Draft.TemplateAsset,
				PreviewPlan,
				Error
			)
	);
	for (const FLLMMotionTrack& Track : PreviewPlan.Clip.Tracks)
	{
		TestEqual(
			TEXT("Preview compilation replaces every semantic placeholder"),
			Track.TargetRef,
			FString(TEXT("authoring_preview_target"))
		);
	}

	FString JobPath;
	TestTrue(
		TEXT("Recipe provenance resolves its sanitized online Job"),
		ReadForwardN7CDraftSourcePath(
			Draft.TemplateAsset->SourceProvenanceJson,
			JobPath
		) &&
			FPaths::FileExists(JobPath)
	);
	if (!Quality.OutputPath.IsEmpty())
	{
		IFileManager::Get().Delete(*Quality.OutputPath);
	}
	if (!JobPath.IsEmpty())
	{
		IFileManager::Get().Delete(*JobPath);
	}
	DeleteForwardN7CAutomationAssets();
	return true;
}

#endif
