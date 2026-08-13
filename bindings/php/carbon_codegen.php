<?php

if (!function_exists('carbon_schema_models')) {
    if (!class_exists('C6C', false)) {
        final class C6C
        {
            public const ADDDATE = 'ADDDATE';
            public const ADDTIME = 'ADDTIME';
            public const AS = 'AS';
            public const ASC = 'ASC';
            public const AND = 'AND';
            public const BETWEEN = 'BETWEEN';
            public const CALL = 'CALL';
            public const CONCAT = 'CONCAT';
            public const COUNT = 'COUNT';
            public const COUNT_ALL = 'COUNT_ALL';
            public const CURRENT_DATE = 'CURRENT_DATE';
            public const CURRENT_TIMESTAMP = 'CURRENT_TIMESTAMP';
            public const DATE = 'DATE';
            public const DATE_ADD = 'DATE_ADD';
            public const DATE_FORMAT = 'DATE_FORMAT';
            public const DATE_SUB = 'DATE_SUB';
            public const DATEDIFF = 'DATEDIFF';
            public const DELETE = 'DELETE';
            public const DESC = 'DESC';
            public const DISTINCT = 'DISTINCT';
            public const EXISTS = 'EXISTS';
            public const FALSE = 'FALSE';
            public const FORCE_INDEX = 'FORCE INDEX';
            public const FROM = 'FROM';
            public const GREATER_THAN = '>';
            public const GREATER_THAN_OR_EQUAL_TO = '>=';
            public const GROUP_BY = 'GROUP_BY';
            public const GROUP_CONCAT = 'GROUP_CONCAT';
            public const HAVING = 'HAVING';
            public const IGNORE_INDEX = 'IGNORE INDEX';
            public const IN = 'IN';
            public const INDEX_HINTS = 'INDEX_HINTS';
            public const INNER = 'INNER';
            public const INSERT = 'INSERT';
            public const IS = 'IS';
            public const IS_NOT = 'IS_NOT';
            public const JOIN = 'JOIN';
            public const LEFT = 'LEFT';
            public const LEFT_OUTER = 'LEFT_OUTER';
            public const LESS_THAN = '<';
            public const LESS_THAN_OR_EQUAL_TO = '<=';
            public const LIKE = 'LIKE';
            public const LIMIT = 'LIMIT';
            public const LIT = 'LIT';
            public const MATCH_AGAINST = 'MATCH_AGAINST';
            public const MBRCONTAINS = 'MBRContains';
            public const MIN = 'MIN';
            public const MAX = 'MAX';
            public const NOT_BETWEEN = 'NOT BETWEEN';
            public const NOT_EQUAL = '<>';
            public const NOT_EXISTS = 'NOT_EXISTS';
            public const NOT_IN = 'NOT_IN';
            public const NOT_LIKE = 'NOT_LIKE';
            public const NULL = 'NULL';
            public const OR = 'OR';
            public const ORDER = 'ORDER';
            public const PAGE = 'PAGE';
            public const PAGINATION = 'PAGINATION';
            public const PARAM = 'PARAM';
            public const REPLACE = 'REPLACE';
            public const RIGHT = 'RIGHT';
            public const RIGHT_OUTER = 'RIGHT_OUTER';
            public const SELECT = 'SELECT';
            public const ST_CONTAINS = 'ST_Contains';
            public const ST_GEOMFROMTEXT = 'ST_GeomFromText';
            public const ST_WITHIN = 'ST_Within';
            public const SUBSELECT = 'SUBSELECT';
            public const SUM = 'SUM';
            public const UPDATE = 'UPDATE';
            public const USE_INDEX = 'USE INDEX';
            public const WHERE = 'WHERE';
        }
    }
    if (!class_exists('C6', false)) {
        class_alias('C6C', 'C6');
    }
    if (!class_exists('CarbonDialect', false)) {
        final class CarbonDialect
        {
            public const MYSQL = 'mysql';
            public const POSTGRESQL = 'postgresql';
            public const POSTGRES = 'postgres';
        }
    }

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

    function carbon_compile_query_value($query, $schema = null, string $dialect = CarbonDialect::MYSQL): array
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

    function carbon_compile_query_result($query, $schema = null, string $dialect = CarbonDialect::MYSQL): array
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
        return [C6C::SUBSELECT, carbon_codegen_query_payload($query)];
    }

    function carbon_derived_target(string $alias, $query): string
    {
        $encoded = json_encode([C6C::SUBSELECT => carbon_codegen_query_payload($query), C6C::AS => $alias]);
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
        return [C6C::LIT, $value];
    }

    function carbon_param($value): array
    {
        return [C6C::PARAM, $value];
    }

    function carbon_call(string $name, ...$arguments): array
    {
        return carbon_fn($name, ...$arguments);
    }

    function carbon_fn(string $name, ...$arguments): array
    {
        return array_merge([$name], $arguments);
    }

    function carbon_custom_call(string $name, ...$arguments): array
    {
        return array_merge([C6C::CALL, $name], $arguments);
    }

    function carbon_st_contains($envelope, $shape): array
    {
        return carbon_fn(C6C::ST_CONTAINS, $envelope, $shape);
    }

    function carbon_st_within($shape, $envelope): array
    {
        return carbon_fn(C6C::ST_WITHIN, $shape, $envelope);
    }

    function carbon_mbr_contains($envelope, $shape): array
    {
        return carbon_fn(C6C::MBRCONTAINS, $envelope, $shape);
    }

    function carbon_alias($expression, string $alias): array
    {
        return [C6C::AS, $expression, $alias];
    }

    function carbon_distinct($expression): array
    {
        return [C6C::DISTINCT, $expression];
    }

    function carbon_between($start, $end): array
    {
        return [C6C::BETWEEN, [$start, $end]];
    }

    function carbon_not_between($start, $end): array
    {
        return [C6C::NOT_BETWEEN, [$start, $end]];
    }

    function carbon_in_list($values): array
    {
        return [C6C::IN, carbon_codegen_set_operand($values)];
    }

    function carbon_not_in_list($values): array
    {
        return [C6C::NOT_IN, carbon_codegen_set_operand($values)];
    }

    function carbon_match_against($search, ?string $mode = null): array
    {
        $payload = [is_string($search) ? carbon_lit($search) : $search];
        if ($mode !== null) {
            $payload[] = $mode;
        }
        return [C6C::MATCH_AGAINST, $payload];
    }

    function carbon_codegen_index_values(array $indexes): array
    {
        if (count($indexes) === 1 && is_array($indexes[0])) {
            return array_values($indexes[0]);
        }
        return array_values($indexes);
    }

    function carbon_index_hint(string $kind, ...$indexes): array
    {
        return [$kind => carbon_codegen_index_values($indexes)];
    }

    function carbon_force_index(...$indexes): array
    {
        return carbon_index_hint(C6C::FORCE_INDEX, ...$indexes);
    }

    function carbon_use_index(...$indexes): array
    {
        return carbon_index_hint(C6C::USE_INDEX, ...$indexes);
    }

    function carbon_ignore_index(...$indexes): array
    {
        return carbon_index_hint(C6C::IGNORE_INDEX, ...$indexes);
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
        return [C6C::EXISTS => array_values($specs)];
    }

    function carbon_not_exists(...$specs): array
    {
        return [C6C::NOT_EXISTS => array_values($specs)];
    }

    function carbon_condition(string $column, $value): array
    {
        return [$column => $value];
    }

    function carbon_group(string $operator, ...$conditions): array
    {
        $operatorKey = strtoupper(str_replace(' ', '_', $operator));
        if ($operatorKey !== C6C::AND && $operatorKey !== C6C::OR) {
            throw new InvalidArgumentException('operator must be AND or OR');
        }
        return [$operatorKey => array_values($conditions)];
    }

    function carbon_and_group(...$conditions): array
    {
        return carbon_group(C6C::AND, ...$conditions);
    }

    function carbon_or_group(...$conditions): array
    {
        return carbon_group(C6C::OR, ...$conditions);
    }

    function carbon_model_table($model): string
    {
        if (is_array($model)) {
            $table = $model['table'] ?? $model['TABLE'] ?? null;
            if (is_string($table) && $table !== '') {
                return $table;
            }
        }

        $class = is_object($model) ? get_class($model) : (is_string($model) ? ltrim($model, '\\') : null);
        if ($class !== null && defined($class . '::TABLE')) {
            $table = constant($class . '::TABLE');
            if (is_string($table) && $table !== '') {
                return $table;
            }
        }
        throw new InvalidArgumentException('model must provide a Carbon table name');
    }

    function carbon_model_columns($model): array
    {
        if (is_array($model)) {
            $columns = $model['columns'] ?? $model['COLUMNS'] ?? null;
            if (is_array($columns)) {
                return $columns;
            }
        }

        $class = is_object($model) ? get_class($model) : (is_string($model) ? ltrim($model, '\\') : null);
        if ($class !== null && defined($class . '::COLUMNS')) {
            $columns = constant($class . '::COLUMNS');
            if (is_array($columns)) {
                return $columns;
            }
        }
        throw new InvalidArgumentException('model must provide Carbon columns');
    }

    function carbon_model_column($model, string $field): string
    {
        $columns = carbon_model_columns($model);
        if (!array_key_exists($field, $columns)) {
            throw new InvalidArgumentException('unknown model field: ' . $field);
        }
        return (string) $columns[$field];
    }

    function carbon_model_query($model): CarbonQuery
    {
        return carbon_query(carbon_model_table($model));
    }

    function carbon_model_select($model, ...$fields): CarbonQuery
    {
        if (count($fields) === 1 && is_array($fields[0])) {
            $fields = array_values($fields[0]);
        }
        $columns = carbon_model_columns($model);
        $selected = $fields === []
            ? array_values($columns)
            : array_map(
                static function ($field) use ($model): string {
                    return carbon_model_column($model, (string) $field);
                },
                $fields
            );
        return carbon_model_query($model)->select($selected);
    }

    function carbon_model_values($model, array $values): array
    {
        $mapped = [];
        foreach ($values as $field => $value) {
            $mapped[carbon_model_column($model, (string) $field)] = $value;
        }
        return $mapped;
    }

    function carbon_model_insert($model, array $values): CarbonQuery
    {
        return carbon_model_query($model)->insert(carbon_model_values($model, $values));
    }

    function carbon_model_replace($model, array $values): CarbonQuery
    {
        return carbon_model_query($model)->replace(carbon_model_values($model, $values));
    }

    function carbon_model_update($model, array $values): CarbonQuery
    {
        return carbon_model_query($model)->update(carbon_model_values($model, $values));
    }

    function carbon_model_upsert($model, array $values, array $fields): CarbonQuery
    {
        return carbon_model_insert($model, $values)->upsert($fields);
    }

    function carbon_model_do_nothing($model, array $values): CarbonQuery
    {
        return carbon_model_insert($model, $values)->doNothing();
    }

    function carbon_codegen_subselect_operand($query)
    {
        if (is_array($query)) {
            $values = array_values($query);
            if (count($values) === 2 && is_string($values[0]) && strtoupper($values[0]) === C6C::SUBSELECT) {
                return $values;
            }
            if (array_key_exists(C6C::SUBSELECT, $query) || array_key_exists('subselect', $query)) {
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
                    $this->payload[C6C::FROM] = $table;
                }
            }

            public function fromTable($table): self
            {
                $this->payload[C6C::FROM] = $table;
                return $this;
            }

            public function select(...$columns): self
            {
                if (count($columns) === 1 && is_array($columns[0])) {
                    $this->payload[C6C::SELECT] = array_values($columns[0]);
                } else {
                    $this->payload[C6C::SELECT] = array_values($columns);
                }
                return $this;
            }

            public function where(array $conditions): self
            {
                $this->payload[C6C::WHERE] = $conditions;
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

            public function whereMatchAgainst(string $column, $search, ?string $mode = null): self
            {
                $where =& $this->wherePayload();
                $where[$column] = carbon_match_against($search, $mode);
                return $this;
            }

            public function whereExists(string $outerColumn, $query, ?string $innerColumn = null): self
            {
                return $this->appendExists(C6C::EXISTS, carbon_exists_spec($outerColumn, $query, $innerColumn));
            }

            public function whereNotExists(string $outerColumn, $query, ?string $innerColumn = null): self
            {
                return $this->appendExists(C6C::NOT_EXISTS, carbon_exists_spec($outerColumn, $query, $innerColumn));
            }

            public function whereGroup(string $operator, ...$conditions): self
            {
                return $this->appendBooleanGroup($operator, $conditions);
            }

            public function whereAnd(...$conditions): self
            {
                return $this->whereGroup('AND', ...$conditions);
            }

            public function whereOr(...$conditions): self
            {
                return $this->whereGroup('OR', ...$conditions);
            }

            public function join(string $kind, $target, array $on): self
            {
                if (!array_key_exists(C6C::JOIN, $this->payload)) {
                    $this->payload[C6C::JOIN] = [];
                }
                if (!is_array($this->payload[C6C::JOIN])) {
                    throw new RuntimeException('JOIN must be an array');
                }
                if (!array_key_exists($kind, $this->payload[C6C::JOIN])) {
                    $this->payload[C6C::JOIN][$kind] = [];
                }
                if (!is_array($this->payload[C6C::JOIN][$kind])) {
                    throw new RuntimeException('JOIN.' . $kind . ' must be an array');
                }
                $this->payload[C6C::JOIN][$kind][$target] = $on;
                return $this;
            }

            public function joinSubselect(string $kind, string $alias, $query, array $on): self
            {
                return $this->join($kind, carbon_derived_target($alias, $query), $on);
            }

            public function indexHints($hints): self
            {
                $this->payload[C6C::INDEX_HINTS] = $hints;
                return $this;
            }

            public function forceIndex(...$indexes): self
            {
                $this->payload[C6C::INDEX_HINTS] = carbon_force_index(...$indexes);
                return $this;
            }

            public function useIndex(...$indexes): self
            {
                $this->payload[C6C::INDEX_HINTS] = carbon_use_index(...$indexes);
                return $this;
            }

            public function ignoreIndex(...$indexes): self
            {
                $this->payload[C6C::INDEX_HINTS] = carbon_ignore_index(...$indexes);
                return $this;
            }

            public function groupBy(...$expressions): self
            {
                if (count($expressions) === 1 && is_array($expressions[0])) {
                    $this->payload[C6C::GROUP_BY] = array_values($expressions[0]);
                } elseif (count($expressions) === 1) {
                    $this->payload[C6C::GROUP_BY] = $expressions[0];
                } else {
                    $this->payload[C6C::GROUP_BY] = array_values($expressions);
                }
                return $this;
            }

            public function having(array $conditions): self
            {
                $this->payload[C6C::HAVING] = $conditions;
                return $this;
            }

            public function insert($values): self
            {
                $this->payload[C6C::INSERT] = $values;
                return $this;
            }

            public function replace($values): self
            {
                $this->payload[C6C::REPLACE] = $values;
                return $this;
            }

            public function update(array $values): self
            {
                $this->payload[C6C::UPDATE] = $values;
                return $this;
            }

            public function delete(bool $enabled = true): self
            {
                $this->payload[C6C::DELETE] = $enabled;
                return $this;
            }

            public function upsert(array $columns): self
            {
                $this->payload[C6C::UPDATE] = array_values($columns);
                return $this;
            }

            public function doNothing(): self
            {
                $this->payload[C6C::UPDATE] = [];
                return $this;
            }

            public function limit(int $value): self
            {
                $pagination =& $this->pagination();
                $pagination[C6C::LIMIT] = $value;
                return $this;
            }

            public function page(int $value): self
            {
                $pagination =& $this->pagination();
                $pagination[C6C::PAGE] = $value;
                return $this;
            }

            public function orderBy($column, string $direction = C6C::ASC): self
            {
                $pagination =& $this->pagination();
                $pagination[C6C::ORDER][] = [$column, $direction];
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

            public function compile($schema = null, string $dialect = CarbonDialect::MYSQL): array
            {
                return carbon_compile_query_result($this->payload, $schema, $dialect);
            }

            private function &pagination(): array
            {
                if (!array_key_exists(C6C::PAGINATION, $this->payload)) {
                    $this->payload[C6C::PAGINATION] = [];
                }
                if (!is_array($this->payload[C6C::PAGINATION])) {
                    throw new RuntimeException('PAGINATION must be an array');
                }
                return $this->payload[C6C::PAGINATION];
            }

            private function &wherePayload(): array
            {
                if (!array_key_exists(C6C::WHERE, $this->payload)) {
                    $this->payload[C6C::WHERE] = [];
                }
                if (!is_array($this->payload[C6C::WHERE])) {
                    throw new RuntimeException('WHERE must be an array');
                }
                return $this->payload[C6C::WHERE];
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

            private function appendBooleanGroup(string $operator, array $conditions): self
            {
                $operatorKey = strtoupper(str_replace(' ', '_', $operator));
                if ($operatorKey !== C6C::AND && $operatorKey !== C6C::OR) {
                    throw new InvalidArgumentException('operator must be AND or OR');
                }
                $where =& $this->wherePayload();
                if (!array_key_exists($operatorKey, $where)) {
                    $where[$operatorKey] = [];
                }
                if (!is_array($where[$operatorKey])) {
                    throw new RuntimeException('WHERE.' . $operatorKey . ' must be an array');
                }
                foreach ($conditions as $condition) {
                    $where[$operatorKey][] = $condition;
                }
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

    function carbon_codegen_constant_name(string $propertyName, array &$used): string
    {
        $reserved = [
            'TABLE' => true,
            'PRIMARY' => true,
            'FIELDS' => true,
            'COLUMNS' => true,
            'COLUMN_NAMES' => true,
            'DB_TYPES' => true,
            'NULLABLE' => true,
        ];
        $name = strtoupper((string) preg_replace('/[^0-9A-Za-z_]/', '_', $propertyName));
        $name = trim($name, '_');
        if ($name === '') {
            $name = 'COLUMN';
        }
        if (preg_match('/^[0-9]/', $name) === 1) {
            $name = 'COLUMN_' . $name;
        }
        if (isset($reserved[$name])) {
            $name .= '_COLUMN';
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
            $usedConstants = [
                'TABLE' => true,
                'PRIMARY' => true,
                'FIELDS' => true,
                'COLUMNS' => true,
                'COLUMN_NAMES' => true,
                'DB_TYPES' => true,
                'NULLABLE' => true,
            ];
            $columns = [];
            $fieldConstants = [];
            $columnConstants = [];
            $columnNames = [];
            $dbTypes = [];
            $nullable = [];
            $propertyTypes = [];
            foreach (($table['columns'] ?? []) as $column) {
                $property = carbon_codegen_property_name((string) ($column['name'] ?? ''), $usedProperties);
                $columns[$property] = (string) ($column['qualified'] ?? '');
                $fieldConstants[$property] = carbon_codegen_constant_name('FIELD_' . $property, $usedConstants);
                $columnConstants[$property] = carbon_codegen_constant_name($property, $usedConstants);
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
            foreach ($columnNames as $property => $columnName) {
                $lines[] = '    public const ' . $fieldConstants[$property] . ' = ' . carbon_codegen_string_literal($columnName) . ';';
            }
            $lines[] = '    public const FIELDS = [';
            foreach ($fieldConstants as $fieldConstant) {
                $lines[] = '        self::' . $fieldConstant . ',';
            }
            $lines[] = '    ];';
            foreach ($columns as $property => $qualified) {
                $lines[] = '    public const ' . $columnConstants[$property] . ' = ' . carbon_codegen_string_literal($qualified) . ';';
            }
            $lines[] = '    public const PRIMARY = ' . carbon_codegen_array_literal($table['primary'] ?? []) . ';';
            $lines[] = '    public const DB_TYPES = ' . carbon_codegen_map_literal($dbTypes) . ';';
            $lines[] = '    public const NULLABLE = ' . carbon_codegen_map_literal($nullable) . ';';
            $lines[] = '    public const COLUMNS = [';
            foreach ($columns as $property => $qualified) {
                $lines[] = '        self::' . $fieldConstants[$property] . ' => self::' . $columnConstants[$property] . ',';
            }
            $lines[] = '    ];';
            $lines[] = '    public const COLUMN_NAMES = [';
            foreach ($columnNames as $property => $columnName) {
                $lines[] = '        self::' . $fieldConstants[$property] . ' => ' . carbon_codegen_string_literal($columnName) . ',';
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
