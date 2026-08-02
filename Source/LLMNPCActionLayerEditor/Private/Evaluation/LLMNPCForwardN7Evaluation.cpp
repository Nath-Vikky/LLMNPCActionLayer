#include "Evaluation/LLMNPCForwardN7Evaluation.h"

#include "Templates/LLMNPCMotionTemplate.h"
#include "Templates/LLMNPCTemplateLibrarySubsystem.h"

namespace
{
const FName TargetMissing(TEXT("target_missing"));
const FName Cooldown(TEXT("cooldown"));
const FName Repeated(TEXT("repeated"));

void AddFailure(
	FLLMNPCForwardN7CaseVerdict& Verdict,
	const FString& Failure
)
{
	if (!Failure.IsEmpty())
	{
		Verdict.FailureReason = Verdict.FailureReason.IsEmpty()
			? Failure
			: Verdict.FailureReason + TEXT("; ") + Failure;
	}
}

bool HasExclusion(
	const FLLMNPCForwardN7ObservedSelection& Observed,
	FName ActionId,
	const TArray<FName>& AllowedReasons
)
{
	return Observed.CandidateExclusions.ContainsByPredicate(
		[ActionId, &AllowedReasons](const FLLMNPCCandidateExclusion& Exclusion)
		{
			return Exclusion.SelectionId == ActionId &&
				(AllowedReasons.IsEmpty() || AllowedReasons.Contains(Exclusion.Reason));
		}
	);
}

bool HasValidRange(const FVector2D& Range)
{
	return FMath::IsFinite(Range.X) &&
		FMath::IsFinite(Range.Y) &&
		Range.X <= Range.Y;
}

FLLMNPCForwardN7MatrixCase ExactCase(
	const TCHAR* CaseId,
	const TCHAR* NaturalLanguage,
	const TCHAR* ExpectedActionId,
	std::initializer_list<const TCHAR*> CoverageTags
)
{
	FLLMNPCForwardN7MatrixCase Result;
	Result.CaseId = CaseId;
	Result.NaturalLanguage = NaturalLanguage;
	Result.ExpectedActionId = ExpectedActionId;
	for (const TCHAR* Tag : CoverageTags)
	{
		Result.CoverageTags.Add(Tag);
	}
	return Result;
}
}

namespace LLMNPCForwardN7Evaluation
{
const TCHAR* GetMatrixSchemaVersion()
{
	return TEXT("llmnpc.forward_n7_selection_matrix.v1");
}

const TCHAR* GetLibraryAuditSchemaVersion()
{
	return TEXT("llmnpc.forward_n7_library_audit.v1");
}

FString ExpectedSelectionToString(ELLMNPCForwardN7ExpectedSelection Value)
{
	switch (Value)
	{
	case ELLMNPCForwardN7ExpectedSelection::NoAction:
		return TEXT("none");
	case ELLMNPCForwardN7ExpectedSelection::ActionExcluded:
		return TEXT("excluded");
	case ELLMNPCForwardN7ExpectedSelection::ExactAction:
	default:
		return TEXT("exact_action");
	}
}

TArray<FName> GetExpectedPublicActionIds()
{
	return {
		TEXT("gesture.beckon"),
		TEXT("gesture.clap"),
		TEXT("gesture.nod"),
		TEXT("gesture.point.target"),
		TEXT("gesture.present"),
		TEXT("gesture.shrug"),
		TEXT("gesture.thumbs_up"),
		TEXT("gesture.wave.right")
	};
}

TArray<FLLMNPCForwardN7MatrixCase> BuildDefaultMatrix()
{
	TArray<FLLMNPCForwardN7MatrixCase> Cases;

	FLLMNPCForwardN7MatrixCase& Nod = Cases.Add_GetRef(ExactCase(
		TEXT("n7f.nod.acknowledge"),
		TEXT("I understand. Give me one small confirming nod without using your hands."),
		TEXT("gesture.nod"),
		{TEXT("public_action"), TEXT("neutral_style"), TEXT("target_none")}
	));
	Nod.bCheckStyle = true;
	Nod.ExpectedStyle = TEXT("neutral");

	FLLMNPCForwardN7MatrixCase& FriendlyWave = Cases.Add_GetRef(ExactCase(
		TEXT("n7f.wave.friendly_right"),
		TEXT("The player just arrived. Greet them with a clear friendly right-hand wave."),
		TEXT("gesture.wave.right"),
		{TEXT("public_action"), TEXT("right_hand"), TEXT("friendly_style")}
	));
	FriendlyWave.bCheckMirror = true;
	FriendlyWave.bExpectedMirror = false;
	FriendlyWave.bCheckStyle = true;
	FriendlyWave.ExpectedStyle = TEXT("friendly");

	FLLMNPCForwardN7MatrixCase& Point = Cases.Add_GetRef(ExactCase(
		TEXT("n7f.point.target"),
		TEXT("Show me exactly where the highlighted test target is by pointing at it."),
		TEXT("gesture.point.target"),
		{TEXT("public_action"), TEXT("target_present"), TEXT("target_required")}
	));
	Point.bProvideTarget = true;
	Point.ExpectedTargetRef = TEXT("n3_test_target");

	FLLMNPCForwardN7MatrixCase& Clap = Cases.Add_GetRef(ExactCase(
		TEXT("n7f.clap.excited"),
		TEXT("That was fantastic news. Applaud it with an excited clap."),
		TEXT("gesture.clap"),
		{TEXT("public_action"), TEXT("excited_style"), TEXT("two_hands")}
	));
	Clap.Emotion = TEXT("excited");
	Clap.EmotionIntensity = 0.9f;
	Clap.EmotionValence = 0.8f;
	Clap.EmotionArousal = 0.9f;
	Clap.bCheckStyle = true;
	Clap.ExpectedStyle = TEXT("excited");

	Cases.Add(ExactCase(
		TEXT("n7f.shrug.uncertain"),
		TEXT("You honestly do not know the answer. Respond with an uncertain shrug."),
		TEXT("gesture.shrug"),
		{TEXT("public_action"), TEXT("shoulders"), TEXT("target_none")}
	));

	FLLMNPCForwardN7MatrixCase& Beckon = Cases.Add_GetRef(ExactCase(
		TEXT("n7f.beckon.target"),
		TEXT("Invite the highlighted test target to come closer with a beckoning gesture."),
		TEXT("gesture.beckon"),
		{TEXT("public_action"), TEXT("target_present"), TEXT("target_required")}
	));
	Beckon.bProvideTarget = true;
	Beckon.ExpectedTargetRef = TEXT("n3_test_target");

	FLLMNPCForwardN7MatrixCase& Present = Cases.Add_GetRef(ExactCase(
		TEXT("n7f.present.target"),
		TEXT("Politely present the highlighted test target to me with an open palm."),
		TEXT("gesture.present"),
		{TEXT("public_action"), TEXT("target_present"), TEXT("open_palm")}
	));
	Present.bProvideTarget = true;
	Present.ExpectedTargetRef = TEXT("n3_test_target");

	FLLMNPCForwardN7MatrixCase& ThumbsUp = Cases.Add_GetRef(ExactCase(
		TEXT("n7f.thumbs_up.friendly"),
		TEXT("Show clear friendly approval with one thumbs-up."),
		TEXT("gesture.thumbs_up"),
		{TEXT("public_action"), TEXT("right_hand"), TEXT("friendly_style")}
	));
	ThumbsUp.bCheckMirror = true;
	ThumbsUp.bExpectedMirror = false;
	ThumbsUp.bCheckStyle = true;
	ThumbsUp.ExpectedStyle = TEXT("friendly");

	FLLMNPCForwardN7MatrixCase NoAction;
	NoAction.CaseId = TEXT("n7f.none.still");
	NoAction.NaturalLanguage =
		TEXT("Remain completely still and do not perform any body action.");
	NoAction.ExpectedSelection = ELLMNPCForwardN7ExpectedSelection::NoAction;
	NoAction.CoverageTags = {TEXT("none"), TEXT("target_none")};
	Cases.Add(MoveTemp(NoAction));

	FLLMNPCForwardN7MatrixCase& MirroredWave = Cases.Add_GetRef(ExactCase(
		TEXT("n7f.wave.right_busy_mirror"),
		TEXT("Your right hand is occupied. Greet the player with a friendly wave using the free hand."),
		TEXT("gesture.wave.right"),
		{TEXT("right_hand_busy"), TEXT("left_hand"), TEXT("mirror")}
	));
	MirroredWave.ActiveStates = {TEXT("right_hand_busy")};
	MirroredWave.bCheckMirror = true;
	MirroredWave.bExpectedMirror = true;
	MirroredWave.bCheckStyle = true;
	MirroredWave.ExpectedStyle = TEXT("friendly");

	FLLMNPCForwardN7MatrixCase& MirroredThumbsUp = Cases.Add_GetRef(ExactCase(
		TEXT("n7f.thumbs_up.right_busy_mirror"),
		TEXT("Your right hand is occupied. Show approval with a thumbs-up using the free hand."),
		TEXT("gesture.thumbs_up"),
		{TEXT("right_hand_busy"), TEXT("left_hand"), TEXT("mirror")}
	));
	MirroredThumbsUp.ActiveStates = {TEXT("right_hand_busy")};
	MirroredThumbsUp.bCheckMirror = true;
	MirroredThumbsUp.bExpectedMirror = true;

	FLLMNPCForwardN7MatrixCase& SubtleWave = Cases.Add_GetRef(ExactCase(
		TEXT("n7f.wave.subtle"),
		TEXT("Give a very small, restrained, subtle wave rather than an energetic one."),
		TEXT("gesture.wave.right"),
		{TEXT("subtle_style"), TEXT("style_variant")}
	));
	SubtleWave.bCheckStyle = true;
	SubtleWave.ExpectedStyle = TEXT("subtle");

	FLLMNPCForwardN7MatrixCase& WalkingWave = Cases.Add_GetRef(ExactCase(
		TEXT("n7f.wave.excited_walking"),
		TEXT("While walking, give an excited wave to celebrate seeing the player."),
		TEXT("gesture.wave.right"),
		{TEXT("walking"), TEXT("excited_style"), TEXT("style_variant")}
	));
	WalkingWave.ActiveStates = {TEXT("walking")};
	WalkingWave.Emotion = TEXT("excited");
	WalkingWave.EmotionIntensity = 0.9f;
	WalkingWave.EmotionValence = 0.8f;
	WalkingWave.EmotionArousal = 0.9f;
	WalkingWave.bCheckStyle = true;
	WalkingWave.ExpectedStyle = TEXT("excited");

	FLLMNPCForwardN7MatrixCase& LeftBusyThumbsUp = Cases.Add_GetRef(ExactCase(
		TEXT("n7f.thumbs_up.left_busy_right"),
		TEXT("Your left hand is occupied. Show approval with a thumbs-up using the free right hand."),
		TEXT("gesture.thumbs_up"),
		{TEXT("left_hand_busy"), TEXT("right_hand")}
	));
	LeftBusyThumbsUp.ActiveStates = {TEXT("left_hand_busy")};
	LeftBusyThumbsUp.bCheckMirror = true;
	LeftBusyThumbsUp.bExpectedMirror = false;

	FLLMNPCForwardN7MatrixCase MissingTarget;
	MissingTarget.CaseId = TEXT("n7f.point.target_missing");
	MissingTarget.NaturalLanguage =
		TEXT("Point to the highlighted test target, even though no target is currently registered.");
	MissingTarget.ExpectedSelection =
		ELLMNPCForwardN7ExpectedSelection::ActionExcluded;
	MissingTarget.ExpectedActionId = TEXT("gesture.point.target");
	MissingTarget.bRequireNoAvailableTargets = true;
	MissingTarget.AllowedExclusionReasons = {TargetMissing};
	MissingTarget.CoverageTags = {TEXT("target_missing"), TEXT("policy_exclusion")};
	Cases.Add(MoveTemp(MissingTarget));

	FLLMNPCForwardN7MatrixCase& RepeatSeed = Cases.Add_GetRef(ExactCase(
		TEXT("n7f.repeat.wave_seed"),
		TEXT("Greet the player now with a friendly wave."),
		TEXT("gesture.wave.right"),
		{TEXT("repeat_seed"), TEXT("friendly_style")}
	));
	RepeatSeed.bCheckStyle = true;
	RepeatSeed.ExpectedStyle = TEXT("friendly");

	FLLMNPCForwardN7MatrixCase RepeatSuppressed;
	RepeatSuppressed.CaseId = TEXT("n7f.repeat.wave_suppressed");
	RepeatSuppressed.NaturalLanguage =
		TEXT("Immediately perform that same friendly wave again.");
	RepeatSuppressed.ExpectedSelection =
		ELLMNPCForwardN7ExpectedSelection::ActionExcluded;
	RepeatSuppressed.ExpectedActionId = TEXT("gesture.wave.right");
	RepeatSuppressed.bResetConversationBefore = false;
	RepeatSuppressed.AllowedExclusionReasons = {Cooldown, Repeated};
	RepeatSuppressed.CoverageTags = {TEXT("repeat_suppression"), TEXT("policy_exclusion")};
	Cases.Add(MoveTemp(RepeatSuppressed));

	return Cases;
}

FLLMNPCForwardN7CaseVerdict EvaluateCase(
	const FLLMNPCForwardN7MatrixCase& TestCase,
	const FLLMNPCForwardN7ObservedSelection& Observed
)
{
	FLLMNPCForwardN7CaseVerdict Verdict;
	Verdict.bProviderPassed =
		Observed.bStrictProviderIdentity && !Observed.bUsedLocalFallback;
	if (!Verdict.bProviderPassed)
	{
		AddFailure(Verdict, TEXT("strict provider identity failed"));
	}
	Verdict.bSchemaPassed = Observed.bResponseSchemaValid;
	if (!Verdict.bSchemaPassed)
	{
		AddFailure(Verdict, TEXT("response schema validation failed"));
	}

	if (!Observed.ErrorCode.IsNone())
	{
		AddFailure(
			Verdict,
			FString::Printf(TEXT("runtime error %s"), *Observed.ErrorCode.ToString())
		);
	}

	Verdict.bContextPassed = true;
	Verdict.bStylePassed = true;
	Verdict.bValidatorPassed = true;
	switch (TestCase.ExpectedSelection)
	{
	case ELLMNPCForwardN7ExpectedSelection::NoAction:
		Verdict.bSelectionPassed =
			Observed.SelectedActionId.IsNone() &&
			Observed.ResolvedTemplateId.IsNone();
		Verdict.bExecutionPassed =
			!Observed.bActionExecuted && !Observed.bBehaviorStarted;
		if (!Verdict.bSelectionPassed)
		{
			AddFailure(Verdict, TEXT("expected no action"));
		}
		if (!Verdict.bExecutionPassed)
		{
			AddFailure(Verdict, TEXT("no-action case started behavior"));
		}
		break;

	case ELLMNPCForwardN7ExpectedSelection::ActionExcluded:
		Verdict.bSelectionPassed =
			!Observed.OfferedCandidateIds.Contains(TestCase.ExpectedActionId) &&
			Observed.SelectedActionId != TestCase.ExpectedActionId &&
			HasExclusion(
				Observed,
				TestCase.ExpectedActionId,
				TestCase.AllowedExclusionReasons
			);
		Verdict.bExecutionPassed = true;
		if (!Verdict.bSelectionPassed)
		{
			AddFailure(
				Verdict,
				FString::Printf(
					TEXT("expected %s to be excluded"),
					*TestCase.ExpectedActionId.ToString()
				)
			);
		}
		break;

	case ELLMNPCForwardN7ExpectedSelection::ExactAction:
	default:
		Verdict.bSelectionPassed =
			Observed.OfferedCandidateIds.Contains(TestCase.ExpectedActionId) &&
			Observed.SelectedActionId == TestCase.ExpectedActionId &&
			!Observed.ResolvedTemplateId.IsNone();
		Verdict.bExecutionPassed =
			Observed.bActionExecuted || Observed.bBehaviorStarted;
		Verdict.bValidatorPassed = Observed.ValidatorResult == TEXT("accepted");
		if (!Verdict.bSelectionPassed)
		{
			AddFailure(
				Verdict,
				FString::Printf(
					TEXT("expected exact action %s"),
					*TestCase.ExpectedActionId.ToString()
				)
			);
		}
		if (!Verdict.bExecutionPassed)
		{
			AddFailure(Verdict, TEXT("expected action did not execute"));
		}
		if (!Verdict.bValidatorPassed)
		{
			AddFailure(Verdict, TEXT("motion validator did not accept the action"));
		}
		break;
	}

	if (!TestCase.ExpectedTargetRef.IsEmpty())
	{
		Verdict.bContextPassed = Observed.TargetRef == TestCase.ExpectedTargetRef;
		if (!Verdict.bContextPassed)
		{
			AddFailure(
				Verdict,
				FString::Printf(
					TEXT("expected target %s"),
					*TestCase.ExpectedTargetRef
				)
			);
		}
	}
	if (TestCase.bCheckMirror)
	{
		Verdict.bContextPassed =
			Verdict.bContextPassed &&
			Observed.bResolvedMirror == TestCase.bExpectedMirror;
		if (Observed.bResolvedMirror != TestCase.bExpectedMirror)
		{
			AddFailure(
				Verdict,
				TestCase.bExpectedMirror
					? TEXT("expected mirrored execution")
					: TEXT("expected non-mirrored execution")
			);
		}
	}
	if (TestCase.bCheckStyle)
	{
		Verdict.bStylePassed = Observed.ResolvedStyle == TestCase.ExpectedStyle;
		if (!Verdict.bStylePassed)
		{
			AddFailure(
				Verdict,
				FString::Printf(
					TEXT("expected style %s"),
					*TestCase.ExpectedStyle.ToString()
				)
			);
		}
	}

	Verdict.bPassed =
		Verdict.bProviderPassed &&
		Verdict.bSchemaPassed &&
		Observed.ErrorCode.IsNone() &&
		Verdict.bSelectionPassed &&
		Verdict.bExecutionPassed &&
		Verdict.bContextPassed &&
		Verdict.bStylePassed &&
		Verdict.bValidatorPassed;
	return Verdict;
}

FLLMNPCForwardN7LibraryAudit AuditMannyLibrary(
	ULLMNPCTemplateLibrarySubsystem& Library,
	FName SkeletonProfileId
)
{
	FLLMNPCForwardN7LibraryAudit Audit;
	Audit.SkeletonProfileId = SkeletonProfileId;

	TArray<FLLMNPCTemplateCandidate> Candidates;
	Library.QueryRuntimeCandidates(SkeletonProfileId, Candidates);
	Audit.PublicActionCount = Candidates.Num();
	for (const FLLMNPCTemplateCandidate& Candidate : Candidates)
	{
		Audit.PublicActionIds.Add(Candidate.SelectionId);
		const bool bComplete =
			!Candidate.SelectionId.IsNone() &&
			!Candidate.Description.IsEmpty() &&
			!Candidate.SelectionSummary.TrimStartAndEnd().IsEmpty() &&
			!Candidate.SuitableWhen.IsEmpty() &&
			!Candidate.AvoidWhen.IsEmpty() &&
			!Candidate.IntentTags.IsEmpty() &&
			!Candidate.SemanticEffectTags.IsEmpty() &&
			!Candidate.AllowedStyles.IsEmpty() &&
			!Candidate.StyleOptions.IsEmpty() &&
			HasValidRange(Candidate.AmplitudeRange) &&
			HasValidRange(Candidate.SpeedRange) &&
			HasValidRange(Candidate.DurationRange);
		if (!bComplete)
		{
			Audit.IncompleteCandidateIds.Add(Candidate.SelectionId);
		}
		if (Candidate.SelectionId == TEXT("gesture.wave.right"))
		{
			Audit.bWaveHasStyleVariants =
				Candidate.StyleOptions.Num() >= 2 &&
				Candidate.AllowedStyles.Contains(TEXT("subtle"));
		}
	}
	Audit.PublicActionIds.Sort(FNameLexicalLess());

	const TArray<FName> ExpectedActions = GetExpectedPublicActionIds();
	for (const FName Expected : ExpectedActions)
	{
		if (!Audit.PublicActionIds.Contains(Expected))
		{
			Audit.MissingPublicActionIds.Add(Expected);
		}
	}
	for (const FName Actual : Audit.PublicActionIds)
	{
		if (!ExpectedActions.Contains(Actual))
		{
			Audit.UnexpectedPublicActionIds.Add(Actual);
		}
	}

	Library.GetPublishedTemplateIdsForProfile(
		SkeletonProfileId,
		Audit.PublishedTemplateIds
	);
	Audit.PublishedTemplateCount = Audit.PublishedTemplateIds.Num();
	TSet<FName> ActionsWithMannyTemplate;
	bool bAllTemplatesPublished = !Audit.PublishedTemplateIds.IsEmpty();
	for (const FName TemplateId : Audit.PublishedTemplateIds)
	{
		const ULLMNPCMotionTemplate* MotionTemplate =
			Library.FindPublishedTemplate(TemplateId);
		if (!MotionTemplate)
		{
			bAllTemplatesPublished = false;
			Audit.Errors.Add(FString::Printf(
				TEXT("Published template %s did not resolve."),
				*TemplateId.ToString()
			));
			continue;
		}
		const bool bSupportsManny =
			MotionTemplate->SupportsSkeletonProfile(SkeletonProfileId);
		const bool bNamesAnotherSkeleton =
			TemplateId.ToString().Contains(TEXT("quinn"), ESearchCase::IgnoreCase) ||
			MotionTemplate->Metadata.SkeletonProfileId.ToString().Contains(
				TEXT("quinn"),
				ESearchCase::IgnoreCase
			);
		if (!bSupportsManny || bNamesAnotherSkeleton)
		{
			Audit.NonMannyTemplateIds.Add(TemplateId);
		}
		bAllTemplatesPublished &=
			MotionTemplate->Metadata.ReviewState ==
				ELLMNPCTemplateReviewState::Published;
		if (!MotionTemplate->Metadata.PublicActionId.IsNone())
		{
			ActionsWithMannyTemplate.Add(MotionTemplate->Metadata.PublicActionId);
		}
		if (MotionTemplate->Metadata.PublicActionId == TEXT("gesture.clap"))
		{
			Audit.bClapHasAnimationAsset |=
				MotionTemplate->Kind == ELLMNPCTemplateKind::AnimationAsset;
			Audit.bClapHasProceduralVariant |=
				MotionTemplate->Kind == ELLMNPCTemplateKind::ProceduralMotion;
		}
	}
	Audit.PublishedTemplateIds.Sort(FNameLexicalLess());
	Audit.bAllTemplatesPublished = bAllTemplatesPublished;

	for (const FName Expected : ExpectedActions)
	{
		if (!ActionsWithMannyTemplate.Contains(Expected))
		{
			Audit.ActionsWithoutMannyTemplate.Add(Expected);
		}
	}

	if (!Library.GetScanErrors().IsEmpty())
	{
		Audit.Errors.Append(Library.GetScanErrors());
	}
	if (!Audit.MissingPublicActionIds.IsEmpty())
	{
		Audit.Errors.Add(TEXT("One or more required N7 Public Actions are missing."));
	}
	if (!Audit.UnexpectedPublicActionIds.IsEmpty())
	{
		Audit.Warnings.Add(TEXT("The runtime library contains actions outside the locked N7 matrix."));
	}
	if (!Audit.ActionsWithoutMannyTemplate.IsEmpty())
	{
		Audit.Errors.Add(TEXT("One or more Public Actions have no Published Manny template."));
	}
	if (!Audit.NonMannyTemplateIds.IsEmpty())
	{
		Audit.Errors.Add(TEXT("The Manny catalog borrows a template from another skeleton profile."));
	}
	if (!Audit.IncompleteCandidateIds.IsEmpty())
	{
		Audit.Errors.Add(TEXT("One or more Candidate Cards have incomplete descriptions, tags, styles, or ranges."));
	}
	if (!Audit.bWaveHasStyleVariants)
	{
		Audit.Errors.Add(TEXT("Wave does not expose the required style variants."));
	}
	if (!Audit.bClapHasAnimationAsset || !Audit.bClapHasProceduralVariant)
	{
		Audit.Errors.Add(TEXT("Clap must retain both Animation Asset and Procedural Published variants."));
	}
	if (!Audit.bAllTemplatesPublished)
	{
		Audit.Errors.Add(TEXT("At least one enumerated Manny template is not Published."));
	}

	Audit.bPassed =
		Audit.PublicActionCount == ExpectedActions.Num() &&
		Audit.Errors.IsEmpty();
	return Audit;
}
}
