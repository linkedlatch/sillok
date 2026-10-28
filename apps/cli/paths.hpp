#ifndef SILLOK_APPS_CLI_PATHS_HPP_
#define SILLOK_APPS_CLI_PATHS_HPP_

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sillok::cli {

// Command-line arguments as UTF-8. On Windows they are taken from the wide
// command line, since argv uses the ANSI code page there.
std::vector<std::string> Utf8Arguments(int argc, char** argv);

std::filesystem::path PathFromUtf8(std::string_view utf8);
std::string PathToUtf8(const std::filesystem::path& path);

// Composes decomposed character sequences (Hangul jamo, accents), so that
// names stored decomposed (NFD, as macOS does) compare equal to the
// precomposed (NFC) names users type.
std::string ComposeForMatch(std::string_view utf8);

// The existing file `utf8_path` names, also when components of the stored
// name differ from the given one only in Unicode normalization.
std::optional<std::filesystem::path> FindFile(std::string_view utf8_path);

// Where models live unless --models or SILLOK_MODELS says otherwise.
std::filesystem::path DefaultModelsDir();

}  // namespace sillok::cli

#endif  // SILLOK_APPS_CLI_PATHS_HPP_
