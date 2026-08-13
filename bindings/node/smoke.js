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

const query = {
  FROM: 'actor',
  SELECT: ['actor.actor_id', 'actor.first_name'],
  WHERE: {'actor.actor_id': ['>', 10]},
  PAGINATION: {LIMIT: 5},
};

assert.strictEqual(carbon.version(), '0.1.0');
assert.strictEqual(carbon.statusCode(3), 'invalid_query');
assert.strictEqual(carbon.statusMessage(0), 'ok');

const result = carbon.compileQuery(JSON.stringify(query), JSON.stringify(schema), 'mysql');

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
assert(modelSource.includes('export interface Actor'));
assert(modelSource.includes('actor_id: number;'));
assert(modelSource.includes('first_name: string;'));
assert(modelSource.includes('export const ActorMeta = {'));
assert(modelSource.includes('primary: ["actor_id"],'));
assert(modelSource.includes('dbTypes: {'));
assert(modelSource.includes('actor_id: "smallint",'));
assert(modelSource.includes('nullable: {'));
assert(modelSource.includes('actor_id: false,'));
assert.strictEqual(carbon.schema_models({}), '');

const aliasResult = carbon.compile_query(JSON.stringify(query), JSON.stringify(schema), 'mysql');
assert.deepStrictEqual(aliasResult, result);

const rejected = carbon.compileQuery(
  JSON.stringify({FROM: 'actor', SELECT: ['actor.last_name']}),
  JSON.stringify(schema),
  'mysql'
);
assert.strictEqual(rejected.status, 3, JSON.stringify(rejected));
assert.strictEqual(rejected.status_code, 'invalid_query', JSON.stringify(rejected));
assert.strictEqual(rejected.error, 'invalid query');

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
