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

query = {
    carbon_codegen.C6C.FROM: Actor.TABLE,
    carbon_codegen.C6C.SELECT: [Actor.ACTOR_ID, Actor.FIRST_NAME],
    carbon_codegen.C6C.WHERE: {
        Actor.ACTOR_ID: carbon_codegen.op(carbon_codegen.C6C.GREATER_THAN, 10),
    },
    carbon_codegen.C6C.PAGINATION: {carbon_codegen.C6C.LIMIT: 5},
}

result = carbon_codegen.compile_query_result(
    query,
    schema=schema,
    dialect=carbon_codegen.CarbonDialect.MYSQL,
)

if result["status"] != 0:
    raise SystemExit(result["error"])

print(result["sql"])
print(result["params"])
print(result["allowlist_key"])
print(result["diagnostics"])
print(carbon.schema_metadata(json.dumps(schema)))
print(carbon_codegen.schema_models(schema))
