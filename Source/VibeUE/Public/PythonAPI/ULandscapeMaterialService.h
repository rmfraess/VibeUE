// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "ULandscapeMaterialService.generated.h"

class UMaterial;
class UMaterialExpression;
class UMaterialExpressionLandscapeLayerBlend;

/**
 * Result of landscape material creation
 */
USTRUCT(BlueprintType)
struct FLandscapeMaterialCreateResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "LandscapeMaterial")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadWrite, Category = "LandscapeMaterial")
	FString AssetPath;

	UPROPERTY(BlueprintReadWrite, Category = "LandscapeMaterial")
	FString ErrorMessage;
};

/**
 * Configuration for a landscape material layer
 */
USTRUCT(BlueprintType)
struct FLandscapeMaterialLayerConfig
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "LandscapeMaterial")
	FString LayerName;

	/** "LB_WeightBlend", "LB_AlphaBlend" or "LB_HeightBlend". Empty: taken from bUseHeightBlend (height or weight). */
	UPROPERTY(BlueprintReadWrite, Category = "LandscapeMaterial")
	FString BlendType;

	UPROPERTY(BlueprintReadWrite, Category = "LandscapeMaterial")
	float PreviewWeight = 1.0f;

	/** Height blend (adds the layer's Height pin). Contradicting a non-height BlendType is an error. */
	UPROPERTY(BlueprintReadWrite, Category = "LandscapeMaterial")
	bool bUseHeightBlend = false;
};

/**
 * Configuration for a layer in the auto-material system.
 * Each layer defines textures and a role that determines automatic blending behavior.
 */
USTRUCT(BlueprintType)
struct FLandscapeAutoLayerConfig
{
	GENERATED_BODY()

	/** Layer name (e.g., "Grass", "Rock", "Snow") */
	UPROPERTY(BlueprintReadWrite, Category = "LandscapeMaterial")
	FString LayerName;

	/** Path to diffuse/albedo texture asset */
	UPROPERTY(BlueprintReadWrite, Category = "LandscapeMaterial")
	FString DiffuseTexturePath;

	/** Optional: Path to normal map texture */
	UPROPERTY(BlueprintReadWrite, Category = "LandscapeMaterial")
	FString NormalTexturePath;

	/** Optional: Path to roughness texture */
	UPROPERTY(BlueprintReadWrite, Category = "LandscapeMaterial")
	FString RoughnessTexturePath;

	/** UV tiling scale (default 0.01 = tile every 100 units) */
	UPROPERTY(BlueprintReadWrite, Category = "LandscapeMaterial")
	float TilingScale = 0.01f;

	/**
	 * Role determines auto-blend behavior:
	 *  "base"   - Default layer, shown on low/flat terrain
	 *  "slope"  - Shown on steep terrain (driven by slope mask)
	 *  "height" - Shown at high elevation (driven by height mask)
	 *  "paint"  - Manual paint only (no auto-blending)
	 */
	UPROPERTY(BlueprintReadWrite, Category = "LandscapeMaterial")
	FString Role = TEXT("paint");
};

/**
 * Result of auto-material creation
 */
USTRUCT(BlueprintType)
struct FLandscapeAutoMaterialResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "LandscapeMaterial")
	bool bSuccess = false;

	/** Path to the created material asset */
	UPROPERTY(BlueprintReadWrite, Category = "LandscapeMaterial")
	FString MaterialAssetPath;

	/** Paths to each created layer info object */
	UPROPERTY(BlueprintReadWrite, Category = "LandscapeMaterial")
	TArray<FString> LayerInfoPaths;

	/** Node ID of the created LandscapeLayerBlend node */
	UPROPERTY(BlueprintReadWrite, Category = "LandscapeMaterial")
	FString BlendNodeId;

	UPROPERTY(BlueprintReadWrite, Category = "LandscapeMaterial")
	FString ErrorMessage;
};

/**
 * A discovered set of landscape textures (albedo + optional normal + optional roughness)
 */
USTRUCT(BlueprintType)
struct FLandscapeTextureSet
{
	GENERATED_BODY()

	/** Terrain type inferred from path/name (e.g., "Grass", "Rock", "Snow") */
	UPROPERTY(BlueprintReadWrite, Category = "LandscapeMaterial")
	FString TerrainType;

	/** Path to albedo/diffuse texture */
	UPROPERTY(BlueprintReadWrite, Category = "LandscapeMaterial")
	FString AlbedoPath;

	/** Path to normal map (empty if not found) */
	UPROPERTY(BlueprintReadWrite, Category = "LandscapeMaterial")
	FString NormalPath;

	/** Path to roughness map (empty if not found) */
	UPROPERTY(BlueprintReadWrite, Category = "LandscapeMaterial")
	FString RoughnessPath;

	/** Resolution of the albedo texture */
	UPROPERTY(BlueprintReadWrite, Category = "LandscapeMaterial")
	int32 TextureWidth = 0;

	UPROPERTY(BlueprintReadWrite, Category = "LandscapeMaterial")
	int32 TextureHeight = 0;
};

/**
 * Information about a LandscapeLayerBlend node
 */
USTRUCT(BlueprintType)
struct FLandscapeLayerBlendInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "LandscapeMaterial")
	FString NodeId;

	UPROPERTY(BlueprintReadWrite, Category = "LandscapeMaterial")
	TArray<FLandscapeMaterialLayerConfig> Layers;
};

/**
 * Result of layer info object creation
 */
USTRUCT(BlueprintType)
struct FLandscapeLayerInfoCreateResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "LandscapeMaterial")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadWrite, Category = "LandscapeMaterial")
	FString AssetPath;

	UPROPERTY(BlueprintReadWrite, Category = "LandscapeMaterial")
	FString LayerName;

	UPROPERTY(BlueprintReadWrite, Category = "LandscapeMaterial")
	FString ErrorMessage;
};

/**
 * Landscape material service exposed directly to Python.
 *
 * Provides landscape material management actions:
 *
 * Auto-Material Creation:
 * - create_auto_material: Create a complete landscape material (LandscapeLayerBlend, texture
 *   samplers, optional height/slope masks), its layer info objects, and assign it to a landscape
 *
 * Everything else is the engine's:
 * - plain materials, landscape expressions (LayerBlend, LayerCoords, LayerSample, LayerWeight,
 *   GrassOutput; expressionClass /Script/Landscape.MaterialExpressionLandscape...) and wiring:
 *   EditorToolset.MaterialToolset.CreateMaterial / AddExpression / ConnectExpressions, with
 *   EditorToolset.ObjectToolset.SetProperties for expression properties (e.g. the LayerBlend's Layers)
 * - layer info assets and landscape assignment: unreal.LandscapeEditorSubsystem
 *   (create_target_layer_info, add_target_layer, set_target_layer_info)
 *
 * Python Usage:
 *   import unreal
 *
 *   layers = [unreal.LandscapeAutoLayerConfig(layer_name="Grass", diffuse_texture_path="/Game/T/T_Grass_D", role="base")]
 *   result = unreal.LandscapeMaterialService.create_auto_material("", "M_Terrain", "/Game/Materials", layers)
 */
UCLASS(BlueprintType)
class VIBEUE_API ULandscapeMaterialService : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	// =================================================================
	// Material Creation
	// =================================================================

	/**
	 * Create a new material pre-configured for landscape use.
	 * Demoted (not an AI tool): use EditorToolset.MaterialToolset.CreateMaterial instead; kept because CreateAutoMaterial calls it.
	 *
	 * @param MaterialName - Name for the new material
	 * @param DestinationPath - Path where to create the asset (e.g., "/Game/Materials")
	 * @return Create result with asset path
	 */
	static FLandscapeMaterialCreateResult CreateLandscapeMaterial(
		const FString& MaterialName,
		const FString& DestinationPath);

	/**
	 * Create a LandscapeLayerBlend node with all layers pre-configured in one call.
	 * Maps to action="create_layer_blend_node_with_layers"
	 *
	 * Creates the blend node and adds all specified layers in a single transaction.
	 *
	 * @param MaterialPath - Full path to the material
	 * @param Layers - Array of layer configurations (name, blend type, preview weight)
	 * @param PosX - X position in the material graph
	 * @param PosY - Y position in the material graph
	 * @return Blend node info with node ID and all layers
	 */
	static FLandscapeLayerBlendInfo CreateLayerBlendNodeWithLayers(
		const FString& MaterialPath,
		const TArray<FLandscapeMaterialLayerConfig>& Layers,
		int32 PosX = -400,
		int32 PosY = 0);

	// =================================================================
	// Layer Info Object Management
	// =================================================================

	/**
	 * Create a ULandscapeLayerInfoObject asset.
	 * Maps to action="create_layer_info_object"
	 *
	 * @param LayerName - Name for the layer
	 * @param DestinationPath - Path where to create the asset (e.g., "/Game/Landscape")
	 * @param bIsWeightBlended - True for weight-blended (default), false for non-weight-blended
	 * @return Create result with asset path and layer name
	 */
	static FLandscapeLayerInfoCreateResult CreateLayerInfoObject(
		const FString& LayerName,
		const FString& DestinationPath,
		bool bIsWeightBlended = true);

	// =================================================================
	// Material Assignment
	// =================================================================

	/**
	 * Assign a material to a landscape and configure layer info objects.
	 * Maps to action="assign_material_to_landscape"
	 *
	 * @param LandscapeNameOrLabel - Name or label of the landscape actor
	 * @param MaterialPath - Full path to the material asset
	 * @param LayerInfoPaths - Map of layer name -> layer info asset path
	 * @return True if assignment succeeded
	 */
	static bool AssignMaterialToLandscape(
		const FString& LandscapeNameOrLabel,
		const FString& MaterialPath,
		const TMap<FString, FString>& LayerInfoPaths);

	/**
	 * Set up a complete height/slope-driven layer blend in one call.
	 * Maps to action="setup_height_slope_blend"
	 *
	 * Creates height and/or slope mask networks and uses each mask to lerp that
	 * layer's input into the base layer's input. Wherever the base layer is painted,
	 * the zone layers appear by elevation/slope with no extra painting; the zone
	 * layers stay paintable too. Base and zone layers need their inputs connected.
	 *
	 * Layer zones:
	 *   BaseLayerName  - Displays on low, flat terrain
	 *   HeightLayerName - Fades in above HeightThreshold (e.g., snow on peaks)
	 *   SlopeLayerName  - Fades in above SlopeThreshold degrees (e.g., rock on cliffs)
	 *
	 * @param MaterialPath     - Full path to the material
	 * @param BlendNodeId      - Existing LandscapeLayerBlend node to configure
	 * @param BaseLayerName    - Layer for low/flat terrain; the masks blend into its input
	 * @param HeightLayerName  - Layer for high-altitude zones. Empty = skip.
	 * @param SlopeLayerName   - Layer for steep-slope zones. Empty = skip.
	 * @param HeightThreshold  - World Z where height layer begins to appear (world units)
	 * @param HeightBlend      - Transition width above threshold (world units)
	 * @param SlopeThreshold   - Slope angle where slope layer begins to appear (degrees)
	 * @param SlopeBlend       - Transition width above threshold (degrees)
	 * @return True if setup succeeded
	 */
	static bool SetupHeightSlopeBlend(
		const FString& MaterialPath,
		const FString& BlendNodeId,
		const FString& BaseLayerName,
		const FString& HeightLayerName = TEXT(""),
		const FString& SlopeLayerName = TEXT(""),
		float HeightThreshold = 5000.0f,
		float HeightBlend = 1000.0f,
		float SlopeThreshold = 35.0f,
		float SlopeBlend = 10.0f);

	// ===== Auto-Material Creation =====

	/**
	 * Create a complete landscape material with automatic layer blending.
	 * Maps to action="create_auto_material"
	 *
	 * Creates a material with LandscapeLayerBlend nodes for base colour, normal and
	 * roughness (the last two only if a layer has that texture), world-space tiled
	 * texture samplers, and optional height/slope auto-blend masks. Also creates
	 * layer info objects and assigns the material to the target landscape.
	 *
	 * Layer roles control auto-blend behavior:
	 *  - "base"   — Default layer, shown on low/flat terrain
	 *  - "slope"  — Shown on steep terrain via a vertex-normal mask
	 *  - "height" — Shown at high elevation via WorldPosition mask
	 *  - "paint"  — Manual paint only (no auto-blending)
	 *
	 * @param LandscapeNameOrLabel - Name/label of target landscape (empty = don't assign)
	 * @param MaterialName - Name for the new material asset
	 * @param MaterialPath - Directory path where material will be created
	 * @param LayerConfigs - Array of layer configurations
	 * @param bAutoBlend - If true, wire height/slope masks for layers with those roles
	 *                     (needs a "base" layer with a diffuse texture; paint the base layer)
	 * @param HeightThreshold - World Z height where height layer begins
	 * @param HeightBlend - Transition width for height blending
	 * @param SlopeThreshold - Slope angle where slope layer begins (degrees)
	 * @param SlopeBlend - Transition width for slope blending (degrees)
	 * @return Result with material path, layer info paths, and any errors
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category ="VibeUE|LandscapeMaterial|AutoMaterial")
	static FLandscapeAutoMaterialResult CreateAutoMaterial(
		const FString& LandscapeNameOrLabel,
		const FString& MaterialName,
		const FString& MaterialPath,
		const TArray<FLandscapeAutoLayerConfig>& LayerConfigs,
		bool bAutoBlend = true,
		float HeightThreshold = 5000.0f,
		float HeightBlend = 1000.0f,
		float SlopeThreshold = 35.0f,
		float SlopeBlend = 10.0f);

private:
	static UMaterial* LoadMaterialAsset(const FString& MaterialPath);
	static UMaterialExpression* FindExpressionById(UMaterial* Material, const FString& ExpressionId);
	static FString GetExpressionId(UMaterialExpression* Expression);
	static UMaterialExpressionLandscapeLayerBlend* FindLayerBlendNode(UMaterial* Material, const FString& NodeId);
	static void RefreshMaterialGraph(UMaterial* Material);
};
