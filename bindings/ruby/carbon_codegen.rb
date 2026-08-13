# frozen_string_literal: true

require 'json'
require_relative 'carbon'

module CarbonC
  module C6C
    ADDDATE = 'ADDDATE'
    ADDTIME = 'ADDTIME'
    AS = 'AS'
    ASC = 'ASC'
    AND = 'AND'
    BETWEEN = 'BETWEEN'
    CALL = 'CALL'
    CONCAT = 'CONCAT'
    COUNT = 'COUNT'
    COUNT_ALL = 'COUNT_ALL'
    CURRENT_DATE = 'CURRENT_DATE'
    CURRENT_TIMESTAMP = 'CURRENT_TIMESTAMP'
    DATE = 'DATE'
    DATE_ADD = 'DATE_ADD'
    DATE_FORMAT = 'DATE_FORMAT'
    DATE_SUB = 'DATE_SUB'
    DATEDIFF = 'DATEDIFF'
    DELETE = 'DELETE'
    DESC = 'DESC'
    DISTINCT = 'DISTINCT'
    EXISTS = 'EXISTS'
    FALSE = 'FALSE'
    FORCE_INDEX = 'FORCE INDEX'
    FROM = 'FROM'
    GREATER_THAN = '>'
    GREATER_THAN_OR_EQUAL_TO = '>='
    GROUP_BY = 'GROUP_BY'
    GROUP_CONCAT = 'GROUP_CONCAT'
    HAVING = 'HAVING'
    IGNORE_INDEX = 'IGNORE INDEX'
    IN = 'IN'
    INDEX_HINTS = 'INDEX_HINTS'
    INNER = 'INNER'
    INSERT = 'INSERT'
    IS = 'IS'
    IS_NOT = 'IS_NOT'
    JOIN = 'JOIN'
    LEFT = 'LEFT'
    LEFT_OUTER = 'LEFT_OUTER'
    LESS_THAN = '<'
    LESS_THAN_OR_EQUAL_TO = '<='
    LIKE = 'LIKE'
    LIMIT = 'LIMIT'
    LIT = 'LIT'
    MATCH_AGAINST = 'MATCH_AGAINST'
    MBRCONTAINS = 'MBRContains'
    MIN = 'MIN'
    MAX = 'MAX'
    NOT_BETWEEN = 'NOT BETWEEN'
    NOT_EQUAL = '<>'
    NOT_EXISTS = 'NOT_EXISTS'
    NOT_IN = 'NOT_IN'
    NOT_LIKE = 'NOT_LIKE'
    NULL = 'NULL'
    OR = 'OR'
    ORDER = 'ORDER'
    PAGE = 'PAGE'
    PAGINATION = 'PAGINATION'
    PARAM = 'PARAM'
    REPLACE = 'REPLACE'
    RIGHT = 'RIGHT'
    RIGHT_OUTER = 'RIGHT_OUTER'
    SELECT = 'SELECT'
    ST_CONTAINS = 'ST_Contains'
    ST_GEOMFROMTEXT = 'ST_GeomFromText'
    ST_WITHIN = 'ST_Within'
    SUBSELECT = 'SUBSELECT'
    SUM = 'SUM'
    UPDATE = 'UPDATE'
    USE_INDEX = 'USE INDEX'
    WHERE = 'WHERE'
  end

  C6 = C6C

  module Dialect
    MYSQL = 'mysql'
    POSTGRESQL = 'postgresql'
    POSTGRES = 'postgres'
  end

  module ExecutionTarget
    AUTO = 'auto'
    LOCAL = 'local'
    SERVER = 'server'
    REMOTE = SERVER
  end

  class Query
    def initialize(table = nil)
      @payload = {}
      @payload[C6C::FROM] = table unless table.nil?
    end

    def from_table(table)
      @payload[C6C::FROM] = table
      self
    end

    def select(*columns)
      @payload[C6C::SELECT] = columns.length == 1 && columns.first.is_a?(Array) ? columns.first.dup : columns
      self
    end

    def where(conditions)
      @payload[C6C::WHERE] = conditions.dup
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
      append_exists(C6C::EXISTS, CarbonC.exists_spec(outer_column, subquery, inner_column))
    end

    def where_not_exists(outer_column, subquery, inner_column = nil)
      append_exists(C6C::NOT_EXISTS, CarbonC.exists_spec(outer_column, subquery, inner_column))
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
      @payload[C6C::JOIN] ||= {}
      raise TypeError, 'JOIN must be a Hash' unless @payload[C6C::JOIN].is_a?(Hash)

      @payload[C6C::JOIN][kind] ||= {}
      unless @payload[C6C::JOIN][kind].is_a?(Hash)
        raise TypeError, "JOIN.#{kind} must be a Hash"
      end

      @payload[C6C::JOIN][kind][target] = on.dup
      self
    end

    def join_subselect(kind, alias_name, subquery, on)
      join(kind, CarbonC.derived_target(alias_name, subquery), on)
    end

    def index_hints(hints)
      @payload[C6C::INDEX_HINTS] = copy_payload_value(hints)
      self
    end

    def force_index(*indexes)
      @payload[C6C::INDEX_HINTS] = CarbonC.force_index(*indexes)
      self
    end

    def use_index(*indexes)
      @payload[C6C::INDEX_HINTS] = CarbonC.use_index(*indexes)
      self
    end

    def ignore_index(*indexes)
      @payload[C6C::INDEX_HINTS] = CarbonC.ignore_index(*indexes)
      self
    end

    def group_by(*expressions)
      @payload[C6C::GROUP_BY] = if expressions.length == 1 && expressions.first.is_a?(Array)
                                  expressions.first.dup
                                elsif expressions.length == 1
                                  expressions.first
                                else
                                  expressions
                                end
      self
    end

    def having(conditions)
      @payload[C6C::HAVING] = conditions.dup
      self
    end

    def insert(values)
      @payload[C6C::INSERT] = copy_payload_value(values)
      self
    end

    def replace(values)
      @payload[C6C::REPLACE] = copy_payload_value(values)
      self
    end

    def update(values)
      @payload[C6C::UPDATE] = values.dup
      self
    end

    def delete(enabled = true)
      @payload[C6C::DELETE] = enabled
      self
    end

    def upsert(columns)
      @payload[C6C::UPDATE] = columns.dup
      self
    end

    def do_nothing
      @payload[C6C::UPDATE] = []
      self
    end

    def limit(value)
      pagination[C6C::LIMIT] = value
      self
    end

    def page(value)
      pagination[C6C::PAGE] = value
      self
    end

    def order_by(column, direction = C6C::ASC)
      pagination[C6C::ORDER] ||= []
      pagination[C6C::ORDER] << [column, direction]
      self
    end

    def to_payload
      JSON.parse(JSON.generate(@payload))
    end

    def compile(schema = nil, dialect = Dialect::MYSQL)
      CarbonC.compile_query_result(@payload, schema, dialect)
    end

    private

    def pagination
      @payload[C6C::PAGINATION] ||= {}
      raise TypeError, 'PAGINATION must be a Hash' unless @payload[C6C::PAGINATION].is_a?(Hash)

      @payload[C6C::PAGINATION]
    end

    def where_payload
      @payload[C6C::WHERE] ||= {}
      raise TypeError, 'WHERE must be a Hash' unless @payload[C6C::WHERE].is_a?(Hash)

      @payload[C6C::WHERE]
    end

    def append_exists(operator, spec)
      where_payload[operator] ||= []
      raise TypeError, "WHERE.#{operator} must be an Array" unless where_payload[operator].is_a?(Array)

      where_payload[operator] << spec
      self
    end

    def append_boolean_group(operator, conditions)
      operator_key = operator.to_s.upcase.gsub(/\s+/, '_')
      raise ArgumentError, 'operator must be AND or OR' unless [C6C::AND, C6C::OR].include?(operator_key)

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
      [C6C::SUBSELECT, carbon_codegen_query_payload(query)]
    end

    def derived_target(alias_name, query)
      JSON.generate(C6C::SUBSELECT => carbon_codegen_query_payload(query), C6C::AS => alias_name)
    end

    def op(operator, *operands)
      [operator, *operands.map { |operand| carbon_codegen_query_payload(operand) }]
    end

    def lit(value)
      [C6C::LIT, value]
    end

    def param(value)
      [C6C::PARAM, value]
    end

    def call(name, *arguments)
      fn(name, *arguments)
    end

    def fn(name, *arguments)
      [name, *arguments.map { |argument| carbon_codegen_query_payload(argument) }]
    end

    def custom_call(name, *arguments)
      [C6C::CALL, name, *arguments.map { |argument| carbon_codegen_query_payload(argument) }]
    end

    def st_contains(envelope, shape)
      fn(C6C::ST_CONTAINS, envelope, shape)
    end

    def st_within(shape, envelope)
      fn(C6C::ST_WITHIN, shape, envelope)
    end

    def mbr_contains(envelope, shape)
      fn(C6C::MBRCONTAINS, envelope, shape)
    end

    def alias_expression(expression, alias_name)
      [C6C::AS, carbon_codegen_query_payload(expression), alias_name]
    end

    def distinct(expression)
      [C6C::DISTINCT, carbon_codegen_query_payload(expression)]
    end

    def between(start_value, end_value)
      [C6C::BETWEEN, [carbon_codegen_query_payload(start_value), carbon_codegen_query_payload(end_value)]]
    end

    def not_between(start_value, end_value)
      [C6C::NOT_BETWEEN, [carbon_codegen_query_payload(start_value), carbon_codegen_query_payload(end_value)]]
    end

    def in_list(values)
      [C6C::IN, carbon_codegen_set_operand(values)]
    end

    def not_in_list(values)
      [C6C::NOT_IN, carbon_codegen_set_operand(values)]
    end

    def match_against(search_value, mode = nil)
      payload = [carbon_codegen_match_search_operand(search_value)]
      payload << mode unless mode.nil?
      [C6C::MATCH_AGAINST, payload]
    end

    def index_hint(kind, *indexes)
      values = indexes.length == 1 && indexes.first.is_a?(Array) ? indexes.first.dup : indexes
      {kind => values}
    end

    def force_index(*indexes)
      index_hint(C6C::FORCE_INDEX, *indexes)
    end

    def use_index(*indexes)
      index_hint(C6C::USE_INDEX, *indexes)
    end

    def ignore_index(*indexes)
      index_hint(C6C::IGNORE_INDEX, *indexes)
    end

    def exists_spec(outer_column, query, inner_column = nil)
      spec = [outer_column, carbon_codegen_subselect_operand(query)]
      spec << inner_column unless inner_column.nil?
      spec
    end

    def exists(*specs)
      {C6C::EXISTS => specs.map { |spec| carbon_codegen_query_payload(spec) }}
    end

    def not_exists(*specs)
      {C6C::NOT_EXISTS => specs.map { |spec| carbon_codegen_query_payload(spec) }}
    end

    def condition(column, value)
      {column => carbon_codegen_query_payload(value)}
    end

    def group(operator, *conditions)
      operator_key = operator.to_s.upcase.gsub(/\s+/, '_')
      raise ArgumentError, 'operator must be AND or OR' unless [C6C::AND, C6C::OR].include?(operator_key)

      {operator_key => conditions.map { |condition| carbon_codegen_query_payload(condition) }}
    end

    def and_group(*conditions)
      group(C6C::AND, *conditions)
    end

    def or_group(*conditions)
      group(C6C::OR, *conditions)
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

    def model_values(model, values)
      raise TypeError, 'model values must be a Hash' unless values.is_a?(Hash)

      values.each_with_object({}) do |(field, value), mapped|
        mapped[model_column(model, field)] = carbon_codegen_query_payload(value)
      end
    end

    def model_insert(model, values)
      model_query(model).insert(model_values(model, values))
    end

    def model_replace(model, values)
      model_query(model).replace(model_values(model, values))
    end

    def model_update(model, values)
      model_query(model).update(model_values(model, values))
    end

    def model_upsert(model, values, fields)
      model_insert(model, values).upsert(fields)
    end

    def model_do_nothing(model, values)
      model_insert(model, values).do_nothing
    end

    def model_get_payload(model, query_payload = nil)
      table = model_table(model)
      payload = carbon_codegen_mapping_payload(query_payload, 'query')
      from_value = carbon_codegen_first_present(payload, C6C::FROM, 'from', 'table')
      unless from_value.nil? || from_value.to_s == table
        raise ArgumentError, "query FROM/table #{from_value.inspect} does not match model table #{table.inspect}"
      end

      payload[C6C::FROM] = table
      payload
    end

    def model_get_request(
      model,
      query_payload = nil,
      schema: nil,
      dialect: Dialect::MYSQL,
      context: nil,
      policy: nil
    )
      table = model_table(model)
      request = query_execution_request(
        model_get_payload(model, query_payload),
        schema: schema,
        dialect: dialect,
        context: context,
        policy: policy
      )
      request['method'] = 'Get'
      request['model'] = table
      request
    end

    def route_query(query_payload, context: nil, policy: nil)
      payload = carbon_codegen_mapping_payload(query_payload, 'query')
      runtime_context = carbon_codegen_mapping_payload(context, 'context')
      route_policy = carbon_codegen_mapping_payload(policy, 'policy')
      requested_target = carbon_codegen_normal_target(
        carbon_codegen_first_present(route_policy, 'target', 'prefer', 'executionTarget', 'execution_target')
      )
      return {'target' => ExecutionTarget::SERVER, 'reason' => 'forced_server'} if requested_target == ExecutionTarget::SERVER
      return {'target' => ExecutionTarget::LOCAL, 'reason' => 'forced_local'} if requested_target == ExecutionTarget::LOCAL

      can_run_local = carbon_codegen_first_present(
        runtime_context,
        'canRunLocal',
        'can_run_local',
        'localAvailable',
        'local_available'
      )
      unless can_run_local.nil? || carbon_codegen_truthy(can_run_local)
        return {'target' => ExecutionTarget::SERVER, 'reason' => 'local_unavailable'}
      end

      offload_mobile = carbon_codegen_first_present(
        route_policy,
        'serverOnMobile',
        'server_on_mobile',
        'remoteOnMobile',
        'remote_on_mobile'
      )
      if carbon_codegen_truthy(offload_mobile) && carbon_codegen_mobile_context?(runtime_context)
        return {'target' => ExecutionTarget::SERVER, 'reason' => 'mobile_offload'}
      end

      limit = carbon_codegen_query_limit(payload)
      max_limit = carbon_codegen_numeric(
        carbon_codegen_first_present(route_policy, 'maxLocalLimit', 'max_local_limit', 'maxClientLimit', 'max_client_limit')
      )
      if !limit.nil? && !max_limit.nil? && limit > max_limit
        return {'target' => ExecutionTarget::SERVER, 'reason' => 'limit'}
      end

      estimated_rows = carbon_codegen_numeric(carbon_codegen_first_present(route_policy, 'estimatedRows', 'estimated_rows'))
      estimated_rows = carbon_codegen_numeric(carbon_codegen_first_present(runtime_context, 'estimatedRows', 'estimated_rows')) if estimated_rows.nil?
      max_rows = carbon_codegen_numeric(
        carbon_codegen_first_present(route_policy, 'maxLocalRows', 'max_local_rows', 'maxClientRows', 'max_client_rows')
      )
      if !estimated_rows.nil? && !max_rows.nil? && estimated_rows > max_rows
        return {'target' => ExecutionTarget::SERVER, 'reason' => 'estimated_rows'}
      end

      estimated_cost = carbon_codegen_numeric(carbon_codegen_first_present(route_policy, 'estimatedCost', 'estimated_cost'))
      estimated_cost = carbon_codegen_numeric(carbon_codegen_first_present(runtime_context, 'estimatedCost', 'estimated_cost')) if estimated_cost.nil?
      max_cost = carbon_codegen_numeric(
        carbon_codegen_first_present(route_policy, 'maxLocalCost', 'max_local_cost', 'maxClientCost', 'max_client_cost')
      )
      if !estimated_cost.nil? && !max_cost.nil? && estimated_cost > max_cost
        return {'target' => ExecutionTarget::SERVER, 'reason' => 'estimated_cost'}
      end

      {'target' => ExecutionTarget::LOCAL, 'reason' => 'local'}
    end

    def query_execution_request(
      query_payload,
      schema: nil,
      dialect: Dialect::MYSQL,
      context: nil,
      policy: nil
    )
      payload = carbon_codegen_mapping_payload(query_payload, 'query')
      request = {
        'query' => payload,
        'dialect' => dialect,
        'route' => route_query(payload, context: context, policy: policy)
      }
      request['schema'] = carbon_codegen_query_payload(schema) unless schema.nil?
      cache_results = carbon_codegen_first_present(payload, 'cacheResults', 'cache_results')
      request['cacheResults'] = cache_results unless cache_results.nil?
      request
    end

    def compile_query_value(query, schema = nil, dialect = Dialect::MYSQL)
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

    def compile_query_result(query, schema = nil, dialect = Dialect::MYSQL)
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
        used_constants = {
          'TABLE' => true,
          'PRIMARY' => true,
          'FIELDS' => true,
          'COLUMNS' => true,
          'COLUMN_NAMES' => true,
          'TYPES' => true,
          'NULLABLE' => true
        }
        fields = table.fetch('columns', []).map do |column|
          field = carbon_codegen_field_name(column.fetch('name', ''), used_fields)
          field_constant = carbon_codegen_constant_name("FIELD_#{field}", used_constants)
          column_constant = carbon_codegen_constant_name(field, used_constants)
          [field, column.fetch('name', ''), column.fetch('qualified', ''), column, column_constant, field_constant]
        end

        if fields.empty?
          lines << "#{base_indent}#{class_name} = Class.new"
        else
          field_list = fields.map { |field, _, _, _, _, _| ":#{field}" }.join(', ')
          lines << "#{base_indent}#{class_name} = Struct.new(#{field_list}, keyword_init: true)"
        end
        lines << "#{base_indent}#{class_name}::TABLE = #{JSON.generate(table.fetch('name', ''))}"
        lines << "#{base_indent}#{class_name}::PRIMARY = #{JSON.generate(table.fetch('primary', []))}.freeze"
        fields.each do |_, original, _, _, _, field_constant|
          lines << "#{base_indent}#{class_name}::#{field_constant} = #{JSON.generate(original)}"
        end
        field_constants = fields.map { |_, _, _, _, _, field_constant| "#{class_name}::#{field_constant}" }
        lines << "#{base_indent}#{class_name}::FIELDS = [#{field_constants.join(', ')}].freeze"
        fields.each do |_, _, qualified, _, column_constant, _|
          lines << "#{base_indent}#{class_name}::#{column_constant} = #{JSON.generate(qualified)}"
        end
        lines << "#{base_indent}#{class_name}::COLUMNS = {"
        fields.each do |_, _, _, _, column_constant, field_constant|
          lines << "#{base_indent}  #{class_name}::#{field_constant} => #{class_name}::#{column_constant},"
        end
        lines << "#{base_indent}}.freeze"
        lines << "#{base_indent}#{class_name}::COLUMN_NAMES = {"
        fields.each do |_, original, _, _, _, field_constant|
          lines << "#{base_indent}  #{class_name}::#{field_constant} => #{JSON.generate(original)},"
        end
        lines << "#{base_indent}}.freeze"
        lines << "#{base_indent}#{class_name}::TYPES = {"
        fields.each do |field, _, _, column, _, _|
          next unless column.key?('db_type')

          lines << "#{base_indent}  #{JSON.generate(field)} => :#{carbon_codegen_ruby_type(column)},"
        end
        lines << "#{base_indent}}.freeze"
        lines << "#{base_indent}#{class_name}::NULLABLE = {"
        fields.each do |field, _, _, column, _, _|
          next unless column.key?('nullable')

          lines << "#{base_indent}  #{JSON.generate(field)} => #{column.fetch('nullable') ? 'true' : 'false'},"
        end
        lines << "#{base_indent}}.freeze"
        lines << "#{base_indent}def #{class_name}.GetPayload(query_payload = nil)"
        lines << "#{base_indent}  CarbonC.model_get_payload(self, query_payload)"
        lines << "#{base_indent}end"
        lines << "#{base_indent}def #{class_name}.Get(query_payload = nil, **options)"
        lines << "#{base_indent}  CarbonC.model_get_request(self, query_payload, **options)"
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

    def carbon_codegen_mapping_payload(value, name)
      return {} if value.nil?

      payload = carbon_codegen_query_payload(value)
      raise TypeError, "#{name} must be a Hash" unless payload.is_a?(Hash)

      payload.dup
    end

    def carbon_codegen_first_present(mapping, *keys)
      keys.each do |key|
        return mapping[key] if mapping.key?(key)
      end
      nil
    end

    def carbon_codegen_normal_target(value)
      return ExecutionTarget::AUTO if value.nil?

      target = value.to_s.strip.downcase.tr('_', '-')
      return ExecutionTarget::AUTO if target.empty? || target == 'auto'
      return ExecutionTarget::LOCAL if %w[local client].include?(target)
      return ExecutionTarget::SERVER if %w[server remote].include?(target)

      raise ArgumentError, 'target must be auto, local, client, server, or remote'
    end

    def carbon_codegen_truthy(value)
      return value if value == true || value == false
      return false if value.nil?
      return %w[1 true yes y on].include?(value.strip.downcase) if value.is_a?(String)

      !!value
    end

    def carbon_codegen_numeric(value)
      return nil if value.nil? || value == true || value == false

      Float(value)
    rescue ArgumentError, TypeError
      nil
    end

    def carbon_codegen_mobile_context?(context)
      mobile = carbon_codegen_first_present(context, 'isMobile', 'is_mobile', 'mobile')
      return carbon_codegen_truthy(mobile) unless mobile.nil?

      device = carbon_codegen_first_present(context, 'deviceClass', 'device_class', 'platform', 'runtime')
      !device.nil? && %w[mobile phone tablet ios android].include?(device.to_s.strip.downcase)
    end

    def carbon_codegen_query_limit(payload)
      pagination = carbon_codegen_first_present(payload, C6C::PAGINATION, 'pagination')
      if pagination.is_a?(Hash)
        limit = carbon_codegen_numeric(carbon_codegen_first_present(pagination, C6C::LIMIT, 'limit'))
        return limit unless limit.nil?
      end
      carbon_codegen_numeric(carbon_codegen_first_present(payload, C6C::LIMIT, 'limit'))
    end

    def carbon_codegen_subselect_operand(query)
      if query.is_a?(Array) && query.length == 2 && query.first.to_s.upcase == C6C::SUBSELECT
        return carbon_codegen_query_payload(query)
      end
      if query.is_a?(Hash) && (query.key?(C6C::SUBSELECT) || query.key?('subselect'))
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

    def carbon_codegen_constant_name(field_name, used)
      name = field_name.to_s.gsub(/[^0-9A-Za-z_]/, '_').gsub(/\A_+|_+\z/, '').upcase
      name = 'COLUMN' if name.empty?
      name = "COLUMN_#{name}" if name.match?(/\A[0-9]/)
      name = "#{name}_COLUMN" if used.key?(name)
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
