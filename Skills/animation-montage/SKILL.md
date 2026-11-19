---
name: animation-montage
display_name: Animation Montages
description: Create and manipulate Animation Montages — sections, slots, segments, branching points, notifies, and blend settings (AnimMontageService). Use when the user asks to create a montage (from an animation or empty), add or link montage sections, set up slots/segments, add branching points, or adjust montage blend timing.
vibeue_classes:
  - AnimMontageService
unreal_classes:
  - AnimMontage
  - AnimNotify
  - AnimNotifyState
keywords:
  - montage
  - animation
  - section
  - slot
  - segment
  - branching point
  - blend
  - combo
  - attack
  - root motion
  - notify
---

> 🧠 **Brains complement:** IF an `unreal-engine-skills-manager` tool (external MCP) exists in this session, call it with `{action: "load", skill: "animation-system"}` for UE domain knowledge on this topic — correct APIs, architecture, best practices — and treat it as the rubric for any review / "best practices" question. If no such tool is available (e.g. running under Claude Code or Codex without that MCP), skip this line entirely and proceed with this skill alone — do NOT attempt the call.

# Animation Montage Skill

Use `AnimMontageService` for section, slot, segment and blend operations; use native APIs for creation, duplication, notifies and editor access.

## Quick Reference - Python API

### Discovery Methods
> **Plain properties are Epic's, not this service's.** Skeleton, playback rate (`RateScale`), blend
> in/out and blend-out trigger time are ordinary `UAnimMontage` UPROPERTYs: read and write them with
> `EditorToolset.ObjectToolset.GetProperties` / `SetProperties` (see *Blend Settings* below), which run
> the engine's `PostEditChangeProperty` fixups. Duplicating is `EditorToolset.AssetToolset.Duplicate`,
> opening the editor is `EditorToolset.EditorAppToolset.OpenEditorForAsset`.

```python
# List all montages (await call_tool inside execute_python)
mon = await call_tool("EditorToolset.AssetToolset.FindAssets", filter={"folderPath": "/Game", "name": "",
        "assetType": {"refPath": "/Script/Engine.AnimMontage"}, "bRecursive": True, "tags": [], "maxResults": 0})

# Montages for a skeleton / using an animation: intersect with the asset's referencers
refs = await call_tool("EditorToolset.AssetToolset.GetReferencers", assetPath=SKELETON_OR_ANIM_PKG)
montages = sorted(set(refs) & set(mon))

# Detailed montage info
m = unreal.load_asset(montage_path); AL = unreal.AnimationLibrary; ev = AL.get_animation_notify_events(m)
info = dict(length=AL.get_sequence_length(m), slots=[str(s) for s in AL.get_montage_slot_names(m)],
            sections=m.get_num_sections(), slot_tracks=len(m.get_editor_property('slot_anim_tracks')),
            notifies=len(ev), blend_in=m.get_default_blend_in_time(), blend_out=m.get_default_blend_out_time(),
            blend_out_trigger=m.get_editor_property('blend_out_trigger_time'),
            skeleton=m.get_editor_property('skeleton').get_path_name())
# Root motion flags: AnimMontageService.get_enable_root_motion_translation / _rotation(montage_path)
```

### Creation Methods
Create with `unreal.AnimMontageFactory` and `unreal.AssetTools.create_asset`; duplicate with `EditorToolset.AssetToolset.Duplicate`.
Notify reads and writes: `unreal.AnimationLibrary`.

### Section Management (CRITICAL for gameplay!)
```python
# List all sections
sections = AnimMontageService.list_sections(montage_path) -> Array[MontageSectionInfo]

# Add new section
AnimMontageService.add_section(montage_path, section_name, start_time) -> bool

# Remove section
AnimMontageService.remove_section(montage_path, section_name) -> bool

# Rename section
AnimMontageService.rename_section(montage_path, old_name, new_name) -> bool

# Move section to new time
AnimMontageService.set_section_start_time(montage_path, section_name, new_time) -> bool

# Get section at time
name = AnimMontageService.get_section_name_at_time(montage_path, time) -> str
```

### Section Linking (Combo/Flow Control)
```python
# Link sections: Section1 -> Section2
AnimMontageService.set_next_section(montage_path, from_section, to_section) -> bool

# Set section to loop
AnimMontageService.set_section_loop(montage_path, section_name, loop=True) -> bool

# Clear link (section ends montage)
AnimMontageService.clear_section_link(montage_path, section_name) -> bool

# Get all section links for flow visualization
links = AnimMontageService.get_all_section_links(montage_path) -> Array[SectionLink]
```

### Animation Segments (Multiple Animations in One Montage)
```python
# List segments in a slot track (native properties; fields: anim_reference, start_pos,
# anim_start_time, anim_end_time, anim_play_rate, looping_count)
segments = unreal.load_asset(montage_path).get_editor_property('slot_anim_tracks')[track_index] \
    .get_editor_property('anim_track').get_editor_property('anim_segments')

# Add animation at specific time
index = AnimMontageService.add_anim_segment(montage_path, track_index, anim_path, start_time, play_rate) -> int

# Remove segment
AnimMontageService.remove_anim_segment(montage_path, track_index, segment_index) -> bool

# Modify segment timing
AnimMontageService.set_segment_start_time(montage_path, track_index, segment_index, time) -> bool
AnimMontageService.set_segment_play_rate(montage_path, track_index, segment_index, rate) -> bool
AnimMontageService.set_segment_loop_count(montage_path, track_index, segment_index, count) -> bool
```

### Slot Tracks
```python
# List slot tracks (native properties)
for i, t in enumerate(unreal.load_asset(montage_path).get_editor_property('slot_anim_tracks')):
    print(i, t.get_editor_property('slot_name'),
          len(t.get_editor_property('anim_track').get_editor_property('anim_segments')))

# Add new slot track
index = AnimMontageService.add_slot_track(montage_path, slot_name) -> int

# Change slot name
AnimMontageService.set_slot_name(montage_path, track_index, new_slot_name) -> bool

# Remove slot track
AnimMontageService.remove_slot_track(montage_path, track_index) -> bool
```

### Blend Settings (and Skeleton, RateScale) — Epic's ObjectToolset
`BlendIn` / `BlendOut` are `FAlphaBlend` structs with `BlendTime` (float) and `BlendOption`
(`Linear`, `Cubic`, `HermiteCubic`, `Sinusoidal`, `QuadraticInOut`, `CubicInOut`, `QuarticInOut`,
`QuinticInOut`, `CircularIn`, `CircularOut`, `CircularInOut`, `ExpIn`, `ExpOut`, `ExpInOut`,
`Custom`); `BlendOutTriggerTime` is a float (-1 = auto). Address struct fields with dotted paths.
These are in-place property writes, not structural edits, so an open montage editor survives them.

```python
# inside execute_python
import json
OT = "EditorToolset.ObjectToolset."
m = {"refPath": "/Game/Montages/AM_Attack.AM_Attack"}     # object path: name repeated after the dot

# Set blend out
AnimMontageService.set_blend_out(montage_path, time, option) -> bool
```
Read blend settings and set the blend-out trigger time with `EditorToolset.ObjectToolset.GetProperties` / `SetProperties`.

### Branching Points (Frame-Accurate Gameplay Events)
```python
# List all branching points: notify events whose montage_tick_type is Branching Point
AL = unreal.AnimationLibrary
bps = [(i, e) for i, e in enumerate(AL.get_animation_notify_events(unreal.load_asset(montage_path)))
       if 'BRANCHING_POINT' in str(e.montage_tick_type).upper()]

# Add branching point for combo input window
index = AnimMontageService.add_branching_point(montage_path, name, trigger_time) -> int

# Remove branching point: remove its notify by raw notify index (i from the list above)
AnimMontageService.remove_notify(montage_path, bps[bp_index][0])
```

### Root Motion
```python
# Enable/disable root motion translation
AnimMontageService.set_enable_root_motion_translation(montage_path, enable) -> bool

# Enable/disable root motion rotation
AnimMontageService.set_enable_root_motion_rotation(montage_path, enable) -> bool
```

### Editor Methods
```python
# Open montage with EditorToolset.EditorAppToolset.OpenEditorForAsset.
# Refresh editor UI ONCE, AFTER all edits are done (see crash warning below)
m = unreal.load_asset(montage_path)
aes = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
aes.close_all_editors_for_asset(m)
aes.open_editor_for_assets([m])
```

> ⚠️ **CRASH WARNING — never structurally edit a montage while its editor is open.**
> Adding/removing **sections, slot tracks, segments, notifies, or branching points** reallocates
> the montage's internal arrays. If the montage editor (Persona) is open, its live preview holds
> pointers into those arrays and the next tick will dereference freed memory → **the editor hard-crashes**.
> Rule: do ALL structural edits FIRST, then close and open the montage editor **exactly once at the very end**.
> Do NOT reopen the editor between edits. (The service now also auto-closes the editor before
> service structural edit as a safety net; native API edits do not use that service safety net. Batch edits
> and open last so the editor is not opening and closing repeatedly.)

## IMPORTANT: Asset Path Format

**All `montage_path` parameters require the FULL asset path (package_name), NOT the folder path.**

```python
# CORRECT: Full asset path
montage_path = "/Game/Montages/AM_Attack"

# WRONG: Folder path
montage_path = "/Game/Montages"  # This is a folder, not an asset!
```

## IMPORTANT: Return Struct Field Names (read before printing results)

These methods return UStruct objects. Access fields with the **exact snake_case names below** —
guessing (`name`, `path`) raises `AttributeError`.
Each struct field is read-only data already populated; just read it.

**`MontageSectionInfo`** (from `list_sections`):
`section_name`, `section_index`, `start_time`, `end_time`, `duration`, `next_section_name`,
`loops`, `segment_count`.

**`SectionLink`** (from `get_all_section_links`): `from_section`, `to_section`, `is_loop`.

> Tip: when unsure, call `get_symbol('unreal.MontageSectionInfo, unreal.SectionLink')`
> — the `Editor Properties` block lists every valid field name. Cheaper than a crash-and-retry loop.

## Key Workflows

### Create Multi-Animation Montage with Sections

```python
import unreal, json

# 1. Create empty montage
skeleton = unreal.load_asset("/Game/Characters/SK_Mannequin")  # skeleton asset, not a skeletal mesh
assert isinstance(skeleton, unreal.Skeleton), "Load a skeleton asset"
factory = unreal.AnimMontageFactory()
factory.set_editor_property("target_skeleton", skeleton)
montage = unreal.AssetToolsHelpers.get_asset_tools().create_asset("AM_ComboAttack", "/Game/Montages", unreal.AnimMontage, factory)
assert montage, "Montage creation failed"
path = montage.get_path_name()

# 2. Add animation segments sequentially (track 0)
unreal.AnimMontageService.add_anim_segment(path, 0, "/Game/Animations/Attack1", 0.0)
unreal.AnimMontageService.add_anim_segment(path, 0, "/Game/Animations/Attack2", 1.0)  
unreal.AnimMontageService.add_anim_segment(path, 0, "/Game/Animations/Attack3", 2.0)

# 3. Add sections at animation boundaries
unreal.AnimMontageService.add_section(path, "Attack1", 0.0)
unreal.AnimMontageService.add_section(path, "Attack2", 1.0)
unreal.AnimMontageService.add_section(path, "Attack3", 2.0)

# 4. Link sections for combo flow
unreal.AnimMontageService.set_next_section(path, "Attack1", "Attack2")
unreal.AnimMontageService.set_next_section(path, "Attack2", "Attack3")
# Attack3 has no next section - ends the montage

# 5. Add branching points for input windows
unreal.AnimMontageService.add_branching_point(path, "Combo1Window", 0.7)
unreal.AnimMontageService.add_branching_point(path, "Combo2Window", 1.7)

# 6. Configure blend settings (plain properties: Epic's ObjectToolset)
await call_tool("EditorToolset.ObjectToolset.SetProperties",
                instance={"refPath": path + "." + path.rsplit("/", 1)[-1]},
                values=json.dumps({"BlendIn.BlendTime": 0.15, "BlendIn.BlendOption": "Cubic",
                                   "BlendOut.BlendTime": 0.2, "BlendOut.BlendOption": "Linear"}))

# 7. Save FIRST, while no editor is open
unreal.EditorAssetLibrary.save_asset(path)

# 8. THEN refresh/open the editor exactly once — never between the edits above
#    (refreshing mid-edit can crash the editor; see CRASH WARNING)
m = unreal.load_asset(path)
aes = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
aes.close_all_editors_for_asset(m)
aes.open_editor_for_assets([m])
```

### Create Layered Upper Body Montage

```python
import unreal, json

# 1. Create montage from animation
animation = unreal.load_asset("/Game/Animations/Reload")
assert isinstance(animation, unreal.AnimSequence), "Load an animation sequence"
factory = unreal.AnimMontageFactory()
factory.set_editor_property("source_animation", animation)
montage = unreal.AssetToolsHelpers.get_asset_tools().create_asset("AM_Reload_UpperBody", "/Game/Montages", unreal.AnimMontage, factory)
assert montage, "Montage creation failed"
path = montage.get_path_name()

# 2. Change slot to UpperBody for layered blending
unreal.AnimMontageService.set_slot_name(path, 0, "UpperBody")

# 3. Configure blend (plain properties: Epic's ObjectToolset)
await call_tool("EditorToolset.ObjectToolset.SetProperties",
                instance={"refPath": path + "." + path.rsplit("/", 1)[-1]},
                values=json.dumps({"BlendIn.BlendTime": 0.1, "BlendIn.BlendOption": "Linear",
                                   "BlendOut.BlendTime": 0.15, "BlendOut.BlendOption": "Linear"}))

# 4. Disable root motion for upper body action
unreal.AnimMontageService.set_enable_root_motion_translation(path, False)
unreal.AnimMontageService.set_enable_root_motion_rotation(path, False)

# 5. Save
unreal.EditorAssetLibrary.save_asset(path)
```

### Set Up Looping Idle Section

```python
import unreal

montage_path = "/Game/Montages/AM_Combat"

# Add sections
unreal.AnimMontageService.add_section(montage_path, "Idle", 0.0)
unreal.AnimMontageService.add_section(montage_path, "Attack", 2.0)

# Make Idle loop until gameplay interrupts
unreal.AnimMontageService.set_section_loop(montage_path, "Idle", True)

# Attack ends the montage (no next section)
unreal.AnimMontageService.clear_section_link(montage_path, "Attack")
```

## COMMON_MISTAKES

### Wrong: Using folder path instead of asset path
```python
# WRONG
montages = AnimMontageService.list_sections("/Game/Montages")

# CORRECT
sections = AnimMontageService.list_sections("/Game/Montages/AM_Attack")
```

### Wrong: Refreshing the editor between structural edits (can CRASH the editor)
```python
# WRONG - opening/refreshing the editor mid-edit, then editing more, can hard-crash
# the editor (live preview holds dangling pointers into reallocated montage arrays)
AnimMontageService.add_section(path, "Attack", 0.5)
unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).open_editor_for_assets([unreal.load_asset(path)])   # editor now open
AnimMontageService.add_anim_segment(path, 0, anim, 1.0)  # mutates arrays under live preview -> CRASH risk

# CORRECT - do ALL structural edits first, save, THEN refresh/open exactly once at the end
AnimMontageService.add_section(path, "Attack", 0.5)
AnimMontageService.add_anim_segment(path, 0, anim, 1.0)
unreal.EditorAssetLibrary.save_asset(path)
m = unreal.load_asset(path)
aes = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
aes.close_all_editors_for_asset(m)
aes.open_editor_for_assets([m])   # single refresh at the very end
```

### Wrong: Adding sections beyond montage duration
```python
# WRONG - Section time must be within montage length
# If montage is 3.0s, this fails:
AnimMontageService.add_section(path, "Late", 5.0)  # Beyond duration

# Montage length (get_montage_length is gone): unreal.EditorAssetLibrary.load_asset(path).get_play_length()
# in Python, or EditorToolset.ObjectToolset.GetProperties(instance={"refPath": ...}, properties=["SequenceLength"])
# CORRECT - Add segments first to extend duration, then add sections
AnimMontageService.add_anim_segment(path, 0, anim_path, 3.0)  # Extends montage
AnimMontageService.add_section(path, "Late", 3.5)  # Now within range
```

### Wrong: Forgetting to save
```python
# WRONG - Changes lost on editor restart
AnimMontageService.add_section(path, "Attack", 0.5)

# CORRECT - Always save after changes
AnimMontageService.add_section(path, "Attack", 0.5)
unreal.EditorAssetLibrary.save_asset(path)
```

## Sample scripts (run via `execute_python`)

- **`scripts/create_montage.txt`** — create a montage from an animation and add named sections.

## Reference pages

Open these with the paths below; UE rewrites them to absolute paths when it serves this skill.

- [scripts/create_montage.txt](scripts/create_montage.txt) - runnable sample
