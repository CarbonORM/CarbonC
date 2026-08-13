<?php

if (!function_exists('carbon_schema_models')) {
    function carbon_codegen_schema_json($schema): string
    {
        if ($schema === null) {
            return '{}';
        }
        if (is_string($schema)) {
            return $schema;
        }
        $encoded = json_encode($schema);
        if ($encoded === false) {
            throw new InvalidArgumentException('schema could not be encoded as JSON');
        }
        return $encoded;
    }

    function carbon_codegen_payload_json($payload): string
    {
        if (is_string($payload)) {
            return $payload;
        }
        $encoded = json_encode($payload);
        if ($encoded === false) {
            throw new InvalidArgumentException('query payload could not be encoded as JSON');
        }
        return $encoded;
    }

    function carbon_compile_query_value($query, $schema = null, string $dialect = 'mysql'): array
    {
        return carbon_compile_query(
            carbon_codegen_payload_json($query),
            carbon_codegen_schema_json($schema),
            $dialect
        );
    }

    function carbon_codegen_decode_json_field(array $result, string $field)
    {
        if (!array_key_exists($field, $result) || !is_string($result[$field])) {
            throw new InvalidArgumentException($field . ' must be a JSON string');
        }
        $decoded = json_decode($result[$field], true);
        if (json_last_error() !== JSON_ERROR_NONE) {
            throw new RuntimeException($field . ' could not be decoded: ' . json_last_error_msg());
        }
        return $decoded;
    }

    function carbon_adapt_compile_result(array $result): array
    {
        $result['params'] = carbon_codegen_decode_json_field($result, 'params_json');
        $result['diagnostics'] = carbon_codegen_decode_json_field($result, 'diagnostics_json');
        return $result;
    }

    function carbon_compile_query_result($query, $schema = null, string $dialect = 'mysql'): array
    {
        return carbon_adapt_compile_result(carbon_compile_query_value($query, $schema, $dialect));
    }

    function carbon_codegen_query_payload($query)
    {
        if ($query instanceof CarbonQuery) {
            return $query->toPayload();
        }
        return $query;
    }

    function carbon_subselect($query): array
    {
        return ['SUBSELECT', carbon_codegen_query_payload($query)];
    }

    function carbon_derived_target(string $alias, $query): string
    {
        $encoded = json_encode(['SUBSELECT' => carbon_codegen_query_payload($query), 'AS' => $alias]);
        if ($encoded === false) {
            throw new InvalidArgumentException('derived JOIN target could not be encoded as JSON');
        }
        return $encoded;
    }

    function carbon_op(string $operator, ...$operands): array
    {
        return array_merge([$operator], $operands);
    }

    function carbon_lit($value): array
    {
        return ['LIT', $value];
    }

    function carbon_param($value): array
    {
        return ['PARAM', $value];
    }

    function carbon_call(string $name, ...$arguments): array
    {
        return array_merge([$name], $arguments);
    }

    function carbon_alias($expression, string $alias): array
    {
        return ['AS', $expression, $alias];
    }

    function carbon_distinct($expression): array
    {
        return ['DISTINCT', $expression];
    }

    function carbon_between($start, $end): array
    {
        return ['BETWEEN', [$start, $end]];
    }

    function carbon_not_between($start, $end): array
    {
        return ['NOT BETWEEN', [$start, $end]];
    }

    function carbon_in_list($values): array
    {
        return ['IN', carbon_codegen_set_operand($values)];
    }

    function carbon_not_in_list($values): array
    {
        return ['NOT_IN', carbon_codegen_set_operand($values)];
    }

    function carbon_exists_spec(string $outerColumn, $query, ?string $innerColumn = null): array
    {
        $spec = [$outerColumn, carbon_codegen_subselect_operand($query)];
        if ($innerColumn !== null) {
            $spec[] = $innerColumn;
        }
        return $spec;
    }

    function carbon_exists(...$specs): array
    {
        return ['EXISTS' => array_values($specs)];
    }

    function carbon_not_exists(...$specs): array
    {
        return ['NOT_EXISTS' => array_values($specs)];
    }

    function carbon_codegen_subselect_operand($query)
    {
        if (is_array($query)) {
            $values = array_values($query);
            if (count($values) === 2 && is_string($values[0]) && strtoupper($values[0]) === 'SUBSELECT') {
                return $values;
            }
            if (array_key_exists('SUBSELECT', $query) || array_key_exists('subselect', $query)) {
                return $query;
            }
        }
        return carbon_subselect($query);
    }

    function carbon_codegen_set_operand($values)
    {
        if ($values instanceof CarbonQuery) {
            return carbon_subselect($values);
        }
        return $values;
    }

    if (!class_exists('CarbonQuery', false)) {
        final class CarbonQuery
        {
            /** @var array<string,mixed> */
            private $payload = [];

            public function __construct($table = null)
            {
                if ($table !== null) {
                    $this->payload['FROM'] = $table;
                }
            }

            public function fromTable($table): self
            {
                $this->payload['FROM'] = $table;
                return $this;
            }

            public function select(...$columns): self
            {
                if (count($columns) === 1 && is_array($columns[0])) {
                    $this->payload['SELECT'] = array_values($columns[0]);
                } else {
                    $this->payload['SELECT'] = array_values($columns);
                }
                return $this;
            }

            public function where(array $conditions): self
            {
                $this->payload['WHERE'] = $conditions;
                return $this;
            }

            public function whereOp(string $column, string $operator, $value): self
            {
                $where =& $this->wherePayload();
                $where[$column] = carbon_op($operator, $value);
                return $this;
            }

            public function whereIn(string $column, $values): self
            {
                $where =& $this->wherePayload();
                $where[$column] = carbon_in_list($values);
                return $this;
            }

            public function whereNotIn(string $column, $values): self
            {
                $where =& $this->wherePayload();
                $where[$column] = carbon_not_in_list($values);
                return $this;
            }

            public function whereBetween(string $column, $start, $end): self
            {
                $where =& $this->wherePayload();
                $where[$column] = carbon_between($start, $end);
                return $this;
            }

            public function whereNotBetween(string $column, $start, $end): self
            {
                $where =& $this->wherePayload();
                $where[$column] = carbon_not_between($start, $end);
                return $this;
            }

            public function whereExists(string $outerColumn, $query, ?string $innerColumn = null): self
            {
                return $this->appendExists('EXISTS', carbon_exists_spec($outerColumn, $query, $innerColumn));
            }

            public function whereNotExists(string $outerColumn, $query, ?string $innerColumn = null): self
            {
                return $this->appendExists('NOT_EXISTS', carbon_exists_spec($outerColumn, $query, $innerColumn));
            }

            public function join(string $kind, $target, array $on): self
            {
                if (!array_key_exists('JOIN', $this->payload)) {
                    $this->payload['JOIN'] = [];
                }
                if (!is_array($this->payload['JOIN'])) {
                    throw new RuntimeException('JOIN must be an array');
                }
                if (!array_key_exists($kind, $this->payload['JOIN'])) {
                    $this->payload['JOIN'][$kind] = [];
                }
                if (!is_array($this->payload['JOIN'][$kind])) {
                    throw new RuntimeException('JOIN.' . $kind . ' must be an array');
                }
                $this->payload['JOIN'][$kind][$target] = $on;
                return $this;
            }

            public function joinSubselect(string $kind, string $alias, $query, array $on): self
            {
                return $this->join($kind, carbon_derived_target($alias, $query), $on);
            }

            public function groupBy(...$expressions): self
            {
                if (count($expressions) === 1 && is_array($expressions[0])) {
                    $this->payload['GROUP_BY'] = array_values($expressions[0]);
                } elseif (count($expressions) === 1) {
                    $this->payload['GROUP_BY'] = $expressions[0];
                } else {
                    $this->payload['GROUP_BY'] = array_values($expressions);
                }
                return $this;
            }

            public function having(array $conditions): self
            {
                $this->payload['HAVING'] = $conditions;
                return $this;
            }

            public function insert($values): self
            {
                $this->payload['INSERT'] = $values;
                return $this;
            }

            public function replace($values): self
            {
                $this->payload['REPLACE'] = $values;
                return $this;
            }

            public function update(array $values): self
            {
                $this->payload['UPDATE'] = $values;
                return $this;
            }

            public function delete(bool $enabled = true): self
            {
                $this->payload['DELETE'] = $enabled;
                return $this;
            }

            public function upsert(array $columns): self
            {
                $this->payload['UPDATE'] = array_values($columns);
                return $this;
            }

            public function doNothing(): self
            {
                $this->payload['UPDATE'] = [];
                return $this;
            }

            public function limit(int $value): self
            {
                $pagination =& $this->pagination();
                $pagination['LIMIT'] = $value;
                return $this;
            }

            public function page(int $value): self
            {
                $pagination =& $this->pagination();
                $pagination['PAGE'] = $value;
                return $this;
            }

            public function orderBy($column, string $direction = 'ASC'): self
            {
                $pagination =& $this->pagination();
                $pagination['ORDER'][] = [$column, $direction];
                return $this;
            }

            public function toPayload(): array
            {
                $encoded = json_encode($this->payload);
                if ($encoded === false) {
                    throw new RuntimeException('query payload could not be encoded as JSON');
                }
                $decoded = json_decode($encoded, true);
                if (!is_array($decoded)) {
                    throw new RuntimeException('query payload could not be copied');
                }
                return $decoded;
            }

            public function compile($schema = null, string $dialect = 'mysql'): array
            {
                return carbon_compile_query_result($this->payload, $schema, $dialect);
            }

            private function &pagination(): array
            {
                if (!array_key_exists('PAGINATION', $this->payload)) {
                    $this->payload['PAGINATION'] = [];
                }
                if (!is_array($this->payload['PAGINATION'])) {
                    throw new RuntimeException('PAGINATION must be an array');
                }
                return $this->payload['PAGINATION'];
            }

            private function &wherePayload(): array
            {
                if (!array_key_exists('WHERE', $this->payload)) {
                    $this->payload['WHERE'] = [];
                }
                if (!is_array($this->payload['WHERE'])) {
                    throw new RuntimeException('WHERE must be an array');
                }
                return $this->payload['WHERE'];
            }

            private function appendExists(string $operator, array $spec): self
            {
                $where =& $this->wherePayload();
                if (!array_key_exists($operator, $where)) {
                    $where[$operator] = [];
                }
                if (!is_array($where[$operator])) {
                    throw new RuntimeException('WHERE.' . $operator . ' must be an array');
                }
                $where[$operator][] = $spec;
                return $this;
            }
        }
    }

    function carbon_query($table = null): CarbonQuery
    {
        return new CarbonQuery($table);
    }

    function carbon_codegen_string_literal(string $value): string
    {
        return "'" . str_replace(['\\', "'"], ['\\\\', "\\'"], $value) . "'";
    }

    function carbon_codegen_array_literal(array $values): string
    {
        if ($values === []) {
            return '[]';
        }
        return '[' . implode(', ', array_map('carbon_codegen_string_literal', array_map('strval', $values))) . ']';
    }

    function carbon_codegen_map_literal(array $values): string
    {
        if ($values === []) {
            return '[]';
        }
        $items = [];
        foreach ($values as $key => $value) {
            if (is_bool($value)) {
                $encodedValue = $value ? 'true' : 'false';
            } else {
                $encodedValue = carbon_codegen_string_literal((string) $value);
            }
            $items[] = carbon_codegen_string_literal((string) $key) . ' => ' . $encodedValue;
        }
        return '[' . implode(', ', $items) . ']';
    }

    function carbon_codegen_dedupe(string $name, array &$used): string
    {
        $candidate = $name;
        $index = 2;
        while (isset($used[$candidate])) {
            $candidate = $name . $index;
            ++$index;
        }
        $used[$candidate] = true;
        return $candidate;
    }

    function carbon_codegen_class_name(string $tableName, array &$used): string
    {
        $reserved = [
            'class' => true,
            'interface' => true,
            'trait' => true,
            'enum' => true,
        ];
        $parts = preg_split('/[^0-9A-Za-z]+/', $tableName, -1, PREG_SPLIT_NO_EMPTY);
        $name = '';
        foreach ($parts ?: [] as $part) {
            $name .= ucfirst($part);
        }
        if ($name === '') {
            $name = 'CarbonModel';
        }
        if (preg_match('/^[0-9]/', $name) === 1) {
            $name = 'Carbon' . $name;
        }
        if (isset($reserved[strtolower($name)])) {
            $name .= 'Model';
        }
        return carbon_codegen_dedupe($name, $used);
    }

    function carbon_codegen_property_name(string $columnName, array &$used): string
    {
        $name = preg_replace('/[^0-9A-Za-z_]/', '_', $columnName);
        $name = trim($name ?? '', '_');
        if ($name === '') {
            $name = 'field';
        }
        if (preg_match('/^[0-9]/', $name) === 1) {
            $name = '_' . $name;
        }
        return carbon_codegen_dedupe($name, $used);
    }

    function carbon_codegen_validate_namespace(?string $namespace): void
    {
        if ($namespace === null || $namespace === '') {
            return;
        }
        if (preg_match('/^[A-Za-z_][A-Za-z0-9_]*(\\\\[A-Za-z_][A-Za-z0-9_]*)*$/', $namespace) !== 1) {
            throw new InvalidArgumentException('namespace must be a valid PHP namespace');
        }
    }

    function carbon_codegen_php_type(array $column): string
    {
        $type = strtolower(trim((string) ($column['db_type'] ?? '')));
        $type = explode('(', $type, 2)[0];
        if ($type === '') {
            return 'mixed';
        }
        if (in_array($type, ['tinyint', 'smallint', 'mediumint', 'int', 'integer', 'bigint', 'year'], true)) {
            $base = 'int';
        } elseif (in_array($type, ['decimal', 'dec', 'numeric', 'float', 'double', 'real'], true)) {
            $base = 'float';
        } elseif (in_array($type, ['boolean', 'bool'], true)) {
            $base = 'bool';
        } elseif (in_array($type, ['json', 'geometry', 'point', 'polygon', 'multipoint', 'multilinestring', 'multipolygon', 'geometrycollection'], true)) {
            $base = 'array';
        } else {
            $base = 'string';
        }
        return (($column['nullable'] ?? false) === true && $base !== 'mixed') ? $base . '|null' : $base;
    }

    function carbon_schema_models($schema = null, ?string $namespace = null): string
    {
        carbon_codegen_validate_namespace($namespace);
        $schemaJson = carbon_codegen_schema_json($schema);
        $metadata = json_decode(carbon_schema_metadata($schemaJson), true);
        if (!is_array($metadata)) {
            throw new RuntimeException('CarbonC schema metadata could not be decoded');
        }

        $usedClasses = [];
        $lines = ['<?php', ''];
        if ($namespace !== null && $namespace !== '') {
            $lines[] = 'namespace ' . $namespace . ';';
            $lines[] = '';
        }

        foreach (($metadata['tables'] ?? []) as $table) {
            $tableName = (string) ($table['name'] ?? '');
            $className = carbon_codegen_class_name($tableName, $usedClasses);
            $usedProperties = [];
            $columns = [];
            $columnNames = [];
            $dbTypes = [];
            $nullable = [];
            $propertyTypes = [];
            foreach (($table['columns'] ?? []) as $column) {
                $property = carbon_codegen_property_name((string) ($column['name'] ?? ''), $usedProperties);
                $columns[$property] = (string) ($column['qualified'] ?? '');
                $columnNames[$property] = (string) ($column['name'] ?? '');
                if (array_key_exists('db_type', $column)) {
                    $dbTypes[$property] = (string) $column['db_type'];
                }
                if (array_key_exists('nullable', $column)) {
                    $nullable[$property] = (bool) $column['nullable'];
                }
                $propertyTypes[$property] = carbon_codegen_php_type($column);
            }

            $lines[] = 'final class ' . $className;
            $lines[] = '{';
            $lines[] = '    public const TABLE = ' . carbon_codegen_string_literal($tableName) . ';';
            $lines[] = '    public const PRIMARY = ' . carbon_codegen_array_literal($table['primary'] ?? []) . ';';
            $lines[] = '    public const DB_TYPES = ' . carbon_codegen_map_literal($dbTypes) . ';';
            $lines[] = '    public const NULLABLE = ' . carbon_codegen_map_literal($nullable) . ';';
            $lines[] = '    public const COLUMNS = [';
            foreach ($columns as $property => $qualified) {
                $lines[] = '        ' . carbon_codegen_string_literal($property) . ' => ' . carbon_codegen_string_literal($qualified) . ',';
            }
            $lines[] = '    ];';
            $lines[] = '    public const COLUMN_NAMES = [';
            foreach ($columnNames as $property => $columnName) {
                $lines[] = '        ' . carbon_codegen_string_literal($property) . ' => ' . carbon_codegen_string_literal($columnName) . ',';
            }
            $lines[] = '    ];';
            if ($columns !== []) {
                $lines[] = '';
                foreach (array_keys($columns) as $property) {
                    $lines[] = '    /** @var ' . $propertyTypes[$property] . ' */';
                    $lines[] = '    public $' . $property . ';';
                }
            }
            $lines[] = '}';
            $lines[] = '';
        }

        return rtrim(implode(PHP_EOL, $lines)) . PHP_EOL;
    }
}
