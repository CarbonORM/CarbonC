'use strict';

const path = require('path');

const addonPath = path.join(__dirname, 'build', 'carbon.node');
let native;

try {
  native = require(addonPath);
} catch (error) {
  error.message = `Unable to load CarbonC Node binding at ${addonPath}. Run "bash build.sh" in bindings/node first.\n${error.message}`;
  throw error;
}

const C6C = Object.freeze({
  ADDDATE: 'ADDDATE',
  ADDTIME: 'ADDTIME',
  AS: 'AS',
  ASC: 'ASC',
  AND: 'AND',
  BETWEEN: 'BETWEEN',
  CALL: 'CALL',
  CONCAT: 'CONCAT',
  COUNT: 'COUNT',
  COUNT_ALL: 'COUNT_ALL',
  CURRENT_DATE: 'CURRENT_DATE',
  CURRENT_TIMESTAMP: 'CURRENT_TIMESTAMP',
  DATE: 'DATE',
  DATE_ADD: 'DATE_ADD',
  DATE_FORMAT: 'DATE_FORMAT',
  DATE_SUB: 'DATE_SUB',
  DATEDIFF: 'DATEDIFF',
  DELETE: 'DELETE',
  DESC: 'DESC',
  DISTINCT: 'DISTINCT',
  EQUAL: '=',
  EXISTS: 'EXISTS',
  FALSE: 'FALSE',
  FORCE_INDEX: 'FORCE INDEX',
  FROM: 'FROM',
  GREATER_THAN: '>',
  GREATER_THAN_OR_EQUAL_TO: '>=',
  GROUP_BY: 'GROUP_BY',
  GROUP_CONCAT: 'GROUP_CONCAT',
  HAVING: 'HAVING',
  IGNORE_INDEX: 'IGNORE INDEX',
  IN: 'IN',
  INDEX_HINTS: 'INDEX_HINTS',
  INNER: 'INNER',
  INSERT: 'INSERT',
  IS: 'IS',
  IS_NOT: 'IS_NOT',
  JOIN: 'JOIN',
  LEFT: 'LEFT',
  LEFT_OUTER: 'LEFT_OUTER',
  LESS_THAN: '<',
  LESS_THAN_OR_EQUAL_TO: '<=',
  LIKE: 'LIKE',
  LIMIT: 'LIMIT',
  LIT: 'LIT',
  MATCH_AGAINST: 'MATCH_AGAINST',
  MBRCONTAINS: 'MBRContains',
  MIN: 'MIN',
  MAX: 'MAX',
  NOT_BETWEEN: 'NOT BETWEEN',
  NOT_EQUAL: '<>',
  NOT_EXISTS: 'NOT_EXISTS',
  NOT_IN: 'NOT_IN',
  NOT_LIKE: 'NOT_LIKE',
  NULL: 'NULL',
  OR: 'OR',
  ORDER: 'ORDER',
  PAGE: 'PAGE',
  PAGINATION: 'PAGINATION',
  PARAM: 'PARAM',
  REPLACE: 'REPLACE',
  RIGHT: 'RIGHT',
  RIGHT_OUTER: 'RIGHT_OUTER',
  SELECT: 'SELECT',
  ST_CONTAINS: 'ST_Contains',
  ST_GEOMFROMTEXT: 'ST_GeomFromText',
  ST_WITHIN: 'ST_Within',
  SUBSELECT: 'SUBSELECT',
  SUM: 'SUM',
  UPDATE: 'UPDATE',
  USE_INDEX: 'USE INDEX',
  WHERE: 'WHERE',
});
const C6 = C6C;
const CarbonDialect = Object.freeze({
  MYSQL: 'mysql',
  POSTGRESQL: 'postgresql',
  POSTGRES: 'postgres',
});
const Dialect = CarbonDialect;

function schemaJson(schema) {
  if (schema === undefined || schema === null) {
    return '{}';
  }
  if (typeof schema === 'string') {
    return schema;
  }
  return JSON.stringify(schema);
}

function payloadJson(payload, name) {
  if (typeof payload === 'string') {
    return payload;
  }
  const encoded = JSON.stringify(payload);
  if (encoded === undefined) {
    throw new TypeError(`${name} must be JSON-serializable`);
  }
  return encoded;
}

function compileQueryValue(query, schema, dialect = CarbonDialect.MYSQL) {
  return native.compileQuery(payloadJson(query, 'query'), schemaJson(schema), dialect);
}

function decodeJsonField(result, field) {
  if (!Object.prototype.hasOwnProperty.call(result, field) || typeof result[field] !== 'string') {
    throw new TypeError(`${field} must be a JSON string`);
  }
  try {
    return JSON.parse(result[field]);
  } catch (error) {
    error.message = `${field} could not be decoded: ${error.message}`;
    throw error;
  }
}

function adaptCompileResult(result) {
  return {
    ...result,
    params: decodeJsonField(result, 'params_json'),
    diagnostics: decodeJsonField(result, 'diagnostics_json'),
  };
}

function compileQueryResult(query, schema, dialect = CarbonDialect.MYSQL) {
  return adaptCompileResult(compileQueryValue(query, schema, dialect));
}

function copyPayloadValue(value) {
  if (Array.isArray(value)) {
    return value.map((item) => {
      if (item !== null && !Array.isArray(item) && typeof item === 'object') {
        return {...item};
      }
      return item;
    });
  }
  if (value !== null && typeof value === 'object') {
    return {...value};
  }
  return value;
}

function queryPayload(query) {
  if (query instanceof CarbonQuery) {
    return query.toPayload();
  }
  return copyPayloadValue(query);
}

function firstPresent(object, keys) {
  for (const key of keys) {
    if (Object.prototype.hasOwnProperty.call(object, key)) {
      return object[key];
    }
  }
  return undefined;
}

function mappingPayload(value, name) {
  if (value === undefined || value === null) {
    return {};
  }
  const payload = queryPayload(value);
  if (payload === null || Array.isArray(payload) || typeof payload !== 'object') {
    throw new TypeError(`${name} must be an object`);
  }
  return {...payload};
}

function subselect(query) {
  return [C6C.SUBSELECT, queryPayload(query)];
}

function derivedTarget(alias, query) {
  return JSON.stringify({[C6C.SUBSELECT]: queryPayload(query), [C6C.AS]: alias});
}

function op(operator, ...operands) {
  return [operator, ...operands.map(copyPayloadValue)];
}

function lit(value) {
  return [C6C.LIT, value];
}

function eqLit(value) {
  return [C6C.EQUAL, lit(value)];
}

function inLit(values) {
  return [C6C.IN, [...values].map(lit)];
}

function notInLit(values) {
  return [C6C.NOT_IN, [...values].map(lit)];
}

function betweenLit(start, end) {
  return [C6C.BETWEEN, [lit(start), lit(end)]];
}

function param(value) {
  return [C6C.PARAM, value];
}

function call(name, ...args) {
  return fn(name, ...args);
}

function fn(name, ...args) {
  return [name, ...args.map(copyPayloadValue)];
}

function customCall(name, ...args) {
  return [C6C.CALL, name, ...args.map(copyPayloadValue)];
}

function stContains(envelope, shape) {
  return fn(C6C.ST_CONTAINS, envelope, shape);
}

function stWithin(shape, envelope) {
  return fn(C6C.ST_WITHIN, shape, envelope);
}

function mbrContains(envelope, shape) {
  return fn(C6C.MBRCONTAINS, envelope, shape);
}

function alias(expression, name) {
  return [C6C.AS, copyPayloadValue(expression), name];
}

function distinct(expression) {
  return [C6C.DISTINCT, copyPayloadValue(expression)];
}

function between(start, end) {
  return [C6C.BETWEEN, [copyPayloadValue(start), copyPayloadValue(end)]];
}

function notBetween(start, end) {
  return [C6C.NOT_BETWEEN, [copyPayloadValue(start), copyPayloadValue(end)]];
}

function inList(values) {
  return [C6C.IN, setOperand(values)];
}

function notInList(values) {
  return [C6C.NOT_IN, setOperand(values)];
}

function matchAgainst(search, mode) {
  const payload = [matchSearchOperand(search)];
  if (mode !== undefined && mode !== null) {
    payload.push(mode);
  }
  return [C6C.MATCH_AGAINST, payload];
}

function indexValues(indexes) {
  if (indexes.length === 1 && Array.isArray(indexes[0])) {
    return [...indexes[0]];
  }
  return [...indexes];
}

function indexHint(kind, ...indexes) {
  return {[kind]: indexValues(indexes)};
}

function forceIndex(...indexes) {
  return indexHint(C6C.FORCE_INDEX, ...indexes);
}

function useIndex(...indexes) {
  return indexHint(C6C.USE_INDEX, ...indexes);
}

function ignoreIndex(...indexes) {
  return indexHint(C6C.IGNORE_INDEX, ...indexes);
}

function existsSpec(outerColumn, query, innerColumn) {
  const spec = [outerColumn, subselectOperand(query)];
  if (innerColumn !== undefined && innerColumn !== null) {
    spec.push(innerColumn);
  }
  return spec;
}

function exists(...specs) {
  return {[C6C.EXISTS]: specs.map(copyPayloadValue)};
}

function notExists(...specs) {
  return {[C6C.NOT_EXISTS]: specs.map(copyPayloadValue)};
}

function condition(column, value) {
  return {[column]: copyPayloadValue(value)};
}

function group(operator, ...conditions) {
  const operatorKey = operator.toUpperCase().replace(/\s+/g, '_');
  if (operatorKey !== C6C.AND && operatorKey !== C6C.OR) {
    throw new TypeError('operator must be AND or OR');
  }
  return {[operatorKey]: conditions.map(copyPayloadValue)};
}

function andGroup(...conditions) {
  return group(C6C.AND, ...conditions);
}

function orGroup(...conditions) {
  return group(C6C.OR, ...conditions);
}

function modelTable(model) {
  const table = model && (model.table || model.TABLE);
  if (typeof table !== 'string' || table.length === 0) {
    throw new TypeError('model must provide a Carbon table name');
  }
  return table;
}

function modelColumns(model) {
  const columns = model && (model.columns || model.COLUMNS);
  if (columns === null || Array.isArray(columns) || typeof columns !== 'object') {
    throw new TypeError('model must provide Carbon columns');
  }
  return {...columns};
}

function modelColumn(model, field) {
  const columns = modelColumns(model);
  if (!Object.prototype.hasOwnProperty.call(columns, field)) {
    throw new TypeError(`unknown model field: ${field}`);
  }
  return columns[field];
}

function modelJoinTarget(model, alias) {
  if (typeof alias !== 'string' || alias.length === 0) {
    throw new TypeError('model join alias must be a non-empty string');
  }
  return `${modelTable(model)} ${alias}`;
}

function modelAliasColumn(model, alias, field) {
  const columns = modelColumns(model);
  if (!Object.prototype.hasOwnProperty.call(columns, field)) {
    throw new TypeError(`unknown model field: ${field}`);
  }
  if (typeof alias !== 'string' || alias.length === 0) {
    throw new TypeError('model column alias must be a non-empty string');
  }
  return `${alias}.${field}`;
}

function modelAliasColumns(model, alias) {
  const columns = modelColumns(model);
  if (typeof alias !== 'string' || alias.length === 0) {
    throw new TypeError('model column alias must be a non-empty string');
  }
  return Object.fromEntries(Object.keys(columns).map((field) => [field, `${alias}.${field}`]));
}

function modelQuery(model) {
  return query(modelTable(model));
}

function modelSelect(model, ...fields) {
  const columns = modelColumns(model);
  const selected = fields.length === 0
    ? Object.values(columns)
    : fields.map((field) => modelColumn(model, field));
  return modelQuery(model).select(selected);
}

function modelValues(model, values) {
  if (values === null || Array.isArray(values) || typeof values !== 'object') {
    throw new TypeError('model values must be an object');
  }
  return Object.fromEntries(
    Object.entries(values).map(([field, value]) => [modelColumn(model, field), copyPayloadValue(value)])
  );
}

function modelInsert(model, values) {
  return modelQuery(model).insert(modelValues(model, values));
}

function modelReplace(model, values) {
  return modelQuery(model).replace(modelValues(model, values));
}

function modelUpdate(model, values) {
  return modelQuery(model).update(modelValues(model, values));
}

function modelUpsert(model, values, fields) {
  return modelInsert(model, values).upsert(fields);
}

function modelDoNothing(model, values) {
  return modelInsert(model, values).doNothing();
}

function modelGetPayload(model, queryValue) {
  const table = modelTable(model);
  const payload = mappingPayload(queryValue, 'query');
  const fromValue = firstPresent(payload, [C6C.FROM, 'from', 'table']);
  if (fromValue !== undefined && String(fromValue) !== table) {
    throw new TypeError(`query FROM/table ${JSON.stringify(fromValue)} does not match model table ${JSON.stringify(table)}`);
  }
  payload[C6C.FROM] = table;
  return payload;
}

function modelGetRequest(model, queryValue, options = {}) {
  const requestOptions = options || {};
  const table = modelTable(model);
  const payload = modelGetPayload(model, queryValue);
  const request = {
    query: payload,
    dialect: requestOptions.dialect || CarbonDialect.MYSQL,
    method: 'Get',
    model: table,
  };
  if (Object.prototype.hasOwnProperty.call(requestOptions, 'schema')) {
    request.schema = copyPayloadValue(requestOptions.schema);
  }
  const cacheResults = firstPresent(payload, ['cacheResults', 'cache_results']);
  if (cacheResults !== undefined) {
    request.cacheResults = cacheResults;
  }
  return request;
}

function modelApi(model) {
  const table = modelTable(model);
  const fields = model && (model.fields || model.FIELDS || {});
  const columns = model && (model.columns || model.COLUMNS || {});
  const api = {
    ...model,
    table,
    TABLE: table,
    fields,
    FIELDS: fields,
    columns,
    COLUMNS: columns,
    GetPayload(queryValue) {
      return modelGetPayload(model, queryValue);
    },
    Get(queryValue, options) {
      return modelGetRequest(model, queryValue, options);
    },
  };
  return Object.freeze(api);
}

function subselectOperand(query) {
  if (Array.isArray(query)
    && query.length === 2
    && typeof query[0] === 'string'
    && query[0].toUpperCase() === C6C.SUBSELECT) {
    return copyPayloadValue(query);
  }
  if (query !== null
    && !Array.isArray(query)
    && typeof query === 'object'
    && (Object.prototype.hasOwnProperty.call(query, C6C.SUBSELECT)
      || Object.prototype.hasOwnProperty.call(query, 'subselect'))) {
    return {...query};
  }
  return subselect(query);
}

function setOperand(values) {
  if (values instanceof CarbonQuery) {
    return subselect(values);
  }
  return copyPayloadValue(values);
}

function matchSearchOperand(search) {
  if (typeof search === 'string') {
    return lit(search);
  }
  return copyPayloadValue(search);
}

class CarbonQuery {
  constructor(table) {
    this.payload = {};
    if (table !== undefined && table !== null) {
      this.payload[C6C.FROM] = table;
    }
  }

  from(table) {
    this.payload[C6C.FROM] = table;
    return this;
  }

  select(...columns) {
    if (columns.length === 1 && Array.isArray(columns[0])) {
      this.payload[C6C.SELECT] = [...columns[0]];
    } else {
      this.payload[C6C.SELECT] = columns;
    }
    return this;
  }

  where(conditions) {
    this.payload[C6C.WHERE] = {...conditions};
    return this;
  }

  whereOp(column, operator, value) {
    this.whereMap()[column] = op(operator, value);
    return this;
  }

  whereIn(column, values) {
    this.whereMap()[column] = inList(values);
    return this;
  }

  whereNotIn(column, values) {
    this.whereMap()[column] = notInList(values);
    return this;
  }

  whereBetween(column, start, end) {
    this.whereMap()[column] = between(start, end);
    return this;
  }

  whereNotBetween(column, start, end) {
    this.whereMap()[column] = notBetween(start, end);
    return this;
  }

  whereMatchAgainst(column, search, mode) {
    this.whereMap()[column] = matchAgainst(search, mode);
    return this;
  }

  whereExists(outerColumn, subquery, innerColumn) {
    return this.appendExists(C6C.EXISTS, existsSpec(outerColumn, subquery, innerColumn));
  }

  whereNotExists(outerColumn, subquery, innerColumn) {
    return this.appendExists(C6C.NOT_EXISTS, existsSpec(outerColumn, subquery, innerColumn));
  }

  whereGroup(operator, ...conditions) {
    return this.appendBooleanGroup(operator, conditions);
  }

  whereAnd(...conditions) {
    return this.whereGroup('AND', ...conditions);
  }

  whereOr(...conditions) {
    return this.whereGroup('OR', ...conditions);
  }

  join(kind, target, on) {
    if (this.payload[C6C.JOIN] === undefined) {
      this.payload[C6C.JOIN] = {};
    }
    if (this.payload[C6C.JOIN] === null || Array.isArray(this.payload[C6C.JOIN]) || typeof this.payload[C6C.JOIN] !== 'object') {
      throw new TypeError('JOIN must be an object');
    }
    if (this.payload[C6C.JOIN][kind] === undefined) {
      this.payload[C6C.JOIN][kind] = {};
    }
    if (this.payload[C6C.JOIN][kind] === null || Array.isArray(this.payload[C6C.JOIN][kind]) || typeof this.payload[C6C.JOIN][kind] !== 'object') {
      throw new TypeError(`JOIN.${kind} must be an object`);
    }
    this.payload[C6C.JOIN][kind][target] = {...on};
    return this;
  }

  joinSubselect(kind, alias, subquery, on) {
    return this.join(kind, derivedTarget(alias, subquery), on);
  }

  indexHints(hints) {
    this.payload[C6C.INDEX_HINTS] = copyPayloadValue(hints);
    return this;
  }

  forceIndex(...indexes) {
    this.payload[C6C.INDEX_HINTS] = forceIndex(...indexes);
    return this;
  }

  useIndex(...indexes) {
    this.payload[C6C.INDEX_HINTS] = useIndex(...indexes);
    return this;
  }

  ignoreIndex(...indexes) {
    this.payload[C6C.INDEX_HINTS] = ignoreIndex(...indexes);
    return this;
  }

  groupBy(...expressions) {
    if (expressions.length === 1 && Array.isArray(expressions[0])) {
      this.payload[C6C.GROUP_BY] = [...expressions[0]];
    } else if (expressions.length === 1) {
      this.payload[C6C.GROUP_BY] = expressions[0];
    } else {
      this.payload[C6C.GROUP_BY] = expressions;
    }
    return this;
  }

  having(conditions) {
    this.payload[C6C.HAVING] = {...conditions};
    return this;
  }

  insert(values) {
    this.payload[C6C.INSERT] = copyPayloadValue(values);
    return this;
  }

  replace(values) {
    this.payload[C6C.REPLACE] = copyPayloadValue(values);
    return this;
  }

  update(values) {
    this.payload[C6C.UPDATE] = {...values};
    return this;
  }

  delete(enabled = true) {
    this.payload[C6C.DELETE] = enabled;
    return this;
  }

  upsert(columns) {
    this.payload[C6C.UPDATE] = [...columns];
    return this;
  }

  doNothing() {
    this.payload[C6C.UPDATE] = [];
    return this;
  }

  limit(value) {
    this.pagination()[C6C.LIMIT] = value;
    return this;
  }

  page(value) {
    this.pagination()[C6C.PAGE] = value;
    return this;
  }

  orderBy(column, direction = C6C.ASC) {
    const pagination = this.pagination();
    if (!Array.isArray(pagination[C6C.ORDER])) {
      pagination[C6C.ORDER] = [];
    }
    pagination[C6C.ORDER].push([column, direction]);
    return this;
  }

  toPayload() {
    return JSON.parse(JSON.stringify(this.payload));
  }

  compile(schema, dialect = CarbonDialect.MYSQL) {
    return compileQueryResult(this.payload, schema, dialect);
  }

  pagination() {
    if (this.payload[C6C.PAGINATION] === undefined) {
      this.payload[C6C.PAGINATION] = {};
    }
    if (this.payload[C6C.PAGINATION] === null || Array.isArray(this.payload[C6C.PAGINATION]) || typeof this.payload[C6C.PAGINATION] !== 'object') {
      throw new TypeError('PAGINATION must be an object');
    }
    return this.payload[C6C.PAGINATION];
  }

  whereMap() {
    if (this.payload[C6C.WHERE] === undefined) {
      this.payload[C6C.WHERE] = {};
    }
    if (this.payload[C6C.WHERE] === null || Array.isArray(this.payload[C6C.WHERE]) || typeof this.payload[C6C.WHERE] !== 'object') {
      throw new TypeError('WHERE must be an object');
    }
    return this.payload[C6C.WHERE];
  }

  appendExists(operator, spec) {
    const where = this.whereMap();
    if (where[operator] === undefined) {
      where[operator] = [];
    }
    if (!Array.isArray(where[operator])) {
      throw new TypeError(`WHERE.${operator} must be an array`);
    }
    where[operator].push(spec);
    return this;
  }

  appendBooleanGroup(operator, conditions) {
    const operatorKey = operator.toUpperCase().replace(/\s+/g, '_');
    if (operatorKey !== C6C.AND && operatorKey !== C6C.OR) {
      throw new TypeError('operator must be AND or OR');
    }
    const where = this.whereMap();
    if (where[operatorKey] === undefined) {
      where[operatorKey] = [];
    }
    if (!Array.isArray(where[operatorKey])) {
      throw new TypeError(`WHERE.${operatorKey} must be an array`);
    }
    where[operatorKey].push(...conditions.map(copyPayloadValue));
    return this;
  }
}

function query(table) {
  return new CarbonQuery(table);
}

function fromTable(table) {
  return new CarbonQuery(table);
}

function schemaModels(schema) {
  return native.schemaModelSource(schemaJson(schema), 'typescript', '{}');
}

native.schemaModels = schemaModels;
native.schema_models = schemaModels;
native.C6C = C6C;
native.C6 = C6;
native.CarbonDialect = CarbonDialect;
native.Dialect = Dialect;
native.compileQueryValue = compileQueryValue;
native.compile_query_value = compileQueryValue;
native.adaptCompileResult = adaptCompileResult;
native.adapt_compile_result = adaptCompileResult;
native.compileQueryResult = compileQueryResult;
native.compile_query_result = compileQueryResult;
native.CarbonQuery = CarbonQuery;
native.query = query;
native.fromTable = fromTable;
native.from_table = fromTable;
native.subselect = subselect;
native.derivedTarget = derivedTarget;
native.derived_target = derivedTarget;
native.op = op;
native.lit = lit;
native.eqLit = eqLit;
native.eq_lit = eqLit;
native.inLit = inLit;
native.in_lit = inLit;
native.notInLit = notInLit;
native.not_in_lit = notInLit;
native.betweenLit = betweenLit;
native.between_lit = betweenLit;
native.param = param;
native.call = call;
native.fn = fn;
native.customCall = customCall;
native.custom_call = customCall;
native.stContains = stContains;
native.st_contains = stContains;
native.stWithin = stWithin;
native.st_within = stWithin;
native.mbrContains = mbrContains;
native.mbr_contains = mbrContains;
native.alias = alias;
native.as = alias;
native.distinct = distinct;
native.between = between;
native.notBetween = notBetween;
native.not_between = notBetween;
native.inList = inList;
native.in_list = inList;
native.notInList = notInList;
native.not_in_list = notInList;
native.matchAgainst = matchAgainst;
native.match_against = matchAgainst;
native.indexHint = indexHint;
native.index_hint = indexHint;
native.forceIndex = forceIndex;
native.force_index = forceIndex;
native.useIndex = useIndex;
native.use_index = useIndex;
native.ignoreIndex = ignoreIndex;
native.ignore_index = ignoreIndex;
native.existsSpec = existsSpec;
native.exists_spec = existsSpec;
native.exists = exists;
native.notExists = notExists;
native.not_exists = notExists;
native.condition = condition;
native.group = group;
native.andGroup = andGroup;
native.and_group = andGroup;
native.orGroup = orGroup;
native.or_group = orGroup;
native.modelTable = modelTable;
native.model_table = modelTable;
native.modelColumns = modelColumns;
native.model_columns = modelColumns;
native.modelColumn = modelColumn;
native.model_column = modelColumn;
native.modelJoinTarget = modelJoinTarget;
native.model_join_target = modelJoinTarget;
native.modelAliasColumn = modelAliasColumn;
native.model_alias_column = modelAliasColumn;
native.modelAliasColumns = modelAliasColumns;
native.model_alias_columns = modelAliasColumns;
native.modelQuery = modelQuery;
native.model_query = modelQuery;
native.modelSelect = modelSelect;
native.model_select = modelSelect;
native.modelValues = modelValues;
native.model_values = modelValues;
native.modelInsert = modelInsert;
native.model_insert = modelInsert;
native.modelGetPayload = modelGetPayload;
native.model_get_payload = modelGetPayload;
native.modelGetRequest = modelGetRequest;
native.model_get_request = modelGetRequest;
native.modelApi = modelApi;
native.model_api = modelApi;
native.modelReplace = modelReplace;
native.model_replace = modelReplace;
native.modelUpdate = modelUpdate;
native.model_update = modelUpdate;
native.modelUpsert = modelUpsert;
native.model_upsert = modelUpsert;
native.modelDoNothing = modelDoNothing;
native.model_do_nothing = modelDoNothing;

module.exports = native;
