// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/ULandscapeMaterialService.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionLandscapeLayerBlend.h"
#include "Materials/MaterialExpressionLandscapeLayerWeight.h"
#include "Materials/MaterialExpressionLandscapeLayerCoords.h"
#include "Materials/MaterialExpressionLandscapeLayerSample.h"
#include "Materials/MaterialExpressionLandscapeGrassOutput.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureObject.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionSmoothStep.h"
#include "LandscapeGrassType.h"
#include "MaterialGraph/MaterialGraph.h"
#include "MaterialEditingLibrary.h"
#include "LandscapeLayerInfoObject.h"
#include "Landscape.h"
#include "LandscapeInfo.h"
#include "LandscapeEdit.h"
#include "EditorAssetLibrary.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/MaterialFactoryNew.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "AssetRegistry/AssetRegistryModule.h"

// =================================================================
// Helper Methods
// =================================================================

UMaterial* ULandscapeMaterialService::LoadMaterialAsset(const FString& MaterialPath)
{
	UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(MaterialPath);
	if (!LoadedObject)
	{
		UE_LOG(LogTemp, Warning, TEXT("ULandscapeMaterialService: Failed to load material: %s"), *MaterialPath);
		return nullptr;
	}

	UMaterial* Material = Cast<UMaterial>(LoadedObject);
	if (!Material)
	{
		UE_LOG(LogTemp, Warning, TEXT("ULandscapeMaterialService: Object is not a material: %s"), *MaterialPath);
		return nullptr;
	}

	return Material;
}

UMaterialExpression* ULandscapeMaterialService::FindExpressionById(UMaterial* Material, const FString& ExpressionId)
{
	if (!Material) return nullptr;

	TArray<UMaterialExpression*> Expressions;
	Material->GetAllExpressionsInMaterialAndFunctionsOfType<UMaterialExpression>(Expressions);

	for (UMaterialExpression* Expression : Expressions)
	{
		if (GetExpressionId(Expression) == ExpressionId)
		{
			return Expression;
		}
	}

	// Try matching by index
	int32 Index = FCString::Atoi(*ExpressionId);
	if (Index >= 0 && Index < Expressions.Num())
	{
		return Expressions[Index];
	}

	return nullptr;
}

FString ULandscapeMaterialService::GetExpressionId(UMaterialExpression* Expression)
{
	if (!Expression) return TEXT("");
	return FString::Printf(TEXT("%s_%p"), *Expression->GetClass()->GetName(), Expression);
}

UMaterialExpressionLandscapeLayerBlend* ULandscapeMaterialService::FindLayerBlendNode(UMaterial* Material, const FString& NodeId)
{
	UMaterialExpression* Expr = FindExpressionById(Material, NodeId);
	if (!Expr)
	{
		return nullptr;
	}
	return Cast<UMaterialExpressionLandscapeLayerBlend>(Expr);
}

void ULandscapeMaterialService::RefreshMaterialGraph(UMaterial* Material)
{
	if (!Material) return;

	if (!IsInGameThread())
	{
		return;
	}

	Material->MarkPackageDirty();

	if (IsValid(Material))
	{
		Material->PreEditChange(nullptr);
		Material->PostEditChange();
	}

	if (Material->MaterialGraph)
	{
		if (UMaterialGraph* MaterialGraph = Cast<UMaterialGraph>(Material->MaterialGraph))
		{
			if (IsValid(MaterialGraph))
			{
				MaterialGraph->LinkMaterialExpressionsFromGraph();
				MaterialGraph->RebuildGraph();
			}
		}
	}
}

// =================================================================
// Material Creation
// =================================================================

FLandscapeMaterialCreateResult ULandscapeMaterialService::CreateLandscapeMaterial(
	const FString& MaterialName,
	const FString& DestinationPath)
{
	FLandscapeMaterialCreateResult Result;

	if (MaterialName.IsEmpty())
	{
		Result.ErrorMessage = TEXT("MaterialName cannot be empty");
		UE_LOG(LogTemp, Error, TEXT("ULandscapeMaterialService::CreateLandscapeMaterial: %s"), *Result.ErrorMessage);
		return Result;
	}

	// Check if asset already exists to avoid blocking overwrite dialog
	FString FullAssetPath = DestinationPath / MaterialName;
	if (UEditorAssetLibrary::DoesAssetExist(FullAssetPath))
	{
		Result.AssetPath = FullAssetPath;
		Result.ErrorMessage = FString::Printf(TEXT("Landscape material '%s' already exists at '%s'. Delete it first or use a different name."), *MaterialName, *FullAssetPath);
		UE_LOG(LogTemp, Error, TEXT("ULandscapeMaterialService::CreateLandscapeMaterial: %s"), *Result.ErrorMessage);
		return Result;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
	UObject* NewAsset = AssetTools.CreateAsset(MaterialName, DestinationPath, UMaterial::StaticClass(), Factory);

	UMaterial* NewMaterial = Cast<UMaterial>(NewAsset);
	if (!NewMaterial)
	{
		Result.ErrorMessage = FString::Printf(TEXT("Failed to create material '%s' at '%s'"), *MaterialName, *DestinationPath);
		UE_LOG(LogTemp, Error, TEXT("ULandscapeMaterialService::CreateLandscapeMaterial: %s"), *Result.ErrorMessage);
		return Result;
	}

	// Landscape materials use Surface domain (same as regular materials)
	// No special domain change needed - MD_Surface is the default

	Result.bSuccess = true;
	Result.AssetPath = FullAssetPath;

	UE_LOG(LogTemp, Log, TEXT("ULandscapeMaterialService::CreateLandscapeMaterial: Created landscape material '%s'"), *Result.AssetPath);
	return Result;
}

static bool ParseLayerBlendType(const FString& Name, ELandscapeLayerBlendType& Out)
{
	if (Name.Equals(TEXT("LB_WeightBlend"), ESearchCase::IgnoreCase)) { Out = LB_WeightBlend; return true; }
	if (Name.Equals(TEXT("LB_AlphaBlend"), ESearchCase::IgnoreCase)) { Out = LB_AlphaBlend; return true; }
	if (Name.Equals(TEXT("LB_HeightBlend"), ESearchCase::IgnoreCase)) { Out = LB_HeightBlend; return true; }
	return false;
}

FLandscapeLayerBlendInfo ULandscapeMaterialService::CreateLayerBlendNodeWithLayers(
	const FString& MaterialPath,
	const TArray<FLandscapeMaterialLayerConfig>& Layers,
	int32 PosX,
	int32 PosY)
{
	FLandscapeLayerBlendInfo Result;

	UMaterial* Material = LoadMaterialAsset(MaterialPath);
	if (!Material)
	{
		return Result;
	}

	// Resolve every layer's blend type before creating anything: an empty BlendType takes bUseHeightBlend,
	// and an unknown type or a contradiction fails instead of silently becoming a weight blend.
	TArray<ELandscapeLayerBlendType> BlendTypes;
	for (const FLandscapeMaterialLayerConfig& LayerConfig : Layers)
	{
		ELandscapeLayerBlendType BlendTypeEnum;
		if (LayerConfig.BlendType.IsEmpty())
		{
			BlendTypeEnum = LayerConfig.bUseHeightBlend ? LB_HeightBlend : LB_WeightBlend;
		}
		else if (!ParseLayerBlendType(LayerConfig.BlendType, BlendTypeEnum))
		{
			UE_LOG(LogTemp, Error, TEXT("ULandscapeMaterialService::CreateLayerBlendNodeWithLayers: Layer '%s' has unknown BlendType '%s' (use LB_WeightBlend, LB_AlphaBlend or LB_HeightBlend)"),
				*LayerConfig.LayerName, *LayerConfig.BlendType);
			return Result;
		}
		if (LayerConfig.bUseHeightBlend && BlendTypeEnum != LB_HeightBlend)
		{
			UE_LOG(LogTemp, Error, TEXT("ULandscapeMaterialService::CreateLayerBlendNodeWithLayers: Layer '%s' sets bUseHeightBlend but BlendType '%s'"),
				*LayerConfig.LayerName, *LayerConfig.BlendType);
			return Result;
		}
		BlendTypes.Add(BlendTypeEnum);
	}

	FScopedTransaction Transaction(NSLOCTEXT("LandscapeMaterialService", "CreateLayerBlendWithLayers", "Create Landscape Layer Blend With Layers"));
	Material->Modify();

	UMaterialExpression* NewExpression = UMaterialEditingLibrary::CreateMaterialExpression(
		Material, UMaterialExpressionLandscapeLayerBlend::StaticClass(), PosX, PosY);

	if (!NewExpression)
	{
		UE_LOG(LogTemp, Error, TEXT("ULandscapeMaterialService::CreateLayerBlendNodeWithLayers: Failed to create expression"));
		return Result;
	}

	UMaterialExpressionLandscapeLayerBlend* BlendNode = Cast<UMaterialExpressionLandscapeLayerBlend>(NewExpression);
	if (!BlendNode)
	{
		return Result;
	}

	BlendNode->Modify();

	// Add all layers in one go
	for (int32 i = 0; i < Layers.Num(); ++i)
	{
		const FLandscapeMaterialLayerConfig& LayerConfig = Layers[i];
		FLayerBlendInput NewLayer;
		NewLayer.LayerName = FName(*LayerConfig.LayerName);
		NewLayer.BlendType = BlendTypes[i];
		NewLayer.PreviewWeight = LayerConfig.PreviewWeight;
		BlendNode->Layers.Add(NewLayer);
	}

	RefreshMaterialGraph(Material);

	// Build result with all layer info
	Result.NodeId = GetExpressionId(NewExpression);
	for (const FLayerBlendInput& Layer : BlendNode->Layers)
	{
		FLandscapeMaterialLayerConfig Config;
		Config.LayerName = Layer.LayerName.ToString();
		Config.BlendType = UEnum::GetValueAsString(Layer.BlendType);
		Config.bUseHeightBlend = Layer.BlendType == LB_HeightBlend;
		Config.PreviewWeight = Layer.PreviewWeight;
		Result.Layers.Add(Config);
	}

	UE_LOG(LogTemp, Log, TEXT("ULandscapeMaterialService::CreateLayerBlendNodeWithLayers: Created node with %d layers"),
		Layers.Num());
	return Result;
}

// =================================================================
// Layer Info Object Management
// =================================================================

FLandscapeLayerInfoCreateResult ULandscapeMaterialService::CreateLayerInfoObject(
	const FString& LayerName,
	const FString& DestinationPath,
	bool bIsWeightBlended)
{
	FLandscapeLayerInfoCreateResult Result;

	if (LayerName.IsEmpty())
	{
		Result.ErrorMessage = TEXT("LayerName cannot be empty");
		UE_LOG(LogTemp, Error, TEXT("ULandscapeMaterialService::CreateLayerInfoObject: %s"), *Result.ErrorMessage);
		return Result;
	}

	// Create the layer info object asset
	FString AssetName = FString::Printf(TEXT("LI_%s"), *LayerName);

	// Check if asset already exists to avoid blocking overwrite dialog
	FString FullAssetPath = DestinationPath / AssetName;
	if (UEditorAssetLibrary::DoesAssetExist(FullAssetPath))
	{
		// Layer info already exists - return it as success
		UObject* ExistingObj = UEditorAssetLibrary::LoadAsset(FullAssetPath);
		ULandscapeLayerInfoObject* ExistingInfo = Cast<ULandscapeLayerInfoObject>(ExistingObj);
		if (ExistingInfo)
		{
			Result.bSuccess = true;
			Result.AssetPath = FullAssetPath;
			Result.LayerName = LayerName;
			UE_LOG(LogTemp, Log, TEXT("ULandscapeMaterialService::CreateLayerInfoObject: Layer info '%s' already exists at '%s', returning existing"),
				*LayerName, *Result.AssetPath);
			return Result;
		}
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	IAssetTools& AssetTools = AssetToolsModule.Get();

	// Create package and object directly since there's no standard factory
	FString PackagePath = DestinationPath / AssetName;
	FString PackageName = FPackageName::ObjectPathToPackageName(PackagePath);

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		Result.ErrorMessage = FString::Printf(TEXT("Failed to create package for '%s'"), *PackagePath);
		UE_LOG(LogTemp, Error, TEXT("ULandscapeMaterialService::CreateLayerInfoObject: %s"), *Result.ErrorMessage);
		return Result;
	}

	ULandscapeLayerInfoObject* LayerInfoObj = NewObject<ULandscapeLayerInfoObject>(
		Package, FName(*AssetName), RF_Public | RF_Standalone);

	if (!LayerInfoObj)
	{
		Result.ErrorMessage = TEXT("Failed to create ULandscapeLayerInfoObject");
		UE_LOG(LogTemp, Error, TEXT("ULandscapeMaterialService::CreateLayerInfoObject: %s"), *Result.ErrorMessage);
		return Result;
	}

	LayerInfoObj->SetLayerName(FName(*LayerName), false);
	LayerInfoObj->SetBlendMethod(
		bIsWeightBlended ? ELandscapeTargetLayerBlendMethod::FinalWeightBlending : ELandscapeTargetLayerBlendMethod::None,
		false);

	// Notify asset registry
	FAssetRegistryModule::AssetCreated(LayerInfoObj);
	LayerInfoObj->MarkPackageDirty();

	// Save the asset
	Result.bSuccess = UEditorAssetLibrary::SaveAsset(FullAssetPath, false);
	if (!Result.bSuccess)
	{
		Result.ErrorMessage = FString::Printf(TEXT("Failed to save layer info asset '%s'"), *FullAssetPath);
		UE_LOG(LogTemp, Error, TEXT("ULandscapeMaterialService::CreateLayerInfoObject: %s"), *Result.ErrorMessage);
		return Result;
	}

	Result.AssetPath = FullAssetPath;
	Result.LayerName = LayerName;

	UE_LOG(LogTemp, Log, TEXT("ULandscapeMaterialService::CreateLayerInfoObject: Created layer info '%s' at '%s'"),
		*LayerName, *Result.AssetPath);
	return Result;
}

// =================================================================
// Material Assignment
// =================================================================

bool ULandscapeMaterialService::AssignMaterialToLandscape(
	const FString& LandscapeNameOrLabel,
	const FString& MaterialPath,
	const TMap<FString, FString>& LayerInfoPaths)
{
	UWorld* World = nullptr;
	if (GEditor)
	{
		World = GEditor->GetEditorWorldContext().World();
	}

	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("ULandscapeMaterialService::AssignMaterialToLandscape: No editor world available"));
		return false;
	}

	// Find landscape
	ALandscapeProxy* LandscapeProxy = nullptr;
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		if ((*It)->GetActorLabel().Equals(LandscapeNameOrLabel, ESearchCase::IgnoreCase) ||
			(*It)->GetName().Equals(LandscapeNameOrLabel, ESearchCase::IgnoreCase))
		{
			LandscapeProxy = *It;
			break;
		}
	}

	if (!LandscapeProxy)
	{
		UE_LOG(LogTemp, Error, TEXT("ULandscapeMaterialService::AssignMaterialToLandscape: Landscape '%s' not found"), *LandscapeNameOrLabel);
		return false;
	}

	// Load material
	UObject* MatObj = UEditorAssetLibrary::LoadAsset(MaterialPath);
	UMaterialInterface* Material = Cast<UMaterialInterface>(MatObj);
	if (!Material)
	{
		UE_LOG(LogTemp, Error, TEXT("ULandscapeMaterialService::AssignMaterialToLandscape: Failed to load material '%s'"), *MaterialPath);
		return false;
	}

	FScopedTransaction Transaction(NSLOCTEXT("LandscapeMaterialService", "AssignMaterial", "Assign Material to Landscape"));
	LandscapeProxy->Modify();

	// Set material
	LandscapeProxy->LandscapeMaterial = Material;

	// PostEditChange first — this triggers material instance creation and may rebuild
	// the landscape info layer list. We MUST do this before adding our layer info objects,
	// otherwise PostEditChange can clear/rebuild them from the material's layers.
	LandscapeProxy->PostEditChange();

	// Now register layer info objects AFTER PostEditChange has rebuilt internals
	ULandscapeInfo* Info = LandscapeProxy->GetLandscapeInfo();
	if (!Info)
	{
		UE_LOG(LogTemp, Error, TEXT("ULandscapeMaterialService::AssignMaterialToLandscape: Failed to get LandscapeInfo for '%s' - material was set but layers could not be configured"),
			*LandscapeNameOrLabel);
		return false;
	}

	int32 SuccessfulLayers = 0;
	int32 FailedLayers = 0;
	// Target layers persist on the parent Landscape; ULandscapeInfo is transient and rebuilt from them on load.
	ALandscapeProxy* TargetOwner = LandscapeProxy->GetLandscapeActor() ? static_cast<ALandscapeProxy*>(LandscapeProxy->GetLandscapeActor()) : LandscapeProxy;
	for (const auto& Pair : LayerInfoPaths)
	{
		UObject* LayerObj = UEditorAssetLibrary::LoadAsset(Pair.Value);
		ULandscapeLayerInfoObject* LayerInfoObj = Cast<ULandscapeLayerInfoObject>(LayerObj);
		if (LayerInfoObj && !LayerInfoObj->GetLayerName().ToString().Equals(Pair.Key, ESearchCase::IgnoreCase))
		{
			FailedLayers++;
			UE_LOG(LogTemp, Error, TEXT("ULandscapeMaterialService::AssignMaterialToLandscape: Layer info '%s' is for layer '%s', not '%s'"),
				*Pair.Value, *LayerInfoObj->GetLayerName().ToString(), *Pair.Key);
			continue;
		}
		if (LayerInfoObj)
		{
			// Register in the PERSISTENT target-layer list: writing only Info->Layers made the layer
			// (and its painted weights) vanish on save/reload.
			const FName LayerFName = LayerInfoObj->GetLayerName();
			const FLandscapeTargetLayerSettings* Existing = TargetOwner->GetTargetLayers().Find(LayerFName);
			if (!Existing)
			{
				TargetOwner->Modify();
				TargetOwner->AddTargetLayer(LayerFName, FLandscapeTargetLayerSettings(LayerInfoObj));
			}
			else if (Existing->LayerInfoObj != LayerInfoObj)
			{
				TargetOwner->Modify();
				TargetOwner->UpdateTargetLayer(LayerFName, FLandscapeTargetLayerSettings(LayerInfoObj));
			}

			// Keep the transient LandscapeInfo in sync for this session
			bool bExists = false;
			for (FLandscapeInfoLayerSettings& LayerSettings : Info->Layers)
			{
				if (LayerSettings.GetLayerName().ToString().Equals(Pair.Key, ESearchCase::IgnoreCase))
				{
					LayerSettings.LayerInfoObj = LayerInfoObj;
					bExists = true;
					break;
				}
			}

			if (!bExists)
			{
				FLandscapeInfoLayerSettings NewLayerSettings(LayerInfoObj, LandscapeProxy);
				Info->Layers.Add(NewLayerSettings);
			}
			SuccessfulLayers++;
		}
		else
		{
			FailedLayers++;
			UE_LOG(LogTemp, Error, TEXT("ULandscapeMaterialService::AssignMaterialToLandscape: Failed to load layer info '%s' for layer '%s'. "
				"Pass the exact path of an existing layer info asset (from LandscapeEditorSubsystem.create_target_layer_info) - do NOT guess paths."),
				*Pair.Value, *Pair.Key);
		}
	}

	Info->UpdateComponentLayerAllowList();

	// Allocate weight maps by painting the first layer to 100% across the entire
	// landscape. FLandscapeEditDataInterface::SetAlphaData() internally creates
	// WeightmapLayerAllocation entries and reallocates weight maps as needed —
	// the same mechanism that PaintLayerAtLocation uses.
	{
		// Find the first valid layer info to use as the "fill" layer
		ULandscapeLayerInfoObject* FillLayer = nullptr;
		for (const FLandscapeInfoLayerSettings& LayerSettings : Info->Layers)
		{
			if (LayerSettings.LayerInfoObj)
			{
				FillLayer = LayerSettings.LayerInfoObj;
				break;
			}
		}

		if (FillLayer)
		{
			int32 MinX, MinY, MaxX, MaxY;
			if (Info->GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
			{
				int32 SizeX = MaxX - MinX + 1;
				int32 SizeY = MaxY - MinY + 1;

				// Fill with 255 (full weight) for the first layer
				TArray<uint8> FillData;
				FillData.SetNumUninitialized(SizeX * SizeY);
				FMemory::Memset(FillData.GetData(), 255, FillData.Num());

				FLandscapeEditDataInterface LandscapeEdit(Info);
				LandscapeEdit.SetAlphaData(FillLayer, MinX, MinY, MaxX, MaxY, FillData.GetData(), 0);

				UE_LOG(LogTemp, Log, TEXT("ULandscapeMaterialService::AssignMaterialToLandscape: Initialized fill layer '%s' across %dx%d extent"),
					*FillLayer->GetLayerName().ToString(), SizeX, SizeY);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("ULandscapeMaterialService::AssignMaterialToLandscape: Could not get landscape extent for weight map initialization"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("ULandscapeMaterialService::AssignMaterialToLandscape: No valid layer info found for weight map initialization"));
		}
	}

	if (FailedLayers > 0)
	{
		UE_LOG(LogTemp, Error, TEXT("ULandscapeMaterialService::AssignMaterialToLandscape: Assigned material '%s' to '%s' but %d of %d layer infos failed to load. "
			"Use the exact layer info asset paths (from LandscapeEditorSubsystem.create_target_layer_info) - do NOT guess paths."),
			*MaterialPath, *LandscapeNameOrLabel, FailedLayers, LayerInfoPaths.Num());
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("ULandscapeMaterialService::AssignMaterialToLandscape: Assigned '%s' to '%s' with %d/%d layers successfully"),
		*MaterialPath, *LandscapeNameOrLabel, SuccessfulLayers, LayerInfoPaths.Num());
	return true;
}

bool ULandscapeMaterialService::SetupHeightSlopeBlend(
	const FString& MaterialPath,
	const FString& BlendNodeId,
	const FString& BaseLayerName,
	const FString& HeightLayerName,
	const FString& SlopeLayerName,
	float HeightThreshold,
	float HeightBlend,
	float SlopeThreshold,
	float SlopeBlend)
{
	if (HeightLayerName.IsEmpty() && SlopeLayerName.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("ULandscapeMaterialService::SetupHeightSlopeBlend: HeightLayerName and SlopeLayerName are both empty — nothing to do"));
		return false;
	}

	UMaterial* Material = LoadMaterialAsset(MaterialPath);
	if (!Material)
	{
		return false;
	}

	UMaterialExpressionLandscapeLayerBlend* BlendNode = FindLayerBlendNode(Material, BlendNodeId);
	if (!BlendNode)
	{
		UE_LOG(LogTemp, Error, TEXT("ULandscapeMaterialService::SetupHeightSlopeBlend: Blend node '%s' not found"), *BlendNodeId);
		return false;
	}

	FScopedTransaction Transaction(NSLOCTEXT("LandscapeMaterialService", "SetupHeightSlopeBlend", "Setup Height/Slope Blend"));
	Material->Modify();
	BlendNode->Modify();

	// Graph layout: place mask nodes to the left of the blend node
	const int32 BlendX = BlendNode->MaterialExpressionEditorX;
	const int32 BlendY = BlendNode->MaterialExpressionEditorY;
	const int32 HeightMaskX = BlendX - 1400;
	const int32 SlopeMaskX  = BlendX - 1400;
	const int32 HeightMaskY = BlendY;
	const int32 SlopeMaskY  = BlendY + 400;

	bool bSuccess = true;

	// --- Height layer ---
	if (!HeightLayerName.IsEmpty())
	{
		// Find layer index
		int32 LayerIdx = INDEX_NONE;
		for (int32 i = 0; i < BlendNode->Layers.Num(); i++)
		{
			if (BlendNode->Layers[i].LayerName.ToString().Equals(HeightLayerName, ESearchCase::IgnoreCase))
			{
				LayerIdx = i;
				break;
			}
		}

		if (LayerIdx == INDEX_NONE)
		{
			UE_LOG(LogTemp, Error, TEXT("ULandscapeMaterialService::SetupHeightSlopeBlend: HeightLayerName '%s' not found in blend node"), *HeightLayerName);
			bSuccess = false;
		}
		else
		{
			// Switch to AlphaBlend so our mask drives the blend (not painted weights)
			BlendNode->Layers[LayerIdx].BlendType = LB_AlphaBlend;

			// Create height mask network
			// Inline creation to avoid a second LoadMaterialAsset round-trip
			UMaterialExpression* WorldPosExpr = UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionWorldPosition::StaticClass(), HeightMaskX - 600, HeightMaskY);
			UMaterialExpression* HMaskExpr = UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionComponentMask::StaticClass(), HeightMaskX - 350, HeightMaskY);
			UMaterialExpression* HSmoothExpr = UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionSmoothStep::StaticClass(), HeightMaskX, HeightMaskY);

			if (WorldPosExpr && HMaskExpr && HSmoothExpr)
			{
				UMaterialExpressionComponentMask* HMaskNode = Cast<UMaterialExpressionComponentMask>(HMaskExpr);
				HMaskNode->R = false; HMaskNode->G = false; HMaskNode->B = true; HMaskNode->A = false;
				HMaskNode->Input.Connect(0, WorldPosExpr);

				UMaterialExpressionSmoothStep* HSmoothNode = Cast<UMaterialExpressionSmoothStep>(HSmoothExpr);
				HSmoothNode->ConstMin = HeightThreshold;
				HSmoothNode->ConstMax = HeightThreshold + HeightBlend;
				HSmoothNode->Value.Connect(0, HMaskNode);

				// Connect mask to the HeightInput of this layer (reused as Alpha for LB_AlphaBlend)
				BlendNode->Layers[LayerIdx].HeightInput.Connect(0, HSmoothNode);

				UE_LOG(LogTemp, Log, TEXT("ULandscapeMaterialService::SetupHeightSlopeBlend: Height mask connected for layer '%s' (threshold=%.1f, blend=%.1f)"),
					*HeightLayerName, HeightThreshold, HeightBlend);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("ULandscapeMaterialService::SetupHeightSlopeBlend: Failed to create height mask nodes"));
				bSuccess = false;
			}
		}
	}

	// --- Slope layer ---
	if (!SlopeLayerName.IsEmpty())
	{
		int32 LayerIdx = INDEX_NONE;
		for (int32 i = 0; i < BlendNode->Layers.Num(); i++)
		{
			if (BlendNode->Layers[i].LayerName.ToString().Equals(SlopeLayerName, ESearchCase::IgnoreCase))
			{
				LayerIdx = i;
				break;
			}
		}

		if (LayerIdx == INDEX_NONE)
		{
			UE_LOG(LogTemp, Error, TEXT("ULandscapeMaterialService::SetupHeightSlopeBlend: SlopeLayerName '%s' not found in blend node"), *SlopeLayerName);
			bSuccess = false;
		}
		else
		{
			BlendNode->Layers[LayerIdx].BlendType = LB_AlphaBlend;

			const float SlopeFactorMin = 1.0f - FMath::Cos(FMath::DegreesToRadians(SlopeThreshold));
			const float SlopeFactorMax = 1.0f - FMath::Cos(FMath::DegreesToRadians(SlopeThreshold + SlopeBlend));

			UMaterialExpression* NormalExpr = UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionVertexNormalWS::StaticClass(), SlopeMaskX - 700, SlopeMaskY);
			UMaterialExpression* SMaskExpr = UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionComponentMask::StaticClass(), SlopeMaskX - 500, SlopeMaskY);
			UMaterialExpression* OneMinusExpr = UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionOneMinus::StaticClass(), SlopeMaskX - 300, SlopeMaskY);
			UMaterialExpression* SSmoothExpr = UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionSmoothStep::StaticClass(), SlopeMaskX, SlopeMaskY);

			if (NormalExpr && SMaskExpr && OneMinusExpr && SSmoothExpr)
			{
				UMaterialExpressionComponentMask* SMaskNode = Cast<UMaterialExpressionComponentMask>(SMaskExpr);
				SMaskNode->R = false; SMaskNode->G = false; SMaskNode->B = true; SMaskNode->A = false;
				SMaskNode->Input.Connect(0, NormalExpr);

				UMaterialExpressionOneMinus* OneMinusNode = Cast<UMaterialExpressionOneMinus>(OneMinusExpr);
				OneMinusNode->Input.Connect(0, SMaskNode);

				UMaterialExpressionSmoothStep* SSmoothNode = Cast<UMaterialExpressionSmoothStep>(SSmoothExpr);
				SSmoothNode->ConstMin = SlopeFactorMin;
				SSmoothNode->ConstMax = SlopeFactorMax;
				SSmoothNode->Value.Connect(0, OneMinusNode);

				BlendNode->Layers[LayerIdx].HeightInput.Connect(0, SSmoothNode);

				UE_LOG(LogTemp, Log, TEXT("ULandscapeMaterialService::SetupHeightSlopeBlend: Slope mask connected for layer '%s' (threshold=%.1f°, blend=%.1f°, factors=%.4f→%.4f)"),
					*SlopeLayerName, SlopeThreshold, SlopeBlend, SlopeFactorMin, SlopeFactorMax);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("ULandscapeMaterialService::SetupHeightSlopeBlend: Failed to create slope mask nodes"));
				bSuccess = false;
			}
		}
	}

	if (bSuccess)
	{
		RefreshMaterialGraph(Material);
		UE_LOG(LogTemp, Log, TEXT("ULandscapeMaterialService::SetupHeightSlopeBlend: Complete for material '%s'"), *MaterialPath);
	}

	return bSuccess;
}

// =================================================================
// Auto-Material Creation
// =================================================================

FLandscapeAutoMaterialResult ULandscapeMaterialService::CreateAutoMaterial(
	const FString& LandscapeNameOrLabel,
	const FString& MaterialName,
	const FString& MaterialPath,
	const TArray<FLandscapeAutoLayerConfig>& LayerConfigs,
	bool bAutoBlend,
	float HeightThreshold,
	float HeightBlend,
	float SlopeThreshold,
	float SlopeBlend)
{
	FLandscapeAutoMaterialResult Result;

	if (MaterialName.IsEmpty() || MaterialPath.IsEmpty())
	{
		Result.ErrorMessage = TEXT("MaterialName and MaterialPath cannot be empty");
		return Result;
	}

	if (LayerConfigs.Num() == 0)
	{
		Result.ErrorMessage = TEXT("At least one layer configuration is required");
		return Result;
	}

	// Step 1: Create the material
	FLandscapeMaterialCreateResult MatResult = CreateLandscapeMaterial(MaterialName, MaterialPath);
	if (!MatResult.bSuccess)
	{
		Result.ErrorMessage = FString::Printf(TEXT("Failed to create material: %s"), *MatResult.ErrorMessage);
		return Result;
	}
	Result.MaterialAssetPath = MatResult.AssetPath;

	UMaterial* Material = LoadMaterialAsset(Result.MaterialAssetPath);
	if (!Material)
	{
		Result.ErrorMessage = TEXT("Failed to load newly created material");
		return Result;
	}

	// Step 2: Build layer configs for LayerBlendNodeWithLayers
	TArray<FLandscapeMaterialLayerConfig> BlendLayerConfigs;
	for (const FLandscapeAutoLayerConfig& AutoLayer : LayerConfigs)
	{
		FLandscapeMaterialLayerConfig LayerCfg;
		LayerCfg.LayerName = AutoLayer.LayerName;

		// Height-role layers use LB_HeightBlend; others use LB_WeightBlend
		if (AutoLayer.Role.Equals(TEXT("height"), ESearchCase::IgnoreCase) ||
			AutoLayer.Role.Equals(TEXT("slope"), ESearchCase::IgnoreCase))
		{
			LayerCfg.BlendType = TEXT("LB_HeightBlend");
			LayerCfg.bUseHeightBlend = true;
		}
		else
		{
			LayerCfg.BlendType = TEXT("LB_WeightBlend");
			LayerCfg.bUseHeightBlend = false;
		}

		LayerCfg.PreviewWeight = 1.0f;
		BlendLayerConfigs.Add(LayerCfg);
	}

	// Step 3: Create LandscapeLayerBlend node with all layers
	FLandscapeLayerBlendInfo BlendInfo = CreateLayerBlendNodeWithLayers(
		Result.MaterialAssetPath, BlendLayerConfigs, 0, 0);

	if (BlendInfo.NodeId.IsEmpty())
	{
		Result.ErrorMessage = TEXT("Failed to create LandscapeLayerBlend node");
		return Result;
	}
	Result.BlendNodeId = BlendInfo.NodeId;

	// Step 4: Create texture samplers for each layer and connect to blend
	FScopedTransaction Transaction(NSLOCTEXT("LandscapeMaterialService", "CreateAutoMaterial", "Create Auto Material"));
	Material->Modify();

	UMaterialExpressionLandscapeLayerBlend* BlendNode = FindLayerBlendNode(Material, BlendInfo.NodeId);
	if (!BlendNode)
	{
		Result.ErrorMessage = TEXT("Failed to find newly created blend node");
		return Result;
	}

	for (int32 i = 0; i < LayerConfigs.Num(); i++)
	{
		const FLandscapeAutoLayerConfig& AutoLayer = LayerConfigs[i];
		int32 BaseY = i * 350;

		// Create diffuse/albedo texture sampler
		if (!AutoLayer.DiffuseTexturePath.IsEmpty())
		{
			UTexture* DiffuseTex = Cast<UTexture>(UEditorAssetLibrary::LoadAsset(AutoLayer.DiffuseTexturePath));
			if (DiffuseTex)
			{
				UMaterialExpressionTextureSample* DiffuseSampler = Cast<UMaterialExpressionTextureSample>(
					UMaterialEditingLibrary::CreateMaterialExpression(
						Material, UMaterialExpressionTextureSample::StaticClass(), -600, BaseY));

				if (DiffuseSampler)
				{
					DiffuseSampler->Texture = DiffuseTex;

					// Connect to Layer input of the blend node
					if (i < BlendNode->Layers.Num())
					{
						BlendNode->Layers[i].LayerInput.Connect(0, DiffuseSampler);
					}
				}
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("CreateAutoMaterial: Diffuse texture not found: %s"), *AutoLayer.DiffuseTexturePath);
			}
		}

		// Create normal texture sampler
		if (!AutoLayer.NormalTexturePath.IsEmpty())
		{
			UTexture* NormalTex = Cast<UTexture>(UEditorAssetLibrary::LoadAsset(AutoLayer.NormalTexturePath));
			if (NormalTex)
			{
				UMaterialExpressionTextureSample* NormalSampler = Cast<UMaterialExpressionTextureSample>(
					UMaterialEditingLibrary::CreateMaterialExpression(
						Material, UMaterialExpressionTextureSample::StaticClass(), -600, BaseY + 150));

				if (NormalSampler)
				{
					NormalSampler->Texture = NormalTex;
					NormalSampler->SamplerType = SAMPLERTYPE_Normal;
					// Normal samplers need separate LandscapeLayerBlend for normal channel
					// For simplicity, we connect them later if the user wants full normal blending
				}
			}
		}
	}

	// Step 5: Connect BlendNode output to material BaseColor
	{
		FExpressionInput* BaseColorInput = Material->GetExpressionInputForProperty(MP_BaseColor);
		if (BaseColorInput)
		{
			BaseColorInput->Expression = BlendNode;
			BaseColorInput->OutputIndex = 0;
		}
	}

	// Step 6: Setup auto-blend masks if requested
	if (bAutoBlend)
	{
		// Identify layers by role
		FString BaseLayerName, SlopeLayerName, HeightLayerName;
		for (const FLandscapeAutoLayerConfig& AutoLayer : LayerConfigs)
		{
			if (AutoLayer.Role.Equals(TEXT("base"), ESearchCase::IgnoreCase))
			{
				BaseLayerName = AutoLayer.LayerName;
			}
			else if (AutoLayer.Role.Equals(TEXT("slope"), ESearchCase::IgnoreCase))
			{
				SlopeLayerName = AutoLayer.LayerName;
			}
			else if (AutoLayer.Role.Equals(TEXT("height"), ESearchCase::IgnoreCase))
			{
				HeightLayerName = AutoLayer.LayerName;
			}
		}

		// Use the existing SetupHeightSlopeBlend if we have the right roles
		if ((!HeightLayerName.IsEmpty() || !SlopeLayerName.IsEmpty()) && !BaseLayerName.IsEmpty())
		{
			SetupHeightSlopeBlend(
				Result.MaterialAssetPath,
				BlendInfo.NodeId,
				BaseLayerName,
				HeightLayerName.IsEmpty() ? TEXT("") : HeightLayerName,
				SlopeLayerName.IsEmpty() ? TEXT("") : SlopeLayerName,
				HeightThreshold,
				HeightBlend,
				SlopeThreshold,
				SlopeBlend);
		}
	}

	RefreshMaterialGraph(Material);

	// Step 7: Create layer info objects
	FString LayerInfoDir = MaterialPath;
	TMap<FString, FString> LayerInfoMap;
	for (const FLandscapeAutoLayerConfig& AutoLayer : LayerConfigs)
	{
		FLandscapeLayerInfoCreateResult InfoResult = CreateLayerInfoObject(
			AutoLayer.LayerName, LayerInfoDir, true);

		if (InfoResult.bSuccess)
		{
			Result.LayerInfoPaths.Add(InfoResult.AssetPath);
			LayerInfoMap.Add(AutoLayer.LayerName, InfoResult.AssetPath);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("CreateAutoMaterial: Failed to create layer info for '%s': %s"),
				*AutoLayer.LayerName, *InfoResult.ErrorMessage);
		}
	}

	// Step 8: Compile the material
	UMaterialEditingLibrary::RecompileMaterial(Material);

	// Step 9: Assign to landscape if specified
	if (!LandscapeNameOrLabel.IsEmpty())
	{
		bool bAssigned = AssignMaterialToLandscape(LandscapeNameOrLabel, Result.MaterialAssetPath, LayerInfoMap);
		if (!bAssigned)
		{
			UE_LOG(LogTemp, Warning, TEXT("CreateAutoMaterial: Material created but failed to assign to landscape '%s'"),
				*LandscapeNameOrLabel);
		}
	}

	// Save the material
	Result.bSuccess = UEditorAssetLibrary::SaveAsset(Result.MaterialAssetPath, false);
	if (!Result.bSuccess)
	{
		Result.ErrorMessage = FString::Printf(TEXT("Failed to save material asset '%s'"), *Result.MaterialAssetPath);
		UE_LOG(LogTemp, Error, TEXT("ULandscapeMaterialService::CreateAutoMaterial: %s"), *Result.ErrorMessage);
		return Result;
	}
	UE_LOG(LogTemp, Log, TEXT("ULandscapeMaterialService::CreateAutoMaterial: Created auto-material '%s' with %d layers"),
		*Result.MaterialAssetPath, LayerConfigs.Num());

	return Result;
}

