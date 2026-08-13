'use strict';

const carbon = require('../../bindings/node');

console.log(carbon.version());

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

const result = carbon.compileQuery(JSON.stringify(query), JSON.stringify(schema), 'mysql');

if (result.status !== 0) {
  console.error(result.error);
  process.exit(1);
}

console.log(result.sql);
console.log(result.params_json);
console.log(result.allowlist_key);
console.log(carbon.schemaMetadata(JSON.stringify(schema)));
