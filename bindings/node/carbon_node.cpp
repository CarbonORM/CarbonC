#include "carbon_node.h"

#include <stdlib.h>

#include "carbon.h"

#ifndef NODE_GYP_MODULE_NAME
#define NODE_GYP_MODULE_NAME carbon
#endif

static napi_value carbon_node_null(napi_env env) {
    napi_value value;
    napi_get_null(env, &value);
    return value;
}

static bool carbon_node_is_undefined(napi_env env, napi_value value) {
    napi_valuetype type;

    if (napi_typeof(env, value, &type) != napi_ok) {
        return false;
    }

    return type == napi_undefined;
}

static bool carbon_node_get_string(
        napi_env env,
        napi_value value,
        const char *name,
        char **out,
        size_t *out_length) {
    napi_valuetype type;
    size_t length;
    char *buffer;

    *out = NULL;
    *out_length = 0;

    if (napi_typeof(env, value, &type) != napi_ok || type != napi_string) {
        napi_throw_type_error(env, NULL, name);
        return false;
    }

    if (napi_get_value_string_utf8(env, value, NULL, 0, &length) != napi_ok) {
        napi_throw_error(env, NULL, "CarbonC failed to read string length");
        return false;
    }

    buffer = static_cast<char *>(malloc(length + 1));
    if (buffer == NULL) {
        napi_throw_error(env, NULL, carbon_status_message(CARBON_STATUS_OUT_OF_MEMORY));
        return false;
    }

    if (napi_get_value_string_utf8(env, value, buffer, length + 1, out_length) != napi_ok) {
        free(buffer);
        napi_throw_error(env, NULL, "CarbonC failed to read string value");
        return false;
    }

    *out = buffer;
    return true;
}

static bool carbon_node_set_string(
        napi_env env,
        napi_value object,
        const char *key,
        const carbon_buffer *buffer) {
    napi_value value;
    const char *data = "";
    size_t length = 0;

    if (buffer != NULL && buffer->data != NULL) {
        data = buffer->data;
        length = buffer->length;
    }

    if (napi_create_string_utf8(env, data, length, &value) != napi_ok) {
        napi_throw_error(env, NULL, "CarbonC failed to allocate result string");
        return false;
    }

    if (napi_set_named_property(env, object, key, value) != napi_ok) {
        napi_throw_error(env, NULL, "CarbonC failed to assign result field");
        return false;
    }

    return true;
}

static bool carbon_node_set_cstring(napi_env env, napi_value object, const char *key, const char *data) {
    napi_value value;

    if (napi_create_string_utf8(env, data == NULL ? "" : data, NAPI_AUTO_LENGTH, &value) != napi_ok) {
        napi_throw_error(env, NULL, "CarbonC failed to allocate result string");
        return false;
    }

    if (napi_set_named_property(env, object, key, value) != napi_ok) {
        napi_throw_error(env, NULL, "CarbonC failed to assign result field");
        return false;
    }

    return true;
}

static bool carbon_node_set_int32(napi_env env, napi_value object, const char *key, int32_t number) {
    napi_value value;

    if (napi_create_int32(env, number, &value) != napi_ok) {
        napi_throw_error(env, NULL, "CarbonC failed to allocate result number");
        return false;
    }

    if (napi_set_named_property(env, object, key, value) != napi_ok) {
        napi_throw_error(env, NULL, "CarbonC failed to assign result field");
        return false;
    }

    return true;
}

static napi_value carbon_node_version(napi_env env, napi_callback_info info) {
    napi_value value;

    (void) info;

    if (napi_create_string_utf8(env, carbon_version(), NAPI_AUTO_LENGTH, &value) != napi_ok) {
        napi_throw_error(env, NULL, "CarbonC failed to allocate version string");
        return carbon_node_null(env);
    }

    return value;
}

static napi_value carbon_node_hello_world(napi_env env, napi_callback_info info) {
    napi_value value;

    (void) info;

    if (napi_create_string_utf8(env, carbon_hello_world(), NAPI_AUTO_LENGTH, &value) != napi_ok) {
        napi_throw_error(env, NULL, "CarbonC failed to allocate hello world string");
        return carbon_node_null(env);
    }

    return value;
}

static napi_value carbon_node_status_message(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    int32_t status;
    napi_value value;

    if (napi_get_cb_info(env, info, &argc, args, NULL, NULL) != napi_ok) {
        napi_throw_error(env, NULL, "CarbonC failed to read arguments");
        return carbon_node_null(env);
    }

    if (argc < 1) {
        napi_throw_type_error(env, NULL, "status must be a number");
        return carbon_node_null(env);
    }

    if (napi_get_value_int32(env, args[0], &status) != napi_ok) {
        napi_throw_type_error(env, NULL, "status must be a number");
        return carbon_node_null(env);
    }

    if (napi_create_string_utf8(
                env,
                carbon_status_message(static_cast<carbon_status>(status)),
                NAPI_AUTO_LENGTH,
                &value) != napi_ok) {
        napi_throw_error(env, NULL, "CarbonC failed to allocate status string");
        return carbon_node_null(env);
    }

    return value;
}

static napi_value carbon_node_status_code(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    int32_t status;
    napi_value value;

    if (napi_get_cb_info(env, info, &argc, args, NULL, NULL) != napi_ok) {
        napi_throw_error(env, NULL, "CarbonC failed to read arguments");
        return carbon_node_null(env);
    }

    if (argc < 1) {
        napi_throw_type_error(env, NULL, "status must be a number");
        return carbon_node_null(env);
    }

    if (napi_get_value_int32(env, args[0], &status) != napi_ok) {
        napi_throw_type_error(env, NULL, "status must be a number");
        return carbon_node_null(env);
    }

    if (napi_create_string_utf8(
                env,
                carbon_status_code(static_cast<carbon_status>(status)),
                NAPI_AUTO_LENGTH,
                &value) != napi_ok) {
        napi_throw_error(env, NULL, "CarbonC failed to allocate status code string");
        return carbon_node_null(env);
    }

    return value;
}

static napi_value carbon_node_compile_query(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3];
    char *query_json = NULL;
    char *schema_json = NULL;
    char *dialect = NULL;
    size_t query_json_length = 0;
    size_t schema_json_length = 0;
    size_t dialect_length = 0;
    bool owns_schema_json = false;
    bool owns_dialect = false;
    carbon_context *context = NULL;
    carbon_compile_request request;
    carbon_compile_result result;
    carbon_buffer diagnostics = {NULL, 0};
    carbon_buffer diagnostic_error = {NULL, 0};
    carbon_status status;
    carbon_status diagnostic_status;
    napi_value object;
    bool ok = false;

    carbon_compile_result_init(&result);

    if (napi_get_cb_info(env, info, &argc, args, NULL, NULL) != napi_ok) {
        napi_throw_error(env, NULL, "CarbonC failed to read arguments");
        goto cleanup;
    }

    if (argc < 1) {
        napi_throw_type_error(env, NULL, "query_json must be a string");
        goto cleanup;
    }

    if (!carbon_node_get_string(
                env,
                args[0],
                "query_json must be a string",
                &query_json,
                &query_json_length)) {
        goto cleanup;
    }

    if (argc >= 2 && !carbon_node_is_undefined(env, args[1])) {
        if (!carbon_node_get_string(
                    env,
                    args[1],
                    "schema_json must be a string",
                    &schema_json,
                    &schema_json_length)) {
            goto cleanup;
        }
        owns_schema_json = true;
    } else {
        schema_json = const_cast<char *>("{}");
        schema_json_length = 2;
    }

    if (argc >= 3 && !carbon_node_is_undefined(env, args[2])) {
        if (!carbon_node_get_string(
                    env,
                    args[2],
                    "dialect must be a string",
                    &dialect,
                    &dialect_length)) {
            goto cleanup;
        }
        owns_dialect = true;
    } else {
        dialect = const_cast<char *>(CARBON_DIALECT_MYSQL);
        dialect_length = 5;
    }

    (void) dialect_length;

    context = carbon_context_new();
    if (context == NULL) {
        napi_throw_error(env, NULL, "CarbonC context allocation failed");
        goto cleanup;
    }

    request.dialect = dialect;
    request.schema_json = schema_json;
    request.schema_json_length = schema_json_length;
    request.query_json = query_json;
    request.query_json_length = query_json_length;

    status = carbon_compile_query(context, &request, &result);
    if (status == CARBON_STATUS_OUT_OF_MEMORY) {
        napi_throw_error(env, NULL, carbon_status_message(CARBON_STATUS_OUT_OF_MEMORY));
        goto cleanup;
    }
    diagnostic_status = carbon_compile_result_diagnostics_json(&result, &diagnostics, &diagnostic_error);
    if (diagnostic_status != CARBON_STATUS_OK) {
        napi_throw_error(
                env,
                NULL,
                diagnostic_error.data == NULL ? carbon_status_message(diagnostic_status) : diagnostic_error.data);
        goto cleanup;
    }

    if (napi_create_object(env, &object) != napi_ok) {
        napi_throw_error(env, NULL, "CarbonC failed to allocate result object");
        goto cleanup;
    }

    ok = carbon_node_set_int32(env, object, "status", static_cast<int32_t>(result.status))
            && carbon_node_set_cstring(env, object, "status_code", carbon_status_code(result.status))
            && carbon_node_set_string(env, object, "sql", &result.sql)
            && carbon_node_set_string(env, object, "params_json", &result.params_json)
            && carbon_node_set_string(env, object, "allowlist_key", &result.allowlist_key)
            && carbon_node_set_string(env, object, "error", &result.error)
            && carbon_node_set_string(env, object, "diagnostics_json", &diagnostics);

cleanup:
    carbon_buffer_free(&diagnostics);
    carbon_buffer_free(&diagnostic_error);
    carbon_compile_result_free(&result);
    carbon_context_free(context);
    free(query_json);
    if (owns_schema_json) {
        free(schema_json);
    }
    if (owns_dialect) {
        free(dialect);
    }

    return ok ? object : carbon_node_null(env);
}

static napi_value carbon_node_normalize_allowlist_sql(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    char *sql = NULL;
    size_t sql_length = 0;
    carbon_buffer out;
    carbon_buffer error;
    carbon_status status;
    napi_value value;

    carbon_buffer_init(&out);
    carbon_buffer_init(&error);

    if (napi_get_cb_info(env, info, &argc, args, NULL, NULL) != napi_ok) {
        napi_throw_error(env, NULL, "CarbonC failed to read arguments");
        goto fail;
    }

    if (argc < 1) {
        napi_throw_type_error(env, NULL, "sql must be a string");
        goto fail;
    }

    if (!carbon_node_get_string(env, args[0], "sql must be a string", &sql, &sql_length)) {
        goto fail;
    }

    (void) sql_length;

    status = carbon_normalize_allowlist_sql(sql, &out, &error);
    if (status != CARBON_STATUS_OK) {
        napi_throw_range_error(
                env,
                NULL,
                error.data == NULL ? carbon_status_message(status) : error.data);
        goto fail;
    }

    if (napi_create_string_utf8(env, out.data == NULL ? "" : out.data, out.length, &value) != napi_ok) {
        napi_throw_error(env, NULL, "CarbonC failed to allocate normalized SQL string");
        goto fail;
    }

    free(sql);
    carbon_buffer_free(&out);
    carbon_buffer_free(&error);
    return value;

fail:
    free(sql);
    carbon_buffer_free(&out);
    carbon_buffer_free(&error);
    return carbon_node_null(env);
}

static napi_value carbon_node_schema_metadata(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    char *schema_json = NULL;
    size_t schema_json_length = 0;
    bool owns_schema_json = false;
    carbon_buffer out;
    carbon_buffer error;
    carbon_status status;
    napi_value value;

    carbon_buffer_init(&out);
    carbon_buffer_init(&error);

    if (napi_get_cb_info(env, info, &argc, args, NULL, NULL) != napi_ok) {
        napi_throw_error(env, NULL, "CarbonC failed to read arguments");
        goto fail;
    }

    if (argc >= 1 && !carbon_node_is_undefined(env, args[0])) {
        if (!carbon_node_get_string(
                    env,
                    args[0],
                    "schema_json must be a string",
                    &schema_json,
                    &schema_json_length)) {
            goto fail;
        }
        owns_schema_json = true;
    } else {
        schema_json = const_cast<char *>("{}");
        schema_json_length = 2;
    }

    status = carbon_schema_metadata(schema_json, schema_json_length, &out, &error);
    if (status != CARBON_STATUS_OK) {
        napi_throw_range_error(
                env,
                NULL,
                error.data == NULL ? carbon_status_message(status) : error.data);
        goto fail;
    }

    if (napi_create_string_utf8(env, out.data == NULL ? "" : out.data, out.length, &value) != napi_ok) {
        napi_throw_error(env, NULL, "CarbonC failed to allocate schema metadata string");
        goto fail;
    }

    if (owns_schema_json) {
        free(schema_json);
    }
    carbon_buffer_free(&out);
    carbon_buffer_free(&error);
    return value;

fail:
    if (owns_schema_json) {
        free(schema_json);
    }
    carbon_buffer_free(&out);
    carbon_buffer_free(&error);
    return carbon_node_null(env);
}

static napi_value carbon_node_schema_from_dump(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    char *sql = NULL;
    size_t sql_length = 0;
    carbon_buffer out;
    carbon_buffer error;
    carbon_status status;
    napi_value value;

    carbon_buffer_init(&out);
    carbon_buffer_init(&error);

    if (napi_get_cb_info(env, info, &argc, args, NULL, NULL) != napi_ok) {
        napi_throw_error(env, NULL, "CarbonC failed to read arguments");
        goto fail;
    }

    if (argc < 1) {
        napi_throw_type_error(env, NULL, "sql must be a string");
        goto fail;
    }

    if (!carbon_node_get_string(env, args[0], "sql must be a string", &sql, &sql_length)) {
        goto fail;
    }

    status = carbon_schema_from_dump(sql, sql_length, &out, &error);
    if (status != CARBON_STATUS_OK) {
        napi_throw_range_error(
                env,
                NULL,
                error.data == NULL ? carbon_status_message(status) : error.data);
        goto fail;
    }

    if (napi_create_string_utf8(env, out.data == NULL ? "" : out.data, out.length, &value) != napi_ok) {
        napi_throw_error(env, NULL, "CarbonC failed to allocate schema string");
        goto fail;
    }

    free(sql);
    carbon_buffer_free(&out);
    carbon_buffer_free(&error);
    return value;

fail:
    free(sql);
    carbon_buffer_free(&out);
    carbon_buffer_free(&error);
    return carbon_node_null(env);
}

static napi_value carbon_node_schema_model_source(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3];
    char *schema_json = NULL;
    size_t schema_json_length = 0;
    char *language = NULL;
    size_t language_length = 0;
    char *options_json = NULL;
    size_t options_json_length = 0;
    carbon_buffer out;
    carbon_buffer error;
    carbon_status status;
    napi_value value;

    carbon_buffer_init(&out);
    carbon_buffer_init(&error);

    if (napi_get_cb_info(env, info, &argc, args, NULL, NULL) != napi_ok) {
        napi_throw_error(env, NULL, "CarbonC failed to read arguments");
        goto fail;
    }

    if (argc < 2) {
        napi_throw_type_error(env, NULL, "schema_json and language must be strings");
        goto fail;
    }

    if (!carbon_node_get_string(env, args[0], "schema_json must be a string", &schema_json, &schema_json_length)
        || !carbon_node_get_string(env, args[1], "language must be a string", &language, &language_length)) {
        goto fail;
    }
    (void) language_length;

    if (argc >= 3) {
        if (!carbon_node_get_string(env, args[2], "options_json must be a string", &options_json, &options_json_length)) {
            goto fail;
        }
    }

    status = carbon_schema_model_source(
            schema_json,
            schema_json_length,
            language,
            options_json == NULL ? "{}" : options_json,
            options_json == NULL ? 2 : options_json_length,
            &out,
            &error);
    if (status != CARBON_STATUS_OK) {
        napi_throw_range_error(
                env,
                NULL,
                error.data == NULL ? carbon_status_message(status) : error.data);
        goto fail;
    }

    if (napi_create_string_utf8(env, out.data == NULL ? "" : out.data, out.length, &value) != napi_ok) {
        napi_throw_error(env, NULL, "CarbonC failed to allocate model source string");
        goto fail;
    }

    free(schema_json);
    free(language);
    free(options_json);
    carbon_buffer_free(&out);
    carbon_buffer_free(&error);
    return value;

fail:
    free(schema_json);
    free(language);
    free(options_json);
    carbon_buffer_free(&out);
    carbon_buffer_free(&error);
    return carbon_node_null(env);
}

napi_value carbon_node_init(napi_env env, napi_value exports) {
    napi_property_descriptor descriptors[] = {
        {"version", NULL, carbon_node_version, NULL, NULL, NULL, napi_default, NULL},
        {"helloWorld", NULL, carbon_node_hello_world, NULL, NULL, NULL, napi_default, NULL},
        {"statusCode", NULL, carbon_node_status_code, NULL, NULL, NULL, napi_default, NULL},
        {"statusMessage", NULL, carbon_node_status_message, NULL, NULL, NULL, napi_default, NULL},
        {"compileQuery", NULL, carbon_node_compile_query, NULL, NULL, NULL, napi_default, NULL},
        {"normalizeAllowlistSql", NULL, carbon_node_normalize_allowlist_sql, NULL, NULL, NULL, napi_default, NULL},
        {"schemaMetadata", NULL, carbon_node_schema_metadata, NULL, NULL, NULL, napi_default, NULL},
        {"schemaFromDump", NULL, carbon_node_schema_from_dump, NULL, NULL, NULL, napi_default, NULL},
        {"schemaModelSource", NULL, carbon_node_schema_model_source, NULL, NULL, NULL, napi_default, NULL},
        {"hello_world", NULL, carbon_node_hello_world, NULL, NULL, NULL, napi_default, NULL},
        {"status_code", NULL, carbon_node_status_code, NULL, NULL, NULL, napi_default, NULL},
        {"status_message", NULL, carbon_node_status_message, NULL, NULL, NULL, napi_default, NULL},
        {"compile_query", NULL, carbon_node_compile_query, NULL, NULL, NULL, napi_default, NULL},
        {"normalize_allowlist_sql", NULL, carbon_node_normalize_allowlist_sql, NULL, NULL, NULL, napi_default, NULL},
        {"schema_metadata", NULL, carbon_node_schema_metadata, NULL, NULL, NULL, napi_default, NULL},
        {"schema_from_dump", NULL, carbon_node_schema_from_dump, NULL, NULL, NULL, napi_default, NULL},
        {"schema_model_source", NULL, carbon_node_schema_model_source, NULL, NULL, NULL, napi_default, NULL},
    };

    if (napi_define_properties(env, exports, sizeof(descriptors) / sizeof(descriptors[0]), descriptors) != napi_ok) {
        napi_throw_error(env, NULL, "CarbonC failed to define module exports");
        return carbon_node_null(env);
    }

    return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, carbon_node_init)
