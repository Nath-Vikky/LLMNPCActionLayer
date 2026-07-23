// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogLLMNPCActionLayer, Log, All);
DECLARE_STATS_GROUP(
	TEXT("LLM NPC Action Layer"),
	STATGROUP_LLMNPCActionLayer,
	STATCAT_Advanced
);

class FLLMNPCActionLayerModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
