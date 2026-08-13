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

#define CARBON_DIALECT_MYSQL "mysql"
#define CARBON_DIALECT_POSTGRESQL "postgresql"
#define CARBON_DIALECT_POSTGRES "postgres"

#define CARBON_C6_AS "AS"
#define CARBON_C6_ASC "ASC"
#define CARBON_C6_AND "AND"
#define CARBON_C6_BETWEEN "BETWEEN"
#define CARBON_C6_CALL "CALL"
#define CARBON_C6_CONCAT "CONCAT"
#define CARBON_C6_COUNT "COUNT"
#define CARBON_C6_DELETE "DELETE"
#define CARBON_C6_DESC "DESC"
#define CARBON_C6_DISTINCT "DISTINCT"
#define CARBON_C6_EQUAL "="
#define CARBON_C6_EXISTS "EXISTS"
#define CARBON_C6_FORCE_INDEX "FORCE INDEX"
#define CARBON_C6_FROM "FROM"
#define CARBON_C6_GREATER_THAN ">"
#define CARBON_C6_GREATER_THAN_OR_EQUAL_TO ">="
#define CARBON_C6_GROUP_BY "GROUP_BY"
#define CARBON_C6_GROUP_CONCAT "GROUP_CONCAT"
#define CARBON_C6_HAVING "HAVING"
#define CARBON_C6_IGNORE_INDEX "IGNORE INDEX"
#define CARBON_C6_IN "IN"
#define CARBON_C6_INDEX_HINTS "INDEX_HINTS"
#define CARBON_C6_INNER "INNER"
#define CARBON_C6_INSERT "INSERT"
#define CARBON_C6_IS "IS"
#define CARBON_C6_IS_NOT "IS_NOT"
#define CARBON_C6_JOIN "JOIN"
#define CARBON_C6_LEFT "LEFT"
#define CARBON_C6_LEFT_OUTER "LEFT_OUTER"
#define CARBON_C6_LESS_THAN "<"
#define CARBON_C6_LESS_THAN_OR_EQUAL_TO "<="
#define CARBON_C6_LIKE "LIKE"
#define CARBON_C6_LIMIT "LIMIT"
#define CARBON_C6_LIT "LIT"
#define CARBON_C6_MATCH_AGAINST "MATCH_AGAINST"
#define CARBON_C6_MBRCONTAINS "MBRContains"
#define CARBON_C6_MIN "MIN"
#define CARBON_C6_MAX "MAX"
#define CARBON_C6_NOT_BETWEEN "NOT BETWEEN"
#define CARBON_C6_NOT_EQUAL "<>"
#define CARBON_C6_NOT_EXISTS "NOT_EXISTS"
#define CARBON_C6_NOT_IN "NOT_IN"
#define CARBON_C6_NOT_LIKE "NOT_LIKE"
#define CARBON_C6_OR "OR"
#define CARBON_C6_ORDER "ORDER"
#define CARBON_C6_PAGE "PAGE"
#define CARBON_C6_PAGINATION "PAGINATION"
#define CARBON_C6_PARAM "PARAM"
#define CARBON_C6_REPLACE "REPLACE"
#define CARBON_C6_RIGHT "RIGHT"
#define CARBON_C6_RIGHT_OUTER "RIGHT_OUTER"
#define CARBON_C6_SELECT "SELECT"
#define CARBON_C6_ST_CONTAINS "ST_Contains"
#define CARBON_C6_ST_GEOMFROMTEXT "ST_GeomFromText"
#define CARBON_C6_ST_WITHIN "ST_Within"
#define CARBON_C6_SUBSELECT "SUBSELECT"
#define CARBON_C6_SUM "SUM"
#define CARBON_C6_UPDATE "UPDATE"
#define CARBON_C6_USE_INDEX "USE INDEX"
#define CARBON_C6_WHERE "WHERE"

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
 * write columns are validated against that schema. Schema COLUMNS metadata also
 * normalizes INSERT, UPDATE, and upsert update-list column ordering.
 * PostgreSQL upsert conflict targets are derived from PRIMARY_SHORT or PRIMARY
 * schema metadata, and PostgreSQL INNER joined updates/deletes compile through
 * UPDATE ... FROM and DELETE ... USING. Stringified derived JOIN targets compile
 * to subselect aliases for normal JOIN SQL. Root-level POST row payloads compile
 * as inserts when no read controls are present. The result is initialized by
 * this function. If a caller reuses a prior result object, it must call
 * carbon_compile_result_free() first.
 */
carbon_status carbon_compile_query(
        carbon_context *context,
        const carbon_compile_request *request,
        carbon_compile_result *result);

/*
 * Builds a binding-friendly diagnostic JSON document from a compile result.
 * The result struct layout stays unchanged; language bindings can expose this
 * string alongside the existing status, status_code, and error fields.
 */
carbon_status carbon_compile_result_diagnostics_json(
        const carbon_compile_result *result,
        carbon_buffer *out,
        carbon_buffer *error);

/*
 * Normalizes C6 schema metadata into a deterministic JSON shape for generated
 * binding types. The output contains a tables array, each with name, ordered
 * columns, and primary key columns. The output buffers are initialized by this
 * function and must be released by the caller.
 */
carbon_status carbon_schema_metadata(
        const char *schema_json,
        size_t schema_json_length,
        carbon_buffer *out,
        carbon_buffer *error);

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
