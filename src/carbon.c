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

typedef struct carbon_schema_view {
    carbon_json_span root;
    carbon_json_span tables;
    int has_tables;
} carbon_schema_view;

typedef struct carbon_query_scope {
    const char *base_table;
    carbon_json_span query;
    const struct carbon_query_scope *parent;
} carbon_query_scope;

typedef struct carbon_compile_state {
    carbon_dialect dialect;
    carbon_string_builder *sql;
    carbon_string_builder *params;
    const carbon_schema_view *schema;
    const carbon_query_scope *scope;
    int param_count;
} carbon_compile_state;

typedef struct carbon_object_entry {
    carbon_json_span key;
    carbon_json_span value;
} carbon_object_entry;

typedef struct carbon_write_columns {
    char **keys;
    char **short_columns;
    size_t count;
} carbon_write_columns;

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

static int carbon_builder_append_json_string(carbon_string_builder *builder, const char *value) {
    const unsigned char *cursor = (const unsigned char *) (value == NULL ? "" : value);

    if (!carbon_builder_append_char(builder, '"')) {
        return 0;
    }
    while (*cursor != '\0') {
        unsigned char ch = *cursor++;

        switch (ch) {
            case '"':
                if (!carbon_builder_append(builder, "\\\"")) return 0;
                break;
            case '\\':
                if (!carbon_builder_append(builder, "\\\\")) return 0;
                break;
            case '\b':
                if (!carbon_builder_append(builder, "\\b")) return 0;
                break;
            case '\f':
                if (!carbon_builder_append(builder, "\\f")) return 0;
                break;
            case '\n':
                if (!carbon_builder_append(builder, "\\n")) return 0;
                break;
            case '\r':
                if (!carbon_builder_append(builder, "\\r")) return 0;
                break;
            case '\t':
                if (!carbon_builder_append(builder, "\\t")) return 0;
                break;
            default:
                if (ch < 0x20) {
                    if (!carbon_builder_append_format(builder, "\\u%04x", (unsigned int) ch)) {
                        return 0;
                    }
                } else if (!carbon_builder_append_char(builder, (char) ch)) {
                    return 0;
                }
                break;
        }
    }
    return carbon_builder_append_char(builder, '"');
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
    const char *cursor;
    char *copy;
    size_t length = 0;

    span = carbon_trim_span(span);
    if (!carbon_parse_json_string_bounds(span.start, span.end, &content_start, &content_end, &next)) {
        return NULL;
    }
    next = carbon_skip_ws(next, span.end);
    if (next != span.end) {
        return NULL;
    }
    if (!carbon_string_has_escape(content_start, content_end)) {
        return carbon_strndup_local(content_start, (size_t) (content_end - content_start));
    }

    copy = (char *) malloc((size_t) (content_end - content_start) + 1);
    if (copy == NULL) {
        return NULL;
    }
    for (cursor = content_start; cursor < content_end; ++cursor) {
        if (*cursor != '\\') {
            copy[length++] = *cursor;
            continue;
        }
        ++cursor;
        if (cursor >= content_end) {
            free(copy);
            return NULL;
        }
        switch (*cursor) {
            case '"':
            case '\\':
            case '/':
                copy[length++] = *cursor;
                break;
            case 'b':
                copy[length++] = '\b';
                break;
            case 'f':
                copy[length++] = '\f';
                break;
            case 'n':
                copy[length++] = '\n';
                break;
            case 'r':
                copy[length++] = '\r';
                break;
            case 't':
                copy[length++] = '\t';
                break;
            default:
                free(copy);
                return NULL;
        }
    }
    copy[length] = '\0';
    return copy;
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

static int carbon_span_is_true(carbon_json_span span) {
    span = carbon_trim_span(span);
    return (size_t) (span.end - span.start) == 4 && strncmp(span.start, "true", 4) == 0;
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
    static const char derived_prefix[] = "__c6DerivedTable__";
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
    if (first == NULL
        || !carbon_identifier_valid(first)
        || strcmp(first, "*") == 0
        || strncmp(first, derived_prefix, sizeof(derived_prefix) - 1) == 0) {
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

static int carbon_join_target_is_derived(const char *raw) {
    const char *cursor = raw;

    if (cursor == NULL) {
        return 0;
    }
    while (isspace((unsigned char) *cursor)) {
        ++cursor;
    }
    return *cursor == '{';
}

static int carbon_copy_derived_join_alias(const char *raw, char **alias) {
    static const char *const alias_names[] = {"AS", "as"};
    carbon_json_span target;
    carbon_json_span alias_span;
    int found;

    *alias = NULL;
    if (!carbon_join_target_is_derived(raw)) {
        return 0;
    }

    target.start = raw;
    target.end = raw + strlen(raw);
    target = carbon_trim_span(target);
    found = carbon_object_get_any(target, alias_names, 2, &alias_span);
    if (found <= 0) {
        return -1;
    }
    *alias = carbon_span_string_copy(alias_span);
    if (*alias == NULL || !carbon_identifier_alias_valid(*alias)) {
        free(*alias);
        *alias = NULL;
        return -1;
    }
    return 1;
}

static int carbon_schema_enabled(const carbon_compile_state *state) {
    return state != NULL && state->schema != NULL && state->schema->has_tables;
}

static int carbon_schema_column_name_matches(const char *candidate, const char *table, const char *column) {
    size_t candidate_length;
    size_t table_length;
    size_t column_length;

    if (candidate == NULL || table == NULL || column == NULL) {
        return 0;
    }
    if (strcmp(candidate, column) == 0) {
        return 1;
    }

    candidate_length = strlen(candidate);
    table_length = strlen(table);
    column_length = strlen(column);
    return candidate_length == table_length + 1 + column_length
           && strncmp(candidate, table, table_length) == 0
           && candidate[table_length] == '.'
           && strcmp(candidate + table_length + 1, column) == 0;
}

static int carbon_schema_span_matches_column(carbon_json_span span, const char *table, const char *column) {
    char *candidate = carbon_span_string_copy(span);
    int matched;

    if (candidate == NULL) {
        return 0;
    }
    matched = carbon_schema_column_name_matches(candidate, table, column);
    free(candidate);
    return matched;
}

static int carbon_schema_columns_array_has_column(
        carbon_json_span columns,
        const char *table,
        const char *column) {
    const char *cursor = NULL;
    carbon_json_span item;
    int next;

    while ((next = carbon_array_next(columns, &cursor, &item)) == 1) {
        if (carbon_schema_span_matches_column(item, table, column)) {
            return 1;
        }
    }
    return next < 0 ? -1 : 0;
}

static int carbon_schema_columns_object_has_column(
        carbon_json_span columns,
        const char *table,
        const char *column) {
    const char *cursor = NULL;
    carbon_object_entry entry;
    int next;

    while ((next = carbon_object_next(columns, &cursor, &entry)) == 1) {
        if (carbon_schema_span_matches_column(entry.key, table, column)
            || carbon_schema_span_matches_column(entry.value, table, column)) {
            return 1;
        }
    }
    return next < 0 ? -1 : 0;
}

static int carbon_schema_find_table(
        const carbon_schema_view *schema,
        const char *table,
        carbon_json_span *definition) {
    if (schema == NULL || !schema->has_tables) {
        return 0;
    }
    return carbon_object_get_property(schema->tables, table, definition);
}

static int carbon_schema_table_has_column(
        carbon_json_span definition,
        const char *table,
        const char *column) {
    static const char *const column_names[] = {"COLUMNS", "columns"};
    carbon_json_span columns;
    int found;

    definition = carbon_trim_span(definition);
    if (carbon_span_starts_with(definition, '[')) {
        return carbon_schema_columns_array_has_column(definition, table, column);
    }
    if (!carbon_span_starts_with(definition, '{')) {
        return -1;
    }

    found = carbon_object_get_any(definition, column_names, 2, &columns);
    if (found < 0) {
        return -1;
    }
    if (found == 0) {
        return 1;
    }

    columns = carbon_trim_span(columns);
    if (carbon_span_starts_with(columns, '[')) {
        return carbon_schema_columns_array_has_column(columns, table, column);
    }
    if (carbon_span_starts_with(columns, '{')) {
        return carbon_schema_columns_object_has_column(columns, table, column);
    }
    return -1;
}

static carbon_status carbon_schema_validate_table(
        const carbon_compile_state *state,
        const char *table) {
    carbon_json_span definition;
    int found;

    if (!carbon_schema_enabled(state)) {
        return CARBON_STATUS_OK;
    }
    found = carbon_schema_find_table(state->schema, table, &definition);
    if (found < 0) {
        return CARBON_STATUS_INVALID_JSON;
    }
    return found > 0 ? CARBON_STATUS_OK : CARBON_STATUS_INVALID_QUERY;
}

static carbon_status carbon_schema_validate_column(
        const carbon_compile_state *state,
        const char *table,
        const char *column) {
    carbon_json_span definition;
    int found;
    int has_column;

    if (!carbon_schema_enabled(state)) {
        return CARBON_STATUS_OK;
    }
    found = carbon_schema_find_table(state->schema, table, &definition);
    if (found < 0) {
        return CARBON_STATUS_INVALID_JSON;
    }
    if (found == 0) {
        return CARBON_STATUS_INVALID_QUERY;
    }
    has_column = carbon_schema_table_has_column(definition, table, column);
    if (has_column < 0) {
        return CARBON_STATUS_INVALID_QUERY;
    }
    return has_column > 0 ? CARBON_STATUS_OK : CARBON_STATUS_INVALID_QUERY;
}

static int carbon_schema_resolve_join_qualifier(
        const carbon_query_scope *scope,
        const char *qualifier,
        char **table) {
    static const char *const join_names[] = {"JOIN", "join"};
    carbon_json_span joins;
    const char *join_cursor = NULL;
    carbon_object_entry join_entry;
    int found;
    int next;

    *table = NULL;
    found = carbon_object_get_any(scope->query, join_names, 2, &joins);
    if (found <= 0) {
        return found;
    }
    if (!carbon_span_starts_with(joins, '{')) {
        return -1;
    }

    while ((next = carbon_object_next(joins, &join_cursor, &join_entry)) == 1) {
        const char *target_cursor = NULL;
        carbon_object_entry target_entry;
        int target_next;

        if (!carbon_span_starts_with(join_entry.value, '{')) {
            return -1;
        }
        while ((target_next = carbon_object_next(join_entry.value, &target_cursor, &target_entry)) == 1) {
            char *raw_target = carbon_span_string_copy(target_entry.key);
            char *candidate_table = NULL;
            char *candidate_alias = NULL;
            int derived_alias_status;
            int matched = 0;

            if (raw_target == NULL) {
                free(raw_target);
                return -1;
            }

            derived_alias_status = carbon_copy_derived_join_alias(raw_target, &candidate_alias);
            if (derived_alias_status < 0) {
                free(raw_target);
                return -1;
            }
            if (derived_alias_status > 0) {
                matched = strcmp(candidate_alias, qualifier) == 0;
                free(raw_target);
                free(candidate_alias);
                if (matched) {
                    return 2;
                }
                continue;
            }

            if (!carbon_parse_join_target(raw_target, &candidate_table, &candidate_alias)) {
                free(raw_target);
                return -1;
            }
            free(raw_target);

            matched = strcmp(candidate_table, qualifier) == 0
                      || (candidate_alias != NULL && strcmp(candidate_alias, qualifier) == 0);
            if (matched) {
                *table = candidate_table;
                free(candidate_alias);
                return 1;
            }

            free(candidate_table);
            free(candidate_alias);
        }
        if (target_next < 0) {
            return -1;
        }
    }

    return next < 0 ? -1 : 0;
}

static int carbon_schema_resolve_qualifier(
        const carbon_compile_state *state,
        const char *qualifier,
        char **table) {
    const carbon_query_scope *scope;

    *table = NULL;
    for (scope = state == NULL ? NULL : state->scope; scope != NULL; scope = scope->parent) {
        int found;
        if (scope->base_table != NULL && strcmp(scope->base_table, qualifier) == 0) {
            *table = carbon_strndup_local(scope->base_table, strlen(scope->base_table));
            return *table == NULL ? -1 : 1;
        }
        found = carbon_schema_resolve_join_qualifier(scope, qualifier, table);
        if (found != 0) {
            return found;
        }
    }
    return 0;
}

static int carbon_schema_resolve_unqualified_reference_table(
        const carbon_compile_state *state,
        char **table) {
    const carbon_query_scope *scope;

    *table = NULL;
    for (scope = state == NULL ? NULL : state->scope; scope != NULL; scope = scope->parent) {
        if (scope->base_table != NULL) {
            *table = carbon_strndup_local(scope->base_table, strlen(scope->base_table));
            return *table == NULL ? -1 : 1;
        }
    }
    return 0;
}

static carbon_status carbon_schema_validate_reference_identifier(
        const carbon_compile_state *state,
        const char *identifier) {
    const char *dot;
    char *qualifier;
    char *table = NULL;
    int found;
    carbon_status status;

    if (!carbon_schema_enabled(state) || identifier == NULL || strcmp(identifier, "*") == 0) {
        return CARBON_STATUS_OK;
    }

    dot = strrchr(identifier, '.');
    if (dot == NULL) {
        if (!carbon_identifier_alias_valid(identifier)) {
            return CARBON_STATUS_INVALID_QUERY;
        }
        found = carbon_schema_resolve_unqualified_reference_table(state, &table);
        if (found <= 0) {
            free(table);
            return found < 0 ? CARBON_STATUS_OUT_OF_MEMORY : CARBON_STATUS_INVALID_QUERY;
        }
        status = carbon_schema_validate_column(state, table, identifier);
        free(table);
        return status;
    }
    if (dot[1] == '\0' || !carbon_identifier_alias_valid(dot + 1)) {
        return CARBON_STATUS_INVALID_QUERY;
    }

    qualifier = carbon_strndup_local(identifier, (size_t) (dot - identifier));
    if (qualifier == NULL) {
        return CARBON_STATUS_OUT_OF_MEMORY;
    }
    found = carbon_schema_resolve_qualifier(state, qualifier, &table);
    free(qualifier);
    if (found < 0) {
        free(table);
        return CARBON_STATUS_INVALID_QUERY;
    }
    if (found == 0) {
        return CARBON_STATUS_INVALID_QUERY;
    }
    if (found == 2) {
        free(table);
        return CARBON_STATUS_OK;
    }

    status = carbon_schema_validate_column(state, table, dot + 1);
    free(table);
    return status;
}

static carbon_status carbon_schema_prepare(
        const char *schema_json,
        size_t schema_json_length,
        carbon_schema_view *schema,
        const char **error_message) {
    static const char *const table_names[] = {"TABLES", "tables"};
    static const char *const c6_names[] = {"C6", "c6"};
    carbon_json_span c6;
    int found;

    schema->root.start = NULL;
    schema->root.end = NULL;
    schema->tables.start = NULL;
    schema->tables.end = NULL;
    schema->has_tables = 0;

    if (schema_json == NULL || schema_json_length == 0) {
        return CARBON_STATUS_OK;
    }

    schema->root.start = schema_json;
    schema->root.end = schema_json + schema_json_length;
    schema->root = carbon_trim_span(schema->root);
    if (schema->root.start == schema->root.end) {
        return CARBON_STATUS_OK;
    }
    if (!carbon_span_starts_with(schema->root, '{')) {
        if (error_message != NULL) {
            *error_message = "schema_json must be an object";
        }
        return CARBON_STATUS_INVALID_JSON;
    }

    found = carbon_object_get_any(schema->root, table_names, 2, &schema->tables);
    if (found < 0) {
        return CARBON_STATUS_INVALID_JSON;
    }
    if (found == 0) {
        found = carbon_object_get_any(schema->root, c6_names, 2, &c6);
        if (found < 0) {
            return CARBON_STATUS_INVALID_JSON;
        }
        if (found > 0) {
            if (!carbon_span_starts_with(c6, '{')) {
                if (error_message != NULL) {
                    *error_message = "schema C6 must be an object";
                }
                return CARBON_STATUS_INVALID_QUERY;
            }
            found = carbon_object_get_any(c6, table_names, 2, &schema->tables);
            if (found < 0) {
                return CARBON_STATUS_INVALID_JSON;
            }
        }
    }
    if (found == 0) {
        return CARBON_STATUS_OK;
    }
    if (!carbon_span_starts_with(schema->tables, '{')) {
        if (error_message != NULL) {
            *error_message = "schema TABLES must be an object";
        }
        return CARBON_STATUS_INVALID_QUERY;
    }

    schema->has_tables = 1;
    return CARBON_STATUS_OK;
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
    if (strcmp(op, "EXISTS") == 0) return "EXISTS";
    if (strcmp(op, "NOT_EXISTS") == 0 || strcmp(op, "NOT EXISTS") == 0) return "NOT EXISTS";
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

static carbon_status carbon_append_reference_from_string(
        carbon_compile_state *state,
        carbon_string_builder *sql,
        carbon_json_span string_span) {
    char *identifier = carbon_span_string_copy(string_span);
    carbon_status status;
    int ok;

    if (identifier == NULL || !carbon_identifier_valid(identifier)) {
        free(identifier);
        return CARBON_STATUS_INVALID_QUERY;
    }
    status = carbon_schema_validate_reference_identifier(state, identifier);
    if (status != CARBON_STATUS_OK) {
        free(identifier);
        return status;
    }

    ok = carbon_builder_append(sql, identifier);
    free(identifier);
    return ok ? CARBON_STATUS_OK : CARBON_STATUS_OUT_OF_MEMORY;
}

static carbon_status carbon_append_condition_operand(
        carbon_compile_state *state,
        carbon_json_span value,
        carbon_string_builder *sql);

static carbon_status carbon_compile_select_statement(
        carbon_compile_state *state,
        carbon_json_span query,
        int is_subselect,
        carbon_string_builder *sql,
        const char *extra_where_sql,
        const char **error_message);

static carbon_status carbon_copy_query_table(
        carbon_compile_state *state,
        carbon_json_span query,
        char **table,
        const char **error_message) {
    static const char *const table_names[] = {"FROM", "table"};
    carbon_json_span table_span;
    carbon_status status;
    int found;

    *table = NULL;
    found = carbon_object_get_any(query, table_names, 2, &table_span);
    if (found < 0) {
        return CARBON_STATUS_INVALID_JSON;
    }
    if (found == 0) {
        if (error_message != NULL) {
            *error_message = "FROM/table is required";
        }
        return CARBON_STATUS_INVALID_QUERY;
    }
    *table = carbon_span_string_copy(table_span);
    if (*table == NULL || !carbon_identifier_valid(*table) || strcmp(*table, "*") == 0) {
        if (error_message != NULL) {
            *error_message = "invalid table identifier";
        }
        free(*table);
        *table = NULL;
        return CARBON_STATUS_INVALID_QUERY;
    }
    status = carbon_schema_validate_table(state, *table);
    if (status != CARBON_STATUS_OK) {
        if (error_message != NULL && status == CARBON_STATUS_INVALID_QUERY) {
            *error_message = "table is not present in schema";
        }
        free(*table);
        *table = NULL;
        return status;
    }
    return CARBON_STATUS_OK;
}

static carbon_status carbon_schema_validate_reference_in_payload_scope(
        carbon_compile_state *state,
        carbon_json_span payload,
        const char *identifier) {
    carbon_query_scope scope;
    const carbon_query_scope *previous_scope = state->scope;
    char *table = NULL;
    carbon_status status;

    if (!carbon_schema_enabled(state)) {
        return CARBON_STATUS_OK;
    }
    status = carbon_copy_query_table(state, payload, &table, NULL);
    if (status != CARBON_STATUS_OK) {
        return status;
    }

    scope.base_table = table;
    scope.query = payload;
    scope.parent = previous_scope;
    state->scope = &scope;
    status = carbon_schema_validate_reference_identifier(state, identifier);
    state->scope = previous_scope;

    free(table);
    return status;
}

static int carbon_extract_subselect_payload(carbon_json_span value, carbon_json_span *payload) {
    carbon_json_span head_span;
    char *head = NULL;
    char *token = NULL;
    size_t length;
    int matched = 0;

    value = carbon_trim_span(value);
    if (!carbon_span_starts_with(value, '[')) {
        return 0;
    }
    if (!carbon_array_count(value, &length)) {
        return -1;
    }
    if (length != 2 || carbon_array_get(value, 0, &head_span) != 1) {
        return 0;
    }

    head = carbon_span_string_copy(head_span);
    token = head == NULL ? NULL : carbon_upper_copy(head);
    if (token != NULL && strcmp(token, "SUBSELECT") == 0) {
        matched = carbon_array_get(value, 1, payload) == 1 ? 1 : -1;
    }

    free(head);
    free(token);
    return matched;
}

static carbon_status carbon_append_scalar_subselect(
        carbon_compile_state *state,
        carbon_json_span payload,
        carbon_string_builder *sql) {
    carbon_status status;

    payload = carbon_trim_span(payload);
    if (!carbon_span_starts_with(payload, '{')) {
        return CARBON_STATUS_INVALID_QUERY;
    }

    if (!carbon_builder_append_char(sql, '(')) {
        return CARBON_STATUS_OUT_OF_MEMORY;
    }
    status = carbon_compile_select_statement(state, payload, 1, sql, NULL, NULL);
    if (status != CARBON_STATUS_OK) {
        return status;
    }
    return carbon_builder_append_char(sql, ')') ? CARBON_STATUS_OK : CARBON_STATUS_OUT_OF_MEMORY;
}

static int carbon_resolve_subselect_payload(carbon_json_span value, carbon_json_span *payload) {
    static const char *const subselect_names[] = {"SUBSELECT", "subselect"};
    int found;

    value = carbon_trim_span(value);
    if (carbon_span_starts_with(value, '[')) {
        found = carbon_extract_subselect_payload(value, payload);
        return found > 0 ? 1 : -1;
    }
    if (!carbon_span_starts_with(value, '{')) {
        return -1;
    }

    found = carbon_object_get_any(value, subselect_names, 2, payload);
    if (found < 0) {
        return -1;
    }
    if (found > 0) {
        return carbon_span_starts_with(*payload, '{') ? 1 : -1;
    }

    *payload = value;
    return 1;
}

static char *carbon_copy_identifier_from_span(carbon_json_span value, int allow_wildcard) {
    char *identifier = carbon_span_string_copy(value);

    if (identifier == NULL
        || !carbon_identifier_valid(identifier)
        || (!allow_wildcard && strcmp(identifier, "*") == 0)) {
        free(identifier);
        return NULL;
    }
    return identifier;
}

static char *carbon_infer_exists_inner_column(carbon_json_span payload, carbon_json_span provided) {
    static const char *const select_names[] = {"SELECT", "select"};
    carbon_json_span select;
    carbon_json_span first;
    int found;

    if (provided.start != NULL) {
        return carbon_copy_identifier_from_span(provided, 0);
    }

    found = carbon_object_get_any(payload, select_names, 2, &select);
    if (found <= 0 || !carbon_span_starts_with(select, '[')
        || carbon_array_get(select, 0, &first) != 1
        || !carbon_span_starts_with(first, '"')) {
        return NULL;
    }
    return carbon_copy_identifier_from_span(first, 0);
}

static carbon_status carbon_append_exists_spec(
        carbon_compile_state *state,
        const char *operator_sql,
        carbon_json_span spec,
        carbon_string_builder *sql) {
    carbon_json_span outer_span;
    carbon_json_span payload_raw;
    carbon_json_span payload;
    carbon_json_span inner_span = {0};
    char *outer_column = NULL;
    char *inner_column = NULL;
    carbon_string_builder correlation = {0};
    carbon_status status = CARBON_STATUS_OK;
    size_t count;

    if (!carbon_span_starts_with(spec, '[')
        || !carbon_array_count(spec, &count)
        || count < 2
        || count > 3
        || carbon_array_get(spec, 0, &outer_span) != 1
        || carbon_array_get(spec, 1, &payload_raw) != 1) {
        return CARBON_STATUS_INVALID_QUERY;
    }
    if (count == 3 && carbon_array_get(spec, 2, &inner_span) != 1) {
        return CARBON_STATUS_INVALID_QUERY;
    }

    outer_column = carbon_copy_identifier_from_span(outer_span, 0);
    if (outer_column == NULL || !carbon_identifier_is_dotted(outer_column)) {
        status = CARBON_STATUS_INVALID_QUERY;
        goto cleanup;
    }
    if (carbon_resolve_subselect_payload(payload_raw, &payload) != 1) {
        status = CARBON_STATUS_INVALID_QUERY;
        goto cleanup;
    }
    inner_column = carbon_infer_exists_inner_column(payload, inner_span);
    if (inner_column == NULL) {
        status = CARBON_STATUS_INVALID_QUERY;
        goto cleanup;
    }
    status = carbon_schema_validate_reference_identifier(state, outer_column);
    if (status != CARBON_STATUS_OK) {
        goto cleanup;
    }
    status = carbon_schema_validate_reference_in_payload_scope(state, payload, inner_column);
    if (status != CARBON_STATUS_OK) {
        goto cleanup;
    }

    if (!carbon_builder_append_char(&correlation, '(')
        || !carbon_builder_append(&correlation, inner_column)
        || !carbon_builder_append(&correlation, ") = ")
        || !carbon_builder_append(&correlation, outer_column)
        || !carbon_builder_append(sql, operator_sql)
        || !carbon_builder_append_char(sql, ' ')
        || !carbon_builder_append_char(sql, '(')) {
        status = CARBON_STATUS_OUT_OF_MEMORY;
        goto cleanup;
    }

    status = carbon_compile_select_statement(
            state,
            payload,
            1,
            sql,
            correlation.data == NULL ? "" : correlation.data,
            NULL);
    if (status != CARBON_STATUS_OK) {
        goto cleanup;
    }
    if (!carbon_builder_append_char(sql, ')')) {
        status = CARBON_STATUS_OUT_OF_MEMORY;
    }

cleanup:
    carbon_builder_free(&correlation);
    free(outer_column);
    free(inner_column);
    return status;
}

static carbon_status carbon_build_exists_operator(
        carbon_compile_state *state,
        const char *operator_sql,
        carbon_json_span operands,
        carbon_string_builder *sql) {
    carbon_json_span first;
    const char *cursor = NULL;
    carbon_json_span item;
    int next;
    int wrote = 0;
    size_t count;

    if (!carbon_span_starts_with(operands, '[') || !carbon_array_count(operands, &count) || count == 0) {
        return CARBON_STATUS_INVALID_QUERY;
    }

    if (carbon_array_get(operands, 0, &first) != 1) {
        return CARBON_STATUS_INVALID_QUERY;
    }
    if (carbon_span_starts_with(first, '"')) {
        return carbon_append_exists_spec(state, operator_sql, operands, sql);
    }

    while ((next = carbon_array_next(operands, &cursor, &item)) == 1) {
        carbon_status status;
        if (wrote && !carbon_builder_append(sql, " AND ")) {
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        status = carbon_append_exists_spec(state, operator_sql, item, sql);
        if (status != CARBON_STATUS_OK) {
            return status;
        }
        wrote = 1;
    }
    return next < 0 || !wrote ? CARBON_STATUS_INVALID_QUERY : CARBON_STATUS_OK;
}

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
        return carbon_append_reference_from_string(state, sql, value);
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

        if (strcmp(token, "SUBSELECT") == 0) {
            carbon_json_span payload;
            if (length != 2 || carbon_array_get(value, 1, &payload) != 1) {
                status = CARBON_STATUS_INVALID_QUERY;
                goto cleanup;
            }
            status = carbon_append_scalar_subselect(state, payload, sql);
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
        if (!carbon_identifier_valid(identifier) || strcmp(identifier, "*") == 0) {
            free(identifier);
            return CARBON_STATUS_INVALID_QUERY;
        }
        {
            carbon_status status = carbon_schema_validate_reference_identifier(state, identifier);
            if (status != CARBON_STATUS_OK) {
                free(identifier);
                return status;
            }
        }
        if (!carbon_builder_append(sql, identifier)) {
            free(identifier);
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        free(identifier);
        return CARBON_STATUS_OK;
    }

    if (carbon_span_starts_with(value, '{')) {
        static const char *const subselect_names[] = {"SUBSELECT", "subselect"};
        carbon_json_span payload;
        int found = carbon_object_get_any(value, subselect_names, 2, &payload);
        if (found < 0) {
            return CARBON_STATUS_INVALID_QUERY;
        }
        if (found > 0) {
            return carbon_append_scalar_subselect(state, payload, sql);
        }
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
    if (strcmp(operator_sql, "EXISTS") == 0 || strcmp(operator_sql, "NOT EXISTS") == 0) {
        return carbon_build_exists_operator(state, operator_sql, operands, sql);
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
        carbon_json_span subselect_payload;
        const char *cursor = NULL;
        carbon_json_span item;
        int next;
        int wrote = 0;
        int subselect_match;

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
        if (!carbon_span_starts_with(values, '[')) {
            return CARBON_STATUS_INVALID_QUERY;
        }

        subselect_match = carbon_extract_subselect_payload(values, &subselect_payload);
        if (subselect_match < 0) {
            return CARBON_STATUS_INVALID_QUERY;
        }
        if (subselect_match > 0) {
            if (!carbon_builder_append_format(sql, " %s ", operator_sql)) {
                return CARBON_STATUS_OUT_OF_MEMORY;
            }
            status = carbon_append_scalar_subselect(state, subselect_payload, sql);
            if (status != CARBON_STATUS_OK) {
                return status;
            }
            return carbon_builder_append(sql, " )") ? CARBON_STATUS_OK : CARBON_STATUS_OUT_OF_MEMORY;
        }

        if (!carbon_builder_append_format(sql, " %s (", operator_sql)) {
            return CARBON_STATUS_OUT_OF_MEMORY;
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
    {
        carbon_status validation_status = carbon_schema_validate_reference_identifier(state, column);
        if (validation_status != CARBON_STATUS_OK) {
            return validation_status;
        }
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

static carbon_status carbon_append_derived_join_target(
        carbon_compile_state *state,
        const char *raw_target,
        carbon_string_builder *sql,
        char **alias,
        const char **error_message) {
    static const char *const subselect_names[] = {"SUBSELECT", "subselect"};
    static const char *const alias_names[] = {"AS", "as"};
    carbon_json_span target;
    carbon_json_span subselect;
    carbon_json_span alias_span;
    char *local_alias = NULL;
    carbon_status status;
    int found_subselect;
    int found_alias;

    *alias = NULL;
    target.start = raw_target;
    target.end = raw_target + strlen(raw_target);
    target = carbon_trim_span(target);
    if (!carbon_span_starts_with(target, '{')) {
        if (error_message != NULL) {
            *error_message = "derived JOIN target must be an object";
        }
        return CARBON_STATUS_INVALID_QUERY;
    }

    found_subselect = carbon_object_get_any(target, subselect_names, 2, &subselect);
    found_alias = carbon_object_get_any(target, alias_names, 2, &alias_span);
    if (found_subselect <= 0 || found_alias <= 0 || !carbon_span_starts_with(subselect, '{')) {
        if (error_message != NULL) {
            *error_message = "derived JOIN target requires SUBSELECT object and AS alias";
        }
        return CARBON_STATUS_INVALID_QUERY;
    }

    local_alias = carbon_span_string_copy(alias_span);
    if (local_alias == NULL || !carbon_identifier_alias_valid(local_alias)) {
        free(local_alias);
        if (error_message != NULL) {
            *error_message = "derived JOIN target has invalid AS alias";
        }
        return CARBON_STATUS_INVALID_QUERY;
    }

    if (!carbon_builder_append_char(sql, '(')) {
        free(local_alias);
        return CARBON_STATUS_OUT_OF_MEMORY;
    }
    status = carbon_compile_select_statement(state, subselect, 1, sql, NULL, error_message);
    if (status != CARBON_STATUS_OK) {
        free(local_alias);
        return status;
    }
    if (!carbon_builder_append(sql, ") AS ")
        || !carbon_append_quoted_table(sql, state->dialect, local_alias)) {
        free(local_alias);
        return CARBON_STATUS_OUT_OF_MEMORY;
    }

    *alias = local_alias;
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
            int is_derived;

            if (raw_target == NULL) {
                free(raw_target);
                free(normalized_join_kind);
                return CARBON_STATUS_INVALID_QUERY;
            }
            is_derived = carbon_join_target_is_derived(raw_target);

            if (!carbon_builder_append_char(sql, ' ')
                || !carbon_builder_append(sql, normalized_join_kind)
                || !carbon_builder_append(sql, " JOIN ")) {
                free(raw_target);
                free(normalized_join_kind);
                return CARBON_STATUS_OUT_OF_MEMORY;
            }

            if (is_derived) {
                status = carbon_append_derived_join_target(state, raw_target, sql, &alias, NULL);
                free(raw_target);
                if (status != CARBON_STATUS_OK) {
                    free(alias);
                    free(normalized_join_kind);
                    return status;
                }
            } else {
                if (!carbon_parse_join_target(raw_target, &table, &alias)) {
                    free(raw_target);
                    free(normalized_join_kind);
                    return CARBON_STATUS_INVALID_QUERY;
                }
                free(raw_target);

                status = carbon_schema_validate_table(state, table);
                if (status != CARBON_STATUS_OK) {
                    free(table);
                    free(alias);
                    free(normalized_join_kind);
                    return status;
                }

                if (!carbon_append_quoted_table(sql, state->dialect, table)) {
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

static carbon_status carbon_append_wrapped_condition_part(
        carbon_string_builder *conditions,
        const char *condition) {
    if (condition == NULL || condition[0] == '\0') {
        return CARBON_STATUS_OK;
    }
    if (conditions->length > 0 && !carbon_builder_append(conditions, " AND ")) {
        return CARBON_STATUS_OUT_OF_MEMORY;
    }
    return carbon_builder_append_wrapped_expression(conditions, condition)
           ? CARBON_STATUS_OK
           : CARBON_STATUS_OUT_OF_MEMORY;
}

static carbon_status carbon_append_postgresql_join_sources(
        carbon_compile_state *state,
        carbon_json_span query,
        carbon_string_builder *sql,
        carbon_string_builder *conditions,
        const char *clause_sql,
        const char *non_inner_error_message,
        const char **error_message) {
    static const char *const join_names[] = {"JOIN", "join"};
    carbon_json_span joins;
    const char *join_cursor = NULL;
    carbon_object_entry join_entry;
    int found = carbon_object_get_any(query, join_names, 2, &joins);
    int next;
    int wrote_source = 0;

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
        if (strcmp(normalized_join_kind, "INNER") != 0) {
            if (error_message != NULL) {
                *error_message = non_inner_error_message;
            }
            free(normalized_join_kind);
            return CARBON_STATUS_UNSUPPORTED_QUERY;
        }

        while ((target_next = carbon_object_next(join_entry.value, &target_cursor, &target_entry)) == 1) {
            char *raw_target = carbon_span_string_copy(target_entry.key);
            char *table = NULL;
            char *alias = NULL;
            carbon_string_builder on_clause = {0};
            carbon_status status = CARBON_STATUS_OK;

            if (raw_target == NULL) {
                free(raw_target);
                free(normalized_join_kind);
                return CARBON_STATUS_INVALID_QUERY;
            }
            if (carbon_join_target_is_derived(raw_target)) {
                if (error_message != NULL) {
                    *error_message = "PostgreSQL joined writes do not support derived table joins yet";
                }
                free(raw_target);
                free(normalized_join_kind);
                return CARBON_STATUS_UNSUPPORTED_QUERY;
            }
            if (!carbon_parse_join_target(raw_target, &table, &alias)) {
                free(raw_target);
                free(normalized_join_kind);
                return CARBON_STATUS_INVALID_QUERY;
            }
            free(raw_target);

            status = carbon_schema_validate_table(state, table);
            if (status != CARBON_STATUS_OK) {
                free(table);
                free(alias);
                free(normalized_join_kind);
                return status;
            }

            if (!wrote_source) {
                if (!carbon_builder_append(sql, clause_sql)) {
                    free(table);
                    free(alias);
                    free(normalized_join_kind);
                    return CARBON_STATUS_OUT_OF_MEMORY;
                }
            } else if (!carbon_builder_append(sql, ", ")) {
                free(table);
                free(alias);
                free(normalized_join_kind);
                return CARBON_STATUS_OUT_OF_MEMORY;
            }
            if (!carbon_append_quoted_table(sql, state->dialect, table)) {
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
            wrote_source = 1;

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
            status = carbon_append_wrapped_condition_part(conditions, on_clause.data);
            carbon_builder_free(&on_clause);
            if (status != CARBON_STATUS_OK) {
                free(normalized_join_kind);
                return status;
            }
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

static char *carbon_trim_write_column(
        carbon_compile_state *state,
        const char *table,
        const char *column) {
    size_t table_length;
    char *short_column;

    if (table == NULL || column == NULL || column[0] == '\0') {
        return NULL;
    }

    table_length = strlen(table);
    if (strncmp(column, table, table_length) == 0 && column[table_length] == '.') {
        column += table_length + 1;
        if (strchr(column, '.') != NULL) {
            return NULL;
        }
    } else if (strchr(column, '.') != NULL) {
        return NULL;
    }

    short_column = carbon_strndup_local(column, strlen(column));
    if (short_column == NULL || !carbon_identifier_alias_valid(short_column)) {
        free(short_column);
        return NULL;
    }
    if (carbon_schema_validate_column(state, table, short_column) != CARBON_STATUS_OK) {
        free(short_column);
        return NULL;
    }
    return short_column;
}

static carbon_status carbon_append_write_value(
        carbon_compile_state *state,
        carbon_json_span value,
        carbon_string_builder *sql) {
    value = carbon_trim_span(value);
    if (carbon_span_starts_with(value, '[')) {
        return carbon_append_expression(state, value, sql);
    }
    if (carbon_span_is_scalar_json(value)) {
        return carbon_append_param(state, value, sql);
    }
    return CARBON_STATUS_INVALID_QUERY;
}

static int carbon_is_root_post_metadata_key(const char *key) {
    static const char *const metadata_keys[] = {
            "FROM", "from", "table",
            "dialect", "DIALECT",
            "DB", "db",
            "SELECT", "select",
            "UPDATE", "update",
            "DELETE", "delete",
            "WHERE", "where",
            "JOIN", "join",
            "ORDER", "order",
            "GROUP_BY", "group_by",
            "HAVING", "having",
            "INDEX_HINTS", "index_hints",
            "PAGINATION", "pagination",
            "INSERT", "insert",
            "REPLACE", "replace",
            "dataInsertMultipleRows",
            "LIMIT", "limit",
            "PAGE", "page",
            "GET", "get",
            "POST", "post",
            "PUT", "put",
            "cacheResults",
            "skipReactBootstrap",
            "fetchDependencies",
            "debug",
            "success",
            "error"
    };
    size_t index;

    if (key == NULL) {
        return 0;
    }
    for (index = 0; index < sizeof(metadata_keys) / sizeof(metadata_keys[0]); ++index) {
        if (strcmp(key, metadata_keys[index]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int carbon_query_has_read_controls(carbon_json_span query) {
    static const char *const read_control_names[] = {
            "SELECT", "select",
            "WHERE", "where",
            "JOIN", "join",
            "GROUP_BY", "group_by",
            "HAVING", "having",
            "PAGINATION", "pagination",
            "ORDER", "order",
            "LIMIT", "limit",
            "PAGE", "page"
    };
    carbon_json_span unused;
    size_t index;

    for (index = 0; index < sizeof(read_control_names) / sizeof(read_control_names[0]); ++index) {
        int found = carbon_object_get_property(query, read_control_names[index], &unused);
        if (found != 0) {
            return found;
        }
    }
    return 0;
}

static int carbon_query_has_root_post_columns(carbon_json_span query) {
    const char *cursor = NULL;
    carbon_object_entry entry;
    int next;

    while ((next = carbon_object_next(query, &cursor, &entry)) == 1) {
        char *key = carbon_span_string_copy(entry.key);

        if (key == NULL) {
            return -1;
        }
        if (!carbon_is_root_post_metadata_key(key)) {
            free(key);
            return 1;
        }
        free(key);
    }
    return next < 0 ? -1 : 0;
}

static void carbon_write_columns_free(carbon_write_columns *columns) {
    size_t index;

    if (columns == NULL) {
        return;
    }
    for (index = 0; index < columns->count; ++index) {
        free(columns->keys[index]);
        free(columns->short_columns[index]);
    }
    free(columns->keys);
    free(columns->short_columns);
    columns->keys = NULL;
    columns->short_columns = NULL;
    columns->count = 0;
}

static int carbon_write_columns_find_short_index(
        const carbon_write_columns *columns,
        const char *short_column,
        size_t *found_index) {
    size_t index;

    if (columns == NULL || short_column == NULL) {
        return 0;
    }
    for (index = 0; index < columns->count; ++index) {
        if (strcmp(columns->short_columns[index], short_column) == 0) {
            if (found_index != NULL) {
                *found_index = index;
            }
            return 1;
        }
    }
    return 0;
}

static int carbon_write_columns_find_short(
        const carbon_write_columns *columns,
        const char *short_column) {
    size_t unused;
    return carbon_write_columns_find_short_index(columns, short_column, &unused);
}

static carbon_status carbon_write_columns_add(
        carbon_write_columns *columns,
        char *key,
        char *short_column) {
    char **next_keys;
    char **next_short_columns;

    if (columns == NULL || key == NULL || short_column == NULL) {
        free(key);
        free(short_column);
        return CARBON_STATUS_INVALID_QUERY;
    }
    if (carbon_write_columns_find_short(columns, short_column)) {
        free(key);
        free(short_column);
        return CARBON_STATUS_INVALID_QUERY;
    }

    next_keys = (char **) realloc(columns->keys, (columns->count + 1) * sizeof(char *));
    if (next_keys == NULL) {
        free(key);
        free(short_column);
        return CARBON_STATUS_OUT_OF_MEMORY;
    }
    columns->keys = next_keys;

    next_short_columns = (char **) realloc(
            columns->short_columns,
            (columns->count + 1) * sizeof(char *));
    if (next_short_columns == NULL) {
        free(key);
        free(short_column);
        return CARBON_STATUS_OUT_OF_MEMORY;
    }
    columns->short_columns = next_short_columns;
    columns->keys[columns->count] = key;
    columns->short_columns[columns->count] = short_column;
    ++columns->count;
    return CARBON_STATUS_OK;
}

static void carbon_write_columns_swap(
        carbon_write_columns *columns,
        size_t left,
        size_t right) {
    char *key;
    char *short_column;

    if (columns == NULL || left == right) {
        return;
    }

    key = columns->keys[left];
    columns->keys[left] = columns->keys[right];
    columns->keys[right] = key;

    short_column = columns->short_columns[left];
    columns->short_columns[left] = columns->short_columns[right];
    columns->short_columns[right] = short_column;
}

static carbon_status carbon_schema_table_columns_span(
        const carbon_compile_state *state,
        const char *table,
        carbon_json_span *columns,
        int *has_columns) {
    static const char *const column_names[] = {"COLUMNS", "columns"};
    carbon_json_span definition;
    int found;

    *has_columns = 0;
    if (!carbon_schema_enabled(state)) {
        return CARBON_STATUS_OK;
    }

    found = carbon_schema_find_table(state->schema, table, &definition);
    if (found < 0) {
        return CARBON_STATUS_INVALID_JSON;
    }
    if (found == 0) {
        return CARBON_STATUS_INVALID_QUERY;
    }

    definition = carbon_trim_span(definition);
    if (carbon_span_starts_with(definition, '[')) {
        *columns = definition;
        *has_columns = 1;
        return CARBON_STATUS_OK;
    }
    if (!carbon_span_starts_with(definition, '{')) {
        return CARBON_STATUS_INVALID_QUERY;
    }

    found = carbon_object_get_any(definition, column_names, 2, columns);
    if (found < 0) {
        return CARBON_STATUS_INVALID_QUERY;
    }
    if (found == 0) {
        return CARBON_STATUS_OK;
    }

    *columns = carbon_trim_span(*columns);
    if (!carbon_span_starts_with(*columns, '[') && !carbon_span_starts_with(*columns, '{')) {
        return CARBON_STATUS_INVALID_QUERY;
    }
    *has_columns = 1;
    return CARBON_STATUS_OK;
}

static char *carbon_copy_schema_write_column_short(
        carbon_compile_state *state,
        const char *table,
        carbon_json_span span) {
    char *column;
    char *short_column;

    if (span.start == NULL || span.end == NULL) {
        return NULL;
    }
    column = carbon_span_string_copy(span);
    if (column == NULL) {
        return NULL;
    }
    short_column = carbon_trim_write_column(state, table, column);
    free(column);
    return short_column;
}

static carbon_status carbon_schema_declared_write_column_short(
        carbon_compile_state *state,
        const char *table,
        carbon_json_span preferred,
        carbon_json_span fallback,
        char **short_column) {
    *short_column = carbon_copy_schema_write_column_short(state, table, preferred);
    if (*short_column != NULL) {
        return CARBON_STATUS_OK;
    }

    *short_column = carbon_copy_schema_write_column_short(state, table, fallback);
    return *short_column == NULL ? CARBON_STATUS_INVALID_QUERY : CARBON_STATUS_OK;
}

static carbon_status carbon_reorder_write_columns_from_schema(
        carbon_compile_state *state,
        const char *table,
        carbon_write_columns *columns) {
    carbon_json_span schema_columns;
    int has_columns = 0;
    size_t write_position = 0;
    carbon_status status;

    if (columns == NULL || columns->count < 2 || !carbon_schema_enabled(state)) {
        return CARBON_STATUS_OK;
    }

    status = carbon_schema_table_columns_span(state, table, &schema_columns, &has_columns);
    if (status != CARBON_STATUS_OK || !has_columns) {
        return status;
    }

    if (carbon_span_starts_with(schema_columns, '[')) {
        const char *cursor = NULL;
        carbon_json_span item;
        int next;

        while ((next = carbon_array_next(schema_columns, &cursor, &item)) == 1) {
            char *short_column = NULL;
            size_t found_index;

            status = carbon_schema_declared_write_column_short(
                    state,
                    table,
                    item,
                    (carbon_json_span) {0},
                    &short_column);
            if (status != CARBON_STATUS_OK) {
                return status;
            }
            if (carbon_write_columns_find_short_index(columns, short_column, &found_index)
                && found_index >= write_position) {
                carbon_write_columns_swap(columns, write_position, found_index);
                ++write_position;
                if (write_position == columns->count) {
                    free(short_column);
                    return CARBON_STATUS_OK;
                }
            }
            free(short_column);
        }
        return next < 0 || write_position != columns->count
               ? CARBON_STATUS_INVALID_QUERY
               : CARBON_STATUS_OK;
    }

    if (carbon_span_starts_with(schema_columns, '{')) {
        const char *cursor = NULL;
        carbon_object_entry entry;
        int next;

        while ((next = carbon_object_next(schema_columns, &cursor, &entry)) == 1) {
            char *short_column = NULL;
            size_t found_index;

            status = carbon_schema_declared_write_column_short(
                    state,
                    table,
                    entry.value,
                    entry.key,
                    &short_column);
            if (status != CARBON_STATUS_OK) {
                return status;
            }
            if (carbon_write_columns_find_short_index(columns, short_column, &found_index)
                && found_index >= write_position) {
                carbon_write_columns_swap(columns, write_position, found_index);
                ++write_position;
                if (write_position == columns->count) {
                    free(short_column);
                    return CARBON_STATUS_OK;
                }
            }
            free(short_column);
        }
        return next < 0 || write_position != columns->count
               ? CARBON_STATUS_INVALID_QUERY
               : CARBON_STATUS_OK;
    }

    return CARBON_STATUS_INVALID_QUERY;
}

static char *carbon_metadata_build_qualified_column(const char *table, const char *short_column) {
    carbon_string_builder builder = {0};
    char *qualified;

    if (!carbon_builder_append(&builder, table)
        || !carbon_builder_append_char(&builder, '.')
        || !carbon_builder_append(&builder, short_column)) {
        carbon_builder_free(&builder);
        return NULL;
    }

    qualified = builder.data;
    builder.data = NULL;
    builder.length = 0;
    builder.capacity = 0;
    return qualified;
}

static char *carbon_metadata_copy_matching_qualified_column(
        carbon_compile_state *state,
        const char *table,
        const char *candidate,
        const char *short_column) {
    char *candidate_short;
    char *qualified = NULL;

    if (candidate == NULL || strchr(candidate, '.') == NULL) {
        return NULL;
    }

    candidate_short = carbon_trim_write_column(state, table, candidate);
    if (candidate_short != NULL && strcmp(candidate_short, short_column) == 0) {
        qualified = carbon_strndup_local(candidate, strlen(candidate));
    }
    free(candidate_short);
    return qualified;
}

static carbon_status carbon_metadata_add_column_from_spans(
        carbon_compile_state *state,
        const char *table,
        carbon_json_span preferred,
        carbon_json_span fallback,
        carbon_write_columns *columns) {
    char *preferred_column = NULL;
    char *fallback_column = NULL;
    char *short_column = NULL;
    char *qualified_column = NULL;
    carbon_status status;

    if (preferred.start != NULL && preferred.end != NULL) {
        preferred_column = carbon_span_string_copy(preferred);
        if (preferred_column != NULL) {
            short_column = carbon_trim_write_column(state, table, preferred_column);
        }
    }
    if (short_column == NULL && fallback.start != NULL && fallback.end != NULL) {
        fallback_column = carbon_span_string_copy(fallback);
        if (fallback_column != NULL) {
            short_column = carbon_trim_write_column(state, table, fallback_column);
        }
    } else if (fallback.start != NULL && fallback.end != NULL) {
        fallback_column = carbon_span_string_copy(fallback);
    }

    if (short_column == NULL) {
        free(preferred_column);
        free(fallback_column);
        return CARBON_STATUS_INVALID_QUERY;
    }

    qualified_column = carbon_metadata_copy_matching_qualified_column(
            state,
            table,
            preferred_column,
            short_column);
    if (qualified_column == NULL) {
        qualified_column = carbon_metadata_copy_matching_qualified_column(
                state,
                table,
                fallback_column,
                short_column);
    }
    if (qualified_column == NULL) {
        qualified_column = carbon_metadata_build_qualified_column(table, short_column);
    }
    if (qualified_column == NULL) {
        free(preferred_column);
        free(fallback_column);
        free(short_column);
        return CARBON_STATUS_OUT_OF_MEMORY;
    }

    status = carbon_write_columns_add(columns, qualified_column, short_column);
    free(preferred_column);
    free(fallback_column);
    return status;
}

static carbon_status carbon_collect_schema_metadata_columns(
        carbon_compile_state *state,
        const char *table,
        carbon_write_columns *columns) {
    carbon_json_span schema_columns;
    int has_columns = 0;
    carbon_status status = carbon_schema_table_columns_span(state, table, &schema_columns, &has_columns);

    if (status != CARBON_STATUS_OK || !has_columns) {
        return status;
    }

    if (carbon_span_starts_with(schema_columns, '[')) {
        const char *cursor = NULL;
        carbon_json_span item;
        int next;

        while ((next = carbon_array_next(schema_columns, &cursor, &item)) == 1) {
            status = carbon_metadata_add_column_from_spans(
                    state,
                    table,
                    item,
                    (carbon_json_span) {0},
                    columns);
            if (status != CARBON_STATUS_OK) {
                return status;
            }
        }
        return next < 0 ? CARBON_STATUS_INVALID_QUERY : CARBON_STATUS_OK;
    }

    if (carbon_span_starts_with(schema_columns, '{')) {
        const char *cursor = NULL;
        carbon_object_entry entry;
        int next;

        while ((next = carbon_object_next(schema_columns, &cursor, &entry)) == 1) {
            status = carbon_metadata_add_column_from_spans(
                    state,
                    table,
                    entry.value,
                    entry.key,
                    columns);
            if (status != CARBON_STATUS_OK) {
                return status;
            }
        }
        return next < 0 ? CARBON_STATUS_INVALID_QUERY : CARBON_STATUS_OK;
    }

    return CARBON_STATUS_INVALID_QUERY;
}

static carbon_status carbon_schema_primary_columns_span(
        const carbon_compile_state *state,
        const char *table,
        carbon_json_span *primary_columns,
        int *has_primary) {
    static const char *const primary_short_names[] = {"PRIMARY_SHORT", "primary_short"};
    static const char *const primary_names[] = {"PRIMARY", "primary"};
    carbon_json_span definition;
    int found;

    *has_primary = 0;
    if (!carbon_schema_enabled(state)) {
        return CARBON_STATUS_OK;
    }

    found = carbon_schema_find_table(state->schema, table, &definition);
    if (found < 0) {
        return CARBON_STATUS_INVALID_JSON;
    }
    if (found == 0) {
        return CARBON_STATUS_INVALID_QUERY;
    }

    definition = carbon_trim_span(definition);
    if (carbon_span_starts_with(definition, '[')) {
        return CARBON_STATUS_OK;
    }
    if (!carbon_span_starts_with(definition, '{')) {
        return CARBON_STATUS_INVALID_QUERY;
    }

    found = carbon_object_get_any(definition, primary_short_names, 2, primary_columns);
    if (found < 0) {
        return CARBON_STATUS_INVALID_QUERY;
    }
    if (found == 0) {
        found = carbon_object_get_any(definition, primary_names, 2, primary_columns);
        if (found < 0) {
            return CARBON_STATUS_INVALID_QUERY;
        }
    }
    if (found == 0) {
        return CARBON_STATUS_OK;
    }
    if (!carbon_span_starts_with(*primary_columns, '[')) {
        return CARBON_STATUS_INVALID_QUERY;
    }

    *has_primary = 1;
    return CARBON_STATUS_OK;
}

static carbon_status carbon_append_schema_metadata_columns(
        const carbon_write_columns *columns,
        carbon_string_builder *json) {
    size_t index;

    if (!carbon_builder_append(json, "\"columns\":[")) {
        return CARBON_STATUS_OUT_OF_MEMORY;
    }
    for (index = 0; columns != NULL && index < columns->count; ++index) {
        if (index > 0 && !carbon_builder_append_char(json, ',')) {
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        if (!carbon_builder_append(json, "{\"name\":")
            || !carbon_builder_append_json_string(json, columns->short_columns[index])
            || !carbon_builder_append(json, ",\"qualified\":")
            || !carbon_builder_append_json_string(json, columns->keys[index])
            || !carbon_builder_append_char(json, '}')) {
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
    }
    return carbon_builder_append_char(json, ']') ? CARBON_STATUS_OK : CARBON_STATUS_OUT_OF_MEMORY;
}

static carbon_status carbon_append_schema_metadata_primary(
        carbon_compile_state *state,
        const char *table,
        carbon_string_builder *json) {
    carbon_json_span primary_columns;
    int has_primary = 0;
    carbon_status status = carbon_schema_primary_columns_span(state, table, &primary_columns, &has_primary);

    if (status != CARBON_STATUS_OK) {
        return status;
    }
    if (!carbon_builder_append(json, "\"primary\":[")) {
        return CARBON_STATUS_OUT_OF_MEMORY;
    }
    if (has_primary) {
        const char *cursor = NULL;
        carbon_json_span item;
        int next;
        int wrote = 0;

        while ((next = carbon_array_next(primary_columns, &cursor, &item)) == 1) {
            char *column = carbon_span_string_copy(item);
            char *short_column = column == NULL ? NULL : carbon_trim_write_column(state, table, column);

            free(column);
            if (short_column == NULL) {
                return CARBON_STATUS_INVALID_QUERY;
            }
            if (wrote && !carbon_builder_append_char(json, ',')) {
                free(short_column);
                return CARBON_STATUS_OUT_OF_MEMORY;
            }
            if (!carbon_builder_append_json_string(json, short_column)) {
                free(short_column);
                return CARBON_STATUS_OUT_OF_MEMORY;
            }
            free(short_column);
            wrote = 1;
        }
        if (next < 0) {
            return CARBON_STATUS_INVALID_QUERY;
        }
    }
    return carbon_builder_append_char(json, ']') ? CARBON_STATUS_OK : CARBON_STATUS_OUT_OF_MEMORY;
}

static carbon_status carbon_append_schema_metadata_table(
        carbon_compile_state *state,
        const char *table,
        carbon_string_builder *json) {
    carbon_write_columns columns = {0};
    carbon_status status;

    status = carbon_collect_schema_metadata_columns(state, table, &columns);
    if (status != CARBON_STATUS_OK) {
        goto cleanup;
    }

    if (!carbon_builder_append(json, "{\"name\":")
        || !carbon_builder_append_json_string(json, table)
        || !carbon_builder_append_char(json, ',')) {
        status = CARBON_STATUS_OUT_OF_MEMORY;
        goto cleanup;
    }

    status = carbon_append_schema_metadata_columns(&columns, json);
    if (status != CARBON_STATUS_OK) {
        goto cleanup;
    }
    if (!carbon_builder_append_char(json, ',')) {
        status = CARBON_STATUS_OUT_OF_MEMORY;
        goto cleanup;
    }
    status = carbon_append_schema_metadata_primary(state, table, json);
    if (status != CARBON_STATUS_OK) {
        goto cleanup;
    }
    if (!carbon_builder_append_char(json, '}')) {
        status = CARBON_STATUS_OUT_OF_MEMORY;
        goto cleanup;
    }

cleanup:
    carbon_write_columns_free(&columns);
    return status;
}

static carbon_status carbon_collect_write_columns(
        carbon_compile_state *state,
        const char *table,
        carbon_json_span rows,
        int ignore_root_post_metadata,
        carbon_write_columns *columns) {
    const char *cursor = NULL;
    carbon_object_entry entry;
    int next;

    while ((next = carbon_object_next(rows, &cursor, &entry)) == 1) {
        char *key = carbon_span_string_copy(entry.key);
        char *short_column;
        carbon_status status;

        if (key != NULL && ignore_root_post_metadata && carbon_is_root_post_metadata_key(key)) {
            free(key);
            continue;
        }
        short_column = key == NULL ? NULL : carbon_trim_write_column(state, table, key);
        if (short_column == NULL) {
            free(key);
            return CARBON_STATUS_INVALID_QUERY;
        }
        status = carbon_write_columns_add(columns, key, short_column);
        if (status != CARBON_STATUS_OK) {
            return status;
        }
    }

    if (next < 0 || columns->count == 0) {
        return CARBON_STATUS_INVALID_QUERY;
    }
    return CARBON_STATUS_OK;
}

static carbon_status carbon_collect_write_column_array(
        carbon_compile_state *state,
        const char *table,
        carbon_json_span column_list,
        int allow_empty,
        carbon_write_columns *columns) {
    const char *cursor = NULL;
    carbon_json_span item;
    int next;

    if (!carbon_span_starts_with(column_list, '[')) {
        return CARBON_STATUS_INVALID_QUERY;
    }

    while ((next = carbon_array_next(column_list, &cursor, &item)) == 1) {
        char *key = carbon_span_string_copy(item);
        char *short_column = key == NULL ? NULL : carbon_trim_write_column(state, table, key);
        carbon_status status;

        if (short_column == NULL) {
            free(key);
            return CARBON_STATUS_INVALID_QUERY;
        }
        status = carbon_write_columns_add(columns, key, short_column);
        if (status != CARBON_STATUS_OK) {
            return status;
        }
    }

    if (next < 0 || (!allow_empty && columns->count == 0)) {
        return CARBON_STATUS_INVALID_QUERY;
    }
    return CARBON_STATUS_OK;
}

static carbon_status carbon_find_write_row_value(
        carbon_compile_state *state,
        const char *table,
        carbon_json_span row,
        const char *key,
        const char *short_column,
        carbon_json_span *value,
        int *found) {
    const char *cursor = NULL;
    carbon_object_entry entry;
    int next;
    int matched = 0;

    *found = 0;
    next = carbon_object_get_property(row, key, value);
    if (next < 0) {
        return CARBON_STATUS_INVALID_QUERY;
    }
    if (next > 0) {
        *found = 1;
        return CARBON_STATUS_OK;
    }

    while ((next = carbon_object_next(row, &cursor, &entry)) == 1) {
        char *candidate_key = carbon_span_string_copy(entry.key);
        char *candidate_short = candidate_key == NULL
                                ? NULL
                                : carbon_trim_write_column(state, table, candidate_key);

        free(candidate_key);
        if (candidate_short == NULL) {
            continue;
        }
        if (strcmp(candidate_short, short_column) == 0) {
            free(candidate_short);
            if (matched) {
                return CARBON_STATUS_INVALID_QUERY;
            }
            *value = entry.value;
            matched = 1;
            continue;
        }
        free(candidate_short);
    }
    if (next < 0) {
        return CARBON_STATUS_INVALID_QUERY;
    }

    *found = matched;
    return CARBON_STATUS_OK;
}

static carbon_status carbon_append_write_column_list(
        carbon_compile_state *state,
        const carbon_write_columns *columns,
        carbon_string_builder *sql) {
    size_t index;

    if (columns == NULL || columns->count == 0) {
        return CARBON_STATUS_INVALID_QUERY;
    }
    for (index = 0; index < columns->count; ++index) {
        if (index > 0 && !carbon_builder_append(sql, ", ")) {
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        if (!carbon_append_quoted_table(sql, state->dialect, columns->short_columns[index])) {
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
    }
    return CARBON_STATUS_OK;
}

static carbon_status carbon_append_write_row_values(
        carbon_compile_state *state,
        const char *table,
        carbon_json_span row,
        const carbon_write_columns *columns,
        carbon_string_builder *sql) {
    size_t index;

    if (columns == NULL || columns->count == 0 || !carbon_span_starts_with(row, '{')) {
        return CARBON_STATUS_INVALID_QUERY;
    }
    for (index = 0; index < columns->count; ++index) {
        carbon_json_span value;
        carbon_status status;
        int found;

        if (index > 0 && !carbon_builder_append(sql, ", ")) {
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        status = carbon_find_write_row_value(
                state,
                table,
                row,
                columns->keys[index],
                columns->short_columns[index],
                &value,
                &found);
        if (status != CARBON_STATUS_OK) {
            return status;
        }
        if (found == 0) {
            value.start = "null";
            value.end = value.start + 4;
        }
        status = carbon_append_write_value(state, value, sql);
        if (status != CARBON_STATUS_OK) {
            return status;
        }
    }
    return CARBON_STATUS_OK;
}

static carbon_status carbon_append_write_values(
        carbon_compile_state *state,
        const char *table,
        carbon_json_span rows,
        const carbon_write_columns *columns,
        carbon_string_builder *sql) {
    if (carbon_span_starts_with(rows, '{')) {
        carbon_status status;

        if (!carbon_builder_append_char(sql, '(')) {
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        status = carbon_append_write_row_values(state, table, rows, columns, sql);
        if (status != CARBON_STATUS_OK) {
            return status;
        }
        return carbon_builder_append_char(sql, ')') ? CARBON_STATUS_OK : CARBON_STATUS_OUT_OF_MEMORY;
    }

    if (carbon_span_starts_with(rows, '[')) {
        const char *cursor = NULL;
        carbon_json_span row;
        int next;
        int wrote = 0;

        while ((next = carbon_array_next(rows, &cursor, &row)) == 1) {
            carbon_status status;

            if (!carbon_span_starts_with(row, '{')) {
                return CARBON_STATUS_INVALID_QUERY;
            }
            if (wrote && !carbon_builder_append(sql, ", ")) {
                return CARBON_STATUS_OUT_OF_MEMORY;
            }
            if (!carbon_builder_append_char(sql, '(')) {
                return CARBON_STATUS_OUT_OF_MEMORY;
            }
            status = carbon_append_write_row_values(state, table, row, columns, sql);
            if (status != CARBON_STATUS_OK) {
                return status;
            }
            if (!carbon_builder_append_char(sql, ')')) {
                return CARBON_STATUS_OUT_OF_MEMORY;
            }
            wrote = 1;
        }
        return next < 0 || !wrote ? CARBON_STATUS_INVALID_QUERY : CARBON_STATUS_OK;
    }

    return CARBON_STATUS_INVALID_QUERY;
}

static carbon_status carbon_append_mysql_upsert_update(
        carbon_compile_state *state,
        const char *table,
        carbon_json_span update_columns,
        carbon_string_builder *sql) {
    carbon_write_columns columns = {0};
    size_t index;
    int wrote = 0;
    carbon_status status;

    if (!carbon_span_starts_with(update_columns, '[')) {
        return CARBON_STATUS_INVALID_QUERY;
    }

    status = carbon_collect_write_column_array(state, table, update_columns, 0, &columns);
    if (status != CARBON_STATUS_OK) {
        goto cleanup;
    }
    status = carbon_reorder_write_columns_from_schema(state, table, &columns);
    if (status != CARBON_STATUS_OK) {
        goto cleanup;
    }

    if (!carbon_builder_append(sql, " ON DUPLICATE KEY UPDATE ")) {
        status = CARBON_STATUS_OUT_OF_MEMORY;
        goto cleanup;
    }
    for (index = 0; index < columns.count; ++index) {
        if (wrote && !carbon_builder_append(sql, ", ")) {
            status = CARBON_STATUS_OUT_OF_MEMORY;
            goto cleanup;
        }
        if (!carbon_append_quoted_table(sql, state->dialect, columns.short_columns[index])
            || !carbon_builder_append(sql, " = VALUES(")
            || !carbon_append_quoted_table(sql, state->dialect, columns.short_columns[index])
            || !carbon_builder_append_char(sql, ')')) {
            status = CARBON_STATUS_OUT_OF_MEMORY;
            goto cleanup;
        }
        wrote = 1;
    }

    status = wrote ? CARBON_STATUS_OK : CARBON_STATUS_INVALID_QUERY;

cleanup:
    carbon_write_columns_free(&columns);
    return status;
}

static carbon_status carbon_append_schema_primary_columns(
        carbon_compile_state *state,
        const char *table,
        carbon_string_builder *sql,
        int *wrote_columns) {
    static const char *const primary_short_names[] = {"PRIMARY_SHORT", "primary_short"};
    static const char *const primary_names[] = {"PRIMARY", "primary"};
    carbon_json_span definition;
    carbon_json_span primary_columns;
    const char *cursor = NULL;
    carbon_json_span item;
    int found;
    int next;
    int wrote = 0;

    if (wrote_columns != NULL) {
        *wrote_columns = 0;
    }
    if (!carbon_schema_enabled(state)) {
        return CARBON_STATUS_UNSUPPORTED_QUERY;
    }

    found = carbon_schema_find_table(state->schema, table, &definition);
    if (found < 0) {
        return CARBON_STATUS_INVALID_JSON;
    }
    if (found == 0 || !carbon_span_starts_with(definition, '{')) {
        return CARBON_STATUS_UNSUPPORTED_QUERY;
    }

    found = carbon_object_get_any(definition, primary_short_names, 2, &primary_columns);
    if (found < 0) {
        return CARBON_STATUS_INVALID_JSON;
    }
    if (found == 0) {
        found = carbon_object_get_any(definition, primary_names, 2, &primary_columns);
        if (found < 0) {
            return CARBON_STATUS_INVALID_JSON;
        }
    }
    if (found == 0) {
        return CARBON_STATUS_UNSUPPORTED_QUERY;
    }
    if (!carbon_span_starts_with(primary_columns, '[')) {
        return CARBON_STATUS_INVALID_QUERY;
    }

    while ((next = carbon_array_next(primary_columns, &cursor, &item)) == 1) {
        char *column = carbon_span_string_copy(item);
        char *short_column = column == NULL ? NULL : carbon_trim_write_column(state, table, column);

        free(column);
        if (short_column == NULL) {
            return CARBON_STATUS_INVALID_QUERY;
        }
        if (wrote && !carbon_builder_append(sql, ", ")) {
            free(short_column);
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        if (!carbon_append_quoted_table(sql, state->dialect, short_column)) {
            free(short_column);
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        free(short_column);
        wrote = 1;
    }
    if (next < 0) {
        return CARBON_STATUS_INVALID_QUERY;
    }
    if (!wrote) {
        return CARBON_STATUS_UNSUPPORTED_QUERY;
    }
    if (wrote_columns != NULL) {
        *wrote_columns = wrote;
    }
    return CARBON_STATUS_OK;
}

static carbon_status carbon_append_postgresql_upsert_update(
        carbon_compile_state *state,
        const char *table,
        carbon_json_span update_columns,
        carbon_string_builder *sql,
        const char **error_message) {
    carbon_write_columns columns = {0};
    size_t index;
    int wrote = 0;
    int wrote_conflict = 0;
    carbon_status status;

    if (!carbon_span_starts_with(update_columns, '[')) {
        return CARBON_STATUS_INVALID_QUERY;
    }
    status = carbon_collect_write_column_array(state, table, update_columns, 1, &columns);
    if (status != CARBON_STATUS_OK) {
        goto cleanup;
    }
    status = carbon_reorder_write_columns_from_schema(state, table, &columns);
    if (status != CARBON_STATUS_OK) {
        goto cleanup;
    }
    if (!carbon_builder_append(sql, " ON CONFLICT (")) {
        status = CARBON_STATUS_OUT_OF_MEMORY;
        goto cleanup;
    }

    status = carbon_append_schema_primary_columns(state, table, sql, &wrote_conflict);
    if (status != CARBON_STATUS_OK) {
        if (status == CARBON_STATUS_UNSUPPORTED_QUERY && error_message != NULL) {
            *error_message = "PostgreSQL ON CONFLICT support requires primary key metadata";
        }
        goto cleanup;
    }
    if (!wrote_conflict || !carbon_builder_append(sql, ") ")) {
        status = wrote_conflict ? CARBON_STATUS_OUT_OF_MEMORY : CARBON_STATUS_UNSUPPORTED_QUERY;
        goto cleanup;
    }

    for (index = 0; index < columns.count; ++index) {
        if (!wrote) {
            if (!carbon_builder_append(sql, "DO UPDATE SET ")) {
                status = CARBON_STATUS_OUT_OF_MEMORY;
                goto cleanup;
            }
        } else if (!carbon_builder_append(sql, ", ")) {
            status = CARBON_STATUS_OUT_OF_MEMORY;
            goto cleanup;
        }
        if (!carbon_append_quoted_table(sql, state->dialect, columns.short_columns[index])
            || !carbon_builder_append(sql, " = EXCLUDED.")
            || !carbon_append_quoted_table(sql, state->dialect, columns.short_columns[index])) {
            status = CARBON_STATUS_OUT_OF_MEMORY;
            goto cleanup;
        }
        wrote = 1;
    }
    if (!wrote && !carbon_builder_append(sql, "DO NOTHING")) {
        status = CARBON_STATUS_OUT_OF_MEMORY;
        goto cleanup;
    }
    status = CARBON_STATUS_OK;

cleanup:
    carbon_write_columns_free(&columns);
    return status;
}

static carbon_status carbon_compile_insert_statement(
        carbon_compile_state *state,
        carbon_json_span query,
        carbon_string_builder *sql,
        int allow_root_post_row,
        const char **error_message) {
    static const char *const insert_names[] = {"INSERT", "insert"};
    static const char *const replace_names[] = {"REPLACE", "replace"};
    static const char *const update_names[] = {"UPDATE", "update"};
    static const char *const multi_row_names[] = {"dataInsertMultipleRows"};
    carbon_json_span insert_rows;
    carbon_json_span first_insert_row;
    carbon_json_span update_columns;
    char *table = NULL;
    carbon_write_columns columns = {0};
    carbon_query_scope scope;
    const carbon_query_scope *previous_scope = state->scope;
    const char *verb = "INSERT";
    int found_replace;
    int found_insert;
    int found_multi_row;
    int found_update;
    int using_multi_row_key = 0;
    int using_root_post_row = 0;
    carbon_status status;

    status = carbon_copy_query_table(state, query, &table, error_message);
    if (status != CARBON_STATUS_OK) {
        return status;
    }
    scope.base_table = table;
    scope.query = query;
    scope.parent = previous_scope;
    state->scope = &scope;

    found_replace = carbon_object_get_any(query, replace_names, 2, &insert_rows);
    if (found_replace < 0) {
        status = CARBON_STATUS_INVALID_JSON;
        goto cleanup;
    }
    if (found_replace > 0) {
        verb = "REPLACE";
        if (state->dialect == CARBON_DIALECT_POSTGRESQL) {
            status = CARBON_STATUS_UNSUPPORTED_QUERY;
            goto cleanup;
        }
    } else {
        found_insert = carbon_object_get_any(query, insert_names, 2, &insert_rows);
        if (found_insert < 0) {
            status = CARBON_STATUS_INVALID_JSON;
            goto cleanup;
        }
        if (found_insert == 0) {
            found_multi_row = carbon_object_get_any(query, multi_row_names, 1, &insert_rows);
            if (found_multi_row < 0) {
                status = CARBON_STATUS_INVALID_JSON;
                goto cleanup;
            }
            if (found_multi_row == 0) {
                if (allow_root_post_row) {
                    insert_rows = query;
                    using_root_post_row = 1;
                } else {
                    status = CARBON_STATUS_INVALID_QUERY;
                    goto cleanup;
                }
            } else {
                using_multi_row_key = 1;
            }
        }
    }

    if (using_multi_row_key && !carbon_span_starts_with(insert_rows, '[')) {
        status = CARBON_STATUS_INVALID_QUERY;
        goto cleanup;
    }

    if (carbon_span_starts_with(insert_rows, '{')) {
        first_insert_row = insert_rows;
    } else if (carbon_span_starts_with(insert_rows, '[')) {
        if (carbon_array_get(insert_rows, 0, &first_insert_row) != 1
            || !carbon_span_starts_with(first_insert_row, '{')) {
            status = CARBON_STATUS_INVALID_QUERY;
            goto cleanup;
        }
    } else {
        status = CARBON_STATUS_INVALID_QUERY;
        goto cleanup;
    }
    status = carbon_collect_write_columns(state, table, first_insert_row, using_root_post_row, &columns);
    if (status != CARBON_STATUS_OK) {
        goto cleanup;
    }
    status = carbon_reorder_write_columns_from_schema(state, table, &columns);
    if (status != CARBON_STATUS_OK) {
        goto cleanup;
    }

    if (!carbon_builder_append(sql, verb)
        || !carbon_builder_append(sql, " INTO ")
        || !carbon_append_quoted_table(sql, state->dialect, table)
        || !carbon_builder_append(sql, " (")) {
        status = CARBON_STATUS_OUT_OF_MEMORY;
        goto cleanup;
    }
    status = carbon_append_write_column_list(state, &columns, sql);
    if (status != CARBON_STATUS_OK) {
        goto cleanup;
    }
    if (!carbon_builder_append(sql, ") VALUES ")) {
        status = CARBON_STATUS_OUT_OF_MEMORY;
        goto cleanup;
    }
    status = carbon_append_write_values(state, table, insert_rows, &columns, sql);
    if (status != CARBON_STATUS_OK) {
        goto cleanup;
    }

    found_update = carbon_object_get_any(query, update_names, 2, &update_columns);
    if (found_update < 0) {
        status = CARBON_STATUS_INVALID_JSON;
        goto cleanup;
    }
    if (found_update > 0) {
        status = state->dialect == CARBON_DIALECT_MYSQL
                 ? carbon_append_mysql_upsert_update(state, table, update_columns, sql)
                 : carbon_append_postgresql_upsert_update(state, table, update_columns, sql, error_message);
        if (status != CARBON_STATUS_OK) {
            goto cleanup;
        }
    }

    if (state->dialect == CARBON_DIALECT_POSTGRESQL
        && !carbon_builder_append(sql, " RETURNING *")) {
        status = CARBON_STATUS_OUT_OF_MEMORY;
        goto cleanup;
    }

    status = CARBON_STATUS_OK;

cleanup:
    state->scope = previous_scope;
    carbon_write_columns_free(&columns);
    free(table);
    return status;
}

static carbon_status carbon_compile_update_statement(
        carbon_compile_state *state,
        carbon_json_span query,
        carbon_string_builder *sql,
        const char **error_message) {
    static const char *const update_names[] = {"UPDATE", "update"};
    static const char *const where_names[] = {"WHERE", "where"};
    static const char *const join_names[] = {"JOIN", "join"};
    carbon_json_span update_rows;
    carbon_json_span where_span;
    carbon_json_span join_span;
    char *table = NULL;
    carbon_write_columns columns = {0};
    carbon_string_builder postgresql_conditions = {0};
    carbon_query_scope scope;
    const carbon_query_scope *previous_scope = state->scope;
    size_t index;
    int wrote = 0;
    int found;
    int has_join;
    int has_pagination = 0;
    carbon_status status;

    status = carbon_copy_query_table(state, query, &table, error_message);
    if (status != CARBON_STATUS_OK) {
        return status;
    }
    scope.base_table = table;
    scope.query = query;
    scope.parent = previous_scope;
    state->scope = &scope;

    found = carbon_object_get_any(query, update_names, 2, &update_rows);
    if (found < 0) {
        status = CARBON_STATUS_INVALID_JSON;
        goto cleanup;
    }
    if (found == 0 || !carbon_span_starts_with(update_rows, '{')) {
        status = CARBON_STATUS_INVALID_QUERY;
        goto cleanup;
    }

    has_join = carbon_object_get_any(query, join_names, 2, &join_span);
    if (has_join < 0) {
        status = CARBON_STATUS_INVALID_JSON;
        goto cleanup;
    }
    if (!carbon_builder_append(sql, "UPDATE ")
        || !carbon_append_quoted_table(sql, state->dialect, table)) {
        status = CARBON_STATUS_OUT_OF_MEMORY;
        goto cleanup;
    }
    if (state->dialect == CARBON_DIALECT_MYSQL) {
        status = carbon_append_join_clauses(state, query, sql);
        if (status != CARBON_STATUS_OK) {
            goto cleanup;
        }
    }
    if (!carbon_builder_append(sql, " SET ")) {
        status = CARBON_STATUS_OUT_OF_MEMORY;
        goto cleanup;
    }

    status = carbon_collect_write_columns(state, table, update_rows, 0, &columns);
    if (status != CARBON_STATUS_OK) {
        goto cleanup;
    }
    status = carbon_reorder_write_columns_from_schema(state, table, &columns);
    if (status != CARBON_STATUS_OK) {
        goto cleanup;
    }

    for (index = 0; index < columns.count; ++index) {
        carbon_json_span value;
        int found_value;

        if (wrote && !carbon_builder_append(sql, ", ")) {
            status = CARBON_STATUS_OUT_OF_MEMORY;
            goto cleanup;
        }
        if (!carbon_append_quoted_table(sql, state->dialect, columns.short_columns[index])
            || !carbon_builder_append(sql, " = ")) {
            status = CARBON_STATUS_OUT_OF_MEMORY;
            goto cleanup;
        }
        status = carbon_find_write_row_value(
                state,
                table,
                update_rows,
                columns.keys[index],
                columns.short_columns[index],
                &value,
                &found_value);
        if (status != CARBON_STATUS_OK || !found_value) {
            status = status == CARBON_STATUS_OK ? CARBON_STATUS_INVALID_QUERY : status;
            goto cleanup;
        }
        status = carbon_append_write_value(state, value, sql);
        if (status != CARBON_STATUS_OK) {
            goto cleanup;
        }
        wrote = 1;
    }
    if (!wrote) {
        status = CARBON_STATUS_INVALID_QUERY;
        goto cleanup;
    }
    if (state->dialect == CARBON_DIALECT_POSTGRESQL && has_join > 0) {
        status = carbon_append_postgresql_join_sources(
                state,
                query,
                sql,
                &postgresql_conditions,
                " FROM ",
                "PostgreSQL UPDATE FROM currently supports INNER joins only",
                error_message);
        if (status != CARBON_STATUS_OK) {
            goto cleanup;
        }
    }

    found = carbon_object_get_any(query, where_names, 2, &where_span);
    if (found < 0) {
        status = CARBON_STATUS_INVALID_JSON;
        goto cleanup;
    }
    if (found > 0) {
        carbon_string_builder where = {0};
        status = carbon_build_where_node(state, where_span, "AND", &where);
        if (status != CARBON_STATUS_OK) {
            carbon_builder_free(&where);
            goto cleanup;
        }
        if (state->dialect == CARBON_DIALECT_POSTGRESQL) {
            status = carbon_append_wrapped_condition_part(&postgresql_conditions, where.data);
        } else if (where.length > 0 && (!carbon_builder_append(sql, " WHERE ")
                                        || !carbon_builder_append(sql, where.data))) {
            status = CARBON_STATUS_OUT_OF_MEMORY;
        }
        if (status != CARBON_STATUS_OK) {
            carbon_builder_free(&where);
            goto cleanup;
        }
        carbon_builder_free(&where);
    }

    if (state->dialect == CARBON_DIALECT_POSTGRESQL
        && postgresql_conditions.length > 0
        && (!carbon_builder_append(sql, " WHERE ")
            || !carbon_builder_append(sql, postgresql_conditions.data))) {
        status = CARBON_STATUS_OUT_OF_MEMORY;
        goto cleanup;
    }

    status = carbon_append_pagination(state, query, sql, &has_pagination);

cleanup:
    state->scope = previous_scope;
    carbon_write_columns_free(&columns);
    carbon_builder_free(&postgresql_conditions);
    free(table);
    return status;
}

static carbon_status carbon_compile_delete_statement(
        carbon_compile_state *state,
        carbon_json_span query,
        carbon_string_builder *sql,
        const char **error_message) {
    static const char *const delete_names[] = {"DELETE", "delete"};
    static const char *const where_names[] = {"WHERE", "where"};
    static const char *const join_names[] = {"JOIN", "join"};
    carbon_json_span delete_span;
    carbon_json_span where_span;
    carbon_json_span join_span;
    char *table = NULL;
    carbon_string_builder postgresql_conditions = {0};
    carbon_query_scope scope;
    const carbon_query_scope *previous_scope = state->scope;
    int found;
    int has_join;
    carbon_status status;

    status = carbon_copy_query_table(state, query, &table, error_message);
    if (status != CARBON_STATUS_OK) {
        return status;
    }
    scope.base_table = table;
    scope.query = query;
    scope.parent = previous_scope;
    state->scope = &scope;

    found = carbon_object_get_any(query, delete_names, 2, &delete_span);
    if (found < 0) {
        status = CARBON_STATUS_INVALID_JSON;
        goto cleanup;
    }
    if (found == 0 || !carbon_span_is_true(delete_span)) {
        status = CARBON_STATUS_INVALID_QUERY;
        goto cleanup;
    }

    has_join = carbon_object_get_any(query, join_names, 2, &join_span);
    if (has_join < 0) {
        status = CARBON_STATUS_INVALID_JSON;
        goto cleanup;
    }
    if (state->dialect == CARBON_DIALECT_MYSQL) {
        if (!carbon_builder_append(sql, "DELETE ")
            || !carbon_append_quoted_table(sql, state->dialect, table)
            || !carbon_builder_append(sql, " FROM ")
            || !carbon_append_quoted_table(sql, state->dialect, table)) {
            status = CARBON_STATUS_OUT_OF_MEMORY;
            goto cleanup;
        }
        status = carbon_append_join_clauses(state, query, sql);
        if (status != CARBON_STATUS_OK) {
            goto cleanup;
        }
    } else {
        if (!carbon_builder_append(sql, "DELETE FROM ")
            || !carbon_append_quoted_table(sql, state->dialect, table)) {
            status = CARBON_STATUS_OUT_OF_MEMORY;
            goto cleanup;
        }
        if (has_join > 0) {
            status = carbon_append_postgresql_join_sources(
                    state,
                    query,
                    sql,
                    &postgresql_conditions,
                    " USING ",
                    "PostgreSQL DELETE USING currently supports INNER joins only",
                    error_message);
            if (status != CARBON_STATUS_OK) {
                goto cleanup;
            }
        }
    }

    found = carbon_object_get_any(query, where_names, 2, &where_span);
    if (found < 0) {
        status = CARBON_STATUS_INVALID_JSON;
        goto cleanup;
    }
    if (found > 0) {
        carbon_string_builder where = {0};
        status = carbon_build_where_node(state, where_span, "AND", &where);
        if (status != CARBON_STATUS_OK) {
            carbon_builder_free(&where);
            goto cleanup;
        }
        if (state->dialect == CARBON_DIALECT_POSTGRESQL) {
            status = carbon_append_wrapped_condition_part(&postgresql_conditions, where.data);
        } else if (where.length > 0 && (!carbon_builder_append(sql, " WHERE ")
                                        || !carbon_builder_append(sql, where.data))) {
            status = CARBON_STATUS_OUT_OF_MEMORY;
        }
        if (status != CARBON_STATUS_OK) {
            carbon_builder_free(&where);
            goto cleanup;
        }
        carbon_builder_free(&where);
    }

    if (state->dialect == CARBON_DIALECT_POSTGRESQL
        && postgresql_conditions.length > 0
        && (!carbon_builder_append(sql, " WHERE ")
            || !carbon_builder_append(sql, postgresql_conditions.data))) {
        status = CARBON_STATUS_OUT_OF_MEMORY;
        goto cleanup;
    }
    status = CARBON_STATUS_OK;

cleanup:
    state->scope = previous_scope;
    carbon_builder_free(&postgresql_conditions);
    free(table);
    return status;
}

static carbon_status carbon_compile_statement(
        carbon_compile_state *state,
        carbon_json_span query,
        carbon_string_builder *sql,
        const char **error_message) {
    static const char *const insert_names[] = {"INSERT", "insert"};
    static const char *const replace_names[] = {"REPLACE", "replace"};
    static const char *const update_names[] = {"UPDATE", "update"};
    static const char *const delete_names[] = {"DELETE", "delete"};
    static const char *const multi_row_names[] = {"dataInsertMultipleRows"};
    carbon_json_span unused;
    carbon_json_span update_span;
    int has_insert = carbon_object_get_any(query, insert_names, 2, &unused);
    int has_replace = carbon_object_get_any(query, replace_names, 2, &unused);
    int has_update = carbon_object_get_any(query, update_names, 2, &update_span);
    int has_delete = carbon_object_get_any(query, delete_names, 2, &unused);
    int has_multi_row = carbon_object_get_any(query, multi_row_names, 1, &unused);
    int has_read_controls = carbon_query_has_read_controls(query);
    int has_root_post_columns = carbon_query_has_root_post_columns(query);

    if (has_insert < 0 || has_replace < 0 || has_update < 0 || has_delete < 0 || has_multi_row < 0
        || has_read_controls < 0 || has_root_post_columns < 0) {
        return CARBON_STATUS_INVALID_JSON;
    }
    if (has_insert > 0 || has_replace > 0 || has_multi_row > 0) {
        return carbon_compile_insert_statement(state, query, sql, 0, error_message);
    }
    if (has_update > 0 && carbon_span_starts_with(update_span, '[') && has_root_post_columns > 0 && has_read_controls == 0) {
        return carbon_compile_insert_statement(state, query, sql, 1, error_message);
    }
    if (has_update > 0) {
        return carbon_compile_update_statement(state, query, sql, error_message);
    }
    if (has_delete > 0) {
        return carbon_compile_delete_statement(state, query, sql, error_message);
    }
    if (has_root_post_columns > 0 && has_read_controls == 0) {
        return carbon_compile_insert_statement(state, query, sql, 1, error_message);
    }
    return carbon_compile_select_statement(state, query, 0, sql, NULL, error_message);
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

const char *carbon_status_code(carbon_status status) {
    switch (status) {
        case CARBON_STATUS_OK:
            return "ok";
        case CARBON_STATUS_INVALID_ARGUMENT:
            return "invalid_argument";
        case CARBON_STATUS_INVALID_JSON:
            return "invalid_json";
        case CARBON_STATUS_INVALID_QUERY:
            return "invalid_query";
        case CARBON_STATUS_UNSUPPORTED_DIALECT:
            return "unsupported_dialect";
        case CARBON_STATUS_UNSUPPORTED_QUERY:
            return "unsupported_query";
        case CARBON_STATUS_OUT_OF_MEMORY:
            return "out_of_memory";
        default:
            return "unknown_status";
    }
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

carbon_status carbon_schema_metadata(
        const char *schema_json,
        size_t schema_json_length,
        carbon_buffer *out,
        carbon_buffer *error) {
    carbon_schema_view schema;
    carbon_compile_state state;
    carbon_string_builder json = {0};
    const char *error_message = NULL;
    const char *cursor = NULL;
    carbon_object_entry entry;
    carbon_status status;
    int next;
    int wrote_table = 0;

    if (out == NULL) {
        if (error != NULL) {
            carbon_buffer_init(error);
            carbon_buffer_set(error, "schema metadata output buffer is required");
        }
        return CARBON_STATUS_INVALID_ARGUMENT;
    }
    if (schema_json == NULL && schema_json_length > 0) {
        carbon_buffer_init(out);
        if (error != NULL) {
            carbon_buffer_init(error);
            carbon_buffer_set(error, "schema_json is required when schema_json_length is greater than zero");
        }
        return CARBON_STATUS_INVALID_ARGUMENT;
    }

    carbon_buffer_init(out);
    if (error != NULL) {
        carbon_buffer_init(error);
    }

    status = carbon_schema_prepare(
            schema_json,
            schema_json == NULL ? 0 : schema_json_length,
            &schema,
            &error_message);
    if (status != CARBON_STATUS_OK) {
        goto fail;
    }

    state.dialect = CARBON_DIALECT_MYSQL;
    state.sql = NULL;
    state.params = NULL;
    state.schema = &schema;
    state.scope = NULL;
    state.param_count = 0;

    if (!carbon_builder_append(&json, "{\"tables\":[")) {
        status = CARBON_STATUS_OUT_OF_MEMORY;
        goto fail;
    }

    if (schema.has_tables) {
        while ((next = carbon_object_next(schema.tables, &cursor, &entry)) == 1) {
            char *table = carbon_span_string_copy(entry.key);

            if (table == NULL
                || !carbon_identifier_valid(table)
                || strcmp(table, "*") == 0
                || (!carbon_span_starts_with(entry.value, '{')
                    && !carbon_span_starts_with(entry.value, '['))) {
                free(table);
                status = CARBON_STATUS_INVALID_QUERY;
                goto fail;
            }

            if (wrote_table && !carbon_builder_append_char(&json, ',')) {
                free(table);
                status = CARBON_STATUS_OUT_OF_MEMORY;
                goto fail;
            }
            status = carbon_append_schema_metadata_table(&state, table, &json);
            free(table);
            if (status != CARBON_STATUS_OK) {
                goto fail;
            }
            wrote_table = 1;
        }
        if (next < 0) {
            status = CARBON_STATUS_INVALID_QUERY;
            goto fail;
        }
    }

    if (!carbon_builder_append(&json, "]}") || !carbon_buffer_take_builder(out, &json)) {
        status = CARBON_STATUS_OUT_OF_MEMORY;
        goto fail;
    }
    if (error != NULL) {
        carbon_buffer_set(error, "");
    }
    return CARBON_STATUS_OK;

fail:
    carbon_builder_free(&json);
    if (error != NULL) {
        carbon_buffer_set(error, error_message == NULL ? carbon_status_message(status) : error_message);
    }
    carbon_buffer_free(out);
    return status;
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

static int carbon_parse_raw_bind_group(const char *cursor, const char *end, const char **next, int *bind_count) {
    int count = 0;

    if (cursor >= end || *cursor != '(') {
        return 0;
    }
    ++cursor;
    cursor = carbon_skip_ws(cursor, end);
    if (cursor >= end || *cursor != '?') {
        return 0;
    }

    while (cursor < end && *cursor == '?') {
        ++count;
        ++cursor;
        cursor = carbon_skip_ws(cursor, end);
        if (cursor < end && *cursor == ',') {
            ++cursor;
            cursor = carbon_skip_ws(cursor, end);
            continue;
        }
        break;
    }

    if (cursor >= end || *cursor != ')' || count == 0) {
        return 0;
    }
    if (next != NULL) {
        *next = cursor + 1;
    }
    if (bind_count != NULL) {
        *bind_count = count;
    }
    return 1;
}

static int carbon_parse_multiply_marker(const char **cursor, const char *end) {
    const char *probe = *cursor;

    if (probe + 2 <= end
        && (unsigned char) probe[0] == 0xC3
        && (unsigned char) probe[1] == 0x97) {
        *cursor = probe + 2;
        return 1;
    }
    return 0;
}

static int carbon_parse_collapsed_bind_group(const char *cursor, const char *end, const char **next) {
    const char *probe = cursor;
    int has_digits = 0;

    if (probe >= end || *probe != '(') {
        return 0;
    }
    ++probe;
    probe = carbon_skip_ws(probe, end);
    if (probe >= end || *probe != '?') {
        return 0;
    }
    ++probe;
    probe = carbon_skip_ws(probe, end);
    if (!carbon_parse_multiply_marker(&probe, end)) {
        return 0;
    }
    while (probe < end && isdigit((unsigned char) *probe)) {
        has_digits = 1;
        ++probe;
    }
    probe = carbon_skip_ws(probe, end);
    if (!has_digits || probe >= end || *probe != ')') {
        return 0;
    }
    if (next != NULL) {
        *next = probe + 1;
    }
    return 1;
}

static const char *carbon_skip_bind_row_multiplier(const char *cursor, const char *end) {
    const char *probe = carbon_skip_ws(cursor, end);

    if (carbon_parse_multiply_marker(&probe, end)) {
        if (probe < end && *probe == '*') {
            return probe + 1;
        }
        while (probe < end && isdigit((unsigned char) *probe)) {
            ++probe;
        }
        return probe;
    }
    return cursor;
}

static int carbon_append_bind_group_collapsed(carbon_string_builder *builder, const char *sql) {
    const char *cursor = sql;
    const char *end = sql + strlen(sql);

    while (cursor < end) {
        const char *next;
        int bind_count;

        if (carbon_parse_raw_bind_group(cursor, end, &next, &bind_count)) {
            if (!carbon_builder_append_format(builder, "(? \xC3\x97%d)", bind_count)) {
                return 0;
            }
            cursor = next;
            continue;
        }

        if (!carbon_builder_append_char(builder, *cursor)) {
            return 0;
        }
        ++cursor;
    }
    return 1;
}

static int carbon_append_repeated_bind_rows_normalized(carbon_string_builder *builder, const char *sql) {
    const char *cursor = sql;
    const char *end = sql + strlen(sql);

    while (cursor < end) {
        const char *row_end;

        if (carbon_parse_collapsed_bind_group(cursor, end, &row_end)) {
            const char *probe = row_end;
            size_t row_length = (size_t) (row_end - cursor);
            int repeated = 0;

            while (probe < end) {
                const char *comma = carbon_skip_ws(probe, end);
                const char *next_row_start;
                const char *next_row_end;

                if (comma >= end || *comma != ',') {
                    break;
                }
                next_row_start = carbon_skip_ws(comma + 1, end);
                if (!carbon_parse_collapsed_bind_group(next_row_start, end, &next_row_end)
                    || (size_t) (next_row_end - next_row_start) != row_length
                    || strncmp(cursor, next_row_start, row_length) != 0) {
                    break;
                }
                repeated = 1;
                probe = next_row_end;
            }

            if (!carbon_builder_append_len(builder, cursor, row_length)) {
                return 0;
            }
            if (repeated) {
                if (!carbon_builder_append(builder, " \xC3\x97*")) {
                    return 0;
                }
                cursor = probe;
            } else {
                cursor = row_end;
            }
            continue;
        }

        if (!carbon_builder_append_char(builder, *cursor)) {
            return 0;
        }
        ++cursor;
    }
    return 1;
}

static int carbon_append_values_bind_rows_normalized(carbon_string_builder *builder, const char *sql) {
    const char *cursor = sql;
    const char *end = sql + strlen(sql);

    while (cursor < end) {
        size_t keyword_length = 0;

        if (carbon_ci_starts_with(cursor, "VALUES")
            && cursor + 6 < end
            && isspace((unsigned char) cursor[6])
            && (cursor == sql || !isalnum((unsigned char) cursor[-1]))) {
            keyword_length = 6;
        } else if (carbon_ci_starts_with(cursor, "VALUE")
                   && cursor + 5 < end
                   && isspace((unsigned char) cursor[5])
                   && (cursor == sql || !isalnum((unsigned char) cursor[-1]))) {
            keyword_length = 5;
        }

        if (keyword_length > 0) {
            const char *row_start = carbon_skip_ws(cursor + keyword_length, end);
            const char *row_end;

            if (carbon_parse_collapsed_bind_group(row_start, end, &row_end)) {
                const char *after_row = carbon_skip_bind_row_multiplier(row_end, end);
                const char *after_row_ws = carbon_skip_ws(after_row, end);

                if (after_row == row_end && after_row_ws < end && *after_row_ws == ',') {
                    if (!carbon_builder_append_char(builder, *cursor)) {
                        return 0;
                    }
                    ++cursor;
                    continue;
                }

                if (!carbon_builder_append_len(builder, cursor, keyword_length)
                    || !carbon_builder_append_char(builder, ' ')
                    || !carbon_builder_append_len(builder, row_start, (size_t) (row_end - row_start))
                    || !carbon_builder_append(builder, " \xC3\x97*")) {
                    return 0;
                }
                cursor = after_row;
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

static int carbon_append_in_bind_normalized(carbon_string_builder *builder, const char *sql) {
    const char *cursor = sql;
    const char *end = sql + strlen(sql);

    while (cursor < end) {
        if (carbon_ci_starts_with(cursor, "IN")
            && cursor + 2 < end
            && isspace((unsigned char) cursor[2])
            && (cursor == sql || !isalnum((unsigned char) cursor[-1]))) {
            const char *row_start = carbon_skip_ws(cursor + 2, end);
            const char *row_end;

            if (carbon_parse_collapsed_bind_group(row_start, end, &row_end)) {
                if (!carbon_builder_append(builder, "IN (? \xC3\x97*)")) {
                    return 0;
                }
                cursor = row_end;
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
    carbon_string_builder bind_groups_collapsed = {0};
    carbon_string_builder repeated_rows_normalized = {0};
    carbon_string_builder values_normalized = {0};
    carbon_string_builder in_normalized = {0};

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
        || !carbon_append_bind_group_collapsed(
                &bind_groups_collapsed,
                limit_normalized.data == NULL ? "" : limit_normalized.data)
        || !carbon_append_repeated_bind_rows_normalized(
                &repeated_rows_normalized,
                bind_groups_collapsed.data == NULL ? "" : bind_groups_collapsed.data)
        || !carbon_append_values_bind_rows_normalized(
                &values_normalized,
                repeated_rows_normalized.data == NULL ? "" : repeated_rows_normalized.data)
        || !carbon_append_in_bind_normalized(
                &in_normalized,
                values_normalized.data == NULL ? "" : values_normalized.data)
        || !carbon_buffer_take_builder(out, &in_normalized)) {
        carbon_builder_free(&collapsed);
        carbon_builder_free(&limit_normalized);
        carbon_builder_free(&bind_groups_collapsed);
        carbon_builder_free(&repeated_rows_normalized);
        carbon_builder_free(&values_normalized);
        carbon_builder_free(&in_normalized);
        if (error != NULL) {
            carbon_buffer_set(error, carbon_status_message(CARBON_STATUS_OUT_OF_MEMORY));
        }
        return CARBON_STATUS_OUT_OF_MEMORY;
    }

    carbon_builder_free(&collapsed);
    carbon_builder_free(&limit_normalized);
    carbon_builder_free(&bind_groups_collapsed);
    carbon_builder_free(&repeated_rows_normalized);
    carbon_builder_free(&values_normalized);
    if (error != NULL) {
        carbon_buffer_set(error, "");
    }
    return CARBON_STATUS_OK;
}

static carbon_status carbon_compile_select_statement(
        carbon_compile_state *state,
        carbon_json_span query,
        int is_subselect,
        carbon_string_builder *sql,
        const char *extra_where_sql,
        const char **error_message) {
    static const char *const where_names[] = {"WHERE", "where"};
    carbon_json_span where_span;
    char *table = NULL;
    carbon_query_scope scope;
    const carbon_query_scope *previous_scope = state->scope;
    carbon_status status;
    int has_pagination = 0;
    int found;

    query = carbon_trim_span(query);
    if (!carbon_span_starts_with(query, '{')) {
        return CARBON_STATUS_INVALID_JSON;
    }

    status = carbon_copy_query_table(state, query, &table, error_message);
    if (status != CARBON_STATUS_OK) {
        return status;
    }
    scope.base_table = table;
    scope.query = query;
    scope.parent = previous_scope;
    state->scope = &scope;

    if (!carbon_builder_append(sql, "SELECT ")) {
        status = CARBON_STATUS_OUT_OF_MEMORY;
        goto cleanup;
    }

    status = carbon_append_select_list(state, query, sql);
    if (status != CARBON_STATUS_OK) {
        goto cleanup;
    }

    if (!carbon_builder_append(sql, " FROM ")
        || !carbon_append_quoted_table(sql, state->dialect, table)) {
        status = CARBON_STATUS_OUT_OF_MEMORY;
        goto cleanup;
    }

    status = carbon_append_join_clauses(state, query, sql);
    if (status != CARBON_STATUS_OK) {
        goto cleanup;
    }

    found = carbon_object_get_any(query, where_names, 2, &where_span);
    if (found < 0) {
        status = CARBON_STATUS_INVALID_JSON;
        goto cleanup;
    }
    if (found > 0 || (extra_where_sql != NULL && extra_where_sql[0] != '\0')) {
        carbon_string_builder where = {0};
        if (found > 0) {
            status = carbon_build_where_node(state, where_span, "AND", &where);
            if (status != CARBON_STATUS_OK) {
                carbon_builder_free(&where);
                goto cleanup;
            }
        }
        if (extra_where_sql != NULL && extra_where_sql[0] != '\0') {
            if (where.length > 0 && !carbon_builder_append(&where, " AND ")) {
                carbon_builder_free(&where);
                status = CARBON_STATUS_OUT_OF_MEMORY;
                goto cleanup;
            }
            if (!carbon_builder_append(&where, extra_where_sql)) {
                carbon_builder_free(&where);
                status = CARBON_STATUS_OUT_OF_MEMORY;
                goto cleanup;
            }
        }
        if (where.length > 0 && (!carbon_builder_append(sql, " WHERE ")
                                 || !carbon_builder_append(sql, where.data))) {
            carbon_builder_free(&where);
            status = CARBON_STATUS_OUT_OF_MEMORY;
            goto cleanup;
        }
        carbon_builder_free(&where);
    }

    status = carbon_append_group_by(state, query, sql);
    if (status != CARBON_STATUS_OK) {
        goto cleanup;
    }

    status = carbon_append_having(state, query, sql);
    if (status != CARBON_STATUS_OK) {
        goto cleanup;
    }

    status = carbon_append_pagination(state, query, sql, &has_pagination);
    if (status != CARBON_STATUS_OK) {
        goto cleanup;
    }
    if (!is_subselect && !has_pagination && !carbon_builder_append(sql, " LIMIT 100")) {
        status = CARBON_STATUS_OUT_OF_MEMORY;
        goto cleanup;
    }

cleanup:
    state->scope = previous_scope;
    free(table);
    return status;
}

carbon_status carbon_compile_query(
        carbon_context *context,
        const carbon_compile_request *request,
        carbon_compile_result *result) {
    static const char *const dialect_names[] = {"dialect", "DIALECT"};
    carbon_json_span query;
    carbon_json_span dialect_span;
    char *dialect_string = NULL;
    carbon_dialect dialect;
    carbon_status status;
    carbon_string_builder sql = {0};
    carbon_string_builder params = {0};
    carbon_compile_state state;
    carbon_schema_view schema;
    const char *error_message = NULL;
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
    if (request->schema_json == NULL && request->schema_json_length > 0) {
        return carbon_set_result_error(result, CARBON_STATUS_INVALID_ARGUMENT,
                                       "schema_json is required when schema_json_length is greater than zero");
    }

    query.start = request->query_json;
    query.end = request->query_json + request->query_json_length;
    query = carbon_trim_span(query);
    if (!carbon_span_starts_with(query, '{')) {
        return carbon_set_result_error(result, CARBON_STATUS_INVALID_JSON, "query_json must be an object");
    }

    status = carbon_schema_prepare(
            request->schema_json,
            request->schema_json == NULL ? 0 : request->schema_json_length,
            &schema,
            &error_message);
    if (status != CARBON_STATUS_OK) {
        goto fail;
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

    state.dialect = dialect;
    state.sql = &sql;
    state.params = &params;
    state.schema = &schema;
    state.scope = NULL;
    state.param_count = 0;

    if (!carbon_builder_append(&params, "[")) {
        status = CARBON_STATUS_OUT_OF_MEMORY;
        goto fail;
    }

    status = carbon_compile_statement(&state, query, &sql, &error_message);
    if (status != CARBON_STATUS_OK) {
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
    return CARBON_STATUS_OK;

fail:
    carbon_builder_free(&sql);
    carbon_builder_free(&params);
    free(dialect_string);
    carbon_compile_result_free(result);
    return carbon_set_result_error(result, status, "%s",
                                   error_message == NULL ? carbon_status_message(status) : error_message);
}
