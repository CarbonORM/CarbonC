#include "carbon.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

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
            "{\"table\":\"carbon_users\","
            "\"select\":[\"user_id\",\"user_email\"],"
            "\"where\":{\"user_id\":42},"
            "\"limit\":1}";
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
    assert_buffer_equals(&result.sql, "SELECT `user_id`, `user_email` FROM `carbon_users` WHERE `user_id` = ? LIMIT ?");
    assert_buffer_equals(&result.params_json, "[42,1]");
    assert_buffer_equals(&result.allowlist_key,
                         "select user_id, user_email from carbon_users where user_id = ? limit ?");

    carbon_compile_result_free(&result);
    carbon_context_free(context);
}

static void test_postgresql_select_string_param(void) {
    carbon_context *context = carbon_context_new();
    const char query[] =
            "{\"dialect\":\"postgresql\","
            "\"table\":\"public.accounts\","
            "\"select\":[\"account_id\",\"email\"],"
            "\"where\":{\"email\":\"richard@example.com\"}}";
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
                         "SELECT \"account_id\", \"email\" FROM \"public\".\"accounts\" WHERE \"email\" = ?");
    assert_buffer_equals(&result.params_json, "[\"richard@example.com\"]");
    assert_buffer_equals(&result.allowlist_key,
                         "select account_id, email from public.accounts where email = ?");

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
    assert_buffer_equals(&result.error, "invalid query");

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
    assert_buffer_equals(&out, "select users.id from users where users.id = ?");
    assert_buffer_equals(&error, "");

    carbon_buffer_free(&out);
    carbon_buffer_free(&error);
}

static void test_multiple_where_rejected_for_v0(void) {
    carbon_context *context = carbon_context_new();
    const char query[] =
            "{\"table\":\"carbon_users\","
            "\"where\":{\"user_id\":42,\"user_email\":\"richard@example.com\"}}";
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
    assert(carbon_compile_query(context, &request, &result) == CARBON_STATUS_UNSUPPORTED_QUERY);
    assert(result.status == CARBON_STATUS_UNSUPPORTED_QUERY);

    carbon_compile_result_free(&result);
    carbon_context_free(context);
}

int main(void) {
    test_version();
    test_mysql_select_where_limit();
    test_postgresql_select_string_param();
    test_identifier_rejection();
    test_allowlist_normalizer();
    test_multiple_where_rejected_for_v0();
    puts("carbonc_tests: ok");
    return 0;
}
