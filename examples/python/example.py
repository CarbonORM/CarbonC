import json

import carbon


schema = {
    "TABLES": {
        "actor": {
            "COLUMNS": {
                "actor.actor_id": "actor_id",
                "actor.first_name": "first_name",
            }
        }
    }
}

query = {
    "FROM": "actor",
    "SELECT": ["actor.actor_id", "actor.first_name"],
    "WHERE": {"actor.actor_id": [">", 10]},
    "PAGINATION": {"LIMIT": 5},
}

result = carbon.compile_query(json.dumps(query), schema_json=json.dumps(schema), dialect="mysql")

if result["status"] != 0:
    raise SystemExit(result["error"])

print(result["sql"])
print(result["params_json"])
print(result["allowlist_key"])
print(carbon.schema_metadata(json.dumps(schema)))
