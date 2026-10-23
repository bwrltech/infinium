#ifndef slic3r_GUI_PhotoForgeDialog_hpp_
#define slic3r_GUI_PhotoForgeDialog_hpp_

#include <string>

#include <nlohmann/json_fwd.hpp>
#include <wx/dialog.h>
#include <wx/webview.h>

namespace Slic3r { namespace GUI {

// INFINIUM: "Photo Forge" (a picture becomes a model: light picture, raised picture, shape, colour picture, colour
// layers) and "Photo to 3D" (the AI on the Infinium server makes a whole model from one photo). Both are the web
// slicer's own tools, built for the desktop into resources/web/forge (infinium-web: `npm run build:desktop`), so the
// two apps make the same models. The page makes the model; this dialog puts it on the plate.
// Page contract (resources/web/forge/forge.js):
//   page → app {"command":"forge_ready"}; app → page window.ForgeInit({nozzles:["#rrggbb",…], server:"http://…"})
//   page → app {"command":"forge_add", name, message, parts:[{name, color, nozzle (1-based), stl (base64 binary STL)}],
//               layers?: {layer_height, first_layer, infill, changes?: [{z, nozzle}], swaps?: [{z, color, note}]}}
//               → one object with a part per entry; on success the dialog closes, else window.ForgeError(text)
//   page → app {"command":"forge_close"}, {"command":"forge_server", url} (the Infinium server address, kept in app config)
class PhotoForgeDialog : public wxDialog
{
public:
    enum class Tool { PhotoForge, PhotoTo3D };

    // Opens the tool (modal). Does nothing while the plater can't take models.
    static void show(Tool tool);

private:
    PhotoForgeDialog(wxWindow *parent, Tool tool);

    void on_script_message(wxWebViewEvent &evt);
    void on_navigating(wxWebViewEvent &evt);
    void send_init();
    void send_error(const wxString &text);
    // Puts the page's model on the plate; false (and why) when it can't.
    bool add_to_plate(const nlohmann::json &msg, wxString &error);

    wxWebView *m_browser{nullptr};
    wxString   m_page_path;
};

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_PhotoForgeDialog_hpp_
