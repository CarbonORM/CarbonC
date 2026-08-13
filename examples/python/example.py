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

get_request = Actor.Get(
    {
        carbon_codegen.C6C.SELECT: [Actor.ACTOR_ID, Actor.FIRST_NAME],
        carbon_codegen.C6C.WHERE: {
            Actor.ACTOR_ID: carbon_codegen.eq_lit(10),
        },
        carbon_codegen.C6C.PAGINATION: {carbon_codegen.C6C.LIMIT: 5},
    },
    schema=schema,
    dialect=carbon_codegen.CarbonDialect.MYSQL,
)

result = carbon_codegen.compile_query_result(
    get_request["query"],
    schema=get_request["schema"],
    dialect=get_request["dialect"],
)

if result["status"] != 0:
    raise SystemExit(result["error"])

print(result["sql"])
print(result["params"])
print(result["allowlist_key"])
print(result["diagnostics"])
print(carbon.schema_metadata(json.dumps(schema)))
print(carbon_codegen.schema_models(schema))
