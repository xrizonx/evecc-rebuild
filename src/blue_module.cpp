#include "blue_module.h"
#include "bluecrypto.h"
#include "py_build_cache.h"
#include <cstdlib>
#include <cstring>
#include <limits>

char *_password = NULL;

static PyObject *blue_SignData(PyObject *self, PyObject *args) {
	char *data = NULL;
	uint32_t data_size = 0;
	if (!PyArg_ParseTuple(args, "s#", &data, &data_size)) {
		return NULL;
	}
	uint32_t signature_size = 0;
	char *signature = SignData(data, data_size, &signature_size, _password);

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
	uint32_t code_size = 0;
	int zip = true;
	if (!PyArg_ParseTuple(args, "s#|i", &code, &code_size, &zip)) {
		return NULL;
	}

	uint32_t r = 0;
	char *output = JumbleString(code, code_size, &r, ctx->set_key_type, zip);
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
	uint32_t code_size = 0;
	int zip = true;

	if (!PyArg_ParseTuple(args, "s#|i", &code, &code_size, &zip)) {
		return NULL;
	}

	if (code_size > std::numeric_limits<uint32_t>::max() / 10) {
        PyErr_SetString(PyExc_OverflowError, "input too large to decompress");
        return NULL;
    }
    const uint32_t capacity = code_size * 10;
    char *output = (char *)calloc(1, capacity ? capacity : 1);
    if (output == NULL) return PyErr_NoMemory();

	int r = UnjumbleString(code, code_size, output, capacity, ctx->set_key_type, zip);
	if (r <= 0 || static_cast<uint32_t>(r) > capacity) {
        free(output);
        PyErr_SetString(PyExc_RuntimeError, "UnjumbleString failed or output buffer too small");
        return NULL;
    }

	PyObject *o = PyString_FromStringAndSize(output, r);
	free(output);
	return o;
}

static PyObject *blue_SetBuildCache(PyObject *self, PyObject *args) {
    char *code_path = NULL;
    char *code_data = NULL;
    uint32_t code_data_size = 0;
    if (!PyArg_ParseTuple(args, "s|s#", &code_path, &code_data, &code_data_size)) {
        PyErr_SetString(PyExc_TypeError, "expected (string, buffer)");
        return NULL;
    }

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
	{"JumbleString", blue_JumbleString, METH_VARARGS, "fuckin"},
	{"UnjumbleString", blue_UnjumbleString, METH_VARARGS, "fuckin"},
	{"SignData", blue_SignData, METH_VARARGS, "fuckin"},
    {"SetBuildCache", blue_SetBuildCache, METH_VARARGS, "fuckin"},
    {"GetBuildCache", blue_GetBuildCache, METH_VARARGS, "fuckin"},
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
