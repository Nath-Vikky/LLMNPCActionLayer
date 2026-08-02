#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Engine/GameInstance.h"
#include "Evaluation/LLMNPCForwardN7Evaluation.h"
#include "Selection/LLMNPCCandidateRetriever.h"
#include "Templates/LLMNPCTemplateLibrarySubsystem.h"

namespace
{
constexpr uint32 ForwardN7FEditorTestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;

const FLLMNPCForwardN7MatrixCase* FindCase(
	const TArray<FLLMNPCForwardN7MatrixCase>& Cases,
	FName CaseId
)
{
	return Cases.FindByPredicate(
		[CaseId](const FLLMNPCForwardN7MatrixCase& TestCase)
		{
			return TestCase.CaseId == CaseId;
		}
	);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN7FMatrixContractTest,
	"LLMNPCActionLayer.ForwardN7F.Editor.MatrixContract",
	ForwardN7FEditorTestFlags
)

bool FLLMNPCForwardN7FMatrixContractTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	const TArray<FLLMNPCForwardN7MatrixCase> Cases =
		LLMNPCForwardN7Evaluation::BuildDefaultMatrix();
	TestEqual(TEXT("The locked N7-F matrix has seventeen cases"), Cases.Num(), 17);

	TSet<FName> CaseIds;
	TSet<FName> ExactActions;
	TSet<FName> CoverageTags;
	int32 PreservedConversationCases = 0;
	for (const FLLMNPCForwardN7MatrixCase& TestCase : Cases)
	{
		TestFalse(TEXT("Every matrix case has an ID"), TestCase.CaseId.IsNone());
		TestFalse(
			*FString::Printf(TEXT("Case %s has natural language"), *TestCase.CaseId.ToString()),
			TestCase.NaturalLanguage.TrimStartAndEnd().IsEmpty()
		);
		TestFalse(
			*FString::Printf(TEXT("Case ID %s is unique"), *TestCase.CaseId.ToString()),
			CaseIds.Contains(TestCase.CaseId)
		);
		CaseIds.Add(TestCase.CaseId);
		if (TestCase.ExpectedSelection == ELLMNPCForwardN7ExpectedSelection::ExactAction)
		{
			ExactActions.Add(TestCase.ExpectedActionId);
		}
		if (!TestCase.bResetConversationBefore)
		{
			++PreservedConversationCases;
		}
		for (const FName Tag : TestCase.CoverageTags)
		{
			CoverageTags.Add(Tag);
		}
	}

	for (const FName ExpectedAction :
		LLMNPCForwardN7Evaluation::GetExpectedPublicActionIds())
	{
		TestTrue(
			*FString::Printf(
				TEXT("The matrix directly selects %s"),
				*ExpectedAction.ToString()
			),
			ExactActions.Contains(ExpectedAction)
		);
	}
	for (const FName RequiredCoverage : {
		FName(TEXT("none")),
		FName(TEXT("right_hand")),
		FName(TEXT("left_hand")),
		FName(TEXT("right_hand_busy")),
		FName(TEXT("left_hand_busy")),
		FName(TEXT("target_present")),
		FName(TEXT("target_missing")),
		FName(TEXT("neutral_style")),
		FName(TEXT("subtle_style")),
		FName(TEXT("excited_style")),
		FName(TEXT("walking")),
		FName(TEXT("repeat_suppression"))
	})
	{
		TestTrue(
			*FString::Printf(
				TEXT("The matrix covers %s"),
				*RequiredCoverage.ToString()
			),
			CoverageTags.Contains(RequiredCoverage)
		);
	}
	TestEqual(
		TEXT("Only the immediate repeat case preserves conversation history"),
		PreservedConversationCases,
		1
	);
	TestTrue(
		TEXT("Standalone retrieval uses the same playback-aware repeat window"),
		FLLMNPCCandidateRetrievalRequest().RepeatSuppressionSeconds >= 5.5f
	);

	const FLLMNPCForwardN7MatrixCase* NoneCase =
		FindCase(Cases, TEXT("n7f.none.still"));
	const FLLMNPCForwardN7MatrixCase* MissingTarget =
		FindCase(Cases, TEXT("n7f.point.target_missing"));
	const FLLMNPCForwardN7MatrixCase* RepeatSuppressed =
		FindCase(Cases, TEXT("n7f.repeat.wave_suppressed"));
	TestTrue(
		TEXT("The explicit None case expects no action"),
		NoneCase &&
		NoneCase->ExpectedSelection == ELLMNPCForwardN7ExpectedSelection::NoAction
	);
	TestTrue(
		TEXT("The missing-target case requires a target_missing exclusion"),
		MissingTarget &&
		MissingTarget->ExpectedSelection ==
			ELLMNPCForwardN7ExpectedSelection::ActionExcluded &&
		MissingTarget->bRequireNoAvailableTargets &&
		MissingTarget->AllowedExclusionReasons.Contains(TEXT("target_missing"))
	);
	TestTrue(
		TEXT("The repeat case accepts cooldown or repeat suppression evidence"),
		RepeatSuppressed &&
		RepeatSuppressed->AllowedExclusionReasons.Contains(TEXT("cooldown")) &&
		RepeatSuppressed->AllowedExclusionReasons.Contains(TEXT("repeated"))
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN7FCaseVerdictTest,
	"LLMNPCActionLayer.ForwardN7F.Editor.CaseVerdict",
	ForwardN7FEditorTestFlags
)

bool FLLMNPCForwardN7FCaseVerdictTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	FLLMNPCForwardN7MatrixCase Exact;
	Exact.CaseId = TEXT("fixture.exact");
	Exact.ExpectedActionId = TEXT("gesture.wave.right");
	Exact.bCheckMirror = true;
	Exact.bExpectedMirror = true;
	Exact.bCheckStyle = true;
	Exact.ExpectedStyle = TEXT("friendly");

	FLLMNPCForwardN7ObservedSelection Observed;
	Observed.bStrictProviderIdentity = true;
	Observed.bResponseSchemaValid = true;
	Observed.OfferedCandidateIds = {TEXT("gesture.wave.right")};
	Observed.SelectedActionId = TEXT("gesture.wave.right");
	Observed.ResolvedTemplateId = TEXT("gesture.wave.right.manny.fk.v1");
	Observed.bActionExecuted = true;
	Observed.ResolvedStyle = TEXT("friendly");
	Observed.bResolvedMirror = true;
	Observed.ValidatorResult = TEXT("accepted");
	TestTrue(
		TEXT("An exact strict-provider observation passes"),
		LLMNPCForwardN7Evaluation::EvaluateCase(Exact, Observed).bPassed
	);

	Observed.bResolvedMirror = false;
	const FLLMNPCForwardN7CaseVerdict MirrorFailure =
		LLMNPCForwardN7Evaluation::EvaluateCase(Exact, Observed);
	TestFalse(TEXT("A wrong hand fails the exact case"), MirrorFailure.bPassed);
	TestTrue(
		TEXT("The wrong-hand failure is traceable"),
		MirrorFailure.FailureReason.Contains(TEXT("mirrored"))
	);

	FLLMNPCForwardN7MatrixCase None;
	None.CaseId = TEXT("fixture.none");
	None.ExpectedSelection = ELLMNPCForwardN7ExpectedSelection::NoAction;
	FLLMNPCForwardN7ObservedSelection NoActionObserved;
	NoActionObserved.bStrictProviderIdentity = true;
	NoActionObserved.bResponseSchemaValid = true;
	TestTrue(
		TEXT("A strict no-action response passes the None case"),
		LLMNPCForwardN7Evaluation::EvaluateCase(None, NoActionObserved).bPassed
	);

	FLLMNPCForwardN7MatrixCase Excluded;
	Excluded.CaseId = TEXT("fixture.excluded");
	Excluded.ExpectedSelection = ELLMNPCForwardN7ExpectedSelection::ActionExcluded;
	Excluded.ExpectedActionId = TEXT("gesture.point.target");
	Excluded.AllowedExclusionReasons = {TEXT("target_missing")};
	FLLMNPCForwardN7ObservedSelection ExcludedObserved;
	ExcludedObserved.bStrictProviderIdentity = true;
	ExcludedObserved.bResponseSchemaValid = true;
	FLLMNPCCandidateExclusion& Exclusion =
		ExcludedObserved.CandidateExclusions.AddDefaulted_GetRef();
	Exclusion.SelectionId = TEXT("gesture.point.target");
	Exclusion.Reason = TEXT("target_missing");
	TestTrue(
		TEXT("A policy-proven excluded action passes"),
		LLMNPCForwardN7Evaluation::EvaluateCase(Excluded, ExcludedObserved).bPassed
	);

	Exclusion.Reason = TEXT("blocked_state");
	TestFalse(
		TEXT("An exclusion with the wrong reason fails"),
		LLMNPCForwardN7Evaluation::EvaluateCase(Excluded, ExcludedObserved).bPassed
	);
	ExcludedObserved.bStrictProviderIdentity = false;
	ExcludedObserved.bUsedLocalFallback = true;
	TestFalse(
		TEXT("A local fallback can never pass the N7-F matrix"),
		LLMNPCForwardN7Evaluation::EvaluateCase(Excluded, ExcludedObserved).bPassed
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN7FMannyLibraryAuditTest,
	"LLMNPCActionLayer.ForwardN7F.Editor.MannyLibraryAudit",
	ForwardN7FEditorTestFlags
)

bool FLLMNPCForwardN7FMannyLibraryAuditTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	UGameInstance* TestGameInstance = NewObject<UGameInstance>();
	ULLMNPCTemplateLibrarySubsystem* Library =
		NewObject<ULLMNPCTemplateLibrarySubsystem>(TestGameInstance);
	Library->RefreshLibrary();
	const FLLMNPCForwardN7LibraryAudit Audit =
		LLMNPCForwardN7Evaluation::AuditMannyLibrary(*Library);
	for (const FString& Error : Audit.Errors)
	{
		AddError(Error);
	}
	TestTrue(TEXT("The N7 Manny library audit passes"), Audit.bPassed);
	TestEqual(TEXT("Exactly eight Public Actions are model-visible"), Audit.PublicActionCount, 8);
	TestTrue(
		TEXT("Every Public Action has a Published Manny implementation"),
		Audit.ActionsWithoutMannyTemplate.IsEmpty()
	);
	TestTrue(
		TEXT("No Quinn or foreign-skeleton template fills the Manny catalog"),
		Audit.NonMannyTemplateIds.IsEmpty()
	);
	TestTrue(TEXT("Wave exposes style variants"), Audit.bWaveHasStyleVariants);
	TestTrue(TEXT("Clap retains the Animation Asset baseline"), Audit.bClapHasAnimationAsset);
	TestTrue(TEXT("Clap has a Published procedural variant"), Audit.bClapHasProceduralVariant);
	TestTrue(TEXT("All enumerated Manny templates are Published"), Audit.bAllTemplatesPublished);
	return true;
}

#endif
