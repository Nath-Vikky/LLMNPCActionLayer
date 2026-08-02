#pragma once

#include "CoreMinimal.h"
#include "Selection/LLMNPCCandidateRetriever.h"

class ULLMNPCTemplateLibrarySubsystem;

enum class ELLMNPCForwardN7ExpectedSelection : uint8
{
	ExactAction,
	NoAction,
	ActionExcluded
};

struct FLLMNPCForwardN7MatrixCase
{
	FName CaseId = NAME_None;
	FString NaturalLanguage;
	ELLMNPCForwardN7ExpectedSelection ExpectedSelection =
		ELLMNPCForwardN7ExpectedSelection::ExactAction;
	FName ExpectedActionId = NAME_None;
	TArray<FName> ActiveStates;
	FName Emotion = NAME_None;
	float EmotionIntensity = 0.0f;
	float EmotionValence = 0.0f;
	float EmotionArousal = 0.0f;
	bool bProvideTarget = false;
	bool bRequireNoAvailableTargets = false;
	FString ExpectedTargetRef;
	float TargetDistanceCm = 240.0f;
	float TargetHeightCm = 0.0f;
	bool bResetConversationBefore = true;
	bool bCheckMirror = false;
	bool bExpectedMirror = false;
	bool bCheckStyle = false;
	FName ExpectedStyle = NAME_None;
	TArray<FName> AllowedExclusionReasons;
	TArray<FName> CoverageTags;
};

struct FLLMNPCForwardN7ObservedSelection
{
	bool bStrictProviderIdentity = false;
	bool bUsedLocalFallback = false;
	bool bResponseSchemaValid = false;
	TArray<FName> OfferedCandidateIds;
	TArray<FLLMNPCCandidateExclusion> CandidateExclusions;
	FName SelectedActionId = NAME_None;
	FName ResolvedTemplateId = NAME_None;
	bool bActionExecuted = false;
	bool bBehaviorStarted = false;
	FName ErrorCode = NAME_None;
	FString TargetRef;
	FName ResolvedStyle = NAME_None;
	bool bResolvedMirror = false;
	FString ValidatorResult;
};

struct FLLMNPCForwardN7CaseVerdict
{
	bool bProviderPassed = false;
	bool bSchemaPassed = false;
	bool bSelectionPassed = false;
	bool bExecutionPassed = false;
	bool bContextPassed = false;
	bool bStylePassed = false;
	bool bValidatorPassed = false;
	bool bPassed = false;
	FString FailureReason;
};

struct FLLMNPCForwardN7LibraryAudit
{
	FName SkeletonProfileId = NAME_None;
	int32 PublicActionCount = 0;
	int32 PublishedTemplateCount = 0;
	TArray<FName> PublicActionIds;
	TArray<FName> PublishedTemplateIds;
	TArray<FName> MissingPublicActionIds;
	TArray<FName> UnexpectedPublicActionIds;
	TArray<FName> ActionsWithoutMannyTemplate;
	TArray<FName> NonMannyTemplateIds;
	TArray<FName> IncompleteCandidateIds;
	bool bWaveHasStyleVariants = false;
	bool bClapHasAnimationAsset = false;
	bool bClapHasProceduralVariant = false;
	bool bAllTemplatesPublished = false;
	bool bPassed = false;
	TArray<FString> Errors;
	TArray<FString> Warnings;
};

namespace LLMNPCForwardN7Evaluation
{
const TCHAR* GetMatrixSchemaVersion();
const TCHAR* GetLibraryAuditSchemaVersion();
FString ExpectedSelectionToString(ELLMNPCForwardN7ExpectedSelection Value);
TArray<FName> GetExpectedPublicActionIds();
TArray<FLLMNPCForwardN7MatrixCase> BuildDefaultMatrix();
FLLMNPCForwardN7CaseVerdict EvaluateCase(
	const FLLMNPCForwardN7MatrixCase& TestCase,
	const FLLMNPCForwardN7ObservedSelection& Observed
);
FLLMNPCForwardN7LibraryAudit AuditMannyLibrary(
	ULLMNPCTemplateLibrarySubsystem& Library,
	FName SkeletonProfileId = TEXT("ue5_manny.v1")
);
}
