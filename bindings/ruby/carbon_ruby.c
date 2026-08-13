#include "carbon_ruby.h"

#include "carbon.h"

static VALUE carbon_ruby_module;

static VALUE carbon_ruby_buffer_to_string(const carbon_buffer *buffer) {
    if (buffer == NULL || buffer->data == NULL) {
        return rb_str_new("", 0);
    }

    return rb_str_new(buffer->data, (long) buffer->length);
}

static void carbon_ruby_hash_set(VALUE hash, const char *key, VALUE value) {
    rb_hash_aset(hash, rb_str_new_cstr(key), value);
}

static VALUE carbon_ruby_version(VALUE self) {
    (void) self;
    return rb_str_new_cstr(carbon_version());
}

static VALUE carbon_ruby_hello_world(VALUE self) {
    (void) self;
    return rb_str_new_cstr(carbon_hello_world());
}

static VALUE carbon_ruby_status_message(VALUE self, VALUE status_value) {
    (void) self;
    return rb_str_new_cstr(carbon_status_message((carbon_status) NUM2INT(status_value)));
}

static VALUE carbon_ruby_status_code(VALUE self, VALUE status_value) {
    (void) self;
    return rb_str_new_cstr(carbon_status_code((carbon_status) NUM2INT(status_value)));
}

static VALUE carbon_ruby_compile_query(int argc, VALUE *argv, VALUE self) {
    VALUE query_json_value;
    VALUE schema_json_value;
    VALUE dialect_value;
    carbon_context *context;
    carbon_compile_request request;
    carbon_compile_result result;
    carbon_buffer diagnostics;
    carbon_buffer diagnostic_error;
    carbon_status status;
    carbon_status diagnostic_status;
    VALUE hash;

    (void) self;

    rb_scan_args(argc, argv, "12", &query_json_value, &schema_json_value, &dialect_value);

    Check_Type(query_json_value, T_STRING);
    if (NIL_P(schema_json_value)) {
        schema_json_value = rb_str_new_cstr("{}");
    } else {
        Check_Type(schema_json_value, T_STRING);
    }

    if (NIL_P(dialect_value)) {
        dialect_value = rb_str_new_cstr(CARBON_DIALECT_MYSQL);
    } else {
        Check_Type(dialect_value, T_STRING);
    }

    context = carbon_context_new();
    if (context == NULL) {
        rb_memerror();
    }

    request.dialect = RSTRING_PTR(dialect_value);
    request.schema_json = RSTRING_PTR(schema_json_value);
    request.schema_json_length = (size_t) RSTRING_LEN(schema_json_value);
    request.query_json = RSTRING_PTR(query_json_value);
    request.query_json_length = (size_t) RSTRING_LEN(query_json_value);

    carbon_compile_result_init(&result);
    status = carbon_compile_query(context, &request, &result);
    if (status == CARBON_STATUS_OUT_OF_MEMORY) {
        carbon_compile_result_free(&result);
        carbon_context_free(context);
        rb_memerror();
    }
    diagnostic_status = carbon_compile_result_diagnostics_json(&result, &diagnostics, &diagnostic_error);
    if (diagnostic_status != CARBON_STATUS_OK) {
        VALUE exception = rb_exc_new2(
                diagnostic_status == CARBON_STATUS_OUT_OF_MEMORY ? rb_eNoMemError : rb_eArgError,
                diagnostic_error.data == NULL ? carbon_status_message(diagnostic_status) : diagnostic_error.data);
        carbon_buffer_free(&diagnostics);
        carbon_buffer_free(&diagnostic_error);
        carbon_compile_result_free(&result);
        carbon_context_free(context);
        rb_exc_raise(exception);
    }

    hash = rb_hash_new();
    carbon_ruby_hash_set(hash, "status", INT2NUM(result.status));
    carbon_ruby_hash_set(hash, "status_code", rb_str_new_cstr(carbon_status_code(result.status)));
    carbon_ruby_hash_set(hash, "sql", carbon_ruby_buffer_to_string(&result.sql));
    carbon_ruby_hash_set(hash, "params_json", carbon_ruby_buffer_to_string(&result.params_json));
    carbon_ruby_hash_set(hash, "allowlist_key", carbon_ruby_buffer_to_string(&result.allowlist_key));
    carbon_ruby_hash_set(hash, "error", carbon_ruby_buffer_to_string(&result.error));
    carbon_ruby_hash_set(hash, "diagnostics_json", carbon_ruby_buffer_to_string(&diagnostics));

    carbon_buffer_free(&diagnostics);
    carbon_buffer_free(&diagnostic_error);
    carbon_compile_result_free(&result);
    carbon_context_free(context);

    return hash;
}

static VALUE carbon_ruby_normalize_allowlist_sql(VALUE self, VALUE sql_value) {
    carbon_buffer out;
    carbon_buffer error;
    carbon_status status;
    VALUE normalized;

    (void) self;

    Check_Type(sql_value, T_STRING);

    carbon_buffer_init(&out);
    carbon_buffer_init(&error);
    status = carbon_normalize_allowlist_sql(RSTRING_PTR(sql_value), &out, &error);
    if (status != CARBON_STATUS_OK) {
        VALUE exception = rb_exc_new2(
                rb_eArgError,
                error.data == NULL ? carbon_status_message(status) : error.data);
        carbon_buffer_free(&out);
        carbon_buffer_free(&error);
        rb_exc_raise(exception);
    }

    normalized = carbon_ruby_buffer_to_string(&out);
    carbon_buffer_free(&out);
    carbon_buffer_free(&error);
    return normalized;
}

static VALUE carbon_ruby_schema_metadata(int argc, VALUE *argv, VALUE self) {
    VALUE schema_json_value;
    carbon_buffer out;
    carbon_buffer error;
    carbon_status status;
    VALUE metadata;

    (void) self;

    rb_scan_args(argc, argv, "01", &schema_json_value);
    if (NIL_P(schema_json_value)) {
        schema_json_value = rb_str_new_cstr("{}");
    } else {
        Check_Type(schema_json_value, T_STRING);
    }

    carbon_buffer_init(&out);
    carbon_buffer_init(&error);
    status = carbon_schema_metadata(
            RSTRING_PTR(schema_json_value),
            (size_t) RSTRING_LEN(schema_json_value),
            &out,
            &error);
    if (status != CARBON_STATUS_OK) {
        VALUE exception = rb_exc_new2(
                rb_eArgError,
                error.data == NULL ? carbon_status_message(status) : error.data);
        carbon_buffer_free(&out);
        carbon_buffer_free(&error);
        rb_exc_raise(exception);
    }

    metadata = carbon_ruby_buffer_to_string(&out);
    carbon_buffer_free(&out);
    carbon_buffer_free(&error);
    return metadata;
}

static VALUE carbon_ruby_schema_from_dump(VALUE self, VALUE sql_value) {
    carbon_buffer out;
    carbon_buffer error;
    carbon_status status;
    VALUE schema;

    (void) self;

    Check_Type(sql_value, T_STRING);

    carbon_buffer_init(&out);
    carbon_buffer_init(&error);
    status = carbon_schema_from_dump(
            RSTRING_PTR(sql_value),
            (size_t) RSTRING_LEN(sql_value),
            &out,
            &error);
    if (status != CARBON_STATUS_OK) {
        VALUE exception = rb_exc_new2(
                rb_eArgError,
                error.data == NULL ? carbon_status_message(status) : error.data);
        carbon_buffer_free(&out);
        carbon_buffer_free(&error);
        rb_exc_raise(exception);
    }

    schema = carbon_ruby_buffer_to_string(&out);
    carbon_buffer_free(&out);
    carbon_buffer_free(&error);
    return schema;
}

void Init_carbon(void) {
    carbon_ruby_module = rb_define_module("CarbonC");
    rb_define_singleton_method(carbon_ruby_module, "version", carbon_ruby_version, 0);
    rb_define_singleton_method(carbon_ruby_module, "hello_world", carbon_ruby_hello_world, 0);
    rb_define_singleton_method(carbon_ruby_module, "status_code", carbon_ruby_status_code, 1);
    rb_define_singleton_method(carbon_ruby_module, "status_message", carbon_ruby_status_message, 1);
    rb_define_singleton_method(carbon_ruby_module, "compile_query", carbon_ruby_compile_query, -1);
    rb_define_singleton_method(
            carbon_ruby_module,
            "normalize_allowlist_sql",
            carbon_ruby_normalize_allowlist_sql,
            1);
    rb_define_singleton_method(carbon_ruby_module, "schema_metadata", carbon_ruby_schema_metadata, -1);
    rb_define_singleton_method(carbon_ruby_module, "schema_from_dump", carbon_ruby_schema_from_dump, 1);
}
