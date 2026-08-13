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
raise 'unexpected C6C FROM constant' unless CarbonC::C6C::FROM == 'FROM'
raise 'unexpected C6C EQUAL constant' unless CarbonC::C6C::EQUAL == '='
raise 'unexpected C6C GREATER_THAN constant' unless CarbonC::C6C::GREATER_THAN == '>'
raise 'unexpected eq_lit payload' unless CarbonC.eq_lit(10) == ['=', ['LIT', 10]]
raise 'unexpected in_lit payload' unless CarbonC.in_lit([1, 2]) == ['IN', [['LIT', 1], ['LIT', 2]]]
raise 'unexpected not_in_lit payload' unless CarbonC.not_in_lit([1, 2]) == ['NOT_IN', [['LIT', 1], ['LIT', 2]]]
raise 'unexpected between_lit payload' unless CarbonC.between_lit(1, 2) == ['BETWEEN', [['LIT', 1], ['LIT', 2]]]
raise 'unexpected dialect MYSQL constant' unless CarbonC::Dialect::MYSQL == 'mysql'
raise 'unexpected dialect POSTGRESQL constant' unless CarbonC::Dialect::POSTGRESQL == 'postgresql'
raise 'unexpected dialect POSTGRES constant' unless CarbonC::Dialect::POSTGRES == 'postgres'

result = CarbonC.compile_query(JSON.generate(query), JSON.generate(schema), CarbonC::Dialect::MYSQL)

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
ergonomic = CarbonC.compile_query_value(query, schema, CarbonC::Dialect::MYSQL)
raise "unexpected ergonomic compile result: #{ergonomic.inspect}" unless ergonomic == result
top_ordered = CarbonC.compile_query_value(
  {
    'FROM' => 'actor',
    'SELECT' => ['actor.actor_id', 'actor.first_name'],
    'WHERE' => {'actor.actor_id' => ['>', 10]},
    'ORDER' => [['actor.first_name', 'ASC']],
    'PAGINATION' => {'LIMIT' => 25}
  },
  schema,
  'mysql'
)
unless top_ordered.fetch('sql') == 'SELECT actor.actor_id, actor.first_name FROM `actor` WHERE (actor.actor_id) > ? ORDER BY actor.first_name ASC LIMIT 25'
  raise "unexpected top-level order sql: #{top_ordered.fetch('sql')}"
end
unless top_ordered.fetch('allowlist_key') == 'SELECT actor.actor_id, actor.first_name FROM `actor` WHERE (actor.actor_id) > ? ORDER BY actor.first_name ASC LIMIT ?'
  raise "unexpected top-level order allowlist: #{top_ordered.fetch('allowlist_key')}"
end
adapted = CarbonC.adapt_compile_result(result)
expected_adapted = result.merge(
  'params' => [10],
  'diagnostics' => JSON.parse(result.fetch('diagnostics_json'))
)
raise "unexpected adapted compile result: #{adapted.inspect}" unless adapted == expected_adapted
typed = CarbonC.compile_query_result(query, schema, CarbonC::Dialect::MYSQL)
raise "unexpected typed compile result: #{typed.inspect}" unless typed == adapted
built = CarbonC.query('actor')
                .select('actor.actor_id', 'actor.first_name')
                .where({'actor.actor_id' => ['>', 10]})
                .limit(5)
raise "unexpected builder payload: #{built.to_payload.inspect}" unless built.to_payload == query
raise 'unexpected builder compile result' unless built.compile(schema, CarbonC::Dialect::MYSQL) == adapted
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
expected_force_index_payload = {
  'FORCE INDEX' => ['idx_county_id', 'idx_property_units_location']
}
unless CarbonC.force_index('idx_county_id', 'idx_property_units_location') == expected_force_index_payload
  raise 'unexpected force index helper payload'
end
hint_payload = CarbonC.query('property_units')
                      .select('property_units.unit_id')
                      .force_index('idx_county_id', 'idx_property_units_location')
                      .limit(10)
                      .to_payload
expected_hint_payload = {
  'FROM' => 'property_units',
  'SELECT' => ['property_units.unit_id'],
  'INDEX_HINTS' => expected_force_index_payload,
  'PAGINATION' => {'LIMIT' => 10}
}
unless hint_payload == expected_hint_payload
  raise "unexpected index hint builder payload: #{hint_payload.inspect}"
end
hinted_join = CarbonC.query('actor')
                     .select('actor.actor_id', 'fa.film_id')
                     .join('INNER', 'film_actor fa', {'fa.actor_id' => ['=', 'actor.actor_id']})
                     .index_hints(
                       'actor' => CarbonC.ignore_index('idx_actor_last_name'),
                       'film_actor fa' => CarbonC.use_index('idx_film_actor_actor_id')
                     )
                     .limit(5)
                     .compile(nil, CarbonC::Dialect::MYSQL)
unless hinted_join.fetch('sql') == 'SELECT actor.actor_id, fa.film_id FROM `actor` IGNORE INDEX (`idx_actor_last_name`) INNER JOIN `film_actor` AS `fa` USE INDEX (`idx_film_actor_actor_id`) ON ((fa.actor_id) = actor.actor_id) LIMIT 5'
  raise "unexpected hinted join sql: #{hinted_join.fetch('sql')}"
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
                 .compile(nil, CarbonC::Dialect::MYSQL)
raise "unexpected derived compile status: #{derived.inspect}" unless derived.fetch('status') == 0
unless derived.fetch('sql') == 'SELECT actor.actor_id, fa_recent.actor_id FROM `actor` INNER JOIN (SELECT film_actor.actor_id FROM `film_actor` WHERE (film_actor.film_id) > ? LIMIT 1) AS `fa_recent` ON ((fa_recent.actor_id) = actor.actor_id) WHERE (actor.actor_id) > ? LIMIT 100'
  raise "unexpected derived sql: #{derived.fetch('sql')}"
end
raise "unexpected derived params: #{derived.fetch('params').inspect}" unless derived.fetch('params') == [10, 100]
sales_query = CarbonC.query('parcel_sales')
                     .select('parcel_sales.parcel_id')
                     .where_op('parcel_sales.sale_price', CarbonC::C6C::GREATER_THAN, 5000)
expected_alias_payload = [
  'AS',
  ['COUNT', 'parcel_sales.parcel_id'],
  'sale_count'
]
unless CarbonC.alias_expression(CarbonC.call('COUNT', 'parcel_sales.parcel_id'), 'sale_count') == expected_alias_payload
  raise 'unexpected alias helper payload'
end
expected_function_payload = [
  'CONCAT',
  ['LIT', 'A'],
  ['LIT', 'B']
]
unless CarbonC.fn('CONCAT', CarbonC.lit('A'), CarbonC.lit('B')) == expected_function_payload
  raise 'unexpected function helper payload'
end
expected_custom_call_payload = [
  'CALL',
  'COALESCE',
  ['LIT', 'UNKNOWN'],
  'actor.last_name'
]
unless CarbonC.custom_call('COALESCE', CarbonC.lit('UNKNOWN'), 'actor.last_name') == expected_custom_call_payload
  raise 'unexpected custom call helper payload'
end
custom_selected = CarbonC.query('actor')
                         .select([CarbonC.alias_expression(CarbonC.custom_call('COALESCE', CarbonC.lit('UNKNOWN'), 'actor.first_name'), 'display_name')])
                         .limit(1)
                         .compile(nil, CarbonC::Dialect::MYSQL)
unless custom_selected.fetch('sql') == 'SELECT COALESCE(?, actor.first_name) AS display_name FROM `actor` LIMIT 1'
  raise "unexpected custom call select sql: #{custom_selected.fetch('sql')}"
end
unless custom_selected.fetch('params') == ['UNKNOWN']
  raise "unexpected custom call select params: #{custom_selected.fetch('params').inspect}"
end
spatial_polygon = 'POLYGON((39.5185659 -105.0142915,39.5401859 -105.0142915,39.5401859 -104.9862115,39.5185659 -104.9862115,39.5185659 -105.0142915))'
spatial_inner_polygon = 'POLYGON((0 0,1 0,1 1,0 1,0 0))'
expected_mbr_contains_payload = [
  'MBRContains',
  'property_units.envelope',
  'property_units.location'
]
unless CarbonC.mbr_contains('property_units.envelope', 'property_units.location') == expected_mbr_contains_payload
  raise 'unexpected MBRContains helper payload'
end
spatial_filtered = CarbonC.query('property_units')
                          .select('property_units.unit_id')
                          .where(
                            {
                              'MBRContains' => [
                                CarbonC.fn('ST_GeomFromText', CarbonC.lit(spatial_polygon), 4326),
                                'property_units.location'
                              ],
                              'OR' => [
                                CarbonC.st_within(
                                  'property_units.location',
                                  CarbonC.fn('ST_GeomFromText', CarbonC.lit(spatial_inner_polygon), 4326)
                                ),
                                CarbonC.st_contains('property_units.envelope', 'property_units.location')
                              ]
                            }
                          )
                          .limit(10)
                          .compile(nil, CarbonC::Dialect::MYSQL)
unless spatial_filtered.fetch('sql') == 'SELECT property_units.unit_id FROM `property_units` WHERE MBRCONTAINS(ST_GEOMFROMTEXT(?, 4326), property_units.location) AND (ST_WITHIN(property_units.location, ST_GEOMFROMTEXT(?, 4326)) OR ST_CONTAINS(property_units.envelope, property_units.location)) LIMIT 10'
  raise "unexpected spatial predicate sql: #{spatial_filtered.fetch('sql')}"
end
unless spatial_filtered.fetch('params') == [spatial_polygon, spatial_inner_polygon]
  raise "unexpected spatial predicate params: #{spatial_filtered.fetch('params').inspect}"
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
                  .compile(nil, CarbonC::Dialect::MYSQL)
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
expected_match_against_payload = [
  'MATCH_AGAINST',
  [['LIT', 'alpha beta'], 'BOOLEAN']
]
unless CarbonC.match_against('alpha beta', 'BOOLEAN') == expected_match_against_payload
  raise "unexpected MATCH_AGAINST helper payload: #{CarbonC.match_against('alpha beta', 'BOOLEAN').inspect}"
end
fulltext = CarbonC.query('actor')
                  .select('actor.actor_id')
                  .where_match_against('actor.first_name', 'alpha beta', 'BOOLEAN')
                  .limit(10)
                  .compile(schema, CarbonC::Dialect::MYSQL)
raise "unexpected full-text compile status: #{fulltext.inspect}" unless fulltext.fetch('status') == 0
unless fulltext.fetch('sql') == 'SELECT actor.actor_id FROM `actor` WHERE (MATCH(actor.first_name) AGAINST(? IN BOOLEAN MODE)) LIMIT 10'
  raise "unexpected full-text sql: #{fulltext.fetch('sql')}"
end
raise "unexpected full-text params: #{fulltext.fetch('params').inspect}" unless fulltext.fetch('params') == ['alpha beta']
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
                         .compile(nil, CarbonC::Dialect::MYSQL)
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
                 .compile(nil, CarbonC::Dialect::MYSQL)
raise "unexpected grouped compile status: #{grouped.inspect}" unless grouped.fetch('status') == 0
unless grouped.fetch('sql') == 'SELECT DISTINCT actor.first_name, COUNT(actor.actor_id) AS cnt FROM `actor` GROUP BY actor.first_name HAVING ((cnt) > ?) LIMIT 5, 5'
  raise "unexpected grouped sql: #{grouped.fetch('sql')}"
end
raise "unexpected grouped params: #{grouped.fetch('params').inspect}" unless grouped.fetch('params') == [1]
inserted = CarbonC.query('actor')
                  .insert({'actor.first_name' => 'ALICE'})
                  .compile(schema, CarbonC::Dialect::MYSQL)
raise "unexpected insert compile status: #{inserted.inspect}" unless inserted.fetch('status') == 0
unless inserted.fetch('sql') == 'INSERT INTO `actor` (`first_name`) VALUES (?)'
  raise "unexpected insert sql: #{inserted.fetch('sql')}"
end
raise "unexpected insert params: #{inserted.fetch('params').inspect}" unless inserted.fetch('params') == ['ALICE']
expression_inserted = CarbonC.query('actor')
                             .insert({
                                       'actor.first_name' => CarbonC.fn('CONCAT', CarbonC.lit('HEL'), CarbonC.lit('LO')),
                                       'actor.last_name' => 'SMITH'
                                     })
                             .compile(nil, CarbonC::Dialect::MYSQL)
unless expression_inserted.fetch('sql') == 'INSERT INTO `actor` (`first_name`, `last_name`) VALUES (CONCAT(?, ?), ?)'
  raise "unexpected expression insert sql: #{expression_inserted.fetch('sql')}"
end
unless expression_inserted.fetch('params') == ['HEL', 'LO', 'SMITH']
  raise "unexpected expression insert params: #{expression_inserted.fetch('params').inspect}"
end
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
                 .compile(schema, CarbonC::Dialect::MYSQL)
unless updated.fetch('sql') == 'UPDATE `actor` SET `first_name` = ? WHERE (actor.actor_id) = ?'
  raise "unexpected update sql: #{updated.fetch('sql')}"
end
raise "unexpected update params: #{updated.fetch('params').inspect}" unless updated.fetch('params') == ['BOB', 1]
expression_updated = CarbonC.query('actor')
                            .update({
                                      'actor.first_name' => CarbonC.fn('CONCAT', CarbonC.lit('Mr. '), 'actor.last_name'),
                                      'actor.last_name' => CarbonC.custom_call('COALESCE', CarbonC.lit('UNKNOWN'), 'actor.last_name')
                                    })
                            .where({'actor.actor_id' => ['=', 7]})
                            .compile(nil, CarbonC::Dialect::MYSQL)
expected_expression_update_sql = 'UPDATE `actor` SET `first_name` = CONCAT(?, actor.last_name), `last_name` = COALESCE(?, actor.last_name) WHERE (actor.actor_id) = ?'
unless expression_updated.fetch('sql') == expected_expression_update_sql
  raise "unexpected expression update sql: #{expression_updated.fetch('sql')}"
end
unless expression_updated.fetch('params') == ['Mr. ', 'UNKNOWN', 7]
  raise "unexpected expression update params: #{expression_updated.fetch('params').inspect}"
end
deleted = CarbonC.query('actor')
                 .delete
                 .where({'actor.actor_id' => 1})
                 .compile(schema, CarbonC::Dialect::MYSQL)
unless deleted.fetch('sql') == 'DELETE `actor` FROM `actor` WHERE (actor.actor_id) = ?'
  raise "unexpected delete sql: #{deleted.fetch('sql')}"
end
raise "unexpected delete params: #{deleted.fetch('params').inspect}" unless deleted.fetch('params') == [1]
upserted = CarbonC.query('actor')
                  .insert({'actor.actor_id' => 1, 'actor.first_name' => 'ALICE'})
                  .upsert(['first_name'])
                  .compile(schema, CarbonC::Dialect::MYSQL)
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
raise "unexpected model source: #{models}" unless models.include?('Actor::FIELD_ACTOR_ID = "actor_id"')
raise "unexpected model source: #{models}" unless models.include?('Actor::FIELD_FIRST_NAME = "first_name"')
raise "unexpected model source: #{models}" unless models.include?('Actor::FIELDS = [Actor::FIELD_ACTOR_ID, Actor::FIELD_FIRST_NAME].freeze')
raise "unexpected model source: #{models}" unless models.include?('Actor::ACTOR_ID = "actor.actor_id"')
raise "unexpected model source: #{models}" unless models.include?('Actor::FIRST_NAME = "actor.first_name"')
raise "unexpected model source: #{models}" unless models.include?('PRIMARY = ["actor_id"].freeze')
raise "unexpected model source: #{models}" unless models.include?('Actor::FIELD_ACTOR_ID => Actor::ACTOR_ID')
raise "unexpected model source: #{models}" unless models.include?('TYPES = {')
raise "unexpected model source: #{models}" unless models.include?('"actor_id" => :integer')
raise "unexpected model source: #{models}" unless models.include?('NULLABLE = {')
raise "unexpected model source: #{models}" unless models.include?('"actor_id" => false')
raise "unexpected model source: #{models}" unless models.include?('def Actor.GetPayload(query_payload = nil)')
raise "unexpected model source: #{models}" unless models.include?('def Actor.Get(query_payload = nil, **options)')
eval(models)
raise 'unexpected model table' unless CarbonC.model_table(CarbonModels::Actor) == 'actor'
raise 'unexpected generated TABLE constant' unless CarbonModels::Actor::TABLE == 'actor'
raise 'unexpected generated FIELD_ACTOR_ID constant' unless CarbonModels::Actor::FIELD_ACTOR_ID == 'actor_id'
raise 'unexpected generated FIELD_FIRST_NAME constant' unless CarbonModels::Actor::FIELD_FIRST_NAME == 'first_name'
raise 'unexpected generated FIELDS constant' unless CarbonModels::Actor::FIELDS == ['actor_id', 'first_name']
raise 'unexpected generated ACTOR_ID constant' unless CarbonModels::Actor::ACTOR_ID == 'actor.actor_id'
raise 'unexpected generated FIRST_NAME constant' unless CarbonModels::Actor::FIRST_NAME == 'actor.first_name'
raise 'unexpected generated COLUMNS constant value' unless CarbonModels::Actor::COLUMNS['actor_id'] == CarbonModels::Actor::ACTOR_ID
unless CarbonC.model_column(CarbonModels::Actor, CarbonModels::Actor::FIELD_FIRST_NAME) == 'actor.first_name'
  raise 'unexpected model column'
end
expected_model_select_payload = {
  'FROM' => 'actor',
  'SELECT' => ['actor.actor_id', 'actor.first_name']
}
unless CarbonC.model_select(CarbonModels::Actor).to_payload == expected_model_select_payload
  raise "unexpected model select payload: #{CarbonC.model_select(CarbonModels::Actor).to_payload.inspect}"
end
constant_built = CarbonC.query(CarbonModels::Actor::TABLE)
                        .select(CarbonModels::Actor::ACTOR_ID, CarbonModels::Actor::FIRST_NAME)
                        .where_op(CarbonModels::Actor::ACTOR_ID, CarbonC::C6C::GREATER_THAN, 0)
                        .order_by(CarbonModels::Actor::FIRST_NAME, CarbonC::C6C::ASC)
                        .limit(1)
                        .compile(schema, CarbonC::Dialect::MYSQL)
unless constant_built.fetch('sql') == 'SELECT actor.actor_id, actor.first_name FROM `actor` WHERE (actor.actor_id) > ? ORDER BY actor.first_name ASC LIMIT 1'
  raise "unexpected constant-built query sql: #{constant_built.fetch('sql')}"
end
raise "unexpected constant-built query params: #{constant_built.fetch('params').inspect}" unless constant_built.fetch('params') == [0]
model_built = CarbonC.model_select(CarbonModels::Actor, CarbonModels::Actor::FIELD_ACTOR_ID)
                     .where_op(CarbonModels::Actor::ACTOR_ID, CarbonC::C6C::GREATER_THAN, 0)
                     .limit(1)
                     .compile(schema, CarbonC::Dialect::MYSQL)
unless model_built.fetch('sql') == 'SELECT actor.actor_id FROM `actor` WHERE (actor.actor_id) > ? LIMIT 1'
  raise "unexpected model query sql: #{model_built.fetch('sql')}"
end
raise "unexpected model query params: #{model_built.fetch('params').inspect}" unless model_built.fetch('params') == [0]
model_values = CarbonC.model_values(
  CarbonModels::Actor,
  CarbonModels::Actor::FIELD_FIRST_NAME => 'ALICE'
)
raise "unexpected model values payload: #{model_values.inspect}" unless model_values == {'actor.first_name' => 'ALICE'}
get_payload = CarbonModels::Actor.GetPayload(
  CarbonC::C6C::SELECT => [CarbonModels::Actor::ACTOR_ID],
  CarbonC::C6C::WHERE => {
    CarbonModels::Actor::ACTOR_ID => CarbonC.eq_lit(10)
  },
  CarbonC::C6C::PAGINATION => {CarbonC::C6C::LIMIT => 500},
  'cacheResults' => false
)
expected_get_payload = {
  'SELECT' => ['actor.actor_id'],
  'WHERE' => {'actor.actor_id' => ['=', ['LIT', 10]]},
  'PAGINATION' => {'LIMIT' => 500},
  'cacheResults' => false,
  'FROM' => 'actor'
}
raise "unexpected model get payload: #{get_payload.inspect}" unless get_payload == expected_get_payload
mobile_route = CarbonC.route_query(
  get_payload,
  context: {'deviceClass' => 'mobile'},
  policy: {'serverOnMobile' => true}
)
raise "unexpected mobile route: #{mobile_route.inspect}" unless mobile_route == {'target' => 'server', 'reason' => 'mobile_offload'}
get_request = CarbonModels::Actor.Get(
  get_payload,
  schema: schema,
  dialect: CarbonC::Dialect::MYSQL,
  context: {'canRunLocal' => false}
)
raise "unexpected model get method: #{get_request.inspect}" unless get_request.fetch('method') == 'Get'
raise "unexpected model get model: #{get_request.inspect}" unless get_request.fetch('model') == 'actor'
raise "unexpected model get cache flag: #{get_request.inspect}" unless get_request.fetch('cacheResults') == false
unless get_request.fetch('route') == {'target' => 'server', 'reason' => 'local_unavailable'}
  raise "unexpected model get route: #{get_request.fetch('route').inspect}"
end
get_result = CarbonC.compile_query_result(
  get_request.fetch('query'),
  get_request.fetch('schema'),
  get_request.fetch('dialect')
)
unless get_result.fetch('sql') == 'SELECT actor.actor_id FROM `actor` WHERE (actor.actor_id) = ? LIMIT 500'
  raise "unexpected model get sql: #{get_result.fetch('sql')}"
end
in_lit_result = CarbonC.compile_query_result(
  {
    CarbonC::C6C::FROM => CarbonModels::Actor::TABLE,
    CarbonC::C6C::SELECT => [CarbonModels::Actor::ACTOR_ID],
    CarbonC::C6C::WHERE => {CarbonModels::Actor::ACTOR_ID => CarbonC.in_lit([1, 2])}
  },
  schema,
  CarbonC::Dialect::MYSQL
)
unless in_lit_result.fetch('sql') == 'SELECT actor.actor_id FROM `actor` WHERE ( actor.actor_id IN (?, ?) ) LIMIT 100'
  raise "unexpected in-lit sql: #{in_lit_result.fetch('sql')}"
end
raise "unexpected in-lit params: #{in_lit_result.fetch('params').inspect}" unless in_lit_result.fetch('params') == [1, 2]
model_inserted = CarbonC.model_insert(
  CarbonModels::Actor,
  CarbonModels::Actor::FIELD_FIRST_NAME => 'ALICE'
).compile(schema, CarbonC::Dialect::MYSQL)
unless model_inserted.fetch('sql') == 'INSERT INTO `actor` (`first_name`) VALUES (?)'
  raise "unexpected model insert sql: #{model_inserted.fetch('sql')}"
end
raise "unexpected model insert params: #{model_inserted.fetch('params').inspect}" unless model_inserted.fetch('params') == ['ALICE']
model_updated = CarbonC.model_update(
  CarbonModels::Actor,
  CarbonModels::Actor::FIELD_FIRST_NAME => 'BOB'
).where_op(CarbonModels::Actor::ACTOR_ID, CarbonC::C6C::GREATER_THAN, 0)
 .compile(schema, CarbonC::Dialect::MYSQL)
unless model_updated.fetch('sql') == 'UPDATE `actor` SET `first_name` = ? WHERE (actor.actor_id) > ?'
  raise "unexpected model update sql: #{model_updated.fetch('sql')}"
end
raise "unexpected model update params: #{model_updated.fetch('params').inspect}" unless model_updated.fetch('params') == ['BOB', 0]
model_upserted = CarbonC.model_upsert(
  CarbonModels::Actor,
  {
    CarbonModels::Actor::FIELD_ACTOR_ID => 1,
    CarbonModels::Actor::FIELD_FIRST_NAME => 'ALICE'
  },
  [CarbonModels::Actor::FIELD_FIRST_NAME]
).compile(schema, CarbonC::Dialect::MYSQL)
unless model_upserted.fetch('sql') == 'INSERT INTO `actor` (`actor_id`, `first_name`) VALUES (?, ?) ON DUPLICATE KEY UPDATE `first_name` = VALUES(`first_name`)'
  raise "unexpected model upsert sql: #{model_upserted.fetch('sql')}"
end
raise "unexpected model upsert params: #{model_upserted.fetch('params').inspect}" unless model_upserted.fetch('params') == [1, 'ALICE']
model_replace_payload = CarbonC.model_replace(
  CarbonModels::Actor,
  CarbonModels::Actor::FIELD_FIRST_NAME => 'BOB'
).to_payload
unless model_replace_payload == {'FROM' => 'actor', 'REPLACE' => {'actor.first_name' => 'BOB'}}
  raise "unexpected model replace payload: #{model_replace_payload.inspect}"
end
model_do_nothing_payload = CarbonC.model_do_nothing(
  CarbonModels::Actor,
  CarbonModels::Actor::FIELD_ACTOR_ID => 1
).to_payload
unless model_do_nothing_payload == {'FROM' => 'actor', 'INSERT' => {'actor.actor_id' => 1}, 'UPDATE' => []}
  raise "unexpected model do-nothing payload: #{model_do_nothing_payload.inspect}"
end

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
