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

eval(CarbonC.schema_models(schema))

get_request = CarbonModels::Actor.Get(
  {
    CarbonC::C6C::SELECT => [CarbonModels::Actor::ACTOR_ID, CarbonModels::Actor::FIRST_NAME],
    CarbonC::C6C::WHERE => {
      CarbonModels::Actor::ACTOR_ID => CarbonC.eq_lit(10)
    },
    CarbonC::C6C::PAGINATION => {CarbonC::C6C::LIMIT => 5}
  },
  schema: schema,
  dialect: CarbonC::Dialect::MYSQL
)

result = CarbonC.compile_query_result(
  get_request.fetch('query'),
  get_request.fetch('schema'),
  get_request.fetch('dialect')
)

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
