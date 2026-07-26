#include "Protocol/LLMNPCTurnRequestV3Adapter.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
TArray<TSharedPtr<FJsonValue>> V3NamesToJson(const TArray<FName>& Names)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FName Name : Names)
	{
		Values.Add(MakeShared<FJsonValueString>(Name.ToString()));
	}
	return Values;
}

TArray<TSharedPtr<FJsonValue>> V3StringsToJson(const TArray<FString>& Strings)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FString& Value : Strings)
	{
		Values.Add(MakeShared<FJsonValueString>(Value));
	}
	return Values;
}

TArray<TSharedPtr<FJsonValue>> V3RangeToJson(const FVector2D& Range)
{
	return {
		MakeShared<FJsonValueNumber>(Range.X),
		MakeShared<FJsonValueNumber>(Range.Y)
	};
}

TSharedRef<FJsonObject> BuildV3CandidateJsonObject(
	const FLLMNPCTemplateCandidate& Candidate
)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("selection_id"), Candidate.SelectionId.ToString());
	Object->SetStringField(TEXT("selection_summary"), Candidate.SelectionSummary);
	Object->SetArrayField(TEXT("suitable_when"), V3StringsToJson(Candidate.SuitableWhen));
	Object->SetArrayField(TEXT("avoid_when"), V3StringsToJson(Candidate.AvoidWhen));
	Object->SetArrayField(TEXT("body_regions"), V3NamesToJson(Candidate.BodyRegionTags));
	Object->SetArrayField(
		TEXT("semantic_effects"),
		V3NamesToJson(Candidate.SemanticEffectTags)
	);

	TSharedRef<FJsonObject> TargetContract = MakeShared<FJsonObject>();
	TargetContract->SetBoolField(TEXT("requires_target"), Candidate.bRequiresTarget);
	TargetContract->SetArrayField(
		TEXT("allowed_categories"),
		V3NamesToJson(Candidate.TargetCategoryTags)
	);
	Object->SetObjectField(TEXT("target_contract"), TargetContract);

	TArray<TSharedPtr<FJsonValue>> StyleValues;
	for (const FLLMNPCCandidateStyleOption& Style : Candidate.StyleOptions)
	{
		TSharedRef<FJsonObject> StyleObject = MakeShared<FJsonObject>();
		StyleObject->SetStringField(TEXT("style"), Style.Style.ToString());
		StyleObject->SetArrayField(
			TEXT("amplitude"),
			V3RangeToJson(Style.AmplitudeRange)
		);
		StyleObject->SetArrayField(
			TEXT("speed_scale"),
			V3RangeToJson(Style.SpeedRange)
		);
		StyleObject->SetArrayField(
			TEXT("duration_scale"),
			V3RangeToJson(Style.DurationRange)
		);
		StyleObject->SetBoolField(TEXT("mirror_allowed"), Style.bMirrorAllowed);
		StyleValues.Add(MakeShared<FJsonValueObject>(StyleObject));
	}
	Object->SetArrayField(TEXT("style_options"), StyleValues);
	Object->SetStringField(
		TEXT("recommended_style"),
		Candidate.RecommendedStyle.ToString()
	);
	Object->SetArrayField(
		TEXT("allowed_target_refs"),
		V3StringsToJson(Candidate.AllowedTargetRefs)
	);
	Object->SetStringField(TEXT("default_target_ref"), Candidate.DefaultTargetRef);
	Object->SetBoolField(TEXT("mirror_recommended"), Candidate.bMirrorRecommended);
	return Object;
}
}

bool FLLMNPCTurnRequestV3Adapter::BuildV2SafeCandidate(
	const FLLMNPCTemplateCandidate& Source,
	FLLMNPCTemplateCandidate& OutCandidate,
	FString& OutError
)
{
	OutError.Reset();
	if (Source.StyleOptions.IsEmpty())
	{
		OutError = TEXT("LLMNPC_TURN_V2_ADAPTER_STYLE_OPTIONS_EMPTY");
		return false;
	}

	OutCandidate = Source;
	OutCandidate.AmplitudeRange = Source.StyleOptions[0].AmplitudeRange;
	OutCandidate.SpeedRange = Source.StyleOptions[0].SpeedRange;
	OutCandidate.DurationRange = Source.StyleOptions[0].DurationRange;
	OutCandidate.bAllowMirror = Source.StyleOptions[0].bMirrorAllowed;
	OutCandidate.AllowedStyles.Reset();
	for (const FLLMNPCCandidateStyleOption& Style : Source.StyleOptions)
	{
		OutCandidate.AmplitudeRange.X = FMath::Max(
			OutCandidate.AmplitudeRange.X,
			Style.AmplitudeRange.X
		);
		OutCandidate.AmplitudeRange.Y = FMath::Min(
			OutCandidate.AmplitudeRange.Y,
			Style.AmplitudeRange.Y
		);
		OutCandidate.SpeedRange.X = FMath::Max(
			OutCandidate.SpeedRange.X,
			Style.SpeedRange.X
		);
		OutCandidate.SpeedRange.Y = FMath::Min(
			OutCandidate.SpeedRange.Y,
			Style.SpeedRange.Y
		);
		OutCandidate.DurationRange.X = FMath::Max(
			OutCandidate.DurationRange.X,
			Style.DurationRange.X
		);
		OutCandidate.DurationRange.Y = FMath::Min(
			OutCandidate.DurationRange.Y,
			Style.DurationRange.Y
		);
		OutCandidate.bAllowMirror &= Style.bMirrorAllowed;
		OutCandidate.AllowedStyles.Add(Style.Style);
	}
	if (
		OutCandidate.AmplitudeRange.X > OutCandidate.AmplitudeRange.Y ||
		OutCandidate.SpeedRange.X > OutCandidate.SpeedRange.Y ||
		OutCandidate.DurationRange.X > OutCandidate.DurationRange.Y
	)
	{
		OutError = TEXT("LLMNPC_TURN_V2_ADAPTER_UNSAFE_STYLE_INTERSECTION");
		return false;
	}
	OutCandidate.Description = FText::FromString(Source.SelectionSummary);
	OutCandidate.RecommendedAmplitude = FMath::Clamp(
		Source.RecommendedAmplitude,
		OutCandidate.AmplitudeRange.X,
		OutCandidate.AmplitudeRange.Y
	);
	OutCandidate.RecommendedSpeedScale = FMath::Clamp(
		Source.RecommendedSpeedScale,
		OutCandidate.SpeedRange.X,
		OutCandidate.SpeedRange.Y
	);
	OutCandidate.RecommendedDurationScale = FMath::Clamp(
		Source.RecommendedDurationScale,
		OutCandidate.DurationRange.X,
		OutCandidate.DurationRange.Y
	);
	return true;
}

void FLLMNPCTurnRequestV3Adapter::AdaptCandidatesForSchema(
	const FString& TargetSchemaVersion,
	const TArray<FLLMNPCTemplateCandidate>& SourceCandidates,
	TArray<FLLMNPCTemplateCandidate>& OutCandidates,
	TArray<FName>* OutExcludedSelectionIds
)
{
	OutCandidates.Reset();
	if (OutExcludedSelectionIds)
	{
		OutExcludedSelectionIds->Reset();
	}
	if (TargetSchemaVersion == TEXT("llmnpc.turn_request.v3"))
	{
		OutCandidates = SourceCandidates;
		return;
	}
	for (const FLLMNPCTemplateCandidate& Candidate : SourceCandidates)
	{
		FLLMNPCTemplateCandidate SafeCandidate;
		FString Error;
		if (BuildV2SafeCandidate(Candidate, SafeCandidate, Error))
		{
			OutCandidates.Add(MoveTemp(SafeCandidate));
		}
		else if (OutExcludedSelectionIds)
		{
			OutExcludedSelectionIds->Add(Candidate.SelectionId);
		}
	}
}

const FLLMNPCCandidateStyleOption*
FLLMNPCTurnRequestV3Adapter::FindStyleOption(
	const FLLMNPCTemplateCandidate& Candidate,
	FName Style
)
{
	return Candidate.StyleOptions.FindByPredicate(
		[Style](const FLLMNPCCandidateStyleOption& Option)
		{
			return Option.Style == Style;
		}
	);
}

FString FLLMNPCTurnRequestV3Adapter::BuildCandidateCardPreviewJson(
	const FLLMNPCTemplateCandidate& Candidate
)
{
	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(BuildV3CandidateJsonObject(Candidate), Writer);
	return Json;
}
