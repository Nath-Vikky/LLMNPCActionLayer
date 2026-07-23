#pragma once

#include "CoreMinimal.h"
#include "Providers/LLMNPCModelProvider.h"

DECLARE_DELEGATE_RetVal(TSharedPtr<ILLMNPCModelProvider>, FLLMNPCModelProviderFactory);

class LLMNPCACTIONLAYER_API FLLMNPCModelProviderRegistry
{
public:
	static FLLMNPCModelProviderRegistry& Get();

	bool RegisterProvider(
		FName ProviderId,
		const FLLMNPCModelProviderFactory& Factory,
		bool bReplaceExisting = false
	);
	bool UnregisterProvider(FName ProviderId);
	bool IsProviderRegistered(FName ProviderId) const;
	TSharedPtr<ILLMNPCModelProvider> CreateProvider(FName ProviderId) const;
	TArray<FName> GetRegisteredProviderIds() const;

private:
	mutable FCriticalSection RegistryMutex;
	TMap<FName, FLLMNPCModelProviderFactory> Factories;
};
