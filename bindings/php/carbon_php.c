// bindings/php/carbon_php.c
#include "carbon_php.h"

PHP_FUNCTION(carbon_version) {
        const char* version = carbon_version();
        RETURN_STRING(version);
}

PHP_FUNCTION(carbon_hello_world) {
        helloWorld();
}

const zend_function_entry carbon_functions[] = {
        PHP_FE(carbon_version, NULL),
        PHP_FE(carbon_hello_world, NULL),
        PHP_FE_END
};

zend_module_entry carbon_module_entry = {
        STANDARD_MODULE_HEADER,
        "carbon",
        carbon_functions,
        NULL, // Module init
        NULL, // Module shutdown
        NULL, // Request init
        NULL, // Request shutdown
        NULL, // Module info
        "1.0",
        STANDARD_MODULE_PROPERTIES
};

ZEND_GET_MODULE(carbon)
