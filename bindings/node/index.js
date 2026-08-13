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

function schemaModels(schema) {
  const metadata = JSON.parse(native.schemaMetadata(schemaJson(schema)));
  const usedTypes = new Set();
  const lines = [];

  for (const table of metadata.tables || []) {
    const name = typeName(table.name || '', usedTypes);
    lines.push(`export interface ${name} {`);
    for (const column of table.columns || []) {
      lines.push(`  ${propertyName(column.name)}: unknown;`);
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
    lines.push('} as const;');
    lines.push('');
  }

  const source = lines.join('\n').trimEnd();
  return source === '' ? '' : `${source}\n`;
}

native.schemaModels = schemaModels;
native.schema_models = schemaModels;

module.exports = native;
