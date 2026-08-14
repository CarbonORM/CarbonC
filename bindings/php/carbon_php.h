#ifndef CARBON_PHP_H
#define CARBON_PHP_H

#include "php.h"
#include "carbon.h"

PHP_FUNCTION(carbon_version);
PHP_FUNCTION(carbon_hello_world);
PHP_FUNCTION(carbon_status_code);
PHP_FUNCTION(carbon_status_message);
PHP_FUNCTION(carbon_compile_query);
PHP_FUNCTION(carbon_schema_metadata);
PHP_FUNCTION(carbon_schema_from_dump);
PHP_FUNCTION(carbon_schema_model_source);
PHP_FUNCTION(carbon_normalize_allowlist_sql);

#endif
