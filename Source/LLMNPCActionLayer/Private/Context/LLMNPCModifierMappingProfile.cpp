#include "Context/LLMNPCModifierMappingProfile.h"

namespace
{
FLLMNPCModifierMappingRule MakeRule(
	ELLMNPCModifierMappingStage Stage,
	ELLMNPCModifierInputField Input,
	ELLMNPCResolvedModifierField Output,
	const FVector2D& Contribution,
	int32 Priority,
	FName RequiredTag = NAME_None,
	const FVector2D& InputRange = FVector2D(0.0f, 1.0f)
)
{
	FLLMNPCModifierMappingRule Rule;
	Rule.Stage = Stage;
	Rule.InputField = Input;
	Rule.OutputModifier = Output;
	Rule.ContributionRange = Contribution;
	Rule.InputRange = InputRange;
	Rule.Priority = Priority;
	Rule.RequiredTag = RequiredTag;
	return Rule;
}

bool IsFiniteOrderedRange(const FVector2D& Range)
{
	return
		FMath::IsFinite(Range.X) &&
		FMath::IsFinite(Range.Y) &&
		Range.Y > Range.X;
}
}

bool ULLMNPCModifierMappingProfile::Validate(FString& OutError) const
{
	OutError.Reset();
	if (
		SchemaVersion != TEXT("llmnpc.modifier_mapping_profile.v1") ||
		ProfileId.IsNone()
	)
	{
		OutError = TEXT("LLMNPC_MODIFIER_MAPPING_IDENTITY_INVALID");
		return false;
	}

	for (const FLLMNPCModifierMappingRule& Rule : Rules)
	{
		if (
			!IsFiniteOrderedRange(Rule.InputRange) ||
			!FMath::IsFinite(Rule.ContributionRange.X) ||
			!FMath::IsFinite(Rule.ContributionRange.Y) ||
			Rule.Priority < -1000 ||
			Rule.Priority > 1000
		)
		{
			OutError = TEXT("LLMNPC_MODIFIER_MAPPING_RULE_INVALID");
			return false;
		}
	}
	return true;
}

TArray<FLLMNPCModifierMappingRule>
ULLMNPCModifierMappingProfile::BuildMannyDefaultRules()
{
	using Stage = ELLMNPCModifierMappingStage;
	using Input = ELLMNPCModifierInputField;
	using Output = ELLMNPCResolvedModifierField;

	TArray<FLLMNPCModifierMappingRule> Result = {
		MakeRule(
			Stage::Personality,
			Input::PersonalityExpressiveness,
			Output::Amplitude,
			FVector2D(0.8f, 1.133333f),
			10,
			NAME_None,
			FVector2D(0.25f, 1.5f)
		),
		MakeRule(
			Stage::Personality,
			Input::PersonalityShyness,
			Output::Amplitude,
			FVector2D(1.0f, 0.75f),
			20
		),
		MakeRule(
			Stage::Personality,
			Input::PersonalityShyness,
			Output::GazeEngagement,
			FVector2D(1.0f, 0.7f),
			21
		),
		MakeRule(
			Stage::Personality,
			Input::PersonalityShyness,
			Output::TorsoParticipation,
			FVector2D(1.0f, 0.85f),
			22
		),
		MakeRule(
			Stage::Emotion,
			Input::EmotionIntensity,
			Output::Amplitude,
			FVector2D(1.0f, 1.15f),
			10,
			TEXT("excited")
		),
		MakeRule(
			Stage::Emotion,
			Input::EmotionIntensity,
			Output::SpeedScale,
			FVector2D(1.0f, 1.1f),
			11,
			TEXT("excited")
		),
		MakeRule(
			Stage::Emotion,
			Input::EmotionIntensity,
			Output::GazeEngagement,
			FVector2D(1.0f, 1.1f),
			12,
			TEXT("excited")
		),
		MakeRule(
			Stage::Emotion,
			Input::EmotionIntensity,
			Output::SpeedScale,
			FVector2D(1.0f, 0.8f),
			20,
			TEXT("sad")
		),
		MakeRule(
			Stage::Emotion,
			Input::EmotionIntensity,
			Output::Amplitude,
			FVector2D(1.0f, 0.9f),
			21,
			TEXT("sad")
		),
		MakeRule(
			Stage::Emotion,
			Input::EmotionIntensity,
			Output::SpeedScale,
			FVector2D(1.0f, 1.12f),
			30,
			TEXT("angry")
		),
		MakeRule(
			Stage::Relationship,
			Input::RelationshipTrust,
			Output::GazeEngagement,
			FVector2D(0.9f, 1.1f),
			10,
			NAME_None,
			FVector2D(-1.0f, 1.0f)
		),
		MakeRule(
			Stage::Relationship,
			Input::RelationshipFamiliarity,
			Output::ReachScale,
			FVector2D(1.0f, 1.05f),
			20
		),
		MakeRule(
			Stage::Relationship,
			Input::RelationshipTrust,
			Output::Amplitude,
			FVector2D(0.85f, 1.0f),
			30,
			TEXT("tense"),
			FVector2D(-1.0f, 1.0f)
		)
	};
	return Result;
}

FPrimaryAssetId ULLMNPCModifierMappingProfile::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("LLMNPCModifierMappingProfile"), GetFName());
}
