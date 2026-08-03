#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Dom/JsonObject.h"
#include "Evaluation/LLMNPCForwardN8Stability.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Online/LLMNPCOnlineReportSanitizer.h"
#include "Serialization/JsonSerializer.h"
#include "Templates/LLMNPCMotionTemplate.h"

namespace
{
constexpr uint32 ForwardN8AEditorTestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;

FLLMNPCForwardN8StabilityReport BuildPassingFormalReport()
{
	FLLMNPCForwardN8StabilityReport Report;
	Report.Status = TEXT("complete");
	Report.Config = LLMNPCForwardN8Stability::BuildFormalConfig();
	Report.ElapsedSeconds = Report.Config.MinimumDurationSeconds;
	Report.SubmittedRequestCount = Report.Config.TargetRequestCount;
	Report.AcceptedRequestCount = Report.Config.TargetRequestCount;
	Report.PlaybackObservedCount = Report.Config.TargetRequestCount;
	Report.CompletedRequestCount = Report.Config.TargetRequestCount;
	Report.StartUsedPhysicalMB = 1000.0;
	Report.PeakUsedPhysicalMB = 1010.0;
	Report.EndUsedPhysicalMB = 1008.0;
	for (int32 Index = 0; Index < Report.Config.TargetRequestCount; ++Index)
	{
		FLLMNPCForwardN8StabilityRequestRecord& Request =
			Report.Requests.AddDefaulted_GetRef();
		Request.Sequence = Index + 1;
		Request.TemplateId = FName(*FString::Printf(
			TEXT("gesture.test.%d"),
			Index % Report.Config.MinimumUniqueTemplateCount
		));
		Request.bAccepted = true;
		Request.bPlaybackObserved = true;
		Request.bPlaybackCompleted = true;
		Request.bPoseRecovered = true;
	}
	FLLMNPCMotionDebugState Debug;
	LLMNPCForwardN8Stability::ObserveFrame(Report, 1.0f / 60.0f, Debug, 1005.0);
	LLMNPCForwardN8Stability::ObserveFrame(Report, 1.0f / 30.0f, Debug, 1010.0);
	return Report;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN8AProfileContractTest,
	"LLMNPCActionLayer.ForwardN8A.Editor.ProfileContract",
	ForwardN8AEditorTestFlags
)

bool FLLMNPCForwardN8AProfileContractTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	const FLLMNPCForwardN8StabilityConfig Smoke =
		LLMNPCForwardN8Stability::BuildSmokeConfig();
	const FLLMNPCForwardN8StabilityConfig Formal =
		LLMNPCForwardN8Stability::BuildFormalConfig();
	FString Error;
	TestTrue(TEXT("The smoke profile is valid"), LLMNPCForwardN8Stability::ValidateConfig(Smoke, Error));
	TestEqual(TEXT("Smoke runs eight requests"), Smoke.TargetRequestCount, 8);
	TestEqual(TEXT("Smoke runs for at least twenty seconds"), Smoke.MinimumDurationSeconds, 20.0);
	TestTrue(TEXT("The formal profile is valid"), LLMNPCForwardN8Stability::ValidateConfig(Formal, Error));
	TestTrue(TEXT("The formal profile is marked as a gate"), Formal.bFormalGate);
	TestEqual(TEXT("The formal gate requires 500 requests"), Formal.TargetRequestCount, 500);
	TestEqual(TEXT("The formal gate requires 30 minutes"), Formal.MinimumDurationSeconds, 1800.0);

	FLLMNPCForwardN8StabilityConfig Undersized = Formal;
	Undersized.TargetRequestCount = 499;
	TestFalse(
		TEXT("A formal profile cannot reduce the request budget"),
		LLMNPCForwardN8Stability::ValidateConfig(Undersized, Error)
	);
	TestEqual(
		TEXT("The formal budget rejection is stable"),
		Error,
		FString(TEXT("LLMNPC_N8_STABILITY_FORMAL_BUDGET_TOO_SMALL"))
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN8AVerdictTest,
	"LLMNPCActionLayer.ForwardN8A.Editor.Verdict",
	ForwardN8AEditorTestFlags
)

bool FLLMNPCForwardN8AVerdictTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	FLLMNPCForwardN8StabilityReport Passing = BuildPassingFormalReport();
	LLMNPCForwardN8Stability::Finalize(Passing);
	TestTrue(TEXT("A complete formal report passes"), Passing.bMachinePassed);
	TestEqual(TEXT("The passing outcome is explicit"), Passing.Outcome, FString(TEXT("passed")));
	TestTrue(TEXT("Average frame time is calculated"), Passing.AverageFrameTimeMs > 0.0f);
	TestTrue(TEXT("P95 frame time is calculated"), Passing.P95FrameTimeMs > 0.0f);
	TestEqual(TEXT("Memory delta is calculated"), Passing.UsedPhysicalDeltaMB, 8.0);

	FLLMNPCForwardN8StabilityReport Short = BuildPassingFormalReport();
	Short.ElapsedSeconds = 1799.0;
	LLMNPCForwardN8Stability::Finalize(Short);
	TestFalse(TEXT("A short formal run fails"), Short.bMachinePassed);
	TestTrue(
		TEXT("The duration failure is attributable"),
		Short.Errors.Contains(TEXT("LLMNPC_N8_STABILITY_DURATION_INCOMPLETE"))
	);

	FLLMNPCForwardN8StabilityReport Rejected = BuildPassingFormalReport();
	Rejected.AcceptedRequestCount = 499;
	Rejected.RejectedRequestCount = 1;
	Rejected.PlaybackObservedCount = 499;
	Rejected.CompletedRequestCount = 499;
	LLMNPCForwardN8Stability::Finalize(Rejected);
	TestFalse(TEXT("A rejected valid request fails the gate"), Rejected.bMachinePassed);
	TestTrue(
		TEXT("The rejection is attributable"),
		Rejected.Errors.Contains(TEXT("LLMNPC_N8_STABILITY_SUBMISSION_REJECTED"))
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN8ARecoveryAndReportTest,
	"LLMNPCActionLayer.ForwardN8A.Editor.RecoveryAndReport",
	ForwardN8AEditorTestFlags
)

bool FLLMNPCForwardN8ARecoveryAndReportTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	FLLMProceduralPoseSnapshot AmbientOnly;
	AmbientOnly.GlobalAlpha = 1.0f;
	AmbientOnly.HeadPitch = 1.2f;
	AmbientOnly.ChestPitch = 0.5f;
	AmbientOnly.GazeAlpha = 0.2f;
	TestEqual(
		TEXT("Ambient head, chest, and gaze do not count as action residue"),
		LLMNPCForwardN8Stability::MeasureActionPoseResidual(AmbientOnly),
		0.0f
	);
	AmbientOnly.RightHandIKAlpha = 0.25f;
	TestEqual(
		TEXT("A retained action IK weight is detected"),
		LLMNPCForwardN8Stability::MeasureActionPoseResidual(AmbientOnly),
		0.25f
	);

	FLLMNPCForwardN8StabilityReport Report = BuildPassingFormalReport();
	Report.ActorName = TEXT("Bearer secret-value-that-must-not-survive");
	LLMNPCForwardN8Stability::Finalize(Report);
	LLMNPCForwardN8Stability::ApplyHumanReview(
		Report,
		TEXT("passed"),
		TEXT("editor_user")
	);
	FString Json;
	TestTrue(
		TEXT("The report is sanitized and serialized"),
		FLLMNPCOnlineReportSanitizer::SanitizeAndSerialize(
			LLMNPCForwardN8Stability::BuildJson(Report),
			Json
		)
	);
	TestFalse(TEXT("The credential-like value is removed"), Json.Contains(TEXT("secret-value")));
	TestTrue(TEXT("The sanitizer leaves an explicit marker"), Json.Contains(TEXT("[REDACTED]")));
	TestTrue(TEXT("The human review source is retained"), Json.Contains(TEXT("editor_user")));
	TestTrue(
		TEXT("The stability schema is retained"),
		Json.Contains(TEXT("llmnpc.forward_n8_stability.v1"))
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN8ALegacyWaveMirrorPolicyTest,
	"LLMNPCActionLayer.ForwardN8A.Editor.LegacyWaveMirrorPolicy",
	ForwardN8AEditorTestFlags
)

bool FLLMNPCForwardN8ALegacyWaveMirrorPolicyTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	const ULLMNPCMotionTemplate* Template = LoadObject<ULLMNPCMotionTemplate>(
		nullptr,
		TEXT("/LLMNPCActionLayer/LLMNPC/MotionTemplates/Manny/MT_Wave_Right_Manny_Procedural_v1.MT_Wave_Right_Manny_Procedural_v1")
	);
	TestNotNull(TEXT("The legacy procedural right-wave asset exists"), Template);
	if (!Template)
	{
		return false;
	}
	TestEqual(
		TEXT("The legacy wave remains the exact right-hand template"),
		Template->Metadata.TemplateId,
		FName(TEXT("gesture.wave.right.manny.procedural.v1"))
	);
	TestFalse(
		TEXT("The visually rejected procedural wave no longer advertises mirroring"),
		Template->ModifierPolicy.bAllowMirror
	);
	TestEqual(
		TEXT("The mirror-policy correction has a distinct semantic version"),
		Template->Metadata.SemanticVersion,
		FString(TEXT("1.1.1"))
	);
	TestEqual(
		TEXT("The corrected catalog revision is explicit"),
		Template->Metadata.CatalogRevision,
		3
	);
	TestFalse(
		TEXT("The comparison variant remains excluded from normal model selection"),
		Template->Metadata.bAllowRuntimeModelSelection
	);

	const TSharedPtr<IPlugin> Plugin =
		IPluginManager::Get().FindPlugin(TEXT("LLMNPCActionLayer"));
	TestTrue(TEXT("The plugin resource root is available"), Plugin.IsValid());
	if (!Plugin.IsValid())
	{
		return false;
	}
	const FString SourcePath = FPaths::Combine(
		Plugin->GetBaseDir(),
		TEXT("Resources/Templates/Manny/gesture.wave.right.manny.procedural.v1.json")
	);
	FString SourceJson;
	TestTrue(
		TEXT("The corrected wave source JSON is available"),
		FFileHelper::LoadFileToString(SourceJson, *SourcePath)
	);
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader =
		TJsonReaderFactory<>::Create(SourceJson);
	TestTrue(
		TEXT("The corrected wave source JSON parses"),
		FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid()
	);
	if (!Root.IsValid())
	{
		return false;
	}
	const TSharedPtr<FJsonObject> Policy =
		Root->GetObjectField(TEXT("modifier_policy"));
	TestFalse(
		TEXT("The source policy also disables mirroring"),
		Policy->GetBoolField(TEXT("allow_mirror"))
	);
	TestEqual(
		TEXT("The source and asset hashes agree"),
		Root->GetStringField(TEXT("catalog_content_hash")),
		Template->Metadata.CatalogContentHash
	);
	return true;
}

#endif
