-- Mercenary dbstr ids: give them a type
--
-- APPLY MANUALLY -- nothing in the server creates or alters these columns.
--   mysql -u <user> -p <database> < utils/sql/git/optional/2026_10_19_merc_dbstring_ids.sql
-- A fresh install from the current utils/sql/merc_tables_bootstrap.sql already has
-- them and does not need this file.  Skip either ALTER if the matching query at the
-- bottom of this file reports the column already exists.
--
-- merc_types.dbstring and merc_templates.dbstring are varchar(12) columns that have never held
-- text. They hold a numeric dbstr_us.txt string id:
--
--   merc_types.dbstring      -> category 21  eMercenaryCategory                '1000100'
--   merc_templates.dbstring  -> category 22  eMercenarySubCategory             '1010101'
--                             and category 23 eMercenarySubCategoryDescription
--                             (22 and 23 share one id, so one column is correct)
--
-- Verified against G:/Games/LS/dbstr_us.txt before writing this migration:
--   * all 48 distinct merc_types.dbstring values resolve in category 21
--   * all 552 distinct merc_templates.dbstring values resolve in BOTH category 22 and 23
--   * every merc_types row follows race_id * 1000000 + proficiency_id * 100
--     (1,1 -> 1000100; 568,2 -> 568000200), and proficiency is only ever 1 or 2
-- So this is a typing fix, not a data fix. No values change.
--
-- Why it matters for Laurion: 0x59b7 and 0x689c carry these ids as int32 record fields
-- (type_string_id at wire 0x04, subtype_string_id at 0x08). The client resolves them in its own
-- dbstr_us.txt. When an id is missing the client prints nothing in the mercenary browser and
-- writes "Unknown DB String %d-%d" to the CLIENT log -- completely invisible from the server.
-- A varchar that can hold 'Master' or '' therefore fails at the player's screen instead of at
-- the query. Renaming to dbstring_id at the same time means nothing can mistake it for a label.
--
-- Conversion caveat worth knowing: MySQL coerces a non-numeric varchar to 0 during this ALTER
-- and only raises a warning. 0 is not a valid mercenary dbstr id, so zone/merc.cpp now rejects
-- a zero id with a LogError instead of forwarding it to the client. Run the two checks below
-- before and after; both should return zero rows.

-- Skip 4.1 if this returns a row:
--   SELECT COLUMN_NAME FROM information_schema.COLUMNS
--   WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'merc_types' AND COLUMN_NAME = 'dbstring_id';

ALTER TABLE `merc_types`
    CHANGE COLUMN `dbstring` `dbstring_id` INT UNSIGNED NOT NULL AFTER `proficiency_id`;

-- Skip 4.2 if this returns a row:
--   SELECT COLUMN_NAME FROM information_schema.COLUMNS
--   WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'merc_templates' AND COLUMN_NAME = 'dbstring_id';

ALTER TABLE `merc_templates`
    CHANGE COLUMN `dbstring` `dbstring_id` INT UNSIGNED NOT NULL AFTER `merc_npc_type_id`;

-- Checks (expect zero rows from each):
--   SELECT merc_type_id, race_id, proficiency_id, dbstring_id FROM merc_types WHERE dbstring_id = 0;
--   SELECT merc_template_id, merc_type_id, dbstring_id FROM merc_templates WHERE dbstring_id = 0;
