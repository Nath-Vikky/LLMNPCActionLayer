// Copyright Epic Games, Inc. All Rights Reserved.

#include "LLMNPCActionLayer.h"
#include "Providers/LLMNPCBackendProxyProvider.h"
#include "Providers/LLMNPCDeepSeekProvider.h"
#include "Providers/LLMNPCMockProvider.h"
#include "Providers/LLMNPCModelProviderRegistry.h"

#define LOCTEXT_NAMESPACE "FLLMNPCActionLayerModule"

DEFINE_LOG_CATEGORY(LogLLMNPCActionLayer);

void FLLMNPCActionLayerModule::StartupModule()
{
	FLLMNPCModelProviderRegistry& Registry = FLLMNPCModelProviderRegistry::Get();
	Registry.RegisterProvider(
		TEXT("mock"),
		FLLMNPCModelProviderFactory::CreateLambda(
			[]() -> TSharedPtr<ILLMNPCModelProvider>
			{
				return MakeShared<FLLMNPCMockProvider>();
			}
		),
		true
	);
	Registry.RegisterProvider(
		TEXT("backend_proxy"),
		FLLMNPCModelProviderFactory::CreateLambda(
			[]() -> TSharedPtr<ILLMNPCModelProvider>
			{
				return MakeShared<FLLMNPCBackendProxyProvider>();
			}
		),
		true
	);
	Registry.RegisterProvider(
		TEXT("deepseek_direct_editor"),
		FLLMNPCModelProviderFactory::CreateLambda(
			[]() -> TSharedPtr<ILLMNPCModelProvider>
			{
				return MakeShared<FLLMNPCDeepSeekProvider>();
			}
		),
		true
	);
}

void FLLMNPCActionLayerModule::ShutdownModule()
{
	FLLMNPCModelProviderRegistry& Registry = FLLMNPCModelProviderRegistry::Get();
	Registry.UnregisterProvider(TEXT("mock"));
	Registry.UnregisterProvider(TEXT("backend_proxy"));
	Registry.UnregisterProvider(TEXT("deepseek_direct_editor"));
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FLLMNPCActionLayerModule, LLMNPCActionLayer)
