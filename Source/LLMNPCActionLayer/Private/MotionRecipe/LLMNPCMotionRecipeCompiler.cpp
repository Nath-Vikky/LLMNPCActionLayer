#include "MotionRecipe/LLMNPCMotionRecipeCompiler.h"

#include "LLMNPCMotionValidator.h"
#include "Misc/SecureHash.h"
#include "MotionRecipe/LLMNPCMotionRecipeValidator.h"

namespace
{
FLLMMotionTrack& AddBaseTrack(
	FLLMMotionClip& Clip,
	FName ControlId,
	ELLMMotionTrackType TrackType,
	ELLMMotionValueType ValueType,
	const FLLMNPCMotionRecipePrimitive& Primitive
)
{
	FLLMMotionTrack& Track = Clip.Tracks.AddDefaulted_GetRef();
	Track.ControlId = ControlId;
	Track.TrackType = TrackType;
	Track.ValueType = ValueType;
	Track.StartTime = static_cast<float>(Primitive.StartTimeSeconds);
	Track.EndTime = static_cast<float>(Primitive.EndTimeSeconds);
	Track.Envelope = ELLMMotionEnvelope::None;
	return Track;
}

void AddShapedFloatTrack(
	FLLMMotionClip& Clip,
	FName ControlId,
	float Value,
	const FLLMNPCMotionRecipePrimitive& Primitive
)
{
	FLLMMotionTrack& Track = AddBaseTrack(
		Clip,
		ControlId,
		ELLMMotionTrackType::Keyframes,
		ELLMMotionValueType::Float,
		Primitive
	);
	const float Duration = Track.EndTime - Track.StartTime;
	const float RiseTime = Duration * 0.24f;
	Track.FloatKeys = {
		{Track.StartTime, 0.0f},
		{Track.StartTime + RiseTime, Value},
		{Track.EndTime - RiseTime, Value},
		{Track.EndTime, 0.0f}
	};
}

FLLMMotionTrack& AddKeyframedFloatTrack(
	FLLMMotionClip& Clip,
	FName ControlId,
	const TArray<FLLMMotionKeyFloat>& Keys,
	const FLLMNPCMotionRecipePrimitive& Primitive
)
{
	FLLMMotionTrack& Track = AddBaseTrack(
		Clip,
		ControlId,
		ELLMMotionTrackType::Keyframes,
		ELLMMotionValueType::Float,
		Primitive
	);
	Track.FloatKeys = Keys;
	return Track;
}

void AddOscillatorTrack(
	FLLMMotionClip& Clip,
	FName ControlId,
	float Amplitude,
	float Cycles,
	const FLLMNPCMotionRecipePrimitive& Primitive
)
{
	FLLMMotionTrack& Track = AddBaseTrack(
		Clip,
		ControlId,
		ELLMMotionTrackType::Oscillator,
		ELLMMotionValueType::Float,
		Primitive
	);
	Track.Amplitude = Amplitude;
	Track.Frequency = Cycles;
	Track.Envelope = ELLMMotionEnvelope::Smooth;
}

void AddSmoothPulseTrack(
	FLLMMotionClip& Clip,
	FName ControlId,
	float Value,
	const FLLMNPCMotionRecipePrimitive& Primitive
)
{
	FLLMMotionTrack& Track = AddBaseTrack(
		Clip,
		ControlId,
		ELLMMotionTrackType::Hold,
		ELLMMotionValueType::Float,
		Primitive
	);
	Track.Amplitude = Value;
	Track.Envelope = ELLMMotionEnvelope::EaseInOut;
}

void AddAnchorTrack(
	FLLMMotionClip& Clip,
	FName ControlId,
	FName Anchor,
	const FVector& Offset,
	float Strength,
	const FLLMNPCMotionRecipePrimitive& Primitive,
	ELLMMotionEnvelope Envelope = ELLMMotionEnvelope::EaseInOut
)
{
	FLLMMotionTrack& Track = AddBaseTrack(
		Clip,
		ControlId,
		ELLMMotionTrackType::Anchor,
		ELLMMotionValueType::Vector,
		Primitive
	);
	Track.Anchor = Anchor;
	Track.Offset = Offset;
	Track.Strength = FMath::Clamp(Strength, 0.0f, 1.0f);
	Track.Envelope = Envelope;
}

void AddReachTrack(
	FLLMMotionClip& Clip,
	FName ControlId,
	const FString& TargetRef,
	float Reach,
	const FVector& Offset,
	float Strength,
	const FLLMNPCMotionRecipePrimitive& Primitive
)
{
	FLLMMotionTrack& Track = AddBaseTrack(
		Clip,
		ControlId,
		ELLMMotionTrackType::IKReach,
		ELLMMotionValueType::Vector,
		Primitive
	);
	Track.TargetRef = TargetRef;
	Track.Reach = FMath::Clamp(Reach, 0.0f, 1.0f);
	Track.Offset = Offset;
	Track.Strength = FMath::Clamp(Strength, 0.0f, 1.0f);
	Track.Envelope = ELLMMotionEnvelope::EaseInOut;
}

void AddLookAtTrack(
	FLLMMotionClip& Clip,
	FName ControlId,
	const FString& TargetRef,
	float Strength,
	const FLLMNPCMotionRecipePrimitive& Primitive
)
{
	FLLMMotionTrack& Track = AddBaseTrack(
		Clip,
		ControlId,
		ELLMMotionTrackType::LookAt,
		ELLMMotionValueType::Vector,
		Primitive
	);
	Track.TargetRef = TargetRef;
	Track.Strength = FMath::Clamp(Strength, 0.0f, 1.0f);
	Track.Envelope = ELLMMotionEnvelope::EaseInOut;
}

int32 SolverPhase(FName BodyRegion)
{
	if (BodyRegion == TEXT("gaze") || BodyRegion == TEXT("head"))
	{
		return 0;
	}
	if (BodyRegion == TEXT("chest") || BodyRegion == TEXT("shoulders"))
	{
		return 1;
	}
	if (BodyRegion == TEXT("arms"))
	{
		return 2;
	}
	return 3;
}

bool ResolveTargetBinding(
	const FLLMNPCMotionRecipePrimitive& Primitive,
	const FLLMNPCMotionRecipeCompileContext& Context,
	FLLMNPCCompiledRecipeMetadata& OutMetadata,
	FString& OutTargetRef,
	FString& OutError
)
{
	OutTargetRef.Reset();
	if (Primitive.TargetSlot.IsNone())
	{
		return true;
	}
	const FString* TargetRef = Context.TargetBindings.Find(Primitive.TargetSlot);
	if (!TargetRef)
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_RECIPE_TARGET_BINDING_MISSING:%s"),
			*Primitive.TargetSlot.ToString()
		);
		return false;
	}
	OutTargetRef = TargetRef->TrimStartAndEnd();
	if (OutTargetRef.IsEmpty() || OutTargetRef.Len() > 128)
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_RECIPE_TARGET_BINDING_INVALID:%s"),
			*Primitive.TargetSlot.ToString()
		);
		return false;
	}
	OutMetadata.DynamicTargetBindings.Add(
		Primitive.TargetSlot,
		OutTargetRef
	);
	return true;
}

bool CompileHeadPrimitive(
	const FLLMNPCMotionRecipePrimitive& Primitive,
	FLLMMotionClip& Clip
)
{
	const float Amplitude = static_cast<float>(
		Primitive.GetNumberParameter(TEXT("amplitude"), 0.65)
	);
	const float Speed = static_cast<float>(
		Primitive.GetNumberParameter(TEXT("speed"), 1.0)
	);
	if (Primitive.PrimitiveId == TEXT("head.nod"))
	{
		AddOscillatorTrack(
			Clip,
			TEXT("head.pitch"),
			18.0f * Amplitude,
			static_cast<float>(
				Primitive.GetNumberParameter(TEXT("cycles"), 1.0)
			) * Speed,
			Primitive
		);
		return true;
	}
	if (Primitive.PrimitiveId == TEXT("head.shake"))
	{
		AddOscillatorTrack(
			Clip,
			TEXT("head.yaw"),
			22.0f * Amplitude,
			static_cast<float>(
				Primitive.GetNumberParameter(TEXT("cycles"), 1.0)
			) * Speed,
			Primitive
		);
		return true;
	}
	if (Primitive.PrimitiveId == TEXT("head.tilt"))
	{
		const float Sign =
			Primitive.GetStringParameter(
				TEXT("direction"),
				TEXT("right")
			) == TEXT("left")
				? -1.0f
				: 1.0f;
		AddShapedFloatTrack(
			Clip,
			TEXT("head.roll"),
			Sign * 14.0f * Amplitude,
			Primitive
		);
		return true;
	}
	return false;
}

bool CompileChestPrimitive(
	const FLLMNPCMotionRecipePrimitive& Primitive,
	FLLMMotionClip& Clip
)
{
	const float Amplitude = static_cast<float>(
		Primitive.GetNumberParameter(TEXT("amplitude"), 0.35)
	);
	if (Primitive.PrimitiveId == TEXT("chest.lean"))
	{
		const float Sign =
			Primitive.GetStringParameter(
				TEXT("direction"),
				TEXT("forward")
			) == TEXT("back")
				? -1.0f
				: 1.0f;
		AddShapedFloatTrack(
			Clip,
			TEXT("chest.pitch"),
			Sign * 10.0f * Amplitude,
			Primitive
		);
		return true;
	}
	if (Primitive.PrimitiveId == TEXT("chest.turn"))
	{
		const float Sign = Primitive.Side == TEXT("left") ? -1.0f : 1.0f;
		AddShapedFloatTrack(
			Clip,
			TEXT("chest.yaw"),
			Sign * 14.0f * Amplitude,
			Primitive
		);
		return true;
	}
	return false;
}

void CompileShrugPrimitive(
	const FLLMNPCMotionRecipePrimitive& Primitive,
	FLLMMotionClip& Clip
)
{
	const float Amplitude = static_cast<float>(
		Primitive.GetNumberParameter(TEXT("amplitude"), 0.75)
	);
	const float Torso = static_cast<float>(
		Primitive.GetNumberParameter(TEXT("torso_participation"), 0.35)
	);
	const float ArmOpenness = static_cast<float>(
		Primitive.GetNumberParameter(TEXT("arm_openness"), 0.6)
	);
	const float PalmOpenness = static_cast<float>(
		Primitive.GetNumberParameter(TEXT("palm_openness"), 0.75)
	);
	const float Asymmetry = static_cast<float>(
		Primitive.GetNumberParameter(TEXT("asymmetry"), 0.05)
	);

	const float BaseLift = 6.0f + 11.0f * Amplitude;
	const float Protraction = 1.5f + 3.0f * ArmOpenness;
	const float RightScale = 1.0f + Asymmetry * 0.5f;
	const float LeftScale = 1.0f - Asymmetry * 0.5f;
	AddSmoothPulseTrack(
		Clip,
		TEXT("right_shoulder.pitch"),
		-BaseLift * RightScale,
		Primitive
	);
	AddSmoothPulseTrack(
		Clip,
		TEXT("left_shoulder.pitch"),
		BaseLift * LeftScale,
		Primitive
	);
	AddSmoothPulseTrack(
		Clip,
		TEXT("right_shoulder.yaw"),
		-Protraction * RightScale,
		Primitive
	);
	AddSmoothPulseTrack(
		Clip,
		TEXT("left_shoulder.yaw"),
		Protraction * LeftScale,
		Primitive
	);
	AddSmoothPulseTrack(
		Clip,
		TEXT("chest.pitch"),
		-4.5f * Torso * Amplitude,
		Primitive
	);
	AddSmoothPulseTrack(
		Clip,
		TEXT("chest.roll"),
		1.5f * Asymmetry,
		Primitive
	);

	const float LateralOffset = (ArmOpenness - 0.6f) * 14.0f;
	const float VerticalOffset =
		4.0f +
		6.0f * Amplitude +
		(ArmOpenness - 0.6f) * 5.0f;
	const float IKStrength = 0.7f + 0.25f * Amplitude;
	AddAnchorTrack(
		Clip,
		TEXT("right_hand.ik"),
		TEXT("right_shrug"),
		FVector(-LateralOffset, 0.0f, VerticalOffset),
		IKStrength * RightScale,
		Primitive
	);
	AddAnchorTrack(
		Clip,
		TEXT("left_hand.ik"),
		TEXT("left_shrug"),
		FVector(LateralOffset, 0.0f, VerticalOffset),
		IKStrength * LeftScale,
		Primitive
	);

	const float RelaxedWeight = 0.45f + 0.5f * PalmOpenness;
	AddSmoothPulseTrack(
		Clip,
		TEXT("right_fingers.relaxed"),
		RelaxedWeight,
		Primitive
	);
	AddSmoothPulseTrack(
		Clip,
		TEXT("left_fingers.relaxed"),
		RelaxedWeight,
		Primitive
	);
}

void AddContactSeparationTrack(
	FLLMMotionClip& Clip,
	FName ControlId,
	float SignedOpenOffset,
	int32 Cycles,
	const FLLMNPCMotionRecipePrimitive& Primitive
)
{
	const float StartTime =
		static_cast<float>(Primitive.StartTimeSeconds);
	const float EndTime =
		static_cast<float>(Primitive.EndTimeSeconds);
	const float Duration = EndTime - StartTime;
	const int32 SegmentCount = FMath::Max(1, Cycles) * 2 + 2;
	TArray<FLLMMotionKeyFloat> Keys;
	Keys.Reserve(SegmentCount + 1);
	Keys.Add({StartTime, 0.0f});
	for (int32 SegmentIndex = 1; SegmentIndex < SegmentCount; ++SegmentIndex)
	{
		const float Alpha =
			static_cast<float>(SegmentIndex) /
			static_cast<float>(SegmentCount);
		Keys.Add(
			{
				FMath::Lerp(StartTime, EndTime, Alpha),
				SegmentIndex % 2 == 1 ? SignedOpenOffset : 0.0f
			}
		);
	}
	Keys.Add({EndTime, 0.0f});
	AddKeyframedFloatTrack(
		Clip,
		ControlId,
		Keys,
		Primitive
	);
}

void CompileHandsContactPrimitive(
	const FLLMNPCMotionRecipePrimitive& Primitive,
	FLLMMotionClip& Clip
)
{
	const float Amplitude = static_cast<float>(
		Primitive.GetNumberParameter(TEXT("amplitude"), 0.75)
	);
	const float Speed = static_cast<float>(
		Primitive.GetNumberParameter(TEXT("speed"), 1.0)
	);
	const int32 Cycles = FMath::RoundToInt(
		Primitive.GetNumberParameter(TEXT("cycles"), 2.0)
	);
	const float ContactHeight = static_cast<float>(
		Primitive.GetNumberParameter(TEXT("contact_height"), 0.55)
	);
	const float Separation = static_cast<float>(
		Primitive.GetNumberParameter(TEXT("separation"), 0.65)
	);
	const float PalmOpenness = static_cast<float>(
		Primitive.GetNumberParameter(TEXT("palm_openness"), 0.9)
	);

	const float HeightOffset = (ContactHeight - 0.55f) * 32.0f;
	const float IKStrength = 0.78f + 0.18f * Amplitude;
	AddAnchorTrack(
		Clip,
		TEXT("right_hand.ik"),
		TEXT("right_clap"),
		FVector(0.0f, 0.0f, HeightOffset),
		IKStrength,
		Primitive,
		ELLMMotionEnvelope::Sustain
	);
	AddAnchorTrack(
		Clip,
		TEXT("left_hand.ik"),
		TEXT("left_clap"),
		FVector(0.0f, 0.0f, HeightOffset),
		IKStrength,
		Primitive,
		ELLMMotionEnvelope::Sustain
	);

	const float OpenOffset =
		FMath::Lerp(4.25f, 8.5f, Separation) *
		FMath::Lerp(0.8f, 1.1f, Amplitude) *
		FMath::Lerp(
			0.9f,
			1.1f,
			FMath::Clamp((Speed - 0.7f) / 0.6f, 0.0f, 1.0f)
		);
	AddContactSeparationTrack(
		Clip,
		TEXT("right_hand.local_offset.x"),
		-OpenOffset,
		Cycles,
		Primitive
	);
	AddContactSeparationTrack(
		Clip,
		TEXT("left_hand.local_offset.x"),
		OpenOffset,
		Cycles,
		Primitive
	);

	const float PalmStrength = 0.72f + 0.25f * PalmOpenness;
	AddAnchorTrack(
		Clip,
		TEXT("right_hand.palm_facing"),
		TEXT("clap_center"),
		FVector(0.0f, 0.0f, HeightOffset),
		PalmStrength,
		Primitive,
		ELLMMotionEnvelope::Sustain
	);
	AddAnchorTrack(
		Clip,
		TEXT("left_hand.palm_facing"),
		TEXT("clap_center"),
		FVector(0.0f, 0.0f, HeightOffset),
		PalmStrength,
		Primitive,
		ELLMMotionEnvelope::Sustain
	);

	const float ContactPoseWeight = 0.85f + 0.15f * PalmOpenness;
	AddShapedFloatTrack(
		Clip,
		TEXT("right_fingers.contact"),
		ContactPoseWeight,
		Primitive
	);
	AddShapedFloatTrack(
		Clip,
		TEXT("left_fingers.contact"),
		ContactPoseWeight,
		Primitive
	);
}

bool CompileArmPrimitive(
	const FLLMNPCMotionRecipePrimitive& Primitive,
	const FString& TargetRef,
	FLLMMotionClip& Clip
)
{
	const bool bRight = Primitive.Side == TEXT("right");
	const FString SidePrefix = bRight ? TEXT("right") : TEXT("left");
	const FName IKControl(*FString::Printf(
		TEXT("%s_hand.ik"),
		*SidePrefix
	));
	const float Height = static_cast<float>(
		Primitive.GetNumberParameter(TEXT("height"), 0.5)
	);
	const FVector Offset(0.0f, 0.0f, (Height - 0.5f) * 20.0f);
	if (Primitive.PrimitiveId == TEXT("arm.reach"))
	{
		AddReachTrack(
			Clip,
			IKControl,
			TargetRef,
			static_cast<float>(
				Primitive.GetNumberParameter(TEXT("reach"), 0.75)
			),
			Offset,
			1.0f,
			Primitive
		);
		return true;
	}
	if (Primitive.PrimitiveId == TEXT("arm.present"))
	{
		const float Amplitude = static_cast<float>(
			Primitive.GetNumberParameter(TEXT("amplitude"), 0.65)
		);
		AddReachTrack(
			Clip,
			IKControl,
			TargetRef,
			FMath::Lerp(0.45f, 0.82f, Amplitude),
			Offset,
			0.75f + 0.2f * Amplitude,
			Primitive
		);
		AddShapedFloatTrack(
			Clip,
			FName(*FString::Printf(
				TEXT("%s_fingers.open"),
				*SidePrefix
			)),
			0.9f,
			Primitive
		);
		AddLookAtTrack(
			Clip,
			FName(*FString::Printf(
				TEXT("%s_hand.palm_target"),
				*SidePrefix
			)),
			TargetRef,
			0.75f,
			Primitive
		);
		return true;
	}
	return false;
}

void CompileWavePrimitive(
	const FLLMNPCMotionRecipePrimitive& Primitive,
	const FString& TargetRef,
	FLLMMotionClip& Clip
)
{
	const bool bRight = Primitive.Side == TEXT("right");
	const FString SidePrefix = bRight ? TEXT("right") : TEXT("left");
	const float Amplitude = static_cast<float>(
		Primitive.GetNumberParameter(TEXT("amplitude"), 0.65)
	);
	const float Height = static_cast<float>(
		Primitive.GetNumberParameter(TEXT("height"), 0.55)
	);
	const float Cycles = static_cast<float>(
		Primitive.GetNumberParameter(TEXT("cycles"), 2.0)
	);
	const float Speed = static_cast<float>(
		Primitive.GetNumberParameter(TEXT("speed"), 1.0)
	);
	const FName IKControl(*FString::Printf(
		TEXT("%s_hand.ik"),
		*SidePrefix
	));
	if (TargetRef.IsEmpty())
	{
		AddAnchorTrack(
			Clip,
			IKControl,
			bRight ? FName(TEXT("right_wave")) : FName(TEXT("left_wave")),
			FVector(0.0f, 0.0f, (Height - 0.55f) * 24.0f),
			0.9f,
			Primitive
		);
	}
	else
	{
		AddReachTrack(
			Clip,
			IKControl,
			TargetRef,
			0.62f,
			FVector(0.0f, 0.0f, (Height - 0.5f) * 20.0f),
			0.9f,
			Primitive
		);
	}
	AddOscillatorTrack(
		Clip,
		FName(*FString::Printf(
			TEXT("%s_hand.local_offset.x"),
			*SidePrefix
		)),
		17.0f * Amplitude,
		Cycles * Speed,
		Primitive
	);
	AddShapedFloatTrack(
		Clip,
		FName(*FString::Printf(
			TEXT("%s_fingers.open"),
			*SidePrefix
		)),
		0.9f,
		Primitive
	);
	if (!TargetRef.IsEmpty())
	{
		AddLookAtTrack(
			Clip,
			FName(*FString::Printf(
				TEXT("%s_hand.palm_target"),
				*SidePrefix
			)),
			TargetRef,
			0.65f,
			Primitive
		);
	}
}

void CompileBeckonPrimitive(
	const FLLMNPCMotionRecipePrimitive& Primitive,
	const FString& TargetRef,
	FLLMMotionClip& Clip
)
{
	const FString SidePrefix =
		Primitive.Side == TEXT("right") ? TEXT("right") : TEXT("left");
	const float Amplitude = static_cast<float>(
		Primitive.GetNumberParameter(TEXT("amplitude"), 0.7)
	);
	const float Speed = static_cast<float>(
		Primitive.GetNumberParameter(TEXT("speed"), 1.0)
	);
	const int32 Cycles = FMath::RoundToInt(
		Primitive.GetNumberParameter(TEXT("cycles"), 2.0)
	);
	const float CurlAmount = static_cast<float>(
		Primitive.GetNumberParameter(TEXT("curl_amount"), 0.72)
	);
	const float Reach = static_cast<float>(
		Primitive.GetNumberParameter(TEXT("reach"), 0.58)
	);
	const float Height = static_cast<float>(
		Primitive.GetNumberParameter(TEXT("height"), 0.55)
	);
	const float SolverReach = FMath::Lerp(
		0.18f,
		0.34f,
		FMath::Clamp(
			(Reach - 0.35f) / (0.78f - 0.35f),
			0.0f,
			1.0f
		)
	);
	const float OutwardOffset =
		Primitive.Side == TEXT("right") ? 5.0f : -5.0f;

	const FName IKControl(*FString::Printf(
		TEXT("%s_hand.ik"),
		*SidePrefix
	));
	AddReachTrack(
		Clip,
		IKControl,
		TargetRef,
		SolverReach,
		FVector(
			0.0f,
			OutwardOffset,
			(Height - 0.5f) * 24.0f
		),
		0.72f + 0.22f * Amplitude,
		Primitive
	);
	AddLookAtTrack(
		Clip,
		FName(*FString::Printf(
			TEXT("%s_hand.palm_target"),
			*SidePrefix
		)),
		TargetRef,
		0.68f + 0.22f * Amplitude,
		Primitive
	);

	const float StartTime =
		static_cast<float>(Primitive.StartTimeSeconds);
	const float EndTime =
		static_cast<float>(Primitive.EndTimeSeconds);
	const float Duration = EndTime - StartTime;
	const float GestureStart = StartTime + Duration * 0.22f;
	const float GestureEnd = EndTime - Duration * 0.18f;
	const float CycleDuration =
		(GestureEnd - GestureStart) /
		static_cast<float>(FMath::Max(1, Cycles));
	const float SpeedAlpha = FMath::Clamp(
		(Speed - 0.7f) / 0.6f,
		0.0f,
		1.0f
	);
	const float CurlPeakPhase = FMath::Lerp(
		0.56f,
		0.38f,
		SpeedAlpha
	);
	const float EffectiveCurl = FMath::Clamp(
		CurlAmount * FMath::Lerp(0.82f, 1.0f, Amplitude),
		0.0f,
		1.0f
	);

	TArray<FLLMMotionKeyFloat> RelaxedKeys;
	TArray<FLLMMotionKeyFloat> CurlKeys;
	RelaxedKeys.Reserve(Cycles * 2 + 3);
	CurlKeys.Reserve(Cycles * 2 + 3);
	RelaxedKeys.Add({StartTime, 0.0f});
	CurlKeys.Add({StartTime, 0.0f});
	RelaxedKeys.Add({GestureStart, 1.0f});
	CurlKeys.Add({GestureStart, 0.0f});
	for (int32 CycleIndex = 0; CycleIndex < Cycles; ++CycleIndex)
	{
		const float CycleStart =
			GestureStart + CycleDuration * CycleIndex;
		const float CurlPeak =
			CycleStart + CycleDuration * CurlPeakPhase;
		const float CycleEnd = CycleStart + CycleDuration;
		RelaxedKeys.Add({CurlPeak, 1.0f - EffectiveCurl});
		CurlKeys.Add({CurlPeak, EffectiveCurl});
		RelaxedKeys.Add({CycleEnd, 1.0f});
		CurlKeys.Add({CycleEnd, 0.0f});
	}
	RelaxedKeys.Add({EndTime, 0.0f});
	CurlKeys.Add({EndTime, 0.0f});

	FLLMMotionTrack& RelaxedTrack = AddKeyframedFloatTrack(
		Clip,
		FName(*FString::Printf(
			TEXT("%s_fingers.relaxed"),
			*SidePrefix
		)),
		RelaxedKeys,
		Primitive
	);
	RelaxedTrack.TargetRef = TargetRef;
	FLLMMotionTrack& CurlTrack = AddKeyframedFloatTrack(
		Clip,
		FName(*FString::Printf(
			TEXT("%s_fingers.curl"),
			*SidePrefix
		)),
		CurlKeys,
		Primitive
	);
	CurlTrack.TargetRef = TargetRef;
}

bool CompileHandPosePrimitive(
	const FLLMNPCMotionRecipePrimitive& Primitive,
	FLLMMotionClip& Clip
)
{
	FString PoseName;
	if (Primitive.PrimitiveId == TEXT("hand.pose.open"))
	{
		PoseName = TEXT("open");
	}
	else if (Primitive.PrimitiveId == TEXT("hand.pose.point"))
	{
		PoseName = TEXT("point");
	}
	else if (Primitive.PrimitiveId == TEXT("hand.pose.relaxed"))
	{
		PoseName = TEXT("relaxed");
	}
	else if (Primitive.PrimitiveId == TEXT("hand.pose.curl"))
	{
		PoseName = TEXT("curl");
	}
	else
	{
		return false;
	}
	AddShapedFloatTrack(
		Clip,
		FName(*FString::Printf(
			TEXT("%s_fingers.%s"),
			*Primitive.Side.ToString(),
			*PoseName
		)),
		static_cast<float>(
			Primitive.GetNumberParameter(TEXT("weight"), 1.0)
		),
		Primitive
	);
	return true;
}

bool CompilePrimitive(
	const FLLMNPCMotionRecipePrimitive& Primitive,
	const FLLMNPCMotionPrimitiveDefinition& Definition,
	const FLLMNPCMotionRecipeCompileContext& Context,
	FLLMMotionClip& Clip,
	FLLMNPCCompiledRecipeMetadata& OutMetadata,
	FString& OutError
)
{
	FString TargetRef;
	if (
		!ResolveTargetBinding(
			Primitive,
			Context,
			OutMetadata,
			TargetRef,
			OutError
		)
	)
	{
		return false;
	}

	const int32 FirstTrackIndex = Clip.Tracks.Num();
	bool bCompiled = false;
	if (
		Primitive.PrimitiveId == TEXT("head.nod") ||
		Primitive.PrimitiveId == TEXT("head.shake") ||
		Primitive.PrimitiveId == TEXT("head.tilt")
	)
	{
		bCompiled = CompileHeadPrimitive(Primitive, Clip);
	}
	else if (Primitive.PrimitiveId == TEXT("gaze.track"))
	{
		AddLookAtTrack(
			Clip,
			TEXT("gaze.target"),
			TargetRef,
			static_cast<float>(
				Primitive.GetNumberParameter(TEXT("engagement"), 0.75)
			),
			Primitive
		);
		bCompiled = true;
	}
	else if (
		Primitive.PrimitiveId == TEXT("chest.lean") ||
		Primitive.PrimitiveId == TEXT("chest.turn")
	)
	{
		bCompiled = CompileChestPrimitive(Primitive, Clip);
	}
	else if (Primitive.PrimitiveId == TEXT("shoulder.shrug"))
	{
		CompileShrugPrimitive(Primitive, Clip);
		bCompiled = true;
	}
	else if (Primitive.PrimitiveId == TEXT("hands.contact"))
	{
		CompileHandsContactPrimitive(Primitive, Clip);
		bCompiled = true;
	}
	else if (
		Primitive.PrimitiveId == TEXT("arm.reach") ||
		Primitive.PrimitiveId == TEXT("arm.present")
	)
	{
		bCompiled = CompileArmPrimitive(Primitive, TargetRef, Clip);
	}
	else if (Primitive.PrimitiveId == TEXT("hand.wave_arc"))
	{
		CompileWavePrimitive(Primitive, TargetRef, Clip);
		bCompiled = true;
	}
	else if (Primitive.PrimitiveId == TEXT("hand.beckon"))
	{
		CompileBeckonPrimitive(Primitive, TargetRef, Clip);
		bCompiled = true;
	}
	else
	{
		bCompiled = CompileHandPosePrimitive(Primitive, Clip);
	}

	if (!bCompiled || Clip.Tracks.Num() == FirstTrackIndex)
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_RECIPE_SOLVER_UNIMPLEMENTED:%s"),
			*Definition.SolverId.ToString()
		);
		return false;
	}

	FLLMNPCCompiledPrimitiveMetadata& Mapping =
		OutMetadata.PrimitiveMappings.AddDefaulted_GetRef();
	Mapping.SourceIndex = Primitive.SourceIndex;
	Mapping.PrimitiveId = Primitive.PrimitiveId;
	Mapping.SolverId = Definition.SolverId;
	for (
		int32 TrackIndex = FirstTrackIndex;
		TrackIndex < Clip.Tracks.Num();
		++TrackIndex
	)
	{
		Mapping.GeneratedControlIds.AddUnique(
			Clip.Tracks[TrackIndex].ControlId
		);
	}
	Mapping.GeneratedControlIds.Sort(FNameLexicalLess());
	FLLMNPCMotionPrimitiveRegistry::Get().ResolveChannels(
		Definition,
		Primitive.Side,
		Mapping.RequiredChannels
	);
	return true;
}

bool MotionPlanWasRewritten(
	const FLLMMotionPlan& Before,
	const FLLMMotionPlan& After
)
{
	if (
		Before.Version != After.Version ||
		Before.Intent != After.Intent ||
		Before.Clip.ClipId != After.Clip.ClipId ||
		!FMath::IsNearlyEqual(Before.Clip.Duration, After.Clip.Duration) ||
		!FMath::IsNearlyEqual(Before.Clip.BlendIn, After.Clip.BlendIn) ||
		!FMath::IsNearlyEqual(Before.Clip.BlendOut, After.Clip.BlendOut) ||
		!FMath::IsNearlyEqual(Before.Clip.Priority, After.Clip.Priority) ||
		Before.Clip.Tracks.Num() != After.Clip.Tracks.Num()
	)
	{
		return true;
	}
	for (int32 Index = 0; Index < Before.Clip.Tracks.Num(); ++Index)
	{
		const FLLMMotionTrack& A = Before.Clip.Tracks[Index];
		const FLLMMotionTrack& B = After.Clip.Tracks[Index];
		if (
			A.ControlId != B.ControlId ||
			A.TrackType != B.TrackType ||
			A.ValueType != B.ValueType ||
			!FMath::IsNearlyEqual(A.StartTime, B.StartTime) ||
			!FMath::IsNearlyEqual(A.EndTime, B.EndTime) ||
			!FMath::IsNearlyEqual(A.Amplitude, B.Amplitude) ||
			!FMath::IsNearlyEqual(A.Frequency, B.Frequency) ||
			!FMath::IsNearlyEqual(A.Phase, B.Phase) ||
			!FMath::IsNearlyEqual(A.Strength, B.Strength) ||
			!FMath::IsNearlyEqual(A.Reach, B.Reach) ||
			!A.Offset.Equals(B.Offset) ||
			A.TargetRef != B.TargetRef ||
			A.Anchor != B.Anchor ||
			A.FloatKeys.Num() != B.FloatKeys.Num()
		)
		{
			return true;
		}
		for (int32 KeyIndex = 0; KeyIndex < A.FloatKeys.Num(); ++KeyIndex)
		{
			if (
				!FMath::IsNearlyEqual(
					A.FloatKeys[KeyIndex].T,
					B.FloatKeys[KeyIndex].T
				) ||
				!FMath::IsNearlyEqual(
					A.FloatKeys[KeyIndex].V,
					B.FloatKeys[KeyIndex].V
				)
			)
			{
				return true;
			}
		}
	}
	return false;
}
}

bool FLLMNPCMotionRecipeCompiler::Compile(
	const FLLMNPCMotionRecipe& Recipe,
	const FLLMNPCSkeletonCapabilitySnapshot& CapabilitySnapshot,
	const FLLMNPCMotionPrimitiveRegistry& Registry,
	const FLLMNPCMotionRecipeCompileContext& Context,
	FLLMMotionPlan& OutPlan,
	FLLMNPCCompiledRecipeMetadata& OutMetadata,
	FString& OutError
)
{
	OutPlan = FLLMMotionPlan();
	OutMetadata = FLLMNPCCompiledRecipeMetadata();
	OutError.Reset();

	FLLMNPCMotionRecipe NormalizedRecipe = Recipe;
	FLLMNPCMotionRecipeValidationResult Validation;
	if (
		!FLLMNPCMotionRecipeValidator::ValidateAndNormalize(
			NormalizedRecipe,
			CapabilitySnapshot,
			Registry,
			Context.ValidationContext,
			Validation
		)
	)
	{
		OutError = Validation.ErrorCode;
		return false;
	}
	OutMetadata.RewriteRecords = Validation.NormalizationRecords;
	OutMetadata.CapabilityHash = CapabilitySnapshot.CapabilityHash;
	OutMetadata.PrimitiveRegistryVersion = Registry.GetRegistryVersion();

	FString CanonicalRecipe;
	if (
		!FLLMNPCMotionRecipeCanonicalizer::BuildCanonicalJson(
			NormalizedRecipe,
			CanonicalRecipe,
			OutError
		)
	)
	{
		return false;
	}
	OutMetadata.RecipeHash =
		FLLMNPCMotionRecipeCanonicalizer::BuildRecipeHash(CanonicalRecipe);
	const FString CompiledHashInput = FString::Printf(
		TEXT("%s\nregistry=%s\ncapability=%s\ncompiler=%s"),
		*CanonicalRecipe,
		*OutMetadata.PrimitiveRegistryVersion,
		*OutMetadata.CapabilityHash,
		*OutMetadata.CompilerVersion
	);
	OutMetadata.CompiledRecipeHash = FString::Printf(
		TEXT("md5:%s"),
		*FMD5::HashAnsiString(*CompiledHashInput)
	);

	OutPlan.Version = TEXT("1.0");
	OutPlan.Intent = NormalizedRecipe.Intent;
	OutPlan.Clip.ClipId = FString::Printf(
		TEXT("recipe_%s"),
		*OutMetadata.CompiledRecipeHash.RightChop(4).Left(16)
	);
	OutPlan.Clip.Duration =
		static_cast<float>(NormalizedRecipe.DurationSeconds);
	OutPlan.Clip.Priority = FMath::Clamp(Context.Priority, 0.0f, 1.0f);
	OutPlan.Clip.bInterruptible = true;

	// Recipe tracks own their entry and exit envelopes. A second clip envelope
	// creates derivative spikes at the blend boundary during Full Preflight.
	OutPlan.Clip.BlendIn = 0.0f;
	OutPlan.Clip.BlendOut = 0.0f;

	TArray<const FLLMNPCMotionRecipePrimitive*> OrderedPrimitives;
	OrderedPrimitives.Reserve(NormalizedRecipe.Primitives.Num());
	for (const FLLMNPCMotionRecipePrimitive& Primitive : NormalizedRecipe.Primitives)
	{
		OrderedPrimitives.Add(&Primitive);
	}
	OrderedPrimitives.Sort(
		[&Registry](
			const FLLMNPCMotionRecipePrimitive& A,
			const FLLMNPCMotionRecipePrimitive& B)
		{
			const FLLMNPCMotionPrimitiveDefinition* ADefinition =
				Registry.Find(A.PrimitiveId);
			const FLLMNPCMotionPrimitiveDefinition* BDefinition =
				Registry.Find(B.PrimitiveId);
			if (!FMath::IsNearlyEqual(
				A.StartTimeSeconds,
				B.StartTimeSeconds,
				1.e-9
			))
			{
				return A.StartTimeSeconds < B.StartTimeSeconds;
			}
			const int32 APhase =
				ADefinition ? SolverPhase(ADefinition->BodyRegion) : MAX_int32;
			const int32 BPhase =
				BDefinition ? SolverPhase(BDefinition->BodyRegion) : MAX_int32;
			if (APhase != BPhase)
			{
				return APhase < BPhase;
			}
			if (
				ADefinition &&
				BDefinition &&
				ADefinition->BodyRegion != BDefinition->BodyRegion
			)
			{
				return ADefinition->BodyRegion.LexicalLess(
					BDefinition->BodyRegion
				);
			}
			if (A.PrimitiveId != B.PrimitiveId)
			{
				return A.PrimitiveId.LexicalLess(B.PrimitiveId);
			}
			return A.SourceIndex < B.SourceIndex;
		}
	);

	for (const FLLMNPCMotionRecipePrimitive* Primitive : OrderedPrimitives)
	{
		const FLLMNPCMotionPrimitiveDefinition* Definition =
			Primitive ? Registry.Find(Primitive->PrimitiveId) : nullptr;
		if (
			!Primitive ||
			!Definition ||
			!CompilePrimitive(
				*Primitive,
				*Definition,
				Context,
				OutPlan.Clip,
				OutMetadata,
				OutError
			)
		)
		{
			if (OutError.IsEmpty())
			{
				OutError = TEXT("LLMNPC_RECIPE_COMPILER_INTERNAL_ERROR");
			}
			OutPlan = FLLMMotionPlan();
			OutMetadata = FLLMNPCCompiledRecipeMetadata();
			return false;
		}
	}

	ULLMNPCMotionValidator* MotionValidator =
		NewObject<ULLMNPCMotionValidator>();
	MotionValidator->Manifest = Context.ControlManifest;
	MotionValidator->MaxClipDuration = static_cast<float>(
		FMath::Min(
			static_cast<double>(
				CapabilitySnapshot.GlobalLimits.MaxActionDurationSeconds
			),
			static_cast<double>(
				LLMNPCMotionRecipe::DefaultMaxDurationSeconds
			)
		)
	);
	MotionValidator->MaxTracks = 32;
	const FLLMMotionPlan BeforeValidation = OutPlan;
	const FLLMMotionValidationResult MotionValidation =
		MotionValidator->ValidateAndClamp(
			OutPlan,
			ELLMNPCMotionValidationSource::PublishedTemplate
		);
	if (!MotionValidation.bValid)
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_RECIPE_MOTION_INVALID:%s"),
			*MotionValidation.ErrorMessage
		);
		OutPlan = FLLMMotionPlan();
		OutMetadata = FLLMNPCCompiledRecipeMetadata();
		return false;
	}
	if (MotionPlanWasRewritten(BeforeValidation, OutPlan))
	{
		OutError = TEXT("LLMNPC_RECIPE_COMPILER_MOTION_CLAMP_REQUIRED");
		OutPlan = FLLMMotionPlan();
		OutMetadata = FLLMNPCCompiledRecipeMetadata();
		return false;
	}
	return true;
}
