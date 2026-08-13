<?php

if (!extension_loaded('carbon')) {
    fwrite(STDERR, "carbon extension is not loaded\n");
    exit(1);
}

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
carbon_assert(
    $result['sql'] === 'SELECT actor.actor_id, actor.first_name FROM `actor` WHERE (actor.actor_id) > ? LIMIT 5',
    'unexpected sql: ' . $result['sql']
);
carbon_assert($result['params_json'] === '[10]', 'unexpected params: ' . $result['params_json']);
carbon_assert(
    $result['allowlist_key'] === 'SELECT actor.actor_id, actor.first_name FROM `actor` WHERE (actor.actor_id) > ? LIMIT ?',
    'unexpected allowlist: ' . $result['allowlist_key']
);

$rejected = carbon_compile_query(
    json_encode(['FROM' => 'actor', 'SELECT' => ['actor.last_name']]),
    json_encode($schema),
    'mysql'
);
carbon_assert($rejected['status'] === 3, 'expected invalid query rejection: ' . json_encode($rejected));

carbon_assert(
    carbon_normalize_allowlist_sql('SELECT * FROM `actor` LIMIT 10') === 'SELECT * FROM `actor` LIMIT ?',
    'unexpected allowlist normalization'
);

echo "php binding smoke: ok\n";
