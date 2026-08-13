'use strict';

const carbon = require('../../bindings/node');

console.log(carbon.version());

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

const result = carbon.compileQuery(JSON.stringify(query), JSON.stringify(schema), 'mysql');

if (result.status !== 0) {
  console.error(result.error);
  process.exit(1);
}

console.log(result.sql);
console.log(result.params_json);
console.log(result.allowlist_key);
console.log(result.diagnostics_json);
console.log(carbon.schemaMetadata(JSON.stringify(schema)));
console.log(carbon.schemaModels(schema));
