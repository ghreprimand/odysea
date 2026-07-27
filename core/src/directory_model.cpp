#include "odysea/core/directory_model.hpp"

#include <algorithm>
#include <cctype>

namespace odysea::core {
namespace fs = std::filesystem;

namespace {

std::string to_lower(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
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

bool ordered_before(const Entry& a, const Entry& b) {
    if (a.is_directory() != b.is_directory()) {
        return a.is_directory();
    }
    return to_lower(a.name) < to_lower(b.name);
}

} // namespace

std::vector<Entry> read_directory(const fs::path& path, const ListOptions& options,
                                  std::error_code& error) {
    std::vector<Entry> entries;

    fs::directory_iterator it(path, fs::directory_options::skip_permission_denied, error);
    if (error) {
        return entries;
    }

    for (const fs::directory_entry& dirent : it) {
        const std::string name = dirent.path().filename().string();
        if (!options.show_hidden && !name.empty() && name.front() == '.') {
            continue;
        }

        std::error_code entry_ec;
        Entry entry;
        entry.name = name;
        entry.path = dirent.path();
        entry.kind = classify(dirent, entry_ec);
        entry.size = (entry.kind == EntryKind::File) ? dirent.file_size(entry_ec) : 0;
        entries.push_back(std::move(entry));
    }

    std::ranges::sort(entries, ordered_before);
    return entries;
}

} // namespace odysea::core
