---
name: landscape-materials/workflows
description: Landscape material workflows — create a complete landscape material, set up layers with textures, create layer info objects, assign material to landscape, layer-weight/coords/sample nodes, grass output, and a full end-to-end build.
---

## Workflows

### Create Complete Landscape Material

**IMPORTANT: Split into multiple code blocks to avoid timeout!**

**Block 1 — Create and connect (fast):**
```python
import json
MT = "EditorToolset.MaterialToolset"; OT = "EditorToolset.ObjectToolset"
MAT = {"refPath": "/Game/Materials/M_Terrain.M_Terrain"}
LB = "/Script/Landscape.MaterialExpressionLandscape"

# 1. Create material — Epic's MaterialToolset (returns a {refPath} handle). A landscape material
#    starts as a plain surface material; the landscape nodes are expressions added below.
await call_tool(f"{MT}.CreateMaterial", folderPath="/Game/Materials", assetName="M_Terrain")

# 2. Create layer blend node (returns the expression ref — keep it)
blend = await call_tool(f"{MT}.AddExpression", materialOrFunction=MAT,
                        expressionClass={"refPath": LB + "LayerBlend"}, x=-400, y=0)

# 3. Add layers (to add one later: GetProperties "Layers", then append — insertion only)
await call_tool(f"{OT}.SetProperties", instance=blend, values=json.dumps({"Layers": [
    {"LayerName": "Grass", "BlendType": "LB_WeightBlend", "PreviewWeight": 1.0},
    {"LayerName": "Rock", "BlendType": "LB_WeightBlend", "PreviewWeight": 1.0},
    {"LayerName": "Sand", "BlendType": "LB_WeightBlend", "PreviewWeight": 1.0}]}))

# 4. Save (but do NOT compile here — compile is slow!)
await call_tool("EditorToolset.AssetToolset.SaveAsset", assetPath="/Game/Materials/M_Terrain")
print("Created: /Game/Materials/M_Terrain")
```

**Step 5 — connect the blend to the BaseColor output**, with the `blend` expression ref from step 2:

```
call_tool MaterialToolset.ConnectToOutput {"expression": {"refPath": "<the LandscapeLayerBlend expression>"},
                                           "outputName": "", "materialProperty": "MP_BaseColor"}
```

> `AddExpression` returns the expression ref (`{"refPath": ...}`) — keep it for later calls. If you lost
> it, list the graph with `MaterialToolset.GetExpressions` and pick the node by its class.

**Block 2 — Compile (slow, run separately).** Epic's, via `call_tool` —
`MaterialService.compile_material` is gone:
```
call_tool MaterialToolset.Recompile {"materialOrFunction": {"refPath": "/Game/Materials/M_Terrain.M_Terrain"}}
```

**Block 3 — Layer info objects and assignment:**
```python
import unreal

sub = unreal.get_editor_subsystem(unreal.LandscapeEditorSubsystem)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()

# Create layer info objects — keep the returned assets (li.get_path_name()), never guess paths
infos = {}
for name in ("Grass", "Rock", "Sand"):
    li = sub.create_target_layer_info(name, "/Game/Landscape")
    li.set_blend_method(unreal.LandscapeTargetLayerBlendMethod.FINAL_WEIGHT_BLENDING)  # NONE for non-weight-blended; confirm enum names via dir()
    unreal.EditorAssetLibrary.save_loaded_asset(li)
    infos[name] = li

# Assign material with the layer infos
land = next(a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Landscape)
            if a.get_actor_label() == "MyTerrain")
mat = unreal.load_asset("/Game/Materials/M_Terrain")
with unreal.ScopedEditorTransaction("Assign landscape material"):
    land.set_editor_property("landscape_material", mat)
    have = {str(n) for n in sub.get_target_layer_names(land)}
    for n in sub.retrieve_target_layer_names_from_material(mat):
        li = infos[str(n)]   # the info's layer name must equal n
        if str(n) in have: sub.set_target_layer_info(land, n, li)
        else: sub.add_target_layer(land, n, li)

# VERIFY layers were added (a wrongly named layer info leaves the layer empty)
info = unreal.LandscapeService.get_landscape_info("MyTerrain")
print(f"Layers: {[l.layer_name for l in info.layers]}")
```

### Setup Layer with Textures

```python
import json
MT = "EditorToolset.MaterialToolset"; OT = "EditorToolset.ObjectToolset"
MAT = {"refPath": "/Game/Materials/M_Terrain.M_Terrain"}
LB = "/Script/Landscape.MaterialExpressionLandscape"
blend = ...  # the LandscapeLayerBlend expression ref returned by AddExpression

# UV coords (tiling scale) + texture sampler for one layer
coords = await call_tool(f"{MT}.AddExpression", materialOrFunction=MAT,
                         expressionClass={"refPath": LB + "LayerCoords"}, x=-800, y=0)
await call_tool(f"{OT}.SetProperties", instance=coords, values=json.dumps({"MappingScale": 0.01}))
tex = await call_tool(f"{MT}.AddExpression", materialOrFunction=MAT,
                      expressionClass={"refPath": "/Script/Engine.MaterialExpressionTextureSample"}, x=-600, y=0)
await call_tool(f"{OT}.SetProperties", instance=tex,
                values=json.dumps({"Texture": {"refPath": "/Game/Textures/T_Grass_D.T_Grass_D"}}))

# coords -> sampler UVs, sampler -> the blend's "Layer <Name>" pin (normal/roughness samplers are wired the same way)
await call_tool(f"{MT}.ConnectExpressions", fromExpression=coords, fromOutputName="", toExpression=tex, toInputName="UVs")
await call_tool(f"{MT}.ConnectExpressions", fromExpression=tex, fromOutputName="RGB", toExpression=blend, toInputName="Layer Grass")

# Compile: Epic's MaterialToolset owns this (MaterialService is gone) — run it as its own step:
#   call_tool MaterialToolset.Recompile {"materialOrFunction": {"refPath": "<mat_path>.<MatName>"}}
# (mid-script equivalent: vibeue.exec_tool("EditorToolset.MaterialToolset", "Recompile", {...}))
```

### Create Layer Info Objects

```python
import unreal

sub = unreal.get_editor_subsystem(unreal.LandscapeEditorSubsystem)

# Create layer info objects for each layer
for name in ("Grass", "Rock", "Sand"):
    li = sub.create_target_layer_info(name, "/Game/Landscape")
    li.set_blend_method(unreal.LandscapeTargetLayerBlendMethod.FINAL_WEIGHT_BLENDING)  # NONE for non-weight-blended; confirm enum names via dir()
    unreal.EditorAssetLibrary.save_loaded_asset(li)
    print(f"{name}: {li.get_path_name()}")
```

### Assign Material to Landscape

```python
import unreal

sub = unreal.get_editor_subsystem(unreal.LandscapeEditorSubsystem)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
land = next(a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Landscape)
            if a.get_actor_label() == "MyTerrain")
mat = unreal.load_asset("/Game/Materials/M_Terrain")

# IMPORTANT: use the EXACT layer info assets (li.get_path_name() from create_target_layer_info);
# each info's layer name must equal the material's layer name, or the layer stays EMPTY!
LAYER_INFO_PATHS = {"Grass": "<Grass layer info path>", "Rock": "<Rock layer info path>", "Sand": "<Sand layer info path>"}
with unreal.ScopedEditorTransaction("Assign landscape material"):
    land.set_editor_property("landscape_material", mat)
    have = {str(n) for n in sub.get_target_layer_names(land)}
    for n in sub.retrieve_target_layer_names_from_material(mat):
        li = unreal.load_asset(LAYER_INFO_PATHS[str(n)])
        if str(n) in have: sub.set_target_layer_info(land, n, li)
        else: sub.add_target_layer(land, n, li)

# Always verify after assignment
info = unreal.LandscapeService.get_landscape_info("MyTerrain")
if not info.layers:
    print("WARNING: No layers found - check the layer info assets!")
```

### Manual Texture Connection with Layer Coords

The recipe above as plain `call_tool` calls. Every node is an Epic expression, so the sampler, its
texture and its connections all go through `MaterialToolset` / `ObjectToolset`.

```
# UV coords: AddExpression returns the expression ref; the tiling scale is a property
call_tool MaterialToolset.AddExpression {"materialOrFunction": {"refPath": "/Game/Materials/M_Terrain.M_Terrain"},
    "expressionClass": "/Script/Landscape.MaterialExpressionLandscapeLayerCoords", "x": -800, "y": 0}
call_tool ObjectToolset.SetProperties {"instance": {"refPath": "<the LayerCoords>"}, "values": "{\"MappingScale\": 0.01}"}

# Texture sample: AddExpression returns the expression object — keep its refPath.
call_tool MaterialToolset.AddExpression {"materialOrFunction": {"refPath": "/Game/Materials/M_Terrain.M_Terrain"},
    "expressionClass": "/Script/Engine.MaterialExpressionTextureSample", "x": -600, "y": 0}

# Its texture is just a property (set_expression_property is gone):
call_tool ObjectToolset.SetProperties {"instance": {"refPath": "<the TextureSample>"},
    "values": "{\"Texture\": {\"refPath\": \"/Game/Textures/T_Grass_D.T_Grass_D\"}}"}
# `values` is a JSON string, not an object.

# Wire sampler -> the LayerBlend's layer input, by PIN NAME.
# GetExpressionInputNames on the LandscapeLayerBlend gives the exact per-layer pin spelling
# (e.g. "Layer Grass", or "Height Rock" for an LB_HeightBlend layer).
call_tool MaterialToolset.GetExpressionInputNames {"expression": {"refPath": "<the LandscapeLayerBlend>"}}
call_tool MaterialToolset.ConnectExpressions {"fromExpression": {"refPath": "<the TextureSample>"},
    "fromOutputName": "RGB", "toExpression": {"refPath": "<the LandscapeLayerBlend>"}, "toInputName": "<layer pin>"}
```

### Using Layer Weight Nodes (Alternative)

```python
import json
MT = "EditorToolset.MaterialToolset"; OT = "EditorToolset.ObjectToolset"
MAT = {"refPath": "/Game/Materials/M_Terrain.M_Terrain"}
LB = "/Script/Landscape.MaterialExpressionLandscape"

# LandscapeLayerWeight is an alternative to LandscapeLayerBlend
# Use when you need more control over blending logic
grass_weight = await call_tool(f"{MT}.AddExpression", materialOrFunction=MAT,
                               expressionClass={"refPath": LB + "LayerWeight"}, x=-400, y=0)
await call_tool(f"{OT}.SetProperties", instance=grass_weight, values=json.dumps({"ParameterName": "Grass", "PreviewWeight": 1.0}))
rock_weight = await call_tool(f"{MT}.AddExpression", materialOrFunction=MAT,
                              expressionClass={"refPath": LB + "LayerWeight"}, x=-400, y=200)
await call_tool(f"{OT}.SetProperties", instance=rock_weight, values=json.dumps({"ParameterName": "Rock", "PreviewWeight": 0.0}))

# Connect layer weight inputs and chain them together
# (more manual but more flexible than LayerBlend)
```

### Check Existence / Inspect a Layer Info

Existence checks and property reads are Epic's `AssetToolset` / `ObjectToolset`:

```python
import unreal

if not unreal.EditorAssetLibrary.does_asset_exist("/Game/Materials/M_Terrain"):
    await call_tool("EditorToolset.MaterialToolset.CreateMaterial", folderPath="/Game/Materials", assetName="M_Terrain")

if not unreal.EditorAssetLibrary.does_asset_exist("/Game/Landscape/LI_Grass"):
    unreal.get_editor_subsystem(unreal.LandscapeEditorSubsystem).create_target_layer_info("Grass", "/Game/Landscape")

# Only a bare asset name? Search for it (FindAssets sees saved assets only):
hits = await call_tool(f"{AT}.FindAssets", filter={
    "name": "M_Terrain", "assetType": {"refPath": "/Script/Engine.Material"}})

# A layer info's layer name and blend method (BlendMethod None = not weight-blended):
info = await call_tool("EditorToolset.ObjectToolset.GetProperties",
                       instance={"refPath": "/Game/Landscape/LI_Grass.LI_Grass"},
                       properties=["LayerName", "BlendMethod"])
{"hits": hits, "LI_Grass": info}
```

### Create Layer Sample Node

Use `LandscapeLayerSample` to sample a single layer's weight (alternative to LayerBlend/LayerWeight):

```python
import json
MT = "EditorToolset.MaterialToolset"; OT = "EditorToolset.ObjectToolset"
MAT = {"refPath": "/Game/Materials/M_Terrain.M_Terrain"}
LB = "/Script/Landscape.MaterialExpressionLandscape"

# Creates a LandscapeLayerSample node for a specific layer
# Returns the scalar weight of that layer (0-1)
grass_sample = await call_tool(f"{MT}.AddExpression", materialOrFunction=MAT,
                               expressionClass={"refPath": LB + "LayerSample"}, x=-600, y=0)
await call_tool(f"{OT}.SetProperties", instance=grass_sample, values=json.dumps({"ParameterName": "Grass"}))

print("layer sample expression:", grass_sample)
```

To drive blending or masking with that weight, add a Multiply and wire the sample into it.
`grass_sample` is the expression ref (`{"refPath": ...}`) that `MaterialToolset` takes:

```
call_tool MaterialToolset.AddExpression {"materialOrFunction": {"refPath": "/Game/Materials/M_Terrain.M_Terrain"},
    "expressionClass": "/Script/Engine.MaterialExpressionMultiply", "x": -400, "y": 0}
call_tool MaterialToolset.ConnectExpressions {"fromExpression": {"refPath": "<the LandscapeLayerSample>"},
    "fromOutputName": "", "toExpression": {"refPath": "<the Multiply>"}, "toInputName": "A"}
```

### Create Grass Output Node

Create a `LandscapeGrassOutput` node for procedural foliage placement:

```python
import json
MT = "EditorToolset.MaterialToolset"; OT = "EditorToolset.ObjectToolset"
MAT = {"refPath": "/Game/Materials/M_Terrain.M_Terrain"}
LB = "/Script/Landscape.MaterialExpressionLandscape"

# Create grass output with grass type mappings
# Each entry: display name -> ULandscapeGrassType asset
grass_out = await call_tool(f"{MT}.AddExpression", materialOrFunction=MAT,
                            expressionClass={"refPath": LB + "GrassOutput"}, x=-200, y=400)
await call_tool(f"{OT}.SetProperties", instance=grass_out, values=json.dumps({"GrassTypes": [
    {"Name": "Grass", "GrassType": {"refPath": "/Game/Foliage/GT_Grass.GT_Grass"}},
    {"Name": "Flowers", "GrassType": {"refPath": "/Game/Foliage/GT_Flowers.GT_Flowers"}}]}))

print("grass output expression:", grass_out)
```

Connect the layer weights into it. Each grass type has an input pin named after it; use the refs
`AddExpression` returned for the grass output and the layer-weight nodes:

```
call_tool MaterialToolset.ConnectExpressions {"fromExpression": {"refPath": "<Grass layer-weight node>"},
    "fromOutputName": "", "toExpression": {"refPath": "<the LandscapeGrassOutput>"}, "toInputName": "Grass"}
call_tool MaterialToolset.ConnectExpressions {"fromExpression": {"refPath": "<Flowers layer-weight node>"},
    "fromOutputName": "", "toExpression": {"refPath": "<the LandscapeGrassOutput>"}, "toInputName": "Flowers"}

# Then compile, as its own step:
call_tool MaterialToolset.Recompile {"materialOrFunction": {"refPath": "<material>"}}
```

### Complete End-to-End Workflow

```python
import json, unreal
MT = "EditorToolset.MaterialToolset"; OT = "EditorToolset.ObjectToolset"
MAT = {"refPath": "/Game/Materials/M_Terrain.M_Terrain"}
LB = "/Script/Landscape.MaterialExpressionLandscape"
mat_path = "/Game/Materials/M_Terrain"

# 1. Create the material (Epic) — the landscape nodes are expressions added below
await call_tool(f"{MT}.CreateMaterial", folderPath="/Game/Materials", assetName="M_Terrain")

# 2. Create blend node and add layers
blend = await call_tool(f"{MT}.AddExpression", materialOrFunction=MAT,
                        expressionClass={"refPath": LB + "LayerBlend"}, x=-400, y=0)
await call_tool(f"{OT}.SetProperties", instance=blend, values=json.dumps({"Layers": [
    {"LayerName": "Grass", "BlendType": "LB_WeightBlend", "PreviewWeight": 1.0},
    {"LayerName": "Rock", "BlendType": "LB_WeightBlend", "PreviewWeight": 1.0}]}))

# 3. Setup textures for each layer (UV coords -> sampler -> the blend's "Layer <Name>" pin)
for i, (layer, tex_path) in enumerate((("Grass", "/Game/Textures/T_Grass_D.T_Grass_D"),
                                       ("Rock", "/Game/Textures/T_Rock_D.T_Rock_D"))):
    coords = await call_tool(f"{MT}.AddExpression", materialOrFunction=MAT,
                             expressionClass={"refPath": LB + "LayerCoords"}, x=-800, y=i * 300)
    await call_tool(f"{OT}.SetProperties", instance=coords, values=json.dumps({"MappingScale": 0.01}))
    tex = await call_tool(f"{MT}.AddExpression", materialOrFunction=MAT,
                          expressionClass={"refPath": "/Script/Engine.MaterialExpressionTextureSample"}, x=-600, y=i * 300)
    await call_tool(f"{OT}.SetProperties", instance=tex, values=json.dumps({"Texture": {"refPath": tex_path}}))
    await call_tool(f"{MT}.ConnectExpressions", fromExpression=coords, fromOutputName="", toExpression=tex, toInputName="UVs")
    await call_tool(f"{MT}.ConnectExpressions", fromExpression=tex, fromOutputName="RGB", toExpression=blend, toInputName=f"Layer {layer}")

# 4. Save the graph so far
unreal.EditorAssetLibrary.save_asset(mat_path)

# --- STEPS 5 and 6 ARE EPIC'S: two call_tool calls, each its own step, NOT Python ---
#   a) connect the blend to BaseColor (the `blend` expression ref from step 2):
#        call_tool MaterialToolset.ConnectToOutput
#            {"expression": {"refPath": "<LandscapeLayerBlend>"}, "outputName": "",
#             "materialProperty": "MP_BaseColor"}
#   b) compile (slow — on its own):
#        call_tool MaterialToolset.Recompile {"materialOrFunction": {"refPath": "<mat_path>.<MatName>"}}
# Then continue with the Python below.

# 6. Create layer info objects — keep the returned assets
sub = unreal.get_editor_subsystem(unreal.LandscapeEditorSubsystem)
infos = {}
for name in ("Grass", "Rock"):
    li = sub.create_target_layer_info(name, "/Game/Landscape")
    li.set_blend_method(unreal.LandscapeTargetLayerBlendMethod.FINAL_WEIGHT_BLENDING)  # NONE for non-weight-blended; confirm enum names via dir()
    unreal.EditorAssetLibrary.save_loaded_asset(li)
    infos[name] = li

# 7. Create landscape (create_landscape takes the CENTRE: 25200 = 8*63*100/2, so the min corner is (0, 0))
L = sub.create_landscape(unreal.Vector(25200, 25200, 0), unreal.Rotator(0, 0, 0), unreal.Vector(100, 100, 100),
                         unreal.IntPoint(8, 8), 1, 63, None, 0)   # ncx/ncy, sections, quads, material, wp_grid_size
L.set_actor_label("MyTerrain")

# 8. Assign material with layer info
mat = unreal.load_asset(mat_path)
with unreal.ScopedEditorTransaction("Assign landscape material"):
    L.set_editor_property("landscape_material", mat)
    have = {str(n) for n in sub.get_target_layer_names(L)}
    for n in sub.retrieve_target_layer_names_from_material(mat):
        if str(n) in have: sub.set_target_layer_info(L, n, infos[str(n)])
        else: sub.add_target_layer(L, n, infos[str(n)])

print("Landscape setup complete!")
```

---


