#pragma once

#include <cstdint>
#include <string>

void set_cache_arguments(int argc, char** argv);
char* get_cache(int32_t* data_size, std::string filename);
void install_cache(std::string filename, char* data, int32_t data_size);
void set_do_build_cache(bool enabled);
