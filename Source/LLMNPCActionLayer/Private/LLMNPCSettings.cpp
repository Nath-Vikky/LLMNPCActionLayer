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
		TEXT("Follow the prompt_version and return one JSON object matching llmnpc.model_turn.v1. ")
		TEXT("Choose only a template_id from candidate_templates and only a target_ref listed for that candidate. ")
		TEXT("Use the supplied emotion, personality, relationship, scene, and action history context. ")
		TEXT("Never output bones, controls, transforms, animation tracks, asset paths, or code. ")
		TEXT("Use action decision none when no candidate is appropriate. ")
		TEXT("Use locomotion decision move_to only for an explicit movement request, and only with a target_ref from selection_context.scene_targets. ")
		TEXT("Never output a path or world-space destination. Use locomotion decision none otherwise. Output JSON only.");
	MotionTemplateScanPaths.Add(TEXT("/LLMNPCActionLayer/LLMNPC/MotionTemplates"));
	MotionTemplateScanPaths.Add(TEXT("/Game/LLMNPCActionLayer/MotionTemplates"));
	SkeletonProfileScanPaths.Add(TEXT("/LLMNPCActionLayer/LLMNPC/SkeletonProfiles"));
}
