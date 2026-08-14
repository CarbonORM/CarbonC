'use strict';

const carbon = require('../../bindings/node');

console.log(carbon.version());

const schemaDump = `
CREATE TABLE \`actor\` (
  \`actor_id\` smallint unsigned NOT NULL AUTO_INCREMENT,
  \`first_name\` varchar(45) NOT NULL,
  PRIMARY KEY (\`actor_id\`)
);
`;

const schema = carbon.schemaFromDump(schemaDump);
const metadata = JSON.parse(carbon.schemaMetadata(schema));
const actorMetadata = metadata.tables[0];
const Actor = carbon.modelApi({
  table: actorMetadata.name,
  fields: Object.freeze(Object.fromEntries(
    actorMetadata.columns.map((column) => [column.name, column.name])
  )),
  columns: Object.freeze(Object.fromEntries(
    actorMetadata.columns.map((column) => [column.name, column.qualified])
  )),
});

const getRequest = Actor.Get({
  [carbon.C6C.SELECT]: [Actor.COLUMNS.actor_id, Actor.COLUMNS.first_name],
  [carbon.C6C.WHERE]: {
    [Actor.COLUMNS.actor_id]: carbon.eqLit(10),
  },
  [carbon.C6C.PAGINATION]: {[carbon.C6C.LIMIT]: 5},
}, {
  schema,
  dialect: carbon.CarbonDialect.MYSQL,
});

const result = carbon.compileQueryResult(getRequest.query, getRequest.schema, getRequest.dialect);

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
