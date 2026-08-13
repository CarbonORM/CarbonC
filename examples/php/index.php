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

$modelSource = carbon_schema_models($schema);
$globalModelsEval = preg_replace('/^<\\?php\\s*/', '', $modelSource);
if (!is_string($globalModelsEval)) {
    fwrite(STDERR, "could not prepare generated model source\n");
    exit(1);
}
eval($globalModelsEval);

$query = [
    C6C::FROM => Actor::TABLE,
    C6C::SELECT => [Actor::ACTOR_ID, Actor::FIRST_NAME],
    C6C::WHERE => [
        Actor::ACTOR_ID => carbon_op(C6C::GREATER_THAN, 10),
    ],
    C6C::PAGINATION => [C6C::LIMIT => 5],
];

$result = carbon_compile_query_result($query, $schema, CarbonDialect::MYSQL);

if ($result['status'] !== 0) {
    fwrite(STDERR, $result['error'] . PHP_EOL);
    exit(1);
}

echo $result['sql'] . PHP_EOL;
echo json_encode($result['params']) . PHP_EOL;
echo $result['allowlist_key'] . PHP_EOL;
echo json_encode($result['diagnostics']) . PHP_EOL;
echo carbon_schema_metadata(json_encode($schema)) . PHP_EOL;
echo carbon_schema_models($schema, 'CarbonORM\\Generated') . PHP_EOL;
