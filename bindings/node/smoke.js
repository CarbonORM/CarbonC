'use strict';

const assert = require('assert');
const carbon = require('./');

const schema = {
  TABLES: {
    actor: {
      PRIMARY_SHORT: ['actor_id'],
      COLUMNS: {
        'actor.actor_id': 'actor_id',
        'actor.first_name': 'first_name',
      },
      TYPE_VALIDATION: {
        'actor.actor_id': {
          COLUMN_NAME: 'actor_id',
          MYSQL_TYPE: 'smallint',
          MAX_LENGTH: '',
          AUTO_INCREMENT: true,
          NOT_NULL: true,
          SKIP_COLUMN_IN_POST: false,
        },
        'actor.first_name': {
          COLUMN_NAME: 'first_name',
          MYSQL_TYPE: 'varchar',
          MAX_LENGTH: '45',
          AUTO_INCREMENT: false,
          NOT_NULL: true,
          SKIP_COLUMN_IN_POST: false,
        },
      },
    },
  },
};
const schemaDump = `
CREATE TABLE \`actor\` (
  \`actor_id\` smallint unsigned NOT NULL AUTO_INCREMENT,
  \`first_name\` varchar(45) NOT NULL,
  PRIMARY KEY (\`actor_id\`)
);
`;
const dumpSchema = carbon.schemaFromDump(schemaDump);
const dumpMetadata = JSON.parse(carbon.schemaMetadata(dumpSchema));
assert.strictEqual(dumpMetadata.tables[0].name, 'actor');
assert.deepStrictEqual(dumpMetadata.tables[0].primary, ['actor_id']);
assert.strictEqual(dumpMetadata.tables[0].columns[0].auto_increment, true);
assert(carbon.schemaModels(dumpSchema).includes('export interface Actor'));
assert.throws(
  () => carbon.schemaFromDump(
    'CREATE TABLE `crm`.`actor` (`id` int PRIMARY KEY);'
    + 'CREATE TABLE `billing`.`actor` (`id` int PRIMARY KEY);'
  ),
  /conflicting CREATE TABLE/
);

const query = {
  FROM: 'actor',
  SELECT: ['actor.actor_id', 'actor.first_name'],
  WHERE: {'actor.actor_id': ['>', 10]},
  PAGINATION: {LIMIT: 5},
};

assert.strictEqual(carbon.version(), '0.1.0');
assert.strictEqual(carbon.statusCode(3), 'invalid_query');
assert.strictEqual(carbon.statusMessage(0), 'ok');
assert.strictEqual(carbon.C6C.FROM, 'FROM');
assert.strictEqual(carbon.C6C.EQUAL, '=');
assert.strictEqual(carbon.C6.GREATER_THAN, '>');
assert.deepStrictEqual(carbon.eqLit(10), ['=', ['LIT', 10]]);
assert.deepStrictEqual(carbon.inLit([1, 2]), ['IN', [['LIT', 1], ['LIT', 2]]]);
assert.deepStrictEqual(carbon.notInLit([1, 2]), ['NOT_IN', [['LIT', 1], ['LIT', 2]]]);
assert.deepStrictEqual(carbon.betweenLit(1, 2), ['BETWEEN', [['LIT', 1], ['LIT', 2]]]);
assert.strictEqual(carbon.CarbonDialect.MYSQL, 'mysql');
assert.strictEqual(carbon.Dialect.POSTGRESQL, 'postgresql');
assert.strictEqual(carbon.Dialect.POSTGRES, 'postgres');

const result = carbon.compileQuery(JSON.stringify(query), JSON.stringify(schema), carbon.CarbonDialect.MYSQL);

assert.strictEqual(result.status, 0, JSON.stringify(result));
assert.strictEqual(result.status_code, 'ok', JSON.stringify(result));
assert.strictEqual(
  result.sql,
  'SELECT actor.actor_id, actor.first_name FROM `actor` WHERE (actor.actor_id) > ? LIMIT 5'
);
assert.strictEqual(result.params_json, '[10]');
assert.strictEqual(
  result.allowlist_key,
  'SELECT actor.actor_id, actor.first_name FROM `actor` WHERE (actor.actor_id) > ? LIMIT ?'
);
assert.deepStrictEqual(JSON.parse(result.diagnostics_json), {
  status: 0,
  status_code: 'ok',
  ok: true,
  diagnostics: [],
});
assert.deepStrictEqual(carbon.compileQueryValue(query, schema, carbon.CarbonDialect.MYSQL), result);
assert.deepStrictEqual(carbon.compile_query_value(query, schema, carbon.CarbonDialect.MYSQL), result);
const topOrdered = carbon.compileQueryValue(
  {
    FROM: 'actor',
    SELECT: ['actor.actor_id', 'actor.first_name'],
    WHERE: {'actor.actor_id': ['>', 10]},
    ORDER: [['actor.first_name', 'ASC']],
    PAGINATION: {LIMIT: 25},
  },
  schema,
  'mysql'
);
assert.strictEqual(
  topOrdered.sql,
  'SELECT actor.actor_id, actor.first_name FROM `actor` WHERE (actor.actor_id) > ? ORDER BY actor.first_name ASC LIMIT 25'
);
assert.strictEqual(
  topOrdered.allowlist_key,
  'SELECT actor.actor_id, actor.first_name FROM `actor` WHERE (actor.actor_id) > ? ORDER BY actor.first_name ASC LIMIT ?'
);
const adapted = carbon.adaptCompileResult(result);
assert.deepStrictEqual(adapted, {
  ...result,
  params: [10],
  diagnostics: JSON.parse(result.diagnostics_json),
});
assert.deepStrictEqual(carbon.adapt_compile_result(result), adapted);
assert.deepStrictEqual(carbon.compileQueryResult(query, schema, carbon.CarbonDialect.MYSQL), adapted);
assert.deepStrictEqual(carbon.compile_query_result(query, schema, carbon.CarbonDialect.MYSQL), adapted);
const built = carbon.query('actor')
  .select('actor.actor_id', 'actor.first_name')
  .where({'actor.actor_id': ['>', 10]})
  .limit(5);
assert.deepStrictEqual(built.toPayload(), query);
assert.deepStrictEqual(built.compile(schema, carbon.CarbonDialect.MYSQL), adapted);
assert.deepStrictEqual(
  carbon.fromTable('actor')
    .select(['actor.actor_id'])
    .orderBy('actor.first_name', 'DESC')
    .limit(5)
    .toPayload(),
  {
    FROM: 'actor',
    SELECT: ['actor.actor_id'],
    PAGINATION: {ORDER: [['actor.first_name', 'DESC']], LIMIT: 5},
  }
);
assert.deepStrictEqual(
  carbon.fromTable('actor')
    .select('actor.actor_id')
    .join('INNER', 'film_actor fa', {'fa.actor_id': ['=', 'actor.actor_id']})
    .toPayload(),
  {
    FROM: 'actor',
    SELECT: ['actor.actor_id'],
    JOIN: {INNER: {'film_actor fa': {'fa.actor_id': ['=', 'actor.actor_id']}}},
  }
);
assert.deepStrictEqual(carbon.forceIndex('idx_county_id', 'idx_property_units_location'), {
  'FORCE INDEX': ['idx_county_id', 'idx_property_units_location'],
});
assert.deepStrictEqual(
  carbon.fromTable('property_units')
    .select('property_units.unit_id')
    .forceIndex('idx_county_id', 'idx_property_units_location')
    .limit(10)
    .toPayload(),
  {
    FROM: 'property_units',
    SELECT: ['property_units.unit_id'],
    INDEX_HINTS: {'FORCE INDEX': ['idx_county_id', 'idx_property_units_location']},
    PAGINATION: {LIMIT: 10},
  }
);
const hintedJoin = carbon.query('actor')
  .select('actor.actor_id', 'fa.film_id')
  .join('INNER', 'film_actor fa', {'fa.actor_id': ['=', 'actor.actor_id']})
  .indexHints({
    actor: carbon.ignoreIndex('idx_actor_last_name'),
    'film_actor fa': carbon.useIndex('idx_film_actor_actor_id'),
  })
  .limit(5)
  .compile(undefined, carbon.CarbonDialect.MYSQL);
assert.strictEqual(
  hintedJoin.sql,
  'SELECT actor.actor_id, fa.film_id FROM `actor` IGNORE INDEX (`idx_actor_last_name`) INNER JOIN `film_actor` AS `fa` USE INDEX (`idx_film_actor_actor_id`) ON ((fa.actor_id) = actor.actor_id) LIMIT 5'
);
const recentActorIds = carbon.query('film_actor')
  .select('film_actor.actor_id')
  .where({'film_actor.film_id': ['>', 10]})
  .limit(1);
assert.deepStrictEqual(carbon.subselect(recentActorIds), [
  'SUBSELECT',
  {
    FROM: 'film_actor',
    SELECT: ['film_actor.actor_id'],
    WHERE: {'film_actor.film_id': ['>', 10]},
    PAGINATION: {LIMIT: 1},
  },
]);
const derived = carbon.query('actor')
  .select('actor.actor_id', 'fa_recent.actor_id')
  .joinSubselect('INNER', 'fa_recent', recentActorIds, {'fa_recent.actor_id': ['=', 'actor.actor_id']})
  .where({'actor.actor_id': ['>', 100]})
  .compile(undefined, carbon.CarbonDialect.MYSQL);
assert.strictEqual(derived.status, 0, JSON.stringify(derived));
assert.strictEqual(
  derived.sql,
  'SELECT actor.actor_id, fa_recent.actor_id FROM `actor` INNER JOIN (SELECT film_actor.actor_id FROM `film_actor` WHERE (film_actor.film_id) > ? LIMIT 1) AS `fa_recent` ON ((fa_recent.actor_id) = actor.actor_id) WHERE (actor.actor_id) > ? LIMIT 100'
);
assert.deepStrictEqual(derived.params, [10, 100]);
const salesQuery = carbon.query('parcel_sales')
  .select('parcel_sales.parcel_id')
  .whereOp('parcel_sales.sale_price', carbon.C6C.GREATER_THAN, 5000);
assert.deepStrictEqual(carbon.alias(carbon.call('COUNT', 'parcel_sales.parcel_id'), 'sale_count'), [
  'AS',
  ['COUNT', 'parcel_sales.parcel_id'],
  'sale_count',
]);
assert.deepStrictEqual(carbon.fn('CONCAT', carbon.lit('A'), carbon.lit('B')), [
  'CONCAT',
  ['LIT', 'A'],
  ['LIT', 'B'],
]);
assert.deepStrictEqual(carbon.customCall('COALESCE', carbon.lit('UNKNOWN'), 'actor.last_name'), [
  'CALL',
  'COALESCE',
  ['LIT', 'UNKNOWN'],
  'actor.last_name',
]);
const customSelected = carbon.query('actor')
  .select([carbon.alias(carbon.customCall('COALESCE', carbon.lit('UNKNOWN'), 'actor.first_name'), 'display_name')])
  .limit(1)
  .compile(undefined, carbon.CarbonDialect.MYSQL);
assert.strictEqual(customSelected.sql, 'SELECT COALESCE(?, actor.first_name) AS display_name FROM `actor` LIMIT 1');
assert.deepStrictEqual(customSelected.params, ['UNKNOWN']);
const spatialPolygon = 'POLYGON((39.5185659 -105.0142915,39.5401859 -105.0142915,39.5401859 -104.9862115,39.5185659 -104.9862115,39.5185659 -105.0142915))';
const spatialInnerPolygon = 'POLYGON((0 0,1 0,1 1,0 1,0 0))';
assert.deepStrictEqual(carbon.mbrContains('property_units.envelope', 'property_units.location'), [
  'MBRContains',
  'property_units.envelope',
  'property_units.location',
]);
const spatialFiltered = carbon.query('property_units')
  .select('property_units.unit_id')
  .where({
    MBRContains: [
      carbon.fn('ST_GeomFromText', carbon.lit(spatialPolygon), 4326),
      'property_units.location',
    ],
    OR: [
      carbon.stWithin(
        'property_units.location',
        carbon.fn('ST_GeomFromText', carbon.lit(spatialInnerPolygon), 4326)
      ),
      carbon.stContains('property_units.envelope', 'property_units.location'),
    ],
  })
  .limit(10)
  .compile(undefined, carbon.CarbonDialect.MYSQL);
assert.strictEqual(
  spatialFiltered.sql,
  'SELECT property_units.unit_id FROM `property_units` WHERE MBRCONTAINS(ST_GEOMFROMTEXT(?, 4326), property_units.location) AND (ST_WITHIN(property_units.location, ST_GEOMFROMTEXT(?, 4326)) OR ST_CONTAINS(property_units.envelope, property_units.location)) LIMIT 10'
);
assert.deepStrictEqual(spatialFiltered.params, [spatialPolygon, spatialInnerPolygon]);
assert.deepStrictEqual(carbon.lit('2023-01-01'), ['LIT', '2023-01-01']);
assert.deepStrictEqual(carbon.existsSpec('property_units.parcel_id', salesQuery), [
  'property_units.parcel_id',
  [
    'SUBSELECT',
    {
      FROM: 'parcel_sales',
      SELECT: ['parcel_sales.parcel_id'],
      WHERE: {'parcel_sales.sale_price': ['>', 5000]},
    },
  ],
]);
const advanced = carbon.query('property_units')
  .select('property_units.unit_id')
  .whereBetween('property_units.unit_id', 1, 10)
  .whereIn('property_units.parcel_id', salesQuery)
  .whereNotIn('property_units.account_id', [99, 100])
  .whereExists('property_units.parcel_id', salesQuery)
  .limit(3)
  .compile(undefined, carbon.CarbonDialect.MYSQL);
assert.strictEqual(advanced.status, 0, JSON.stringify(advanced));
assert.strictEqual(
  advanced.sql,
  'SELECT property_units.unit_id FROM `property_units` WHERE (property_units.unit_id) BETWEEN ? AND ? AND ( property_units.parcel_id IN (SELECT parcel_sales.parcel_id FROM `parcel_sales` WHERE (parcel_sales.sale_price) > ?) ) AND ( property_units.account_id NOT IN (?, ?) ) AND EXISTS (SELECT parcel_sales.parcel_id FROM `parcel_sales` WHERE (parcel_sales.sale_price) > ? AND (parcel_sales.parcel_id) = property_units.parcel_id) LIMIT 3'
);
assert.deepStrictEqual(advanced.params, [1, 10, 5000, 99, 100, 5000]);
assert.deepStrictEqual(
  carbon.andGroup(
    carbon.condition('actor.actor_id', carbon.op('>', 2)),
    carbon.orGroup(
      carbon.condition('actor.first_name', carbon.op('LIKE', carbon.lit('A%'))),
      carbon.condition('actor.first_name', carbon.op('LIKE', carbon.lit('B%')))
    )
  ),
  {
    AND: [
      {'actor.actor_id': ['>', 2]},
      {
        OR: [
          {'actor.first_name': ['LIKE', ['LIT', 'A%']]},
          {'actor.first_name': ['LIKE', ['LIT', 'B%']]},
        ],
      },
    ],
  }
);
assert.deepStrictEqual(carbon.matchAgainst('alpha beta', 'BOOLEAN'), [
  'MATCH_AGAINST',
  [['LIT', 'alpha beta'], 'BOOLEAN'],
]);
const fulltext = carbon.query('actor')
  .select('actor.actor_id')
  .whereMatchAgainst('actor.first_name', 'alpha beta', 'BOOLEAN')
  .limit(10)
  .compile(schema, carbon.CarbonDialect.MYSQL);
assert.strictEqual(fulltext.status, 0, JSON.stringify(fulltext));
assert.strictEqual(
  fulltext.sql,
  'SELECT actor.actor_id FROM `actor` WHERE (MATCH(actor.first_name) AGAINST(? IN BOOLEAN MODE)) LIMIT 10'
);
assert.deepStrictEqual(fulltext.params, ['alpha beta']);
const booleanGrouped = carbon.query('actor')
  .select('actor.actor_id')
  .whereBetween('actor.actor_id', 1, 10)
  .whereOr(
    carbon.condition('actor.first_name', carbon.op('LIKE', carbon.lit('A%'))),
    carbon.condition('actor.first_name', carbon.op('LIKE', carbon.lit('B%')))
  )
  .whereAnd(
    carbon.condition('actor.actor_id', carbon.op('>', 2)),
    carbon.condition('actor.actor_id', carbon.op('<', 9))
  )
  .limit(5)
  .compile(undefined, carbon.CarbonDialect.MYSQL);
assert.strictEqual(booleanGrouped.status, 0, JSON.stringify(booleanGrouped));
assert.strictEqual(
  booleanGrouped.sql,
  'SELECT actor.actor_id FROM `actor` WHERE (actor.actor_id) BETWEEN ? AND ? AND ((actor.first_name) LIKE ? OR (actor.first_name) LIKE ?) AND ((actor.actor_id) > ? AND (actor.actor_id) < ?) LIMIT 5'
);
assert.deepStrictEqual(booleanGrouped.params, [1, 10, 'A%', 'B%', 2, 9]);
const grouped = carbon.query('actor')
  .select(['DISTINCT', 'actor.first_name'], ['AS', ['COUNT', 'actor.actor_id'], 'cnt'])
  .groupBy('actor.first_name')
  .having({cnt: ['>', 1]})
  .page(2)
  .limit(5)
  .compile(undefined, carbon.CarbonDialect.MYSQL);
assert.strictEqual(grouped.status, 0, JSON.stringify(grouped));
assert.strictEqual(
  grouped.sql,
  'SELECT DISTINCT actor.first_name, COUNT(actor.actor_id) AS cnt FROM `actor` GROUP BY actor.first_name HAVING ((cnt) > ?) LIMIT 5, 5'
);
assert.deepStrictEqual(grouped.params, [1]);
const inserted = carbon.query('actor')
  .insert({'actor.first_name': 'ALICE'})
  .compile(schema, carbon.CarbonDialect.MYSQL);
assert.strictEqual(inserted.status, 0, JSON.stringify(inserted));
assert.strictEqual(inserted.sql, 'INSERT INTO `actor` (`first_name`) VALUES (?)');
assert.deepStrictEqual(inserted.params, ['ALICE']);
const expressionInserted = carbon.query('actor')
  .insert({
    'actor.first_name': carbon.fn('CONCAT', carbon.lit('HEL'), carbon.lit('LO')),
    'actor.last_name': 'SMITH',
  })
  .compile(undefined, carbon.CarbonDialect.MYSQL);
assert.strictEqual(
  expressionInserted.sql,
  'INSERT INTO `actor` (`first_name`, `last_name`) VALUES (CONCAT(?, ?), ?)'
);
assert.deepStrictEqual(expressionInserted.params, ['HEL', 'LO', 'SMITH']);
assert.deepStrictEqual(
  carbon.query('actor').replace({'actor.first_name': 'BOB'}).toPayload(),
  {
    FROM: 'actor',
    REPLACE: {'actor.first_name': 'BOB'},
  }
);
const updated = carbon.query('actor')
  .update({'actor.first_name': 'BOB'})
  .where({'actor.actor_id': 1})
  .compile(schema, carbon.CarbonDialect.MYSQL);
assert.strictEqual(updated.sql, 'UPDATE `actor` SET `first_name` = ? WHERE (actor.actor_id) = ?');
assert.deepStrictEqual(updated.params, ['BOB', 1]);
const expressionUpdated = carbon.query('actor')
  .update({
    'actor.first_name': carbon.fn('CONCAT', carbon.lit('Mr. '), 'actor.last_name'),
    'actor.last_name': carbon.customCall('COALESCE', carbon.lit('UNKNOWN'), 'actor.last_name'),
  })
  .where({'actor.actor_id': ['=', 7]})
  .compile(undefined, carbon.CarbonDialect.MYSQL);
assert.strictEqual(
  expressionUpdated.sql,
  'UPDATE `actor` SET `first_name` = CONCAT(?, actor.last_name), `last_name` = COALESCE(?, actor.last_name) WHERE (actor.actor_id) = ?'
);
assert.deepStrictEqual(expressionUpdated.params, ['Mr. ', 'UNKNOWN', 7]);
const deleted = carbon.query('actor')
  .delete()
  .where({'actor.actor_id': 1})
  .compile(schema, carbon.CarbonDialect.MYSQL);
assert.strictEqual(deleted.sql, 'DELETE `actor` FROM `actor` WHERE (actor.actor_id) = ?');
assert.deepStrictEqual(deleted.params, [1]);
const upserted = carbon.query('actor')
  .insert({'actor.actor_id': 1, 'actor.first_name': 'ALICE'})
  .upsert(['first_name'])
  .compile(schema, carbon.CarbonDialect.MYSQL);
assert.strictEqual(
  upserted.sql,
  'INSERT INTO `actor` (`actor_id`, `first_name`) VALUES (?, ?) ON DUPLICATE KEY UPDATE `first_name` = VALUES(`first_name`)'
);
assert.deepStrictEqual(upserted.params, [1, 'ALICE']);
assert.deepStrictEqual(
  carbon.query('actor').insert({'actor.actor_id': 1}).doNothing().toPayload(),
  {
    FROM: 'actor',
    INSERT: {'actor.actor_id': 1},
    UPDATE: [],
  }
);
assert.deepStrictEqual(JSON.parse(carbon.schemaMetadata(JSON.stringify(schema))), {
  tables: [
    {
      name: 'actor',
      columns: [
        {
          name: 'actor_id',
          qualified: 'actor.actor_id',
          db_type: 'smallint',
          max_length: '',
          nullable: false,
          auto_increment: true,
          skip_insert: false,
        },
        {
          name: 'first_name',
          qualified: 'actor.first_name',
          db_type: 'varchar',
          max_length: '45',
          nullable: false,
          auto_increment: false,
          skip_insert: false,
        },
      ],
      primary: ['actor_id'],
    },
  ],
});
assert.strictEqual(
  carbon.schema_metadata(JSON.stringify({})),
  '{"tables":[]}'
);
const modelSource = carbon.schemaModels(schema);
assert.strictEqual(
  modelSource,
  carbon.schemaModelSource(JSON.stringify(schema), 'typescript', '{}'),
  'schemaModels should delegate to the C generator'
);
assert(modelSource.includes("import * as carbon from '@carbonorm/carbonc';"));
assert(modelSource.includes('export interface Actor'));
assert(modelSource.includes('actor_id: number;'));
assert(modelSource.includes('first_name: string;'));
assert(modelSource.includes('export const ActorTable = "actor" as const;'));
assert(modelSource.includes('export const ActorFields = {'));
assert(modelSource.includes('actor_id: "actor_id",'));
assert(modelSource.includes('export const ActorColumns = {'));
assert(modelSource.includes('actor_id: "actor.actor_id",'));
assert(modelSource.includes('export const ActorMeta = {'));
assert(modelSource.includes('table: ActorTable,'));
assert(modelSource.includes('primary: ["actor_id"],'));
assert(modelSource.includes('fields: ActorFields,'));
assert(modelSource.includes('columns: ActorColumns,'));
assert(modelSource.includes('dbTypes: {'));
assert(modelSource.includes('actor_id: "smallint",'));
assert(modelSource.includes('nullable: {'));
assert(modelSource.includes('actor_id: false,'));
assert(modelSource.includes('export const Actor = Object.freeze({'));
assert(modelSource.includes('GetPayload(query = {}) {'));
assert(modelSource.includes('Get(query = {}, options = {}) {'));
const actorMeta = {
  table: 'actor',
  fields: {
    actor_id: 'actor_id',
    first_name: 'first_name',
  },
  columns: {
    actor_id: 'actor.actor_id',
    first_name: 'actor.first_name',
  },
};
const Actor = carbon.modelApi(actorMeta);
assert.strictEqual(carbon.modelTable(actorMeta), 'actor');
assert.strictEqual(Actor.TABLE, 'actor');
assert.strictEqual(Actor.FIELDS.first_name, 'first_name');
assert.strictEqual(Actor.COLUMNS.first_name, 'actor.first_name');
assert.strictEqual(carbon.modelColumn(actorMeta, actorMeta.fields.first_name), 'actor.first_name');
assert.deepStrictEqual(carbon.modelSelect(actorMeta).toPayload(), {
  FROM: 'actor',
  SELECT: ['actor.actor_id', 'actor.first_name'],
});
const constantBuilt = carbon.query(actorMeta.table)
  .select(actorMeta.columns.actor_id, actorMeta.columns.first_name)
  .whereOp(actorMeta.columns.actor_id, carbon.C6C.GREATER_THAN, 0)
  .orderBy(actorMeta.columns.first_name, carbon.C6C.ASC)
  .limit(1)
  .compile(schema, carbon.CarbonDialect.MYSQL);
assert.strictEqual(
  constantBuilt.sql,
  'SELECT actor.actor_id, actor.first_name FROM `actor` WHERE (actor.actor_id) > ? ORDER BY actor.first_name ASC LIMIT 1'
);
assert.deepStrictEqual(constantBuilt.params, [0]);
const conflictingModelSchema = {
  TABLES: {
    foo_bar: {COLUMNS: {'foo_bar.id': 'id'}},
    foo__bar: {COLUMNS: {'foo__bar.id': 'id'}},
  },
};
assert.throws(
  () => carbon.schemaModels(conflictingModelSchema),
  /generated name conflict/
);
const modelBuilt = carbon.modelSelect(actorMeta, actorMeta.fields.actor_id)
  .whereOp(actorMeta.columns.actor_id, carbon.C6C.GREATER_THAN, 0)
  .limit(1)
  .compile(schema, carbon.CarbonDialect.MYSQL);
assert.strictEqual(modelBuilt.sql, 'SELECT actor.actor_id FROM `actor` WHERE (actor.actor_id) > ? LIMIT 1');
assert.deepStrictEqual(modelBuilt.params, [0]);
assert.deepStrictEqual(carbon.modelValues(actorMeta, {[actorMeta.fields.first_name]: 'ALICE'}), {
  'actor.first_name': 'ALICE',
});
const getPayload = Actor.GetPayload({
  [carbon.C6C.SELECT]: [Actor.COLUMNS.actor_id],
  [carbon.C6C.WHERE]: {
    [Actor.COLUMNS.actor_id]: carbon.eqLit(10),
  },
  [carbon.C6C.PAGINATION]: {[carbon.C6C.LIMIT]: 500},
  cacheResults: false,
});
assert.deepStrictEqual(getPayload, {
  SELECT: ['actor.actor_id'],
  WHERE: {'actor.actor_id': ['=', ['LIT', 10]]},
  PAGINATION: {LIMIT: 500},
  cacheResults: false,
  FROM: 'actor',
});
const getRequest = Actor.Get(getPayload, {
  schema,
  dialect: carbon.CarbonDialect.MYSQL,
});
assert.strictEqual(getRequest.method, 'Get');
assert.strictEqual(getRequest.model, 'actor');
assert.strictEqual(getRequest.cacheResults, false);
assert.ok(!Object.prototype.hasOwnProperty.call(getRequest, 'route'));
const getResult = carbon.compileQueryResult(getRequest.query, getRequest.schema, getRequest.dialect);
assert.strictEqual(getResult.sql, 'SELECT actor.actor_id FROM `actor` WHERE (actor.actor_id) = ? LIMIT 500');
const inLitResult = carbon.compileQueryResult({
  [carbon.C6C.FROM]: Actor.TABLE,
  [carbon.C6C.SELECT]: [Actor.COLUMNS.actor_id],
  [carbon.C6C.WHERE]: {[Actor.COLUMNS.actor_id]: carbon.inLit([1, 2])},
}, schema);
assert.strictEqual(inLitResult.sql, 'SELECT actor.actor_id FROM `actor` WHERE ( actor.actor_id IN (?, ?) ) LIMIT 100');
assert.deepStrictEqual(inLitResult.params, [1, 2]);
const modelInserted = carbon.modelInsert(actorMeta, {[actorMeta.fields.first_name]: 'ALICE'})
  .compile(schema, carbon.CarbonDialect.MYSQL);
assert.strictEqual(modelInserted.sql, 'INSERT INTO `actor` (`first_name`) VALUES (?)');
assert.deepStrictEqual(modelInserted.params, ['ALICE']);
const modelUpdated = carbon.modelUpdate(actorMeta, {[actorMeta.fields.first_name]: 'BOB'})
  .whereOp(actorMeta.columns.actor_id, carbon.C6C.GREATER_THAN, 0)
  .compile(schema, carbon.CarbonDialect.MYSQL);
assert.strictEqual(modelUpdated.sql, 'UPDATE `actor` SET `first_name` = ? WHERE (actor.actor_id) > ?');
assert.deepStrictEqual(modelUpdated.params, ['BOB', 0]);
const modelUpserted = carbon.modelUpsert(
  actorMeta,
  {[actorMeta.fields.actor_id]: 1, [actorMeta.fields.first_name]: 'ALICE'},
  [actorMeta.fields.first_name]
).compile(schema, carbon.CarbonDialect.MYSQL);
assert.strictEqual(
  modelUpserted.sql,
  'INSERT INTO `actor` (`actor_id`, `first_name`) VALUES (?, ?) ON DUPLICATE KEY UPDATE `first_name` = VALUES(`first_name`)'
);
assert.deepStrictEqual(modelUpserted.params, [1, 'ALICE']);
assert.deepStrictEqual(carbon.modelReplace(actorMeta, {[actorMeta.fields.first_name]: 'BOB'}).toPayload(), {
  FROM: 'actor',
  REPLACE: {'actor.first_name': 'BOB'},
});
assert.deepStrictEqual(carbon.modelDoNothing(actorMeta, {[actorMeta.fields.actor_id]: 1}).toPayload(), {
  FROM: 'actor',
  INSERT: {'actor.actor_id': 1},
  UPDATE: [],
});
assert.strictEqual(carbon.schema_models({}), '');

const aliasResult = carbon.compile_query(JSON.stringify(query), JSON.stringify(schema), carbon.CarbonDialect.MYSQL);
assert.deepStrictEqual(aliasResult, result);

const rejected = carbon.compileQuery(
  JSON.stringify({FROM: 'actor', SELECT: ['actor.last_name']}),
  JSON.stringify(schema),
  'mysql'
);
assert.strictEqual(rejected.status, 3, JSON.stringify(rejected));
assert.strictEqual(rejected.status_code, 'invalid_query', JSON.stringify(rejected));
assert.strictEqual(rejected.error, 'invalid query');

const rejectedTable = carbon.compileQuery(
  JSON.stringify({FROM: 'film', SELECT: ['film.film_id']}),
  JSON.stringify(schema),
  'mysql'
);
assert.strictEqual(rejectedTable.status, 3, JSON.stringify(rejectedTable));
assert.strictEqual(rejectedTable.status_code, 'invalid_query', JSON.stringify(rejectedTable));
assert.strictEqual(rejectedTable.error, 'table is not present in schema');
assert.deepStrictEqual(JSON.parse(rejectedTable.diagnostics_json), {
  status: 3,
  status_code: 'invalid_query',
  ok: false,
  diagnostics: [
    {
      severity: 'error',
      code: 'invalid_query',
      message: 'table is not present in schema',
      source: 'schema',
      path: '$.FROM',
    },
  ],
});

assert.strictEqual(
  carbon.normalizeAllowlistSql('SELECT * FROM `actor` LIMIT 10'),
  'SELECT * FROM `actor` LIMIT ?'
);
assert.strictEqual(
  carbon.normalize_allowlist_sql('SELECT * FROM `actor` LIMIT 25'),
  'SELECT * FROM `actor` LIMIT ?'
);

assert.throws(() => carbon.compileQuery({}), TypeError);
assert.throws(() => carbon.normalizeAllowlistSql(), TypeError);

console.log('node binding smoke: ok');
