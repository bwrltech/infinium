#pragma once

class wxWindow;

namespace Slic3r { namespace GUI {

// "Easy Print" for people who don't know the settings: they pick a goal in plain words
// (Smooth & beautiful / Smooth & fast / Strong part / Fast print), the models on the plate are analysed
// (size, overhangs, flat tops, tall or thin) and the matching layer, wall, seam, support, brim and
// first-layer settings are applied to the process preset and saved as a user preset
// (named after the goal, the model and its size) so it can be selected again later.
enum class PrintGoal { Smooth, SmoothFast, Strong, Fast };

// The goal Easy Print last used (remembered in the app config).
PrintGoal easy_print_goal();

// Applies the last chosen goal. silent: no dialogs (used by the automatic trigger).
void apply_auto_quality(bool silent = false);

// One-click form used by the simple view: remembers `goal`, applies it and says so in a short
// notification instead of the summary dialog.
void apply_easy_print_goal(PrintGoal goal);

// Easy Print leaves supports off and lets the slicer's support check decide. Called after slicing
// when that check found parts that would hang in the air: turns the easy-break supports on in the
// selected Easy Print preset (only those, and only if supports are off) and slices again.
// Returns false when it changed nothing.
bool easy_print_add_needed_support();

// The Easy Print window: goal cards, what was found on the object, and the "do this again on
// size change" switch. Applies the chosen goal on OK.
void show_easy_print_dialog(wxWindow *parent);

// When "Auto on size change" is on, re-applies Easy Print once the plate's model size has
// changed (load, scale, resize) and stayed unchanged for a moment. Owned by `owner`.
void start_auto_quality_watcher(wxWindow *owner);

}} // namespace Slic3r::GUI
