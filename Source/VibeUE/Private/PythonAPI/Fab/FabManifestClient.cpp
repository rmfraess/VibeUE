// Copyright Buckley Builds LLC 2026 All Rights Reserved.

// Compiled out when the engine install lacks the Fab plugin (issue #525) — see VibeUE.Build.cs.
#if WITH_VIBEUE_FAB

#include "FabManifestClient.h"
#include "FabEndpoints.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "HttpManager.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/IBuildManifest.h"
#include "Interfaces/IBuildPatchServicesModule.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

FString FFabDownloadInfo::ToDownloadUrl() const
{
	// FFabDownloadRequest (BuildPatch) expects "manifestURL,baseURL[,baseURL2,...]" split on the FIRST comma.
	FString Url = ManifestOrFileUrl;
	for (const FString& Base : BaseUrls)
	{
		Url += TEXT(",");
		Url += Base;
	}
	return Url;
}

TSharedPtr<FJsonObject> FFabManifestInspection::ToJsonObject() const
{
	TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("metadata_fidelity"), TEXT("manifest"));
	Object->SetNumberField(TEXT("manifest_bytes"), static_cast<double>(ManifestBytes));
	Object->SetNumberField(TEXT("build_size_bytes"), static_cast<double>(BuildSizeBytes));
	Object->SetNumberField(TEXT("download_size_bytes"), static_cast<double>(DownloadSizeBytes));
	Object->SetNumberField(TEXT("total_files"), TotalFiles);
	Object->SetNumberField(TEXT("returned_files"), Files.Num());
	Object->SetBoolField(TEXT("manifest_metadata_only"), true);
	Object->SetNumberField(TEXT("payload_chunk_requests"), 0);

	TArray<TSharedPtr<FJsonValue>> JsonFiles;
	JsonFiles.Reserve(Files.Num());
	for (const FFile& File : Files)
	{
		TSharedPtr<FJsonObject> JsonFile = MakeShared<FJsonObject>();
		JsonFile->SetStringField(TEXT("path"), File.Path);
		JsonFile->SetNumberField(TEXT("size_bytes"), static_cast<double>(File.SizeBytes));
		JsonFile->SetStringField(TEXT("type_hint"), File.TypeHint);
		JsonFile->SetStringField(TEXT("type_inference"), TEXT("heuristic"));
		JsonFiles.Add(MakeShared<FJsonValueObject>(JsonFile));
	}
	Object->SetArrayField(TEXT("files"), MoveTemp(JsonFiles));
	return Object;
}

namespace
{
	bool SyncPost(const FString& Url, const FString& Bearer, const FString& Body,
	              double TimeoutSeconds, int32& OutCode, FString& OutBody)
	{
		struct FState { bool bDone = false; bool bOk = false; int32 Code = 0; FString Body; };
		TSharedPtr<FState, ESPMode::ThreadSafe> St = MakeShared<FState, ESPMode::ThreadSafe>();

		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
		Req->SetVerb(TEXT("POST"));
		Req->SetURL(Url);
		Req->SetHeader(TEXT("accept"), TEXT("application/json"));
		Req->SetHeader(TEXT("content-type"), TEXT("application/json"));
		Req->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *Bearer));
		Req->SetContentAsString(Body);
		Req->OnProcessRequestComplete().BindLambda(
			[St](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
			{
				St->bDone = true; St->bOk = bOk;
				if (bOk && Resp.IsValid()) { St->Code = Resp->GetResponseCode(); St->Body = Resp->GetContentAsString(); }
			});
		Req->ProcessRequest();

		const double Start = FPlatformTime::Seconds();
		while (!St->bDone && (FPlatformTime::Seconds() - Start) < TimeoutSeconds)
		{
			FHttpModule::Get().GetHttpManager().Tick(0.f);
			FPlatformProcess::Sleep(0.01f);
		}
		if (!St->bDone) { Req->CancelRequest(); OutCode = 0; return false; }
		OutCode = St->Code; OutBody = St->Body; return St->bOk;
	}

	bool SyncGetBytes(const FString& Url, double TimeoutSeconds, int32& OutCode, TArray<uint8>& OutData)
	{
		struct FState
		{
			bool bDone = false;
			bool bOk = false;
			int32 Code = 0;
			TArray<uint8> Data;
		};
		TSharedPtr<FState, ESPMode::ThreadSafe> State = MakeShared<FState, ESPMode::ThreadSafe>();

		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
		Request->SetVerb(TEXT("GET"));
		Request->SetURL(Url);
		Request->OnProcessRequestComplete().BindLambda(
			[State](FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
			{
				State->bDone = true;
				State->bOk = bSucceeded;
				if (bSucceeded && Response.IsValid())
				{
					State->Code = Response->GetResponseCode();
					State->Data = Response->GetContent();
				}
			});
		Request->ProcessRequest();

		const double Start = FPlatformTime::Seconds();
		while (!State->bDone && FPlatformTime::Seconds() - Start < TimeoutSeconds)
		{
			FHttpModule::Get().GetHttpManager().Tick(0.f);
			FPlatformProcess::Sleep(0.01f);
		}
		if (!State->bDone)
		{
			Request->CancelRequest();
			OutCode = 0;
			OutData.Reset();
			return false;
		}
		OutCode = State->Code;
		OutData = MoveTemp(State->Data);
		return State->bOk;
	}
}

bool FVibeFabManifest::ParseDownloadResponse(const FString& Response, FFabDownloadInfo& OutInfo, FString& OutError)
{
	OutInfo = FFabDownloadInfo();
	OutError.Reset();
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("Manifest response was not valid JSON.");
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* DownloadInfo = nullptr;
	if (!Root->TryGetArrayField(TEXT("downloadInfo"), DownloadInfo) || DownloadInfo->Num() == 0)
	{
		OutError = TEXT("Manifest response had no downloadInfo entries.");
		return false;
	}

	const TSharedPtr<FJsonObject>* InfoObject = nullptr;
	if (!(*DownloadInfo)[0]->TryGetObject(InfoObject) || !InfoObject->IsValid())
	{
		OutError = TEXT("Malformed downloadInfo entry.");
		return false;
	}
	const TSharedPtr<FJsonObject>& Info = *InfoObject;
	Info->TryGetStringField(TEXT("type"), OutInfo.RawType);
	Info->TryGetStringField(TEXT("assetFormat"), OutInfo.AssetFormat);
	Info->TryGetStringField(TEXT("buildVersion"), OutInfo.BuildVersion);
	if (OutInfo.RawType != TEXT("manifest"))
	{
		OutError = FString::Printf(
			TEXT("Unsupported download type '%s' (only BuildPatch 'manifest' assets are supported so far)."),
			*OutInfo.RawType);
		return false;
	}
	OutInfo.bIsBuildPatch = true;

	const TArray<TSharedPtr<FJsonValue>>* Points = nullptr;
	if (Info->TryGetArrayField(TEXT("distributionPoints"), Points) && Points->Num() > 0)
	{
		const TSharedPtr<FJsonObject>* FirstPoint = nullptr;
		if ((*Points)[0]->TryGetObject(FirstPoint) && FirstPoint->IsValid())
		{
			(*FirstPoint)->TryGetStringField(TEXT("manifestUrl"), OutInfo.ManifestOrFileUrl);
		}
	}
	if (OutInfo.ManifestOrFileUrl.IsEmpty())
	{
		OutError = TEXT("Manifest response had no signed manifest location.");
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Bases = nullptr;
	if (Info->TryGetArrayField(TEXT("distributionPointBaseUrls"), Bases))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Bases)
		{
			FString Base;
			if (Value.IsValid() && Value->TryGetString(Base) && !Base.IsEmpty())
			{
				OutInfo.BaseUrls.Add(MoveTemp(Base));
			}
		}
	}
	if (OutInfo.BaseUrls.Num() == 0)
	{
		OutError = TEXT("Manifest response had no distribution base locations.");
		return false;
	}
	return true;
}

bool FVibeFabManifest::Fetch(const FString& BaseUrl, const FString& ArtifactId, const FString& AssetNamespace,
                             const FString& AssetId, const FString& Platform, const FString& BearerToken,
                             double TimeoutSeconds, FFabDownloadInfo& OutInfo, int32& OutHttpCode, FString& OutError)
{
	if (ArtifactId.IsEmpty() || AssetNamespace.IsEmpty() || AssetId.IsEmpty())
	{
		OutError = TEXT("Missing artifactId / namespace / itemId for the manifest request.");
		return false;
	}

	const FString Url = VibeUE::Fab::ArtifactManifestUrl(BaseUrl, ArtifactId);
	const FString Body = FString::Printf(
		TEXT("{\"namespace\":\"%s\",\"itemId\":\"%s\",\"platform\":\"%s\"}"),
		*AssetNamespace, *AssetId, *(Platform.IsEmpty() ? FString(TEXT("Windows")) : Platform));

	FString Resp;
	if (!SyncPost(Url, BearerToken, Body, TimeoutSeconds, OutHttpCode, Resp) || OutHttpCode < 200 || OutHttpCode >= 300)
	{
		// Never include the response body: unofficial endpoint failures can echo signed locations or identity data.
		OutError = FString::Printf(TEXT("Manifest request failed (HTTP %d)."), OutHttpCode);
		return false;
	}
	return ParseDownloadResponse(Resp, OutInfo, OutError);
}

bool FVibeFabManifest::ParseBuildPatchData(const TArray<uint8>& ManifestData, int32 MaxFiles,
	FFabManifestInspection& OutInspection, FString& OutError)
{
	OutInspection = FFabManifestInspection();
	OutError.Reset();
	if (ManifestData.Num() == 0)
	{
		OutError = TEXT("BuildPatch manifest was empty.");
		return false;
	}
	IBuildPatchServicesModule* BuildPatchServices =
		FModuleManager::LoadModulePtr<IBuildPatchServicesModule>(TEXT("BuildPatchServices"));
	if (BuildPatchServices == nullptr)
	{
		OutError = TEXT("BuildPatchServices module is unavailable.");
		return false;
	}
	IBuildManifestPtr Manifest = BuildPatchServices->MakeManifestFromData(ManifestData);
	if (!Manifest.IsValid())
	{
		OutError = TEXT("BuildPatch manifest data was malformed or unsupported.");
		return false;
	}

	TArray<FString> Paths = Manifest->GetBuildFileList();
	Paths.Sort();
	OutInspection.ManifestBytes = ManifestData.Num();
	OutInspection.BuildSizeBytes = Manifest->GetBuildSize();
	OutInspection.DownloadSizeBytes = Manifest->GetDownloadSize();
	OutInspection.TotalFiles = Paths.Num();
	const int32 ReturnCount = MaxFiles <= 0 ? Paths.Num() : FMath::Min(MaxFiles, Paths.Num());
	OutInspection.Files.Reserve(ReturnCount);
	for (int32 Index = 0; Index < ReturnCount; ++Index)
	{
		FFabManifestInspection::FFile File;
		File.Path = Paths[Index].Replace(TEXT("\\"), TEXT("/"));
		File.SizeBytes = Manifest->GetFileSize(Paths[Index]);
		const FString Extension = FPaths::GetExtension(Paths[Index], true).ToLower();
		if (Extension == TEXT(".umap"))
		{
			File.TypeHint = TEXT("unreal-map");
		}
		else if (Extension == TEXT(".uasset"))
		{
			File.TypeHint = TEXT("unreal-package");
		}
		else
		{
			File.TypeHint = Extension.IsEmpty() ? TEXT("file") : Extension.RightChop(1);
		}
		OutInspection.Files.Add(MoveTemp(File));
	}
	return true;
}

bool FVibeFabManifest::InspectBuildPatch(const FFabDownloadInfo& Info, double TimeoutSeconds, int32 MaxFiles,
	FFabManifestInspection& OutInspection, int32& OutHttpCode, FString& OutError)
{
	OutHttpCode = 0;
	if (!Info.bIsBuildPatch || Info.ManifestOrFileUrl.IsEmpty())
	{
		OutError = TEXT("No BuildPatch manifest location was resolved.");
		return false;
	}
	TArray<uint8> ManifestData;
	if (!SyncGetBytes(Info.ManifestOrFileUrl, TimeoutSeconds, OutHttpCode, ManifestData)
		|| OutHttpCode < 200 || OutHttpCode >= 300)
	{
		OutError = FString::Printf(TEXT("BuildPatch manifest download failed (HTTP %d)."), OutHttpCode);
		return false;
	}
	return ParseBuildPatchData(ManifestData, MaxFiles, OutInspection, OutError);
}

#endif // WITH_VIBEUE_FAB
