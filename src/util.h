#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

template <typename T, size_t N>
constexpr size_t countof(T(&)[N]) { return N; }

#pragma pack(push, 1)
struct PyPacket {
    uint32_t unk1;
    char uhh[2];
    char uhh1;
    char uhh2;
    uint32_t not_size;
    uint32_t unk2;
    uint32_t unk3;
};
#pragma pack(pop)

inline bool HasFile(const char* name) {
    if (name == nullptr) return false;
    std::error_code error;
    return std::filesystem::exists(name, error) && !error;
}

void* find_memory_pattern(const void* haystack, size_t haystack_size,
                          const void* needle, size_t needle_size);
wchar_t* chartow(const char* text);
int hctoi(char digit);
const char* str_ext(const char* filename);
std::string get_last_error_string();
