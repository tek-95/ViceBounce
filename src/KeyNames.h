#pragma once
#include <array>
#include <cctype>
#include <cstdlib>
#include <string>

namespace vicebounce {
// Up to two Windows virtual-key codes per action; 0 marks an unused slot.
using KeyList = std::array<int, 2>;

// Translates one [Keys] name to a virtual-key code, or 0 if unknown.
// Punctuation names follow the physical key, so they work on Russian and other layouts too.
inline int KeyCode(std::string name) {
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front()))) name.erase(name.begin());
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.pop_back();
    if (name.empty()) return 0;
    for (auto& c : name) c = char(std::toupper(static_cast<unsigned char>(c)));
    if (name.size() == 1 && ((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= '0' && name[0] <= '9'))) return name[0];
    if (name.size() > 2 && name.rfind("0X", 0) == 0) {
        char* end = nullptr;
        const long code = std::strtol(name.c_str() + 2, &end, 16);
        return end && !*end && code > 0 && code < 0xFF ? int(code) : 0;
    }
    if (name[0] == 'F' && name.size() <= 3) {
        char* end = nullptr;
        const long n = std::strtol(name.c_str() + 1, &end, 10);
        if (end && !*end && n >= 1 && n <= 24) return 0x70 + int(n) - 1;
    }
    if (name.rfind("NUMPAD", 0) == 0 && name.size() == 7 && name[6] >= '0' && name[6] <= '9') return 0x60 + (name[6] - '0');
    struct Named { const char* name; int code; };
    static constexpr Named named[] = {
        {"HOME",0x24},{"END",0x23},{"INSERT",0x2D},{"DELETE",0x2E},{"PAGEUP",0x21},{"PAGEDOWN",0x22},
        {"BACKSPACE",0x08},{"TAB",0x09},{"PAUSE",0x13},{"SCROLLLOCK",0x91},
        {"GRAVE",0xC0},{"TILDE",0xC0},{"MINUS",0xBD},{"EQUALS",0xBB},{"COMMA",0xBC},{"PERIOD",0xBE},
        {"SLASH",0xBF},{"SEMICOLON",0xBA},{"APOSTROPHE",0xDE},{"LEFTBRACKET",0xDB},{"RIGHTBRACKET",0xDD},
        {"BACKSLASH",0xDC},{"NUMPADMULTIPLY",0x6A},{"NUMPADPLUS",0x6B},{"NUMPADMINUS",0x6D},
        {"NUMPADDECIMAL",0x6E},{"NUMPADDIVIDE",0x6F},
    };
    for (const auto& entry : named) if (name == entry.name) return entry.code;
    return 0;
}

// Parses "Grave, Pause" style values. "None" clears the action; an unknown name keeps the fallback.
inline KeyList ParseKeyList(const char* text, KeyList fallback) {
    if (!text) return fallback;
    std::string value(text);
    value = value.substr(0, value.find(';'));  // allow "End ; comment"
    KeyList result{};
    size_t count = 0, start = 0;
    bool any = false;
    while (start <= value.size()) {
        const size_t comma = value.find(',', start);
        std::string part = value.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        std::string trimmed;
        for (char c : part) if (!std::isspace(static_cast<unsigned char>(c))) trimmed += char(std::toupper(static_cast<unsigned char>(c)));
        if (!trimmed.empty()) {
            any = true;
            if (trimmed != "NONE") {
                const int code = KeyCode(trimmed);
                if (!code) return fallback;
                if (count < result.size()) result[count++] = code;
            }
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return any ? result : fallback;
}
}
