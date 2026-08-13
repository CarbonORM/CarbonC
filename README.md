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
- schema-diff and schema-introspection normalization

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

- `FROM` / legacy `table`
- `SELECT` references, `AS`, `DISTINCT`, and function tuples
- `JOIN` clauses for `INNER`, `LEFT`, `LEFT_OUTER`, `RIGHT`, and
  `RIGHT_OUTER` table aliases
- `WHERE` column mappings, `AND` / `OR`, comparison operators, `IN`, `NOT_IN`,
  `BETWEEN`, `IS`, `IS_NOT`, `EXISTS`, `NOT_EXISTS`, `LIT`, and `PARAM`
- `GROUP_BY` expression lists and `HAVING` boolean clauses
- scalar `SUBSELECT` expressions in `SELECT` and `WHERE` operands
- explicit `INSERT`, `REPLACE`, `UPDATE`, and `DELETE` write payloads
- MySQL upsert update lists through `UPDATE: ["column_name"]`
- `PAGINATION.ORDER`, `LIMIT`, and `PAGE`
- MySQL `?` placeholders and PostgreSQL `$1`-style placeholders
- CarbonNode-style allowlist normalization for whitespace, `LIMIT` forms
  including `LIMIT ... OFFSET ...`, and `IN` bind-list cardinality
- schema-aware table, dotted-reference, join-alias, and write-column validation
  when `schema_json` includes a `TABLES` object

Write support is intentionally explicit: this slice accepts operation keys such
as `INSERT`, `REPLACE`, `UPDATE`, and `DELETE`, not CarbonNode's loose root-level
POST rows. Single-row insert/upsert payloads are covered now. PostgreSQL writes
currently cover simple insert/update/delete forms; joined writes, multi-row POST
normalization, and schema-derived conflict targets are later work.

Schema validation is opt-in for this slice. Passing `{}` keeps syntax-only
identifier checks. Passing a `TABLES` object validates against C6-style table
metadata:

```json
{
  "TABLES": {
    "actor": {
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
- `carbon_compile_result_free()`
- `carbon_normalize_allowlist_sql()`

All buffers returned by CarbonC are owned by the caller and must be released
with `carbon_buffer_free()` or `carbon_compile_result_free()`.

## Build And Test

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Python Binding

The Python binding wraps the same C ABI from
`bindings/python/carbon_python.c` and returns a plain Python `dict` with
`status`, `status_code`, `sql`, `params_json`, `allowlist_key`, and `error`
fields:

```python
import json
import carbon

result = carbon.compile_query(
    json.dumps({"FROM": "actor", "SELECT": ["actor.actor_id"]}),
    schema_json=json.dumps({
        "TABLES": {
            "actor": {"COLUMNS": {"actor.actor_id": "actor_id"}}
        }
    }),
    dialect="mysql",
)
```

Build and smoke-test it from the repository root:

```bash
(cd bindings/python && python3 setup.py build_ext --inplace)
PYTHONPATH=bindings/python python3 bindings/python/smoke.py
```

## PHP Binding

The PHP extension wraps the same compile result shape from
`bindings/php/carbon_php.c` as an associative array:

```php
$result = carbon_compile_query(
    json_encode(["FROM" => "actor", "SELECT" => ["actor.actor_id"]]),
    json_encode([
        "TABLES" => [
            "actor" => ["COLUMNS" => ["actor.actor_id" => "actor_id"]],
        ],
    ]),
    "mysql"
);
```

Build and smoke-test it from the repository root:

```bash
(cd bindings/php && bash build.sh)
php -d extension=bindings/php/modules/carbon.so bindings/php/smoke.php
php -d extension=bindings/php/modules/carbon.so examples/php/index.php
```

The extension exposes `carbon_version()`, `carbon_hello_world()`,
`carbon_status_code()`, `carbon_status_message()`, `carbon_compile_query()`,
and `carbon_normalize_allowlist_sql()`.

## Node Binding

The Node binding uses plain N-API from `bindings/node/carbon_node.cpp` and
exports camelCase methods plus snake_case aliases:

```js
const carbon = require('./bindings/node');

const result = carbon.compileQuery(
  JSON.stringify({FROM: 'actor', SELECT: ['actor.actor_id']}),
  JSON.stringify({
    TABLES: {
      actor: {COLUMNS: {'actor.actor_id': 'actor_id'}},
    },
  }),
  'mysql'
);
```

Build and smoke-test it from the repository root:

```bash
(cd bindings/node && bash build.sh)
node bindings/node/smoke.js
node examples/node/index.js
```

The addon exposes `version()`, `helloWorld()`, `statusMessage()`,
`statusCode()`, `compileQuery()`, and `normalizeAllowlistSql()`, plus
snake_case aliases for the multiword functions.

## Ruby Binding

The Ruby extension wraps the same compile result shape from
`bindings/ruby/carbon_ruby.c` as a `Hash` with string keys:

```ruby
require 'json'
require_relative './bindings/ruby/carbon'

result = CarbonC.compile_query(
  JSON.generate({'FROM' => 'actor', 'SELECT' => ['actor.actor_id']}),
  JSON.generate({
    'TABLES' => {
      'actor' => {'COLUMNS' => {'actor.actor_id' => 'actor_id'}}
    }
  }),
  'mysql'
)
```

Build and smoke-test it from the repository root:

```bash
(cd bindings/ruby && bash build.sh)
ruby bindings/ruby/smoke.rb
ruby examples/ruby/example.rb
```

The extension exposes `CarbonC.version`, `CarbonC.hello_world`,
`CarbonC.status_code`, `CarbonC.status_message`, `CarbonC.compile_query`, and
`CarbonC.normalize_allowlist_sql`.

## Next Milestones

1. Expand fixture coverage for derived joins, multi-row writes, PostgreSQL
   conflict targets, and schema-aware write normalization.
2. Expand schema validation to unqualified references, generated type metadata,
   and binding-friendly diagnostic paths.
3. Add package-level ergonomics for each binding without moving DB execution
   into C.
4. Add structured diagnostic paths for binding-friendly errors.
