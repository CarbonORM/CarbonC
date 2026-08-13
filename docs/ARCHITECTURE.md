# CarbonC Architecture

CarbonC should be a portable kernel, not a language-level ORM replacement. The
purpose of the C layer is to make CarbonORM's deterministic behavior identical
across supported languages.

## Boundary

CarbonC owns pure transformations:

- schema metadata normalization
- query payload validation
- SQL compilation
- parameter list extraction
- allowlist key normalization
- schema diff calculations

Bindings own runtime integration:

- connection pools
- transactions
- async/event-loop behavior
- framework adapters
- native payload serialization helpers
- language-native C6 token constants
- language-native dialect constants
- query-builder facades
- native model classes and generated types
- exception mapping

The initial Python, PHP, Node, and Ruby bindings keep the native extensions
thin: they call the C compiler, return SQL, params JSON, allowlist key, numeric
status, stable status code, error fields, and normalized schema metadata. The
package layer around those extensions owns native dict/array/object/hash
serialization helpers, typed result adapters that decode params/diagnostics JSON,
query-builder facades, CarbonNode-compatible `C6C` token constants, C
`CARBON_C6_*` macros, `CarbonDialect` / `Dialect` constants, C
`CARBON_DIALECT_*` macros, and typed source generators for Python dataclasses,
TypeScript interfaces, PHP model classes, and Ruby Struct models. Generated
model sources expose table, field-name, and qualified-column constants so query
authors do not hand-type schema identifiers. DB execution remains outside
CarbonC.

This keeps the C ABI stable and keeps each language package idiomatic.

## Data Flow

```text
native binding object
  -> canonical JSON or MessagePack payload
  -> CarbonC compile/validate function
  -> SQL + params + allowlist key + status/status_code + diagnostics JSON
  -> native driver executes prepared statement

schema JSON -> CarbonC schema metadata normalizer -> native type generator
```

The first implementation uses JSON because every target language can produce
and inspect it easily. MessagePack or a compact binary payload can be added
later without changing the higher-level contract.

## ABI Rules

- Functions return `carbon_status`.
- `carbon_status_code()` maps each status to a stable machine-readable string.
- Output strings use `carbon_buffer`.
- The caller owns returned buffers.
- CarbonC exposes explicit free functions.
- Context state is opaque.
- New behavior should be added without changing existing struct layout until a
  major ABI version is declared.

## v0.1 Query Shape

The initial compiler supports:

- schema metadata normalization into deterministic JSON for generated binding
  types
- schema metadata enrichment from CarbonNode-style `TYPE_VALIDATION` entries
  keyed by qualified column name
- package-level typed source generators for Python dataclasses, TypeScript
  interfaces, PHP model classes, and Ruby Struct models, including generated
  table and column constants
- package-level compile helpers for native Python dicts, PHP arrays, JavaScript
  objects, and Ruby hashes
- package-level C6 token constants exposed idiomatically as `C6C` / `C6`
- package-level dialect constants exposed idiomatically as `CarbonDialect` /
  `Dialect`
- package-level result adapters that retain the raw JSON fields while adding
  decoded native `params` and `diagnostics` values
- package-level query-builder facades that emit canonical payloads for
  table/from, select, where, boolean predicate groups, join, group/having,
  writes, subselects, limit/page, and order controls
- `dialect`: `mysql`, `postgresql`, or `postgres`
- `FROM` or legacy `table`
- `SELECT` references, wrapper-form `AS` / `DISTINCT`, direct tuples for
  CarbonNode known functions, and canonical `CALL` custom-function tuples
- `JOIN` clauses for `INNER`, `LEFT`, `LEFT_OUTER`, `RIGHT`, and
  `RIGHT_OUTER` table aliases and stringified derived targets
- `WHERE` column mappings, `AND` / `OR`, comparison operators, `IN`,
  `NOT_IN`, `BETWEEN`, `IS`, `IS_NOT`, `EXISTS`, `NOT_EXISTS`,
  `MATCH_AGAINST`, `LIT`, `PARAM`, and boolean spatial-function predicates
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
- MySQL `INDEX_HINTS` on base and joined tables, with PostgreSQL no-op emission
- `PAGINATION.ORDER`, compatible top-level `ORDER`, `LIMIT`, and `PAGE`
- MySQL and PostgreSQL placeholder styles
- allowlist normalization for parenthesized bind groups, multi-row `VALUES`
  cardinality, `IN` bind-list cardinality, and `LIMIT` forms
- opt-in schema validation from `schema_json.TABLES`, `schema_json.tables`, or
  `schema_json.C6.TABLES`
- schema-declared write column ordering for `INSERT`, `UPDATE`, and upsert
  update lists

Direct expression tuples are intentionally restricted to CarbonNode's known
SQL function list. Custom/unknown functions go through `CALL`, while legacy
positional aliases such as `[function, arg, "AS", alias]` are rejected in
favor of `["AS", expression, alias]`.

Top-level `ORDER` is accepted for CarbonNode compatibility and normalized into
the same compiler path as `PAGINATION.ORDER` when no pagination order is
declared. Legacy object-map `ORDER` payloads and duplicate top-level plus
pagination order declarations are invalid.

Derived JOIN targets stay ABI-neutral: bindings pass a JSON string key whose
decoded value is an object containing `SUBSELECT` and `AS`, and the associated
JOIN value remains the `ON` clause.

`INDEX_HINTS` supports CarbonNode's `FORCE INDEX`, `USE INDEX`, and
`IGNORE INDEX` hint specs. Target-keyed maps are resolved by alias, `table alias`,
table, and `__base__` precedence before emitting MySQL syntax.

Unsupported query shapes return `CARBON_STATUS_UNSUPPORTED_QUERY` rather than
silently compiling weaker SQL.

`carbon_schema_metadata()` returns a stable `{"tables":[...]}` JSON document
with ordered table names, ordered `columns` entries (`name` plus `qualified`),
and `primary` short-column names. Bindings can parse that shape to generate
language-native models without duplicating C6 schema interpretation. Those
generators emit native table, field-name, and qualified-column constants
(`Actor.FIELD_ACTOR_ID`, `Actor::ACTOR_ID`, `ActorFields.actor_id`,
`ActorColumns.actor_id`, etc.) and metadata maps that point back to the same
values. When a table includes CarbonNode-style
`TYPE_VALIDATION`, column entries also include optional `db_type`, `max_length`,
`nullable`, `auto_increment`, and `skip_insert` fields. Object-valued `COLUMNS`
entries may also carry those fields directly for schema sources that do not
separate type validation.

PostgreSQL write support covers simple insert/update/delete forms, multi-row
`INSERT ... RETURNING *`, and schema-derived `ON CONFLICT` targets in this
slice. PostgreSQL `INNER` joined updates compile to `UPDATE ... FROM`, and
`INNER` joined deletes compile to `DELETE ... USING`; non-`INNER` joined writes
and PostgreSQL derived joined writes remain outside the v0.1 compiler boundary.

Root-level POST row normalization is intentionally narrow because the C ABI does
not carry the HTTP method. A payload with `FROM`/`table`, no explicit write
payload, no read controls, and at least one non-metadata root key compiles as an
insert row. `UPDATE: [...]` is treated as upsert metadata for that row, while
`UPDATE: {...}` remains an update statement.

When `TABLES` metadata is present, the compiler validates `FROM` tables, joined
tables, unqualified current-table references, dotted column references,
join-alias references, insert/update/upsert write columns against C6 `COLUMNS`
data, and PostgreSQL upsert conflict targets from `PRIMARY_SHORT` or `PRIMARY`.
Schema `COLUMNS` declaration order is used to normalize write SQL and parameter
order for `INSERT`, `UPDATE`, and upsert update lists; later insert rows may use
qualified or short keys for the same normalized column. Derived aliases are
validated as JOIN-visible qualifiers while their projected columns stay owned by
the nested `SUBSELECT`. Empty or absent schema metadata keeps the previous
syntax-only behavior so language bindings can adopt the validator incrementally.
Compile failures can be projected through `carbon_compile_result_diagnostics_json()`
as deterministic JSON with severity, machine code, source, and JSON-style path
fields so bindings can display actionable errors without depending on C struct
layout changes.

## Direction

CarbonC now carries the first CarbonNode-derived golden fixtures under
`tests/fixtures/*.case`, plus native Python, PHP, Node, and Ruby smoke
wrappers, package-level native payload helpers, typed result adapters,
read/write/subselect/predicate query-builder facades, boolean-group compiler
wrapping, C6 token constants, dialect constants, typed source generators with
generated field and column constants, model-aware query scaffolds, full-text
`MATCH_AGAINST` predicates, boolean spatial-function predicates, canonical
custom-call expressions, expression-valued writes, MySQL index hints, and binding-friendly
diagnostic JSON. The next
implementation step is to cover additional C6 grammar and schema-aware write
normalization edge cases while importing higher-level package examples from
production C6 query shapes.
