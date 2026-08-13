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
$ergonomic = carbon_compile_query_value($query, $schema, 'mysql');
carbon_assert($ergonomic === $result, 'unexpected ergonomic compile result: ' . json_encode($ergonomic));
$adapted = carbon_adapt_compile_result($result);
$expectedAdapted = $result;
$expectedAdapted['params'] = [10];
$expectedAdapted['diagnostics'] = $diagnostics;
carbon_assert($adapted === $expectedAdapted, 'unexpected adapted compile result: ' . json_encode($adapted));
$typed = carbon_compile_query_result($query, $schema, 'mysql');
carbon_assert($typed === $adapted, 'unexpected typed compile result: ' . json_encode($typed));
$built = carbon_query('actor')
    ->select('actor.actor_id', 'actor.first_name')
    ->where(['actor.actor_id' => ['>', 10]])
    ->limit(5);
carbon_assert($built->toPayload() === $query, 'unexpected builder payload: ' . json_encode($built->toPayload()));
carbon_assert($built->compile($schema, 'mysql') === $adapted, 'unexpected builder compile result');
$orderedPayload = carbon_query()
    ->fromTable('actor')
    ->select(['actor.actor_id'])
    ->orderBy('actor.first_name', 'DESC')
    ->limit(5)
    ->toPayload();
carbon_assert(
    $orderedPayload === [
        'FROM' => 'actor',
        'SELECT' => ['actor.actor_id'],
        'PAGINATION' => ['ORDER' => [['actor.first_name', 'DESC']], 'LIMIT' => 5],
    ],
    'unexpected ordered builder payload: ' . json_encode($orderedPayload)
);
$joinPayload = carbon_query('actor')
    ->select('actor.actor_id')
    ->join('INNER', 'film_actor fa', ['fa.actor_id' => ['=', 'actor.actor_id']])
    ->toPayload();
carbon_assert(
    $joinPayload === [
        'FROM' => 'actor',
        'SELECT' => ['actor.actor_id'],
        'JOIN' => ['INNER' => ['film_actor fa' => ['fa.actor_id' => ['=', 'actor.actor_id']]]],
    ],
    'unexpected join builder payload: ' . json_encode($joinPayload)
);
$recentActorIds = carbon_query('film_actor')
    ->select('film_actor.actor_id')
    ->where(['film_actor.film_id' => ['>', 10]])
    ->limit(1);
carbon_assert(
    carbon_subselect($recentActorIds) === [
        'SUBSELECT',
        [
            'FROM' => 'film_actor',
            'SELECT' => ['film_actor.actor_id'],
            'WHERE' => ['film_actor.film_id' => ['>', 10]],
            'PAGINATION' => ['LIMIT' => 1],
        ],
    ],
    'unexpected subselect helper payload'
);
$derived = carbon_query('actor')
    ->select('actor.actor_id', 'fa_recent.actor_id')
    ->joinSubselect('INNER', 'fa_recent', $recentActorIds, ['fa_recent.actor_id' => ['=', 'actor.actor_id']])
    ->where(['actor.actor_id' => ['>', 100]])
    ->compile(null, 'mysql');
carbon_assert($derived['status'] === 0, 'unexpected derived compile status: ' . json_encode($derived));
carbon_assert(
    $derived['sql'] === 'SELECT actor.actor_id, fa_recent.actor_id FROM `actor` INNER JOIN (SELECT film_actor.actor_id FROM `film_actor` WHERE (film_actor.film_id) > ? LIMIT 1) AS `fa_recent` ON ((fa_recent.actor_id) = actor.actor_id) WHERE (actor.actor_id) > ? LIMIT 100',
    'unexpected derived sql: ' . $derived['sql']
);
carbon_assert($derived['params'] === [10, 100], 'unexpected derived params: ' . json_encode($derived['params']));
$salesQuery = carbon_query('parcel_sales')
    ->select('parcel_sales.parcel_id')
    ->whereOp('parcel_sales.sale_price', '>', 5000);
carbon_assert(
    carbon_alias(carbon_call('COUNT', 'parcel_sales.parcel_id'), 'sale_count') === [
        'AS',
        ['COUNT', 'parcel_sales.parcel_id'],
        'sale_count',
    ],
    'unexpected alias helper payload'
);
carbon_assert(carbon_lit('2023-01-01') === ['LIT', '2023-01-01'], 'unexpected literal helper payload');
carbon_assert(
    carbon_exists_spec('property_units.parcel_id', $salesQuery) === [
        'property_units.parcel_id',
        [
            'SUBSELECT',
            [
                'FROM' => 'parcel_sales',
                'SELECT' => ['parcel_sales.parcel_id'],
                'WHERE' => ['parcel_sales.sale_price' => ['>', 5000]],
            ],
        ],
    ],
    'unexpected exists spec payload'
);
$advanced = carbon_query('property_units')
    ->select('property_units.unit_id')
    ->whereBetween('property_units.unit_id', 1, 10)
    ->whereIn('property_units.parcel_id', $salesQuery)
    ->whereNotIn('property_units.account_id', [99, 100])
    ->whereExists('property_units.parcel_id', $salesQuery)
    ->limit(3)
    ->compile(null, 'mysql');
carbon_assert($advanced['status'] === 0, 'unexpected advanced compile status: ' . json_encode($advanced));
carbon_assert(
    $advanced['sql'] === 'SELECT property_units.unit_id FROM `property_units` WHERE (property_units.unit_id) BETWEEN ? AND ? AND ( property_units.parcel_id IN (SELECT parcel_sales.parcel_id FROM `parcel_sales` WHERE (parcel_sales.sale_price) > ?) ) AND ( property_units.account_id NOT IN (?, ?) ) AND EXISTS (SELECT parcel_sales.parcel_id FROM `parcel_sales` WHERE (parcel_sales.sale_price) > ? AND (parcel_sales.parcel_id) = property_units.parcel_id) LIMIT 3',
    'unexpected advanced sql: ' . $advanced['sql']
);
carbon_assert(
    $advanced['params'] === [1, 10, 5000, 99, 100, 5000],
    'unexpected advanced params: ' . json_encode($advanced['params'])
);
$grouped = carbon_query('actor')
    ->select(['DISTINCT', 'actor.first_name'], ['AS', ['COUNT', 'actor.actor_id'], 'cnt'])
    ->groupBy('actor.first_name')
    ->having(['cnt' => ['>', 1]])
    ->page(2)
    ->limit(5)
    ->compile(null, 'mysql');
carbon_assert($grouped['status'] === 0, 'unexpected grouped compile status: ' . json_encode($grouped));
carbon_assert(
    $grouped['sql'] === 'SELECT DISTINCT actor.first_name, COUNT(actor.actor_id) AS cnt FROM `actor` GROUP BY actor.first_name HAVING ((cnt) > ?) LIMIT 5, 5',
    'unexpected grouped sql: ' . $grouped['sql']
);
carbon_assert($grouped['params'] === [1], 'unexpected grouped params: ' . json_encode($grouped['params']));
$inserted = carbon_query('actor')
    ->insert(['actor.first_name' => 'ALICE'])
    ->compile($schema, 'mysql');
carbon_assert($inserted['status'] === 0, 'unexpected insert compile status: ' . json_encode($inserted));
carbon_assert(
    $inserted['sql'] === 'INSERT INTO `actor` (`first_name`) VALUES (?)',
    'unexpected insert sql: ' . $inserted['sql']
);
carbon_assert($inserted['params'] === ['ALICE'], 'unexpected insert params: ' . json_encode($inserted['params']));
carbon_assert(
    carbon_query('actor')->replace(['actor.first_name' => 'BOB'])->toPayload() === [
        'FROM' => 'actor',
        'REPLACE' => ['actor.first_name' => 'BOB'],
    ],
    'unexpected replace builder payload'
);
$updated = carbon_query('actor')
    ->update(['actor.first_name' => 'BOB'])
    ->where(['actor.actor_id' => 1])
    ->compile($schema, 'mysql');
carbon_assert(
    $updated['sql'] === 'UPDATE `actor` SET `first_name` = ? WHERE (actor.actor_id) = ?',
    'unexpected update sql: ' . $updated['sql']
);
carbon_assert($updated['params'] === ['BOB', 1], 'unexpected update params: ' . json_encode($updated['params']));
$deleted = carbon_query('actor')
    ->delete()
    ->where(['actor.actor_id' => 1])
    ->compile($schema, 'mysql');
carbon_assert(
    $deleted['sql'] === 'DELETE `actor` FROM `actor` WHERE (actor.actor_id) = ?',
    'unexpected delete sql: ' . $deleted['sql']
);
carbon_assert($deleted['params'] === [1], 'unexpected delete params: ' . json_encode($deleted['params']));
$upserted = carbon_query('actor')
    ->insert(['actor.actor_id' => 1, 'actor.first_name' => 'ALICE'])
    ->upsert(['first_name'])
    ->compile($schema, 'mysql');
carbon_assert(
    $upserted['sql'] === 'INSERT INTO `actor` (`actor_id`, `first_name`) VALUES (?, ?) ON DUPLICATE KEY UPDATE `first_name` = VALUES(`first_name`)',
    'unexpected upsert sql: ' . $upserted['sql']
);
carbon_assert($upserted['params'] === [1, 'ALICE'], 'unexpected upsert params: ' . json_encode($upserted['params']));
carbon_assert(
    carbon_query('actor')->insert(['actor.actor_id' => 1])->doNothing()->toPayload() === [
        'FROM' => 'actor',
        'INSERT' => ['actor.actor_id' => 1],
        'UPDATE' => [],
    ],
    'unexpected do-nothing builder payload'
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
