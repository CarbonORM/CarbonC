#include "carbon_php.h"

static void carbon_add_assoc_buffer(zval *array, const char *key, const carbon_buffer *buffer) {
        if (buffer == NULL || buffer->data == NULL) {
                add_assoc_stringl(array, key, "", 0);
                return;
        }

        add_assoc_stringl(array, key, buffer->data, buffer->length);
}

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_carbon_version, 0, 0, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_carbon_hello_world, 0, 0, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_carbon_status_message, 0, 1, IS_STRING, 0)
        ZEND_ARG_TYPE_INFO(0, status, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_carbon_compile_query, 0, 1, IS_ARRAY, 0)
        ZEND_ARG_TYPE_INFO(0, query_json, IS_STRING, 0)
        ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, schema_json, IS_STRING, 0, "\"{}\"")
        ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, dialect, IS_STRING, 0, "\"mysql\"")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_carbon_normalize_allowlist_sql, 0, 1, IS_STRING, 0)
        ZEND_ARG_TYPE_INFO(0, sql, IS_STRING, 0)
ZEND_END_ARG_INFO()

PHP_FUNCTION(carbon_version) {
        const char *version = carbon_version();
        RETURN_STRING(version);
}

PHP_FUNCTION(carbon_hello_world) {
        RETURN_STRING(carbon_hello_world());
}

PHP_FUNCTION(carbon_status_message) {
        zend_long status;

        ZEND_PARSE_PARAMETERS_START(1, 1)
                Z_PARAM_LONG(status)
        ZEND_PARSE_PARAMETERS_END();

        RETURN_STRING(carbon_status_message((carbon_status) status));
}

PHP_FUNCTION(carbon_compile_query) {
        char *query_json;
        char *schema_json = "{}";
        char *dialect = "mysql";
        size_t query_json_length;
        size_t schema_json_length = 2;
        size_t dialect_length = 5;
        carbon_context *context;
        carbon_compile_request request;
        carbon_compile_result result;
        carbon_status status;

        ZEND_PARSE_PARAMETERS_START(1, 3)
                Z_PARAM_STRING(query_json, query_json_length)
                Z_PARAM_OPTIONAL
                Z_PARAM_STRING(schema_json, schema_json_length)
                Z_PARAM_STRING(dialect, dialect_length)
        ZEND_PARSE_PARAMETERS_END();

        context = carbon_context_new();
        if (context == NULL) {
                zend_throw_error(NULL, "CarbonC context allocation failed");
                RETURN_THROWS();
        }

        request.dialect = dialect;
        request.schema_json = schema_json;
        request.schema_json_length = schema_json_length;
        request.query_json = query_json;
        request.query_json_length = query_json_length;

        carbon_compile_result_init(&result);
        status = carbon_compile_query(context, &request, &result);
        if (status == CARBON_STATUS_OUT_OF_MEMORY) {
                carbon_compile_result_free(&result);
                carbon_context_free(context);
                zend_throw_error(NULL, "%s", carbon_status_message(CARBON_STATUS_OUT_OF_MEMORY));
                RETURN_THROWS();
        }

        array_init(return_value);
        add_assoc_long(return_value, "status", result.status);
        carbon_add_assoc_buffer(return_value, "sql", &result.sql);
        carbon_add_assoc_buffer(return_value, "params_json", &result.params_json);
        carbon_add_assoc_buffer(return_value, "allowlist_key", &result.allowlist_key);
        carbon_add_assoc_buffer(return_value, "error", &result.error);

        carbon_compile_result_free(&result);
        carbon_context_free(context);
}

PHP_FUNCTION(carbon_normalize_allowlist_sql) {
        char *sql;
        size_t sql_length;
        carbon_buffer out;
        carbon_buffer error;
        carbon_status status;

        ZEND_PARSE_PARAMETERS_START(1, 1)
                Z_PARAM_STRING(sql, sql_length)
        ZEND_PARSE_PARAMETERS_END();

        status = carbon_normalize_allowlist_sql(sql, &out, &error);
        if (status != CARBON_STATUS_OK) {
                zend_value_error("%s", error.data == NULL ? carbon_status_message(status) : error.data);
                carbon_buffer_free(&out);
                carbon_buffer_free(&error);
                RETURN_THROWS();
        }

        RETVAL_STRINGL(out.data == NULL ? "" : out.data, out.length);
        carbon_buffer_free(&out);
        carbon_buffer_free(&error);
}

const zend_function_entry carbon_functions[] = {
        PHP_FE(carbon_version, arginfo_carbon_version)
        PHP_FE(carbon_hello_world, arginfo_carbon_hello_world)
        PHP_FE(carbon_status_message, arginfo_carbon_status_message)
        PHP_FE(carbon_compile_query, arginfo_carbon_compile_query)
        PHP_FE(carbon_normalize_allowlist_sql, arginfo_carbon_normalize_allowlist_sql)
        PHP_FE_END
};

zend_module_entry carbon_module_entry = {
        STANDARD_MODULE_HEADER,
        "carbon",
        carbon_functions,
        NULL, // Module init
        NULL, // Module shutdown
        NULL, // Request init
        NULL, // Request shutdown
        NULL, // Module info
        CARBON_VERSION,
        STANDARD_MODULE_PROPERTIES
};

ZEND_GET_MODULE(carbon)
