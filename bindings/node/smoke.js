'use strict';

const assert = require('assert');
const carbon = require('./');

const schema = {
  TABLES: {
    actor: {
      COLUMNS: {
        'actor.actor_id': 'actor_id',
        'actor.first_name': 'first_name',
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
