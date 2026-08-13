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
            foreach (($table['columns'] ?? []) as $column) {
                $property = carbon_codegen_property_name((string) ($column['name'] ?? ''), $usedProperties);
                $columns[$property] = (string) ($column['qualified'] ?? '');
                $columnNames[$property] = (string) ($column['name'] ?? '');
            }

            $lines[] = 'final class ' . $className;
            $lines[] = '{';
            $lines[] = '    public const TABLE = ' . carbon_codegen_string_literal($tableName) . ';';
            $lines[] = '    public const PRIMARY = ' . carbon_codegen_array_literal($table['primary'] ?? []) . ';';
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
                    $lines[] = '    public $' . $property . ';';
                }
            }
            $lines[] = '}';
            $lines[] = '';
        }

        return rtrim(implode(PHP_EOL, $lines)) . PHP_EOL;
    }
}
