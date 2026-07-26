#pragma once

#include "CoreMinimal.h"

class FLLMNPCContextModifierSmokeRunner
{
public:
	static bool Start(bool bExitEditorWhenComplete, FString& OutError);
	static bool IsRunning();
	static void Cancel();
};
