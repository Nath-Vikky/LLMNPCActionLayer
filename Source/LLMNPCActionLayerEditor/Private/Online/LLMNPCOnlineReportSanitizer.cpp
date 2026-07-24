#include "Online/LLMNPCOnlineReportSanitizer.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
TSharedPtr<FJsonValue> SanitizeValue(const TSharedPtr<FJsonValue>& Value);

TSharedRef<FJsonObject> SanitizeObject(const TSharedRef<FJsonObject>& Source)
{
	TSharedRef<FJsonObject> Sanitized = MakeShared<FJsonObject>();
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Source->Values)
	{
		if (FLLMNPCOnlineReportSanitizer::IsForbiddenFieldName(Pair.Key))
		{
			continue;
		}
		if (TSharedPtr<FJsonValue> SanitizedValue = SanitizeValue(Pair.Value))
		{
			Sanitized->SetField(Pair.Key, MoveTemp(SanitizedValue));
		}
	}
	return Sanitized;
}

TSharedPtr<FJsonValue> SanitizeValue(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid())
	{
		return nullptr;
	}

	switch (Value->Type)
	{
	case EJson::None:
	case EJson::Null:
		return MakeShared<FJsonValueNull>();
	case EJson::String:
		{
			const FString Text = Value->AsString();
			return Text.TrimStart().StartsWith(TEXT("Bearer "), ESearchCase::IgnoreCase)
				? MakeShared<FJsonValueString>(TEXT("[REDACTED]"))
				: MakeShared<FJsonValueString>(Text);
		}
	case EJson::Number:
		return MakeShared<FJsonValueNumber>(Value->AsNumber());
	case EJson::Boolean:
		return MakeShared<FJsonValueBoolean>(Value->AsBool());
	case EJson::Array:
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			for (const TSharedPtr<FJsonValue>& Entry : Value->AsArray())
			{
				if (TSharedPtr<FJsonValue> SanitizedEntry = SanitizeValue(Entry))
				{
					Values.Add(MoveTemp(SanitizedEntry));
				}
			}
			return MakeShared<FJsonValueArray>(MoveTemp(Values));
		}
	case EJson::Object:
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			if (!Object.IsValid())
			{
				return MakeShared<FJsonValueNull>();
			}
			return MakeShared<FJsonValueObject>(
				SanitizeObject(Object.ToSharedRef())
			);
		}
	default:
		return nullptr;
	}
}
}

bool FLLMNPCOnlineReportSanitizer::SanitizeAndSerialize(
	const TSharedRef<FJsonObject>& Source,
	FString& OutJson
)
{
	OutJson.Reset();
	const TSharedRef<FJsonObject> Sanitized = SanitizeObject(Source);
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
	return FJsonSerializer::Serialize(Sanitized, Writer);
}

bool FLLMNPCOnlineReportSanitizer::IsForbiddenFieldName(const FString& FieldName)
{
	FString Normalized = FieldName.TrimStartAndEnd().ToLower();
	Normalized.ReplaceInline(TEXT("-"), TEXT("_"));
	Normalized.ReplaceInline(TEXT(" "), TEXT("_"));

	static const TSet<FString> ForbiddenFields = {
		TEXT("authorization"),
		TEXT("authorization_header"),
		TEXT("authorizationheader"),
		TEXT("api_key"),
		TEXT("apikey"),
		TEXT("openai_api_key"),
		TEXT("openaiapikey"),
		TEXT("token"),
		TEXT("access_token"),
		TEXT("accesstoken"),
		TEXT("refresh_token"),
		TEXT("refreshtoken"),
		TEXT("secret"),
		TEXT("credential"),
		TEXT("credentials"),
		TEXT("password"),
		TEXT("headers"),
		TEXT("request_headers"),
		TEXT("response_headers"),
		TEXT("raw_request"),
		TEXT("rawrequest"),
		TEXT("raw_response"),
		TEXT("rawresponse"),
		TEXT("raw_authorization")
	};
	return ForbiddenFields.Contains(Normalized);
}
