#include "blue_module.h"
#include "bluecrypto.h"
#include "py_build_cache.h"
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>
#include <algorithm>

char *_password = NULL;

static PyObject *blue_SignData(PyObject *self, PyObject *args) {
	char *data = NULL;
	int data_size = 0;
	if (!PyArg_ParseTuple(args, "s#", &data, &data_size)) {
		return NULL;
	}
	if (data_size < 0) { PyErr_SetString(PyExc_ValueError, "invalid signed data"); return NULL; }
    uint32_t signature_size = 0;
	char *signature = SignData(data, static_cast<uint32_t>(data_size), &signature_size, _password);

	if (signature == NULL) {
		PyErr_SetString(PyExc_RuntimeError, "SignData failed");
        return NULL;
	}

	PyObject *o = PyString_FromStringAndSize(signature, signature_size);
	free(signature);
	return o;

}

static PyObject *blue_JumbleString(PyObject *self, PyObject *args) {
	char *code = NULL;
	int code_size = 0;
	int zip = true;
	if (!PyArg_ParseTuple(args, "s#|i", &code, &code_size, &zip)) {
		return NULL;
	}

	uint32_t r = 0;
	if (code_size < 0 || !ctx) { PyErr_SetString(PyExc_ValueError, "invalid source data"); return NULL; }
    char *output = JumbleString(code, static_cast<uint32_t>(code_size), &r, ctx->set_key_type, zip);
	if (r <= 0 || output == NULL) {
		PyErr_SetString(PyExc_RuntimeError, "JumbleString failed");
        return NULL;
	}

	PyObject *o = PyString_FromStringAndSize(output, r);
	free(output);
	return o;
}

static PyObject *blue_UnjumbleString(PyObject *self, PyObject *args) {
    char *code = NULL;
    int code_size = 0;
    int zip = true;
    if (!PyArg_ParseTuple(args, "s#|i", &code, &code_size, &zip))
        return NULL;
    if (code_size <= 0 || !ctx) {
        PyErr_SetString(PyExc_ValueError, "encrypted input missing");
        return NULL;
    }

    // A highly compressed marshal stream can expand far beyond 10x.
    // Grow only up to a fixed ceiling to guard against decompression bombs.
    static const size_t limit = 128u * 1024u * 1024u;
    const size_t first = std::min(limit, std::max<size_t>(
        4096, static_cast<size_t>(code_size) * 2));
    for (size_t capacity = first; capacity <= limit;) {
        std::vector<char> output(capacity);
        uint32_t decoded = UnjumbleString(
            code, static_cast<uint32_t>(code_size), output.data(),
            static_cast<uint32_t>(output.size()), ctx->set_key_type, zip != 0);
        if (decoded && decoded <= capacity)
            return PyString_FromStringAndSize(output.data(), decoded);
        if (capacity == limit) break;
        capacity = std::min(limit, capacity * 2);
    }

    PyErr_SetString(PyExc_RuntimeError,
                    "failed to decrypt/decompress or 128 MiB output limit exceeded");
    return NULL;
}

static PyObject *blue_SetBuildCache(PyObject *self, PyObject *args) {
    char *code_path = NULL;
    char *code_data = NULL;
    int code_data_size = 0;
    if (!PyArg_ParseTuple(args, "s|s#", &code_path, &code_data, &code_data_size)) {
        PyErr_SetString(PyExc_TypeError, "expected (string, buffer)");
        return NULL;
    }

    if (code_data && code_data_size >= 0)
        install_cache(std::string(code_path), code_data, code_data_size);
    Py_IncRef(Py_None);
    return Py_None;
}

static PyObject *blue_GetBuildCache(PyObject *self, PyObject *args) {
    char *code_path = NULL;
    if (!PyArg_ParseTuple(args, "s", &code_path)) {
        PyErr_SetString(PyExc_TypeError, "expected (string)");
        return NULL;
    }

    int32_t cache_size = 0;
    char *cache_data = get_cache(&cache_size, std::string(code_path));
    if (cache_data == NULL) {
        Py_IncRef(Py_None);
        return Py_None;
    }

    PyObject *data = PyString_FromStringAndSize(cache_data, cache_size);
    free(cache_data);
    return data;
}

static PyMethodDef BlueMethods[] = {
	{"JumbleString", blue_JumbleString, METH_VARARGS, "EVECC legacy client cryptography or cache operation"},
	{"UnjumbleString", blue_UnjumbleString, METH_VARARGS, "EVECC legacy client cryptography or cache operation"},
	{"SignData", blue_SignData, METH_VARARGS, "EVECC legacy client cryptography or cache operation"},
    {"SetBuildCache", blue_SetBuildCache, METH_VARARGS, "EVECC legacy client cryptography or cache operation"},
    {"GetBuildCache", blue_GetBuildCache, METH_VARARGS, "EVECC legacy client cryptography or cache operation"},
	{NULL, NULL, 0, NULL},
};

PyMODINIT_FUNC initblue(void) {
	PyObject *m;

	m = Py_InitModule4("blue", BlueMethods, NULL, NULL, PYTHON_API_VERSION);
	if (m == NULL)
		return;
}


void set_password(char *password) {
    free(_password);
    _password = password ? strdup(password) : NULL;
}
