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
$metadata = json_decode(carbon_schema_metadata(json_encode($schema)), true);
carbon_assert(
    $metadata === [
        'tables' => [
            [
                'name' => 'actor',
                'columns' => [
                    ['name' => 'actor_id', 'qualified' => 'actor.actor_id'],
                    ['name' => 'first_name', 'qualified' => 'actor.first_name'],
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

carbon_assert(carbon_status_code(3) === 'invalid_query', 'unexpected direct status code');
carbon_assert(
    carbon_normalize_allowlist_sql('SELECT * FROM `actor` LIMIT 10') === 'SELECT * FROM `actor` LIMIT ?',
    'unexpected allowlist normalization'
);

echo "php binding smoke: ok\n";
