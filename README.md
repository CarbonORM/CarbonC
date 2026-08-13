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
    "actor.actor_id": [">", 10]
  },
  "PAGINATION": {
    "ORDER": [["actor.last_name", "ASC"]],
    "LIMIT": 25
  }
}
```

The compiler returns a numeric `status`, stable string `status_code`, SQL, a
JSON array of bound parameter values, and a normalized allowlist key:

```sql
SELECT actor.actor_id, actor.first_name FROM `actor` WHERE (actor.actor_id) > ? ORDER BY actor.last_name ASC LIMIT 25
```

```json
[10]
```

```text
SELECT actor.actor_id, actor.first_name FROM `actor` WHERE (actor.actor_id) > ? ORDER BY actor.last_name ASC LIMIT ?
```

Supported in this slice:

- schema metadata normalization into deterministic JSON for generated binding
  types
- schema metadata enrichment from CarbonNode-style `TYPE_VALIDATION` entries,
  including DB type, max length, nullability, auto-increment, and insert-skip
  flags
- package-level typed source generation helpers for Python dataclasses,
  TypeScript interfaces, PHP model classes, and Ruby Struct models
- package-level native payload compile helpers that serialize idiomatic
  dict/array/object/hash inputs into the stable C JSON boundary
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
- `WHERE` column mappings, `AND` / `OR`, comparison operators, `IN`, `NOT_IN`,
  `BETWEEN`, `IS`, `IS_NOT`, `EXISTS`, `NOT_EXISTS`, `MATCH_AGAINST`, `LIT`,
  and `PARAM`
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
`diagnostics_json` fields. `bindings/python/carbon_codegen.py` adds a
`Query` facade, native-payload compile helpers, typed result adapters, and
source generation helpers over the normalized schema metadata:

```python
import json
import carbon
import carbon_codegen

typed_result = (
    carbon_codegen.query("actor")
    .select("actor.actor_id")
    .where({"actor.actor_id": [">", 10]})
    .limit(5)
    .compile(
        schema={"TABLES": {"actor": {"COLUMNS": {"actor.actor_id": "actor_id"}}}},
        dialect="mysql",
    )
)
metadata_json = carbon.schema_metadata(json.dumps({
    "TABLES": {
        "actor": {
            "PRIMARY_SHORT": ["actor_id"],
            "COLUMNS": {"actor.actor_id": "actor_id"}
        }
    }
}))
dataclass_source = carbon_codegen.schema_models({
    "TABLES": {
        "actor": {
            "PRIMARY_SHORT": ["actor_id"],
            "COLUMNS": {"actor.actor_id": "actor_id"},
            "TYPE_VALIDATION": {
                "actor.actor_id": {
                    "COLUMN_NAME": "actor_id",
                    "MYSQL_TYPE": "smallint",
                    "NOT_NULL": True
                }
            }
        }
    }
})
```

`Query.to_payload()` returns the canonical dict sent through the C JSON boundary
for table/from, select, where, join, group/having, write operations
(`insert`, `replace`, `update`, `delete`, `upsert`, `do_nothing`), scalar
subselect helpers, derived `join_subselect` targets, advanced predicate helpers
(`where_op`, `where_in`, `where_not_in`, `where_between`, `where_not_between`,
`where_match_against`, `where_exists`, `where_not_exists`), composable boolean group helpers
(`condition`, `and_`, `or_`, `where_and`, `where_or`), expression helpers
(`fn`, `custom_call`, `call`, `lit`, `param`), limit/page, and order
controls.
`compile_query_result()` returns the same fields as the raw result plus decoded
Python `params` and `diagnostics` values. The generated dataclass source maps DB
metadata into Python annotations such as
`int`, `float`, `str`, `bool`, `bytes`, `Dict[str, Any]`, and `Optional[...]`,
and includes `__carbon_db_types__` and `__carbon_nullable__` metadata. Runtime
helpers such as `model_query()`, `model_select()`, and `model_column()` consume
that generated metadata to build schema-backed `Query` payloads.

Build and smoke-test it from the repository root:

```bash
(cd bindings/python && python3 setup.py build_ext --inplace)
PYTHONPATH=bindings/python python3 bindings/python/smoke.py
```

## PHP Binding

The PHP extension wraps the same compile result shape from
`bindings/php/carbon_php.c` as an associative array, including
`diagnostics_json`. The
`bindings/php/carbon_codegen.php` helper adds `CarbonQuery`,
`carbon_compile_query_value()` for native arrays, typed result adapters, and
native PHP model class source over
`carbon_schema_metadata()`:

```php
require_once __DIR__ . "/bindings/php/carbon_codegen.php";

$typedResult = carbon_query("actor")
    ->select("actor.actor_id")
    ->where(["actor.actor_id" => [">", 10]])
    ->limit(5)
    ->compile(
        ["TABLES" => ["actor" => ["COLUMNS" => ["actor.actor_id" => "actor_id"]]]],
        "mysql"
    );
$metadataJson = carbon_schema_metadata(json_encode([
    "TABLES" => [
        "actor" => [
            "PRIMARY_SHORT" => ["actor_id"],
            "COLUMNS" => ["actor.actor_id" => "actor_id"],
        ],
    ],
]));
$modelSource = carbon_schema_models([
    "TABLES" => [
        "actor" => [
            "PRIMARY_SHORT" => ["actor_id"],
            "COLUMNS" => ["actor.actor_id" => "actor_id"],
            "TYPE_VALIDATION" => [
                "actor.actor_id" => [
                    "COLUMN_NAME" => "actor_id",
                    "MYSQL_TYPE" => "smallint",
                    "NOT_NULL" => true,
                ],
            ],
        ],
    ],
], "CarbonORM\\Generated");
```

`CarbonQuery::toPayload()` returns the canonical array sent through the C JSON
boundary for table/from, select, where, join, group/having, write operations
(`insert`, `replace`, `update`, `delete`, `upsert`, `doNothing`), scalar
`carbon_subselect()` helpers, derived `joinSubselect()` targets, advanced
predicate helpers (`whereOp`, `whereIn`, `whereNotIn`, `whereBetween`,
`whereNotBetween`, `whereMatchAgainst`, `whereExists`, `whereNotExists`), composable boolean group
helpers (`carbon_condition`, `carbon_and_group`, `carbon_or_group`, `whereAnd`,
`whereOr`), expression helpers (`carbon_fn`, `carbon_custom_call`,
`carbon_call`, `carbon_lit`, `carbon_param`), limit/page, and order controls.
`carbon_compile_query_result()`
returns the same fields as the
raw result plus decoded PHP `params` and `diagnostics` values. The generated PHP
class source keeps properties untyped for runtime compatibility, adds PHPDoc type
annotations, and includes `DB_TYPES` and `NULLABLE` constants. Runtime helpers
such as `carbon_model_query()`, `carbon_model_select()`, and
`carbon_model_column()` consume generated class constants or metadata arrays to
build schema-backed `CarbonQuery` payloads.

Build and smoke-test it from the repository root:

```bash
(cd bindings/php && bash build.sh)
php -d extension=bindings/php/modules/carbon.so bindings/php/smoke.php
php -d extension=bindings/php/modules/carbon.so examples/php/index.php
```

The extension exposes `carbon_version()`, `carbon_hello_world()`,
`carbon_status_code()`, `carbon_status_message()`, `carbon_compile_query()`,
`carbon_compile_query_value()`, `carbon_compile_query_result()`,
`carbon_adapt_compile_result()`, `carbon_query()`, `carbon_schema_metadata()`,
and `carbon_normalize_allowlist_sql()`.

## Node Binding

The Node binding uses plain N-API from `bindings/node/carbon_node.cpp` and
exports camelCase methods plus snake_case aliases. `bindings/node/index.js`
adds `CarbonQuery`, `query()`, `compileQueryValue()` / `compile_query_value()`
for native objects, typed result adapters, and a package-level TypeScript source
generator. Compile results include
`diagnostics_json` beside the status, SQL, params, allowlist, and error fields:

```js
const carbon = require('./bindings/node');

const typedResult = carbon.query('actor')
  .select('actor.actor_id')
  .where({'actor.actor_id': ['>', 10]})
  .limit(5)
  .compile(
    {TABLES: {actor: {COLUMNS: {'actor.actor_id': 'actor_id'}}}},
    'mysql'
  );
const metadataJson = carbon.schemaMetadata(JSON.stringify({
  TABLES: {
    actor: {
      PRIMARY_SHORT: ['actor_id'],
      COLUMNS: {'actor.actor_id': 'actor_id'},
    },
  },
}));
const typeSource = carbon.schemaModels({
  TABLES: {
    actor: {
      PRIMARY_SHORT: ['actor_id'],
      COLUMNS: {'actor.actor_id': 'actor_id'},
      TYPE_VALIDATION: {
        'actor.actor_id': {
          COLUMN_NAME: 'actor_id',
          MYSQL_TYPE: 'smallint',
          NOT_NULL: true,
        },
      },
    },
  },
});
```

`CarbonQuery.toPayload()` returns the canonical object sent through the C JSON
boundary for table/from, select, where, join, group/having, write operations
(`insert`, `replace`, `update`, `delete`, `upsert`, `doNothing`), scalar
`subselect()` helpers, derived `joinSubselect()` targets, advanced predicate
helpers (`whereOp`, `whereIn`, `whereNotIn`, `whereBetween`, `whereNotBetween`,
`whereMatchAgainst`, `whereExists`, `whereNotExists`), composable boolean group helpers
(`condition`, `andGroup`, `orGroup`, `whereAnd`, `whereOr`), expression helpers
(`fn`, `customCall`, `call`, `lit`, `param`), limit/page, and order controls.
`compileQueryResult()` returns the same fields as the raw result
plus decoded JavaScript `params` and `diagnostics` values. The generated
TypeScript source maps DB metadata into primitive field types and adds `dbTypes`
and `nullable` metadata beside each generated interface. Runtime helpers such as
`modelQuery()`, `modelSelect()`, and `modelColumn()` consume generated `*Meta`
objects to build schema-backed `CarbonQuery` payloads.

Build and smoke-test it from the repository root:

```bash
(cd bindings/node && bash build.sh)
node bindings/node/smoke.js
node examples/node/index.js
```

The addon exposes `version()`, `helloWorld()`, `statusMessage()`,
`statusCode()`, `compileQuery()`, `compileQueryValue()`,
`compileQueryResult()`, `adaptCompileResult()`, `CarbonQuery`, `query()`,
`fromTable()`, `subselect()`, `derivedTarget()`, `op()`, `lit()`, `param()`,
`call()`, `alias()`, `distinct()`, `between()`, `inList()`, `existsSpec()`,
`exists()`, `notExists()`, `condition()`, `andGroup()`, `orGroup()`,
`modelQuery()`, `modelSelect()`, `modelColumn()`, `schemaMetadata()`,
`schemaModels()`, and
`normalizeAllowlistSql()`, plus snake_case aliases for the multiword functions.

## Ruby Binding

The Ruby extension wraps the same compile result shape from
`bindings/ruby/carbon_ruby.c` as a `Hash` with string keys, including
`diagnostics_json`. The
`bindings/ruby/carbon_codegen.rb` helper adds `CarbonC::Query`,
`CarbonC.compile_query_value` for native hashes, typed result adapters, and Ruby
Struct model source over
`CarbonC.schema_metadata`:

```ruby
require 'json'
require_relative './bindings/ruby/carbon_codegen'

typed_result = CarbonC.query('actor')
                      .select('actor.actor_id')
                      .where({'actor.actor_id' => ['>', 10]})
                      .limit(5)
                      .compile(
                        {'TABLES' => {'actor' => {'COLUMNS' => {'actor.actor_id' => 'actor_id'}}}},
                        'mysql'
                      )
metadata_json = CarbonC.schema_metadata(JSON.generate({
  'TABLES' => {
    'actor' => {
      'PRIMARY_SHORT' => ['actor_id'],
      'COLUMNS' => {'actor.actor_id' => 'actor_id'}
    }
  }
}))
model_source = CarbonC.schema_models({
  'TABLES' => {
    'actor' => {
      'PRIMARY_SHORT' => ['actor_id'],
      'COLUMNS' => {'actor.actor_id' => 'actor_id'},
      'TYPE_VALIDATION' => {
        'actor.actor_id' => {
          'COLUMN_NAME' => 'actor_id',
          'MYSQL_TYPE' => 'smallint',
          'NOT_NULL' => true
        }
      }
    }
  }
})
```

`CarbonC::Query#to_payload` returns the canonical hash sent through the C JSON
boundary for table/from, select, where, join, group/having, write operations
(`insert`, `replace`, `update`, `delete`, `upsert`, `do_nothing`), scalar
`CarbonC.subselect` helpers, derived `join_subselect` targets, advanced
predicate helpers (`where_op`, `where_in`, `where_not_in`, `where_between`,
`where_not_between`, `where_match_against`, `where_exists`, `where_not_exists`), composable boolean
group helpers (`CarbonC.condition`, `CarbonC.and_group`, `CarbonC.or_group`,
`where_and`, `where_or`), expression helpers (`CarbonC.fn`,
`CarbonC.custom_call`, `CarbonC.call`, `CarbonC.lit`, `CarbonC.param`),
limit/page, and order controls.
`CarbonC.compile_query_result` returns the same fields as the raw
result plus decoded Ruby `params` and `diagnostics` values. The generated Ruby
Struct source includes `TYPES` and `NULLABLE` metadata constants for runtime
consumers. Runtime helpers such as `CarbonC.model_query`,
`CarbonC.model_select`, and `CarbonC.model_column` consume generated constants
or metadata hashes to build schema-backed `CarbonC::Query` payloads.

Build and smoke-test it from the repository root:

```bash
(cd bindings/ruby && bash build.sh)
ruby bindings/ruby/smoke.rb
ruby examples/ruby/example.rb
```

The extension exposes `CarbonC.version`, `CarbonC.hello_world`,
`CarbonC.status_code`, `CarbonC.status_message`, `CarbonC.compile_query`,
`CarbonC.compile_query_value`, `CarbonC.compile_query_result`,
`CarbonC.adapt_compile_result`, `CarbonC.query`, `CarbonC.from_table`,
`CarbonC.subselect`, `CarbonC.derived_target`, `CarbonC.op`, `CarbonC.lit`,
`CarbonC.param`, `CarbonC.call`, `CarbonC.alias_expression`,
`CarbonC.distinct`, `CarbonC.between`, `CarbonC.in_list`,
`CarbonC.exists_spec`, `CarbonC.exists`, `CarbonC.not_exists`,
`CarbonC.condition`, `CarbonC.and_group`, `CarbonC.or_group`,
`CarbonC.model_query`, `CarbonC.model_select`, `CarbonC.model_column`,
`CarbonC.schema_metadata`, and
`CarbonC.normalize_allowlist_sql`.

## Next Milestones

1. Expand the remaining CarbonNode C6 grammar behind golden fixtures.
2. Add richer multi-diagnostic reporting for validation batches.
3. Add higher-level package examples and fixture imports from production C6
   query shapes as the remaining grammar lands.
