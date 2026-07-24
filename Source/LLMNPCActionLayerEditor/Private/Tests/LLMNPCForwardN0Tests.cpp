#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Dialogue/LLMNPCModelTurnContract.h"
#include "Dialogue/LLMNPCModelTurnValidator.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Online/LLMNPCOnlineReportSanitizer.h"
#include "Online/LLMNPCOnlineTestConfigLoader.h"
#include "Providers/LLMNPCDeepSeekProvider.h"
#include "Providers/LLMNPCProviderSession.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "Templates/LLMNPCTemplateCompiler.h"
#include "Templates/LLMNPCTemplateLibrarySubsystem.h"

namespace
{
constexpr uint32 ForwardN0TestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;

bool ParseExpectingError(
	const FString& ConfigText,
	FName ExpectedError,
	FAutomationTestBase& Test
)
{
	FLLMNPCParsedOnlineTestConfig Parsed;
	FName ErrorCode = NAME_None;
	const bool bParsed = FLLMNPCOnlineTestConfigLoader::ParseConfigText(
		ConfigText,
		Parsed,
		ErrorCode
	);
	Parsed.ClearSecret();
	Test.TestFalse(TEXT("The invalid local config is rejected"), bParsed);
	Test.TestEqual(TEXT("The parser returns the stable rejection code"), ErrorCode, ExpectedError);
	return !bParsed && ErrorCode == ExpectedError;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN0ModelTurnContractTest,
	"LLMNPCActionLayer.ForwardN0.OnlineConfig.ModelTurnContract",
	ForwardN0TestFlags
)

bool FLLMNPCForwardN0ModelTurnContractTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);

	const FString Response =
		FLLMNPCModelTurnContract::BuildCanonicalNoActionResponse(
			TEXT("Connection verified."),
			TEXT("connection_test")
		);
	FLLMNPCModelTurnDecision Decision;
	FString ParseError;
	TestTrue(
		TEXT("The canonical connection response satisfies the strict model-turn parser"),
		FLLMNPCModelTurnParser::Parse(Response, Decision, ParseError)
	);
	TestTrue(TEXT("A canonical response has no parser error"), ParseError.IsEmpty());
	TestEqual(TEXT("The canonical action is none"), Decision.Action.Decision, FName(TEXT("none")));
	TestEqual(
		TEXT("The canonical locomotion decision is none"),
		Decision.Locomotion.Decision,
		FName(TEXT("none"))
	);
	TestTrue(
		TEXT("The provider instruction says that none decisions still require every field"),
		FLLMNPCModelTurnContract::GetResponseInstruction().Contains(
			TEXT("including fields belonging to a decision of none")
		)
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN0OnlineConfigParserTest,
	"LLMNPCActionLayer.ForwardN0.OnlineConfig.StrictParser",
	ForwardN0TestFlags
)

bool FLLMNPCForwardN0OnlineConfigParserTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);

	const FString ConfigText =
		TEXT("# Local online test profile\n")
		TEXT("LLM_MODEL = \"test-model-v1\"\n")
		TEXT("OPENAI_BASE_URL = https://provider.example/v1/\n")
		TEXT("OPENAI_API_KEY = 'test-session-secret'\n");

	FLLMNPCParsedOnlineTestConfig Parsed;
	FName ErrorCode = NAME_None;
	TestTrue(
		TEXT("A complete local online test profile parses"),
		FLLMNPCOnlineTestConfigLoader::ParseConfigText(ConfigText, Parsed, ErrorCode)
	);
	TestTrue(TEXT("A valid profile has no rejection code"), ErrorCode.IsNone());
	TestEqual(TEXT("The model is normalized"), Parsed.Model, FString(TEXT("test-model-v1")));
	TestEqual(
		TEXT("The base URL drops trailing separators"),
		Parsed.BaseUrl,
		FString(TEXT("https://provider.example/v1"))
	);
	TestEqual(
		TEXT("Only the endpoint origin is exposed for status and reports"),
		Parsed.EndpointOrigin,
		FString(TEXT("https://provider.example"))
	);
	TestFalse(TEXT("The non-secret config hash is present"), Parsed.NonSecretConfigHash.IsEmpty());
	Parsed.ClearSecret();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN0OnlineConfigRejectionTest,
	"LLMNPCActionLayer.ForwardN0.OnlineConfig.StrictRejections",
	ForwardN0TestFlags
)

bool FLLMNPCForwardN0OnlineConfigRejectionTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);

	ParseExpectingError(
		TEXT("LLM_MODEL=test-model\nOPENAI_BASE_URL=https://provider.example\n"),
		TEXT("LLMNPC_ONLINE_CONFIG_KEY_MISSING"),
		*this
	);
	ParseExpectingError(
		TEXT("LLM_MODEL=a\nLLM_MODEL=b\nOPENAI_BASE_URL=https://provider.example\nOPENAI_API_KEY=test\n"),
		TEXT("LLMNPC_ONLINE_CONFIG_KEY_DUPLICATE"),
		*this
	);
	ParseExpectingError(
		TEXT("LLM_MODEL=a\nOPENAI_BASE_URL=https://provider.example\nOPENAI_API_KEY=test\nEXTRA=value\n"),
		TEXT("LLMNPC_ONLINE_CONFIG_KEY_UNKNOWN"),
		*this
	);
	ParseExpectingError(
		TEXT("LLM_MODEL=a\nOPENAI_BASE_URL:https://provider.example\nOPENAI_API_KEY=test\n"),
		TEXT("LLMNPC_ONLINE_CONFIG_LINE_INVALID"),
		*this
	);
	ParseExpectingError(
		TEXT("LLM_MODEL=a\nOPENAI_BASE_URL=https://provider.example\nOPENAI_API_KEY=\"\"\n"),
		TEXT("LLMNPC_ONLINE_CONFIG_VALUE_EMPTY"),
		*this
	);
	ParseExpectingError(
		TEXT("LLM_MODEL=a\nOPENAI_BASE_URL=https://provider.example?token=forbidden\nOPENAI_API_KEY=test\n"),
		TEXT("LLMNPC_ONLINE_CONFIG_URL_INVALID"),
		*this
	);
	ParseExpectingError(
		TEXT("LLM_MODEL=\"unterminated\nOPENAI_BASE_URL=https://provider.example\nOPENAI_API_KEY=test\n"),
		TEXT("LLMNPC_ONLINE_CONFIG_QUOTE_INVALID"),
		*this
	);
	ParseExpectingError(
		TEXT("LLM_MODEL=model with spaces\nOPENAI_BASE_URL=https://provider.example\nOPENAI_API_KEY=test\n"),
		TEXT("LLMNPC_ONLINE_CONFIG_MODEL_INVALID"),
		*this
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN0NonSecretFingerprintTest,
	"LLMNPCActionLayer.ForwardN0.OnlineConfig.NonSecretFingerprint",
	ForwardN0TestFlags
)

bool FLLMNPCForwardN0NonSecretFingerprintTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);

	const FString Prefix =
		TEXT("LLM_MODEL=test-model\n")
		TEXT("OPENAI_BASE_URL=https://provider.example/v1\n")
		TEXT("OPENAI_API_KEY=");
	FLLMNPCParsedOnlineTestConfig First;
	FLLMNPCParsedOnlineTestConfig Second;
	FName ErrorCode = NAME_None;
	TestTrue(
		TEXT("The first secret variant parses"),
		FLLMNPCOnlineTestConfigLoader::ParseConfigText(Prefix + TEXT("first-test-secret\n"), First, ErrorCode)
	);
	TestTrue(
		TEXT("The second secret variant parses"),
		FLLMNPCOnlineTestConfigLoader::ParseConfigText(Prefix + TEXT("second-test-secret\n"), Second, ErrorCode)
	);
	TestEqual(
		TEXT("Changing only the API key does not change the non-secret config hash"),
		First.NonSecretConfigHash,
		Second.NonSecretConfigHash
	);
	TestFalse(
		TEXT("The first fixture secret is absent from the config hash"),
		First.NonSecretConfigHash.Contains(TEXT("first-test-secret"))
	);
	TestFalse(
		TEXT("The second fixture secret is absent from the config hash"),
		Second.NonSecretConfigHash.Contains(TEXT("second-test-secret"))
	);
	First.ClearSecret();
	Second.ClearSecret();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN0SessionOverridesTest,
	"LLMNPCActionLayer.ForwardN0.OnlineConfig.SessionOverrideLifecycle",
	ForwardN0TestFlags
)

bool FLLMNPCForwardN0SessionOverridesTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);

	const FName ProviderId(TEXT("forward_n0_test_provider"));
	FLLMNPCProviderSession::ClearSessionOverrides(ProviderId);

	FLLMNPCProviderSessionOverrides Overrides;
	Overrides.BaseUrl = TEXT("  https://provider.example/v1/  ");
	Overrides.Model = TEXT("  test-model  ");
	Overrides.NonSecretConfigHash = TEXT("  test-hash  ");
	FLLMNPCProviderSession::SetSessionOverrides(ProviderId, Overrides);

	FLLMNPCProviderSessionOverrides Resolved;
	TestTrue(
		TEXT("Editor session provider overrides can be resolved"),
		FLLMNPCProviderSession::GetSessionOverrides(ProviderId, Resolved)
	);
	TestEqual(
		TEXT("Session base URLs are trimmed"),
		Resolved.BaseUrl,
		FString(TEXT("https://provider.example/v1/"))
	);
	TestEqual(TEXT("Session model IDs are trimmed"), Resolved.Model, FString(TEXT("test-model")));
	TestEqual(
		TEXT("Session config hashes are trimmed"),
		Resolved.NonSecretConfigHash,
		FString(TEXT("test-hash"))
	);

	FLLMNPCProviderSession::ClearSessionOverrides(ProviderId);
	TestFalse(
		TEXT("Session provider overrides can be explicitly cleared"),
		FLLMNPCProviderSession::GetSessionOverrides(ProviderId, Resolved)
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN0ResolvedModifierTraceTest,
	"LLMNPCActionLayer.ForwardN0.MotionTest.ResolvedModifierTrace",
	ForwardN0TestFlags
)

bool FLLMNPCForwardN0ResolvedModifierTraceTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);

	const ULLMNPCSkeletonProfile* Profile = LoadObject<ULLMNPCSkeletonProfile>(
		nullptr,
		TEXT("/LLMNPCActionLayer/LLMNPC/SkeletonProfiles/SP_UE5_Manny_v1.SP_UE5_Manny_v1")
	);
	const ULLMNPCMotionTemplate* NodTemplate = LoadObject<ULLMNPCMotionTemplate>(
		nullptr,
		TEXT("/LLMNPCActionLayer/LLMNPC/MotionTemplates/Manny/MT_Nod_Manny_v1.MT_Nod_Manny_v1")
	);
	TestNotNull(TEXT("The N0 trace test has the Manny profile"), Profile);
	TestNotNull(TEXT("The N0 trace test has the Published nod template"), NodTemplate);
	if (!Profile || !NodTemplate)
	{
		return false;
	}

	FLLMNPCTemplateModifiers Requested;
	Requested.Amplitude = 100.0f;
	Requested.SpeedScale = 0.001f;
	Requested.DurationScale = 100.0f;
	Requested.Style = TEXT("neutral");
	Requested.RandomSeed = 0;

	FLLMMotionPlan Plan;
	FString CompileError;
	FLLMNPCTemplateResolvedModifiers Resolved;
	TestTrue(
		TEXT("The compiler exposes resolved modifiers for the test console"),
		FLLMNPCTemplateCompiler::Compile(
			*NodTemplate,
			Requested,
			*Profile,
			Plan,
			CompileError,
			&Resolved
		)
	);
	if (!CompileError.IsEmpty())
	{
		AddError(CompileError);
	}

	TestTrue(
		TEXT("Resolved amplitude reports the template-policy clamp"),
		FMath::IsNearlyEqual(
			Resolved.Amplitude,
			static_cast<float>(NodTemplate->ModifierPolicy.AmplitudeRange.Y)
		)
	);
	TestTrue(
		TEXT("Resolved speed reports the template-policy clamp"),
		FMath::IsNearlyEqual(
			Resolved.SpeedScale,
			static_cast<float>(NodTemplate->ModifierPolicy.SpeedRange.X)
		)
	);
	TestTrue(
		TEXT("Resolved duration reports the template-policy clamp"),
		FMath::IsNearlyEqual(
			Resolved.DurationScale,
			static_cast<float>(NodTemplate->ModifierPolicy.DurationRange.Y)
		)
	);
	TestEqual(TEXT("Resolved style is observable"), Resolved.Style, FName(TEXT("neutral")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN0MannyTemplateEnumerationTest,
	"LLMNPCActionLayer.ForwardN0.MotionTest.MannyPublishedTemplateEnumeration",
	ForwardN0TestFlags
)

bool FLLMNPCForwardN0MannyTemplateEnumerationTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);

	UGameInstance* TestGameInstance = NewObject<UGameInstance>();
	ULLMNPCTemplateLibrarySubsystem* Library =
		NewObject<ULLMNPCTemplateLibrarySubsystem>(TestGameInstance);
	Library->RefreshLibrary();

	const FName MannyProfileId(TEXT("ue5_manny.v1"));
	TArray<FName> TemplateIds;
	Library->GetPublishedTemplateIdsForProfile(MannyProfileId, TemplateIds);
	TestFalse(TEXT("The N0 console can enumerate Manny Published templates"), TemplateIds.IsEmpty());
	TestTrue(
		TEXT("The faithful Manny wave is available to the N0 console"),
		TemplateIds.Contains(TEXT("gesture.wave.right.manny.fk.v1"))
	);

	for (int32 Index = 0; Index < TemplateIds.Num(); ++Index)
	{
		const ULLMNPCMotionTemplate* MotionTemplate =
			Library->FindPublishedTemplate(TemplateIds[Index]);
		TestNotNull(
			*FString::Printf(TEXT("Enumerated template %s resolves"), *TemplateIds[Index].ToString()),
			MotionTemplate
		);
		if (MotionTemplate)
		{
			TestTrue(
				*FString::Printf(
					TEXT("Enumerated template %s supports Manny"),
					*TemplateIds[Index].ToString()
				),
				MotionTemplate->SupportsSkeletonProfile(MannyProfileId)
			);
		}
		if (Index > 0)
		{
			TestTrue(
				TEXT("Manny template IDs are returned in stable lexical order"),
				FNameLexicalLess()(TemplateIds[Index - 1], TemplateIds[Index])
			);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN0ProviderMetadataTest,
	"LLMNPCActionLayer.ForwardN0.OnlineConfig.ProviderMetadata",
	ForwardN0TestFlags
)

bool FLLMNPCForwardN0ProviderMetadataTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);

	const FString Envelope =
		TEXT("{\"model\":\"expected-model-v1\",")
		TEXT("\"usage\":{\"prompt_tokens\":17,\"completion_tokens\":9,\"total_tokens\":26},")
		TEXT("\"choices\":[{\"finish_reason\":\"stop\",\"message\":{\"content\":")
		TEXT("\"{\\\"schema_version\\\":\\\"llmnpc.model_turn.v1\\\",")
		TEXT("\\\"assistant_text\\\":\\\"ok\\\",")
		TEXT("\\\"action\\\":{\\\"decision\\\":\\\"none\\\"},")
		TEXT("\\\"locomotion\\\":{\\\"decision\\\":\\\"none\\\"}}\"}}]}");
	FString DecisionJson;
	FString Error;
	FString ModelId;
	int32 PromptTokens = INDEX_NONE;
	int32 CompletionTokens = INDEX_NONE;
	int32 TotalTokens = INDEX_NONE;
	TestTrue(
		TEXT("The direct provider extracts a decision and non-secret response metadata"),
		FLLMNPCDeepSeekProvider::ExtractDecisionJson(
			Envelope,
			DecisionJson,
			Error,
			&ModelId,
			&PromptTokens,
			&CompletionTokens,
			&TotalTokens
		)
	);
	TestEqual(TEXT("The returned provider model is observable"), ModelId, FString(TEXT("expected-model-v1")));
	TestEqual(TEXT("Prompt token usage is observable"), PromptTokens, 17);
	TestEqual(TEXT("Completion token usage is observable"), CompletionTokens, 9);
	TestEqual(TEXT("Total token usage is observable"), TotalTokens, 26);
	TestFalse(TEXT("The extracted decision is present"), DecisionJson.IsEmpty());
	TestTrue(TEXT("A valid provider envelope has no extraction error"), Error.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN0ReportSanitizerTest,
	"LLMNPCActionLayer.ForwardN0.OnlineConfig.ReportSanitizer",
	ForwardN0TestFlags
)

bool FLLMNPCForwardN0ReportSanitizerTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("model"), TEXT("safe-model"));
	Root->SetNumberField(TEXT("total_tokens"), 42);
	Root->SetStringField(TEXT("authorization"), TEXT("Bearer fixture-secret"));
	Root->SetStringField(TEXT("api_key"), TEXT("fixture-api-key"));
	TSharedRef<FJsonObject> Nested = MakeShared<FJsonObject>();
	Nested->SetStringField(TEXT("raw_response"), TEXT("fixture-raw-response"));
	Nested->SetStringField(TEXT("status"), TEXT("passed"));
	Root->SetObjectField(TEXT("nested"), Nested);

	FString SanitizedJson;
	TestTrue(
		TEXT("The online report sanitizer serializes an allowlisted report"),
		FLLMNPCOnlineReportSanitizer::SanitizeAndSerialize(Root, SanitizedJson)
	);
	TestTrue(TEXT("Safe model metadata is retained"), SanitizedJson.Contains(TEXT("safe-model")));
	TestTrue(TEXT("Token usage fields are retained"), SanitizedJson.Contains(TEXT("total_tokens")));
	TestTrue(TEXT("Safe nested status is retained"), SanitizedJson.Contains(TEXT("passed")));
	TestFalse(TEXT("Authorization fields are removed"), SanitizedJson.Contains(TEXT("authorization")));
	TestFalse(TEXT("API key fields are removed"), SanitizedJson.Contains(TEXT("api_key")));
	TestFalse(TEXT("Raw response fields are removed"), SanitizedJson.Contains(TEXT("raw_response")));
	TestFalse(TEXT("Authorization values are removed"), SanitizedJson.Contains(TEXT("fixture-secret")));
	TestFalse(TEXT("API key values are removed"), SanitizedJson.Contains(TEXT("fixture-api-key")));
	TestFalse(TEXT("Raw response values are removed"), SanitizedJson.Contains(TEXT("fixture-raw-response")));
	return true;
}

#endif
