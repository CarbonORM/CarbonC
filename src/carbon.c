#include "carbon.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct carbon_context {
    unsigned int abi_version;
};

typedef enum carbon_dialect {
    CARBON_DIALECT_MYSQL,
    CARBON_DIALECT_POSTGRESQL
} carbon_dialect;

typedef struct carbon_string_builder {
    char *data;
    size_t length;
    size_t capacity;
} carbon_string_builder;

typedef struct carbon_json_span {
    const char *start;
    const char *end;
} carbon_json_span;

typedef struct carbon_compile_state {
    carbon_dialect dialect;
    carbon_string_builder *sql;
    carbon_string_builder *params;
    int param_count;
} carbon_compile_state;

typedef struct carbon_object_entry {
    carbon_json_span key;
    carbon_json_span value;
} carbon_object_entry;

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

static int carbon_builder_append_format(carbon_string_builder *builder, const char *format, ...) {
    va_list args;
    va_list count_args;
    int count;
    size_t start;

    va_start(args, format);
    va_copy(count_args, args);
    count = vsnprintf(NULL, 0, format, count_args);
    va_end(count_args);

    if (count < 0 || !carbon_builder_reserve(builder, (size_t) count)) {
        va_end(args);
        return 0;
    }

    start = builder->length;
    vsnprintf(builder->data + start, builder->capacity - start, format, args);
    builder->length += (size_t) count;
    va_end(args);
    return 1;
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

static const char *carbon_skip_ws(const char *cursor, const char *end) {
    while (cursor < end && isspace((unsigned char) *cursor)) {
        ++cursor;
    }
    return cursor;
}

static carbon_json_span carbon_trim_span(carbon_json_span span) {
    span.start = carbon_skip_ws(span.start, span.end);
    while (span.end > span.start && isspace((unsigned char) span.end[-1])) {
        --span.end;
    }
    return span;
}

static int carbon_span_starts_with(carbon_json_span span, char value) {
    span = carbon_trim_span(span);
    return span.start < span.end && span.start[0] == value;
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

static char *carbon_span_string_copy(carbon_json_span span) {
    const char *content_start;
    const char *content_end;
    const char *next;

    span = carbon_trim_span(span);
    if (!carbon_parse_json_string_bounds(span.start, span.end, &content_start, &content_end, &next)) {
        return NULL;
    }
    next = carbon_skip_ws(next, span.end);
    if (next != span.end || carbon_string_has_escape(content_start, content_end)) {
        return NULL;
    }
    return carbon_strndup_local(content_start, (size_t) (content_end - content_start));
}

static int carbon_span_string_equals(carbon_json_span span, const char *value) {
    char *copy = carbon_span_string_copy(span);
    int equals;

    if (copy == NULL) {
        return 0;
    }
    equals = strcmp(copy, value) == 0;
    free(copy);
    return equals;
}

static char *carbon_upper_copy(const char *value) {
    size_t length = carbon_strlen(value);
    char *copy = (char *) malloc(length + 1);
    size_t index;

    if (copy == NULL) {
        return NULL;
    }
    for (index = 0; index < length; ++index) {
        copy[index] = (char) toupper((unsigned char) value[index]);
    }
    copy[length] = '\0';
    return copy;
}

static const char *carbon_json_value_end(const char *cursor, const char *end) {
    char stack[128];
    size_t depth = 0;
    int in_string = 0;
    int escaped = 0;

    cursor = carbon_skip_ws(cursor, end);
    if (cursor >= end) {
        return NULL;
    }

    if (*cursor == '"') {
        const char *next = NULL;
        return carbon_parse_json_string_bounds(cursor, end, NULL, NULL, &next) ? next : NULL;
    }

    if (*cursor == '{' || *cursor == '[') {
        while (cursor < end) {
            char ch = *cursor;

            if (in_string) {
                if (escaped) {
                    escaped = 0;
                } else if (ch == '\\') {
                    escaped = 1;
                } else if (ch == '"') {
                    in_string = 0;
                }
                ++cursor;
                continue;
            }

            if (ch == '"') {
                in_string = 1;
                ++cursor;
                continue;
            }

            if (ch == '{' || ch == '[') {
                if (depth == sizeof(stack)) {
                    return NULL;
                }
                stack[depth++] = ch == '{' ? '}' : ']';
                ++cursor;
                continue;
            }

            if (ch == '}' || ch == ']') {
                if (depth == 0 || stack[depth - 1] != ch) {
                    return NULL;
                }
                --depth;
                ++cursor;
                if (depth == 0) {
                    return cursor;
                }
                continue;
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

static int carbon_object_next(
        carbon_json_span object,
        const char **cursor,
        carbon_object_entry *entry) {
    const char *key_start;
    const char *key_end;
    const char *value_start;
    const char *value_end;
    const char *next;

    object = carbon_trim_span(object);
    if (object.start >= object.end || object.start[0] != '{') {
        return -1;
    }

    if (*cursor == NULL) {
        *cursor = object.start + 1;
    }

    *cursor = carbon_skip_ws(*cursor, object.end);
    if (*cursor < object.end && **cursor == '}') {
        return 0;
    }

    if (!carbon_parse_json_string_bounds(*cursor, object.end, &key_start, &key_end, &next)) {
        return -1;
    }

    next = carbon_skip_ws(next, object.end);
    if (next >= object.end || *next != ':') {
        return -1;
    }

    value_start = carbon_skip_ws(next + 1, object.end);
    value_end = carbon_json_value_end(value_start, object.end);
    if (value_end == NULL) {
        return -1;
    }

    entry->key.start = *cursor;
    entry->key.end = next;
    entry->value.start = value_start;
    entry->value.end = value_end;

    *cursor = carbon_skip_ws(value_end, object.end);
    if (*cursor < object.end && **cursor == ',') {
        ++(*cursor);
    }
    return 1;
}

static int carbon_array_next(
        carbon_json_span array,
        const char **cursor,
        carbon_json_span *item) {
    const char *value_start;
    const char *value_end;

    array = carbon_trim_span(array);
    if (array.start >= array.end || array.start[0] != '[') {
        return -1;
    }

    if (*cursor == NULL) {
        *cursor = array.start + 1;
    }

    *cursor = carbon_skip_ws(*cursor, array.end);
    if (*cursor < array.end && **cursor == ']') {
        return 0;
    }

    value_start = carbon_skip_ws(*cursor, array.end);
    value_end = carbon_json_value_end(value_start, array.end);
    if (value_end == NULL) {
        return -1;
    }

    item->start = value_start;
    item->end = value_end;

    *cursor = carbon_skip_ws(value_end, array.end);
    if (*cursor < array.end && **cursor == ',') {
        ++(*cursor);
    }
    return 1;
}

static int carbon_array_get(carbon_json_span array, size_t wanted, carbon_json_span *item) {
    const char *cursor = NULL;
    carbon_json_span current;
    size_t index = 0;
    int next;

    while ((next = carbon_array_next(array, &cursor, &current)) == 1) {
        if (index == wanted) {
            *item = current;
            return 1;
        }
        ++index;
    }
    return next < 0 ? -1 : 0;
}

static int carbon_array_count(carbon_json_span array, size_t *count) {
    const char *cursor = NULL;
    carbon_json_span item;
    int next;

    *count = 0;
    while ((next = carbon_array_next(array, &cursor, &item)) == 1) {
        ++(*count);
    }
    return next < 0 ? 0 : 1;
}

static int carbon_object_get_property(carbon_json_span object, const char *name, carbon_json_span *value) {
    const char *cursor = NULL;
    carbon_object_entry entry;
    int next;

    while ((next = carbon_object_next(object, &cursor, &entry)) == 1) {
        if (carbon_span_string_equals(entry.key, name)) {
            *value = entry.value;
            return 1;
        }
    }
    return next < 0 ? -1 : 0;
}

static int carbon_object_get_any(
        carbon_json_span object,
        const char *const *names,
        size_t name_count,
        carbon_json_span *value) {
    size_t index;

    for (index = 0; index < name_count; ++index) {
        int found = carbon_object_get_property(object, names[index], value);
        if (found != 0) {
            return found;
        }
    }
    return 0;
}

static int carbon_span_is_numeric_key(carbon_json_span key) {
    char *copy = carbon_span_string_copy(key);
    char *cursor;
    int numeric = 1;

    if (copy == NULL || copy[0] == '\0') {
        free(copy);
        return 0;
    }

    for (cursor = copy; *cursor != '\0'; ++cursor) {
        if (!isdigit((unsigned char) *cursor)) {
            numeric = 0;
            break;
        }
    }
    free(copy);
    return numeric;
}

static int carbon_span_is_null(carbon_json_span span) {
    span = carbon_trim_span(span);
    return (size_t) (span.end - span.start) == 4 && strncmp(span.start, "null", 4) == 0;
}

static int carbon_span_is_scalar_json(carbon_json_span span) {
    span = carbon_trim_span(span);
    if (span.start >= span.end) {
        return 0;
    }
    return span.start[0] == '"'
           || span.start[0] == '-'
           || isdigit((unsigned char) span.start[0])
           || carbon_span_is_null(span)
           || ((size_t) (span.end - span.start) == 4 && strncmp(span.start, "true", 4) == 0)
           || ((size_t) (span.end - span.start) == 5 && strncmp(span.start, "false", 5) == 0);
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

static int carbon_identifier_is_dotted(const char *identifier) {
    return identifier != NULL && strchr(identifier, '.') != NULL;
}

static int carbon_identifier_alias_valid(const char *identifier) {
    return identifier != NULL
           && strcmp(identifier, "*") != 0
           && strchr(identifier, '.') == NULL
           && carbon_identifier_valid(identifier);
}

static int carbon_append_quoted_table(
        carbon_string_builder *builder,
        carbon_dialect dialect,
        const char *identifier) {
    char quote = dialect == CARBON_DIALECT_MYSQL ? '`' : '"';

    return carbon_builder_append_char(builder, quote)
           && carbon_builder_append(builder, identifier)
           && carbon_builder_append_char(builder, quote);
}

static int carbon_ascii_case_equals(const char *left, const char *right) {
    while (*left != '\0' && *right != '\0') {
        if (toupper((unsigned char) *left) != toupper((unsigned char) *right)) {
            return 0;
        }
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

static int carbon_parse_join_target(const char *raw, char **table, char **alias) {
    const char *cursor;
    const char *start;
    const char *end;
    char *first = NULL;
    char *second = NULL;

    *table = NULL;
    *alias = NULL;

    if (raw == NULL) {
        return 0;
    }

    cursor = raw;
    while (isspace((unsigned char) *cursor)) {
        ++cursor;
    }
    start = cursor;
    while (*cursor != '\0' && !isspace((unsigned char) *cursor)) {
        ++cursor;
    }
    end = cursor;
    if (start == end) {
        return 0;
    }

    first = carbon_strndup_local(start, (size_t) (end - start));
    if (first == NULL || !carbon_identifier_valid(first) || strcmp(first, "*") == 0) {
        free(first);
        return 0;
    }

    while (isspace((unsigned char) *cursor)) {
        ++cursor;
    }
    if (*cursor == '\0') {
        *table = first;
        return 1;
    }

    start = cursor;
    while (*cursor != '\0' && !isspace((unsigned char) *cursor)) {
        ++cursor;
    }
    end = cursor;
    second = carbon_strndup_local(start, (size_t) (end - start));
    if (second == NULL) {
        free(first);
        return 0;
    }

    if (carbon_ascii_case_equals(second, "AS")) {
        free(second);
        second = NULL;
        while (isspace((unsigned char) *cursor)) {
            ++cursor;
        }
        start = cursor;
        while (*cursor != '\0' && !isspace((unsigned char) *cursor)) {
            ++cursor;
        }
        end = cursor;
        if (start == end) {
            free(first);
            return 0;
        }
        second = carbon_strndup_local(start, (size_t) (end - start));
        if (second == NULL) {
            free(first);
            return 0;
        }
    }

    while (isspace((unsigned char) *cursor)) {
        ++cursor;
    }
    if (*cursor != '\0' || !carbon_identifier_alias_valid(second)) {
        free(first);
        free(second);
        return 0;
    }

    *table = first;
    *alias = second;
    return 1;
}

static char *carbon_normalize_join_type(const char *raw) {
    carbon_string_builder builder = {0};
    int wrote_space = 0;
    char *kind;

    if (raw == NULL) {
        return NULL;
    }

    while (isspace((unsigned char) *raw)) {
        ++raw;
    }
    while (*raw != '\0') {
        unsigned char ch = (unsigned char) *raw;
        if (ch == '_' || isspace(ch)) {
            if (builder.length > 0) {
                wrote_space = 1;
            }
        } else {
            if (wrote_space && !carbon_builder_append_char(&builder, ' ')) {
                carbon_builder_free(&builder);
                return NULL;
            }
            wrote_space = 0;
            if (!carbon_builder_append_char(&builder, (char) toupper(ch))) {
                carbon_builder_free(&builder);
                return NULL;
            }
        }
        ++raw;
    }

    kind = builder.data;
    if (kind == NULL
        || !(strcmp(kind, "INNER") == 0
             || strcmp(kind, "LEFT") == 0
             || strcmp(kind, "LEFT OUTER") == 0
             || strcmp(kind, "RIGHT") == 0
             || strcmp(kind, "RIGHT OUTER") == 0)) {
        carbon_builder_free(&builder);
        return NULL;
    }

    return kind;
}

static int carbon_builder_append_wrapped_expression(carbon_string_builder *builder, const char *expression) {
    carbon_json_span span;

    if (expression == NULL) {
        return 1;
    }

    span.start = expression;
    span.end = expression + strlen(expression);
    span = carbon_trim_span(span);
    if (span.start == span.end) {
        return 1;
    }

    if (span.start[0] == '(' && span.end[-1] == ')') {
        return carbon_builder_append_len(builder, span.start, (size_t) (span.end - span.start));
    }

    return carbon_builder_append_char(builder, '(')
           && carbon_builder_append_len(builder, span.start, (size_t) (span.end - span.start))
           && carbon_builder_append_char(builder, ')');
}

static int carbon_parse_dialect(const char *value, carbon_dialect *dialect) {
    if (value == NULL || strcmp(value, "mysql") == 0) {
        *dialect = CARBON_DIALECT_MYSQL;
        return 1;
    }

    if (strcmp(value, "postgresql") == 0 || strcmp(value, "postgres") == 0) {
        *dialect = CARBON_DIALECT_POSTGRESQL;
        return 1;
    }

    return 0;
}

static const char *carbon_operator_format(const char *op) {
    if (strcmp(op, "=") == 0) return "=";
    if (strcmp(op, "<=>") == 0) return "<=>";
    if (strcmp(op, "<>") == 0) return "<>";
    if (strcmp(op, "<") == 0) return "<";
    if (strcmp(op, "<=") == 0) return "<=";
    if (strcmp(op, ">") == 0) return ">";
    if (strcmp(op, ">=") == 0) return ">=";
    if (strcmp(op, "LIKE") == 0) return "LIKE";
    if (strcmp(op, "NOT_LIKE") == 0 || strcmp(op, "NOT LIKE") == 0) return "NOT LIKE";
    if (strcmp(op, "IN") == 0) return "IN";
    if (strcmp(op, "NOT_IN") == 0 || strcmp(op, "NOT IN") == 0) return "NOT IN";
    if (strcmp(op, "IS") == 0) return "IS";
    if (strcmp(op, "IS_NOT") == 0 || strcmp(op, "IS NOT") == 0) return "IS NOT";
    if (strcmp(op, "BETWEEN") == 0) return "BETWEEN";
    if (strcmp(op, "NOT BETWEEN") == 0) return "NOT BETWEEN";
    return NULL;
}

static int carbon_is_boolean_operator(const char *op) {
    return strcmp(op, "AND") == 0 || strcmp(op, "OR") == 0;
}

static carbon_status carbon_append_param(
        carbon_compile_state *state,
        carbon_json_span raw_value,
        carbon_string_builder *target_sql) {
    raw_value = carbon_trim_span(raw_value);
    if (state->param_count > 0 && !carbon_builder_append(state->params, ",")) {
        return CARBON_STATUS_OUT_OF_MEMORY;
    }
    if (!carbon_builder_append_len(state->params, raw_value.start, (size_t) (raw_value.end - raw_value.start))) {
        return CARBON_STATUS_OUT_OF_MEMORY;
    }
    ++state->param_count;

    if (state->dialect == CARBON_DIALECT_POSTGRESQL) {
        if (!carbon_builder_append_format(target_sql, "$%d", state->param_count)) {
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
    } else if (!carbon_builder_append(target_sql, "?")) {
        return CARBON_STATUS_OUT_OF_MEMORY;
    }
    return CARBON_STATUS_OK;
}

static carbon_status carbon_append_reference_from_string(carbon_string_builder *sql, carbon_json_span string_span) {
    char *identifier = carbon_span_string_copy(string_span);
    int ok;

    if (identifier == NULL || !carbon_identifier_valid(identifier)) {
        free(identifier);
        return CARBON_STATUS_INVALID_QUERY;
    }

    ok = carbon_builder_append(sql, identifier);
    free(identifier);
    return ok ? CARBON_STATUS_OK : CARBON_STATUS_OUT_OF_MEMORY;
}

static carbon_status carbon_append_condition_operand(
        carbon_compile_state *state,
        carbon_json_span value,
        carbon_string_builder *sql);

static carbon_status carbon_append_expression(
        carbon_compile_state *state,
        carbon_json_span value,
        carbon_string_builder *sql) {
    carbon_json_span head_span;
    char *head = NULL;
    char *token = NULL;
    size_t length;
    size_t index;
    carbon_status status = CARBON_STATUS_OK;

    value = carbon_trim_span(value);

    if (carbon_span_starts_with(value, '"')) {
        return carbon_append_reference_from_string(sql, value);
    }

    if (carbon_span_starts_with(value, '[')) {
        if (carbon_array_count(value, &length) == 0 || length == 0) {
            return CARBON_STATUS_INVALID_QUERY;
        }
        if (carbon_array_get(value, 0, &head_span) != 1) {
            return CARBON_STATUS_INVALID_QUERY;
        }
        head = carbon_span_string_copy(head_span);
        token = head == NULL ? NULL : carbon_upper_copy(head);
        if (head == NULL || token == NULL) {
            status = CARBON_STATUS_INVALID_QUERY;
            goto cleanup;
        }

        if (strcmp(token, "AS") == 0) {
            carbon_json_span expression;
            carbon_json_span alias_span;
            char *alias;

            if (length != 3 || carbon_array_get(value, 1, &expression) != 1 || carbon_array_get(value, 2, &alias_span) != 1) {
                status = CARBON_STATUS_INVALID_QUERY;
                goto cleanup;
            }
            status = carbon_append_expression(state, expression, sql);
            if (status != CARBON_STATUS_OK) {
                goto cleanup;
            }
            alias = carbon_span_string_copy(alias_span);
            if (alias == NULL || !carbon_identifier_valid(alias) || strchr(alias, '.') != NULL) {
                free(alias);
                status = CARBON_STATUS_INVALID_QUERY;
                goto cleanup;
            }
            if (!carbon_builder_append(sql, " AS ") || !carbon_builder_append(sql, alias)) {
                free(alias);
                status = CARBON_STATUS_OUT_OF_MEMORY;
                goto cleanup;
            }
            free(alias);
            goto cleanup;
        }

        if (strcmp(token, "DISTINCT") == 0) {
            carbon_json_span expression;
            if (length != 2 || carbon_array_get(value, 1, &expression) != 1) {
                status = CARBON_STATUS_INVALID_QUERY;
                goto cleanup;
            }
            if (!carbon_builder_append(sql, "DISTINCT ")) {
                status = CARBON_STATUS_OUT_OF_MEMORY;
                goto cleanup;
            }
            status = carbon_append_expression(state, expression, sql);
            goto cleanup;
        }

        if (strcmp(token, "LIT") == 0 || strcmp(token, "PARAM") == 0) {
            carbon_json_span literal;
            if (length != 2 || carbon_array_get(value, 1, &literal) != 1) {
                status = CARBON_STATUS_INVALID_QUERY;
                goto cleanup;
            }
            status = carbon_append_param(state, literal, sql);
            goto cleanup;
        }

        if (!carbon_identifier_valid(token) || strcmp(token, "*") == 0) {
            status = CARBON_STATUS_INVALID_QUERY;
            goto cleanup;
        }
        if (!carbon_builder_append(sql, token) || !carbon_builder_append(sql, "(")) {
            status = CARBON_STATUS_OUT_OF_MEMORY;
            goto cleanup;
        }
        for (index = 1; index < length; ++index) {
            carbon_json_span item;
            if (index > 1 && !carbon_builder_append(sql, ", ")) {
                status = CARBON_STATUS_OUT_OF_MEMORY;
                goto cleanup;
            }
            if (carbon_array_get(value, index, &item) != 1) {
                status = CARBON_STATUS_INVALID_QUERY;
                goto cleanup;
            }
            status = carbon_append_expression(state, item, sql);
            if (status != CARBON_STATUS_OK) {
                goto cleanup;
            }
        }
        if (!carbon_builder_append(sql, ")")) {
            status = CARBON_STATUS_OUT_OF_MEMORY;
            goto cleanup;
        }
        goto cleanup;
    }

    if (carbon_span_is_scalar_json(value)) {
        if (!carbon_builder_append_len(sql, value.start, (size_t) (value.end - value.start))) {
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        return CARBON_STATUS_OK;
    }

    return CARBON_STATUS_INVALID_QUERY;

cleanup:
    free(head);
    free(token);
    return status;
}

static carbon_status carbon_append_condition_operand(
        carbon_compile_state *state,
        carbon_json_span value,
        carbon_string_builder *sql) {
    char *identifier;

    value = carbon_trim_span(value);
    if (carbon_span_starts_with(value, '[')) {
        return carbon_append_expression(state, value, sql);
    }

    if (carbon_span_starts_with(value, '"')) {
        if (carbon_span_string_equals(value, "NULL")) {
            carbon_json_span null_span;
            null_span.start = "null";
            null_span.end = null_span.start + 4;
            return carbon_append_param(state, null_span, sql);
        }
        identifier = carbon_span_string_copy(value);
        if (identifier == NULL) {
            return CARBON_STATUS_INVALID_QUERY;
        }
        if (!carbon_identifier_valid(identifier) || !carbon_identifier_is_dotted(identifier)) {
            free(identifier);
            return CARBON_STATUS_INVALID_QUERY;
        }
        if (!carbon_builder_append(sql, identifier)) {
            free(identifier);
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        free(identifier);
        return CARBON_STATUS_OK;
    }

    if (carbon_span_is_scalar_json(value)) {
        return carbon_append_param(state, value, sql);
    }

    return CARBON_STATUS_INVALID_QUERY;
}

static carbon_status carbon_build_operator(
        carbon_compile_state *state,
        const char *operator_raw,
        carbon_json_span operands,
        const char *context_column,
        carbon_string_builder *sql) {
    const char *operator_sql = carbon_operator_format(operator_raw);
    carbon_json_span left;
    carbon_json_span right;
    carbon_json_span start;
    carbon_json_span end;
    carbon_string_builder left_sql = {0};
    carbon_string_builder right_sql = {0};
    size_t count = 0;
    carbon_status status = CARBON_STATUS_OK;

    if (operator_sql == NULL) {
        return CARBON_STATUS_INVALID_QUERY;
    }

    if (context_column != NULL) {
        left.start = context_column;
        left.end = context_column + strlen(context_column);
    }

    if (!carbon_span_starts_with(operands, '[')) {
        if (context_column == NULL) {
            return CARBON_STATUS_INVALID_QUERY;
        }
        right = operands;
        count = 2;
    } else if (carbon_array_count(operands, &count) == 0) {
        return CARBON_STATUS_INVALID_QUERY;
    } else if (context_column != NULL) {
        if (count < 2 || carbon_array_get(operands, 1, &right) != 1) {
            return CARBON_STATUS_INVALID_QUERY;
        }
    } else if (count >= 2) {
        carbon_json_span first;
        carbon_json_span second;
        char *first_string;
        char *second_string;

        if (carbon_array_get(operands, 0, &first) != 1 || carbon_array_get(operands, 1, &second) != 1) {
            return CARBON_STATUS_INVALID_QUERY;
        }
        first_string = carbon_span_string_copy(first);
        second_string = carbon_span_string_copy(second);
        if (first_string != NULL && strcmp(first_string, operator_raw) == 0) {
            free(first_string);
            free(second_string);
            if (count < 3 || carbon_array_get(operands, 1, &left) != 1 || carbon_array_get(operands, 2, &right) != 1) {
                return CARBON_STATUS_INVALID_QUERY;
            }
        } else if (second_string != NULL && strcmp(second_string, operator_raw) == 0) {
            free(first_string);
            free(second_string);
            if (count < 3 || carbon_array_get(operands, 0, &left) != 1 || carbon_array_get(operands, 2, &right) != 1) {
                return CARBON_STATUS_INVALID_QUERY;
            }
        } else {
            free(first_string);
            free(second_string);
            if (carbon_array_get(operands, 0, &left) != 1 || carbon_array_get(operands, 1, &right) != 1) {
                return CARBON_STATUS_INVALID_QUERY;
            }
        }
    } else {
        return CARBON_STATUS_INVALID_QUERY;
    }

    if (strcmp(operator_sql, "IN") == 0 || strcmp(operator_sql, "NOT IN") == 0) {
        carbon_json_span values = right;
        const char *cursor = NULL;
        carbon_json_span item;
        int next;
        int wrote = 0;

        if (!carbon_builder_append(sql, "( ")) {
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        if (context_column != NULL) {
            if (!carbon_builder_append(sql, context_column)) {
                return CARBON_STATUS_OUT_OF_MEMORY;
            }
        } else {
            status = carbon_append_condition_operand(state, left, sql);
            if (status != CARBON_STATUS_OK) {
                return status;
            }
        }
        if (!carbon_builder_append_format(sql, " %s (", operator_sql)) {
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        if (!carbon_span_starts_with(values, '[')) {
            return CARBON_STATUS_INVALID_QUERY;
        }
        while ((next = carbon_array_next(values, &cursor, &item)) == 1) {
            if (wrote && !carbon_builder_append(sql, ", ")) {
                return CARBON_STATUS_OUT_OF_MEMORY;
            }
            status = carbon_append_condition_operand(state, item, sql);
            if (status != CARBON_STATUS_OK) {
                return status;
            }
            wrote = 1;
        }
        if (next < 0 || !wrote) {
            return CARBON_STATUS_INVALID_QUERY;
        }
        if (!carbon_builder_append(sql, ") )")) {
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        return CARBON_STATUS_OK;
    }

    if (strcmp(operator_sql, "BETWEEN") == 0 || strcmp(operator_sql, "NOT BETWEEN") == 0) {
        if (context_column != NULL) {
            if (!carbon_span_starts_with(right, '[') || carbon_array_count(right, &count) == 0 || count != 2
                || carbon_array_get(right, 0, &start) != 1 || carbon_array_get(right, 1, &end) != 1) {
                return CARBON_STATUS_INVALID_QUERY;
            }
        } else {
            if (count == 2) {
                if (!carbon_span_starts_with(right, '[') || carbon_array_count(right, &count) == 0 || count != 2
                    || carbon_array_get(right, 0, &start) != 1 || carbon_array_get(right, 1, &end) != 1) {
                    return CARBON_STATUS_INVALID_QUERY;
                }
            } else if (count == 3) {
                if (carbon_array_get(operands, 1, &start) != 1 || carbon_array_get(operands, 2, &end) != 1) {
                    return CARBON_STATUS_INVALID_QUERY;
                }
            } else {
                return CARBON_STATUS_INVALID_QUERY;
            }
        }

        if (!carbon_builder_append(sql, "(")) {
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        if (context_column != NULL) {
            if (!carbon_builder_append(sql, context_column)) {
                return CARBON_STATUS_OUT_OF_MEMORY;
            }
        } else {
            status = carbon_append_condition_operand(state, left, sql);
            if (status != CARBON_STATUS_OK) return status;
        }
        if (!carbon_builder_append_format(sql, ") %s ", operator_sql)) {
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        status = carbon_append_condition_operand(state, start, sql);
        if (status != CARBON_STATUS_OK) return status;
        if (!carbon_builder_append(sql, " AND ")) {
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        return carbon_append_condition_operand(state, end, sql);
    }

    if (context_column != NULL) {
        if (!carbon_builder_append(&left_sql, context_column)) {
            status = CARBON_STATUS_OUT_OF_MEMORY;
            goto cleanup;
        }
    } else {
        status = carbon_append_condition_operand(state, left, &left_sql);
        if (status != CARBON_STATUS_OK) goto cleanup;
    }
    status = carbon_append_condition_operand(state, right, &right_sql);
    if (status != CARBON_STATUS_OK) goto cleanup;

    if (!carbon_span_starts_with(left, '[') && !carbon_builder_append(sql, "(")) {
        status = CARBON_STATUS_OUT_OF_MEMORY;
        goto cleanup;
    }
    if (!carbon_builder_append(sql, left_sql.data == NULL ? "" : left_sql.data)) {
        status = CARBON_STATUS_OUT_OF_MEMORY;
        goto cleanup;
    }
    if (!carbon_span_starts_with(left, '[') && !carbon_builder_append(sql, ")")) {
        status = CARBON_STATUS_OUT_OF_MEMORY;
        goto cleanup;
    }
    if (!carbon_builder_append_format(sql, " %s ", operator_sql)
        || !carbon_builder_append(sql, right_sql.data == NULL ? "" : right_sql.data)) {
        status = CARBON_STATUS_OUT_OF_MEMORY;
        goto cleanup;
    }

cleanup:
    carbon_builder_free(&left_sql);
    carbon_builder_free(&right_sql);
    return status;
}

static carbon_status carbon_build_where_node(
        carbon_compile_state *state,
        carbon_json_span node,
        const char *default_operator,
        carbon_string_builder *sql);

static carbon_status carbon_join_where_parts(
        carbon_compile_state *state,
        carbon_json_span array,
        const char *operator_sql,
        carbon_string_builder *sql) {
    const char *cursor = NULL;
    carbon_json_span item;
    int next;
    int wrote = 0;
    carbon_status status;

    while ((next = carbon_array_next(array, &cursor, &item)) == 1) {
        carbon_string_builder part = {0};
        status = carbon_build_where_node(state, item, operator_sql, &part);
        if (status != CARBON_STATUS_OK) {
            carbon_builder_free(&part);
            return status;
        }
        if (part.length == 0) {
            carbon_builder_free(&part);
            continue;
        }
        if (wrote && !carbon_builder_append_format(sql, " %s ", operator_sql)) {
            carbon_builder_free(&part);
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        if (!carbon_builder_append(sql, part.data)) {
            carbon_builder_free(&part);
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        carbon_builder_free(&part);
        wrote = 1;
    }
    return next < 0 ? CARBON_STATUS_INVALID_QUERY : CARBON_STATUS_OK;
}

static carbon_status carbon_build_legacy_column_condition(
        carbon_compile_state *state,
        const char *column,
        carbon_json_span value,
        carbon_string_builder *sql) {
    carbon_json_span op_span;
    char *op = NULL;
    carbon_status status;
    size_t count;

    if (!carbon_identifier_valid(column)) {
        return CARBON_STATUS_INVALID_QUERY;
    }

    if (carbon_span_starts_with(value, '[')
        && carbon_array_count(value, &count)
        && count >= 2
        && carbon_array_get(value, 0, &op_span) == 1) {
        op = carbon_span_string_copy(op_span);
        if (op == NULL) {
            return CARBON_STATUS_INVALID_QUERY;
        }
        status = carbon_build_operator(state, op, value, column, sql);
        free(op);
        return status;
    }

    if (carbon_span_starts_with(value, '{')) {
        const char *cursor = NULL;
        carbon_object_entry entry;
        int next = carbon_object_next(value, &cursor, &entry);
        char *key;

        if (next != 1) {
            return next < 0 ? CARBON_STATUS_INVALID_QUERY : CARBON_STATUS_OK;
        }
        key = carbon_span_string_copy(entry.key);
        if (key == NULL) {
            return CARBON_STATUS_INVALID_QUERY;
        }
        if (carbon_operator_format(key) != NULL) {
            status = carbon_build_operator(state, key, entry.value, column, sql);
        } else {
            status = CARBON_STATUS_INVALID_QUERY;
        }
        free(key);
        return status;
    }

    return carbon_build_operator(state, "=", value, column, sql);
}

static carbon_status carbon_build_where_array(
        carbon_compile_state *state,
        carbon_json_span node,
        const char *default_operator,
        carbon_string_builder *sql) {
    size_t count;
    carbon_json_span first;
    carbon_json_span second;
    carbon_json_span third;
    char *first_string = NULL;
    char *second_string = NULL;
    carbon_status status = CARBON_STATUS_OK;

    if (!carbon_array_count(node, &count)) {
        return CARBON_STATUS_INVALID_QUERY;
    }
    if (count == 0) {
        return CARBON_STATUS_OK;
    }

    if (count == 3
        && carbon_array_get(node, 0, &first) == 1
        && carbon_array_get(node, 1, &second) == 1
        && carbon_array_get(node, 2, &third) == 1) {
        first_string = carbon_span_string_copy(first);
        second_string = carbon_span_string_copy(second);
        if (second_string != NULL && carbon_operator_format(second_string) != NULL) {
            status = carbon_build_operator(state, second_string, node, NULL, sql);
            goto cleanup;
        }
        if (first_string != NULL && carbon_operator_format(first_string) != NULL) {
            status = carbon_build_operator(state, first_string, node, NULL, sql);
            goto cleanup;
        }
    }

    status = carbon_join_where_parts(state, node, default_operator, sql);

cleanup:
    free(first_string);
    free(second_string);
    return status;
}

static carbon_status carbon_build_where_object_pass(
        carbon_compile_state *state,
        carbon_json_span node,
        int numeric,
        const char *default_operator,
        carbon_string_builder *sql,
        int *wrote) {
    const char *cursor = NULL;
    carbon_object_entry entry;
    int next;
    carbon_status status;

    while ((next = carbon_object_next(node, &cursor, &entry)) == 1) {
        char *key;
        char *key_upper;
        carbon_string_builder part = {0};
        int is_numeric = carbon_span_is_numeric_key(entry.key);

        if (numeric != is_numeric) {
            continue;
        }

        key = carbon_span_string_copy(entry.key);
        key_upper = key == NULL ? NULL : carbon_upper_copy(key);
        if (key == NULL || key_upper == NULL) {
            free(key);
            free(key_upper);
            return CARBON_STATUS_INVALID_QUERY;
        }

        if (carbon_is_boolean_operator(key_upper)) {
            if (!carbon_span_starts_with(entry.value, '[')) {
                status = CARBON_STATUS_INVALID_QUERY;
            } else {
                status = carbon_join_where_parts(state, entry.value, key_upper, &part);
            }
        } else if (carbon_operator_format(key_upper) != NULL) {
            status = carbon_build_operator(state, key_upper, entry.value, NULL, &part);
        } else if (is_numeric) {
            status = carbon_build_where_node(state, entry.value, "OR", &part);
        } else {
            status = carbon_build_legacy_column_condition(state, key, entry.value, &part);
        }

        free(key);
        free(key_upper);
        if (status != CARBON_STATUS_OK) {
            carbon_builder_free(&part);
            return status;
        }
        if (part.length == 0) {
            carbon_builder_free(&part);
            continue;
        }
        if (*wrote && !carbon_builder_append_format(sql, " %s ", default_operator)) {
            carbon_builder_free(&part);
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        if (!carbon_builder_append(sql, part.data)) {
            carbon_builder_free(&part);
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        carbon_builder_free(&part);
        *wrote = 1;
    }
    return next < 0 ? CARBON_STATUS_INVALID_QUERY : CARBON_STATUS_OK;
}

static carbon_status carbon_build_where_node(
        carbon_compile_state *state,
        carbon_json_span node,
        const char *default_operator,
        carbon_string_builder *sql) {
    int wrote = 0;
    carbon_status status;

    node = carbon_trim_span(node);
    if (carbon_span_starts_with(node, '[')) {
        return carbon_build_where_array(state, node, default_operator, sql);
    }
    if (!carbon_span_starts_with(node, '{')) {
        return CARBON_STATUS_INVALID_QUERY;
    }

    status = carbon_build_where_object_pass(state, node, 0, default_operator, sql, &wrote);
    if (status != CARBON_STATUS_OK) {
        return status;
    }
    return carbon_build_where_object_pass(state, node, 1, default_operator, sql, &wrote);
}

static carbon_status carbon_append_select_list(
        carbon_compile_state *state,
        carbon_json_span query,
        carbon_string_builder *sql) {
    static const char *const select_names[] = {"SELECT", "select"};
    carbon_json_span select;
    const char *cursor = NULL;
    carbon_json_span item;
    int next;
    int wrote = 0;
    int found = carbon_object_get_any(query, select_names, 2, &select);

    if (found < 0) {
        return CARBON_STATUS_INVALID_JSON;
    }
    if (found == 0) {
        return carbon_builder_append(sql, "*") ? CARBON_STATUS_OK : CARBON_STATUS_OUT_OF_MEMORY;
    }
    if (!carbon_span_starts_with(select, '[')) {
        return CARBON_STATUS_INVALID_QUERY;
    }

    while ((next = carbon_array_next(select, &cursor, &item)) == 1) {
        if (wrote && !carbon_builder_append(sql, ", ")) {
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        {
            carbon_status status = carbon_append_expression(state, item, sql);
            if (status != CARBON_STATUS_OK) {
                return status;
            }
        }
        wrote = 1;
    }
    if (next < 0) {
        return CARBON_STATUS_INVALID_QUERY;
    }
    if (!wrote) {
        return carbon_builder_append(sql, "*") ? CARBON_STATUS_OK : CARBON_STATUS_OUT_OF_MEMORY;
    }
    return CARBON_STATUS_OK;
}

static carbon_status carbon_append_join_clauses(
        carbon_compile_state *state,
        carbon_json_span query,
        carbon_string_builder *sql) {
    static const char *const join_names[] = {"JOIN", "join"};
    carbon_json_span joins;
    const char *join_cursor = NULL;
    carbon_object_entry join_entry;
    int found = carbon_object_get_any(query, join_names, 2, &joins);
    int next;

    if (found < 0) {
        return CARBON_STATUS_INVALID_JSON;
    }
    if (found == 0) {
        return CARBON_STATUS_OK;
    }
    if (!carbon_span_starts_with(joins, '{')) {
        return CARBON_STATUS_INVALID_QUERY;
    }

    while ((next = carbon_object_next(joins, &join_cursor, &join_entry)) == 1) {
        char *join_kind = carbon_span_string_copy(join_entry.key);
        char *normalized_join_kind = join_kind == NULL ? NULL : carbon_normalize_join_type(join_kind);
        const char *target_cursor = NULL;
        carbon_object_entry target_entry;
        int target_next;

        free(join_kind);
        if (normalized_join_kind == NULL || !carbon_span_starts_with(join_entry.value, '{')) {
            free(normalized_join_kind);
            return CARBON_STATUS_INVALID_QUERY;
        }

        while ((target_next = carbon_object_next(join_entry.value, &target_cursor, &target_entry)) == 1) {
            char *raw_target = carbon_span_string_copy(target_entry.key);
            char *table = NULL;
            char *alias = NULL;
            carbon_string_builder on_clause = {0};
            carbon_status status = CARBON_STATUS_OK;

            if (raw_target == NULL || !carbon_parse_join_target(raw_target, &table, &alias)) {
                free(raw_target);
                free(normalized_join_kind);
                return CARBON_STATUS_INVALID_QUERY;
            }
            free(raw_target);

            if (!carbon_builder_append_char(sql, ' ')
                || !carbon_builder_append(sql, normalized_join_kind)
                || !carbon_builder_append(sql, " JOIN ")
                || !carbon_append_quoted_table(sql, state->dialect, table)) {
                free(table);
                free(alias);
                free(normalized_join_kind);
                return CARBON_STATUS_OUT_OF_MEMORY;
            }

            if (alias != NULL
                && (!carbon_builder_append(sql, " AS ")
                    || !carbon_append_quoted_table(sql, state->dialect, alias))) {
                free(table);
                free(alias);
                free(normalized_join_kind);
                return CARBON_STATUS_OUT_OF_MEMORY;
            }

            if (carbon_span_starts_with(target_entry.value, '{')) {
                status = carbon_build_where_node(state, target_entry.value, "AND", &on_clause);
            } else {
                status = CARBON_STATUS_INVALID_QUERY;
            }

            free(table);
            free(alias);
            if (status != CARBON_STATUS_OK) {
                carbon_builder_free(&on_clause);
                free(normalized_join_kind);
                return status;
            }
            if (on_clause.length > 0
                && (!carbon_builder_append(sql, " ON ")
                    || !carbon_builder_append_wrapped_expression(sql, on_clause.data))) {
                carbon_builder_free(&on_clause);
                free(normalized_join_kind);
                return CARBON_STATUS_OUT_OF_MEMORY;
            }
            carbon_builder_free(&on_clause);
        }

        free(normalized_join_kind);
        if (target_next < 0) {
            return CARBON_STATUS_INVALID_QUERY;
        }
    }

    return next < 0 ? CARBON_STATUS_INVALID_QUERY : CARBON_STATUS_OK;
}

static carbon_status carbon_append_group_by(
        carbon_compile_state *state,
        carbon_json_span query,
        carbon_string_builder *sql) {
    static const char *const group_by_names[] = {"GROUP_BY", "group_by"};
    carbon_json_span group_by;
    int found = carbon_object_get_any(query, group_by_names, 2, &group_by);

    if (found < 0) {
        return CARBON_STATUS_INVALID_JSON;
    }
    if (found == 0) {
        return CARBON_STATUS_OK;
    }
    if (!carbon_builder_append(sql, " GROUP BY ")) {
        return CARBON_STATUS_OUT_OF_MEMORY;
    }

    if (carbon_span_starts_with(group_by, '[')) {
        const char *cursor = NULL;
        carbon_json_span item;
        int next;
        int wrote = 0;

        while ((next = carbon_array_next(group_by, &cursor, &item)) == 1) {
            carbon_status status;
            if (wrote && !carbon_builder_append(sql, ", ")) {
                return CARBON_STATUS_OUT_OF_MEMORY;
            }
            status = carbon_append_expression(state, item, sql);
            if (status != CARBON_STATUS_OK) {
                return status;
            }
            wrote = 1;
        }
        if (next < 0 || !wrote) {
            return CARBON_STATUS_INVALID_QUERY;
        }
        return CARBON_STATUS_OK;
    }

    if (!carbon_span_starts_with(group_by, '"')) {
        return CARBON_STATUS_INVALID_QUERY;
    }
    return carbon_append_expression(state, group_by, sql);
}

static carbon_status carbon_append_having(
        carbon_compile_state *state,
        carbon_json_span query,
        carbon_string_builder *sql) {
    static const char *const having_names[] = {"HAVING", "having"};
    carbon_json_span having;
    carbon_string_builder having_clause = {0};
    carbon_status status;
    int found = carbon_object_get_any(query, having_names, 2, &having);

    if (found < 0) {
        return CARBON_STATUS_INVALID_JSON;
    }
    if (found == 0) {
        return CARBON_STATUS_OK;
    }

    status = carbon_build_where_node(state, having, "AND", &having_clause);
    if (status != CARBON_STATUS_OK) {
        carbon_builder_free(&having_clause);
        return status;
    }
    if (having_clause.length > 0
        && (!carbon_builder_append(sql, " HAVING ")
            || !carbon_builder_append_wrapped_expression(sql, having_clause.data))) {
        carbon_builder_free(&having_clause);
        return CARBON_STATUS_OUT_OF_MEMORY;
    }

    carbon_builder_free(&having_clause);
    return CARBON_STATUS_OK;
}

static carbon_status carbon_append_pagination(
        carbon_compile_state *state,
        carbon_json_span query,
        carbon_string_builder *sql,
        int *has_pagination) {
    static const char *const pagination_names[] = {"PAGINATION", "pagination"};
    static const char *const order_names[] = {"ORDER", "order"};
    static const char *const limit_names[] = {"LIMIT", "limit"};
    static const char *const page_names[] = {"PAGE", "page"};
    carbon_json_span pagination;
    carbon_json_span order;
    carbon_json_span limit_span;
    carbon_json_span page_span;
    int found;
    int wrote_order = 0;

    *has_pagination = 0;
    found = carbon_object_get_any(query, pagination_names, 2, &pagination);
    if (found < 0) {
        return CARBON_STATUS_INVALID_JSON;
    }

    if (found == 0) {
        found = carbon_object_get_any(query, limit_names, 2, &limit_span);
        if (found < 0) {
            return CARBON_STATUS_INVALID_JSON;
        }
        if (found == 0) {
            return CARBON_STATUS_OK;
        }
        *has_pagination = 1;
        if (!carbon_builder_append(sql, " LIMIT ")
            || !carbon_builder_append_len(sql, carbon_trim_span(limit_span).start,
                                          (size_t) (carbon_trim_span(limit_span).end - carbon_trim_span(limit_span).start))) {
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        return CARBON_STATUS_OK;
    }

    *has_pagination = 1;
    if (!carbon_span_starts_with(pagination, '{')) {
        return CARBON_STATUS_INVALID_QUERY;
    }

    found = carbon_object_get_any(pagination, order_names, 2, &order);
    if (found < 0) {
        return CARBON_STATUS_INVALID_JSON;
    }
    if (found > 0) {
        const char *cursor = NULL;
        carbon_json_span term;
        int next;

        if (!carbon_span_starts_with(order, '[') || !carbon_builder_append(sql, " ORDER BY ")) {
            return CARBON_STATUS_INVALID_QUERY;
        }
        while ((next = carbon_array_next(order, &cursor, &term)) == 1) {
            carbon_json_span expression = term;
            carbon_json_span direction_span;
            char *direction = NULL;
            size_t count;

            if (wrote_order && !carbon_builder_append(sql, ", ")) {
                return CARBON_STATUS_OUT_OF_MEMORY;
            }

            if (carbon_span_starts_with(term, '[')
                && carbon_array_count(term, &count)
                && count == 2
                && carbon_array_get(term, 1, &direction_span) == 1) {
                direction = carbon_span_string_copy(direction_span);
                if (direction != NULL) {
                    char *upper = carbon_upper_copy(direction);
                    if (upper != NULL && (strcmp(upper, "ASC") == 0 || strcmp(upper, "DESC") == 0)) {
                        carbon_array_get(term, 0, &expression);
                        free(direction);
                        direction = upper;
                    } else {
                        free(upper);
                    }
                }
            }

            {
                carbon_status status = carbon_append_expression(state, expression, sql);
                if (status != CARBON_STATUS_OK) {
                    free(direction);
                    return status;
                }
            }
            if (!carbon_builder_append_char(sql, ' ')
                || !carbon_builder_append(sql, direction == NULL ? "ASC" : direction)) {
                free(direction);
                return CARBON_STATUS_OUT_OF_MEMORY;
            }
            free(direction);
            wrote_order = 1;
        }
        if (next < 0) {
            return CARBON_STATUS_INVALID_QUERY;
        }
    }

    found = carbon_object_get_any(pagination, limit_names, 2, &limit_span);
    if (found < 0) {
        return CARBON_STATUS_INVALID_JSON;
    }
    if (found > 0) {
        long limit;
        long page = 1;
        carbon_json_span trimmed = carbon_trim_span(limit_span);
        limit = strtol(trimmed.start, NULL, 10);
        found = carbon_object_get_any(pagination, page_names, 2, &page_span);
        if (found > 0) {
            carbon_json_span page_trimmed = carbon_trim_span(page_span);
            page = strtol(page_trimmed.start, NULL, 10);
            if (page < 1) {
                page = 1;
            }
        }
        if (state->dialect == CARBON_DIALECT_POSTGRESQL) {
            if (!carbon_builder_append_format(sql, " LIMIT %ld", limit)) {
                return CARBON_STATUS_OUT_OF_MEMORY;
            }
            if (page > 1 && !carbon_builder_append_format(sql, " OFFSET %ld", (page - 1) * limit)) {
                return CARBON_STATUS_OUT_OF_MEMORY;
            }
        } else {
            if (page > 1) {
                if (!carbon_builder_append_format(sql, " LIMIT %ld, %ld", (page - 1) * limit, limit)) {
                    return CARBON_STATUS_OUT_OF_MEMORY;
                }
            } else if (!carbon_builder_append_format(sql, " LIMIT %ld", limit)) {
                return CARBON_STATUS_OUT_OF_MEMORY;
            }
        }
    }

    return CARBON_STATUS_OK;
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

static int carbon_append_collapsed_space(carbon_string_builder *builder, const char *sql) {
    int wrote_space = 0;

    while (*sql != '\0') {
        unsigned char ch = (unsigned char) *sql;

        if (isspace(ch)) {
            if (builder->length > 0) {
                wrote_space = 1;
            }
            ++sql;
            continue;
        }

        if (wrote_space) {
            if (!carbon_builder_append_char(builder, ' ')) {
                return 0;
            }
            wrote_space = 0;
        }
        if (!carbon_builder_append_char(builder, (char) ch)) {
            return 0;
        }
        ++sql;
    }

    while (builder->length > 0 && isspace((unsigned char) builder->data[builder->length - 1])) {
        builder->data[--builder->length] = '\0';
    }
    if (builder->length > 0 && builder->data[builder->length - 1] == ';') {
        builder->data[--builder->length] = '\0';
    }
    return 1;
}

static int carbon_ci_starts_with(const char *value, const char *prefix) {
    while (*prefix != '\0') {
        if (toupper((unsigned char) *value) != toupper((unsigned char) *prefix)) {
            return 0;
        }
        ++value;
        ++prefix;
    }
    return 1;
}

static const char *carbon_skip_digits_and_spaces(const char *cursor) {
    cursor = carbon_skip_ws(cursor, cursor + strlen(cursor));
    while (isdigit((unsigned char) *cursor)) {
        ++cursor;
    }
    return carbon_skip_ws(cursor, cursor + strlen(cursor));
}

static int carbon_append_limit_normalized(carbon_string_builder *builder, const char *sql) {
    const char *cursor = sql;

    while (*cursor != '\0') {
        if (carbon_ci_starts_with(cursor, "LIMIT ")
            && (cursor == sql || !isalnum((unsigned char) cursor[-1]))) {
            const char *after_limit = cursor + 6;
            const char *after_first = carbon_skip_digits_and_spaces(after_limit);

            if (*after_first == ',') {
                const char *after_second = carbon_skip_digits_and_spaces(after_first + 1);
                if (!carbon_builder_append(builder, "LIMIT ?, ?")) {
                    return 0;
                }
                cursor = after_second;
                continue;
            }

            if (carbon_ci_starts_with(after_first, "OFFSET ")) {
                const char *after_offset = carbon_skip_digits_and_spaces(after_first + 7);
                if (!carbon_builder_append(builder, "LIMIT ? OFFSET ?")) {
                    return 0;
                }
                cursor = after_offset;
                continue;
            }

            if (!carbon_builder_append(builder, "LIMIT ?")) {
                return 0;
            }
            cursor = after_first;
            continue;
        }

        if (!carbon_builder_append_char(builder, *cursor)) {
            return 0;
        }
        ++cursor;
    }
    return 1;
}

static int carbon_append_in_bind_normalized(carbon_string_builder *builder, const char *sql) {
    const char *cursor = sql;

    while (*cursor != '\0') {
        if (carbon_ci_starts_with(cursor, "IN (")) {
            const char *probe = cursor + 4;
            int binds = 0;

            probe = carbon_skip_ws(probe, probe + strlen(probe));
            while (*probe == '?') {
                ++binds;
                ++probe;
                probe = carbon_skip_ws(probe, probe + strlen(probe));
                if (*probe == ',') {
                    ++probe;
                    probe = carbon_skip_ws(probe, probe + strlen(probe));
                    continue;
                }
                break;
            }

            if (binds > 1 && *probe == ')') {
                if (!carbon_builder_append(builder, "IN (? \xC3\x97*)")) {
                    return 0;
                }
                cursor = probe + 1;
                continue;
            }
        }

        if (!carbon_builder_append_char(builder, *cursor)) {
            return 0;
        }
        ++cursor;
    }
    return 1;
}

carbon_status carbon_normalize_allowlist_sql(
        const char *sql,
        carbon_buffer *out,
        carbon_buffer *error) {
    carbon_string_builder collapsed = {0};
    carbon_string_builder limit_normalized = {0};
    carbon_string_builder bind_normalized = {0};

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

    if (!carbon_append_collapsed_space(&collapsed, sql)
        || !carbon_append_limit_normalized(&limit_normalized, collapsed.data == NULL ? "" : collapsed.data)
        || !carbon_append_in_bind_normalized(&bind_normalized, limit_normalized.data == NULL ? "" : limit_normalized.data)
        || !carbon_buffer_take_builder(out, &bind_normalized)) {
        carbon_builder_free(&collapsed);
        carbon_builder_free(&limit_normalized);
        carbon_builder_free(&bind_normalized);
        if (error != NULL) {
            carbon_buffer_set(error, carbon_status_message(CARBON_STATUS_OUT_OF_MEMORY));
        }
        return CARBON_STATUS_OUT_OF_MEMORY;
    }

    carbon_builder_free(&collapsed);
    carbon_builder_free(&limit_normalized);
    if (error != NULL) {
        carbon_buffer_set(error, "");
    }
    return CARBON_STATUS_OK;
}

carbon_status carbon_compile_query(
        carbon_context *context,
        const carbon_compile_request *request,
        carbon_compile_result *result) {
    static const char *const dialect_names[] = {"dialect", "DIALECT"};
    static const char *const table_names[] = {"FROM", "table"};
    static const char *const where_names[] = {"WHERE", "where"};
    carbon_json_span query;
    carbon_json_span dialect_span;
    carbon_json_span table_span;
    carbon_json_span where_span;
    char *dialect_string = NULL;
    char *table = NULL;
    carbon_dialect dialect;
    carbon_status status;
    carbon_string_builder sql = {0};
    carbon_string_builder params = {0};
    carbon_compile_state state;
    int has_pagination = 0;
    int found;

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

    query.start = request->query_json;
    query.end = request->query_json + request->query_json_length;
    query = carbon_trim_span(query);
    if (!carbon_span_starts_with(query, '{')) {
        return carbon_set_result_error(result, CARBON_STATUS_INVALID_JSON, "query_json must be an object");
    }

    found = carbon_object_get_any(query, dialect_names, 2, &dialect_span);
    if (found < 0) {
        return carbon_set_result_error(result, CARBON_STATUS_INVALID_JSON, "invalid query json");
    }
    if (found > 0) {
        dialect_string = carbon_span_string_copy(dialect_span);
        if (dialect_string == NULL) {
            return carbon_set_result_error(result, CARBON_STATUS_INVALID_QUERY, "dialect must be a string");
        }
    }
    if (!carbon_parse_dialect(dialect_string != NULL ? dialect_string : request->dialect, &dialect)) {
        free(dialect_string);
        return carbon_set_result_error(result, CARBON_STATUS_UNSUPPORTED_DIALECT,
                                       "%s", carbon_status_message(CARBON_STATUS_UNSUPPORTED_DIALECT));
    }

    found = carbon_object_get_any(query, table_names, 2, &table_span);
    if (found < 0) {
        free(dialect_string);
        return carbon_set_result_error(result, CARBON_STATUS_INVALID_JSON, "invalid query json");
    }
    if (found == 0) {
        free(dialect_string);
        return carbon_set_result_error(result, CARBON_STATUS_INVALID_QUERY, "FROM/table is required");
    }
    table = carbon_span_string_copy(table_span);
    if (table == NULL || !carbon_identifier_valid(table) || strcmp(table, "*") == 0) {
        free(dialect_string);
        free(table);
        return carbon_set_result_error(result, CARBON_STATUS_INVALID_QUERY, "invalid table identifier");
    }

    state.dialect = dialect;
    state.sql = &sql;
    state.params = &params;
    state.param_count = 0;

    if (!carbon_builder_append(&params, "[")
        || !carbon_builder_append(&sql, "SELECT ")) {
        status = CARBON_STATUS_OUT_OF_MEMORY;
        goto fail;
    }

    status = carbon_append_select_list(&state, query, &sql);
    if (status != CARBON_STATUS_OK) {
        goto fail;
    }

    if (!carbon_builder_append(&sql, " FROM ")
        || !carbon_append_quoted_table(&sql, dialect, table)) {
        status = CARBON_STATUS_OUT_OF_MEMORY;
        goto fail;
    }

    status = carbon_append_join_clauses(&state, query, &sql);
    if (status != CARBON_STATUS_OK) {
        goto fail;
    }

    found = carbon_object_get_any(query, where_names, 2, &where_span);
    if (found < 0) {
        status = CARBON_STATUS_INVALID_JSON;
        goto fail;
    }
    if (found > 0) {
        carbon_string_builder where = {0};
        status = carbon_build_where_node(&state, where_span, "AND", &where);
        if (status != CARBON_STATUS_OK) {
            carbon_builder_free(&where);
            goto fail;
        }
        if (where.length > 0 && (!carbon_builder_append(&sql, " WHERE ")
                                 || !carbon_builder_append(&sql, where.data))) {
            carbon_builder_free(&where);
            status = CARBON_STATUS_OUT_OF_MEMORY;
            goto fail;
        }
        carbon_builder_free(&where);
    }

    status = carbon_append_group_by(&state, query, &sql);
    if (status != CARBON_STATUS_OK) {
        goto fail;
    }

    status = carbon_append_having(&state, query, &sql);
    if (status != CARBON_STATUS_OK) {
        goto fail;
    }

    status = carbon_append_pagination(&state, query, &sql, &has_pagination);
    if (status != CARBON_STATUS_OK) {
        goto fail;
    }
    if (!has_pagination && !carbon_builder_append(&sql, " LIMIT 100")) {
        status = CARBON_STATUS_OUT_OF_MEMORY;
        goto fail;
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
    return CARBON_STATUS_OK;

fail:
    carbon_builder_free(&sql);
    carbon_builder_free(&params);
    free(dialect_string);
    free(table);
    carbon_compile_result_free(result);
    return carbon_set_result_error(result, status, "%s", carbon_status_message(status));
}
