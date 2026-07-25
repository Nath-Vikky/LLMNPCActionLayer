#pragma once

#include "CoreMinimal.h"

class FLLMNPCCatalogSelectionSmokeRunner
{
public:
	static bool Start(bool bExitEditorWhenComplete, FString& OutError);
	static bool IsRunning();
	static void Cancel();
};
