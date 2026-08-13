import json

import carbon
import carbon_codegen


def main() -> None:
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
    assert result["status"] == 0, result
    assert result["status_code"] == "ok", result
    assert result["sql"] == (
        "SELECT actor.actor_id, actor.first_name FROM `actor` "
        "WHERE (actor.actor_id) > ? LIMIT 5"
    )
    assert result["params_json"] == "[10]"
    assert result["allowlist_key"] == (
        "SELECT actor.actor_id, actor.first_name FROM `actor` "
        "WHERE (actor.actor_id) > ? LIMIT ?"
    )
    assert json.loads(result["diagnostics_json"]) == {
        "status": 0,
        "status_code": "ok",
        "ok": True,
        "diagnostics": [],
    }
    ergonomic = carbon_codegen.compile_query_value(query, schema=schema, dialect="mysql")
    assert ergonomic == result, ergonomic
    ergonomic_alias = carbon_codegen.compile_query(query, schema=schema, dialect="mysql")
    assert ergonomic_alias == result, ergonomic_alias
    adapted = carbon_codegen.adapt_compile_result(result)
    assert adapted == {
        **result,
        "params": [10],
        "diagnostics": json.loads(result["diagnostics_json"]),
    }, adapted
    typed = carbon_codegen.compile_query_result(query, schema=schema, dialect="mysql")
    assert typed == adapted, typed
    built = (
        carbon_codegen.query("actor")
        .select("actor.actor_id", "actor.first_name")
        .where({"actor.actor_id": [">", 10]})
        .limit(5)
    )
    assert built.to_payload() == query, built.to_payload()
    assert built.compile(schema=schema, dialect="mysql") == adapted
    ordered_payload = (
        carbon_codegen.from_table("actor")
        .select(["actor.actor_id"])
        .order_by("actor.first_name", "DESC")
        .limit(5)
        .to_payload()
    )
    assert ordered_payload == {
        "FROM": "actor",
        "SELECT": ["actor.actor_id"],
        "PAGINATION": {"ORDER": [["actor.first_name", "DESC"]], "LIMIT": 5},
    }, ordered_payload
    metadata = json.loads(carbon.schema_metadata(json.dumps(schema)))
    assert metadata == {
        "tables": [
            {
                "name": "actor",
                "columns": [
                    {
                        "name": "actor_id",
                        "qualified": "actor.actor_id",
                        "db_type": "smallint",
                        "max_length": "",
                        "nullable": False,
                        "auto_increment": True,
                        "skip_insert": False,
                    },
                    {
                        "name": "first_name",
                        "qualified": "actor.first_name",
                        "db_type": "varchar",
                        "max_length": "45",
                        "nullable": False,
                        "auto_increment": False,
                        "skip_insert": False,
                    },
                ],
                "primary": ["actor_id"],
            }
        ]
    }, metadata
    models = carbon_codegen.schema_models(schema)
    assert "class Actor:" in models, models
    assert "__carbon_primary__ = ('actor_id',)" in models, models
    assert "'actor_id': 'actor.actor_id'" in models, models
    assert "__carbon_db_types__ = {" in models, models
    assert "'actor_id': 'smallint'" in models, models
    assert "__carbon_nullable__ = {" in models, models
    assert "'actor_id': False" in models, models
    assert "actor_id: int = None" in models, models
    assert "first_name: str = None" in models, models

    rejected = carbon.compile_query(
        json.dumps({"FROM": "actor", "SELECT": ["actor.last_name"]}),
        schema_json=json.dumps(schema),
        dialect="mysql",
    )
    assert rejected["status"] == 3, rejected
    assert rejected["status_code"] == "invalid_query", rejected

    rejected_table = carbon.compile_query(
        json.dumps({"FROM": "film", "SELECT": ["film.film_id"]}),
        schema_json=json.dumps(schema),
        dialect="mysql",
    )
    assert rejected_table["status"] == 3, rejected_table
    assert rejected_table["status_code"] == "invalid_query", rejected_table
    assert rejected_table["error"] == "table is not present in schema", rejected_table
    assert json.loads(rejected_table["diagnostics_json"]) == {
        "status": 3,
        "status_code": "invalid_query",
        "ok": False,
        "diagnostics": [
            {
                "severity": "error",
                "code": "invalid_query",
                "message": "table is not present in schema",
                "source": "schema",
                "path": "$.FROM",
            }
        ],
    }

    assert carbon.status_code(3) == "invalid_query"
    assert carbon.normalize_allowlist_sql("SELECT * FROM `actor` LIMIT 10") == "SELECT * FROM `actor` LIMIT ?"
    print("python binding smoke: ok")


if __name__ == "__main__":
    main()
