# frozen_string_literal: true

require 'json'
require_relative 'carbon'

module CarbonC
  class << self
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
          [field, column.fetch('name', ''), column.fetch('qualified', '')]
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
        fields.each do |field, _, qualified|
          lines << "#{base_indent}    #{JSON.generate(field)} => #{JSON.generate(qualified)},"
        end
        lines << "#{base_indent}  }.freeze"
        lines << "#{base_indent}  COLUMN_NAMES = {"
        fields.each do |field, original, _|
          lines << "#{base_indent}    #{JSON.generate(field)} => #{JSON.generate(original)},"
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
  end
end
