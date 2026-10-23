#include "FarmPanel.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <nlohmann/json.hpp>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include "libslic3r/AppConfig.hpp"
#include "slic3r/Utils/Http.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "NotificationManager.hpp"
#include "PartPlate.hpp"
#include "Plater.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/WebView.hpp"

using json = nlohmann::json;

namespace Slic3r { namespace GUI {

namespace {

const char *CONFIG_KEY     = "infinium_server";
const char *DEFAULT_SERVER = "http://127.0.0.1:8180";

// "192.168.3.223:8180/" → "http://192.168.3.223:8180"
std::string clean_url(std::string url)
{
    boost::algorithm::trim(url);
    while (!url.empty() && url.back() == '/')
        url.pop_back();
    if (!url.empty() && !boost::starts_with(url, "http://") && !boost::starts_with(url, "https://"))
        url = "http://" + url;
    return url;
}

// The page shown when the server can't be reached: what to check, in simple words.
wxString offline_page(const std::string &server)
{
    return wxString::Format(
        "<html><body style=\"font-family:sans-serif;color:#2B5785;text-align:center;padding-top:80px\">"
        "<h2>%s</h2><p>%s</p><p style=\"color:#666\">%s</p></body></html>",
        _L("Can't reach your Infinium server"),
        wxString::Format(_L("Looked for it at %s."), from_u8(server)),
        _L("Check that the server is switched on, or type its address above and press Connect."));
}

void notify(const std::string &text, NotificationManager::NotificationLevel level)
{
    if (Plater *plater = wxGetApp().plater())
        plater->get_notification_manager()->push_notification(NotificationType::CustomNotification, level, text);
}

} // namespace

std::string FarmPanel::server_url()
{
    const std::string url = clean_url(wxGetApp().app_config->get(CONFIG_KEY));
    return url.empty() ? DEFAULT_SERVER : url;
}

void FarmPanel::set_server_url(const std::string &url)
{
    const std::string clean = clean_url(url);
    if (clean.empty() || clean == server_url())
        return;
    wxGetApp().app_config->set(CONFIG_KEY, clean);
    wxGetApp().app_config->save();
}

FarmPanel::FarmPanel(wxWindow *parent) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
{
    SetBackgroundColour(*wxWHITE);
    auto *sizer = new wxBoxSizer(wxVERTICAL);

    // Where the server is, and the plate → queue button.
    auto *bar   = new wxBoxSizer(wxHORIZONTAL);
    auto *label = new wxStaticText(this, wxID_ANY, _L("Infinium server"));
    label->SetFont(Label::Body_14);
    m_server = new wxTextCtrl(this, wxID_ANY, from_u8(server_url()), wxDefaultPosition, wxSize(FromDIP(260), -1), wxTE_PROCESS_ENTER);
    auto *connect = new Button(this, _L("Connect"));
    connect->SetStyle(ButtonStyle::Regular, ButtonType::Choice);
    auto *send = new Button(this, _L("Send the sliced plate to my printers"));
    send->SetStyle(ButtonStyle::Confirm, ButtonType::Choice);
    bar->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    bar->Add(m_server, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    bar->Add(connect, 0, wxALIGN_CENTER_VERTICAL);
    bar->AddStretchSpacer();
    bar->Add(send, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(bar, 0, wxEXPAND | wxALL, FromDIP(10));

    m_server->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent &) { apply_server(); });
    connect->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { apply_server(); });
    send->Bind(wxEVT_BUTTON, [](wxCommandEvent &) { send_plate(); });

    m_browser = WebView::CreateWebView(this, wxEmptyString);
    if (m_browser == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "My printers: could not create the web view";
    } else {
        sizer->Add(m_browser, 1, wxEXPAND);
        Bind(wxEVT_WEBVIEW_NAVIGATING, &FarmPanel::on_navigating, this);
        Bind(wxEVT_WEBVIEW_ERROR, &FarmPanel::on_error, this);
        Bind(wxEVT_WEBVIEW_NEWWINDOW, [](wxWebViewEvent &evt) { wxLaunchDefaultBrowser(evt.GetURL()); });
    }
    SetSizer(sizer);
}

void FarmPanel::apply_server()
{
    set_server_url(into_u8(m_server->GetValue()));
    m_server->ChangeValue(from_u8(server_url()));
    m_loaded.clear();
    reload();
}

void FarmPanel::reload()
{
    if (m_browser == nullptr)
        return;
    const std::string server = server_url();
    if (server == m_loaded)
        m_browser->Reload();
    else {
        m_loaded = server;
        m_browser->LoadURL(from_u8(server + "/?embed=farm"));
    }
}

void FarmPanel::on_navigating(wxWebViewEvent &evt)
{
    // The server's own pages stay here; anything else (help links, a printer's own page) opens in the browser.
    const std::string url = evt.GetURL().utf8_string();
    if (boost::starts_with(url, server_url()) || boost::starts_with(url, "about:") || boost::starts_with(url, "data:"))
        return;
    evt.Veto();
    if (boost::starts_with(url, "http://") || boost::starts_with(url, "https://"))
        wxLaunchDefaultBrowser(evt.GetURL());
}

void FarmPanel::on_error(wxWebViewEvent &evt)
{
    BOOST_LOG_TRIVIAL(warning) << "My printers: " << evt.GetURL().utf8_string() << ": " << evt.GetString().utf8_string();
    if (!evt.GetURL().StartsWith(from_u8(server_url())))
        return;
    m_loaded.clear(); // the next reload tries the server again
    m_browser->SetPage(offline_page(server_url()), "about:blank");
}

void FarmPanel::send_plate()
{
    Plater *plater = wxGetApp().plater();
    if (plater == nullptr)
        return;
    PartPlate *plate = plater->get_partplate_list().get_curr_plate();
    const std::string gcode = plate ? plate->get_tmp_gcode_path() : std::string();
    if (plate == nullptr || !plate->is_slice_result_ready_for_print() || gcode.empty() || !boost::filesystem::exists(gcode)) {
        notify(_u8L("Slice the plate first, then send it to your printers."), NotificationManager::NotificationLevel::WarningNotificationLevel);
        return;
    }
    std::string name = into_u8(plater->get_project_name());
    if (name.empty())
        name = "plate";
    const std::string server = server_url();
    notify(_u8L("Sending it to your printers…"), NotificationManager::NotificationLevel::RegularNotificationLevel);
    Http::post(server + "/api/farm/queue/upload")
        .form_add("name", name)
        .form_add_file("file", gcode, name + ".gcode")
        .on_complete([](std::string, unsigned) {
            wxGetApp().CallAfter([] {
                notify(_u8L("It is in the queue: it prints on the first free printer of this model. See My printers."),
                       NotificationManager::NotificationLevel::RegularNotificationLevel);
                if (MainFrame *frame = wxGetApp().mainframe)
                    if (frame->m_farm)
                        frame->m_farm->reload();
            });
        })
        .on_error([server](std::string body, std::string error, unsigned status) {
            // The server says why in simple words ({"detail": "…"}); else it couldn't be reached.
            std::string text;
            try {
                const json reply = json::parse(body);
                if (reply.contains("detail") && reply["detail"].is_string())
                    text = reply["detail"].get<std::string>();
            } catch (...) {}
            if (text.empty())
                text = status == 0 ? (boost::format(_u8L("Can't reach your Infinium server at %1%.")) % server).str() : error;
            BOOST_LOG_TRIVIAL(warning) << "My printers: upload failed, HTTP " << status << ": " << error << " " << body;
            wxGetApp().CallAfter([text] { notify(text, NotificationManager::NotificationLevel::WarningNotificationLevel); });
        })
        .perform();
}

}} // namespace Slic3r::GUI
