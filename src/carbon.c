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
    CARBON_DIALECT_KIND_MYSQL,
    CARBON_DIALECT_KIND_POSTGRESQL
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

typedef enum carbon_index_hint_kind {
    CARBON_INDEX_HINT_NONE = 0,
    CARBON_INDEX_HINT_FORCE,
    CARBON_INDEX_HINT_USE,
    CARBON_INDEX_HINT_IGNORE
} carbon_index_hint_kind;

static int carbon_ascii_case_equals(const char *left, const char *right);

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

static int carbon_span_is_false(carbon_json_span span) {
    span = carbon_trim_span(span);
    return (size_t) (span.end - span.start) == 5 && strncmp(span.start, "false", 5) == 0;
}

static int carbon_span_bool_value(carbon_json_span span, int *value) {
    char *copy;

    if (carbon_span_is_true(span)) {
        *value = 1;
        return 1;
    }
    if (carbon_span_is_false(span)) {
        *value = 0;
        return 1;
    }

    copy = carbon_span_string_copy(span);
    if (copy == NULL) {
        return 0;
    }
    if (carbon_ascii_case_equals(copy, "true") || carbon_ascii_case_equals(copy, "yes") || strcmp(copy, "1") == 0) {
        *value = 1;
        free(copy);
        return 1;
    }
    if (carbon_ascii_case_equals(copy, "false") || carbon_ascii_case_equals(copy, "no") || strcmp(copy, "0") == 0) {
        *value = 0;
        free(copy);
        return 1;
    }
    free(copy);
    return 0;
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
    char quote = dialect == CARBON_DIALECT_KIND_MYSQL ? '`' : '"';

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

static char *carbon_span_unquoted_trimmed_copy(carbon_json_span span) {
    char *copy = carbon_span_string_copy(span);
    char *read;
    char *write;
    char *start;
    char *end;

    if (copy == NULL) {
        return NULL;
    }

    read = copy;
    write = copy;
    while (*read != '\0') {
        if (*read != '`') {
            *write++ = *read;
        }
        ++read;
    }
    *write = '\0';

    start = copy;
    while (isspace((unsigned char) *start)) {
        ++start;
    }
    end = start + strlen(start);
    while (end > start && isspace((unsigned char) end[-1])) {
        --end;
    }
    *end = '\0';
    if (start != copy) {
        memmove(copy, start, strlen(start) + 1);
    }
    return copy;
}

static carbon_status carbon_index_hint_kind_from_key(
        carbon_json_span key,
        carbon_index_hint_kind *kind) {
    carbon_string_builder normalized = {0};
    char *raw = carbon_span_string_copy(key);
    int pending_space = 0;
    const char *cursor;

    *kind = CARBON_INDEX_HINT_NONE;
    if (raw == NULL) {
        return CARBON_STATUS_INVALID_QUERY;
    }

    for (cursor = raw; *cursor != '\0'; ++cursor) {
        unsigned char ch = (unsigned char) *cursor;

        if (ch == '`') {
            continue;
        }
        if (ch == '_' || isspace(ch)) {
            if (normalized.length > 0) {
                pending_space = 1;
            }
            continue;
        }
        if (pending_space && !carbon_builder_append_char(&normalized, ' ')) {
            free(raw);
            carbon_builder_free(&normalized);
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        pending_space = 0;
        if (!carbon_builder_append_char(&normalized, (char) toupper(ch))) {
            free(raw);
            carbon_builder_free(&normalized);
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
    }
    free(raw);

    if (normalized.data != NULL) {
        if (strcmp(normalized.data, "FORCE INDEX") == 0) {
            *kind = CARBON_INDEX_HINT_FORCE;
        } else if (strcmp(normalized.data, "USE INDEX") == 0) {
            *kind = CARBON_INDEX_HINT_USE;
        } else if (strcmp(normalized.data, "IGNORE INDEX") == 0) {
            *kind = CARBON_INDEX_HINT_IGNORE;
        }
    }
    carbon_builder_free(&normalized);
    return CARBON_STATUS_OK;
}

static const char *carbon_index_hint_keyword(carbon_index_hint_kind kind) {
    switch (kind) {
        case CARBON_INDEX_HINT_FORCE:
            return "FORCE INDEX";
        case CARBON_INDEX_HINT_USE:
            return "USE INDEX";
        case CARBON_INDEX_HINT_IGNORE:
            return "IGNORE INDEX";
        case CARBON_INDEX_HINT_NONE:
        default:
            return NULL;
    }
}

static int carbon_function_known(const char *token) {
    static const char *const functions[] = {
            "ADDDATE",
            "ADDTIME",
            "CONCAT",
            "CONVERT_TZ",
            "COUNT",
            "COUNT_ALL",
            "CURRENT_DATE",
            "CURRENT_TIMESTAMP",
            "DAY",
            "DAY_HOUR",
            "DAY_MICROSECOND",
            "DAY_MINUTE",
            "DAY_SECOND",
            "DAYNAME",
            "DAYOFMONTH",
            "DAYOFWEEK",
            "DAYOFYEAR",
            "DATE",
            "DATE_ADD",
            "DATEDIFF",
            "DATE_SUB",
            "DATE_FORMAT",
            "EXTRACT",
            "FROM_DAYS",
            "FROM_UNIXTIME",
            "GET_FORMAT",
            "GROUP_CONCAT",
            "HEX",
            "HOUR",
            "HOUR_MICROSECOND",
            "HOUR_MINUTE",
            "HOUR_SECOND",
            "INTERVAL",
            "LOCALTIME",
            "LOCALTIMESTAMP",
            "MAKEDATE",
            "MAKETIME",
            "MAX",
            "MBRCONTAINS",
            "MICROSECOND",
            "MIN",
            "MINUTE",
            "MINUTE_MICROSECOND",
            "MINUTE_SECOND",
            "MONTH",
            "MONTHNAME",
            "NOW",
            "POINT",
            "POLYGON",
            "SECOND",
            "SECOND_MICROSECOND",
            "ST_AREA",
            "ST_ASBINARY",
            "ST_ASTEXT",
            "ST_BUFFER",
            "ST_CONTAINS",
            "ST_CROSSES",
            "ST_DIFFERENCE",
            "ST_DIMENSION",
            "ST_DISJOINT",
            "ST_DISTANCE",
            "ST_DISTANCE_SPHERE",
            "ST_ENDPOINT",
            "ST_ENVELOPE",
            "ST_EQUALS",
            "ST_GEOMFROMGEOJSON",
            "ST_GEOMFROMTEXT",
            "ST_GEOMFROMWKB",
            "ST_INTERSECTS",
            "ST_LENGTH",
            "ST_MAKEENVELOPE",
            "ST_OVERLAPS",
            "ST_POINT",
            "ST_SETSRID",
            "ST_SRID",
            "ST_STARTPOINT",
            "ST_SYMDIFFERENCE",
            "ST_TOUCHES",
            "ST_UNION",
            "ST_WITHIN",
            "ST_X",
            "ST_Y",
            "STR_TO_DATE",
            "SUBDATE",
            "SUBTIME",
            "SUM",
            "SYSDATE",
            "TIME",
            "TIME_FORMAT",
            "TIME_TO_SEC",
            "TIMEDIFF",
            "TIMESTAMP",
            "TIMESTAMPADD",
            "TIMESTAMPDIFF",
            "TO_DAYS",
            "TO_SECONDS",
            "TRANSACTION_TIMESTAMP",
            "UNHEX",
            "UNIX_TIMESTAMP",
            "UTC_DATE",
            "UTC_TIME",
            "UTC_TIMESTAMP",
            "WEEKDAY",
            "WEEKOFYEAR",
            "YEARWEEK"
    };
    size_t index;

    if (token == NULL) {
        return 0;
    }

    for (index = 0; index < sizeof(functions) / sizeof(functions[0]); ++index) {
        if (strcmp(token, functions[index]) == 0) {
            return 1;
        }
    }

    return 0;
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

static int carbon_schema_span_matches_column(carbon_json_span span, const char *table, const char *column);

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

static int carbon_schema_object_matches_column(carbon_json_span object, const char *table, const char *column) {
    static const char *const qualified_names[] = {
            "qualified", "QUALIFIED", "qualified_column", "QUALIFIED_COLUMN", "full_column", "FULL_COLUMN"
    };
    static const char *const short_names[] = {
            "name", "NAME", "column", "COLUMN", "column_name", "COLUMN_NAME", "short", "SHORT", "short_name", "SHORT_NAME"
    };
    carbon_json_span value;
    int found;

    object = carbon_trim_span(object);
    if (!carbon_span_starts_with(object, '{')) {
        return 0;
    }

    found = carbon_object_get_any(object, qualified_names, sizeof(qualified_names) / sizeof(qualified_names[0]), &value);
    if (found > 0 && carbon_schema_span_matches_column(value, table, column)) {
        return 1;
    }
    if (found < 0) {
        return 0;
    }

    found = carbon_object_get_any(object, short_names, sizeof(short_names) / sizeof(short_names[0]), &value);
    if (found > 0 && carbon_schema_span_matches_column(value, table, column)) {
        return 1;
    }
    return 0;
}

static int carbon_schema_span_matches_column(carbon_json_span span, const char *table, const char *column) {
    char *candidate = carbon_span_string_copy(span);
    int matched;

    if (candidate == NULL) {
        return carbon_schema_object_matches_column(span, table, column);
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
        *dialect = CARBON_DIALECT_KIND_MYSQL;
        return 1;
    }

    if (strcmp(value, "postgresql") == 0 || strcmp(value, "postgres") == 0) {
        *dialect = CARBON_DIALECT_KIND_POSTGRESQL;
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
    if (strcmp(op, "MATCH_AGAINST") == 0) return "MATCH_AGAINST";
    return NULL;
}

static int carbon_is_boolean_operator(const char *op) {
    return strcmp(op, "AND") == 0 || strcmp(op, "OR") == 0;
}

static int carbon_is_boolean_function_key(const char *token) {
    return token != NULL
           && (strcmp(token, "ST_CONTAINS") == 0
               || strcmp(token, "ST_WITHIN") == 0
               || strcmp(token, "MBRCONTAINS") == 0);
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

    if (state->dialect == CARBON_DIALECT_KIND_POSTGRESQL) {
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

static carbon_status carbon_append_expression(
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

static carbon_status carbon_append_match_against_search(
        carbon_compile_state *state,
        carbon_json_span search,
        carbon_string_builder *sql) {
    carbon_json_span head_span;
    char *head = NULL;
    char *token = NULL;
    size_t count;
    carbon_status status = CARBON_STATUS_OK;

    search = carbon_trim_span(search);

    if (carbon_span_starts_with(search, '"')) {
        return CARBON_STATUS_INVALID_QUERY;
    }

    if (carbon_span_starts_with(search, '[')) {
        if (!carbon_array_count(search, &count)
            || count != 2
            || carbon_array_get(search, 0, &head_span) != 1) {
            return CARBON_STATUS_INVALID_QUERY;
        }
        head = carbon_span_string_copy(head_span);
        token = head == NULL ? NULL : carbon_upper_copy(head);
        if (token == NULL) {
            status = head == NULL ? CARBON_STATUS_INVALID_QUERY : CARBON_STATUS_OUT_OF_MEMORY;
            goto cleanup;
        }
        if (strcmp(token, "LIT") != 0 && strcmp(token, "PARAM") != 0) {
            status = CARBON_STATUS_INVALID_QUERY;
            goto cleanup;
        }
        status = carbon_append_expression(state, search, sql);
        goto cleanup;
    }

    if (carbon_span_is_scalar_json(search)) {
        return carbon_append_param(state, search, sql);
    }

    return CARBON_STATUS_INVALID_QUERY;

cleanup:
    free(head);
    free(token);
    return status;
}

static carbon_status carbon_append_match_against_mode(
        carbon_json_span mode,
        carbon_string_builder *sql) {
    char *mode_raw = NULL;
    char *token = NULL;
    carbon_status status = CARBON_STATUS_OK;

    if (mode.start == NULL) {
        return CARBON_STATUS_OK;
    }
    mode = carbon_trim_span(mode);
    if (mode.start >= mode.end || !carbon_span_starts_with(mode, '"')) {
        return CARBON_STATUS_OK;
    }

    mode_raw = carbon_span_string_copy(mode);
    token = mode_raw == NULL ? NULL : carbon_upper_copy(mode_raw);
    if (token == NULL) {
        status = mode_raw == NULL ? CARBON_STATUS_INVALID_QUERY : CARBON_STATUS_OUT_OF_MEMORY;
        goto cleanup;
    }

    if (strcmp(token, "BOOLEAN") == 0) {
        if (!carbon_builder_append(sql, " IN BOOLEAN MODE")) {
            status = CARBON_STATUS_OUT_OF_MEMORY;
        }
    } else if (strcmp(token, "WITH QUERY EXPANSION") == 0) {
        if (!carbon_builder_append(sql, " WITH QUERY EXPANSION")) {
            status = CARBON_STATUS_OUT_OF_MEMORY;
        }
    } else if (strcmp(token, "NATURAL LANGUAGE MODE") == 0) {
        if (!carbon_builder_append(sql, " IN NATURAL LANGUAGE MODE")) {
            status = CARBON_STATUS_OUT_OF_MEMORY;
        }
    }

cleanup:
    free(mode_raw);
    free(token);
    return status;
}

static carbon_status carbon_build_match_against_operator(
        carbon_compile_state *state,
        carbon_json_span left,
        carbon_json_span right,
        const char *context_column,
        carbon_string_builder *sql) {
    carbon_json_span search;
    carbon_json_span mode = {0};
    carbon_string_builder search_sql = {0};
    char *owned_column = NULL;
    const char *column = context_column;
    size_t count;
    carbon_status status = CARBON_STATUS_OK;

    if (column == NULL) {
        owned_column = carbon_copy_identifier_from_span(left, 0);
        column = owned_column;
    }
    if (column == NULL || !carbon_identifier_valid(column) || strcmp(column, "*") == 0) {
        status = CARBON_STATUS_INVALID_QUERY;
        goto cleanup;
    }

    status = carbon_schema_validate_reference_identifier(state, column);
    if (status != CARBON_STATUS_OK) {
        goto cleanup;
    }

    if (!carbon_span_starts_with(right, '[')
        || !carbon_array_count(right, &count)
        || count == 0
        || carbon_array_get(right, 0, &search) != 1) {
        status = CARBON_STATUS_INVALID_QUERY;
        goto cleanup;
    }
    if (count > 1 && carbon_array_get(right, 1, &mode) != 1) {
        status = CARBON_STATUS_INVALID_QUERY;
        goto cleanup;
    }

    status = carbon_append_match_against_search(state, search, &search_sql);
    if (status != CARBON_STATUS_OK) {
        goto cleanup;
    }

    if (!carbon_builder_append(sql, "(MATCH(")
        || !carbon_builder_append(sql, column)
        || !carbon_builder_append(sql, ") AGAINST(")
        || !carbon_builder_append(sql, search_sql.data == NULL ? "" : search_sql.data)) {
        status = CARBON_STATUS_OUT_OF_MEMORY;
        goto cleanup;
    }
    status = carbon_append_match_against_mode(mode, sql);
    if (status != CARBON_STATUS_OK) {
        goto cleanup;
    }
    if (!carbon_builder_append(sql, "))")) {
        status = CARBON_STATUS_OUT_OF_MEMORY;
    }

cleanup:
    carbon_builder_free(&search_sql);
    free(owned_column);
    return status;
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

        if (strcmp(token, "CALL") == 0) {
            carbon_json_span function_span;
            char *function_name = NULL;

            if (length < 2 || carbon_array_get(value, 1, &function_span) != 1) {
                status = CARBON_STATUS_INVALID_QUERY;
                goto cleanup;
            }
            function_name = carbon_span_string_copy(function_span);
            if (function_name == NULL
                || !carbon_identifier_alias_valid(function_name)) {
                free(function_name);
                status = CARBON_STATUS_INVALID_QUERY;
                goto cleanup;
            }
            if (!carbon_builder_append(sql, function_name)
                || !carbon_builder_append(sql, "(")) {
                free(function_name);
                status = CARBON_STATUS_OUT_OF_MEMORY;
                goto cleanup;
            }
            free(function_name);
            for (index = 2; index < length; ++index) {
                carbon_json_span item;
                if (index > 2 && !carbon_builder_append(sql, ", ")) {
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

        if (length >= 3) {
            carbon_json_span maybe_as_span;
            if (carbon_array_get(value, length - 2, &maybe_as_span) != 1) {
                status = CARBON_STATUS_INVALID_QUERY;
                goto cleanup;
            }
            maybe_as_span = carbon_trim_span(maybe_as_span);
            if (carbon_span_starts_with(maybe_as_span, '"')) {
                char *maybe_as = carbon_span_string_copy(maybe_as_span);
                char *maybe_as_token = maybe_as == NULL ? NULL : carbon_upper_copy(maybe_as);
                if (maybe_as == NULL || maybe_as_token == NULL) {
                    free(maybe_as);
                    free(maybe_as_token);
                    status = CARBON_STATUS_INVALID_QUERY;
                    goto cleanup;
                }
                if (strcmp(maybe_as_token, "AS") == 0) {
                    free(maybe_as);
                    free(maybe_as_token);
                    status = CARBON_STATUS_INVALID_QUERY;
                    goto cleanup;
                }
                free(maybe_as);
                free(maybe_as_token);
            }
        }

        if (!carbon_identifier_alias_valid(token) || !carbon_function_known(token)) {
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

    if (strcmp(operator_sql, "MATCH_AGAINST") == 0) {
        return carbon_build_match_against_operator(state, left, right, context_column, sql);
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

static carbon_status carbon_build_boolean_function_predicate(
        carbon_compile_state *state,
        const char *function_name,
        carbon_json_span args,
        size_t start_index,
        carbon_string_builder *sql) {
    size_t count;
    size_t index;
    int wrote = 0;

    if (!carbon_is_boolean_function_key(function_name)
        || !carbon_span_starts_with(args, '[')
        || !carbon_array_count(args, &count)
        || count <= start_index) {
        return CARBON_STATUS_INVALID_QUERY;
    }

    if (!carbon_builder_append(sql, function_name)
        || !carbon_builder_append_char(sql, '(')) {
        return CARBON_STATUS_OUT_OF_MEMORY;
    }

    for (index = start_index; index < count; ++index) {
        carbon_json_span item;
        carbon_status status;

        if (wrote && !carbon_builder_append(sql, ", ")) {
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        if (carbon_array_get(args, index, &item) != 1) {
            return CARBON_STATUS_INVALID_QUERY;
        }
        status = carbon_append_expression(state, item, sql);
        if (status != CARBON_STATUS_OK) {
            return status;
        }
        wrote = 1;
    }

    return carbon_builder_append_char(sql, ')') ? CARBON_STATUS_OK : CARBON_STATUS_OUT_OF_MEMORY;
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
    char *first_upper = NULL;
    char *second_string = NULL;
    carbon_status status = CARBON_STATUS_OK;

    if (!carbon_array_count(node, &count)) {
        return CARBON_STATUS_INVALID_QUERY;
    }
    if (count == 0) {
        return CARBON_STATUS_OK;
    }

    if (carbon_array_get(node, 0, &first) == 1) {
        first_string = carbon_span_string_copy(first);
        first_upper = first_string == NULL ? NULL : carbon_upper_copy(first_string);
        if (first_upper != NULL && carbon_is_boolean_function_key(first_upper)) {
            status = carbon_build_boolean_function_predicate(state, first_upper, node, 1, sql);
            goto cleanup;
        }
    }

    if (count == 3
        && carbon_array_get(node, 1, &second) == 1
        && carbon_array_get(node, 2, &third) == 1) {
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
    free(first_upper);
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
        int wrap_part = 0;

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
            wrap_part = 1;
            if (!carbon_span_starts_with(entry.value, '[')) {
                status = CARBON_STATUS_INVALID_QUERY;
            } else {
                status = carbon_join_where_parts(state, entry.value, key_upper, &part);
            }
        } else if (carbon_is_boolean_function_key(key_upper)) {
            status = carbon_build_boolean_function_predicate(state, key_upper, entry.value, 0, &part);
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
        if (wrap_part
            ? !carbon_builder_append_wrapped_expression(sql, part.data)
            : !carbon_builder_append(sql, part.data)) {
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

static carbon_status carbon_append_index_name_list(
        carbon_compile_state *state,
        carbon_json_span value,
        carbon_string_builder *sql,
        int *wrote) {
    carbon_json_span item;
    const char *cursor = NULL;
    int next;

    *wrote = 0;
    value = carbon_trim_span(value);

    if (carbon_span_starts_with(value, '"')) {
        char *index_name = carbon_span_unquoted_trimmed_copy(value);
        int valid;

        if (index_name == NULL) {
            return CARBON_STATUS_INVALID_QUERY;
        }
        if (index_name[0] == '\0') {
            free(index_name);
            return CARBON_STATUS_OK;
        }
        valid = carbon_identifier_alias_valid(index_name);
        if (!valid) {
            free(index_name);
            return CARBON_STATUS_INVALID_QUERY;
        }
        if (!carbon_builder_append_char(sql, '(')
            || !carbon_append_quoted_table(sql, state->dialect, index_name)
            || !carbon_builder_append_char(sql, ')')) {
            free(index_name);
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        free(index_name);
        *wrote = 1;
        return CARBON_STATUS_OK;
    }

    if (!carbon_span_starts_with(value, '[')) {
        return CARBON_STATUS_OK;
    }

    while ((next = carbon_array_next(value, &cursor, &item)) == 1) {
        char *index_name = carbon_span_unquoted_trimmed_copy(item);

        if (index_name == NULL) {
            return CARBON_STATUS_INVALID_QUERY;
        }
        if (index_name[0] == '\0') {
            free(index_name);
            continue;
        }
        if (!carbon_identifier_alias_valid(index_name)) {
            free(index_name);
            return CARBON_STATUS_INVALID_QUERY;
        }
        if (!*wrote) {
            if (!carbon_builder_append_char(sql, '(')) {
                free(index_name);
                return CARBON_STATUS_OUT_OF_MEMORY;
            }
        } else if (!carbon_builder_append(sql, ", ")) {
            free(index_name);
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        if (!carbon_append_quoted_table(sql, state->dialect, index_name)) {
            free(index_name);
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        free(index_name);
        *wrote = 1;
    }
    if (next < 0) {
        return CARBON_STATUS_INVALID_QUERY;
    }
    if (*wrote && !carbon_builder_append_char(sql, ')')) {
        return CARBON_STATUS_OUT_OF_MEMORY;
    }
    return CARBON_STATUS_OK;
}

static carbon_status carbon_append_index_hint_kind_clause(
        carbon_compile_state *state,
        carbon_index_hint_kind kind,
        carbon_json_span indexes,
        carbon_string_builder *sql) {
    carbon_string_builder list = {0};
    const char *keyword = carbon_index_hint_keyword(kind);
    carbon_status status;
    int wrote = 0;

    if (keyword == NULL) {
        return CARBON_STATUS_OK;
    }

    status = carbon_append_index_name_list(state, indexes, &list, &wrote);
    if (status != CARBON_STATUS_OK) {
        carbon_builder_free(&list);
        return status;
    }
    if (!wrote) {
        carbon_builder_free(&list);
        return CARBON_STATUS_OK;
    }
    if (!carbon_builder_append_char(sql, ' ')
        || !carbon_builder_append(sql, keyword)
        || !carbon_builder_append_char(sql, ' ')
        || !carbon_builder_append(sql, list.data)) {
        carbon_builder_free(&list);
        return CARBON_STATUS_OUT_OF_MEMORY;
    }

    carbon_builder_free(&list);
    return CARBON_STATUS_OK;
}

static int carbon_index_hint_spec_has_hint_keys(carbon_json_span spec, carbon_status *status) {
    const char *cursor = NULL;
    carbon_object_entry entry;
    int next;

    *status = CARBON_STATUS_OK;
    spec = carbon_trim_span(spec);
    if (!carbon_span_starts_with(spec, '{')) {
        return 0;
    }

    while ((next = carbon_object_next(spec, &cursor, &entry)) == 1) {
        carbon_index_hint_kind kind;

        *status = carbon_index_hint_kind_from_key(entry.key, &kind);
        if (*status != CARBON_STATUS_OK) {
            return 0;
        }
        if (kind != CARBON_INDEX_HINT_NONE) {
            return 1;
        }
    }
    if (next < 0) {
        *status = CARBON_STATUS_INVALID_QUERY;
    }
    return 0;
}

static carbon_status carbon_append_index_hint_spec(
        carbon_compile_state *state,
        carbon_json_span spec,
        carbon_string_builder *sql) {
    const char *cursor = NULL;
    carbon_object_entry entry;
    int next;

    if (state->dialect != CARBON_DIALECT_KIND_MYSQL) {
        return CARBON_STATUS_OK;
    }

    spec = carbon_trim_span(spec);
    if (carbon_span_starts_with(spec, '"') || carbon_span_starts_with(spec, '[')) {
        return carbon_append_index_hint_kind_clause(state, CARBON_INDEX_HINT_FORCE, spec, sql);
    }
    if (!carbon_span_starts_with(spec, '{')) {
        return CARBON_STATUS_OK;
    }

    while ((next = carbon_object_next(spec, &cursor, &entry)) == 1) {
        carbon_index_hint_kind kind;
        carbon_status status = carbon_index_hint_kind_from_key(entry.key, &kind);

        if (status != CARBON_STATUS_OK) {
            return status;
        }
        if (kind == CARBON_INDEX_HINT_NONE) {
            continue;
        }
        status = carbon_append_index_hint_kind_clause(state, kind, entry.value, sql);
        if (status != CARBON_STATUS_OK) {
            return status;
        }
    }
    return next < 0 ? CARBON_STATUS_INVALID_QUERY : CARBON_STATUS_OK;
}

static int carbon_index_hint_target_matches(carbon_json_span key, const char *candidate) {
    char *target = carbon_span_unquoted_trimmed_copy(key);
    int matches;

    if (target == NULL) {
        return 0;
    }
    matches = strcmp(target, candidate) == 0;
    free(target);
    return matches;
}

static carbon_status carbon_append_index_hints_for_candidate(
        carbon_compile_state *state,
        carbon_json_span hints,
        const char *candidate,
        carbon_string_builder *sql,
        int *matched) {
    const char *cursor = NULL;
    carbon_object_entry entry;
    int next;

    *matched = 0;
    while ((next = carbon_object_next(hints, &cursor, &entry)) == 1) {
        if (!carbon_index_hint_target_matches(entry.key, candidate)) {
            continue;
        }
        *matched = 1;
        return carbon_append_index_hint_spec(state, entry.value, sql);
    }
    return next < 0 ? CARBON_STATUS_INVALID_QUERY : CARBON_STATUS_OK;
}

static carbon_status carbon_append_index_hints_for_target(
        carbon_compile_state *state,
        carbon_json_span query,
        const char *table,
        const char *alias,
        carbon_string_builder *sql) {
    static const char *const index_hint_names[] = {"INDEX_HINTS", "index_hints"};
    carbon_json_span hints;
    carbon_status status = CARBON_STATUS_OK;
    int found = carbon_object_get_any(query, index_hint_names, 2, &hints);
    int matched = 0;

    if (found < 0) {
        return CARBON_STATUS_INVALID_JSON;
    }
    if (found == 0 || state->dialect != CARBON_DIALECT_KIND_MYSQL) {
        return CARBON_STATUS_OK;
    }

    hints = carbon_trim_span(hints);
    if (carbon_span_starts_with(hints, '"') || carbon_span_starts_with(hints, '[')) {
        return carbon_append_index_hint_spec(state, hints, sql);
    }
    if (!carbon_span_starts_with(hints, '{')) {
        return CARBON_STATUS_OK;
    }
    if (carbon_index_hint_spec_has_hint_keys(hints, &status)) {
        return carbon_append_index_hint_spec(state, hints, sql);
    }
    if (status != CARBON_STATUS_OK) {
        return status;
    }

    if (alias != NULL) {
        status = carbon_append_index_hints_for_candidate(state, hints, alias, sql, &matched);
        if (status != CARBON_STATUS_OK || matched) {
            return status;
        }
    }
    if (table != NULL && alias != NULL) {
        carbon_string_builder candidate = {0};

        if (!carbon_builder_append(&candidate, table)
            || !carbon_builder_append_char(&candidate, ' ')
            || !carbon_builder_append(&candidate, alias)) {
            carbon_builder_free(&candidate);
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        status = carbon_append_index_hints_for_candidate(state, hints, candidate.data, sql, &matched);
        carbon_builder_free(&candidate);
        if (status != CARBON_STATUS_OK || matched) {
            return status;
        }
    }
    if (table != NULL) {
        status = carbon_append_index_hints_for_candidate(state, hints, table, sql, &matched);
        if (status != CARBON_STATUS_OK || matched) {
            return status;
        }
    }
    return carbon_append_index_hints_for_candidate(state, hints, "__base__", sql, &matched);
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

                status = carbon_append_index_hints_for_target(state, query, table, alias, sql);
                if (status != CARBON_STATUS_OK) {
                    free(table);
                    free(alias);
                    free(normalized_join_kind);
                    return status;
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

static carbon_status carbon_append_order_clause(
        carbon_compile_state *state,
        carbon_json_span order,
        carbon_string_builder *sql) {
    const char *cursor = NULL;
    carbon_json_span term;
    int next;
    int wrote_order = 0;

    if (!carbon_span_starts_with(order, '[')) {
        return CARBON_STATUS_INVALID_QUERY;
    }
    if (!carbon_builder_append(sql, " ORDER BY ")) {
        return CARBON_STATUS_OUT_OF_MEMORY;
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
    if (next < 0 || !wrote_order) {
        return CARBON_STATUS_INVALID_QUERY;
    }
    return CARBON_STATUS_OK;
}

static carbon_status carbon_append_limit_clause(
        carbon_compile_state *state,
        carbon_json_span limit_span,
        carbon_json_span page_span,
        int has_page,
        carbon_string_builder *sql) {
    long limit;
    long page = 1;
    carbon_json_span trimmed = carbon_trim_span(limit_span);

    limit = strtol(trimmed.start, NULL, 10);
    if (has_page) {
        carbon_json_span page_trimmed = carbon_trim_span(page_span);
        page = strtol(page_trimmed.start, NULL, 10);
        if (page < 1) {
            page = 1;
        }
    }
    if (state->dialect == CARBON_DIALECT_KIND_POSTGRESQL) {
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
    carbon_json_span root_order;
    carbon_json_span limit_span;
    carbon_json_span page_span;
    int found_pagination;
    int found_order = 0;
    int found_root_order;
    int found_limit;
    int found_page = 0;

    *has_pagination = 0;
    found_pagination = carbon_object_get_any(query, pagination_names, 2, &pagination);
    if (found_pagination < 0) {
        return CARBON_STATUS_INVALID_JSON;
    }
    found_root_order = carbon_object_get_any(query, order_names, 2, &root_order);
    if (found_root_order < 0) {
        return CARBON_STATUS_INVALID_JSON;
    }

    if (found_pagination > 0) {
        if (!carbon_span_starts_with(pagination, '{')) {
            return CARBON_STATUS_INVALID_QUERY;
        }
        found_order = carbon_object_get_any(pagination, order_names, 2, &order);
        if (found_order < 0) {
            return CARBON_STATUS_INVALID_JSON;
        }
        if (found_order > 0 && found_root_order > 0) {
            return CARBON_STATUS_INVALID_QUERY;
        }
        found_limit = carbon_object_get_any(pagination, limit_names, 2, &limit_span);
        if (found_limit < 0) {
            return CARBON_STATUS_INVALID_JSON;
        }
        found_page = carbon_object_get_any(pagination, page_names, 2, &page_span);
        if (found_page < 0) {
            return CARBON_STATUS_INVALID_JSON;
        }
    } else {
        found_limit = carbon_object_get_any(query, limit_names, 2, &limit_span);
        if (found_limit < 0) {
            return CARBON_STATUS_INVALID_JSON;
        }
        found_page = carbon_object_get_any(query, page_names, 2, &page_span);
        if (found_page < 0) {
            return CARBON_STATUS_INVALID_JSON;
        }
    }

    if (found_order == 0 && found_root_order > 0) {
        order = root_order;
        found_order = 1;
    }

    if (found_order > 0) {
        carbon_status status;
        *has_pagination = 1;
        status = carbon_append_order_clause(state, order, sql);
        if (status != CARBON_STATUS_OK) {
            return status;
        }
    }

    if (found_limit > 0) {
        *has_pagination = 1;
        return carbon_append_limit_clause(state, limit_span, page_span, found_page > 0, sql);
    }

    if (found_pagination > 0) {
        *has_pagination = 1;
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

static carbon_status carbon_schema_table_type_validation_span(
        const carbon_compile_state *state,
        const char *table,
        carbon_json_span *type_validation,
        int *has_type_validation) {
    static const char *const type_validation_names[] = {"TYPE_VALIDATION", "type_validation", "validation"};
    carbon_json_span definition;
    int found;

    *has_type_validation = 0;
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

    found = carbon_object_get_any(
            definition,
            type_validation_names,
            sizeof(type_validation_names) / sizeof(type_validation_names[0]),
            type_validation);
    if (found < 0) {
        return CARBON_STATUS_INVALID_QUERY;
    }
    if (found == 0) {
        return CARBON_STATUS_OK;
    }

    *type_validation = carbon_trim_span(*type_validation);
    if (!carbon_span_starts_with(*type_validation, '{')) {
        return CARBON_STATUS_INVALID_QUERY;
    }
    *has_type_validation = 1;
    return CARBON_STATUS_OK;
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

static carbon_status carbon_metadata_copy_object_string_any(
        carbon_json_span object,
        const char *const *names,
        size_t name_count,
        char **out) {
    carbon_json_span value;
    int found;

    *out = NULL;
    object = carbon_trim_span(object);
    if (!carbon_span_starts_with(object, '{')) {
        return CARBON_STATUS_OK;
    }

    found = carbon_object_get_any(object, names, name_count, &value);
    if (found < 0) {
        return CARBON_STATUS_INVALID_QUERY;
    }
    if (found == 0) {
        return CARBON_STATUS_OK;
    }

    *out = carbon_span_string_copy(value);
    return *out == NULL ? CARBON_STATUS_INVALID_QUERY : CARBON_STATUS_OK;
}

static carbon_status carbon_metadata_append_candidate(char **candidates, size_t *candidate_count, char *candidate) {
    if (candidate == NULL) {
        return CARBON_STATUS_OK;
    }
    if (*candidate_count >= 8) {
        free(candidate);
        return CARBON_STATUS_INVALID_QUERY;
    }
    candidates[*candidate_count] = candidate;
    ++(*candidate_count);
    return CARBON_STATUS_OK;
}

static void carbon_metadata_free_candidates(char **candidates, size_t candidate_count) {
    size_t index;

    for (index = 0; index < candidate_count; ++index) {
        free(candidates[index]);
    }
}

static carbon_status carbon_metadata_add_span_candidates(
        carbon_json_span span,
        char **candidates,
        size_t *candidate_count) {
    static const char *const qualified_names[] = {
            "qualified", "QUALIFIED", "qualified_column", "QUALIFIED_COLUMN", "full_column", "FULL_COLUMN"
    };
    static const char *const short_names[] = {
            "name", "NAME", "column", "COLUMN", "column_name", "COLUMN_NAME", "short", "SHORT", "short_name", "SHORT_NAME"
    };
    carbon_status status;
    char *candidate = NULL;

    span = carbon_trim_span(span);
    if (span.start == NULL || span.end == NULL || span.start == span.end) {
        return CARBON_STATUS_OK;
    }

    if (carbon_span_starts_with(span, '{')) {
        status = carbon_metadata_copy_object_string_any(
                span,
                qualified_names,
                sizeof(qualified_names) / sizeof(qualified_names[0]),
                &candidate);
        if (status != CARBON_STATUS_OK) {
            return status;
        }
        status = carbon_metadata_append_candidate(candidates, candidate_count, candidate);
        if (status != CARBON_STATUS_OK) {
            return status;
        }

        candidate = NULL;
        status = carbon_metadata_copy_object_string_any(
                span,
                short_names,
                sizeof(short_names) / sizeof(short_names[0]),
                &candidate);
        if (status != CARBON_STATUS_OK) {
            return status;
        }
        return carbon_metadata_append_candidate(candidates, candidate_count, candidate);
    }

    candidate = carbon_span_string_copy(span);
    if (candidate == NULL) {
        return CARBON_STATUS_OK;
    }
    return carbon_metadata_append_candidate(candidates, candidate_count, candidate);
}

static carbon_status carbon_metadata_extract_column_identity(
        carbon_compile_state *state,
        const char *table,
        carbon_json_span preferred,
        carbon_json_span fallback,
        char **qualified_column,
        char **short_column) {
    char *candidates[8] = {0};
    size_t candidate_count = 0;
    size_t index;
    carbon_status status;

    *qualified_column = NULL;
    *short_column = NULL;

    status = carbon_metadata_add_span_candidates(preferred, candidates, &candidate_count);
    if (status != CARBON_STATUS_OK) {
        carbon_metadata_free_candidates(candidates, candidate_count);
        return status;
    }
    status = carbon_metadata_add_span_candidates(fallback, candidates, &candidate_count);
    if (status != CARBON_STATUS_OK) {
        carbon_metadata_free_candidates(candidates, candidate_count);
        return status;
    }

    for (index = 0; index < candidate_count; ++index) {
        *short_column = carbon_trim_write_column(state, table, candidates[index]);
        if (*short_column != NULL) {
            break;
        }
    }
    if (*short_column == NULL) {
        carbon_metadata_free_candidates(candidates, candidate_count);
        return CARBON_STATUS_INVALID_QUERY;
    }

    for (index = 0; index < candidate_count; ++index) {
        *qualified_column = carbon_metadata_copy_matching_qualified_column(
                state,
                table,
                candidates[index],
                *short_column);
        if (*qualified_column != NULL) {
            break;
        }
    }
    if (*qualified_column == NULL) {
        *qualified_column = carbon_metadata_build_qualified_column(table, *short_column);
        if (*qualified_column == NULL) {
            free(*short_column);
            *short_column = NULL;
            carbon_metadata_free_candidates(candidates, candidate_count);
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
    }

    carbon_metadata_free_candidates(candidates, candidate_count);
    return CARBON_STATUS_OK;
}

static char *carbon_copy_schema_write_column_short(
        carbon_compile_state *state,
        const char *table,
        carbon_json_span span) {
    char *qualified_column = NULL;
    char *short_column;
    carbon_status status;

    if (span.start == NULL || span.end == NULL) {
        return NULL;
    }
    status = carbon_metadata_extract_column_identity(
            state,
            table,
            span,
            (carbon_json_span) {0},
            &qualified_column,
            &short_column);
    free(qualified_column);
    if (status != CARBON_STATUS_OK) {
        return NULL;
    }
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

static carbon_status carbon_metadata_add_column_from_spans(
        carbon_compile_state *state,
        const char *table,
        carbon_json_span preferred,
        carbon_json_span fallback,
        carbon_write_columns *columns) {
    char *short_column = NULL;
    char *qualified_column = NULL;
    carbon_status status;

    status = carbon_metadata_extract_column_identity(
            state,
            table,
            preferred,
            fallback,
            &qualified_column,
            &short_column);
    if (status != CARBON_STATUS_OK) {
        return status;
    }

    status = carbon_write_columns_add(columns, qualified_column, short_column);
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

static int carbon_metadata_identity_matches(
        const char *candidate_qualified,
        const char *candidate_short,
        const char *qualified,
        const char *short_column) {
    return (candidate_qualified != NULL && qualified != NULL && strcmp(candidate_qualified, qualified) == 0)
           || (candidate_short != NULL && short_column != NULL && strcmp(candidate_short, short_column) == 0);
}

static carbon_status carbon_schema_find_column_metadata_in_collection(
        carbon_compile_state *state,
        const char *table,
        carbon_json_span collection,
        const char *qualified,
        const char *short_column,
        carbon_json_span *metadata,
        int *found_metadata) {
    *found_metadata = 0;
    collection = carbon_trim_span(collection);

    if (carbon_span_starts_with(collection, '[')) {
        const char *cursor = NULL;
        carbon_json_span item;
        int next;

        while ((next = carbon_array_next(collection, &cursor, &item)) == 1) {
            char *candidate_qualified = NULL;
            char *candidate_short = NULL;
            carbon_status status;

            if (!carbon_span_starts_with(item, '{')) {
                continue;
            }
            status = carbon_metadata_extract_column_identity(
                    state,
                    table,
                    item,
                    (carbon_json_span) {0},
                    &candidate_qualified,
                    &candidate_short);
            if (status != CARBON_STATUS_OK) {
                free(candidate_qualified);
                free(candidate_short);
                return status;
            }
            if (carbon_metadata_identity_matches(candidate_qualified, candidate_short, qualified, short_column)) {
                *metadata = item;
                *found_metadata = 1;
                free(candidate_qualified);
                free(candidate_short);
                return CARBON_STATUS_OK;
            }
            free(candidate_qualified);
            free(candidate_short);
        }
        return next < 0 ? CARBON_STATUS_INVALID_QUERY : CARBON_STATUS_OK;
    }

    if (carbon_span_starts_with(collection, '{')) {
        const char *cursor = NULL;
        carbon_object_entry entry;
        int next;

        while ((next = carbon_object_next(collection, &cursor, &entry)) == 1) {
            char *candidate_qualified = NULL;
            char *candidate_short = NULL;
            carbon_status status;

            if (!carbon_span_starts_with(entry.value, '{')) {
                continue;
            }
            status = carbon_metadata_extract_column_identity(
                    state,
                    table,
                    entry.value,
                    entry.key,
                    &candidate_qualified,
                    &candidate_short);
            if (status != CARBON_STATUS_OK) {
                free(candidate_qualified);
                free(candidate_short);
                return status;
            }
            if (carbon_metadata_identity_matches(candidate_qualified, candidate_short, qualified, short_column)) {
                *metadata = entry.value;
                *found_metadata = 1;
                free(candidate_qualified);
                free(candidate_short);
                return CARBON_STATUS_OK;
            }
            free(candidate_qualified);
            free(candidate_short);
        }
        return next < 0 ? CARBON_STATUS_INVALID_QUERY : CARBON_STATUS_OK;
    }

    return CARBON_STATUS_OK;
}

static carbon_status carbon_schema_find_column_metadata(
        carbon_compile_state *state,
        const char *table,
        const char *qualified,
        const char *short_column,
        carbon_json_span *metadata,
        int *found_metadata) {
    carbon_json_span type_validation;
    carbon_json_span columns;
    int has_type_validation = 0;
    int has_columns = 0;
    carbon_status status;

    *found_metadata = 0;
    status = carbon_schema_table_type_validation_span(state, table, &type_validation, &has_type_validation);
    if (status != CARBON_STATUS_OK) {
        return status;
    }
    if (has_type_validation) {
        status = carbon_schema_find_column_metadata_in_collection(
                state,
                table,
                type_validation,
                qualified,
                short_column,
                metadata,
                found_metadata);
        if (status != CARBON_STATUS_OK || *found_metadata) {
            return status;
        }
    }

    status = carbon_schema_table_columns_span(state, table, &columns, &has_columns);
    if (status != CARBON_STATUS_OK || !has_columns) {
        return status;
    }
    return carbon_schema_find_column_metadata_in_collection(
            state,
            table,
            columns,
            qualified,
            short_column,
            metadata,
            found_metadata);
}

static carbon_status carbon_metadata_copy_string_field(
        carbon_json_span metadata,
        const char *const *names,
        size_t name_count,
        char **value,
        int *found_value) {
    carbon_status status;

    *found_value = 0;
    status = carbon_metadata_copy_object_string_any(metadata, names, name_count, value);
    if (status != CARBON_STATUS_OK) {
        return status;
    }
    *found_value = *value != NULL;
    return CARBON_STATUS_OK;
}

static carbon_status carbon_metadata_copy_boolean_field(
        carbon_json_span metadata,
        const char *const *names,
        size_t name_count,
        int invert,
        int *value,
        int *found_value) {
    carbon_json_span span;
    int found;

    *found_value = 0;
    found = carbon_object_get_any(metadata, names, name_count, &span);
    if (found < 0) {
        return CARBON_STATUS_INVALID_QUERY;
    }
    if (found == 0) {
        return CARBON_STATUS_OK;
    }
    if (!carbon_span_bool_value(span, value)) {
        return CARBON_STATUS_INVALID_QUERY;
    }
    if (invert) {
        *value = !*value;
    }
    *found_value = 1;
    return CARBON_STATUS_OK;
}

static carbon_status carbon_append_schema_metadata_column_details(
        carbon_compile_state *state,
        const char *table,
        const char *qualified,
        const char *short_column,
        carbon_string_builder *json) {
    static const char *const db_type_names[] = {
            "MYSQL_TYPE", "mysql_type", "DATA_TYPE", "data_type", "COLUMN_TYPE", "column_type", "db_type", "DB_TYPE", "type", "TYPE"
    };
    static const char *const max_length_names[] = {
            "MAX_LENGTH", "max_length", "CHARACTER_MAXIMUM_LENGTH", "character_maximum_length", "length", "LENGTH"
    };
    static const char *const nullable_names[] = {"nullable", "NULLABLE", "IS_NULLABLE", "is_nullable"};
    static const char *const not_null_names[] = {"NOT_NULL", "not_null"};
    static const char *const auto_increment_names[] = {"AUTO_INCREMENT", "auto_increment"};
    static const char *const skip_insert_names[] = {"SKIP_COLUMN_IN_POST", "skip_column_in_post", "skip_insert", "SKIP_INSERT"};
    carbon_json_span metadata;
    int found_metadata = 0;
    char *string_value = NULL;
    int found_value = 0;
    int bool_value = 0;
    carbon_status status;

    status = carbon_schema_find_column_metadata(
            state,
            table,
            qualified,
            short_column,
            &metadata,
            &found_metadata);
    if (status != CARBON_STATUS_OK || !found_metadata) {
        return status;
    }

    status = carbon_metadata_copy_string_field(
            metadata,
            db_type_names,
            sizeof(db_type_names) / sizeof(db_type_names[0]),
            &string_value,
            &found_value);
    if (status != CARBON_STATUS_OK) {
        return status;
    }
    if (found_value) {
        if (!carbon_builder_append(json, ",\"db_type\":")
            || !carbon_builder_append_json_string(json, string_value)) {
            free(string_value);
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        free(string_value);
        string_value = NULL;
    }

    status = carbon_metadata_copy_string_field(
            metadata,
            max_length_names,
            sizeof(max_length_names) / sizeof(max_length_names[0]),
            &string_value,
            &found_value);
    if (status != CARBON_STATUS_OK) {
        return status;
    }
    if (found_value) {
        if (!carbon_builder_append(json, ",\"max_length\":")
            || !carbon_builder_append_json_string(json, string_value)) {
            free(string_value);
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        free(string_value);
        string_value = NULL;
    }

    status = carbon_metadata_copy_boolean_field(
            metadata,
            nullable_names,
            sizeof(nullable_names) / sizeof(nullable_names[0]),
            0,
            &bool_value,
            &found_value);
    if (status != CARBON_STATUS_OK) {
        return status;
    }
    if (!found_value) {
        status = carbon_metadata_copy_boolean_field(
                metadata,
                not_null_names,
                sizeof(not_null_names) / sizeof(not_null_names[0]),
                1,
                &bool_value,
                &found_value);
        if (status != CARBON_STATUS_OK) {
            return status;
        }
    }
    if (found_value
        && !carbon_builder_append(json, bool_value ? ",\"nullable\":true" : ",\"nullable\":false")) {
        return CARBON_STATUS_OUT_OF_MEMORY;
    }

    status = carbon_metadata_copy_boolean_field(
            metadata,
            auto_increment_names,
            sizeof(auto_increment_names) / sizeof(auto_increment_names[0]),
            0,
            &bool_value,
            &found_value);
    if (status != CARBON_STATUS_OK) {
        return status;
    }
    if (found_value
        && !carbon_builder_append(json, bool_value ? ",\"auto_increment\":true" : ",\"auto_increment\":false")) {
        return CARBON_STATUS_OUT_OF_MEMORY;
    }

    status = carbon_metadata_copy_boolean_field(
            metadata,
            skip_insert_names,
            sizeof(skip_insert_names) / sizeof(skip_insert_names[0]),
            0,
            &bool_value,
            &found_value);
    if (status != CARBON_STATUS_OK) {
        return status;
    }
    if (found_value
        && !carbon_builder_append(json, bool_value ? ",\"skip_insert\":true" : ",\"skip_insert\":false")) {
        return CARBON_STATUS_OUT_OF_MEMORY;
    }

    return CARBON_STATUS_OK;
}

static carbon_status carbon_append_schema_metadata_columns(
        carbon_compile_state *state,
        const char *table,
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
            || !carbon_builder_append_json_string(json, columns->keys[index])) {
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        {
            carbon_status status = carbon_append_schema_metadata_column_details(
                    state,
                    table,
                    columns->keys[index],
                    columns->short_columns[index],
                    json);
            if (status != CARBON_STATUS_OK) {
                return status;
            }
        }
        if (!carbon_builder_append_char(json, '}')) {
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

    status = carbon_append_schema_metadata_columns(state, table, &columns, json);
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

typedef struct carbon_dump_column {
    char *name;
    char *db_type;
    char *max_length;
    int not_null;
    int auto_increment;
    int skip_insert;
    int primary;
} carbon_dump_column;

typedef struct carbon_dump_table {
    char *name;
    carbon_dump_column *columns;
    size_t column_count;
    size_t column_capacity;
    char **primary;
    size_t primary_count;
    size_t primary_capacity;
} carbon_dump_table;

typedef struct carbon_dump_schema {
    carbon_dump_table *tables;
    size_t table_count;
    size_t table_capacity;
} carbon_dump_schema;

static void carbon_dump_column_free(carbon_dump_column *column) {
    if (column == NULL) {
        return;
    }
    free(column->name);
    free(column->db_type);
    free(column->max_length);
    column->name = NULL;
    column->db_type = NULL;
    column->max_length = NULL;
}

static void carbon_dump_table_free(carbon_dump_table *table) {
    size_t index;

    if (table == NULL) {
        return;
    }
    free(table->name);
    for (index = 0; index < table->column_count; ++index) {
        carbon_dump_column_free(&table->columns[index]);
    }
    free(table->columns);
    for (index = 0; index < table->primary_count; ++index) {
        free(table->primary[index]);
    }
    free(table->primary);
    memset(table, 0, sizeof(*table));
}

static void carbon_dump_schema_free(carbon_dump_schema *schema) {
    size_t index;

    if (schema == NULL) {
        return;
    }
    for (index = 0; index < schema->table_count; ++index) {
        carbon_dump_table_free(&schema->tables[index]);
    }
    free(schema->tables);
    schema->tables = NULL;
    schema->table_count = 0;
    schema->table_capacity = 0;
}

static int carbon_dump_reserve_tables(carbon_dump_schema *schema) {
    carbon_dump_table *next;
    size_t capacity;

    if (schema->table_count < schema->table_capacity) {
        return 1;
    }
    capacity = schema->table_capacity == 0 ? 4 : schema->table_capacity * 2;
    next = (carbon_dump_table *) realloc(schema->tables, capacity * sizeof(*schema->tables));
    if (next == NULL) {
        return 0;
    }
    memset(next + schema->table_capacity, 0, (capacity - schema->table_capacity) * sizeof(*schema->tables));
    schema->tables = next;
    schema->table_capacity = capacity;
    return 1;
}

static carbon_dump_table *carbon_dump_find_table(carbon_dump_schema *schema, const char *name) {
    size_t index;

    for (index = 0; index < schema->table_count; ++index) {
        if (schema->tables[index].name != NULL && strcmp(schema->tables[index].name, name) == 0) {
            return &schema->tables[index];
        }
    }
    return NULL;
}

static carbon_status carbon_dump_add_table(
        carbon_dump_schema *schema,
        char *name,
        carbon_dump_table **table) {
    carbon_dump_table *slot;

    *table = NULL;
    if (name == NULL || !carbon_identifier_alias_valid(name)) {
        free(name);
        return CARBON_STATUS_INVALID_QUERY;
    }
    if (carbon_dump_find_table(schema, name) != NULL) {
        free(name);
        return CARBON_STATUS_INVALID_QUERY;
    }
    if (!carbon_dump_reserve_tables(schema)) {
        free(name);
        return CARBON_STATUS_OUT_OF_MEMORY;
    }
    slot = &schema->tables[schema->table_count++];
    memset(slot, 0, sizeof(*slot));
    slot->name = name;
    *table = slot;
    return CARBON_STATUS_OK;
}

static int carbon_dump_reserve_columns(carbon_dump_table *table) {
    carbon_dump_column *next;
    size_t capacity;

    if (table->column_count < table->column_capacity) {
        return 1;
    }
    capacity = table->column_capacity == 0 ? 8 : table->column_capacity * 2;
    next = (carbon_dump_column *) realloc(table->columns, capacity * sizeof(*table->columns));
    if (next == NULL) {
        return 0;
    }
    memset(next + table->column_capacity, 0, (capacity - table->column_capacity) * sizeof(*table->columns));
    table->columns = next;
    table->column_capacity = capacity;
    return 1;
}

static carbon_dump_column *carbon_dump_find_column(carbon_dump_table *table, const char *name) {
    size_t index;

    for (index = 0; index < table->column_count; ++index) {
        if (table->columns[index].name != NULL && strcmp(table->columns[index].name, name) == 0) {
            return &table->columns[index];
        }
    }
    return NULL;
}

static carbon_status carbon_dump_add_column(
        carbon_dump_table *table,
        carbon_dump_column *column) {
    carbon_dump_column *slot;

    if (column->name == NULL || !carbon_identifier_alias_valid(column->name)) {
        carbon_dump_column_free(column);
        return CARBON_STATUS_INVALID_QUERY;
    }
    if (carbon_dump_find_column(table, column->name) != NULL) {
        carbon_dump_column_free(column);
        return CARBON_STATUS_INVALID_QUERY;
    }
    if (!carbon_dump_reserve_columns(table)) {
        carbon_dump_column_free(column);
        return CARBON_STATUS_OUT_OF_MEMORY;
    }
    slot = &table->columns[table->column_count++];
    *slot = *column;
    memset(column, 0, sizeof(*column));
    return CARBON_STATUS_OK;
}

static int carbon_dump_reserve_primary(carbon_dump_table *table) {
    char **next;
    size_t capacity;

    if (table->primary_count < table->primary_capacity) {
        return 1;
    }
    capacity = table->primary_capacity == 0 ? 2 : table->primary_capacity * 2;
    next = (char **) realloc(table->primary, capacity * sizeof(*table->primary));
    if (next == NULL) {
        return 0;
    }
    memset(next + table->primary_capacity, 0, (capacity - table->primary_capacity) * sizeof(*table->primary));
    table->primary = next;
    table->primary_capacity = capacity;
    return 1;
}

static carbon_status carbon_dump_mark_primary(carbon_dump_table *table, char *column_name) {
    carbon_dump_column *column;
    size_t index;

    if (column_name == NULL || !carbon_identifier_alias_valid(column_name)) {
        free(column_name);
        return CARBON_STATUS_INVALID_QUERY;
    }
    for (index = 0; index < table->primary_count; ++index) {
        if (strcmp(table->primary[index], column_name) == 0) {
            free(column_name);
            return CARBON_STATUS_OK;
        }
    }
    if (!carbon_dump_reserve_primary(table)) {
        free(column_name);
        return CARBON_STATUS_OUT_OF_MEMORY;
    }
    table->primary[table->primary_count++] = column_name;
    column = carbon_dump_find_column(table, column_name);
    if (column != NULL) {
        column->primary = 1;
        column->not_null = 1;
    }
    return CARBON_STATUS_OK;
}

static int carbon_sql_word_char(char ch) {
    return isalnum((unsigned char) ch) || ch == '_' || ch == '$';
}

static carbon_json_span carbon_sql_trim_span(const char *start, const char *end) {
    carbon_json_span span;

    span.start = start;
    span.end = end;
    return carbon_trim_span(span);
}

static const char *carbon_sql_skip_quoted(const char *cursor, const char *end, char quote) {
    if (cursor >= end || *cursor != quote) {
        return NULL;
    }
    ++cursor;
    while (cursor < end) {
        if (*cursor == '\\' && cursor + 1 < end) {
            cursor += 2;
            continue;
        }
        if (*cursor == quote) {
            if (cursor + 1 < end && cursor[1] == quote) {
                cursor += 2;
                continue;
            }
            return cursor + 1;
        }
        ++cursor;
    }
    return NULL;
}

static const char *carbon_sql_skip_ws_comments(const char *cursor, const char *end, int *ok) {
    int advanced = 1;

    if (ok != NULL) {
        *ok = 1;
    }
    while (advanced) {
        advanced = 0;
        while (cursor < end && isspace((unsigned char) *cursor)) {
            ++cursor;
            advanced = 1;
        }
        if (cursor + 1 < end && cursor[0] == '-' && cursor[1] == '-') {
            cursor += 2;
            while (cursor < end && *cursor != '\n' && *cursor != '\r') {
                ++cursor;
            }
            advanced = 1;
            continue;
        }
        if (cursor < end && *cursor == '#') {
            while (cursor < end && *cursor != '\n' && *cursor != '\r') {
                ++cursor;
            }
            advanced = 1;
            continue;
        }
        if (cursor + 1 < end && cursor[0] == '/' && cursor[1] == '*') {
            cursor += 2;
            while (cursor + 1 < end && !(cursor[0] == '*' && cursor[1] == '/')) {
                ++cursor;
            }
            if (cursor + 1 >= end) {
                if (ok != NULL) {
                    *ok = 0;
                }
                return end;
            }
            cursor += 2;
            advanced = 1;
        }
    }
    return cursor;
}

static int carbon_sql_keyword_at(const char *cursor, const char *end, const char *keyword) {
    size_t length = strlen(keyword);
    size_t index;

    if ((size_t) (end - cursor) < length) {
        return 0;
    }
    for (index = 0; index < length; ++index) {
        if (toupper((unsigned char) cursor[index]) != toupper((unsigned char) keyword[index])) {
            return 0;
        }
    }
    return cursor + length == end || !carbon_sql_word_char(cursor[length]);
}

static const char *carbon_sql_consume_keyword(const char *cursor, const char *end, const char *keyword) {
    return carbon_sql_keyword_at(cursor, end, keyword) ? cursor + strlen(keyword) : NULL;
}

static char *carbon_sql_copy_quoted_identifier(const char *cursor, const char *end, const char **next) {
    carbon_string_builder builder = {0};
    char quote = *cursor;

    ++cursor;
    while (cursor < end) {
        if (*cursor == '\\' && cursor + 1 < end) {
            if (!carbon_builder_append_char(&builder, cursor[1])) {
                carbon_builder_free(&builder);
                return NULL;
            }
            cursor += 2;
            continue;
        }
        if (*cursor == quote) {
            if (cursor + 1 < end && cursor[1] == quote) {
                if (!carbon_builder_append_char(&builder, quote)) {
                    carbon_builder_free(&builder);
                    return NULL;
                }
                cursor += 2;
                continue;
            }
            *next = cursor + 1;
            if (builder.data == NULL && !carbon_builder_append(&builder, "")) {
                return NULL;
            }
            return builder.data;
        }
        if (!carbon_builder_append_char(&builder, *cursor)) {
            carbon_builder_free(&builder);
            return NULL;
        }
        ++cursor;
    }
    carbon_builder_free(&builder);
    return NULL;
}

static char *carbon_sql_parse_identifier(const char *cursor, const char *end, const char **next) {
    const char *start;

    if (cursor >= end) {
        return NULL;
    }
    if (*cursor == '`' || *cursor == '"') {
        return carbon_sql_copy_quoted_identifier(cursor, end, next);
    }
    if (!carbon_sql_word_char(*cursor) || isdigit((unsigned char) *cursor)) {
        return NULL;
    }
    start = cursor;
    while (cursor < end && carbon_sql_word_char(*cursor)) {
        ++cursor;
    }
    *next = cursor;
    return carbon_strndup_local(start, (size_t) (cursor - start));
}

static char *carbon_sql_parse_qualified_identifier(const char *cursor, const char *end, const char **next) {
    char *identifier;
    int ok = 1;

    cursor = carbon_sql_skip_ws_comments(cursor, end, &ok);
    if (!ok) {
        return NULL;
    }
    identifier = carbon_sql_parse_identifier(cursor, end, next);
    if (identifier == NULL) {
        return NULL;
    }
    cursor = *next;
    while (1) {
        char *segment;

        cursor = carbon_sql_skip_ws_comments(cursor, end, &ok);
        if (!ok) {
            free(identifier);
            return NULL;
        }
        if (cursor >= end || *cursor != '.') {
            break;
        }
        cursor = carbon_sql_skip_ws_comments(cursor + 1, end, &ok);
        if (!ok) {
            free(identifier);
            return NULL;
        }
        segment = carbon_sql_parse_identifier(cursor, end, next);
        if (segment == NULL) {
            free(identifier);
            return NULL;
        }
        free(identifier);
        identifier = segment;
        cursor = *next;
    }
    *next = cursor;
    return identifier;
}

static char *carbon_sql_lower_copy(const char *start, const char *end) {
    char *copy = carbon_strndup_local(start, (size_t) (end - start));
    char *cursor;

    if (copy == NULL) {
        return NULL;
    }
    for (cursor = copy; *cursor != '\0'; ++cursor) {
        *cursor = (char) tolower((unsigned char) *cursor);
    }
    return copy;
}

static int carbon_sql_span_contains_keyword(const char *start, const char *end, const char *keyword) {
    const char *cursor = start;

    while (cursor < end) {
        int ok = 1;
        cursor = carbon_sql_skip_ws_comments(cursor, end, &ok);
        if (!ok) {
            return 0;
        }
        if (cursor >= end) {
            return 0;
        }
        if (*cursor == '\'' || *cursor == '"' || *cursor == '`') {
            cursor = carbon_sql_skip_quoted(cursor, end, *cursor);
            if (cursor == NULL) {
                return 0;
            }
            continue;
        }
        if ((cursor == start || !carbon_sql_word_char(cursor[-1])) && carbon_sql_keyword_at(cursor, end, keyword)) {
            return 1;
        }
        ++cursor;
    }
    return 0;
}

static int carbon_sql_span_contains_phrase(
        const char *start,
        const char *end,
        const char *first,
        const char *second) {
    const char *cursor = start;

    while (cursor < end) {
        int ok = 1;
        cursor = carbon_sql_skip_ws_comments(cursor, end, &ok);
        if (!ok) {
            return 0;
        }
        if (cursor >= end) {
            return 0;
        }
        if (*cursor == '\'' || *cursor == '"' || *cursor == '`') {
            cursor = carbon_sql_skip_quoted(cursor, end, *cursor);
            if (cursor == NULL) {
                return 0;
            }
            continue;
        }
        if ((cursor == start || !carbon_sql_word_char(cursor[-1])) && carbon_sql_keyword_at(cursor, end, first)) {
            const char *after = carbon_sql_skip_ws_comments(cursor + strlen(first), end, &ok);
            if (ok && after < end && carbon_sql_keyword_at(after, end, second)) {
                return 1;
            }
        }
        ++cursor;
    }
    return 0;
}

static const char *carbon_sql_find_matching_paren(const char *open, const char *end) {
    const char *cursor = open;
    int depth = 0;

    while (cursor < end) {
        if (*cursor == '\'' || *cursor == '"' || *cursor == '`') {
            cursor = carbon_sql_skip_quoted(cursor, end, *cursor);
            if (cursor == NULL) {
                return NULL;
            }
            continue;
        }
        if (cursor + 1 < end && cursor[0] == '/' && cursor[1] == '*') {
            int ok = 1;
            cursor = carbon_sql_skip_ws_comments(cursor, end, &ok);
            if (!ok) {
                return NULL;
            }
            continue;
        }
        if (cursor + 1 < end && cursor[0] == '-' && cursor[1] == '-') {
            int ok = 1;
            cursor = carbon_sql_skip_ws_comments(cursor, end, &ok);
            if (!ok) {
                return NULL;
            }
            continue;
        }
        if (*cursor == '#') {
            int ok = 1;
            cursor = carbon_sql_skip_ws_comments(cursor, end, &ok);
            if (!ok) {
                return NULL;
            }
            continue;
        }
        if (*cursor == '(') {
            ++depth;
        } else if (*cursor == ')') {
            --depth;
            if (depth == 0) {
                return cursor;
            }
            if (depth < 0) {
                return NULL;
            }
        }
        ++cursor;
    }
    return NULL;
}

static carbon_status carbon_dump_parse_primary_columns(
        carbon_dump_table *table,
        const char *start,
        const char *end) {
    const char *open;
    const char *close;
    const char *cursor;

    for (open = start; open < end && *open != '('; ++open) {
        if (*open == '\'' || *open == '"' || *open == '`') {
            open = carbon_sql_skip_quoted(open, end, *open);
            if (open == NULL) {
                return CARBON_STATUS_INVALID_QUERY;
            }
            --open;
        }
    }
    if (open >= end || *open != '(') {
        return CARBON_STATUS_INVALID_QUERY;
    }
    close = carbon_sql_find_matching_paren(open, end);
    if (close == NULL) {
        return CARBON_STATUS_INVALID_QUERY;
    }
    cursor = open + 1;
    while (cursor < close) {
        char *column;
        const char *next;
        int ok = 1;
        carbon_status status;

        cursor = carbon_sql_skip_ws_comments(cursor, close, &ok);
        if (!ok) {
            return CARBON_STATUS_INVALID_QUERY;
        }
        if (cursor >= close) {
            break;
        }
        column = carbon_sql_parse_qualified_identifier(cursor, close, &next);
        if (column == NULL) {
            return CARBON_STATUS_INVALID_QUERY;
        }
        status = carbon_dump_mark_primary(table, column);
        if (status != CARBON_STATUS_OK) {
            return status;
        }
        cursor = next;
        while (cursor < close && *cursor != ',') {
            ++cursor;
        }
        if (cursor < close && *cursor == ',') {
            ++cursor;
        }
    }
    return CARBON_STATUS_OK;
}

static int carbon_dump_is_table_constraint(const char *start, const char *end, const char **after_constraint_name) {
    const char *next;
    char *name;

    *after_constraint_name = start;
    if (carbon_sql_keyword_at(start, end, "PRIMARY")) {
        return 1;
    }
    if (carbon_sql_keyword_at(start, end, "KEY")
        || carbon_sql_keyword_at(start, end, "INDEX")
        || carbon_sql_keyword_at(start, end, "UNIQUE")
        || carbon_sql_keyword_at(start, end, "FOREIGN")
        || carbon_sql_keyword_at(start, end, "CHECK")
        || carbon_sql_keyword_at(start, end, "FULLTEXT")
        || carbon_sql_keyword_at(start, end, "SPATIAL")
        || carbon_sql_keyword_at(start, end, "EXCLUDE")) {
        return 1;
    }
    if (!carbon_sql_keyword_at(start, end, "CONSTRAINT")) {
        return 0;
    }
    start = carbon_sql_skip_ws_comments(start + strlen("CONSTRAINT"), end, NULL);
    name = carbon_sql_parse_identifier(start, end, &next);
    free(name);
    if (name == NULL) {
        *after_constraint_name = start;
        return 1;
    }
    *after_constraint_name = carbon_sql_skip_ws_comments(next, end, NULL);
    return 1;
}

static carbon_status carbon_dump_parse_column(
        carbon_dump_table *table,
        const char *start,
        const char *end) {
    carbon_dump_column column;
    const char *cursor;
    const char *type_start;
    const char *type_end;
    int ok = 1;

    memset(&column, 0, sizeof(column));
    start = carbon_sql_skip_ws_comments(start, end, &ok);
    if (!ok || start >= end) {
        return CARBON_STATUS_INVALID_QUERY;
    }
    column.name = carbon_sql_parse_identifier(start, end, &cursor);
    if (column.name == NULL) {
        return CARBON_STATUS_INVALID_QUERY;
    }
    cursor = carbon_sql_skip_ws_comments(cursor, end, &ok);
    if (!ok || cursor >= end) {
        carbon_dump_column_free(&column);
        return CARBON_STATUS_INVALID_QUERY;
    }
    type_start = cursor;
    if (*cursor == '"' || *cursor == '`') {
        char *quoted_type = carbon_sql_copy_quoted_identifier(cursor, end, &cursor);
        if (quoted_type == NULL) {
            carbon_dump_column_free(&column);
            return CARBON_STATUS_INVALID_QUERY;
        }
        column.db_type = quoted_type;
        while (*quoted_type != '\0') {
            *quoted_type = (char) tolower((unsigned char) *quoted_type);
            ++quoted_type;
        }
    } else {
        while (cursor < end && carbon_sql_word_char(*cursor)) {
            ++cursor;
        }
        type_end = cursor;
        if (type_end == type_start) {
            carbon_dump_column_free(&column);
            return CARBON_STATUS_INVALID_QUERY;
        }
        column.db_type = carbon_sql_lower_copy(type_start, type_end);
    }
    if (column.db_type == NULL) {
        carbon_dump_column_free(&column);
        return CARBON_STATUS_OUT_OF_MEMORY;
    }
    cursor = carbon_sql_skip_ws_comments(cursor, end, &ok);
    if (!ok) {
        carbon_dump_column_free(&column);
        return CARBON_STATUS_INVALID_QUERY;
    }
    if (cursor < end && *cursor == '(') {
        const char *close = carbon_sql_find_matching_paren(cursor, end);
        carbon_json_span length_span;

        if (close == NULL) {
            carbon_dump_column_free(&column);
            return CARBON_STATUS_INVALID_QUERY;
        }
        length_span = carbon_sql_trim_span(cursor + 1, close);
        column.max_length = carbon_strndup_local(length_span.start, (size_t) (length_span.end - length_span.start));
        if (column.max_length == NULL) {
            carbon_dump_column_free(&column);
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        cursor = close + 1;
    }

    column.not_null = carbon_sql_span_contains_phrase(cursor, end, "NOT", "NULL")
                      || carbon_sql_span_contains_phrase(cursor, end, "PRIMARY", "KEY");
    column.auto_increment = carbon_sql_span_contains_keyword(cursor, end, "AUTO_INCREMENT")
                            || carbon_sql_span_contains_keyword(cursor, end, "AUTOINCREMENT")
                            || (carbon_sql_span_contains_keyword(cursor, end, "GENERATED")
                                && carbon_sql_span_contains_keyword(cursor, end, "IDENTITY"))
                            || carbon_ascii_case_equals(column.db_type, "serial")
                            || carbon_ascii_case_equals(column.db_type, "bigserial")
                            || carbon_ascii_case_equals(column.db_type, "smallserial");
    column.skip_insert = column.auto_increment;
    column.primary = carbon_sql_span_contains_phrase(cursor, end, "PRIMARY", "KEY");
    if (column.primary) {
        column.not_null = 1;
    }
    {
        char *primary_name = column.primary ? carbon_strndup_local(column.name, strlen(column.name)) : NULL;
        carbon_status status = carbon_dump_add_column(table, &column);
        if (status != CARBON_STATUS_OK) {
            free(primary_name);
            return status;
        }
        if (primary_name != NULL) {
            return carbon_dump_mark_primary(table, primary_name);
        }
    }
    return CARBON_STATUS_OK;
}

static carbon_status carbon_dump_parse_table_element(
        carbon_dump_table *table,
        const char *start,
        const char *end) {
    carbon_json_span element = carbon_sql_trim_span(start, end);
    const char *after_constraint_name = NULL;

    if (element.start >= element.end) {
        return CARBON_STATUS_OK;
    }
    if (carbon_dump_is_table_constraint(element.start, element.end, &after_constraint_name)) {
        const char *primary_start = element.start;

        if (after_constraint_name != NULL
            && after_constraint_name < element.end
            && carbon_sql_keyword_at(after_constraint_name, element.end, "PRIMARY")) {
            primary_start = after_constraint_name;
        }
        if (carbon_sql_keyword_at(primary_start, element.end, "PRIMARY")) {
            return carbon_dump_parse_primary_columns(table, primary_start, element.end);
        }
        return CARBON_STATUS_OK;
    }
    return carbon_dump_parse_column(table, element.start, element.end);
}

static carbon_status carbon_dump_parse_table_body(
        carbon_dump_table *table,
        const char *start,
        const char *end) {
    const char *cursor = start;
    const char *element_start = start;
    int depth = 0;

    while (cursor <= end) {
        if (cursor == end || (*cursor == ',' && depth == 0)) {
            carbon_status status = carbon_dump_parse_table_element(table, element_start, cursor);
            if (status != CARBON_STATUS_OK) {
                return status;
            }
            element_start = cursor + 1;
            ++cursor;
            continue;
        }
        if (*cursor == '\'' || *cursor == '"' || *cursor == '`') {
            cursor = carbon_sql_skip_quoted(cursor, end, *cursor);
            if (cursor == NULL) {
                return CARBON_STATUS_INVALID_QUERY;
            }
            continue;
        }
        if ((cursor + 1 < end && cursor[0] == '/' && cursor[1] == '*')
            || (cursor + 1 < end && cursor[0] == '-' && cursor[1] == '-')
            || *cursor == '#') {
            int ok = 1;

            cursor = carbon_sql_skip_ws_comments(cursor, end, &ok);
            if (!ok) {
                return CARBON_STATUS_INVALID_QUERY;
            }
            continue;
        }
        if (*cursor == '(') {
            ++depth;
        } else if (*cursor == ')') {
            --depth;
            if (depth < 0) {
                return CARBON_STATUS_INVALID_QUERY;
            }
        }
        ++cursor;
    }
    return depth == 0 ? CARBON_STATUS_OK : CARBON_STATUS_INVALID_QUERY;
}

static carbon_status carbon_dump_parse_create_table(
        carbon_dump_schema *schema,
        const char *after_table,
        const char *end,
        const char **next_statement) {
    const char *cursor = after_table;
    const char *open;
    const char *close;
    char *table_name;
    carbon_dump_table *table = NULL;
    carbon_status status;
    int ok = 1;

    cursor = carbon_sql_skip_ws_comments(cursor, end, &ok);
    if (!ok) {
        return CARBON_STATUS_INVALID_QUERY;
    }
    if ((open = carbon_sql_consume_keyword(cursor, end, "IF")) != NULL) {
        cursor = carbon_sql_skip_ws_comments(open, end, &ok);
        if (!ok || (open = carbon_sql_consume_keyword(cursor, end, "NOT")) == NULL) {
            return CARBON_STATUS_INVALID_QUERY;
        }
        cursor = carbon_sql_skip_ws_comments(open, end, &ok);
        if (!ok || (open = carbon_sql_consume_keyword(cursor, end, "EXISTS")) == NULL) {
            return CARBON_STATUS_INVALID_QUERY;
        }
        cursor = open;
    }
    table_name = carbon_sql_parse_qualified_identifier(cursor, end, &cursor);
    status = carbon_dump_add_table(schema, table_name, &table);
    if (status != CARBON_STATUS_OK) {
        return status;
    }
    cursor = carbon_sql_skip_ws_comments(cursor, end, &ok);
    if (!ok) {
        return CARBON_STATUS_INVALID_QUERY;
    }
    open = cursor;
    while (open < end && *open != '(') {
        if (*open == '\'' || *open == '"' || *open == '`') {
            open = carbon_sql_skip_quoted(open, end, *open);
            if (open == NULL) {
                return CARBON_STATUS_INVALID_QUERY;
            }
            continue;
        }
        ++open;
    }
    if (open >= end || *open != '(') {
        return CARBON_STATUS_INVALID_QUERY;
    }
    close = carbon_sql_find_matching_paren(open, end);
    if (close == NULL) {
        return CARBON_STATUS_INVALID_QUERY;
    }
    status = carbon_dump_parse_table_body(table, open + 1, close);
    if (status != CARBON_STATUS_OK) {
        return status;
    }
    *next_statement = close + 1;
    return CARBON_STATUS_OK;
}

static const char *carbon_dump_find_next_create_table(const char *cursor, const char *end, int *ok) {
    *ok = 1;
    while (cursor < end) {
        const char *after_create;
        const char *after_table;

        cursor = carbon_sql_skip_ws_comments(cursor, end, ok);
        if (!*ok || cursor >= end) {
            return NULL;
        }
        if (*cursor == '\'' || *cursor == '"' || *cursor == '`') {
            cursor = carbon_sql_skip_quoted(cursor, end, *cursor);
            if (cursor == NULL) {
                *ok = 0;
                return NULL;
            }
            continue;
        }
        after_create = carbon_sql_consume_keyword(cursor, end, "CREATE");
        if (after_create == NULL) {
            ++cursor;
            continue;
        }
        after_create = carbon_sql_skip_ws_comments(after_create, end, ok);
        if (!*ok) {
            return NULL;
        }
        if ((after_table = carbon_sql_consume_keyword(after_create, end, "TEMPORARY")) != NULL
            || (after_table = carbon_sql_consume_keyword(after_create, end, "TEMP")) != NULL
            || (after_table = carbon_sql_consume_keyword(after_create, end, "UNLOGGED")) != NULL) {
            after_create = carbon_sql_skip_ws_comments(after_table, end, ok);
            if (!*ok) {
                return NULL;
            }
        }
        after_table = carbon_sql_consume_keyword(after_create, end, "TABLE");
        if (after_table != NULL) {
            return after_table;
        }
        cursor = after_create;
    }
    return NULL;
}

static carbon_status carbon_dump_parse_schema(
        const char *sql,
        size_t sql_length,
        carbon_dump_schema *schema) {
    const char *cursor = sql;
    const char *end = sql + sql_length;
    int ok = 1;

    while ((cursor = carbon_dump_find_next_create_table(cursor, end, &ok)) != NULL) {
        carbon_status status = carbon_dump_parse_create_table(schema, cursor, end, &cursor);
        if (status != CARBON_STATUS_OK) {
            return status;
        }
    }
    return ok ? CARBON_STATUS_OK : CARBON_STATUS_INVALID_QUERY;
}

static carbon_status carbon_dump_append_schema_json(
        const carbon_dump_schema *schema,
        carbon_string_builder *json) {
    size_t table_index;

    if (!carbon_builder_append(json, "{\"TABLES\":{")) {
        return CARBON_STATUS_OUT_OF_MEMORY;
    }
    for (table_index = 0; table_index < schema->table_count; ++table_index) {
        const carbon_dump_table *table = &schema->tables[table_index];
        size_t column_index;
        size_t primary_index;

        if (table_index > 0 && !carbon_builder_append_char(json, ',')) {
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        if (!carbon_builder_append_json_string(json, table->name)
            || !carbon_builder_append(json, ":{\"PRIMARY_SHORT\":[")) {
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        for (primary_index = 0; primary_index < table->primary_count; ++primary_index) {
            if (primary_index > 0 && !carbon_builder_append_char(json, ',')) {
                return CARBON_STATUS_OUT_OF_MEMORY;
            }
            if (!carbon_builder_append_json_string(json, table->primary[primary_index])) {
                return CARBON_STATUS_OUT_OF_MEMORY;
            }
        }
        if (!carbon_builder_append(json, "],\"COLUMNS\":{")) {
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        for (column_index = 0; column_index < table->column_count; ++column_index) {
            const carbon_dump_column *column = &table->columns[column_index];

            if (column_index > 0 && !carbon_builder_append_char(json, ',')) {
                return CARBON_STATUS_OUT_OF_MEMORY;
            }
            if (!carbon_builder_append_char(json, '"')
                || !carbon_builder_append(json, table->name)
                || !carbon_builder_append_char(json, '.')
                || !carbon_builder_append(json, column->name)
                || !carbon_builder_append(json, "\":")
                || !carbon_builder_append_json_string(json, column->name)) {
                return CARBON_STATUS_OUT_OF_MEMORY;
            }
        }
        if (!carbon_builder_append(json, "},\"TYPE_VALIDATION\":{")) {
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
        for (column_index = 0; column_index < table->column_count; ++column_index) {
            const carbon_dump_column *column = &table->columns[column_index];

            if (column_index > 0 && !carbon_builder_append_char(json, ',')) {
                return CARBON_STATUS_OUT_OF_MEMORY;
            }
            if (!carbon_builder_append_char(json, '"')
                || !carbon_builder_append(json, table->name)
                || !carbon_builder_append_char(json, '.')
                || !carbon_builder_append(json, column->name)
                || !carbon_builder_append(json, "\":{\"COLUMN_NAME\":")
                || !carbon_builder_append_json_string(json, column->name)
                || !carbon_builder_append(json, ",\"MYSQL_TYPE\":")
                || !carbon_builder_append_json_string(json, column->db_type == NULL ? "" : column->db_type)) {
                return CARBON_STATUS_OUT_OF_MEMORY;
            }
            if (column->max_length != NULL
                && (!carbon_builder_append(json, ",\"MAX_LENGTH\":")
                    || !carbon_builder_append_json_string(json, column->max_length))) {
                return CARBON_STATUS_OUT_OF_MEMORY;
            }
            if (!carbon_builder_append(json, column->auto_increment ? ",\"AUTO_INCREMENT\":true" : ",\"AUTO_INCREMENT\":false")
                || !carbon_builder_append(json, column->not_null ? ",\"NOT_NULL\":true" : ",\"NOT_NULL\":false")
                || !carbon_builder_append(json, column->skip_insert ? ",\"SKIP_COLUMN_IN_POST\":true" : ",\"SKIP_COLUMN_IN_POST\":false")
                || !carbon_builder_append_char(json, '}')) {
                return CARBON_STATUS_OUT_OF_MEMORY;
            }
        }
        if (!carbon_builder_append(json, "}}")) {
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
    }
    return carbon_builder_append(json, "}}") ? CARBON_STATUS_OK : CARBON_STATUS_OUT_OF_MEMORY;
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
        if (state->dialect == CARBON_DIALECT_KIND_POSTGRESQL) {
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
        status = state->dialect == CARBON_DIALECT_KIND_MYSQL
                 ? carbon_append_mysql_upsert_update(state, table, update_columns, sql)
                 : carbon_append_postgresql_upsert_update(state, table, update_columns, sql, error_message);
        if (status != CARBON_STATUS_OK) {
            goto cleanup;
        }
    }

    if (state->dialect == CARBON_DIALECT_KIND_POSTGRESQL
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
    if (state->dialect == CARBON_DIALECT_KIND_MYSQL) {
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
    if (state->dialect == CARBON_DIALECT_KIND_POSTGRESQL && has_join > 0) {
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
        if (state->dialect == CARBON_DIALECT_KIND_POSTGRESQL) {
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

    if (state->dialect == CARBON_DIALECT_KIND_POSTGRESQL
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
    if (state->dialect == CARBON_DIALECT_KIND_MYSQL) {
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
        if (state->dialect == CARBON_DIALECT_KIND_POSTGRESQL) {
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

    if (state->dialect == CARBON_DIALECT_KIND_POSTGRESQL
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

static const char *carbon_diagnostic_message(const carbon_compile_result *result) {
    if (result == NULL) {
        return "compile result is required";
    }
    if (result->error.data != NULL && result->error.length > 0) {
        return result->error.data;
    }
    return carbon_status_message(result->status);
}

static const char *carbon_diagnostic_source(carbon_status status, const char *message) {
    if (status == CARBON_STATUS_UNSUPPORTED_DIALECT) {
        return "dialect";
    }
    if (message == NULL) {
        return "query";
    }
    if (strstr(message, "schema") != NULL
        || strstr(message, "primary key metadata") != NULL
        || strstr(message, "not present in schema") != NULL) {
        return "schema";
    }
    if (strstr(message, "dialect") != NULL) {
        return "dialect";
    }
    if (status == CARBON_STATUS_OUT_OF_MEMORY) {
        return "runtime";
    }
    return "query";
}

static const char *carbon_diagnostic_path(carbon_status status, const char *message) {
    if (status == CARBON_STATUS_UNSUPPORTED_DIALECT
        || (message != NULL && strstr(message, "dialect") != NULL)) {
        return "$.dialect";
    }
    if (message == NULL) {
        return "$";
    }
    if (strcmp(message, "schema_json must be an object") == 0) {
        return "$schema";
    }
    if (strcmp(message, "schema C6 must be an object") == 0) {
        return "$schema.C6";
    }
    if (strcmp(message, "schema TABLES must be an object") == 0) {
        return "$schema.TABLES";
    }
    if (strstr(message, "primary key metadata") != NULL) {
        return "$schema.TABLES.*.PRIMARY_SHORT";
    }
    if (strcmp(message, "FROM/table is required") == 0
        || strcmp(message, "invalid table identifier") == 0
        || strcmp(message, "table is not present in schema") == 0) {
        return "$.FROM";
    }
    if (strstr(message, "JOIN") != NULL || strstr(message, "joined writes") != NULL) {
        return "$.JOIN";
    }
    if (strstr(message, "query_json") != NULL || strstr(message, "query json") != NULL) {
        return "$";
    }
    return "$";
}

carbon_status carbon_compile_result_diagnostics_json(
        const carbon_compile_result *result,
        carbon_buffer *out,
        carbon_buffer *error) {
    carbon_string_builder json = {0};
    carbon_status status;
    const char *status_code;
    const char *message;

    if (out == NULL) {
        if (error != NULL) {
            carbon_buffer_init(error);
            carbon_buffer_set(error, "diagnostics output buffer is required");
        }
        return CARBON_STATUS_INVALID_ARGUMENT;
    }
    carbon_buffer_init(out);
    if (error != NULL) {
        carbon_buffer_init(error);
    }
    if (result == NULL) {
        if (error != NULL) {
            carbon_buffer_set(error, "compile result is required");
        }
        return CARBON_STATUS_INVALID_ARGUMENT;
    }

    status = result->status;
    status_code = carbon_status_code(status);
    message = carbon_diagnostic_message(result);

    if (!carbon_builder_append(&json, "{\"status\":")
        || !carbon_builder_append_format(&json, "%d", (int) status)
        || !carbon_builder_append(&json, ",\"status_code\":")
        || !carbon_builder_append_json_string(&json, status_code)
        || !carbon_builder_append(&json, status == CARBON_STATUS_OK ? ",\"ok\":true" : ",\"ok\":false")
        || !carbon_builder_append(&json, ",\"diagnostics\":[")) {
        carbon_builder_free(&json);
        if (error != NULL) {
            carbon_buffer_set(error, carbon_status_message(CARBON_STATUS_OUT_OF_MEMORY));
        }
        return CARBON_STATUS_OUT_OF_MEMORY;
    }

    if (status != CARBON_STATUS_OK) {
        if (!carbon_builder_append(&json, "{\"severity\":\"error\",\"code\":")
            || !carbon_builder_append_json_string(&json, status_code)
            || !carbon_builder_append(&json, ",\"message\":")
            || !carbon_builder_append_json_string(&json, message)
            || !carbon_builder_append(&json, ",\"source\":")
            || !carbon_builder_append_json_string(&json, carbon_diagnostic_source(status, message))
            || !carbon_builder_append(&json, ",\"path\":")
            || !carbon_builder_append_json_string(&json, carbon_diagnostic_path(status, message))
            || !carbon_builder_append_char(&json, '}')) {
            carbon_builder_free(&json);
            if (error != NULL) {
                carbon_buffer_set(error, carbon_status_message(CARBON_STATUS_OUT_OF_MEMORY));
            }
            return CARBON_STATUS_OUT_OF_MEMORY;
        }
    }

    if (!carbon_builder_append(&json, "]}")
        || !carbon_buffer_take_builder(out, &json)) {
        carbon_builder_free(&json);
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

    state.dialect = CARBON_DIALECT_KIND_MYSQL;
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

carbon_status carbon_schema_from_dump(
        const char *sql,
        size_t sql_length,
        carbon_buffer *out,
        carbon_buffer *error) {
    carbon_dump_schema schema = {0};
    carbon_string_builder json = {0};
    carbon_status status;

    if (out == NULL) {
        if (error != NULL) {
            carbon_buffer_init(error);
            carbon_buffer_set(error, "schema dump output buffer is required");
        }
        return CARBON_STATUS_INVALID_ARGUMENT;
    }
    if (sql == NULL && sql_length > 0) {
        carbon_buffer_init(out);
        if (error != NULL) {
            carbon_buffer_init(error);
            carbon_buffer_set(error, "sql is required when sql_length is greater than zero");
        }
        return CARBON_STATUS_INVALID_ARGUMENT;
    }

    carbon_buffer_init(out);
    if (error != NULL) {
        carbon_buffer_init(error);
    }

    status = carbon_dump_parse_schema(sql == NULL ? "" : sql, sql == NULL ? 0 : sql_length, &schema);
    if (status != CARBON_STATUS_OK) {
        goto fail;
    }
    status = carbon_dump_append_schema_json(&schema, &json);
    if (status != CARBON_STATUS_OK) {
        goto fail;
    }
    if (!carbon_buffer_take_builder(out, &json)) {
        status = CARBON_STATUS_OUT_OF_MEMORY;
        goto fail;
    }
    if (error != NULL) {
        carbon_buffer_set(error, "");
    }
    carbon_dump_schema_free(&schema);
    return CARBON_STATUS_OK;

fail:
    carbon_builder_free(&json);
    carbon_dump_schema_free(&schema);
    if (error != NULL) {
        if (status == CARBON_STATUS_INVALID_QUERY) {
            carbon_buffer_set(error, "schema dump contains unsupported or conflicting CREATE TABLE metadata");
        } else {
            carbon_buffer_set(error, carbon_status_message(status));
        }
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

    status = carbon_append_index_hints_for_target(state, query, table, NULL, sql);
    if (status != CARBON_STATUS_OK) {
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
