#include "paths.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

namespace sillok::cli {
namespace {

struct Composition {
  char32_t first;
  char32_t second;
  char32_t composite;
};

constexpr Composition kCompositions[] = {
#include "compose_table.inc"
};

// Hangul syllable composition (Unicode chapter 3.12).
constexpr char32_t kSyllableBase = 0xAC00;
constexpr char32_t kLeadBase = 0x1100;
constexpr char32_t kVowelBase = 0x1161;
constexpr char32_t kTailBase = 0x11A7;
constexpr char32_t kVowels = 21;
constexpr char32_t kTails = 28;

std::optional<char32_t> Compose(char32_t first, char32_t second) {
  if (first >= kLeadBase && first < kLeadBase + 19 && second >= kVowelBase &&
      second < kVowelBase + kVowels) {
    return kSyllableBase +
           ((first - kLeadBase) * kVowels + (second - kVowelBase)) * kTails;
  }
  if (first >= kSyllableBase && first < kSyllableBase + 11172 &&
      (first - kSyllableBase) % kTails == 0 && second > kTailBase &&
      second < kTailBase + kTails) {
    return first + (second - kTailBase);
  }
  const auto* it = std::lower_bound(
      std::begin(kCompositions), std::end(kCompositions), Composition{first, second, 0},
      [](const Composition& a, const Composition& b) {
        return a.first != b.first ? a.first < b.first : a.second < b.second;
      });
  if (it != std::end(kCompositions) && it->first == first && it->second == second) {
    return it->composite;
  }
  return std::nullopt;
}

// Decodes well-formed UTF-8; nullopt for anything else.
std::optional<std::u32string> Decode(std::string_view text) {
  std::u32string out;
  for (std::size_t i = 0; i < text.size();) {
    const auto lead = static_cast<unsigned char>(text[i]);
    const std::size_t length = lead < 0x80 ? 1 : lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC2 ? 2 : 0;
    if (length == 0 || i + length > text.size()) return std::nullopt;
    char32_t code = length == 1 ? lead : lead & (0x7Fu >> length);
    for (std::size_t k = 1; k < length; ++k) {
      const auto byte = static_cast<unsigned char>(text[i + k]);
      if ((byte & 0xC0) != 0x80) return std::nullopt;
      code = (code << 6) | (byte & 0x3Fu);
    }
    out.push_back(code);
    i += length;
  }
  return out;
}

std::string Encode(std::u32string_view text) {
  std::string out;
  for (const char32_t c : text) {
    if (c < 0x80) {
      out.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (c >> 6)));
      out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (c >> 12)));
      out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (c >> 18)));
      out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
  }
  return out;
}

#ifdef _WIN32
std::string WideToUtf8(const wchar_t* text) {
  const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<std::size_t>(size > 0 ? size - 1 : 0), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
  return out;
}
#endif

}  // namespace

std::vector<std::string> Utf8Arguments(int argc, char** argv) {
#ifdef _WIN32
  static_cast<void>(argc);
  static_cast<void>(argv);
  int count = 0;
  wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &count);
  std::vector<std::string> args;
  for (int i = 0; i < count; ++i) args.push_back(WideToUtf8(wide[i]));
  LocalFree(wide);
  return args;
#else
  return std::vector<std::string>(argv, argv + argc);
#endif
}

std::filesystem::path PathFromUtf8(std::string_view utf8) {
  std::u8string text(utf8.size(), u8'\0');
  std::transform(utf8.begin(), utf8.end(), text.begin(),
                 [](char c) { return static_cast<char8_t>(c); });
  return std::filesystem::path(text);
}

std::string PathToUtf8(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  std::string out(text.size(), '\0');
  std::transform(text.begin(), text.end(), out.begin(),
                 [](char8_t c) { return static_cast<char>(c); });
  return out;
}

std::string ComposeForMatch(std::string_view utf8) {
  const std::optional<std::u32string> decoded = Decode(utf8);
  if (!decoded) return std::string(utf8);
  std::u32string out;
  for (const char32_t c : *decoded) {
    if (!out.empty()) {
      if (const auto composite = Compose(out.back(), c)) {
        out.back() = *composite;
        continue;
      }
    }
    out.push_back(c);
  }
  return Encode(out);
}

std::optional<std::filesystem::path> FindFile(std::string_view utf8_path) {
  const std::filesystem::path path = PathFromUtf8(utf8_path);
  std::error_code error;
  if (std::filesystem::exists(path, error)) return path;

  std::filesystem::path current =
      path.has_root_path() ? path.root_path() : std::filesystem::path(".");
  for (const std::filesystem::path& component : path.relative_path()) {
    const std::filesystem::path exact = current / component;
    if (std::filesystem::exists(exact, error)) {
      current = exact;
      continue;
    }
    const std::string wanted = ComposeForMatch(PathToUtf8(component));
    std::optional<std::filesystem::path> match;
    for (const auto& entry : std::filesystem::directory_iterator(current, error)) {
      if (ComposeForMatch(PathToUtf8(entry.path().filename())) == wanted) {
        match = entry.path();
        break;
      }
    }
    if (!match) return std::nullopt;
    current = *match;
  }
  return current;
}

std::filesystem::path DefaultModelsDir() {
#ifdef _WIN32
  const wchar_t* base = _wgetenv(L"LOCALAPPDATA");
  if (base != nullptr) return std::filesystem::path(base) / "sillok" / "models";
#elif defined(__APPLE__)
  const char* home = std::getenv("HOME");
  if (home != nullptr) {
    return std::filesystem::path(home) / "Library" / "Application Support" / "sillok" / "models";
  }
#else
  const char* data = std::getenv("XDG_DATA_HOME");
  if (data != nullptr && *data != '\0') return std::filesystem::path(data) / "sillok" / "models";
  const char* home = std::getenv("HOME");
  if (home != nullptr) return std::filesystem::path(home) / ".local" / "share" / "sillok" / "models";
#endif
  return std::filesystem::path("models");
}

}  // namespace sillok::cli
