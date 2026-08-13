# CarbonC

CarbonC is the portable CarbonORM kernel. Its job is to hold the deterministic
parts of C6 in a stable C ABI so PHP, Node, Python, Ruby, and other language
packages can share one implementation without giving up their native runtime
ergonomics.

The C layer should own:

- query payload validation
- SQL generation
- bind-parameter extraction
- allowlist normalization
- schema metadata, diff, and introspection normalization

The language bindings should own:

- package-native model APIs
- connection pools and transactions
- async/promise/event-loop behavior
- exceptions and framework integration
- generated class/type surfaces

## v0.1 Kernel Scope

The current kernel is intentionally small. It provides a versioned ABI, explicit
buffer ownership, and a fixture-backed query compiler slice for canonical C6
payloads:

```json
{
  "FROM": "actor",
  "SELECT": ["actor.actor_id", "actor.first_name"],
  "WHERE": {
    "actor.actor_id": ["=", ["LIT", 10]]
  },
  "PAGINATION": {
    "ORDER": [["actor.last_name", "ASC"]],
    "LIMIT": 25
  }
}
```

This JSON is the stable ABI shape. Normal package code should build it through
generated table/field/column constants plus the `C6C` token constants and
literal predicate helpers such as `eqLit()` / `eq_lit()` / `carbon_eq_lit()`,
plus `CarbonDialect` / `Dialect` dialect constants so query authors do not
hand-type grammar strings or schema identifiers.

Language packages also expose a small execution-envelope layer for applications
that need the same model method payload to run in different contexts. The
helpers keep the query object intact, add the generated model `FROM` table when
needed, and return a deterministic route decision such as
`{target: "server", reason: "mobile_offload"}` for the caller's own local or
server executor:

```js
const Actor = carbon.modelApi(ActorMeta);
const request = Actor.Get({
  [carbon.C6C.SELECT]: [ActorColumns.actor_id],
  [carbon.C6C.WHERE]: {
    [ActorColumns.actor_id]: carbon.eqLit(10),
  },
  [carbon.C6C.PAGINATION]: {[carbon.C6C.LIMIT]: 500},
  cacheResults: false,
}, {
  schema,
  dialect: carbon.CarbonDialect.MYSQL,
  context: {deviceClass: 'mobile'},
  policy: {serverOnMobile: true},
});
```

The compiler returns a numeric `status`, stable string `status_code`, SQL, a
JSON array of bound parameter values, and a normalized allowlist key:

```sql
SELECT actor.actor_id, actor.first_name FROM `actor` WHERE (actor.actor_id) = ? ORDER BY actor.last_name ASC LIMIT 25
```

```json
[10]
```

```text
SELECT actor.actor_id, actor.first_name FROM `actor` WHERE (actor.actor_id) = ? ORDER BY actor.last_name ASC LIMIT ?
```

Supported in this slice:

- schema metadata normalization into deterministic JSON for generated binding
  types
- schema metadata enrichment from CarbonNode-style `TYPE_VALIDATION` entries,
  including DB type, max length, nullability, auto-increment, and insert-skip
  flags
- CarbonNode-compatible C6 token constants exposed idiomatically in each binding
  as `C6C`/`C6`
- dialect constants exposed in each binding as `CarbonDialect` / `Dialect`
- C consumers can use the matching `CARBON_C6_*` token macros and
  `CARBON_DIALECT_*` macros from `include/carbon.h`
- language-normalized literal predicate helpers such as `eqLit()` / `eq_lit()`,
  `inLit()` / `in_lit()`, `notInLit()` / `not_in_lit()`, and `betweenLit()` /
  `between_lit()` for generated query payloads
- package-level typed source generation helpers for Python dataclasses,
  TypeScript interfaces, PHP model classes, and Ruby Struct models, including
  generated table, field-name, and qualified-column constants
- package-level native payload compile helpers that serialize idiomatic
  dict/array/object/hash inputs into the stable C JSON boundary
- package-level model `Get` payload helpers and execution request envelopes
  with context-based local/server route decisions
- package-level typed result adapters that add decoded native `params` and
  `diagnostics` values without removing raw JSON fields
- package-level SELECT query-builder facades that emit the same canonical
  payloads as the native helper layer for table/from, select, where, join,
  group/having, limit/page, and order controls
- `FROM` / legacy `table`
- `SELECT` references, wrapper-form `AS` / `DISTINCT`, direct tuples for
  CarbonNode known functions, and canonical `CALL` custom-function tuples
- `JOIN` clauses for `INNER`, `LEFT`, `LEFT_OUTER`, `RIGHT`, and
  `RIGHT_OUTER` table aliases and stringified derived targets
- MySQL `INDEX_HINTS` for base and joined tables, including CarbonNode-compatible
  `FORCE INDEX`, `USE INDEX`, and `IGNORE INDEX` shapes
- `WHERE` column mappings, `AND` / `OR`, comparison operators, `IN`, `NOT_IN`,
  `BETWEEN`, `IS`, `IS_NOT`, `EXISTS`, `NOT_EXISTS`, `MATCH_AGAINST`, `LIT`,
  `PARAM`, and boolean spatial-function predicates
- `GROUP_BY` expression lists and `HAVING` boolean clauses
- scalar `SUBSELECT` expressions in `SELECT` and `WHERE` operands
- explicit `INSERT`, `REPLACE`, `UPDATE`, and `DELETE` write payloads,
  including expression-valued `INSERT`/`UPDATE` columns and array-valued
  multi-row `INSERT`/MySQL `REPLACE` rows
- CarbonNode-compatible root-level POST row inserts when no read operation
  controls are present
- CarbonNode-compatible `dataInsertMultipleRows` insert payloads
- MySQL upsert update lists through `UPDATE: ["column_name"]`
- PostgreSQL `ON CONFLICT` upserts from `PRIMARY_SHORT` or `PRIMARY` schema
  metadata, including `UPDATE: []` / `DO NOTHING`
- PostgreSQL `UPDATE ... FROM` for `INNER` joined table aliases
- PostgreSQL `DELETE ... USING` for `INNER` joined table aliases
- `PAGINATION.ORDER`, compatible top-level `ORDER`, `LIMIT`, and `PAGE`
- MySQL `?` placeholders and PostgreSQL `$1`-style placeholders
- CarbonNode-style allowlist normalization for whitespace, `LIMIT` forms
  including `LIMIT ... OFFSET ...`, parenthesized bind groups,
  multi-row `VALUES` cardinality, and `IN` bind-list cardinality
- schema-aware table, unqualified-reference, dotted-reference, join-alias, and
  write-column validation when `schema_json` includes a `TABLES` object
- schema-declared write column ordering for `INSERT`, `UPDATE`, and upsert
  update lists

Direct expression tuples are limited to CarbonNode's known SQL function list.
Unknown/custom SQL functions must use `["CALL", "FUNCTION_NAME", ...args]`,
and legacy positional alias forms such as `["COUNT", "id", "AS", "cnt"]`
are rejected in favor of `["AS", expression, "cnt"]`.

Top-level `ORDER` is accepted for CarbonNode compatibility and is treated as
`PAGINATION.ORDER` when the pagination object does not also declare `ORDER`.
Legacy object-map `ORDER` payloads and duplicate top-level/pagination order
declarations are rejected.

Derived JOIN targets use the same JSON-only ABI as other compiler inputs: the
JOIN target key is a stringified object with `SUBSELECT` and `AS`, while the
JOIN target value remains the `ON` clause.

`INDEX_HINTS` accepts CarbonNode's base hint form (`["idx"]`, `"idx"`, or an
object keyed by `FORCE INDEX`, `USE INDEX`, and `IGNORE INDEX`) and target-keyed
maps for joined tables. MySQL emits hints after the base table or joined alias;
PostgreSQL accepts the payload and emits no hint syntax.

Write support accepts explicit operation keys such as `INSERT`, `REPLACE`,
`UPDATE`, and `DELETE`. It also accepts CarbonNode-style root-level POST rows
like `{"FROM":"actor","first_name":"ALICE"}` when no read controls such as
`SELECT`, `WHERE`, `JOIN`, `GROUP_BY`, `HAVING`, or `PAGINATION` are present.
Root-level POST rows ignore request metadata keys such as `DB`, `cacheResults`,
and `UPDATE: [...]`; that `UPDATE` array remains upsert metadata. Single-row and
multi-row insert/upsert payloads are covered now, including
`dataInsertMultipleRows`; later rows bind `null` for missing first-row columns to
match CarbonNode's batch insert behavior. When schema `COLUMNS` metadata is
present, write columns are emitted in schema order and later rows may use either
qualified or short keys for the same normalized column. PostgreSQL writes
currently cover simple insert/update/delete forms and schema-derived conflict
targets for `ON CONFLICT` upserts, plus `INNER` joined updates through
`UPDATE ... FROM` and `INNER` joined deletes through `DELETE ... USING`.
PostgreSQL non-`INNER` joined writes and PostgreSQL derived joined writes are
later work.

Schema validation is opt-in for this slice. Passing `{}` keeps syntax-only
identifier checks. Passing a `TABLES` object validates unqualified references
against the current `FROM` table and validates dotted/join-alias references
against C6-style table metadata:

```json
{
  "TABLES": {
    "actor": {
      "PRIMARY_SHORT": ["actor_id"],
      "COLUMNS": {
        "actor.actor_id": "actor_id",
        "actor.first_name": "first_name"
      }
    }
  }
}
```

This is not the full C6 grammar yet. It is the foundation for porting the rest
of CarbonNode's canonical query grammar into C behind stable fixtures.

## Public C API

The public header is `include/carbon.h`.

Primary entrypoints:

- `carbon_version()`
- `carbon_context_new()`
- `carbon_context_free()`
- `carbon_buffer_init()`
- `carbon_buffer_free()`
- `carbon_status_code()`
- `carbon_status_message()`
- `carbon_compile_query()`
- `carbon_compile_result_diagnostics_json()`
- `carbon_schema_metadata()`
- `carbon_compile_result_free()`
- `carbon_normalize_allowlist_sql()`

All buffers returned by CarbonC are owned by the caller and must be released
with `carbon_buffer_free()` or `carbon_compile_result_free()`.

`carbon_compile_result_diagnostics_json()` returns a deterministic diagnostic
document for language bindings. Successful compiles include an empty
`diagnostics` array; failed compiles include a stable status code plus a
binding-friendly `source` and JSON-style `path`:

```json
{
  "status": 3,
  "status_code": "invalid_query",
  "ok": false,
  "diagnostics": [
    {
      "severity": "error",
      "code": "invalid_query",
      "message": "table is not present in schema",
      "source": "schema",
      "path": "$.FROM"
    }
  ]
}
```

`carbon_schema_metadata()` returns a canonical JSON shape that package-level
generators can turn into native models or types. When the input schema includes
CarbonNode-style `TYPE_VALIDATION` metadata keyed by qualified column name,
column entries include optional type details:

```json
{
  "tables": [
    {
      "name": "actor",
      "columns": [
        {
          "name": "actor_id",
          "qualified": "actor.actor_id",
          "db_type": "smallint",
          "max_length": "",
          "nullable": false,
          "auto_increment": true,
          "skip_insert": false
        },
        {
          "name": "first_name",
          "qualified": "actor.first_name",
          "db_type": "varchar",
          "max_length": "45",
          "nullable": false,
          "auto_increment": false,
          "skip_insert": false
        }
      ],
      "primary": ["actor_id"]
    }
  ]
}
```

## Build And Test

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Python Binding

The Python binding wraps the same C ABI from
`bindings/python/carbon_python.c` and returns a plain Python `dict` with
`status`, `status_code`, `sql`, `params_json`, `allowlist_key`, `error`, and
`diagnostics_json` fields. `bindings/python/carbon_codegen.py` adds
native-payload compile helpers, typed result adapters, source generation helpers
over the normalized schema metadata, and an optional `Query` facade for
incremental construction:

```python
import json
import carbon
import carbon_codegen

schema = {
    "TABLES": {
        "actor": {
            "PRIMARY_SHORT": ["actor_id"],
            "COLUMNS": {
                "actor.actor_id": "actor_id",
                "actor.first_name": "first_name",
            },
            "TYPE_VALIDATION": {
                "actor.actor_id": {
                    "COLUMN_NAME": "actor_id",
                    "MYSQL_TYPE": "smallint",
                    "NOT_NULL": True,
                },
                "actor.first_name": {
                    "COLUMN_NAME": "first_name",
                    "MYSQL_TYPE": "varchar",
                    "NOT_NULL": True,
                },
            },
        }
    }
}
generated: dict[str, object] = {}
exec(carbon_codegen.schema_models(schema), generated)
Actor = generated["Actor"]

query = {
    carbon_codegen.C6C.FROM: Actor.TABLE,
    carbon_codegen.C6C.SELECT: [Actor.ACTOR_ID],
    carbon_codegen.C6C.WHERE: {
        Actor.ACTOR_ID: carbon_codegen.eq_lit(10),
    },
    carbon_codegen.C6C.PAGINATION: {carbon_codegen.C6C.LIMIT: 5},
}
typed_result = carbon_codegen.compile_query_result(
    query,
    schema=schema,
    dialect=carbon_codegen.CarbonDialect.MYSQL,
)
get_request = Actor.Get(
    {
        carbon_codegen.C6C.SELECT: [Actor.ACTOR_ID],
        carbon_codegen.C6C.WHERE: {
            Actor.ACTOR_ID: carbon_codegen.eq_lit(10),
        },
        carbon_codegen.C6C.PAGINATION: {carbon_codegen.C6C.LIMIT: 500},
        "cacheResults": False,
    },
    schema=schema,
    dialect=carbon_codegen.CarbonDialect.MYSQL,
    context={"deviceClass": "mobile"},
    policy={"serverOnMobile": True},
)
field_column = carbon_codegen.model_column(Actor, Actor.FIELD_ACTOR_ID)
write_payload = {
    carbon_codegen.C6C.FROM: Actor.TABLE,
    carbon_codegen.C6C.INSERT: carbon_codegen.model_values(
        Actor,
        {Actor.FIELD_FIRST_NAME: "ALICE"},
    ),
}
write_result = carbon_codegen.compile_query_result(
    write_payload,
    schema=schema,
    dialect=carbon_codegen.CarbonDialect.MYSQL,
)
metadata_json = carbon.schema_metadata(json.dumps(schema))
dataclass_source = carbon_codegen.schema_models(schema)
```

The default package path is to pass a complete native dict payload through
`compile_query_result()` / `compile_query_value()`, matching CarbonORM's
front-end/back-end query-object convention. `Query.to_payload()` is available
when callers want incremental construction; it returns the canonical dict sent
through the C JSON boundary
for table/from, select, where, join, group/having, write operations
(`insert`, `replace`, `update`, `delete`, `upsert`, `do_nothing`), scalar
subselect helpers, derived `join_subselect` targets, advanced predicate helpers
(`where_op`, `where_in`, `where_not_in`, `where_between`, `where_not_between`,
`where_match_against`, `where_exists`, `where_not_exists`), composable boolean group helpers
(`condition`, `and_`, `or_`, `where_and`, `where_or`), expression helpers
(`fn`, `custom_call`, `call`, `lit`, `eq_lit`, `in_lit`, `not_in_lit`,
`between_lit`, `param`, `st_contains`, `st_within`,
`mbr_contains`), CarbonNode-compatible token constants (`C6C` / `C6`),
dialect constants (`CarbonDialect` / `Dialect`), index hint helpers (`index_hints`, `force_index`,
`use_index`, `ignore_index`), limit/page, and order controls.
`compile_query_result()` returns the same fields as the raw result plus decoded
Python `params` and `diagnostics` values. The generated dataclass source maps DB
metadata into Python annotations such as
`int`, `float`, `str`, `bool`, `bytes`, `Dict[str, Any]`, and `Optional[...]`,
includes `TABLE`, `PRIMARY`, `FIELDS`, `COLUMNS`, field constants such as
`Actor.FIELD_ACTOR_ID`, direct column constants such as `Actor.ACTOR_ID`, and
retains `__carbon_db_types__` / `__carbon_nullable__` metadata. Generated
classes also expose `Get()` / `GetPayload()` delegates for routeable model
requests. Runtime helpers
such as `model_query()`, `model_select()`, and `model_column()` consume that
generated metadata to build schema-backed `Query` payloads. Model write helpers
such as `model_insert()`, `model_replace()`, `model_update()`,
`model_upsert()`, and `model_do_nothing()` accept values keyed by generated
field constants and map them to qualified write columns. Runtime routing helpers
`model_get_payload()`, `route_query()`, `query_execution_request()`, and
`model_get_request()` keep the same native query object usable in front-end,
back-end, or server-offloaded contexts; they return route metadata only and do
not execute database calls.

Build and smoke-test it from the repository root:

```bash
(cd bindings/python && python3 setup.py build_ext --inplace)
PYTHONPATH=bindings/python python3 bindings/python/smoke.py
```

## PHP Binding

The PHP extension wraps the same compile result shape from
`bindings/php/carbon_php.c` as an associative array, including
`diagnostics_json`. The
`bindings/php/carbon_codegen.php` helper adds `C6C` / `C6`, `CarbonDialect`,
`carbon_compile_query_value()` for native arrays, typed result adapters, native
PHP model class source over `carbon_schema_metadata()`, and an optional
`CarbonQuery` facade for incremental construction:

```php
require_once __DIR__ . "/bindings/php/carbon_codegen.php";

$schema = [
    "TABLES" => [
        "actor" => [
            "PRIMARY_SHORT" => ["actor_id"],
            "COLUMNS" => [
                "actor.actor_id" => "actor_id",
                "actor.first_name" => "first_name",
            ],
            "TYPE_VALIDATION" => [
                "actor.actor_id" => [
                    "COLUMN_NAME" => "actor_id",
                    "MYSQL_TYPE" => "smallint",
                    "NOT_NULL" => true,
                ],
                "actor.first_name" => [
                    "COLUMN_NAME" => "first_name",
                    "MYSQL_TYPE" => "varchar",
                    "NOT_NULL" => true,
                ],
            ],
        ],
    ],
];
$modelSource = carbon_schema_models($schema);
eval(preg_replace('/^<\\?php\\s*/', '', $modelSource));

$query = [
    C6C::FROM => Actor::TABLE,
    C6C::SELECT => [Actor::ACTOR_ID],
    C6C::WHERE => [
        Actor::ACTOR_ID => carbon_eq_lit(10),
    ],
    C6C::PAGINATION => [C6C::LIMIT => 5],
];
$typedResult = carbon_compile_query_result($query, $schema, CarbonDialect::MYSQL);
$getRequest = Actor::Get(
    [
        C6C::SELECT => [Actor::ACTOR_ID],
        C6C::WHERE => [
            Actor::ACTOR_ID => carbon_eq_lit(10),
        ],
        C6C::PAGINATION => [C6C::LIMIT => 500],
        "cacheResults" => false,
    ],
    $schema,
    CarbonDialect::MYSQL,
    ["deviceClass" => "mobile"],
    ["serverOnMobile" => true]
);
$fieldColumn = carbon_model_column(Actor::class, Actor::FIELD_ACTOR_ID);
$writePayload = [
    C6C::FROM => Actor::TABLE,
    C6C::INSERT => carbon_model_values(Actor::class, [
        Actor::FIELD_FIRST_NAME => "ALICE",
    ]),
];
$writeResult = carbon_compile_query_result($writePayload, $schema, CarbonDialect::MYSQL);
$metadataJson = carbon_schema_metadata(json_encode($schema));
$namespacedModelSource = carbon_schema_models($schema, "CarbonORM\\Generated");
```

The default package path is to pass a complete native array payload through
`carbon_compile_query_result()` / `carbon_compile_query_value()`, matching
CarbonORM's front-end/back-end query-object convention. `CarbonQuery::toPayload()`
is available when callers want incremental construction; it returns the
canonical array sent through the C JSON
boundary for table/from, select, where, join, group/having, write operations
(`insert`, `replace`, `update`, `delete`, `upsert`, `doNothing`), scalar
`carbon_subselect()` helpers, derived `joinSubselect()` targets, advanced
predicate helpers (`whereOp`, `whereIn`, `whereNotIn`, `whereBetween`,
`whereNotBetween`, `whereMatchAgainst`, `whereExists`, `whereNotExists`), composable boolean group
helpers (`carbon_condition`, `carbon_and_group`, `carbon_or_group`, `whereAnd`,
`whereOr`), expression helpers (`carbon_fn`, `carbon_custom_call`,
`carbon_call`, `carbon_lit`, `carbon_eq_lit`, `carbon_in_lit`,
`carbon_not_in_lit`, `carbon_between_lit`, `carbon_param`, `carbon_st_contains`,
`carbon_st_within`, `carbon_mbr_contains`), index hint helpers
(`indexHints`, `forceIndex`, `useIndex`, `ignoreIndex`, `carbon_force_index`,
`carbon_use_index`, `carbon_ignore_index`), limit/page, and order controls.
`carbon_compile_query_result()`
returns the same fields as the
raw result plus decoded PHP `params` and `diagnostics` values. The generated PHP
class source keeps properties untyped for runtime compatibility, adds PHPDoc type
annotations, and includes `TABLE`, `PRIMARY`, `FIELDS`, `COLUMNS`, field
constants such as `Actor::FIELD_ACTOR_ID`, direct column constants such as
`Actor::ACTOR_ID`, `DB_TYPES`, and `NULLABLE`. Generated classes also expose
static `Get()` / `GetPayload()` delegates for routeable model requests. Runtime
helpers such as
`carbon_model_query()`, `carbon_model_select()`, and `carbon_model_column()`
consume generated class constants or metadata arrays to build schema-backed
`CarbonQuery` payloads. Model write helpers such as `carbon_model_insert()`,
`carbon_model_replace()`, `carbon_model_update()`, `carbon_model_upsert()`, and
`carbon_model_do_nothing()` accept values keyed by generated field constants and
map them to qualified write columns. Runtime routing helpers
`carbon_model_get_payload()`, `carbon_route_query()`,
`carbon_query_execution_request()`, and `carbon_model_get_request()` keep the
same native query object usable in front-end, back-end, or server-offloaded
contexts; they return route metadata only and do not execute database calls.

Build and smoke-test it from the repository root:

```bash
(cd bindings/php && bash build.sh)
php -d extension=bindings/php/modules/carbon.so bindings/php/smoke.php
php -d extension=bindings/php/modules/carbon.so examples/php/index.php
```

The PHP surface exposes `carbon_version()`, `carbon_hello_world()`,
`carbon_status_code()`, `carbon_status_message()`, `carbon_compile_query()`,
`carbon_compile_query_value()`, `carbon_compile_query_result()`,
`carbon_adapt_compile_result()`, `carbon_query()`, `carbon_force_index()`,
`carbon_use_index()`, `carbon_ignore_index()`, `C6C`, `C6`, `CarbonDialect`,
`CarbonExecutionTarget`, `carbon_route_query()`,
`carbon_query_execution_request()`, `carbon_model_get_payload()`,
`carbon_model_get_request()`,
`carbon_model_query()`, `carbon_model_select()`, `carbon_model_column()`,
`carbon_model_insert()`, `carbon_model_replace()`, `carbon_model_update()`,
`carbon_model_upsert()`, `carbon_model_do_nothing()`,
`carbon_schema_metadata()`,
and `carbon_normalize_allowlist_sql()`.

## Node Binding

The Node binding uses plain N-API from `bindings/node/carbon_node.cpp` and
exports camelCase methods plus snake_case aliases. `bindings/node/index.js`
adds `compileQueryValue()` / `compile_query_value()` for native objects, typed
result adapters, a package-level TypeScript source generator, and an optional
`CarbonQuery` / `query()` facade for incremental construction. Compile results include
`diagnostics_json` beside the status, SQL, params, allowlist, and error fields:

```js
const carbon = require('./bindings/node');

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
          NOT_NULL: true,
        },
        'actor.first_name': {
          COLUMN_NAME: 'first_name',
          MYSQL_TYPE: 'varchar',
          NOT_NULL: true,
        },
      },
    },
  },
};
const metadata = JSON.parse(carbon.schemaMetadata(JSON.stringify(schema)));
const typeSource = carbon.schemaModels(schema);
// The generated TypeScript module exports the same ActorTable/ActorColumns shape,
// plus an Actor object with Get/GetPayload delegates.
const actorMetadata = metadata.tables[0];
const ActorTable = actorMetadata.name;
const ActorFields = Object.freeze(Object.fromEntries(
  actorMetadata.columns.map((column) => [column.name, column.name])
));
const ActorColumns = Object.freeze(Object.fromEntries(
  actorMetadata.columns.map((column) => [column.name, column.qualified])
));
const ActorMeta = {table: ActorTable, fields: ActorFields, columns: ActorColumns};
const Actor = carbon.modelApi(ActorMeta);

const query = {
  [carbon.C6C.FROM]: ActorTable,
  [carbon.C6C.SELECT]: [ActorColumns.actor_id],
  [carbon.C6C.WHERE]: {
    [ActorColumns.actor_id]: carbon.eqLit(10),
  },
  [carbon.C6C.PAGINATION]: {[carbon.C6C.LIMIT]: 5},
};
const typedResult = carbon.compileQueryResult(
  query,
  schema,
  carbon.CarbonDialect.MYSQL,
);
const getRequest = Actor.Get({
  [carbon.C6C.SELECT]: [Actor.COLUMNS.actor_id],
  [carbon.C6C.WHERE]: {
    [Actor.COLUMNS.actor_id]: carbon.eqLit(10),
  },
  [carbon.C6C.PAGINATION]: {[carbon.C6C.LIMIT]: 500},
  cacheResults: false,
}, {
  schema,
  dialect: carbon.CarbonDialect.MYSQL,
  context: {deviceClass: 'mobile'},
  policy: {serverOnMobile: true},
});
const fieldColumn = carbon.modelColumn(ActorMeta, ActorFields.actor_id);
const writePayload = {
  [carbon.C6C.FROM]: ActorTable,
  [carbon.C6C.INSERT]: carbon.modelValues(ActorMeta, {
    [ActorFields.first_name]: 'ALICE',
  }),
};
const writeResult = carbon.compileQueryResult(
  writePayload,
  schema,
  carbon.CarbonDialect.MYSQL,
);
const metadataJson = JSON.stringify(metadata);
```

The default package path is to pass a complete native object payload through
`compileQueryResult()` / `compileQueryValue()`, matching CarbonORM's
front-end/back-end query-object convention. `CarbonQuery.toPayload()` is
available when callers want incremental construction; it returns the canonical
object sent through the C JSON
boundary for table/from, select, where, join, group/having, write operations
(`insert`, `replace`, `update`, `delete`, `upsert`, `doNothing`), scalar
`subselect()` helpers, derived `joinSubselect()` targets, advanced predicate
helpers (`whereOp`, `whereIn`, `whereNotIn`, `whereBetween`, `whereNotBetween`,
`whereMatchAgainst`, `whereExists`, `whereNotExists`), composable boolean group helpers
(`condition`, `andGroup`, `orGroup`, `whereAnd`, `whereOr`), expression helpers
(`fn`, `customCall`, `call`, `lit`, `eqLit`, `inLit`, `notInLit`,
`betweenLit`, `param`, `stContains`, `stWithin`,
`mbrContains`), index hint helpers (`indexHints`, `forceIndex`, `useIndex`,
`ignoreIndex`), limit/page, and order controls.
`compileQueryResult()` returns the same fields as the raw result
plus decoded JavaScript `params` and `diagnostics` values. The generated
TypeScript source maps DB metadata into primitive field types, exports
`ActorTable`, `ActorFields`, and `ActorColumns` constants beside each interface,
adds `fields`, `dbTypes`, and `nullable` metadata to the generated `*Meta`
object, and exports an `Actor` model object with `Get()` / `GetPayload()`
delegates. Runtime helpers such as `modelApi()`, `modelQuery()`, `modelSelect()`, and
`modelColumn()` consume generated `*Meta` objects to build schema-backed
`CarbonQuery` payloads. Model write helpers such as `modelInsert()`,
`modelReplace()`, `modelUpdate()`, `modelUpsert()`, and `modelDoNothing()`
accept values keyed by generated field constants and map them to qualified write
columns. Runtime routing helpers `modelGetPayload()`, `routeQuery()`,
`queryExecutionRequest()`, and `modelGetRequest()` keep the same native query
object usable in front-end, back-end, or server-offloaded contexts; they return
route metadata only and do not execute database calls.

Build and smoke-test it from the repository root:

```bash
(cd bindings/node && bash build.sh)
node bindings/node/smoke.js
node examples/node/index.js
```

The addon exposes `version()`, `helloWorld()`, `statusMessage()`,
`statusCode()`, `compileQuery()`, `compileQueryValue()`,
`compileQueryResult()`, `adaptCompileResult()`, `C6C`, `C6`, `CarbonDialect`,
`Dialect`, `CarbonExecutionTarget`, `ExecutionTarget`, `CarbonQuery`, `query()`,
`fromTable()`, `subselect()`, `derivedTarget()`, `op()`, `lit()`, `eqLit()`,
`inLit()`, `notInLit()`, `betweenLit()`, `param()`, `call()`, `alias()`,
`distinct()`, `between()`, `inList()`, `existsSpec()`,
`exists()`, `notExists()`, `condition()`, `andGroup()`, `orGroup()`,
`forceIndex()`, `useIndex()`, `ignoreIndex()`, `modelQuery()`, `modelSelect()`,
`modelColumn()`, `modelApi()`, `modelGetPayload()`, `modelGetRequest()`, `routeQuery()`,
`queryExecutionRequest()`, `modelInsert()`, `modelReplace()`, `modelUpdate()`,
`modelUpsert()`, `modelDoNothing()`, `schemaMetadata()`, `schemaModels()`, and
`normalizeAllowlistSql()`, plus snake_case aliases for the multiword functions.

## Ruby Binding

The Ruby extension wraps the same compile result shape from
`bindings/ruby/carbon_ruby.c` as a `Hash` with string keys, including
`diagnostics_json`. The
`bindings/ruby/carbon_codegen.rb` helper adds `CarbonC::C6C` / `CarbonC::C6`,
`CarbonC::Dialect`, `CarbonC.compile_query_value` for native hashes, typed
result adapters, Ruby Struct model source over `CarbonC.schema_metadata`, and
an optional `CarbonC::Query` facade for incremental construction:

```ruby
require 'json'
require_relative './bindings/ruby/carbon_codegen'

schema = {
  'TABLES' => {
    'actor' => {
      'PRIMARY_SHORT' => ['actor_id'],
      'COLUMNS' => {
        'actor.actor_id' => 'actor_id',
        'actor.first_name' => 'first_name'
      },
      'TYPE_VALIDATION' => {
        'actor.actor_id' => {
          'COLUMN_NAME' => 'actor_id',
          'MYSQL_TYPE' => 'smallint',
          'NOT_NULL' => true
        },
        'actor.first_name' => {
          'COLUMN_NAME' => 'first_name',
          'MYSQL_TYPE' => 'varchar',
          'NOT_NULL' => true
        }
      }
    }
  }
}
eval(CarbonC.schema_models(schema))

query = {
  CarbonC::C6C::FROM => CarbonModels::Actor::TABLE,
  CarbonC::C6C::SELECT => [CarbonModels::Actor::ACTOR_ID],
  CarbonC::C6C::WHERE => {
    CarbonModels::Actor::ACTOR_ID => CarbonC.eq_lit(10)
  },
  CarbonC::C6C::PAGINATION => {CarbonC::C6C::LIMIT => 5}
}
typed_result = CarbonC.compile_query_result(query, schema, CarbonC::Dialect::MYSQL)
get_request = CarbonModels::Actor.Get(
  {
    CarbonC::C6C::SELECT => [CarbonModels::Actor::ACTOR_ID],
    CarbonC::C6C::WHERE => {
      CarbonModels::Actor::ACTOR_ID => CarbonC.eq_lit(10)
    },
    CarbonC::C6C::PAGINATION => {CarbonC::C6C::LIMIT => 500},
    'cacheResults' => false
  },
  schema: schema,
  dialect: CarbonC::Dialect::MYSQL,
  context: {'deviceClass' => 'mobile'},
  policy: {'serverOnMobile' => true}
)
field_column = CarbonC.model_column(CarbonModels::Actor, CarbonModels::Actor::FIELD_ACTOR_ID)
write_payload = {
  CarbonC::C6C::FROM => CarbonModels::Actor::TABLE,
  CarbonC::C6C::INSERT => CarbonC.model_values(
    CarbonModels::Actor,
    CarbonModels::Actor::FIELD_FIRST_NAME => 'ALICE'
  )
}
write_result = CarbonC.compile_query_result(write_payload, schema, CarbonC::Dialect::MYSQL)
metadata_json = CarbonC.schema_metadata(JSON.generate(schema))
model_source = CarbonC.schema_models(schema)
```

The default package path is to pass a complete native hash payload through
`CarbonC.compile_query_result` / `CarbonC.compile_query_value`, matching
CarbonORM's front-end/back-end query-object convention. `CarbonC::Query#to_payload`
is available when callers want incremental construction; it returns the
canonical hash sent through the C JSON
boundary for table/from, select, where, join, group/having, write operations
(`insert`, `replace`, `update`, `delete`, `upsert`, `do_nothing`), scalar
`CarbonC.subselect` helpers, derived `join_subselect` targets, advanced
predicate helpers (`where_op`, `where_in`, `where_not_in`, `where_between`,
`where_not_between`, `where_match_against`, `where_exists`, `where_not_exists`), composable boolean
group helpers (`CarbonC.condition`, `CarbonC.and_group`, `CarbonC.or_group`,
`where_and`, `where_or`), expression helpers (`CarbonC.fn`,
`CarbonC.custom_call`, `CarbonC.call`, `CarbonC.lit`, `CarbonC.eq_lit`,
`CarbonC.in_lit`, `CarbonC.not_in_lit`, `CarbonC.between_lit`, `CarbonC.param`,
`CarbonC.st_contains`, `CarbonC.st_within`, `CarbonC.mbr_contains`), index hint
helpers (`index_hints`, `force_index`, `use_index`, `ignore_index`), limit/page,
and order controls.
`CarbonC.compile_query_result` returns the same fields as the raw
result plus decoded Ruby `params` and `diagnostics` values. The generated Ruby
Struct source includes `TABLE`, `PRIMARY`, `FIELDS`, `COLUMNS`, field constants
such as `CarbonModels::Actor::FIELD_ACTOR_ID`, direct column constants such as
`CarbonModels::Actor::ACTOR_ID`, plus `TYPES` and `NULLABLE` metadata constants
for runtime consumers. Generated Struct classes also expose `Get` / `GetPayload`
delegates for routeable model requests. Runtime helpers such as `CarbonC.model_query`,
`CarbonC.model_select`, and `CarbonC.model_column` consume generated constants
or metadata hashes to build schema-backed `CarbonC::Query` payloads. Model write
helpers such as `CarbonC.model_insert`, `CarbonC.model_replace`,
`CarbonC.model_update`, `CarbonC.model_upsert`, and `CarbonC.model_do_nothing`
accept values keyed by generated field constants and map them to qualified write
columns. Runtime routing helpers `CarbonC.model_get_payload`,
`CarbonC.route_query`, `CarbonC.query_execution_request`, and
`CarbonC.model_get_request` keep the same native query object usable in
front-end, back-end, or server-offloaded contexts; they return route metadata
only and do not execute database calls.

Build and smoke-test it from the repository root:

```bash
(cd bindings/ruby && bash build.sh)
ruby bindings/ruby/smoke.rb
ruby examples/ruby/example.rb
```

The extension exposes `CarbonC.version`, `CarbonC.hello_world`,
`CarbonC.status_code`, `CarbonC.status_message`, `CarbonC.compile_query`,
`CarbonC.compile_query_value`, `CarbonC.compile_query_result`,
`CarbonC.adapt_compile_result`, `CarbonC::C6C`, `CarbonC::C6`,
`CarbonC::Dialect`, `CarbonC::ExecutionTarget`,
`CarbonC.query`, `CarbonC.from_table`,
`CarbonC.subselect`, `CarbonC.derived_target`, `CarbonC.op`, `CarbonC.lit`,
`CarbonC.eq_lit`, `CarbonC.in_lit`, `CarbonC.not_in_lit`,
`CarbonC.between_lit`,
`CarbonC.param`, `CarbonC.call`, `CarbonC.alias_expression`,
`CarbonC.distinct`, `CarbonC.between`, `CarbonC.in_list`,
`CarbonC.exists_spec`, `CarbonC.exists`, `CarbonC.not_exists`,
`CarbonC.condition`, `CarbonC.and_group`, `CarbonC.or_group`,
`CarbonC.force_index`, `CarbonC.use_index`, `CarbonC.ignore_index`,
`CarbonC.model_query`, `CarbonC.model_select`, `CarbonC.model_column`,
`CarbonC.model_get_payload`, `CarbonC.model_get_request`,
`CarbonC.route_query`, `CarbonC.query_execution_request`,
`CarbonC.model_insert`, `CarbonC.model_replace`, `CarbonC.model_update`,
`CarbonC.model_upsert`, `CarbonC.model_do_nothing`,
`CarbonC.schema_metadata`, and
`CarbonC.normalize_allowlist_sql`.

## Next Milestones

1. Expand the remaining CarbonNode C6 grammar behind golden fixtures.
2. Add richer multi-diagnostic reporting for validation batches.
3. Add higher-level package examples and fixture imports from production C6
   query shapes as the remaining grammar lands.
