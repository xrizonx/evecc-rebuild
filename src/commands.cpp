//
// Created by nick on 2/5/2023.
//

#include <iostream>
#include <algorithm>
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

int cmd_dumpcode(cmd_args *args) {
    high_resolution_clock::time_point t1 = high_resolution_clock::now();
    PyGILState_STATE s = PyGILState_Ensure();
    PyObject *main = PyImport_AddModule("__main__");
    if (args->input_file == NULL || args->output_file == NULL) {
        LOG_F(ERROR, "you must set -i <input file> and -o <output directory> for dumpcode");
        return -1;
    }

    PyObject *o = PyString_FromString(args->output_file);
    PyObject_SetAttrString(main, "output_path", o);

    PyObject *i = PyString_FromString(args->input_file);
    PyObject_SetAttrString(main, "input_path", i);

    int r = PyRun_SimpleString(uncompile_code_script);

    PyGILState_Release(s);

    high_resolution_clock::time_point t2 = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(t2 - t1).count();
    std::cout << "Dumped codefile in " << duration << "us\n";
    fflush(stdout);

    return 0;
}


int cmd_dumplib(cmd_args *args) {
    high_resolution_clock::time_point t1 = high_resolution_clock::now();
    PyGILState_STATE s = PyGILState_Ensure();
    PyObject *main = PyImport_AddModule("__main__");
    if (args->input_file == NULL || args->output_file == NULL) {
        LOG_F(ERROR, "you must set -i <input file> and -o <output directory> for dumplib");
        return -1;
    }

    PyObject *o = PyString_FromString(args->output_file);
    PyObject_SetAttrString(main, "output_path", o);

    PyObject *i = PyString_FromString(args->input_file);
    PyObject_SetAttrString(main, "input_path", i);

    int r = PyRun_SimpleString(uncompile_lib_script);

    PyGILState_Release(s);

    high_resolution_clock::time_point t2 = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(t2 - t1).count();
    std::cout << "Dumped codefile in " << duration << "us\n";
    fflush(stdout);

    return 0;
}

int cmd_compilecode(cmd_args *args) {
	high_resolution_clock::time_point t1 = high_resolution_clock::now();
	PyGILState_STATE s = PyGILState_Ensure();
	PyObject *main = PyImport_AddModule("__main__");

	if (args->input_files.size() > 0) {
		//PyObject *p = PyString_FromString(input_file);
		//PyObject_SetAttrString(main, "input_path", p);
		//Py_DECREF(p);
		PyObject *p = PyList_New(args->input_files.size());
		for (uint32_t i = 0; i < args->input_files.size(); i++) {
			PyObject *str =
				PyString_FromString(args->input_files[i].c_str());
			//PyList_Append(p, str);
			PyList_SetItem(p, i, str);
			//Py_DECREF(str);
		}
		PyObject_SetAttrString(main, "input_paths", p);
		//Py_DECREF(p);
	} else {
		LOG_F(ERROR,"No input paths set. (Use -I flag)");
		return 3;
	}

	if (args->output_file != NULL) {
		PyObject *o = PyString_FromString(args->output_file);
		PyObject_SetAttrString(main, "output_path", o);
		//Py_DECREF(o);
	}

	//Py_DECREF(main);

	int r = PyRun_SimpleString(compile_code_script);

	PyGILState_Release(s);

	high_resolution_clock::time_point t2 = high_resolution_clock::now();
	auto duration = duration_cast<microseconds>(t2 - t1).count();
	std::cout << "Compiled codefile in " << duration << "us\n";
	fflush(stdout);
	return r;
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

    if (make_code_accessors(args->password)) {
        LOG_F(INFO,"code accessors generated sucessfully");
    }

    return 0;
}

int cmd_dumpkeys(cmd_args *args) {
    if (args->input_file == NULL) {
        LOG_F(ERROR, "you must supply -i <input file>");
        return -1;
    }
    FILE *f = fopen(args->input_file, "rb");
    if (f == NULL) {
        LOG_F(ERROR, "could not open input file: %s", args->input_file);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    int size = ftell(f);
    rewind(f);
    char *buf = (char *)malloc(size);
    char *rewind = buf;
    fread(buf, sizeof(char), size, f);
    fclose(f);


    keys_blob *keys = new keys_blob;
    keys->pub_key = (char *)calloc(RSA_LEN, sizeof(char));
    keys->crypt_blob  = (char *)calloc(TDES_LEN, sizeof(char));

    LOG_F(INFO,"scanning for RSA public key");
    char *ptr = (char *)find_memory_pattern(buf, size, RSA_PUB, sizeof(RSA_PUB) - 1);
    if (ptr == NULL || (ptr - buf) + RSA_LEN > size) {
        LOG_F(ERROR,"could not find RSA_PUB in input file: %s", args->input_file);
        return -2;
    }
    buf = rewind;
    LOG_F(INFO, "RSA public key found!");
    memcpy(keys->pub_key, ptr, RSA_LEN);
    keys->pub_key_size = RSA_LEN;

    LOG_F(INFO,"scanning for 3DES crypt key");
    ptr = (char *)find_memory_pattern(buf, size, TDES_SIG, sizeof(TDES_SIG) - 1);
    if (ptr == NULL || (ptr - buf) + TDES_LEN > size) {
        LOG_F(INFO,"could not find TDES_SIG in input file: %s", args->input_file);
        return -3;
    }
    LOG_F(INFO, "3DES crypt key found!");
    memcpy(keys->crypt_blob, ptr, TDES_LEN);
    keys->crypt_blob_size = TDES_LEN;

    keys->dump(CCP_KEYS);
    return 0;
}
