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

	FFabDownloadInfoRequest Request;
	Request.ListingUid = ListingId;
	Request.AssetFormat = TEXT("unreal-engine");
	if (EngineVersion.IsEmpty())
	{
		const FEngineVersion& Current = FEngineVersion::Current();
		Request.EngineVersion = FString::Printf(TEXT("UE_%u.%u"), static_cast<uint32>(Current.GetMajor()), static_cast<uint32>(Current.GetMinor()));
	}
	else
	{
		Request.EngineVersion = EngineVersion.StartsWith(TEXT("UE_")) ? EngineVersion : TEXT("UE_") + EngineVersion;
	}

	struct FResolveState
	{
		bool bDone = false;
		bool bOk = false;
		FFabDownloadInfo Info;
		FString Error;
	};
	const TSharedRef<FResolveState> Resolve = MakeShared<FResolveState>();
	Fab->GetDownloadInfo(Request, FFabDownloadInfoDelegate::CreateLambda(
		[Resolve](bool bOk, const FFabDownloadInfo& Info, const FString& Error)
		{
			Resolve->bDone = true;
			Resolve->bOk = bOk;
			Resolve->Info = Info;
			Resolve->Error = Error;
		}));
	if (!PumpHttpUntil(Resolve->bDone))
	{
		return ErrJson(TEXT("MANIFEST_RESOLUTION_TIMEOUT"),
			TEXT("Fab did not answer in time. After an editor start Fab restores its sign-in on the first Fab call, which this blocking call cannot drive: run FabToolset.SearchFabMyLibrary once, then retry."));
	}
	if (!Resolve->bOk)
	{
		return ErrJson(TEXT("MANIFEST_RESOLUTION_FAILED"), Resolve->Error);
	}
	const FFabResolvedDownload* Download = Resolve->Info.Downloads.FindByPredicate(
		[](const FFabResolvedDownload& Candidate) { return Candidate.IsManifest(); });
	if (Download == nullptr || Download->DownloadUrl.IsEmpty())
	{
		return ErrJson(TEXT("NOT_A_MANIFEST"), TEXT("Fab resolved this listing to a plain file, not a BuildPatch manifest."));
	}

	// The signed manifest location stays in this function: it is fetched, never returned.
	struct FFetchState
	{
		bool bDone = false;
		int32 Code = 0;
		TArray<uint8> Data;
	};
	const TSharedRef<FFetchState, ESPMode::ThreadSafe> Fetch = MakeShared<FFetchState, ESPMode::ThreadSafe>();
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->SetURL(Download->DownloadUrl);
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
	Object->SetStringField(TEXT("product_id"), Resolve->Info.ItemId);
	Object->SetStringField(TEXT("listing_id"), ListingId);
	Object->SetStringField(TEXT("artifact_id"), Download->ArtifactId);
	Object->SetStringField(TEXT("engine_version"), Request.EngineVersion);
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
