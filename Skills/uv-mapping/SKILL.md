---
name: uv-mapping
display_name: UV Mapping
description: Inspect, generate, transform, and visualize UV channels on StaticMesh assets (UVMappingService). Use when the user asks to check/add/remove UV channels, auto-unwrap UVs, transform (tile/offset/rotate) UVs, check UV health/islands, or export a UV layout image.
vibeue_classes:
  - UVMappingService
unreal_classes:
  - StaticMesh
  - MeshDescription
  - StaticMeshAttributes
  - StaticMeshOperations
keywords:
  - uv
  - uvs
  - uv mapping
  - uv channel
  - lightmap
  - lightmap uvs
  - unwrap
  - auto unwrap
  - texel density
  - uv overlap
  - uv layout
  - retopo uvs
  - texture coordinates
  - uv pack
---

> 🧠 **Brains complement:** IF an `unreal-engine-skills-manager` tool (external MCP) exists in this session, call it with `{action: "load", skill: "meshes-static-and-skeletal"}` for UE domain knowledge on this topic — correct APIs, architecture, best practices — and treat it as the rubric for any review / "best practices" question. If no such tool is available (e.g. running under Claude Code or Codex without that MCP), skip this line entirely and proceed with this skill alone — do NOT attempt the call.

# UV Mapping Skill

For seams on imported organic assets, first distinguish UV boundaries from split
corner normals, image-border wrapping and invalid material-section indices. See
the materials skill's `references/generated-organic-surfaces.md` and the
asset-management skill's `references/mesh-reimport-materials.md`. Do not unwrap
an approved mesh merely because its shading has a sharp band.

`UVMappingService` provides selected UV editing and layout export for StaticMesh assets. Native StaticMeshEditorSubsystem and GeometryScript APIs cover inspection (`ModelingService.get_uv_stats`) and other UV and lightmap operations. Save edited assets after a batch.

## When NOT to Use This Service

UV transforms are the **wrong tool** for fixing a tiling material that looks bad on a basic shape (cube, sphere, cylinder, cone). Symptoms that say "this is not a UV problem":

- "The bricks face the wrong way on this face."
- "The bricks tile fine on the side but look stretched on the cap."
- "It tiles correctly on one shape but not another with the same material."

These are **material-orientation problems**, not UV-layout problems. A single 2D UV transform can't satisfy multiple face orientations on a 3D shape — whichever projection you pick, at least one face will be wrong. **Reach for one of these instead:**

1. **Per-actor child Material Instance with `Tiling` overridden.** If the parent material exposes a `Tiling` scalar (Megascans `M_MS_Srf` does), create a child MI with the right multiplier and assign it to the specific actor. The mesh keeps its stock UVs.
2. **World-aligned / triplanar material.** A material that samples textures projected from world axes and blends by world normal. Every face on every shape shows correctly oriented bricks at the right size, regardless of mesh UVs. UE provides `/Engine/Functions/Engine_MaterialFunctions02/Texturing/WorldAlignedTexture` and `WorldAlignedNormal` for this.

`UVMappingService` is the right tool for:

- UV layout export for documentation
- Selected region, island and polygon-group UV edits, and packing

Use native StaticMeshEditorSubsystem / GeometryScript APIs for channel addition, removal and count changes, copying, unwrapping, transforms and lightmap settings, and `ModelingService.get_uv_stats` for texel-density auditing.

If the user asks to "fix UVs" on a basic shape with a tiling material, ask whether they want material-side or mesh-side first — defaulting to mesh-side will burn iterations.

## Critical Rules

### StaticMesh Only (Mostly)

Detailed inspection and all mutating operations target `UStaticMesh`. SkeletalMesh meshes are **not supported** by any `UVMappingService` method — the editing path goes through `FSkeletalMeshLODModel` and is not yet implemented. If you're prompted to UV-map a skeletal mesh, stop and tell the user.

### Save After Edits

`UVMappingService` calls `MarkPackageDirty()` and `PostEditChange()` on every successful mutation but does not save. After a batch of edits:

```python
# Set lightmap build settings with native StaticMeshEditorSubsystem.get_lod_build_settings / set_lod_build_settings and mesh lightmap properties.
# Save once at the end:
unreal.EditorAssetLibrary.save_asset("/Game/Meshes/SM_Wall")
```

### Health Check Before Editing

Always inspect first. `ModelingService.get_uv_stats` reports per channel the island count, unset triangles, coverage, overlap and texel density; the lightmap config is the mesh's `light_map_coordinate_index` / `light_map_resolution` plus `StaticMeshEditorSubsystem.get_lod_build_settings`. This prevents wasted work like generating lightmap UVs into an index that already has good UVs. `uv_stats` below is reused by the workflows:

```python
import unreal

def uv_stats(mesh_path, lod, ch):
    svc = unreal.ModelingService
    mesh = unreal.load_asset(mesh_path)
    h = svc.create_mesh().handle
    try:
        unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(mesh, svc.get_dynamic_mesh(h), unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD(lod_index=lod))
        return svc.get_uv_stats(h, ch, 512)
    finally:
        svc.release_mesh(h)

mesh = unreal.load_asset("/Game/Meshes/SM_Wall")
sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
for lod in range(mesh.get_num_lods()):
    for ch in range(sub.get_num_uv_channels(mesh, lod)):
        print(lod, ch, uv_stats("/Game/Meshes/SM_Wall", lod, ch))
lm = mesh.get_editor_property("light_map_coordinate_index"); res = mesh.get_editor_property("light_map_resolution")
bs = sub.get_lod_build_settings(mesh, 0)
```

### Lightmap Channel Convention

By UE convention:
- **Channel 0** holds the texture/material UVs.
- **Channel 1** is the lightmap UV channel.
- `LightMapCoordinateIndex` on the static mesh **must** point at the lightmap channel.

Set `LightMapCoordinateIndex` explicitly when configuring native lightmap build settings. Re-check it after `StaticMeshEditorSubsystem.remove_uv_channel` on a channel above or at the lightmap.

### Channel 0 Cannot Be Removed

Never remove channel 0 (assert `ch != 0` before `StaticMeshEditorSubsystem.remove_uv_channel`) — every static mesh must have at least one UV channel. For replacement, use native StaticMeshEditorSubsystem projection or GeometryScript UV copy with a mesh roundtrip.

### MAX_MESH_TEXTURE_COORDS_MD = 8

Static meshes support at most 8 UV channels. Native `StaticMeshEditorSubsystem.add_uv_channel` adds a channel (and returns False at the limit).

### Overlap Detection Is Raster-Based

`get_uv_stats(h, channel, 512)` rasterises the channel; `overlap_fraction` is measured on that raster (the last argument is its resolution), so use it as a smoke test and raise the resolution for fine overlaps.

### Pack Is Fit-To-Square, Not True Island Packing

`pack_u_vs` rescales the entire channel’s UV bounds into `[padding, 1-padding]^2`. It does **not** detect connected islands and shelf-pack them. Native StaticMeshEditorSubsystem provides box projection.

---

## Per-Region UV Edits (Artist-Grade)

For meshes where one face/region needs a different UV transform than the rest (e.g. a thin disc whose side strip distorts a wall texture while the cap looks fine), use the per-region transforms. They apply an affine transform to a *subset* of vertex instances, leaving everything else untouched.

### Filter By Vertex Normal Direction

`transform_u_vs_by_normal(mesh, lod, channel, axis_x, axis_y, axis_z, min_dot, max_dot, scale_u, scale_v, rotation_degrees, offset_u, offset_v)`

Applies the affine transform only to vertex instances whose normal, dotted with the given axis, falls in `[min_dot, max_dot]`. Common windows for axis = world +Z `(0,0,1)`:

| Window | Selects |
|---|---|
| `min_dot=0.5, max_dot=1.0` | Top (upward-facing) |
| `min_dot=-1.0, max_dot=-0.5` | Bottom (downward-facing) |
| `min_dot=-0.5, max_dot=0.5` | Sides (horizontal-pointing) |
| `min_dot=0.7, max_dot=1.0` | Strict "up" only |

`count_vertices_by_normal(mesh, lod, axis_x, axis_y, axis_z, min_dot, max_dot)` previews the selection without modifying anything — call it first to verify your filter is selecting the right region.

**Worked example — fix a thin disc's side strip aspect ratio while leaving the cap correct:**

```python
# Disc: scale (2.5, 2.5, 0.4), so world dimensions 250cm wide × 40cm tall.
# Side strip is 785cm (circumference) × 40cm (height) — 20:1 aspect.
# Cap is 250×250cm — 1:1 aspect.
# With a uniform brick material at Tiling=4, the cap renders correct bricks
# but the side renders as horizontal pinstripes.

# Step 1: confirm the filter selects only the side
side = unreal.UVMappingService.count_vertices_by_normal(
    "/Game/Meshes/SM_Disc", 0,  0,0,1,  -0.5, 0.5)
print(f"side vertex instances: {side}")  # ~960 for engine cylinder

# Step 2: scale U on the side only by 20x to bring tile aspect back to ~1:1
unreal.UVMappingService.transform_u_vs_by_normal(
    "/Game/Meshes/SM_Disc", 0, 0,
    0,0,1,        # axis = world Z
    -0.5, 0.5,    # side selector
    scale_u=20.0, scale_v=1.0,  # only U
)
unreal.EditorAssetLibrary.save_asset("/Game/Meshes/SM_Disc")
```

> 💡 **Which axis to scale**: figure out the world-space aspect of the misbehaving region, then scale UVs to compensate. If a region is 20× wider than tall in world, scale U by 20× to make the brick texture tile 20× more in U than V — bricks now appear correctly proportioned.

### UV Islands (Most Robust Per-Region Filter)

Use `identify_uv_islands` + `transform_uv_island` when normal-direction or polygon-group filters can't cleanly separate the regions you want to operate on (e.g. an irregular mesh, or a UV layout where similar-normal faces are still distinct islands).

`identify_uv_islands(mesh, lod, channel)` returns an array of `FUVIslandInfo`, one per connected UV island. Two vertex instances are in the same island iff they're reachable through triangles with no UV seam between them.

```python
islands = unreal.UVMappingService.identify_uv_islands("/Game/Meshes/SM_Disc", 0, 0)
for i in islands:
    n, c = i.average_normal, i.world_center
    print(f"Island {i.island_id}: tris={i.triangle_count} VIs={i.vertex_instance_count} "
          f"bounds=({i.min_u:.2f},{i.min_v:.2f})..({i.max_u:.2f},{i.max_v:.2f}) "
          f"normal=({n.x:.2f},{n.y:.2f},{n.z:.2f}) center=({c.x:.0f},{c.y:.0f},{c.z:.0f})")
```

Each island carries its own bounds, area, average world-space center and average normal — use those fields to identify which island is which. For a simple cylinder you'll get 3 islands: top cap (normal ≈ +Z), bottom cap (normal ≈ -Z), and the side strip (largest triangle count).

Once you've identified the island, transform just it:

```python
# Example: scale only the side strip's UV U by 4x — caps and other islands untouched
side = max(islands, key=lambda i: i.triangle_count)
unreal.UVMappingService.transform_uv_island(
    "/Game/Meshes/SM_Disc", 0, 0, side.island_id,
    scale_u=4.0, scale_v=1.0)
```

Island ids are stable as long as the mesh's UV topology hasn't changed since `identify_uv_islands` was called. If you re-run an `auto_unwrap` / `pack` / structural change, call `identify_uv_islands` again for fresh ids before transforming.

### Filter By Polygon Group (Material Slot)

`transform_u_vs_by_polygon_group(mesh, lod, channel, polygon_group_name, scale_u, scale_v, rotation_degrees, offset_u, offset_v)`

Applies the transform only to triangles in the named polygon group (typically a material slot). Use when the mesh has clean per-section slots — e.g. a wall with separate `body` and `trim` material slots, where you want different tiling per slot but the same base material.

`list_polygon_groups(mesh, lod)` returns the slot names available on a LOD. The engine basic shapes have just one slot (`DefaultMaterial`), so this filter is most useful on imported meshes with authored slots.

```python
groups = unreal.UVMappingService.list_polygon_groups("/Game/Meshes/SM_Wall", 0)
# ['Body', 'Trim']
unreal.UVMappingService.transform_u_vs_by_polygon_group(
    "/Game/Meshes/SM_Wall", 0, 0, "Trim",
    scale_u=2.0, scale_v=2.0,  # 2x denser bricks on trim only
)
```

---

## Common Mistakes to Avoid

| WRONG | CORRECT |
|-------|---------|
| `result.success` returning False but no error string | Check `result.message` — every failure includes a reason |
| `pack_uvs` | A trailing `UVs` splits: `pack_u_vs`. `UV` + a word does not: `identify_uv_islands`, `export_uv_layout_image` |
| Transforming the lightmap channel | Avoid — transforms break the channel’s `[0,1]` packing. Use transforms on material UV channels only |
| Forgetting to save after edits | Add `unreal.EditorAssetLibrary.save_asset(...)` after the batch |
| Editing UVs on a skeletal mesh | Not supported — only StaticMesh works |
| Setting `LightMapCoordinateIndex` past the channel count | Add channels first with native `StaticMeshEditorSubsystem.add_uv_channel` |

---

## Return Types — Exact Properties

### FUVMappingResult (every mutating call)
| Property | Type | Description |
|---|---|---|
| `success` | bool | True iff the operation completed |
| `mesh_path` | str | Echoes the input path |
| `message` | str | Failure reason or success summary |

### `ModelingService.get_uv_stats(handle, channel, resolution)` fields
| Property | Description |
|---|---|
| `num_islands` | Connected UV islands |
| `num_unset_triangles` | Triangles without UVs in this channel |
| `coverage` | UV coverage of the unit square |
| `overlap_fraction` | Overlap, measured on the raster |
| `texels_per_cm_at1_k` | Texel density at a 1024px reference |
| `uv_bounds_min`, `uv_bounds_max` | UV bounding box |
| `mesh_area`, `uv_area` | Surface area in world / UV space |

Lightmap settings: use native `StaticMeshEditorSubsystem.get_lod_build_settings` and mesh lightmap properties.

---

## Workflows

### Workflow: Generate Lightmap UVs for a Newly Imported Mesh

```python
import unreal

mesh = "/Game/Meshes/SM_Wall"

# 1. Health check (uv_stats from "Health Check Before Editing")
m = unreal.load_asset(mesh)
sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
print(f"Channels found: {sub.get_num_uv_channels(m, 0)}, lightmap idx: {m.get_editor_property('light_map_coordinate_index')}")

# Configure lightmap generation using native StaticMeshEditorSubsystem
# get_lod_build_settings / set_lod_build_settings and mesh lightmap properties.

# 4. Save
unreal.EditorAssetLibrary.save_asset(mesh)

# 5. Verify
assert uv_stats(mesh, 0, 1).overlap_fraction <= 0.0, "Lightmap still has overlaps after generation"
```

### Workflow: Add a Detail-Texture UV Channel and Tile It

```python
import unreal

mesh = "/Game/Meshes/SM_Wall"

sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
if not 2 < sub.get_num_uv_channels(unreal.load_asset(mesh), 0):
    print("Channel 2 is absent")
# Add a channel with native StaticMeshEditorSubsystem.add_uv_channel.
# Copy and transform UVs with native GeometryScript_UVs and a static/dynamic mesh roundtrip.

unreal.EditorAssetLibrary.save_asset(mesh)
```

### Workflow: Auto-Unwrap a Mesh That Imported with Bad UVs

```python
import unreal

mesh = "/Game/Meshes/SM_RuinPiece"

# Unwrap with native StaticMeshEditorSubsystem.generate_box_uv_channel.

# Pack the result tightly
r = unreal.UVMappingService.pack_u_vs(mesh, 0, 0, padding_percent=1.0)
print(f"PACKED: {r.message}")

# Configure lightmap generation using native StaticMeshEditorSubsystem build settings and mesh lightmap properties.

unreal.EditorAssetLibrary.save_asset(mesh)
```

### Workflow: Visual UV Inspection (AI-Friendly)

```python
import unreal, os

mesh = "/Game/Meshes/SM_Wall"
out = os.path.join(os.environ["TEMP"], "uv_layout.png")

r = unreal.UVMappingService.export_uv_layout_image(
    mesh, lod_index=0, channel_index=1, output_path=out, image_size=1024)
print(f"EXPORTED: {r.message}")
# Then read the PNG back to inspect it visually (Read the file path directly,
# or surface it through the engine's screenshot/image toolset).
```

### Workflow: Batch Lightmap Pass on All Meshes in a Folder

```python
import unreal

# list_assets returns a flat list of OBJECT paths ("/Game/.../SM_Wall.SM_Wall") — strip the
# suffix for VibeUE, and read the class off the asset registry rather than loading each mesh.
sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
for object_path in unreal.EditorAssetLibrary.list_assets("/Game/Meshes/Architecture"):
    data = unreal.EditorAssetLibrary.find_asset_data(object_path)
    if str(data.asset_class_path.asset_name) != "StaticMesh":
        continue
    path = object_path.rsplit(".", 1)[0]

    m = unreal.load_asset(path)
    lm = m.get_editor_property("light_map_coordinate_index")
    bs = sub.get_lod_build_settings(m, 0)
    if (lm >= sub.get_num_uv_channels(m, 0) or uv_stats(path, 0, lm).overlap_fraction > 0.0
            or not bs.generate_lightmap_u_vs):   # uv_stats from "Health Check Before Editing"
        print(f"NEEDS LIGHTMAP REVIEW: {path}")
        # Configure generation with native StaticMeshEditorSubsystem build settings and mesh lightmap properties.
    else:
        print(f"OK:    {path}")
```

---

## Property Formats

### Projection Types
Native StaticMeshEditorSubsystem supports planar, box, and cylindrical UV projection.


### Lightmap Resolution
Power of two preferred:
- `32`–`64` — background / non-hero
- `128` — typical world geometry
- `256`–`512` — hero props, large architecture

---

## Verification After Edits

For lightmap-related changes, **always re-run `uv_stats`** after the save to verify:

```python
assert uv_stats(mesh, 0, 1).overlap_fraction <= 0.0
assert unreal.load_asset(mesh).get_editor_property("light_map_coordinate_index") == 1
```

For unwrap operations, export the layout PNG and visually inspect it (read the PNG back, or surface it through the engine's screenshot/image toolset) — the cheapest way to confirm the unwrap looks reasonable.

## Sample scripts (run via `execute_python`)

- **`scripts/inspect_uvs.txt`** — inspect UV channels/health and export a UV layout image.

## Reference pages

Open these with the paths below; UE rewrites them to absolute paths when it serves this skill.

- [scripts/inspect_uvs.txt](scripts/inspect_uvs.txt) - runnable sample
