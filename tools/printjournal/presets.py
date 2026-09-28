"""Turn a saved print's settings into Infinium user presets.

A user preset file holds only the settings that differ from its parent preset,
plus the parent's name in "inherits" (the same format Infinium itself writes,
see Preset::save in src/libslic3r/Preset.cpp). So for each part (quality/process
and filament) we rebuild the parent's full settings from the profile files,
compare them with what the print really used, and write the differences.
Infinium loads the new presets the next time it starts.
"""
import glob
import json
import os
import re
import zipfile

APP_DIR = os.path.dirname(os.path.realpath(os.path.expanduser(
    os.environ.get("INFINIUM_APP", "~/infinium/build/package/infinium-slicer"))))
PROFILES = os.path.join(APP_DIR, "resources", "profiles")
DATA_DIR = os.path.expanduser(os.environ.get("INFINIUM_DATA", "~/.config/Infinium"))

# Which setting names the print file uses for each kind of preset's name.
KINDS = {
    "process": "print_settings_id",
    "filament": "filament_settings_id",
}
LABEL = {"process": "quality", "filament": "filament"}
# Bookkeeping keys, never copied as "settings".
META = {"name", "inherits", "from", "version", "type", "instantiation", "setting_id", "base_id", "filament_id",
        "print_settings_id", "filament_settings_id", "printer_settings_id", "compatible_printers",
        "compatible_printers_condition", "compatible_prints", "compatible_prints_condition",
        "is_custom_defined", "different_settings_to_system", "inherits_group", "renamed_from", "description",
        "print_compatible_printers", "upward_compatible_machine", "filament_extruder_variant", "print_extruder_id",
        "print_extruder_variant", "filament_self_index"}

_index = {}


def user_folder():
    try:
        with open(os.path.join(DATA_DIR, "Infinium.conf"), encoding="utf-8") as f:
            folder = json.load(f).get("app", {}).get("preset_folder") or ""
    except (OSError, ValueError):
        folder = ""
    return os.path.join(DATA_DIR, "user", folder or "default")


def system_index(kind):
    """(vendor, name) -> file for every system preset of this kind.

    Base names like "fdm_process_common" exist under many vendors with different
    values, so a parent is always looked up in its child's vendor first.
    """
    if kind not in _index:
        idx = {}
        for path in glob.glob(os.path.join(PROFILES, "*", kind, "**", "*.json"), recursive=True):
            try:
                with open(path, encoding="utf-8") as f:
                    name = json.load(f).get("name")
            except (OSError, ValueError):
                continue
            if name:
                vendor = os.path.relpath(path, PROFILES).split(os.sep)[0]
                idx.setdefault((vendor, name), path)
        _index[kind] = idx
    return _index[kind]


def find_system(kind, name, vendor=None):
    idx = system_index(kind)
    if vendor and (vendor, name) in idx:
        return idx[(vendor, name)], vendor
    for (v, n), path in sorted(idx.items()):
        if n == name:
            return path, v
    return None, None


def vendor_version(kind, name):
    """The profile version of the vendor a system preset belongs to (e.g. BBL 02.01.00.25)."""
    _, vendor = find_system(kind, name)
    try:
        with open(os.path.join(PROFILES, vendor + ".json"), encoding="utf-8") as f:
            return json.load(f)["version"]
    except (TypeError, OSError, ValueError, KeyError):
        return "2.0.0.0"


def is_system_name(kind, name):
    return find_system(kind, name)[0] is not None


def resolve(kind, name, vendor=None, depth=0):
    """Full settings of a preset, following its "inherits" chain. User presets win over system ones."""
    if depth > 20:
        raise ValueError(f"preset chain too deep at {name!r}")
    user_file = os.path.join(user_folder(), kind, name + ".json")
    if depth == 0 and os.path.exists(user_file):
        path = user_file
    else:
        path, vendor = find_system(kind, name, vendor)
    if not path:
        raise ValueError(f"can't find the {kind} preset {name!r}")
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    parent = data.get("inherits")
    full = resolve(kind, parent, vendor, depth + 1) if parent else {}
    full.update(data)
    return full


def same(a, b):
    """Compare two setting values, treating ["x"] and "x", and "15%" and "15", alike."""
    def norm(v):
        out = []
        for x in (v if isinstance(v, list) else [v]):
            x = str(x)[:-1] if str(x).endswith("%") else str(x)
            try:
                x = float(x)  # "1" and "1.0" are the same setting
            except ValueError:
                pass
            out.append(x)
        return out
    return norm(a) == norm(b)


def shape_like(value, like):
    """Store the value in the same form (list or plain) the parent preset uses."""
    if isinstance(like, list) and not isinstance(value, list):
        return [str(value)]
    if not isinstance(like, list) and isinstance(value, list):
        return str(value[0]) if len(value) == 1 else ",".join(map(str, value))
    return value


def valid_name(name):
    name = name.strip()
    if not name or len(name) > 80 or re.search(r'[\\/:*?"<>|]', name) or name.startswith("."):
        raise ValueError('Use a short name without \\ / : * ? " < > |')
    return name


def make_presets(print_3mf, name, support_used=None):
    """Write a process and a filament user preset from a saved print. Returns a result dict."""
    name = valid_name(name)
    with zipfile.ZipFile(print_3mf) as z:
        used = json.loads(z.read("Metadata/project_settings.config"))

    created, notes, plans = [], [], []
    for kind, id_key in KINDS.items():
        parent_name = used.get(id_key)
        if isinstance(parent_name, list):
            parent_name = parent_name[0] if parent_name else None
        if not parent_name:
            notes.append(f"The print has no {LABEL[kind]} preset name; skipped.")
            continue
        if name == parent_name or is_system_name(kind, name):
            raise ValueError(f"{name!r} is already a built-in {LABEL[kind]} preset name. Pick another name.")
        target = os.path.join(user_folder(), kind, name + ".json")
        if os.path.exists(target):
            raise ValueError(f"You already have a {LABEL[kind]} preset called {name!r}. Pick another name.")

        parent = resolve(kind, parent_name)
        diff = {}
        for key, parent_value in parent.items():
            if key in META or key not in used:
                continue
            if not same(used[key], parent_value):
                diff[key] = shape_like(used[key], parent_value)

        # Supports switched on for the object itself are not part of any preset.
        # Carry them into the quality preset so a new model gets them too.
        if kind == "process" and support_used and str(used.get("enable_support")) in ("0", "false"):
            diff["enable_support"] = shape_like("1", parent.get("enable_support", "0"))
            notes.append("Supports were turned on for the object, so this quality preset turns supports on.")

        preset = {
            "version": parent.get("version") or vendor_version(kind, parent_name),
            "name": name,
            "from": "User",
            "inherits": parent_name,
            id_key: [name] if kind == "filament" else name,
        }
        preset.update(diff)
        plans.append((target, preset, kind, parent_name, diff))

    # Write only after both parts checked out, so a bad name never leaves half a recipe.
    for target, preset, kind, parent_name, diff in plans:
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, "w", encoding="utf-8") as f:
            json.dump(preset, f, indent=4, ensure_ascii=False)
        created.append({"kind": kind, "file": target, "based_on": parent_name, "changed": sorted(diff)})
    return {"name": name, "created": created, "notes": notes}
