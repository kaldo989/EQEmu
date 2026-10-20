-- ============================================================================
-- Mercenary Alternate Advancement -- DRAFT schema (Laurion / SoF)
--
-- Derived statically from the client's `0x78e8` ability-definition record
-- (ten u32 fields `f0..f9` plus a `{u32 lo, u32 hi}` array).  Evidence and
-- confidence levels are recorded in docs/mercenary.md, section 4.9
-- "Resolved: which fN is the ability id".
--
-- Column provenance legend used in COMMENTs below:
--   [PROVEN]    the client demonstrably reads this value in this role
--   [CANDIDATE] best static inference; confirm against a live 0x78e8 capture
--   [PACKET]    this column is copied verbatim into a wire field
--
-- Naming follows utils/sql/merc_tables_bootstrap.sql; the shape follows the
-- player AA tables aa_ability / aa_ranks / aa_rank_effects / aa_rank_prereqs
-- (utils/sql/peq_aa_tables_post_rework.sql) so that zone/aa.cpp patterns can be
-- reused.  NOTE: `aa_ranks.title_sid` / `aa_ranks.desc_sid` already establish
-- the "_sid = dbstr_us.txt string id" convention used here.
--
-- HOW THIS IS APPLIED.
-- There is no entry for any of these tables in
-- common/database/database_update_manifest.h.  The server never creates them.
-- Import this file yourself:
--
--     mysql -u <user> -p <database> < utils/sql/git/optional/2026_10_18_mercenary_aa_schema_draft.sql
--
-- Re-running it is safe: every CREATE TABLE is IF NOT EXISTS and every seed row
-- is INSERT IGNORE, so an existing table and its player purchases are left alone.
-- Because INSERT IGNORE never updates a row, correcting a seeded value by editing
-- this file and re-importing does nothing -- edit the table instead.
--
-- Companion files, in order:
--   2026_10_18_mercenary_aa_schema_draft.sql        (this file)
--   2026_10_20_db_str_mercenary_aa_strings.sql      (categories 37 and 38)
--   2026_10_19_merc_dbstring_ids.sql                (merc_types / merc_templates)
--   2026_10_22_merc_auto_assist.sql                 (mercs.AutoAssist -- required)
--   2026_10_21_merc_aa_rank_index.sql               (only if you imported this file
--                                                   before rank_index existed)
-- ============================================================================

/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;

-- ----------------------------------------------------------------------------
-- merc_aa_ability
--
-- One row per 0x78e8 record.  The client's model is one ability id PER RANK,
-- all sharing one group_id, with rank_index (f5) numbering them:

--   FUN_1401a71f0          the granted container holds one ability id per group
--                          and keeps whichever granted ability has the highest f5
--   GetMercCurrentRank     returns that ability's f5
--   GetMercMaxRank         returns the group's member count, NOT f3
--   FUN_1401a4ff0          increments the group's member counter once per record
--                          that shares its f4

-- So f3 only drives the "1 rank" / "N ranks" label; the maximum the client will
-- let you buy is COUNT(*) of enabled rows sharing group_id.
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `merc_aa_ability`
(
    `ability_id`  int(11) UNSIGNED NOT NULL
        COMMENT 'f0 [PROVEN][PACKET] Hash key (mgr+0x68, id & 0x3f), CListWnd item data, and the first u32 of the 0x3861 buy request. Known live values: 101,201,301,401,1000,1001,1100,1200,1300,1400,1500,1600,1700,5000.',
    `name_sid`    int(11) NOT NULL DEFAULT -1
        COMMENT 'f1 [PROVEN][PACKET] dbstr_us.txt category 37 (eMercenaryAbilityName). Client: CDBStr__GetString(pinstCDBStr, *(rec + 4), 0x25).',
    `desc_sid`    int(11) NOT NULL DEFAULT -1
        COMMENT 'f2 [CANDIDATE][PACKET] dbstr_us.txt category 38 (eMercenaryAbilityDescription) for rank 1. Per-rank descriptions appear to be desc_sid + (rank - 1): Improved Health = 101,102,103,104.',
    `max_rank`    int(11) NOT NULL DEFAULT 1
        COMMENT '[LOCAL] Rank count for auditing only. It is NOT what the client enforces: CAltAbilityData__GetMercMaxRank returns the number of enabled rows sharing group_id, and f3 turned out to be the purchase cost (see `cost`). Kept so merc_aa_rank row counts can be checked against an explicit expectation.',
    `group_id`    int(11) NOT NULL DEFAULT -1
        COMMENT 'f4 [PROVEN][PACKET] Rank-family key. FUN_1401a4ff0 builds its outer 64-bucket index on f4 & 0x3f, and CAAWnd__Update restores the list selection after a refresh by comparing rec[4] (== f4).',
    `rank_index`  int(11) NOT NULL DEFAULT 1
        COMMENT 'f5 [PROVEN][PACKET] Rank index inside group_id. FUN_1401a71f0 keeps the highest-rank_index granted ability of a group, CAltAbilityData__GetMercCurrentRank returns it, and FUN_1401a4ff0 indexes the group''s 16-bucket sub-container on it.',
    `aa_type`     int(11) NOT NULL DEFAULT 0
        COMMENT 'f6 [PROVEN][PACKET] dbstr_us.txt category 36 (eMercenaryAbilityType): 1 General, 2 Tank, 3 Healer, 4 Melee DPS, 5 Caster DPS. FUN_14033cd80 renders the Type column of AAW_MercAAList with CDBStr__GetString(f6, 36) when f6 > 0, and FUN_1401a5fa0 builds AAW_MercTypeFilter from the 0x3e8c container keyed by this same id. Values outside 1..5 make the client print "Unknown DB String <n>-36" in the row; 0 renders a blank cell.',
    `cost`        int(11) NOT NULL DEFAULT 0
        COMMENT 'f3 [PROVEN][PACKET] Point cost of this rank. FUN_14033cd80 prints it in the cost column (DAT_14081c65c) while the ability is still buyable, and FUN_1401a53f0 refuses the purchase unless cost <= *(int *)(PlayerClient + 0x2738), the pool that 0x50e6 action 1 writes.',
    `currency_id` int(11) NOT NULL DEFAULT 0
        COMMENT 'f8 [PROVEN][PACKET] Currency for the purchase. FUN_1401a53f0 calls FUN_1401251e0(mgr + 0x488, f8); a zero or unknown id skips the currency branch. 0 = mercenary AA points.',
    `level_req`   int(11) NOT NULL DEFAULT 0
        COMMENT 'f7 [PROVEN][PACKET] Minimum PLAYER level (not mercenary level). FUN_1401a53f0 compares it against the local player level getter at vtable + 0xe8 and blocks the purchase when it is higher.',
    `flags`       int(11) NOT NULL DEFAULT 0
        COMMENT '[LOCAL] Raw bitfield kept so a live capture can be dropped straight into f9 without a schema change. f9 still has no identified consumer, so the encoder sends 0.',
    `enabled`     tinyint(3) UNSIGNED NOT NULL DEFAULT 1
        COMMENT '[LOCAL] never sent. Lets a server disable an ability without deleting the row.',
    PRIMARY KEY (`ability_id`) USING BTREE,
    INDEX `idx_merc_aa_group` (`group_id`) USING BTREE,
    INDEX `idx_merc_aa_type` (`aa_type`) USING BTREE
) ENGINE = InnoDB CHARACTER SET = latin1 COLLATE = latin1_swedish_ci ROW_FORMAT = Compact;

-- ----------------------------------------------------------------------------
-- merc_aa_rank
--
-- One row per rank.  This is the `{ u32 lo; u32 hi; } array[array_count]` tail
-- of the 0x78e8 record, emitted by FUN_1401a6ea0 in wire order.
--
-- array_count is NOT sent independently -- it is the number of pairs the server
-- chooses to emit, so it must equal max_rank above.
--
-- The meaning of lo/hi is the single largest remaining unknown in the mercenary
-- AA family.  Two readings survive static analysis:
--   A) lo = rank index (1..max_rank), hi = per-rank desc_sid
--   B) lo = per-rank cost, hi = per-rank effect value
-- Reading A is favoured because the client has 279 category-38 descriptions but
-- only 14 category-37 names, which means per-rank descriptions must arrive
-- somewhere other than the record header.  Column names below are deliberately
-- neutral (`lo_value` / `hi_value`) so a capture can rename them without a
-- data migration.
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `merc_aa_rank`
(
    `ability_id` int(11) UNSIGNED NOT NULL,
    `rank_index` int(11) UNSIGNED NOT NULL
        COMMENT '1-based rank number. [CANDIDATE] equals wire `lo` under reading A.',
    `lo_value`   int(11) NOT NULL DEFAULT 0
        COMMENT 'First u32 of the wire pair. [CANDIDATE] rank index (reading A) or rank cost (reading B).',
    `hi_value`   int(11) NOT NULL DEFAULT 0
        COMMENT 'Second u32 of the wire pair. [CANDIDATE] per-rank desc_sid (reading A) or effect value (reading B).',
    `desc_sid`   int(11) NOT NULL DEFAULT -1
        COMMENT 'Server-side convenience column. If -1, derive as merc_aa_ability.desc_sid + rank_index - 1.',
    PRIMARY KEY (`ability_id`, `rank_index`) USING BTREE,
    CONSTRAINT `fk_merc_aa_rank_ability` FOREIGN KEY (`ability_id`)
        REFERENCES `merc_aa_ability` (`ability_id`) ON DELETE CASCADE
) ENGINE = InnoDB CHARACTER SET = latin1 COLLATE = latin1_swedish_ci ROW_FORMAT = Compact;

-- ----------------------------------------------------------------------------
-- merc_aa_rank_effects
--
-- What a purchased rank actually does to the mercenary.  This table is NOT part
-- of any AA packet: its output is folded into the 0x1f38 stat table (keys 3..32)
-- that the Merc tab renders.  Modelled directly on aa_rank_effects.
-- `stat_key` uses the 0x1f38 key numbering documented in docs/mercenary.md 4.8.
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `merc_aa_rank_effects`
(
    `ability_id` int(11) UNSIGNED NOT NULL,
    `rank_index` int(11) UNSIGNED NOT NULL,
    `slot`       int(11) UNSIGNED NOT NULL DEFAULT 1,
    `stat_key`   int(11) NOT NULL DEFAULT 0
        COMMENT '0x1f38 key: 3 HP, 4 MAX_HP, 5 MANA, 6 MAX_MANA, 7 ENDURANCE, 8 MAX_ENDURANCE, 9 MITIGATION, 10 EVASION, 11 OFFENSE, 12 ACCURACY, 13 HASTE, 14 STR, 15 STA, 16 INT, 17 WIS, 18 AGI, 19 DEX, 20 CHA, 21..27 HEROIC_*, 28 COMBAT_HP_REGEN, 29 COMBAT_MANA_REGEN, 30 COMBAT_END_REGEN, 31 HEAL_AMOUNT, 32 SPELL_DAMAGE',
    `base_value` int(11) NOT NULL DEFAULT 0,
    `max_value`  int(11) NOT NULL DEFAULT 0,
    PRIMARY KEY (`ability_id`, `rank_index`, `slot`) USING BTREE,
    CONSTRAINT `fk_merc_aa_effect_rank` FOREIGN KEY (`ability_id`, `rank_index`)
        REFERENCES `merc_aa_rank` (`ability_id`, `rank_index`) ON DELETE CASCADE
) ENGINE = InnoDB CHARACTER SET = latin1 COLLATE = latin1_swedish_ci ROW_FORMAT = Compact;

-- ----------------------------------------------------------------------------
-- merc_aa_prereqs
--
-- Mirrors aa_rank_prereqs.  The client has no visible mercenary AA prerequisite
-- UI, so this is optional; keep it if the server wants to gate rank N behind
-- another ability.  Buying is otherwise linear (rank N requires rank N-1), which
-- needs no table.
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `merc_aa_prereqs`
(
    `ability_id` int(11) UNSIGNED NOT NULL,
    `rank_index` int(11) UNSIGNED NOT NULL,
    `req_ability_id` int(11) UNSIGNED NOT NULL,
    `req_rank`   int(11) UNSIGNED NOT NULL DEFAULT 1,
    PRIMARY KEY (`ability_id`, `rank_index`, `req_ability_id`) USING BTREE
) ENGINE = InnoDB CHARACTER SET = latin1 COLLATE = latin1_swedish_ci ROW_FORMAT = Compact;

-- ----------------------------------------------------------------------------
-- merc_aa_merc_types
--
-- Which ability lines are offered to which mercenary proficiency.  Feeds the
-- AAW_MercTypeFilter combo box.  `aa_type` alone may be sufficient; this table
-- exists so the mapping can be corrected after a capture without a schema
-- change.  `merc_proficiency_id` matches merc_types / merc_subtypes proficiency.
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `merc_aa_merc_types`
(
    `merc_proficiency_id` int(11) UNSIGNED NOT NULL,
    `ability_id`          int(11) UNSIGNED NOT NULL,
    PRIMARY KEY (`merc_proficiency_id`, `ability_id`) USING BTREE,
    CONSTRAINT `fk_merc_aa_type_ability` FOREIGN KEY (`ability_id`)
        REFERENCES `merc_aa_ability` (`ability_id`) ON DELETE CASCADE
) ENGINE = InnoDB CHARACTER SET = latin1 COLLATE = latin1_swedish_ci ROW_FORMAT = Compact;

-- ----------------------------------------------------------------------------
-- player_merc_aa
--
-- Per-character purchased ranks.  Feeds:
--   0x0516  -> count x { u32 aa_id, u32 value }   (full state on zone-in)
--   0x3e8c  -> u32 n, u32 ids[n], u32 m, u32 values[m]   (n MUST equal m)
--   0x50e6  -> the incremental action result after a 0x3861 purchase
-- `aa_value` is the value the client stores via FUN_14061e0c0(player, id, value);
-- for a rank-based ability that is the current rank, but the client treats it as
-- an opaque u32, so keep it as such.
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `player_merc_aa`
(
    `char_id`    int(11) UNSIGNED NOT NULL,
    `ability_id` int(11) UNSIGNED NOT NULL,
    `rank`       int(11) UNSIGNED NOT NULL DEFAULT 0
        COMMENT 'Highest rank purchased. 0 = known but not purchased.',
    `aa_value`   int(11) NOT NULL DEFAULT 0
        COMMENT 'Opaque u32 replayed to the client as the `value` half of 0x0516 / 0x3e8c / 0x50e6.',
    PRIMARY KEY (`char_id`, `ability_id`) USING BTREE,
    INDEX `idx_player_merc_aa_char` (`char_id`) USING BTREE,
    CONSTRAINT `fk_player_merc_aa_ability` FOREIGN KEY (`ability_id`)
        REFERENCES `merc_aa_ability` (`ability_id`) ON DELETE CASCADE
) ENGINE = InnoDB CHARACTER SET = latin1 COLLATE = latin1_swedish_ci ROW_FORMAT = Compact;

-- ----------------------------------------------------------------------------
-- merc_aa_currency
--
-- The three counters the Merc AA tab renders.  These are NOT carried by the AA
-- packets -- they arrive as 0x1f38 keys 0, 1, 2 and bind to label types
--   335 AAW_MercAAExpPct       (LABELTYPE_MERCENARY_AA_EXP_PERCENT)
--   336 AAW_MercAACurrentCount (unassigned points)
--   337 AAW_MercAAAssignedCount(assigned points)
-- Client-side mirrors inside CAltAdvManager: player+0x2730 (u64, set by 0x50e6
-- action 0 and 1), player+0x2738 and player+0x273c (set by action 1).
--
-- Kept in its own table rather than as columns on `characters` so the existing
-- AA blob and char_stats queries are untouched.
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `merc_aa_currency`
(
    `char_id`            int(11) UNSIGNED NOT NULL,
    `aa_exp_pct`         float NOT NULL DEFAULT 0
        COMMENT '0x1f38 key 0. Stored as float; scale to the int32 the wire expects when encoding.',
    `aa_points`          int(11) NOT NULL DEFAULT 0
        COMMENT '0x1f38 key 1 -> label 336.',
    `aa_points_assigned` int(11) NOT NULL DEFAULT 0
        COMMENT '0x1f38 key 2 -> label 337.',
    PRIMARY KEY (`char_id`) USING BTREE
) ENGINE = InnoDB CHARACTER SET = latin1 COLLATE = latin1_swedish_ci ROW_FORMAT = Compact;

/*!40101 SET character_set_client = @saved_cs_client */;

-- ============================================================================
-- Seed data: the 14 ability names the client already knows
--
-- Extracted from G:/Games/LS/dbstr_us.txt category 37 (eMercenaryAbilityName).
-- ability_id is seeded equal to name_sid because every observed category-37 id
-- is a plausible ability id and the two are indistinguishable without a capture.
-- If a live 0x78e8 shows f0 != f1, only these two columns need editing -- the
-- wire encoder reads them separately.
--
-- max_rank / group_id are placeholders pending a capture, and cost is seeded to 1
-- because the client only needs some non-zero gate to grey the row out. aa_type is
-- derived, not guessed: the client's own category-38 rank descriptions name the
-- mercenary each ability applies to ("your tank mercenary", "your healer mercenary",
-- "your melee damage mercenary", "your damage caster mercenary", or a plain "your
-- mercenary"), which maps onto category 36 ids 2, 3, 4, 5 and 1. See
-- 2026_10_25_merc_aa_type.sql. Ability 101 has four ranks confirmed from category 38
-- (101,102,103,104); its max_rank is set to 4 so the COUNT(*) == max_rank invariant
-- holds.
--
-- SHAPE WARNING.  These 14 rows are one row per FAMILY, which is what the
-- category-37 list looks like, but the client needs one row per RANK.  Ability
-- 101 declares max_rank 4 while only one enabled row carries group_id 101, so
-- the client treats Improved Health as a single-rank ability and ranks 2..4 are
-- unbuyable.  Zone::LoadMercenaryAAAbilities() says so at load time:
--
--   merc_aa_ability 101: max_rank is 4 but 1 enabled abilities share group_id
--   101. The client derives maximum rank from the group member count, so it will
--   treat this family as 1 ranks max.
--
-- The real per-rank ability ids cannot be derived statically; they have to come
-- from a live 0x78e8 capture.  Nothing here is invented to paper over that.
-- Note 1001 and 1100 share the name "Steadfast Resolve": they are two different
-- ability lines with the same label, which is exactly why group_id exists.
-- ============================================================================
INSERT IGNORE INTO `merc_aa_ability`
    (`ability_id`, `name_sid`, `desc_sid`, `max_rank`, `group_id`, `rank_index`,
     `aa_type`, `cost`, `currency_id`, `level_req`, `flags`, `enabled`)
VALUES
    ( 101,  101,  101, 4,  101, 1, 1, 1, 0, 0, 0, 1),  -- Improved Health (4 ranks confirmed)   General
    ( 201,  201,  201, 1,  201, 1, 1, 1, 0, 0, 0, 1),  -- Improved Mana                        General
    ( 301,  301,  301, 1,  301, 1, 1, 1, 0, 0, 0, 1),  -- Improved Melee Defense               General
    ( 401,  401,  401, 1,  401, 1, 1, 1, 0, 0, 0, 1),  -- Improved Spell Resistance            General
    (1000, 1000, 1000, 1, 1000, 1, 2, 1, 0, 0, 0, 1),  -- Furious Wrath                        Tank
    (1001, 1001, 1001, 1, 1001, 1, 2, 1, 0, 0, 0, 1),  -- Steadfast Resolve                    Tank
    (1100, 1100, 1100, 1, 1100, 1, 2, 1, 0, 0, 0, 1),  -- Steadfast Resolve (second line)      Tank
    (1200, 1200, 1200, 1, 1200, 1, 3, 1, 0, 0, 0, 1),  -- Subtle Magic                         Healer
    (1300, 1300, 1300, 1, 1300, 1, 3, 1, 0, 0, 0, 1),  -- Healing Acumen                       Healer
    (1400, 1400, 1400, 1, 1400, 1, 4, 1, 0, 0, 0, 1),  -- Subtle Strikes                       Melee DPS
    (1500, 1500, 1500, 1, 1500, 1, 4, 1, 0, 0, 0, 1),  -- Rising Fury                          Melee DPS
    (1600, 1600, 1600, 1, 1600, 1, 5, 1, 0, 0, 0, 1),  -- Silent Arcanum                       Caster DPS
    (1700, 1700, 1700, 1, 1700, 1, 5, 1, 0, 0, 0, 1),  -- Mystic Destruction                   Caster DPS
    (5000, 5000, 5000, 1, 5000, 1, 1, 1, 0, 0, 0, 1);  -- Essence of the Dragon                General

-- One rank row per rank.  This is the { u32 lo; u32 hi; } array tail of the
-- 0x78e8 record, so an ability with max_rank N and zero rows here is sent to the
-- client with array_count = 0 and no rank data at all.  Every enabled ability
-- therefore needs COUNT(merc_aa_rank rows) == max_rank.
--
-- lo = rank_index, hi = that rank's category-38 desc_sid (reading A).
--
-- Improved Health is the only multi-rank ladder confirmed by static analysis:
--   101 -> 125 HP, 102 -> 150 HP, 103 -> 225 HP, 104 -> 325 HP
-- The other thirteen are single-rank, and their rank-1 descriptions are the
-- category-38 rows with the same id as the ability (verified present in db_str).
INSERT IGNORE INTO `merc_aa_rank`
    (`ability_id`, `rank_index`, `lo_value`, `hi_value`, `desc_sid`)
VALUES
    ( 101, 1, 1, 101, 101),  -- 125 HP
    ( 101, 2, 2, 102, 102),  -- 150 HP
    ( 101, 3, 3, 103, 103),  -- 225 HP
    ( 101, 4, 4, 104, 104),  -- 325 HP
    ( 201, 1, 1, 201, 201),  -- Improved Mana: +115 max mana
    ( 301, 1, 1, 301, 301),  -- Improved Melee Defense
    ( 401, 1, 1, 401, 401),  -- Improved Spell Resistance
    (1000, 1, 1, 1000, 1000),  -- Furious Wrath
    (1001, 1, 1, 1001, 1001),  -- Steadfast Resolve
    (1100, 1, 1, 1100, 1100),  -- Steadfast Resolve (second line)
    (1200, 1, 1, 1200, 1200),  -- Subtle Magic
    (1300, 1, 1, 1300, 1300),  -- Healing Acumen
    (1400, 1, 1, 1400, 1400),  -- Subtle Strikes
    (1500, 1, 1, 1500, 1500),  -- Rising Fury
    (1600, 1, 1, 1600, 1600),  -- Silent Arcanum
    (1700, 1, 1, 1700, 1700),  -- Mystic Destruction
    (5000, 1, 1, 5000, 5000);  -- Essence of the Dragon

-- ============================================================================
-- What the server actually does with these tables
-- (already implemented; no further code is needed to go with this file)
--
-- Zone::LoadMercenaryAAAbilities() builds the definition list and audits it.
-- Client::SendMercenaryAAAbilities() / SendMercenaryAAState() send it on zone-in,
-- definitions first, then grants -- FUN_1401a71f0 discards a grant whose
-- definition is missing, so the order is not cosmetic.
--
-- 0x78e8  u32 count
--         for each enabled merc_aa_ability row:
--             f0 = ability_id      f5 = rank_index
--             f1 = name_sid        f6 = 0   (cost is NOT sent)
--             f2 = desc_sid        f7 = 0   (currency_id is NOT sent)
--             f3 = max_rank        f8 = 0   (level_req is NOT sent)
--             f4 = group_id        f9 = 0   (flags is NOT sent)
--             u32 array_count = COUNT(*) FROM merc_aa_rank WHERE ability_id = f0
--             for each rank row in rank_index order: { lo_value, hi_value }
--
-- 0x0516  u32 count
--         for each player_merc_aa row for this character: { ability_id, aa_value }
--
-- 0x3e8c  u32 n, u32 ids[n], u32 m, u32 values[m]   -- n MUST equal m or the
--         client silently discards the packet.
--
-- 0x50e6  u32 arg0, u32 arg1, u64 arg2, u32 action  -- 20 bytes
--         action 0/1: arg2 -> merc AA point pool (arg0/arg1 -> assigned/current)
--         action 2/7: grant rank   (arg0 = ability_id, arg1 = value)
--         action 3/8: clear rank
--         action 4:   refund all
--         actions 5 and 6 are not handled by the client -- never send them.
--
-- 0x3861  (C->S, 20 bytes) u32 ability_id, u32 zero, u64 zero, u32 mode
--         mode = 2 normally; (rank & 0xff) + 9 when the client requests a
--         specific rank.
-- ============================================================================
