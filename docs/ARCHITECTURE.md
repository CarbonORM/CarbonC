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

The initial Python binding is intentionally thin: it calls the C compiler,
returns SQL, params JSON, allowlist key, status, and error fields, and leaves DB
execution to the Python package layer.

This keeps the C ABI stable and keeps each language package idiomatic.

## Data Flow

```text
native binding object
  -> canonical JSON or MessagePack payload
  -> CarbonC compile/validate function
  -> SQL + params + allowlist key + diagnostics
  -> native driver executes prepared statement
```

The first implementation uses JSON because every target language can produce
and inspect it easily. MessagePack or a compact binary payload can be added
later without changing the higher-level contract.

## ABI Rules

- Functions return `carbon_status`.
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
  `RIGHT_OUTER` table aliases
- `WHERE` column mappings, `AND` / `OR`, comparison operators, `IN`,
  `NOT_IN`, `BETWEEN`, `IS`, `IS_NOT`, `EXISTS`, `NOT_EXISTS`, `LIT`, and
  `PARAM`
- `GROUP_BY` expression lists and `HAVING` boolean clauses
- scalar `SUBSELECT` expressions in `SELECT` and `WHERE` operands
- explicit `INSERT`, `REPLACE`, `UPDATE`, and `DELETE` write payloads
- MySQL upsert update lists through `UPDATE: ["column_name"]`
- `PAGINATION.ORDER`, `LIMIT`, and `PAGE`
- MySQL and PostgreSQL placeholder styles
- opt-in schema validation from `schema_json.TABLES`, `schema_json.tables`, or
  `schema_json.C6.TABLES`

Unsupported query shapes return `CARBON_STATUS_UNSUPPORTED_QUERY` rather than
silently compiling weaker SQL.

PostgreSQL write support covers simple insert/update/delete forms in this
slice. Joined writes and schema-derived conflict targets remain outside the
v0.1 compiler boundary.

When `TABLES` metadata is present, the compiler validates `FROM` tables, joined
tables, dotted column references, join-alias references, and insert/update/upsert
write columns against C6 `COLUMNS` data. Empty or absent schema metadata keeps
the previous syntax-only behavior so language bindings can adopt the validator
incrementally.

## Direction

CarbonC now carries the first CarbonNode-derived golden fixtures under
`tests/fixtures/*.case`, plus a native Python smoke wrapper. The next
implementation step is to expand those fixtures to derived joins, multi-row
writes, PostgreSQL conflict targets, unqualified-reference validation, and
generated type metadata, then carry the same compile-result wrapper shape to
Node, PHP, and Ruby.
