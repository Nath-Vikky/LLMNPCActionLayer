#include "Authoring/LLMNPCMotionRecipeAuthoringPrompt.h"

#include "Capabilities/LLMNPCSkeletonCapabilityBuilder.h"
#include "Capabilities/LLMNPCSkeletonCapabilityTypes.h"
#include "Dom/JsonObject.h"
#include "Misc/SecureHash.h"
#include "MotionRecipe/LLMNPCMotionPrimitiveRegistry.h"
#include "MotionRecipe/LLMNPCMotionRecipeParser.h"
#include "MotionRecipe/LLMNPCMotionRecipeValidator.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Templates/LLMNPCMotionTemplate.h"

namespace
{
bool ParseObject(const FString& Json, TSharedPtr<FJsonObject>& OutObject)
{
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	return FJsonSerializer::Deserialize(Reader, OutObject) &&
		OutObject.IsValid();
}

bool SerializeObject(
	const TSharedRef<FJsonObject>& Object,
	FString& OutJson
)
{
	OutJson.Reset();
	const TSharedRef<TJsonWriter<>> Writer =
		TJsonWriterFactory<>::Create(&OutJson);
	return FJsonSerializer::Serialize(Object, Writer);
}

TArray<TSharedPtr<FJsonValue>> NameValues(const TArray<FName>& Names)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FName Name : Names)
	{
		Values.Add(MakeShared<FJsonValueString>(Name.ToString()));
	}
	return Values;
}

TArray<TSharedPtr<FJsonValue>> StringValues(
	const TArray<FString>& Strings
)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FString& String : Strings)
	{
		Values.Add(MakeShared<FJsonValueString>(String));
	}
	return Values;
}

bool HasOnlyFields(
	const TSharedPtr<FJsonObject>& Object,
	const TSet<FString>& AllowedFields,
	FString& OutUnexpectedField
)
{
	if (!Object.IsValid())
	{
		OutUnexpectedField = TEXT("<invalid_object>");
		return false;
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
	{
		if (!AllowedFields.Contains(Pair.Key))
		{
			OutUnexpectedField = Pair.Key;
			return false;
		}
	}
	return true;
}

bool ReadBoundedString(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	int32 MaxLength,
	FString& OutValue
)
{
	if (!Object->TryGetStringField(Field, OutValue))
	{
		return false;
	}
	OutValue = OutValue.TrimStartAndEnd();
	return !OutValue.IsEmpty() && OutValue.Len() <= MaxLength;
}

bool ReadBoundedStringArray(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	int32 MaxEntries,
	int32 MaxEntryLength,
	TArray<FString>& OutValues
)
{
	OutValues.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (
		!Object->TryGetArrayField(Field, Values) ||
		!Values ||
		Values->IsEmpty() ||
		Values->Num() > MaxEntries
	)
	{
		return false;
	}
	TSet<FString> Unique;
	for (const TSharedPtr<FJsonValue>& Value : *Values)
	{
		FString Text;
		if (!Value.IsValid() || !Value->TryGetString(Text))
		{
			return false;
		}
		Text = Text.TrimStartAndEnd();
		const FString Key = Text.ToLower();
		if (
			Text.IsEmpty() ||
			Text.Len() > MaxEntryLength ||
			Unique.Contains(Key)
		)
		{
			return false;
		}
		Unique.Add(Key);
		OutValues.Add(MoveTemp(Text));
	}
	return true;
}

TSharedRef<FJsonObject> BuildExampleObject(
	const ULLMNPCMotionTemplate& Template
)
{
	const FLLMNPCTemplateMetadata& Metadata = Template.Metadata;
	TSharedRef<FJsonObject> Example = MakeShared<FJsonObject>();
	Example->SetStringField(
		TEXT("public_action_id"),
		Metadata.PublicActionId.ToString()
	);
	Example->SetStringField(
		TEXT("variant_id"),
		Metadata.VariantId.ToString()
	);
	Example->SetStringField(
		TEXT("display_name"),
		Metadata.DisplayName.ToString()
	);
	Example->SetStringField(
		TEXT("selection_description"),
		Metadata.Description.ToString()
	);
	Example->SetStringField(
		TEXT("visual_description"),
		Metadata.VisualDescription
	);
	Example->SetArrayField(
		TEXT("style_tags"),
		NameValues(Metadata.VariantStyleTags)
	);
	Example->SetArrayField(
		TEXT("body_regions"),
		NameValues(Metadata.BodyRegionTags)
	);
	Example->SetArrayField(
		TEXT("semantic_effects"),
		NameValues(Metadata.SemanticEffectTags)
	);
	Example->SetBoolField(
		TEXT("requires_target"),
		Metadata.bRequiresTarget
	);
	return Example;
}
}

bool FLLMNPCMotionRecipeAuthoringPrompt::Build(
	const FString& DesiredAction,
	const FLLMNPCSkeletonCapabilitySnapshot& CapabilitySnapshot,
	const TArray<const ULLMNPCMotionTemplate*>& PublishedExamples,
	FLLMNPCMotionRecipePromptPackage& OutPackage,
	FString& OutError
)
{
	OutPackage = FLLMNPCMotionRecipePromptPackage();
	OutError.Reset();

	const FString CleanIntent = DesiredAction.TrimStartAndEnd();
	if (CleanIntent.IsEmpty() || CleanIntent.Len() > 600)
	{
		OutError = TEXT("LLMNPC_RECIPE_AUTHORING_INTENT_INVALID");
		return false;
	}

	FString CapabilityJson;
	if (!FLLMNPCSkeletonCapabilityBuilder::BuildModelViewJson(
		CapabilitySnapshot,
		CapabilityJson,
		OutError
	))
	{
		return false;
	}
	FString RestrictedField;
	if (FLLMNPCSkeletonCapabilityBuilder::ModelViewContainsRestrictedFields(
		CapabilityJson,
		RestrictedField
	))
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_RECIPE_AUTHORING_CAPABILITY_RESTRICTED:%s"),
			*RestrictedField
		);
		return false;
	}

	const FLLMNPCMotionPrimitiveRegistry& Registry =
		FLLMNPCMotionPrimitiveRegistry::Get();
	FString RecipeSchemaJson;
	if (!Registry.BuildModelSchemaJson(
		&CapabilitySnapshot,
		RecipeSchemaJson,
		OutError
	))
	{
		return false;
	}

	TSharedPtr<FJsonObject> CapabilityObject;
	TSharedPtr<FJsonObject> RecipeSchemaObject;
	if (
		!ParseObject(CapabilityJson, CapabilityObject) ||
		!ParseObject(RecipeSchemaJson, RecipeSchemaObject)
	)
	{
		OutError = TEXT("LLMNPC_RECIPE_AUTHORING_CONTEXT_JSON_INVALID");
		return false;
	}

	TSharedRef<FJsonObject> Request = MakeShared<FJsonObject>();
	Request->SetStringField(
		TEXT("schema_version"),
		TEXT("llmnpc.motion_recipe_authoring_request.v1")
	);
	Request->SetStringField(TEXT("desired_action"), CleanIntent);
	Request->SetStringField(
		TEXT("target_contract"),
		TEXT("No scene target is available for this generation request.")
	);
	Request->SetObjectField(
		TEXT("skeleton_capability"),
		CapabilityObject.ToSharedRef()
	);
	Request->SetObjectField(
		TEXT("motion_recipe_json_schema"),
		RecipeSchemaObject.ToSharedRef()
	);
	TSharedRef<FJsonObject> AuthoringConstraints =
		MakeShared<FJsonObject>();
	AuthoringConstraints->SetStringField(
		TEXT("phase"),
		TEXT("forward_n5_shrug")
	);
	AuthoringConstraints->SetStringField(
		TEXT("required_intent"),
		TEXT("express_uncertainty")
	);
	AuthoringConstraints->SetNumberField(
		TEXT("primitive_count"),
		1
	);
	AuthoringConstraints->SetArrayField(
		TEXT("allowed_primitive_ids"),
		StringValues({TEXT("shoulder.shrug")})
	);
	AuthoringConstraints->SetStringField(
		TEXT("timing_contract"),
		TEXT("Use one shoulder.shrug primitive from start 0 through the recipe duration. ")
		TEXT("Unreal supplies easing and the readable hold internally; do not create timing, hold, ")
		TEXT("pause, ease, transition, or helper primitives.")
	);
	Request->SetObjectField(
		TEXT("authoring_constraints"),
		AuthoringConstraints
	);

	TArray<const ULLMNPCMotionTemplate*> SortedExamples;
	for (const ULLMNPCMotionTemplate* Example : PublishedExamples)
	{
		if (
			Example &&
			Example->IsPublished() &&
			Example->SupportsSkeletonProfile(CapabilitySnapshot.ProfileId)
		)
		{
			SortedExamples.Add(Example);
		}
	}
	SortedExamples.Sort(
		[](const ULLMNPCMotionTemplate& A, const ULLMNPCMotionTemplate& B)
		{
			return A.Metadata.TemplateId.LexicalLess(B.Metadata.TemplateId);
		}
	);
	if (SortedExamples.Num() > 6)
	{
		SortedExamples.SetNum(6);
	}
	TArray<TSharedPtr<FJsonValue>> Examples;
	for (const ULLMNPCMotionTemplate* Example : SortedExamples)
	{
		Examples.Add(MakeShared<FJsonValueObject>(
			BuildExampleObject(*Example)
		));
	}
	Request->SetArrayField(TEXT("similar_published_templates"), Examples);

	TSharedRef<FJsonObject> ResponseContract = MakeShared<FJsonObject>();
	ResponseContract->SetStringField(
		TEXT("schema_version"),
		LLMNPCMotionRecipeAuthoring::ResponseSchemaVersion
	);
	ResponseContract->SetStringField(
		TEXT("status"),
		TEXT("recipe | unsupported")
	);
	ResponseContract->SetStringField(
		TEXT("recipe"),
		TEXT("Required only for status=recipe; must match motion_recipe_json_schema exactly.")
	);
	TSharedRef<FJsonObject> CatalogContract = MakeShared<FJsonObject>();
	CatalogContract->SetStringField(TEXT("display_name"), TEXT("1..80 characters"));
	CatalogContract->SetStringField(TEXT("selection_summary"), TEXT("1..240 characters"));
	CatalogContract->SetStringField(TEXT("visual_description"), TEXT("1..600 characters"));
	CatalogContract->SetStringField(TEXT("suitable_when"), TEXT("1..4 concise strings"));
	CatalogContract->SetStringField(TEXT("avoid_when"), TEXT("1..4 concise strings"));
	ResponseContract->SetObjectField(TEXT("catalog_draft"), CatalogContract);
	ResponseContract->SetStringField(
		TEXT("reason"),
		TEXT("Required only for status=unsupported.")
	);
	Request->SetObjectField(TEXT("response_contract"), ResponseContract);

	OutPackage.SystemPrompt =
		TEXT("You are the authoring model for an Unreal Engine upper-body NPC motion layer. ")
		TEXT("Act as a motion director, not as a skeleton controller. Use only semantic primitives and ")
		TEXT("bounded parameters present in the supplied JSON Schema. Never invent a primitive, field, ")
		TEXT("target, solver, asset path, control, joint, pose index, transform, rotation, or root motion. ")
		TEXT("Copy every primitive_id verbatim from a const value in the supplied Schema and obey the ")
		TEXT("authoring_constraints exactly. Words from prose are never primitive IDs. Use primitive start ")
		TEXT("and end values for timing; Unreal owns easing, holds, transitions, and pose synthesis. ")
		TEXT("Produce one interruptible action no longer than the supplied limits. The catalog text must describe ")
		TEXT("the exact visible result without overstating it. Return exactly one JSON object and no markdown. ")
		TEXT("Use schema_version llmnpc.motion_recipe_authoring_response.v1. For a supported request, set ")
		TEXT("status to recipe and return recipe plus catalog_draft. If the supplied capabilities cannot ")
		TEXT("express the requested action safely, set status to unsupported and return only a concise reason.");
	if (!SerializeObject(Request, OutPackage.UserJson))
	{
		OutError = TEXT("LLMNPC_RECIPE_AUTHORING_REQUEST_SERIALIZE_FAILED");
		return false;
	}

	OutPackage.RecipeSchemaJson = MoveTemp(RecipeSchemaJson);
	OutPackage.CapabilityModelViewJson = MoveTemp(CapabilityJson);
	OutPackage.CapabilityHash = CapabilitySnapshot.CapabilityHash;
	OutPackage.RegistryVersion = Registry.GetRegistryVersion();
	OutPackage.SimilarTemplateCount = SortedExamples.Num();
	const FString StablePrompt = FString::Printf(
		TEXT("%s\n%s\n%s"),
		LLMNPCMotionRecipeAuthoring::PromptVersion,
		*OutPackage.SystemPrompt,
		*OutPackage.UserJson
	);
	OutPackage.PromptHash = FString::Printf(
		TEXT("md5:%s"),
		*FMD5::HashAnsiString(*StablePrompt)
	);
	return true;
}

bool FLLMNPCMotionRecipeAuthoringPrompt::ValidateRecipeForCapability(
	const FLLMNPCMotionRecipeAuthoringResponse& Response,
	const FLLMNPCSkeletonCapabilitySnapshot& CapabilitySnapshot,
	const TSet<FName>& AllowedPrimitiveIds,
	FName RequiredIntent,
	int32 MaxPrimitiveCount,
	FString& OutError
)
{
	OutError.Reset();
	if (
		Response.bUnsupported ||
		Response.RecipeJson.IsEmpty() ||
		AllowedPrimitiveIds.IsEmpty() ||
		MaxPrimitiveCount <= 0
	)
	{
		OutError = TEXT("LLMNPC_RECIPE_AUTHORING_VALIDATION_INPUT_INVALID");
		return false;
	}

	FLLMNPCMotionRecipe Recipe;
	if (!FLLMNPCMotionRecipeParser::Parse(
		Response.RecipeJson,
		Recipe,
		OutError
	))
	{
		return false;
	}
	if (
		!RequiredIntent.IsNone() &&
		FName(*Recipe.Intent) != RequiredIntent
	)
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_RECIPE_AUTHORING_INTENT_MISMATCH:%s"),
			*Recipe.Intent
		);
		return false;
	}
	if (Recipe.Primitives.Num() > MaxPrimitiveCount)
	{
		OutError = TEXT("LLMNPC_RECIPE_AUTHORING_PRIMITIVE_COUNT_EXCEEDED");
		return false;
	}
	for (
		const FLLMNPCMotionRecipePrimitive& Primitive :
		Recipe.Primitives
	)
	{
		if (!AllowedPrimitiveIds.Contains(Primitive.PrimitiveId))
		{
			OutError = FString::Printf(
				TEXT("LLMNPC_RECIPE_AUTHORING_PRIMITIVE_NOT_ALLOWED:%s"),
				*Primitive.PrimitiveId.ToString()
			);
			return false;
		}
		if (!Primitive.TargetSlot.IsNone())
		{
			OutError =
				TEXT("LLMNPC_RECIPE_AUTHORING_TARGET_SLOT_NOT_ALLOWED");
			return false;
		}
	}

	FLLMNPCMotionRecipeValidationContext ValidationContext;
	ValidationContext.Mode =
		ELLMNPCMotionRecipeMode::AuthoringSandbox;
	ValidationContext.MaxPrimitiveCount = FMath::Min(
		ValidationContext.MaxPrimitiveCount,
		MaxPrimitiveCount
	);
	FLLMNPCMotionRecipeValidationResult Validation;
	if (!FLLMNPCMotionRecipeValidator::ValidateAndNormalize(
		Recipe,
		CapabilitySnapshot,
		FLLMNPCMotionPrimitiveRegistry::Get(),
		ValidationContext,
		Validation
	))
	{
		OutError = Validation.ErrorCode.IsEmpty()
			? TEXT("LLMNPC_RECIPE_AUTHORING_VALIDATION_FAILED")
			: Validation.ErrorCode;
		return false;
	}
	return true;
}

bool FLLMNPCMotionRecipeAuthoringPrompt::ParseResponse(
	const FString& ResponseJson,
	FLLMNPCMotionRecipeAuthoringResponse& OutResponse,
	FString& OutError
)
{
	OutResponse = FLLMNPCMotionRecipeAuthoringResponse();
	OutError.Reset();

	TSharedPtr<FJsonObject> Root;
	if (!ParseObject(ResponseJson, Root))
	{
		OutError = TEXT("LLMNPC_RECIPE_AUTHORING_RESPONSE_JSON_INVALID");
		return false;
	}
	FString SchemaVersion;
	FString Status;
	if (
		!Root->TryGetStringField(TEXT("schema_version"), SchemaVersion) ||
		SchemaVersion != LLMNPCMotionRecipeAuthoring::ResponseSchemaVersion ||
		!Root->TryGetStringField(TEXT("status"), Status)
	)
	{
		OutError = TEXT("LLMNPC_RECIPE_AUTHORING_RESPONSE_HEADER_INVALID");
		return false;
	}

	FString UnexpectedField;
	if (Status == TEXT("unsupported"))
	{
		if (
			!HasOnlyFields(
				Root,
				{TEXT("schema_version"), TEXT("status"), TEXT("reason")},
				UnexpectedField
			) ||
			!ReadBoundedString(
				Root,
				TEXT("reason"),
				400,
				OutResponse.UnsupportedReason
			)
		)
		{
			OutError = UnexpectedField.IsEmpty()
				? TEXT("LLMNPC_RECIPE_AUTHORING_UNSUPPORTED_INVALID")
				: FString::Printf(
					TEXT("LLMNPC_RECIPE_AUTHORING_RESPONSE_FIELD_UNKNOWN:%s"),
					*UnexpectedField
				);
			return false;
		}
		OutResponse.bUnsupported = true;
		return true;
	}
	if (Status != TEXT("recipe"))
	{
		OutError = TEXT("LLMNPC_RECIPE_AUTHORING_RESPONSE_STATUS_INVALID");
		return false;
	}
	if (!HasOnlyFields(
		Root,
		{
			TEXT("schema_version"),
			TEXT("status"),
			TEXT("recipe"),
			TEXT("catalog_draft")
		},
		UnexpectedField
	))
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_RECIPE_AUTHORING_RESPONSE_FIELD_UNKNOWN:%s"),
			*UnexpectedField
		);
		return false;
	}

	const TSharedPtr<FJsonObject>* Recipe = nullptr;
	const TSharedPtr<FJsonObject>* Catalog = nullptr;
	if (
		!Root->TryGetObjectField(TEXT("recipe"), Recipe) ||
		!Recipe ||
		!Recipe->IsValid() ||
		!Root->TryGetObjectField(TEXT("catalog_draft"), Catalog) ||
		!Catalog ||
		!Catalog->IsValid() ||
		!SerializeObject(Recipe->ToSharedRef(), OutResponse.RecipeJson)
	)
	{
		OutError = TEXT("LLMNPC_RECIPE_AUTHORING_RESPONSE_BODY_INVALID");
		return false;
	}
	if (!HasOnlyFields(
		*Catalog,
		{
			TEXT("display_name"),
			TEXT("selection_summary"),
			TEXT("visual_description"),
			TEXT("suitable_when"),
			TEXT("avoid_when")
		},
		UnexpectedField
	))
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_RECIPE_AUTHORING_CATALOG_FIELD_UNKNOWN:%s"),
			*UnexpectedField
		);
		return false;
	}
	if (
		!ReadBoundedString(
			*Catalog,
			TEXT("display_name"),
			80,
			OutResponse.CatalogDraft.DisplayName
		) ||
		!ReadBoundedString(
			*Catalog,
			TEXT("selection_summary"),
			240,
			OutResponse.CatalogDraft.SelectionSummary
		) ||
		!ReadBoundedString(
			*Catalog,
			TEXT("visual_description"),
			600,
			OutResponse.CatalogDraft.VisualDescription
		) ||
		!ReadBoundedStringArray(
			*Catalog,
			TEXT("suitable_when"),
			4,
			160,
			OutResponse.CatalogDraft.SuitableWhen
		) ||
		!ReadBoundedStringArray(
			*Catalog,
			TEXT("avoid_when"),
			4,
			160,
			OutResponse.CatalogDraft.AvoidWhen
		)
	)
	{
		OutError = TEXT("LLMNPC_RECIPE_AUTHORING_CATALOG_DRAFT_INVALID");
		return false;
	}
	return true;
}
