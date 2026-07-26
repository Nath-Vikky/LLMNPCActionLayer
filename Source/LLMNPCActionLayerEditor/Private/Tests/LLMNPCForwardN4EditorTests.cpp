#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Animation/AnimationAsset.h"
#include "Authoring/LLMNPCAnimationTemplateDraftImporter.h"
#include "Authoring/LLMNPCTemplateAuthoringSubsystem.h"
#include "Dom/JsonObject.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "ObjectTools.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Templates/LLMNPCActionVocabulary.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "Templates/LLMNPCPublicActionDefinition.h"
#include "Templates/LLMNPCTemplateSearchIndex.h"

namespace
{
constexpr uint32 ForwardN4EditorTestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;

FString ForwardN4ExamplePath()
{
	const TSharedPtr<IPlugin> Plugin =
		IPluginManager::Get().FindPlugin(TEXT("LLMNPCActionLayer"));
	return Plugin.IsValid()
		? FPaths::Combine(
			Plugin->GetBaseDir(),
			TEXT("Resources/AuthoringExamples/DT_Clap_Manny_AnimationAsset_v1.json")
		)
		: FString();
}

FString ForwardN4SchemaPath()
{
	const TSharedPtr<IPlugin> Plugin =
		IPluginManager::Get().FindPlugin(TEXT("LLMNPCActionLayer"));
	return Plugin.IsValid()
		? FPaths::Combine(
			Plugin->GetBaseDir(),
			TEXT("Resources/Schemas/llmnpc_animation_template_draft.schema.json")
		)
		: FString();
}

FString ForwardN4PublishedSourcePath()
{
	const TSharedPtr<IPlugin> Plugin =
		IPluginManager::Get().FindPlugin(TEXT("LLMNPCActionLayer"));
	return Plugin.IsValid()
		? FPaths::Combine(
			Plugin->GetBaseDir(),
			TEXT(
				"Resources/Templates/Published/"
				"gesture_clap_manny_asset_v1_1_0_0_r1.json"
			)
		)
		: FString();
}

bool LoadForwardN4Draft(FString& OutJson)
{
	return FFileHelper::LoadFileToString(
		OutJson,
		*ForwardN4ExamplePath()
	);
}

UAnimationAsset* LoadForwardN4Fixture()
{
	if (UAnimationAsset* Clapping = LoadObject<UAnimationAsset>(
		nullptr,
		TEXT("/Game/LLMNPC/Animation/Clapping.Clapping")
	))
	{
		return Clapping;
	}
	return LoadObject<UAnimationAsset>(
		nullptr,
		TEXT("/Game/LLMNPC/Animation/Waving.Waving")
	);
}

bool ParseForwardN4Object(
	const FString& Json,
	TSharedPtr<FJsonObject>& OutObject
)
{
	const TSharedRef<TJsonReader<>> Reader =
		TJsonReaderFactory<>::Create(Json);
	return FJsonSerializer::Deserialize(Reader, OutObject) &&
		OutObject.IsValid();
}

bool SerializeForwardN4Object(
	const TSharedRef<FJsonObject>& Object,
	FString& OutJson
)
{
	const TSharedRef<TJsonWriter<>> Writer =
		TJsonWriterFactory<>::Create(&OutJson);
	return FJsonSerializer::Serialize(Object, Writer);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN4AnimationDraftParserTest,
	"LLMNPCActionLayer.ForwardN4.Authoring.StrictAnimationDraftParser",
	ForwardN4EditorTestFlags
)

bool FLLMNPCForwardN4AnimationDraftParserTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	FString DraftJson;
	TestTrue(
		TEXT("The Animation Draft example loads"),
		LoadForwardN4Draft(DraftJson)
	);
	UAnimationAsset* AnimationAsset = LoadForwardN4Fixture();
	if (!AnimationAsset)
	{
		AddWarning(
			TEXT("No Clapping or Waving fixture is available; Animation Draft parser assertions were skipped.")
		);
		return true;
	}

	ULLMNPCMotionTemplate* Template =
		NewObject<ULLMNPCMotionTemplate>();
	FLLMNPCParsedAnimationDraftInfo Info;
	FString Error;
	TestTrue(
		*FString::Printf(TEXT("A valid Animation Draft parses: %s"), *Error),
		FLLMNPCAnimationTemplateDraftImporter::ParseDraftJson(
			DraftJson,
			*AnimationAsset,
			*Template,
			Info,
			Error
		)
	);
	TestEqual(
		TEXT("The importer forces AnimationAsset kind"),
		Template->Kind,
		ELLMNPCTemplateKind::AnimationAsset
	);
	TestEqual(
		TEXT("The importer forces Generated review state"),
		Template->Metadata.ReviewState,
		ELLMNPCTemplateReviewState::Generated
	);
	TestEqual(
		TEXT("The selected asset is injected outside the model JSON"),
		Template->AnimationAsset.Get(),
		AnimationAsset
	);
	TestTrue(
		TEXT("Animation Asset templates do not expose amplitude"),
		Template->ModifierPolicy.AmplitudeRange.Equals(
			FVector2D(1.0f, 1.0f)
		)
	);
	TestEqual(
		TEXT("The current Manny DefaultSlot reserves exactly one scheduler channel"),
		Template->Metadata.RequiredChannels.Num(),
		1
	);
	if (Template->Metadata.RequiredChannels.Num() == 1)
	{
		TestEqual(
			TEXT("The current Manny DefaultSlot reserves the full-body scheduler channel"),
			Template->Metadata.RequiredChannels[0],
			FName(TEXT("full_body"))
		);
	}
	TestTrue(
		TEXT("Provenance records the trusted Asset Picker path"),
		Template->SourceProvenanceJson.Contains(
			AnimationAsset->GetPathName()
		)
	);
	TestTrue(
		TEXT("The importer fingerprints the saved source package"),
		Info.SelectedAnimationAssetPackageHash.StartsWith(
			TEXT("md5:")
		)
	);
	const FString ImportedPackageHash =
		Info.SelectedAnimationAssetPackageHash;
	TestTrue(
		TEXT("Provenance pins the selected source package fingerprint"),
		Template->SourceProvenanceJson.Contains(
			TEXT("\"source_asset_package_hash\"")
		)
	);
	TestTrue(
		TEXT("Provenance explicitly records that the model did not supply the path"),
		Template->SourceProvenanceJson.Contains(
			TEXT("\"model_supplied_asset_path\"")
		)
	);

	const FString InjectedPath = DraftJson.Replace(
		TEXT("\"schema_version\": \"llmnpc.animation_template_draft.v1\","),
		TEXT(
			"\"schema_version\": \"llmnpc.animation_template_draft.v1\",\n"
			"  \"animation_asset_path\": \"/Game/Untrusted.Untrusted\","
		)
	);
	TestFalse(
		TEXT("A model-supplied asset path is rejected as an unknown field"),
		FLLMNPCAnimationTemplateDraftImporter::ParseDraftJson(
			InjectedPath,
			*AnimationAsset,
			*NewObject<ULLMNPCMotionTemplate>(),
			Info,
			Error
		)
	);
	TestEqual(
		TEXT("The untrusted path rejection is stable"),
		Error,
		FString(
			TEXT("LLMNPC_ANIMATION_DRAFT_ROOT_FIELD_UNKNOWN:animation_asset_path")
		)
	);

	const FString AmplitudeDraft = DraftJson.Replace(
		TEXT("\"amplitude\": [\n      1.0,\n      1.0\n    ]"),
		TEXT("\"amplitude\": [\n      0.8,\n      1.2\n    ]")
	);
	TestFalse(
		TEXT("Animation Asset drafts cannot advertise amplitude scaling"),
		FLLMNPCAnimationTemplateDraftImporter::ParseDraftJson(
			AmplitudeDraft,
			*AnimationAsset,
			*NewObject<ULLMNPCMotionTemplate>(),
			Info,
			Error
		)
	);
	TestEqual(
		TEXT("Amplitude policy rejection is stable"),
		Error,
		FString(
			TEXT("LLMNPC_ANIMATION_DRAFT_ASSET_MODIFIER_POLICY_INVALID")
		)
	);

	const FString RootMotionDraft = DraftJson.Replace(
		TEXT("\"allow_root_motion\": false"),
		TEXT("\"allow_root_motion\": true")
	);
	TestFalse(
		TEXT("v0.10 Animation Drafts cannot enable Root Motion"),
		FLLMNPCAnimationTemplateDraftImporter::ParseDraftJson(
			RootMotionDraft,
			*AnimationAsset,
			*NewObject<ULLMNPCMotionTemplate>(),
			Info,
			Error
		)
	);
	TestEqual(
		TEXT("Root Motion policy rejection is stable"),
		Error,
		FString(
			TEXT("LLMNPC_ANIMATION_DRAFT_PLAYBACK_POLICY_INVALID")
		)
	);

	TSharedPtr<FJsonObject> InvalidProvenanceObject;
	TestTrue(
		TEXT("The valid Draft can be mutated structurally for a negative test"),
		ParseForwardN4Object(DraftJson, InvalidProvenanceObject)
	);
	const TSharedPtr<FJsonObject>* InvalidProvenance = nullptr;
	if (
		InvalidProvenanceObject.IsValid() &&
		InvalidProvenanceObject->TryGetObjectField(
			TEXT("provenance"),
			InvalidProvenance
		) &&
		InvalidProvenance &&
		InvalidProvenance->IsValid()
	)
	{
		(*InvalidProvenance)->SetObjectField(
			TEXT("source_notes"),
			MakeShared<FJsonObject>()
		);
	}
	FString InvalidProvenanceType;
	TestTrue(
		TEXT("The invalid provenance fixture serializes"),
		InvalidProvenanceObject.IsValid() &&
			SerializeForwardN4Object(
				InvalidProvenanceObject.ToSharedRef(),
				InvalidProvenanceType
			)
	);
	TestFalse(
		TEXT("Optional provenance fields still require their declared JSON type"),
		FLLMNPCAnimationTemplateDraftImporter::ParseDraftJson(
			InvalidProvenanceType,
			*AnimationAsset,
			*NewObject<ULLMNPCMotionTemplate>(),
			Info,
			Error
		)
	);
	TestEqual(
		TEXT("Invalid optional provenance types have a stable rejection"),
		Error,
		FString(
			TEXT("LLMNPC_ANIMATION_DRAFT_STRING_INVALID:source_notes")
		)
	);

	ULLMNPCTemplateAuthoringSubsystem* Authoring =
		NewObject<ULLMNPCTemplateAuthoringSubsystem>();
	const FLLMNPCAuthoringOperationResult Quality =
		Authoring->GenerateQualityReport(Template, TEXT(""));
	TestTrue(
		*FString::Printf(
			TEXT("Animation Asset quality passes without optional UEPI evidence: %s"),
			*Quality.Message
		),
		Quality.bSuccess
	);
	TestTrue(
		TEXT("The quality report records the selected Animation Asset identity"),
		Template->ValidationReportJson.Contains(
			TEXT("\"source_asset_path\"")
		)
	);
	TestTrue(
		TEXT("The quality report pins the saved source package fingerprint"),
		Template->ValidationReportJson.Contains(
			TEXT("\"source_asset_package_hash\"")
		)
	);
	TestTrue(
		TEXT("The quality report checks the fixed Animation Asset modifier surface"),
		Template->ValidationReportJson.Contains(
			TEXT("animation_modifier_surface")
		)
	);

	ULLMNPCMotionTemplate* ChangedSourceIdentity =
		DuplicateObject<ULLMNPCMotionTemplate>(
			Template,
			GetTransientPackage()
		);
	ChangedSourceIdentity->SourceProvenanceJson.ReplaceInline(
		*ImportedPackageHash,
		TEXT("md5:00000000000000000000000000000000")
	);
	const FLLMNPCAuthoringOperationResult ChangedSourceQuality =
		Authoring->GenerateQualityReport(
			ChangedSourceIdentity,
			TEXT("")
		);
	TestFalse(
		TEXT("Quality rejects a Draft whose source asset changed after import"),
		ChangedSourceQuality.bSuccess
	);
	TestTrue(
		TEXT("The stale source identity check is recorded in the report"),
		ChangedSourceIdentity->ValidationReportJson.Contains(
			TEXT("animation_asset_source_fingerprint")
		)
	);

	Template->ModifierPolicy.AmplitudeRange =
		FVector2D(0.8f, 1.2f);
	const FLLMNPCAuthoringOperationResult UnsafeQuality =
		Authoring->GenerateQualityReport(Template, TEXT(""));
	TestFalse(
		TEXT("Quality rejects a hand-edited Animation Asset amplitude surface"),
		UnsafeQuality.bSuccess
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN4AnimationDraftAssetImportTest,
	"LLMNPCActionLayer.ForwardN4.Authoring.AnimationDraftAssetImport",
	ForwardN4EditorTestFlags
)

bool FLLMNPCForwardN4AnimationDraftAssetImportTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	UAnimationAsset* AnimationAsset = LoadForwardN4Fixture();
	if (!AnimationAsset)
	{
		AddWarning(
			TEXT("No Clapping or Waving fixture is available; Animation Draft asset import assertions were skipped.")
		);
		return true;
	}
	FString DraftJson;
	TestTrue(
		TEXT("The Animation Draft example loads"),
		LoadForwardN4Draft(DraftJson)
	);
	DraftJson.ReplaceInline(
		TEXT("MT_Clap_Manny_Asset_v1_Generated"),
		TEXT("MT_ForwardN4_Animation_Import_Automation")
	);
	DraftJson.ReplaceInline(
		TEXT("gesture.clap.manny.asset.v1"),
		TEXT("gesture.clap.manny.asset.automation.v1")
	);

	const FString AssetPath =
		TEXT("/Game/LLMNPCAutomation/ForwardN4/MT_ForwardN4_Animation_Import_Automation.MT_ForwardN4_Animation_Import_Automation");
	if (ULLMNPCMotionTemplate* Existing =
		LoadObject<ULLMNPCMotionTemplate>(nullptr, *AssetPath))
	{
		ObjectTools::DeleteObjectsUnchecked({ Existing });
	}

	ULLMNPCTemplateAuthoringSubsystem* Authoring =
		NewObject<ULLMNPCTemplateAuthoringSubsystem>();
	const FLLMNPCAuthoringOperationResult Result =
		Authoring->ImportAnimationDraftJson(
			DraftJson,
			AnimationAsset,
			TEXT("/Game/LLMNPCAutomation/ForwardN4")
		);
	TestTrue(
		*FString::Printf(
			TEXT("The selected asset imports through the Animation Draft path: %s"),
			*Result.Message
		),
		Result.bSuccess
	);
	TestNotNull(
		TEXT("The Animation Draft importer returns a template asset"),
		Result.TemplateAsset.Get()
	);
	if (Result.TemplateAsset)
	{
		TestEqual(
			TEXT("The imported asset remains Generated"),
			Result.TemplateAsset->Metadata.ReviewState,
			ELLMNPCTemplateReviewState::Generated
		);
		TestEqual(
			TEXT("The imported template retains the trusted selected asset"),
			Result.TemplateAsset->AnimationAsset.Get(),
			AnimationAsset
		);
		TestEqual(
			TEXT("The automation asset is removed after verification"),
			ObjectTools::DeleteObjectsUnchecked({
				Result.TemplateAsset.Get()
			}),
			1
		);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN4ClapCatalogSeedTest,
	"LLMNPCActionLayer.ForwardN4.Catalog.ClapSeedRequiresPublishedVariant",
	ForwardN4EditorTestFlags
)

bool FLLMNPCForwardN4ClapCatalogSeedTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	FString SchemaJson;
	TestTrue(
		TEXT("The independent Animation Draft schema is readable"),
		FFileHelper::LoadFileToString(
			SchemaJson,
			*ForwardN4SchemaPath()
		)
	);
	TSharedPtr<FJsonObject> Schema;
	TestTrue(
		TEXT("The independent Animation Draft schema parses"),
		ParseForwardN4Object(SchemaJson, Schema)
	);
	if (Schema.IsValid())
	{
		TestEqual(
			TEXT("The schema uses the dedicated Animation Draft identity"),
			Schema->GetStringField(TEXT("$id")),
			FString(TEXT("llmnpc.animation_template_draft.v1"))
		);
		TestFalse(
			TEXT("The Animation Draft root rejects unknown fields"),
			Schema->GetBoolField(TEXT("additionalProperties"))
		);
		const TSharedPtr<FJsonObject>* Properties = nullptr;
		TestTrue(
			TEXT("The Animation Draft schema exposes a properties object"),
			Schema->TryGetObjectField(TEXT("properties"), Properties) &&
			Properties
		);
		if (Properties)
		{
			TestFalse(
				TEXT("The model-facing schema never exposes an Animation Asset path"),
				(*Properties)->HasField(TEXT("animation_asset_path"))
			);
		}
	}

	ULLMNPCActionVocabulary* Vocabulary =
		LoadObject<ULLMNPCActionVocabulary>(
			nullptr,
			TEXT(
				"/LLMNPCActionLayer/LLMNPC/Catalog/"
				"AV_LLMNPC_Default.AV_LLMNPC_Default"
			)
		);
	ULLMNPCPublicActionDefinition* Clap =
		LoadObject<ULLMNPCPublicActionDefinition>(
			nullptr,
			TEXT(
				"/LLMNPCActionLayer/LLMNPC/PublicActions/"
				"PA_Gesture_Clap.PA_Gesture_Clap"
			)
		);
	TestNotNull(TEXT("The N4 controlled vocabulary loads"), Vocabulary);
	TestNotNull(TEXT("The Published Clap definition seed loads"), Clap);
	if (!Vocabulary || !Clap)
	{
		return false;
	}
	FString Error;
	TestTrue(
		*FString::Printf(
			TEXT("The Published Clap definition validates: %s"),
			*Error
		),
		Clap->ValidateDefinition(Vocabulary, Error)
	);
	TestEqual(
		TEXT("The Clap seed has the stable Public Action ID"),
		Clap->PublicActionId,
		FName(TEXT("gesture.clap"))
	);

	FLLMNPCTemplateSearchIndex Index;
	const TArray<ULLMNPCMotionTemplate*> NoTemplates;
	const TArray<ULLMNPCPublicActionDefinition*> Definitions = { Clap };
	const TSet<FName> Profiles = { TEXT("ue5_manny.v1") };
	TestTrue(
		TEXT("A Published definition may exist before its first variant"),
		Index.Build(
			NoTemplates,
			Definitions,
			Vocabulary,
			Profiles
		)
	);
	TestNotNull(
		TEXT("The authoring catalog can resolve the Clap definition seed"),
		Index.FindDefinition(TEXT("gesture.clap"))
	);
	TestNull(
		TEXT("Clap has no runtime variants before a template is Published"),
		Index.FindVariants(TEXT("gesture.clap"))
	);
	FLLMNPCTemplateCandidate Candidate;
	TestFalse(
		TEXT("The model cannot select Clap before a variant is Published"),
		Index.BuildRuntimeCandidate(
			TEXT("gesture.clap"),
			TEXT("ue5_manny.v1"),
			Candidate
		)
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN4PublishedSourceTest,
	"LLMNPCActionLayer.ForwardN4.Authoring.PublishedSourceLivesInPlugin",
	ForwardN4EditorTestFlags
)

bool FLLMNPCForwardN4PublishedSourceTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	const TSharedPtr<IPlugin> Plugin =
		IPluginManager::Get().FindPlugin(TEXT("LLMNPCActionLayer"));
	TestTrue(TEXT("The plugin is discoverable"), Plugin.IsValid());
	if (!Plugin.IsValid())
	{
		return false;
	}

	const FString SourcePath = ForwardN4PublishedSourcePath();
	FString SourceJson;
	TestTrue(
		TEXT("The reviewed Clap source is versioned inside the plugin"),
		FFileHelper::LoadFileToString(SourceJson, *SourcePath)
	);
	TestFalse(
		TEXT("The canonical source excludes transient local Draft paths"),
		SourceJson.Contains(TEXT("draft_source_copy_path"))
	);

	if (!Plugin->GetDescriptor().bInstalled)
	{
		TestEqual(
			TEXT("Editable plugin publication exports into plugin Resources"),
			ULLMNPCTemplateAuthoringSubsystem::GetPublishedSourceDirectory(),
			FPaths::ConvertRelativePathToFull(FPaths::GetPath(SourcePath))
		);
	}

	TSharedPtr<FJsonObject> Source;
	TestTrue(
		TEXT("The reviewed Clap source parses"),
		ParseForwardN4Object(SourceJson, Source)
	);
	if (!Source.IsValid())
	{
		return false;
	}

	FString SchemaVersion;
	TestTrue(
		TEXT("The source declares a schema"),
		Source->TryGetStringField(TEXT("schema_version"), SchemaVersion)
	);
	TestEqual(
		TEXT("The source uses the canonical Published schema"),
		SchemaVersion,
		FString(TEXT("llmnpc.published_template_source.v1"))
	);

	const TSharedPtr<FJsonObject>* Metadata = nullptr;
	TestTrue(
		TEXT("The source contains template metadata"),
		Source->TryGetObjectField(TEXT("metadata"), Metadata) &&
			Metadata && Metadata->IsValid()
	);
	if (Metadata && Metadata->IsValid())
	{
		TestEqual(
			TEXT("The source identifies the reviewed Clap template"),
			(*Metadata)->GetStringField(TEXT("templateId")),
			FString(TEXT("gesture.clap.manny.asset.v1"))
		);
		TestEqual(
			TEXT("The source maps to the stable public action"),
			(*Metadata)->GetStringField(TEXT("publicActionId")),
			FString(TEXT("gesture.clap"))
		);
	}

	const TSharedPtr<FJsonObject>* Provenance = nullptr;
	const TSharedPtr<FJsonObject>* License = nullptr;
	TestTrue(
		TEXT("The source retains external asset provenance"),
		Source->TryGetObjectField(TEXT("source_provenance"), Provenance) &&
			Provenance && Provenance->IsValid() &&
			(*Provenance)->TryGetObjectField(TEXT("source_license"), License) &&
			License && License->IsValid()
	);
	if (License && License->IsValid())
	{
		TestFalse(
			TEXT("The licensed raw animation remains non-redistributable"),
			(*License)->GetBoolField(TEXT("redistribution_allowed"))
		);
	}
	return true;
}

#endif
