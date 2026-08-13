#ifndef CARBONC_CARBON_H
#define CARBONC_CARBON_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CARBON_VERSION_MAJOR 0
#define CARBON_VERSION_MINOR 1
#define CARBON_VERSION_PATCH 0
#define CARBON_VERSION "0.1.0"

typedef enum carbon_status {
    CARBON_STATUS_OK = 0,
    CARBON_STATUS_INVALID_ARGUMENT = 1,
    CARBON_STATUS_INVALID_JSON = 2,
    CARBON_STATUS_INVALID_QUERY = 3,
    CARBON_STATUS_UNSUPPORTED_DIALECT = 4,
    CARBON_STATUS_UNSUPPORTED_QUERY = 5,
    CARBON_STATUS_OUT_OF_MEMORY = 6
} carbon_status;

typedef struct carbon_context carbon_context;

typedef struct carbon_buffer {
    char *data;
    size_t length;
} carbon_buffer;

typedef struct carbon_compile_request {
    const char *dialect;
    const char *schema_json;
    size_t schema_json_length;
    const char *query_json;
    size_t query_json_length;
} carbon_compile_request;

typedef struct carbon_compile_result {
    carbon_status status;
    carbon_buffer sql;
    carbon_buffer params_json;
    carbon_buffer allowlist_key;
    carbon_buffer error;
} carbon_compile_result;

const char *carbon_version(void);
const char *carbon_hello_world(void);
const char *carbon_status_code(carbon_status status);
const char *carbon_status_message(carbon_status status);

carbon_context *carbon_context_new(void);
void carbon_context_free(carbon_context *context);

void carbon_buffer_init(carbon_buffer *buffer);
void carbon_buffer_free(carbon_buffer *buffer);
void carbon_compile_result_init(carbon_compile_result *result);
void carbon_compile_result_free(carbon_compile_result *result);

/*
 * Compiles the v0.1 canonical query payload into SQL, params JSON, and an
 * allowlist key. If schema_json contains a TABLES object, table names,
 * unqualified current-table references, dotted references, join aliases, and
 * write columns are validated against that schema. PostgreSQL upsert conflict
 * targets are derived from PRIMARY_SHORT or PRIMARY schema metadata, and
 * PostgreSQL INNER joined updates/deletes compile through UPDATE ... FROM and
 * DELETE ... USING. Root-level POST row payloads compile as inserts when no
 * read controls are present. The result is initialized by this function. If a
 * caller reuses a prior result object, it must call carbon_compile_result_free()
 * first.
 */
carbon_status carbon_compile_query(
        carbon_context *context,
        const carbon_compile_request *request,
        carbon_compile_result *result);

/*
 * Normalizes generated SQL into the deterministic allowlist key used by
 * CarbonORM, including LIMIT, IN bind-list, parenthesized bind-group, and
 * multi-row VALUES cardinality normalization. The output buffers are
 * initialized by this function and must be released by the caller.
 */
carbon_status carbon_normalize_allowlist_sql(
        const char *sql,
        carbon_buffer *out,
        carbon_buffer *error);

#ifdef __cplusplus
}
#endif

#endif
