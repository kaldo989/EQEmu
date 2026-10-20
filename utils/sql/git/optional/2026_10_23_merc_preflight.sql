-- ============================================================================
-- Mercenary upgrade preflight -- READ ONLY, changes nothing
--
-- Run this first.  It tells you which mercenary tables and columns your
-- database already has, so you know exactly which import files you still need
-- and which ones to skip.
--
--     mysql -u <user> -p <database> -t < utils/sql/git/optional/2026_10_23_merc_preflight.sql
--
-- -t (table output) is optional but makes the results readable.  Every
-- statement below is a SELECT.
--
-- How to read the answers
--   merc template tables MISSING  -> import utils/sql/merc_tables_bootstrap.sql
--   mercs.AutoAssist MISSING      -> import 2026_10_22_merc_auto_assist.sql
--   dbstring MISSING / dbstring_id MISSING
--                                 -> import 2026_10_19_merc_dbstring_ids.sql
--   db_str 37 / 38 count 0        -> import 2026_10_20_db_str_mercenary_aa_strings.sql
--   merc_aa_* MISSING             -> import 2026_10_18_mercenary_aa_schema_draft.sql
--
-- The first two of those are not cosmetic.  MercsRepository names AutoAssist in
-- its SELECT list and the merc type/template repositories name dbstring_id, so a
-- missing column makes every mercs query fail and mercenaries stop loading at
-- all.
-- ============================================================================

-- 1. Mercenary tables --------------------------------------------------------
-- 'present' / 'MISSING' for every table the mercenary subsystem expects.

SELECT t.expected AS table_name,
       IF(i.TABLE_NAME IS NULL, 'MISSING', 'present') AS state
FROM (
    -- template / content tables, created by utils/sql/merc_tables_bootstrap.sql
    SELECT 'merc_armorinfo'                  AS expected, 'bootstrap' AS source UNION ALL
    SELECT 'merc_buffs',                     'bootstrap' UNION ALL
    SELECT 'merc_inventory',                 'bootstrap' UNION ALL
    SELECT 'merc_merchant_entries',          'bootstrap' UNION ALL
    SELECT 'merc_merchant_template_entries', 'bootstrap' UNION ALL
    SELECT 'merc_merchant_templates',        'bootstrap' UNION ALL
    SELECT 'merc_name_types',                'bootstrap' UNION ALL
    SELECT 'merc_npc_types',                 'bootstrap' UNION ALL
    SELECT 'merc_spell_list_entries',        'bootstrap' UNION ALL
    SELECT 'merc_spell_lists',               'bootstrap' UNION ALL
    SELECT 'merc_stance_entries',            'bootstrap' UNION ALL
    SELECT 'merc_stats',                     'bootstrap' UNION ALL
    SELECT 'merc_subtypes',                  'bootstrap' UNION ALL
    SELECT 'merc_templates',                 'bootstrap' UNION ALL
    SELECT 'merc_types',                     'bootstrap' UNION ALL
    SELECT 'merc_weaponinfo',                'bootstrap' UNION ALL
    SELECT 'mercs',                          'bootstrap' UNION ALL
    -- mercenary AA tables, created by 2026_10_18_mercenary_aa_schema_draft.sql
    SELECT 'merc_aa_ability',                'merc_aa_schema_draft' UNION ALL
    SELECT 'merc_aa_rank',                   'merc_aa_schema_draft' UNION ALL
    SELECT 'merc_aa_rank_effects',           'merc_aa_schema_draft' UNION ALL
    SELECT 'merc_aa_prereqs',                'merc_aa_schema_draft' UNION ALL
    SELECT 'merc_aa_merc_types',             'merc_aa_schema_draft' UNION ALL
    SELECT 'player_merc_aa',                 'merc_aa_schema_draft' UNION ALL
    SELECT 'merc_aa_currency',               'merc_aa_schema_draft'
) t
LEFT JOIN information_schema.TABLES i
       ON i.TABLE_SCHEMA = DATABASE() AND i.TABLE_NAME = t.expected
ORDER BY state DESC, t.expected;

-- 2. Columns the code selects by name --------------------------------------
-- A 'MISSING' row here is a query that will fail at runtime, not a feature you
-- are merely missing.  dbstring and dbstring_id are listed together on purpose:
-- whichever one is present tells you which side of the rename you are on.

SELECT c.table_name,
       c.column_name,
       IF(x.COLUMN_NAME IS NULL, 'MISSING', 'present') AS state,
       IFNULL(x.COLUMN_TYPE, '-') AS column_type
FROM (
    SELECT 'mercs'           AS table_name, 'AutoAssist'  AS column_name UNION ALL
    SELECT 'merc_types',     'dbstring'   UNION ALL
    SELECT 'merc_types',     'dbstring_id' UNION ALL
    SELECT 'merc_templates', 'dbstring'   UNION ALL
    SELECT 'merc_templates', 'dbstring_id' UNION ALL
    SELECT 'merc_aa_ability','rank_index'
) c
LEFT JOIN information_schema.COLUMNS x
       ON x.TABLE_SCHEMA = DATABASE() AND x.TABLE_NAME = c.table_name
      AND x.COLUMN_NAME = c.column_name
ORDER BY c.table_name, c.column_name;

-- 3. Mercenary string categories in db_str ----------------------------------
-- 21..25 and 36 normally arrive with a PEQ dump.  37 and 38 only exist if you
-- have imported 2026_10_20_db_str_mercenary_aa_strings.sql.  A count of 0 for
-- 37 or 38 means the server cannot validate any mercenary AA name or description.

SELECT cat.type,
       cat.expected_text,
       IFNULL((SELECT COUNT(*) FROM db_str s WHERE s.type = cat.type), 0) AS rows_in_db_str
FROM (
    SELECT 21 AS type, 'eMercenaryCategory'               AS expected_text UNION ALL
    SELECT 22, 'eMercenarySubCategory'                    UNION ALL
    SELECT 23, 'eMercenarySubCategoryDescription'         UNION ALL
    SELECT 24, 'eMercenaryStanceName'                     UNION ALL
    SELECT 25, 'eMercenaryStanceDescription'              UNION ALL
    SELECT 36, 'eMercenaryAbilityType'                    UNION ALL
    SELECT 37, 'eMercenaryAbilityName'                    UNION ALL
    SELECT 38, 'eMercenaryAbilityDescription'
) cat
ORDER BY cat.type;

-- 4. Mercenary rules ---------------------------------------------------------
-- Mercs:AllowMercs = false means the subsystem is off regardless of schema.

SELECT rule_name, rule_value
FROM rule_values
WHERE rule_name IN ('Mercs:AllowMercs', 'Mercs:MaxMercSlots', 'World:ExpansionSettings')
ORDER BY rule_name;

-- 5. Owned mercenaries that would be lost by re-running the bootstrap -------
-- utils/sql/merc_tables_bootstrap.sql starts every block with DROP TABLE IF
-- EXISTS, including `mercs` and `merc_buffs`.  If either table holds rows, do
-- NOT run the bootstrap on this database -- create the missing tables
-- individually and ALTER mercs instead.
--
-- TABLE_ROWS is an InnoDB estimate, so it is shown as "approx".  It is used here
-- because this query must not fail on a database where the tables are absent.

SELECT t.tbl AS table_name,
       IF(i.TABLE_NAME IS NULL, 'table does not exist', CONCAT(i.TABLE_ROWS, ' (approx)')) AS approx_rows
FROM (SELECT 'mercs' AS tbl UNION ALL SELECT 'merc_buffs') t
LEFT JOIN information_schema.TABLES i
       ON i.TABLE_SCHEMA = DATABASE() AND i.TABLE_NAME = t.tbl;

-- Exact counts, once both tables exist:
--   SELECT (SELECT COUNT(*) FROM mercs) AS mercs_rows,
--          (SELECT COUNT(*) FROM merc_buffs) AS merc_buffs_rows;

-- 6. Schema version the server has reached ----------------------------------
-- The server applies manifest migrations itself on boot.  Mercenary tables are
-- not in that manifest at all, so this number tells you nothing about whether
-- the mercenary imports above have been run.

SELECT version, custom_version FROM db_version;
