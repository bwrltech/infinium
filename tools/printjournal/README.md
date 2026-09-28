# Print Journal

Saves every print you send from Infinium, lets you rate it, and gets the same quality again.

**Page:** http://127.0.0.1:8790 (app menu → *Print Journal*)
**Saved prints:** `~/InfiniumPrints/<date>_<model>/`, holding `print.3mf` (the exact print: all settings and the G-code), `plate.png` and `summary.json`

## How it works

When you click **Print plate**, Infinium builds the complete print file in its temp folder (`/tmp/infinium_<uid>/infinium_model/.../Metadata/.<pid>.<n>.3mf`), sends it, and later deletes it. The journal service watches that folder and copies each new print file. The copy is identified by the G-code's MD5, so sending the same print twice saves it once.

It runs as a user service that starts at login:

```bash
systemctl --user status infinium-printjournal     # running?
journalctl --user -u infinium-printjournal -f     # see prints being saved
systemctl --user disable --now infinium-printjournal   # turn it off
```

## Getting the same quality again

- **Same model:** use **Print again** (or `printjournal reprint ID`). It opens the saved file in Infinium, so the printer gets identical G-code.
- **New model:** use **Save as preset** (or `printjournal preset ID "Name"`). It writes a quality preset and a filament preset into `~/.config/Infinium/user/default/`. Each holds only the settings you changed from the built-in preset it's based on, which is the same format Infinium writes itself. If supports were on for the object, the quality preset turns supports on. Restart Infinium to see the new presets.

## Commands

```
printjournal list [--good]          saved prints, newest first
printjournal show ID                details and key settings
printjournal rate ID good|bad [--note "text"]
printjournal reprint ID             open in Infinium
printjournal preset ID "Name"       save as your own presets
printjournal add FILE.3mf           add a sliced .3mf by hand
printjournal serve | watch          what the service runs (serve = watch + page)
```

Standard library only. Environment overrides for testing: `INFINIUM_JOURNAL`, `INFINIUM_DATA`, `INFINIUM_APP`, `TMPDIR`.
