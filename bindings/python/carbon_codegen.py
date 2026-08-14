"""Python package-level code generation helpers for CarbonC schemas."""

from __future__ import annotations

import json
from typing import Any, Mapping, Sequence

import carbon


class C6C:
    """CarbonNode-compatible C6 token constants."""

    ADDDATE = "ADDDATE"
    ADDTIME = "ADDTIME"
    AS = "AS"
    ASC = "ASC"
    AND = "AND"
    BETWEEN = "BETWEEN"
    CALL = "CALL"
    CONCAT = "CONCAT"
    COUNT = "COUNT"
    COUNT_ALL = "COUNT_ALL"
    CURRENT_DATE = "CURRENT_DATE"
    CURRENT_TIMESTAMP = "CURRENT_TIMESTAMP"
    DATE = "DATE"
    DATE_ADD = "DATE_ADD"
    DATE_FORMAT = "DATE_FORMAT"
    DATE_SUB = "DATE_SUB"
    DATEDIFF = "DATEDIFF"
    DELETE = "DELETE"
    DESC = "DESC"
    DISTINCT = "DISTINCT"
    EQUAL = "="
    EXISTS = "EXISTS"
    FALSE = "FALSE"
    FORCE_INDEX = "FORCE INDEX"
    FROM = "FROM"
    GREATER_THAN = ">"
    GREATER_THAN_OR_EQUAL_TO = ">="
    GROUP_BY = "GROUP_BY"
    GROUP_CONCAT = "GROUP_CONCAT"
    HAVING = "HAVING"
    IGNORE_INDEX = "IGNORE INDEX"
    IN = "IN"
    INDEX_HINTS = "INDEX_HINTS"
    INNER = "INNER"
    INSERT = "INSERT"
    IS = "IS"
    IS_NOT = "IS_NOT"
    JOIN = "JOIN"
    LEFT = "LEFT"
    LEFT_OUTER = "LEFT_OUTER"
    LESS_THAN = "<"
    LESS_THAN_OR_EQUAL_TO = "<="
    LIKE = "LIKE"
    LIMIT = "LIMIT"
    LIT = "LIT"
    MATCH_AGAINST = "MATCH_AGAINST"
    MBRCONTAINS = "MBRContains"
    MIN = "MIN"
    MAX = "MAX"
    NOT_BETWEEN = "NOT BETWEEN"
    NOT_EQUAL = "<>"
    NOT_EXISTS = "NOT_EXISTS"
    NOT_IN = "NOT_IN"
    NOT_LIKE = "NOT_LIKE"
    NULL = "NULL"
    OR = "OR"
    ORDER = "ORDER"
    PAGE = "PAGE"
    PAGINATION = "PAGINATION"
    PARAM = "PARAM"
    REPLACE = "REPLACE"
    RIGHT = "RIGHT"
    RIGHT_OUTER = "RIGHT_OUTER"
    SELECT = "SELECT"
    ST_CONTAINS = "ST_Contains"
    ST_GEOMFROMTEXT = "ST_GeomFromText"
    ST_WITHIN = "ST_Within"
    SUBSELECT = "SUBSELECT"
    SUM = "SUM"
    UPDATE = "UPDATE"
    USE_INDEX = "USE INDEX"
    WHERE = "WHERE"


C6 = C6C


class CarbonDialect:
    """Dialect tokens accepted by the CarbonC compiler."""

    MYSQL = "mysql"
    POSTGRESQL = "postgresql"
    POSTGRES = "postgres"


Dialect = CarbonDialect


_MISSING = object()


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


def compile_query_value(query: Any, schema: Any = None, dialect: str = CarbonDialect.MYSQL) -> dict[str, Any]:
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


def compile_query_result(query: Any, schema: Any = None, dialect: str = CarbonDialect.MYSQL) -> dict[str, Any]:
    """Compile a native Python query payload and decode JSON result fields."""

    return adapt_compile_result(compile_query_value(query, schema=schema, dialect=dialect))


def _first_present(mapping: Mapping[str, Any], *keys: str) -> Any:
    for key in keys:
        if key in mapping:
            return mapping[key]
    return _MISSING


def _mapping_payload(value: Any, name: str) -> dict[str, Any]:
    if value is None:
        return {}
    payload = _query_payload(value)
    if not isinstance(payload, Mapping):
        raise TypeError(f"{name} must be a mapping")
    return dict(payload)


class Query:
    """Small package-level query facade that emits canonical CarbonC payloads."""

    def __init__(self, table: Any = None) -> None:
        self._payload: dict[str, Any] = {}
        if table is not None:
            self._payload[C6C.FROM] = table

    def from_table(self, table: Any) -> "Query":
        self._payload[C6C.FROM] = table
        return self

    def select(self, *columns: Any) -> "Query":
        if len(columns) == 1 and isinstance(columns[0], (list, tuple)):
            self._payload[C6C.SELECT] = list(columns[0])
        else:
            self._payload[C6C.SELECT] = list(columns)
        return self

    def where(self, conditions: Mapping[str, Any]) -> "Query":
        self._payload[C6C.WHERE] = dict(conditions)
        return self

    def where_op(self, column: str, operator: str, value: Any) -> "Query":
        self._where()[column] = op(operator, value)
        return self

    def where_in(self, column: str, values: Any) -> "Query":
        self._where()[column] = in_(values)
        return self

    def where_not_in(self, column: str, values: Any) -> "Query":
        self._where()[column] = not_in(values)
        return self

    def where_between(self, column: str, start: Any, end: Any) -> "Query":
        self._where()[column] = between(start, end)
        return self

    def where_not_between(self, column: str, start: Any, end: Any) -> "Query":
        self._where()[column] = not_between(start, end)
        return self

    def where_match_against(self, column: str, search: Any, mode: str | None = None) -> "Query":
        self._where()[column] = match_against(search, mode)
        return self

    def where_exists(self, outer_column: str, subquery: Any, inner_column: str | None = None) -> "Query":
        self._append_exists(C6C.EXISTS, exists_spec(outer_column, subquery, inner_column))
        return self

    def where_not_exists(self, outer_column: str, subquery: Any, inner_column: str | None = None) -> "Query":
        self._append_exists(C6C.NOT_EXISTS, exists_spec(outer_column, subquery, inner_column))
        return self

    def where_group(self, operator: str, *conditions: Any) -> "Query":
        self._append_boolean(operator, conditions)
        return self

    def where_and(self, *conditions: Any) -> "Query":
        return self.where_group("AND", *conditions)

    def where_or(self, *conditions: Any) -> "Query":
        return self.where_group("OR", *conditions)

    def join(self, kind: str, target: Any, on: Mapping[str, Any]) -> "Query":
        joins = self._payload.setdefault(C6C.JOIN, {})
        if not isinstance(joins, dict):
            raise TypeError("JOIN must be a mapping")
        join_group = joins.setdefault(kind, {})
        if not isinstance(join_group, dict):
            raise TypeError(f"JOIN.{kind} must be a mapping")
        join_group[target] = dict(on)
        return self

    def join_subselect(self, kind: str, alias: str, subquery: Any, on: Mapping[str, Any]) -> "Query":
        return self.join(kind, derived_target(alias, subquery), on)

    def index_hints(self, hints: Any) -> "Query":
        self._payload[C6C.INDEX_HINTS] = self._copy_payload_value(hints)
        return self

    def force_index(self, *indexes: Any) -> "Query":
        self._payload[C6C.INDEX_HINTS] = force_index(*indexes)
        return self

    def use_index(self, *indexes: Any) -> "Query":
        self._payload[C6C.INDEX_HINTS] = use_index(*indexes)
        return self

    def ignore_index(self, *indexes: Any) -> "Query":
        self._payload[C6C.INDEX_HINTS] = ignore_index(*indexes)
        return self

    def group_by(self, *expressions: Any) -> "Query":
        if len(expressions) == 1 and isinstance(expressions[0], (list, tuple)):
            self._payload[C6C.GROUP_BY] = list(expressions[0])
        elif len(expressions) == 1:
            self._payload[C6C.GROUP_BY] = expressions[0]
        else:
            self._payload[C6C.GROUP_BY] = list(expressions)
        return self

    def having(self, conditions: Mapping[str, Any]) -> "Query":
        self._payload[C6C.HAVING] = dict(conditions)
        return self

    def insert(self, values: Any) -> "Query":
        self._payload[C6C.INSERT] = self._copy_payload_value(values)
        return self

    def replace(self, values: Any) -> "Query":
        self._payload[C6C.REPLACE] = self._copy_payload_value(values)
        return self

    def update(self, values: Mapping[str, Any]) -> "Query":
        self._payload[C6C.UPDATE] = dict(values)
        return self

    def delete(self, enabled: bool = True) -> "Query":
        self._payload[C6C.DELETE] = enabled
        return self

    def upsert(self, columns: Sequence[Any]) -> "Query":
        self._payload[C6C.UPDATE] = list(columns)
        return self

    def do_nothing(self) -> "Query":
        self._payload[C6C.UPDATE] = []
        return self

    def limit(self, value: int) -> "Query":
        self._pagination()[C6C.LIMIT] = value
        return self

    def page(self, value: int) -> "Query":
        self._pagination()[C6C.PAGE] = value
        return self

    def order_by(self, column: Any, direction: str = C6C.ASC) -> "Query":
        pagination = self._pagination()
        order = pagination.setdefault(C6C.ORDER, [])
        if not isinstance(order, list):
            raise TypeError("PAGINATION.ORDER must be a list")
        order.append([column, direction])
        return self

    def to_payload(self) -> dict[str, Any]:
        return json.loads(json.dumps(self._payload, separators=(",", ":")))

    def compile(self, schema: Any = None, dialect: str = CarbonDialect.MYSQL) -> dict[str, Any]:
        return compile_query_result(self._payload, schema=schema, dialect=dialect)

    def _where(self) -> dict[str, Any]:
        where = self._payload.setdefault(C6C.WHERE, {})
        if not isinstance(where, dict):
            raise TypeError("WHERE must be a mapping")
        return where

    def _append_exists(self, operator: str, spec: list[Any]) -> None:
        where = self._where()
        specs = where.setdefault(operator, [])
        if not isinstance(specs, list):
            raise TypeError(f"WHERE.{operator} must be a list")
        specs.append(spec)

    def _append_boolean(self, operator: str, conditions: Sequence[Any]) -> None:
        op_key = operator.upper().replace(" ", "_")
        if op_key not in {C6C.AND, C6C.OR}:
            raise ValueError("operator must be AND or OR")
        where = self._where()
        parts = where.setdefault(op_key, [])
        if not isinstance(parts, list):
            raise TypeError(f"WHERE.{op_key} must be a list")
        parts.extend(_condition_payload(condition) for condition in conditions)

    def _pagination(self) -> dict[str, Any]:
        pagination = self._payload.setdefault(C6C.PAGINATION, {})
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


def model_table(model: Any) -> str:
    if isinstance(model, Mapping):
        table = model.get("table", model.get(C6C.FROM, model.get("TABLE")))
    else:
        table = getattr(model, "__carbon_table__", None)
        if table is None:
            table = getattr(model, "TABLE", None)
        if table is None and not isinstance(model, type):
            table = getattr(type(model), "__carbon_table__", None)
            if table is None:
                table = getattr(type(model), "TABLE", None)
    if not isinstance(table, str) or not table:
        raise TypeError("model must provide a Carbon table name")
    return table


def model_columns(model: Any) -> dict[str, str]:
    if isinstance(model, Mapping):
        columns = model.get("columns", model.get("COLUMNS"))
    else:
        columns = getattr(model, "__carbon_columns__", None)
        if columns is None:
            columns = getattr(model, "COLUMNS", None)
        if columns is None and not isinstance(model, type):
            columns = getattr(type(model), "__carbon_columns__", None)
            if columns is None:
                columns = getattr(type(model), "COLUMNS", None)
    if not isinstance(columns, Mapping):
        raise TypeError("model must provide Carbon columns")
    return {str(field): str(column) for field, column in columns.items()}


def model_column(model: Any, field: str) -> str:
    columns = model_columns(model)
    if field not in columns:
        raise KeyError(f"unknown model field: {field}")
    return columns[field]


def model_query(model: Any) -> Query:
    return query(model_table(model))


def model_select(model: Any, *fields: str) -> Query:
    columns = model_columns(model)
    selected = [model_column(model, field) for field in fields] if fields else list(columns.values())
    return model_query(model).select(selected)


def model_values(model: Any, values: Mapping[str, Any]) -> dict[str, Any]:
    if not isinstance(values, Mapping):
        raise TypeError("model values must be a mapping")
    return {model_column(model, str(field)): Query._copy_payload_value(value) for field, value in values.items()}


def model_insert(model: Any, values: Mapping[str, Any]) -> Query:
    return model_query(model).insert(model_values(model, values))


def model_replace(model: Any, values: Mapping[str, Any]) -> Query:
    return model_query(model).replace(model_values(model, values))


def model_update(model: Any, values: Mapping[str, Any]) -> Query:
    return model_query(model).update(model_values(model, values))


def model_upsert(model: Any, values: Mapping[str, Any], *fields: str) -> Query:
    update_fields = list(fields[0]) if len(fields) == 1 and isinstance(fields[0], (list, tuple)) else list(fields)
    return model_insert(model, values).upsert(update_fields)


def model_do_nothing(model: Any, values: Mapping[str, Any]) -> Query:
    return model_insert(model, values).do_nothing()


def model_get_payload(model: Any, query: Any = None) -> dict[str, Any]:
    """Return a complete read payload for a generated model and native query object."""

    table = model_table(model)
    payload = _mapping_payload(query, "query")
    from_value = _first_present(payload, C6C.FROM, "from", "table")
    if from_value is not _MISSING and str(from_value) != table:
        raise ValueError(f"query FROM/table {from_value!r} does not match model table {table!r}")
    payload[C6C.FROM] = table
    return payload


def model_get_request(
    model: Any,
    query: Any = None,
    schema: Any = None,
    dialect: str = CarbonDialect.MYSQL,
) -> dict[str, Any]:
    """Return a serializable model Get envelope for the application executor."""

    table = model_table(model)
    payload = model_get_payload(model, query)
    request: dict[str, Any] = {
        "query": payload,
        "dialect": dialect,
        "method": "Get",
        "model": table,
    }
    if schema is not None:
        request["schema"] = Query._copy_payload_value(schema)
    cache_results = _first_present(payload, "cacheResults", "cache_results")
    if cache_results is not _MISSING:
        request["cacheResults"] = cache_results
    return request


def subselect(query: Any) -> list[Any]:
    return [C6C.SUBSELECT, _query_payload(query)]


def derived_target(alias: str, query: Any) -> str:
    return json.dumps({C6C.SUBSELECT: _query_payload(query), C6C.AS: alias}, separators=(",", ":"))


def op(operator: str, *operands: Any) -> list[Any]:
    return [operator, *[Query._copy_payload_value(value) for value in operands]]


def lit(value: Any) -> list[Any]:
    return [C6C.LIT, value]


def eq_lit(value: Any) -> list[Any]:
    return [C6C.EQUAL, lit(value)]


def in_lit(values: Any) -> list[Any]:
    return [C6C.IN, [lit(value) for value in values]]


def not_in_lit(values: Any) -> list[Any]:
    return [C6C.NOT_IN, [lit(value) for value in values]]


def between_lit(start: Any, end: Any) -> list[Any]:
    return [C6C.BETWEEN, [lit(start), lit(end)]]


def param(value: Any) -> list[Any]:
    return [C6C.PARAM, value]


def call(name: str, *arguments: Any) -> list[Any]:
    return fn(name, *arguments)


def fn(name: str, *arguments: Any) -> list[Any]:
    return [name, *[Query._copy_payload_value(argument) for argument in arguments]]


def custom_call(name: str, *arguments: Any) -> list[Any]:
    return [C6C.CALL, name, *[Query._copy_payload_value(argument) for argument in arguments]]


def st_contains(envelope: Any, shape: Any) -> list[Any]:
    return fn(C6C.ST_CONTAINS, envelope, shape)


def st_within(shape: Any, envelope: Any) -> list[Any]:
    return fn(C6C.ST_WITHIN, shape, envelope)


def mbr_contains(envelope: Any, shape: Any) -> list[Any]:
    return fn(C6C.MBRCONTAINS, envelope, shape)


def as_(expression: Any, alias: str) -> list[Any]:
    return [C6C.AS, Query._copy_payload_value(expression), alias]


def distinct(expression: Any) -> list[Any]:
    return [C6C.DISTINCT, Query._copy_payload_value(expression)]


def between(start: Any, end: Any) -> list[Any]:
    return [C6C.BETWEEN, [Query._copy_payload_value(start), Query._copy_payload_value(end)]]


def not_between(start: Any, end: Any) -> list[Any]:
    return [C6C.NOT_BETWEEN, [Query._copy_payload_value(start), Query._copy_payload_value(end)]]


def in_(values: Any) -> list[Any]:
    return [C6C.IN, _set_operand(values)]


def not_in(values: Any) -> list[Any]:
    return [C6C.NOT_IN, _set_operand(values)]


def match_against(search: Any, mode: str | None = None) -> list[Any]:
    payload = [_match_search_operand(search)]
    if mode is not None:
        payload.append(mode)
    return [C6C.MATCH_AGAINST, payload]


def index_hint(kind: str, *indexes: Any) -> dict[str, list[Any]]:
    if len(indexes) == 1 and isinstance(indexes[0], (list, tuple)):
        values = list(indexes[0])
    else:
        values = list(indexes)
    return {kind: values}


def force_index(*indexes: Any) -> dict[str, list[Any]]:
    return index_hint(C6C.FORCE_INDEX, *indexes)


def use_index(*indexes: Any) -> dict[str, list[Any]]:
    return index_hint(C6C.USE_INDEX, *indexes)


def ignore_index(*indexes: Any) -> dict[str, list[Any]]:
    return index_hint(C6C.IGNORE_INDEX, *indexes)


def exists_spec(outer_column: str, query: Any, inner_column: str | None = None) -> list[Any]:
    spec = [outer_column, _subselect_operand(query)]
    if inner_column is not None:
        spec.append(inner_column)
    return spec


def exists(*specs: Sequence[Any]) -> dict[str, list[Any]]:
    return {C6C.EXISTS: [list(spec) for spec in specs]}


def not_exists(*specs: Sequence[Any]) -> dict[str, list[Any]]:
    return {C6C.NOT_EXISTS: [list(spec) for spec in specs]}


def condition(column: str, value: Any) -> dict[str, Any]:
    return {column: Query._copy_payload_value(value)}


def group(operator: str, *conditions: Any) -> dict[str, list[Any]]:
    op_key = operator.upper().replace(" ", "_")
    if op_key not in {C6C.AND, C6C.OR}:
        raise ValueError("operator must be AND or OR")
    return {op_key: [_condition_payload(condition) for condition in conditions]}


def and_(*conditions: Any) -> dict[str, list[Any]]:
    return group(C6C.AND, *conditions)


def or_(*conditions: Any) -> dict[str, list[Any]]:
    return group(C6C.OR, *conditions)


def _query_payload(query: Any) -> Any:
    if isinstance(query, Query):
        return query.to_payload()
    return Query._copy_payload_value(query)


def _condition_payload(condition: Any) -> Any:
    return Query._copy_payload_value(condition)


def _subselect_operand(query: Any) -> Any:
    if isinstance(query, (list, tuple)) and len(query) == 2 and str(query[0]).upper() == C6C.SUBSELECT:
        return Query._copy_payload_value(query)
    if isinstance(query, Mapping) and (C6C.SUBSELECT in query or "subselect" in query):
        return dict(query)
    return subselect(query)


def _set_operand(values: Any) -> Any:
    if isinstance(values, Query):
        return subselect(values)
    return Query._copy_payload_value(values)


def _match_search_operand(search: Any) -> Any:
    if isinstance(search, str):
        return lit(search)
    return Query._copy_payload_value(search)


def schema_models(schema: Any = None) -> str:
    """Return Python dataclass source generated from CarbonC schema metadata."""

    return carbon.schema_model_source(_schema_json(schema), "python", "{}")


schema_dataclasses = schema_models
compile_query = compile_query_value

__all__ = [
    "C6",
    "C6C",
    "CarbonDialect",
    "Dialect",
    "Query",
    "adapt_compile_result",
    "and_",
    "as_",
    "between",
    "between_lit",
    "call",
    "compile_query",
    "compile_query_result",
    "compile_query_value",
    "condition",
    "custom_call",
    "derived_target",
    "distinct",
    "exists",
    "exists_spec",
    "eq_lit",
    "fn",
    "force_index",
    "from_table",
    "group",
    "ignore_index",
    "in_",
    "in_lit",
    "index_hint",
    "lit",
    "match_against",
    "mbr_contains",
    "model_column",
    "model_columns",
    "model_query",
    "model_do_nothing",
    "model_insert",
    "model_get_payload",
    "model_get_request",
    "model_replace",
    "model_select",
    "model_table",
    "model_update",
    "model_upsert",
    "model_values",
    "not_between",
    "not_exists",
    "not_in",
    "not_in_lit",
    "op",
    "or_",
    "param",
    "query",
    "schema_dataclasses",
    "schema_models",
    "st_contains",
    "st_within",
    "subselect",
    "use_index",
]
