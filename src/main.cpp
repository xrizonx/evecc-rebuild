#define Py_NO_ENABLE_SHARED
#include <Python.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "commands.h"
#include "blue_module.h"
#include "bluecrypto.h"
#include "util.h"
#include "loguru/loguru.hpp"
#include "py_build_cache.h"

namespace fs = std::filesystem;

static bool has_argument(int argc, char** argv, const char* argument, int place) {
    if (place >= 0) return place < argc && std::strcmp(argument, argv[place]) == 0;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argument, argv[i]) == 0) return true;
    return false;
}

static char* parse_argument(int argc, char** argv, const char* argument) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argument, argv[i]) == 0)
            return i + 1 < argc ? argv[i + 1] : nullptr;
    return nullptr;
}

static std::vector<std::string> parse_all_string_args(int argc, char** argv,
                                                       const char* argument) {
    std::vector<std::string> values;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argument, argv[i]) == 0) values.emplace_back(argv[++i]);
    return values;
}

int main(int argc, char** argv) {
    loguru::g_colorlogtostderr = false;
    loguru::add_file("evecc.log", loguru::Truncate, loguru::Verbosity_INFO);

    // Find the runtime relative to the executable, not the caller's cwd.
    fs::path binary = fs::absolute(argv[0]);
#if defined(__linux__)
    std::error_code ec;
    const fs::path proc_binary = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) binary = proc_binary;
#endif
    const fs::path python_dir = binary.parent_path() / "Python";
#ifdef _WIN32
    const fs::path stdlib = python_dir / "Lib";
#else
    const fs::path stdlib = python_dir / "lib" / "python2.7";
#endif
    const fs::path uncompyle2 = python_dir / "uncompyle2.zip";
    const fs::path uncompyle6 = python_dir / "uncompyle6.zip";

    if (!HasFile(stdlib.string().c_str()) ||
        !HasFile(uncompyle2.string().c_str()) ||
        !HasFile(uncompyle6.string().c_str())) {
        LOG_F(ERROR, "Missing embedded Python 2.7 runtime under %s",
              python_dir.string().c_str());
        return 1;
    }

    std::string home = python_dir.string();
    std::string program = binary.string();
#ifdef _WIN32
    const char separator = ';';
#else
    const char separator = ':';
#endif
    const std::string py_path =
        stdlib.string() + separator + uncompyle2.string() +
        separator + uncompyle6.string();

    Py_NoSiteFlag = 1;
    Py_SetPythonHome(const_cast<char*>(home.c_str()));
    Py_SetProgramName(const_cast<char*>(program.c_str()));
    Py_Initialize();
    PySys_SetPath(const_cast<char*>(py_path.c_str()));
    // Import the codecs registry after sys.path is populated. Python 2.7 may
    // initialize without it when embedded into a non-system executable.
    if (PyRun_SimpleString("import encodings, codecs\\ncodecs.lookup('utf-8')") != 0) {
        LOG_F(ERROR, "Unable to initialize Python 2.7 encodings from %s",
              stdlib.string().c_str());
        Py_Finalize();
        return 2;
    }
    initblue();

    cmd_args args;
    args.do_dumpcode = has_argument(argc, argv, "dumpcode", 1);
    args.do_dumplib = has_argument(argc, argv, "dumplib", 1);
    args.do_compilecode = has_argument(argc, argv, "compilecode", 1);
    args.do_compilelib = has_argument(argc, argv, "compilelib", 1);
    args.do_dumpkeys = has_argument(argc, argv, "dumpkeys", 1);
    args.do_genkeys = has_argument(argc, argv, "genkeys", 1);
    args.do_help = has_argument(argc, argv, "help", 1) ||
                   has_argument(argc, argv, "--help", -1) ||
                   has_argument(argc, argv, "-h", -1);
    args.do_unpyj = has_argument(argc, argv, "unpyj", 1);
    args.do_console = has_argument(argc, argv, "console", 1);
    args.do_runscript = has_argument(argc, argv, "runscript", 1);
    args.do_cleancache = has_argument(argc, argv, "cleancache", 1);
    args.do_nocache = has_argument(argc, argv, "--no-cache", -1);
    args.input_file = parse_argument(argc, argv, "-i");
    args.output_file = parse_argument(argc, argv, "-o");
    args.password = parse_argument(argc, argv, "--password");
    args.key_type = parse_argument(argc, argv, "-k");
    args.input_files = parse_all_string_args(argc, argv, "-I");

    set_cache_arguments(argc, argv);
    set_do_build_cache(!args.do_nocache);
    int result = 0;

    if (args.do_help) {
        result = cmd_help();
    } else if (args.do_cleancache) {
        std::error_code ec;
        fs::remove_all(".build_cache", ec);
        if (ec) { LOG_F(ERROR, "Unable to clear cache: %s", ec.message().c_str()); result = 1; }
    } else {
        const bool needs_crypto = args.do_dumpcode || args.do_runscript ||
            args.do_console || args.do_unpyj || args.do_compilelib ||
            args.do_compilecode || args.do_dumplib;
        if (needs_crypto) {
            result = init_cryptcontext(args.password);
            if (result == 0 && ctx) ctx->set_key_type = parse_key_type(args.key_type);
        } else if (args.do_genkeys || args.do_dumpkeys) {
            result = init_cryptcontext_gen(args.password);
        }
        if (result == 0) {
            set_password(args.password);
            if (args.do_dumpcode) result = cmd_dumpcode(&args);
            else if (args.do_dumplib) result = cmd_dumplib(&args);
            else if (args.do_dumpkeys) result = cmd_dumpkeys(&args);
            else if (args.do_genkeys) result = cmd_genkey(&args);
            else if (args.do_compilecode) result = cmd_compilecode(&args);
            else if (args.do_compilelib) result = cmd_compilelib(&args);
            else if (args.do_unpyj) {
                result = args.input_file && args.output_file ? cmd_unpyj(&args) : 2;
            } else if (args.do_console) {
                result = PyRun_SimpleString("import code\ncode.interact()");
            } else if (args.do_runscript) {
                if (!args.input_file) result = 2;
                else {
                    FILE* file = std::fopen(args.input_file, "rb");
                    if (!file) result = 2;
                    else {
                        result = PyRun_SimpleFile(file, args.input_file);
                        std::fclose(file);
                    }
                }
            } else result = cmd_help();
        }
    }

    Py_Finalize();
    return result;
}
