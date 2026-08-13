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
buffer ownership, and a first query compiler slice for a narrow JSON shape:

```json
{
  "dialect": "mysql",
  "table": "carbon_users",
  "select": ["user_id", "user_email"],
  "where": {
    "user_id": 42
  },
  "limit": 1
}
```

The compiler emits SQL, a JSON array of bound parameter values, and a normalized
allowlist key:

```sql
SELECT `user_id`, `user_email` FROM `carbon_users` WHERE `user_id` = ? LIMIT ?
```

```json
[42,1]
```

```text
select user_id, user_email from carbon_users where user_id = ? limit ?
```

This is not the full C6 grammar yet. It is the foundation for porting the
canonical CarbonNode query grammar into C behind stable tests.

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

1. Port CarbonNode's canonical expression grammar into golden fixtures.
2. Add schema metadata checks so identifiers are validated against generated C6
   schema data, not only identifier syntax.
3. Add structured error codes and paths for binding-friendly diagnostics.
4. Wrap the kernel from Node N-API, PHP, Python, and Ruby without moving DB
   execution into C.
