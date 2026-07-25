#include "Online/LLMNPCCapabilitySmokeRunner.h"

#include "Async/Async.h"
#include "Capabilities/LLMNPCSkeletonCapabilityBuilder.h"
#include "Dialogue/LLMNPCDialogueTypes.h"
#include "Dialogue/LLMNPCModelTurnValidator.h"
#include "Dom/JsonObject.h"
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
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"

DEFINE_LOG_CATEGORY_STATIC(LogLLMNPCCapabilitySmoke, Log, All);

namespace
{
const FString ChallengePrefix(TEXT("capability_smoke|"));

struct FLLMNPCActiveCapabilitySmoke
{
	bool bExitEditorWhenComplete = false;
	FGuid RequestId;
	FLLMNPCOnlineTestConfigState Config;
	FLLMNPCSkeletonCapabilitySnapshot Snapshot;
	FLLMNPCCapabilitySmokeChallenge Challenge;
	TSharedPtr<FLLMNPCDeepSeekProvider> Provider;
};

TSharedPtr<FLLMNPCActiveCapabilitySmoke> ActiveSmoke;

TArray<TSharedPtr<FJsonValue>> BuildNameArray(const TArray<FName>& Names)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	Values.Reserve(Names.Num());
	for (const FName Name : Names)
	{
		Values.Add(MakeShared<FJsonValueString>(Name.ToString()));
	}
	return Values;
}

void ParseObservedCapabilityIds(
	const FString& AssistantText,
	TArray<FName>& OutCapabilityIds
)
{
	OutCapabilityIds.Reset();
	if (!AssistantText.StartsWith(ChallengePrefix))
	{
		return;
	}

	TArray<FString> Values;
	AssistantText.RightChop(ChallengePrefix.Len()).ParseIntoArray(
		Values,
		TEXT(","),
		true
	);
	for (FString Value : Values)
	{
		Value = Value.TrimStartAndEnd();
		if (!Value.IsEmpty())
		{
			OutCapabilityIds.Add(FName(*Value));
		}
	}
}

bool ContainsPrivateIdentifier(
	const ULLMNPCSkeletonProfile& Profile,
	const FLLMNPCSkeletonCapabilitySnapshot& Snapshot,
	const FString& Payload,
	FString& OutIdentifier
)
{
	OutIdentifier.Reset();
	TSet<FName> PublicCapabilityIds;
	for (const FLLMNPCSemanticCapability& Capability : Snapshot.Capabilities)
	{
		PublicCapabilityIds.Add(Capability.CapabilityId);
	}

	for (const TPair<FName, FName>& Pair : Profile.SemanticBoneMap)
	{
		if (Pair.Value.IsNone() || Pair.Key == Pair.Value)
		{
			continue;
		}
		const FString Quoted = FString::Printf(
			TEXT("\"%s\""),
			*Pair.Value.ToString()
		);
		if (Payload.Contains(Quoted, ESearchCase::IgnoreCase))
		{
			OutIdentifier = Pair.Value.ToString();
			return true;
		}
	}

	for (const FLLMNPCSemanticCapability& Capability : Snapshot.Capabilities)
	{
		for (const FName ControlId : Capability.InternalControlIds)
		{
			if (ControlId.IsNone() || PublicCapabilityIds.Contains(ControlId))
			{
				continue;
			}
			const FString Quoted = FString::Printf(
				TEXT("\"%s\""),
				*ControlId.ToString()
			);
			if (Payload.Contains(Quoted, ESearchCase::IgnoreCase))
			{
				OutIdentifier = ControlId.ToString();
				return true;
			}
		}
	}

	for (const TCHAR* RestrictedToken : {
		TEXT("\"bone_name\""),
		TEXT("\"compact_pose_index\""),
		TEXT("\"quaternion\""),
		TEXT("\"transform\""),
		TEXT("\"component_space\"")
	})
	{
		if (Payload.Contains(RestrictedToken, ESearchCase::IgnoreCase))
		{
			OutIdentifier = RestrictedToken;
			return true;
		}
	}
	return false;
}

FName ResolveSmokeError(
	const FLLMNPCModelTurnResult& Result,
	bool bProviderMatches,
	bool bModelMatches,
	bool bConfigMatches,
	const FLLMNPCCapabilitySmokeValidation& Validation
)
{
	if (!Result.bSuccess)
	{
		return Result.ErrorCode.IsNone()
			? FName(TEXT("LLMNPC_CAPABILITY_SMOKE_PROVIDER_FAILED"))
			: Result.ErrorCode;
	}
	if (!bProviderMatches)
	{
		return TEXT("LLMNPC_CAPABILITY_SMOKE_PROVIDER_MISMATCH");
	}
	if (!bModelMatches)
	{
		return TEXT("LLMNPC_CAPABILITY_SMOKE_MODEL_MISMATCH");
	}
	if (!bConfigMatches)
	{
		return TEXT("LLMNPC_CAPABILITY_SMOKE_CONFIG_CHANGED");
	}
	if (!Validation.ErrorCode.IsNone())
	{
		return Validation.ErrorCode;
	}
	return NAME_None;
}

bool SaveSmokeReport(
	const FLLMNPCActiveCapabilitySmoke& Run,
	const FLLMNPCModelTurnResult& Result,
	const FLLMNPCCapabilitySmokeValidation& Validation,
	bool bProviderMatches,
	bool bModelMatches,
	bool bConfigMatches,
	bool bPassed,
	FName ErrorCode,
	FString& OutFilename,
	FString& OutError
)
{
	OutFilename.Reset();
	OutError.Reset();
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(
		TEXT("schema_version"),
		TEXT("llmnpc.forward_n1_capability_smoke_report.v1")
	);
	Root->SetStringField(TEXT("status"), bPassed ? TEXT("passed") : TEXT("failed"));
	Root->SetStringField(TEXT("generated_at"), FDateTime::UtcNow().ToIso8601());
	Root->SetStringField(
		TEXT("request_id"),
		Run.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower)
	);
	Root->SetStringField(TEXT("profile_id"), Run.Snapshot.ProfileId.ToString());
	Root->SetStringField(
		TEXT("profile_semantic_version"),
		Run.Snapshot.ProfileSemanticVersion
	);
	Root->SetStringField(
		TEXT("skeleton_signature"),
		Run.Snapshot.SkeletonSignature
	);
	Root->SetStringField(
		TEXT("capability_hash"),
		Run.Snapshot.CapabilityHash
	);

	TSharedRef<FJsonObject> Session = MakeShared<FJsonObject>();
	Session->SetStringField(TEXT("expected_model"), Run.Config.Model);
	Session->SetStringField(TEXT("endpoint_origin"), Run.Config.EndpointOrigin);
	Session->SetStringField(
		TEXT("non_secret_config_hash"),
		Run.Config.NonSecretConfigHash
	);
	Session->SetStringField(TEXT("provider_id"), Result.ProviderId.ToString());
	Session->SetStringField(TEXT("provider_model_id"), Result.ProviderModelId);
	Session->SetBoolField(TEXT("provider_matches"), bProviderMatches);
	Session->SetBoolField(TEXT("model_matches"), bModelMatches);
	Session->SetBoolField(TEXT("config_matches"), bConfigMatches);
	Root->SetObjectField(TEXT("online_test_session"), Session);

	TSharedRef<FJsonObject> RequestEvidence = MakeShared<FJsonObject>();
	RequestEvidence->SetStringField(
		TEXT("model_view_hash"),
		Run.Challenge.ModelViewHash
	);
	RequestEvidence->SetStringField(
		TEXT("payload_hash"),
		Run.Challenge.PayloadHash
	);
	RequestEvidence->SetNumberField(
		TEXT("capability_count"),
		Run.Snapshot.Capabilities.Num()
	);
	RequestEvidence->SetBoolField(
		TEXT("restricted_field_scan_passed"),
		Run.Challenge.bRestrictedFieldScanPassed
	);
	RequestEvidence->SetBoolField(
		TEXT("private_identifier_scan_passed"),
		Run.Challenge.bPrivateIdentifierScanPassed
	);
	RequestEvidence->SetBoolField(TEXT("raw_request_persisted"), false);
	RequestEvidence->SetBoolField(TEXT("raw_response_persisted"), false);
	Root->SetObjectField(TEXT("request_evidence"), RequestEvidence);

	TSharedRef<FJsonObject> Challenge = MakeShared<FJsonObject>();
	Challenge->SetStringField(
		TEXT("challenge_id"),
		Run.Challenge.ChallengeId.ToString()
	);
	Challenge->SetArrayField(
		TEXT("expected_capability_ids"),
		BuildNameArray(Run.Challenge.ExpectedCapabilityIds)
	);
	Challenge->SetArrayField(
		TEXT("observed_capability_ids"),
		BuildNameArray(Validation.ObservedCapabilityIds)
	);
	Challenge->SetBoolField(
		TEXT("schema_valid"),
		Validation.bSchemaValid
	);
	Challenge->SetBoolField(
		TEXT("no_action_contract_valid"),
		Validation.bNoActionContractValid
	);
	Challenge->SetBoolField(
		TEXT("exact_capability_selection"),
		Validation.bExactCapabilitySelection
	);
	Root->SetObjectField(TEXT("challenge"), Challenge);

	TSharedRef<FJsonObject> Metrics = MakeShared<FJsonObject>();
	Metrics->SetNumberField(TEXT("http_status"), Result.HttpStatus);
	Metrics->SetNumberField(TEXT("attempt_count"), Result.AttemptCount);
	Metrics->SetNumberField(
		TEXT("latency_seconds"),
		Result.TotalLatencySeconds
	);
	Metrics->SetNumberField(TEXT("prompt_tokens"), Result.PromptTokens);
	Metrics->SetNumberField(TEXT("completion_tokens"), Result.CompletionTokens);
	Metrics->SetNumberField(TEXT("total_tokens"), Result.TotalTokens);
	Root->SetObjectField(TEXT("metrics"), Metrics);
	Root->SetStringField(TEXT("error_code"), ErrorCode.ToString());

	FString ReportJson;
	if (!FLLMNPCOnlineReportSanitizer::SanitizeAndSerialize(Root, ReportJson))
	{
		OutError = TEXT("LLMNPC_CAPABILITY_SMOKE_REPORT_SERIALIZE_FAILED");
		return false;
	}

	FString ApiKey;
	ELLMNPCCredentialSource CredentialSource =
		ELLMNPCCredentialSource::Missing;
	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	const bool bResolvedCredential =
		Settings &&
		FLLMNPCProviderCredentials::ResolveDeepSeekApiKey(
			*Settings,
			ApiKey,
			CredentialSource
		);
	static_cast<void>(CredentialSource);
	const bool bContainsCredential =
		bResolvedCredential &&
		!ApiKey.IsEmpty() &&
		ReportJson.Contains(ApiKey);
	ApiKey.Reset();
	if (
		bContainsCredential ||
		ReportJson.Contains(TEXT("Bearer "), ESearchCase::IgnoreCase) ||
		ReportJson.Contains(TEXT("OPENAI_API_KEY"), ESearchCase::IgnoreCase)
	)
	{
		OutError = TEXT("LLMNPC_CAPABILITY_SMOKE_REPORT_SECRET_DETECTED");
		return false;
	}

	const FString Directory = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("LLMNPCActionLayer"),
		TEXT("ForwardN1"),
		TEXT("Reports")
	);
	if (!IFileManager::Get().MakeDirectory(*Directory, true))
	{
		OutError = TEXT("LLMNPC_CAPABILITY_SMOKE_REPORT_DIRECTORY_FAILED");
		return false;
	}
	OutFilename = FPaths::Combine(
		Directory,
		FString::Printf(
			TEXT("capability_smoke_%s.json"),
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
		OutError = TEXT("LLMNPC_CAPABILITY_SMOKE_REPORT_WRITE_FAILED");
		return false;
	}
	return true;
}

void CompleteSmoke(
	const TSharedPtr<FLLMNPCActiveCapabilitySmoke>& Run,
	const FLLMNPCModelTurnResult& Result
)
{
	if (!Run.IsValid() || ActiveSmoke != Run)
	{
		return;
	}

	const FLLMNPCOnlineTestConfigState CurrentConfig =
		FLLMNPCOnlineTestConfigLoader::GetState();
	FLLMNPCCapabilitySmokeValidation Validation;
	if (Result.bSuccess)
	{
		FLLMNPCCapabilitySmokeRunner::ValidateResponse(
			Result.ResponseJson,
			Run->Challenge,
			Validation
		);
	}
	const bool bProviderMatches =
		Result.ProviderId ==
		FLLMNPCProviderCredentials::DeepSeekProviderId();
	const bool bModelMatches =
		!Run->Config.Model.IsEmpty() &&
		Result.ProviderModelId == Run->Config.Model;
	const bool bConfigMatches =
		CurrentConfig.IsLoaded() &&
		CurrentConfig.NonSecretConfigHash ==
			Run->Config.NonSecretConfigHash;
	const FName ErrorCode = ResolveSmokeError(
		Result,
		bProviderMatches,
		bModelMatches,
		bConfigMatches,
		Validation
	);
	const bool bPassed =
		Result.bSuccess &&
		bProviderMatches &&
		bModelMatches &&
		bConfigMatches &&
		Validation.bSchemaValid &&
		Validation.bNoActionContractValid &&
		Validation.bExactCapabilitySelection;

	FLLMNPCOnlineTestConfigLoader::RecordConnectionTest(
		bPassed,
		Result.ProviderId,
		Result.ProviderModelId,
		Run->Config.NonSecretConfigHash,
		ErrorCode,
		Result.HttpStatus,
		Result.TotalLatencySeconds
	);

	FString ReportFilename;
	FString ReportError;
	const bool bReportSaved = SaveSmokeReport(
		*Run,
		Result,
		Validation,
		bProviderMatches,
		bModelMatches,
		bConfigMatches,
		bPassed,
		ErrorCode,
		ReportFilename,
		ReportError
	);
	if (bPassed && bReportSaved)
	{
		UE_LOG(
			LogLLMNPCCapabilitySmoke,
			Display,
			TEXT("LLMNPC Forward N1 Capability Smoke PASSED. Model=%s CapabilityHash=%s Report=%s"),
			*Result.ProviderModelId,
			*Run->Snapshot.CapabilityHash,
			*ReportFilename
		);
	}
	else
	{
		UE_LOG(
			LogLLMNPCCapabilitySmoke,
			Error,
			TEXT("LLMNPC Forward N1 Capability Smoke FAILED. Error=%s ReportError=%s Report=%s"),
			*ErrorCode.ToString(),
			*ReportError,
			*ReportFilename
		);
	}

	const bool bExitEditorWhenComplete = Run->bExitEditorWhenComplete;
	ActiveSmoke.Reset();
	if (bExitEditorWhenComplete)
	{
		FPlatformMisc::RequestExit(false);
	}
}
}

bool FLLMNPCCapabilitySmokeRunner::BuildChallenge(
	const ULLMNPCSkeletonProfile& Profile,
	FLLMNPCSkeletonCapabilitySnapshot& OutSnapshot,
	FLLMNPCCapabilitySmokeChallenge& OutChallenge,
	FString& OutError
)
{
	OutSnapshot = FLLMNPCSkeletonCapabilitySnapshot();
	OutChallenge = FLLMNPCCapabilitySmokeChallenge();
	OutError.Reset();
	const FLLMNPCSkeletonCapabilityBuildResult BuildResult =
		FLLMNPCSkeletonCapabilityBuilder::Build(
			Profile,
			nullptr,
			OutSnapshot
		);
	if (!BuildResult.bSucceeded)
	{
		OutError = BuildResult.Errors.IsEmpty()
			? TEXT("LLMNPC_CAPABILITY_SMOKE_BUILD_FAILED")
			: BuildResult.Errors[0];
		return false;
	}

	FString ModelViewJson;
	if (!FLLMNPCSkeletonCapabilityBuilder::BuildModelViewJson(
		OutSnapshot,
		ModelViewJson,
		OutError
	))
	{
		return false;
	}
	FString RestrictedField;
	OutChallenge.bRestrictedFieldScanPassed =
		!FLLMNPCSkeletonCapabilityBuilder::ModelViewContainsRestrictedFields(
			ModelViewJson,
			RestrictedField
		);
	if (!OutChallenge.bRestrictedFieldScanPassed)
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_CAPABILITY_SMOKE_RESTRICTED_FIELD:%s"),
			*RestrictedField
		);
		return false;
	}

	for (const FLLMNPCSemanticCapability& Capability : OutSnapshot.Capabilities)
	{
		const bool bHasWeightParameter =
			Capability.ParameterRanges.ContainsByPredicate(
				[](const FLLMNPCCapabilityParameterRange& Range)
				{
					return Range.ParameterId == TEXT("weight");
				}
			);
		if (
			Capability.BodyRegion == TEXT("hands") &&
			Capability.bRuntimeRecipeAllowed &&
			bHasWeightParameter
		)
		{
			OutChallenge.ExpectedCapabilityIds.Add(
				Capability.CapabilityId
			);
		}
	}
	OutChallenge.ExpectedCapabilityIds.Sort(FNameLexicalLess());
	if (OutChallenge.ExpectedCapabilityIds.IsEmpty())
	{
		OutError = TEXT("LLMNPC_CAPABILITY_SMOKE_CHALLENGE_EMPTY");
		return false;
	}

	TArray<FString> ExpectedIds;
	for (const FName CapabilityId : OutChallenge.ExpectedCapabilityIds)
	{
		ExpectedIds.Add(CapabilityId.ToString());
	}
	OutChallenge.ExpectedAssistantText =
		ChallengePrefix + FString::Join(ExpectedIds, TEXT(","));

	TSharedPtr<FJsonObject> CapabilityObject;
	const TSharedRef<TJsonReader<>> Reader =
		TJsonReaderFactory<>::Create(ModelViewJson);
	if (
		!FJsonSerializer::Deserialize(Reader, CapabilityObject) ||
		!CapabilityObject.IsValid()
	)
	{
		OutError = TEXT("LLMNPC_CAPABILITY_SMOKE_MODEL_VIEW_INVALID");
		return false;
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	OutChallenge.ChallengeId = TEXT("hands_runtime_weight_parameter");
	Root->SetStringField(
		TEXT("schema_version"),
		TEXT("llmnpc.capability_smoke_request.v1")
	);
	Root->SetStringField(
		TEXT("challenge_id"),
		OutChallenge.ChallengeId.ToString()
	);
	Root->SetStringField(
		TEXT("task"),
		TEXT("Inspect every entry in skeleton_capability.capabilities. Select the complete set satisfying all three JSON predicates: body_region == \"hands\"; runtime_recipe_allowed == true; and parameter_ranges is a JSON object with a property whose name is exactly \"weight\". Ignore descriptions and all other fields. Sort the selected capability_id values ascending. Do not infer, rename, or omit matching entries.")
	);
	Root->SetObjectField(
		TEXT("skeleton_capability"),
		CapabilityObject.ToSharedRef()
	);
	TSharedRef<FJsonObject> ResponseRules = MakeShared<FJsonObject>();
	ResponseRules->SetStringField(
		TEXT("assistant_text_format"),
		TEXT("capability_smoke|<comma-separated selected capability_id values>")
	);
	ResponseRules->SetStringField(TEXT("action_decision"), TEXT("none"));
	ResponseRules->SetStringField(
		TEXT("action_reason_tag"),
		TEXT("capability_smoke")
	);
	ResponseRules->SetStringField(TEXT("locomotion_decision"), TEXT("none"));
	Root->SetObjectField(TEXT("response_rules"), ResponseRules);

	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(
			&OutChallenge.ContextJson
		);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		OutError = TEXT("LLMNPC_CAPABILITY_SMOKE_PAYLOAD_SERIALIZE_FAILED");
		return false;
	}

	FString PrivateIdentifier;
	OutChallenge.bPrivateIdentifierScanPassed = !ContainsPrivateIdentifier(
		Profile,
		OutSnapshot,
		OutChallenge.ContextJson,
		PrivateIdentifier
	);
	if (!OutChallenge.bPrivateIdentifierScanPassed)
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_CAPABILITY_SMOKE_PRIVATE_IDENTIFIER:%s"),
			*PrivateIdentifier
		);
		return false;
	}

	OutChallenge.ModelViewHash = FString::Printf(
		TEXT("md5:%s"),
		*FMD5::HashAnsiString(*ModelViewJson)
	);
	OutChallenge.PayloadHash = FString::Printf(
		TEXT("md5:%s"),
		*FMD5::HashAnsiString(*OutChallenge.ContextJson)
	);
	ModelViewJson.Reset();
	return true;
}

bool FLLMNPCCapabilitySmokeRunner::ValidateResponse(
	const FString& ResponseJson,
	const FLLMNPCCapabilitySmokeChallenge& Challenge,
	FLLMNPCCapabilitySmokeValidation& OutValidation
)
{
	OutValidation = FLLMNPCCapabilitySmokeValidation();
	FLLMNPCModelTurnDecision Decision;
	FString SchemaError;
	OutValidation.bSchemaValid = FLLMNPCModelTurnParser::Parse(
		ResponseJson,
		Decision,
		SchemaError
	);
	if (!OutValidation.bSchemaValid)
	{
		OutValidation.ErrorCode = SchemaError.IsEmpty()
			? FName(TEXT("LLMNPC_CAPABILITY_SMOKE_SCHEMA_INVALID"))
			: FName(*SchemaError.Left(128));
		return false;
	}

	ParseObservedCapabilityIds(
		Decision.AssistantText,
		OutValidation.ObservedCapabilityIds
	);
	OutValidation.bNoActionContractValid =
		Decision.Action.Decision == TEXT("none") &&
		Decision.Action.ReasonTag == TEXT("capability_smoke") &&
		Decision.Locomotion.Decision == TEXT("none");
	if (!OutValidation.bNoActionContractValid)
	{
		OutValidation.ErrorCode =
			TEXT("LLMNPC_CAPABILITY_SMOKE_NO_ACTION_CONTRACT_INVALID");
		return false;
	}

	OutValidation.bExactCapabilitySelection =
		Decision.AssistantText == Challenge.ExpectedAssistantText &&
		OutValidation.ObservedCapabilityIds ==
			Challenge.ExpectedCapabilityIds;
	if (!OutValidation.bExactCapabilitySelection)
	{
		OutValidation.ErrorCode =
			TEXT("LLMNPC_CAPABILITY_SMOKE_SELECTION_MISMATCH");
		return false;
	}
	return true;
}

bool FLLMNPCCapabilitySmokeRunner::Start(
	bool bExitEditorWhenComplete,
	FString& OutError
)
{
	OutError.Reset();
	if (ActiveSmoke.IsValid())
	{
		OutError = TEXT("LLMNPC_CAPABILITY_SMOKE_ALREADY_RUNNING");
		return false;
	}

	const FLLMNPCOnlineTestConfigState Config =
		FLLMNPCOnlineTestConfigLoader::LoadProjectConfig();
	if (!Config.IsLoaded() || !Config.bCredentialPresent)
	{
		OutError = Config.ErrorCode.IsNone()
			? TEXT("LLMNPC_CAPABILITY_SMOKE_CONFIG_NOT_READY")
			: Config.ErrorCode.ToString();
		return false;
	}

	const ULLMNPCSkeletonProfile* Profile =
		LoadObject<ULLMNPCSkeletonProfile>(
			nullptr,
			TEXT("/LLMNPCActionLayer/LLMNPC/SkeletonProfiles/SP_UE5_Manny_v1.SP_UE5_Manny_v1")
		);
	if (!Profile)
	{
		OutError = TEXT("LLMNPC_CAPABILITY_SMOKE_PROFILE_MISSING");
		return false;
	}

	TSharedPtr<FLLMNPCActiveCapabilitySmoke> Run =
		MakeShared<FLLMNPCActiveCapabilitySmoke>();
	Run->bExitEditorWhenComplete = bExitEditorWhenComplete;
	Run->RequestId = FGuid::NewGuid();
	Run->Config = Config;
	if (!BuildChallenge(
		*Profile,
		Run->Snapshot,
		Run->Challenge,
		OutError
	))
	{
		return false;
	}

	Run->Provider = MakeShared<FLLMNPCDeepSeekProvider>();
	FLLMNPCModelTurnRequest Request;
	Request.RequestId = Run->RequestId;
	Request.SessionId = FGuid::NewGuid();
	Request.NPCId = TEXT("manny_capability_smoke");
	Request.UserMessage = TEXT("Forward N1 capability smoke");
	Request.ContextJson = Run->Challenge.ContextJson;

	ActiveSmoke = Run;
	const TWeakPtr<FLLMNPCActiveCapabilitySmoke> WeakRun = Run;
	Run->Provider->SendTurn(
		Request,
		[WeakRun](const FLLMNPCModelTurnResult& Result)
		{
			AsyncTask(
				ENamedThreads::GameThread,
				[WeakRun, Result]()
				{
					if (
						const TSharedPtr<FLLMNPCActiveCapabilitySmoke> Pinned =
							WeakRun.Pin()
					)
					{
						CompleteSmoke(Pinned, Result);
					}
				}
			);
		}
	);
	return true;
}

bool FLLMNPCCapabilitySmokeRunner::IsRunning()
{
	return ActiveSmoke.IsValid();
}

void FLLMNPCCapabilitySmokeRunner::Cancel()
{
	const TSharedPtr<FLLMNPCActiveCapabilitySmoke> Run = ActiveSmoke;
	ActiveSmoke.Reset();
	if (Run.IsValid() && Run->Provider.IsValid())
	{
		Run->Provider->CancelRequest(Run->RequestId);
	}
}
