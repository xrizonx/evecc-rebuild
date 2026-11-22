#include "util.h"

#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#ifdef _WIN32
#include <Windows.h>
#endif

const char* str_ext(const char* filename) {
    if (!filename) return "";
    const char* dot = std::strrchr(filename, '.');
    return !dot || dot == filename ? "" : dot + 1;
}

void* find_memory_pattern(const void* haystack, size_t haystack_size,
                          const void* needle, size_t needle_size) {
    if (!haystack || !needle || !needle_size || needle_size > haystack_size)
        return nullptr;
    const auto* data = static_cast<const unsigned char*>(haystack);
    const auto* pattern = static_cast<const unsigned char*>(needle);
    for (size_t i = 0; i <= haystack_size - needle_size; ++i)
        if (data[i] == pattern[0] &&
            std::memcmp(data + i, pattern, needle_size) == 0)
            return const_cast<unsigned char*>(data + i);
    return nullptr;
}

int hctoi(char h) {
    const auto c = static_cast<unsigned char>(h);
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

wchar_t* chartow(const char* text) {
    if (!text) return nullptr;
    const size_t count = std::mbstowcs(nullptr, text, 0);
    if (count == static_cast<size_t>(-1)) return nullptr;
    auto* result = static_cast<wchar_t*>(std::calloc(count + 1, sizeof(wchar_t)));
    if (result) std::mbstowcs(result, text, count + 1);
    return result;
}

std::string get_last_error_string() {
#ifdef _WIN32
    const DWORD err = GetLastError();
    if (!err) return {};
    LPSTR buffer = nullptr;
    const DWORD size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buffer), 0, nullptr);
    const std::string message = buffer && size ? std::string(buffer, size) :
                                "Windows error " + std::to_string(err);
    if (buffer) LocalFree(buffer);
    return message;
#else
    return std::strerror(errno);
#endif
}
