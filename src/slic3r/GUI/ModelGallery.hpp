#pragma once

#include <string>

namespace Slic3r { namespace GUI {

// Model gallery for the start screen: finds models on Printables.com (its public GraphQL API, no
// account needed) and puts the chosen one on the plate. Results are kept child-safe: anything the
// site marks NSFW, and names or searches with words from a small block list, are left out.
// Both calls are asynchronous; replies go to the home page as "gallery_results" / "gallery_loaded"
// messages (see resources/web/homepage/js/home.js).

// One page of results for `query`, starting at `offset`.
void gallery_search(const std::string &query, int offset);

// Downloads the model's smallest STL (or 3MF when it has none) to data_dir()/gallery and loads it.
void gallery_load(const std::string &print_id);

}} // namespace Slic3r::GUI
