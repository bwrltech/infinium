#ifndef slic3r_GUI_FarmPanel_hpp_
#define slic3r_GUI_FarmPanel_hpp_

#include <string>

#include <wx/panel.h>
#include <wx/webview.h>

class wxTextCtrl;

namespace Slic3r { namespace GUI {

// INFINIUM: the "My printers" tab: the printer farm of the Infinium server (infinium-web: printers found on the Wi-Fi,
// live cameras, the print queue), shown from the server itself (<server>/?embed=farm) so it is the same page as on
// the web. "Send to my printers" puts the sliced plate's G-code in the server's queue (POST /api/farm/queue/upload):
// the farm prints it on the first free printer of that model.
class FarmPanel : public wxPanel
{
public:
    explicit FarmPanel(wxWindow *parent);

    // Loads the page again (when the tab is shown, or the server address changed).
    void reload();

    // The Infinium server ("http://host:port", app config "infinium_server"; this computer when not set).
    static std::string server_url();
    static void        set_server_url(const std::string &url);

    // Sends the current plate's sliced G-code to the farm's queue. Says what happened in a notification.
    static void send_plate();

private:
    void on_navigating(wxWebViewEvent &evt);
    void on_error(wxWebViewEvent &evt);
    void apply_server();

    wxWebView  *m_browser{nullptr};
    wxTextCtrl *m_server{nullptr};
    std::string m_loaded;
};

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_FarmPanel_hpp_
