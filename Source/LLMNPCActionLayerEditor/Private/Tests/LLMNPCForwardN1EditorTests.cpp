#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Dialogue/LLMNPCModelTurnContract.h"
#include "Dom/JsonObject.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Online/LLMNPCCapabilitySmokeRunner.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"

namespace
{
constexpr uint32 ForwardN1EditorTestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN1CapabilitySmokeContractTest,
	"LLMNPCActionLayer.ForwardN1.Online.CapabilitySmokeContract",
	ForwardN1EditorTestFlags
)

bool FLLMNPCForwardN1CapabilitySmokeContractTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	const ULLMNPCSkeletonProfile* Profile =
		LoadObject<ULLMNPCSkeletonProfile>(
			nullptr,
			TEXT("/LLMNPCActionLayer/LLMNPC/SkeletonProfiles/SP_UE5_Manny_v1.SP_UE5_Manny_v1")
		);
	TestNotNull(TEXT("The shipped Manny Profile loads"), Profile);
	if (!Profile)
	{
		return false;
	}

	FLLMNPCSkeletonCapabilitySnapshot Snapshot;
	FLLMNPCCapabilitySmokeChallenge Challenge;
	FString Error;
	TestTrue(
		TEXT("A model-safe Capability Smoke challenge builds"),
		FLLMNPCCapabilitySmokeRunner::BuildChallenge(
			*Profile,
			Snapshot,
			Challenge,
			Error
		)
	);
	if (!Error.IsEmpty())
	{
		AddError(Error);
	}
	TestTrue(
		TEXT("The recursive restricted-field scan passes"),
		Challenge.bRestrictedFieldScanPassed
	);
	TestTrue(
		TEXT("The private identifier scan passes"),
		Challenge.bPrivateIdentifierScanPassed
	);
	TestEqual(
		TEXT("The challenge derives all matching hand capabilities"),
		Challenge.ExpectedCapabilityIds.Num(),
		5
	);
	TestTrue(
		TEXT("The challenge includes the Relaxed hand pose"),
		Challenge.ExpectedCapabilityIds.Contains(TEXT("hand.pose.relaxed"))
	);
	TestTrue(
		TEXT("The challenge includes the calibrated Thumbs Up hand pose"),
		Challenge.ExpectedCapabilityIds.Contains(
			TEXT("hand.pose.thumbs_up")
		)
	);
	TestFalse(
		TEXT("The challenge excludes hand abilities without a Weight parameter"),
		Challenge.ExpectedCapabilityIds.Contains(TEXT("hand.wave_arc"))
	);
	for (const TCHAR* PrivateValue : {
		TEXT("clavicle_r"),
		TEXT("upperarm_r"),
		TEXT("right_fingers.open"),
		TEXT("compact_pose_index"),
		TEXT("component_space")
	})
	{
		TestFalse(
			FString::Printf(
				TEXT("The online payload omits private value '%s'"),
				PrivateValue
			),
			Challenge.ContextJson.Contains(
				PrivateValue,
				ESearchCase::IgnoreCase
			)
		);
	}

	const FString ValidResponse =
		FLLMNPCModelTurnContract::BuildCanonicalNoActionResponse(
			Challenge.ExpectedAssistantText,
			TEXT("capability_smoke")
		);
	FLLMNPCCapabilitySmokeValidation Validation;
	TestTrue(
		TEXT("The exact no-action Capability response passes"),
		FLLMNPCCapabilitySmokeRunner::ValidateResponse(
			ValidResponse,
			Challenge,
			Validation
		)
	);
	TestTrue(TEXT("The response schema is valid"), Validation.bSchemaValid);
	TestTrue(
		TEXT("The response preserves the no-action contract"),
		Validation.bNoActionContractValid
	);
	TestTrue(
		TEXT("The response selected the exact derived capability set"),
		Validation.bExactCapabilitySelection
	);

	const FString InvalidResponse =
		FLLMNPCModelTurnContract::BuildCanonicalNoActionResponse(
			TEXT("capability_smoke|hand.pose.open"),
			TEXT("capability_smoke")
		);
	TestFalse(
		TEXT("An incomplete model selection is rejected"),
		FLLMNPCCapabilitySmokeRunner::ValidateResponse(
			InvalidResponse,
			Challenge,
			Validation
		)
	);
	TestEqual(
		TEXT("The mismatch has a stable error code"),
		Validation.ErrorCode,
		FName(TEXT("LLMNPC_CAPABILITY_SMOKE_SELECTION_MISMATCH"))
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN1ApprovedBaselineContractTest,
	"LLMNPCActionLayer.ForwardN1.Baseline.ApprovedMannyContract",
	ForwardN1EditorTestFlags
)

bool FLLMNPCForwardN1ApprovedBaselineContractTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	const ULLMNPCSkeletonProfile* Profile =
		LoadObject<ULLMNPCSkeletonProfile>(
			nullptr,
			TEXT("/LLMNPCActionLayer/LLMNPC/SkeletonProfiles/SP_UE5_Manny_v1.SP_UE5_Manny_v1")
		);
	const TSharedPtr<IPlugin> Plugin =
		IPluginManager::Get().FindPlugin(TEXT("LLMNPCActionLayer"));
	TestNotNull(TEXT("The approved Manny Profile loads"), Profile);
	TestTrue(TEXT("The plugin directory resolves"), Plugin.IsValid());
	if (!Profile || !Plugin.IsValid())
	{
		return false;
	}

	const FString BaselinePath = FPaths::Combine(
		Plugin->GetBaseDir(),
		TEXT("Resources"),
		TEXT("Validation"),
		TEXT("Manny"),
		TEXT("MannyValidationBaseline.v1.json")
	);
	FString Json;
	TestTrue(
		TEXT("The Manny Baseline artifact loads"),
		FFileHelper::LoadFileToString(Json, *BaselinePath)
	);
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader =
		TJsonReaderFactory<>::Create(Json);
	TestTrue(
		TEXT("The Manny Baseline artifact parses"),
		FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid()
	);
	if (!Root.IsValid())
	{
		return false;
	}

	const FString BaselineHash =
		Root->GetStringField(TEXT("baseline_hash"));
	TestEqual(
		TEXT("The Baseline is human approved"),
		Root->GetStringField(TEXT("status")),
		FString(TEXT("approved"))
	);
	TestTrue(
		TEXT("The Profile carries the approval gate"),
		Profile->UpperBodyConstraints.bKinematicBaselineApproved
	);
	TestEqual(
		TEXT("The Profile pins the exact approved Baseline"),
		Profile->UpperBodyConstraints.ValidationBaselineHash,
		BaselineHash
	);

	const TSharedPtr<FJsonObject>* Approval = nullptr;
	TestTrue(
		TEXT("Human approval metadata exists"),
		Root->TryGetObjectField(TEXT("human_approval"), Approval) &&
		Approval &&
		Approval->IsValid()
	);
	if (Approval && Approval->IsValid())
	{
		TestFalse(
			TEXT("The approver is recorded"),
			(*Approval)->GetStringField(TEXT("approved_by")).IsEmpty()
		);
		TestFalse(
			TEXT("The approval date is recorded"),
			(*Approval)->GetStringField(TEXT("approved_at")).IsEmpty()
		);
	}

	const TArray<TSharedPtr<FJsonValue>>& TemplateReports =
		Root->GetArrayField(TEXT("template_reports"));
	TestEqual(
		TEXT("All five Published Manny procedural templates are baselined"),
		TemplateReports.Num(),
		5
	);
	for (const TSharedPtr<FJsonValue>& Value : TemplateReports)
	{
		const TSharedPtr<FJsonObject> Report = Value->AsObject();
		TestTrue(
			TEXT("Every Published template passes the approved thresholds"),
			Report.IsValid() &&
			Report->GetBoolField(TEXT("diagnostic_pass"))
		);
	}
	return true;
}

#endif
