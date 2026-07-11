#include "Style/LLMNPCStyleResolver.h"

namespace
{
bool Allows(const TArray<FName>& AllowedStyles, FName Style)
{
	return AllowedStyles.IsEmpty() || AllowedStyles.Contains(Style);
}
}

FName ULLMNPCStyleResolver::ResolveRecommendedStyle(
	const FLLMNPCSelectionContextSnapshot& Context,
	const TArray<FName>& AllowedStyles
)
{
	if (
		Context.Emotion.PrimaryEmotion == TEXT("excited") &&
		Context.Emotion.Intensity >= 0.4f &&
		Allows(AllowedStyles, TEXT("excited"))
	)
	{
		return TEXT("excited");
	}
	if (
		(Context.Personality.Shyness >= 0.55f || Context.Personality.PersonalityTags.Contains(TEXT("shy"))) &&
		Allows(AllowedStyles, TEXT("subtle"))
	)
	{
		return TEXT("subtle");
	}
	if (
		(
			Context.Emotion.PrimaryEmotion == TEXT("friendly") ||
			Context.Personality.Sociability >= 0.65f ||
			Context.Relationship.Affinity >= 0.35f
		) &&
		Allows(AllowedStyles, TEXT("friendly"))
	)
	{
		return TEXT("friendly");
	}
	if (Allows(AllowedStyles, TEXT("neutral")))
	{
		return TEXT("neutral");
	}
	return AllowedStyles.IsEmpty() ? FName(TEXT("neutral")) : AllowedStyles[0];
}

FLLMNPCStylePreset ULLMNPCStyleResolver::GetBuiltInPreset(FName StyleTag)
{
	FLLMNPCStylePreset Preset;
	Preset.StyleTag = StyleTag.IsNone() ? FName(TEXT("neutral")) : StyleTag;
	if (Preset.StyleTag == TEXT("subtle"))
	{
		Preset.AmplitudeScale = 0.82f;
		Preset.SpeedScale = 0.9f;
		Preset.DurationScale = 1.06f;
		Preset.FrequencyScale = 0.86f;
		Preset.OffsetScale = 0.85f;
		Preset.BlendTimeScale = 1.15f;
		Preset.MaxPhaseJitterRadians = 0.08f;
		Preset.MicroMotionScale = 0.7f;
		Preset.GazeEngagement = 0.25f;
	}
	else if (Preset.StyleTag == TEXT("friendly"))
	{
		Preset.AmplitudeScale = 1.0f;
		Preset.SpeedScale = 1.0f;
		Preset.DurationScale = 1.0f;
		Preset.FrequencyScale = 0.96f;
		Preset.OffsetScale = 1.0f;
		Preset.BlendTimeScale = 1.0f;
		Preset.MaxPhaseJitterRadians = 0.12f;
		Preset.MicroMotionScale = 1.0f;
		Preset.GazeEngagement = 0.8f;
	}
	else if (Preset.StyleTag == TEXT("excited"))
	{
		Preset.AmplitudeScale = 1.1f;
		Preset.SpeedScale = 1.12f;
		Preset.DurationScale = 0.96f;
		Preset.FrequencyScale = 1.2f;
		Preset.OffsetScale = 1.08f;
		Preset.BlendTimeScale = 0.85f;
		Preset.MaxPhaseJitterRadians = 0.18f;
		Preset.MicroMotionScale = 1.25f;
		Preset.GazeEngagement = 0.9f;
	}
	else
	{
		Preset.StyleTag = TEXT("neutral");
		Preset.MaxPhaseJitterRadians = 0.06f;
	}
	return Preset;
}

int32 ULLMNPCStyleResolver::BuildDeterministicSeed(
	const FGuid& SessionId,
	const FGuid& RequestId,
	FName NPCId,
	FName SelectionId
)
{
	uint32 Hash = GetTypeHash(SessionId);
	Hash = HashCombine(Hash, GetTypeHash(RequestId));
	Hash = HashCombine(Hash, GetTypeHash(NPCId));
	Hash = HashCombine(Hash, GetTypeHash(SelectionId));
	return static_cast<int32>(Hash & 0x7fffffffU);
}
