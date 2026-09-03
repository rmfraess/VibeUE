// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/** Resolved download for an owned artifact version. */
struct FFabDownloadInfo
{
	bool bIsBuildPatch = false;         // true = BuildPatchServices manifest; false = direct HTTP file
	FString ManifestOrFileUrl;          // signed BuildPatch manifest URL, or (future) a direct file URL
	TArray<FString> BaseUrls;           // distributionPointBaseUrls (BuildPatch CloudDirectories)
	FString AssetFormat;                // e.g. "asset-format/game-engine/unreal-engine"
	FString BuildVersion;               // e.g. "5.5.0-31272837+++UE5+Dev-Marketplace-Windows"
	FString RawType;                    // downloadInfo[].type as reported ("manifest", ...)

	/** DownloadURL string for FFabDownloadRequest (BuildPatch): "manifestUrl,baseUrl1,baseUrl2,...". */
	FString ToDownloadUrl() const;
};

/** Stable, sanitized metadata parsed from one BuildPatch manifest. */
struct FFabManifestInspection
{
	struct FFile
	{
		FString Path;
		int64 SizeBytes = 0;
		FString TypeHint;
	};

	int64 ManifestBytes = 0;
	int64 BuildSizeBytes = 0;
	int64 DownloadSizeBytes = 0;
	int32 TotalFiles = 0;
	TArray<FFile> Files;

	/** Serialize only stable metadata; never includes source URLs, CDN data, identity, or auth. */
	TSharedPtr<FJsonObject> ToJsonObject() const;
};

/**
 * Resolves an owned artifact into a concrete download — the step the engine Fab plugin performs in
 * fab.com JS, not C++. Verified live: POST {base}/e/artifacts/{artifactId}/manifest with body
 * {"namespace":<assetNamespace>,"itemId":<assetId UUID4 hex>,"platform":"Windows"} returns
 * downloadInfo[] with distributionPoints[].manifestUrl (signed) + distributionPointBaseUrls[].
 * Unofficial/reverse-engineered — see docs/design/fab-service-spec.md.
 */
class FVibeFabManifest
{
public:
	/** Parse the artifact endpoint response. Signed locations remain transient in OutInfo. */
	static bool ParseDownloadResponse(const FString& Response, FFabDownloadInfo& OutInfo, FString& OutError);

	/**
	 * @param Platform e.g. "Windows".
	 * @return true on success with OutInfo populated. On a non-BuildPatch asset format, returns false
	 *         with OutError describing the unsupported format (OutInfo.RawType carries the type).
	 */
	static bool Fetch(const FString& BaseUrl, const FString& ArtifactId, const FString& AssetNamespace,
	                  const FString& AssetId, const FString& Platform, const FString& BearerToken,
	                  double TimeoutSeconds, FFabDownloadInfo& OutInfo, int32& OutHttpCode, FString& OutError);

	/** Parse already-fetched manifest bytes without creating or starting an installer. */
	static bool ParseBuildPatchData(const TArray<uint8>& ManifestData, int32 MaxFiles,
	                                FFabManifestInspection& OutInspection, FString& OutError);

	/** Fetch and parse only the BuildPatch manifest referenced by Info; no chunk/payload requests. */
	static bool InspectBuildPatch(const FFabDownloadInfo& Info, double TimeoutSeconds, int32 MaxFiles,
	                              FFabManifestInspection& OutInspection, int32& OutHttpCode, FString& OutError);
};
