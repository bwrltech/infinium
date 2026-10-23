#include "PhotoForgeDialog.hpp"

#include <algorithm>

#include <boost/beast/core/detail/base64.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

#include <wx/display.h>
#include <wx/sizer.h>

#include "libslic3r/CustomGCode.hpp"
#include "libslic3r/Format/STL.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"
#include "FarmPanel.hpp"
#include "GLCanvas3D.hpp"
#include "GUI_App.hpp"
#include "GUI_ObjectList.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "NotificationManager.hpp"
#include "PartPlate.hpp"
#include "Plater.hpp"
#include "Tab.hpp"
#include "Widgets/WebView.hpp"

using json = nlohmann::json;
namespace fs = boost::filesystem;

namespace Slic3r { namespace GUI {

namespace {

const char *PAGE_PATH = "/web/forge/index.html";

// The project's filament colours, one per nozzle (what the page offers to print each colour with).
std::vector<std::string> nozzle_colours()
{
    std::vector<std::string> colours;
    if (const auto *opt = wxGetApp().preset_bundle->project_config.option<ConfigOptionStrings>("filament_colour"))
        colours = opt->values;
    if (colours.empty())
        colours.push_back("#FFFFFF");
    return colours;
}

std::string base64_decode(const std::string &text)
{
    std::string out(boost::beast::detail::base64::decoded_size(text.size()), '\0');
    out.resize(boost::beast::detail::base64::decode(out.data(), text.data(), text.size()).first);
    return out;
}

// A binary STL from the page as a mesh (the app's own reader: it repairs the mesh too). Empty when unreadable.
TriangleMesh read_stl(const std::string &data, const fs::path &file)
{
    {
        boost::nowide::ofstream out(file.string(), std::ios::binary);
        out.write(data.data(), std::streamsize(data.size()));
    }
    Model model;
    if (!load_stl(file.string().c_str(), &model) || model.objects.empty() || model.objects.front()->volumes.empty())
        return {};
    return model.objects.front()->volumes.front()->mesh();
}

} // namespace

void PhotoForgeDialog::show(Tool tool)
{
    MainFrame *frame = wxGetApp().mainframe;
    Plater    *plater = wxGetApp().plater();
    if (frame == nullptr || plater == nullptr || !plater->can_add_model())
        return;
    frame->select_tab(TAB_ID_PREPARE);
    PhotoForgeDialog dlg(frame, tool);
    dlg.ShowModal();
}

PhotoForgeDialog::PhotoForgeDialog(wxWindow *parent, Tool tool)
    : wxDialog(parent, wxID_ANY, tool == Tool::PhotoForge ? _L("Photo Forge") : _L("Photo to 3D"), wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    SetBackgroundColour(*wxWHITE);
    m_page_path = PAGE_PATH;
    const wxString url = wxString::Format("file://%s%s%s", from_u8(resources_dir()), PAGE_PATH,
                                          tool == Tool::PhotoForge ? "#forge" : "#photo3d");

    auto *sizer = new wxBoxSizer(wxVERTICAL);
    m_browser   = WebView::CreateWebView(this, url);
    if (m_browser == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "Photo Forge: could not create the web view";
    } else {
        sizer->Add(m_browser, 1, wxEXPAND);
        // On Linux the "wx" bridge comes a moment after the page starts loading: answer both the page's own
        // "forge_ready" and the loaded event, whichever is later.
        Bind(wxEVT_WEBVIEW_LOADED, [this](wxWebViewEvent &evt) {
            if (evt.GetURL().Contains(m_page_path))
                send_init();
        });
        Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &PhotoForgeDialog::on_script_message, this);
        Bind(wxEVT_WEBVIEW_NAVIGATING, &PhotoForgeDialog::on_navigating, this);
    }
    SetSizer(sizer);

    // Large enough for the picture and its settings side by side, never larger than the screen.
    wxSize size(FromDIP(1100), FromDIP(760));
    const int display = wxDisplay::GetFromWindow(parent);
    const wxRect area = wxDisplay(display == wxNOT_FOUND ? 0u : unsigned(display)).GetClientArea();
    size.x = std::min(size.x, area.width - FromDIP(40));
    size.y = std::min(size.y, area.height - FromDIP(40));
    SetSize(size);
    CenterOnParent();
}

void PhotoForgeDialog::send_init()
{
    if (m_browser == nullptr)
        return;
    const json init = {{"nozzles", nozzle_colours()}, {"server", FarmPanel::server_url()}};
    WebView::RunScript(m_browser, from_u8("window.ForgeInit && window.ForgeInit(" + init.dump(-1, ' ', true) + ");"));
}

void PhotoForgeDialog::send_error(const wxString &text)
{
    if (m_browser != nullptr)
        WebView::RunScript(m_browser, "window.ForgeError && window.ForgeError(" + from_u8(json(into_u8(text)).dump(-1, ' ', true)) + ");");
}

void PhotoForgeDialog::on_script_message(wxWebViewEvent &evt)
{
    json msg;
    try {
        msg = json::parse(evt.GetString().utf8_string());
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(warning) << "Photo Forge: bad message from the page: " << e.what();
        return;
    }
    const std::string command = msg.value("command", "");
    if (command == "forge_ready")
        send_init();
    else if (command == "forge_close")
        CallAfter([this] { EndModal(wxID_CANCEL); });
    else if (command == "forge_server")
        FarmPanel::set_server_url(msg.value("url", ""));
    else if (command == "forge_add") {
        CallAfter([this, msg = std::move(msg)] {
            wxString error;
            if (add_to_plate(msg, error))
                EndModal(wxID_OK);
            else
                send_error(error);
        });
    }
}

void PhotoForgeDialog::on_navigating(wxWebViewEvent &evt)
{
    // The page itself only: the tools don't open other pages.
    const wxString url = evt.GetURL();
    if (!url.Contains(m_page_path) && !url.StartsWith("about:"))
        evt.Veto();
}

bool PhotoForgeDialog::add_to_plate(const json &msg, wxString &error)
{
    const json &parts = msg.contains("parts") && msg["parts"].is_array() ? msg["parts"] : json::array();
    if (parts.empty()) {
        error = _L("There was nothing to put on the plate.");
        return false;
    }
    Plater *plater = wxGetApp().plater();
    const std::vector<std::string> colours = nozzle_colours();
    const int nozzles = int(colours.size());
    auto nozzle_of = [nozzles](const json &j) { return std::clamp(j.is_number_integer() ? j.get<int>() : 1, 1, nozzles); };

    // Each part's mesh first: nothing goes on the plate unless all of them could be read.
    const fs::path folder = fs::temp_directory_path() / fs::unique_path("infinium-forge-%%%%-%%%%");
    std::vector<TriangleMesh> meshes;
    try {
        fs::create_directories(folder);
        for (const json &part : parts) {
            TriangleMesh mesh = read_stl(base64_decode(part.value("stl", "")), folder / ("part" + std::to_string(meshes.size()) + ".stl"));
            if (mesh.empty())
                break;
            meshes.push_back(std::move(mesh));
        }
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "Photo Forge: " << e.what();
    }
    boost::system::error_code ec;
    fs::remove_all(folder, ec);
    if (meshes.size() != parts.size()) {
        error = _L("The model couldn't be read. Try a smaller picture.");
        return false;
    }

    Plater::TakeSnapshot snapshot(plater, "Photo Forge");
    Model &model = plater->model();
    const std::string name = msg.value("name", "Photo Forge");
    ModelObject *object = model.add_object();
    object->name = name;
    object->add_instance();
    // One object, a part per colour: the parts keep their places (each is centred, with an offset that keeps it there).
    BoundingBoxf3 bb;
    for (size_t i = 0; i < meshes.size(); ++i) {
        bb.merge(meshes[i].bounding_box());
        ModelVolume *volume = object->add_volume(std::move(meshes[i]));
        volume->name = parts[i].value("name", name);
        if (parts.size() > 1)
            volume->config.set_key_value("extruder", new ConfigOptionInt(nozzle_of(parts[i].value("nozzle", json(1)))));
    }
    object->sort_volumes(true);
    object->config.set_key_value("extruder", new ConfigOptionInt(nozzle_of(parts[0].value("nozzle", json(1)))));

    // Colour layers: thin layers so the colours blend, solid inside so light goes through evenly, and the filament
    // changes from their heights (another nozzle on a tool changer, else a pause to change it by hand).
    if (const json &layers = msg.contains("layers") && msg["layers"].is_object() ? msg["layers"] : json(); layers.is_object()) {
        object->config.set_key_value("layer_height", new ConfigOptionFloat(layers.value("layer_height", 0.08)));
        object->config.set_key_value("sparse_infill_density", new ConfigOptionPercent(layers.value("infill", 100.)));
        const double first_layer = layers.value("first_layer", 0.16);
        if (Tab *tab = wxGetApp().get_tab(Preset::TYPE_PRINT);
            tab != nullptr && wxGetApp().preset_bundle->prints.get_edited_preset().config.opt_float("initial_layer_print_height") != first_layer) {
            DynamicPrintConfig first;
            first.set_key_value("initial_layer_print_height", new ConfigOptionFloat(first_layer));
            tab->load_config(first);
        }
        CustomGCode::Info info;
        if (layers.contains("changes") && layers["changes"].is_array() && nozzles > 1) {
            info.mode = CustomGCode::MultiAsSingle;
            for (const json &c : layers["changes"]) {
                const int nozzle = nozzle_of(c.value("nozzle", json(1)));
                info.gcodes.push_back({c.value("z", 0.), CustomGCode::ToolChange, nozzle, colours[nozzle - 1], ""});
            }
        } else if (layers.contains("swaps") && layers["swaps"].is_array()) {
            info.mode = CustomGCode::SingleExtruder;
            for (const json &s : layers["swaps"])
                info.gcodes.push_back({s.value("z", 0.), CustomGCode::PausePrint, 1, s.value("color", ""), s.value("note", "")});
        }
        std::sort(info.gcodes.begin(), info.gcodes.end());
        if (!info.gcodes.empty())
            model.plates_custom_gcodes[plater->get_partplate_list().get_curr_plate_index()] = info;
    }

    // Placed like an added shape: centred, in a free spot of the plate, standing on it.
    object->invalidate_bounding_box();
    object->translate(-bb.center());
    const Vec2d start_point = plater->build_volume().bounding_volume2d().center();
    const auto  empty_cell  = plater->canvas3D()->get_nearest_empty_cell({start_point(0), start_point(1)});
    object->instances[0]->set_offset(to_3d(Vec2d(empty_cell(0), empty_cell(1)), -object->origin_translation.z()));
    object->ensure_on_bed();
    object->instances[0]->set_assemble_transformation(object->instances[0]->get_transformation());
    Slic3r::save_object_mesh(*object);

    wxGetApp().obj_list()->paste_objects_into_list({model.objects.size() - 1});
    wxGetApp().mainframe->update_title();

    const std::string message = msg.value("message", "");
    if (!message.empty())
        plater->get_notification_manager()->push_notification(NotificationType::CustomNotification,
                                                              NotificationManager::NotificationLevel::RegularNotificationLevel, message);
    return true;
}

}} // namespace Slic3r::GUI
