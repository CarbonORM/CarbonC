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
join_payload = CarbonC.query('actor')
                      .select('actor.actor_id')
                      .join('INNER', 'film_actor fa', {'fa.actor_id' => ['=', 'actor.actor_id']})
                      .to_payload
expected_join_payload = {
  'FROM' => 'actor',
  'SELECT' => ['actor.actor_id'],
  'JOIN' => {'INNER' => {'film_actor fa' => {'fa.actor_id' => ['=', 'actor.actor_id']}}}
}
unless join_payload == expected_join_payload
  raise "unexpected join builder payload: #{join_payload.inspect}"
end
recent_actor_ids = CarbonC.query('film_actor')
                          .select('film_actor.actor_id')
                          .where({'film_actor.film_id' => ['>', 10]})
                          .limit(1)
expected_subselect_payload = [
  'SUBSELECT',
  {
    'FROM' => 'film_actor',
    'SELECT' => ['film_actor.actor_id'],
    'WHERE' => {'film_actor.film_id' => ['>', 10]},
    'PAGINATION' => {'LIMIT' => 1}
  }
]
unless CarbonC.subselect(recent_actor_ids) == expected_subselect_payload
  raise "unexpected subselect helper payload: #{CarbonC.subselect(recent_actor_ids).inspect}"
end
derived = CarbonC.query('actor')
                 .select('actor.actor_id', 'fa_recent.actor_id')
                 .join_subselect('INNER', 'fa_recent', recent_actor_ids, {'fa_recent.actor_id' => ['=', 'actor.actor_id']})
                 .where({'actor.actor_id' => ['>', 100]})
                 .compile(nil, 'mysql')
raise "unexpected derived compile status: #{derived.inspect}" unless derived.fetch('status') == 0
unless derived.fetch('sql') == 'SELECT actor.actor_id, fa_recent.actor_id FROM `actor` INNER JOIN (SELECT film_actor.actor_id FROM `film_actor` WHERE (film_actor.film_id) > ? LIMIT 1) AS `fa_recent` ON ((fa_recent.actor_id) = actor.actor_id) WHERE (actor.actor_id) > ? LIMIT 100'
  raise "unexpected derived sql: #{derived.fetch('sql')}"
end
raise "unexpected derived params: #{derived.fetch('params').inspect}" unless derived.fetch('params') == [10, 100]
sales_query = CarbonC.query('parcel_sales')
                     .select('parcel_sales.parcel_id')
                     .where_op('parcel_sales.sale_price', '>', 5000)
expected_alias_payload = [
  'AS',
  ['COUNT', 'parcel_sales.parcel_id'],
  'sale_count'
]
unless CarbonC.alias_expression(CarbonC.call('COUNT', 'parcel_sales.parcel_id'), 'sale_count') == expected_alias_payload
  raise 'unexpected alias helper payload'
end
raise 'unexpected literal helper payload' unless CarbonC.lit('2023-01-01') == ['LIT', '2023-01-01']
expected_exists_spec = [
  'property_units.parcel_id',
  [
    'SUBSELECT',
    {
      'FROM' => 'parcel_sales',
      'SELECT' => ['parcel_sales.parcel_id'],
      'WHERE' => {'parcel_sales.sale_price' => ['>', 5000]}
    }
  ]
]
unless CarbonC.exists_spec('property_units.parcel_id', sales_query) == expected_exists_spec
  raise "unexpected exists spec payload: #{CarbonC.exists_spec('property_units.parcel_id', sales_query).inspect}"
end
advanced = CarbonC.query('property_units')
                  .select('property_units.unit_id')
                  .where_between('property_units.unit_id', 1, 10)
                  .where_in('property_units.parcel_id', sales_query)
                  .where_not_in('property_units.account_id', [99, 100])
                  .where_exists('property_units.parcel_id', sales_query)
                  .limit(3)
                  .compile(nil, 'mysql')
raise "unexpected advanced compile status: #{advanced.inspect}" unless advanced.fetch('status') == 0
unless advanced.fetch('sql') == 'SELECT property_units.unit_id FROM `property_units` WHERE (property_units.unit_id) BETWEEN ? AND ? AND ( property_units.parcel_id IN (SELECT parcel_sales.parcel_id FROM `parcel_sales` WHERE (parcel_sales.sale_price) > ?) ) AND ( property_units.account_id NOT IN (?, ?) ) AND EXISTS (SELECT parcel_sales.parcel_id FROM `parcel_sales` WHERE (parcel_sales.sale_price) > ? AND (parcel_sales.parcel_id) = property_units.parcel_id) LIMIT 3'
  raise "unexpected advanced sql: #{advanced.fetch('sql')}"
end
unless advanced.fetch('params') == [1, 10, 5000, 99, 100, 5000]
  raise "unexpected advanced params: #{advanced.fetch('params').inspect}"
end
expected_boolean_group_payload = {
  'AND' => [
    {'actor.actor_id' => ['>', 2]},
    {
      'OR' => [
        {'actor.first_name' => ['LIKE', ['LIT', 'A%']]},
        {'actor.first_name' => ['LIKE', ['LIT', 'B%']]}
      ]
    }
  ]
}
actual_boolean_group_payload = CarbonC.and_group(
  CarbonC.condition('actor.actor_id', CarbonC.op('>', 2)),
  CarbonC.or_group(
    CarbonC.condition('actor.first_name', CarbonC.op('LIKE', CarbonC.lit('A%'))),
    CarbonC.condition('actor.first_name', CarbonC.op('LIKE', CarbonC.lit('B%')))
  )
)
unless actual_boolean_group_payload == expected_boolean_group_payload
  raise "unexpected boolean group helper payload: #{actual_boolean_group_payload.inspect}"
end
boolean_grouped = CarbonC.query('actor')
                         .select('actor.actor_id')
                         .where_between('actor.actor_id', 1, 10)
                         .where_or(
                           CarbonC.condition('actor.first_name', CarbonC.op('LIKE', CarbonC.lit('A%'))),
                           CarbonC.condition('actor.first_name', CarbonC.op('LIKE', CarbonC.lit('B%')))
                         )
                         .where_and(
                           CarbonC.condition('actor.actor_id', CarbonC.op('>', 2)),
                           CarbonC.condition('actor.actor_id', CarbonC.op('<', 9))
                         )
                         .limit(5)
                         .compile(nil, 'mysql')
raise "unexpected boolean group status: #{boolean_grouped.inspect}" unless boolean_grouped.fetch('status') == 0
unless boolean_grouped.fetch('sql') == 'SELECT actor.actor_id FROM `actor` WHERE (actor.actor_id) BETWEEN ? AND ? AND ((actor.first_name) LIKE ? OR (actor.first_name) LIKE ?) AND ((actor.actor_id) > ? AND (actor.actor_id) < ?) LIMIT 5'
  raise "unexpected boolean group sql: #{boolean_grouped.fetch('sql')}"
end
unless boolean_grouped.fetch('params') == [1, 10, 'A%', 'B%', 2, 9]
  raise "unexpected boolean group params: #{boolean_grouped.fetch('params').inspect}"
end
grouped = CarbonC.query('actor')
                 .select(['DISTINCT', 'actor.first_name'], ['AS', ['COUNT', 'actor.actor_id'], 'cnt'])
                 .group_by('actor.first_name')
                 .having({'cnt' => ['>', 1]})
                 .page(2)
                 .limit(5)
                 .compile(nil, 'mysql')
raise "unexpected grouped compile status: #{grouped.inspect}" unless grouped.fetch('status') == 0
unless grouped.fetch('sql') == 'SELECT DISTINCT actor.first_name, COUNT(actor.actor_id) AS cnt FROM `actor` GROUP BY actor.first_name HAVING ((cnt) > ?) LIMIT 5, 5'
  raise "unexpected grouped sql: #{grouped.fetch('sql')}"
end
raise "unexpected grouped params: #{grouped.fetch('params').inspect}" unless grouped.fetch('params') == [1]
inserted = CarbonC.query('actor')
                  .insert({'actor.first_name' => 'ALICE'})
                  .compile(schema, 'mysql')
raise "unexpected insert compile status: #{inserted.inspect}" unless inserted.fetch('status') == 0
unless inserted.fetch('sql') == 'INSERT INTO `actor` (`first_name`) VALUES (?)'
  raise "unexpected insert sql: #{inserted.fetch('sql')}"
end
raise "unexpected insert params: #{inserted.fetch('params').inspect}" unless inserted.fetch('params') == ['ALICE']
replace_payload = CarbonC.query('actor').replace({'actor.first_name' => 'BOB'}).to_payload
expected_replace_payload = {
  'FROM' => 'actor',
  'REPLACE' => {'actor.first_name' => 'BOB'}
}
unless replace_payload == expected_replace_payload
  raise "unexpected replace builder payload: #{replace_payload.inspect}"
end
updated = CarbonC.query('actor')
                 .update({'actor.first_name' => 'BOB'})
                 .where({'actor.actor_id' => 1})
                 .compile(schema, 'mysql')
unless updated.fetch('sql') == 'UPDATE `actor` SET `first_name` = ? WHERE (actor.actor_id) = ?'
  raise "unexpected update sql: #{updated.fetch('sql')}"
end
raise "unexpected update params: #{updated.fetch('params').inspect}" unless updated.fetch('params') == ['BOB', 1]
deleted = CarbonC.query('actor')
                 .delete
                 .where({'actor.actor_id' => 1})
                 .compile(schema, 'mysql')
unless deleted.fetch('sql') == 'DELETE `actor` FROM `actor` WHERE (actor.actor_id) = ?'
  raise "unexpected delete sql: #{deleted.fetch('sql')}"
end
raise "unexpected delete params: #{deleted.fetch('params').inspect}" unless deleted.fetch('params') == [1]
upserted = CarbonC.query('actor')
                  .insert({'actor.actor_id' => 1, 'actor.first_name' => 'ALICE'})
                  .upsert(['first_name'])
                  .compile(schema, 'mysql')
unless upserted.fetch('sql') == 'INSERT INTO `actor` (`actor_id`, `first_name`) VALUES (?, ?) ON DUPLICATE KEY UPDATE `first_name` = VALUES(`first_name`)'
  raise "unexpected upsert sql: #{upserted.fetch('sql')}"
end
raise "unexpected upsert params: #{upserted.fetch('params').inspect}" unless upserted.fetch('params') == [1, 'ALICE']
do_nothing_payload = CarbonC.query('actor').insert({'actor.actor_id' => 1}).do_nothing.to_payload
expected_do_nothing_payload = {
  'FROM' => 'actor',
  'INSERT' => {'actor.actor_id' => 1},
  'UPDATE' => []
}
unless do_nothing_payload == expected_do_nothing_payload
  raise "unexpected do-nothing builder payload: #{do_nothing_payload.inspect}"
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
eval(models)
raise 'unexpected model table' unless CarbonC.model_table(CarbonModels::Actor) == 'actor'
unless CarbonC.model_column(CarbonModels::Actor, 'first_name') == 'actor.first_name'
  raise 'unexpected model column'
end
expected_model_select_payload = {
  'FROM' => 'actor',
  'SELECT' => ['actor.actor_id', 'actor.first_name']
}
unless CarbonC.model_select(CarbonModels::Actor).to_payload == expected_model_select_payload
  raise "unexpected model select payload: #{CarbonC.model_select(CarbonModels::Actor).to_payload.inspect}"
end
model_built = CarbonC.model_select(CarbonModels::Actor, 'actor_id')
                     .where_op('actor.actor_id', '>', 0)
                     .limit(1)
                     .compile(schema, 'mysql')
unless model_built.fetch('sql') == 'SELECT actor.actor_id FROM `actor` WHERE (actor.actor_id) > ? LIMIT 1'
  raise "unexpected model query sql: #{model_built.fetch('sql')}"
end
raise "unexpected model query params: #{model_built.fetch('params').inspect}" unless model_built.fetch('params') == [0]

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
