# frozen_string_literal: true

require 'json'
require_relative 'carbon'

module CarbonC
  class Query
    def initialize(table = nil)
      @payload = {}
      @payload['FROM'] = table unless table.nil?
    end

    def from_table(table)
      @payload['FROM'] = table
      self
    end

    def select(*columns)
      @payload['SELECT'] = columns.length == 1 && columns.first.is_a?(Array) ? columns.first.dup : columns
      self
    end

    def where(conditions)
      @payload['WHERE'] = conditions.dup
      self
    end

    def join(kind, target, on)
      @payload['JOIN'] ||= {}
      raise TypeError, 'JOIN must be a Hash' unless @payload['JOIN'].is_a?(Hash)

      @payload['JOIN'][kind] ||= {}
      unless @payload['JOIN'][kind].is_a?(Hash)
        raise TypeError, "JOIN.#{kind} must be a Hash"
      end

      @payload['JOIN'][kind][target] = on.dup
      self
    end

    def group_by(*expressions)
      @payload['GROUP_BY'] = if expressions.length == 1 && expressions.first.is_a?(Array)
                               expressions.first.dup
                             elsif expressions.length == 1
                               expressions.first
                             else
                               expressions
                             end
      self
    end

    def having(conditions)
      @payload['HAVING'] = conditions.dup
      self
    end

    def limit(value)
      pagination['LIMIT'] = value
      self
    end

    def page(value)
      pagination['PAGE'] = value
      self
    end

    def order_by(column, direction = 'ASC')
      pagination['ORDER'] ||= []
      pagination['ORDER'] << [column, direction]
      self
    end

    def to_payload
      JSON.parse(JSON.generate(@payload))
    end

    def compile(schema = nil, dialect = 'mysql')
      CarbonC.compile_query_result(@payload, schema, dialect)
    end

    private

    def pagination
      @payload['PAGINATION'] ||= {}
      raise TypeError, 'PAGINATION must be a Hash' unless @payload['PAGINATION'].is_a?(Hash)

      @payload['PAGINATION']
    end
  end

  class << self
    def query(table = nil)
      Query.new(table)
    end

    def from_table(table)
      Query.new(table)
    end

    def compile_query_value(query, schema = nil, dialect = 'mysql')
      compile_query(
        carbon_codegen_payload_json(query),
        carbon_codegen_schema_json(schema),
        dialect
      )
    end

    def adapt_compile_result(result)
      result.merge(
        'params' => carbon_codegen_decode_json_field(result, 'params_json'),
        'diagnostics' => carbon_codegen_decode_json_field(result, 'diagnostics_json')
      )
    end

    def compile_query_result(query, schema = nil, dialect = 'mysql')
      adapt_compile_result(compile_query_value(query, schema, dialect))
    end

    def schema_models(schema = nil, module_name: 'CarbonModels')
      metadata = JSON.parse(schema_metadata(carbon_codegen_schema_json(schema)))
      module_parts = carbon_codegen_module_parts(module_name)
      used_classes = {}
      lines = []

      module_parts.each_with_index do |part, index|
        lines << "#{'  ' * index}module #{part}"
      end

      base_indent = '  ' * module_parts.length
      metadata.fetch('tables', []).each do |table|
        class_name = carbon_codegen_class_name(table.fetch('name', ''), used_classes)
        used_fields = {}
        fields = table.fetch('columns', []).map do |column|
          field = carbon_codegen_field_name(column.fetch('name', ''), used_fields)
          [field, column.fetch('name', ''), column.fetch('qualified', ''), column]
        end

        if fields.empty?
          lines << "#{base_indent}#{class_name} = Class.new do"
        else
          field_list = fields.map { |field, _, _| ":#{field}" }.join(', ')
          lines << "#{base_indent}#{class_name} = Struct.new(#{field_list}, keyword_init: true) do"
        end
        lines << "#{base_indent}  TABLE = #{JSON.generate(table.fetch('name', ''))}"
        lines << "#{base_indent}  PRIMARY = #{JSON.generate(table.fetch('primary', []))}.freeze"
        lines << "#{base_indent}  COLUMNS = {"
        fields.each do |field, _, qualified, _|
          lines << "#{base_indent}    #{JSON.generate(field)} => #{JSON.generate(qualified)},"
        end
        lines << "#{base_indent}  }.freeze"
        lines << "#{base_indent}  COLUMN_NAMES = {"
        fields.each do |field, original, _, _|
          lines << "#{base_indent}    #{JSON.generate(field)} => #{JSON.generate(original)},"
        end
        lines << "#{base_indent}  }.freeze"
        lines << "#{base_indent}  TYPES = {"
        fields.each do |field, _, _, column|
          next unless column.key?('db_type')

          lines << "#{base_indent}    #{JSON.generate(field)} => :#{carbon_codegen_ruby_type(column)},"
        end
        lines << "#{base_indent}  }.freeze"
        lines << "#{base_indent}  NULLABLE = {"
        fields.each do |field, _, _, column|
          next unless column.key?('nullable')

          lines << "#{base_indent}    #{JSON.generate(field)} => #{column.fetch('nullable') ? 'true' : 'false'},"
        end
        lines << "#{base_indent}  }.freeze"
        lines << "#{base_indent}end"
        lines << ''
      end

      (module_parts.length - 1).downto(0) do |index|
        lines << "#{'  ' * index}end"
      end

      "#{lines.join("\n").rstrip}\n"
    end

    private

    def carbon_codegen_schema_json(schema)
      return '{}' if schema.nil?
      return schema if schema.is_a?(String)

      JSON.generate(schema)
    end

    def carbon_codegen_payload_json(payload)
      return payload if payload.is_a?(String)

      JSON.generate(payload)
    end

    def carbon_codegen_decode_json_field(result, field)
      value = result.fetch(field)
      raise TypeError, "#{field} must be a JSON string" unless value.is_a?(String)

      JSON.parse(value)
    end

    def carbon_codegen_module_parts(module_name)
      name = module_name.to_s
      unless name.match?(/\A[A-Z][A-Za-z0-9_]*(?:::[A-Z][A-Za-z0-9_]*)*\z/)
        raise ArgumentError, 'module_name must be a valid Ruby constant path'
      end
      name.split('::')
    end

    def carbon_codegen_dedupe(name, used)
      candidate = name
      index = 2
      while used.key?(candidate)
        candidate = "#{name}#{index}"
        index += 1
      end
      used[candidate] = true
      candidate
    end

    def carbon_codegen_class_name(table_name, used)
      name = table_name.to_s.split(/[^0-9A-Za-z]+/).reject(&:empty?).map do |part|
        part[0].upcase + part[1, part.length].to_s
      end.join
      name = 'CarbonModel' if name.empty?
      name = "Carbon#{name}" if name.match?(/\A[0-9]/)
      carbon_codegen_dedupe(name, used)
    end

    def carbon_codegen_field_name(column_name, used)
      name = column_name.to_s.gsub(/[^0-9A-Za-z_]/, '_').gsub(/\A_+|_+\z/, '')
      name = 'field' if name.empty?
      name = "_#{name}" if name.match?(/\A[0-9]/)
      name = "#{name}_" if %w[class module def end].include?(name)
      carbon_codegen_dedupe(name, used)
    end

    def carbon_codegen_ruby_type(column)
      type = column.fetch('db_type', '').to_s.strip.downcase.split('(', 2).first
      case type
      when 'tinyint', 'smallint', 'mediumint', 'int', 'integer', 'bigint', 'year'
        'integer'
      when 'decimal', 'dec', 'numeric', 'float', 'double', 'real'
        'float'
      when 'boolean', 'bool'
        'boolean'
      when 'binary', 'varbinary', 'blob', 'tinyblob', 'mediumblob', 'longblob'
        'binary'
      when 'json', 'geometry', 'point', 'polygon', 'multipoint', 'multilinestring', 'multipolygon', 'geometrycollection'
        'object'
      else
        type.empty? ? 'any' : 'string'
      end
    end
  end
end
