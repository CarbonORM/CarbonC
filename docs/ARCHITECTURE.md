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
- native model classes and generated types
- exception mapping

The initial Python, PHP, Node, and Ruby bindings are intentionally thin: they
call the C compiler, return SQL, params JSON, allowlist key, numeric status,
stable status code, and error fields, and leave DB execution to the language
package layer.

This keeps the C ABI stable and keeps each language package idiomatic.

## Data Flow

```text
native binding object
  -> canonical JSON or MessagePack payload
  -> CarbonC compile/validate function
  -> SQL + params + allowlist key + status/status_code diagnostics
  -> native driver executes prepared statement
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

- `dialect`: `mysql`, `postgresql`, or `postgres`
- `FROM` or legacy `table`
- `SELECT` references, `AS`, `DISTINCT`, and function tuples
- `JOIN` clauses for `INNER`, `LEFT`, `LEFT_OUTER`, `RIGHT`, and
  `RIGHT_OUTER` table aliases and stringified derived targets
- `WHERE` column mappings, `AND` / `OR`, comparison operators, `IN`,
  `NOT_IN`, `BETWEEN`, `IS`, `IS_NOT`, `EXISTS`, `NOT_EXISTS`, `LIT`, and
  `PARAM`
- `GROUP_BY` expression lists and `HAVING` boolean clauses
- scalar `SUBSELECT` expressions in `SELECT` and `WHERE` operands
- explicit `INSERT`, `REPLACE`, `UPDATE`, and `DELETE` write payloads,
  including array-valued multi-row `INSERT`/MySQL `REPLACE` rows
- CarbonNode-compatible root-level POST row inserts when no read operation
  controls are present
- CarbonNode-compatible `dataInsertMultipleRows` insert payloads
- MySQL upsert update lists through `UPDATE: ["column_name"]`
- PostgreSQL `ON CONFLICT` upserts from `PRIMARY_SHORT` or `PRIMARY` schema
  metadata, including `UPDATE: []` / `DO NOTHING`
- PostgreSQL `UPDATE ... FROM` for `INNER` joined table aliases
- PostgreSQL `DELETE ... USING` for `INNER` joined table aliases
- `PAGINATION.ORDER`, `LIMIT`, and `PAGE`
- MySQL and PostgreSQL placeholder styles
- allowlist normalization for parenthesized bind groups, multi-row `VALUES`
  cardinality, `IN` bind-list cardinality, and `LIMIT` forms
- opt-in schema validation from `schema_json.TABLES`, `schema_json.tables`, or
  `schema_json.C6.TABLES`
- schema-declared write column ordering for `INSERT`, `UPDATE`, and upsert
  update lists

Derived JOIN targets stay ABI-neutral: bindings pass a JSON string key whose
decoded value is an object containing `SUBSELECT` and `AS`, and the associated
JOIN value remains the `ON` clause.

Unsupported query shapes return `CARBON_STATUS_UNSUPPORTED_QUERY` rather than
silently compiling weaker SQL.

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

## Direction

CarbonC now carries the first CarbonNode-derived golden fixtures under
`tests/fixtures/*.case`, plus native Python, PHP, Node, and Ruby smoke
wrappers. The next implementation step is to expand those fixtures to generated
type metadata, binding-friendly diagnostic paths, schema-aware write
normalization edge cases, and package-level ergonomics.
