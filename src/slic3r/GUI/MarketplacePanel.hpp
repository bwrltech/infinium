#ifndef slic3r_GUI_MarketplacePanel_hpp_
#define slic3r_GUI_MarketplacePanel_hpp_

#include <set>
#include <string>
#include <vector>

#include <wx/panel.h>
#include <wx/webview.h>

namespace Slic3r { namespace GUI {

// INFINIUM: the "Marketplace" tab. Shows resources/web/marketplace/index.html: each printer maker's own apps,
// free model sites and help/firmware pages (from marketplace.json, the same file the web slicer uses).
// The brands of the installed printers come first. Links open in the system browser, and only links that
// are in marketplace.json: the page cannot open anything else.
// Page contract (resources/web/marketplace/marketplace.js): the app calls MarketplaceSetData(data, vendors) when the
// page has loaded or asks with {"command":"marketplace_get"}, and MarketplaceSetVendors(vendors) when the tab is shown;
// the page sends {"command":"marketplace_open","url":…} for a click (or, without the bridge, opens a new window,
// which ends in on_new_window()).
class MarketplacePanel : public wxPanel
{
public:
    explicit MarketplacePanel(wxWindow *parent);

    // Sends the installed printers' brands to the page again (printers can be added while the app runs).
    void refresh_brands();

    // Vendor ids ("BBL", "Creality", …) of the printer presets that are installed (visible) and of the selected one.
    static std::vector<std::string> installed_printer_vendors();

private:
    void on_script_message(wxWebViewEvent &evt);
    void on_navigating(wxWebViewEvent &evt);
    void on_new_window(wxWebViewEvent &evt);
    void send_data();
    bool open_external(const std::string &url);
    // marketplace.json as compact JSON (ASCII only), and every link in it into m_allowed. Empty when unreadable.
    std::string load_data();

    wxWebView            *m_browser{nullptr};
    wxString              m_page_url;
    std::set<std::string> m_allowed;
    bool                  m_page_ready{false};
};

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_MarketplacePanel_hpp_
