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

const metadata = JSON.parse(carbon.schemaMetadata(JSON.stringify(schema)));
const actorMetadata = metadata.tables[0];
const Actor = Object.freeze({
  TABLE: actorMetadata.name,
  COLUMNS: Object.freeze(Object.fromEntries(
    actorMetadata.columns.map((column) => [column.name, column.qualified])
  )),
});

const result = carbon.query(Actor.TABLE)
  .select(Actor.COLUMNS.actor_id, Actor.COLUMNS.first_name)
  .whereOp(Actor.COLUMNS.actor_id, carbon.C6C.GREATER_THAN, 10)
  .limit(5)
  .compile(schema, 'mysql');

if (result.status !== 0) {
  console.error(result.error);
  process.exit(1);
}

console.log(result.sql);
console.log(JSON.stringify(result.params));
console.log(result.allowlist_key);
console.log(JSON.stringify(result.diagnostics));
console.log(JSON.stringify(metadata));
console.log(carbon.schemaModels(schema));
