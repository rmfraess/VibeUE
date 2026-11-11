---
name: animation-editing/common-mistakes
description: Animation editing pitfalls — always specify bone space, preview/validate before baking, analyze the reference pose before guessing axes, build a skeleton profile first, and never load assets in loops (crashes); use VibeUE safe-discovery methods.
---

# Animation Editing — Common Mistakes

## Always specify bone space
Every rotation must state `"local"` / `"component"` / `"world"`. Default `"local"` for user intent.

## Edit via preview → validate → bake (not direct apply)
```python
# WRONG — direct edit, no constraint check (e.g. impossible 180° elbow)
ass.apply_bone_rotation(path, "lowerarm_r", unreal.Rotator(0,180,0), "local", 0, -1, True)
# CORRECT
ass.preview_bone_rotation(...); 
if ass.validate_pose(path, True).is_valid: ass.bake_preview_to_keyframes(...)
```

## Don't guess bone axis orientations — read the reference pose
```python
pose = unreal.AnimPoseExtensions.get_reference_pose(unreal.load_asset(skeleton_path))
if "upperarm_r" not in map(str, unreal.AnimPoseExtensions.get_bone_names(pose)):
    raise ValueError("Bone not found in reference pose")
t = unreal.AnimPoseExtensions.get_ref_bone_pose(pose, "upperarm_r")
e = t.rotation.rotator()
print(e.roll, e.pitch, e.yaw)   # baseline to add deltas to
```

## Build a skeleton profile first
Without `create_skeleton_profile(skeleton_path)` there are no constraints to validate against.

## NEVER load assets in loops — it crashes (0xC0000005)
```python
# WRONG — unreal.load_asset() in a loop exhausts memory / access violation
for p in asset_subsystem.list_assets("/Game", recursive=True):
    anim = unreal.load_asset(p)   # CRASH

# CORRECT — Asset Registry discovery (metadata only, no loading)
ar = unreal.AssetRegistryHelpers.get_asset_registry()
anims = [a for a in ar.get_assets_by_class(unreal.TopLevelAssetPath('/Script/Engine', 'AnimSequence'))
         if skeleton_path in str(a.get_tag_value('Skeleton'))]
for a in anims:
    print(a.asset_name, a.package_name)
```
Safe alternatives: the Asset Registry Skeleton-tag query (best), `list_anim_sequences`,
`get_anim_sequence_info`, Epic's `AssetToolset.FindAssets`, or the Asset Registry API (metadata without loading).

## `unreal.Transform` has `.translation`, NOT `.location`
When inspecting a bone transform (reference pose or a baked frame), the struct fields are
`.translation` (Vector), `.rotation` (Quat), `.scale3d` (Vector). `transform.location` raises
`AttributeError: 'Transform' object has no attribute 'location'`. To read a bone's rotation in
degrees, always convert the quaternion:
```python
t = bp.transform                      # FBonePose.transform or a frame transform
e = t.rotation.rotator()             # NOT t.location / NOT t.rotation.euler()
print(e.roll, e.pitch, e.yaw, t.translation)
```

## Duplicating an animation — use native asset Python, not a service method
There is no `duplicate_anim_sequence` on AnimSequenceService. To make a working copy before
editing, use `unreal.EditorAssetLibrary.duplicate_asset(source_path, destination_path)` (or Epic's
`AssetTools` toolset).

## `create_from_pose` / new-asset paths may lack the object suffix
Some creation calls return a package path like `/Game/Anims/AS_Foo` without the `.AS_Foo` object
suffix. If a follow-up call returns NOT FOUND, append the asset name:
`f"{path}.{path.rsplit('/',1)[-1]}"` (or check with `unreal.EditorAssetLibrary.does_asset_exist(path)`).
