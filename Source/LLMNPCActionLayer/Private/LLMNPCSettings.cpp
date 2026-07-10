#include "LLMNPCSettings.h"

ULLMNPCSettings::ULLMNPCSettings()
{
	DefaultPostProcessAnimClass = TSoftClassPtr<UAnimInstance>(FSoftObjectPath(
		TEXT("/LLMNPCActionLayer/LLMNPC/Animation/ABP_LLMNPC_PostProcess.ABP_LLMNPC_PostProcess_C")
	));
	DefaultChatWidgetClass = TSoftClassPtr<UUserWidget>(FSoftObjectPath(
		TEXT("/LLMNPCActionLayer/LLMNPC/UI/WBP_LLMNPCChat.WBP_LLMNPCChat_C")
	));
	DeepSeekSystemPrompt =
		TEXT("Return one JSON object matching llmnpc.model_turn.v1. ")
		TEXT("Choose only a template_id from candidate_templates. ")
		TEXT("Never output bones, controls, transforms, animation tracks, asset paths, or code. ")
		TEXT("Use action decision none when no candidate is appropriate. ")
		TEXT("Use locomotion decision none. Output JSON only.");
	MotionTemplateScanPaths.Add(TEXT("/LLMNPCActionLayer/LLMNPC/MotionTemplates"));
	MotionTemplateScanPaths.Add(TEXT("/Game/LLMNPCActionLayer/MotionTemplates"));
	SkeletonProfileScanPaths.Add(TEXT("/LLMNPCActionLayer/LLMNPC/SkeletonProfiles"));
}
