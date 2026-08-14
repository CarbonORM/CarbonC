# CarbonC

CarbonC is the portable CarbonORM kernel. It keeps the deterministic parts of
C6 behind a stable C ABI so PHP, Node, Python, Ruby, and other language packages
can share one compiler and schema interpretation while still presenting native
runtime APIs.

The C layer owns pure transformations:

- SQL dump schema extraction
- schema metadata normalization
- model source generation
- query payload validation
- SQL compilation
- bind-parameter extraction
- allowlist key normalization

Language bindings own runtime integration:

- native package APIs and generated model loading
- connection pools, transactions, and async behavior
- exception mapping and framework integration
- serialization helpers, typed result adapters, and query-builder facades

CarbonC does not execute database queries. It turns a canonical C6 payload into
SQL, parameters, diagnostics, and an allowlist key for the calling runtime to
execute through its own database layer.

For the detailed compiler boundary and supported grammar, see
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). Golden compiler fixtures live in
[tests/fixtures](tests/fixtures).

## Core Workflow

```text
SQL dump
  -> carbon_schema_from_dump()
  -> C6 TABLES schema JSON
  -> carbon_schema_metadata()
  -> carbon_schema_model_source()
  -> generated constants and model helpers

native query object/array/hash
  -> binding serializes canonical JSON
  -> carbon_compile_query()
  -> SQL + params JSON + allowlist key + status + diagnostics JSON
  -> native runtime executes prepared statement
```

The package-level contract is a complete native query payload keyed by generated
constants. Fluent builders are convenience APIs that emit the same payload
shape.

## Query Payload Contract

CarbonC's stable boundary is JSON. Bindings can expose native objects, arrays,
dicts, or hashes, but they serialize to the same C6 shape:

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

For MySQL, that compiles to:

```sql
SELECT actor.actor_id, actor.first_name FROM `actor` WHERE (actor.actor_id) = ? ORDER BY actor.last_name ASC LIMIT 25
```

With bound params:

```json
[10]
```

And an allowlist key normalized for stable policy checks:

```text
SELECT actor.actor_id, actor.first_name FROM `actor` WHERE (actor.actor_id) = ? ORDER BY actor.last_name ASC LIMIT ?
```

Normal application code should avoid hand-typed grammar strings and schema
identifiers. Bindings expose C6 token constants (`C6C` / `C6`), dialect
constants (`CarbonDialect` / `Dialect`), generated table/field/column
constants, and literal helpers such as `eqLit()`, `eq_lit()`, and
`carbon_eq_lit()`.

## Current Scope

The v0.1 kernel includes:

- deterministic schema metadata from C6 `TABLES` JSON
- SQL dump extraction into the same `TABLES` schema shape
- model source generation for Python dataclasses, TypeScript interfaces, PHP
  model classes, and Ruby Struct models
- typed source enrichment from DB type, length, nullability, primary key, and
  insert-skip metadata
- MySQL and PostgreSQL SELECT compilation with joins, aliases, derived joins,
  subselects, grouping, having, ordering, pagination, index hints, and common
  expression/predicate forms
- INSERT, REPLACE, UPDATE, DELETE, multi-row insert, CarbonNode-style root POST
  rows, MySQL upsert lists, and PostgreSQL `ON CONFLICT` upserts
- PostgreSQL `UPDATE ... FROM` and `DELETE ... USING` for `INNER` joined writes
- schema-aware validation for tables, columns, join aliases, write columns, and
  schema-ordered write parameter emission
- CarbonNode-compatible allowlist normalization for bind groups, `IN` lists,
  multi-row `VALUES`, and `LIMIT` forms
- binding-friendly diagnostics with stable status codes and JSON-style paths

The kernel intentionally rejects unsupported or ambiguous shapes instead of
silently compiling weaker SQL. Direct expression tuples are limited to
CarbonNode's known SQL function list; custom SQL functions must use the
canonical `["CALL", "FUNCTION_NAME", ...args]` form.

## Public C API

The public header is [include/carbon.h](include/carbon.h).

Primary entrypoints:

- `carbon_version()`
- `carbon_status_code()` / `carbon_status_message()`
- `carbon_context_new()` / `carbon_context_free()`
- `carbon_buffer_init()` / `carbon_buffer_free()`
- `carbon_compile_query()`
- `carbon_compile_result_diagnostics_json()`
- `carbon_compile_result_free()`
- `carbon_schema_from_dump()`
- `carbon_schema_metadata()`
- `carbon_schema_model_source()`
- `carbon_normalize_allowlist_sql()`

All buffers returned by CarbonC are owned by the caller. Release them with
`carbon_buffer_free()` or `carbon_compile_result_free()`.

Compile diagnostics are projected as deterministic JSON so bindings can expose
actionable failures without depending on C struct layout:

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

## Build And Test

Build the C library and test binary with CMake:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Bindings

Each binding wraps the same C ABI and exposes native helpers around the shared
payload contract.

### Python

```bash
(cd bindings/python && python3 setup.py build_ext --inplace)
PYTHONPATH=bindings/python python3 bindings/python/smoke.py
PYTHONPATH=bindings/python python3 examples/python/example.py
```

Python exposes `carbon.schema_from_dump()`, `carbon.schema_metadata()`,
`carbon.schema_model_source()`, raw compile functions, and
`carbon_codegen` helpers for native payloads, typed result adapters, generated
models, predicates, dialect constants, model request envelopes, write helpers,
joins, and query-builder facades.

### PHP

```bash
(cd bindings/php && bash build.sh)
php -d extension=bindings/php/modules/carbon.so bindings/php/smoke.php
php -d extension=bindings/php/modules/carbon.so examples/php/index.php
```

PHP exposes raw extension functions plus `bindings/php/carbon_codegen.php` for
`C6C`, `CarbonDialect`, native array compilation, typed result adapters,
generated model classes, model request envelopes, joins, write helpers, and the
optional `CarbonQuery` builder.

### Node

```bash
(cd bindings/node && bash build.sh)
node bindings/node/smoke.js
node examples/node/index.js
```

Node uses plain N-API and exports camelCase methods plus snake_case aliases.
`bindings/node/index.js` adds native object compilation, typed result adapters,
generated TypeScript source, `C6C`, `CarbonDialect`, model APIs, joins, write
helpers, and the optional `CarbonQuery` builder.

### Ruby

```bash
(cd bindings/ruby && bash build.sh)
ruby bindings/ruby/smoke.rb
ruby examples/ruby/example.rb
```

Ruby exposes `CarbonC` extension methods and `bindings/ruby/carbon_codegen.rb`
helpers for native hash compilation, typed result adapters, generated Struct
models, `CarbonC::C6C`, `CarbonC::Dialect`, model request envelopes, joins,
write helpers, and the optional `CarbonC::Query` builder.

## Shared Binding Pattern

All bindings follow the same shape:

1. Extract or provide C6 schema JSON.
2. Generate model metadata or source from that schema.
3. Build query payloads with generated constants and literal helpers.
4. Compile the payload through CarbonC.
5. Execute the returned SQL and params in the host runtime.

Example JavaScript payload:

```js
const Actor = carbon.modelApi(ActorMeta);

const request = Actor.Get({
  [carbon.C6C.SELECT]: [Actor.COLUMNS.actor_id],
  [carbon.C6C.WHERE]: {
    [Actor.COLUMNS.actor_id]: carbon.eqLit(10),
  },
  [carbon.C6C.PAGINATION]: {[carbon.C6C.LIMIT]: 500},
  cacheResults: false,
}, {
  schema,
  dialect: carbon.CarbonDialect.MYSQL,
});

const result = carbon.compileQueryResult(
  request.query,
  request.schema,
  request.dialect,
);
```

Model `Get` helpers return serializable request envelopes. They do not choose a
database, route traffic, apply device/offload policy, or execute the query.

## Project Layout

```text
include/carbon.h          public C ABI
src/carbon.c              kernel implementation
tests/test_carbon.c       C test runner
tests/fixtures/*.case     golden compiler fixtures
bindings/python           Python extension and helpers
bindings/php              PHP extension and helpers
bindings/node             Node N-API addon and helpers
bindings/ruby             Ruby extension and helpers
examples                  binding examples
docs/ARCHITECTURE.md      detailed boundary and grammar notes
```

## Direction

Next work should continue behind stable fixtures:

1. Expand the remaining CarbonNode C6 grammar.
2. Add richer batched diagnostics.
3. Add higher-level package examples from real production C6 query shapes.
