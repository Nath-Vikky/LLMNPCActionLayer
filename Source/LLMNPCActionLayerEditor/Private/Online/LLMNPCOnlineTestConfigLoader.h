#pragma once

#include "CoreMinimal.h"

enum class ELLMNPCOnlineTestConfigStatus : uint8
{
	NotLoaded,
	Loaded,
	Missing,
	Invalid
};

struct FLLMNPCOnlineTestConfigState
{
	ELLMNPCOnlineTestConfigStatus Status = ELLMNPCOnlineTestConfigStatus::NotLoaded;
	FString Model;
	FString EndpointOrigin;
	FString NonSecretConfigHash;
	FName ErrorCode = NAME_None;
	bool bCredentialPresent = false;
	bool bConnectionTestPassed = false;
	FName ConnectionProviderId = NAME_None;
	FString ConnectionModel;
	FString ConnectionConfigHash;
	FDateTime ConnectionTestedAtUtc;
	FName ConnectionErrorCode = NAME_None;
	int32 ConnectionHttpStatus = 0;
	float ConnectionLatencySeconds = -1.0f;

	bool IsLoaded() const
	{
		return Status == ELLMNPCOnlineTestConfigStatus::Loaded;
	}

	bool HasPassingConnectionForCurrentConfig() const
	{
		return IsLoaded() &&
			bConnectionTestPassed &&
			ConnectionConfigHash == NonSecretConfigHash;
	}
};

struct FLLMNPCParsedOnlineTestConfig
{
	FString Model;
	FString BaseUrl;
	FString ApiKey;
	FString EndpointOrigin;
	FString NonSecretConfigHash;

	void ClearSecret()
	{
		ApiKey.Reset();
	}
};

class FLLMNPCOnlineTestConfigLoader
{
public:
	static FLLMNPCOnlineTestConfigState LoadProjectConfig();
	static void ClearSession();
	static FLLMNPCOnlineTestConfigState GetState();
	static void RecordConnectionTest(
		bool bPassed,
		FName ProviderId,
		const FString& Model,
		const FString& ConfigHash,
		FName ErrorCode,
		int32 HttpStatus,
		float LatencySeconds
	);
	static FString GetProjectConfigPath();

	static bool ParseConfigText(
		const FString& ConfigText,
		FLLMNPCParsedOnlineTestConfig& OutConfig,
		FName& OutErrorCode
	);
};
