# frozen_string_literal: true

require 'json'
require_relative '../../bindings/ruby/carbon_codegen'

puts CarbonC.version

schema_dump = <<~SQL
  CREATE TABLE `actor` (
    `actor_id` smallint unsigned NOT NULL AUTO_INCREMENT,
    `first_name` varchar(45) NOT NULL,
    PRIMARY KEY (`actor_id`)
  );
SQL

schema = CarbonC.schema_from_dump(schema_dump)

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
puts CarbonC.schema_metadata(schema)
puts CarbonC.schema_models(schema)
