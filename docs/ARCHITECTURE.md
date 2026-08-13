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
  `NOT_IN`, `BETWEEN`, `IS`, `IS_NOT`, `LIT`, and `PARAM`
- `GROUP_BY` expression lists and `HAVING` boolean clauses
- scalar `SUBSELECT` expressions in `SELECT` and `WHERE` operands
- `PAGINATION.ORDER`, `LIMIT`, and `PAGE`
- MySQL and PostgreSQL placeholder styles

Unsupported query shapes return `CARBON_STATUS_UNSUPPORTED_QUERY` rather than
silently compiling weaker SQL.

## Direction

CarbonC now carries the first CarbonNode-derived golden fixtures under
`tests/fixtures/*.case`. The next implementation step is to expand those
fixtures to derived joins, write builders, and schema-aware identifier
validation. That should happen before expanding language bindings beyond smoke
wrappers.
