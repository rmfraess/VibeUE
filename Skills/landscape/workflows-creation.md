---
name: workflows-creation
description: Workflows for creating landscapes, querying landscape info, importing/exporting heightmaps (with resolution matching), recreating splines across landscapes, copying terrain heights and paint layers between landscapes, calculating side-by-side offsets, and reading height data in a region
---

# Workflows — Creation, Import, and Copy

### Create Basic Landscape

```python
import unreal

ls = unreal.get_editor_subsystem(unreal.LandscapeEditorSubsystem)
# location is the landscape CENTRE: corner = centre - (ncx*sections*quads/2, ncy*sections*quads/2) * scale_xy
# (25200 = 8*63*100/2, so the min corner lands on (0, 0))
L = ls.create_landscape(
    unreal.Vector(25200, 25200, 0),
    unreal.Rotator(0, 0, 0),
    unreal.Vector(100, 100, 100),
    unreal.IntPoint(8, 8),   # component count x, y
    1,                       # sections per component
    63,                      # quads per section
    None,                    # material (assign later)
    0)                       # wp_grid_size (components per proxy grid cell in a World Partition level)
L.set_actor_label("MyTerrain")
print(f"Created: {L.get_actor_label()}")
```

### Get Landscape Info

```python
import unreal

# get_landscape_info returns LandscapeInfo_Custom (NO success field)
info = unreal.LandscapeService.get_landscape_info("MyTerrain")
# Check actor_label to verify it found the landscape
if info.actor_label:
    print(f"Label: {info.actor_label}")
    print(f"Scale: {info.scale}")
    print(f"Components: {info.num_components}")
    print(f"Resolution: {info.resolution_x}x{info.resolution_y}")
    print(f"Subsections: {info.num_subsections}")
    print(f"Subsection Size: {info.subsection_size_quads}")
    print(f"Material: {info.material_path}")
```

### Import Heightmap (with Resolution Matching)

**Recommended: Request heightmap at exact landscape resolution**
```python
import unreal

# 1. Decide landscape config
comp_x, comp_y = 8, 8
quads = 63
sections = 2

# 2. Calculate required resolution
res_x = comp_x * quads * sections + 1
res_y = comp_y * quads * sections + 1
print(f"Need {res_x}x{res_y} heightmap")
# → 1009×1009

# 3. Use terrain_data MCP tool with resolution=1009 to download the heightmap
# (The AI calls terrain_data generate_heightmap with resolution=1009)

# 4. Create landscape
ls = unreal.get_editor_subsystem(unreal.LandscapeEditorSubsystem)
sx = sy = 100
centre = unreal.Vector((res_x - 1) * sx / 2, (res_y - 1) * sy / 2, 0)   # min corner at (0, 0)
L = ls.create_landscape(
    centre,
    unreal.Rotator(0, 0, 0),
    unreal.Vector(sx, sy, 200),  # Z=200 for mountainous terrain
    unreal.IntPoint(comp_x, comp_y),
    sections,
    quads,
    None,   # material
    0)      # wp_grid_size
L.set_actor_label("MtFuji")

# 5. Import — sizes match perfectly
ls.import_heightmap_from_file(L, ls.get_active_edit_layer(L), "C:/Heightmaps/mt_fuji_1009x1009.png", False, False)
```

**Fallback: Resize existing heightmap to match landscape**
```python
import unreal

# If you already have a heightmap at the wrong resolution
import struct
d = open("C:/Heightmaps/terrain_1081x1081.png", "rb").read(26)
w, h = struct.unpack(">II", d[16:24])   # PNG header
print(f"Source: {w}x{h}")  # 1081×1081

# Get the landscape's actual resolution
info = unreal.LandscapeService.get_landscape_info("MyTerrain")
target_w = info.resolution_x  # e.g. 1009
target_h = info.resolution_y

# Resize the heightmap (bilinear interpolation, preserves terrain shape)
resize_result = unreal.LandscapeService.resize_heightmap(
    "C:/Heightmaps/terrain_1081x1081.png",
    target_w, target_h
    # OutputPath auto-generated as terrain_1081x1081_1009x1009.png
)
if resize_result.success:
    print(f"Resized to {resize_result.new_dimensions}")
    print(f"Output: {resize_result.output_file}")
    # Now import the resized file
    ls = unreal.get_editor_subsystem(unreal.LandscapeEditorSubsystem)
    L = next(l for l in ls.get_landscapes() if l.get_actor_label() == "MyTerrain")
    ls.import_heightmap_from_file(L, ls.get_active_edit_layer(L), resize_result.output_file, False, False)
```

**Alternative: Find a landscape config that matches your heightmap**
```python
import unreal

# If you want to create a landscape that matches an existing heightmap
import struct
d = open("C:/Heightmaps/terrain_505x505.png", "rb").read(26)
W, H = struct.unpack(">II", d[16:24])   # PNG header
# (total_components, ncx, ncy, quads, sections) for every config whose resolution matches
cfg = [((W - 1) // (q * s) * ((H - 1) // (q * s)), (W - 1) // (q * s), (H - 1) // (q * s), q, s)
       for s in (1, 2) for q in (7, 15, 31, 63, 127, 255)
       if (W - 1) % (q * s) == 0 and (H - 1) % (q * s) == 0]
best = min(cfg) if cfg else None   # fewest components
if best:
    print(f"Config: {best[1]}x{best[2]} components, {best[3]} quads/section, {best[4]} section/component → {W}x{H}")
    # Create landscape using this config, then import directly
```

### Export Heightmap

```python
import unreal

unreal.LandscapeService.export_heightmap("MyTerrain", "C:/Heightmaps/terrain_export.raw")
```

### Recreate Splines from One Landscape to Another

```python
import unreal

svc = unreal.LandscapeService

# 1. Get source spline data
src = svc.get_spline_info("SourceLandscape")

# 2. Create control points on destination (use pt.layer_name, NOT pt.paint_layer_name)
#    IMPORTANT: keyword is world_location=, NOT location=
point_mapping = {}
for pt in src.control_points:
    res = svc.create_spline_point(
        "DestLandscape", world_location=pt.location,
        width=pt.width, side_falloff=pt.side_falloff, end_falloff=pt.end_falloff,
        paint_layer_name=pt.layer_name if pt.layer_name else "",
        raise_terrain=pt.raise_terrain, lower_terrain=pt.lower_terrain
    )
    if res.success:
        point_mapping[pt.point_index] = res.point_index

# 3. Connect segments with EXACT tangent lengths (BOTH start and end)
for seg in src.segments:
    if seg.start_point_index in point_mapping and seg.end_point_index in point_mapping:
        svc.connect_spline_points(
            "DestLandscape",
            point_mapping[seg.start_point_index],
            point_mapping[seg.end_point_index],
            tangent_length=seg.start_tangent_length,
            end_tangent_length=seg.end_tangent_length,  # MUST pass both!
            paint_layer_name=seg.layer_name if seg.layer_name else "",
            raise_terrain=seg.raise_terrain, lower_terrain=seg.lower_terrain
        )

# 4. Set EXACT rotations AFTER all connections
#    CRITICAL: connect_spline_points triggers auto-calc rotation,
#    so rotations must be applied LAST to stick
#    IMPORTANT: keyword is world_location=, NOT location=
for pt in src.control_points:
    if pt.point_index in point_mapping:
        svc.modify_spline_point(
            "DestLandscape", point_mapping[pt.point_index],
            world_location=pt.location,
            rotation=pt.rotation, auto_calc_rotation=False
        )

# 5. Copy spline meshes from source segments
for seg in src.segments:
    if seg.spline_meshes and len(seg.spline_meshes) > 0:
        svc.set_spline_segment_meshes(
            "DestLandscape", seg.segment_index, seg.spline_meshes
        )

# 6. Copy control point meshes from source points
for pt in src.control_points:
    if pt.mesh_path:
        svc.set_spline_point_mesh(
            "DestLandscape", point_mapping[pt.point_index],
            pt.mesh_path, pt.mesh_scale, pt.segment_mesh_offset
        )

# 7. Apply to deform terrain
# First create a Splines-type edit layer on the destination in Landscape mode.
# An ordinary sculpt layer named "Splines" is not a reserved spline layer.
assert svc.apply_splines_to_landscape("DestLandscape")
```

> **Order matters:** Create points → Connect segments → Set rotations → Set meshes → Apply.
> `connect_spline_points` overwrites control point rotations via auto-calc.
> Always set explicit rotations as the LAST step before apply.

### Copy Terrain Between Landscapes

```python
import unreal

# Method 1: Using get_height_in_region / set_height_in_region (no file I/O)
info = unreal.LandscapeService.get_landscape_info("SourceLandscape")
res_x = info.resolution_x
res_y = info.resolution_y

# Read all heights as world-space Z values (landscape-local vertex coords)
heights = unreal.LandscapeService.get_height_in_region("SourceLandscape", 0, 0, res_x, res_y)
if len(heights) > 0:
    # Write to destination (must have same resolution)
    unreal.LandscapeService.set_height_in_region("DestLandscape", 0, 0, res_x, res_y, heights)

# Method 2: Using export/import via temp file
import os
temp_path = os.path.join(unreal.Paths.project_saved_dir(), "temp_heightmap.raw")
unreal.LandscapeService.export_heightmap("SourceLandscape", temp_path)
ls = unreal.get_editor_subsystem(unreal.LandscapeEditorSubsystem)
dest = next(l for l in ls.get_landscapes() if l.get_actor_label() == "DestLandscape")
ls.import_heightmap_from_file(dest, ls.get_active_edit_layer(dest), temp_path, False, False)
os.remove(temp_path)  # Clean up
os.remove(os.path.splitext(temp_path)[0] + ".json")  # .raw export writes a width/height/bpp sidecar
```

### Copy Paint Layers Between Landscapes

```python
import unreal

src = "SourceLandscape"
dst = "DestLandscape"
info = unreal.LandscapeService.get_landscape_info(src)
ls = unreal.get_editor_subsystem(unreal.LandscapeEditorSubsystem)
src_l = next(l for l in ls.get_landscapes() if l.get_actor_label() == src)
dst_l = next(l for l in ls.get_landscapes() if l.get_actor_label() == dst)
layers = [(str(n), ls.get_target_layer_info(src_l, n)) for n in ls.get_target_layer_names(src_l)]

# 1. Add all layers to destination
for name, li in layers:
    if li:
        ls.add_target_layer(dst_l, li.get_layer_name(), li)   # fallback name: li.get_editor_property('layer_name')

# 2. Copy weights for each layer
for name, li in layers:
    weights = unreal.LandscapeService.get_weights_in_region(
        src, name, 0, 0, info.resolution_x, info.resolution_y)
    if weights and sum(weights) > 0:
        unreal.LandscapeService.set_weights_in_region(
            dst, name, 0, 0, info.resolution_x, info.resolution_y, weights)
        print(f"Copied weights for {name}")
```

### Calculate Landscape Offset for Side-by-Side

```python
import unreal

# Correct way to position a duplicate landscape next to the source:
info = unreal.LandscapeService.get_landscape_info("SourceLandscape")
# Total world size = (resolution - 1) * scale component
landscape_width = (info.resolution_x - 1) * info.scale.x  # e.g. (1009-1)*100 = 100800
new_x = info.location.x + landscape_width  # place immediately to the right
new_loc = unreal.Vector(new_x, info.location.y, info.location.z)
# Do NOT use arbitrary offsets like 110000 — calculate from actual dimensions
```

### Read Height Data in Region

```python
import unreal

# Read a 100x100 vertex region starting at vertex (0, 0)
# Returns world-space Z heights as a flat array (row-major)
heights = unreal.LandscapeService.get_height_in_region("MyTerrain", 0, 0, 100, 100)
if len(heights) > 0:
    print(f"Read {len(heights)} height values")
    print(f"First height: {heights[0]}")
    print(f"Last height: {heights[-1]}")

# Read the ENTIRE landscape heightmap
info = unreal.LandscapeService.get_landscape_info("MyTerrain")
all_heights = unreal.LandscapeService.get_height_in_region(
    "MyTerrain", 0, 0, info.resolution_x, info.resolution_y)
```
