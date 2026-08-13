<?php

if (!extension_loaded('carbon')) {
    fwrite(STDERR, "carbon extension is not loaded\n");
    exit(1);
}

require_once __DIR__ . '/carbon_codegen.php';

function carbon_assert(bool $condition, string $message): void
{
    if (!$condition) {
        fwrite(STDERR, $message . "\n");
        exit(1);
    }
}

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

carbon_assert($result['status'] === 0, 'expected compile success: ' . json_encode($result));
carbon_assert($result['status_code'] === 'ok', 'unexpected status code: ' . json_encode($result));
carbon_assert(
    $result['sql'] === 'SELECT actor.actor_id, actor.first_name FROM `actor` WHERE (actor.actor_id) > ? LIMIT 5',
    'unexpected sql: ' . $result['sql']
);
carbon_assert($result['params_json'] === '[10]', 'unexpected params: ' . $result['params_json']);
carbon_assert(
    $result['allowlist_key'] === 'SELECT actor.actor_id, actor.first_name FROM `actor` WHERE (actor.actor_id) > ? LIMIT ?',
    'unexpected allowlist: ' . $result['allowlist_key']
);
$diagnostics = json_decode($result['diagnostics_json'], true);
carbon_assert(
    $diagnostics === [
        'status' => 0,
        'status_code' => 'ok',
        'ok' => true,
        'diagnostics' => [],
    ],
    'unexpected success diagnostics: ' . $result['diagnostics_json']
);
$metadata = json_decode(carbon_schema_metadata(json_encode($schema)), true);
carbon_assert(
    $metadata === [
        'tables' => [
            [
                'name' => 'actor',
                'columns' => [
                    [
                        'name' => 'actor_id',
                        'qualified' => 'actor.actor_id',
                        'db_type' => 'smallint',
                        'max_length' => '',
                        'nullable' => false,
                        'auto_increment' => true,
                        'skip_insert' => false,
                    ],
                    [
                        'name' => 'first_name',
                        'qualified' => 'actor.first_name',
                        'db_type' => 'varchar',
                        'max_length' => '45',
                        'nullable' => false,
                        'auto_increment' => false,
                        'skip_insert' => false,
                    ],
                ],
                'primary' => ['actor_id'],
            ],
        ],
    ],
    'unexpected metadata: ' . json_encode($metadata)
);
$models = carbon_schema_models($schema, 'CarbonORM\\Generated');
carbon_assert(strpos($models, 'namespace CarbonORM\\Generated;') !== false, 'expected generated namespace');
carbon_assert(strpos($models, 'final class Actor') !== false, 'expected generated Actor class');
carbon_assert(strpos($models, "public const PRIMARY = ['actor_id'];") !== false, 'expected generated primary metadata');
carbon_assert(strpos($models, "public const DB_TYPES = ['actor_id' => 'smallint', 'first_name' => 'varchar'];") !== false, 'expected generated DB type metadata');
carbon_assert(strpos($models, "public const NULLABLE = ['actor_id' => false, 'first_name' => false];") !== false, 'expected generated nullable metadata');
carbon_assert(strpos($models, '/** @var int */') !== false, 'expected generated int docblock');
carbon_assert(strpos($models, '/** @var string */') !== false, 'expected generated string docblock');
carbon_assert(strpos($models, 'public $actor_id;') !== false, 'expected generated actor_id property');

$rejected = carbon_compile_query(
    json_encode(['FROM' => 'actor', 'SELECT' => ['actor.last_name']]),
    json_encode($schema),
    'mysql'
);
carbon_assert($rejected['status'] === 3, 'expected invalid query rejection: ' . json_encode($rejected));
carbon_assert(
    $rejected['status_code'] === 'invalid_query',
    'unexpected rejection status code: ' . json_encode($rejected)
);

$rejectedTable = carbon_compile_query(
    json_encode(['FROM' => 'film', 'SELECT' => ['film.film_id']]),
    json_encode($schema),
    'mysql'
);
carbon_assert($rejectedTable['status'] === 3, 'expected invalid table rejection: ' . json_encode($rejectedTable));
carbon_assert(
    $rejectedTable['status_code'] === 'invalid_query',
    'unexpected table rejection status code: ' . json_encode($rejectedTable)
);
carbon_assert(
    $rejectedTable['error'] === 'table is not present in schema',
    'unexpected table rejection message: ' . json_encode($rejectedTable)
);
$rejectedDiagnostics = json_decode($rejectedTable['diagnostics_json'], true);
carbon_assert(
    $rejectedDiagnostics === [
        'status' => 3,
        'status_code' => 'invalid_query',
        'ok' => false,
        'diagnostics' => [
            [
                'severity' => 'error',
                'code' => 'invalid_query',
                'message' => 'table is not present in schema',
                'source' => 'schema',
                'path' => '$.FROM',
            ],
        ],
    ],
    'unexpected table rejection diagnostics: ' . $rejectedTable['diagnostics_json']
);

carbon_assert(carbon_status_code(3) === 'invalid_query', 'unexpected direct status code');
carbon_assert(
    carbon_normalize_allowlist_sql('SELECT * FROM `actor` LIMIT 10') === 'SELECT * FROM `actor` LIMIT ?',
    'unexpected allowlist normalization'
);

echo "php binding smoke: ok\n";
