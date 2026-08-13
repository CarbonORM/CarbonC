<?php

if (!extension_loaded('carbon')) {
    fwrite(STDERR, "Run with: php -d extension=bindings/php/modules/carbon.so examples/php/index.php\n");
    exit(1);
}

echo carbon_version();
echo PHP_EOL;

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

if ($result['status'] !== 0) {
    fwrite(STDERR, $result['error'] . PHP_EOL);
    exit(1);
}

echo $result['sql'] . PHP_EOL;
echo $result['params_json'] . PHP_EOL;
echo $result['allowlist_key'] . PHP_EOL;
