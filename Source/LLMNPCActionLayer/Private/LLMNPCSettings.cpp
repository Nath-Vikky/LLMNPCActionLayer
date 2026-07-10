#include "LLMNPCSettings.h"

ULLMNPCSettings::ULLMNPCSettings()
{
	DefaultPostProcessAnimClass = TSoftClassPtr<UAnimInstance>(FSoftObjectPath(
		TEXT("/LLMNPCActionLayer/LLMNPC/Animation/ABP_LLMNPC_PostProcess.ABP_LLMNPC_PostProcess_C")
	));
	MotionTemplateScanPaths.Add(TEXT("/LLMNPCActionLayer/LLMNPC/MotionTemplates"));
	SkeletonProfileScanPaths.Add(TEXT("/LLMNPCActionLayer/LLMNPC/SkeletonProfiles"));
}
