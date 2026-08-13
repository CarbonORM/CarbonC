# frozen_string_literal: true

require 'json'
require_relative 'carbon_codegen'

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

raise 'unexpected version' unless CarbonC.version == '0.1.0'
raise 'unexpected status code' unless CarbonC.status_code(3) == 'invalid_query'
raise 'unexpected status message' unless CarbonC.status_message(0) == 'ok'

result = CarbonC.compile_query(JSON.generate(query), JSON.generate(schema), 'mysql')

raise "expected compile success: #{result.inspect}" unless result.fetch('status') == 0
raise "unexpected status code: #{result.inspect}" unless result.fetch('status_code') == 'ok'
unless result.fetch('sql') == 'SELECT actor.actor_id, actor.first_name FROM `actor` WHERE (actor.actor_id) > ? LIMIT 5'
  raise "unexpected sql: #{result.fetch('sql')}"
end
raise "unexpected params: #{result.fetch('params_json')}" unless result.fetch('params_json') == '[10]'
unless result.fetch('allowlist_key') == 'SELECT actor.actor_id, actor.first_name FROM `actor` WHERE (actor.actor_id) > ? LIMIT ?'
  raise "unexpected allowlist: #{result.fetch('allowlist_key')}"
end
diagnostics = JSON.parse(result.fetch('diagnostics_json'))
expected_diagnostics = {
  'status' => 0,
  'status_code' => 'ok',
  'ok' => true,
  'diagnostics' => []
}
raise "unexpected success diagnostics: #{diagnostics.inspect}" unless diagnostics == expected_diagnostics
ergonomic = CarbonC.compile_query_value(query, schema, 'mysql')
raise "unexpected ergonomic compile result: #{ergonomic.inspect}" unless ergonomic == result
adapted = CarbonC.adapt_compile_result(result)
expected_adapted = result.merge(
  'params' => [10],
  'diagnostics' => JSON.parse(result.fetch('diagnostics_json'))
)
raise "unexpected adapted compile result: #{adapted.inspect}" unless adapted == expected_adapted
typed = CarbonC.compile_query_result(query, schema, 'mysql')
raise "unexpected typed compile result: #{typed.inspect}" unless typed == adapted
built = CarbonC.query('actor')
                .select('actor.actor_id', 'actor.first_name')
                .where({'actor.actor_id' => ['>', 10]})
                .limit(5)
raise "unexpected builder payload: #{built.to_payload.inspect}" unless built.to_payload == query
raise 'unexpected builder compile result' unless built.compile(schema, 'mysql') == adapted
ordered_payload = CarbonC.from_table('actor')
                         .select(['actor.actor_id'])
                         .order_by('actor.first_name', 'DESC')
                         .limit(5)
                         .to_payload
expected_ordered_payload = {
  'FROM' => 'actor',
  'SELECT' => ['actor.actor_id'],
  'PAGINATION' => {'ORDER' => [['actor.first_name', 'DESC']], 'LIMIT' => 5}
}
unless ordered_payload == expected_ordered_payload
  raise "unexpected ordered builder payload: #{ordered_payload.inspect}"
end
metadata = JSON.parse(CarbonC.schema_metadata(JSON.generate(schema)))
expected_metadata = {
  'tables' => [
    {
      'name' => 'actor',
      'columns' => [
        {
          'name' => 'actor_id',
          'qualified' => 'actor.actor_id',
          'db_type' => 'smallint',
          'max_length' => '',
          'nullable' => false,
          'auto_increment' => true,
          'skip_insert' => false
        },
        {
          'name' => 'first_name',
          'qualified' => 'actor.first_name',
          'db_type' => 'varchar',
          'max_length' => '45',
          'nullable' => false,
          'auto_increment' => false,
          'skip_insert' => false
        }
      ],
      'primary' => ['actor_id']
    }
  ]
}
raise "unexpected metadata: #{metadata.inspect}" unless metadata == expected_metadata
models = CarbonC.schema_models(schema)
raise "unexpected model source: #{models}" unless models.include?('module CarbonModels')
raise "unexpected model source: #{models}" unless models.include?('Actor = Struct.new(:actor_id, :first_name, keyword_init: true)')
raise "unexpected model source: #{models}" unless models.include?('PRIMARY = ["actor_id"].freeze')
raise "unexpected model source: #{models}" unless models.include?('"actor_id" => "actor.actor_id"')
raise "unexpected model source: #{models}" unless models.include?('TYPES = {')
raise "unexpected model source: #{models}" unless models.include?('"actor_id" => :integer')
raise "unexpected model source: #{models}" unless models.include?('NULLABLE = {')
raise "unexpected model source: #{models}" unless models.include?('"actor_id" => false')

rejected = CarbonC.compile_query(
  JSON.generate({'FROM' => 'actor', 'SELECT' => ['actor.last_name']}),
  JSON.generate(schema),
  'mysql'
)
raise "expected invalid query rejection: #{rejected.inspect}" unless rejected.fetch('status') == 3
raise "unexpected rejection status code: #{rejected.inspect}" unless rejected.fetch('status_code') == 'invalid_query'
raise "unexpected rejection message: #{rejected.inspect}" unless rejected.fetch('error') == 'invalid query'

rejected_table = CarbonC.compile_query(
  JSON.generate({'FROM' => 'film', 'SELECT' => ['film.film_id']}),
  JSON.generate(schema),
  'mysql'
)
raise "expected invalid table rejection: #{rejected_table.inspect}" unless rejected_table.fetch('status') == 3
unless rejected_table.fetch('status_code') == 'invalid_query'
  raise "unexpected table rejection status code: #{rejected_table.inspect}"
end
unless rejected_table.fetch('error') == 'table is not present in schema'
  raise "unexpected table rejection message: #{rejected_table.inspect}"
end
rejected_diagnostics = JSON.parse(rejected_table.fetch('diagnostics_json'))
expected_rejected_diagnostics = {
  'status' => 3,
  'status_code' => 'invalid_query',
  'ok' => false,
  'diagnostics' => [
    {
      'severity' => 'error',
      'code' => 'invalid_query',
      'message' => 'table is not present in schema',
      'source' => 'schema',
      'path' => '$.FROM'
    }
  ]
}
unless rejected_diagnostics == expected_rejected_diagnostics
  raise "unexpected table rejection diagnostics: #{rejected_diagnostics.inspect}"
end

unless CarbonC.normalize_allowlist_sql('SELECT * FROM `actor` LIMIT 10') == 'SELECT * FROM `actor` LIMIT ?'
  raise 'unexpected allowlist normalization'
end

begin
  CarbonC.compile_query({})
  raise 'expected TypeError for non-string query'
rescue TypeError
end

begin
  CarbonC.normalize_allowlist_sql(nil)
  raise 'expected TypeError for nil SQL'
rescue TypeError
end

puts 'ruby binding smoke: ok'
