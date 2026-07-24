#include "Online/LLMNPCOnlineTestConfigLoader.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Misc/SecureHash.h"
#include "Providers/LLMNPCProviderCredentials.h"
#include "Providers/LLMNPCProviderSession.h"

namespace
{
FCriticalSection ConfigStateMutex;
FLLMNPCOnlineTestConfigState ConfigState;

const FString ModelKey(TEXT("LLM_MODEL"));
const FString BaseUrlKey(TEXT("OPENAI_BASE_URL"));
const FString ApiKeyConfigKey(TEXT("OPENAI_API_KEY"));

void SetState(const FLLMNPCOnlineTestConfigState& NewState)
{
	FScopeLock Lock(&ConfigStateMutex);
	ConfigState = NewState;
}

void ClearProviderSession()
{
	const FName ProviderId = FLLMNPCProviderCredentials::DeepSeekProviderId();
	FLLMNPCProviderCredentials::ClearSessionSecret(ProviderId);
	FLLMNPCProviderSession::ClearSessionOverrides(ProviderId);
}

bool IsKnownKey(const FString& Key)
{
	return Key == ModelKey || Key == BaseUrlKey || Key == ApiKeyConfigKey;
}

bool UnquoteValue(FString& Value)
{
	if (Value.IsEmpty())
	{
		return true;
	}

	const TCHAR First = Value[0];
	const bool bStartsQuoted = First == TCHAR('"') || First == TCHAR('\'');
	const bool bEndsQuoted =
		Value.Len() >= 2 &&
		(Value[Value.Len() - 1] == TCHAR('"') || Value[Value.Len() - 1] == TCHAR('\''));
	if (!bStartsQuoted && !bEndsQuoted)
	{
		return true;
	}
	if (!bStartsQuoted || !bEndsQuoted || Value[Value.Len() - 1] != First)
	{
		return false;
	}

	Value = Value.Mid(1, Value.Len() - 2).TrimStartAndEnd();
	return true;
}

bool ContainsWhitespace(const FString& Value)
{
	for (const TCHAR Character : Value)
	{
		if (FChar::IsWhitespace(Character))
		{
			return true;
		}
	}
	return false;
}

bool NormalizeBaseUrl(
	const FString& Input,
	FString& OutBaseUrl,
	FString& OutOrigin
)
{
	FString Clean = Input.TrimStartAndEnd();
	FString Scheme;
	FString Remainder;
	if (Clean.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase))
	{
		Scheme = TEXT("https://");
		Remainder = Clean.RightChop(8);
	}
	else if (Clean.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase))
	{
		Scheme = TEXT("http://");
		Remainder = Clean.RightChop(7);
	}
	else
	{
		return false;
	}

	if (
		Remainder.IsEmpty() ||
		ContainsWhitespace(Remainder) ||
		Remainder.Contains(TEXT("@")) ||
		Remainder.Contains(TEXT("?")) ||
		Remainder.Contains(TEXT("#"))
	)
	{
		return false;
	}

	int32 FirstSlash = INDEX_NONE;
	Remainder.FindChar(TCHAR('/'), FirstSlash);
	const FString Authority =
		FirstSlash == INDEX_NONE ? Remainder : Remainder.Left(FirstSlash);
	if (Authority.IsEmpty())
	{
		return false;
	}

	while (Remainder.EndsWith(TEXT("/")))
	{
		Remainder.LeftChopInline(1);
	}
	if (Remainder.IsEmpty())
	{
		return false;
	}

	OutBaseUrl = Scheme + Remainder;
	OutOrigin = Scheme + Authority;
	return true;
}

FLLMNPCOnlineTestConfigState FailureState(
	ELLMNPCOnlineTestConfigStatus Status,
	FName ErrorCode
)
{
	FLLMNPCOnlineTestConfigState State;
	State.Status = Status;
	State.ErrorCode = ErrorCode;
	return State;
}
}

FLLMNPCOnlineTestConfigState FLLMNPCOnlineTestConfigLoader::LoadProjectConfig()
{
	ClearProviderSession();

	const FString ConfigPath = GetProjectConfigPath();
	if (!IFileManager::Get().FileExists(*ConfigPath))
	{
		const FLLMNPCOnlineTestConfigState State = FailureState(
			ELLMNPCOnlineTestConfigStatus::Missing,
			TEXT("LLMNPC_ONLINE_CONFIG_FILE_MISSING")
		);
		SetState(State);
		return State;
	}

	FString ConfigText;
	if (!FFileHelper::LoadFileToString(ConfigText, *ConfigPath))
	{
		const FLLMNPCOnlineTestConfigState State = FailureState(
			ELLMNPCOnlineTestConfigStatus::Invalid,
			TEXT("LLMNPC_ONLINE_CONFIG_FILE_READ_FAILED")
		);
		SetState(State);
		return State;
	}

	FLLMNPCParsedOnlineTestConfig Parsed;
	FName ErrorCode = NAME_None;
	if (!ParseConfigText(ConfigText, Parsed, ErrorCode))
	{
		ConfigText.Reset();
		Parsed.ClearSecret();
		const FLLMNPCOnlineTestConfigState State = FailureState(
			ELLMNPCOnlineTestConfigStatus::Invalid,
			ErrorCode
		);
		SetState(State);
		return State;
	}
	ConfigText.Reset();

	const FName ProviderId = FLLMNPCProviderCredentials::DeepSeekProviderId();
	FLLMNPCProviderSessionOverrides Overrides;
	Overrides.BaseUrl = Parsed.BaseUrl;
	Overrides.Model = Parsed.Model;
	Overrides.NonSecretConfigHash = Parsed.NonSecretConfigHash;
	FLLMNPCProviderSession::SetSessionOverrides(ProviderId, Overrides);
	FLLMNPCProviderCredentials::SetSessionSecret(ProviderId, Parsed.ApiKey);

	FLLMNPCOnlineTestConfigState State;
	State.Status = ELLMNPCOnlineTestConfigStatus::Loaded;
	State.Model = Parsed.Model;
	State.EndpointOrigin = Parsed.EndpointOrigin;
	State.NonSecretConfigHash = Parsed.NonSecretConfigHash;
	State.bCredentialPresent = FLLMNPCProviderCredentials::HasSessionSecret(ProviderId);
	Parsed.ClearSecret();
	SetState(State);
	return State;
}

void FLLMNPCOnlineTestConfigLoader::ClearSession()
{
	ClearProviderSession();
	SetState(FLLMNPCOnlineTestConfigState());
}

FLLMNPCOnlineTestConfigState FLLMNPCOnlineTestConfigLoader::GetState()
{
	FLLMNPCOnlineTestConfigState State;
	{
		FScopeLock Lock(&ConfigStateMutex);
		State = ConfigState;
	}
	if (State.IsLoaded())
	{
		State.bCredentialPresent = FLLMNPCProviderCredentials::HasSessionSecret(
			FLLMNPCProviderCredentials::DeepSeekProviderId()
		);
	}
	return State;
}

void FLLMNPCOnlineTestConfigLoader::RecordConnectionTest(
	bool bPassed,
	FName ProviderId,
	const FString& Model,
	const FString& ConfigHash,
	FName ErrorCode,
	int32 HttpStatus,
	float LatencySeconds
)
{
	FScopeLock Lock(&ConfigStateMutex);
	ConfigState.bConnectionTestPassed =
		bPassed &&
		ConfigState.IsLoaded() &&
		ConfigHash == ConfigState.NonSecretConfigHash;
	ConfigState.ConnectionProviderId = ProviderId;
	ConfigState.ConnectionModel = Model.TrimStartAndEnd();
	ConfigState.ConnectionConfigHash = ConfigHash;
	ConfigState.ConnectionTestedAtUtc = FDateTime::UtcNow();
	ConfigState.ConnectionErrorCode = ErrorCode;
	ConfigState.ConnectionHttpStatus = HttpStatus;
	ConfigState.ConnectionLatencySeconds = LatencySeconds;
}

FString FLLMNPCOnlineTestConfigLoader::GetProjectConfigPath()
{
	return FPaths::Combine(FPaths::ProjectDir(), TEXT("env.txt"));
}

bool FLLMNPCOnlineTestConfigLoader::ParseConfigText(
	const FString& ConfigText,
	FLLMNPCParsedOnlineTestConfig& OutConfig,
	FName& OutErrorCode
)
{
	OutConfig = FLLMNPCParsedOnlineTestConfig();
	OutErrorCode = NAME_None;

	TMap<FString, FString> Values;
	TArray<FString> Lines;
	ConfigText.ParseIntoArrayLines(Lines, false);
	for (FString Line : Lines)
	{
		Line = Line.TrimStartAndEnd();
		if (
			Line.IsEmpty() ||
			Line.StartsWith(TEXT("#")) ||
			Line.StartsWith(TEXT(";"))
		)
		{
			continue;
		}

		int32 EqualsIndex = INDEX_NONE;
		if (!Line.FindChar(TCHAR('='), EqualsIndex) || EqualsIndex <= 0)
		{
			OutErrorCode = TEXT("LLMNPC_ONLINE_CONFIG_LINE_INVALID");
			return false;
		}

		const FString Key = Line.Left(EqualsIndex).TrimStartAndEnd();
		FString Value = Line.Mid(EqualsIndex + 1).TrimStartAndEnd();
		if (!IsKnownKey(Key))
		{
			OutErrorCode = TEXT("LLMNPC_ONLINE_CONFIG_KEY_UNKNOWN");
			return false;
		}
		if (Values.Contains(Key))
		{
			OutErrorCode = TEXT("LLMNPC_ONLINE_CONFIG_KEY_DUPLICATE");
			return false;
		}
		if (!UnquoteValue(Value))
		{
			OutErrorCode = TEXT("LLMNPC_ONLINE_CONFIG_QUOTE_INVALID");
			return false;
		}
		if (Value.IsEmpty())
		{
			OutErrorCode = TEXT("LLMNPC_ONLINE_CONFIG_VALUE_EMPTY");
			return false;
		}
		Values.Add(Key, MoveTemp(Value));
	}

	const FString* Model = Values.Find(ModelKey);
	const FString* BaseUrl = Values.Find(BaseUrlKey);
	const FString* Secret = Values.Find(ApiKeyConfigKey);
	if (!Model || !BaseUrl || !Secret)
	{
		OutErrorCode = TEXT("LLMNPC_ONLINE_CONFIG_KEY_MISSING");
		return false;
	}
	if (ContainsWhitespace(*Model))
	{
		OutErrorCode = TEXT("LLMNPC_ONLINE_CONFIG_MODEL_INVALID");
		return false;
	}

	OutConfig.Model = *Model;
	OutConfig.ApiKey = *Secret;
	if (!NormalizeBaseUrl(*BaseUrl, OutConfig.BaseUrl, OutConfig.EndpointOrigin))
	{
		OutConfig.ClearSecret();
		OutErrorCode = TEXT("LLMNPC_ONLINE_CONFIG_URL_INVALID");
		return false;
	}

	const FString CanonicalNonSecretConfig = FString::Printf(
		TEXT("llmnpc.online_test_config.v1\nmodel=%s\nbase_url=%s"),
		*OutConfig.Model,
		*OutConfig.BaseUrl
	);
	OutConfig.NonSecretConfigHash = FMD5::HashAnsiString(*CanonicalNonSecretConfig);
	return true;
}
