#include "Authoring/LLMNPCUEPIArtifactAdapter.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
const FString ReconstructionSchema(TEXT("uepi.animation_reconstruction_profile.v1"));
const FString FullPoseSchema(TEXT("uepi.animation_full_pose_samples.v1"));

bool GetRequiredString(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	FString& OutValue,
	FString& OutError
)
{
	if (!Object.IsValid() || !Object->TryGetStringField(Field, OutValue))
	{
		OutError = FString::Printf(TEXT("LLMNPC_AUTHORING_UEPI_FIELD_MISSING:%s"), Field);
		return false;
	}
	OutValue = OutValue.TrimStartAndEnd();
	if (OutValue.IsEmpty())
	{
		OutError = FString::Printf(TEXT("LLMNPC_AUTHORING_UEPI_FIELD_EMPTY:%s"), Field);
		return false;
	}
	return true;
}

bool GetRequiredFiniteNumber(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	double& OutValue,
	FString& OutError
)
{
	if (
		!Object.IsValid() ||
		!Object->TryGetNumberField(Field, OutValue) ||
		!FMath::IsFinite(OutValue)
	)
	{
		OutError = FString::Printf(TEXT("LLMNPC_AUTHORING_UEPI_NUMBER_INVALID:%s"), Field);
		return false;
	}
	return true;
}

bool GetRequiredArray(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	const TArray<TSharedPtr<FJsonValue>>*& OutValues,
	FString& OutError
)
{
	if (!Object.IsValid() || !Object->TryGetArrayField(Field, OutValues) || !OutValues)
	{
		OutError = FString::Printf(TEXT("LLMNPC_AUTHORING_UEPI_ARRAY_MISSING:%s"), Field);
		return false;
	}
	return true;
}

bool ValidateDriverCurves(
	const TArray<TSharedPtr<FJsonValue>>& DriverCurves,
	int32& OutKeyCount,
	FString& OutError
)
{
	if (DriverCurves.IsEmpty() || DriverCurves.Num() > 64)
	{
		OutError = TEXT("LLMNPC_AUTHORING_UEPI_DRIVER_CURVE_COUNT_INVALID");
		return false;
	}

	OutKeyCount = 0;
	for (int32 CurveIndex = 0; CurveIndex < DriverCurves.Num(); ++CurveIndex)
	{
		const TSharedPtr<FJsonObject> Curve = DriverCurves[CurveIndex]->AsObject();
		FString BoneName;
		if (!GetRequiredString(Curve, TEXT("bone_name"), BoneName, OutError))
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Keyframes = nullptr;
		if (!GetRequiredArray(Curve, TEXT("keyframes"), Keyframes, OutError))
		{
			return false;
		}
		if (Keyframes->IsEmpty() || Keyframes->Num() > 300)
		{
			OutError = FString::Printf(
				TEXT("LLMNPC_AUTHORING_UEPI_DRIVER_KEY_COUNT_INVALID:%s"),
				*BoneName
			);
			return false;
		}

		double PreviousNormalizedTime = -1.0;
		for (const TSharedPtr<FJsonValue>& KeyValue : *Keyframes)
		{
			const TSharedPtr<FJsonObject> Key = KeyValue->AsObject();
			double NormalizedTime = 0.0;
			if (!GetRequiredFiniteNumber(Key, TEXT("normalized_time"), NormalizedTime, OutError))
			{
				return false;
			}
			if (
				NormalizedTime < -KINDA_SMALL_NUMBER ||
				NormalizedTime > 1.001 ||
				NormalizedTime + KINDA_SMALL_NUMBER < PreviousNormalizedTime
			)
			{
				OutError = FString::Printf(
					TEXT("LLMNPC_AUTHORING_UEPI_DRIVER_TIME_INVALID:%s"),
					*BoneName
				);
				return false;
			}

			const TSharedPtr<FJsonObject>* LocalTransform = nullptr;
			if (
				!Key.IsValid() ||
				!Key->TryGetObjectField(TEXT("local_transform"), LocalTransform) ||
				!LocalTransform ||
				!LocalTransform->IsValid()
			)
			{
				OutError = FString::Printf(
					TEXT("LLMNPC_AUTHORING_UEPI_LOCAL_TRANSFORM_MISSING:%s"),
					*BoneName
				);
				return false;
			}
			PreviousNormalizedTime = NormalizedTime;
		}
		OutKeyCount += Keyframes->Num();
	}
	return true;
}

TSharedRef<FJsonObject> CopyDriverCurve(const TSharedPtr<FJsonObject>& Source)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	static const TArray<FString> StringFields = {
		TEXT("bone_name"),
		TEXT("intent_group"),
		TEXT("curve_semantics"),
		TEXT("programmatic_recipe_hint")
	};
	static const TArray<FString> NumberFields = {
		TEXT("rank"),
		TEXT("bone_index"),
		TEXT("parent_index"),
		TEXT("driver_score"),
		TEXT("local_translation_range"),
		TEXT("local_rotation_range_degrees"),
		TEXT("local_scale_range"),
		TEXT("rotation_extrema_count")
	};

	for (const FString& Field : StringFields)
	{
		FString Value;
		if (Source->TryGetStringField(Field, Value))
		{
			Result->SetStringField(Field, Value);
		}
	}
	for (const FString& Field : NumberFields)
	{
		double Value = 0.0;
		if (Source->TryGetNumberField(Field, Value) && FMath::IsFinite(Value))
		{
			Result->SetNumberField(Field, Value);
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* Keyframes = nullptr;
	if (Source->TryGetArrayField(TEXT("keyframes"), Keyframes) && Keyframes)
	{
		TArray<TSharedPtr<FJsonValue>> CopiedKeys;
		CopiedKeys.Reserve(Keyframes->Num());
		for (const TSharedPtr<FJsonValue>& KeyValue : *Keyframes)
		{
			const TSharedPtr<FJsonObject> SourceKey = KeyValue->AsObject();
			TSharedRef<FJsonObject> Key = MakeShared<FJsonObject>();
			double Number = 0.0;
			if (SourceKey->TryGetNumberField(TEXT("frame_number"), Number))
			{
				Key->SetNumberField(TEXT("frame_number"), Number);
			}
			if (SourceKey->TryGetNumberField(TEXT("time_seconds"), Number))
			{
				Key->SetNumberField(TEXT("time_seconds"), Number);
			}
			if (SourceKey->TryGetNumberField(TEXT("normalized_time"), Number))
			{
				Key->SetNumberField(TEXT("normalized_time"), Number);
			}
			const TSharedPtr<FJsonObject>* Transform = nullptr;
			if (SourceKey->TryGetObjectField(TEXT("local_transform"), Transform) && Transform && Transform->IsValid())
			{
				Key->SetObjectField(TEXT("local_transform"), *Transform);
			}
			CopiedKeys.Add(MakeShared<FJsonValueObject>(Key));
		}
		Result->SetArrayField(TEXT("keyframes"), MoveTemp(CopiedKeys));
	}
	return Result;
}
}

bool FLLMNPCUEPIArtifactAdapter::LoadReconstructionProfile(
	const FString& ProfileFilePath,
	FLLMNPCUEPIReconstructionSummary& OutSummary,
	FString& OutAuthoringContextJson,
	FString& OutError
)
{
	FString ProfileJson;
	if (!FFileHelper::LoadFileToString(ProfileJson, *ProfileFilePath))
	{
		OutError = TEXT("LLMNPC_AUTHORING_UEPI_PROFILE_READ_FAILED");
		return false;
	}
	if (!ParseReconstructionProfile(ProfileJson, OutSummary, OutAuthoringContextJson, OutError))
	{
		return false;
	}
	return true;
}

bool FLLMNPCUEPIArtifactAdapter::ParseReconstructionProfile(
	const FString& ProfileJson,
	FLLMNPCUEPIReconstructionSummary& OutSummary,
	FString& OutAuthoringContextJson,
	FString& OutError
)
{
	OutSummary = FLLMNPCUEPIReconstructionSummary();
	OutAuthoringContextJson.Reset();
	OutError.Reset();

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ProfileJson);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("LLMNPC_AUTHORING_UEPI_PROFILE_JSON_INVALID");
		return false;
	}

	if (!GetRequiredString(Root, TEXT("schema_version"), OutSummary.ProfileSchemaVersion, OutError))
	{
		return false;
	}
	if (OutSummary.ProfileSchemaVersion != ReconstructionSchema)
	{
		OutError = TEXT("LLMNPC_AUTHORING_UEPI_PROFILE_SCHEMA_UNSUPPORTED");
		return false;
	}

	FString AnalysisState;
	double PlayLength = 0.0;
	if (
		!GetRequiredString(Root, TEXT("analysis_state"), AnalysisState, OutError) ||
		!GetRequiredString(Root, TEXT("sequence_path"), OutSummary.SequencePath, OutError) ||
		!GetRequiredString(Root, TEXT("skeleton_path"), OutSummary.SkeletonPath, OutError) ||
		!GetRequiredFiniteNumber(Root, TEXT("play_length_seconds"), PlayLength, OutError)
	)
	{
		return false;
	}
	if (AnalysisState != TEXT("ready") || PlayLength <= 0.0 || PlayLength > 600.0)
	{
		OutError = TEXT("LLMNPC_AUTHORING_UEPI_PROFILE_NOT_READY");
		return false;
	}
	OutSummary.PlayLengthSeconds = static_cast<float>(PlayLength);
	Root->TryGetStringField(TEXT("artifact_id"), OutSummary.ArtifactId);
	Root->TryGetStringField(TEXT("artifact_uri"), OutSummary.ArtifactUri);
	OutSummary.ProfileContentHash = HashJson(ProfileJson);

	const TArray<TSharedPtr<FJsonValue>>* DriverCurves = nullptr;
	if (!GetRequiredArray(Root, TEXT("driver_track_curves"), DriverCurves, OutError))
	{
		return false;
	}
	if (!ValidateDriverCurves(*DriverCurves, OutSummary.DriverKeyCount, OutError))
	{
		return false;
	}
	OutSummary.DriverCurveCount = DriverCurves->Num();

	const TArray<TSharedPtr<FJsonValue>>* RecommendedBones = nullptr;
	if (Root->TryGetArrayField(TEXT("recommended_driver_bones"), RecommendedBones) && RecommendedBones)
	{
		for (const TSharedPtr<FJsonValue>& Value : *RecommendedBones)
		{
			FString BoneName;
			if (Value.IsValid() && Value->TryGetString(BoneName) && !BoneName.IsEmpty())
			{
				OutSummary.RecommendedDriverBones.Add(FName(*BoneName));
			}
		}
	}

	const TSharedPtr<FJsonObject>* FullPoseManifest = nullptr;
	if (
		Root->TryGetObjectField(TEXT("full_pose_sample_artifact"), FullPoseManifest) &&
		FullPoseManifest &&
		FullPoseManifest->IsValid()
	)
	{
		(*FullPoseManifest)->TryGetStringField(TEXT("artifact_uri"), OutSummary.FullPoseArtifactUri);
		(*FullPoseManifest)->TryGetStringField(TEXT("path"), OutSummary.FullPoseArtifactPath);
		double SampleCount = 0.0;
		if ((*FullPoseManifest)->TryGetNumberField(TEXT("sample_count"), SampleCount) && FMath::IsFinite(SampleCount))
		{
			OutSummary.FullPoseSampleCount = FMath::Max(0, FMath::RoundToInt(SampleCount));
		}
	}

	TSharedRef<FJsonObject> Context = MakeShared<FJsonObject>();
	Context->SetStringField(TEXT("schema_version"), TEXT("llmnpc.authoring_context.v1"));
	TSharedRef<FJsonObject> Source = MakeShared<FJsonObject>();
	Source->SetStringField(TEXT("profile_schema_version"), OutSummary.ProfileSchemaVersion);
	Source->SetStringField(TEXT("profile_content_hash"), OutSummary.ProfileContentHash);
	Source->SetStringField(TEXT("artifact_id"), OutSummary.ArtifactId);
	Source->SetStringField(TEXT("artifact_uri"), OutSummary.ArtifactUri);
	Source->SetStringField(TEXT("sequence_path"), OutSummary.SequencePath);
	Source->SetStringField(TEXT("skeleton_path"), OutSummary.SkeletonPath);
	Source->SetNumberField(TEXT("play_length_seconds"), OutSummary.PlayLengthSeconds);
	Source->SetNumberField(TEXT("driver_curve_count"), OutSummary.DriverCurveCount);
	Source->SetNumberField(TEXT("driver_key_count"), OutSummary.DriverKeyCount);
	Source->SetStringField(TEXT("full_pose_artifact_uri"), OutSummary.FullPoseArtifactUri);
	Source->SetStringField(TEXT("full_pose_artifact_path"), OutSummary.FullPoseArtifactPath);
	Source->SetNumberField(TEXT("full_pose_sample_count"), OutSummary.FullPoseSampleCount);
	Context->SetObjectField(TEXT("source"), Source);

	TSharedRef<FJsonObject> Policy = MakeShared<FJsonObject>();
	Policy->SetBoolField(TEXT("runtime_bone_output_forbidden"), true);
	Policy->SetBoolField(TEXT("published_controls_only"), true);
	Policy->SetStringField(TEXT("full_pose_usage"), TEXT("validation_only"));
	Context->SetObjectField(TEXT("reconstruction_policy"), Policy);

	TArray<TSharedPtr<FJsonValue>> BoneValues;
	for (const FName BoneName : OutSummary.RecommendedDriverBones)
	{
		BoneValues.Add(MakeShared<FJsonValueString>(BoneName.ToString()));
	}
	Context->SetArrayField(TEXT("recommended_driver_bones"), MoveTemp(BoneValues));

	const TArray<TSharedPtr<FJsonValue>>* PhaseEstimates = nullptr;
	if (Root->TryGetArrayField(TEXT("phase_estimates"), PhaseEstimates) && PhaseEstimates)
	{
		Context->SetArrayField(TEXT("phase_estimates"), *PhaseEstimates);
	}
	else
	{
		Context->SetArrayField(TEXT("phase_estimates"), {});
	}

	TArray<TSharedPtr<FJsonValue>> ContextCurves;
	ContextCurves.Reserve(DriverCurves->Num());
	for (const TSharedPtr<FJsonValue>& CurveValue : *DriverCurves)
	{
		ContextCurves.Add(MakeShared<FJsonValueObject>(CopyDriverCurve(CurveValue->AsObject())));
	}
	Context->SetArrayField(TEXT("driver_track_curves"), MoveTemp(ContextCurves));

	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutAuthoringContextJson);
	if (!FJsonSerializer::Serialize(Context, Writer))
	{
		OutError = TEXT("LLMNPC_AUTHORING_CONTEXT_SERIALIZE_FAILED");
		return false;
	}
	return true;
}

bool FLLMNPCUEPIArtifactAdapter::ValidateFullPoseArtifact(
	const FString& FullPoseJson,
	const FLLMNPCUEPIReconstructionSummary& Summary,
	FString& OutError
)
{
	OutError.Reset();
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FullPoseJson);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("LLMNPC_AUTHORING_FULL_POSE_JSON_INVALID");
		return false;
	}

	FString Schema;
	FString SequencePath;
	FString SkeletonPath;
	if (
		!GetRequiredString(Root, TEXT("schema_version"), Schema, OutError) ||
		!GetRequiredString(Root, TEXT("sequence_path"), SequencePath, OutError) ||
		!GetRequiredString(Root, TEXT("skeleton_path"), SkeletonPath, OutError)
	)
	{
		return false;
	}
	if (Schema != FullPoseSchema)
	{
		OutError = TEXT("LLMNPC_AUTHORING_FULL_POSE_SCHEMA_UNSUPPORTED");
		return false;
	}
	if (SequencePath != Summary.SequencePath || SkeletonPath != Summary.SkeletonPath)
	{
		OutError = TEXT("LLMNPC_AUTHORING_FULL_POSE_SOURCE_MISMATCH");
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Samples = nullptr;
	if (!GetRequiredArray(Root, TEXT("samples"), Samples, OutError) || Samples->Num() < 2)
	{
		OutError = TEXT("LLMNPC_AUTHORING_FULL_POSE_SAMPLES_INVALID");
		return false;
	}
	double DeclaredSampleCount = 0.0;
	if (
		!GetRequiredFiniteNumber(Root, TEXT("sample_count"), DeclaredSampleCount, OutError) ||
		FMath::RoundToInt(DeclaredSampleCount) != Samples->Num() ||
		(Summary.FullPoseSampleCount > 0 && Summary.FullPoseSampleCount != Samples->Num())
	)
	{
		OutError = TEXT("LLMNPC_AUTHORING_FULL_POSE_SAMPLE_COUNT_MISMATCH");
		return false;
	}

	double PreviousNormalizedTime = -1.0;
	for (const TSharedPtr<FJsonValue>& SampleValue : *Samples)
	{
		const TSharedPtr<FJsonObject> Sample = SampleValue->AsObject();
		double NormalizedTime = 0.0;
		const TArray<TSharedPtr<FJsonValue>>* Bones = nullptr;
		if (
			!GetRequiredFiniteNumber(Sample, TEXT("normalized_time"), NormalizedTime, OutError) ||
			NormalizedTime < -KINDA_SMALL_NUMBER ||
			NormalizedTime > 1.001 ||
			NormalizedTime + KINDA_SMALL_NUMBER < PreviousNormalizedTime ||
			!GetRequiredArray(Sample, TEXT("bones"), Bones, OutError) ||
			Bones->IsEmpty()
		)
		{
			OutError = TEXT("LLMNPC_AUTHORING_FULL_POSE_SAMPLE_INVALID");
			return false;
		}
		PreviousNormalizedTime = NormalizedTime;
	}
	return true;
}

FString FLLMNPCUEPIArtifactAdapter::HashJson(const FString& JsonString)
{
	FTCHARToUTF8 Utf8(*JsonString);
	uint8 Digest[FSHA1::DigestSize];
	FSHA1::HashBuffer(Utf8.Get(), Utf8.Length(), Digest);
	return TEXT("sha1:") + BytesToHex(Digest, FSHA1::DigestSize).ToLower();
}
