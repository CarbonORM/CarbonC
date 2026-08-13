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

generated_models: dict[str, object] = {}
exec(carbon_codegen.schema_models(schema), generated_models)
Actor = generated_models["Actor"]

result = (
    carbon_codegen.query(Actor.TABLE)
    .select(Actor.ACTOR_ID, Actor.FIRST_NAME)
    .where_op(Actor.ACTOR_ID, carbon_codegen.C6C.GREATER_THAN, 10)
    .limit(5)
    .compile(schema=schema, dialect="mysql")
)

if result["status"] != 0:
    raise SystemExit(result["error"])

print(result["sql"])
print(result["params"])
print(result["allowlist_key"])
print(result["diagnostics"])
print(carbon.schema_metadata(json.dumps(schema)))
print(carbon_codegen.schema_models(schema))
