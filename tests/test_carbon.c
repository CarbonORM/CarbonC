#include "carbon.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CARBONC_TEST_SOURCE_DIR
#define CARBONC_TEST_SOURCE_DIR "."
#endif

static void assert_buffer_equals(const carbon_buffer *buffer, const char *expected) {
    assert(buffer != NULL);
    assert(buffer->data != NULL);
    if (strcmp(buffer->data, expected) != 0) {
        fprintf(stderr, "expected: %s\nactual:   %s\n", expected, buffer->data);
    }
    assert(strcmp(buffer->data, expected) == 0);
    assert(buffer->length == strlen(expected));
}

static void test_version(void) {
    assert(strcmp(carbon_version(), "0.1.0") == 0);
    assert(strcmp(carbon_hello_world(), "CarbonC portable kernel") == 0);
}

static void test_mysql_select_where_limit(void) {
    carbon_context *context = carbon_context_new();
    const char query[] =
            "{\"FROM\":\"carbon_users\","
            "\"SELECT\":[\"user_id\",\"user_email\"],"
            "\"WHERE\":{\"user_id\":42},"
            "\"PAGINATION\":{\"LIMIT\":1}}";
    carbon_compile_request request = {
            .dialect = "mysql",
            .schema_json = "{}",
            .schema_json_length = 2,
            .query_json = query,
            .query_json_length = sizeof(query) - 1
    };
    carbon_compile_result result;

    carbon_compile_result_init(&result);
    assert(context != NULL);
    assert(carbon_compile_query(context, &request, &result) == CARBON_STATUS_OK);
    assert(result.status == CARBON_STATUS_OK);
    assert_buffer_equals(&result.sql, "SELECT user_id, user_email FROM `carbon_users` WHERE (user_id) = ? LIMIT 1");
    assert_buffer_equals(&result.params_json, "[42]");
    assert_buffer_equals(&result.allowlist_key,
                         "SELECT user_id, user_email FROM `carbon_users` WHERE (user_id) = ? LIMIT ?");

    carbon_compile_result_free(&result);
    carbon_context_free(context);
}

static void test_postgresql_select_string_param(void) {
    carbon_context *context = carbon_context_new();
    const char query[] =
            "{\"dialect\":\"postgresql\","
            "\"FROM\":\"public.accounts\","
            "\"SELECT\":[\"account_id\",\"email\"],"
            "\"WHERE\":{\"email\":[\"=\",[\"LIT\",\"richard@example.com\"]]}}";
    carbon_compile_request request = {
            .dialect = "mysql",
            .schema_json = "{}",
            .schema_json_length = 2,
            .query_json = query,
            .query_json_length = sizeof(query) - 1
    };
    carbon_compile_result result;

    carbon_compile_result_init(&result);
    assert(context != NULL);
    assert(carbon_compile_query(context, &request, &result) == CARBON_STATUS_OK);
    assert_buffer_equals(&result.sql,
                         "SELECT account_id, email FROM \"public.accounts\" WHERE (email) = $1 LIMIT 100");
    assert_buffer_equals(&result.params_json, "[\"richard@example.com\"]");
    assert_buffer_equals(&result.allowlist_key,
                         "SELECT account_id, email FROM \"public.accounts\" WHERE (email) = $1 LIMIT ?");

    carbon_compile_result_free(&result);
    carbon_context_free(context);
}

static void test_identifier_rejection(void) {
    carbon_context *context = carbon_context_new();
    const char query[] = "{\"table\":\"carbon users\",\"select\":[\"user_id\"]}";
    carbon_compile_request request = {
            .dialect = "mysql",
            .schema_json = "{}",
            .schema_json_length = 2,
            .query_json = query,
            .query_json_length = sizeof(query) - 1
    };
    carbon_compile_result result;

    carbon_compile_result_init(&result);
    assert(context != NULL);
    assert(carbon_compile_query(context, &request, &result) == CARBON_STATUS_INVALID_QUERY);
    assert(result.status == CARBON_STATUS_INVALID_QUERY);
    assert_buffer_equals(&result.error, "invalid table identifier");

    carbon_compile_result_free(&result);
    carbon_context_free(context);
}

static void test_allowlist_normalizer(void) {
    carbon_buffer out;
    carbon_buffer error;

    assert(carbon_normalize_allowlist_sql(
                   " SELECT  `users`.`id`  FROM  `users` \n WHERE `users`.`id` = ? ",
                   &out,
                   &error) == CARBON_STATUS_OK);
    assert_buffer_equals(&out, "SELECT `users`.`id` FROM `users` WHERE `users`.`id` = ?");
    assert_buffer_equals(&error, "");

    carbon_buffer_free(&out);
    carbon_buffer_free(&error);
}

static void test_multiple_where_uses_and(void) {
    carbon_context *context = carbon_context_new();
    const char query[] =
            "{\"table\":\"carbon_users\","
            "\"where\":{\"user_id\":42,\"user_email\":[\"=\",[\"LIT\",\"richard@example.com\"]]}}";
    carbon_compile_request request = {
            .dialect = "mysql",
            .schema_json = "{}",
            .schema_json_length = 2,
            .query_json = query,
            .query_json_length = sizeof(query) - 1
    };
    carbon_compile_result result;

    carbon_compile_result_init(&result);
    assert(context != NULL);
    assert(carbon_compile_query(context, &request, &result) == CARBON_STATUS_OK);
    assert(result.status == CARBON_STATUS_OK);
    assert_buffer_equals(&result.sql,
                         "SELECT * FROM `carbon_users` WHERE (user_id) = ? AND (user_email) = ? LIMIT 100");
    assert_buffer_equals(&result.params_json, "[42,\"richard@example.com\"]");

    carbon_compile_result_free(&result);
    carbon_context_free(context);
}

static char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    long size;
    char *contents;

    assert(file != NULL);
    assert(fseek(file, 0, SEEK_END) == 0);
    size = ftell(file);
    assert(size >= 0);
    rewind(file);

    contents = (char *) malloc((size_t) size + 1);
    assert(contents != NULL);
    assert(fread(contents, 1, (size_t) size, file) == (size_t) size);
    contents[size] = '\0';
    fclose(file);
    return contents;
}

static char *copy_trimmed(const char *start, const char *end) {
    char *copy;
    size_t length;

    while (start < end && (*start == '\n' || *start == '\r' || *start == '\t' || *start == ' ')) {
        ++start;
    }
    while (end > start && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == '\t' || end[-1] == ' ')) {
        --end;
    }
    length = (size_t) (end - start);
    copy = (char *) malloc(length + 1);
    assert(copy != NULL);
    memcpy(copy, start, length);
    copy[length] = '\0';
    return copy;
}

static char *fixture_section(const char *raw, const char *marker, const char *next_marker) {
    const char *start = strstr(raw, marker);
    const char *end;

    assert(start != NULL);
    start += strlen(marker);
    end = next_marker == NULL ? raw + strlen(raw) : strstr(start, next_marker);
    assert(end != NULL);
    return copy_trimmed(start, end);
}

static void run_fixture(const char *fixture_name) {
    char path[1024];
    char *raw;
    char *query;
    char *sql;
    char *params;
    char *allowlist;
    carbon_context *context = carbon_context_new();
    carbon_compile_request request;
    carbon_compile_result result;

    snprintf(path, sizeof(path), "%s/tests/fixtures/%s.case", CARBONC_TEST_SOURCE_DIR, fixture_name);
    raw = read_file(path);
    query = fixture_section(raw, "-- query", "-- sql");
    sql = fixture_section(raw, "-- sql", "-- params");
    params = fixture_section(raw, "-- params", "-- allowlist");
    allowlist = fixture_section(raw, "-- allowlist", NULL);

    request.dialect = "mysql";
    request.schema_json = "{}";
    request.schema_json_length = 2;
    request.query_json = query;
    request.query_json_length = strlen(query);

    carbon_compile_result_init(&result);
    assert(context != NULL);
    assert(carbon_compile_query(context, &request, &result) == CARBON_STATUS_OK);
    assert_buffer_equals(&result.sql, sql);
    assert_buffer_equals(&result.params_json, params);
    assert_buffer_equals(&result.allowlist_key, allowlist);

    carbon_compile_result_free(&result);
    carbon_context_free(context);
    free(raw);
    free(query);
    free(sql);
    free(params);
    free(allowlist);
}

static void test_golden_fixtures(void) {
    run_fixture("select-pagination");
    run_fixture("spatial-order");
    run_fixture("where-in-between");
}

int main(void) {
    test_version();
    test_mysql_select_where_limit();
    test_postgresql_select_string_param();
    test_identifier_rejection();
    test_allowlist_normalizer();
    test_multiple_where_uses_and();
    test_golden_fixtures();
    puts("carbonc_tests: ok");
    return 0;
}
