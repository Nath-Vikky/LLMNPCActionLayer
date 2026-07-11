#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "LLMNPCSettings.h"
#include "Providers/LLMNPCProviderCredentials.h"

namespace
{
constexpr uint32 Phase6ProviderTestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCPhase6SessionCredentialTest,
	"LLMNPCActionLayer.Phase6.Provider.SessionCredentialLifecycle",
	Phase6ProviderTestFlags
)

bool FLLMNPCPhase6SessionCredentialTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	const FName TestProviderId(TEXT("llmnpc_automation_credential"));
	FLLMNPCProviderCredentials::ClearSessionSecret(TestProviderId);

	FLLMNPCProviderCredentials::SetSessionSecret(TestProviderId, TEXT("  test-secret  "));
	TestTrue(
		TEXT("A non-empty editor-session secret is retained in memory"),
		FLLMNPCProviderCredentials::HasSessionSecret(TestProviderId)
	);

	FLLMNPCProviderCredentials::SetSessionSecret(TestProviderId, TEXT("   "));
	TestFalse(
		TEXT("An empty editor-session secret removes the stored value"),
		FLLMNPCProviderCredentials::HasSessionSecret(TestProviderId)
	);

	FLLMNPCProviderCredentials::SetSessionSecret(TestProviderId, TEXT("test-secret"));
	FLLMNPCProviderCredentials::ClearSessionSecret(TestProviderId);
	TestFalse(
		TEXT("A session secret can be explicitly cleared"),
		FLLMNPCProviderCredentials::HasSessionSecret(TestProviderId)
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCPhase6CredentialConfigBoundaryTest,
	"LLMNPCActionLayer.Phase6.Provider.NoSerializedApiKey",
	Phase6ProviderTestFlags
)

bool FLLMNPCPhase6CredentialConfigBoundaryTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	const UClass* SettingsClass = ULLMNPCSettings::StaticClass();
	TestNull(
		TEXT("The project settings do not expose a serializable DeepSeek API key"),
		SettingsClass->FindPropertyByName(TEXT("DeepSeekApiKey"))
	);
	TestNull(
		TEXT("The project settings do not expose a generic serializable API key"),
		SettingsClass->FindPropertyByName(TEXT("ApiKey"))
	);
	TestNotNull(
		TEXT("Only an environment-variable name is stored in project settings"),
		SettingsClass->FindPropertyByName(GET_MEMBER_NAME_CHECKED(ULLMNPCSettings, ApiKeyEnvironmentVariable))
	);
	return true;
}

#endif
