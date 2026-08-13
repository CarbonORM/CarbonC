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

const CLASS_SPLIT_RE = /[^0-9A-Za-z]+/;
const IDENTIFIER_RE = /^[A-Za-z_$][0-9A-Za-z_$]*$/;
const RESERVED_TYPESCRIPT_NAMES = new Set([
  'class',
  'const',
  'enum',
  'export',
  'function',
  'import',
  'interface',
  'let',
  'type',
]);

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

function compileQueryValue(query, schema, dialect = 'mysql') {
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

function compileQueryResult(query, schema, dialect = 'mysql') {
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

function subselect(query) {
  return ['SUBSELECT', queryPayload(query)];
}

function derivedTarget(alias, query) {
  return JSON.stringify({SUBSELECT: queryPayload(query), AS: alias});
}

function op(operator, ...operands) {
  return [operator, ...operands.map(copyPayloadValue)];
}

function lit(value) {
  return ['LIT', value];
}

function param(value) {
  return ['PARAM', value];
}

function call(name, ...args) {
  return [name, ...args.map(copyPayloadValue)];
}

function alias(expression, name) {
  return ['AS', copyPayloadValue(expression), name];
}

function distinct(expression) {
  return ['DISTINCT', copyPayloadValue(expression)];
}

function between(start, end) {
  return ['BETWEEN', [copyPayloadValue(start), copyPayloadValue(end)]];
}

function notBetween(start, end) {
  return ['NOT BETWEEN', [copyPayloadValue(start), copyPayloadValue(end)]];
}

function inList(values) {
  return ['IN', setOperand(values)];
}

function notInList(values) {
  return ['NOT_IN', setOperand(values)];
}

function existsSpec(outerColumn, query, innerColumn) {
  const spec = [outerColumn, subselectOperand(query)];
  if (innerColumn !== undefined && innerColumn !== null) {
    spec.push(innerColumn);
  }
  return spec;
}

function exists(...specs) {
  return {EXISTS: specs.map(copyPayloadValue)};
}

function notExists(...specs) {
  return {NOT_EXISTS: specs.map(copyPayloadValue)};
}

function condition(column, value) {
  return {[column]: copyPayloadValue(value)};
}

function group(operator, ...conditions) {
  const operatorKey = operator.toUpperCase().replace(/\s+/g, '_');
  if (operatorKey !== 'AND' && operatorKey !== 'OR') {
    throw new TypeError('operator must be AND or OR');
  }
  return {[operatorKey]: conditions.map(copyPayloadValue)};
}

function andGroup(...conditions) {
  return group('AND', ...conditions);
}

function orGroup(...conditions) {
  return group('OR', ...conditions);
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

function subselectOperand(query) {
  if (Array.isArray(query)
    && query.length === 2
    && typeof query[0] === 'string'
    && query[0].toUpperCase() === 'SUBSELECT') {
    return copyPayloadValue(query);
  }
  if (query !== null
    && !Array.isArray(query)
    && typeof query === 'object'
    && (Object.prototype.hasOwnProperty.call(query, 'SUBSELECT')
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

class CarbonQuery {
  constructor(table) {
    this.payload = {};
    if (table !== undefined && table !== null) {
      this.payload.FROM = table;
    }
  }

  from(table) {
    this.payload.FROM = table;
    return this;
  }

  select(...columns) {
    if (columns.length === 1 && Array.isArray(columns[0])) {
      this.payload.SELECT = [...columns[0]];
    } else {
      this.payload.SELECT = columns;
    }
    return this;
  }

  where(conditions) {
    this.payload.WHERE = {...conditions};
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

  whereExists(outerColumn, subquery, innerColumn) {
    return this.appendExists('EXISTS', existsSpec(outerColumn, subquery, innerColumn));
  }

  whereNotExists(outerColumn, subquery, innerColumn) {
    return this.appendExists('NOT_EXISTS', existsSpec(outerColumn, subquery, innerColumn));
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
    if (this.payload.JOIN === undefined) {
      this.payload.JOIN = {};
    }
    if (this.payload.JOIN === null || Array.isArray(this.payload.JOIN) || typeof this.payload.JOIN !== 'object') {
      throw new TypeError('JOIN must be an object');
    }
    if (this.payload.JOIN[kind] === undefined) {
      this.payload.JOIN[kind] = {};
    }
    if (this.payload.JOIN[kind] === null || Array.isArray(this.payload.JOIN[kind]) || typeof this.payload.JOIN[kind] !== 'object') {
      throw new TypeError(`JOIN.${kind} must be an object`);
    }
    this.payload.JOIN[kind][target] = {...on};
    return this;
  }

  joinSubselect(kind, alias, subquery, on) {
    return this.join(kind, derivedTarget(alias, subquery), on);
  }

  groupBy(...expressions) {
    if (expressions.length === 1 && Array.isArray(expressions[0])) {
      this.payload.GROUP_BY = [...expressions[0]];
    } else if (expressions.length === 1) {
      this.payload.GROUP_BY = expressions[0];
    } else {
      this.payload.GROUP_BY = expressions;
    }
    return this;
  }

  having(conditions) {
    this.payload.HAVING = {...conditions};
    return this;
  }

  insert(values) {
    this.payload.INSERT = copyPayloadValue(values);
    return this;
  }

  replace(values) {
    this.payload.REPLACE = copyPayloadValue(values);
    return this;
  }

  update(values) {
    this.payload.UPDATE = {...values};
    return this;
  }

  delete(enabled = true) {
    this.payload.DELETE = enabled;
    return this;
  }

  upsert(columns) {
    this.payload.UPDATE = [...columns];
    return this;
  }

  doNothing() {
    this.payload.UPDATE = [];
    return this;
  }

  limit(value) {
    this.pagination().LIMIT = value;
    return this;
  }

  page(value) {
    this.pagination().PAGE = value;
    return this;
  }

  orderBy(column, direction = 'ASC') {
    const pagination = this.pagination();
    if (!Array.isArray(pagination.ORDER)) {
      pagination.ORDER = [];
    }
    pagination.ORDER.push([column, direction]);
    return this;
  }

  toPayload() {
    return JSON.parse(JSON.stringify(this.payload));
  }

  compile(schema, dialect = 'mysql') {
    return compileQueryResult(this.payload, schema, dialect);
  }

  pagination() {
    if (this.payload.PAGINATION === undefined) {
      this.payload.PAGINATION = {};
    }
    if (this.payload.PAGINATION === null || Array.isArray(this.payload.PAGINATION) || typeof this.payload.PAGINATION !== 'object') {
      throw new TypeError('PAGINATION must be an object');
    }
    return this.payload.PAGINATION;
  }

  whereMap() {
    if (this.payload.WHERE === undefined) {
      this.payload.WHERE = {};
    }
    if (this.payload.WHERE === null || Array.isArray(this.payload.WHERE) || typeof this.payload.WHERE !== 'object') {
      throw new TypeError('WHERE must be an object');
    }
    return this.payload.WHERE;
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
    if (operatorKey !== 'AND' && operatorKey !== 'OR') {
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

function dedupe(name, used) {
  let candidate = name;
  let index = 2;
  while (used.has(candidate)) {
    candidate = `${name}${index}`;
    index += 1;
  }
  used.add(candidate);
  return candidate;
}

function typeName(tableName, used) {
  const parts = String(tableName).split(CLASS_SPLIT_RE).filter(Boolean);
  let name = parts.map((part) => `${part.slice(0, 1).toUpperCase()}${part.slice(1)}`).join('') || 'CarbonModel';
  if (/^[0-9]/.test(name)) {
    name = `Carbon${name}`;
  }
  if (RESERVED_TYPESCRIPT_NAMES.has(name.toLowerCase())) {
    name = `${name}Model`;
  }
  return dedupe(name, used);
}

function propertyName(columnName) {
  const name = String(columnName);
  if (IDENTIFIER_RE.test(name) && !RESERVED_TYPESCRIPT_NAMES.has(name)) {
    return name;
  }
  return JSON.stringify(name);
}

function baseDbType(column) {
  return String(column.db_type || '').trim().toLowerCase().split('(', 1)[0];
}

function typescriptType(column) {
  switch (baseDbType(column)) {
    case 'tinyint':
    case 'smallint':
    case 'mediumint':
    case 'int':
    case 'integer':
    case 'bigint':
    case 'decimal':
    case 'dec':
    case 'numeric':
    case 'float':
    case 'double':
    case 'real':
    case 'year':
      return column.nullable === true ? 'number | null' : 'number';
    case 'boolean':
    case 'bool':
      return column.nullable === true ? 'boolean | null' : 'boolean';
    case 'date':
    case 'datetime':
    case 'timestamp':
    case 'time':
      return column.nullable === true ? 'Date | number | string | null' : 'Date | number | string';
    case 'binary':
    case 'varbinary':
    case 'blob':
    case 'tinyblob':
    case 'mediumblob':
    case 'longblob':
      return column.nullable === true ? 'Buffer | string | null' : 'Buffer | string';
    case 'json':
    case 'geometry':
    case 'point':
    case 'polygon':
    case 'multipoint':
    case 'multilinestring':
    case 'multipolygon':
    case 'geometrycollection':
      return column.nullable === true ? 'Record<string, unknown> | null' : 'Record<string, unknown>';
    default:
      if (!column.db_type) {
        return column.nullable === true ? 'unknown | null' : 'unknown';
      }
      return column.nullable === true ? 'string | null' : 'string';
  }
}

function schemaModels(schema) {
  const metadata = JSON.parse(native.schemaMetadata(schemaJson(schema)));
  const usedTypes = new Set();
  const lines = [];

  for (const table of metadata.tables || []) {
    const name = typeName(table.name || '', usedTypes);
    lines.push(`export interface ${name} {`);
    for (const column of table.columns || []) {
      lines.push(`  ${propertyName(column.name)}: ${typescriptType(column)};`);
    }
    lines.push('}');
    lines.push('');
    lines.push(`export const ${name}Meta = {`);
    lines.push(`  table: ${JSON.stringify(table.name || '')},`);
    lines.push(`  primary: ${JSON.stringify(table.primary || [])},`);
    lines.push('  columns: {');
    for (const column of table.columns || []) {
      lines.push(`    ${propertyName(column.name)}: ${JSON.stringify(column.qualified || '')},`);
    }
    lines.push('  },');
    lines.push('  dbTypes: {');
    for (const column of table.columns || []) {
      if (column.db_type !== undefined) {
        lines.push(`    ${propertyName(column.name)}: ${JSON.stringify(column.db_type)},`);
      }
    }
    lines.push('  },');
    lines.push('  nullable: {');
    for (const column of table.columns || []) {
      if (typeof column.nullable === 'boolean') {
        lines.push(`    ${propertyName(column.name)}: ${column.nullable ? 'true' : 'false'},`);
      }
    }
    lines.push('  },');
    lines.push('} as const;');
    lines.push('');
  }

  const source = lines.join('\n').trimEnd();
  return source === '' ? '' : `${source}\n`;
}

native.schemaModels = schemaModels;
native.schema_models = schemaModels;
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
native.param = param;
native.call = call;
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
native.modelQuery = modelQuery;
native.model_query = modelQuery;
native.modelSelect = modelSelect;
native.model_select = modelSelect;

module.exports = native;
