import carbon
import carbon_codegen


SCHEMA_DUMP = """
CREATE TABLE `actor` (
  `actor_id` smallint unsigned NOT NULL AUTO_INCREMENT,
  `first_name` varchar(45) NOT NULL,
  PRIMARY KEY (`actor_id`)
);
"""

schema = carbon.schema_from_dump(SCHEMA_DUMP)

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
print(carbon.schema_metadata(schema))
print(carbon_codegen.schema_models(schema))
