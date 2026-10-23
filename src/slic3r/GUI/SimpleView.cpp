#include "SimpleView.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem/path.hpp>

#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/wupdlock.h>
#include <wx/stattext.h>

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"
#include "GLCanvas3D.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "ParamsPanel.hpp"
#include "PartPlate.hpp"
#include "Plater.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/HyperLink.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/SideButton.hpp"
#include "Widgets/StaticBox.hpp"

namespace Slic3r { namespace GUI {

namespace {

constexpr const char *SIMPLE_VIEW_KEY = "simple_view";

SimpleViewPanel *s_panel = nullptr;

const wxColour BLUE("#386CA1");
const wxColour TITLE("#1F3A5A");
const wxColour TEXT("#5B6B7C");

wxColour themed(const wxColour &c) { return StateColor::darkModeColorFor(c); }

// The pastel goal colours are for light mode; in dark mode the cards share one dark surface.
wxColour card_colour(const wxColour &c) { return wxGetApp().dark_mode() ? wxColour("#36363B") : c; }

// Clicks one of the Get ready / Print it buttons in the top bar, so the simple view does exactly
// what they do (including their checks and pop-ups).
void click(SideButton *btn)
{
    if (btn == nullptr || !btn->IsEnabled())
        return;
    wxCommandEvent evt(wxEVT_BUTTON, btn->GetId());
    evt.SetEventObject(btn);
    btn->GetEventHandler()->ProcessEvent(evt);
}

// The goal of the selected process preset, when it is one Easy Print made (see AutoQuality.cpp).
int goal_of_selected_preset()
{
    const std::string &name = wxGetApp().preset_bundle->prints.get_edited_preset().name;
    if (boost::starts_with(name, "Easy Smooth-Fast")) return int(PrintGoal::SmoothFast);
    if (boost::starts_with(name, "Easy Smooth"))      return int(PrintGoal::Smooth);
    if (boost::starts_with(name, "Easy Strong"))      return int(PrintGoal::Strong);
    if (boost::starts_with(name, "Easy Fast"))        return int(PrintGoal::Fast);
    return -1;
}

void load_cat()
{
    wxGetApp().plater()->load_files({boost::filesystem::path(resources_dir()) / "handy_models" / "calicat.drc"}, LoadStrategy::LoadModel);
}

} // namespace

bool simple_view()
{
    const std::string value = wxGetApp().app_config->get(SIMPLE_VIEW_KEY);
    return value.empty() || value == "1";
}

void set_simple_view(bool on)
{
    wxGetApp().app_config->set(SIMPLE_VIEW_KEY, on ? "1" : "0");
    wxGetApp().app_config->save();
    apply_simple_view();
}

void apply_simple_view()
{
    const bool on     = simple_view();
    ParamsPanel *params = wxGetApp().params_panel();
    if (params == nullptr || s_panel == nullptr)
        return;

    wxWindowUpdateLocker lock(s_panel->GetParent());
    s_panel->Show(on);
    if (StaticBox *header = params->get_top_panel())
        header->Show(!on);
    // Back to the global settings either way: per-object settings live in the normal view only.
    // ParamsPanel shows or hides itself there, honouring simple_view().
    params->switch_to_global();

    s_panel->GetParent()->Layout();
    if (auto *scrolled = dynamic_cast<wxScrolledWindow *>(s_panel->GetParent()))
        scrolled->FitInside();
    wxGetApp().sidebar().Layout();

    // The toolbars read simple_view() while rendering.
    if (GLCanvas3D *canvas = wxGetApp().plater()->get_current_canvas3D()) {
        canvas->set_as_dirty();
        canvas->request_extra_frame();
    }
}

SimpleViewPanel::SimpleViewPanel(wxWindow *parent) : wxPanel(parent, wxID_ANY)
{
    s_panel = this;
    SetBackgroundColour(themed(*wxWHITE));
    const int gap = FromDIP(12);

    auto *v_sizer = new wxBoxSizer(wxVERTICAL);

    // ---- What to do next ----
    m_step_box = new StaticBox(this);
    m_step_box->SetCornerRadius(FromDIP(14));
    m_step_box->SetBorderWidth(0);
    m_step_box->SetBackgroundColorNormal(themed(wxColour("#EBF0F6")));

    m_step_title = new wxStaticText(m_step_box, wxID_ANY, "");
    m_step_title->SetFont(Label::Head_18);
    m_step_title->SetForegroundColour(themed(TITLE));
    m_step_text = new wxStaticText(m_step_box, wxID_ANY, "");
    m_step_text->SetFont(Label::Body_14);
    m_step_text->SetForegroundColour(themed(TEXT));

    m_step_btn = new Button(m_step_box, "");
    m_step_btn->SetStyle(ButtonStyle::Confirm, ButtonType::Expanded);
    m_step_btn->SetFont(Label::Head_16);
    m_step_btn->SetMinSize(wxSize(-1, FromDIP(44)));
    m_step_btn->SetCornerRadius(FromDIP(22));
    m_step_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        MainFrame *frame = wxGetApp().mainframe;
        switch (m_step) {
        case Step::AddModel: wxGetApp().plater()->add_file(); break;
        case Step::GetReady: click(frame->m_slice_btn); break;
        case Step::Print:    click(frame->m_print_btn); break;
        }
        refresh();
    });

    m_fun_btn = new Button(m_step_box, _L("Or try a cat"));
    m_fun_btn->SetStyle(ButtonStyle::Regular, ButtonType::Expanded);
    m_fun_btn->SetFont(Label::Body_14);
    m_fun_btn->SetMinSize(wxSize(-1, FromDIP(38)));
    m_fun_btn->SetCornerRadius(FromDIP(19));
    m_fun_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        load_cat();
        refresh();
    });

    auto *step_sizer = new wxBoxSizer(wxVERTICAL);
    step_sizer->Add(m_step_title, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(16));
    step_sizer->Add(m_step_text, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(16));
    step_sizer->Add(m_step_btn, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(16));
    step_sizer->Add(m_fun_btn, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(16));
    step_sizer->AddSpacer(FromDIP(16));
    m_step_box->SetSizer(step_sizer);

    v_sizer->Add(m_step_box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, gap);

    // ---- Easy Print goals ----
    m_cards_title = new wxStaticText(this, wxID_ANY, _L("How should it come out?"));
    m_cards_title->SetFont(Label::Head_16);
    m_cards_title->SetForegroundColour(themed(TITLE));
    v_sizer->Add(m_cards_title, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(16));

    auto *grid = new wxGridSizer(2, FromDIP(10), FromDIP(10));
    add_card(this, grid, PrintGoal::Smooth, wxColour("#FFE4E1"), _L("Smooth & beautiful"), _L("Looks the best. Takes the longest."));
    add_card(this, grid, PrintGoal::SmoothFast, wxColour("#EFE6FF"), _L("Smooth & fast"), _L("Looks nice. Less waiting."));
    add_card(this, grid, PrintGoal::Strong, wxColour("#FFF1D6"), _L("Strong part"), _L("Hard to break."));
    add_card(this, grid, PrintGoal::Fast, wxColour("#DDF6F3"), _L("Fast print"), _L("Done quickly. You will see lines."));
    v_sizer->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, gap);

    // ---- Auto-centre (same setting as the Arrange menu and Preferences) ----
    {
        auto *row = new wxBoxSizer(wxHORIZONTAL);
        m_auto_center = new ::CheckBox(this);
        m_auto_center->SetValue(Plater::auto_center_enabled());
        auto *label = new wxStaticText(this, wxID_ANY, _L("Keep my models in the middle of the plate"));
        label->SetFont(Label::Body_14);
        label->SetForegroundColour(themed(TEXT));
        const wxString tip = _L("Auto-centre: when you add a model or click Get ready, all models slide to the middle together. "
                                "Models you moved yourself stay where you put them.");
        m_auto_center->SetToolTip(tip);
        label->SetToolTip(tip);
        m_auto_center->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent &) { Plater::set_auto_center_enabled(m_auto_center->GetValue()); });
        label->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) {
            m_auto_center->SetValue(!m_auto_center->GetValue());
            Plater::set_auto_center_enabled(m_auto_center->GetValue());
        });
        row->Add(m_auto_center, 0, wxALIGN_CENTER_VERTICAL);
        row->Add(label, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
        v_sizer->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(16));
    }

    // ---- The way to everything else ----
    m_all_settings = new HyperLink(this, _L("Show all settings"));
    m_all_settings->SetFont(Label::Body_14);
    m_all_settings->SetToolTip(_L("For grown-ups: every setting, and settings for each object"));
    m_all_settings->Bind(wxEVT_LEFT_DOWN, [](wxMouseEvent &) { wxGetApp().CallAfter([] { set_simple_view(false); }); });
    v_sizer->Add(m_all_settings, 0, wxALIGN_CENTER | wxALL, FromDIP(18));

    SetSizer(v_sizer);

    show_step(Step::AddModel);
    m_timer.Bind(wxEVT_TIMER, [this](wxTimerEvent &) { refresh(); });
    m_timer.Start(500);
}

SimpleViewPanel::~SimpleViewPanel()
{
    m_timer.Stop();
    if (s_panel == this)
        s_panel = nullptr;
}

void SimpleViewPanel::add_card(wxWindow *parent, wxSizer *grid, PrintGoal goal, const wxColour &colour, const wxString &name, const wxString &text)
{
    // Each goal has its own soft colour, like the tiles on the home page, so they can be told apart
    // without reading. Emoji are not used here: wx text controls don't draw them on every system.
    auto *box = new StaticBox(parent);
    box->SetCornerRadius(FromDIP(14));
    box->SetBorderWidth(FromDIP(3));
    box->SetBorderColorNormal(card_colour(colour));
    box->SetBackgroundColorNormal(card_colour(colour));

    auto *name_txt = new wxStaticText(box, wxID_ANY, wxControl::EscapeMnemonics(name));
    name_txt->SetFont(Label::Head_14);
    name_txt->SetForegroundColour(themed(TITLE));
    auto *text_txt = new wxStaticText(box, wxID_ANY, text);
    text_txt->SetFont(Label::Body_12);
    text_txt->SetForegroundColour(themed(TEXT));
    text_txt->Wrap(FromDIP(130));

    auto *sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(name_txt, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
    sizer->Add(text_txt, 1, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
    sizer->AddSpacer(FromDIP(12));
    box->SetSizer(sizer);
    box->SetMinSize(wxSize(-1, FromDIP(100)));

    // The whole card is the button, including the text on it.
    auto pick = [this, goal](wxMouseEvent &) {
        if (wxGetApp().plater()->model().objects.empty()) {
            show_step(Step::AddModel);
            return;
        }
        highlight_goal(int(goal));
        wxGetApp().CallAfter([goal] { apply_easy_print_goal(goal); });
    };
    for (wxWindow *w : {static_cast<wxWindow *>(box), static_cast<wxWindow *>(name_txt), static_cast<wxWindow *>(text_txt)}) {
        w->Bind(wxEVT_LEFT_DOWN, pick);
        w->SetCursor(wxCursor(wxCURSOR_HAND));
    }

    grid->Add(box, 1, wxEXPAND);
    m_cards.push_back({goal, box, colour});
}

void SimpleViewPanel::highlight_goal(int goal)
{
    if (goal == m_goal)
        return;
    m_goal = goal;
    for (const Card &card : m_cards) {
        const bool selected = int(card.goal) == goal;
        card.box->SetBorderColorNormal(selected ? BLUE : card_colour(card.colour));
        card.box->Refresh();
    }
}

void SimpleViewPanel::refresh()
{
    if (!IsShownOnScreen())
        return;
    Plater *plater = wxGetApp().plater();
    Step    step   = Step::AddModel;
    if (!plater->model().objects.empty()) {
        const PartPlate *plate = plater->get_partplate_list().get_curr_plate();
        step = plate != nullptr && plate->is_slice_result_ready_for_print() ? Step::Print : Step::GetReady;
    }
    show_step(step);
    highlight_goal(goal_of_selected_preset());
    // The setting can also be changed in the Arrange menu or Preferences.
    if (m_auto_center != nullptr && m_auto_center->GetValue() != Plater::auto_center_enabled())
        m_auto_center->SetValue(Plater::auto_center_enabled());

    MainFrame *frame = wxGetApp().mainframe;
    SideButton *target = step == Step::GetReady ? frame->m_slice_btn : step == Step::Print ? frame->m_print_btn : nullptr;
    m_step_btn->Enable(target == nullptr || target->IsEnabled());
}

void SimpleViewPanel::show_step(Step step)
{
    if (m_step_shown && step == m_step)
        return;
    m_step       = step;
    m_step_shown = true;

    wxString title, text, button;
    switch (step) {
    case Step::AddModel:
        title  = _L("Step 1: Add a model");
        text   = _L("Pick a 3D file from this computer. You can also drag it onto the plate.");
        button = _L("Add a model");
        break;
    case Step::GetReady:
        title  = _L("Step 2: Choose and get ready");
        text   = _L("Tap a card below to choose how it should come out. Then click Get ready.");
        button = _L("Get ready");
        break;
    case Step::Print:
        title  = _L("Step 3: Print it!");
        text   = _L("Check that the printer is on and has plastic. Then click Print it.");
        button = _L("Print it");
        break;
    }
    m_step_title->SetLabel(title);
    m_step_text->SetLabel(text);
    m_step_text->Wrap(FromDIP(290));
    m_step_btn->SetLabel(button);
    m_fun_btn->Show(step == Step::AddModel);
    // The wrapped text changes height: re-measure up to the scrolled sidebar.
    m_step_text->InvalidateBestSize();
    m_step_box->InvalidateBestSize();
    m_step_box->Layout();
    InvalidateBestSize();
    Layout();
    if (wxWindow *parent = GetParent()) {
        parent->Layout();
        if (auto *scrolled = dynamic_cast<wxScrolledWindow *>(parent))
            scrolled->FitInside();
    }
}

void SimpleViewPanel::msw_rescale()
{
    m_step_btn->Rescale();
    m_fun_btn->Rescale();
    if (m_auto_center != nullptr)
        m_auto_center->Rescale();
    Layout();
}

}} // namespace Slic3r::GUI
