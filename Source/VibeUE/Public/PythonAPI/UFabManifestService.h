// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "UFabManifestService.generated.h"

/**
 * Reads the file list of an owned Fab pack without downloading it.
 *
 * Epic's FabToolset searches, describes and installs listings but exposes no manifest, so this is the
 * one Fab capability VibeUE still carries. It resolves the listing through the engine Fab plugin's own
 * IFabModule::GetDownloadInfo (and so its sign-in), then parses the small BuildPatch manifest.
 *
 * Python: unreal.FabManifestService.inspect_owned_manifest(listing_id, "UE_5.7", 0)
 */
UCLASS(BlueprintType)
class VIBEUE_API UFabManifestService : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	/**
	 * Resolve the Unreal Engine format of one owned listing, download only its BuildPatch manifest, and
	 * return sanitized JSON: product_id, listing_id, artifact_id, sizes and files[] (path, size_bytes,
	 * type_hint). Never starts an install or requests payload chunks. File type hints are filename
	 * heuristics, not Asset Registry evidence. Call FabToolset.SearchFabMyLibrary first in each editor
	 * session: it restores the Fab sign-in, which this blocking call cannot do and times out without.
	 * @param ListingId     listingId from FabToolset.SearchFabMyLibrary.
	 * @param EngineVersion A version the listing offers, "UE_5.7" or "5.7": the engineVersion of its
	 *                      unreal-engine installOptions entry. Empty = the running engine.
	 * @param MaxFiles      Maximum file rows to return; 0 = all files.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Fab")
	static FString InspectOwnedManifest(const FString& ListingId, const FString& EngineVersion = TEXT(""),
	                                    int32 MaxFiles = 1000);
};
