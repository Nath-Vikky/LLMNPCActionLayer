#include "Online/LLMNPCCatalogSelectionSmokeRunner.h"

#include "Async/Async.h"
#include "Context/LLMNPCContextTypes.h"
#include "Dialogue/LLMNPCConversationSession.h"
#include "Dialogue/LLMNPCDialogueTypes.h"
#include "Dialogue/LLMNPCModelTurnValidator.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "LLMNPCSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Online/LLMNPCOnlineReportSanitizer.h"
#include "Online/LLMNPCOnlineTestConfigLoader.h"
#include "Providers/LLMNPCDeepSeekProvider.h"
#include "Providers/LLMNPCProviderCredentials.h"
#include "Selection/LLMNPCCandidateRetriever.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "Templates/LLMNPCTemplateCompiler.h"
#include "Templates/LLMNPCTemplateLibrarySubsystem.h"
#include "UObject/StrongObjectPtr.h"

DEFINE_LOG_CATEGORY_STATIC(LogLLMNPCCatalogSelectionSmoke, Log, All);

namespace
{
constexpr int32 SelectionSmokeRunsPerCase = 3;

struct FLLMNPCSelectionSmokeCase
{
	FName CaseId = NAME_None;
	FString UserMessage;
	bool bExpectedAction = false;
	FName ExpectedSelectionId = NAME_None;
	FString ExpectedTargetRef;
	bool bProvideDoorTarget = false;
	TArray<FName> AllowedSourceSelectionIds;
};

struct FLLMNPCSelectionSmokeSample
{
	FName CaseId = NAME_None;
	int32 Iteration = 0;
	FGuid RequestId;
	FString RequestHash;
	TArray<FName> OfferedSelectionIds;
	FName ExpectedSelectionId = NAME_None;
	FString ExpectedTargetRef;
	FName ObservedSelectionId = NAME_None;
	FName ResolvedTemplateId = NAME_None;
	FString ObservedTargetRef;
	bool bExpectedAction = false;
	bool bProviderSuccess = false;
	bool bProviderMatches = false;
	bool bModelMatches = false;
	bool bConfigMatches = false;
	bool bPrivacyScanPassed = false;
	bool bSchemaValid = false;
	bool bSelectionPolicyValid = false;
	bool bValidatorAccepted = false;
	bool bCompiled = false;
	bool bExpectedMatch = false;
	bool bPassed = false;
	bool bObservedNone = false;
	FName ErrorCode = NAME_None;
	int32 HttpStatus = 0;
	int32 AttemptCount = 0;
	float LatencySeconds = -1.0f;
	int32 PromptTokens = INDEX_NONE;
	int32 CompletionTokens = INDEX_NONE;
	int32 TotalTokens = INDEX_NONE;
};

struct FLLMNPCActiveCatalogSelectionSmoke
{
	bool bExitEditorWhenComplete = false;
	int32 CaseIndex = 0;
	int32 Iteration = 0;
	FGuid SessionId = FGuid::NewGuid();
	FGuid CurrentRequestId;
	FString CurrentRequestHash;
	bool bCurrentPrivacyScanPassed = false;
	FLLMNPCOnlineTestConfigState Config;
	TStrongObjectPtr<UGameInstance> GameInstance;
	TStrongObjectPtr<ULLMNPCTemplateLibrarySubsystem> Library;
	TStrongObjectPtr<ULLMNPCSkeletonProfile> Profile;
	TSharedPtr<FLLMNPCDeepSeekProvider> Provider;
	TArray<FLLMNPCTemplateCandidate> BaseCandidates;
	TArray<FLLMNPCTemplateCandidate> CurrentOfferedCandidates;
	TArray<FLLMNPCSelectionSmokeCase> Cases;
	TArray<FLLMNPCSelectionSmokeSample> Samples;
	bool bLocalUnofferedGatePassed = false;
};

TSharedPtr<FLLMNPCActiveCatalogSelectionSmoke> ActiveSelectionSmoke;

TArray<TSharedPtr<FJsonValue>> SelectionSmokeNamesToJson(
	const TArray<FName>& Names
)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FName Name : Names)
	{
		Values.Add(MakeShared<FJsonValueString>(Name.ToString()));
	}
	return Values;
}

TArray<FLLMNPCSelectionSmokeCase> SelectionSmokeBuildCases()
{
	TArray<FLLMNPCSelectionSmokeCase> Cases;
	auto AddCase = [&Cases](
		const TCHAR* CaseId,
		const TCHAR* UserMessage,
		bool bExpectedAction,
		const TCHAR* ExpectedSelectionId,
		const TCHAR* ExpectedTargetRef = TEXT("")
	)
	{
		FLLMNPCSelectionSmokeCase& TestCase = Cases.AddDefaulted_GetRef();
		TestCase.CaseId = CaseId;
		TestCase.UserMessage = UserMessage;
		TestCase.bExpectedAction = bExpectedAction;
		TestCase.ExpectedSelectionId = ExpectedSelectionId;
		TestCase.ExpectedTargetRef = ExpectedTargetRef;
		return &TestCase;
	};

	AddCase(
		TEXT("greet_wave"),
		TEXT("Greet me with a friendly wave."),
		true,
		TEXT("gesture.wave.right")
	);
	AddCase(
		TEXT("acknowledge_nod"),
		TEXT("Acknowledge that you understand with a small nod."),
		true,
		TEXT("gesture.nod")
	);
	FLLMNPCSelectionSmokeCase* PointCase = AddCase(
		TEXT("locate_door_point"),
		TEXT("Show me where the door is by pointing at it."),
		true,
		TEXT("gesture.point.target"),
		TEXT("door")
	);
	PointCase->bProvideDoorTarget = true;
	AddCase(
		TEXT("explicit_still"),
		TEXT("Remain completely still and answer without any body gesture."),
		false,
		TEXT("")
	);
	AddCase(
		TEXT("unsupported_clap"),
		TEXT("Clap once. No clap action is offered, so do not substitute another gesture."),
		false,
		TEXT("")
	);
	AddCase(
		TEXT("missing_target"),
		TEXT("Point to the door. No registered scene target is available, so do not substitute another gesture."),
		false,
		TEXT("")
	);
	FLLMNPCSelectionSmokeCase* UnofferedCase = AddCase(
		TEXT("unoffered_wave"),
		TEXT("Wave hello. Wave is intentionally not offered, so do not substitute another gesture."),
		false,
		TEXT("")
	);
	UnofferedCase->AllowedSourceSelectionIds = { TEXT("gesture.nod") };
	return Cases;
}

bool SelectionSmokePayloadIsPrivate(
	const FString& Payload,
	FString& OutForbiddenToken
)
{
	OutForbiddenToken.Reset();
	for (const TCHAR* Forbidden : {
		TEXT("\"bone\""),
		TEXT("\"bone_name\""),
		TEXT("\"transform\""),
		TEXT("\"quaternion\""),
		TEXT("\"compact_pose"),
		TEXT("\"component_space"),
		TEXT("clavicle_"),
		TEXT("upperarm_"),
		TEXT("lowerarm_"),
		TEXT("hand_r"),
		TEXT("/Game/"),
		TEXT("/LLMNPCActionLayer/")
	})
	{
		if (Payload.Contains(Forbidden, ESearchCase::IgnoreCase))
		{
			OutForbiddenToken = Forbidden;
			return false;
		}
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Payload);
	FString SchemaVersion;
	return
		FJsonSerializer::Deserialize(Reader, Root) &&
		Root.IsValid() &&
		Root->TryGetStringField(TEXT("schema_version"), SchemaVersion) &&
		SchemaVersion == TEXT("llmnpc.turn_request.v3");
}

FName SelectionSmokeResolveError(
	const FLLMNPCSelectionSmokeSample& Sample,
	const FLLMNPCModelTurnResult& Result,
	const FString& ParserError,
	const FString& PolicyError,
	const FString& ValidatorError,
	const FString& CompilerError
)
{
	if (!Result.bSuccess)
	{
		return Result.ErrorCode.IsNone()
			? FName(TEXT("LLMNPC_N2_PROVIDER_FAILED"))
			: Result.ErrorCode;
	}
	if (!Sample.bProviderMatches)
	{
		return TEXT("LLMNPC_N2_PROVIDER_MISMATCH");
	}
	if (!Sample.bModelMatches)
	{
		return TEXT("LLMNPC_N2_MODEL_MISMATCH");
	}
	if (!Sample.bConfigMatches)
	{
		return TEXT("LLMNPC_N2_CONFIG_CHANGED");
	}
	if (!Sample.bPrivacyScanPassed)
	{
		return TEXT("LLMNPC_N2_REQUEST_PRIVACY_FAILED");
	}
	if (!Sample.bSchemaValid)
	{
		return ParserError.IsEmpty()
			? FName(TEXT("LLMNPC_N2_RESPONSE_SCHEMA_INVALID"))
			: FName(*ParserError);
	}
	if (!Sample.bSelectionPolicyValid)
	{
		return PolicyError.IsEmpty()
			? FName(TEXT("LLMNPC_N2_SELECTION_POLICY_REJECTED"))
			: FName(*PolicyError);
	}
	if (!Sample.bValidatorAccepted)
	{
		return ValidatorError.IsEmpty()
			? FName(TEXT("LLMNPC_N2_VALIDATOR_REJECTED"))
			: FName(*ValidatorError);
	}
	if (!Sample.bCompiled)
	{
		return CompilerError.IsEmpty()
			? FName(TEXT("LLMNPC_N2_TEMPLATE_COMPILE_FAILED"))
			: FName(*CompilerError);
	}
	if (!Sample.bExpectedMatch)
	{
		return TEXT("LLMNPC_N2_SELECTION_EXPECTATION_MISMATCH");
	}
	return NAME_None;
}

float SelectionSmokeRate(int32 Numerator, int32 Denominator)
{
	return Denominator > 0
		? static_cast<float>(Numerator) / static_cast<float>(Denominator)
		: 0.0f;
}

float SelectionSmokePercentile95(TArray<float> Values)
{
	Values.RemoveAll(
		[](float Value)
		{
			return Value < 0.0f || !FMath::IsFinite(Value);
		}
	);
	if (Values.IsEmpty())
	{
		return -1.0f;
	}
	Values.Sort();
	const int32 Index = FMath::Clamp(
		FMath::CeilToInt(Values.Num() * 0.95f) - 1,
		0,
		Values.Num() - 1
	);
	return Values[Index];
}

bool SelectionSmokeSaveReport(
	const FLLMNPCActiveCatalogSelectionSmoke& Run,
	bool bPassed,
	FString& OutFilename,
	FString& OutError
)
{
	OutFilename.Reset();
	OutError.Reset();
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(
		TEXT("schema_version"),
		TEXT("llmnpc.forward_n2_catalog_selection_report.v1")
	);
	Root->SetStringField(TEXT("suite_version"), TEXT("forward_n2.selection.v1"));
	Root->SetStringField(TEXT("status"), bPassed ? TEXT("passed") : TEXT("failed"));
	Root->SetStringField(TEXT("generated_at"), FDateTime::UtcNow().ToIso8601());
	Root->SetNumberField(TEXT("runs_per_case"), SelectionSmokeRunsPerCase);
	Root->SetNumberField(TEXT("case_count"), Run.Cases.Num());
	Root->SetNumberField(TEXT("sample_count"), Run.Samples.Num());
	Root->SetStringField(TEXT("catalog_hash"), Run.Library->GetCatalogHash());
	Root->SetStringField(TEXT("profile_id"), Run.Profile->ProfileId.ToString());
	Root->SetBoolField(
		TEXT("local_unoffered_gate_passed"),
		Run.bLocalUnofferedGatePassed
	);

	TSharedRef<FJsonObject> Session = MakeShared<FJsonObject>();
	Session->SetStringField(TEXT("expected_provider"), TEXT("deepseek_direct_editor"));
	Session->SetStringField(TEXT("expected_model"), Run.Config.Model);
	Session->SetStringField(TEXT("endpoint_origin"), Run.Config.EndpointOrigin);
	Session->SetStringField(
		TEXT("non_secret_config_hash"),
		Run.Config.NonSecretConfigHash
	);
	Session->SetBoolField(TEXT("raw_requests_persisted"), false);
	Session->SetBoolField(TEXT("raw_responses_persisted"), false);
	Root->SetObjectField(TEXT("online_test_session"), Session);

	int32 PassedCount = 0;
	int32 SchemaCount = 0;
	int32 IllegalCandidateCount = 0;
	int32 UnnecessaryActionCount = 0;
	int32 MissedActionCount = 0;
	int32 TargetErrorCount = 0;
	int32 ValidatorRejectCount = 0;
	int32 ExecutionCompleteCount = 0;
	int64 TotalTokenSum = 0;
	int32 TokenSampleCount = 0;
	double LatencySum = 0.0;
	int32 LatencySampleCount = 0;
	TArray<float> Latencies;
	TArray<TSharedPtr<FJsonValue>> SampleValues;
	for (const FLLMNPCSelectionSmokeSample& Sample : Run.Samples)
	{
		PassedCount += Sample.bPassed ? 1 : 0;
		SchemaCount += Sample.bSchemaValid ? 1 : 0;
		IllegalCandidateCount +=
			Sample.bSchemaValid &&
			!Sample.bObservedNone &&
			!Sample.bSelectionPolicyValid
				? 1
				: 0;
		UnnecessaryActionCount +=
			!Sample.bExpectedAction && !Sample.bObservedNone ? 1 : 0;
		MissedActionCount +=
			Sample.bExpectedAction && Sample.bObservedNone ? 1 : 0;
		TargetErrorCount +=
			!Sample.ExpectedTargetRef.IsEmpty() &&
			Sample.ObservedTargetRef != Sample.ExpectedTargetRef
				? 1
				: 0;
		ValidatorRejectCount +=
			Sample.bSelectionPolicyValid && !Sample.bValidatorAccepted ? 1 : 0;
		ExecutionCompleteCount += Sample.bCompiled ? 1 : 0;
		if (Sample.TotalTokens >= 0)
		{
			TotalTokenSum += Sample.TotalTokens;
			++TokenSampleCount;
		}
		if (Sample.LatencySeconds >= 0.0f)
		{
			LatencySum += Sample.LatencySeconds;
			++LatencySampleCount;
			Latencies.Add(Sample.LatencySeconds);
		}

		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("case_id"), Sample.CaseId.ToString());
		Object->SetNumberField(TEXT("iteration"), Sample.Iteration);
		Object->SetStringField(
			TEXT("request_id"),
			Sample.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower)
		);
		Object->SetStringField(TEXT("request_hash"), Sample.RequestHash);
		Object->SetArrayField(
			TEXT("offered_selection_ids"),
			SelectionSmokeNamesToJson(Sample.OfferedSelectionIds)
		);
		Object->SetStringField(
			TEXT("expected_selection_id"),
			Sample.ExpectedSelectionId.ToString()
		);
		Object->SetStringField(TEXT("expected_target_ref"), Sample.ExpectedTargetRef);
		Object->SetStringField(
			TEXT("observed_selection_id"),
			Sample.ObservedSelectionId.ToString()
		);
		Object->SetStringField(
			TEXT("resolved_template_id"),
			Sample.ResolvedTemplateId.ToString()
		);
		Object->SetStringField(TEXT("observed_target_ref"), Sample.ObservedTargetRef);
		Object->SetBoolField(TEXT("privacy_scan_passed"), Sample.bPrivacyScanPassed);
		Object->SetBoolField(TEXT("schema_valid"), Sample.bSchemaValid);
		Object->SetBoolField(
			TEXT("selection_policy_valid"),
			Sample.bSelectionPolicyValid
		);
		Object->SetBoolField(TEXT("validator_accepted"), Sample.bValidatorAccepted);
		Object->SetBoolField(TEXT("compiled"), Sample.bCompiled);
		Object->SetBoolField(TEXT("expected_match"), Sample.bExpectedMatch);
		Object->SetBoolField(TEXT("passed"), Sample.bPassed);
		Object->SetStringField(TEXT("error_code"), Sample.ErrorCode.ToString());
		Object->SetNumberField(TEXT("http_status"), Sample.HttpStatus);
		Object->SetNumberField(TEXT("attempt_count"), Sample.AttemptCount);
		Object->SetNumberField(TEXT("latency_seconds"), Sample.LatencySeconds);
		Object->SetNumberField(TEXT("prompt_tokens"), Sample.PromptTokens);
		Object->SetNumberField(TEXT("completion_tokens"), Sample.CompletionTokens);
		Object->SetNumberField(TEXT("total_tokens"), Sample.TotalTokens);
		SampleValues.Add(MakeShared<FJsonValueObject>(Object));
	}
	Root->SetArrayField(TEXT("samples"), SampleValues);

	TSharedRef<FJsonObject> Metrics = MakeShared<FJsonObject>();
	const int32 SampleCount = Run.Samples.Num();
	Metrics->SetNumberField(
		TEXT("pass_rate"),
		SelectionSmokeRate(PassedCount, SampleCount)
	);
	Metrics->SetNumberField(
		TEXT("schema_success_rate"),
		SelectionSmokeRate(SchemaCount, SampleCount)
	);
	Metrics->SetNumberField(
		TEXT("illegal_candidate_rate"),
		SelectionSmokeRate(IllegalCandidateCount, SampleCount)
	);
	Metrics->SetNumberField(
		TEXT("unnecessary_action_rate"),
		SelectionSmokeRate(UnnecessaryActionCount, SampleCount)
	);
	Metrics->SetNumberField(
		TEXT("missed_action_rate"),
		SelectionSmokeRate(MissedActionCount, SampleCount)
	);
	Metrics->SetNumberField(
		TEXT("target_error_rate"),
		SelectionSmokeRate(TargetErrorCount, SampleCount)
	);
	Metrics->SetNumberField(
		TEXT("validator_reject_rate"),
		SelectionSmokeRate(ValidatorRejectCount, SampleCount)
	);
	Metrics->SetNumberField(
		TEXT("execution_completion_rate"),
		SelectionSmokeRate(ExecutionCompleteCount, SampleCount)
	);
	Metrics->SetNumberField(
		TEXT("average_latency_seconds"),
		LatencySampleCount > 0 ? LatencySum / LatencySampleCount : -1.0
	);
	Metrics->SetNumberField(
		TEXT("p95_latency_seconds"),
		SelectionSmokePercentile95(Latencies)
	);
	Metrics->SetNumberField(
		TEXT("average_total_tokens"),
		TokenSampleCount > 0
			? static_cast<double>(TotalTokenSum) / TokenSampleCount
			: -1.0
	);
	Root->SetObjectField(TEXT("metrics"), Metrics);

	FString ReportJson;
	if (!FLLMNPCOnlineReportSanitizer::SanitizeAndSerialize(Root, ReportJson))
	{
		OutError = TEXT("LLMNPC_N2_REPORT_SERIALIZE_FAILED");
		return false;
	}
	FString ApiKey;
	ELLMNPCCredentialSource CredentialSource =
		ELLMNPCCredentialSource::Missing;
	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	const bool bHasCredential =
		Settings &&
		FLLMNPCProviderCredentials::ResolveDeepSeekApiKey(
			*Settings,
			ApiKey,
			CredentialSource
		);
	static_cast<void>(CredentialSource);
	const bool bSecretDetected =
		(bHasCredential && !ApiKey.IsEmpty() && ReportJson.Contains(ApiKey)) ||
		ReportJson.Contains(TEXT("Bearer "), ESearchCase::IgnoreCase) ||
		ReportJson.Contains(TEXT("OPENAI_API_KEY"), ESearchCase::IgnoreCase);
	ApiKey.Reset();
	if (bSecretDetected)
	{
		OutError = TEXT("LLMNPC_N2_REPORT_SECRET_DETECTED");
		return false;
	}

	const FString Directory = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("LLMNPCActionLayer/ForwardN2/Reports")
	);
	if (!IFileManager::Get().MakeDirectory(*Directory, true))
	{
		OutError = TEXT("LLMNPC_N2_REPORT_DIRECTORY_FAILED");
		return false;
	}
	OutFilename = FPaths::Combine(
		Directory,
		FString::Printf(
			TEXT("catalog_selection_%s.json"),
			*FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S"))
		)
	);
	if (!FFileHelper::SaveStringToFile(
		ReportJson,
		*OutFilename,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM
	))
	{
		OutFilename.Reset();
		OutError = TEXT("LLMNPC_N2_REPORT_WRITE_FAILED");
		return false;
	}
	return true;
}

void SelectionSmokeFinish(
	const TSharedPtr<FLLMNPCActiveCatalogSelectionSmoke>& Run
)
{
	if (!Run.IsValid() || ActiveSelectionSmoke != Run)
	{
		return;
	}
	const int32 ExpectedSampleCount =
		Run->Cases.Num() * SelectionSmokeRunsPerCase;
	const bool bPassed =
		Run->bLocalUnofferedGatePassed &&
		Run->Samples.Num() == ExpectedSampleCount &&
		Run->Samples.ContainsByPredicate(
			[](const FLLMNPCSelectionSmokeSample& Sample)
			{
				return !Sample.bPassed;
			}
		) == false;
	FString ReportFilename;
	FString ReportError;
	const bool bSaved = SelectionSmokeSaveReport(
		*Run,
		bPassed,
		ReportFilename,
		ReportError
	);
	const FLLMNPCSelectionSmokeSample* LastSample =
		Run->Samples.IsEmpty() ? nullptr : &Run->Samples.Last();
	FLLMNPCOnlineTestConfigLoader::RecordConnectionTest(
		bPassed && bSaved,
		FLLMNPCProviderCredentials::DeepSeekProviderId(),
		Run->Config.Model,
		Run->Config.NonSecretConfigHash,
		bPassed && bSaved
			? NAME_None
			: FName(TEXT("LLMNPC_N2_SELECTION_SUITE_FAILED")),
		LastSample ? LastSample->HttpStatus : 0,
		LastSample ? LastSample->LatencySeconds : -1.0f
	);
	if (bPassed && bSaved)
	{
		UE_LOG(
			LogLLMNPCCatalogSelectionSmoke,
			Display,
			TEXT("LLMNPC Forward N2 Catalog Selection PASSED. Samples=%d Catalog=%s Model=%s Report=%s"),
			Run->Samples.Num(),
			*Run->Library->GetCatalogHash(),
			*Run->Config.Model,
			*ReportFilename
		);
	}
	else
	{
		UE_LOG(
			LogLLMNPCCatalogSelectionSmoke,
			Error,
			TEXT("LLMNPC Forward N2 Catalog Selection FAILED. Samples=%d Expected=%d ReportError=%s Report=%s"),
			Run->Samples.Num(),
			ExpectedSampleCount,
			*ReportError,
			*ReportFilename
		);
	}
	const bool bExit = Run->bExitEditorWhenComplete;
	ActiveSelectionSmoke.Reset();
	if (bExit)
	{
		FPlatformMisc::RequestExit(false);
	}
}

bool SelectionSmokePrepareRequest(
	const TSharedPtr<FLLMNPCActiveCatalogSelectionSmoke>& Run,
	FLLMNPCModelTurnRequest& OutRequest,
	FString& OutError
)
{
	OutError.Reset();
	const FLLMNPCSelectionSmokeCase& TestCase = Run->Cases[Run->CaseIndex];
	TArray<FLLMNPCTemplateCandidate> SourceCandidates;
	for (const FLLMNPCTemplateCandidate& Candidate : Run->BaseCandidates)
	{
		if (
			TestCase.AllowedSourceSelectionIds.IsEmpty() ||
			TestCase.AllowedSourceSelectionIds.Contains(Candidate.SelectionId)
		)
		{
			SourceCandidates.Add(Candidate);
		}
	}

	FLLMNPCSelectionContextSnapshot Context;
	Context.Personality.ProfileId = TEXT("n2_eval_neutral");
	Context.Relationship.OtherActorRef = TEXT("player");
	if (TestCase.bProvideDoorTarget)
	{
		FLLMNPCSceneTargetContext& Door =
			Context.AvailableTargets.AddDefaulted_GetRef();
		Door.TargetRef = TEXT("door");
		Door.Category = TEXT("scene_target");
		Door.SemanticTags = { TEXT("door"), TEXT("location") };
		Door.Salience = 1.0f;
	}
	FLLMNPCCandidateRetrievalRequest RetrievalRequest;
	RetrievalRequest.UserMessage = TestCase.UserMessage;
	RetrievalRequest.SourceCandidates = SourceCandidates;
	RetrievalRequest.Context = Context;
	RetrievalRequest.MaxCandidates = 8;
	RetrievalRequest.RepeatSuppressionSeconds = 0.0f;
	const FLLMNPCCandidateRetrievalResult Retrieval =
		ULLMNPCCandidateRetriever::Retrieve(RetrievalRequest);
	Run->CurrentOfferedCandidates = Retrieval.Candidates;
	if (
		TestCase.bExpectedAction &&
		!Run->CurrentOfferedCandidates.ContainsByPredicate(
			[&TestCase](const FLLMNPCTemplateCandidate& Candidate)
			{
				return Candidate.SelectionId == TestCase.ExpectedSelectionId;
			}
		)
	)
	{
		OutError = TEXT("LLMNPC_N2_EXPECTED_CANDIDATE_NOT_OFFERED");
		return false;
	}

	ULLMNPCConversationSession* Session =
		NewObject<ULLMNPCConversationSession>();
	Session->InitializeSession(TEXT("manny_n2_selection_eval"), 8);
	Session->AddMessage(ELLMNPCDialogueRole::Player, TestCase.UserMessage);
	Run->CurrentRequestId = FGuid::NewGuid();
	const FString ContextJson = Session->BuildContextualRequestJsonForSchema(
		Run->CurrentRequestId,
		Run->CurrentOfferedCandidates,
		Context,
		TEXT("llmnpc.selection_prompt.v3"),
		TEXT("llmnpc.turn_request.v3")
	);
	FString ForbiddenToken;
	Run->bCurrentPrivacyScanPassed =
		SelectionSmokePayloadIsPrivate(ContextJson, ForbiddenToken);
	if (!Run->bCurrentPrivacyScanPassed)
	{
		OutError = ForbiddenToken.IsEmpty()
			? TEXT("LLMNPC_N2_REQUEST_SCHEMA_OR_PRIVACY_INVALID")
			: FString::Printf(
				TEXT("LLMNPC_N2_REQUEST_FORBIDDEN_TOKEN:%s"),
				*ForbiddenToken
			);
		return false;
	}
	Run->CurrentRequestHash = FString::Printf(
		TEXT("md5:%s"),
		*FMD5::HashAnsiString(*ContextJson)
	);
	OutRequest.RequestId = Run->CurrentRequestId;
	OutRequest.SessionId = Run->SessionId;
	OutRequest.NPCId = TEXT("manny_n2_selection_eval");
	OutRequest.UserMessage = TestCase.UserMessage;
	OutRequest.ContextJson = ContextJson;
	return true;
}

void SelectionSmokeStartNext(
	const TSharedPtr<FLLMNPCActiveCatalogSelectionSmoke>& Run
);

void SelectionSmokeCompleteSample(
	const TSharedPtr<FLLMNPCActiveCatalogSelectionSmoke>& Run,
	const FLLMNPCModelTurnResult& Result
)
{
	if (!Run.IsValid() || ActiveSelectionSmoke != Run)
	{
		return;
	}
	const FLLMNPCSelectionSmokeCase& TestCase = Run->Cases[Run->CaseIndex];
	FLLMNPCSelectionSmokeSample Sample;
	Sample.CaseId = TestCase.CaseId;
	Sample.Iteration = Run->Iteration + 1;
	Sample.RequestId = Run->CurrentRequestId;
	Sample.RequestHash = Run->CurrentRequestHash;
	Sample.ExpectedSelectionId = TestCase.ExpectedSelectionId;
	Sample.ExpectedTargetRef = TestCase.ExpectedTargetRef;
	Sample.bExpectedAction = TestCase.bExpectedAction;
	Sample.bProviderSuccess = Result.bSuccess;
	Sample.bProviderMatches =
		Result.ProviderId == FLLMNPCProviderCredentials::DeepSeekProviderId();
	Sample.bModelMatches =
		!Run->Config.Model.IsEmpty() &&
		Result.ProviderModelId == Run->Config.Model;
	const FLLMNPCOnlineTestConfigState CurrentConfig =
		FLLMNPCOnlineTestConfigLoader::GetState();
	Sample.bConfigMatches =
		CurrentConfig.IsLoaded() &&
		CurrentConfig.NonSecretConfigHash == Run->Config.NonSecretConfigHash;
	Sample.bPrivacyScanPassed = Run->bCurrentPrivacyScanPassed;
	Sample.HttpStatus = Result.HttpStatus;
	Sample.AttemptCount = Result.AttemptCount;
	Sample.LatencySeconds = Result.TotalLatencySeconds;
	Sample.PromptTokens = Result.PromptTokens;
	Sample.CompletionTokens = Result.CompletionTokens;
	Sample.TotalTokens = Result.TotalTokens;
	for (const FLLMNPCTemplateCandidate& Candidate : Run->CurrentOfferedCandidates)
	{
		Sample.OfferedSelectionIds.Add(Candidate.SelectionId);
	}

	FString ParserError;
	FString PolicyError;
	FString ValidatorError;
	FString CompilerError;
	FLLMNPCModelTurnDecision Decision;
	const ULLMNPCMotionTemplate* ResolvedTemplate = nullptr;
	FLLMNPCTemplateModifiers Modifiers;
	if (Result.bSuccess)
	{
		Sample.bSchemaValid = FLLMNPCModelTurnParser::Parse(
			Result.ResponseJson,
			Decision,
			ParserError
		);
	}
	if (Sample.bSchemaValid)
	{
		Sample.ObservedSelectionId = Decision.Action.TemplateId;
		Sample.ObservedTargetRef = Decision.Action.TargetRef;
		Sample.bObservedNone = Decision.Action.Decision == TEXT("none");
		Sample.bSelectionPolicyValid =
			ULLMNPCCandidateRetriever::ApplySelectionPolicy(
				Decision,
				Run->CurrentOfferedCandidates,
				PolicyError
			);
		Sample.ObservedTargetRef = Decision.Action.TargetRef;
	}
	if (Sample.bSelectionPolicyValid)
	{
		Sample.bValidatorAccepted =
			FLLMNPCModelTurnValidator::ValidateAndResolve(
				Decision,
				*Run->Library,
				Run->Profile->ProfileId,
				ResolvedTemplate,
				Modifiers,
				ValidatorError
			);
	}
	if (Sample.bValidatorAccepted)
	{
		if (Decision.Action.Decision == TEXT("none"))
		{
			Sample.bCompiled = true;
		}
		else if (ResolvedTemplate)
		{
			FLLMMotionPlan Plan;
			Sample.bCompiled = FLLMNPCTemplateCompiler::Compile(
				*ResolvedTemplate,
				Modifiers,
				*Run->Profile,
				Plan,
				CompilerError
			);
			Sample.ResolvedTemplateId = ResolvedTemplate->Metadata.TemplateId;
		}
	}
	const bool bLocomotionNone =
		Sample.bSchemaValid &&
		Decision.Locomotion.Decision == TEXT("none");
	if (TestCase.bExpectedAction)
	{
		Sample.bExpectedMatch =
			Decision.Action.Decision == TEXT("execute_template") &&
			Sample.ObservedSelectionId == TestCase.ExpectedSelectionId &&
			(TestCase.ExpectedTargetRef.IsEmpty() ||
				Sample.ObservedTargetRef == TestCase.ExpectedTargetRef) &&
			bLocomotionNone;
	}
	else
	{
		Sample.bExpectedMatch =
			Sample.bObservedNone &&
			Decision.Action.TemplateId.IsNone() &&
			Decision.Action.TargetRef.IsEmpty() &&
			bLocomotionNone;
	}
	Sample.ErrorCode = SelectionSmokeResolveError(
		Sample,
		Result,
		ParserError,
		PolicyError,
		ValidatorError,
		CompilerError
	);
	Sample.bPassed =
		Result.bSuccess &&
		Sample.bProviderMatches &&
		Sample.bModelMatches &&
		Sample.bConfigMatches &&
		Sample.bPrivacyScanPassed &&
		Sample.bSchemaValid &&
		Sample.bSelectionPolicyValid &&
		Sample.bValidatorAccepted &&
		Sample.bCompiled &&
		Sample.bExpectedMatch;
	Run->Samples.Add(Sample);

	if (Sample.bPassed)
	{
		UE_LOG(
			LogLLMNPCCatalogSelectionSmoke,
			Display,
			TEXT("N2 selection sample %s %d/%d: PASS observed=%s target=%s latency=%.3fs"),
			*TestCase.CaseId.ToString(),
			Run->Iteration + 1,
			SelectionSmokeRunsPerCase,
			*Sample.ObservedSelectionId.ToString(),
			*Sample.ObservedTargetRef,
			Sample.LatencySeconds
		);
	}
	else
	{
		UE_LOG(
			LogLLMNPCCatalogSelectionSmoke,
			Error,
			TEXT("N2 selection sample %s %d/%d: FAIL observed=%s target=%s error=%s latency=%.3fs"),
			*TestCase.CaseId.ToString(),
			Run->Iteration + 1,
			SelectionSmokeRunsPerCase,
			*Sample.ObservedSelectionId.ToString(),
			*Sample.ObservedTargetRef,
			*Sample.ErrorCode.ToString(),
			Sample.LatencySeconds
		);
	}

	++Run->Iteration;
	if (Run->Iteration >= SelectionSmokeRunsPerCase)
	{
		Run->Iteration = 0;
		++Run->CaseIndex;
	}
	SelectionSmokeStartNext(Run);
}

void SelectionSmokeStartNext(
	const TSharedPtr<FLLMNPCActiveCatalogSelectionSmoke>& Run
)
{
	if (!Run.IsValid() || ActiveSelectionSmoke != Run)
	{
		return;
	}
	if (Run->CaseIndex >= Run->Cases.Num())
	{
		SelectionSmokeFinish(Run);
		return;
	}
	FLLMNPCModelTurnRequest Request;
	FString Error;
	if (!SelectionSmokePrepareRequest(Run, Request, Error))
	{
		FLLMNPCSelectionSmokeSample Sample;
		Sample.CaseId = Run->Cases[Run->CaseIndex].CaseId;
		Sample.Iteration = Run->Iteration + 1;
		Sample.RequestId = Run->CurrentRequestId;
		Sample.bExpectedAction = Run->Cases[Run->CaseIndex].bExpectedAction;
		Sample.ExpectedSelectionId =
			Run->Cases[Run->CaseIndex].ExpectedSelectionId;
		Sample.ExpectedTargetRef =
			Run->Cases[Run->CaseIndex].ExpectedTargetRef;
		Sample.ErrorCode = FName(*Error);
		Run->Samples.Add(Sample);
		UE_LOG(
			LogLLMNPCCatalogSelectionSmoke,
			Error,
			TEXT("N2 selection request preparation failed: %s"),
			*Error
		);
		Run->CaseIndex = Run->Cases.Num();
		SelectionSmokeFinish(Run);
		return;
	}

	const TWeakPtr<FLLMNPCActiveCatalogSelectionSmoke> WeakRun = Run;
	Run->Provider->SendTurn(
		Request,
		[WeakRun](const FLLMNPCModelTurnResult& Result)
		{
			AsyncTask(
				ENamedThreads::GameThread,
				[WeakRun, Result]()
				{
					if (
						const TSharedPtr<FLLMNPCActiveCatalogSelectionSmoke> Pinned =
							WeakRun.Pin()
					)
					{
						SelectionSmokeCompleteSample(Pinned, Result);
					}
				}
			);
		}
	);
}
}

bool FLLMNPCCatalogSelectionSmokeRunner::Start(
	bool bExitEditorWhenComplete,
	FString& OutError
)
{
	OutError.Reset();
	if (ActiveSelectionSmoke.IsValid())
	{
		OutError = TEXT("LLMNPC_N2_SELECTION_SMOKE_ALREADY_RUNNING");
		return false;
	}
	const FLLMNPCOnlineTestConfigState Config =
		FLLMNPCOnlineTestConfigLoader::LoadProjectConfig();
	if (!Config.IsLoaded() || !Config.bCredentialPresent)
	{
		OutError = Config.ErrorCode.IsNone()
			? TEXT("LLMNPC_N2_SELECTION_CONFIG_NOT_READY")
			: Config.ErrorCode.ToString();
		return false;
	}

	TSharedPtr<FLLMNPCActiveCatalogSelectionSmoke> Run =
		MakeShared<FLLMNPCActiveCatalogSelectionSmoke>();
	Run->bExitEditorWhenComplete = bExitEditorWhenComplete;
	Run->Config = Config;
	Run->GameInstance = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>());
	Run->Library = TStrongObjectPtr<ULLMNPCTemplateLibrarySubsystem>(
		NewObject<ULLMNPCTemplateLibrarySubsystem>(Run->GameInstance.Get())
	);
	Run->Library->RefreshLibrary();
	if (
		!Run->Library->GetScanErrors().IsEmpty() ||
		Run->Library->GetPublishedPublicActionCount() < 3
	)
	{
		OutError = TEXT("LLMNPC_N2_SELECTION_CATALOG_NOT_READY");
		return false;
	}
	Run->Profile = TStrongObjectPtr<ULLMNPCSkeletonProfile>(
		LoadObject<ULLMNPCSkeletonProfile>(
			nullptr,
			TEXT("/LLMNPCActionLayer/LLMNPC/SkeletonProfiles/SP_UE5_Manny_v1.SP_UE5_Manny_v1")
		)
	);
	if (!Run->Profile.IsValid())
	{
		OutError = TEXT("LLMNPC_N2_SELECTION_MANNY_PROFILE_MISSING");
		return false;
	}
	Run->Library->QueryRuntimeCandidates(
		Run->Profile->ProfileId,
		Run->BaseCandidates
	);
	if (Run->BaseCandidates.Num() < 3)
	{
		OutError = TEXT("LLMNPC_N2_SELECTION_CANDIDATES_MISSING");
		return false;
	}

	const FLLMNPCTemplateCandidate* Nod =
		Run->BaseCandidates.FindByPredicate(
			[](const FLLMNPCTemplateCandidate& Candidate)
			{
				return Candidate.SelectionId == TEXT("gesture.nod");
			}
		);
	if (!Nod)
	{
		OutError = TEXT("LLMNPC_N2_SELECTION_NOD_CANDIDATE_MISSING");
		return false;
	}
	FLLMNPCModelTurnDecision UnofferedDecision;
	UnofferedDecision.Action.Decision = TEXT("execute_template");
	UnofferedDecision.Action.TemplateId = TEXT("gesture.wave.right");
	UnofferedDecision.Action.Style = TEXT("friendly");
	FString PolicyError;
	Run->bLocalUnofferedGatePassed =
		!ULLMNPCCandidateRetriever::ApplySelectionPolicy(
			UnofferedDecision,
			{ *Nod },
			PolicyError
		) &&
		PolicyError == TEXT("LLMNPC_SELECTION_ACTION_NOT_OFFERED");
	if (!Run->bLocalUnofferedGatePassed)
	{
		OutError = TEXT("LLMNPC_N2_SELECTION_LOCAL_UNOFFERED_GATE_FAILED");
		return false;
	}

	Run->Cases = SelectionSmokeBuildCases();
	Run->Provider = MakeShared<FLLMNPCDeepSeekProvider>();
	ActiveSelectionSmoke = Run;
	UE_LOG(
		LogLLMNPCCatalogSelectionSmoke,
		Display,
		TEXT("LLMNPC Forward N2 Catalog Selection started. Cases=%d RunsPerCase=%d Model=%s Catalog=%s"),
		Run->Cases.Num(),
		SelectionSmokeRunsPerCase,
		*Run->Config.Model,
		*Run->Library->GetCatalogHash()
	);
	SelectionSmokeStartNext(Run);
	return true;
}

bool FLLMNPCCatalogSelectionSmokeRunner::IsRunning()
{
	return ActiveSelectionSmoke.IsValid();
}

void FLLMNPCCatalogSelectionSmokeRunner::Cancel()
{
	const TSharedPtr<FLLMNPCActiveCatalogSelectionSmoke> Run =
		ActiveSelectionSmoke;
	ActiveSelectionSmoke.Reset();
	if (Run.IsValid() && Run->Provider.IsValid() && Run->CurrentRequestId.IsValid())
	{
		Run->Provider->CancelRequest(Run->CurrentRequestId);
	}
}
