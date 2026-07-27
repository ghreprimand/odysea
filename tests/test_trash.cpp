// Headless tests for freedesktop.org trash support.
//
// The tests redirect XDG_DATA_HOME into a temporary tree, so nothing touches a
// real desktop trash directory.
#include "odysea/core/trash.hpp"

#include "test_support.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

namespace fs = std::filesystem;
using odysea::test::check;
using namespace odysea::core;

namespace {

void test_encoding() {
    check(encode_trash_location("/tmp/plain.txt") == "/tmp/plain.txt",
          "an unreserved path is left literal");
    check(encode_trash_location("/tmp/two words.txt") == "/tmp/two%20words.txt",
          "spaces are percent-encoded");
    check(encode_trash_location("/tmp/hash#and%pct") == "/tmp/hash%23and%25pct",
          "reserved characters are percent-encoded in upper-case hex");
    check(encode_trash_location("/tmp/caf\xc3\xa9") == "/tmp/caf%C3%A9",
          "non-ASCII bytes are encoded one byte at a time");
}

void test_home_trash_follows_the_data_home_variable() {
    std::error_code ec;
    const fs::path trash = home_trash_directory(ec);
    check(!ec, "the home trash resolves when XDG_DATA_HOME is set");
    check(trash.filename() == "Trash", "the home trash is a Trash directory");
    check(trash.parent_path().filename() == "data_home",
          "the home trash sits under the configured data home");
}

void test_trashing_a_file() {
    const odysea::test::TemporaryTree tree("trash_file");
    const fs::path source = tree.file("documents/report.txt", "contents");

    const TrashOutcome outcome = move_to_trash(source);
    check(outcome.succeeded(), "trashing a file should succeed");
    check(!fs::exists(source), "the source is gone after trashing");
    check(fs::exists(outcome.trashed_path), "the entry lands in the trash files directory");
    check(outcome.trashed_path.filename() == "report.txt", "the trashed entry keeps its name");
    check(odysea::test::read_text(outcome.trashed_path) == "contents",
          "trashing preserves contents");

    const std::string record = odysea::test::read_text(outcome.info_path);
    check(record.starts_with("[Trash Info]\n"), "the record starts with the specified header");
    check(record.find("Path=" + encode_trash_location(source.string()) + "\n") != std::string::npos,
          "the record stores the encoded original path");
    check(record.find("\nDeletionDate=20") != std::string::npos,
          "the record stores a deletion timestamp");
    check(outcome.info_path.extension() == ".trashinfo", "the record uses the trashinfo extension");
}

void test_trashing_a_directory() {
    const odysea::test::TemporaryTree tree("trash_directory");
    tree.file("project/nested/file.txt", "deep");

    const TrashOutcome outcome = move_to_trash(tree.root() / "project");
    check(outcome.succeeded(), "trashing a directory should succeed");
    check(fs::exists(outcome.trashed_path / "nested/file.txt"),
          "a trashed directory keeps its children");
    check(!fs::exists(tree.root() / "project"), "the source directory is gone");
}

void test_name_collisions_are_resolved() {
    const odysea::test::TemporaryTree tree("trash_collision");
    const fs::path first = tree.file("one/notes.txt", "first");
    const fs::path second = tree.file("two/notes.txt", "second");

    const TrashOutcome first_outcome = move_to_trash(first);
    const TrashOutcome second_outcome = move_to_trash(second);
    check(first_outcome.succeeded() && second_outcome.succeeded(),
          "two entries with the same name can both be trashed");
    check(second_outcome.trashed_path.filename() == "notes_1.txt",
          "a colliding name is suffixed before the extension");
    check(odysea::test::read_text(first_outcome.trashed_path) == "first" &&
              odysea::test::read_text(second_outcome.trashed_path) == "second",
          "each trashed entry keeps its own contents");
    check(second_outcome.info_path.filename() == "notes_1.txt.trashinfo",
          "the record name matches the trashed name");
}

void test_encoded_names_round_trip_into_the_record() {
    const odysea::test::TemporaryTree tree("trash_encoding");
    const fs::path source = tree.file("odd name #1.txt", "odd");

    const TrashOutcome outcome = move_to_trash(source);
    check(outcome.succeeded(), "an awkward file name can be trashed");
    check(outcome.trashed_path.filename() == "odd name #1.txt",
          "the trashed file keeps its literal name on disk");
    const std::string record = odysea::test::read_text(outcome.info_path);
    check(record.find("odd%20name%20%231.txt") != std::string::npos,
          "the record percent-encodes the awkward name");
}

void test_missing_source() {
    const odysea::test::TemporaryTree tree("trash_missing");
    check(move_to_trash(tree.root() / "absent.txt").error == std::errc::no_such_file_or_directory,
          "trashing a missing entry reports a missing source");
    check(move_to_trash({}).error == std::errc::invalid_argument, "an empty path is rejected");
}

} // namespace

int main() {
    // Redirect the desktop data directory into a synthetic temporary tree.
    const odysea::test::TemporaryTree data_home("trash_data_home");
    const fs::path redirected = data_home.directory("data_home");
    ::setenv("XDG_DATA_HOME", redirected.c_str(), 1);

    test_encoding();
    test_home_trash_follows_the_data_home_variable();
    test_trashing_a_file();
    test_trashing_a_directory();
    test_name_collisions_are_resolved();
    test_encoded_names_round_trip_into_the_record();
    test_missing_source();
    return odysea::test::report("trash");
}
