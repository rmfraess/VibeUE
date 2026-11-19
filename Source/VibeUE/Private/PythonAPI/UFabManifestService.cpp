// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UFabManifestService.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	FString ToJsonString(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Object, Writer);
		return Out;
	}

	FString ErrJson(const FString& Code, const FString& Message)
	{
		const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetBoolField(TEXT("success"), false);
		Object->SetStringField(TEXT("error_code"), Code);
		Object->SetStringField(TEXT("error"), Message);
		return ToJsonString(Object);
	}
}

// Compiled to a stub when the engine install has no Fab plugin — see VibeUE.Build.cs.
#if !WITH_VIBEUE_FAB

FString UFabManifestService::InspectOwnedManifest(const FString&, const FString&, int32)
{
	return ErrJson(TEXT("FAB_UNAVAILABLE"), TEXT("This engine install has no Fab plugin."));
}

#else // WITH_VIBEUE_FAB

#include "FabModule.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HttpManager.h"
#include "HttpModule.h"
#include "Interfaces/IBuildManifest.h"
#include "Interfaces/IBuildPatchServicesModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/EngineVersion.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

namespace
{
	const double RequestTimeoutSeconds = 30.0;

	// Both requests complete from HTTP callbacks on this (game) thread, so ticking the HTTP manager
	// delivers them while the Python call blocks. Fab's on-demand sign-in runs on the EOS tick, which
	// this loop does not drive, so a call made before any session exists ends in the timeout.
	bool PumpHttpUntil(const bool& bDone)
	{
		const double Start = FPlatformTime::Seconds();
		while (!bDone && FPlatformTime::Seconds() - Start < RequestTimeoutSeconds)
		{
			FHttpModule::Get().GetHttpManager().Tick(0.f);
			FPlatformProcess::Sleep(0.01f);
		}
		return bDone;
	}

	struct FFetchState
	{
		bool bDone = false;
		int32 Code = 0;
		TArray<uint8> Data;
	};

	/** Blocking GET; Code stays 0 on a transport failure or timeout. */
	TSharedRef<FFetchState, ESPMode::ThreadSafe> FetchUrl(const FString& Url, const FString& BearerToken)
	{
		const TSharedRef<FFetchState, ESPMode::ThreadSafe> Fetch = MakeShared<FFetchState, ESPMode::ThreadSafe>();
		const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
		HttpRequest->SetVerb(TEXT("GET"));
		HttpRequest->SetURL(Url);
		if (!BearerToken.IsEmpty())
		{
			HttpRequest->SetHeader(TEXT("accept"), TEXT("application/json"));
			HttpRequest->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + BearerToken);
		}
		HttpRequest->OnProcessRequestComplete().BindLambda(
			[Fetch](FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
			{
				if (bSucceeded && Response.IsValid())
				{
					Fetch->Code = Response->GetResponseCode();
					Fetch->Data = Response->GetContent();
				}
				Fetch->bDone = true;
			});
		HttpRequest->ProcessRequest();
		if (!PumpHttpUntil(Fetch->bDone))
		{
			HttpRequest->CancelRequest();
		}
		return Fetch;
	}

	FString TypeHint(const FString& Path)
	{
		const FString Extension = FPaths::GetExtension(Path, true).ToLower();
		if (Extension == TEXT(".umap"))
		{
			return TEXT("unreal-map");
		}
		if (Extension == TEXT(".uasset"))
		{
			return TEXT("unreal-package");
		}
		return Extension.IsEmpty() ? TEXT("file") : Extension.RightChop(1);
	}
}

FString UFabManifestService::InspectOwnedManifest(const FString& ListingId, const FString& EngineVersion, int32 MaxFiles)
{
	if (ListingId.IsEmpty())
	{
		return ErrJson(TEXT("BAD_ARGUMENT"), TEXT("ListingId is required."));
	}
	if (MaxFiles < 0)
	{
		return ErrJson(TEXT("BAD_ARGUMENT"), TEXT("MaxFiles must be zero or positive."));
	}
	IFabModule* Fab = FModuleManager::LoadModulePtr<IFabModule>(TEXT("Fab"));
	if (Fab == nullptr)
	{
		return ErrJson(TEXT("FAB_UNAVAILABLE"), TEXT("The Fab plugin is not enabled in this project."));
	}

	FString RequestEngineVersion;
	if (EngineVersion.IsEmpty())
	{
		const FEngineVersion& Current = FEngineVersion::Current();
		RequestEngineVersion = FString::Printf(TEXT("UE_%u.%u"), static_cast<uint32>(Current.GetMajor()), static_cast<uint32>(Current.GetMinor()));
	}
	else
	{
		RequestEngineVersion = EngineVersion.StartsWith(TEXT("UE_")) ? EngineVersion : TEXT("UE_") + EngineVersion;
	}

	// 5.9 Preview 2 moved IFabModule::GetDownloadInfo into FabToolset's private client, so call its
	// /p/agents download-info route directly, signed with the token IFabModule hands out per url.
	const FString Host = Fab->GetAgentsApiHost();
	if (Host.IsEmpty())
	{
		return ErrJson(TEXT("FAB_UNAVAILABLE"), TEXT("No Fab environment is configured."));
	}
	const FString InfoUrl = Host + TEXT("/p/agents/listings/") + FGenericPlatformHttp::UrlEncode(ListingId)
		+ TEXT("/download-info?assetFormat=unreal-engine&engineVersion=") + FGenericPlatformHttp::UrlEncode(RequestEngineVersion)
		+ TEXT("&platform=") + FString(FPlatformProperties::IniPlatformName());
	const FString Token = Fab->GetAuthTokenForUrl(InfoUrl);
	if (Token.IsEmpty())
	{
		return ErrJson(TEXT("MANIFEST_RESOLUTION_FAILED"),
			TEXT("Fab is not signed in. After an editor start Fab restores its sign-in on the first Fab call, which this blocking call cannot drive: run FabToolset.SearchFabMyLibrary once, then retry."));
	}
	const TSharedRef<FFetchState, ESPMode::ThreadSafe> Info = FetchUrl(InfoUrl, Token);
	if (!Info->bDone || Info->Code == 0)
	{
		return ErrJson(TEXT("MANIFEST_RESOLUTION_TIMEOUT"), TEXT("Fab did not answer in time."));
	}
	const FString InfoBody = FString(FUTF8ToTCHAR(reinterpret_cast<const ANSICHAR*>(Info->Data.GetData()), Info->Data.Num()));
	if (Info->Code < 200 || Info->Code >= 300)
	{
		// Error bodies only; a success body carries signed urls and is never returned.
		return ErrJson(TEXT("MANIFEST_RESOLUTION_FAILED"), FString::Printf(TEXT("Fab download-info HTTP %d: %s"), Info->Code, *InfoBody.Left(500)));
	}
	TSharedPtr<FJsonObject> InfoJson;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(InfoBody), InfoJson) || !InfoJson.IsValid())
	{
		return ErrJson(TEXT("MANIFEST_RESOLUTION_FAILED"), TEXT("Fab returned a download-info response that is not JSON."));
	}
	const FString ItemId = InfoJson->GetStringField(TEXT("itemId"));
	FString ArtifactId;
	FString ManifestUrl;
	const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
	if (InfoJson->TryGetArrayField(TEXT("downloadInfo"), Entries))
	{
		for (const TSharedPtr<FJsonValue>& Entry : *Entries)
		{
			const TSharedPtr<FJsonObject>* EntryJson = nullptr;
			if (Entry.IsValid() && Entry->TryGetObject(EntryJson) && (*EntryJson)->GetStringField(TEXT("type")) == TEXT("manifest"))
			{
				ArtifactId = (*EntryJson)->GetStringField(TEXT("artifactId"));
				ManifestUrl = (*EntryJson)->GetStringField(TEXT("downloadUrl"));
				break;
			}
		}
	}
	if (ManifestUrl.IsEmpty())
	{
		return ErrJson(TEXT("NOT_A_MANIFEST"), TEXT("Fab resolved this listing to a plain file, not a BuildPatch manifest."));
	}
	if (!ManifestUrl.StartsWith(TEXT("https://")))
	{
		// Same rule as Fab's own parser: a signed url never travels in plaintext.
		return ErrJson(TEXT("MANIFEST_RESOLUTION_FAILED"), TEXT("Fab returned a manifest location this editor will not fetch from."));
	}

	// The signed manifest location stays in this function: it is fetched, never returned.
	const TSharedRef<FFetchState, ESPMode::ThreadSafe> Fetch = FetchUrl(ManifestUrl, FString());
	if (Fetch->Code < 200 || Fetch->Code >= 300 || Fetch->Data.Num() == 0)
	{
		return ErrJson(TEXT("MANIFEST_DOWNLOAD_FAILED"),
			FString::Printf(TEXT("BuildPatch manifest download failed (HTTP %d)."), Fetch->Code));
	}

	IBuildPatchServicesModule* BuildPatchServices =
		FModuleManager::LoadModulePtr<IBuildPatchServicesModule>(TEXT("BuildPatchServices"));
	const IBuildManifestPtr Manifest = BuildPatchServices != nullptr
		? BuildPatchServices->MakeManifestFromData(Fetch->Data)
		: IBuildManifestPtr();
	if (!Manifest.IsValid())
	{
		return ErrJson(TEXT("MANIFEST_PARSE_FAILED"), TEXT("BuildPatch manifest data was malformed or unsupported."));
	}

	TArray<FString> Paths = Manifest->GetBuildFileList();
	Paths.Sort();
	const int32 ReturnCount = MaxFiles == 0 ? Paths.Num() : FMath::Min(MaxFiles, Paths.Num());
	TArray<TSharedPtr<FJsonValue>> Files;
	Files.Reserve(ReturnCount);
	for (int32 Index = 0; Index < ReturnCount; ++Index)
	{
		const TSharedRef<FJsonObject> File = MakeShared<FJsonObject>();
		File->SetStringField(TEXT("path"), Paths[Index].Replace(TEXT("\\"), TEXT("/")));
		File->SetNumberField(TEXT("size_bytes"), static_cast<double>(Manifest->GetFileSize(Paths[Index])));
		File->SetStringField(TEXT("type_hint"), TypeHint(Paths[Index]));
		File->SetStringField(TEXT("type_inference"), TEXT("heuristic"));
		Files.Add(MakeShared<FJsonValueObject>(File));
	}

	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetBoolField(TEXT("success"), true);
	Object->SetStringField(TEXT("product_id"), ItemId);
	Object->SetStringField(TEXT("listing_id"), ListingId);
	Object->SetStringField(TEXT("artifact_id"), ArtifactId);
	Object->SetStringField(TEXT("engine_version"), RequestEngineVersion);
	Object->SetStringField(TEXT("metadata_fidelity"), TEXT("manifest"));
	Object->SetNumberField(TEXT("manifest_bytes"), Fetch->Data.Num());
	Object->SetNumberField(TEXT("build_size_bytes"), static_cast<double>(Manifest->GetBuildSize()));
	Object->SetNumberField(TEXT("download_size_bytes"), static_cast<double>(Manifest->GetDownloadSize()));
	Object->SetNumberField(TEXT("total_files"), Paths.Num());
	Object->SetNumberField(TEXT("returned_files"), ReturnCount);
	Object->SetBoolField(TEXT("manifest_metadata_only"), true);
	Object->SetNumberField(TEXT("payload_chunk_requests"), 0);
	Object->SetArrayField(TEXT("files"), MoveTemp(Files));
	return ToJsonString(Object);
}

#endif // WITH_VIBEUE_FAB
