#!/usr/bin/env python3
"""Print Journal for Infinium: keep every print's exact settings, mark the good ones,
and print them again with the same quality.

How it works
------------
When you click Print in Infinium, the app builds one complete print file (a .3mf)
in its temp folder, uploads it, and throws it away later. That file holds
everything: all ~730 settings, the exact G-code, print time, filament used and a
picture of the plate. The journal copies it into ~/InfiniumPrints before it
disappears, and writes a short summary next to it.

Commands
--------
  printjournal.py watch              keep running and save every print you send
  printjournal.py serve              the same, plus the web page at http://127.0.0.1:8790
  printjournal.py add FILE.3mf       save a sliced .3mf by hand (e.g. "Export plate sliced file")
  printjournal.py list [--good]      show saved prints (only the good ones with --good)
  printjournal.py show ID            all details of one print
  printjournal.py rate ID good|bad [--note "text"]
  printjournal.py reprint ID         open that exact print in Infinium; press Print there
  printjournal.py preset ID "Name"   save its settings as your own presets, for new models

ID is the number shown by `list` (1 = newest). Standard library only.
"""
import argparse
import datetime
import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
import zipfile

JOURNAL = os.path.expanduser(os.environ.get("INFINIUM_JOURNAL", "~/InfiniumPrints"))
# Where Infinium writes the print file it sends: <tmp>/infinium_<uid>/infinium_model/<day>/<session>/Metadata/.<pid>.<n>.3mf
TEMP_GLOB = os.path.join(os.environ.get("TMPDIR", "/tmp"), "infinium_*", "infinium_model", "*", "*", "Metadata", ".*.3mf")
INFINIUM = os.path.expanduser(os.environ.get("INFINIUM_APP", "~/infinium/build/package/infinium-slicer"))

# The settings worth seeing at a glance. Everything else stays in the saved .3mf.
KEY_SETTINGS = {
    "Layer height (mm)": "layer_height",
    "First layer height (mm)": "initial_layer_print_height",
    "Walls": "wall_loops",
    "Top layers": "top_shell_layers",
    "Bottom layers": "bottom_shell_layers",
    "Infill": "sparse_infill_density",
    "Infill pattern": "sparse_infill_pattern",
    "Supports (all objects)": "enable_support",
    "Support type": "support_type",
    "Support angle": "support_threshold_angle",
    "Brim": "brim_type",
    "Seam": "seam_position",
    "Ironing": "ironing_type",
    "Nozzle temp (C)": "nozzle_temperature",
    "First layer nozzle temp (C)": "nozzle_temperature_initial_layer",
    "Bed plate": "curr_bed_type",
    "Outer wall speed (mm/s)": "outer_wall_speed",
    "Inner wall speed (mm/s)": "inner_wall_speed",
    "Infill speed (mm/s)": "sparse_infill_speed",
    "Flow ratio": "filament_flow_ratio",
    "Pressure advance": "pressure_advance",
}
BED_TEMP_KEY = {  # which bed temperature applies depends on the plate
    "Cool Plate": "cool_plate_temp",
    "Engineering Plate": "eng_plate_temp",
    "High Temp Plate": "hot_plate_temp",
    "Textured PEI Plate": "textured_plate_temp",
    "Smooth PEI Plate": "hot_plate_temp",
}


def one(value):
    """Settings for filaments/extruders are lists; with one filament show the plain value."""
    if isinstance(value, list):
        return value[0] if len(value) == 1 else ", ".join(map(str, value))
    return value


def fmt_time(seconds):
    seconds = int(float(seconds or 0))
    h, m = divmod(seconds // 60, 60)
    return f"{h}h {m:02d}m" if h else f"{m}m"


# ---------------------------------------------------------------------------
# Reading a print file
# ---------------------------------------------------------------------------
def read_print(path):
    """Pull a summary out of a sliced .3mf. Raises ValueError if it isn't one."""
    try:
        z = zipfile.ZipFile(path)
    except (zipfile.BadZipFile, OSError) as e:
        raise ValueError(f"not a readable 3mf: {e}")
    with z:
        names = set(z.namelist())
        gcodes = sorted(n for n in names if re.fullmatch(r"Metadata/plate_\d+\.gcode", n))
        if "Metadata/project_settings.config" not in names or not gcodes:
            raise ValueError("this 3mf has no sliced G-code; slice it and export the *sliced* file")

        settings = json.loads(z.read("Metadata/project_settings.config"))

        # Fingerprint of the exact G-code, so sending the same print twice saves it once.
        md5_name = gcodes[0] + ".md5"
        if md5_name in names:
            fingerprint = z.read(md5_name).decode().strip()
        else:
            fingerprint = hashlib.md5(z.read(gcodes[0])).hexdigest()

        info = {"objects": [], "filaments": [], "time_s": 0, "weight_g": 0.0, "support_used": None, "warnings": []}
        if "Metadata/slice_info.config" in names:
            root = ET.fromstring(z.read("Metadata/slice_info.config"))
            for plate in root.iter("plate"):
                meta = {m.get("key"): m.get("value") for m in plate.findall("metadata")}
                info["time_s"] += int(float(meta.get("prediction", 0) or 0))
                info["weight_g"] += float(meta.get("weight", 0) or 0)
                if meta.get("support_used") is not None:
                    info["support_used"] = meta.get("support_used") == "true"
                info["objects"] += [o.get("name") for o in plate.findall("object") if o.get("skipped") != "true"]
                info["filaments"] += [{"type": f.get("type"), "color": f.get("color"),
                                       "used_g": float(f.get("used_g", 0) or 0), "used_m": float(f.get("used_m", 0) or 0)}
                                      for f in plate.findall("filament")]
                info["warnings"] += [w.get("msg") for w in plate.findall("warning")]

        thumbnail = z.read("Metadata/plate_1.png") if "Metadata/plate_1.png" in names else None

    bed = one(settings.get("curr_bed_type"))
    key = {label: one(settings.get(k)) for label, k in KEY_SETTINGS.items()}
    if bed in BED_TEMP_KEY:
        key["Bed temp (C)"] = one(settings.get(BED_TEMP_KEY[bed]))

    summary = {
        "fingerprint": fingerprint,
        "model": ", ".join(dict.fromkeys(info["objects"])) or "unnamed",
        "printer": settings.get("printer_settings_id"),
        "process": settings.get("print_settings_id"),
        "filament": one(settings.get("filament_settings_id")),
        "filament_type": one(settings.get("filament_type")),
        "filament_color": one(settings.get("filament_colour")),
        "print_time": fmt_time(info["time_s"]),
        "print_time_s": info["time_s"],
        "filament_g": round(info["weight_g"], 2),
        "support_used": info["support_used"],
        "warnings": list(dict.fromkeys(info["warnings"])),
        "settings": key,
        "plates": len(gcodes),
    }
    return summary, thumbnail


# ---------------------------------------------------------------------------
# The journal on disk: one folder per print
#   ~/InfiniumPrints/2026-09-24_1414_Cat/print.3mf, summary.json, plate.png
# ---------------------------------------------------------------------------
def entries():
    """All saved prints, newest first, as (folder, summary)."""
    out = []
    for summ in glob.glob(os.path.join(JOURNAL, "*", "summary.json")):
        try:
            with open(summ, encoding="utf-8") as f:
                out.append((os.path.dirname(summ), json.load(f)))
        except (OSError, ValueError):
            continue
    out.sort(key=lambda e: e[1].get("saved_at", ""), reverse=True)
    return out


def save_summary(folder, summary):
    tmp = os.path.join(folder, "summary.json.tmp")
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2, ensure_ascii=False)
    os.replace(tmp, os.path.join(folder, "summary.json"))


def capture(path, source):
    """Copy one print file into the journal. Returns the folder, or None if already saved."""
    summary, thumb = read_print(path)
    for folder, existing in entries():
        if existing.get("fingerprint") == summary["fingerprint"]:
            return None

    now = datetime.datetime.now()
    safe_model = re.sub(r"[^A-Za-z0-9._-]+", "_", os.path.splitext(summary["model"].split(",")[0])[0])[:40] or "print"
    folder = os.path.join(JOURNAL, f"{now:%Y-%m-%d_%H%M%S}_{safe_model}")
    os.makedirs(folder, exist_ok=True)
    shutil.copy2(path, os.path.join(folder, "print.3mf"))
    if thumb:
        with open(os.path.join(folder, "plate.png"), "wb") as f:
            f.write(thumb)

    summary.update({"saved_at": now.isoformat(timespec="seconds"), "source": source,
                    "rating": None, "note": ""})
    save_summary(folder, summary)
    return folder


def pick(entry_id):
    items = entries()
    try:
        n = int(entry_id)
    except ValueError:
        sys.exit(f"ID must be a number from `list`, got {entry_id!r}")
    if not 1 <= n <= len(items):
        sys.exit(f"No print #{n}. There are {len(items)} saved prints (see `list`).")
    return items[n - 1]


def watch_forever(interval):
    """Save every print file Infinium builds when you click Print. Never returns."""
    seen = {}  # path -> (mtime, size) of the version already handled
    while True:
        for path in glob.glob(TEMP_GLOB):
            if path.endswith("_config.3mf"):
                continue
            try:
                st = os.stat(path)
            except OSError:
                continue
            state = (st.st_mtime, st.st_size)
            # Only touch a file that has stopped changing (Infinium writes it in stages).
            if seen.get(path) == state or time.time() - st.st_mtime < 3:
                continue
            try:
                folder = capture(path, "sent from Infinium")
                if folder:
                    s = entries()[0][1]
                    print(f"[{datetime.datetime.now():%H:%M}] saved {s['model']}: {s['process']}, "
                          f"{s['filament']}, {s['print_time']}, {s['filament_g']} g", flush=True)
            except ValueError:
                pass  # not a sliced print file (yet)
            except OSError as e:
                print(f"could not save {path}: {e}", file=sys.stderr, flush=True)
            seen[path] = state
        time.sleep(interval)


def open_in_infinium(folder):
    if not os.path.exists(INFINIUM):
        raise FileNotFoundError(f"Infinium not found at {INFINIUM} (set INFINIUM_APP).")
    subprocess.Popen([INFINIUM, os.path.join(folder, "print.3mf")], start_new_session=True,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------
def cmd_watch(args):
    print(f"Watching for prints sent from Infinium. Saving to {JOURNAL}")
    print("Leave this running. Press Ctrl+C to stop.", flush=True)
    watch_forever(args.interval)


def cmd_serve(args):
    import threading
    import webpage
    threading.Thread(target=watch_forever, args=(args.interval,), daemon=True).start()
    print(f"Saving every print to {JOURNAL}")
    print(f"Print Journal page: http://127.0.0.1:{args.port}", flush=True)
    webpage.serve(args.port)


def cmd_add(args):
    try:
        folder = capture(args.file, "added by hand")
    except ValueError as e:
        sys.exit(f"Can't add {args.file}: {e}")
    print("Already in the journal." if folder is None else f"Saved to {folder}")


def cmd_list(args):
    items = entries()
    if not items:
        print(f"No prints saved yet in {JOURNAL}.")
        return
    mark = {"good": "GOOD", "bad": "bad ", None: "  - "}
    print(f"{'ID':>3}  {'Rating':6} {'Date':16}  {'Model':24} {'Quality':28} {'Filament':28} {'Time':>8}")
    for i, (_, s) in enumerate(items, 1):
        if args.good and s.get("rating") != "good":
            continue
        print(f"{i:>3}  {mark.get(s.get('rating'), '  - '):6} {s['saved_at'][:16].replace('T', ' '):16}  "
              f"{s['model'][:24]:24} {str(s['process'])[:28]:28} {str(s['filament'])[:28]:28} {s['print_time']:>8}")


def cmd_show(args):
    folder, s = pick(args.id)
    print(f"{s['model']}   ({s.get('rating') or 'not rated'})")
    if s.get("note"):
        print(f"Note: {s['note']}")
    print(f"Saved:     {s['saved_at'].replace('T', ' ')}  ({s['source']})")
    print(f"Printer:   {s['printer']}")
    print(f"Quality:   {s['process']}")
    print(f"Filament:  {s['filament']}  ({s['filament_type']}, colour {s['filament_color']})")
    print(f"Time:      {s['print_time']}    Filament used: {s['filament_g']} g    Supports used: {s['support_used']}")
    print("Settings:")
    for k, v in s["settings"].items():
        print(f"  {k:30} {v}")
    if s.get("warnings"):
        print("Slicer warnings: " + ", ".join(s["warnings"]))
    print(f"Files:     {folder}")


def cmd_rate(args):
    folder, s = pick(args.id)
    s["rating"] = args.rating
    if args.note is not None:
        s["note"] = args.note
    save_summary(folder, s)
    print(f"Marked {s['model']} ({s['process']}) as {args.rating}.")


def cmd_reprint(args):
    folder, s = pick(args.id)
    try:
        open_in_infinium(folder)
    except FileNotFoundError as e:
        sys.exit(str(e))
    print(f"Opening {s['model']} in Infinium with its saved settings.")
    print("When it has loaded, click Print plate. The G-code is identical to the saved print.")


def make_presets_for(folder, summary, name):
    """Create the user presets for one saved print and remember them in its summary."""
    import presets
    result = presets.make_presets(os.path.join(folder, "print.3mf"), name, summary.get("support_used"))
    summary.setdefault("presets", []).append(result["name"])
    save_summary(folder, summary)
    return result


def cmd_preset(args):
    folder, s = pick(args.id)
    try:
        r = make_presets_for(folder, s, args.name)
    except ValueError as e:
        sys.exit(f"Couldn't make the preset: {e}")
    for c in r["created"]:
        kind = "Quality (process)" if c["kind"] == "process" else "Filament"
        changed = ", ".join(c["changed"]) if c["changed"] else "no changes, same as the original"
        print(f"{kind} preset \"{r['name']}\" based on {c['based_on']}: {changed}")
    for n in r["notes"]:
        print("Note: " + n)
    print("Restart Infinium, then pick these two presets for a new model to print it the same way.")


def main():
    p = argparse.ArgumentParser(description="Save every Infinium print's settings and reprint the good ones.")
    sub = p.add_subparsers(dest="cmd", required=True)
    w = sub.add_parser("watch", help="save every print sent from Infinium")
    w.add_argument("--interval", type=float, default=3.0, help="seconds between checks")
    sv = sub.add_parser("serve", help="save every print and show the Print Journal web page")
    sv.add_argument("--port", type=int, default=8790)
    sv.add_argument("--interval", type=float, default=3.0, help="seconds between checks")
    a = sub.add_parser("add", help="save a sliced .3mf file")
    a.add_argument("file")
    l = sub.add_parser("list", help="list saved prints")
    l.add_argument("--good", action="store_true", help="only prints rated good")
    s = sub.add_parser("show", help="details of one print")
    s.add_argument("id")
    r = sub.add_parser("rate", help="mark a print good or bad")
    r.add_argument("id")
    r.add_argument("rating", choices=["good", "bad"])
    r.add_argument("--note", help="what was good or wrong, e.g. 'stringing on the ears'")
    rp = sub.add_parser("reprint", help="open a saved print in Infinium")
    rp.add_argument("id")
    ps = sub.add_parser("preset", help="save a print's settings as your own Infinium presets")
    ps.add_argument("id")
    ps.add_argument("name", help='preset name, e.g. "Cat Good"')
    args = p.parse_args()
    os.makedirs(JOURNAL, exist_ok=True)
    {"watch": cmd_watch, "serve": cmd_serve, "add": cmd_add, "list": cmd_list, "show": cmd_show,
     "rate": cmd_rate, "reprint": cmd_reprint, "preset": cmd_preset}[args.cmd](args)


if __name__ == "__main__":
    main()
