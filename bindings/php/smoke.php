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

carbon_assert(C6C::FROM === 'FROM', 'unexpected C6C FROM constant');
carbon_assert(C6C::EQUAL === '=', 'unexpected C6C EQUAL constant');
carbon_assert(C6C::GREATER_THAN === '>', 'unexpected C6C GREATER_THAN constant');
carbon_assert(carbon_eq_lit(10) === ['=', ['LIT', 10]], 'unexpected carbon_eq_lit payload');
carbon_assert(carbon_in_lit([1, 2]) === ['IN', [['LIT', 1], ['LIT', 2]]], 'unexpected carbon_in_lit payload');
carbon_assert(carbon_not_in_lit([1, 2]) === ['NOT_IN', [['LIT', 1], ['LIT', 2]]], 'unexpected carbon_not_in_lit payload');
carbon_assert(carbon_between_lit(1, 2) === ['BETWEEN', [['LIT', 1], ['LIT', 2]]], 'unexpected carbon_between_lit payload');
carbon_assert(CarbonDialect::MYSQL === 'mysql', 'unexpected CarbonDialect MYSQL constant');
carbon_assert(CarbonDialect::POSTGRESQL === 'postgresql', 'unexpected CarbonDialect POSTGRESQL constant');
carbon_assert(CarbonDialect::POSTGRES === 'postgres', 'unexpected CarbonDialect POSTGRES constant');

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

$result = carbon_compile_query(json_encode($query), json_encode($schema), CarbonDialect::MYSQL);

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
$ergonomic = carbon_compile_query_value($query, $schema, CarbonDialect::MYSQL);
carbon_assert($ergonomic === $result, 'unexpected ergonomic compile result: ' . json_encode($ergonomic));
$topOrdered = carbon_compile_query_value(
    [
        'FROM' => 'actor',
        'SELECT' => ['actor.actor_id', 'actor.first_name'],
        'WHERE' => ['actor.actor_id' => ['>', 10]],
        'ORDER' => [['actor.first_name', 'ASC']],
        'PAGINATION' => ['LIMIT' => 25],
    ],
    $schema,
    'mysql'
);
carbon_assert(
    $topOrdered['sql'] === 'SELECT actor.actor_id, actor.first_name FROM `actor` WHERE (actor.actor_id) > ? ORDER BY actor.first_name ASC LIMIT 25',
    'unexpected top-level order sql: ' . $topOrdered['sql']
);
carbon_assert(
    $topOrdered['allowlist_key'] === 'SELECT actor.actor_id, actor.first_name FROM `actor` WHERE (actor.actor_id) > ? ORDER BY actor.first_name ASC LIMIT ?',
    'unexpected top-level order allowlist: ' . $topOrdered['allowlist_key']
);
$adapted = carbon_adapt_compile_result($result);
$expectedAdapted = $result;
$expectedAdapted['params'] = [10];
$expectedAdapted['diagnostics'] = $diagnostics;
carbon_assert($adapted === $expectedAdapted, 'unexpected adapted compile result: ' . json_encode($adapted));
$typed = carbon_compile_query_result($query, $schema, CarbonDialect::MYSQL);
carbon_assert($typed === $adapted, 'unexpected typed compile result: ' . json_encode($typed));
$built = carbon_query('actor')
    ->select('actor.actor_id', 'actor.first_name')
    ->where(['actor.actor_id' => ['>', 10]])
    ->limit(5);
carbon_assert($built->toPayload() === $query, 'unexpected builder payload: ' . json_encode($built->toPayload()));
carbon_assert($built->compile($schema, CarbonDialect::MYSQL) === $adapted, 'unexpected builder compile result');
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
carbon_assert(
    carbon_force_index('idx_county_id', 'idx_property_units_location') === [
        'FORCE INDEX' => ['idx_county_id', 'idx_property_units_location'],
    ],
    'unexpected force index helper payload'
);
$hintPayload = carbon_query('property_units')
    ->select('property_units.unit_id')
    ->forceIndex('idx_county_id', 'idx_property_units_location')
    ->limit(10)
    ->toPayload();
carbon_assert(
    $hintPayload === [
        'FROM' => 'property_units',
        'SELECT' => ['property_units.unit_id'],
        'INDEX_HINTS' => ['FORCE INDEX' => ['idx_county_id', 'idx_property_units_location']],
        'PAGINATION' => ['LIMIT' => 10],
    ],
    'unexpected index hint builder payload: ' . json_encode($hintPayload)
);
$hintedJoin = carbon_query('actor')
    ->select('actor.actor_id', 'fa.film_id')
    ->join('INNER', 'film_actor fa', ['fa.actor_id' => ['=', 'actor.actor_id']])
    ->indexHints([
        'actor' => carbon_ignore_index('idx_actor_last_name'),
        'film_actor fa' => carbon_use_index('idx_film_actor_actor_id'),
    ])
    ->limit(5)
    ->compile(null, CarbonDialect::MYSQL);
carbon_assert(
    $hintedJoin['sql'] === 'SELECT actor.actor_id, fa.film_id FROM `actor` IGNORE INDEX (`idx_actor_last_name`) INNER JOIN `film_actor` AS `fa` USE INDEX (`idx_film_actor_actor_id`) ON ((fa.actor_id) = actor.actor_id) LIMIT 5',
    'unexpected hinted join sql: ' . $hintedJoin['sql']
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
    ->compile(null, CarbonDialect::MYSQL);
carbon_assert($derived['status'] === 0, 'unexpected derived compile status: ' . json_encode($derived));
carbon_assert(
    $derived['sql'] === 'SELECT actor.actor_id, fa_recent.actor_id FROM `actor` INNER JOIN (SELECT film_actor.actor_id FROM `film_actor` WHERE (film_actor.film_id) > ? LIMIT 1) AS `fa_recent` ON ((fa_recent.actor_id) = actor.actor_id) WHERE (actor.actor_id) > ? LIMIT 100',
    'unexpected derived sql: ' . $derived['sql']
);
carbon_assert($derived['params'] === [10, 100], 'unexpected derived params: ' . json_encode($derived['params']));
$salesQuery = carbon_query('parcel_sales')
    ->select('parcel_sales.parcel_id')
    ->whereOp('parcel_sales.sale_price', C6C::GREATER_THAN, 5000);
carbon_assert(
    carbon_alias(carbon_call('COUNT', 'parcel_sales.parcel_id'), 'sale_count') === [
        'AS',
        ['COUNT', 'parcel_sales.parcel_id'],
        'sale_count',
    ],
    'unexpected alias helper payload'
);
carbon_assert(
    carbon_fn('CONCAT', carbon_lit('A'), carbon_lit('B')) === ['CONCAT', ['LIT', 'A'], ['LIT', 'B']],
    'unexpected function helper payload'
);
carbon_assert(
    carbon_custom_call('COALESCE', carbon_lit('UNKNOWN'), 'actor.last_name') === [
        'CALL',
        'COALESCE',
        ['LIT', 'UNKNOWN'],
        'actor.last_name',
    ],
    'unexpected custom call helper payload'
);
$customSelected = carbon_query('actor')
    ->select([carbon_alias(carbon_custom_call('COALESCE', carbon_lit('UNKNOWN'), 'actor.first_name'), 'display_name')])
    ->limit(1)
    ->compile(null, CarbonDialect::MYSQL);
carbon_assert(
    $customSelected['sql'] === 'SELECT COALESCE(?, actor.first_name) AS display_name FROM `actor` LIMIT 1',
    'unexpected custom call select sql: ' . $customSelected['sql']
);
carbon_assert(
    $customSelected['params'] === ['UNKNOWN'],
    'unexpected custom call select params: ' . json_encode($customSelected['params'])
);
$spatialPolygon = 'POLYGON((39.5185659 -105.0142915,39.5401859 -105.0142915,39.5401859 -104.9862115,39.5185659 -104.9862115,39.5185659 -105.0142915))';
$spatialInnerPolygon = 'POLYGON((0 0,1 0,1 1,0 1,0 0))';
carbon_assert(
    carbon_mbr_contains('property_units.envelope', 'property_units.location') === [
        'MBRContains',
        'property_units.envelope',
        'property_units.location',
    ],
    'unexpected MBRContains helper payload'
);
$spatialFiltered = carbon_query('property_units')
    ->select('property_units.unit_id')
    ->where([
        'MBRContains' => [
            carbon_fn('ST_GeomFromText', carbon_lit($spatialPolygon), 4326),
            'property_units.location',
        ],
        'OR' => [
            carbon_st_within(
                'property_units.location',
                carbon_fn('ST_GeomFromText', carbon_lit($spatialInnerPolygon), 4326)
            ),
            carbon_st_contains('property_units.envelope', 'property_units.location'),
        ],
    ])
    ->limit(10)
    ->compile(null, CarbonDialect::MYSQL);
carbon_assert(
    $spatialFiltered['sql'] === 'SELECT property_units.unit_id FROM `property_units` WHERE MBRCONTAINS(ST_GEOMFROMTEXT(?, 4326), property_units.location) AND (ST_WITHIN(property_units.location, ST_GEOMFROMTEXT(?, 4326)) OR ST_CONTAINS(property_units.envelope, property_units.location)) LIMIT 10',
    'unexpected spatial predicate sql: ' . $spatialFiltered['sql']
);
carbon_assert(
    $spatialFiltered['params'] === [$spatialPolygon, $spatialInnerPolygon],
    'unexpected spatial predicate params: ' . json_encode($spatialFiltered['params'])
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
    ->compile(null, CarbonDialect::MYSQL);
carbon_assert($advanced['status'] === 0, 'unexpected advanced compile status: ' . json_encode($advanced));
carbon_assert(
    $advanced['sql'] === 'SELECT property_units.unit_id FROM `property_units` WHERE (property_units.unit_id) BETWEEN ? AND ? AND ( property_units.parcel_id IN (SELECT parcel_sales.parcel_id FROM `parcel_sales` WHERE (parcel_sales.sale_price) > ?) ) AND ( property_units.account_id NOT IN (?, ?) ) AND EXISTS (SELECT parcel_sales.parcel_id FROM `parcel_sales` WHERE (parcel_sales.sale_price) > ? AND (parcel_sales.parcel_id) = property_units.parcel_id) LIMIT 3',
    'unexpected advanced sql: ' . $advanced['sql']
);
carbon_assert(
    $advanced['params'] === [1, 10, 5000, 99, 100, 5000],
    'unexpected advanced params: ' . json_encode($advanced['params'])
);
carbon_assert(
    carbon_and_group(
        carbon_condition('actor.actor_id', carbon_op('>', 2)),
        carbon_or_group(
            carbon_condition('actor.first_name', carbon_op('LIKE', carbon_lit('A%'))),
            carbon_condition('actor.first_name', carbon_op('LIKE', carbon_lit('B%')))
        )
    ) === [
        'AND' => [
            ['actor.actor_id' => ['>', 2]],
            [
                'OR' => [
                    ['actor.first_name' => ['LIKE', ['LIT', 'A%']]],
                    ['actor.first_name' => ['LIKE', ['LIT', 'B%']]],
                ],
            ],
        ],
    ],
    'unexpected boolean group helper payload'
);
carbon_assert(
    carbon_match_against('alpha beta', 'BOOLEAN') === ['MATCH_AGAINST', [['LIT', 'alpha beta'], 'BOOLEAN']],
    'unexpected MATCH_AGAINST helper payload'
);
$fulltext = carbon_query('actor')
    ->select('actor.actor_id')
    ->whereMatchAgainst('actor.first_name', 'alpha beta', 'BOOLEAN')
    ->limit(10)
    ->compile($schema, CarbonDialect::MYSQL);
carbon_assert($fulltext['status'] === 0, 'unexpected full-text compile status: ' . json_encode($fulltext));
carbon_assert(
    $fulltext['sql'] === 'SELECT actor.actor_id FROM `actor` WHERE (MATCH(actor.first_name) AGAINST(? IN BOOLEAN MODE)) LIMIT 10',
    'unexpected full-text sql: ' . $fulltext['sql']
);
carbon_assert($fulltext['params'] === ['alpha beta'], 'unexpected full-text params: ' . json_encode($fulltext['params']));
$booleanGrouped = carbon_query('actor')
    ->select('actor.actor_id')
    ->whereBetween('actor.actor_id', 1, 10)
    ->whereOr(
        carbon_condition('actor.first_name', carbon_op('LIKE', carbon_lit('A%'))),
        carbon_condition('actor.first_name', carbon_op('LIKE', carbon_lit('B%')))
    )
    ->whereAnd(
        carbon_condition('actor.actor_id', carbon_op('>', 2)),
        carbon_condition('actor.actor_id', carbon_op('<', 9))
    )
    ->limit(5)
    ->compile(null, CarbonDialect::MYSQL);
carbon_assert($booleanGrouped['status'] === 0, 'unexpected boolean group status: ' . json_encode($booleanGrouped));
carbon_assert(
    $booleanGrouped['sql'] === 'SELECT actor.actor_id FROM `actor` WHERE (actor.actor_id) BETWEEN ? AND ? AND ((actor.first_name) LIKE ? OR (actor.first_name) LIKE ?) AND ((actor.actor_id) > ? AND (actor.actor_id) < ?) LIMIT 5',
    'unexpected boolean group sql: ' . $booleanGrouped['sql']
);
carbon_assert(
    $booleanGrouped['params'] === [1, 10, 'A%', 'B%', 2, 9],
    'unexpected boolean group params: ' . json_encode($booleanGrouped['params'])
);
$grouped = carbon_query('actor')
    ->select(['DISTINCT', 'actor.first_name'], ['AS', ['COUNT', 'actor.actor_id'], 'cnt'])
    ->groupBy('actor.first_name')
    ->having(['cnt' => ['>', 1]])
    ->page(2)
    ->limit(5)
    ->compile(null, CarbonDialect::MYSQL);
carbon_assert($grouped['status'] === 0, 'unexpected grouped compile status: ' . json_encode($grouped));
carbon_assert(
    $grouped['sql'] === 'SELECT DISTINCT actor.first_name, COUNT(actor.actor_id) AS cnt FROM `actor` GROUP BY actor.first_name HAVING ((cnt) > ?) LIMIT 5, 5',
    'unexpected grouped sql: ' . $grouped['sql']
);
carbon_assert($grouped['params'] === [1], 'unexpected grouped params: ' . json_encode($grouped['params']));
$inserted = carbon_query('actor')
    ->insert(['actor.first_name' => 'ALICE'])
    ->compile($schema, CarbonDialect::MYSQL);
carbon_assert($inserted['status'] === 0, 'unexpected insert compile status: ' . json_encode($inserted));
carbon_assert(
    $inserted['sql'] === 'INSERT INTO `actor` (`first_name`) VALUES (?)',
    'unexpected insert sql: ' . $inserted['sql']
);
carbon_assert($inserted['params'] === ['ALICE'], 'unexpected insert params: ' . json_encode($inserted['params']));
$expressionInserted = carbon_query('actor')
    ->insert([
        'actor.first_name' => carbon_fn('CONCAT', carbon_lit('HEL'), carbon_lit('LO')),
        'actor.last_name' => 'SMITH',
    ])
    ->compile(null, CarbonDialect::MYSQL);
carbon_assert(
    $expressionInserted['sql'] === 'INSERT INTO `actor` (`first_name`, `last_name`) VALUES (CONCAT(?, ?), ?)',
    'unexpected expression insert sql: ' . $expressionInserted['sql']
);
carbon_assert(
    $expressionInserted['params'] === ['HEL', 'LO', 'SMITH'],
    'unexpected expression insert params: ' . json_encode($expressionInserted['params'])
);
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
    ->compile($schema, CarbonDialect::MYSQL);
carbon_assert(
    $updated['sql'] === 'UPDATE `actor` SET `first_name` = ? WHERE (actor.actor_id) = ?',
    'unexpected update sql: ' . $updated['sql']
);
carbon_assert($updated['params'] === ['BOB', 1], 'unexpected update params: ' . json_encode($updated['params']));
$expressionUpdated = carbon_query('actor')
    ->update([
        'actor.first_name' => carbon_fn('CONCAT', carbon_lit('Mr. '), 'actor.last_name'),
        'actor.last_name' => carbon_custom_call('COALESCE', carbon_lit('UNKNOWN'), 'actor.last_name'),
    ])
    ->where(['actor.actor_id' => ['=', 7]])
    ->compile(null, CarbonDialect::MYSQL);
carbon_assert(
    $expressionUpdated['sql'] === 'UPDATE `actor` SET `first_name` = CONCAT(?, actor.last_name), `last_name` = COALESCE(?, actor.last_name) WHERE (actor.actor_id) = ?',
    'unexpected expression update sql: ' . $expressionUpdated['sql']
);
carbon_assert(
    $expressionUpdated['params'] === ['Mr. ', 'UNKNOWN', 7],
    'unexpected expression update params: ' . json_encode($expressionUpdated['params'])
);
$deleted = carbon_query('actor')
    ->delete()
    ->where(['actor.actor_id' => 1])
    ->compile($schema, CarbonDialect::MYSQL);
carbon_assert(
    $deleted['sql'] === 'DELETE `actor` FROM `actor` WHERE (actor.actor_id) = ?',
    'unexpected delete sql: ' . $deleted['sql']
);
carbon_assert($deleted['params'] === [1], 'unexpected delete params: ' . json_encode($deleted['params']));
$upserted = carbon_query('actor')
    ->insert(['actor.actor_id' => 1, 'actor.first_name' => 'ALICE'])
    ->upsert(['first_name'])
    ->compile($schema, CarbonDialect::MYSQL);
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
carbon_assert(strpos($models, "public const FIELD_ACTOR_ID = 'actor_id';") !== false, 'expected generated actor_id field constant');
carbon_assert(strpos($models, "public const FIELD_FIRST_NAME = 'first_name';") !== false, 'expected generated first_name field constant');
carbon_assert(strpos($models, "public const FIELDS = [") !== false, 'expected generated FIELDS constant');
carbon_assert(strpos($models, "public const ACTOR_ID = 'actor.actor_id';") !== false, 'expected generated actor_id constant');
carbon_assert(strpos($models, "public const FIRST_NAME = 'actor.first_name';") !== false, 'expected generated first_name constant');
carbon_assert(strpos($models, "public const PRIMARY = ['actor_id'];") !== false, 'expected generated primary metadata');
carbon_assert(strpos($models, "public const DB_TYPES = ['actor_id' => 'smallint', 'first_name' => 'varchar'];") !== false, 'expected generated DB type metadata');
carbon_assert(strpos($models, "public const NULLABLE = ['actor_id' => false, 'first_name' => false];") !== false, 'expected generated nullable metadata');
carbon_assert(strpos($models, "self::FIELD_ACTOR_ID => self::ACTOR_ID") !== false, 'expected generated COLUMNS to use constants');
carbon_assert(strpos($models, 'public static function GetPayload(array $query = []): array') !== false, 'expected generated GetPayload method');
carbon_assert(strpos($models, 'public static function Get(array $query = [], $schema = null') !== false, 'expected generated Get method');
carbon_assert(strpos($models, '/** @var int */') !== false, 'expected generated int docblock');
carbon_assert(strpos($models, '/** @var string */') !== false, 'expected generated string docblock');
carbon_assert(strpos($models, 'public $actor_id;') !== false, 'expected generated actor_id property');
$globalModels = carbon_schema_models($schema);
$globalModelsEval = preg_replace('/^<\\?php\\s*/', '', $globalModels);
carbon_assert(is_string($globalModelsEval), 'expected generated global model source');
eval($globalModelsEval);
carbon_assert(carbon_model_table(Actor::class) === 'actor', 'unexpected model table');
carbon_assert(Actor::TABLE === 'actor', 'unexpected generated TABLE constant');
carbon_assert(Actor::FIELD_ACTOR_ID === 'actor_id', 'unexpected generated FIELD_ACTOR_ID constant');
carbon_assert(Actor::FIELD_FIRST_NAME === 'first_name', 'unexpected generated FIELD_FIRST_NAME constant');
carbon_assert(Actor::FIELDS === ['actor_id', 'first_name'], 'unexpected generated FIELDS constant');
carbon_assert(Actor::ACTOR_ID === 'actor.actor_id', 'unexpected generated ACTOR_ID constant');
carbon_assert(Actor::FIRST_NAME === 'actor.first_name', 'unexpected generated FIRST_NAME constant');
carbon_assert(Actor::COLUMNS['actor_id'] === Actor::ACTOR_ID, 'unexpected generated COLUMNS constant value');
carbon_assert(carbon_model_column(Actor::class, Actor::FIELD_FIRST_NAME) === 'actor.first_name', 'unexpected model column');
carbon_assert(
    carbon_model_select(Actor::class)->toPayload() === [
        'FROM' => 'actor',
        'SELECT' => ['actor.actor_id', 'actor.first_name'],
    ],
    'unexpected model select payload'
);
$constantBuilt = carbon_query(Actor::TABLE)
    ->select(Actor::ACTOR_ID, Actor::FIRST_NAME)
    ->whereOp(Actor::ACTOR_ID, C6C::GREATER_THAN, 0)
    ->orderBy(Actor::FIRST_NAME, C6C::ASC)
    ->limit(1)
    ->compile($schema, CarbonDialect::MYSQL);
carbon_assert(
    $constantBuilt['sql'] === 'SELECT actor.actor_id, actor.first_name FROM `actor` WHERE (actor.actor_id) > ? ORDER BY actor.first_name ASC LIMIT 1',
    'unexpected constant-built query sql: ' . $constantBuilt['sql']
);
carbon_assert($constantBuilt['params'] === [0], 'unexpected constant-built query params: ' . json_encode($constantBuilt['params']));
$modelBuilt = carbon_model_select(Actor::class, Actor::FIELD_ACTOR_ID)
    ->whereOp(Actor::ACTOR_ID, C6C::GREATER_THAN, 0)
    ->limit(1)
    ->compile($schema, CarbonDialect::MYSQL);
carbon_assert(
    $modelBuilt['sql'] === 'SELECT actor.actor_id FROM `actor` WHERE (actor.actor_id) > ? LIMIT 1',
    'unexpected model query sql: ' . $modelBuilt['sql']
);
carbon_assert($modelBuilt['params'] === [0], 'unexpected model query params: ' . json_encode($modelBuilt['params']));
carbon_assert(
    carbon_model_values(Actor::class, [Actor::FIELD_FIRST_NAME => 'ALICE']) === ['actor.first_name' => 'ALICE'],
    'unexpected model values payload'
);
$getQuery = [
    C6C::SELECT => [Actor::ACTOR_ID],
    C6C::WHERE => [
        Actor::ACTOR_ID => carbon_eq_lit(10),
    ],
    C6C::PAGINATION => [C6C::LIMIT => 500],
    'cacheResults' => false,
];
$getPayload = Actor::GetPayload($getQuery);
carbon_assert(
    $getPayload === [
        'SELECT' => ['actor.actor_id'],
        'WHERE' => ['actor.actor_id' => ['=', ['LIT', 10]]],
        'PAGINATION' => ['LIMIT' => 500],
        'cacheResults' => false,
        'FROM' => 'actor',
    ],
    'unexpected model get payload: ' . json_encode($getPayload)
);
carbon_assert(
    carbon_route_query($getPayload, ['deviceClass' => 'mobile'], ['serverOnMobile' => true])
        === ['target' => 'server', 'reason' => 'mobile_offload'],
    'unexpected mobile route'
);
$getRequest = Actor::Get(
    $getPayload,
    $schema,
    CarbonDialect::MYSQL,
    ['canRunLocal' => false]
);
carbon_assert($getRequest['method'] === 'Get', 'unexpected model get request method');
carbon_assert($getRequest['model'] === 'actor', 'unexpected model get request model');
carbon_assert($getRequest['cacheResults'] === false, 'unexpected model get cache flag');
carbon_assert(
    $getRequest['route'] === ['target' => 'server', 'reason' => 'local_unavailable'],
    'unexpected model get route: ' . json_encode($getRequest['route'])
);
$getResult = carbon_compile_query_result($getRequest['query'], $getRequest['schema'], $getRequest['dialect']);
carbon_assert(
    $getResult['sql'] === 'SELECT actor.actor_id FROM `actor` WHERE (actor.actor_id) = ? LIMIT 500',
    'unexpected model get sql: ' . $getResult['sql']
);
$inLitResult = carbon_compile_query_result(
    [
        C6C::FROM => Actor::TABLE,
        C6C::SELECT => [Actor::ACTOR_ID],
        C6C::WHERE => [Actor::ACTOR_ID => carbon_in_lit([1, 2])],
    ],
    $schema,
    CarbonDialect::MYSQL
);
carbon_assert(
    $inLitResult['sql'] === 'SELECT actor.actor_id FROM `actor` WHERE ( actor.actor_id IN (?, ?) ) LIMIT 100',
    'unexpected in-lit sql: ' . $inLitResult['sql']
);
carbon_assert($inLitResult['params'] === [1, 2], 'unexpected in-lit params: ' . json_encode($inLitResult['params']));
$modelInserted = carbon_model_insert(Actor::class, [Actor::FIELD_FIRST_NAME => 'ALICE'])
    ->compile($schema, CarbonDialect::MYSQL);
carbon_assert(
    $modelInserted['sql'] === 'INSERT INTO `actor` (`first_name`) VALUES (?)',
    'unexpected model insert sql: ' . $modelInserted['sql']
);
carbon_assert($modelInserted['params'] === ['ALICE'], 'unexpected model insert params: ' . json_encode($modelInserted['params']));
$modelUpdated = carbon_model_update(Actor::class, [Actor::FIELD_FIRST_NAME => 'BOB'])
    ->whereOp(Actor::ACTOR_ID, C6C::GREATER_THAN, 0)
    ->compile($schema, CarbonDialect::MYSQL);
carbon_assert(
    $modelUpdated['sql'] === 'UPDATE `actor` SET `first_name` = ? WHERE (actor.actor_id) > ?',
    'unexpected model update sql: ' . $modelUpdated['sql']
);
carbon_assert($modelUpdated['params'] === ['BOB', 0], 'unexpected model update params: ' . json_encode($modelUpdated['params']));
$modelUpserted = carbon_model_upsert(
    Actor::class,
    [Actor::FIELD_ACTOR_ID => 1, Actor::FIELD_FIRST_NAME => 'ALICE'],
    [Actor::FIELD_FIRST_NAME]
)->compile($schema, CarbonDialect::MYSQL);
carbon_assert(
    $modelUpserted['sql'] === 'INSERT INTO `actor` (`actor_id`, `first_name`) VALUES (?, ?) ON DUPLICATE KEY UPDATE `first_name` = VALUES(`first_name`)',
    'unexpected model upsert sql: ' . $modelUpserted['sql']
);
carbon_assert($modelUpserted['params'] === [1, 'ALICE'], 'unexpected model upsert params: ' . json_encode($modelUpserted['params']));
carbon_assert(
    carbon_model_replace(Actor::class, [Actor::FIELD_FIRST_NAME => 'BOB'])->toPayload() === [
        'FROM' => 'actor',
        'REPLACE' => ['actor.first_name' => 'BOB'],
    ],
    'unexpected model replace payload'
);
carbon_assert(
    carbon_model_do_nothing(Actor::class, [Actor::FIELD_ACTOR_ID => 1])->toPayload() === [
        'FROM' => 'actor',
        'INSERT' => ['actor.actor_id' => 1],
        'UPDATE' => [],
    ],
    'unexpected model do-nothing payload'
);

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
