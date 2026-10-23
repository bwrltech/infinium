#include "MarketplacePanel.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "Widgets/WebView.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

#include <wx/sizer.h>
#include <wx/utils.h>

#include <sstream>

using json = nlohmann::json;

namespace Slic3r { namespace GUI {

namespace {

const char *PAGE_PATH = "/web/marketplace/index.html";

boost::filesystem::path data_file() { return boost::filesystem::path(resources_dir()) / "web" / "marketplace" / "marketplace.json"; }

bool is_web_link(const std::string &url) { return boost::starts_with(url, "https://") || boost::starts_with(url, "http://"); }

} // namespace

MarketplacePanel::MarketplacePanel(wxWindow *parent) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
{
    SetBackgroundColour(*wxWHITE);
    m_page_url = wxString::Format("file://%s%s", from_u8(resources_dir()), PAGE_PATH);

    auto *sizer = new wxBoxSizer(wxVERTICAL);
    m_browser   = WebView::CreateWebView(this, m_page_url);
    if (m_browser == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "Marketplace: could not create the web view";
        SetSizer(sizer);
        return;
    }
    sizer->Add(m_browser, 1, wxEXPAND);
    SetSizer(sizer);

    // The links are pushed to the page when it has loaded: on Linux the "wx" message bridge is added a moment
    // after the page starts loading, so the page's own request ("marketplace_get") can come too early.
    Bind(wxEVT_WEBVIEW_LOADED, [this](wxWebViewEvent &evt) {
        if (evt.GetURL().Contains(PAGE_PATH)) {
            m_page_ready = true;
            send_data();
        }
    });
    Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &MarketplacePanel::on_script_message, this);
    Bind(wxEVT_WEBVIEW_NAVIGATING, &MarketplacePanel::on_navigating, this);
    Bind(wxEVT_WEBVIEW_NEWWINDOW, &MarketplacePanel::on_new_window, this);
}

std::vector<std::string> MarketplacePanel::installed_printer_vendors()
{
    std::set<std::string> vendors;
    const PresetBundle   *bundle = wxGetApp().preset_bundle;
    if (bundle == nullptr)
        return {};
    const PrinterPresetCollection &printers = bundle->printers;
    auto add = [&](const Preset &preset) {
        // User presets have no vendor of their own: it comes from the system preset they are based on.
        const PresetWithVendorProfile with_vendor = printers.get_preset_with_vendor_profile(preset);
        if (with_vendor.vendor != nullptr && !with_vendor.vendor->id.empty())
            vendors.insert(with_vendor.vendor->id);
    };
    for (const Preset &preset : printers)
        if (preset.is_visible && !preset.is_default)
            add(preset);
    if (printers.get_selected_idx() < printers.size())
        add(printers.get_selected_preset());
    return {vendors.begin(), vendors.end()};
}

std::string MarketplacePanel::load_data()
{
    m_allowed.clear();
    try {
        boost::nowide::ifstream file(data_file().string());
        if (!file) {
            BOOST_LOG_TRIVIAL(error) << "Marketplace: cannot open " << data_file().string();
            return {};
        }
        json data = json::parse(file);
        auto collect = [this](const json &link) {
            for (const char *key : {"url", "android", "iphone"})
                if (link.contains(key) && link[key].is_string())
                    m_allowed.insert(link[key].get<std::string>());
        };
        for (const json &brand : data.value("brands", json::array()))
            for (const char *section : {"apps", "models", "help"})
                for (const json &link : brand.value(section, json::array()))
                    collect(link);
        for (const json &link : data.value("models", json::array()))
            collect(link);
        // ASCII only, so the script reaches the page the same way on every platform.
        return data.dump(-1, ' ', true);
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "Marketplace: cannot read " << data_file().string() << ": " << e.what();
        return {};
    }
}

void MarketplacePanel::send_data()
{
    if (m_browser == nullptr)
        return;
    std::string data = load_data();
    if (data.empty())
        data = "{\"version\":1,\"checked\":\"\",\"brands\":[],\"models\":[]}";
    const std::string script = "window.MarketplaceSetData && window.MarketplaceSetData(" + data + ", " +
                               json(installed_printer_vendors()).dump(-1, ' ', true) + ");";
    WebView::RunScript(m_browser, from_u8(script));
}

void MarketplacePanel::refresh_brands()
{
    if (m_browser == nullptr || !m_page_ready)
        return;
    const std::string script = "window.MarketplaceSetVendors && window.MarketplaceSetVendors(" +
                               json(installed_printer_vendors()).dump(-1, ' ', true) + ");";
    WebView::RunScript(m_browser, from_u8(script));
}

bool MarketplacePanel::open_external(const std::string &url)
{
    // Only the links shown on the page (they come from marketplace.json), and only web links.
    if (!is_web_link(url) || m_allowed.find(url) == m_allowed.end()) {
        BOOST_LOG_TRIVIAL(warning) << "Marketplace: not opening a link that is not in marketplace.json: " << url;
        return false;
    }
    BOOST_LOG_TRIVIAL(info) << "Marketplace: opening " << url;
    return wxLaunchDefaultBrowser(from_u8(url));
}

void MarketplacePanel::on_script_message(wxWebViewEvent &evt)
{
    try {
        const json        msg     = json::parse(evt.GetString().utf8_string());
        const std::string command = msg.value("command", "");
        if (command == "marketplace_get") {
            m_page_ready = true;
            send_data();
        } else if (command == "marketplace_open") {
            const std::string url = msg.value("url", "");
            CallAfter([this, url] { open_external(url); });
        }
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(warning) << "Marketplace: bad message from the page: " << e.what();
    }
}

void MarketplacePanel::on_navigating(wxWebViewEvent &evt)
{
    const wxString url = evt.GetURL();
    // The page itself (first load, reloads after a theme or language change) may load; nothing else may.
    if (url.Contains(PAGE_PATH) || url.StartsWith("about:"))
        return;
    evt.Veto();
    const std::string link = url.utf8_string();
    if (is_web_link(link))
        CallAfter([this, link] { open_external(link); });
}

void MarketplacePanel::on_new_window(wxWebViewEvent &evt)
{
    const std::string link = evt.GetURL().utf8_string();
    if (is_web_link(link))
        CallAfter([this, link] { open_external(link); });
}

}} // namespace Slic3r::GUI
