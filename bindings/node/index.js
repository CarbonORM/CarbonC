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

  limit(value) {
    this.pagination().LIMIT = value;
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

module.exports = native;
