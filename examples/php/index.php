<?php

if (!extension_loaded('carbon')) {
    fwrite(STDERR, "Run with: php -d extension=bindings/php/modules/carbon.so examples/php/index.php\n");
    exit(1);
}

require_once __DIR__ . '/../../bindings/php/carbon_codegen.php';

echo carbon_version();
echo PHP_EOL;

$schemaDump = <<<'SQL'
CREATE TABLE `actor` (
  `actor_id` smallint unsigned NOT NULL AUTO_INCREMENT,
  `first_name` varchar(45) NOT NULL,
  PRIMARY KEY (`actor_id`)
);
SQL;

$schema = carbon_schema_from_dump($schemaDump);

$modelSource = carbon_schema_models($schema);
$globalModelsEval = preg_replace('/^<\\?php\\s*/', '', $modelSource);
if (!is_string($globalModelsEval)) {
    fwrite(STDERR, "could not prepare generated model source\n");
    exit(1);
}
eval($globalModelsEval);

$getRequest = Actor::Get(
    [
        C6C::SELECT => [Actor::ACTOR_ID, Actor::FIRST_NAME],
        C6C::WHERE => [
            Actor::ACTOR_ID => carbon_eq_lit(10),
        ],
        C6C::PAGINATION => [C6C::LIMIT => 5],
    ],
    $schema,
    CarbonDialect::MYSQL
);

$result = carbon_compile_query_result($getRequest['query'], $getRequest['schema'], $getRequest['dialect']);

if ($result['status'] !== 0) {
    fwrite(STDERR, $result['error'] . PHP_EOL);
    exit(1);
}

echo $result['sql'] . PHP_EOL;
echo json_encode($result['params']) . PHP_EOL;
echo $result['allowlist_key'] . PHP_EOL;
echo json_encode($result['diagnostics']) . PHP_EOL;
echo carbon_schema_metadata($schema) . PHP_EOL;
echo carbon_schema_models($schema, 'CarbonORM\\Generated') . PHP_EOL;
