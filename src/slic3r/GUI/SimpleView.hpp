#pragma once

#include <vector>

#include <wx/colour.h>
#include <wx/panel.h>
#include <wx/timer.h>

#include "AutoQuality.hpp"

class wxStaticText;
class Button;
class StaticBox;
class CheckBox;

namespace Slic3r { namespace GUI {

class HyperLink;

// The simple view is the default sidebar for children and first-time users. Instead of the long
// list of process settings it shows what to do next (add a model, get ready, print) and the four
// Easy Print goals as big cards. "Show all settings" switches to the normal sidebar, which offers
// the way back. It also trims the 3D toolbars to the tools a beginner needs. Stored in the app
// config as "simple_view"; on unless it was turned off.
bool simple_view();
void set_simple_view(bool on);

// Shows/hides the simple panel, the process settings and their header to match simple_view().
void apply_simple_view();

class SimpleViewPanel : public wxPanel
{
public:
    explicit SimpleViewPanel(wxWindow *parent);
    ~SimpleViewPanel() override;

    void msw_rescale();

private:
    enum class Step { AddModel, GetReady, Print };

    struct Card
    {
        PrintGoal  goal;
        StaticBox *box;
        wxColour   colour;
    };

    void add_card(wxWindow *parent, wxSizer *grid, PrintGoal goal, const wxColour &colour, const wxString &name, const wxString &text);
    void refresh();
    void show_step(Step step);
    void highlight_goal(int goal);

    wxTimer             m_timer;
    Step                m_step{Step::AddModel};
    bool                m_step_shown{false};
    int                 m_goal{-1};
    StaticBox          *m_step_box{nullptr};
    wxStaticText       *m_step_title{nullptr};
    wxStaticText       *m_step_text{nullptr};
    Button             *m_step_btn{nullptr};
    Button             *m_fun_btn{nullptr};
    Button             *m_picture_btn{nullptr};
    Button             *m_farm_btn{nullptr};
    wxStaticText       *m_cards_title{nullptr};
    std::vector<Card>   m_cards;
    HyperLink          *m_all_settings{nullptr};
    ::CheckBox         *m_auto_center{nullptr};
};

}} // namespace Slic3r::GUI
