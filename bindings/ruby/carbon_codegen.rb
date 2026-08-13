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

    def where_op(column, operator, value)
      where_payload[column] = CarbonC.op(operator, value)
      self
    end

    def where_in(column, values)
      where_payload[column] = CarbonC.in_list(values)
      self
    end

    def where_not_in(column, values)
      where_payload[column] = CarbonC.not_in_list(values)
      self
    end

    def where_between(column, start_value, end_value)
      where_payload[column] = CarbonC.between(start_value, end_value)
      self
    end

    def where_not_between(column, start_value, end_value)
      where_payload[column] = CarbonC.not_between(start_value, end_value)
      self
    end

    def where_match_against(column, search_value, mode = nil)
      where_payload[column] = CarbonC.match_against(search_value, mode)
      self
    end

    def where_exists(outer_column, subquery, inner_column = nil)
      append_exists('EXISTS', CarbonC.exists_spec(outer_column, subquery, inner_column))
    end

    def where_not_exists(outer_column, subquery, inner_column = nil)
      append_exists('NOT_EXISTS', CarbonC.exists_spec(outer_column, subquery, inner_column))
    end

    def where_group(operator, *conditions)
      append_boolean_group(operator, conditions)
    end

    def where_and(*conditions)
      where_group('AND', *conditions)
    end

    def where_or(*conditions)
      where_group('OR', *conditions)
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

    def join_subselect(kind, alias_name, subquery, on)
      join(kind, CarbonC.derived_target(alias_name, subquery), on)
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

    def insert(values)
      @payload['INSERT'] = copy_payload_value(values)
      self
    end

    def replace(values)
      @payload['REPLACE'] = copy_payload_value(values)
      self
    end

    def update(values)
      @payload['UPDATE'] = values.dup
      self
    end

    def delete(enabled = true)
      @payload['DELETE'] = enabled
      self
    end

    def upsert(columns)
      @payload['UPDATE'] = columns.dup
      self
    end

    def do_nothing
      @payload['UPDATE'] = []
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

    def where_payload
      @payload['WHERE'] ||= {}
      raise TypeError, 'WHERE must be a Hash' unless @payload['WHERE'].is_a?(Hash)

      @payload['WHERE']
    end

    def append_exists(operator, spec)
      where_payload[operator] ||= []
      raise TypeError, "WHERE.#{operator} must be an Array" unless where_payload[operator].is_a?(Array)

      where_payload[operator] << spec
      self
    end

    def append_boolean_group(operator, conditions)
      operator_key = operator.to_s.upcase.gsub(/\s+/, '_')
      raise ArgumentError, 'operator must be AND or OR' unless %w[AND OR].include?(operator_key)

      where_payload[operator_key] ||= []
      raise TypeError, "WHERE.#{operator_key} must be an Array" unless where_payload[operator_key].is_a?(Array)

      where_payload[operator_key].concat(conditions.map { |condition| copy_payload_value(condition) })
      self
    end

    def copy_payload_value(value)
      case value
      when Array
        value.map { |item| item.is_a?(Hash) ? item.dup : item }
      when Hash
        value.dup
      else
        value
      end
    end
  end

  class << self
    def query(table = nil)
      Query.new(table)
    end

    def from_table(table)
      Query.new(table)
    end

    def subselect(query)
      ['SUBSELECT', carbon_codegen_query_payload(query)]
    end

    def derived_target(alias_name, query)
      JSON.generate('SUBSELECT' => carbon_codegen_query_payload(query), 'AS' => alias_name)
    end

    def op(operator, *operands)
      [operator, *operands.map { |operand| carbon_codegen_query_payload(operand) }]
    end

    def lit(value)
      ['LIT', value]
    end

    def param(value)
      ['PARAM', value]
    end

    def call(name, *arguments)
      fn(name, *arguments)
    end

    def fn(name, *arguments)
      [name, *arguments.map { |argument| carbon_codegen_query_payload(argument) }]
    end

    def custom_call(name, *arguments)
      ['CALL', name, *arguments.map { |argument| carbon_codegen_query_payload(argument) }]
    end

    def alias_expression(expression, alias_name)
      ['AS', carbon_codegen_query_payload(expression), alias_name]
    end

    def distinct(expression)
      ['DISTINCT', carbon_codegen_query_payload(expression)]
    end

    def between(start_value, end_value)
      ['BETWEEN', [carbon_codegen_query_payload(start_value), carbon_codegen_query_payload(end_value)]]
    end

    def not_between(start_value, end_value)
      ['NOT BETWEEN', [carbon_codegen_query_payload(start_value), carbon_codegen_query_payload(end_value)]]
    end

    def in_list(values)
      ['IN', carbon_codegen_set_operand(values)]
    end

    def not_in_list(values)
      ['NOT_IN', carbon_codegen_set_operand(values)]
    end

    def match_against(search_value, mode = nil)
      payload = [carbon_codegen_match_search_operand(search_value)]
      payload << mode unless mode.nil?
      ['MATCH_AGAINST', payload]
    end

    def exists_spec(outer_column, query, inner_column = nil)
      spec = [outer_column, carbon_codegen_subselect_operand(query)]
      spec << inner_column unless inner_column.nil?
      spec
    end

    def exists(*specs)
      {'EXISTS' => specs.map { |spec| carbon_codegen_query_payload(spec) }}
    end

    def not_exists(*specs)
      {'NOT_EXISTS' => specs.map { |spec| carbon_codegen_query_payload(spec) }}
    end

    def condition(column, value)
      {column => carbon_codegen_query_payload(value)}
    end

    def group(operator, *conditions)
      operator_key = operator.to_s.upcase.gsub(/\s+/, '_')
      raise ArgumentError, 'operator must be AND or OR' unless %w[AND OR].include?(operator_key)

      {operator_key => conditions.map { |condition| carbon_codegen_query_payload(condition) }}
    end

    def and_group(*conditions)
      group('AND', *conditions)
    end

    def or_group(*conditions)
      group('OR', *conditions)
    end

    def model_table(model)
      table = if model.is_a?(Hash)
                model['table'] || model[:table] || model['TABLE'] || model[:TABLE]
              else
                model_class = model.is_a?(Class) ? model : model.class
                model_class.const_defined?(:TABLE, false) ? model_class.const_get(:TABLE) : nil
              end
      raise ArgumentError, 'model must provide a Carbon table name' unless table.is_a?(String) && !table.empty?

      table
    end

    def model_columns(model)
      columns = if model.is_a?(Hash)
                  model['columns'] || model[:columns] || model['COLUMNS'] || model[:COLUMNS]
                else
                  model_class = model.is_a?(Class) ? model : model.class
                  model_class.const_defined?(:COLUMNS, false) ? model_class.const_get(:COLUMNS) : nil
                end
      raise ArgumentError, 'model must provide Carbon columns' unless columns.is_a?(Hash)

      columns.transform_keys(&:to_s).transform_values(&:to_s)
    end

    def model_column(model, field)
      columns = model_columns(model)
      key = field.to_s
      raise KeyError, "unknown model field: #{key}" unless columns.key?(key)

      columns.fetch(key)
    end

    def model_query(model)
      query(model_table(model))
    end

    def model_select(model, *fields)
      columns = model_columns(model)
      selected = fields.empty? ? columns.values : fields.map { |field| model_column(model, field) }
      model_query(model).select(selected)
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
          lines << "#{base_indent}#{class_name} = Class.new"
        else
          field_list = fields.map { |field, _, _| ":#{field}" }.join(', ')
          lines << "#{base_indent}#{class_name} = Struct.new(#{field_list}, keyword_init: true)"
        end
        lines << "#{base_indent}#{class_name}::TABLE = #{JSON.generate(table.fetch('name', ''))}"
        lines << "#{base_indent}#{class_name}::PRIMARY = #{JSON.generate(table.fetch('primary', []))}.freeze"
        lines << "#{base_indent}#{class_name}::COLUMNS = {"
        fields.each do |field, _, qualified, _|
          lines << "#{base_indent}  #{JSON.generate(field)} => #{JSON.generate(qualified)},"
        end
        lines << "#{base_indent}}.freeze"
        lines << "#{base_indent}#{class_name}::COLUMN_NAMES = {"
        fields.each do |field, original, _, _|
          lines << "#{base_indent}  #{JSON.generate(field)} => #{JSON.generate(original)},"
        end
        lines << "#{base_indent}}.freeze"
        lines << "#{base_indent}#{class_name}::TYPES = {"
        fields.each do |field, _, _, column|
          next unless column.key?('db_type')

          lines << "#{base_indent}  #{JSON.generate(field)} => :#{carbon_codegen_ruby_type(column)},"
        end
        lines << "#{base_indent}}.freeze"
        lines << "#{base_indent}#{class_name}::NULLABLE = {"
        fields.each do |field, _, _, column|
          next unless column.key?('nullable')

          lines << "#{base_indent}  #{JSON.generate(field)} => #{column.fetch('nullable') ? 'true' : 'false'},"
        end
        lines << "#{base_indent}}.freeze"
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

    def carbon_codegen_query_payload(query)
      return query.to_payload if query.is_a?(Query)

      case query
      when Array
        query.map { |item| item.is_a?(Hash) ? item.dup : item }
      when Hash
        query.dup
      else
        query
      end
    end

    def carbon_codegen_subselect_operand(query)
      if query.is_a?(Array) && query.length == 2 && query.first.to_s.upcase == 'SUBSELECT'
        return carbon_codegen_query_payload(query)
      end
      if query.is_a?(Hash) && (query.key?('SUBSELECT') || query.key?('subselect'))
        return query.dup
      end

      subselect(query)
    end

    def carbon_codegen_set_operand(values)
      return subselect(values) if values.is_a?(Query)

      carbon_codegen_query_payload(values)
    end

    def carbon_codegen_match_search_operand(search_value)
      search_value.is_a?(String) ? lit(search_value) : carbon_codegen_query_payload(search_value)
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
