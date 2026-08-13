# frozen_string_literal: true

require 'json'
require_relative '../../bindings/ruby/carbon'

puts CarbonC.version

schema = {
  'TABLES' => {
    'actor' => {
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

result = CarbonC.compile_query(JSON.generate(query), JSON.generate(schema), 'mysql')

if result.fetch('status') != 0
  warn result.fetch('error')
  exit 1
end

puts result.fetch('sql')
puts result.fetch('params_json')
puts result.fetch('allowlist_key')
