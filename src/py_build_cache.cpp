#include "py_build_cache.h"
#include "crc32c/crc32c.h"
#include "loguru/loguru.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>

namespace fs = std::filesystem;
namespace {
bool caching_enabled = true;
std::string argument_fingerprint;

fs::path path_for(const std::string& source) {
    const uint32_t arg_hash = crc32c::Crc32c(argument_fingerprint);
    const uint32_t source_hash = crc32c::Crc32c(source);
    return fs::path(".build_cache") / std::to_string(arg_hash) /
           (std::to_string(source_hash) + ".bin");
}
}

void set_cache_arguments(int argc, char** argv) {
    argument_fingerprint.clear();
    for (int i = 0; i < argc; ++i) {
        argument_fingerprint.append(argv[i] ? argv[i] : "");
        argument_fingerprint.push_back('\0');
    }
}

void set_do_build_cache(bool enabled) { caching_enabled = enabled; }

char* get_cache(int32_t* data_size, std::string filename) {
    if (!caching_enabled || !data_size) return nullptr;
    const fs::path source(filename), cache = path_for(filename);
    std::error_code ec;
    if (!fs::exists(source, ec) || ec || !fs::exists(cache, ec) || ec)
        return nullptr;
    if (fs::last_write_time(source, ec) > fs::last_write_time(cache, ec) || ec)
        return nullptr;
    const auto length = fs::file_size(cache, ec);
    if (ec || length > static_cast<uintmax_t>(std::numeric_limits<int32_t>::max()))
        return nullptr;
    FILE* file = std::fopen(cache.string().c_str(), "rb");
    if (!file) return nullptr;
    auto* data = static_cast<char*>(std::malloc(length ? length : 1));
    if (!data) { std::fclose(file); return nullptr; }
    const size_t read = std::fread(data, 1, static_cast<size_t>(length), file);
    std::fclose(file);
    if (read != length) { std::free(data); return nullptr; }
    *data_size = static_cast<int32_t>(length);
    return data;
}

void install_cache(std::string filename, char* data, int32_t data_size) {
    if (!caching_enabled || !data || data_size < 0) return;
    const fs::path cache = path_for(filename);
    std::error_code ec;
    fs::create_directories(cache.parent_path(), ec);
    if (ec) { LOG_F(ERROR, "Unable to create cache: %s", ec.message().c_str()); return; }
    FILE* file = std::fopen(cache.string().c_str(), "wb");
    if (!file) return;
    const size_t count = std::fwrite(data, 1, static_cast<size_t>(data_size), file);
    std::fclose(file);
    if (count != static_cast<size_t>(data_size)) { fs::remove(cache, ec); return; }
    const auto source_time = fs::last_write_time(filename, ec);
    if (!ec) fs::last_write_time(cache, source_time, ec);
}
