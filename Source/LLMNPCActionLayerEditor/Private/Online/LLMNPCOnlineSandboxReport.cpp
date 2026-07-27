#include "Online/LLMNPCOnlineSandboxReport.h"

#include "Authoring/LLMNPCTemplateAuthoringSubsystem.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Online/LLMNPCAuthoringModelClient.h"
#include "Online/LLMNPCOnlineReportSanitizer.h"
#include "Sandbox/LLMNPCAuthoringSandbox.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
FString SafeRequestId(const FGuid& RequestId)
{
	return RequestId.IsValid()
		? RequestId.ToString(EGuidFormats::DigitsWithHyphensLower)
		: TEXT("invalid-request");
}

TArray<TSharedPtr<FJsonValue>> StringArray(
	const TArray<FString>& Values
)
{
	TArray<TSharedPtr<FJsonValue>> JsonValues;
	JsonValues.Reserve(Values.Num());
	for (const FString& Value : Values)
	{
		JsonValues.Add(MakeShared<FJsonValueString>(Value));
	}
	return JsonValues;
}

bool ParseObject(
	const FString& Json,
	TSharedPtr<FJsonObject>& OutObject
)
{
	const TSharedRef<TJsonReader<>> Reader =
		TJsonReaderFactory<>::Create(Json);
	return
		FJsonSerializer::Deserialize(Reader, OutObject) &&
		OutObject.IsValid();
}
}

void FLLMNPCOnlineSandboxReport::ApplyAuthoringResult(
	const FLLMNPCAuthoringJsonResult& Result,
	FLLMNPCOnlineSandboxReportRecord& InOutRecord
)
{
	InOutRecord.RequestId = Result.RequestId;
	InOutRecord.ProviderId = Result.ProviderId;
	InOutRecord.ProviderModelId = Result.ProviderModelId;
	InOutRecord.TotalLatencySeconds = Result.TotalLatencySeconds;
	InOutRecord.AttemptCount = Result.AttemptCount;
	InOutRecord.PromptTokens = Result.PromptTokens;
	InOutRecord.CompletionTokens = Result.CompletionTokens;
	InOutRecord.TotalTokens = Result.TotalTokens;
	InOutRecord.UpdatedAtUtc = FDateTime::UtcNow();
	if (!Result.bSuccess)
	{
		InOutRecord.Outcome =
			Result.ErrorCode == TEXT("LLMNPC_AUTHORING_REQUEST_TIMEOUT")
				? FName(TEXT("timeout"))
				: Result.ErrorCode == TEXT(
					"LLMNPC_AUTHORING_REQUEST_CANCELLED"
				)
					? FName(TEXT("cancelled"))
					: FName(TEXT("provider_failed"));
		InOutRecord.ErrorCode = Result.ErrorCode;
	}
}

void FLLMNPCOnlineSandboxReport::ApplyPreflightResult(
	const FLLMNPCAuthoringSandboxPreflightResult& Result,
	FLLMNPCOnlineSandboxReportRecord& InOutRecord
)
{
	InOutRecord.bPreflightPassed = Result.bPassed;
	InOutRecord.bTransientPlanSubmitted = false;
	InOutRecord.bDraftRecordSaved = false;
	InOutRecord.DraftRecordPath.Reset();
	InOutRecord.HumanVisualDecision = TEXT("not_recorded");
	InOutRecord.HumanVisualNotes.Reset();
	InOutRecord.ErrorCode = Result.ErrorCode;
	InOutRecord.RecipeHash = Result.CompiledMetadata.RecipeHash;
	InOutRecord.CompiledRecipeHash =
		Result.CompiledMetadata.CompiledRecipeHash;
	InOutRecord.CapabilityHash =
		Result.CompiledMetadata.CapabilityHash.IsEmpty()
			? InOutRecord.CapabilityHash
			: Result.CompiledMetadata.CapabilityHash;
	if (!Result.CompiledMetadata.PrimitiveRegistryVersion.IsEmpty())
	{
		InOutRecord.RegistryVersion =
			Result.CompiledMetadata.PrimitiveRegistryVersion;
	}
	InOutRecord.KinematicReportHash =
		Result.KinematicReport.ReportHash;
	InOutRecord.PreflightIssueCodes.Reset();
	for (
		const FLLMNPCKinematicValidationIssue& Issue :
		Result.KinematicReport.Issues
	)
	{
		InOutRecord.PreflightIssueCodes.AddUnique(Issue.Code);
	}
	InOutRecord.Outcome = Result.bPassed
		? FName(TEXT("preflight_passed"))
		: FName(TEXT("preflight_rejected"));
	InOutRecord.UpdatedAtUtc = FDateTime::UtcNow();
}

bool FLLMNPCOnlineSandboxReport::Save(
	const FLLMNPCOnlineSandboxReportRecord& Record,
	FString& OutPath,
	FString& OutError
)
{
	OutPath.Reset();
	OutError.Reset();
	if (!Record.RequestId.IsValid())
	{
		OutError = TEXT("LLMNPC_SANDBOX_REPORT_REQUEST_ID_INVALID");
		return false;
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(
		TEXT("schema_version"),
		LLMNPCAuthoringSandbox::ReportSchemaVersion
	);
	Root->SetStringField(
		TEXT("request_id"),
		SafeRequestId(Record.RequestId)
	);
	Root->SetStringField(
		TEXT("started_at_utc"),
		Record.StartedAtUtc.GetTicks() > 0
			? Record.StartedAtUtc.ToIso8601()
			: FString()
	);
	Root->SetStringField(
		TEXT("updated_at_utc"),
		Record.UpdatedAtUtc.GetTicks() > 0
			? Record.UpdatedAtUtc.ToIso8601()
			: FDateTime::UtcNow().ToIso8601()
	);
	Root->SetStringField(TEXT("outcome"), Record.Outcome.ToString());
	Root->SetStringField(
		TEXT("error_code"),
		Record.ErrorCode.ToString()
	);

	TSharedRef<FJsonObject> Provider = MakeShared<FJsonObject>();
	Provider->SetStringField(
		TEXT("provider_id"),
		Record.ProviderId.ToString()
	);
	Provider->SetStringField(
		TEXT("model_id"),
		Record.ProviderModelId
	);
	Provider->SetStringField(
		TEXT("endpoint_origin"),
		Record.EndpointOrigin
	);
	Provider->SetStringField(
		TEXT("non_secret_config_hash"),
		Record.NonSecretConfigHash
	);
	Provider->SetNumberField(
		TEXT("attempt_count"),
		Record.AttemptCount
	);
	Provider->SetNumberField(
		TEXT("latency_seconds"),
		Record.TotalLatencySeconds
	);
	Provider->SetNumberField(
		TEXT("prompt_tokens"),
		Record.PromptTokens
	);
	Provider->SetNumberField(
		TEXT("completion_tokens"),
		Record.CompletionTokens
	);
	Provider->SetNumberField(
		TEXT("total_tokens"),
		Record.TotalTokens
	);
	Root->SetObjectField(TEXT("provider"), Provider);

	TSharedRef<FJsonObject> Identity = MakeShared<FJsonObject>();
	Identity->SetStringField(
		TEXT("prompt_version"),
		Record.PromptVersion
	);
	Identity->SetStringField(TEXT("prompt_hash"), Record.PromptHash);
	Identity->SetStringField(
		TEXT("capability_hash"),
		Record.CapabilityHash
	);
	Identity->SetStringField(
		TEXT("primitive_registry_version"),
		Record.RegistryVersion
	);
	Identity->SetStringField(
		TEXT("recipe_hash"),
		Record.RecipeHash
	);
	Identity->SetStringField(
		TEXT("compiled_recipe_hash"),
		Record.CompiledRecipeHash
	);
	Root->SetObjectField(TEXT("identity"), Identity);

	TSharedRef<FJsonObject> Preflight = MakeShared<FJsonObject>();
	Preflight->SetBoolField(
		TEXT("passed"),
		Record.bPreflightPassed
	);
	Preflight->SetStringField(
		TEXT("kinematic_report_hash"),
		Record.KinematicReportHash
	);
	Preflight->SetArrayField(
		TEXT("issue_codes"),
		StringArray(Record.PreflightIssueCodes)
	);
	Preflight->SetBoolField(
		TEXT("transient_plan_submitted"),
		Record.bTransientPlanSubmitted
	);
	Root->SetObjectField(TEXT("preflight"), Preflight);

	TSharedRef<FJsonObject> Draft = MakeShared<FJsonObject>();
	Draft->SetBoolField(
		TEXT("saved"),
		Record.bDraftRecordSaved
	);
	Draft->SetStringField(
		TEXT("record_path"),
		Record.DraftRecordPath
	);
	Root->SetObjectField(TEXT("draft_record"), Draft);

	TSharedRef<FJsonObject> HumanReview = MakeShared<FJsonObject>();
	HumanReview->SetStringField(
		TEXT("decision"),
		Record.HumanVisualDecision.ToString()
	);
	HumanReview->SetStringField(
		TEXT("notes"),
		Record.HumanVisualNotes
	);
	Root->SetObjectField(TEXT("human_visual_review"), HumanReview);

	FString Json;
	if (!FLLMNPCOnlineReportSanitizer::SanitizeAndSerialize(
		Root,
		Json
	))
	{
		OutError = TEXT("LLMNPC_SANDBOX_REPORT_SERIALIZE_FAILED");
		return false;
	}

	const FString Directory =
		ULLMNPCTemplateAuthoringSubsystem::GetReportDirectory();
	if (!IFileManager::Get().MakeDirectory(*Directory, true))
	{
		OutError = TEXT("LLMNPC_SANDBOX_REPORT_DIRECTORY_FAILED");
		return false;
	}
	OutPath = FPaths::Combine(
		Directory,
		FString::Printf(
			TEXT("online_sandbox_%s.json"),
			*SafeRequestId(Record.RequestId)
		)
	);
	if (!FFileHelper::SaveStringToFile(
		Json,
		*OutPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM
	))
	{
		OutError = TEXT("LLMNPC_SANDBOX_REPORT_WRITE_FAILED");
		OutPath.Reset();
		return false;
	}
	return true;
}

bool FLLMNPCOnlineSandboxReport::SaveDraftRecord(
	const FString& CanonicalRecipeJson,
	const FLLMNPCOnlineSandboxReportRecord& Report,
	FString& OutPath,
	FString& OutError
)
{
	OutPath.Reset();
	OutError.Reset();
	if (
		!Report.RequestId.IsValid() ||
		!Report.bPreflightPassed ||
		Report.RecipeHash.IsEmpty()
	)
	{
		OutError = TEXT("LLMNPC_SANDBOX_DRAFT_PREFLIGHT_REQUIRED");
		return false;
	}
	TSharedPtr<FJsonObject> Recipe;
	if (!ParseObject(CanonicalRecipeJson, Recipe))
	{
		OutError = TEXT("LLMNPC_SANDBOX_DRAFT_RECIPE_INVALID");
		return false;
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(
		TEXT("schema_version"),
		LLMNPCAuthoringSandbox::DraftRecordSchemaVersion
	);
	Root->SetStringField(
		TEXT("request_id"),
		SafeRequestId(Report.RequestId)
	);
	Root->SetStringField(
		TEXT("created_at_utc"),
		FDateTime::UtcNow().ToIso8601()
	);
	Root->SetStringField(TEXT("review_state"), TEXT("Draft"));
	Root->SetStringField(
		TEXT("capability_hash"),
		Report.CapabilityHash
	);
	Root->SetStringField(
		TEXT("primitive_registry_version"),
		Report.RegistryVersion
	);
	Root->SetStringField(
		TEXT("recipe_hash"),
		Report.RecipeHash
	);
	Root->SetStringField(
		TEXT("compiled_recipe_hash"),
		Report.CompiledRecipeHash
	);
	Root->SetStringField(
		TEXT("kinematic_report_hash"),
		Report.KinematicReportHash
	);
	Root->SetObjectField(TEXT("motion_recipe"), Recipe.ToSharedRef());

	FString Json;
	if (!FLLMNPCOnlineReportSanitizer::SanitizeAndSerialize(
		Root,
		Json
	))
	{
		OutError = TEXT("LLMNPC_SANDBOX_DRAFT_SERIALIZE_FAILED");
		return false;
	}

	const FString Directory = FPaths::Combine(
		ULLMNPCTemplateAuthoringSubsystem::GetDraftDirectory(),
		TEXT("Sandbox")
	);
	if (!IFileManager::Get().MakeDirectory(*Directory, true))
	{
		OutError = TEXT("LLMNPC_SANDBOX_DRAFT_DIRECTORY_FAILED");
		return false;
	}
	OutPath = FPaths::Combine(
		Directory,
		FString::Printf(
			TEXT("sandbox_%s_%s.json"),
			*SafeRequestId(Report.RequestId),
			*Report.RecipeHash.Replace(TEXT(":"), TEXT("_"))
		)
	);
	if (!FFileHelper::SaveStringToFile(
		Json,
		*OutPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM
	))
	{
		OutError = TEXT("LLMNPC_SANDBOX_DRAFT_WRITE_FAILED");
		OutPath.Reset();
		return false;
	}
	return true;
}
