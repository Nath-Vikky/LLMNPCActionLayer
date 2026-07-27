#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Authoring/LLMNPCMotionRecipeAuthoringPrompt.h"
#include "Authoring/LLMNPCTemplateAuthoringSubsystem.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Online/LLMNPCAuthoringModelClient.h"
#include "Online/LLMNPCOnlineSandboxReport.h"
#include "Sandbox/LLMNPCAuthoringSandbox.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"

namespace
{
constexpr uint32 ForwardN6EditorTestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;

const FString ForwardN6EditorRecipe = TEXT(R"JSON(
{
  "schema_version": "llmnpc.motion_recipe.v1",
  "recipe_id": "sandbox.report.automation",
  "intent": "express_uncertainty",
  "duration": 1.8,
  "interruptible": true,
  "primitives": [
    {
      "primitive_id": "shoulder.shrug",
      "side": "none",
      "start": 0.0,
      "end": 1.8,
      "parameters": {
        "amplitude": 0.72,
        "speed": 1.0,
        "torso_participation": 0.35,
        "arm_openness": 0.58,
        "palm_openness": 0.76,
        "asymmetry": 0.04
      }
    }
  ]
}
)JSON");

ULLMNPCSkeletonProfile* LoadForwardN6EditorProfile()
{
	return LoadObject<ULLMNPCSkeletonProfile>(
		nullptr,
		TEXT("/LLMNPCActionLayer/LLMNPC/SkeletonProfiles/SP_UE5_Manny_v1.SP_UE5_Manny_v1")
	);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN6SandboxReportTest,
	"LLMNPCActionLayer.ForwardN6.Editor.SandboxReport",
	ForwardN6EditorTestFlags
)

bool FLLMNPCForwardN6SandboxReportTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	ULLMNPCSkeletonProfile* Profile =
		LoadForwardN6EditorProfile();
	TestNotNull(TEXT("Manny Profile loads"), Profile);
	if (!Profile)
	{
		return false;
	}

	FLLMNPCAuthoringSandboxRequest Request;
	Request.RecipeJson = ForwardN6EditorRecipe;
	Request.SkeletonProfile = Profile;
	const FLLMNPCAuthoringSandboxPreflightResult Preflight =
		FLLMNPCAuthoringSandbox::RunFullPreflight(Request);
	TestTrue(
		*FString::Printf(
			TEXT("The report fixture passes Preflight: %s"),
			*Preflight.ErrorMessage
		),
		Preflight.bPassed
	);
	if (!Preflight.bPassed)
	{
		return false;
	}

	FLLMNPCOnlineSandboxReportRecord Report;
	Report.RequestId = FGuid::NewGuid();
	Report.ProviderId = TEXT("deepseek_direct_editor_authoring");
	Report.ProviderModelId = TEXT("automation-model");
	Report.EndpointOrigin = TEXT("https://example.invalid");
	Report.NonSecretConfigHash = TEXT("md5:config");
	Report.PromptVersion =
		LLMNPCMotionRecipeAuthoring::PromptVersion;
	Report.PromptHash = TEXT("md5:prompt");
	Report.CapabilityHash =
		Preflight.CompiledMetadata.CapabilityHash;
	Report.RegistryVersion =
		Preflight.CompiledMetadata.PrimitiveRegistryVersion;
	Report.StartedAtUtc = FDateTime::UtcNow();
	Report.UpdatedAtUtc = Report.StartedAtUtc;
	FLLMNPCOnlineSandboxReport::ApplyPreflightResult(
		Preflight,
		Report
	);
	Report.bTransientPlanSubmitted = true;
	Report.HumanVisualDecision = TEXT("pass");
	Report.HumanVisualNotes =
		TEXT("Bearer should-never-be-written");

	FString DraftPath;
	FString Error;
	TestTrue(
		*FString::Printf(
			TEXT("A successful Preflight can write only a Saved Draft Record: %s"),
			*Error
		),
		FLLMNPCOnlineSandboxReport::SaveDraftRecord(
			Preflight.CanonicalRecipeJson,
			Report,
			DraftPath,
			Error
		)
	);
	Report.bDraftRecordSaved = true;
	Report.DraftRecordPath = DraftPath;
	TestTrue(
		TEXT("The Draft Record is under Project Saved"),
		FPaths::IsUnderDirectory(
			DraftPath,
			FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir())
		)
	);
	TestFalse(
		TEXT("The Sandbox Draft Record is not a Content package"),
		DraftPath.EndsWith(TEXT(".uasset"))
	);

	FString ReportPath;
	TestTrue(
		*FString::Printf(
			TEXT("The Online Sandbox Report writes: %s"),
			*Error
		),
		FLLMNPCOnlineSandboxReport::Save(
			Report,
			ReportPath,
			Error
		)
	);
	FString ReportJson;
	TestTrue(
		TEXT("The written report can be read"),
		FFileHelper::LoadFileToString(ReportJson, *ReportPath)
	);
	TestTrue(
		TEXT("The report associates Recipe and Capability hashes"),
		ReportJson.Contains(Report.RecipeHash) &&
			ReportJson.Contains(Report.CapabilityHash)
	);
	TestTrue(
		TEXT("The report associates the human visual decision"),
		ReportJson.Contains(TEXT("\"decision\": \"pass\""))
	);
	TestFalse(
		TEXT("Credential-like values are redacted"),
		ReportJson.Contains(TEXT("should-never-be-written"))
	);
	TestTrue(
		TEXT("The redaction is explicit"),
		ReportJson.Contains(TEXT("[REDACTED]"))
	);

	IFileManager::Get().Delete(*DraftPath);
	IFileManager::Get().Delete(*ReportPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN6FailureAccountingTest,
	"LLMNPCActionLayer.ForwardN6.Editor.FailureAccounting",
	ForwardN6EditorTestFlags
)

bool FLLMNPCForwardN6FailureAccountingTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	FLLMNPCOnlineSandboxReportRecord Report;
	Report.RequestId = FGuid::NewGuid();

	FLLMNPCAuthoringJsonResult Timeout;
	Timeout.RequestId = Report.RequestId;
	Timeout.ErrorCode = TEXT("LLMNPC_AUTHORING_REQUEST_TIMEOUT");
	Timeout.AttemptCount = 2;
	Timeout.TotalLatencySeconds = 4.0f;
	FLLMNPCOnlineSandboxReport::ApplyAuthoringResult(
		Timeout,
		Report
	);
	TestEqual(
		TEXT("Timeout has a distinct report outcome"),
		Report.Outcome,
		FName(TEXT("timeout"))
	);

	FLLMNPCAuthoringJsonResult Cancelled;
	Cancelled.RequestId = Report.RequestId;
	Cancelled.ErrorCode =
		TEXT("LLMNPC_AUTHORING_REQUEST_CANCELLED");
	FLLMNPCOnlineSandboxReport::ApplyAuthoringResult(
		Cancelled,
		Report
	);
	TestEqual(
		TEXT("Cancellation has a distinct report outcome"),
		Report.Outcome,
		FName(TEXT("cancelled"))
	);

	const FLLMNPCAuthoringSandboxPreflightResult BadJson =
		FLLMNPCAuthoringSandbox::RunFullPreflight(
			{
				TEXT("{bad-json"),
				LoadForwardN6EditorProfile()
			}
		);
	TestFalse(TEXT("Bad JSON is rejected"), BadJson.bPassed);
	TestTrue(
		TEXT("Bad JSON never produces a transient plan"),
		BadJson.TransientPlan.Clip.Tracks.IsEmpty()
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN6SchemaArtifactTest,
	"LLMNPCActionLayer.ForwardN6.Editor.SchemaArtifact",
	ForwardN6EditorTestFlags
)

bool FLLMNPCForwardN6SchemaArtifactTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	const TSharedPtr<IPlugin> Plugin =
		IPluginManager::Get().FindPlugin(TEXT("LLMNPCActionLayer"));
	TestTrue(TEXT("LLMNPCActionLayer plugin resolves"), Plugin.IsValid());
	if (!Plugin.IsValid())
	{
		return false;
	}
	const FString SchemaPath = FPaths::Combine(
		Plugin->GetBaseDir(),
		TEXT("Resources/Schemas/llmnpc_online_sandbox_report_v1.schema.json")
	);
	FString SchemaJson;
	TestTrue(
		TEXT("The Online Sandbox Report Schema is packaged"),
		FFileHelper::LoadFileToString(SchemaJson, *SchemaPath)
	);
	TestTrue(
		TEXT("The Schema pins the report contract"),
		SchemaJson.Contains(
			LLMNPCAuthoringSandbox::ReportSchemaVersion
		)
	);
	const FString DraftSchemaPath = FPaths::Combine(
		Plugin->GetBaseDir(),
		TEXT("Resources/Schemas/llmnpc_sandbox_draft_record_v1.schema.json")
	);
	FString DraftSchemaJson;
	TestTrue(
		TEXT("The Sandbox Draft Record Schema is packaged"),
		FFileHelper::LoadFileToString(DraftSchemaJson, *DraftSchemaPath)
	);
	TestTrue(
		TEXT("The Draft Schema pins the Saved-only record contract"),
		DraftSchemaJson.Contains(
			LLMNPCAuthoringSandbox::DraftRecordSchemaVersion
		)
	);
	for (const FString& Forbidden : {
		FString(TEXT("OPENAI_API_KEY")),
		FString(TEXT("\"api_key\"")),
		FString(TEXT("\"authorization\"")),
		FString(TEXT("\"secret\""))
	})
	{
		TestFalse(
			*FString::Printf(
				TEXT("The report schema omits secret field '%s'"),
				*Forbidden
			),
			SchemaJson.Contains(
				Forbidden,
				ESearchCase::IgnoreCase
			)
		);
		TestFalse(
			*FString::Printf(
				TEXT("The draft schema omits secret field '%s'"),
				*Forbidden
			),
			DraftSchemaJson.Contains(
				Forbidden,
				ESearchCase::IgnoreCase
			)
		);
	}
	return true;
}

#endif
