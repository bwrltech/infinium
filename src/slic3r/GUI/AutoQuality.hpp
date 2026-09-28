#pragma once

class wxWindow;

namespace Slic3r { namespace GUI {

// "Easy Print" for people who don't know the settings: they pick a goal in plain words
// (Smooth & beautiful / Strong part / Fast print), the models on the plate are analysed
// (size, overhangs, flat tops, tall or thin) and the matching layer, wall, seam, support, brim and
// first-layer settings are applied to the process preset and saved as a user preset
// (named after the goal, the model and its size) so it can be selected again later.
// Applies the last chosen goal. silent: no dialogs (used by the automatic trigger).
void apply_auto_quality(bool silent = false);

// The Easy Print window: goal cards, what was found on the object, and the "do this again on
// size change" switch. Applies the chosen goal on OK.
void show_easy_print_dialog(wxWindow *parent);

// When "Auto on size change" is on, re-applies Easy Print once the plate's model size has
// changed (load, scale, resize) and stayed unchanged for a moment. Owned by `owner`.
void start_auto_quality_watcher(wxWindow *owner);

}} // namespace Slic3r::GUI
