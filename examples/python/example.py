import json

import carbon
import carbon_codegen


schema = {
        "TABLES": {
            "actor": {
                "PRIMARY_SHORT": ["actor_id"],
                "COLUMNS": {
                    "actor.actor_id": "actor_id",
                    "actor.first_name": "first_name",
                },
                "TYPE_VALIDATION": {
                    "actor.actor_id": {
                        "COLUMN_NAME": "actor_id",
                        "MYSQL_TYPE": "smallint",
                        "MAX_LENGTH": "",
                        "AUTO_INCREMENT": True,
                        "NOT_NULL": True,
                        "SKIP_COLUMN_IN_POST": False,
                    },
                    "actor.first_name": {
                        "COLUMN_NAME": "first_name",
                        "MYSQL_TYPE": "varchar",
                        "MAX_LENGTH": "45",
                        "AUTO_INCREMENT": False,
                        "NOT_NULL": True,
                        "SKIP_COLUMN_IN_POST": False,
                    },
                },
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
print(result["diagnostics_json"])
print(carbon.schema_metadata(json.dumps(schema)))
print(carbon_codegen.schema_models(schema))
