#include "odysea/core/directory_model.hpp"

#include <algorithm>
#include <cctype>

namespace odysea::core {
namespace fs = std::filesystem;

namespace {

std::string to_lower(std::string value) {
    std::ranges::transform(value, value.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

EntryKind classify(const fs::directory_entry& entry, std::error_code& ec) {
    if (entry.is_symlink(ec)) {
        return EntryKind::Symlink;
    }
    if (entry.is_directory(ec)) {
        return EntryKind::Directory;
    }
    if (entry.is_regular_file(ec)) {
        return EntryKind::File;
    }
    return EntryKind::Other;
}

} // namespace

bool is_hidden_name(std::string_view name) {
    return !name.empty() && name.front() == '.';
}

Entry make_entry(const fs::directory_entry& element) {
    std::error_code ec;
    Entry entry;
    entry.name = element.path().filename().string();
    entry.path = element.path();
    entry.kind = classify(element, ec);
    entry.size = (entry.kind == EntryKind::File) ? element.file_size(ec) : 0;
    return entry;
}

bool entry_orders_before(const Entry& first, const Entry& second) {
    if (first.is_directory() != second.is_directory()) {
        return first.is_directory();
    }
    return to_lower(first.name) < to_lower(second.name);
}

void sort_entries(std::vector<Entry>& entries) {
    std::ranges::sort(entries, entry_orders_before);
}

std::vector<Entry> read_directory(const fs::path& path, const ListOptions& options,
                                  std::error_code& error) {
    std::vector<Entry> entries;

    fs::directory_iterator it(path, fs::directory_options::skip_permission_denied, error);
    if (error) {
        return entries;
    }

    for (const fs::directory_entry& dirent : it) {
        const std::string name = dirent.path().filename().string();
        if (!options.show_hidden && is_hidden_name(name)) {
            continue;
        }
        entries.push_back(make_entry(dirent));
    }

    sort_entries(entries);
    return entries;
}

} // namespace odysea::core
