---
name: enhanced-input
display_name: Enhanced Input System
description: Create and configure Enhanced Input — Input Actions, Mapping Contexts, key mappings, triggers, and modifiers (InputService). Use when the user asks to set up player input, create an Input Action (IA_) or Input Mapping Context (IMC_), bind keys to actions, or configure input triggers/modifiers.
vibeue_classes:
  - InputService
unreal_classes:
  - EditorAssetLibrary
---

> 🧠 **Brains complement:** IF an `unreal-engine-skills-manager` tool (external MCP) exists in this session, call it with `{action: "load", skill: "enhanced-input"}` for UE domain knowledge on this topic — correct APIs, architecture, best practices — and treat it as the rubric for any review / "best practices" question. If no such tool is available (e.g. running under Claude Code or Codex without that MCP), skip this line entirely and proceed with this skill alone — do NOT attempt the call.

# Enhanced Input Skill

## Critical Rules

### 🚨 Real Python method names — do NOT search for the docstring's "action" names

The remaining `InputService` Python methods are:

| Area | Methods |
|------|---------|
| Reflection | `discover_types()`, `get_available_keys(filter="")` |
| Modifiers | `add_modifier(context_path, mapping_index, modifier_type)` |
| Triggers | `add_trigger(context_path, mapping_index, trigger_type, properties_json="")`, `add_action_trigger(action_path, trigger_type, properties_json="")` → JSON |

Create assets with native `unreal.AssetTools.create_asset`; inspect them with `ObjectToolset.GetProperties`,
including `DefaultKeyMappings.Mappings` for mapping indices. Use `InputMappingContext.map_key` /
`unmap_key` for mappings and `ObjectToolset.SearchSubclasses` for modifier and trigger types.
Modifiers and triggers are addressed by **index on the mapping**.

### ⚠️ Property Names on Info Structs

| Struct | WRONG | CORRECT |
|--------|-------|---------|
| `InputTypeDiscoveryResult` | `value_types` | `action_value_types` |
| `KeyMappingInfo` | `key` | `key_name` |
| `InputModifierInfo` | `modifier_type` | `type_name` or `display_name` |
| `InputTriggerInfo` | `trigger_type` | `type_name` or `display_name` |

### ⚠️ Value Types

| Value Type | Use Case |
|------------|----------|
| `Boolean` | Simple press/release (Jump, Fire) |
| `Axis1D` | Single axis (Throttle, Zoom) |
| `Axis2D` | Two axes (Move, Look) |
| `Axis3D` | Three axes (3D manipulation) |

### ⚠️ Key Names

**Keyboard:** `SpaceBar`, `LeftShift`, `W`, `A`, `S`, `D`, `F1`, `Enter`, `Escape`, `BackSpace`...  
**Mouse:** `LeftMouseButton`, `RightMouseButton`, `MouseScrollUp`  
**Gamepad:** `Gamepad_FaceButton_Bottom`, `Gamepad_LeftThumbstick`, `Gamepad_LeftTrigger`, `Gamepad_RightTrigger`  
**Paired axes (use these for Axis2D bindings):** `Mouse2D` (mouse look), `Gamepad_Left2D` / `Gamepad_Right2D` (analog sticks)

Verify any other name with `get_available_keys("thumb")` (substring filter) instead of guessing.

### ⚠️ Triggers and Modifiers

**Triggers:** `Pressed`, `Released`, `Down`, `Hold`, `Tap`, `Pulse`  
**Modifiers:** `Negate`, `DeadZone`, `Scalar`, `SwizzleAxis` (also: `Smooth`, `SmoothDelta`, `ScaleByDeltaTime` — use `ObjectToolset.SearchSubclasses` for the full set)

---

## Workflows

### Create Input Action

Use native `unreal.AssetTools.create_asset` with `unreal.InputAction`, set its value type, and save it.

### Create Mapping Context

Use native `unreal.AssetTools.create_asset` with `unreal.InputMappingContext` and save it.

### Add Key Mapping

Load the context and action, then use native `unreal.InputMappingContext.map_key` and save the context.

### Add Triggers and Modifiers

```python
import unreal

context_path = "/Game/Input/IMC_Default"
# Inspect DefaultKeyMappings.Mappings with ObjectToolset.GetProperties first.
# Replace None with the index of the existing mapping to edit.
mapping_index = None
assert mapping_index is not None, "Choose the mapping index before editing"

# Add trigger/modifier using mapping index
unreal.InputService.add_trigger(context_path, mapping_index, "Pressed")
unreal.InputService.add_modifier(context_path, mapping_index, "DeadZone")
unreal.EditorAssetLibrary.save_asset(context_path)
```

### Trigger Settings

`properties_json` sets the new trigger's settings, by C++ name or snake_case
(`HoldTimeThreshold` or `hold_time_threshold`, `bIsOneShot` or `is_one_shot`). Only the
settings the trigger's Details panel shows can be set: runtime state such as `HeldDuration`,
`LastValue` or `bShouldAlwaysTick` is refused ("not a trigger setting"), and a number outside
the property's `ClampMin`/`ClampMax` is refused with the allowed range (`hold_time_threshold`
must be >= 0). An unknown name, a refused setting or a bad value fails the call and adds
nothing. `add_action_trigger` puts the trigger on the Input Action itself, so it applies to
every mapping of the action; it returns `PIE_ACTIVE` while a play session runs (stop PIE
first). Neither saves the asset, and a running PIE session keeps its copy of the triggers:
restart PIE to see a change.

```python
import unreal

# Hold 0.4 s before the charged attack fires, once per hold
unreal.InputService.add_trigger("/Game/Input/IMC_Default", 0, "Hold",
    '{"hold_time_threshold": 0.4, "is_one_shot": true}')

# A quick tap on the action itself, for every key bound to it
result = unreal.InputService.add_action_trigger("/Game/Input/IA_Dash", "Tap",
    '{"tap_release_time_threshold": 0.25}')
print(result)   # {"success": true, "trigger_index": 0, ...} or an error_code
unreal.EditorAssetLibrary.save_asset("/Game/Input/IA_Dash")
```

### Get Mappings Info

Use `ObjectToolset.GetProperties` on the context (`DefaultKeyMappings.Mappings`) or action.

### Discover Available Types

```python
import unreal

types = unreal.InputService.discover_types()
print(f"Value Types: {types.action_value_types}")
print(f"Modifiers: {types.modifier_types}")
print(f"Triggers: {types.trigger_types}")

keys = unreal.InputService.get_available_keys()
```

## Sample scripts

- **`scripts/setup_input.txt`** — native API pointers for creating input assets and binding a key.

## PIE input injection (issue #550)

Test input-driven gameplay without remapping assets or OS-level SendKeys using Epic's
`InputToolset.ApplyInput` via `call_tool`. Pass `Entries` and `DurationSeconds`: each entry sets
exactly one of `Action` or legacy `Key` and a nonzero `Value` (X/Y/Z for actions, X for keys).
Zero duration is a one-tick tap; a positive duration holds then releases. The async call completes
on release. The tool selects a local player automatically and has no `pie_instance` parameter.
For an indefinite action hold, use the native `EnhancedInputLocalPlayerSubsystem` continuous
injection APIs and explicitly stop it. See the `pie-testing` skill for the verification loop.

## Reference pages

Open these with the paths below; UE rewrites them to absolute paths when it serves this skill.

- [scripts/setup_input.txt](scripts/setup_input.txt) - native API pointers
