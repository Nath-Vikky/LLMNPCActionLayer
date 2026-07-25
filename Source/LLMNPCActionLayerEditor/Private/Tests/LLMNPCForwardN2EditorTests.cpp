#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Authoring/LLMNPCTemplateAuthoringSubsystem.h"
#include "Dom/JsonObject.h"
#include "Framework/Docking/TabManager.h"
#include "Interfaces/IPluginManager.h"
#include "LLMNPCSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "Templates/LLMNPCPublicActionDefinition.h"

namespace
{
constexpr uint32 ForwardN2EditorTestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;

bool ForwardN2LoadSchema(
	const FString& SchemaDirectory,
	const FString& Filename,
	TSharedPtr<FJsonObject>& OutRoot
)
{
	FString Json;
	if (!FFileHelper::LoadFileToString(
		Json,
		*FPaths::Combine(SchemaDirectory, Filename)
	))
	{
		return false;
	}
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	return FJsonSerializer::Deserialize(Reader, OutRoot) && OutRoot.IsValid();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN2SchemaAndWorkbenchTest,
	"LLMNPCActionLayer.ForwardN2.Editor.SchemaAndWorkbench",
	ForwardN2EditorTestFlags
)

bool FLLMNPCForwardN2SchemaAndWorkbenchTest::RunTest(
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
	const FString SchemaDirectory = FPaths::Combine(
		Plugin->GetBaseDir(),
		TEXT("Resources/Schemas")
	);
	for (const FString& Filename : {
		TEXT("llmnpc_turn_request_v3.schema.json"),
		TEXT("llmnpc_public_action_definition_v1.schema.json"),
		TEXT("llmnpc_action_vocabulary_v1.schema.json")
	})
	{
		TSharedPtr<FJsonObject> Root;
		TestTrue(
			FString::Printf(TEXT("N2 schema parses: %s"), *Filename),
			ForwardN2LoadSchema(SchemaDirectory, Filename, Root)
		);
		if (Root.IsValid())
		{
			TestEqual(
				TEXT("N2 schemas reject undeclared top-level fields"),
				Root->GetBoolField(TEXT("additionalProperties")),
				false
			);
		}
	}

	TSharedPtr<FJsonObject> TurnRequestSchema;
	TestTrue(
		TEXT("Turn Request v3 schema reloads for structural checks"),
		ForwardN2LoadSchema(
			SchemaDirectory,
			TEXT("llmnpc_turn_request_v3.schema.json"),
			TurnRequestSchema
		)
	);
	if (TurnRequestSchema.IsValid())
	{
		const TSharedPtr<FJsonObject>* Definitions = nullptr;
		const TSharedPtr<FJsonObject>* Candidate = nullptr;
		TestTrue(
			TEXT("Turn Request v3 defines a closed Candidate Card"),
			TurnRequestSchema->TryGetObjectField(TEXT("$defs"), Definitions) &&
			Definitions &&
			(*Definitions)->TryGetObjectField(TEXT("candidate"), Candidate) &&
			Candidate &&
			!(*Candidate)->GetBoolField(TEXT("additionalProperties"))
		);
	}

	TestTrue(
		TEXT("The Template Workbench Nomad tab is registered"),
		FGlobalTabmanager::Get()->HasTabSpawner(TEXT("LLMNPCTemplateWorkbench"))
	);
	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	TestNotNull(TEXT("N2 settings are available"), Settings);
	if (Settings)
	{
		TestEqual(
			TEXT("Templates publish to the project Published path"),
			Settings->ProjectPublishedTemplatePath,
			FString(TEXT("/Game/LLMNPCActionLayer/MotionTemplates/Published"))
		);
		TestEqual(
			TEXT("Public Actions publish to the project Published path"),
			Settings->ProjectPublishedPublicActionPath,
			FString(TEXT("/Game/LLMNPCActionLayer/PublicActions/Published"))
		);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN2AuthoringGatesTest,
	"LLMNPCActionLayer.ForwardN2.Editor.AuthoringGates",
	ForwardN2EditorTestFlags
)

bool FLLMNPCForwardN2AuthoringGatesTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	ULLMNPCTemplateAuthoringSubsystem* Authoring =
		NewObject<ULLMNPCTemplateAuthoringSubsystem>();
	const ULLMNPCPublicActionDefinition* PublishedDefinition =
		LoadObject<ULLMNPCPublicActionDefinition>(
			nullptr,
			TEXT("/LLMNPCActionLayer/LLMNPC/PublicActions/PA_Gesture_Nod.PA_Gesture_Nod")
		);
	const ULLMNPCMotionTemplate* PublishedTemplate =
		LoadObject<ULLMNPCMotionTemplate>(
			nullptr,
			TEXT("/LLMNPCActionLayer/LLMNPC/MotionTemplates/Manny/MT_Nod_Manny_v1.MT_Nod_Manny_v1")
		);
	TestNotNull(TEXT("The Authoring subsystem can be constructed"), Authoring);
	TestNotNull(TEXT("The Published Nod definition loads"), PublishedDefinition);
	TestNotNull(TEXT("The Published Nod template loads"), PublishedTemplate);
	if (!Authoring || !PublishedDefinition || !PublishedTemplate)
	{
		return false;
	}

	ULLMNPCPublicActionDefinition* Candidate =
		DuplicateObject<ULLMNPCPublicActionDefinition>(
			PublishedDefinition,
			GetTransientPackage()
		);
	Candidate->ReviewState = ELLMNPCTemplateReviewState::HumanApproved;
	Candidate->ReviewRecordJson =
		TEXT("{\"schema_version\":\"llmnpc.public_action_review.v1\",")
		TEXT("\"human_review\":{\"reviewer\":\"automation\",")
		TEXT("\"notes\":\"candidate description and target contract accepted\",")
		TEXT("\"timestamp_utc\":\"2026-07-25T00:00:00Z\",\"approved\":true}}");
	Candidate->ContentHash.Reset();
	Candidate->ContentHash =
		ULLMNPCPublicActionDefinition::BuildContentHash(*Candidate);
	FString Error;
	TestTrue(
		TEXT("A valid HumanApproved Public Action passes the publish gate"),
		Authoring->CanPublishPublicAction(Candidate, Error)
	);

	Candidate->SelectionSummary.Reset();
	Candidate->ContentHash.Reset();
	Candidate->ContentHash =
		ULLMNPCPublicActionDefinition::BuildContentHash(*Candidate);
	TestFalse(
		TEXT("A Public Action without a model-facing description cannot publish"),
		Authoring->CanPublishPublicAction(Candidate, Error)
	);
	TestEqual(
		TEXT("The missing description has a stable validation code"),
		Error,
		FString(TEXT("LLMNPC_PUBLIC_ACTION_SELECTION_SUMMARY_INVALID"))
	);

	Candidate->SelectionSummary = PublishedDefinition->SelectionSummary;
	Candidate->ReviewRecordJson.Reset();
	Candidate->ContentHash.Reset();
	Candidate->ContentHash =
		ULLMNPCPublicActionDefinition::BuildContentHash(*Candidate);
	TestFalse(
		TEXT("A Public Action without a human review record cannot publish"),
		Authoring->CanPublishPublicAction(Candidate, Error)
	);
	TestEqual(
		TEXT("The missing review has a stable authoring code"),
		Error,
		FString(TEXT("LLMNPC_AUTHORING_PUBLIC_ACTION_REVIEW_REQUIRED"))
	);

	ULLMNPCMotionTemplate* MissingDescription =
		DuplicateObject<ULLMNPCMotionTemplate>(
			PublishedTemplate,
			GetTransientPackage()
		);
	MissingDescription->Metadata.VisualDescription.Reset();
	MissingDescription->Metadata.CatalogContentHash.Reset();
	MissingDescription->Metadata.CatalogContentHash =
		ULLMNPCMotionTemplate::BuildCatalogContentHash(*MissingDescription);
	TestFalse(
		TEXT("A Published template without a visual description is invalid"),
		MissingDescription->ValidateTemplate(Error)
	);
	TestEqual(
		TEXT("The template description gate has a stable code"),
		Error,
		FString(TEXT("LLMNPC_TEMPLATE_VISUAL_DESCRIPTION_INVALID"))
	);
	return true;
}

#endif
