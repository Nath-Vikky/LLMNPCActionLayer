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

const TArray<FLLMNPCMotionRecipeAuthoringContract>& BuildContracts()
{
	static const TArray<FLLMNPCMotionRecipeAuthoringContract> Contracts = []
	{
		TArray<FLLMNPCMotionRecipeAuthoringContract> Result;

		FLLMNPCMotionRecipeAuthoringContract Shrug;
		Shrug.ContractId =
			LLMNPCMotionRecipeAuthoring::DefaultAuthoringContractId;
		Shrug.PublicActionId = TEXT("gesture.shrug");
		Shrug.DisplayName = FText::FromString(TEXT("Bilateral Shrug"));
		Shrug.DefaultDesiredAction =
			TEXT("A readable bilateral shrug that expresses uncertainty. ")
			TEXT("Raise both shoulders naturally, let the elbows open slightly, ")
			TEXT("keep both hands relaxed, then return smoothly to neutral.");
		Shrug.Phase = TEXT("forward_n5_shrug");
		Shrug.RequiredIntent = TEXT("express_uncertainty");
		Shrug.AllowedPrimitiveIds = {TEXT("shoulder.shrug")};
		Shrug.PrimitiveCount = 1;
		Shrug.MinDurationSeconds = 0.2;
		Shrug.MaxDurationSeconds = 4.0;
		Shrug.TimingContract =
			TEXT("Use one shoulder.shrug primitive from start 0 through the recipe duration. ")
			TEXT("Unreal supplies easing and the readable hold internally; do not create timing, hold, ")
			TEXT("pause, ease, transition, or helper primitives.");
		Shrug.RevisionPolicy =
			TEXT("Create a new Draft. Preserve the shrug intent and address only the bounded visual feedback.");
		Result.Add(MoveTemp(Shrug));

		FLLMNPCMotionRecipeAuthoringContract Clap;
		Clap.ContractId =
			LLMNPCMotionRecipeAuthoring::
				ProceduralClapAuthoringContractId;
		Clap.PublicActionId = TEXT("gesture.clap");
		Clap.DisplayName = FText::FromString(TEXT("Procedural Clap"));
		Clap.DefaultDesiredAction =
			TEXT("Two clear celebratory claps in front of the chest. ")
			TEXT("Approach with open relaxed palms, make readable contact, ")
			TEXT("separate between claps, then recover smoothly to neutral.");
		Clap.Phase = TEXT("forward_n7b_procedural_clap");
		Clap.RequiredIntent = TEXT("applaud");
		Clap.AllowedPrimitiveIds = {TEXT("hands.contact")};
		Clap.PrimitiveCount = 1;
		Clap.MinDurationSeconds = 0.8;
		Clap.MaxDurationSeconds = 3.2;
		Clap.TimingContract =
			TEXT("Use one hands.contact primitive from start 0 through the recipe duration. ")
			TEXT("Set cycles to the requested clap count. Unreal owns bilateral approach, ")
			TEXT("contact, separation, palm targeting, continuous easing, and recovery; ")
			TEXT("do not create hold, pause, timing, transition, or helper primitives.");
		Clap.RevisionPolicy =
			TEXT("Create a new Draft. Preserve the clap intent and address only the bounded visual feedback.");
		Result.Add(MoveTemp(Clap));

		FLLMNPCMotionRecipeAuthoringContract Beckon;
		Beckon.ContractId =
			LLMNPCMotionRecipeAuthoring::
				ProceduralBeckonAuthoringContractId;
		Beckon.PublicActionId = TEXT("gesture.beckon");
		Beckon.DisplayName =
			FText::FromString(TEXT("Procedural Beckon"));
		Beckon.DefaultDesiredAction =
			TEXT("Invite the primary scene target to come closer with one friendly palm-up hand. ")
			TEXT("Reach toward the target, curl the relaxed fingers inward for two readable invitations, ")
			TEXT("then recover smoothly to neutral.");
		Beckon.Phase =
			TEXT("forward_n7c_procedural_beckon");
		Beckon.RequiredIntent = TEXT("attract_attention");
		Beckon.AllowedPrimitiveIds = {TEXT("hand.beckon")};
		Beckon.PrimitiveCount = 1;
		Beckon.MinDurationSeconds = 0.9;
		Beckon.MaxDurationSeconds = 3.2;
		Beckon.AllowedTargetSlots = {TEXT("primary")};
		Beckon.bTargetRequired = true;
		Beckon.bAllowMirror = true;
		Beckon.TargetContract =
			TEXT("Exactly one semantic scene target is available as target_slot 'primary'. ")
			TEXT("Use that exact slot on hand.beckon. Unreal binds it to the live Actor, tracks motion, ")
			TEXT("limits reach and wrist orientation, and fades the gesture if the target is lost.");
		Beckon.TimingContract =
			TEXT("Use one hand.beckon primitive from start 0 through the recipe duration. ")
			TEXT("Set cycles to the requested invitation count. Unreal owns arm reach, palm-up targeting, ")
			TEXT("relaxed-to-curl finger curves, continuous easing, target tracking, and recovery; ")
			TEXT("do not create hold, pause, timing, transition, or helper primitives.");
		Beckon.RevisionPolicy =
			TEXT("Create a new Draft. Preserve the beckon intent and primary target contract, and address only the bounded visual feedback.");
		Result.Add(MoveTemp(Beckon));

		FLLMNPCMotionRecipeAuthoringContract Present;
		Present.ContractId =
			LLMNPCMotionRecipeAuthoring::
				ProceduralPresentAuthoringContractId;
		Present.PublicActionId = TEXT("gesture.present");
		Present.DisplayName =
			FText::FromString(TEXT("Targeted Open-Palm Present"));
		Present.DefaultDesiredAction =
			TEXT("Present the primary scene target with one helpful open palm. ")
			TEXT("Extend one arm toward the target with a comfortably bent elbow, ")
			TEXT("turn the open palm upward, keep every finger naturally extended, ")
			TEXT("then recover smoothly to neutral.");
		Present.Phase =
			TEXT("forward_n7d_procedural_present");
		Present.RequiredIntent = TEXT("indicate");
		Present.AllowedPrimitiveIds = {TEXT("arm.present")};
		Present.PrimitiveCount = 1;
		Present.MinDurationSeconds = 0.8;
		Present.MaxDurationSeconds = 3.0;
		Present.AllowedTargetSlots = {TEXT("primary")};
		Present.bTargetRequired = true;
		Present.bAllowMirror = true;
		Present.TargetContract =
			TEXT("Exactly one semantic scene target is available as target_slot 'primary'. ")
			TEXT("Use that exact slot on arm.present. Unreal binds it to the live Actor, ")
			TEXT("limits reach, keeps the palm upward, and fades the gesture if the target is lost.");
		Present.TimingContract =
			TEXT("Use one arm.present primitive from start 0 through the recipe duration. ")
			TEXT("Unreal owns arm IK, the palm-up wrist constraint, open-finger calibration, ")
			TEXT("continuous easing, target tracking, and recovery; do not create hold, pause, ")
			TEXT("timing, transition, or helper primitives.");
		Present.RevisionPolicy =
			TEXT("Create a new Draft. Preserve the open-palm presentation intent and primary target contract, and address only the bounded visual feedback.");
		Result.Add(MoveTemp(Present));

		FLLMNPCMotionRecipeAuthoringContract ThumbsUp;
		ThumbsUp.ContractId =
			LLMNPCMotionRecipeAuthoring::
				ProceduralThumbsUpAuthoringContractId;
		ThumbsUp.PublicActionId = TEXT("gesture.thumbs_up");
		ThumbsUp.DisplayName =
			FText::FromString(TEXT("Procedural Thumbs Up"));
		ThumbsUp.DefaultDesiredAction =
			TEXT("Give one clear, friendly thumbs-up to signal approval or agreement. ")
			TEXT("Raise one hand near the upper chest with a comfortably bent elbow, ")
			TEXT("face the hand outward, extend the thumb upward, curl the other four fingers naturally, ")
			TEXT("hold the readable sign briefly, then recover smoothly to neutral.");
		ThumbsUp.Phase =
			TEXT("forward_n7e_procedural_thumbs_up");
		ThumbsUp.RequiredIntent = TEXT("agree");
		ThumbsUp.AllowedPrimitiveIds = {TEXT("hand.thumbs_up")};
		ThumbsUp.PrimitiveCount = 1;
		ThumbsUp.MinDurationSeconds = 0.8;
		ThumbsUp.MaxDurationSeconds = 2.6;
		ThumbsUp.bAllowMirror = true;
		ThumbsUp.TimingContract =
			TEXT("Use one hand.thumbs_up primitive from start 0 through the recipe duration. ")
			TEXT("Unreal owns the upper-chest arm anchor, bent-elbow IK, outward wrist orientation, ")
			TEXT("calibrated thumb extension, four-finger curl, continuous easing, readable hold, and recovery; ")
			TEXT("do not create hold, pause, timing, transition, target, or helper primitives.");
		ThumbsUp.RevisionPolicy =
			TEXT("Create a new Draft. Preserve the approval or agreement intent and address only the bounded visual feedback.");
		Result.Add(MoveTemp(ThumbsUp));

		return Result;
	}();
	return Contracts;
}

bool CapabilitySupportsPrimitive(
	const FLLMNPCSkeletonCapabilitySnapshot& CapabilitySnapshot,
	FName PrimitiveId
)
{
	return CapabilitySnapshot.Capabilities.ContainsByPredicate(
		[PrimitiveId](const FLLMNPCSemanticCapability& Capability)
		{
			return Capability.CapabilityId == PrimitiveId;
		}
	);
}

bool ValidateRecipeForCapabilityInternal(
	const FLLMNPCMotionRecipeAuthoringResponse& Response,
	const FLLMNPCSkeletonCapabilitySnapshot& CapabilitySnapshot,
	const TSet<FName>& AllowedPrimitiveIds,
	const TSet<FName>& AllowedTargetSlots,
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
		OutError =
			TEXT("LLMNPC_RECIPE_AUTHORING_PRIMITIVE_COUNT_EXCEEDED");
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
		if (
			!Primitive.TargetSlot.IsNone() &&
			!AllowedTargetSlots.Contains(Primitive.TargetSlot)
		)
		{
			OutError = AllowedTargetSlots.IsEmpty()
				? TEXT("LLMNPC_RECIPE_AUTHORING_TARGET_SLOT_NOT_ALLOWED")
				: FString::Printf(
					TEXT("LLMNPC_RECIPE_AUTHORING_TARGET_SLOT_UNKNOWN:%s"),
					*Primitive.TargetSlot.ToString()
				);
			return false;
		}
	}

	FLLMNPCMotionRecipeValidationContext ValidationContext;
	ValidationContext.Mode =
		ELLMNPCMotionRecipeMode::AuthoringSandbox;
	ValidationContext.AllowedTargetSlots = AllowedTargetSlots;
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
}

const TArray<FLLMNPCMotionRecipeAuthoringContract>&
FLLMNPCMotionRecipeAuthoringPrompt::GetContracts()
{
	return BuildContracts();
}

const FLLMNPCMotionRecipeAuthoringContract*
FLLMNPCMotionRecipeAuthoringPrompt::FindContract(FName ContractId)
{
	return BuildContracts().FindByPredicate(
		[ContractId](
			const FLLMNPCMotionRecipeAuthoringContract& Contract
		)
		{
			return Contract.ContractId == ContractId;
		}
	);
}

const FLLMNPCMotionRecipeAuthoringContract*
FLLMNPCMotionRecipeAuthoringPrompt::FindContractForPublicAction(
	FName PublicActionId
)
{
	return BuildContracts().FindByPredicate(
		[PublicActionId](
			const FLLMNPCMotionRecipeAuthoringContract& Contract
		)
		{
			return Contract.PublicActionId == PublicActionId;
		}
	);
}

bool FLLMNPCMotionRecipeAuthoringPrompt::Build(
	const FString& DesiredAction,
	const FLLMNPCSkeletonCapabilitySnapshot& CapabilitySnapshot,
	const TArray<const ULLMNPCMotionTemplate*>& PublishedExamples,
	FLLMNPCMotionRecipePromptPackage& OutPackage,
	FString& OutError,
	const FLLMNPCMotionRecipeRequestContext& RequestContext
)
{
	OutPackage = FLLMNPCMotionRecipePromptPackage();
	OutError.Reset();

	const FLLMNPCMotionRecipeAuthoringContract* Contract =
		FindContract(RequestContext.AuthoringContractId);
	if (!Contract)
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_RECIPE_AUTHORING_CONTRACT_UNKNOWN:%s"),
			*RequestContext.AuthoringContractId.ToString()
		);
		return false;
	}
	if (
		Contract->AllowedPrimitiveIds.IsEmpty() ||
		Contract->PrimitiveCount <= 0 ||
		Contract->MinDurationSeconds < 0.05 ||
		Contract->MaxDurationSeconds <
			Contract->MinDurationSeconds ||
		(
			Contract->bTargetRequired &&
			Contract->AllowedTargetSlots.IsEmpty()
		) ||
		Contract->AllowedTargetSlots.Num() >
			LLMNPCMotionRecipe::DefaultMaxTargetCount
	)
	{
		OutError =
			TEXT("LLMNPC_RECIPE_AUTHORING_CONTRACT_INVALID");
		return false;
	}
	for (const FName PrimitiveId : Contract->AllowedPrimitiveIds)
	{
		if (
			!FLLMNPCMotionPrimitiveRegistry::Get().Find(PrimitiveId) ||
			!CapabilitySupportsPrimitive(
				CapabilitySnapshot,
				PrimitiveId
			)
		)
		{
			OutError = FString::Printf(
				TEXT("LLMNPC_RECIPE_AUTHORING_CONTRACT_UNSUPPORTED:%s"),
				*PrimitiveId.ToString()
			);
			return false;
		}
	}

	const FString CleanIntent = DesiredAction.TrimStartAndEnd();
	if (CleanIntent.IsEmpty() || CleanIntent.Len() > 600)
	{
		OutError = TEXT("LLMNPC_RECIPE_AUTHORING_INTENT_INVALID");
		return false;
	}
	const bool bManualRequest =
		RequestContext.TriggerSource ==
			LLMNPCMotionRecipeAuthoring::ManualTriggerSource;
	const bool bRegeneration = RequestContext.IsRegeneration();
	if (
		(!bManualRequest && !bRegeneration) ||
		(
			bManualRequest &&
			(
				!RequestContext.SourceTemplateId.IsNone() ||
				!RequestContext.SourceRecipeHash.IsEmpty() ||
				!RequestContext.ReviewFeedback.IsEmpty()
			)
		) ||
		(
			bRegeneration &&
			(
				RequestContext.SourceTemplateId.IsNone() ||
				RequestContext.SourceRecipeHash.TrimStartAndEnd().IsEmpty() ||
				RequestContext.SourceRecipeHash.Len() > 160 ||
				RequestContext.ReviewFeedback.TrimStartAndEnd().IsEmpty() ||
				RequestContext.ReviewFeedback.Len() > 600
			)
		)
	)
	{
		OutError =
			TEXT("LLMNPC_RECIPE_AUTHORING_REQUEST_CONTEXT_INVALID");
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
		TEXT("llmnpc.motion_recipe_authoring_request.v2")
	);
	Request->SetStringField(
		TEXT("trigger_source"),
		RequestContext.TriggerSource.ToString()
	);
	Request->SetStringField(TEXT("desired_action"), CleanIntent);
	if (bRegeneration)
	{
		TSharedRef<FJsonObject> RevisionContext =
			MakeShared<FJsonObject>();
		RevisionContext->SetStringField(
			TEXT("source_template_id"),
			RequestContext.SourceTemplateId.ToString()
		);
		RevisionContext->SetStringField(
			TEXT("source_recipe_hash"),
			RequestContext.SourceRecipeHash
		);
		RevisionContext->SetStringField(
			TEXT("human_review_feedback"),
			RequestContext.ReviewFeedback.TrimStartAndEnd()
		);
		RevisionContext->SetStringField(
			TEXT("revision_policy"),
			Contract->RevisionPolicy
		);
		Request->SetObjectField(
			TEXT("revision_context"),
			RevisionContext
		);
	}
	Request->SetStringField(
		TEXT("target_contract"),
		Contract->TargetContract
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
		TEXT("contract_id"),
		Contract->ContractId.ToString()
	);
	AuthoringConstraints->SetStringField(
		TEXT("public_action_id"),
		Contract->PublicActionId.ToString()
	);
	AuthoringConstraints->SetStringField(
		TEXT("phase"),
		Contract->Phase.ToString()
	);
	AuthoringConstraints->SetStringField(
		TEXT("required_intent"),
		Contract->RequiredIntent.ToString()
	);
	AuthoringConstraints->SetNumberField(
		TEXT("primitive_count"),
		Contract->PrimitiveCount
	);
	TSharedRef<FJsonObject> DurationRange =
		MakeShared<FJsonObject>();
	DurationRange->SetNumberField(
		TEXT("min"),
		Contract->MinDurationSeconds
	);
	DurationRange->SetNumberField(
		TEXT("max"),
		Contract->MaxDurationSeconds
	);
	AuthoringConstraints->SetObjectField(
		TEXT("duration_range_seconds"),
		DurationRange
	);
	AuthoringConstraints->SetBoolField(
		TEXT("primitive_must_cover_recipe"),
		Contract->bPrimitiveCoversRecipe
	);
	TArray<FName> AllowedPrimitiveIds =
		Contract->AllowedPrimitiveIds.Array();
	AllowedPrimitiveIds.Sort(FNameLexicalLess());
	AuthoringConstraints->SetArrayField(
		TEXT("allowed_primitive_ids"),
		NameValues(AllowedPrimitiveIds)
	);
	TArray<FName> AllowedTargetSlots =
		Contract->AllowedTargetSlots.Array();
	AllowedTargetSlots.Sort(FNameLexicalLess());
	AuthoringConstraints->SetArrayField(
		TEXT("allowed_target_slots"),
		NameValues(AllowedTargetSlots)
	);
	AuthoringConstraints->SetBoolField(
		TEXT("target_required"),
		Contract->bTargetRequired
	);
	AuthoringConstraints->SetBoolField(
		TEXT("mirror_allowed_after_authoring"),
		Contract->bAllowMirror
	);
	AuthoringConstraints->SetStringField(
		TEXT("timing_contract"),
		Contract->TimingContract
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
		[Contract](
			const ULLMNPCMotionTemplate& A,
			const ULLMNPCMotionTemplate& B
		)
		{
			const bool bAMatches =
				A.Metadata.PublicActionId == Contract->PublicActionId;
			const bool bBMatches =
				B.Metadata.PublicActionId == Contract->PublicActionId;
			if (bAMatches != bBMatches)
			{
				return bAMatches;
			}
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
		TEXT("Use only semantic target slots listed in authoring_constraints.allowed_target_slots; ")
		TEXT("never substitute an Actor name, asset, coordinate, or invented target. ")
		TEXT("Copy every primitive_id verbatim from a const value in the supplied Schema and obey the ")
		TEXT("authoring_constraints exactly. Words from prose are never primitive IDs. Use primitive start ")
		TEXT("and end values for timing; Unreal owns easing, holds, transitions, and pose synthesis. ")
		TEXT("When revision_context is present, address its human_review_feedback only through the supplied ")
		TEXT("semantic primitive parameters; never weaken constraints or change the requested action. ")
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
	OutPackage.AuthoringContract = *Contract;
	OutPackage.RequestContext = RequestContext;
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
	const FLLMNPCMotionRecipeAuthoringContract& Contract,
	FString& OutError
)
{
	if (!ValidateRecipeForCapabilityInternal(
		Response,
		CapabilitySnapshot,
		Contract.AllowedPrimitiveIds,
		Contract.AllowedTargetSlots,
		Contract.RequiredIntent,
		Contract.PrimitiveCount,
		OutError
	))
	{
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
	if (Recipe.Primitives.Num() != Contract.PrimitiveCount)
	{
		OutError =
			TEXT("LLMNPC_RECIPE_AUTHORING_PRIMITIVE_COUNT_MISMATCH");
		return false;
	}
	if (
		Recipe.DurationSeconds < Contract.MinDurationSeconds ||
		Recipe.DurationSeconds > Contract.MaxDurationSeconds
	)
	{
		OutError =
			TEXT("LLMNPC_RECIPE_AUTHORING_DURATION_OUT_OF_CONTRACT");
		return false;
	}
	if (Contract.bPrimitiveCoversRecipe)
	{
		for (
			const FLLMNPCMotionRecipePrimitive& Primitive :
			Recipe.Primitives
		)
		{
			if (
				!FMath::IsNearlyZero(
					Primitive.StartTimeSeconds,
					0.0001
				) ||
				!FMath::IsNearlyEqual(
					Primitive.EndTimeSeconds,
					Recipe.DurationSeconds,
					0.0001
				)
			)
			{
				OutError =
					TEXT("LLMNPC_RECIPE_AUTHORING_TIMING_COVERAGE_MISMATCH");
				return false;
			}
		}
	}
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
	return ValidateRecipeForCapabilityInternal(
		Response,
		CapabilitySnapshot,
		AllowedPrimitiveIds,
		TSet<FName>(),
		RequiredIntent,
		MaxPrimitiveCount,
		OutError
	);
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
