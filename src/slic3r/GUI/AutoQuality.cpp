#include "AutoQuality.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/timer.h>
#include <wx/utils.h>

#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "GUI_App.hpp"
#include "GUI_Utils.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "MsgDialog.hpp"
#include "NotificationManager.hpp"
#include "Plater.hpp"
#include "Tab.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/DialogButtons.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/StaticBox.hpp"

namespace Slic3r { namespace GUI {

namespace {

enum class PrintGoal { Smooth, SmoothFast, Strong, Fast };

std::string fmt_mm(double v, int decimals)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

// Preset names may not contain these (see SavePresetDialog).
std::string sanitize_preset_name(std::string name)
{
    const char *unusable_symbols = "<>[]:/\\|?*\"";
    for (char &c : name)
        if (std::strchr(unusable_symbols, c) != nullptr)
            c = '_';
    return name;
}

std::string model_label(const Model &model)
{
    if (model.objects.size() != 1)
        return std::to_string(model.objects.size()) + " objects";
    std::string name = model.objects.front()->name;
    if (size_t dot = name.find_last_of('.'); dot != std::string::npos && dot > 0)
        name.erase(dot);
    return name.empty() ? "model" : name;
}

constexpr const char *AUTO_ON_RESIZE_KEY = "auto_quality_on_resize";
constexpr const char *GOAL_KEY           = "auto_quality_goal";

bool auto_on_resize() { return wxGetApp().app_config->get_bool(AUTO_ON_RESIZE_KEY); }

PrintGoal saved_goal()
{
    const std::string goal = wxGetApp().app_config->get(GOAL_KEY);
    return goal == "strong" ? PrintGoal::Strong : goal == "fast" ? PrintGoal::Fast : goal == "smooth_fast" ? PrintGoal::SmoothFast : PrintGoal::Smooth;
}

void save_goal(PrintGoal goal)
{
    wxGetApp().app_config->set(GOAL_KEY, goal == PrintGoal::Strong     ? "strong" :
                                         goal == PrintGoal::Fast       ? "fast" :
                                         goal == PrintGoal::SmoothFast ? "smooth_fast" : "smooth");
}

const char *goal_name(PrintGoal goal)
{
    return goal == PrintGoal::Strong ? "Strong" : goal == PrintGoal::Fast ? "Fast" : goal == PrintGoal::SmoothFast ? "Smooth-Fast" : "Smooth";
}

// What the object looks like, from its meshes as placed on the plate.
struct ShapeInfo
{
    Vec3d  size          = Vec3d::Zero();
    double max_xy        = 0.;
    double min_xy        = 0.;
    double height        = 0.;
    double longest       = 0.;
    double surface_area  = 0.; // mm²
    double overhang_area = 0.; // faces pointing down steeper than 45° that don't rest on the bed
    double flat_top_area = 0.; // upward flat faces above the bed

    bool needs_support() const { return overhang_area > std::max(25., surface_area * 0.005); }
    bool has_flat_top() const { return flat_top_area >= 100.; }
    bool tall_or_tiny() const { return height / min_xy > 3. || min_xy < 10.; }
    bool large_base() const { return max_xy > 150.; }
};

ShapeInfo analyze_shape(const Model &model)
{
    ShapeInfo      info;
    const BoundingBoxf3 bbox = model.bounding_box_exact();
    info.size    = bbox.size();
    info.max_xy  = std::max(info.size.x(), info.size.y());
    info.min_xy  = std::max(std::min(info.size.x(), info.size.y()), 0.1);
    info.height  = info.size.z();
    info.longest = std::max(info.max_xy, info.height);

    const double bed_z = bbox.min.z();
    for (const ModelObject *object : model.objects) {
        if (object->instances.empty())
            continue;
        const Transform3d &instance = object->instances.front()->get_matrix();
        for (const ModelVolume *volume : object->volumes) {
            if (!volume->is_model_part())
                continue;
            const Transform3d          t   = instance * volume->get_matrix();
            const indexed_triangle_set &its = volume->mesh().its;
            for (const stl_triangle_vertex_indices &face : its.indices) {
                const Vec3d  a    = t * its.vertices[face[0]].cast<double>();
                const Vec3d  b    = t * its.vertices[face[1]].cast<double>();
                const Vec3d  c    = t * its.vertices[face[2]].cast<double>();
                Vec3d        n    = (b - a).cross(c - a);
                const double norm = n.norm();
                if (norm <= 0.)
                    continue;
                const double area = 0.5 * norm;
                n /= norm;
                info.surface_area += area;
                if ((a.z() + b.z() + c.z()) / 3. - bed_z < 0.3)
                    continue; // sits on the bed
                if (n.z() < -0.707)
                    info.overhang_area += area;
                else if (n.z() > 0.985)
                    info.flat_top_area += area;
            }
        }
    }
    return info;
}

// Builds the settings change on top of the system profile, so switching goals never keeps
// leftovers (a slow wall from "Smooth" in a "Fast" print).
class SettingsDelta
{
public:
    SettingsDelta(const DynamicPrintConfig &base) : m_base(base) {}

    void set(const char *key, const std::string &value)
    {
        if (m_base.has(key))
            m_delta.set_deserialize_strict(key, value);
    }
    // Speed/acceleration lists (one value per extruder): keep the list length.
    void set_floats(const char *key, double value, bool lower_only)
    {
        if (auto *opt = m_base.option<ConfigOptionFloats>(key)) {
            auto *copy = static_cast<ConfigOptionFloats *>(opt->clone());
            for (double &v : copy->values)
                v = lower_only ? std::min(v, value) : value;
            m_delta.set_key_value(key, copy);
        }
    }
    void reset(const char *key)
    {
        if (const ConfigOption *opt = m_base.option(key))
            m_delta.set_key_value(key, opt->clone());
    }
    const DynamicPrintConfig &config() const { return m_delta; }

private:
    const DynamicPrintConfig &m_base;
    DynamicPrintConfig        m_delta;
};

// Keys only some goals change. They are put back to the profile's values first, so a goal never
// inherits them from an earlier Easy Print result (e.g. Strong's 35% gyroid in a later Smooth print).
const char *const GOAL_KEYS[] = {"outer_wall_speed", "inner_wall_speed", "outer_wall_acceleration", "inner_wall_acceleration",
                                 "top_surface_speed", "top_surface_acceleration", "precise_outer_wall", "wall_sequence",
                                 "seam_slope_type", "seam_slope_conditional", "scarf_angle_threshold", "seam_slope_steps",
                                 "seam_slope_min_length", "seam_slope_inner_walls", "wipe_before_external_loop", "wipe_on_loops",
                                 "staggered_inner_seams", "detect_thin_wall", "sparse_infill_density", "sparse_infill_pattern",
                                 "ironing_flow", "ironing_spacing", "infill_combination", "only_one_wall_top"};

double round_layer(double v) { return std::round(v * 100.) / 100.; }

wxString mm(double v, int decimals) { return wxString::FromUTF8(fmt_mm(v, decimals)); }

// Plain-words line about the object, shown in the window before anything is changed.
wxString describe_shape(const ShapeInfo &shape)
{
    wxString text = wxString::Format(_L("Your object: %s x %s x %s mm."), mm(shape.size.x(), 0), mm(shape.size.y(), 0), mm(shape.size.z(), 0));
    if (shape.needs_support())
        text += " " + _L("Some parts hang in the air, so supports will be added.");
    if (shape.tall_or_tiny())
        text += " " + _L("It is tall or thin, so a brim will stop it falling over.");
    else if (shape.large_base())
        text += " " + _L("It has a big base, so a brim will stop the corners lifting.");
    return text;
}

class EasyPrintDialog : public DPIDialog
{
public:
    EasyPrintDialog(wxWindow *parent, const ShapeInfo &shape)
        : DPIDialog(parent, wxID_ANY, _L("Easy Print"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
        , m_goal(saved_goal())
    {
        SetBackgroundColour(*wxWHITE);
        SetFont(Label::Body_14);

        auto *v_sizer = new wxBoxSizer(wxVERTICAL);

        auto *title = new wxStaticText(this, wxID_ANY, _L("What do you want from this print?"));
        title->SetFont(Label::Head_18);
        v_sizer->Add(title, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

        auto *hint = new wxStaticText(this, wxID_ANY, _L("Pick one. Infinium looks at your object and sets everything for you."));
        v_sizer->Add(hint, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(20) / 2);

        auto *cards = new wxBoxSizer(wxHORIZONTAL);
        add_card(cards, PrintGoal::Smooth, _L("Smooth & beautiful"),
                 _L("Almost no lines on the sides. Seam hidden. Smooth top. Best for figures, gifts and show pieces."), _L("Print time: slow"));
        add_card(cards, PrintGoal::SmoothFast, _L("Smooth & fast"),
                 _L("Nicer outside than Fast print: hidden seam, cleaner sides and top. Takes about the same time. Not for parts that take force."),
                 _L("Print time: fast"));
        add_card(cards, PrintGoal::Strong, _L("Strong part"),
                 _L("Thick walls and a strong inside. Best for holders, brackets and parts that take force."), _L("Print time: normal"));
        add_card(cards, PrintGoal::Fast, _L("Fast print"),
                 _L("Thick layers, finishes quickly. Lines will show. Best for tests and quick drafts."), _L("Print time: fast"));
        v_sizer->Add(cards, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

        auto *shape_text = new wxStaticText(this, wxID_ANY, describe_shape(shape));
        shape_text->Wrap(FromDIP(4 * 220 + 3 * 10));
        v_sizer->Add(shape_text, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

        auto *auto_sizer = new wxBoxSizer(wxHORIZONTAL);
        m_auto_cb        = new ::CheckBox(this);
        m_auto_cb->SetValue(auto_on_resize());
        auto_sizer->Add(m_auto_cb, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        auto_sizer->Add(new wxStaticText(this, wxID_ANY, _L("Do this again by itself when I change the object's size")), 0,
                        wxALIGN_CENTER_VERTICAL);
        v_sizer->Add(auto_sizer, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

        auto *dlg_btns = new DialogButtons(this, {"OK", "Cancel"});
        dlg_btns->GetOK()->SetLabel(_L("Set it up for me"));
        dlg_btns->GetOK()->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
            save_goal(m_goal);
            wxGetApp().app_config->set_bool(AUTO_ON_RESIZE_KEY, m_auto_cb->GetValue());
            EndModal(wxID_OK);
        });
        dlg_btns->GetCANCEL()->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { EndModal(wxID_CANCEL); });
        v_sizer->Add(dlg_btns, 0, wxEXPAND | wxTOP, FromDIP(10));

        SetSizerAndFit(v_sizer);
        wxGetApp().UpdateDlgDarkUI(this);
        update_cards();
        CenterOnParent();
    }

private:
    struct Card
    {
        PrintGoal  goal;
        StaticBox *box;
    };
    std::vector<Card> m_cards;
    PrintGoal         m_goal;
    ::CheckBox       *m_auto_cb = nullptr;

    void add_card(wxBoxSizer *row, PrintGoal goal, const wxString &name, const wxString &text, const wxString &time)
    {
        auto *box = new StaticBox(this);
        box->SetCornerRadius(FromDIP(8));
        auto *sizer = new wxBoxSizer(wxVERTICAL);

        auto *name_txt = new wxStaticText(box, wxID_ANY, name);
        name_txt->SetFont(Label::Head_16);
        auto *text_txt = new wxStaticText(box, wxID_ANY, text);
        text_txt->SetFont(Label::Body_13);
        text_txt->Wrap(FromDIP(190));
        auto *time_txt = new wxStaticText(box, wxID_ANY, time);
        time_txt->SetFont(Label::Head_13);

        sizer->Add(name_txt, 0, wxALL, FromDIP(14));
        sizer->Add(text_txt, 1, wxLEFT | wxRIGHT, FromDIP(14));
        sizer->Add(time_txt, 0, wxALL, FromDIP(14));
        box->SetSizer(sizer);
        box->SetMinSize(wxSize(FromDIP(220), FromDIP(170)));

        // The whole card is the button, including the text on it.
        auto select = [this, goal](wxMouseEvent &) {
            m_goal = goal;
            update_cards();
        };
        for (wxWindow *w : {static_cast<wxWindow *>(box), static_cast<wxWindow *>(name_txt), static_cast<wxWindow *>(text_txt),
                            static_cast<wxWindow *>(time_txt)}) {
            w->Bind(wxEVT_LEFT_DOWN, select);
            w->SetCursor(wxCursor(wxCURSOR_HAND));
        }

        row->Add(box, 1, wxEXPAND | (m_cards.empty() ? 0 : wxLEFT), FromDIP(10));
        m_cards.push_back({goal, box});
    }

    void update_cards()
    {
        for (const Card &card : m_cards) {
            const bool selected = card.goal == m_goal;
            card.box->SetBorderWidth(FromDIP(selected ? 2 : 1));
            card.box->SetBorderColorNormal(selected ? wxColour("#009688") : StateColor::darkModeColorFor(wxColour("#DBDBDB")));
            card.box->Refresh();
        }
    }

    void on_dpi_changed(const wxRect &) override {}
};

} // namespace

void apply_auto_quality(bool silent)
{
    Plater *plater = wxGetApp().plater();
    const Model &model = plater->model();
    if (model.objects.empty()) {
        if (silent)
            return;
        MessageDialog(plater, _L("Add a model to the plate first, then click Easy Print."), _L("Easy Print"), wxOK | wxICON_INFORMATION).ShowModal();
        return;
    }

    const PrintGoal goal  = saved_goal();
    const ShapeInfo shape = analyze_shape(model);

    PresetBundle &bundle = *wxGetApp().preset_bundle;
    const DynamicPrintConfig &printer = bundle.printers.get_edited_preset().config;
    double nozzle = 0.4;
    if (auto *opt = printer.option<ConfigOptionFloats>("nozzle_diameter"); opt && !opt->values.empty() && opt->values.front() > 0.)
        nozzle = opt->values.front();
    double min_layer = 0.04, max_layer = nozzle * 0.75;
    if (auto *opt = printer.option<ConfigOptionFloats>("min_layer_height"); opt && !opt->values.empty() && opt->values.front() > 0.)
        min_layer = opt->values.front();
    if (auto *opt = printer.option<ConfigOptionFloats>("max_layer_height"); opt && !opt->values.empty() && opt->values.front() > 0.)
        max_layer = opt->values.front();

    // Start from the system profile the current one comes from, not from an earlier Easy Print result.
    const Preset             *parent = bundle.prints.get_selected_preset_parent();
    const DynamicPrintConfig &base   = parent != nullptr && parent->is_system ? parent->config : bundle.prints.get_edited_preset().config;
    SettingsDelta             delta(base);
    std::vector<wxString>     done; // plain-words list of what was set, for the person printing

    // Layer height
    double lh_ratio = 0.5;
    if (goal == PrintGoal::Smooth)
        // Thin layers hide the lines; very big objects get a bit thicker ones so the print doesn't take days.
        lh_ratio = shape.longest <= 100. ? 0.20 : shape.longest <= 180. ? 0.30 : 0.40;
    else if (goal == PrintGoal::Fast)
        lh_ratio = 0.70;
    else if (goal == PrintGoal::SmoothFast)
        // Print time follows the number of layers (every layer is another trip around the walls), so
        // this stays close to Fast: a 3DBenchy on an A1 mini took 46.5 min against Fast's 45 min at
        // 0.65 (0.26 mm), but 57 min at 0.60. The looks come from the seam, wall and top settings.
        lh_ratio = 0.65;
    const double layer_height = std::clamp(round_layer(nozzle * lh_ratio), round_layer(min_layer), round_layer(max_layer));
    delta.set("layer_height", fmt_mm(layer_height, 2));

    for (const char *key : GOAL_KEYS)
        delta.reset(key);

    switch (goal) {
    case PrintGoal::Smooth: {
        delta.set("top_shell_thickness", "1");
        delta.set("bottom_shell_thickness", "0.8");
        delta.set("top_shell_layers", std::to_string(std::max(4, int(std::ceil(1.0 / layer_height)))));
        delta.set("bottom_shell_layers", std::to_string(std::max(3, int(std::ceil(0.8 / layer_height)))));
        delta.set("wall_loops", shape.longest <= 20. ? "2" : "3");
        delta.set_floats("outer_wall_speed", 60., true);
        delta.set_floats("inner_wall_speed", 150., true);
        delta.set_floats("outer_wall_acceleration", 2000., true);
        delta.set_floats("inner_wall_acceleration", 4000., true);
        delta.set_floats("top_surface_speed", 100., true);
        delta.set_floats("top_surface_acceleration", 1500., true);
        delta.set("wall_sequence", "inner wall/outer wall");
        delta.set("precise_outer_wall", "1");
        delta.set("detect_thin_wall", shape.min_xy < 20. ? "1" : "0");
        // Seam in a corner where there is one; blended (scarf) on round sides where there isn't.
        delta.set("seam_position", "aligned");
        delta.set("seam_slope_type", "external");
        delta.set("seam_slope_conditional", "1");
        delta.set("scarf_angle_threshold", "155");
        delta.set("seam_slope_steps", "20");
        delta.set("seam_slope_min_length", "20");
        delta.set("seam_slope_inner_walls", "1");
        delta.set("wipe_before_external_loop", "1");
        delta.set("wipe_on_loops", "1");
        delta.set("staggered_inner_seams", "1");
        delta.set("ironing_type", shape.has_flat_top() ? "top" : "no ironing");
        delta.set("ironing_flow", "10%");
        delta.set("ironing_spacing", "0.15");

        if (lh_ratio <= 0.20)
            done.push_back(wxString::Format(_L("Very thin layers (%s mm), so the lines on the sides are very hard to see."), mm(layer_height, 2)));
        else
            done.push_back(wxString::Format(_L("Thin layers (%s mm). Your object is big, so they are a little thicker to keep the print time sensible."),
                                            mm(layer_height, 2)));
        done.push_back(_L("The outside wall prints slowly, for a smooth and clean side."));
        done.push_back(_L("The seam (where each layer starts) is hidden in a corner, or blended in on round sides."));
        if (shape.has_flat_top())
            done.push_back(_L("The flat top is ironed, so it comes out smooth."));
        break;
    }
    case PrintGoal::SmoothFast: {
        // Only what you see is printed carefully: the outer wall, the seam and the top. Inner walls
        // and infill keep the profile's full speed; the infill is the quick lightning pattern,
        // printed every second layer. Constant layers on purpose: an adaptive layer profile made
        // the Benchy slower, not faster, and doesn't work with the organic supports used here.
        delta.set("top_shell_thickness", "0.8");
        delta.set("bottom_shell_thickness", "0.6");
        delta.set("top_shell_layers", std::to_string(std::max(4, int(std::ceil(0.8 / layer_height)))));
        delta.set("bottom_shell_layers", std::to_string(std::max(3, int(std::ceil(0.6 / layer_height)))));
        delta.set("wall_loops", "2");
        delta.set("sparse_infill_density", "10%");
        delta.set("sparse_infill_pattern", "lightning");
        delta.set("infill_combination", "1");
        delta.set("only_one_wall_top", "1");
        delta.set_floats("outer_wall_speed", 150., true);
        delta.set_floats("outer_wall_acceleration", 4000., true);
        delta.set_floats("top_surface_speed", 150., true);
        delta.set_floats("top_surface_acceleration", 3000., true);
        delta.set("wall_sequence", "inner wall/outer wall");
        delta.set("precise_outer_wall", "1");
        delta.set("detect_thin_wall", shape.min_xy < 20. ? "1" : "0");
        delta.set("seam_position", "aligned");
        delta.set("seam_slope_type", "external");
        delta.set("seam_slope_conditional", "1");
        delta.set("scarf_angle_threshold", "155");
        delta.set("seam_slope_steps", "10");
        delta.set("seam_slope_min_length", "10");
        delta.set("wipe_before_external_loop", "1");
        delta.set("ironing_type", "no ironing");

        done.push_back(wxString::Format(_L("Layers of %s mm, a little thinner than Fast print, so it takes about the same time."),
                                        mm(layer_height, 2)));
        done.push_back(_L("Only the outside wall and the top are printed carefully, so they come out clean. Everything inside prints at full speed."));
        done.push_back(_L("The inside is a light, quick pattern (10%). Good for looks, not for parts that take force."));
        done.push_back(_L("The seam (where each layer starts) is hidden in a corner, or blended in on round sides."));
        break;
    }
    case PrintGoal::Strong:
        delta.set("top_shell_thickness", "1.2");
        delta.set("bottom_shell_thickness", "1");
        delta.set("top_shell_layers", std::to_string(std::max(5, int(std::ceil(1.2 / layer_height)))));
        delta.set("bottom_shell_layers", std::to_string(std::max(4, int(std::ceil(1.0 / layer_height)))));
        delta.set("wall_loops", "4");
        delta.set("sparse_infill_density", "35%");
        delta.set("sparse_infill_pattern", "gyroid");
        delta.set("seam_position", "aligned");
        delta.set("ironing_type", "no ironing");
        done.push_back(wxString::Format(_L("Normal layers (%s mm), which stick to each other well."), mm(layer_height, 2)));
        done.push_back(_L("4 walls and a strong inside pattern (35% gyroid), so the part doesn't break easily."));
        break;
    case PrintGoal::Fast:
        delta.set("top_shell_thickness", "0.8");
        delta.set("bottom_shell_thickness", "0.6");
        delta.set("top_shell_layers", std::to_string(std::max(3, int(std::ceil(0.8 / layer_height)))));
        delta.set("bottom_shell_layers", std::to_string(std::max(2, int(std::ceil(0.6 / layer_height)))));
        delta.set("wall_loops", "2");
        delta.set("sparse_infill_density", "10%");
        delta.set("seam_position", "aligned");
        delta.set("ironing_type", "no ironing");
        done.push_back(wxString::Format(_L("Thick layers (%s mm) and 2 walls, so it finishes quickly. The lines will show."), mm(layer_height, 2)));
        done.push_back(_L("Less plastic inside (10%), to save time and filament."));
        break;
    }

    // Supports only when the object has parts hanging in the air; otherwise they stay off, so nothing
    // is added under small overhangs the printer bridges fine. When on: auto tree supports set up to
    // come off by hand - a gap of whole layers (~0.16-0.24 mm) to the model instead of the fine
    // profiles' single 0.08 mm layer, a wider side gap and a thin, open top.
    if (shape.needs_support()) {
        const double support_gap = layer_height * std::max(1., std::ceil(0.16 / layer_height - 1e-6));
        delta.set("enable_support", "1");
        delta.set("support_type", "tree(auto)");
        delta.set("support_threshold_angle", "30");
        delta.set("independent_support_layer_height", "1");
        delta.set("support_top_z_distance", fmt_mm(support_gap, 2));
        delta.set("support_bottom_z_distance", fmt_mm(support_gap, 2));
        delta.set("support_object_xy_distance", "0.4");
        delta.set("support_interface_top_layers", "2");
        delta.set("support_interface_spacing", "0.5");
        done.push_back(_L("Some parts hang in the air, so supports are added under them. They are set up to break off easily by hand."));
    } else {
        delta.set("enable_support", "0");
        done.push_back(_L("Nothing on your object hangs in the air, so supports are off."));
    }

    // First layer and holding the object on the bed.
    const double first_layer_speed = shape.min_xy < 20. ? 20. : 30.;
    delta.set("initial_layer_print_height", fmt_mm(std::round(nozzle * 50.) / 100., 2));
    delta.set_floats("initial_layer_speed", first_layer_speed, true);
    delta.set_floats("initial_layer_infill_speed", first_layer_speed * 2., true);
    delta.set("skirt_loops", "1");
    if (shape.tall_or_tiny()) {
        delta.set("brim_type", "outer_only");
        delta.set("brim_width", fmt_mm(std::clamp(shape.height * 0.1, 3., 8.), 0));
        done.push_back(_L("It is tall or thin, so a brim (a thin ring around the bottom) stops it falling over. Peel it off after printing."));
    } else if (shape.large_base()) {
        delta.set("brim_type", "outer_only");
        delta.set("brim_width", "5");
        done.push_back(_L("It has a big base, so a brim (a thin ring around the bottom) stops the corners lifting."));
    } else {
        delta.set("brim_type", "auto_brim");
    }
    done.push_back(_L("The first layer prints slowly, so it sticks well to the bed."));

    Tab *tab = wxGetApp().get_tab(Preset::TYPE_PRINT);
    if (tab == nullptr)
        return;
    tab->load_config(delta.config());

    // Store it as a user preset so the same settings can be picked again from the process list.
    const std::string size_str = fmt_mm(shape.size.x(), 0) + "x" + fmt_mm(shape.size.y(), 0) + "x" + fmt_mm(shape.size.z(), 0) + "mm";
    const std::string name     = sanitize_preset_name(std::string("Easy ") + goal_name(goal) + " " + fmt_mm(layer_height, 2) + "mm - " +
                                                      model_label(model) + " " + size_str);
    tab->save_preset(name);

    if (silent) {
        const std::string summary = _u8L("Easy Print updated the settings for the new size and saved them as") + " \"" + name + "\".";
        plater->get_notification_manager()->push_notification(NotificationType::CustomNotification,
                                                              NotificationManager::NotificationLevel::RegularNotificationLevel, summary);
        return;
    }

    wxString message = _L("All set. Here is what I did for you:") + "\n";
    for (const wxString &line : done)
        message += "\n•  " + line;
    message += "\n\n" + wxString::Format(_L("Saved as \"%s\" in your process list. Now click Slice."), wxString::FromUTF8(name));
    MessageDialog(plater, message, _L("Easy Print"), wxOK | wxICON_INFORMATION).ShowModal();
}

void show_easy_print_dialog(wxWindow *parent)
{
    Plater *plater = wxGetApp().plater();
    if (plater->model().objects.empty()) {
        MessageDialog(plater, _L("Add a model to the plate first, then click Easy Print."), _L("Easy Print"), wxOK | wxICON_INFORMATION).ShowModal();
        return;
    }
    EasyPrintDialog dlg(parent ? wxGetTopLevelParent(parent) : static_cast<wxWindow *>(wxGetApp().mainframe), analyze_shape(plater->model()));
    if (dlg.ShowModal() == wxID_OK)
        apply_auto_quality();
}

void start_auto_quality_watcher(wxWindow *owner)
{
    // Polling the plate size catches every way it can change (load, gizmo, size fields, scale to fit, delete)
    // without hooking each of them. The check is a cheap bounding box over the instances.
    // A self-notifying timer, so it doesn't share the owner's wxEVT_TIMER handlers.
    class Watcher : public wxTimer
    {
        Vec3d m_seen    = Vec3d::Zero();
        Vec3d m_applied = Vec3d::Zero();
        int   m_stable_ticks = 0;

    public:
        void Notify() override
        {
            Plater *plater = wxGetApp().plater();
            if (!auto_on_resize() || plater == nullptr) {
                m_applied = m_seen = Vec3d::Zero();
                return;
            }
            const Model &model = plater->model();
            const Vec3d  size  = model.objects.empty() ? Vec3d::Zero() : Vec3d(model.bounding_box_approx().size());
            if ((size - m_seen).cwiseAbs().maxCoeff() > 0.5) {
                m_seen         = size;
                m_stable_ticks = 0;
                return;
            }
            // Wait ~1.5 s of no change, and not mid-drag, before applying.
            if (++m_stable_ticks < 3 || wxGetMouseState().LeftIsDown() || size.isZero())
                return;
            if ((size - m_applied).cwiseAbs().maxCoeff() > 0.5) {
                m_applied = size;
                apply_auto_quality(true);
            }
        }
    };
    auto *watcher = new Watcher();
    owner->Bind(wxEVT_DESTROY, [owner, watcher](wxWindowDestroyEvent &evt) {
        if (evt.GetWindow() == owner)
            delete watcher;
        evt.Skip();
    });
    watcher->Start(500);
}

}} // namespace Slic3r::GUI
