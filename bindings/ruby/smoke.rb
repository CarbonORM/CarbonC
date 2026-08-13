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
metadata = JSON.parse(CarbonC.schema_metadata(JSON.generate(schema)))
expected_metadata = {
  'tables' => [
    {
      'name' => 'actor',
      'columns' => [
        {'name' => 'actor_id', 'qualified' => 'actor.actor_id'},
        {'name' => 'first_name', 'qualified' => 'actor.first_name'}
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

rejected = CarbonC.compile_query(
  JSON.generate({'FROM' => 'actor', 'SELECT' => ['actor.last_name']}),
  JSON.generate(schema),
  'mysql'
)
raise "expected invalid query rejection: #{rejected.inspect}" unless rejected.fetch('status') == 3
raise "unexpected rejection status code: #{rejected.inspect}" unless rejected.fetch('status_code') == 'invalid_query'
raise "unexpected rejection message: #{rejected.inspect}" unless rejected.fetch('error') == 'invalid query'

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
