//
// Created by nick on 2/5/2023.
//

#include <iostream>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <limits>
#include "version.h"
#include "commands.h"
#include "util.h"

#include "scripts.h"
#ifdef _MSC_VER
#pragma warning(push)
#endif
#ifdef _MSC_VER
#pragma warning(disable:5033)
#endif
// this is just some dumb shit to prevent it from trying to load python27_d.lib
#ifdef _DEBUG
#undef _DEBUG
#include "Python.h"
#define _DEBUG
#else
#include "Python.h"
#endif
#ifdef _MSC_VER
#pragma warning(pop)
#endif

using namespace std::chrono;

static int set_python_string(PyObject* main, const char* name, const char* value) {
    PyObject* data = PyString_FromString(value);
    if (!data) return -1;
    const int result = PyObject_SetAttrString(main, name, data);
    Py_DECREF(data);
    return result;
}

int cmd_dumpcode(cmd_args* args) {
    if (!args || !args->input_file || !args->output_file) {
        LOG_F(ERROR, "dumpcode requires -i <compiled.code> and -o <directory>");
        return 2;
    }
    const auto started = high_resolution_clock::now();
    PyGILState_STATE gil = PyGILState_Ensure();
    PyObject* main = PyImport_AddModule("__main__");
    const int result = main &&
        set_python_string(main, "output_path", args->output_file) == 0 &&
        set_python_string(main, "input_path", args->input_file) == 0 ?
        PyRun_SimpleString(uncompile_code_script) : -1;
    if (result != 0 && PyErr_Occurred()) PyErr_Print();
    PyGILState_Release(gil);
    const auto ms = duration_cast<milliseconds>(
        high_resolution_clock::now() - started).count();
    LOG_F(INFO, "dumpcode %s (%lld ms)", result == 0 ? "completed" : "failed",
          static_cast<long long>(ms));
    return result == 0 ? 0 : 3;
}

int cmd_dumplib(cmd_args* args) {
    if (!args || !args->input_file || !args->output_file) {
        LOG_F(ERROR, "dumplib requires -i <library.ccp> and -o <directory>");
        return 2;
    }
    PyGILState_STATE gil = PyGILState_Ensure();
    PyObject* main = PyImport_AddModule("__main__");
    const int result = main &&
        set_python_string(main, "output_path", args->output_file) == 0 &&
        set_python_string(main, "input_path", args->input_file) == 0 ?
        PyRun_SimpleString(uncompile_lib_script) : -1;
    if (result != 0 && PyErr_Occurred()) PyErr_Print();
    PyGILState_Release(gil);
    return result == 0 ? 0 : 3;
}

int cmd_compilecode(cmd_args* args) {
    if (!args || args->input_files.empty() || !args->output_file) {
        LOG_F(ERROR, "compilecode requires -o <compiled.code> and one or more -I <source_dir>");
        return 2;
    }
    for (const auto& path : args->input_files) {
        if (!std::filesystem::is_directory(path)) {
            LOG_F(ERROR, "Invalid source directory: %s", path.c_str());
            return 2;
        }
    }
    const auto started = high_resolution_clock::now();
    PyGILState_STATE gil = PyGILState_Ensure();
    PyObject* main = PyImport_AddModule("__main__");
    int result = -1;
    if (main && set_python_string(main, "output_path", args->output_file) == 0) {
        PyObject* paths = PyList_New(static_cast<Py_ssize_t>(args->input_files.size()));
        if (paths) {
            bool ok = true;
            for (size_t i = 0; i < args->input_files.size(); ++i) {
                PyObject* value = PyString_FromString(args->input_files[i].c_str());
                if (!value) { ok = false; break; }
                // SetItem steals the single reference.
                if (PyList_SetItem(paths, static_cast<Py_ssize_t>(i), value) != 0) {
                    ok = false;
                    break;
                }
            }
            if (ok && PyObject_SetAttrString(main, "input_paths", paths) == 0)
                result = PyRun_SimpleString(compile_code_script);
            Py_DECREF(paths);
        }
    }
    if (result != 0 && PyErr_Occurred()) PyErr_Print();
    PyGILState_Release(gil);
    const auto ms = duration_cast<milliseconds>(
        high_resolution_clock::now() - started).count();
    LOG_F(INFO, "compilecode %s (%lld ms)", result == 0 ? "completed" : "failed",
          static_cast<long long>(ms));
    return result == 0 ? 0 : 3;
}

int cmd_unpyj(cmd_args* args) {
    if (!args || !args->input_file || !args->output_file || !ctx) return 2;
    FILE* file = fopen(args->input_file, "rb");
    if (!file) return 2;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return 2; }
    const long length = ftell(file);
    if (length <= 0 || length > 64L * 1024 * 1024) {
        fclose(file);
        LOG_F(ERROR, "Invalid or excessively large encrypted input");
        return 2;
    }
    rewind(file);
    std::vector<char> encrypted(static_cast<size_t>(length));
    const size_t read = fread(encrypted.data(), 1, encrypted.size(), file);
    fclose(file);
    if (read != encrypted.size()) return 2;

    std::vector<char> output;
    uint32_t decoded_size = 0;
    // Decompressed bytecode may be more than 2x its encrypted size.
    for (size_t capacity = std::max<size_t>(4096, encrypted.size() * 2);
         capacity <= 128 * 1024 * 1024; capacity *= 2) {
        output.resize(capacity);
        const uint32_t len = UnjumbleString(
            encrypted.data(), static_cast<uint32_t>(encrypted.size()),
            output.data(), static_cast<uint32_t>(output.size()),
            ctx->set_key_type);
        if (len > 0 && len <= output.size()) { decoded_size = len; break; }
    }
    if (!decoded_size) {
        LOG_F(ERROR, "Unable to decode input (wrong key or oversized output)");
        return 3;
    }
    file = fopen(args->output_file, "wb");
    if (!file) return 2;
    const size_t written = fwrite(output.data(), 1, decoded_size, file);
    const int closed = fclose(file);
    return written == decoded_size && closed == 0 ? 0 : 2;
}

int cmd_compilelib(cmd_args *args) {
	high_resolution_clock::time_point t1 = high_resolution_clock::now();
	PyGILState_STATE s = PyGILState_Ensure();
	PyObject *main = PyImport_AddModule("__main__");

	if (args->input_file != NULL) {
		PyObject *p = PyString_FromString(args->input_file);
		PyObject_SetAttrString(main, "input_path", p);
		Py_DECREF(p);
	}

	if (args->output_file != NULL) {
		PyObject *o = PyString_FromString(args->output_file);
		PyObject_SetAttrString(main, "output_path", o);
		//Py_DECREF(o);
	}
	int r = PyRun_SimpleString(compile_lib_script);

	PyGILState_Release(s);

	high_resolution_clock::time_point t2 = high_resolution_clock::now();
	auto duration = duration_cast<microseconds>(t2 - t1).count();
	std::cout << "Compiled lib in " << duration << "us\n";
	return r;
}


static const char *help_text = R"ADFAAF(
%s(%s): Compile EVE legacy client code

  commands:
    genkeys                            | generate several necessary files
    dumpkeys -i <file>                 | dump CCP's keys. supply path to clean blue.dll
    dumpcode -i <file>                 | dump and decompile .code file
    compilecode [-I <dir>]{--no-cache} | compile .code file, provide all needed dirs per documentation
    unpyj -i <file>                    | dump .pyj file (or really any JumbleString data)
    compilelib                       * | compile .lib file (currently this is non-functional)
    dumplib -i <file>                * | dump and decompile .ccp file
    help or --help or -h               | print this message
    runscript -i <file>                | run python script in the evecc environment
    console                            | start a python console in the evecc environment

    * signifies non-functional commands
    --no-cache: disables build caching
)ADFAAF";
int cmd_help() {
	printf(help_text, "evecc", GIT_HASH);

	return 0;
}

int cmd_genkey(cmd_args *args) {
    LOG_F(INFO,"making code accessors");

    if (!make_code_accessors(args->password)) {
        LOG_F(ERROR, "unable to generate RSA key pair");
        return 3;
    }
    LOG_F(INFO, "code accessors generated successfully");
    return 0;
}

int cmd_dumpkeys(cmd_args* args) {
    if (!args || !args->input_file) {
        LOG_F(ERROR, "dumpkeys requires -i <blue.dll>");
        return 2;
    }
    // blue.dll is parsed as binary data only, never loaded as an executable.
    FILE* input = std::fopen(args->input_file, "rb");
    if (!input) {
        LOG_F(ERROR, "Unable to open blue.dll: %s", args->input_file);
        return 2;
    }
    if (std::fseek(input, 0, SEEK_END) != 0) {
        std::fclose(input);
        return 2;
    }
    const long length = std::ftell(input);
    if (length <= 0 || length > 512L * 1024 * 1024 ||
        std::fseek(input, 0, SEEK_SET) != 0) {
        LOG_F(ERROR, "Invalid or excessive blue.dll size");
        std::fclose(input);
        return 2;
    }
    std::vector<char> data(static_cast<size_t>(length));
    const size_t read = std::fread(data.data(), 1, data.size(), input);
    std::fclose(input);
    if (read != data.size()) return 2;

    const char* pub = static_cast<const char*>(find_memory_pattern(
        data.data(), data.size(), RSA_PUB, sizeof(RSA_PUB) - 1));
    if (!pub || static_cast<size_t>(data.data() + data.size() - pub) < RSA_LEN) {
        LOG_F(ERROR, "Unable to find complete RSA public key in %s", args->input_file);
        return 3;
    }

    const char* crypt = static_cast<const char*>(find_memory_pattern(
        data.data(), data.size(), TDES_SIG, sizeof(TDES_SIG) - 1));
    if (!crypt || static_cast<size_t>(data.data() + data.size() - crypt) < TDES_LEN) {
        LOG_F(ERROR, "Unable to find complete 3DES SIMPLEBLOB in %s", args->input_file);
        return 3;
    }

    // keys_blob::dump writes the data immediately; its pointers do not
    // outlive this function, so no heap allocations or leaked buffers.
    keys_blob keys;
    keys.pub_key = const_cast<char*>(pub);
    keys.pub_key_size = RSA_LEN;
    keys.crypt_blob = const_cast<char*>(crypt);
    keys.crypt_blob_size = TDES_LEN;
    keys.dump(CCP_KEYS);
    LOG_F(INFO, "Extracted %zu-byte public key and %zu-byte 3DES key blob",
          keys.pub_key_size, keys.crypt_blob_size);
    return 0;
}
