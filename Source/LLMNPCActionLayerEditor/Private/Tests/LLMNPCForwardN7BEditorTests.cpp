#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Authoring/LLMNPCMotionRecipeAuthoringPrompt.h"
#include "Authoring/LLMNPCTemplateAuthoringSubsystem.h"
#include "Authoring/LLMNPCUEPIArtifactAdapter.h"
#include "Capabilities/LLMNPCSkeletonCapabilityBuilder.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Interfaces/IPluginManager.h"
#include "LLMNPCMotionComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "MotionRecipe/LLMNPCMotionPrimitiveRegistry.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "Templates/LLMNPCMotionTemplate.h"

namespace
{
constexpr uint32 ForwardN7BEditorTestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;

const FString ForwardN7BClapRecipe = TEXT(R"JSON(
{
  "schema_version": "llmnpc.motion_recipe.v1",
  "recipe_id": "clap_online_automation",
  "intent": "applaud",
  "duration": 1.8,
  "interruptible": true,
  "primitives": [
    {
      "primitive_id": "hands.contact",
      "side": "none",
      "start": 0.0,
      "end": 1.8,
      "parameters": {
        "amplitude": 0.75,
        "speed": 1.0,
        "cycles": 2,
        "contact_height": 0.55,
        "separation": 0.65,
        "palm_openness": 0.9
      }
    }
  ]
}
)JSON");

const FString ForwardN7BShrugRecipe = TEXT(R"JSON(
{
  "schema_version": "llmnpc.motion_recipe.v1",
  "recipe_id": "shrug_wrong_contract",
  "intent": "express_uncertainty",
  "duration": 1.5,
  "interruptible": true,
  "primitives": [
    {
      "primitive_id": "shoulder.shrug",
      "side": "none",
      "start": 0.0,
      "end": 1.5,
      "parameters": {}
    }
  ]
}
)JSON");

bool BuildForwardN7BCapability(
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

FString BuildForwardN7BResponse(const FString& RecipeJson)
{
	return FString::Printf(
		TEXT(
			"{"
			"\"schema_version\":\"llmnpc.motion_recipe_authoring_response.v1\","
			"\"status\":\"recipe\","
			"\"recipe\":%s,"
			"\"catalog_draft\":{"
			"\"display_name\":\"Procedural Manny Clap\","
			"\"selection_summary\":\"Bring both open palms together twice to applaud.\","
			"\"visual_description\":\"Both hands approach in front of the chest, make two readable contacts, separate, and recover to neutral.\","
			"\"suitable_when\":[\"celebrating a success\"],"
			"\"avoid_when\":[\"either hand is occupied\"]"
			"}"
			"}"
		),
		*RecipeJson
	);
}

ULLMNPCMotionTemplate* MakePublishedExample(
	FName TemplateId,
	FName PublicActionId,
	FName ProfileId
)
{
	ULLMNPCMotionTemplate* Template =
		NewObject<ULLMNPCMotionTemplate>();
	Template->Metadata.TemplateId = TemplateId;
	Template->Metadata.PublicActionId = PublicActionId;
	Template->Metadata.SkeletonProfileId = ProfileId;
	Template->Metadata.VariantId = TEXT("automation");
	Template->Metadata.DisplayName =
		FText::FromName(TemplateId);
	Template->Metadata.Description =
		FText::FromString(TEXT("Automation example"));
	Template->Metadata.VisualDescription =
		TEXT("Automation example");
	Template->Metadata.ReviewState =
		ELLMNPCTemplateReviewState::Published;
	return Template;
}

ULLMNPCMotionComponent* LoadMannyMotionComponentTemplate()
{
	UBlueprint* Blueprint = LoadObject<UBlueprint>(
		nullptr,
		TEXT("/Game/LLMNPC/Blueprints/BP_LLMNPC_Manny.BP_LLMNPC_Manny")
	);
	if (!Blueprint || !Blueprint->SimpleConstructionScript)
	{
		return nullptr;
	}
	for (
		USCS_Node* Node :
		Blueprint->SimpleConstructionScript->GetAllNodes()
	)
	{
		if (Node)
		{
			if (
				ULLMNPCMotionComponent* Motion =
					Cast<ULLMNPCMotionComponent>(
						Node->ComponentTemplate
					)
			)
			{
				return Motion;
			}
		}
	}
	return nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN7BArtifactContractTest,
	"LLMNPCActionLayer.ForwardN7B.Editor.ArtifactContract",
	ForwardN7BEditorTestFlags
)

bool FLLMNPCForwardN7BArtifactContractTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	const TSharedPtr<IPlugin> Plugin =
		IPluginManager::Get().FindPlugin(TEXT("LLMNPCActionLayer"));
	TestTrue(TEXT("The plugin directory resolves"), Plugin.IsValid());
	if (!Plugin.IsValid())
	{
		return false;
	}

	FLLMNPCSkeletonCapabilitySnapshot Capability;
	FString Error;
	TestTrue(
		TEXT("The current Manny Capability builds"),
		BuildForwardN7BCapability(Capability, Error)
	);
	FString CapabilityJson;
	TestTrue(
		TEXT("The current Manny model view serializes"),
		FLLMNPCSkeletonCapabilityBuilder::BuildModelViewJson(
			Capability,
			CapabilityJson,
			Error
		)
	);
	FString SchemaJson;
	TestTrue(
		TEXT("The current base Recipe Schema builds"),
		FLLMNPCMotionPrimitiveRegistry::Get().BuildModelSchemaJson(
			nullptr,
			SchemaJson,
			Error
		)
	);
	if (
		CapabilityJson.IsEmpty() ||
		SchemaJson.IsEmpty()
	)
	{
		AddError(Error);
		return false;
	}

	const FString CapabilityPath = FPaths::Combine(
		Plugin->GetBaseDir(),
		TEXT("Resources"),
		TEXT("Capabilities"),
		TEXT("Manny"),
		TEXT("ue5_manny_v1.capability.json")
	);
	const FString SchemaPath = FPaths::Combine(
		Plugin->GetBaseDir(),
		TEXT("Resources"),
		TEXT("Schemas"),
		TEXT("llmnpc_motion_recipe_v1.schema.json")
	);
	if (
		FParse::Param(
			FCommandLine::Get(),
			TEXT("LLMNPCUpdateArtifacts")
		)
	)
	{
		TestTrue(
			TEXT("The Manny Capability artifact is updated"),
			FFileHelper::SaveStringToFile(
				CapabilityJson,
				*CapabilityPath,
				FFileHelper::EEncodingOptions::
					ForceUTF8WithoutBOM
			)
		);
		TestTrue(
			TEXT("The Motion Recipe Schema artifact is updated"),
			FFileHelper::SaveStringToFile(
				SchemaJson,
				*SchemaPath,
				FFileHelper::EEncodingOptions::
					ForceUTF8WithoutBOM
			)
		);
	}

	FString CheckedInCapabilityJson;
	FString CheckedInSchemaJson;
	TestTrue(
		TEXT("The checked-in Manny Capability artifact loads"),
		FFileHelper::LoadFileToString(
			CheckedInCapabilityJson,
			*CapabilityPath
		)
	);
	TestTrue(
		TEXT("The checked-in Recipe Schema artifact loads"),
		FFileHelper::LoadFileToString(
			CheckedInSchemaJson,
			*SchemaPath
		)
	);
	TestEqual(
		TEXT("The checked-in Recipe Schema matches the current Registry"),
		FLLMNPCUEPIArtifactAdapter::HashJson(
			CheckedInSchemaJson
		),
		FLLMNPCUEPIArtifactAdapter::HashJson(SchemaJson)
	);

	TSharedPtr<FJsonObject> CapabilityObject;
	const TSharedRef<TJsonReader<>> CapabilityReader =
		TJsonReaderFactory<>::Create(CheckedInCapabilityJson);
	TestTrue(
		TEXT("The checked-in Manny Capability is valid JSON"),
		FJsonSerializer::Deserialize(
			CapabilityReader,
			CapabilityObject
		) &&
			CapabilityObject.IsValid()
	);
	FString CheckedInCapabilityHash;
	FString CheckedInManifestVersion;
	TestTrue(
		TEXT("The Capability artifact pins current identity"),
		CapabilityObject.IsValid() &&
			CapabilityObject->TryGetStringField(
				TEXT("capability_hash"),
				CheckedInCapabilityHash
			) &&
			CapabilityObject->TryGetStringField(
				TEXT("control_manifest_version"),
				CheckedInManifestVersion
			) &&
			CheckedInCapabilityHash ==
				Capability.CapabilityHash &&
			CheckedInManifestVersion ==
				TEXT("llmnpc.control_manifest.v3")
	);
	TestTrue(
		TEXT("The Capability artifact exposes hands.contact"),
		CheckedInCapabilityJson.Contains(TEXT("hands.contact"))
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN7BMannyBlueprintReadinessTest,
	"LLMNPCActionLayer.ForwardN7B.Editor.MannyBlueprintReadiness",
	ForwardN7BEditorTestFlags
)

bool FLLMNPCForwardN7BMannyBlueprintReadinessTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	ULLMNPCMotionComponent* Motion =
		LoadMannyMotionComponentTemplate();
	TestNotNull(
		TEXT("BP_LLMNPC_Manny contains an LLMNPC Motion Component"),
		Motion
	);
	if (!Motion)
	{
		return false;
	}

	ULLMNPCSkeletonProfile* Profile =
		Motion->SkeletonProfile.LoadSynchronous();
	TestNotNull(
		TEXT("The Manny Blueprint Motion Component resolves its Profile"),
		Profile
	);
	if (!Profile)
	{
		return false;
	}

	FLLMNPCSkeletonCapabilitySnapshot Capability;
	const FLLMNPCSkeletonCapabilityBuildResult BuildResult =
		FLLMNPCSkeletonCapabilityBuilder::Build(
			*Profile,
			Motion->ControlManifest,
			Capability
		);
	TestTrue(
		TEXT("The Manny Blueprint's actual Motion configuration builds"),
		BuildResult.bSucceeded
	);
	TestTrue(
		TEXT("The configured Manny Blueprint exposes hands.contact"),
		Capability.Capabilities.ContainsByPredicate(
			[](const FLLMNPCSemanticCapability& Candidate)
			{
				return Candidate.CapabilityId ==
					TEXT("hands.contact");
			}
		)
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN7BClapAuthoringContractTest,
	"LLMNPCActionLayer.ForwardN7B.Editor.ClapAuthoringContract",
	ForwardN7BEditorTestFlags
)

bool FLLMNPCForwardN7BClapAuthoringContractTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	FLLMNPCSkeletonCapabilitySnapshot Capability;
	FString Error;
	TestTrue(
		TEXT("The Manny Capability builds"),
		BuildForwardN7BCapability(Capability, Error)
	);
	if (!Error.IsEmpty())
	{
		AddError(Error);
		return false;
	}

	const FLLMNPCMotionRecipeAuthoringContract* ClapContract =
		FLLMNPCMotionRecipeAuthoringPrompt::FindContract(
			LLMNPCMotionRecipeAuthoring::
				ProceduralClapAuthoringContractId
		);
	TestNotNull(
		TEXT("The Procedural Clap contract is registered"),
		ClapContract
	);
	TestTrue(
		TEXT("gesture.clap resolves back to the Procedural Clap contract"),
		FLLMNPCMotionRecipeAuthoringPrompt::
			FindContractForPublicAction(TEXT("gesture.clap")) ==
			ClapContract
	);
	if (!ClapContract)
	{
		return false;
	}

	ULLMNPCMotionTemplate* Nod = MakePublishedExample(
		TEXT("gesture.nod.automation"),
		TEXT("gesture.nod"),
		Capability.ProfileId
	);
	ULLMNPCMotionTemplate* AssetClap = MakePublishedExample(
		TEXT("gesture.clap.asset.automation"),
		TEXT("gesture.clap"),
		Capability.ProfileId
	);
	FLLMNPCMotionRecipeRequestContext RequestContext;
	RequestContext.AuthoringContractId =
		ClapContract->ContractId;
	FLLMNPCMotionRecipePromptPackage Prompt;
	TestTrue(
		*FString::Printf(
			TEXT("The Clap Authoring Prompt builds: %s"),
			*Error
		),
		FLLMNPCMotionRecipeAuthoringPrompt::Build(
			ClapContract->DefaultDesiredAction,
			Capability,
			{Nod, AssetClap},
			Prompt,
			Error,
			RequestContext
		)
	);
	TestEqual(
		TEXT("The Prompt records its immutable contract"),
		Prompt.AuthoringContract.ContractId,
		ClapContract->ContractId
	);
	TSharedPtr<FJsonObject> RequestObject;
	const TSharedRef<TJsonReader<>> RequestReader =
		TJsonReaderFactory<>::Create(Prompt.UserJson);
	TestTrue(
		TEXT("The generated request is valid structured JSON"),
		FJsonSerializer::Deserialize(
			RequestReader,
			RequestObject
		) &&
			RequestObject.IsValid()
	);
	const TSharedPtr<FJsonObject>* Constraints = nullptr;
	FString ContractId;
	FString RequiredIntent;
	const TSharedPtr<FJsonObject>* DurationRange = nullptr;
	double MinimumDuration = 0.0;
	double MaximumDuration = 0.0;
	bool bPrimitiveMustCoverRecipe = false;
	const TArray<TSharedPtr<FJsonValue>>* AllowedPrimitives =
		nullptr;
	const bool bHasConstraints =
		RequestObject.IsValid() &&
		RequestObject->TryGetObjectField(
			TEXT("authoring_constraints"),
			Constraints
		) &&
		Constraints &&
		Constraints->IsValid() &&
		(*Constraints)->TryGetStringField(
			TEXT("contract_id"),
			ContractId
		) &&
		(*Constraints)->TryGetStringField(
			TEXT("required_intent"),
			RequiredIntent
		) &&
		(*Constraints)->TryGetObjectField(
			TEXT("duration_range_seconds"),
			DurationRange
		) &&
		DurationRange &&
		DurationRange->IsValid() &&
		(*DurationRange)->TryGetNumberField(
			TEXT("min"),
			MinimumDuration
		) &&
		(*DurationRange)->TryGetNumberField(
			TEXT("max"),
			MaximumDuration
		) &&
		(*Constraints)->TryGetBoolField(
			TEXT("primitive_must_cover_recipe"),
			bPrimitiveMustCoverRecipe
		) &&
		(*Constraints)->TryGetArrayField(
			TEXT("allowed_primitive_ids"),
			AllowedPrimitives
		);
	TestTrue(
		TEXT("The request binds the Procedural Clap contract"),
		bHasConstraints &&
			ContractId == TEXT("gesture.clap.procedural")
	);
	TestTrue(
		TEXT("The request requires the applaud intent"),
		bHasConstraints && RequiredIntent == TEXT("applaud")
	);
	TestTrue(
		TEXT("The request binds duration and full-coverage timing"),
		bHasConstraints &&
			FMath::IsNearlyEqual(MinimumDuration, 0.8) &&
			FMath::IsNearlyEqual(MaximumDuration, 3.2) &&
			bPrimitiveMustCoverRecipe
	);
	FString AllowedPrimitive;
	TestTrue(
		TEXT("The request allows only hands.contact"),
		bHasConstraints &&
			AllowedPrimitives &&
			AllowedPrimitives->Num() == 1 &&
			(*AllowedPrimitives)[0]->TryGetString(
				AllowedPrimitive
			) &&
			AllowedPrimitive == TEXT("hands.contact")
	);
	const TArray<TSharedPtr<FJsonValue>>* Examples = nullptr;
	FString FirstExamplePublicAction;
	FString SecondExamplePublicAction;
	const bool bHasExamples =
		RequestObject.IsValid() &&
		RequestObject->TryGetArrayField(
			TEXT("similar_published_templates"),
			Examples
		) &&
		Examples &&
		Examples->Num() == 2 &&
		(*Examples)[0]->AsObject().IsValid() &&
		(*Examples)[1]->AsObject().IsValid() &&
		(*Examples)[0]->AsObject()->TryGetStringField(
			TEXT("public_action_id"),
			FirstExamplePublicAction
		) &&
		(*Examples)[1]->AsObject()->TryGetStringField(
			TEXT("public_action_id"),
			SecondExamplePublicAction
		);
	TestTrue(
		TEXT("The reviewed AnimationAsset Clap baseline is prioritized"),
		bHasExamples &&
			FirstExamplePublicAction == TEXT("gesture.clap") &&
			SecondExamplePublicAction == TEXT("gesture.nod")
	);

	FLLMNPCMotionRecipeAuthoringResponse ClapResponse;
	TestTrue(
		TEXT("The strict Clap response parses"),
		FLLMNPCMotionRecipeAuthoringPrompt::ParseResponse(
			BuildForwardN7BResponse(ForwardN7BClapRecipe),
			ClapResponse,
			Error
		)
	);
	TestTrue(
		*FString::Printf(
			TEXT("The Clap Recipe passes its contract: %s"),
			*Error
		),
		FLLMNPCMotionRecipeAuthoringPrompt::
			ValidateRecipeForCapability(
				ClapResponse,
				Capability,
				*ClapContract,
				Error
			)
	);

	FLLMNPCMotionRecipeAuthoringResponse PartialClapResponse;
	TestTrue(
		TEXT("A partially timed Clap response remains structurally valid"),
		FLLMNPCMotionRecipeAuthoringPrompt::ParseResponse(
			BuildForwardN7BResponse(
				ForwardN7BClapRecipe.Replace(
					TEXT("\"end\": 1.8"),
					TEXT("\"end\": 1.4")
				)
			),
			PartialClapResponse,
			Error
		)
	);
	TestFalse(
		TEXT("The Clap contract rejects a primitive that does not cover the Recipe"),
		FLLMNPCMotionRecipeAuthoringPrompt::
			ValidateRecipeForCapability(
				PartialClapResponse,
				Capability,
				*ClapContract,
				Error
			)
	);
	TestTrue(
		TEXT("The timing-coverage rejection is explicit"),
		Error.Contains(TEXT("TIMING_COVERAGE_MISMATCH"))
	);

	FLLMNPCMotionRecipeAuthoringResponse ShrugResponse;
	TestTrue(
		TEXT("A structurally valid Shrug response parses"),
		FLLMNPCMotionRecipeAuthoringPrompt::ParseResponse(
			BuildForwardN7BResponse(ForwardN7BShrugRecipe),
			ShrugResponse,
			Error
		)
	);
	TestFalse(
		TEXT("The Clap contract rejects a semantic Shrug"),
		FLLMNPCMotionRecipeAuthoringPrompt::
			ValidateRecipeForCapability(
				ShrugResponse,
				Capability,
				*ClapContract,
				Error
			)
	);
	TestTrue(
		TEXT("The cross-contract rejection is explicit"),
		Error.Contains(TEXT("INTENT_MISMATCH")) ||
			Error.Contains(TEXT("PRIMITIVE_NOT_ALLOWED"))
	);

	RequestContext.AuthoringContractId =
		TEXT("gesture.unknown");
	TestFalse(
		TEXT("An unregistered Authoring contract fails closed"),
		FLLMNPCMotionRecipeAuthoringPrompt::Build(
			TEXT("Invent something"),
			Capability,
			{},
			Prompt,
			Error,
			RequestContext
		)
	);
	TestTrue(
		TEXT("The unknown contract has a stable error"),
		Error.Contains(TEXT("CONTRACT_UNKNOWN"))
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN7BPublishedClapQualityContinuityTest,
	"LLMNPCActionLayer.ForwardN7B.Editor.PublishedClapQualityContinuity",
	ForwardN7BEditorTestFlags
)

bool FLLMNPCForwardN7BPublishedClapQualityContinuityTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	const ULLMNPCMotionTemplate* PublishedClap =
		LoadObject<ULLMNPCMotionTemplate>(
			nullptr,
			TEXT(
				"/Game/LLMNPCActionLayer/MotionTemplates/Published/"
				"MT_Clap_Manny_Asset_v1_Generated."
				"MT_Clap_Manny_Asset_v1_Generated"
			)
		);
	ULLMNPCTemplateAuthoringSubsystem* Authoring =
		GEditor
			? GEditor->GetEditorSubsystem<
				ULLMNPCTemplateAuthoringSubsystem>()
			: nullptr;
	TestNotNull(
		TEXT("The Published AnimationAsset Clap is available"),
		PublishedClap
	);
	TestNotNull(TEXT("The Authoring subsystem is available"), Authoring);
	if (!PublishedClap || !Authoring)
	{
		return false;
	}

	ULLMNPCMotionTemplate* PublishCandidate =
		DuplicateObject<ULLMNPCMotionTemplate>(
			PublishedClap,
			GetTransientPackage()
		);
	PublishCandidate->Metadata.ReviewState =
		ELLMNPCTemplateReviewState::HumanApproved;
	FString Error;
	const bool bCanPublish =
		Authoring->CanPublishTemplate(PublishCandidate, Error);
	TestTrue(
		*FString::Printf(
			TEXT(
				"Published catalog derivation does not stale its "
				"passing Quality report: %s"
			),
			*Error
		),
		bCanPublish
	);
	return true;
}

#endif
