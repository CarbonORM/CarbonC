#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "carbon.h"

static PyObject *carbon_py_version(PyObject *self, PyObject *args) {
    (void) self;
    (void) args;
    return PyUnicode_FromString(carbon_version());
}

static PyObject *carbon_py_hello_world(PyObject *self, PyObject *args) {
    (void) self;
    (void) args;
    return PyUnicode_FromString(carbon_hello_world());
}

static PyObject *carbon_py_status_message(PyObject *self, PyObject *args) {
    int status;

    (void) self;
    if (!PyArg_ParseTuple(args, "i:status_message", &status)) {
        return NULL;
    }
    return PyUnicode_FromString(carbon_status_message((carbon_status) status));
}

static PyObject *carbon_py_status_code(PyObject *self, PyObject *args) {
    int status;

    (void) self;
    if (!PyArg_ParseTuple(args, "i:status_code", &status)) {
        return NULL;
    }
    return PyUnicode_FromString(carbon_status_code((carbon_status) status));
}

static PyObject *carbon_py_result_dict(const carbon_compile_result *result) {
    return Py_BuildValue(
            "{s:i,s:s,s:s#,s:s#,s:s#,s:s#}",
            "status", (int) result->status,
            "status_code", carbon_status_code(result->status),
            "sql", result->sql.data == NULL ? "" : result->sql.data, (Py_ssize_t) result->sql.length,
            "params_json", result->params_json.data == NULL ? "" : result->params_json.data,
            (Py_ssize_t) result->params_json.length,
            "allowlist_key", result->allowlist_key.data == NULL ? "" : result->allowlist_key.data,
            (Py_ssize_t) result->allowlist_key.length,
            "error", result->error.data == NULL ? "" : result->error.data, (Py_ssize_t) result->error.length);
}

static PyObject *carbon_py_compile_query(PyObject *self, PyObject *args, PyObject *kwargs) {
    static char *kwlist[] = {"query_json", "schema_json", "dialect", NULL};
    const char *query_json;
    const char *schema_json = "{}";
    const char *dialect = "mysql";
    Py_ssize_t query_json_length;
    Py_ssize_t schema_json_length = 2;
    carbon_context *context;
    carbon_compile_request request;
    carbon_compile_result result;
    carbon_status status;
    PyObject *out;

    (void) self;
    if (!PyArg_ParseTupleAndKeywords(
                args,
                kwargs,
                "s#|s#s:compile_query",
                kwlist,
                &query_json,
                &query_json_length,
                &schema_json,
                &schema_json_length,
                &dialect)) {
        return NULL;
    }

    context = carbon_context_new();
    if (context == NULL) {
        return PyErr_NoMemory();
    }

    request.dialect = dialect;
    request.schema_json = schema_json;
    request.schema_json_length = (size_t) schema_json_length;
    request.query_json = query_json;
    request.query_json_length = (size_t) query_json_length;

    carbon_compile_result_init(&result);
    status = carbon_compile_query(context, &request, &result);
    if (status == CARBON_STATUS_OUT_OF_MEMORY) {
        carbon_compile_result_free(&result);
        carbon_context_free(context);
        return PyErr_NoMemory();
    }

    out = carbon_py_result_dict(&result);
    carbon_compile_result_free(&result);
    carbon_context_free(context);
    return out;
}

static PyObject *carbon_py_normalize_allowlist_sql(PyObject *self, PyObject *args) {
    const char *sql;
    Py_ssize_t sql_length;
    carbon_buffer out;
    carbon_buffer error;
    carbon_status status;
    PyObject *result;

    (void) self;
    if (!PyArg_ParseTuple(args, "s#:normalize_allowlist_sql", &sql, &sql_length)) {
        return NULL;
    }

    (void) sql_length;
    status = carbon_normalize_allowlist_sql(sql, &out, &error);
    if (status != CARBON_STATUS_OK) {
        PyObject *message = PyUnicode_FromString(error.data == NULL ? carbon_status_message(status) : error.data);
        carbon_buffer_free(&out);
        carbon_buffer_free(&error);
        if (message == NULL) {
            return NULL;
        }
        PyErr_SetObject(PyExc_ValueError, message);
        Py_DECREF(message);
        return NULL;
    }

    result = PyUnicode_FromStringAndSize(out.data == NULL ? "" : out.data, (Py_ssize_t) out.length);
    carbon_buffer_free(&out);
    carbon_buffer_free(&error);
    return result;
}

static PyObject *carbon_py_schema_metadata(PyObject *self, PyObject *args) {
    const char *schema_json = "{}";
    Py_ssize_t schema_json_length = 2;
    carbon_buffer out;
    carbon_buffer error;
    carbon_status status;
    PyObject *result;

    (void) self;
    if (!PyArg_ParseTuple(args, "|s#:schema_metadata", &schema_json, &schema_json_length)) {
        return NULL;
    }

    status = carbon_schema_metadata(schema_json, (size_t) schema_json_length, &out, &error);
    if (status != CARBON_STATUS_OK) {
        PyObject *message = PyUnicode_FromString(error.data == NULL ? carbon_status_message(status) : error.data);
        carbon_buffer_free(&out);
        carbon_buffer_free(&error);
        if (message == NULL) {
            return NULL;
        }
        PyErr_SetObject(PyExc_ValueError, message);
        Py_DECREF(message);
        return NULL;
    }

    result = PyUnicode_FromStringAndSize(out.data == NULL ? "" : out.data, (Py_ssize_t) out.length);
    carbon_buffer_free(&out);
    carbon_buffer_free(&error);
    return result;
}

static PyMethodDef carbon_methods[] = {
        {"version", carbon_py_version, METH_NOARGS, "Return the CarbonC version."},
        {"hello_world", carbon_py_hello_world, METH_NOARGS, "Return the CarbonC smoke-test message."},
        {"status_code", carbon_py_status_code, METH_VARARGS, "Return a stable CarbonC status code."},
        {"status_message", carbon_py_status_message, METH_VARARGS, "Return a CarbonC status message."},
        {"compile_query", (PyCFunction) carbon_py_compile_query, METH_VARARGS | METH_KEYWORDS,
         "Compile canonical C6 JSON into SQL, params JSON, allowlist key, and diagnostics."},
        {"normalize_allowlist_sql", carbon_py_normalize_allowlist_sql, METH_VARARGS,
         "Normalize generated SQL into a CarbonORM allowlist key."},
        {"schema_metadata", carbon_py_schema_metadata, METH_VARARGS,
         "Normalize C6 schema metadata into JSON for generated binding types."},
        {NULL, NULL, 0, NULL}
};

static struct PyModuleDef carbon_module = {
        PyModuleDef_HEAD_INIT,
        "carbon",
        "Python bindings for the CarbonC portable CarbonORM kernel.",
        -1,
        carbon_methods
};

PyMODINIT_FUNC PyInit_carbon(void) {
    return PyModule_Create(&carbon_module);
}
