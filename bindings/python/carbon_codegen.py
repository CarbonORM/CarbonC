"""Python package-level code generation helpers for CarbonC schemas."""

from __future__ import annotations

import json
import keyword
import re
from typing import Any, Mapping, MutableSet, Sequence

import carbon


_CLASS_SPLIT_RE = re.compile(r"[^0-9A-Za-z]+")
_FIELD_RE = re.compile(r"[^0-9A-Za-z_]")


def _schema_json(schema: Any) -> str:
    if schema is None:
        return "{}"
    if isinstance(schema, str):
        return schema
    return json.dumps(schema, separators=(",", ":"))


def _payload_json(payload: Any) -> str:
    if isinstance(payload, str):
        return payload
    return json.dumps(payload, separators=(",", ":"))


def compile_query_value(query: Any, schema: Any = None, dialect: str = "mysql") -> dict[str, Any]:
    """Compile a native Python query payload through the CarbonC JSON boundary."""

    return carbon.compile_query(_payload_json(query), schema_json=_schema_json(schema), dialect=dialect)


def _decode_json_field(result: Mapping[str, Any], field: str) -> Any:
    value = result.get(field)
    if not isinstance(value, str):
        raise TypeError(f"{field} must be a JSON string")
    return json.loads(value)


def adapt_compile_result(result: Mapping[str, Any]) -> dict[str, Any]:
    """Return a compile result with params and diagnostics decoded to Python values."""

    adapted = dict(result)
    adapted["params"] = _decode_json_field(result, "params_json")
    adapted["diagnostics"] = _decode_json_field(result, "diagnostics_json")
    return adapted


def compile_query_result(query: Any, schema: Any = None, dialect: str = "mysql") -> dict[str, Any]:
    """Compile a native Python query payload and decode JSON result fields."""

    return adapt_compile_result(compile_query_value(query, schema=schema, dialect=dialect))


class Query:
    """Small package-level query facade that emits canonical CarbonC payloads."""

    def __init__(self, table: Any = None) -> None:
        self._payload: dict[str, Any] = {}
        if table is not None:
            self._payload["FROM"] = table

    def from_table(self, table: Any) -> "Query":
        self._payload["FROM"] = table
        return self

    def select(self, *columns: Any) -> "Query":
        if len(columns) == 1 and isinstance(columns[0], (list, tuple)):
            self._payload["SELECT"] = list(columns[0])
        else:
            self._payload["SELECT"] = list(columns)
        return self

    def where(self, conditions: Mapping[str, Any]) -> "Query":
        self._payload["WHERE"] = dict(conditions)
        return self

    def join(self, kind: str, target: Any, on: Mapping[str, Any]) -> "Query":
        joins = self._payload.setdefault("JOIN", {})
        if not isinstance(joins, dict):
            raise TypeError("JOIN must be a mapping")
        join_group = joins.setdefault(kind, {})
        if not isinstance(join_group, dict):
            raise TypeError(f"JOIN.{kind} must be a mapping")
        join_group[target] = dict(on)
        return self

    def join_subselect(self, kind: str, alias: str, subquery: Any, on: Mapping[str, Any]) -> "Query":
        return self.join(kind, derived_target(alias, subquery), on)

    def group_by(self, *expressions: Any) -> "Query":
        if len(expressions) == 1 and isinstance(expressions[0], (list, tuple)):
            self._payload["GROUP_BY"] = list(expressions[0])
        elif len(expressions) == 1:
            self._payload["GROUP_BY"] = expressions[0]
        else:
            self._payload["GROUP_BY"] = list(expressions)
        return self

    def having(self, conditions: Mapping[str, Any]) -> "Query":
        self._payload["HAVING"] = dict(conditions)
        return self

    def insert(self, values: Any) -> "Query":
        self._payload["INSERT"] = self._copy_payload_value(values)
        return self

    def replace(self, values: Any) -> "Query":
        self._payload["REPLACE"] = self._copy_payload_value(values)
        return self

    def update(self, values: Mapping[str, Any]) -> "Query":
        self._payload["UPDATE"] = dict(values)
        return self

    def delete(self, enabled: bool = True) -> "Query":
        self._payload["DELETE"] = enabled
        return self

    def upsert(self, columns: Sequence[Any]) -> "Query":
        self._payload["UPDATE"] = list(columns)
        return self

    def do_nothing(self) -> "Query":
        self._payload["UPDATE"] = []
        return self

    def limit(self, value: int) -> "Query":
        self._pagination()["LIMIT"] = value
        return self

    def page(self, value: int) -> "Query":
        self._pagination()["PAGE"] = value
        return self

    def order_by(self, column: Any, direction: str = "ASC") -> "Query":
        pagination = self._pagination()
        order = pagination.setdefault("ORDER", [])
        if not isinstance(order, list):
            raise TypeError("PAGINATION.ORDER must be a list")
        order.append([column, direction])
        return self

    def to_payload(self) -> dict[str, Any]:
        return json.loads(json.dumps(self._payload, separators=(",", ":")))

    def compile(self, schema: Any = None, dialect: str = "mysql") -> dict[str, Any]:
        return compile_query_result(self._payload, schema=schema, dialect=dialect)

    def _pagination(self) -> dict[str, Any]:
        pagination = self._payload.setdefault("PAGINATION", {})
        if not isinstance(pagination, dict):
            raise TypeError("PAGINATION must be a mapping")
        return pagination

    @staticmethod
    def _copy_payload_value(value: Any) -> Any:
        if isinstance(value, Mapping):
            return dict(value)
        if isinstance(value, (list, tuple)):
            return [dict(item) if isinstance(item, Mapping) else item for item in value]
        return value


def query(table: Any = None) -> Query:
    return Query(table)


def from_table(table: Any) -> Query:
    return Query(table)


def subselect(query: Any) -> list[Any]:
    return ["SUBSELECT", _query_payload(query)]


def derived_target(alias: str, query: Any) -> str:
    return json.dumps({"SUBSELECT": _query_payload(query), "AS": alias}, separators=(",", ":"))


def _query_payload(query: Any) -> Any:
    if isinstance(query, Query):
        return query.to_payload()
    return Query._copy_payload_value(query)


def _dedupe(name: str, used: MutableSet[str]) -> str:
    candidate = name
    index = 2
    while candidate in used:
        candidate = f"{name}{index}"
        index += 1
    used.add(candidate)
    return candidate


def _class_name(table_name: str, used: MutableSet[str]) -> str:
    parts = [part for part in _CLASS_SPLIT_RE.split(table_name) if part]
    name = "".join(part[:1].upper() + part[1:] for part in parts) or "CarbonModel"
    if name[0].isdigit():
        name = f"Carbon{name}"
    if keyword.iskeyword(name):
        name = f"{name}Model"
    return _dedupe(name, used)


def _field_name(column_name: str, used: MutableSet[str]) -> str:
    name = _FIELD_RE.sub("_", column_name).strip("_") or "field"
    if name[0].isdigit():
        name = f"_{name}"
    if keyword.iskeyword(name):
        name = f"{name}_"
    return _dedupe(name, used)


def _tuple_literal(values: Sequence[str]) -> str:
    if not values:
        return "()"
    if len(values) == 1:
        return f"({values[0]!r},)"
    return "(" + ", ".join(repr(value) for value in values) + ")"


def _append_dict_literal(lines: list[str], name: str, values: Mapping[str, str]) -> None:
    if not values:
        lines.append(f"    {name} = {{}}")
        return
    lines.append(f"    {name} = {{")
    for key, value in values.items():
        lines.append(f"        {key!r}: {value!r},")
    lines.append("    }")


def _append_bool_dict_literal(lines: list[str], name: str, values: Mapping[str, bool]) -> None:
    if not values:
        lines.append(f"    {name} = {{}}")
        return
    lines.append(f"    {name} = {{")
    for key, value in values.items():
        lines.append(f"        {key!r}: {value!r},")
    lines.append("    }")


def _python_type(column: Mapping[str, Any]) -> str:
    db_type = str(column.get("db_type", "")).strip().lower().split("(", 1)[0]
    if not db_type:
        return "Any"

    if db_type in {"tinyint", "smallint", "mediumint", "int", "integer", "bigint", "year"}:
        base = "int"
    elif db_type in {"decimal", "dec", "numeric", "float", "double", "real"}:
        base = "float"
    elif db_type in {"boolean", "bool"}:
        base = "bool"
    elif db_type in {"binary", "varbinary", "blob", "tinyblob", "mediumblob", "longblob"}:
        base = "bytes"
    elif db_type in {"json", "geometry", "point", "polygon", "multipoint", "multilinestring", "multipolygon", "geometrycollection"}:
        base = "Dict[str, Any]"
    else:
        base = "str"

    if column.get("nullable") is True and base != "Any":
        return f"Optional[{base}]"
    return base


def schema_models(schema: Any = None) -> str:
    """Return Python dataclass source generated from CarbonC schema metadata."""

    metadata = json.loads(carbon.schema_metadata(_schema_json(schema)))
    used_classes: set[str] = set()
    lines = [
        "from dataclasses import dataclass",
        "from typing import Any, Dict, Optional",
        "",
    ]

    for table in metadata.get("tables", []):
        table_name = str(table.get("name", ""))
        class_name = _class_name(table_name, used_classes)
        used_fields: set[str] = set()
        fields: list[tuple[str, str, str, str, str | None, bool | None]] = []
        for column in table.get("columns", []):
            original_name = str(column.get("name", ""))
            field_name = _field_name(original_name, used_fields)
            db_type = column.get("db_type")
            nullable = column.get("nullable")
            fields.append((
                field_name,
                original_name,
                str(column.get("qualified", "")),
                _python_type(column),
                str(db_type) if db_type is not None else None,
                nullable if isinstance(nullable, bool) else None,
            ))

        lines.append("@dataclass")
        lines.append(f"class {class_name}:")
        lines.append(f"    __carbon_table__ = {table_name!r}")
        lines.append(f"    __carbon_primary__ = {_tuple_literal([str(value) for value in table.get('primary', [])])}")
        _append_dict_literal(lines, "__carbon_columns__", {field: qualified for field, _, qualified, _, _, _ in fields})
        _append_dict_literal(lines, "__carbon_column_names__", {field: original for field, original, _, _, _, _ in fields})
        _append_dict_literal(
            lines,
            "__carbon_db_types__",
            {field: db_type for field, _, _, _, db_type, _ in fields if db_type is not None},
        )
        _append_bool_dict_literal(
            lines,
            "__carbon_nullable__",
            {field: nullable for field, _, _, _, _, nullable in fields if nullable is not None},
        )
        if fields:
            for field, _, _, python_type, _, _ in fields:
                lines.append(f"    {field}: {python_type} = None")
        lines.append("")

    return "\n".join(lines).rstrip() + "\n"


schema_dataclasses = schema_models
compile_query = compile_query_value

__all__ = [
    "Query",
    "adapt_compile_result",
    "compile_query",
    "compile_query_result",
    "compile_query_value",
    "from_table",
    "query",
    "schema_dataclasses",
    "schema_models",
]
