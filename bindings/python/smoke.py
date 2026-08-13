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
    top_ordered = carbon_codegen.compile_query_value(
        {
            "FROM": "actor",
            "SELECT": ["actor.actor_id", "actor.first_name"],
            "WHERE": {"actor.actor_id": [">", 10]},
            "ORDER": [["actor.first_name", "ASC"]],
            "PAGINATION": {"LIMIT": 25},
        },
        schema=schema,
        dialect="mysql",
    )
    assert top_ordered["sql"] == (
        "SELECT actor.actor_id, actor.first_name FROM `actor` "
        "WHERE (actor.actor_id) > ? ORDER BY actor.first_name ASC LIMIT 25"
    ), top_ordered
    assert top_ordered["allowlist_key"] == (
        "SELECT actor.actor_id, actor.first_name FROM `actor` "
        "WHERE (actor.actor_id) > ? ORDER BY actor.first_name ASC LIMIT ?"
    ), top_ordered
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
    join_payload = (
        carbon_codegen.from_table("actor")
        .select("actor.actor_id")
        .join("INNER", "film_actor fa", {"fa.actor_id": ["=", "actor.actor_id"]})
        .to_payload()
    )
    assert join_payload == {
        "FROM": "actor",
        "SELECT": ["actor.actor_id"],
        "JOIN": {"INNER": {"film_actor fa": {"fa.actor_id": ["=", "actor.actor_id"]}}},
    }, join_payload
    recent_actor_ids = (
        carbon_codegen.query("film_actor")
        .select("film_actor.actor_id")
        .where({"film_actor.film_id": [">", 10]})
        .limit(1)
    )
    assert carbon_codegen.subselect(recent_actor_ids) == [
        "SUBSELECT",
        {
            "FROM": "film_actor",
            "SELECT": ["film_actor.actor_id"],
            "WHERE": {"film_actor.film_id": [">", 10]},
            "PAGINATION": {"LIMIT": 1},
        },
    ]
    derived = (
        carbon_codegen.query("actor")
        .select("actor.actor_id", "fa_recent.actor_id")
        .join_subselect("INNER", "fa_recent", recent_actor_ids, {"fa_recent.actor_id": ["=", "actor.actor_id"]})
        .where({"actor.actor_id": [">", 100]})
        .compile(dialect="mysql")
    )
    assert derived["status"] == 0, derived
    assert derived["sql"] == (
        "SELECT actor.actor_id, fa_recent.actor_id FROM `actor` "
        "INNER JOIN (SELECT film_actor.actor_id FROM `film_actor` WHERE (film_actor.film_id) > ? LIMIT 1) "
        "AS `fa_recent` ON ((fa_recent.actor_id) = actor.actor_id) "
        "WHERE (actor.actor_id) > ? LIMIT 100"
    ), derived
    assert derived["params"] == [10, 100], derived
    sales_query = (
        carbon_codegen.query("parcel_sales")
        .select("parcel_sales.parcel_id")
        .where_op("parcel_sales.sale_price", ">", 5000)
    )
    assert carbon_codegen.as_(carbon_codegen.call("COUNT", "parcel_sales.parcel_id"), "sale_count") == [
        "AS",
        ["COUNT", "parcel_sales.parcel_id"],
        "sale_count",
    ]
    assert carbon_codegen.fn("CONCAT", carbon_codegen.lit("A"), carbon_codegen.lit("B")) == [
        "CONCAT",
        ["LIT", "A"],
        ["LIT", "B"],
    ]
    assert carbon_codegen.custom_call("COALESCE", carbon_codegen.lit("UNKNOWN"), "actor.last_name") == [
        "CALL",
        "COALESCE",
        ["LIT", "UNKNOWN"],
        "actor.last_name",
    ]
    custom_selected = (
        carbon_codegen.query("actor")
        .select([carbon_codegen.as_(carbon_codegen.custom_call("COALESCE", carbon_codegen.lit("UNKNOWN"), "actor.first_name"), "display_name")])
        .limit(1)
        .compile(dialect="mysql")
    )
    assert custom_selected["sql"] == "SELECT COALESCE(?, actor.first_name) AS display_name FROM `actor` LIMIT 1", custom_selected
    assert custom_selected["params"] == ["UNKNOWN"], custom_selected
    spatial_polygon = "POLYGON((39.5185659 -105.0142915,39.5401859 -105.0142915,39.5401859 -104.9862115,39.5185659 -104.9862115,39.5185659 -105.0142915))"
    spatial_inner_polygon = "POLYGON((0 0,1 0,1 1,0 1,0 0))"
    assert carbon_codegen.mbr_contains("property_units.envelope", "property_units.location") == [
        "MBRContains",
        "property_units.envelope",
        "property_units.location",
    ]
    spatial_filtered = (
        carbon_codegen.query("property_units")
        .select("property_units.unit_id")
        .where(
            {
                "MBRContains": [
                    carbon_codegen.fn("ST_GeomFromText", carbon_codegen.lit(spatial_polygon), 4326),
                    "property_units.location",
                ],
                "OR": [
                    carbon_codegen.st_within(
                        "property_units.location",
                        carbon_codegen.fn("ST_GeomFromText", carbon_codegen.lit(spatial_inner_polygon), 4326),
                    ),
                    carbon_codegen.st_contains("property_units.envelope", "property_units.location"),
                ],
            }
        )
        .limit(10)
        .compile(dialect="mysql")
    )
    assert spatial_filtered["sql"] == (
        "SELECT property_units.unit_id FROM `property_units` "
        "WHERE MBRCONTAINS(ST_GEOMFROMTEXT(?, 4326), property_units.location) "
        "AND (ST_WITHIN(property_units.location, ST_GEOMFROMTEXT(?, 4326)) "
        "OR ST_CONTAINS(property_units.envelope, property_units.location)) LIMIT 10"
    ), spatial_filtered
    assert spatial_filtered["params"] == [spatial_polygon, spatial_inner_polygon], spatial_filtered
    assert carbon_codegen.lit("2023-01-01") == ["LIT", "2023-01-01"]
    assert carbon_codegen.exists_spec("property_units.parcel_id", sales_query) == [
        "property_units.parcel_id",
        [
            "SUBSELECT",
            {
                "FROM": "parcel_sales",
                "SELECT": ["parcel_sales.parcel_id"],
                "WHERE": {"parcel_sales.sale_price": [">", 5000]},
            },
        ],
    ]
    advanced = (
        carbon_codegen.query("property_units")
        .select("property_units.unit_id")
        .where_between("property_units.unit_id", 1, 10)
        .where_in("property_units.parcel_id", sales_query)
        .where_not_in("property_units.account_id", [99, 100])
        .where_exists("property_units.parcel_id", sales_query)
        .limit(3)
        .compile(dialect="mysql")
    )
    assert advanced["status"] == 0, advanced
    assert advanced["sql"] == (
        "SELECT property_units.unit_id FROM `property_units` "
        "WHERE (property_units.unit_id) BETWEEN ? AND ? "
        "AND ( property_units.parcel_id IN (SELECT parcel_sales.parcel_id FROM `parcel_sales` "
        "WHERE (parcel_sales.sale_price) > ?) ) "
        "AND ( property_units.account_id NOT IN (?, ?) ) "
        "AND EXISTS (SELECT parcel_sales.parcel_id FROM `parcel_sales` "
        "WHERE (parcel_sales.sale_price) > ? AND (parcel_sales.parcel_id) = property_units.parcel_id) "
        "LIMIT 3"
    ), advanced
    assert advanced["params"] == [1, 10, 5000, 99, 100, 5000], advanced
    assert carbon_codegen.and_(
        carbon_codegen.condition("actor.actor_id", carbon_codegen.op(">", 2)),
        carbon_codegen.or_(
            carbon_codegen.condition("actor.first_name", carbon_codegen.op("LIKE", carbon_codegen.lit("A%"))),
            carbon_codegen.condition("actor.first_name", carbon_codegen.op("LIKE", carbon_codegen.lit("B%"))),
        ),
    ) == {
        "AND": [
            {"actor.actor_id": [">", 2]},
            {
                "OR": [
                    {"actor.first_name": ["LIKE", ["LIT", "A%"]]},
                    {"actor.first_name": ["LIKE", ["LIT", "B%"]]},
                ]
            },
        ]
    }
    assert carbon_codegen.match_against("alpha beta", "BOOLEAN") == [
        "MATCH_AGAINST",
        [["LIT", "alpha beta"], "BOOLEAN"],
    ]
    fulltext = (
        carbon_codegen.query("actor")
        .select("actor.actor_id")
        .where_match_against("actor.first_name", "alpha beta", "BOOLEAN")
        .limit(10)
        .compile(schema=schema, dialect="mysql")
    )
    assert fulltext["status"] == 0, fulltext
    assert fulltext["sql"] == (
        "SELECT actor.actor_id FROM `actor` "
        "WHERE (MATCH(actor.first_name) AGAINST(? IN BOOLEAN MODE)) LIMIT 10"
    ), fulltext
    assert fulltext["params"] == ["alpha beta"], fulltext
    boolean_grouped = (
        carbon_codegen.query("actor")
        .select("actor.actor_id")
        .where_between("actor.actor_id", 1, 10)
        .where_or(
            carbon_codegen.condition("actor.first_name", carbon_codegen.op("LIKE", carbon_codegen.lit("A%"))),
            carbon_codegen.condition("actor.first_name", carbon_codegen.op("LIKE", carbon_codegen.lit("B%"))),
        )
        .where_and(
            carbon_codegen.condition("actor.actor_id", carbon_codegen.op(">", 2)),
            carbon_codegen.condition("actor.actor_id", carbon_codegen.op("<", 9)),
        )
        .limit(5)
        .compile(dialect="mysql")
    )
    assert boolean_grouped["status"] == 0, boolean_grouped
    assert boolean_grouped["sql"] == (
        "SELECT actor.actor_id FROM `actor` "
        "WHERE (actor.actor_id) BETWEEN ? AND ? "
        "AND ((actor.first_name) LIKE ? OR (actor.first_name) LIKE ?) "
        "AND ((actor.actor_id) > ? AND (actor.actor_id) < ?) LIMIT 5"
    ), boolean_grouped
    assert boolean_grouped["params"] == [1, 10, "A%", "B%", 2, 9], boolean_grouped
    grouped = (
        carbon_codegen.query("actor")
        .select(["DISTINCT", "actor.first_name"], ["AS", ["COUNT", "actor.actor_id"], "cnt"])
        .group_by("actor.first_name")
        .having({"cnt": [">", 1]})
        .page(2)
        .limit(5)
        .compile(dialect="mysql")
    )
    assert grouped["status"] == 0, grouped
    assert grouped["sql"] == (
        "SELECT DISTINCT actor.first_name, COUNT(actor.actor_id) AS cnt FROM `actor` "
        "GROUP BY actor.first_name HAVING ((cnt) > ?) LIMIT 5, 5"
    ), grouped
    assert grouped["params"] == [1], grouped
    inserted = (
        carbon_codegen.query("actor")
        .insert({"actor.first_name": "ALICE"})
        .compile(schema=schema, dialect="mysql")
    )
    assert inserted["status"] == 0, inserted
    assert inserted["sql"] == "INSERT INTO `actor` (`first_name`) VALUES (?)", inserted
    assert inserted["params"] == ["ALICE"], inserted
    expression_inserted = (
        carbon_codegen.query("actor")
        .insert(
            {
                "actor.first_name": carbon_codegen.fn("CONCAT", carbon_codegen.lit("HEL"), carbon_codegen.lit("LO")),
                "actor.last_name": "SMITH",
            }
        )
        .compile(dialect="mysql")
    )
    assert expression_inserted["sql"] == (
        "INSERT INTO `actor` (`first_name`, `last_name`) VALUES (CONCAT(?, ?), ?)"
    ), expression_inserted
    assert expression_inserted["params"] == ["HEL", "LO", "SMITH"], expression_inserted
    assert carbon_codegen.query("actor").replace({"actor.first_name": "BOB"}).to_payload() == {
        "FROM": "actor",
        "REPLACE": {"actor.first_name": "BOB"},
    }
    updated = (
        carbon_codegen.query("actor")
        .update({"actor.first_name": "BOB"})
        .where({"actor.actor_id": 1})
        .compile(schema=schema, dialect="mysql")
    )
    assert updated["sql"] == "UPDATE `actor` SET `first_name` = ? WHERE (actor.actor_id) = ?", updated
    assert updated["params"] == ["BOB", 1], updated
    expression_updated = (
        carbon_codegen.query("actor")
        .update(
            {
                "actor.first_name": carbon_codegen.fn("CONCAT", carbon_codegen.lit("Mr. "), "actor.last_name"),
                "actor.last_name": carbon_codegen.custom_call(
                    "COALESCE",
                    carbon_codegen.lit("UNKNOWN"),
                    "actor.last_name",
                ),
            }
        )
        .where({"actor.actor_id": ["=", 7]})
        .compile(dialect="mysql")
    )
    assert expression_updated["sql"] == (
        "UPDATE `actor` SET `first_name` = CONCAT(?, actor.last_name), "
        "`last_name` = COALESCE(?, actor.last_name) WHERE (actor.actor_id) = ?"
    ), expression_updated
    assert expression_updated["params"] == ["Mr. ", "UNKNOWN", 7], expression_updated
    deleted = (
        carbon_codegen.query("actor")
        .delete()
        .where({"actor.actor_id": 1})
        .compile(schema=schema, dialect="mysql")
    )
    assert deleted["sql"] == "DELETE `actor` FROM `actor` WHERE (actor.actor_id) = ?", deleted
    assert deleted["params"] == [1], deleted
    upserted = (
        carbon_codegen.query("actor")
        .insert({"actor.actor_id": 1, "actor.first_name": "ALICE"})
        .upsert(["first_name"])
        .compile(schema=schema, dialect="mysql")
    )
    assert upserted["sql"] == (
        "INSERT INTO `actor` (`actor_id`, `first_name`) VALUES (?, ?) "
        "ON DUPLICATE KEY UPDATE `first_name` = VALUES(`first_name`)"
    ), upserted
    assert upserted["params"] == [1, "ALICE"], upserted
    assert carbon_codegen.query("actor").insert({"actor.actor_id": 1}).do_nothing().to_payload() == {
        "FROM": "actor",
        "INSERT": {"actor.actor_id": 1},
        "UPDATE": [],
    }
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
    generated: dict[str, object] = {}
    exec(models, generated)
    actor_model = generated["Actor"]
    assert carbon_codegen.model_table(actor_model) == "actor"
    assert carbon_codegen.model_column(actor_model, "first_name") == "actor.first_name"
    assert carbon_codegen.model_select(actor_model).to_payload() == {
        "FROM": "actor",
        "SELECT": ["actor.actor_id", "actor.first_name"],
    }
    model_built = (
        carbon_codegen.model_select(actor_model, "actor_id")
        .where_op("actor.actor_id", ">", 0)
        .limit(1)
        .compile(schema=schema, dialect="mysql")
    )
    assert model_built["sql"] == "SELECT actor.actor_id FROM `actor` WHERE (actor.actor_id) > ? LIMIT 1", model_built
    assert model_built["params"] == [0], model_built

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
