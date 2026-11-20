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
	UFUNCTION(BlueprintCallable, Category ="VibeUE|LandscapeMaterial")
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
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category ="VibeUE|LandscapeMaterial")
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
public:
	// Restored from upstream: the native replacement does not do all of this (cut audit).
	/**
	 * Create a height-based mask node network in the material graph.
	 * Maps to action="create_height_mask_node"
	 *
	 * Builds: AbsoluteWorldPosition → ComponentMask(B/Z) → SmoothStep
	 * Output: 0 below MinHeight, smooth S-curve transition, 1 above MaxHeight.
	 *
	 * Use the returned expression ID as an Alpha input on an LB_AlphaBlend layer
	 * to blend a layer in above a certain elevation — e.g., snow above 8000 units.
	 * No weight-map painting required; the material handles it automatically.
	 *
	 * @param MaterialPath    - Full path to the material asset
	 * @param MinHeight       - World Z where mask begins rising from 0 (world units)
	 * @param MaxHeight       - World Z where mask reaches 1 (world units)
	 * @param PosX            - X position of the output node in the material graph
	 * @param PosY            - Y position of the output node
	 * @return Expression ID of the SmoothStep output node, or empty string on failure
	 *
	 * Example:
	 *   mask_id = svc.create_height_mask_node("/Game/M_Terrain", 5000.0, 8000.0)
	 *   svc.connect_to_layer_input("/Game/M_Terrain", mask_id, "", blend_id, "Snow", "Alpha")
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category ="VibeUE|LandscapeMaterial")
	static FString CreateHeightMaskNode(
		const FString& MaterialPath,
		float MinHeight,
		float MaxHeight,
		int32 PosX = -1200,
		int32 PosY = 0);

	/**
	 * Create a slope-based mask node network in the material graph.
	 * Maps to action="create_slope_mask_node"
	 *
	 * Builds: VertexNormalWS → ComponentMask(B/Z) → OneMinus → SmoothStep
	 * Output: 0 for flat terrain (below MinSlopeDegrees), 1 for steep/cliff terrain
	 * (above MaxSlopeDegrees).
	 *
	 * WorldNormal.Z = cos(slope_angle):
	 *   Flat (0°)     → NormalZ ≈ 1.0 → SlopeFactor ≈ 0
	 *   Vertical (90°) → NormalZ ≈ 0.0 → SlopeFactor ≈ 1
	 *
	 * @param MaterialPath      - Full path to the material asset
	 * @param MinSlopeDegrees   - Slope angle below which mask = 0 (flat terrain)
	 * @param MaxSlopeDegrees   - Slope angle above which mask = 1 (cliff terrain)
	 * @param PosX              - X position of the output node
	 * @param PosY              - Y position of the output node
	 * @return Expression ID of the SmoothStep output node, or empty string on failure
	 *
	 * Example:
	 *   mask_id = svc.create_slope_mask("/Game/M_Terrain", 30.0, 60.0)
	 *   svc.connect_to_layer_input("/Game/M_Terrain", mask_id, "", blend_id, "Rock", "Alpha")
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category ="VibeUE|LandscapeMaterial")
	static FString CreateSlopeMaskNode(
		const FString& MaterialPath,
		float MinSlopeDegrees = 30.0f,
		float MaxSlopeDegrees = 60.0f,
		int32 PosX = -1200,
		int32 PosY = 300);

	/**
	 * Remove a layer from a LandscapeLayerBlend node.
	 * Maps to action="remove_layer_from_blend_node"
	 *
	 * @param MaterialPath - Full path to the material
	 * @param BlendNodeId - ID of the blend node expression
	 * @param LayerName - Name of the layer to remove
	 * @return True if layer was removed
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category ="VibeUE|LandscapeMaterial")
	static bool RemoveLayerFromBlendNode(
		const FString& MaterialPath,
		const FString& BlendNodeId,
		const FString& LayerName);

	/**
	 * Search the content browser for landscape-suitable textures.
	 * Maps to action="find_landscape_textures"
	 *
	 * Searches content directories for textures matching common landscape naming
	 * conventions (Albedo/Diffuse/BaseColor, Normal, Roughness, in Landscape/
	 * Terrain/Ground directories). Groups matched textures into sets.
	 *
	 * @param SearchPath - Root content path to search (empty = "/Game/")
	 * @param TerrainType - Optional filter: "grass", "rock", "snow", "mud", "sand", etc.
	 * @param bIncludeNormals - Also return matching normal maps
	 * @return Array of discovered texture sets grouped by terrain type
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Read"), Category ="VibeUE|LandscapeMaterial|AutoMaterial")
	static TArray<FLandscapeTextureSet> FindLandscapeTextures(
		const FString& SearchPath = TEXT(""),
		const FString& TerrainType = TEXT(""),
		bool bIncludeNormals = true);

public:
	// Restored from upstream: the native replacement does not do all of this (cut audit).
	/**
	 * Create a LandscapeLayerBlend node in a material.
	 * Maps to action="create_layer_blend_node"
	 *
	 * Demoted (not an AI tool): use EditorToolset.MaterialToolset.AddExpression (expressionClass={"refPath": "/Script/Landscape.MaterialExpressionLandscapeLayerBlend"}) instead; kept because it returns the VibeUE blend-node ID that RemoveLayerFromBlendNode takes; add or read layers with ObjectToolset on the node's Layers property.
	 *
	 * @param MaterialPath - Full path to the material
	 * @param PosX - X position in the material graph
	 * @param PosY - Y position in the material graph
	 * @return Blend node info with node ID
	 */
	UFUNCTION(BlueprintCallable, Category ="VibeUE|LandscapeMaterial")
	static FLandscapeLayerBlendInfo CreateLayerBlendNode(
		const FString& MaterialPath,
		int32 PosX = -400,
		int32 PosY = 0);

	/**
	 * Create a LandscapeLayerCoords expression for UV mapping.
	 * Maps to action="create_layer_coords_node"
	 *
	 * Demoted (not an AI tool): use EditorToolset.MaterialToolset.AddExpression (expressionClass={"refPath": "/Script/Landscape.MaterialExpressionLandscapeLayerCoords"}) then EditorToolset.ObjectToolset.SetProperties instead; kept because one call creates and configures the node for the landscape-materials workflows; the returned ID is VibeUE-internal, so wire the node with MaterialToolset.ConnectExpressions.
	 *
	 * @param MaterialPath - Full path to the material
	 * @param MappingScale - UV tiling scale (default 0.01 = tile every 100 units)
	 * @param PosX - X position in the material graph
	 * @param PosY - Y position in the material graph
	 * @return Expression ID of the created node, or empty string on failure
	 */
	UFUNCTION(BlueprintCallable, Category ="VibeUE|LandscapeMaterial")
	static FString CreateLayerCoordsNode(
		const FString& MaterialPath,
		float MappingScale = 0.01f,
		int32 PosX = -800,
		int32 PosY = 0);

	/**
	 * Create a LandscapeLayerSample expression to sample a layer's weight.
	 * Maps to action="create_layer_sample_node"
	 *
	 * Unlike LandscapeLayerBlend which blends multiple layers, LayerSample outputs
	 * the raw weight (0-1) of a single layer. Useful for masking and procedural effects.
	 *
	 * Demoted (not an AI tool): use EditorToolset.MaterialToolset.AddExpression (expressionClass={"refPath": "/Script/Landscape.MaterialExpressionLandscapeLayerSample"}) then EditorToolset.ObjectToolset.SetProperties instead; kept because one call creates and configures the node for the landscape-materials workflows; the returned ID is VibeUE-internal, so wire the node with MaterialToolset.ConnectExpressions.
	 *
	 * @param MaterialPath - Full path to the material
	 * @param LayerName - Name of the landscape layer to sample
	 * @param PosX - X position in the material graph
	 * @param PosY - Y position in the material graph
	 * @return Expression ID of the created node, or empty string on failure
	 *
	 * Example:
	 *   sample_id = unreal.LandscapeMaterialService.create_layer_sample_node("/Game/M_Test", "Grass", -800, 0)
	 */
	UFUNCTION(BlueprintCallable, Category ="VibeUE|LandscapeMaterial")
	static FString CreateLayerSampleNode(
		const FString& MaterialPath,
		const FString& LayerName,
		int32 PosX = -800,
		int32 PosY = 0);

	/**
	 * Create a LandscapeGrassOutput expression for procedural grass/foliage spawning.
	 * Maps to action="create_grass_output"
	 *
	 * Each entry in GrassTypeNames maps a display name to a grass type asset path.
	 * The node will have one input per grass type for driving spawn density.
	 *
	 * Demoted (not an AI tool): use EditorToolset.MaterialToolset.AddExpression (expressionClass={"refPath": "/Script/Landscape.MaterialExpressionLandscapeGrassOutput"}) then EditorToolset.ObjectToolset.SetProperties instead; kept because one call creates and configures the node for the landscape-materials workflows; the returned ID is VibeUE-internal, so wire the node with MaterialToolset.ConnectExpressions.
	 *
	 * @param MaterialPath - Full path to the material
	 * @param GrassTypeNames - Map of input name -> LandscapeGrassType asset path
	 * @param PosX - X position in the material graph
	 * @param PosY - Y position in the material graph
	 * @return Expression ID of the created node, or empty string on failure
	 *
	 * Example:
	 *   grass_id = unreal.LandscapeMaterialService.create_grass_output("/Game/M_Test",
	 *       {"ShortGrass": "/Game/Foliage/LGT_ShortGrass", "TallGrass": "/Game/Foliage/LGT_TallGrass"}, 400, 0)
	 */
	UFUNCTION(BlueprintCallable, Category ="VibeUE|LandscapeMaterial")
	static FString CreateGrassOutput(
		const FString& MaterialPath,
		const TMap<FString, FString>& GrassTypeNames,
		int32 PosX = 400,
		int32 PosY = 0);

	/**
	 * Create a LandscapeLayerWeight expression (alternative to LandscapeLayerBlend).
	 * Maps to action="create_layer_weight_node"
	 *
	 * Demoted (not an AI tool): use EditorToolset.MaterialToolset.AddExpression (expressionClass={"refPath": "/Script/Landscape.MaterialExpressionLandscapeLayerWeight"}) then EditorToolset.ObjectToolset.SetProperties instead; kept because one call creates and configures the node for the landscape-materials workflows; the returned ID is VibeUE-internal, so wire the node with MaterialToolset.ConnectExpressions.
	 *
	 * @param MaterialPath - Full path to the material
	 * @param LayerName - Layer name for this weight node
	 * @param PreviewWeight - Preview weight value (0.0-1.0)
	 * @param PosX - X position in the material graph
	 * @param PosY - Y position in the material graph
	 * @return Expression ID of the created node, or empty string on failure
	 */
	UFUNCTION(BlueprintCallable, Category ="VibeUE|LandscapeMaterial")
	static FString CreateLayerWeightNode(
		const FString& MaterialPath,
		const FString& LayerName,
		float PreviewWeight = 1.0f,
		int32 PosX = -400,
		int32 PosY = 0);

public:
	// Restored from upstream: the native replacement does not do all of this (cut audit).
	/**
	 * Connect an expression output to a specific layer input on a blend node.
	 * Maps to action="connect_to_layer_input"
	 *
	 * @param MaterialPath - Full path to the material
	 * @param SourceExpressionId - Source expression ID
	 * @param SourceOutput - Output name (empty for first output)
	 * @param BlendNodeId - Blend node ID
	 * @param LayerName - Layer name to connect to
	 * @param InputType - "Layer" (the layer's color input) or "Height" (only LB_HeightBlend layers
	 *                   have one; "Alpha" is an alias). Height/slope masks go to a height-blend layer's Height.
	 * @return True if connection was made
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category ="VibeUE|LandscapeMaterial")
	static bool ConnectToLayerInput(
		const FString& MaterialPath,
		const FString& SourceExpressionId,
		const FString& SourceOutput,
		const FString& BlendNodeId,
		const FString& LayerName,
		const FString& InputType = TEXT("Layer"));

};
