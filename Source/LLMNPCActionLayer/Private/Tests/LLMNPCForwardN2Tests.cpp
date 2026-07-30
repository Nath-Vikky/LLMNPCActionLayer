#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Context/LLMNPCContextTypes.h"
#include "Dialogue/LLMNPCConversationSession.h"
#include "Dialogue/LLMNPCModelTurnContract.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Protocol/LLMNPCTurnRequestV3Adapter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Templates/LLMNPCActionVocabulary.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "Templates/LLMNPCPublicActionDefinition.h"
#include "Templates/LLMNPCTemplateLibrarySubsystem.h"
#include "Templates/LLMNPCTemplateSearchIndex.h"

namespace
{
constexpr uint32 ForwardN2TestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;

const TCHAR* ForwardN2TemplatePaths[] = {
	TEXT("/LLMNPCActionLayer/LLMNPC/MotionTemplates/Manny/MT_Nod_Manny_v1.MT_Nod_Manny_v1"),
	TEXT("/LLMNPCActionLayer/LLMNPC/MotionTemplates/Manny/MT_Point_Target_Manny_v1.MT_Point_Target_Manny_v1"),
	TEXT("/LLMNPCActionLayer/LLMNPC/MotionTemplates/Manny/MT_Wave_Right_Manny_FK_v1.MT_Wave_Right_Manny_FK_v1"),
	TEXT("/LLMNPCActionLayer/LLMNPC/MotionTemplates/Manny/MT_Wave_Right_Manny_Procedural_v1.MT_Wave_Right_Manny_Procedural_v1"),
	TEXT("/LLMNPCActionLayer/LLMNPC/MotionTemplates/Manny/MT_Wave_Right_Manny_Subtle_v1.MT_Wave_Right_Manny_Subtle_v1"),
	TEXT("/Game/LLMNPCActionLayer/MotionTemplates/MT_Wave_Asset_Manny_v1.MT_Wave_Asset_Manny_v1")
};

const TCHAR* ForwardN2DefinitionPaths[] = {
	TEXT("/LLMNPCActionLayer/LLMNPC/PublicActions/PA_Gesture_Clap.PA_Gesture_Clap"),
	TEXT("/LLMNPCActionLayer/LLMNPC/PublicActions/PA_Gesture_Nod.PA_Gesture_Nod"),
	TEXT("/LLMNPCActionLayer/LLMNPC/PublicActions/PA_Gesture_Wave_Right.PA_Gesture_Wave_Right"),
	TEXT("/LLMNPCActionLayer/LLMNPC/PublicActions/PA_Gesture_Point_Target.PA_Gesture_Point_Target")
};

ULLMNPCTemplateLibrarySubsystem* ForwardN2BuildLibrary()
{
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	ULLMNPCTemplateLibrarySubsystem* Library =
		NewObject<ULLMNPCTemplateLibrarySubsystem>(GameInstance);
	Library->RefreshLibrary();
	return Library;
}

const FLLMNPCTemplateCandidate* ForwardN2FindCandidate(
	const TArray<FLLMNPCTemplateCandidate>& Candidates,
	FName SelectionId
)
{
	return Candidates.FindByPredicate(
		[SelectionId](const FLLMNPCTemplateCandidate& Candidate)
		{
			return Candidate.SelectionId == SelectionId;
		}
	);
}

bool ForwardN2ParseObject(
	const FString& Json,
	TSharedPtr<FJsonObject>& OutObject
)
{
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN2CatalogBaselineTest,
	"LLMNPCActionLayer.ForwardN2.Catalog.PublishedBaseline",
	ForwardN2TestFlags
)

bool FLLMNPCForwardN2CatalogBaselineTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	ULLMNPCActionVocabulary* Vocabulary = LoadObject<ULLMNPCActionVocabulary>(
		nullptr,
		TEXT("/LLMNPCActionLayer/LLMNPC/Catalog/AV_LLMNPC_Default.AV_LLMNPC_Default")
	);
	TestNotNull(TEXT("The N2 controlled vocabulary loads"), Vocabulary);
	if (!Vocabulary)
	{
		return false;
	}
	FString Error;
	TestTrue(TEXT("The controlled vocabulary validates"), Vocabulary->ValidateVocabulary(Error));
	TestEqual(
		TEXT("The vocabulary content hash is current"),
		Vocabulary->ContentHash,
		ULLMNPCActionVocabulary::BuildContentHash(*Vocabulary)
	);
	TestEqual(
		TEXT("Vocabulary synonyms resolve deterministically"),
		Vocabulary->ResolveTag(TEXT("confirmation")),
		FName(TEXT("confirm"))
	);
	TestTrue(
		TEXT("The resolved confirmation tag is legal for Intent"),
		Vocabulary->IsTagAllowed(
			TEXT("confirm"),
			ELLMNPCActionVocabularyField::Intent
		)
	);

	TArray<ULLMNPCPublicActionDefinition*> Definitions;
	for (const TCHAR* Path : ForwardN2DefinitionPaths)
	{
		ULLMNPCPublicActionDefinition* Definition =
			LoadObject<ULLMNPCPublicActionDefinition>(nullptr, Path);
		TestNotNull(FString::Printf(TEXT("Definition loads: %s"), Path), Definition);
		if (!Definition)
		{
			continue;
		}
		Definitions.Add(Definition);
		Error.Reset();
		TestTrue(
			FString::Printf(
				TEXT("Published definition validates: %s"),
				*Definition->PublicActionId.ToString()
			),
			Definition->ValidateDefinition(Vocabulary, Error)
		);
		TestFalse(
			TEXT("Every Published Public Action has a model-facing summary"),
			Definition->SelectionSummary.TrimStartAndEnd().IsEmpty()
		);
	}
	TestEqual(TEXT("Exactly four Manny Public Action definitions migrated"), Definitions.Num(), 4);

	TArray<ULLMNPCMotionTemplate*> Templates;
	for (const TCHAR* Path : ForwardN2TemplatePaths)
	{
		ULLMNPCMotionTemplate* Template =
			LoadObject<ULLMNPCMotionTemplate>(nullptr, Path);
		TestNotNull(FString::Printf(TEXT("Template loads: %s"), Path), Template);
		if (!Template)
		{
			continue;
		}
		Templates.Add(Template);
		Error.Reset();
		TestTrue(
			FString::Printf(
				TEXT("Published template validates: %s"),
				*Template->Metadata.TemplateId.ToString()
			),
			Template->ValidateTemplate(Error)
		);
		TestFalse(
			TEXT("Every Published template has a visual description"),
			Template->Metadata.VisualDescription.TrimStartAndEnd().IsEmpty()
		);
		TestEqual(
			TEXT("Every Published template catalog hash is current"),
			Template->Metadata.CatalogContentHash,
			ULLMNPCMotionTemplate::BuildCatalogContentHash(*Template)
		);
	}
	TestEqual(TEXT("Six Manny Published template variants migrated"), Templates.Num(), 6);

	FLLMNPCTemplateSearchIndex Index;
	const TSet<FName> Profiles = { TEXT("ue5_manny.v1") };
	TestTrue(
		TEXT("The Published catalog index builds"),
		Index.Build(Templates, Definitions, Vocabulary, Profiles)
	);
	TestTrue(TEXT("The Published catalog has no diagnostics"), Index.GetDiagnostics().IsEmpty());
	TestEqual(TEXT("The catalog indexes all six variants"), Index.GetTemplateCount(), 6);
	TestEqual(TEXT("The catalog indexes four Public Action definitions"), Index.GetPublicActionCount(), 4);
	const TSharedPtr<IPlugin> Plugin =
		IPluginManager::Get().FindPlugin(TEXT("LLMNPCActionLayer"));
	TestTrue(TEXT("The plugin descriptor is discoverable"), Plugin.IsValid());
	FString CatalogArtifactJson;
	TSharedPtr<FJsonObject> CatalogArtifact;
	if (Plugin.IsValid())
	{
		TestTrue(
			TEXT("The current catalog artifact is readable"),
			FFileHelper::LoadFileToString(
				CatalogArtifactJson,
				*FPaths::Combine(
					Plugin->GetBaseDir(),
					TEXT("Resources"),
					TEXT("Catalog"),
					TEXT("Manny"),
					TEXT("public_actions_v1.json")
				)
			)
		);
		TestTrue(
			TEXT("The current catalog artifact is valid JSON"),
			ForwardN2ParseObject(CatalogArtifactJson, CatalogArtifact)
		);
	}
	FString ArtifactCatalogHash;
	if (CatalogArtifact.IsValid())
	{
		CatalogArtifact->TryGetStringField(
			TEXT("catalog_hash"),
			ArtifactCatalogHash
		);
	}
	TestEqual(
		TEXT("The runtime catalog matches the current versioned artifact"),
		Index.GetCatalogHash(),
		ArtifactCatalogHash
	);

	ULLMNPCMotionTemplate* Generated = DuplicateObject<ULLMNPCMotionTemplate>(
		Templates[0],
		GetTransientPackage()
	);
	Generated->Metadata.TemplateId = TEXT("generated.must.not.index");
	Generated->Metadata.ReviewState = ELLMNPCTemplateReviewState::Generated;
	Templates.Add(Generated);
	TestTrue(
		TEXT("The catalog rebuilds with a Generated asset present"),
		Index.Build(Templates, Definitions, Vocabulary, Profiles)
	);
	TestEqual(
		TEXT("Generated assets remain excluded from the runtime catalog"),
		Index.GetTemplateCount(),
		6
	);
	TestNull(
		TEXT("Generated IDs are not addressable through the runtime index"),
		Index.FindTemplate(TEXT("generated.must.not.index"))
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN2CandidateAdapterTest,
	"LLMNPCActionLayer.ForwardN2.Catalog.CandidateAndV2Adapter",
	ForwardN2TestFlags
)

bool FLLMNPCForwardN2CandidateAdapterTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	ULLMNPCTemplateLibrarySubsystem* Library = ForwardN2BuildLibrary();
	TestNotNull(TEXT("The runtime catalog library is available"), Library);
	if (!Library)
	{
		return false;
	}
	TestTrue(TEXT("The runtime catalog has no scan errors"), Library->GetScanErrors().IsEmpty());

	TArray<FLLMNPCTemplateCandidate> Candidates;
	Library->QueryRuntimeCandidates(TEXT("ue5_manny.v1"), Candidates);
	TestEqual(TEXT("Manny exposes six Public Action candidates"), Candidates.Num(), 6);
	const FLLMNPCTemplateCandidate* Wave =
		ForwardN2FindCandidate(Candidates, TEXT("gesture.wave.right"));
	const FLLMNPCTemplateCandidate* Point =
		ForwardN2FindCandidate(Candidates, TEXT("gesture.point.target"));
	const FLLMNPCTemplateCandidate* Clap =
		ForwardN2FindCandidate(Candidates, TEXT("gesture.clap"));
	const FLLMNPCTemplateCandidate* Shrug =
		ForwardN2FindCandidate(Candidates, TEXT("gesture.shrug"));
	const FLLMNPCTemplateCandidate* Beckon =
		ForwardN2FindCandidate(Candidates, TEXT("gesture.beckon"));
	TestNotNull(TEXT("The aggregated Wave Candidate exists"), Wave);
	TestNotNull(TEXT("The aggregated Point Candidate exists"), Point);
	TestNotNull(TEXT("The Published Clap Candidate exists"), Clap);
	TestNotNull(TEXT("The Published Shrug Candidate exists"), Shrug);
	TestNotNull(TEXT("The Published Beckon Candidate exists"), Beckon);
	if (!Wave || !Point || !Clap || !Shrug || !Beckon)
	{
		return false;
	}
	TestTrue(TEXT("Wave has style-specific modifier ranges"), Wave->StyleOptions.Num() >= 2);
	TestTrue(TEXT("Point requires a scene target"), Point->bRequiresTarget);
	TestTrue(
		TEXT("Point exposes only the model-safe scene_target category"),
		Point->TargetCategoryTags.Contains(TEXT("scene_target"))
	);
	TestTrue(TEXT("Beckon requires a scene target"), Beckon->bRequiresTarget);
	TestTrue(
		TEXT("Beckon exposes only the model-safe scene_target category"),
		Beckon->TargetCategoryTags.Contains(TEXT("scene_target"))
	);

	FLLMNPCTemplateCandidate V2Wave;
	FString Error;
	TestTrue(
		TEXT("The real Wave candidate has a safe v2 intersection"),
		FLLMNPCTurnRequestV3Adapter::BuildV2SafeCandidate(
			*Wave,
			V2Wave,
			Error
		)
	);
	for (const FLLMNPCCandidateStyleOption& Style : Wave->StyleOptions)
	{
		TestTrue(
			TEXT("The v2 amplitude range is inside every v3 style range"),
			V2Wave.AmplitudeRange.X >= Style.AmplitudeRange.X &&
			V2Wave.AmplitudeRange.Y <= Style.AmplitudeRange.Y
		);
		TestTrue(
			TEXT("The v2 speed range is inside every v3 style range"),
			V2Wave.SpeedRange.X >= Style.SpeedRange.X &&
			V2Wave.SpeedRange.Y <= Style.SpeedRange.Y
		);
	}

	FLLMNPCTemplateCandidate Unsafe = *Wave;
	Unsafe.SelectionId = TEXT("unsafe.disjoint.styles");
	Unsafe.StyleOptions.SetNum(2);
	Unsafe.StyleOptions[0].Style = TEXT("small");
	Unsafe.StyleOptions[0].AmplitudeRange = FVector2D(0.5f, 0.7f);
	Unsafe.StyleOptions[0].SpeedRange = FVector2D(0.8f, 1.0f);
	Unsafe.StyleOptions[0].DurationRange = FVector2D(0.8f, 1.0f);
	Unsafe.StyleOptions[1].Style = TEXT("large");
	Unsafe.StyleOptions[1].AmplitudeRange = FVector2D(1.2f, 1.4f);
	Unsafe.StyleOptions[1].SpeedRange = FVector2D(0.8f, 1.0f);
	Unsafe.StyleOptions[1].DurationRange = FVector2D(0.8f, 1.0f);
	TestFalse(
		TEXT("Disjoint style ranges cannot be flattened into v2"),
		FLLMNPCTurnRequestV3Adapter::BuildV2SafeCandidate(
			Unsafe,
			V2Wave,
			Error
		)
	);
	TestEqual(
		TEXT("Unsafe v2 intersections have a stable error"),
		Error,
		FString(TEXT("LLMNPC_TURN_V2_ADAPTER_UNSAFE_STYLE_INTERSECTION"))
	);

	TArray<FLLMNPCTemplateCandidate> Adapted;
	TArray<FName> Excluded;
	FLLMNPCTurnRequestV3Adapter::AdaptCandidatesForSchema(
		TEXT("llmnpc.turn_request.v2"),
		{ Unsafe },
		Adapted,
		&Excluded
	);
	TestTrue(TEXT("The unsafe candidate is excluded from v2"), Adapted.IsEmpty());
	TestTrue(
		TEXT("The v2 adapter reports the excluded selection ID"),
		Excluded.Contains(Unsafe.SelectionId)
	);
	FLLMNPCTurnRequestV3Adapter::AdaptCandidatesForSchema(
		TEXT("llmnpc.turn_request.v3"),
		{ Unsafe },
		Adapted,
		&Excluded
	);
	TestEqual(TEXT("v3 preserves style-specific candidates"), Adapted.Num(), 1);
	TestTrue(TEXT("v3 excludes nothing from this adapter"), Excluded.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN2TurnRequestV3PrivacyTest,
	"LLMNPCActionLayer.ForwardN2.Protocol.TurnRequestV3Privacy",
	ForwardN2TestFlags
)

bool FLLMNPCForwardN2TurnRequestV3PrivacyTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	const FString& SafetyInstruction =
		FLLMNPCModelTurnContract::GetSelectionSafetyInstruction();
	TestTrue(
		TEXT("The mandatory provider instruction names selection_id"),
		SafetyInstruction.Contains(TEXT("selection_id"))
	);
	TestTrue(
		TEXT("The mandatory provider instruction forbids approximate substitution"),
		SafetyInstruction.Contains(TEXT("Never substitute"))
	);
	TestTrue(
		TEXT("The mandatory provider instruction requires None for missing candidates"),
		SafetyInstruction.Contains(TEXT("no appropriate offered candidate"))
	);
	ULLMNPCTemplateLibrarySubsystem* Library = ForwardN2BuildLibrary();
	if (!Library)
	{
		AddError(TEXT("Could not build the N2 library."));
		return false;
	}
	TArray<FLLMNPCTemplateCandidate> Candidates;
	Library->QueryRuntimeCandidates(TEXT("ue5_manny.v1"), Candidates);
	FLLMNPCTemplateCandidate* Point = Candidates.FindByPredicate(
		[](const FLLMNPCTemplateCandidate& Candidate)
		{
			return Candidate.SelectionId == TEXT("gesture.point.target");
		}
	);
	if (!Point)
	{
		AddError(TEXT("Point candidate is missing."));
		return false;
	}
	Point->AllowedTargetRefs = { TEXT("door") };
	Point->DefaultTargetRef = TEXT("door");

	ULLMNPCConversationSession* Session =
		NewObject<ULLMNPCConversationSession>();
	Session->InitializeSession(TEXT("manny_test"), 8);
	Session->AddMessage(
		ELLMNPCDialogueRole::Player,
		TEXT("Where is the door?")
	);
	FLLMNPCSelectionContextSnapshot Context;
	FLLMNPCSceneTargetContext& Door = Context.AvailableTargets.AddDefaulted_GetRef();
	Door.TargetRef = TEXT("door");
	Door.Category = TEXT("scene_target");
	Door.SemanticTags = { TEXT("door"), TEXT("location") };
	Door.Salience = 1.0f;
	const FString Json = Session->BuildContextualRequestJsonForSchema(
		FGuid::NewGuid(),
		Candidates,
		Context,
		TEXT("llmnpc.selection_prompt.v3"),
		TEXT("llmnpc.turn_request.v3")
	);

	TSharedPtr<FJsonObject> Root;
	TestTrue(TEXT("The Turn Request v3 JSON parses"), ForwardN2ParseObject(Json, Root));
	if (!Root.IsValid())
	{
		return false;
	}
	TestEqual(
		TEXT("The request pins the v3 schema"),
		Root->GetStringField(TEXT("schema_version")),
		FString(TEXT("llmnpc.turn_request.v3"))
	);
	const TArray<TSharedPtr<FJsonValue>>* CandidateValues = nullptr;
	TestTrue(
		TEXT("The request contains Candidate Cards"),
		Root->TryGetArrayField(TEXT("candidate_templates"), CandidateValues) &&
		CandidateValues
	);
	if (CandidateValues)
	{
		TestEqual(TEXT("All six Public Actions are offered"), CandidateValues->Num(), 6);
		bool bHasPublishedShrug = false;
		bool bHasPublishedBeckon = false;
		for (const TSharedPtr<FJsonValue>& Value : *CandidateValues)
		{
			const TSharedPtr<FJsonObject> CandidateObject = Value->AsObject();
			FString SelectionId;
			const bool bHasSelectionId =
				CandidateObject.IsValid() &&
				CandidateObject->TryGetStringField(TEXT("selection_id"), SelectionId);
			bHasPublishedShrug |= bHasSelectionId && SelectionId == TEXT("gesture.shrug");
			bHasPublishedBeckon |= bHasSelectionId && SelectionId == TEXT("gesture.beckon");
			TestTrue(
				TEXT("v3 Candidate Cards use selection_id"),
				CandidateObject.IsValid() &&
				CandidateObject->HasField(TEXT("selection_id"))
			);
			TestFalse(
				TEXT("v3 Candidate Cards do not expose internal template_id"),
				CandidateObject.IsValid() &&
				CandidateObject->HasField(TEXT("template_id"))
			);
			TestTrue(
				TEXT("v3 Candidate Cards preserve style-specific ranges"),
				CandidateObject.IsValid() &&
				CandidateObject->HasTypedField<EJson::Array>(TEXT("style_options"))
			);
		}
		TestTrue(TEXT("The request offers the Published Shrug"), bHasPublishedShrug);
		TestTrue(TEXT("The request offers the Published Beckon"), bHasPublishedBeckon);
	}
	for (const TCHAR* Forbidden : {
		TEXT("clavicle_"),
		TEXT("upperarm_"),
		TEXT("lowerarm_"),
		TEXT("hand_r"),
		TEXT("compact_pose"),
		TEXT("component_space"),
		TEXT("\"transform\""),
		TEXT("\"bone\""),
		TEXT("/Game/"),
		TEXT("/LLMNPCActionLayer/")
	})
	{
		TestFalse(
			FString::Printf(
				TEXT("Provider payload omits internal token: %s"),
				Forbidden
			),
			Json.Contains(Forbidden, ESearchCase::IgnoreCase)
		);
	}

	const FString CardJson =
		FLLMNPCTurnRequestV3Adapter::BuildCandidateCardPreviewJson(*Point);
	TSharedPtr<FJsonObject> Card;
	TestTrue(TEXT("Workbench Candidate Card preview parses"), ForwardN2ParseObject(CardJson, Card));
	if (Card.IsValid())
	{
		TestEqual(
			TEXT("The Point preview keeps its Public Action ID"),
			Card->GetStringField(TEXT("selection_id")),
			FString(TEXT("gesture.point.target"))
		);
		const TSharedPtr<FJsonObject>* TargetContract = nullptr;
		TestTrue(
			TEXT("The Point preview carries its target contract"),
			Card->TryGetObjectField(TEXT("target_contract"), TargetContract) &&
			TargetContract &&
			(*TargetContract)->GetBoolField(TEXT("requires_target"))
		);
	}
	return true;
}

#endif
