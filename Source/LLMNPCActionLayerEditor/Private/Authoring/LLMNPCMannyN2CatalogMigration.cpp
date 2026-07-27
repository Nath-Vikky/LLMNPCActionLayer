#include "Authoring/LLMNPCMannyN2CatalogMigration.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Templates/LLMNPCActionVocabulary.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "Templates/LLMNPCPublicActionDefinition.h"
#include "Templates/LLMNPCTemplateSearchIndex.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogLLMNPCMannyN2Migration, Log, All);

namespace
{
const TCHAR* VocabularyPackagePath =
	TEXT("/LLMNPCActionLayer/LLMNPC/Catalog/AV_LLMNPC_Default");
const TCHAR* VocabularyAssetName = TEXT("AV_LLMNPC_Default");
const TCHAR* ValidationBaselineHash =
	TEXT("md5:2e346854d3be9b09e8a666484b5821da");

struct FPublicActionSeed
{
	const TCHAR* AssetName;
	const TCHAR* PublicActionId;
	const TCHAR* DisplayName;
	const TCHAR* SelectionSummary;
	TArray<FString> SuitableWhen;
	TArray<FString> AvoidWhen;
	TArray<FName> SemanticEffects;
	TArray<FName> TargetCategories;
	FName GestureFamily;
	FName DefaultStyle;
	TArray<FString> SearchKeywords;
	bool bRequiresTarget = false;
};

struct FTemplateCatalogSeed
{
	const TCHAR* AssetPath;
	const TCHAR* SourceFilename;
	FName VariantId;
	TArray<FName> VariantStyles;
	FString VisualDescription;
	TArray<FName> BodyRegions;
	TArray<FName> SpatialRequirements;
	TArray<FName> SemanticEffects;
	TArray<FName> TargetCategories;
	TArray<FName> RequiredCapabilities;
	float Expressiveness = 0.5f;
	float Energy = 0.5f;
	float SocialIntensity = 0.5f;
	FString VariantDifference;
};

template<typename T>
T* LoadOrCreateAsset(
	const FString& PackagePath,
	const FString& AssetName,
	bool& bOutCreated,
	FString& OutError
)
{
	bOutCreated = false;
	const FString ObjectPath = PackagePath + TEXT(".") + AssetName;
	if (T* Existing = LoadObject<T>(nullptr, *ObjectPath))
	{
		Existing->Modify();
		return Existing;
	}
	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		OutError = TEXT("LLMNPC_N2_PACKAGE_CREATE_FAILED");
		return nullptr;
	}
	T* Asset = FindObject<T>(Package, *AssetName);
	if (!Asset)
	{
		Asset = NewObject<T>(
			Package,
			*AssetName,
			RF_Public | RF_Standalone | RF_Transactional
		);
		if (!Asset)
		{
			OutError = TEXT("LLMNPC_N2_ASSET_CREATE_FAILED");
			return nullptr;
		}
		FAssetRegistryModule::AssetCreated(Asset);
		bOutCreated = true;
	}
	Asset->Modify();
	return Asset;
}

bool SaveAsset(UObject* Asset, FString& OutError)
{
	if (!Asset)
	{
		OutError = TEXT("LLMNPC_N2_ASSET_MISSING");
		return false;
	}
	UPackage* Package = Asset->GetOutermost();
	Package->MarkPackageDirty();
	const FString Filename = FPackageName::LongPackageNameToFilename(
		Package->GetName(),
		FPackageName::GetAssetPackageExtension()
	);
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_None;
	SaveArgs.Error = GError;
	if (!UPackage::SavePackage(Package, Asset, *Filename, SaveArgs))
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_N2_ASSET_SAVE_FAILED:%s"),
			*Package->GetName()
		);
		return false;
	}
	return true;
}

void AddVocabularyEntry(
	ULLMNPCActionVocabulary& Vocabulary,
	FName Tag,
	const TCHAR* English,
	const TCHAR* Chinese,
	std::initializer_list<ELLMNPCActionVocabularyField> Fields,
	std::initializer_list<FName> Synonyms = {}
)
{
	FLLMNPCActionVocabularyEntry& Entry =
		Vocabulary.Entries.AddDefaulted_GetRef();
	Entry.Tag = Tag;
	Entry.EnglishDisplayName = FText::FromString(English);
	Entry.ChineseDisplayName = FText::FromString(Chinese);
	for (const ELLMNPCActionVocabularyField Field : Fields)
	{
		Entry.AllowedFields.Add(Field);
	}
	for (const FName Synonym : Synonyms)
	{
		Entry.Synonyms.Add(Synonym);
	}
}

void PopulateVocabulary(ULLMNPCActionVocabulary& Vocabulary)
{
	Vocabulary.VocabularyId = TEXT("llmnpc.manny_social_actions");
	Vocabulary.SchemaVersion = LLMNPCCatalog::VocabularySchemaVersion;
	Vocabulary.SemanticVersion = TEXT("1.1.0");
	Vocabulary.Revision = 2;
	Vocabulary.Entries.Reset();

	AddVocabularyEntry(Vocabulary, TEXT("nod"), TEXT("Nod"), TEXT("\u70b9\u5934"),
		{ ELLMNPCActionVocabularyField::GestureFamily });
	AddVocabularyEntry(Vocabulary, TEXT("wave"), TEXT("Wave"), TEXT("\u6325\u624b"),
		{ ELLMNPCActionVocabularyField::GestureFamily });
	AddVocabularyEntry(Vocabulary, TEXT("point"), TEXT("Point"), TEXT("\u6307\u5411"),
		{ ELLMNPCActionVocabularyField::GestureFamily });
	AddVocabularyEntry(Vocabulary, TEXT("clap"), TEXT("Clap"), TEXT("\u9f13\u638c"),
		{ ELLMNPCActionVocabularyField::GestureFamily });
	AddVocabularyEntry(Vocabulary, TEXT("shrug"), TEXT("Shrug"), TEXT("\u8038\u80a9"),
		{ ELLMNPCActionVocabularyField::GestureFamily });

	AddVocabularyEntry(Vocabulary, TEXT("confirm"), TEXT("Confirm"), TEXT("\u786e\u8ba4"),
		{ ELLMNPCActionVocabularyField::Intent }, { TEXT("confirmation") });
	AddVocabularyEntry(Vocabulary, TEXT("agree"), TEXT("Agree"), TEXT("\u540c\u610f"),
		{ ELLMNPCActionVocabularyField::Intent, ELLMNPCActionVocabularyField::SemanticEffect });
	AddVocabularyEntry(Vocabulary, TEXT("acknowledge"), TEXT("Acknowledge"), TEXT("\u56de\u5e94"),
		{ ELLMNPCActionVocabularyField::Intent, ELLMNPCActionVocabularyField::SemanticEffect });
	AddVocabularyEntry(Vocabulary, TEXT("affirm"), TEXT("Affirm"), TEXT("\u80af\u5b9a"),
		{ ELLMNPCActionVocabularyField::SemanticEffect });
	AddVocabularyEntry(Vocabulary, TEXT("greet"), TEXT("Greet"), TEXT("\u95ee\u5019"),
		{ ELLMNPCActionVocabularyField::Intent, ELLMNPCActionVocabularyField::SemanticEffect });
	AddVocabularyEntry(Vocabulary, TEXT("farewell"), TEXT("Farewell"), TEXT("\u9053\u522b"),
		{ ELLMNPCActionVocabularyField::Intent, ELLMNPCActionVocabularyField::SemanticEffect });
	AddVocabularyEntry(Vocabulary, TEXT("attract_attention"), TEXT("Attract attention"), TEXT("\u5438\u5f15\u6ce8\u610f"),
		{ ELLMNPCActionVocabularyField::Intent, ELLMNPCActionVocabularyField::SemanticEffect });
	AddVocabularyEntry(Vocabulary, TEXT("indicate"), TEXT("Indicate"), TEXT("\u6307\u793a"),
		{ ELLMNPCActionVocabularyField::Intent, ELLMNPCActionVocabularyField::SemanticEffect });
	AddVocabularyEntry(Vocabulary, TEXT("direct_attention"), TEXT("Direct attention"), TEXT("\u5f15\u5bfc\u6ce8\u610f"),
		{ ELLMNPCActionVocabularyField::Intent, ELLMNPCActionVocabularyField::SemanticEffect });
	AddVocabularyEntry(Vocabulary, TEXT("answer_where"), TEXT("Answer where"), TEXT("\u56de\u7b54\u4f4d\u7f6e"),
		{ ELLMNPCActionVocabularyField::Intent });
	AddVocabularyEntry(Vocabulary, TEXT("applaud"), TEXT("Applaud"), TEXT("\u559d\u5f69"),
		{ ELLMNPCActionVocabularyField::Intent, ELLMNPCActionVocabularyField::SemanticEffect },
		{ TEXT("applause") });
	AddVocabularyEntry(Vocabulary, TEXT("express_uncertainty"), TEXT("Express uncertainty"), TEXT("\u8868\u8fbe\u4e0d\u786e\u5b9a"),
		{ ELLMNPCActionVocabularyField::Intent, ELLMNPCActionVocabularyField::SemanticEffect },
		{ TEXT("uncertain_response") });
	AddVocabularyEntry(Vocabulary, TEXT("noncommittal"), TEXT("Noncommittal"), TEXT("\u4e0d\u7f6e\u53ef\u5426"),
		{ ELLMNPCActionVocabularyField::SemanticEffect });

	AddVocabularyEntry(Vocabulary, TEXT("neutral"), TEXT("Neutral"), TEXT("\u4e2d\u6027"),
		{ ELLMNPCActionVocabularyField::Emotion, ELLMNPCActionVocabularyField::VariantStyle });
	AddVocabularyEntry(Vocabulary, TEXT("friendly"), TEXT("Friendly"), TEXT("\u53cb\u597d"),
		{ ELLMNPCActionVocabularyField::Emotion, ELLMNPCActionVocabularyField::VariantStyle });
	AddVocabularyEntry(Vocabulary, TEXT("excited"), TEXT("Excited"), TEXT("\u5174\u594b"),
		{ ELLMNPCActionVocabularyField::Emotion, ELLMNPCActionVocabularyField::VariantStyle });
	AddVocabularyEntry(Vocabulary, TEXT("helpful"), TEXT("Helpful"), TEXT("\u70ed\u5fc3"),
		{ ELLMNPCActionVocabularyField::Emotion });
	AddVocabularyEntry(Vocabulary, TEXT("subtle"), TEXT("Subtle"), TEXT("\u8f7b\u5fae"),
		{ ELLMNPCActionVocabularyField::VariantStyle });
	AddVocabularyEntry(Vocabulary, TEXT("uncertain"), TEXT("Uncertain"), TEXT("\u4e0d\u786e\u5b9a"),
		{ ELLMNPCActionVocabularyField::Emotion, ELLMNPCActionVocabularyField::VariantStyle });
	AddVocabularyEntry(Vocabulary, TEXT("shy"), TEXT("Shy"), TEXT("\u5bb3\u7f9e"),
		{ ELLMNPCActionVocabularyField::Personality });
	AddVocabularyEntry(Vocabulary, TEXT("reserved"), TEXT("Reserved"), TEXT("\u514b\u5236"),
		{ ELLMNPCActionVocabularyField::Personality });

	AddVocabularyEntry(Vocabulary, TEXT("head"), TEXT("Head"), TEXT("\u5934\u90e8"),
		{ ELLMNPCActionVocabularyField::BodyRegion });
	AddVocabularyEntry(Vocabulary, TEXT("gaze"), TEXT("Gaze"), TEXT("\u6ce8\u89c6"),
		{ ELLMNPCActionVocabularyField::BodyRegion });
	AddVocabularyEntry(Vocabulary, TEXT("one_arm"), TEXT("One arm"), TEXT("\u5355\u81c2"),
		{ ELLMNPCActionVocabularyField::BodyRegion });
	AddVocabularyEntry(Vocabulary, TEXT("hand"), TEXT("Hand"), TEXT("\u624b\u90e8"),
		{ ELLMNPCActionVocabularyField::BodyRegion });
	AddVocabularyEntry(Vocabulary, TEXT("fingers"), TEXT("Fingers"), TEXT("\u624b\u6307"),
		{ ELLMNPCActionVocabularyField::BodyRegion });
	AddVocabularyEntry(Vocabulary, TEXT("two_hands"), TEXT("Two hands"), TEXT("\u53cc\u624b"),
		{ ELLMNPCActionVocabularyField::BodyRegion });
	AddVocabularyEntry(Vocabulary, TEXT("shoulders"), TEXT("Shoulders"), TEXT("\u80a9\u90e8"),
		{ ELLMNPCActionVocabularyField::BodyRegion });
	AddVocabularyEntry(Vocabulary, TEXT("upper_torso"), TEXT("Upper torso"), TEXT("\u4e0a\u534a\u8eab"),
		{ ELLMNPCActionVocabularyField::BodyRegion });
	AddVocabularyEntry(Vocabulary, TEXT("two_arms"), TEXT("Two arms"), TEXT("\u53cc\u81c2"),
		{ ELLMNPCActionVocabularyField::BodyRegion });

	AddVocabularyEntry(Vocabulary, TEXT("target_independent"), TEXT("Target independent"), TEXT("\u65e0\u76ee\u6807"),
		{ ELLMNPCActionVocabularyField::SpatialRequirement });
	AddVocabularyEntry(Vocabulary, TEXT("target_required"), TEXT("Target required"), TEXT("\u9700\u8981\u76ee\u6807"),
		{ ELLMNPCActionVocabularyField::SpatialRequirement });
	AddVocabularyEntry(Vocabulary, TEXT("scene_target"), TEXT("Any registered scene target"), TEXT("\u573a\u666f\u76ee\u6807"),
		{ ELLMNPCActionVocabularyField::TargetCategory });

	Vocabulary.ContentHash.Reset();
	Vocabulary.ContentHash = ULLMNPCActionVocabulary::BuildContentHash(Vocabulary);
}

TArray<FPublicActionSeed> GetPublicActionSeeds()
{
	return {
		{
			TEXT("PA_Gesture_Nod"),
			TEXT("gesture.nod"),
			TEXT("Nod"),
			TEXT("Dip the head briefly to acknowledge, confirm, or agree without interrupting the conversation."),
			{ TEXT("acknowledging a statement"), TEXT("confirming understanding"), TEXT("quiet agreement") },
			{ TEXT("clear disagreement"), TEXT("the character must remain completely still") },
			{ TEXT("acknowledge"), TEXT("affirm") },
			{},
			TEXT("nod"),
			TEXT("neutral"),
			{ TEXT("yes"), TEXT("understood"), TEXT("agreement"), TEXT("acknowledgement") },
			false
		},
		{
			TEXT("PA_Gesture_Wave_Right"),
			TEXT("gesture.wave.right"),
			TEXT("Right-Hand Wave"),
			TEXT("Raise one open hand and wave for a greeting, farewell, or a polite attempt to attract attention."),
			{ TEXT("greeting someone"), TEXT("saying goodbye"), TEXT("attracting attention at a distance") },
			{ TEXT("both hands are occupied"), TEXT("formal stillness is required"), TEXT("close two-hand interaction") },
			{ TEXT("greet"), TEXT("farewell"), TEXT("attract_attention") },
			{},
			TEXT("wave"),
			TEXT("friendly"),
			{ TEXT("hello"), TEXT("goodbye"), TEXT("wave"), TEXT("attention") },
			false
		},
		{
			TEXT("PA_Gesture_Point_Target"),
			TEXT("gesture.point.target"),
			TEXT("Gaze And Point"),
			TEXT("Look toward a registered scene target and point at it with one hand to direct attention or answer where it is."),
			{ TEXT("showing where something is"), TEXT("directing attention to a known target"), TEXT("answering a location question") },
			{ TEXT("no legal target is available"), TEXT("the pointing hand is occupied") },
			{ TEXT("indicate"), TEXT("direct_attention") },
			{ TEXT("scene_target") },
			TEXT("point"),
			TEXT("neutral"),
			{ TEXT("where"), TEXT("there"), TEXT("look"), TEXT("point"), TEXT("location") },
			true
		},
		{
			TEXT("PA_Gesture_Clap"),
			TEXT("gesture.clap"),
			TEXT("Clap"),
			TEXT("Bring both hands together in a visible clap to applaud, celebrate, or show enthusiastic approval."),
			{ TEXT("applauding an achievement"), TEXT("celebrating good news"), TEXT("showing enthusiastic approval") },
			{ TEXT("either hand is occupied"), TEXT("quiet restraint is required"), TEXT("the character must keep both arms available") },
			{ TEXT("applaud"), TEXT("acknowledge") },
			{},
			TEXT("clap"),
			TEXT("excited"),
			{ TEXT("clap"), TEXT("applaud"), TEXT("applause"), TEXT("celebrate"), TEXT("bravo") },
			false
		}
	};
}

TArray<FTemplateCatalogSeed> GetTemplateSeeds()
{
	return {
		{
			TEXT("/LLMNPCActionLayer/LLMNPC/MotionTemplates/Manny/MT_Nod_Manny_v1.MT_Nod_Manny_v1"),
			TEXT("gesture.nod.manny.v1.json"),
			TEXT("neutral"),
			{ TEXT("neutral"), TEXT("friendly"), TEXT("subtle") },
			TEXT("The head pitches down and returns once while the chest, arms, hands, and lower body remain under their existing animation."),
			{ TEXT("head") },
			{ TEXT("target_independent") },
			{ TEXT("acknowledge"), TEXT("affirm") },
			{},
			{ TEXT("head.nod") },
			0.35f,
			0.25f,
			0.30f,
			TEXT("Compact single-cycle head-only implementation.")
		},
		{
			TEXT("/LLMNPCActionLayer/LLMNPC/MotionTemplates/Manny/MT_Point_Target_Manny_v1.MT_Point_Target_Manny_v1"),
			TEXT("gesture.point.target.manny.v1.json"),
			TEXT("right_hand"),
			{ TEXT("neutral"), TEXT("friendly"), TEXT("subtle") },
			TEXT("The gaze turns toward a registered target while the right arm reaches toward it, the palm follows the target, and the index finger forms a pointing hand."),
			{ TEXT("gaze"), TEXT("one_arm"), TEXT("hand"), TEXT("fingers") },
			{ TEXT("target_required") },
			{ TEXT("indicate"), TEXT("direct_attention") },
			{ TEXT("scene_target") },
			{ TEXT("gaze.track"), TEXT("arm.reach"), TEXT("hand.pose.point") },
			0.65f,
			0.55f,
			0.55f,
			TEXT("Right-arm target-aware point with gaze and explicit finger pose.")
		},
		{
			TEXT("/LLMNPCActionLayer/LLMNPC/MotionTemplates/Manny/MT_Wave_Right_Manny_FK_v1.MT_Wave_Right_Manny_FK_v1"),
			TEXT("gesture.wave.right.manny.fk.v1.json"),
			TEXT("faithful_fk"),
			{ TEXT("neutral"), TEXT("friendly"), TEXT("excited") },
			TEXT("The right arm rises beside the upper body, the open palm faces outward, and the forearm, wrist, and fingers follow the reconstructed Waving reference motion."),
			{ TEXT("one_arm"), TEXT("hand"), TEXT("fingers") },
			{ TEXT("target_independent") },
			{ TEXT("greet"), TEXT("farewell"), TEXT("attract_attention") },
			{},
			{ TEXT("hand.wave_arc"), TEXT("hand.pose.open") },
			0.75f,
			0.70f,
			0.75f,
			TEXT("Reference-faithful FK wave used by normal runtime selection.")
		},
		{
			TEXT("/LLMNPCActionLayer/LLMNPC/MotionTemplates/Manny/MT_Wave_Right_Manny_Procedural_v1.MT_Wave_Right_Manny_Procedural_v1"),
			TEXT("gesture.wave.right.manny.procedural.v1.json"),
			TEXT("procedural"),
			{ TEXT("neutral"), TEXT("friendly"), TEXT("excited") },
			TEXT("The right hand follows a semantic wave anchor and a bounded lateral arc while wrist keyframes and an open-hand pose maintain the visible wave shape."),
			{ TEXT("one_arm"), TEXT("hand"), TEXT("fingers") },
			{ TEXT("target_independent") },
			{ TEXT("greet"), TEXT("farewell"), TEXT("attract_attention") },
			{},
			{ TEXT("hand.wave_arc"), TEXT("hand.pose.open") },
			0.75f,
			0.70f,
			0.75f,
			TEXT("Fully procedural comparison variant; excluded from normal model selection pending a later promotion.")
		},
		{
			TEXT("/LLMNPCActionLayer/LLMNPC/MotionTemplates/Manny/MT_Wave_Right_Manny_Subtle_v1.MT_Wave_Right_Manny_Subtle_v1"),
			TEXT("gesture.wave.right.manny.subtle.v1.json"),
			TEXT("subtle"),
			{ TEXT("subtle") },
			TEXT("The same reviewed right-hand wave is played with a smaller, quieter arm and wrist range for restrained greetings or farewells."),
			{ TEXT("one_arm"), TEXT("hand"), TEXT("fingers") },
			{ TEXT("target_independent") },
			{ TEXT("greet"), TEXT("farewell") },
			{},
			{ TEXT("hand.wave_arc"), TEXT("hand.pose.open") },
			0.45f,
			0.40f,
			0.45f,
			TEXT("Lower-amplitude Published style variant.")
		},
		{
			TEXT("/Game/LLMNPCActionLayer/MotionTemplates/MT_Wave_Asset_Manny_v1.MT_Wave_Asset_Manny_v1"),
			TEXT(""),
			TEXT("animation_asset"),
			{ TEXT("neutral"), TEXT("friendly") },
			TEXT("The reviewed Waving animation sequence plays as an upper-body Dynamic Montage with bounded blend and interruption settings."),
			{ TEXT("one_arm"), TEXT("hand"), TEXT("fingers") },
			{ TEXT("target_independent") },
			{ TEXT("greet"), TEXT("farewell"), TEXT("attract_attention") },
			{},
			{ TEXT("hand.wave_arc"), TEXT("hand.pose.open") },
			0.70f,
			0.65f,
			0.70f,
			TEXT("Animation Asset comparison variant retained for explicit demo playback.")
		}
	};
}

TArray<TSharedPtr<FJsonValue>> NamesToJson(const TArray<FName>& Names)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FName Name : Names)
	{
		Values.Add(MakeShared<FJsonValueString>(Name.ToString()));
	}
	return Values;
}

TArray<TSharedPtr<FJsonValue>> StringsToJson(const TArray<FString>& Strings)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FString& Value : Strings)
	{
		Values.Add(MakeShared<FJsonValueString>(Value));
	}
	return Values;
}

bool WriteJson(
	const FString& Path,
	const TSharedRef<FJsonObject>& Root,
	FString& OutError
)
{
	if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true))
	{
		OutError = TEXT("LLMNPC_N2_RESOURCE_DIRECTORY_CREATE_FAILED");
		return false;
	}
	FString Json;
	const TSharedRef<TJsonWriter<>> Writer =
		TJsonWriterFactory<>::Create(&Json);
	if (!FJsonSerializer::Serialize(Root, Writer) ||
		!FFileHelper::SaveStringToFile(
			Json,
			*Path,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM
		))
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_N2_RESOURCE_WRITE_FAILED:%s"),
			*Path
		);
		return false;
	}
	return true;
}

bool ExportVocabulary(
	const ULLMNPCActionVocabulary& Vocabulary,
	const FString& OutputPath,
	FString& OutError
)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("schema_version"), Vocabulary.SchemaVersion);
	Root->SetStringField(TEXT("vocabulary_id"), Vocabulary.VocabularyId.ToString());
	Root->SetStringField(TEXT("semantic_version"), Vocabulary.SemanticVersion);
	Root->SetNumberField(TEXT("revision"), Vocabulary.Revision);
	Root->SetStringField(TEXT("content_hash"), Vocabulary.ContentHash);
	TArray<TSharedPtr<FJsonValue>> Entries;
	for (const FLLMNPCActionVocabularyEntry& Entry : Vocabulary.Entries)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("tag"), Entry.Tag.ToString());
		Object->SetStringField(
			TEXT("display_name_en"),
			Entry.EnglishDisplayName.ToString()
		);
		Object->SetStringField(
			TEXT("display_name_zh"),
			Entry.ChineseDisplayName.ToString()
		);
		Object->SetArrayField(TEXT("synonyms"), NamesToJson(Entry.Synonyms));
		TArray<TSharedPtr<FJsonValue>> Fields;
		for (const ELLMNPCActionVocabularyField Field : Entry.AllowedFields)
		{
			Fields.Add(MakeShared<FJsonValueString>(
				StaticEnum<ELLMNPCActionVocabularyField>()->GetNameStringByValue(
					static_cast<int64>(Field)
				)
			));
		}
		Object->SetArrayField(TEXT("allowed_fields"), Fields);
		Object->SetBoolField(TEXT("deprecated"), Entry.bDeprecated);
		Object->SetStringField(
			TEXT("replacement_tag"),
			Entry.ReplacementTag.ToString()
		);
		Entries.Add(MakeShared<FJsonValueObject>(Object));
	}
	Root->SetArrayField(TEXT("entries"), Entries);
	return WriteJson(OutputPath, Root, OutError);
}

bool ExportDefinitions(
	const TArray<ULLMNPCPublicActionDefinition*>& Definitions,
	const FString& CatalogHash,
	const FString& OutputPath,
	FString& OutError
)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(
		TEXT("schema_version"),
		TEXT("llmnpc.public_action_catalog.v1")
	);
	Root->SetStringField(TEXT("catalog_hash"), CatalogHash);
	TArray<TSharedPtr<FJsonValue>> Values;
	TArray<ULLMNPCPublicActionDefinition*> Sorted = Definitions;
	Sorted.Sort(
		[](const ULLMNPCPublicActionDefinition& A, const ULLMNPCPublicActionDefinition& B)
		{
			return A.PublicActionId.LexicalLess(B.PublicActionId);
		}
	);
	for (const ULLMNPCPublicActionDefinition* Definition : Sorted)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(
			TEXT("schema_version"),
			Definition->CatalogSchemaVersion
		);
		Object->SetStringField(
			TEXT("public_action_id"),
			Definition->PublicActionId.ToString()
		);
		Object->SetStringField(
			TEXT("semantic_version"),
			Definition->SemanticVersion
		);
		Object->SetNumberField(
			TEXT("definition_revision"),
			Definition->DefinitionRevision
		);
		Object->SetStringField(
			TEXT("display_name"),
			Definition->DisplayName.ToString()
		);
		Object->SetStringField(
			TEXT("selection_summary"),
			Definition->SelectionSummary
		);
		Object->SetArrayField(
			TEXT("suitable_when"),
			StringsToJson(Definition->SuitableWhen)
		);
		Object->SetArrayField(
			TEXT("avoid_when"),
			StringsToJson(Definition->AvoidWhen)
		);
		Object->SetArrayField(
			TEXT("semantic_effect_tags"),
			NamesToJson(Definition->SemanticEffectTags)
		);
		Object->SetArrayField(
			TEXT("target_category_tags"),
			NamesToJson(Definition->TargetCategoryTags)
		);
		Object->SetStringField(
			TEXT("gesture_family"),
			Definition->GestureFamily.ToString()
		);
		Object->SetStringField(
			TEXT("default_style"),
			Definition->DefaultStyle.ToString()
		);
		Object->SetArrayField(
			TEXT("search_keywords"),
			StringsToJson(Definition->SearchKeywords)
		);
		Object->SetBoolField(
			TEXT("requires_target"),
			Definition->bRequiresTarget
		);
		Object->SetStringField(TEXT("content_hash"), Definition->ContentHash);
		Values.Add(MakeShared<FJsonValueObject>(Object));
	}
	Root->SetArrayField(TEXT("public_actions"), Values);
	return WriteJson(OutputPath, Root, OutError);
}

bool UpdateTemplateSource(
	const FString& SourcePath,
	const ULLMNPCMotionTemplate& Template,
	FString& OutError
)
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *SourcePath))
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_N2_TEMPLATE_SOURCE_READ_FAILED:%s"),
			*SourcePath
		);
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader =
		TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_N2_TEMPLATE_SOURCE_JSON_INVALID:%s"),
			*SourcePath
		);
		return false;
	}
	const FLLMNPCTemplateMetadata& Metadata = Template.Metadata;
	Root->SetStringField(
		TEXT("catalog_schema_version"),
		Metadata.CatalogSchemaVersion
	);
	Root->SetNumberField(TEXT("catalog_revision"), Metadata.CatalogRevision);
	Root->SetStringField(TEXT("variant_id"), Metadata.VariantId.ToString());
	Root->SetNumberField(TEXT("variant_weight"), Metadata.VariantWeight);
	Root->SetArrayField(
		TEXT("variant_style_tags"),
		NamesToJson(Metadata.VariantStyleTags)
	);
	Root->SetStringField(TEXT("visual_description"), Metadata.VisualDescription);
	Root->SetStringField(
		TEXT("catalog_content_hash"),
		Metadata.CatalogContentHash
	);
	TSharedPtr<FJsonObject> MetadataObject;
	const TSharedPtr<FJsonObject>* ExistingMetadata = nullptr;
	if (
		Root->TryGetObjectField(TEXT("metadata"), ExistingMetadata) &&
		ExistingMetadata &&
		ExistingMetadata->IsValid()
	)
	{
		MetadataObject = *ExistingMetadata;
	}
	else
	{
		MetadataObject = MakeShared<FJsonObject>();
		Root->SetObjectField(TEXT("metadata"), MetadataObject.ToSharedRef());
	}
	MetadataObject->SetArrayField(
		TEXT("body_region_tags"),
		NamesToJson(Metadata.BodyRegionTags)
	);
	MetadataObject->SetArrayField(
		TEXT("spatial_requirement_tags"),
		NamesToJson(Metadata.SpatialRequirementTags)
	);
	MetadataObject->SetArrayField(
		TEXT("semantic_effect_tags"),
		NamesToJson(Metadata.SemanticEffectTags)
	);
	MetadataObject->SetArrayField(
		TEXT("target_category_tags"),
		NamesToJson(Metadata.TargetCategoryTags)
	);
	MetadataObject->SetArrayField(
		TEXT("required_capabilities"),
		NamesToJson(Metadata.RequiredCapabilities)
	);
	MetadataObject->SetNumberField(
		TEXT("expressiveness"),
		Metadata.Expressiveness
	);
	MetadataObject->SetNumberField(TEXT("energy"), Metadata.Energy);
	MetadataObject->SetNumberField(
		TEXT("social_intensity"),
		Metadata.SocialIntensity
	);
	MetadataObject->SetStringField(
		TEXT("variant_difference"),
		Metadata.VariantDifference
	);
	MetadataObject->SetStringField(
		TEXT("kinematic_report_hash"),
		Metadata.KinematicReportHash
	);
	return WriteJson(SourcePath, Root.ToSharedRef(), OutError);
}
}

bool FLLMNPCMannyN2CatalogMigration::Run(
	FString& OutCatalogHash,
	FString& OutError
)
{
	OutCatalogHash.Reset();
	OutError.Reset();
	const TSharedPtr<IPlugin> Plugin =
		IPluginManager::Get().FindPlugin(TEXT("LLMNPCActionLayer"));
	if (!Plugin.IsValid())
	{
		OutError = TEXT("LLMNPC_N2_PLUGIN_NOT_FOUND");
		return false;
	}
	UE_LOG(
		LogLLMNPCMannyN2Migration,
		Display,
		TEXT("N2 migration: creating or loading vocabulary.")
	);

	bool bCreated = false;
	ULLMNPCActionVocabulary* Vocabulary =
		LoadOrCreateAsset<ULLMNPCActionVocabulary>(
			VocabularyPackagePath,
			VocabularyAssetName,
			bCreated,
			OutError
		);
	if (!Vocabulary)
	{
		return false;
	}
	UE_LOG(
		LogLLMNPCMannyN2Migration,
		Display,
		TEXT("N2 migration: populating vocabulary.")
	);
	PopulateVocabulary(*Vocabulary);
	UE_LOG(
		LogLLMNPCMannyN2Migration,
		Display,
		TEXT("N2 migration: validating and saving vocabulary.")
	);
	if (!Vocabulary->ValidateVocabulary(OutError) ||
		!SaveAsset(Vocabulary, OutError))
	{
		return false;
	}

	TArray<ULLMNPCPublicActionDefinition*> Definitions;
	for (const FPublicActionSeed& Seed : GetPublicActionSeeds())
	{
		UE_LOG(
			LogLLMNPCMannyN2Migration,
			Display,
			TEXT("N2 migration: public action %s."),
			Seed.PublicActionId
		);
		const FString PackagePath = FString::Printf(
			TEXT("/LLMNPCActionLayer/LLMNPC/PublicActions/%s"),
			Seed.AssetName
		);
		ULLMNPCPublicActionDefinition* Definition =
			LoadOrCreateAsset<ULLMNPCPublicActionDefinition>(
				PackagePath,
				Seed.AssetName,
				bCreated,
				OutError
			);
		if (!Definition)
		{
			return false;
		}
		Definition->PublicActionId = Seed.PublicActionId;
		Definition->SemanticVersion = TEXT("1.0.0");
		Definition->DefinitionRevision = 1;
		Definition->DisplayName = FText::FromString(Seed.DisplayName);
		Definition->SelectionSummary = Seed.SelectionSummary;
		Definition->SuitableWhen = Seed.SuitableWhen;
		Definition->AvoidWhen = Seed.AvoidWhen;
		Definition->SemanticEffectTags = Seed.SemanticEffects;
		Definition->TargetCategoryTags = Seed.TargetCategories;
		Definition->GestureFamily = Seed.GestureFamily;
		Definition->DefaultStyle = Seed.DefaultStyle;
		Definition->IncompatibleActionFamilies.Reset();
		Definition->SearchKeywords = Seed.SearchKeywords;
		Definition->bRequiresTarget = Seed.bRequiresTarget;
		Definition->CatalogSchemaVersion =
			LLMNPCCatalog::PublicActionSchemaVersion;
		Definition->ReviewState = ELLMNPCTemplateReviewState::Published;
		Definition->ReviewRecordJson =
			TEXT("{\"schema_version\":\"llmnpc.public_action_review.v1\",")
			TEXT("\"reviewer\":\"project_owner\",\"approved\":true,")
			TEXT("\"note\":\"Migrated from the visually accepted Manny Published action set.\"}");
		Definition->ContentHash.Reset();
		Definition->ContentHash =
			ULLMNPCPublicActionDefinition::BuildContentHash(*Definition);
		if (
			!Definition->ValidateDefinition(Vocabulary, OutError) ||
			!SaveAsset(Definition, OutError)
		)
		{
			return false;
		}
		Definitions.Add(Definition);
	}

	TArray<ULLMNPCMotionTemplate*> Templates;
	const FString TemplateSourceDirectory = FPaths::Combine(
		Plugin->GetBaseDir(),
		TEXT("Resources"),
		TEXT("Templates"),
		TEXT("Manny")
	);
	for (const FTemplateCatalogSeed& Seed : GetTemplateSeeds())
	{
		UE_LOG(
			LogLLMNPCMannyN2Migration,
			Display,
			TEXT("N2 migration: template %s."),
			Seed.AssetPath
		);
		ULLMNPCMotionTemplate* Template =
			LoadObject<ULLMNPCMotionTemplate>(nullptr, Seed.AssetPath);
		if (!Template)
		{
			OutError = FString::Printf(
				TEXT("LLMNPC_N2_TEMPLATE_LOAD_FAILED:%s"),
				Seed.AssetPath
			);
			return false;
		}
		Template->Modify();
		FLLMNPCTemplateMetadata& Metadata = Template->Metadata;
		Metadata.CatalogSchemaVersion = LLMNPCCatalog::SchemaVersion;
		Metadata.CatalogRevision = FMath::Max(
			Metadata.CatalogRevision,
			1
		);
		if (FString(Seed.AssetPath).Contains(TEXT("MT_Wave_Asset_Manny_v1")))
		{
			Metadata.PublicActionId = TEXT("gesture.wave.right");
			Metadata.bAllowRuntimeModelSelection = false;
		}
		Metadata.VariantId = Seed.VariantId;
		Metadata.VariantStyleTags = Seed.VariantStyles;
		Metadata.VisualDescription = Seed.VisualDescription;
		Metadata.BodyRegionTags = Seed.BodyRegions;
		Metadata.SpatialRequirementTags = Seed.SpatialRequirements;
		Metadata.SemanticEffectTags = Seed.SemanticEffects;
		Metadata.TargetCategoryTags = Seed.TargetCategories;
		Metadata.RequiredCapabilities = Seed.RequiredCapabilities;
		Metadata.Expressiveness = Seed.Expressiveness;
		Metadata.Energy = Seed.Energy;
		Metadata.SocialIntensity = Seed.SocialIntensity;
		Metadata.VariantDifference = Seed.VariantDifference;
		Metadata.KinematicReportHash = ValidationBaselineHash;
		Metadata.CatalogContentHash.Reset();
		Metadata.CatalogContentHash =
			ULLMNPCMotionTemplate::BuildCatalogContentHash(*Template);
		if (!Template->ValidateTemplate(OutError) ||
			!SaveAsset(Template, OutError))
		{
			return false;
		}
		if (
			!FString(Seed.SourceFilename).IsEmpty() &&
			!UpdateTemplateSource(
				FPaths::Combine(TemplateSourceDirectory, Seed.SourceFilename),
				*Template,
				OutError
			)
		)
		{
			return false;
		}
		Templates.Add(Template);
	}

	FLLMNPCTemplateSearchIndex Index;
	UE_LOG(
		LogLLMNPCMannyN2Migration,
		Display,
		TEXT("N2 migration: building final catalog index.")
	);
	const TSet<FName> Profiles = { TEXT("ue5_manny.v1") };
	if (!Index.Build(Templates, Definitions, Vocabulary, Profiles))
	{
		OutError = Index.GetDiagnostics().IsEmpty()
			? TEXT("LLMNPC_N2_CATALOG_BUILD_FAILED")
			: Index.GetDiagnostics()[0].Code.ToString();
		return false;
	}
	if (!Index.GetDiagnostics().IsEmpty())
	{
		OutError = Index.GetDiagnostics()[0].Code.ToString();
		return false;
	}
	OutCatalogHash = Index.GetCatalogHash();

	const FString CatalogDirectory = FPaths::Combine(
		Plugin->GetBaseDir(),
		TEXT("Resources"),
		TEXT("Catalog"),
		TEXT("Manny")
	);
	UE_LOG(
		LogLLMNPCMannyN2Migration,
		Display,
		TEXT("N2 migration: exporting JSON artifacts.")
	);
	if (
		!ExportVocabulary(
			*Vocabulary,
			FPaths::Combine(
				Plugin->GetBaseDir(),
				TEXT("Resources"),
				TEXT("Vocabulary"),
				TEXT("action_vocabulary_v1.json")
			),
			OutError
		) ||
		!ExportDefinitions(
			Definitions,
			OutCatalogHash,
			FPaths::Combine(CatalogDirectory, TEXT("public_actions_v1.json")),
			OutError
		)
	)
	{
		return false;
	}
	return true;
}
