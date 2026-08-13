# frozen_string_literal: true

require 'json'
require_relative '../../bindings/ruby/carbon_codegen'

puts CarbonC.version

schema = {
  'TABLES' => {
    'actor' => {
      'PRIMARY_SHORT' => ['actor_id'],
      'COLUMNS' => {
        'actor.actor_id' => 'actor_id',
        'actor.first_name' => 'first_name'
      },
      'TYPE_VALIDATION' => {
        'actor.actor_id' => {
          'COLUMN_NAME' => 'actor_id',
          'MYSQL_TYPE' => 'smallint',
          'MAX_LENGTH' => '',
          'AUTO_INCREMENT' => true,
          'NOT_NULL' => true,
          'SKIP_COLUMN_IN_POST' => false
        },
        'actor.first_name' => {
          'COLUMN_NAME' => 'first_name',
          'MYSQL_TYPE' => 'varchar',
          'MAX_LENGTH' => '45',
          'AUTO_INCREMENT' => false,
          'NOT_NULL' => true,
          'SKIP_COLUMN_IN_POST' => false
        }
      }
    }
  }
}

query = {
  'FROM' => 'actor',
  'SELECT' => ['actor.actor_id', 'actor.first_name'],
  'WHERE' => {'actor.actor_id' => ['>', 10]},
  'PAGINATION' => {'LIMIT' => 5}
}

result = CarbonC.compile_query_result(query, schema, 'mysql')

if result.fetch('status') != 0
  warn result.fetch('error')
  exit 1
end

puts result.fetch('sql')
puts JSON.generate(result.fetch('params'))
puts result.fetch('allowlist_key')
puts JSON.generate(result.fetch('diagnostics'))
puts CarbonC.schema_metadata(JSON.generate(schema))
puts CarbonC.schema_models(schema)
