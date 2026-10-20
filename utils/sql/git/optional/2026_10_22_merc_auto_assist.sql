-- 2026_10_22_merc_auto_assist.sql
--
-- APPLY MANUALLY -- nothing in the server creates this column.
--   mysql -u <user> -p <database> < utils/sql/git/optional/2026_10_22_merc_auto_assist.sql
-- A fresh install from the current utils/sql/merc_tables_bootstrap.sql already has it.
--
-- THIS ONE IS REQUIRED, NOT OPTIONAL.  MercsRepository lists AutoAssist in its
-- SELECT column list, so if the column is missing every mercs query fails and
-- mercenaries stop loading at all -- not just auto-assist.
--
-- The client's Manage window has an AssistModeCheckbox (EQUI_MercenaryManageWnd.xml) and the
-- slash command "/assistmerc on|off". Both call FUN_1402b18c0, which sets MercenaryInfo.autoAssist
-- (MercenaryClientData+0x50), raises UI event 0x15, and sends OP_MercenaryAssistMode (0x2d14) with
-- a single on/off byte. The server's answer is OP_MercenaryAssistModeResult (0x4be9), whose arm at
-- 0x1402af76c prints legacy string 0x37ad and raises UI event 0x17 with the value.
--
-- The flag belongs to the owned mercenary, not to the spawn: it survives a suspend, a dismiss of
-- the spawn, and a zone change, and it is carried in the MercenaryInfo record as the autoAssist
-- byte at wire offset 0x36. That makes mercs the right table for it.
--
-- Verified in Ghidra against eqgame.exe: FUN_1402b18c0, FUN_1402af76c, FUN_1402a90d0,
-- FUN_1404504d0 (AssistModeCheckbox), FUN_140228c00 (/assistmerc). See docs/mercenary.md 4.1.

-- Skip it if this returns a row:
--   SELECT COLUMN_NAME FROM information_schema.COLUMNS
--   WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'mercs' AND COLUMN_NAME = 'AutoAssist';

ALTER TABLE `mercs`
    ADD COLUMN `AutoAssist` TINYINT(1) UNSIGNED NOT NULL DEFAULT 0 AFTER `DrakkinDetails`;

-- Post-migration check: every existing mercenary starts with auto-assist off, which matches the
-- client's own default for a freshly hired mercenary.
--   SELECT COUNT(*) FROM mercs WHERE AutoAssist <> 0;
