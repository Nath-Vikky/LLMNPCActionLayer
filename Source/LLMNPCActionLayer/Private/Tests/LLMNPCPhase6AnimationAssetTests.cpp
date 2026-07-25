#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Animation/LLMNPCAnimationAssetPlayer.h"
#include "Animation/AnimationAsset.h"
#include "Templates/LLMNPCMotionTemplate.h"

namespace
{
constexpr uint32 Phase6AnimationTestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;

ULLMNPCMotionTemplate* MakeAnimationTemplate(UAnimationAsset* AnimationAsset)
{
	ULLMNPCMotionTemplate* Template = NewObject<ULLMNPCMotionTemplate>();
	Template->Kind = ELLMNPCTemplateKind::AnimationAsset;
	Template->Metadata.TemplateId = TEXT("gesture.wave.asset.manny.v1");
	Template->Metadata.PublicActionId = TEXT("gesture.wave.asset");
	Template->Metadata.CatalogSchemaVersion = LLMNPCCatalog::SchemaVersion;
	Template->Metadata.CatalogRevision = 1;
	Template->Metadata.VariantId = TEXT("animation_asset");
	Template->Metadata.VariantStyleTags = { TEXT("neutral"), TEXT("friendly") };
	Template->Metadata.VisualDescription =
		TEXT("A reviewed right-hand wave animation plays through a bounded Dynamic Montage.");
	Template->Metadata.IntentTags = { TEXT("greet"), TEXT("farewell") };
	Template->Metadata.BodyRegionTags = { TEXT("one_arm"), TEXT("hand"), TEXT("fingers") };
	Template->Metadata.SpatialRequirementTags = { TEXT("target_independent") };
	Template->Metadata.SemanticEffectTags = { TEXT("greet"), TEXT("farewell") };
	Template->Metadata.RequiredCapabilities = { TEXT("animation_asset.playback") };
	Template->Metadata.SkeletonProfileId = TEXT("ue5_manny.v1");
	Template->Metadata.RequiredChannels = { TEXT("full_body") };
	Template->Metadata.ReviewState = ELLMNPCTemplateReviewState::Published;
	Template->ModifierPolicy.AmplitudeRange = FVector2D(1.0f, 1.0f);
	Template->ModifierPolicy.SpeedRange = FVector2D(0.8f, 1.2f);
	Template->ModifierPolicy.DurationRange = FVector2D(0.8f, 1.2f);
	Template->ModifierPolicy.AllowedStyleTags = { TEXT("neutral"), TEXT("friendly") };
	Template->AnimationAsset = AnimationAsset;
	Template->AnimationPlayback.SlotName = TEXT("DefaultSlot");
	Template->AnimationPlayback.MaxDurationSeconds = 5.0f;
	Template->SourceProvenanceJson = TEXT("{\"source\":\"automation\"}");
	Template->ValidationReportJson = TEXT("{\"status\":\"pass\"}");
	Template->Metadata.CatalogContentHash =
		ULLMNPCMotionTemplate::BuildCatalogContentHash(*Template);
	return Template;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCPhase6AnimationTemplatePolicyTest,
	"LLMNPCActionLayer.Phase6.AnimationAsset.TemplatePolicy",
	Phase6AnimationTestFlags
)

bool FLLMNPCPhase6AnimationTemplatePolicyTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	UAnimationAsset* Waving = LoadObject<UAnimationAsset>(
		nullptr,
		TEXT("/Game/LLMNPC/Animation/Waving.Waving")
	);
	if (!Waving)
	{
		AddWarning(TEXT("The demo Waving sequence is unavailable; asset-resolution assertions were skipped."));
		return true;
	}
	TestNotNull(TEXT("The demo Waving sequence is available"), Waving);

	ULLMNPCMotionTemplate* Template = MakeAnimationTemplate(Waving);
	FString Error;
	TestTrue(TEXT("A reviewed animation alias has a valid template policy"), Template->ValidateTemplate(Error));

	Template->Metadata.RequiredChannels.Reset();
	Template->Metadata.CatalogContentHash =
		ULLMNPCMotionTemplate::BuildCatalogContentHash(*Template);
	TestFalse(TEXT("Animation aliases require explicit interaction channels"), Template->ValidateTemplate(Error));
	TestEqual(TEXT("Missing channels have a stable error"), Error, FString(TEXT("LLMNPC_TEMPLATE_ANIMATION_CHANNELS_MISSING")));

	Template->Metadata.RequiredChannels = { TEXT("full_body") };
	Template->Metadata.CatalogContentHash =
		ULLMNPCMotionTemplate::BuildCatalogContentHash(*Template);
	Template->AnimationPlayback.MaxDurationSeconds = 0.0f;
	TestFalse(TEXT("Animation aliases reject an invalid hard timeout"), Template->ValidateTemplate(Error));
	TestEqual(
		TEXT("Invalid playback policy has a stable error"),
		Error,
		FString(TEXT("LLMNPC_TEMPLATE_ANIMATION_PLAYBACK_POLICY_INVALID"))
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCPhase6AnimationRequestTest,
	"LLMNPCActionLayer.Phase6.AnimationAsset.ValidatedPlaybackRequest",
	Phase6AnimationTestFlags
)

bool FLLMNPCPhase6AnimationRequestTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	UAnimationAsset* Waving = LoadObject<UAnimationAsset>(
		nullptr,
		TEXT("/Game/LLMNPC/Animation/Waving.Waving")
	);
	if (!Waving)
	{
		AddWarning(TEXT("The demo Waving sequence is unavailable; playback-request assertions were skipped."));
		return true;
	}
	TestNotNull(TEXT("The demo Waving sequence is available"), Waving);

	ULLMNPCMotionTemplate* Template = MakeAnimationTemplate(Waving);
	FLLMNPCTemplateModifiers Modifiers;
	Modifiers.SpeedScale = 1.1f;
	Modifiers.DurationScale = 0.9f;
	UAnimationAsset* ResolvedAsset = nullptr;
	float PlayRate = 0.0f;
	FString Error;
	TestTrue(
		TEXT("A Published Waving alias resolves to an approved playback request"),
		ULLMNPCAnimationAssetPlayer::ValidatePlaybackRequest(
			*Template,
			Modifiers,
			ResolvedAsset,
			PlayRate,
			Error
		)
	);
	TestEqual(TEXT("The resolved asset stays behind the template alias"), ResolvedAsset, Waving);
	TestTrue(TEXT("Speed and duration produce a bounded play rate"), FMath::IsNearlyEqual(PlayRate, 1.1f / 0.9f));

	Modifiers.bMirror = true;
	TestFalse(
		TEXT("Runtime mirroring cannot be applied to an unreviewed animation asset"),
		ULLMNPCAnimationAssetPlayer::ValidatePlaybackRequest(
			*Template,
			Modifiers,
			ResolvedAsset,
			PlayRate,
			Error
		)
	);
	TestEqual(
		TEXT("Asset mirror rejection has a stable error"),
		Error,
		FString(TEXT("LLMNPC_ANIMATION_MIRROR_REQUIRES_APPROVED_VARIANT"))
	);
	return true;
}

#endif
