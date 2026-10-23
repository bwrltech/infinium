#include "ModelGallery.hpp"

#include <algorithm>
#include <cctype>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

#include "libslic3r/Utils.hpp"
#include "slic3r/Utils/Http.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "NotificationManager.hpp"
#include "Plater.hpp"
#include "WebViewDialog.hpp"

namespace Slic3r { namespace GUI {

namespace {

using json = nlohmann::json;

constexpr const char *API_URL   = "https://api.printables.com/graphql/";
constexpr const char *MEDIA_URL = "https://media.printables.com/";
constexpr int         PAGE_SIZE = 24;
constexpr size_t      MAX_FILE  = 60 * 1024 * 1024; // bigger models are not for a first print

// The site's NSFW flag catches most things; this catches names it misses. Lower-case, matched as
// parts of words, so keep entries specific.
const char *const BLOCKED_WORDS[] = {"nsfw", "nude", "naked", "sexy", "erotic", "porn", "boob", "breast", "lingerie", "bikini",
                                     "hentai", "fetish", "gun", "pistol", "rifle", "weapon", "knife", "blade", "bullet", "ammo",
                                     "grenade", "bomb", "skull", "gore", "blood", "drug", "weed", "bong"};

// Field of an object, or null. (const operator[] on a missing key is undefined behaviour.)
const json &field(const json &j, const char *key)
{
    static const json null_json;
    if (!j.is_object())
        return null_json;
    const auto it = j.find(key);
    return it == j.end() ? null_json : *it;
}

std::string text(const json &j, const char *key)
{
    const json &v = field(j, key);
    return v.is_string() ? v.get<std::string>() : std::string();
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

bool is_child_safe(const std::string &text)
{
    const std::string l = lower(text);
    return std::none_of(std::begin(BLOCKED_WORDS), std::end(BLOCKED_WORDS),
                        [&l](const char *word) { return l.find(word) != std::string::npos; });
}

void post_to_home(const json &message)
{
    MainFrame *frame = wxGetApp().mainframe;
    if (frame == nullptr || frame->m_webview == nullptr)
        return;
    frame->m_webview->RunScript(wxString::Format("window.postMessage(%s)", wxString::FromUTF8(message.dump())));
}

void post_failure(const char *command, const std::string &id, const std::string &why)
{
    BOOST_LOG_TRIVIAL(warning) << "Model gallery: " << command << " failed: " << why;
    wxGetApp().CallAfter([command = std::string(command), id] {
        post_to_home({{"command", command}, {"ok", false}, {"id", id}});
    });
}

// Runs a GraphQL request; `done` gets the "data" object on the GUI thread.
void graphql(const char *what, const std::string &id, const std::string &query, const json &variables,
             std::function<void(const json &)> done)
{
    const std::string body = json{{"query", query}, {"variables", variables}}.dump();
    Http::post(API_URL)
        .header("Content-Type", "application/json")
        .timeout_connect(10)
        .timeout_max(30)
        .set_post_body(body)
        .on_complete([what, id, done](std::string reply, unsigned status) {
            json data;
            try {
                json parsed = json::parse(reply);
                if (!parsed.contains("data") || parsed["data"].is_null())
                    return post_failure(what, id, "no data, HTTP " + std::to_string(status));
                data = std::move(parsed["data"]);
            } catch (const std::exception &e) {
                return post_failure(what, id, e.what());
            }
            wxGetApp().CallAfter([what, id, done, data] {
                try {
                    done(data);
                } catch (const std::exception &e) {
                    post_failure(what, id, e.what());
                }
            });
        })
        .on_error([what, id](std::string, std::string error, unsigned status) {
            post_failure(what, id, error + ", HTTP " + std::to_string(status));
        })
        .perform();
}

// Printables keeps small thumbnails next to each picture.
std::string thumbnail_url(const std::string &file_path)
{
    const size_t slash = file_path.find_last_of('/');
    if (slash == std::string::npos)
        return MEDIA_URL + file_path;
    std::string name = file_path.substr(slash + 1);
    if (const size_t dot = name.find_last_of('.'); dot != std::string::npos)
        name = name.substr(0, dot);
    return MEDIA_URL + file_path.substr(0, slash) + "/thumbs/inside/320x240/jpg/" + name + ".webp";
}

std::string safe_file_name(std::string name)
{
    for (char &c : name)
        if (!(std::isalnum((unsigned char) c) || c == '.' || c == '-' || c == '_'))
            c = '_';
    return name.empty() ? "model.stl" : name;
}

void load_on_plate(const boost::filesystem::path &file, const std::string &id, const std::string &name)
{
    wxGetApp().mainframe->select_tab(TAB_ID_PREPARE);
    wxGetApp().plater()->load_files({file}, LoadStrategy::LoadModel);
    wxGetApp().plater()->get_notification_manager()->push_notification(
        NotificationType::CustomNotification, NotificationManager::NotificationLevel::RegularNotificationLevel,
        _u8L("Here it is!") + " \"" + name + "\" " + _u8L("from Printables.com"));
    post_to_home({{"command", "gallery_loaded"}, {"ok", true}, {"id", id}});
}

} // namespace

void gallery_search(const std::string &query, int offset)
{
    const std::string id = std::to_string(offset);
    if (!is_child_safe(query)) {
        post_to_home({{"command", "gallery_results"}, {"ok", true}, {"query", query}, {"offset", offset}, {"items", json::array()}});
        return;
    }
    const char *gql = "query Search($q: String!, $limit: Int, $offset: Int) { searchPrints2(query: $q, limit: $limit, offset: $offset) {"
                      " items { id name nsfw user { publicUsername } license { name } image { filePath } } } }";
    graphql("gallery_results", id, gql, {{"q", query}, {"limit", PAGE_SIZE}, {"offset", std::max(0, offset)}},
            [query, offset](const json &data) {
                json items = json::array();
                const json &found = field(field(data, "searchPrints2"), "items");
                if (found.is_array())
                    for (const json &item : found) {
                        const std::string name = text(item, "name");
                        const json &nsfw = field(item, "nsfw");
                        const std::string image = text(field(item, "image"), "filePath");
                        if (!nsfw.is_boolean() || nsfw.get<bool>() || !is_child_safe(name) || image.empty())
                            continue;
                        items.push_back({{"id", text(item, "id")},
                                         {"name", name},
                                         {"maker", text(field(item, "user"), "publicUsername")},
                                         {"license", text(field(item, "license"), "name")},
                                         {"image", thumbnail_url(image)},
                                         {"image_full", MEDIA_URL + image}});
                    }
                // A short page that is not the first means the end of the list.
                post_to_home({{"command", "gallery_results"}, {"ok", true}, {"query", query}, {"offset", offset},
                              {"more", found.is_array() && found.size() == size_t(PAGE_SIZE)}, {"items", items}});
            });
}

void gallery_load(const std::string &print_id)
{
    if (print_id.empty() || !std::all_of(print_id.begin(), print_id.end(), [](unsigned char c) { return std::isdigit(c); }))
        return;

    const char *gql = "query Print($id: ID!) { print(id: $id) { id name nsfw stls { id name fileSize } } }";
    graphql("gallery_loaded", print_id, gql, {{"id", print_id}}, [print_id](const json &data) {
        const json &print = field(data, "print");
        const json &nsfw  = field(print, "nsfw");
        if (!nsfw.is_boolean() || nsfw.get<bool>() || !is_child_safe(text(print, "name")))
            return post_failure("gallery_loaded", print_id, "not available");

        // The smallest STL is usually the plain, single model; a 3MF only when there is no STL.
        const json &files = field(print, "stls");
        const json *pick  = nullptr;
        auto size_of = [](const json &file) {
            const json &size = field(file, "fileSize");
            return size.is_number_unsigned() || size.is_number_integer() ? size.get<size_t>() : MAX_FILE + 1;
        };
        for (const char *ext : {".stl", ".3mf"}) {
            if (files.is_array())
                for (const json &file : files)
                    if (boost::ends_with(lower(text(file, "name")), ext) && size_of(file) <= MAX_FILE &&
                        (pick == nullptr || size_of(file) < size_of(*pick)))
                        pick = &file;
            if (pick != nullptr)
                break;
        }
        if (pick == nullptr)
            return post_failure("gallery_loaded", print_id, "no printable file");

        const std::string file_id   = text(*pick, "id");
        const std::string file_name = safe_file_name(text(*pick, "name"));
        const std::string title     = text(print, "name");
        const char *link_gql = "mutation Link($id: ID!, $printId: ID!) { getDownloadLink(id: $id, printId: $printId, fileType: stl,"
                               " source: model_detail) { ok output { link } } }";
        graphql("gallery_loaded", print_id, link_gql, {{"id", file_id}, {"printId", print_id}},
                [print_id, file_name, title](const json &data) {
                    const std::string link = text(field(field(data, "getDownloadLink"), "output"), "link");
                    if (!boost::starts_with(link, "https://"))
                        return post_failure("gallery_loaded", print_id, "no download link");

                    const boost::filesystem::path dir = boost::filesystem::path(data_dir()) / "gallery" / print_id;
                    Http::get(link)
                        .timeout_connect(10)
                        .timeout_max(180)
                        .size_limit(MAX_FILE)
                        .on_complete([print_id, file_name, title, dir](std::string body, unsigned) {
                            try {
                                boost::filesystem::create_directories(dir);
                                const boost::filesystem::path file = dir / file_name;
                                boost::nowide::ofstream(file.string(), std::ios::binary) << body;
                                wxGetApp().CallAfter([file, print_id, title] { load_on_plate(file, print_id, title); });
                            } catch (const std::exception &e) {
                                post_failure("gallery_loaded", print_id, e.what());
                            }
                        })
                        .on_error([print_id](std::string, std::string error, unsigned status) {
                            post_failure("gallery_loaded", print_id, error + ", HTTP " + std::to_string(status));
                        })
                        .perform();
                });
    });
}

}} // namespace Slic3r::GUI
