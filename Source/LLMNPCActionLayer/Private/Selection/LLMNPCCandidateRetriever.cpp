#include "Selection/LLMNPCCandidateRetriever.h"

#include "Style/LLMNPCStyleResolver.h"

namespace
{
const FName ExclusionTargetMissing(TEXT("target_missing"));
const FName ExclusionBlockedState(TEXT("blocked_state"));
const FName ExclusionCooldown(TEXT("cooldown"));
const FName ExclusionRepeated(TEXT("repeated"));

bool ContainsAny(const TArray<FName>& A, const TArray<FName>& B)
{
	for (const FName Value : A)
	{
		if (B.Contains(Value))
		{
			return true;
		}
	}
	return false;
}

TArray<FName> InferIntentTags(const FString& UserMessage)
{
	const FString Text = UserMessage.ToLower();
	TArray<FName> Tags;
	if (
		Text.Contains(TEXT("hello")) || Text.Contains(TEXT(" hi")) || Text.StartsWith(TEXT("hi")) ||
		Text.Contains(TEXT("\u4f60\u597d")) || Text.Contains(TEXT("\u55e8"))
	)
	{
		Tags.Add(TEXT("greet"));
	}
	if (
		Text.Contains(TEXT("agree")) || Text.Contains(TEXT("nod")) ||
		Text.Contains(TEXT("\u540c\u610f")) || Text.Contains(TEXT("\u70b9\u5934"))
	)
	{
		Tags.Add(TEXT("agree"));
		Tags.Add(TEXT("confirm"));
	}
	if (
		Text.Contains(TEXT("where")) || Text.Contains(TEXT("point")) ||
		Text.Contains(TEXT("\u5728\u54ea")) || Text.Contains(TEXT("\u54ea\u91cc")) ||
		Text.Contains(TEXT("\u6307"))
	)
	{
		Tags.Add(TEXT("indicate"));
		Tags.Add(TEXT("direct_attention"));
	}
	return Tags;
}

bool TargetMatchesMessage(const FLLMNPCSceneTargetContext& Target, const FString& UserMessage)
{
	const FString Text = UserMessage.ToLower();
	if (!Target.Category.IsNone() && Text.Contains(Target.Category.ToString().ToLower()))
	{
		return true;
	}
	for (const FName Tag : Target.SemanticTags)
	{
		const FString Value = Tag.ToString().ToLower();
		if (!Value.IsEmpty() && Text.Contains(Value))
		{
			return true;
		}
		if (Tag == TEXT("door") && Text.Contains(TEXT("\u95e8")))
		{
			return true;
		}
	}
	return false;
}

double ResolveNowSeconds(double RequestedNow)
{
	return RequestedNow > 0.0 ? RequestedNow : FPlatformTime::Seconds();
}

const FLLMNPCActionHistoryEntry* FindLatest(
	const TArray<FLLMNPCActionHistoryEntry>& History,
	FName SelectionId
)
{
	for (int32 Index = History.Num() - 1; Index >= 0; --Index)
	{
		if (History[Index].SelectionId == SelectionId)
		{
			return &History[Index];
		}
	}
	return nullptr;
}

void AddExclusion(FLLMNPCCandidateRetrievalResult& Result, FName SelectionId, FName Reason)
{
	FLLMNPCCandidateExclusion& Exclusion = Result.Exclusions.AddDefaulted_GetRef();
	Exclusion.SelectionId = SelectionId;
	Exclusion.Reason = Reason;
}
}

FLLMNPCCandidateRetrievalResult ULLMNPCCandidateRetriever::Retrieve(
	const FLLMNPCCandidateRetrievalRequest& Request
)
{
	FLLMNPCCandidateRetrievalResult Result;
	const double NowSeconds = ResolveNowSeconds(Request.NowSeconds);
	const TArray<FName> InferredIntentTags = InferIntentTags(Request.UserMessage);

	for (const FLLMNPCTemplateCandidate& Source : Request.SourceCandidates)
	{
		bool bMirrorRecommended = false;
		bool bBlocked = false;
		for (const FName BlockedState : Source.BlockedStates)
		{
			if (!Request.Context.ActiveStates.Contains(BlockedState))
			{
				continue;
			}
			if (
				BlockedState == TEXT("right_hand_busy") &&
				Source.bAllowMirror &&
				!Request.Context.ActiveStates.Contains(TEXT("left_hand_busy"))
			)
			{
				bMirrorRecommended = true;
				continue;
			}
			bBlocked = true;
			break;
		}
		if (bBlocked)
		{
			AddExclusion(Result, Source.SelectionId, ExclusionBlockedState);
			continue;
		}
		if (Source.bRequiresTarget && Request.Context.AvailableTargets.IsEmpty())
		{
			AddExclusion(Result, Source.SelectionId, ExclusionTargetMissing);
			continue;
		}

		if (const FLLMNPCActionHistoryEntry* Latest = FindLatest(Request.ActionHistory, Source.SelectionId))
		{
			const double Age = FMath::Max(0.0, NowSeconds - Latest->TimestampSeconds);
			if (Source.CooldownSeconds > 0.0f && Age < Source.CooldownSeconds)
			{
				AddExclusion(Result, Source.SelectionId, ExclusionCooldown);
				continue;
			}
			if (Request.RepeatSuppressionSeconds > 0.0f && Age < Request.RepeatSuppressionSeconds)
			{
				AddExclusion(Result, Source.SelectionId, ExclusionRepeated);
				continue;
			}
		}

		FLLMNPCTemplateCandidate Candidate = Source;
		Candidate.bMirrorRecommended = bMirrorRecommended;
		Candidate.RelevanceScore = ContainsAny(Candidate.IntentTags, InferredIntentTags) ? 4.0f : 0.0f;
		if (Candidate.EmotionTags.Contains(Request.Context.Emotion.PrimaryEmotion))
		{
			Candidate.RelevanceScore += 1.0f + Request.Context.Emotion.Intensity;
		}
		if (
			!Candidate.PersonalityTags.IsEmpty() &&
			ContainsAny(Candidate.PersonalityTags, Request.Context.Personality.PersonalityTags)
		)
		{
			Candidate.RelevanceScore += 1.0f;
		}

		Candidate.RecommendedStyle = ULLMNPCStyleResolver::ResolveRecommendedStyle(
			Request.Context,
			Candidate.AllowedStyles
		);
		const FLLMNPCStylePreset StylePreset = ULLMNPCStyleResolver::GetBuiltInPreset(
			Candidate.RecommendedStyle
		);
		const float ExpressionScale = FMath::Clamp(
			Request.Context.Personality.Expressiveness *
			FMath::Lerp(1.0f, 0.65f, Request.Context.Personality.Shyness),
			0.25f,
			1.5f
		);
		Candidate.AmplitudeRange.Y = FMath::Max(
			Candidate.AmplitudeRange.X,
			FMath::Min(Candidate.AmplitudeRange.Y, Candidate.AmplitudeRange.Y * ExpressionScale)
		);
		Candidate.RecommendedAmplitude = FMath::Clamp(
			ExpressionScale * StylePreset.AmplitudeScale,
			Candidate.AmplitudeRange.X,
			Candidate.AmplitudeRange.Y
		);
		Candidate.RecommendedSpeedScale = FMath::Clamp(
			StylePreset.SpeedScale,
			Candidate.SpeedRange.X,
			Candidate.SpeedRange.Y
		);
		Candidate.RecommendedDurationScale = FMath::Clamp(
			StylePreset.DurationScale,
			Candidate.DurationRange.X,
			Candidate.DurationRange.Y
		);

		if (Candidate.bRequiresTarget)
		{
			TArray<FLLMNPCSceneTargetContext> Targets = Request.Context.AvailableTargets;
			Targets.Sort(
				[&Request](const FLLMNPCSceneTargetContext& A, const FLLMNPCSceneTargetContext& B)
				{
					const bool bAMatches = TargetMatchesMessage(A, Request.UserMessage);
					const bool bBMatches = TargetMatchesMessage(B, Request.UserMessage);
					if (bAMatches != bBMatches)
					{
						return bAMatches;
					}
					if (!FMath::IsNearlyEqual(A.Salience, B.Salience))
					{
						return A.Salience > B.Salience;
					}
					return A.TargetRef < B.TargetRef;
				}
			);
			for (const FLLMNPCSceneTargetContext& Target : Targets)
			{
				Candidate.AllowedTargetRefs.Add(Target.TargetRef);
			}
			Candidate.DefaultTargetRef = Candidate.AllowedTargetRefs[0];
			Candidate.RelevanceScore += Targets[0].Salience;
			if (TargetMatchesMessage(Targets[0], Request.UserMessage))
			{
				Candidate.RelevanceScore += 2.0f;
			}
		}
		Result.Candidates.Add(MoveTemp(Candidate));
	}

	Result.Candidates.Sort(
		[](const FLLMNPCTemplateCandidate& A, const FLLMNPCTemplateCandidate& B)
		{
			if (!FMath::IsNearlyEqual(A.RelevanceScore, B.RelevanceScore))
			{
				return A.RelevanceScore > B.RelevanceScore;
			}
			return A.SelectionId.LexicalLess(B.SelectionId);
		}
	);
	const int32 MaxCandidates = FMath::Clamp(Request.MaxCandidates, 1, 32);
	if (Result.Candidates.Num() > MaxCandidates)
	{
		Result.Candidates.SetNum(MaxCandidates);
	}
	return Result;
}

bool ULLMNPCCandidateRetriever::ApplySelectionPolicy(
	FLLMNPCModelTurnDecision& Decision,
	const TArray<FLLMNPCTemplateCandidate>& OfferedCandidates,
	FString& OutError
)
{
	OutError.Reset();
	if (Decision.Action.Decision == TEXT("none"))
	{
		return true;
	}

	const FLLMNPCTemplateCandidate* Candidate = OfferedCandidates.FindByPredicate(
		[&Decision](const FLLMNPCTemplateCandidate& Offered)
		{
			return Offered.SelectionId == Decision.Action.TemplateId;
		}
	);
	if (!Candidate)
	{
		OutError = TEXT("LLMNPC_SELECTION_ACTION_NOT_OFFERED");
		return false;
	}

	if (Candidate->bRequiresTarget)
	{
		if (Decision.Action.TargetRef.IsEmpty())
		{
			Decision.Action.TargetRef = Candidate->DefaultTargetRef;
		}
		if (!Candidate->AllowedTargetRefs.Contains(Decision.Action.TargetRef))
		{
			OutError = TEXT("LLMNPC_SELECTION_TARGET_NOT_OFFERED");
			return false;
		}
	}
	else
	{
		Decision.Action.TargetRef.Reset();
	}

	const bool bApplyRecommendedStyle =
		Decision.Action.Style == TEXT("neutral") &&
		Candidate->RecommendedStyle != TEXT("neutral");
	if (bApplyRecommendedStyle)
	{
		Decision.Action.Style = Candidate->RecommendedStyle;
		Decision.Action.Amplitude = Candidate->RecommendedAmplitude;
		Decision.Action.SpeedScale = Candidate->RecommendedSpeedScale;
		Decision.Action.DurationScale = Candidate->RecommendedDurationScale;
	}

	if (
		!Candidate->AllowedStyles.IsEmpty() &&
		!Candidate->AllowedStyles.Contains(Decision.Action.Style)
	)
	{
		OutError = TEXT("LLMNPC_SELECTION_STYLE_NOT_OFFERED");
		return false;
	}

	Decision.Action.Amplitude = FMath::Clamp(
		Decision.Action.Amplitude,
		Candidate->AmplitudeRange.X,
		Candidate->AmplitudeRange.Y
	);
	Decision.Action.SpeedScale = FMath::Clamp(
		Decision.Action.SpeedScale,
		Candidate->SpeedRange.X,
		Candidate->SpeedRange.Y
	);
	Decision.Action.DurationScale = FMath::Clamp(
		Decision.Action.DurationScale,
		Candidate->DurationRange.X,
		Candidate->DurationRange.Y
	);
	Decision.Action.bMirror = Candidate->bMirrorRecommended;
	Decision.Action.ContextAmplitudeRange = Candidate->AmplitudeRange;
	Decision.Action.ContextSpeedRange = Candidate->SpeedRange;
	Decision.Action.ContextDurationRange = Candidate->DurationRange;
	return true;
}
