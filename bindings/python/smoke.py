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
    metadata = json.loads(carbon.schema_metadata(json.dumps(schema)))
    assert metadata == {
        "tables": [
            {
                "name": "actor",
                "columns": [
                    {"name": "actor_id", "qualified": "actor.actor_id"},
                    {"name": "first_name", "qualified": "actor.first_name"},
                ],
                "primary": ["actor_id"],
            }
        ]
    }, metadata
    models = carbon_codegen.schema_models(schema)
    assert "class Actor:" in models, models
    assert "__carbon_primary__ = ('actor_id',)" in models, models
    assert "'actor_id': 'actor.actor_id'" in models, models
    assert "actor_id: Any = None" in models, models

    rejected = carbon.compile_query(
        json.dumps({"FROM": "actor", "SELECT": ["actor.last_name"]}),
        schema_json=json.dumps(schema),
        dialect="mysql",
    )
    assert rejected["status"] == 3, rejected
    assert rejected["status_code"] == "invalid_query", rejected

    assert carbon.status_code(3) == "invalid_query"
    assert carbon.normalize_allowlist_sql("SELECT * FROM `actor` LIMIT 10") == "SELECT * FROM `actor` LIMIT ?"
    print("python binding smoke: ok")


if __name__ == "__main__":
    main()
