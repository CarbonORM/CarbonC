<?php

if (!extension_loaded('carbon')) {
    fwrite(STDERR, "Run with: php -d extension=bindings/php/modules/carbon.so examples/php/index.php\n");
    exit(1);
}

require_once __DIR__ . '/../../bindings/php/carbon_codegen.php';

echo carbon_version();
echo PHP_EOL;

$schema = [
    'TABLES' => [
        'actor' => [
            'PRIMARY_SHORT' => ['actor_id'],
            'COLUMNS' => [
                'actor.actor_id' => 'actor_id',
                'actor.first_name' => 'first_name',
            ],
            'TYPE_VALIDATION' => [
                'actor.actor_id' => [
                    'COLUMN_NAME' => 'actor_id',
                    'MYSQL_TYPE' => 'smallint',
                    'MAX_LENGTH' => '',
                    'AUTO_INCREMENT' => true,
                    'NOT_NULL' => true,
                    'SKIP_COLUMN_IN_POST' => false,
                ],
                'actor.first_name' => [
                    'COLUMN_NAME' => 'first_name',
                    'MYSQL_TYPE' => 'varchar',
                    'MAX_LENGTH' => '45',
                    'AUTO_INCREMENT' => false,
                    'NOT_NULL' => true,
                    'SKIP_COLUMN_IN_POST' => false,
                ],
            ],
        ],
    ],
];

$query = [
    'FROM' => 'actor',
    'SELECT' => ['actor.actor_id', 'actor.first_name'],
    'WHERE' => ['actor.actor_id' => ['>', 10]],
    'PAGINATION' => ['LIMIT' => 5],
];

$result = carbon_compile_query(json_encode($query), json_encode($schema), 'mysql');

if ($result['status'] !== 0) {
    fwrite(STDERR, $result['error'] . PHP_EOL);
    exit(1);
}

echo $result['sql'] . PHP_EOL;
echo $result['params_json'] . PHP_EOL;
echo $result['allowlist_key'] . PHP_EOL;
echo $result['diagnostics_json'] . PHP_EOL;
echo carbon_schema_metadata(json_encode($schema)) . PHP_EOL;
echo carbon_schema_models($schema, 'CarbonORM\\Generated') . PHP_EOL;
