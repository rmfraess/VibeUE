// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UAssetDiscoveryService.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "EditorAssetLibrary.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "EditorFramework/AssetImportData.h"
#include "Engine/Texture2D.h"
#include "HAL/PlatformFileManager.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Factories/TextureFactory.h"
#include "EditorReimportHandler.h"
#include "UObject/Package.h"
#include "ObjectTools.h"
#include "UObject/ReferencerFinder.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UObjectGlobals.h"
#include "BlueprintActionDatabase.h"   // A13 false-refusal fix: clear transient node spawners like the engine's own delete path
#include "BlueprintAssetHandler.h"     // A13: engine fallback for a non-Blueprint asset that still owns a UBlueprint
#include "Engine/Blueprint.h"          // A13: complete UBlueprint type for the UBlueprint* -> UObject* base conversion above
#include "UObject/GCObject.h"          // A13: UGCObjectReferencer — the native GC root a Python global appears as (refusal signal)
#include "AssetImportTask.h"           // Meshes and other non-image files through AssetImportTask
#include "Async/TaskGraphInterfaces.h" // IsThreadProcessingTasks, the RecursionGuard check
#include "Engine/StaticMesh.h"         // ranking what a multi-asset import returns
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"    // wait out in-flight async compression before force-deleting a clip
#include "PhysicsEngine/PhysicsAsset.h"
#include "Materials/MaterialInterface.h"
#include "UObject/SoftObjectPath.h"
#include "HAL/FileManager.h"                // P16: confirm the package file really left the disk
#include "Editor/Transactor.h"              // P16: UTransactor::GetQueueLength, to report the engine's undo reset

namespace
{

}

bool UAssetDiscoveryService::ReimportAsset(
	const FString& AssetPath,
	const FString& NewSourcePath,
	FString& OutSourceFileUsed,
	FString& OutError)
{
	OutSourceFileUsed.Empty();
	OutError.Empty();

	if (AssetPath.IsEmpty())
	{
		OutError = TEXT("AssetPath is required");
		return false;
	}

	if (!UEditorAssetLibrary::DoesAssetExist(AssetPath))
	{
		OutError = FString::Printf(TEXT("Asset was not found: %s"), *AssetPath);
		return false;
	}

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset)
	{
		OutError = FString::Printf(TEXT("Asset was not found or could not be loaded: %s"), *AssetPath);
		return false;
	}

	FReimportManager* ReimportManager = FReimportManager::Instance();
	if (!ReimportManager)
	{
		OutError = TEXT("Unreal's reimport manager is unavailable");
		return false;
	}

	FString ReplacementSource;
	if (!NewSourcePath.IsEmpty())
	{
		ReplacementSource = FPaths::ConvertRelativePathToFull(NewSourcePath);
		FPaths::NormalizeFilename(ReplacementSource);
		if (!FPaths::FileExists(ReplacementSource))
		{
			OutError = FString::Printf(TEXT("New source file does not exist: %s"), *ReplacementSource);
			return false;
		}

		// Let the registered handler update the correct import-data representation. This
		// works for both Interchange and legacy factories without asset-type branching.
		ReimportManager->UpdateReimportPath(Asset, ReplacementSource, INDEX_NONE);
	}

	TArray<FString> SourceFiles;
	if (!ReimportManager->CanReimport(Asset, &SourceFiles))
	{
		OutError = FString::Printf(
			TEXT("No registered reimport handler supports asset '%s'%s"),
			*AssetPath,
			ReplacementSource.IsEmpty() ? TEXT("") : TEXT(" with the supplied source file"));
		return false;
	}

	if (SourceFiles.IsEmpty())
	{
		OutError = FString::Printf(TEXT("Asset has no stored source file: %s"), *AssetPath);
		return false;
	}

	// Report the selected source even when validation or the handler later fails. This makes
	// failure responses actionable, especially for assets whose stored source has moved.
	OutSourceFileUsed = ReplacementSource.IsEmpty()
		? UAssetImportData::ResolveImportFilename(SourceFiles[0], Asset->GetOutermost())
		: ReplacementSource;
	FPaths::NormalizeFilename(OutSourceFileUsed);

	for (const FString& SourceFile : SourceFiles)
	{
		if (SourceFile.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Asset has an empty stored source file: %s"), *AssetPath);
			return false;
		}
		FString ResolvedSourceFile = UAssetImportData::ResolveImportFilename(SourceFile, Asset->GetOutermost());
		FPaths::NormalizeFilename(ResolvedSourceFile);
		if (!FPaths::FileExists(ResolvedSourceFile))
		{
			OutError = FString::Printf(TEXT("Stored source file does not exist: %s"), *ResolvedSourceFile);
			return false;
		}
	}

	const bool bReimported = ReimportManager->Reimport(
		Asset,
		/*bAskForNewFileIfMissing=*/ false,
		/*bShowNotification=*/ false,
		/*PreferredReimportFile=*/ TEXT(""),
		/*SpecifiedReimportHandler=*/ nullptr,
		/*SourceFileIndex=*/ INDEX_NONE,
		/*bForceNewFile=*/ false,
		/*bAutomated=*/ true);

	if (!bReimported)
	{
		OutError = FString::Printf(
			TEXT("Reimport failed for asset '%s' using source file '%s'. See the Unreal log for handler details."),
			*AssetPath,
			*OutSourceFileUsed);
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("UAssetDiscoveryService::ReimportAsset: reimported '%s' from '%s'"),
		*AssetPath, *OutSourceFileUsed);
	return true;
}

// ========== Open Assets & Content Browser ==========


FUnattendedDeleteResult UAssetDiscoveryService::DeleteAssetUnattended(const FString& AssetPath, bool bForceEvenIfReferenced)
{
	FUnattendedDeleteResult Result;
	if (AssetPath.IsEmpty())
	{
		Result.ErrorMessage = TEXT("AssetPath is empty");
		return Result;
	}
	if (!UEditorAssetLibrary::DoesAssetExist(AssetPath))
	{
		Result.ErrorMessage = FString::Printf(TEXT("Asset not found: %s"), *AssetPath);
		return Result;
	}

	// P16: never delete while an editor transaction is OPEN (a ScopedEditorTransaction block that is
	// still running). The engine delete below resets the undo history itself whenever undo is an
	// asset's only holder (ObjectTools.cpp DeleteSingleObject and CleanupAfterSuccessfulDelete call
	// GEditor->ResetTransaction), and ForceReplaceReferences transacts too; with ActiveCount != 0,
	// UTransBuffer::Reset cancels the caller's open transaction out from under it. This is refused for
	// every asset, undo-held or not, before anything is touched.
	if (GEditor && GEditor->IsTransactionActive())
	{
		Result.ErrorCode = TEXT("TRANSACTION_OPEN");
		Result.ErrorMessage = FString::Printf(
			TEXT("Refusing to delete %s while an editor transaction is open (a ScopedEditorTransaction block?): the delete ")
			TEXT("can reset the undo buffer, which would cancel that transaction. Leave the transaction, then retry."),
			*AssetPath);
		UE_LOG(LogTemp, Warning, TEXT("UAssetDiscoveryService::DeleteAssetUnattended: %s"), *Result.ErrorMessage);
		return Result;
	}

	// Who points at it (the question the modal dialog would have asked the human)
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	const FString PackageName = FPackageName::ObjectPathToPackageName(AssetPath);
	TArray<FName> ReferencerNames;
	AssetRegistryModule.Get().GetReferencers(FName(*PackageName), ReferencerNames);
	for (const FName& Referencer : ReferencerNames)
	{
		const FString ReferencerString = Referencer.ToString();
		if (ReferencerString != PackageName && !ReferencerString.StartsWith(TEXT("/Temp/")) && !ReferencerString.StartsWith(TEXT("/Engine/Transient")))
		{
			Result.Referencers.Add(ReferencerString);
		}
	}
	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset)
	{
		Result.ErrorMessage = FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath);
		return Result;
	}

	// An anim sequence may still be compressing on a worker thread (the save that created
	// it a moment ago queued the build); force-deleting mid-build hands the worker a dying
	// data model and crashes in FTransform::Blend. Finish any in-flight compression first.
	if (UAnimSequence* AnimSeqToDelete = Cast<UAnimSequence>(Asset))
	{
		AnimSeqToDelete->WaitOnExistingCompression(false);
	}

	// ForceDeleteObjects can still open a read-only-package prompt even when confirmation is
	// disabled. Preflight the resolved package file so this API keeps its unattended/no-modal
	// contract instead of wedging the game thread on a source-control or filesystem attribute.
	//
	// Whether the package had a file at all is also kept for the post-delete check: an in-memory asset
	// that was never saved has nothing on disk to remove.
	FString PackageFilename;
	const bool bFileExistedBefore = FPackageName::DoesPackageExist(PackageName, &PackageFilename);
	if (bFileExistedBefore &&
		FPlatformFileManager::Get().GetPlatformFile().IsReadOnly(*PackageFilename))
	{
		Result.ErrorMessage = FString::Printf(
			TEXT("Asset package is read-only; refusing unattended delete to avoid a modal prompt: %s"),
			*PackageFilename);
		return Result;
	}

	// The registry lags a freshly saved referencer (a montage built on this clip seconds ago is
	// not in its dependency map yet), so also ask memory: every loaded asset package that holds
	// a pointer to this object counts. Transient / compiled-in outers are the Python wrapper and
	// the editor itself, not references worth refusing over.
	const TArray<UObject*> Referencees = { Asset };
	for (UObject* Referencer : FReferencerFinder::GetAllReferencers(Referencees, nullptr))
	{
		UPackage* Package = Referencer ? Referencer->GetOutermost() : nullptr;
		if (!Package || Package == Asset->GetOutermost() || Package == GetTransientPackage() || Package->HasAnyPackageFlags(PKG_CompiledIn))
		{
			continue;
		}
		const FString ReferencerPackage = Package->GetName();
		if (ReferencerPackage.StartsWith(TEXT("/Game/")) || ReferencerPackage.StartsWith(TEXT("/Engine/")) || FPackageName::IsValidLongPackageName(ReferencerPackage))
		{
			if (!ReferencerPackage.StartsWith(TEXT("/Temp/")) && !ReferencerPackage.StartsWith(TEXT("/Engine/Transient")))
			{
				Result.Referencers.AddUnique(ReferencerPackage);
			}
		}
	}
	if (Result.Referencers.Num() > 0 && !bForceEvenIfReferenced)
	{
		Result.ErrorMessage = FString::Printf(TEXT("%s is referenced by %d asset(s); pass bForceEvenIfReferenced to delete anyway and clear the references"), *AssetPath, Result.Referencers.Num());
		return Result;
	}
	// Close any editor showing it first, or the delete is refused
	if (GEditor)
	{
		if (UAssetEditorSubsystem* AssetEditors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
		{
			AssetEditors->CloseAllEditorsForAsset(Asset);
		}
	}

	// A13 follow-up: mirror what the engine's own delete does BEFORE its in-memory referencer check.
	// FBlueprintActionDatabase roots a set of transient UBlueprintNodeSpawner objects (variable /
	// function / event spawners) for every loaded Blueprint, kept alive by its AddReferencedObjects
	// (BlueprintActionDatabase.cpp:1221-1238). Those spawners hold a pointer back to the Blueprint, so
	// GatherObjectReferencersForDeletion would report them and we would refuse a perfectly deletable
	// Blueprint. Inside ObjectTools::ForceDeleteObjects the engine avoids exactly this: it broadcasts
	// FEditorDelegates::OnAssetsPreDelete (ObjectTools.cpp:3978) *before* DeleteSingleObject's gather
	// (ObjectTools.cpp:3500), and FBlueprintActionDatabase::OnAssetsPendingDelete
	// (BlueprintActionDatabase.cpp:985-1014) responds by calling ClearAssetActions on the deleting
	// object. ClearAssetActions(UBlueprint) drops the single entry that holds BOTH the blueprint-graph
	// spawners and the skeleton-class member spawners (RefreshAssetActions:1656-1660). We do the same
	// here, then let the CollectGarbage below actually reap the now-unreferenced spawners before we
	// gather. On a refusal we rebuild the entry so the editor's palette is left intact.
	bool bClearedActionDatabase = false;
	if (FBlueprintActionDatabase* ActionDatabase = FBlueprintActionDatabase::TryGet())
	{
		bClearedActionDatabase = ActionDatabase->ClearAssetActions(Asset);
		if (!bClearedActionDatabase)
		{
			// A non-Blueprint asset can still own a Blueprint (matches the engine's own fallback branch,
			// BlueprintActionDatabase.cpp:1006-1013).
			if (const IBlueprintAssetHandler* Handler = FBlueprintAssetHandler::Get().FindHandler(Asset->GetClass()))
			{
				if (UBlueprint* OwnedBlueprint = Handler->RetrieveBlueprint(Asset))
				{
					bClearedActionDatabase = ActionDatabase->ClearAssetActions(OwnedBlueprint);
				}
			}
		}
	}
	// Rebuild the action-database entry we cleared, so refusing the delete does not leave the loaded
	// Blueprint's palette actions empty until its next compile/reload.
	auto RestoreActionDatabase = [&]()
	{
		if (bClearedActionDatabase && IsValid(Asset))
		{
			if (FBlueprintActionDatabase* ActionDatabase = FBlueprintActionDatabase::TryGet())
			{
				ActionDatabase->RefreshAssetActions(Asset);
			}
		}
	};

	// The refusal DECISION must be strictly NON-MUTATING. A previous shape ran ForceReplaceReferences
	// here to mirror the engine before its check; on a refusal that left the Blueprint's skeleton/generated
	// classes with a null ClassGeneratedBy, and the caller's retry (after releasing the global) then
	// crashed: "Fatal error: UBlueprintGeneratedClass::GetAuthoritativeClass: ClassGeneratedBy is null"
	// (BlueprintGeneratedClass.cpp:686). So we only LOOK before deciding, and never replace.
	//
	// ClearAssetActions above already dropped the transient Blueprint-palette node spawners (harmless —
	// mirrors the engine's OnAssetsPreDelete handler). Collect garbage the same way the engine does so a
	// global that was already del'd but not yet swept does not cause a false refusal; the asset carries
	// RF_Standalone (a GARBAGE_COLLECTION_KEEPFLAGS flag) so it survives the sweep.
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	if (!IsValid(Asset))
	{
		Result.ErrorMessage = FString::Printf(TEXT("Asset %s was garbage-collected before deletion could proceed"), *AssetPath);
		return Result;
	}

	// Look at who holds the asset WITHOUT mutating anything (default flags, no replace first). The only
	// referencer that would make the engine's force delete stall on its modal "is in use" dialog is a
	// NATIVE GC root holding the asset directly — a UGCObjectReferencer, which is what a Python
	// module-level global manifests as. Everything else the engine's own force delete clears without
	// prompting: on-disk asset references (it nulls them via ForceReplaceReferences), the action
	// database's transient node spawners, and transient editor helpers like AnimSequencerController
	// (unrooted once the reflected refs are gone). Observed in the editor (2026-09-12): the held
	// duplicated Blueprint listed GCObjectReferencer + 2 node spawners (refuse); after releasing the
	// Python global it listed only the 2 spawners (deletes fine); an anim sequence listed only its
	// AnimSequencerController (deletes fine). So the conservative, correct signal is: refuse iff some
	// external referencer is a UGCObjectReferencer.
	bool bIsReferencedByUndo = false;
	{
		FReferencerInformationList Refs;
		bool bIsReferenced = false;
		ObjectTools::GatherObjectReferencersForDeletion(Asset, bIsReferenced, bIsReferencedByUndo, &Refs);

		TArray<FString> NativeRoots;
		for (const FReferencerInformation& Info : Refs.ExternalReferences)
		{
			if (Info.Referencer && Info.Referencer->IsA<UGCObjectReferencer>())
			{
				NativeRoots.AddUnique(Info.Referencer->GetFullName());
			}
		}

		if (NativeRoots.Num() > 0)
		{
			for (const FString& Ref : NativeRoots)
			{
				Result.Referencers.AddUnique(Ref);
			}
			Result.ErrorMessage = FString::Printf(
				TEXT("%s is held by %d native GC root(s) (e.g. %s); in an unattended session that is a Python ")
				TEXT("module-level global that created or loaded the asset. The engine's force delete would stall ")
				TEXT("on its modal 'is in use' dialog, so this refuses instead of deleting. Release the Python ")
				TEXT("globals holding it (del them, then unreal.SystemLibrary.collect_garbage()) and retry."),
				*AssetPath, NativeRoots.Num(), *NativeRoots.Last());
			UE_LOG(LogTemp, Warning, TEXT("UAssetDiscoveryService::DeleteAssetUnattended: %s"), *Result.ErrorMessage);
			RestoreActionDatabase();
			return Result;
		}
	}

	// P16: the undo buffer. An asset edited inside a committed editor transaction is held by the undo
	// history. That alone does not stop the delete: when undo is the asset's ONLY holder the engine's own
	// delete resets the undo history and deletes it fully (ObjectTools.cpp DeleteSingleObject and
	// CleanupAfterSuccessfulDelete -> GEditor->ResetTransaction), and it always did so through this
	// function. So proceed, but say so: the caller loses its undo history. The case that really leaves a
	// file behind is undo PLUS a second holder (the 2026-10-21 SM_Rails copy): the engine then keeps the
	// package, and the post-delete file check below reports it as FILE_NOT_REMOVED.
	const bool bUndoHeld = bIsReferencedByUndo || (GEditor && GEditor->IsObjectInTransactionBuffer(Asset));
	const int32 UndoQueueLengthBefore = (GEditor && GEditor->Trans) ? GEditor->Trans->GetQueueLength() : 0;
	if (bUndoHeld)
	{
		UE_LOG(LogTemp, Warning, TEXT("delete_asset_unattended: '%s' is in the undo buffer; the engine resets the undo history to delete it"), *AssetPath);
	}

	// No native root holds it: hand the deletion to the engine's real force delete, exactly as the
	// pre-A13 code did. ForceDeleteObjects nulls the reflected references itself (on-disk data-asset
	// pointers etc.), reparents child Blueprints, removes child redirectors / generated classes and
	// reinstances UUserDefinedStructs, and clears the transient helpers — none of which makes it prompt,
	// because nothing rooted-and-native holds the asset (we just checked).
	TArray<UObject*> Objects;
	Objects.Add(Asset);
	const int32 Deleted = ObjectTools::ForceDeleteObjects(Objects, /*bShowConfirmation*/ false);

	// Whether the engine's delete really reset the undo history, measured around ForceDeleteObjects itself so
	// EVERY return below reports it truthfully, failures included: the reset happens inside the delete
	// (DeleteSingleObject / CleanupAfterSuccessfulDelete), before the outcome checks. Read from the queue
	// length, not the asset: the asset may already be garbage-collected here. The Warning above names the
	// reset in advance; when the queue did not go to 0, the result says what happened instead.
	const int32 UndoQueueLengthAfter = (GEditor && GEditor->Trans) ? GEditor->Trans->GetQueueLength() : 0;
	Result.bUndoBufferReset = GEditor && GEditor->Trans && UndoQueueLengthBefore > 0 && UndoQueueLengthAfter == 0;
	if (Result.bUndoBufferReset)
	{
		Result.Message = bUndoHeld
			? FString(TEXT("The asset was in the undo buffer, and the engine reset the undo history during the delete: earlier edits can no longer be undone."))
			: FString(TEXT("The engine reset the undo history during the delete: earlier edits can no longer be undone."));
	}
	else if (bUndoHeld)
	{
		Result.Message = FString::Printf(
			TEXT("The asset was in the undo buffer, but the engine did NOT reset the undo history (queue length %d before the delete, %d after): ")
			TEXT("earlier edits are still undoable, and the undo buffer may still hold the asset."),
			UndoQueueLengthBefore, UndoQueueLengthAfter);
	}

	if (Deleted <= 0)
	{
		Result.ErrorMessage = FString::Printf(TEXT("ForceDeleteObjects returned 0 for %s (system veto; see the log)"), *AssetPath);
		UE_LOG(LogTemp, Warning, TEXT("UAssetDiscoveryService::DeleteAssetUnattended: %s"), *Result.ErrorMessage);
		RestoreActionDatabase();
		return Result;
	}

	// P16: "deleted" must mean the file is gone too. The engine skips the file of a package something
	// still holds (CleanupAfterSuccessfulDelete culls referenced packages), and a file left behind brings
	// the asset back on the next editor start. Check the file resolved BEFORE the delete; a package that
	// never had one (an in-memory asset that was never saved) is reported as such, not as removed.
	if (bFileExistedBefore)
	{
		if (IFileManager::Get().FileExists(*PackageFilename))
		{
			Result.FileStatus = TEXT("still_on_disk");
			Result.ErrorCode = TEXT("FILE_NOT_REMOVED");
			Result.ErrorMessage = FString::Printf(
				TEXT("%s was deleted in memory and from the asset registry, but its package file is still on disk (%s): something ")
				TEXT("still held the package, and the asset will come back on the next editor start. Delete the file with the editor closed."),
				*AssetPath, *PackageFilename);
			UE_LOG(LogTemp, Warning, TEXT("UAssetDiscoveryService::DeleteAssetUnattended: %s"), *Result.ErrorMessage);
			return Result;
		}
		Result.bFileRemoved = true;
		Result.FileStatus = TEXT("removed");
	}
	else
	{
		Result.FileStatus = TEXT("never_on_disk");
	}

	UE_LOG(LogTemp, Log, TEXT("UAssetDiscoveryService::DeleteAssetUnattended: deleted %s (%d referencer(s) reported, file: %s)"),
		*AssetPath, Result.Referencers.Num(), *Result.FileStatus);
	Result.bSuccess = true;
	return Result;
}
