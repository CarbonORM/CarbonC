#include "carbon.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct carbon_context {
    unsigned int abi_version;
};

typedef struct carbon_string_builder {
    char *data;
    size_t length;
    size_t capacity;
} carbon_string_builder;

typedef struct carbon_json_array {
    char **items;
    size_t length;
} carbon_json_array;

typedef struct carbon_where_clause {
    char *column;
    char *raw_value;
} carbon_where_clause;

typedef enum carbon_dialect {
    CARBON_DIALECT_MYSQL,
    CARBON_DIALECT_POSTGRESQL
} carbon_dialect;

static size_t carbon_strlen(const char *value) {
    return value == NULL ? 0 : strlen(value);
}

static void *carbon_alloc(size_t size) {
    return calloc(1, size == 0 ? 1 : size);
}

static char *carbon_strndup_local(const char *value, size_t length) {
    char *copy = (char *) malloc(length + 1);
    if (copy == NULL) {
        return NULL;
    }
    if (length > 0) {
        memcpy(copy, value, length);
    }
    copy[length] = '\0';
    return copy;
}

static void carbon_builder_free(carbon_string_builder *builder) {
    if (builder == NULL) {
        return;
    }
    free(builder->data);
    builder->data = NULL;
    builder->length = 0;
    builder->capacity = 0;
}

static int carbon_builder_reserve(carbon_string_builder *builder, size_t extra) {
    size_t needed;
    size_t capacity;
    char *next;

    if (builder == NULL) {
        return 0;
    }

    needed = builder->length + extra + 1;
    if (needed <= builder->capacity) {
        return 1;
    }

    capacity = builder->capacity == 0 ? 64 : builder->capacity;
    while (capacity < needed) {
        capacity *= 2;
    }

    next = (char *) realloc(builder->data, capacity);
    if (next == NULL) {
        return 0;
    }

    builder->data = next;
    builder->capacity = capacity;
    return 1;
}

static int carbon_builder_append_len(carbon_string_builder *builder, const char *value, size_t length) {
    if (!carbon_builder_reserve(builder, length)) {
        return 0;
    }
    if (length > 0) {
        memcpy(builder->data + builder->length, value, length);
    }
    builder->length += length;
    builder->data[builder->length] = '\0';
    return 1;
}

static int carbon_builder_append(carbon_string_builder *builder, const char *value) {
    return carbon_builder_append_len(builder, value, carbon_strlen(value));
}

static int carbon_builder_append_char(carbon_string_builder *builder, char value) {
    return carbon_builder_append_len(builder, &value, 1);
}

static int carbon_buffer_set_len(carbon_buffer *buffer, const char *value, size_t length) {
    char *copy;

    if (buffer == NULL) {
        return 0;
    }

    carbon_buffer_free(buffer);
    copy = carbon_strndup_local(value == NULL ? "" : value, value == NULL ? 0 : length);
    if (copy == NULL) {
        return 0;
    }

    buffer->data = copy;
    buffer->length = value == NULL ? 0 : length;
    return 1;
}

static int carbon_buffer_set(carbon_buffer *buffer, const char *value) {
    return carbon_buffer_set_len(buffer, value, carbon_strlen(value));
}

static int carbon_buffer_take_builder(carbon_buffer *buffer, carbon_string_builder *builder) {
    if (buffer == NULL || builder == NULL) {
        return 0;
    }

    carbon_buffer_free(buffer);
    if (builder->data == NULL && !carbon_builder_append(builder, "")) {
        return 0;
    }

    buffer->data = builder->data;
    buffer->length = builder->length;
    builder->data = NULL;
    builder->length = 0;
    builder->capacity = 0;
    return 1;
}

static void carbon_json_array_free(carbon_json_array *array) {
    size_t index;

    if (array == NULL) {
        return;
    }

    for (index = 0; index < array->length; ++index) {
        free(array->items[index]);
    }
    free(array->items);
    array->items = NULL;
    array->length = 0;
}

static void carbon_where_clause_free(carbon_where_clause *where) {
    if (where == NULL) {
        return;
    }
    free(where->column);
    free(where->raw_value);
    where->column = NULL;
    where->raw_value = NULL;
}

static const char *carbon_skip_ws(const char *cursor, const char *end) {
    while (cursor < end && isspace((unsigned char) *cursor)) {
        ++cursor;
    }
    return cursor;
}

static int carbon_parse_json_string_bounds(
        const char *cursor,
        const char *end,
        const char **content_start,
        const char **content_end,
        const char **next) {
    int escaped = 0;
    const char *start;

    if (cursor >= end || *cursor != '"') {
        return 0;
    }

    start = cursor + 1;
    ++cursor;

    while (cursor < end) {
        if (escaped) {
            escaped = 0;
            ++cursor;
            continue;
        }
        if (*cursor == '\\') {
            escaped = 1;
            ++cursor;
            continue;
        }
        if (*cursor == '"') {
            if (content_start != NULL) {
                *content_start = start;
            }
            if (content_end != NULL) {
                *content_end = cursor;
            }
            if (next != NULL) {
                *next = cursor + 1;
            }
            return 1;
        }
        ++cursor;
    }

    return 0;
}

static int carbon_string_has_escape(const char *start, const char *end) {
    while (start < end) {
        if (*start == '\\') {
            return 1;
        }
        ++start;
    }
    return 0;
}

static char *carbon_copy_json_identifier_string(const char *start, const char *end) {
    if (carbon_string_has_escape(start, end)) {
        return NULL;
    }
    return carbon_strndup_local(start, (size_t) (end - start));
}

static const char *carbon_json_value_end(const char *cursor, const char *end) {
    const char *next;

    cursor = carbon_skip_ws(cursor, end);
    if (cursor >= end) {
        return NULL;
    }

    if (*cursor == '"') {
        return carbon_parse_json_string_bounds(cursor, end, NULL, NULL, &next) ? next : NULL;
    }

    if (*cursor == '{' || *cursor == '[') {
        char open = *cursor;
        char close = open == '{' ? '}' : ']';
        int depth = 0;
        int in_string = 0;
        int escaped = 0;

        while (cursor < end) {
            if (in_string) {
                if (escaped) {
                    escaped = 0;
                } else if (*cursor == '\\') {
                    escaped = 1;
                } else if (*cursor == '"') {
                    in_string = 0;
                }
                ++cursor;
                continue;
            }

            if (*cursor == '"') {
                in_string = 1;
                ++cursor;
                continue;
            }
            if (*cursor == open) {
                ++depth;
            } else if (*cursor == close) {
                --depth;
                if (depth == 0) {
                    return cursor + 1;
                }
            }
            ++cursor;
        }

        return NULL;
    }

    if (*cursor == '-' || isdigit((unsigned char) *cursor)) {
        ++cursor;
        while (cursor < end && (isdigit((unsigned char) *cursor)
                               || *cursor == '.'
                               || *cursor == 'e'
                               || *cursor == 'E'
                               || *cursor == '+'
                               || *cursor == '-')) {
            ++cursor;
        }
        return cursor;
    }

    if ((size_t) (end - cursor) >= 4 && strncmp(cursor, "true", 4) == 0) {
        return cursor + 4;
    }
    if ((size_t) (end - cursor) >= 5 && strncmp(cursor, "false", 5) == 0) {
        return cursor + 5;
    }
    if ((size_t) (end - cursor) >= 4 && strncmp(cursor, "null", 4) == 0) {
        return cursor + 4;
    }

    return NULL;
}

static int carbon_key_equals(const char *start, const char *end, const char *name) {
    size_t length = (size_t) (end - start);
    return strlen(name) == length && strncmp(start, name, length) == 0;
}

static const char *carbon_find_top_level_property(
        const char *json,
        size_t length,
        const char *name,
        const char **value_end) {
    const char *cursor = json;
    const char *end = json + length;

    cursor = carbon_skip_ws(cursor, end);
    if (cursor >= end || *cursor != '{') {
        return NULL;
    }

    ++cursor;
    while (cursor < end) {
        const char *key_start;
        const char *key_end;
        const char *value_start;
        const char *after_value;
        const char *next;

        cursor = carbon_skip_ws(cursor, end);
        if (cursor < end && *cursor == '}') {
            return NULL;
        }

        if (!carbon_parse_json_string_bounds(cursor, end, &key_start, &key_end, &next)
            || carbon_string_has_escape(key_start, key_end)) {
            return NULL;
        }

        cursor = carbon_skip_ws(next, end);
        if (cursor >= end || *cursor != ':') {
            return NULL;
        }
        value_start = carbon_skip_ws(cursor + 1, end);
        after_value = carbon_json_value_end(value_start, end);
        if (after_value == NULL) {
            return NULL;
        }

        if (carbon_key_equals(key_start, key_end, name)) {
            if (value_end != NULL) {
                *value_end = after_value;
            }
            return value_start;
        }

        cursor = carbon_skip_ws(after_value, end);
        if (cursor < end && *cursor == ',') {
            ++cursor;
            continue;
        }
        if (cursor < end && *cursor == '}') {
            return NULL;
        }
        return NULL;
    }

    return NULL;
}

static carbon_status carbon_parse_string_property(
        const char *json,
        size_t length,
        const char *name,
        int required,
        char **out) {
    const char *value_start;
    const char *value_end;
    const char *content_start;
    const char *content_end;

    *out = NULL;
    value_start = carbon_find_top_level_property(json, length, name, &value_end);
    if (value_start == NULL) {
        return required ? CARBON_STATUS_INVALID_QUERY : CARBON_STATUS_OK;
    }

    if (!carbon_parse_json_string_bounds(value_start, value_end, &content_start, &content_end, NULL)) {
        return CARBON_STATUS_INVALID_QUERY;
    }

    *out = carbon_copy_json_identifier_string(content_start, content_end);
    return *out == NULL ? CARBON_STATUS_INVALID_QUERY : CARBON_STATUS_OK;
}

static carbon_status carbon_parse_select_array(
        const char *json,
        size_t length,
        carbon_json_array *out) {
    const char *cursor;
    const char *end_value;
    const char *end;

    out->items = NULL;
    out->length = 0;

    cursor = carbon_find_top_level_property(json, length, "select", &end_value);
    if (cursor == NULL) {
        return CARBON_STATUS_OK;
    }

    cursor = carbon_skip_ws(cursor, end_value);
    if (cursor >= end_value || *cursor != '[') {
        return CARBON_STATUS_INVALID_QUERY;
    }

    ++cursor;
    end = end_value;
    while (cursor < end) {
        const char *content_start;
        const char *content_end;
        const char *next;
        char **items;
        char *item;

        cursor = carbon_skip_ws(cursor, end);
        if (cursor < end && *cursor == ']') {
            return CARBON_STATUS_OK;
        }

        if (!carbon_parse_json_string_bounds(cursor, end, &content_start, &content_end, &next)) {
            return CARBON_STATUS_INVALID_QUERY;
        }

        item = carbon_copy_json_identifier_string(content_start, content_end);
        if (item == NULL) {
            return CARBON_STATUS_INVALID_QUERY;
        }

        items = (char **) realloc(out->items, sizeof(char *) * (out->length + 1));
        if (items == NULL) {
            free(item);
            return CARBON_STATUS_OUT_OF_MEMORY;
        }

        out->items = items;
        out->items[out->length++] = item;

        cursor = carbon_skip_ws(next, end);
        if (cursor < end && *cursor == ',') {
            ++cursor;
            continue;
        }
        if (cursor < end && *cursor == ']') {
            return CARBON_STATUS_OK;
        }
        return CARBON_STATUS_INVALID_QUERY;
    }

    return CARBON_STATUS_INVALID_QUERY;
}

static carbon_status carbon_parse_where_object(
        const char *json,
        size_t length,
        carbon_where_clause *out,
        int *has_where) {
    const char *cursor;
    const char *end_value;
    const char *key_start;
    const char *key_end;
    const char *value_start;
    const char *after_value;
    const char *next;

    out->column = NULL;
    out->raw_value = NULL;
    *has_where = 0;

    cursor = carbon_find_top_level_property(json, length, "where", &end_value);
    if (cursor == NULL) {
        return CARBON_STATUS_OK;
    }

    cursor = carbon_skip_ws(cursor, end_value);
    if (cursor >= end_value || *cursor != '{') {
        return CARBON_STATUS_INVALID_QUERY;
    }

    cursor = carbon_skip_ws(cursor + 1, end_value);
    if (cursor < end_value && *cursor == '}') {
        return CARBON_STATUS_OK;
    }

    if (!carbon_parse_json_string_bounds(cursor, end_value, &key_start, &key_end, &next)
        || carbon_string_has_escape(key_start, key_end)) {
        return CARBON_STATUS_INVALID_QUERY;
    }

    cursor = carbon_skip_ws(next, end_value);
    if (cursor >= end_value || *cursor != ':') {
        return CARBON_STATUS_INVALID_QUERY;
    }

    value_start = carbon_skip_ws(cursor + 1, end_value);
    after_value = carbon_json_value_end(value_start, end_value);
    if (after_value == NULL) {
        return CARBON_STATUS_INVALID_QUERY;
    }

    cursor = carbon_skip_ws(after_value, end_value);
    if (cursor >= end_value || *cursor != '}') {
        return CARBON_STATUS_UNSUPPORTED_QUERY;
    }

    out->column = carbon_copy_json_identifier_string(key_start, key_end);
    out->raw_value = carbon_strndup_local(value_start, (size_t) (after_value - value_start));
    if (out->column == NULL || out->raw_value == NULL) {
        carbon_where_clause_free(out);
        return CARBON_STATUS_OUT_OF_MEMORY;
    }

    *has_where = 1;
    return CARBON_STATUS_OK;
}

static carbon_status carbon_parse_limit(
        const char *json,
        size_t length,
        char **raw_limit,
        int *has_limit) {
    const char *cursor;
    const char *end_value;
    const char *start;

    *raw_limit = NULL;
    *has_limit = 0;

    cursor = carbon_find_top_level_property(json, length, "limit", &end_value);
    if (cursor == NULL) {
        return CARBON_STATUS_OK;
    }

    cursor = carbon_skip_ws(cursor, end_value);
    if (cursor >= end_value || !isdigit((unsigned char) *cursor)) {
        return CARBON_STATUS_INVALID_QUERY;
    }

    start = cursor;
    while (cursor < end_value) {
        if (!isdigit((unsigned char) *cursor)) {
            return CARBON_STATUS_INVALID_QUERY;
        }
        ++cursor;
    }

    *raw_limit = carbon_strndup_local(start, (size_t) (end_value - start));
    if (*raw_limit == NULL) {
        return CARBON_STATUS_OUT_OF_MEMORY;
    }
    *has_limit = 1;
    return CARBON_STATUS_OK;
}

static int carbon_identifier_segment_valid(const char *start, size_t length) {
    size_t index;

    if (length == 0 || !(isalpha((unsigned char) start[0]) || start[0] == '_')) {
        return 0;
    }

    for (index = 1; index < length; ++index) {
        if (!(isalnum((unsigned char) start[index]) || start[index] == '_')) {
            return 0;
        }
    }

    return 1;
}

static int carbon_identifier_valid(const char *identifier) {
    const char *segment = identifier;
    const char *cursor = identifier;

    if (identifier == NULL || *identifier == '\0') {
        return 0;
    }

    if (strcmp(identifier, "*") == 0) {
        return 1;
    }

    while (1) {
        if (*cursor == '.' || *cursor == '\0') {
            if (!carbon_identifier_segment_valid(segment, (size_t) (cursor - segment))) {
                return 0;
            }
            if (*cursor == '\0') {
                return 1;
            }
            segment = cursor + 1;
        }
        ++cursor;
    }
}

static int carbon_append_quoted_identifier(
        carbon_string_builder *builder,
        carbon_dialect dialect,
        const char *identifier) {
    char quote = dialect == CARBON_DIALECT_MYSQL ? '`' : '"';
    const char *segment = identifier;
    const char *cursor = identifier;

    if (strcmp(identifier, "*") == 0) {
        return carbon_builder_append_char(builder, '*');
    }

    while (1) {
        if (*cursor == '.' || *cursor == '\0') {
            if (!carbon_builder_append_char(builder, quote)
                || !carbon_builder_append_len(builder, segment, (size_t) (cursor - segment))
                || !carbon_builder_append_char(builder, quote)) {
                return 0;
            }
            if (*cursor == '\0') {
                return 1;
            }
            if (!carbon_builder_append_char(builder, '.')) {
                return 0;
            }
            segment = cursor + 1;
        }
        ++cursor;
    }
}

static int carbon_append_param(carbon_string_builder *params, const char *raw_value, int *param_count) {
    if (*param_count > 0 && !carbon_builder_append(params, ",")) {
        return 0;
    }
    if (!carbon_builder_append(params, raw_value)) {
        return 0;
    }
    ++(*param_count);
    return 1;
}

static carbon_status carbon_parse_dialect(const char *value, carbon_dialect *dialect) {
    if (value == NULL || strcmp(value, "mysql") == 0) {
        *dialect = CARBON_DIALECT_MYSQL;
        return CARBON_STATUS_OK;
    }

    if (strcmp(value, "postgresql") == 0 || strcmp(value, "postgres") == 0) {
        *dialect = CARBON_DIALECT_POSTGRESQL;
        return CARBON_STATUS_OK;
    }

    return CARBON_STATUS_UNSUPPORTED_DIALECT;
}

static carbon_status carbon_set_result_error(
        carbon_compile_result *result,
        carbon_status status,
        const char *format,
        ...) {
    va_list args;
    va_list count_args;
    int count;
    char *message;

    if (result == NULL) {
        return status;
    }

    result->status = status;

    va_start(args, format);
    va_copy(count_args, args);
    count = vsnprintf(NULL, 0, format, count_args);
    va_end(count_args);
    if (count < 0) {
        va_end(args);
        carbon_buffer_set(&result->error, carbon_status_message(status));
        return status;
    }

    message = (char *) malloc((size_t) count + 1);
    if (message == NULL) {
        va_end(args);
        result->status = CARBON_STATUS_OUT_OF_MEMORY;
        carbon_buffer_set(&result->error, carbon_status_message(CARBON_STATUS_OUT_OF_MEMORY));
        return CARBON_STATUS_OUT_OF_MEMORY;
    }

    vsnprintf(message, (size_t) count + 1, format, args);
    va_end(args);
    carbon_buffer_set_len(&result->error, message, (size_t) count);
    free(message);
    return status;
}

const char *carbon_version(void) {
    return CARBON_VERSION;
}

const char *carbon_hello_world(void) {
    return "CarbonC portable kernel";
}

const char *carbon_status_message(carbon_status status) {
    switch (status) {
        case CARBON_STATUS_OK:
            return "ok";
        case CARBON_STATUS_INVALID_ARGUMENT:
            return "invalid argument";
        case CARBON_STATUS_INVALID_JSON:
            return "invalid json";
        case CARBON_STATUS_INVALID_QUERY:
            return "invalid query";
        case CARBON_STATUS_UNSUPPORTED_DIALECT:
            return "unsupported dialect";
        case CARBON_STATUS_UNSUPPORTED_QUERY:
            return "unsupported query";
        case CARBON_STATUS_OUT_OF_MEMORY:
            return "out of memory";
        default:
            return "unknown status";
    }
}

carbon_context *carbon_context_new(void) {
    carbon_context *context = (carbon_context *) carbon_alloc(sizeof(carbon_context));
    if (context != NULL) {
        context->abi_version = 1;
    }
    return context;
}

void carbon_context_free(carbon_context *context) {
    free(context);
}

void carbon_buffer_init(carbon_buffer *buffer) {
    if (buffer == NULL) {
        return;
    }
    buffer->data = NULL;
    buffer->length = 0;
}

void carbon_buffer_free(carbon_buffer *buffer) {
    if (buffer == NULL) {
        return;
    }
    free(buffer->data);
    buffer->data = NULL;
    buffer->length = 0;
}

void carbon_compile_result_init(carbon_compile_result *result) {
    if (result == NULL) {
        return;
    }
    result->status = CARBON_STATUS_OK;
    carbon_buffer_init(&result->sql);
    carbon_buffer_init(&result->params_json);
    carbon_buffer_init(&result->allowlist_key);
    carbon_buffer_init(&result->error);
}

void carbon_compile_result_free(carbon_compile_result *result) {
    if (result == NULL) {
        return;
    }
    carbon_buffer_free(&result->sql);
    carbon_buffer_free(&result->params_json);
    carbon_buffer_free(&result->allowlist_key);
    carbon_buffer_free(&result->error);
    result->status = CARBON_STATUS_OK;
}

carbon_status carbon_normalize_allowlist_sql(
        const char *sql,
        carbon_buffer *out,
        carbon_buffer *error) {
    carbon_string_builder builder = {0};
    int wrote_space = 0;

    if (out == NULL || sql == NULL) {
        if (error != NULL) {
            carbon_buffer_init(error);
            carbon_buffer_set(error, "sql and output buffer are required");
        }
        return CARBON_STATUS_INVALID_ARGUMENT;
    }

    carbon_buffer_init(out);
    if (error != NULL) {
        carbon_buffer_init(error);
    }

    while (*sql != '\0') {
        unsigned char ch = (unsigned char) *sql;

        if (ch == '`' || ch == '"') {
            ++sql;
            continue;
        }

        if (isspace(ch)) {
            if (builder.length > 0) {
                wrote_space = 1;
            }
            ++sql;
            continue;
        }

        if (wrote_space) {
            if (!carbon_builder_append_char(&builder, ' ')) {
                carbon_builder_free(&builder);
                if (error != NULL) {
                    carbon_buffer_set(error, carbon_status_message(CARBON_STATUS_OUT_OF_MEMORY));
                }
                return CARBON_STATUS_OUT_OF_MEMORY;
            }
            wrote_space = 0;
        }

        ch = (unsigned char) tolower(ch);
        if (!carbon_builder_append_char(&builder, (char) ch)) {
            carbon_builder_free(&builder);
            if (error != NULL) {
                carbon_buffer_set(error, carbon_status_message(CARBON_STATUS_OUT_OF_MEMORY));
            }
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        ++sql;
    }

    if (!carbon_buffer_take_builder(out, &builder)) {
        carbon_builder_free(&builder);
        if (error != NULL) {
            carbon_buffer_set(error, carbon_status_message(CARBON_STATUS_OUT_OF_MEMORY));
        }
        return CARBON_STATUS_OUT_OF_MEMORY;
    }

    if (error != NULL) {
        carbon_buffer_set(error, "");
    }
    return CARBON_STATUS_OK;
}

carbon_status carbon_compile_query(
        carbon_context *context,
        const carbon_compile_request *request,
        carbon_compile_result *result) {
    carbon_status status;
    carbon_dialect dialect;
    char *dialect_string = NULL;
    char *table = NULL;
    char *limit = NULL;
    carbon_json_array select = {0};
    carbon_where_clause where = {0};
    int has_where = 0;
    int has_limit = 0;
    int param_count = 0;
    carbon_string_builder sql = {0};
    carbon_string_builder params = {0};
    size_t index;

    if (result == NULL) {
        return CARBON_STATUS_INVALID_ARGUMENT;
    }

    carbon_compile_result_init(result);

    if (context == NULL || request == NULL || request->query_json == NULL) {
        return carbon_set_result_error(result, CARBON_STATUS_INVALID_ARGUMENT,
                                       "context, request, and query_json are required");
    }

    if (request->query_json_length == 0) {
        return carbon_set_result_error(result, CARBON_STATUS_INVALID_QUERY,
                                       "query_json_length must be greater than zero");
    }

    status = carbon_parse_string_property(
            request->query_json,
            request->query_json_length,
            "dialect",
            0,
            &dialect_string);
    if (status != CARBON_STATUS_OK) {
        goto fail;
    }

    status = carbon_parse_dialect(dialect_string != NULL ? dialect_string : request->dialect, &dialect);
    if (status != CARBON_STATUS_OK) {
        goto fail;
    }

    status = carbon_parse_string_property(
            request->query_json,
            request->query_json_length,
            "table",
            1,
            &table);
    if (status != CARBON_STATUS_OK) {
        goto fail;
    }
    if (!carbon_identifier_valid(table) || strcmp(table, "*") == 0) {
        status = CARBON_STATUS_INVALID_QUERY;
        goto fail;
    }

    status = carbon_parse_select_array(request->query_json, request->query_json_length, &select);
    if (status != CARBON_STATUS_OK) {
        goto fail;
    }

    for (index = 0; index < select.length; ++index) {
        if (!carbon_identifier_valid(select.items[index])) {
            status = CARBON_STATUS_INVALID_QUERY;
            goto fail;
        }
    }

    status = carbon_parse_where_object(request->query_json, request->query_json_length, &where, &has_where);
    if (status != CARBON_STATUS_OK) {
        goto fail;
    }
    if (has_where && !carbon_identifier_valid(where.column)) {
        status = CARBON_STATUS_INVALID_QUERY;
        goto fail;
    }

    status = carbon_parse_limit(request->query_json, request->query_json_length, &limit, &has_limit);
    if (status != CARBON_STATUS_OK) {
        goto fail;
    }

    if (!carbon_builder_append(&sql, "SELECT ")) {
        status = CARBON_STATUS_OUT_OF_MEMORY;
        goto fail;
    }

    if (select.length == 0) {
        if (!carbon_builder_append_char(&sql, '*')) {
            status = CARBON_STATUS_OUT_OF_MEMORY;
            goto fail;
        }
    } else {
        for (index = 0; index < select.length; ++index) {
            if (index > 0 && !carbon_builder_append(&sql, ", ")) {
                status = CARBON_STATUS_OUT_OF_MEMORY;
                goto fail;
            }
            if (!carbon_append_quoted_identifier(&sql, dialect, select.items[index])) {
                status = CARBON_STATUS_OUT_OF_MEMORY;
                goto fail;
            }
        }
    }

    if (!carbon_builder_append(&sql, " FROM ")
        || !carbon_append_quoted_identifier(&sql, dialect, table)
        || !carbon_builder_append(&params, "[")) {
        status = CARBON_STATUS_OUT_OF_MEMORY;
        goto fail;
    }

    if (has_where) {
        if (!carbon_builder_append(&sql, " WHERE ")
            || !carbon_append_quoted_identifier(&sql, dialect, where.column)
            || !carbon_builder_append(&sql, " = ?")
            || !carbon_append_param(&params, where.raw_value, &param_count)) {
            status = CARBON_STATUS_OUT_OF_MEMORY;
            goto fail;
        }
    }

    if (has_limit) {
        if (!carbon_builder_append(&sql, " LIMIT ?")
            || !carbon_append_param(&params, limit, &param_count)) {
            status = CARBON_STATUS_OUT_OF_MEMORY;
            goto fail;
        }
    }

    if (!carbon_builder_append(&params, "]")
        || !carbon_buffer_take_builder(&result->sql, &sql)
        || !carbon_buffer_take_builder(&result->params_json, &params)) {
        status = CARBON_STATUS_OUT_OF_MEMORY;
        goto fail;
    }

    status = carbon_normalize_allowlist_sql(result->sql.data, &result->allowlist_key, &result->error);
    if (status != CARBON_STATUS_OK) {
        goto fail;
    }

    result->status = CARBON_STATUS_OK;
    carbon_buffer_set(&result->error, "");

    free(dialect_string);
    free(table);
    free(limit);
    carbon_json_array_free(&select);
    carbon_where_clause_free(&where);
    return CARBON_STATUS_OK;

fail:
    carbon_builder_free(&sql);
    carbon_builder_free(&params);
    free(dialect_string);
    free(table);
    free(limit);
    carbon_json_array_free(&select);
    carbon_where_clause_free(&where);
    carbon_compile_result_free(result);
    return carbon_set_result_error(result, status, "%s", carbon_status_message(status));
}
