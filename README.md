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

The compiler emits SQL, a JSON array of bound parameter values, and a normalized
allowlist key:

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
- CarbonNode-style allowlist normalization for whitespace, `LIMIT`, `OFFSET`,
  and `IN` bind-list cardinality
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

## Next Milestones

1. Expand fixture coverage for derived joins, multi-row writes, PostgreSQL
   conflict targets, and schema-aware write normalization.
2. Expand schema validation to unqualified references, generated type metadata,
   and binding-friendly diagnostic paths.
3. Add structured error codes and paths for binding-friendly diagnostics.
4. Wrap the kernel from Node N-API, PHP, Python, and Ruby without moving DB
   execution into C.
