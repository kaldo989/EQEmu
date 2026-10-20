-- Mercenary alternate-advancement strings: categories 37 and 38
--
-- APPLY MANUALLY -- there is no manifest entry for this; the 293 rows are too large
-- to embed in a C++ string literal (MSVC rejects literals over 65535 bytes with
-- error C2026), which is exactly why it lives here instead.
--   mysql -u <user> -p <database> < utils/sql/git/optional/2026_10_20_db_str_mercenary_aa_strings.sql
--
-- Why this migration exists.
--
-- The Laurion client resolves every mercenary AA ability name and description out of its own
-- dbstr_us.txt, and the packets only carry the numeric ids: the client calls
--   CDBStr__GetString(pinstCDBStr, *(rec + 4), 0x25)   -- 0x25 == 37 == eMercenaryAbilityName
-- and the description through category 38. The server has no copy of those two categories:
--   SELECT type, COUNT(*) FROM db_str WHERE type IN (37, 38) GROUP BY type;
--   (no rows)
-- so a mercenary AA row could seed a name_sid / desc_sid that the server could not check, and a
-- typo would only ever show up as a blank label in the client's AA window plus
-- "Unknown DB String %d-%d" in the client log -- invisible from the server.
--
-- Provenance.
--
-- Every value below is copied verbatim from the Laurion client's dbstr_us.txt. Nothing here is
-- authored server-side. Counts: 14 names, 279 descriptions, matching the client exactly.
-- The text is pure ASCII and contains no backslashes; single quotes are doubled for MySQL.
--
-- Idempotent by construction: db_str's primary key is (id, type) and every statement is
-- INSERT IGNORE, so a database whose PEQ dump already carries these categories is left alone.
--
-- Categories 21 through 25 and 36 already ship in db_str and are not touched here.

-- category 37 : eMercenaryAbilityName  (14 rows)

INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (101, 37, 'Improved Health');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (201, 37, 'Improved Mana');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (301, 37, 'Improved Melee Defense');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (401, 37, 'Improved Spell Resistance');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1000, 37, 'Furious Wrath');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1001, 37, 'Steadfast Resolve');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1100, 37, 'Steadfast Resolve');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1200, 37, 'Subtle Magic');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1300, 37, 'Healing Acumen');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1400, 37, 'Subtle Strikes');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1500, 37, 'Rising Fury');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1600, 37, 'Silent Arcanum');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1700, 37, 'Mystic Destruction');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (5000, 37, 'Essence of the Dragon');

-- category 38 : eMercenaryAbilityDescription  (279 rows)

INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (101, 38, 'This passive ability increases your mercenary''s maximum health by 125 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (102, 38, 'This passive ability increases your mercenary''s maximum health by 150 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (103, 38, 'This passive ability increases your mercenary''s maximum health by 225 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (104, 38, 'This passive ability increases your mercenary''s maximum health by 325 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (105, 38, 'This passive ability increases your mercenary''s maximum health by 435 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (106, 38, 'This passive ability increases your mercenary''s maximum health by 550 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (107, 38, 'This passive ability increases your mercenary''s maximum health by 725 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (108, 38, 'This passive ability increases your mercenary''s maximum health by 860 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (109, 38, 'This passive ability increases your mercenary''s maximum health by 1000 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (110, 38, 'This passive ability increases your mercenary''s maximum health by 1175 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (111, 38, 'This passive ability increases your mercenary''s maximum health by 1350 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (112, 38, 'This passive ability increases your mercenary''s maximum health by 1550 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (113, 38, 'This passive ability increases your mercenary''s maximum health by 1800 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (114, 38, 'This passive ability increases your mercenary''s maximum health by 2400 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (115, 38, 'This passive ability increases your mercenary''s maximum health by 3050 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (116, 38, 'This passive ability increases your mercenary''s maximum health by 3675 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (117, 38, 'This passive ability increases your mercenary''s maximum health by 4475 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (118, 38, 'This passive ability increases your mercenary''s maximum health by 9650 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (119, 38, 'This passive ability increases your mercenary''s maximum health by 14800 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (120, 38, 'This passive ability increases your mercenary''s maximum health by 20000 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (121, 38, 'This passive ability increases your mercenary''s maximum health by 23500 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (122, 38, 'This passive ability increases your mercenary''s maximum health by 27000 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (123, 38, 'This passive ability increases your mercenary''s maximum health by 30000 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (124, 38, 'This passive ability increases your mercenary''s maximum health by 35000 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (125, 38, 'This passive ability increases your mercenary''s maximum health by 40000 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (126, 38, 'This passive ability increases your mercenary''s maximum health by 46000 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (127, 38, 'This passive ability increases your mercenary''s maximum health by 52000 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (128, 38, 'This passive ability increases your mercenary''s maximum health by 58000 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (129, 38, 'This passive ability increases your mercenary''s maximum health by 64000 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (130, 38, 'This passive ability increases your mercenary''s maximum health by 70000 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (131, 38, 'This passive ability increases your mercenary''s maximum health by 76000 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (201, 38, 'This passive ability increases your mercenary''s maximum mana by 115 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (202, 38, 'This passive ability increases your mercenary''s maximum mana by 195 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (203, 38, 'This passive ability increases your mercenary''s maximum mana by 265 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (204, 38, 'This passive ability increases your mercenary''s maximum mana by 350 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (205, 38, 'This passive ability increases your mercenary''s maximum mana by 445 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (206, 38, 'This passive ability increases your mercenary''s maximum mana by 530 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (207, 38, 'This passive ability increases your mercenary''s maximum mana by 620 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (208, 38, 'This passive ability increases your mercenary''s maximum mana by 715 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (209, 38, 'This passive ability increases your mercenary''s maximum mana by 800 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (210, 38, 'This passive ability increases your mercenary''s maximum mana by 895 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (211, 38, 'This passive ability increases your mercenary''s maximum mana by 995 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (212, 38, 'This passive ability increases your mercenary''s maximum mana by 1095 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (213, 38, 'This passive ability increases your mercenary''s maximum mana by 1195 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (214, 38, 'This passive ability increases your mercenary''s maximum mana by 1365 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (215, 38, 'This passive ability increases your mercenary''s maximum mana by 2050 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (216, 38, 'This passive ability increases your mercenary''s maximum mana by 2600 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (217, 38, 'This passive ability increases your mercenary''s maximum mana by 3685 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (218, 38, 'This passive ability increases your mercenary''s maximum mana by 7460 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (219, 38, 'This passive ability increases your mercenary''s maximum mana by 11250 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (220, 38, 'This passive ability increases your mercenary''s maximum mana by 15000 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (221, 38, 'This passive ability increases your mercenary''s maximum mana by 15800 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (222, 38, 'This passive ability increases your mercenary''s maximum mana by 16600 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (223, 38, 'This passive ability increases your mercenary''s maximum mana by 17500 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (224, 38, 'This passive ability increases your mercenary''s maximum mana by 18750 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (225, 38, 'This passive ability increases your mercenary''s maximum mana by 20000 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (226, 38, 'This passive ability increases your mercenary''s maximum mana by 21500 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (227, 38, 'This passive ability increases your mercenary''s maximum mana by 23000 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (228, 38, 'This passive ability increases your mercenary''s maximum mana by 24500 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (229, 38, 'This passive ability increases your mercenary''s maximum mana by 26000 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (230, 38, 'This passive ability increases your mercenary''s maximum mana by 27500 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (231, 38, 'This passive ability increases your mercenary''s maximum mana by 29000 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (301, 38, 'This passive ability increases your mercenary''s <c "#66CCFF">base armor class (prior to soft cap multipliers)</c> by 10 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (302, 38, 'This passive ability increases your mercenary''s <c "#66CCFF">base armor class (prior to soft cap multipliers)</c> by 20 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (303, 38, 'This passive ability increases your mercenary''s <c "#66CCFF">base armor class (prior to soft cap multipliers)</c> by 30 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (304, 38, 'This passive ability increases your mercenary''s <c "#66CCFF">base armor class (prior to soft cap multipliers)</c> by 40 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (305, 38, 'This passive ability increases your mercenary''s <c "#66CCFF">base armor class (prior to soft cap multipliers)</c> by 50 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (306, 38, 'This passive ability increases your mercenary''s <c "#66CCFF">base armor class (prior to soft cap multipliers)</c> by 60 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (307, 38, 'This passive ability increases your mercenary''s <c "#66CCFF">base armor class (prior to soft cap multipliers)</c> by 70 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (308, 38, 'This passive ability increases your mercenary''s <c "#66CCFF">base armor class (prior to soft cap multipliers)</c> by 80 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (309, 38, 'This passive ability increases your mercenary''s <c "#66CCFF">base armor class (prior to soft cap multipliers)</c> by 90 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (310, 38, 'This passive ability increases your mercenary''s <c "#66CCFF">base armor class (prior to soft cap multipliers)</c> by 100 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (311, 38, 'This passive ability increases your mercenary''s <c "#66CCFF">base armor class (prior to soft cap multipliers)</c> by 110 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (312, 38, 'This passive ability increases your mercenary''s <c "#66CCFF">base armor class (prior to soft cap multipliers)</c> by 120 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (313, 38, 'This passive ability increases your mercenary''s <c "#66CCFF">base armor class (prior to soft cap multipliers)</c> by 130 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (314, 38, 'This passive ability increases your mercenary''s <c "#66CCFF">base armor class (prior to soft cap multipliers)</c> by 140 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (315, 38, 'This passive ability increases your mercenary''s <c "#66CCFF">base armor class (prior to soft cap multipliers)</c> by 150 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (316, 38, 'This passive ability increases your mercenary''s <c "#66CCFF">base armor class (prior to soft cap multipliers)</c> by 165 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (317, 38, 'This passive ability increases your mercenary''s <c "#66CCFF">base armor class (prior to soft cap multipliers)</c> by 180 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (318, 38, 'This passive ability increases your mercenary''s <c "#66CCFF">base armor class (prior to soft cap multipliers)</c> by 200 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (319, 38, 'This passive ability increases your mercenary''s <c "#66CCFF">base armor class (prior to soft cap multipliers)</c> by 300 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (320, 38, 'This passive ability increases your mercenary''s <c "#66CCFF">base armor class (prior to soft cap multipliers)</c> by 400 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (321, 38, 'This passive ability increases your mercenary''s <c "#66CCFF">base armor class (prior to soft cap multipliers)</c> by 520 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (322, 38, 'This passive ability increases your mercenary''s <c "#66CCFF">base armor class (prior to soft cap multipliers)</c> by 640 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (323, 38, 'This passive ability increases your mercenary''s <c "#66CCFF">base armor class (prior to soft cap multipliers)</c> by 760 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (324, 38, 'This passive ability increases your mercenary''s <c "#66CCFF">base armor class (prior to soft cap multipliers)</c> by 880 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (401, 38, 'This passive ability improves your mercenary''s cold, disease, fire, magic, and poison resistances by 10 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (402, 38, 'This passive ability improves your mercenary''s cold, disease, fire, magic, and poison resistances by 20 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (403, 38, 'This passive ability improves your mercenary''s cold, disease, fire, magic, and poison resistances by 30 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (404, 38, 'This passive ability improves your mercenary''s cold, disease, fire, magic, and poison resistances by 40 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (405, 38, 'This passive ability improves your mercenary''s cold, disease, fire, magic, and poison resistances by 50 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (406, 38, 'This passive ability improves your mercenary''s cold, disease, fire, magic, and poison resistances by 60 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (407, 38, 'This passive ability improves your mercenary''s cold, disease, fire, magic, and poison resistances by 70 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (408, 38, 'This passive ability improves your mercenary''s cold, disease, fire, magic, and poison resistances by 80 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (409, 38, 'This passive ability improves your mercenary''s cold, disease, fire, magic, and poison resistances by 90 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (410, 38, 'This passive ability improves your mercenary''s cold, disease, fire, magic, and poison resistances by 100 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (411, 38, 'This passive ability improves your mercenary''s cold, disease, fire, magic, and poison resistances by 110 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (412, 38, 'This passive ability improves your mercenary''s cold, disease, fire, magic, and poison resistances by 120 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (413, 38, 'This passive ability improves your mercenary''s cold, disease, fire, magic, and poison resistances by 130 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (414, 38, 'This passive ability improves your mercenary''s cold, disease, fire, magic, and poison resistances by 140 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (415, 38, 'This passive ability improves your mercenary''s cold, disease, fire, magic, and poison resistances by 150 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (416, 38, 'This passive ability improves your mercenary''s cold, disease, fire, magic, and poison resistances by 160 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (417, 38, 'This passive ability improves your mercenary''s cold, disease, fire, magic, and poison resistances by 170 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (418, 38, 'This passive ability improves your mercenary''s cold, disease, fire, magic, and poison resistances by 180 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (419, 38, 'This passive ability improves your mercenary''s cold, disease, fire, magic, and poison resistances by 190 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (420, 38, 'This passive ability improves your mercenary''s cold, disease, fire, magic, and poison resistances by 200 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (421, 38, 'This passive ability improves your mercenary''s cold, disease, fire, magic, and poison resistances by 210 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (422, 38, 'This passive ability improves your mercenary''s cold, disease, fire, magic, and poison resistances by 220 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (423, 38, 'This passive ability improves your mercenary''s cold, disease, fire, magic, and poison resistances by 230 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (424, 38, 'This passive ability improves your mercenary''s cold, disease, fire, magic, and poison resistances by 240 points.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1000, 38, 'This passive ability increases the amount of hate generated by your tank mercenary''s attacks and spells by 2%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1001, 38, 'This passive ability increases the amount of hate generated by your tank mercenary''s attacks and spells by 4%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1002, 38, 'This passive ability increases the amount of hate generated by your tank mercenary''s attacks and spells by 6%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1003, 38, 'This passive ability increases the amount of hate generated by your tank mercenary''s attacks and spells by 8%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1004, 38, 'This passive ability increases the amount of hate generated by your tank mercenary''s attacks and spells by 10%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1005, 38, 'This passive ability increases the amount of hate generated by your tank mercenary''s attacks and spells by 12%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1006, 38, 'This passive ability increases the amount of hate generated by your tank mercenary''s attacks and spells by 14%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1007, 38, 'This passive ability increases the amount of hate generated by your tank mercenary''s attacks and spells by 16%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1008, 38, 'This passive ability increases the amount of hate generated by your tank mercenary''s attacks and spells by 18%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1009, 38, 'This passive ability increases the amount of hate generated by your tank mercenary''s attacks and spells by 20%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1010, 38, 'This passive ability increases the amount of hate generated by your tank mercenary''s attacks and spells by 22%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1011, 38, 'This passive ability increases the amount of hate generated by your tank mercenary''s attacks and spells by 24%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1012, 38, 'This passive ability increases the amount of hate generated by your tank mercenary''s attacks and spells by 25%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1013, 38, 'This passive ability increases the amount of hate generated by your tank mercenary''s attacks and spells by 26%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1014, 38, 'This passive ability increases the amount of hate generated by your tank mercenary''s attacks and spells by 27%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1015, 38, 'This passive ability increases the amount of hate generated by your tank mercenary''s attacks and spells by 28%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1016, 38, 'This passive ability increases the amount of hate generated by your tank mercenary''s attacks and spells by 29%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1017, 38, 'This passive ability increases the amount of hate generated by your tank mercenary''s attacks and spells by 30%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1018, 38, 'This passive ability increases the amount of hate generated by your tank mercenary''s attacks and spells by 31%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1019, 38, 'This passive ability increases the amount of hate generated by your tank mercenary''s attacks and spells by 32%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1020, 38, 'This passive ability increases the amount of hate generated by your tank mercenary''s attacks and spells by 33%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1100, 38, 'This passive ability reduces incoming melee damage for your tank mercenary by 1%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1101, 38, 'This passive ability reduces incoming melee damage for your tank mercenary by 2%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1102, 38, 'This passive ability reduces incoming melee damage for your tank mercenary by 3%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1103, 38, 'This passive ability reduces incoming melee damage for your tank mercenary by 4%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1104, 38, 'This passive ability reduces incoming melee damage for your tank mercenary by 5%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1105, 38, 'This passive ability reduces incoming melee damage for your tank mercenary by 6%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1106, 38, 'This passive ability reduces incoming melee damage for your tank mercenary by 7%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1107, 38, 'This passive ability reduces incoming melee damage for your tank mercenary by 8%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1108, 38, 'This passive ability reduces incoming melee damage for your tank mercenary by 9%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1109, 38, 'This passive ability reduces incoming melee damage for your tank mercenary by 10%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1110, 38, 'This passive ability reduces incoming melee damage for your tank mercenary by 11%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1111, 38, 'This passive ability reduces incoming melee damage for your tank mercenary by 12%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1112, 38, 'This passive ability reduces incoming melee damage for your tank mercenary by 13%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1113, 38, 'This passive ability reduces incoming melee damage for your tank mercenary by 14%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1114, 38, 'This passive ability reduces incoming melee damage for your tank mercenary by 15%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1115, 38, 'This passive ability reduces incoming melee damage for your tank mercenary by 16%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1116, 38, 'This passive ability reduces incoming melee damage for your tank mercenary by 17%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1117, 38, 'This passive ability reduces incoming melee damage for your tank mercenary by 18%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1118, 38, 'This passive ability reduces incoming melee damage for your tank mercenary by 19%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1119, 38, 'This passive ability reduces incoming melee damage for your tank mercenary by 20%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1120, 38, 'This passive ability reduces incoming melee damage for your tank mercenary by 21%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1200, 38, 'This passive ability reduces the amount of hate generated by your healer mercenary''s spells by 3%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1201, 38, 'This passive ability reduces the amount of hate generated by your healer mercenary''s spells by 6%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1202, 38, 'This passive ability reduces the amount of hate generated by your healer mercenary''s spells by 9%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1203, 38, 'This passive ability reduces the amount of hate generated by your healer mercenary''s spells by 12%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1204, 38, 'This passive ability reduces the amount of hate generated by your healer mercenary''s spells by 15%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1205, 38, 'This passive ability reduces the amount of hate generated by your healer mercenary''s spells by 18%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1206, 38, 'This passive ability reduces the amount of hate generated by your healer mercenary''s spells by 21%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1207, 38, 'This passive ability reduces the amount of hate generated by your healer mercenary''s spells by 24%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1208, 38, 'This passive ability reduces the amount of hate generated by your healer mercenary''s spells by 27%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1209, 38, 'This passive ability reduces the amount of hate generated by your healer mercenary''s spells by 30%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1210, 38, 'This passive ability reduces the amount of hate generated by your healer mercenary''s spells by 31%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1211, 38, 'This passive ability reduces the amount of hate generated by your healer mercenary''s spells by 32%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1212, 38, 'This passive ability reduces the amount of hate generated by your healer mercenary''s spells by 33%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1213, 38, 'This passive ability reduces the amount of hate generated by your healer mercenary''s spells by 34%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1214, 38, 'This passive ability reduces the amount of hate generated by your healer mercenary''s spells by 35%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1215, 38, 'This passive ability reduces the amount of hate generated by your healer mercenary''s spells by 36%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1216, 38, 'This passive ability reduces the amount of hate generated by your healer mercenary''s spells by 37%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1217, 38, 'This passive ability reduces the amount of hate generated by your healer mercenary''s spells by 38%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1218, 38, 'This passive ability reduces the amount of hate generated by your healer mercenary''s spells by 39%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1219, 38, 'This passive ability reduces the amount of hate generated by your healer mercenary''s spells by 40%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1220, 38, 'This passive ability reduces the amount of hate generated by your healer mercenary''s spells by 41%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1300, 38, 'This passive ability increases the effectiveness of your healer mercenary''s instant-duration healing spells by 1%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1301, 38, 'This passive ability increases the effectiveness of your healer mercenary''s instant-duration healing spells by 2%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1302, 38, 'This passive ability increases the effectiveness of your healer mercenary''s instant-duration healing spells by 3%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1303, 38, 'This passive ability increases the effectiveness of your healer mercenary''s instant-duration healing spells by 4%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1304, 38, 'This passive ability increases the effectiveness of your healer mercenary''s instant-duration healing spells by 5%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1305, 38, 'This passive ability increases the effectiveness of your healer mercenary''s instant-duration healing spells by 6%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1306, 38, 'This passive ability increases the effectiveness of your healer mercenary''s instant-duration healing spells by 7%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1307, 38, 'This passive ability increases the effectiveness of your healer mercenary''s instant-duration healing spells by 8%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1308, 38, 'This passive ability increases the effectiveness of your healer mercenary''s instant-duration healing spells by 9%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1309, 38, 'This passive ability increases the effectiveness of your healer mercenary''s instant-duration healing spells by 10%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1310, 38, 'This passive ability increases the effectiveness of your healer mercenary''s instant-duration healing spells by 11%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1311, 38, 'This passive ability increases the effectiveness of your healer mercenary''s instant-duration healing spells by 12%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1312, 38, 'This passive ability increases the effectiveness of your healer mercenary''s instant-duration healing spells by 13%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1313, 38, 'This passive ability increases the effectiveness of your healer mercenary''s instant-duration healing spells by 14%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1314, 38, 'This passive ability increases the effectiveness of your healer mercenary''s instant-duration healing spells by 15%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1315, 38, 'This passive ability increases the effectiveness of your healer mercenary''s instant-duration healing spells by 16%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1316, 38, 'This passive ability increases the effectiveness of your healer mercenary''s instant-duration healing spells by 17%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1317, 38, 'This passive ability increases the effectiveness of your healer mercenary''s instant-duration healing spells by 18%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1318, 38, 'This passive ability increases the effectiveness of your healer mercenary''s instant-duration healing spells by 19%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1319, 38, 'This passive ability increases the effectiveness of your healer mercenary''s instant-duration healing spells by 20%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1320, 38, 'This passive ability increases the effectiveness of your healer mercenary''s instant-duration healing spells by 21%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1400, 38, 'This passive ability reduces the amount of hate generated by your melee damage mercenary''s attacks and spells by 1%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1401, 38, 'This passive ability reduces the amount of hate generated by your melee damage mercenary''s attacks and spells by 2%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1402, 38, 'This passive ability reduces the amount of hate generated by your melee damage mercenary''s attacks and spells by 3%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1403, 38, 'This passive ability reduces the amount of hate generated by your melee damage mercenary''s attacks and spells by 4%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1404, 38, 'This passive ability reduces the amount of hate generated by your melee damage mercenary''s attacks and spells by 5%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1405, 38, 'This passive ability reduces the amount of hate generated by your melee damage mercenary''s attacks and spells by 6%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1406, 38, 'This passive ability reduces the amount of hate generated by your melee damage mercenary''s attacks and spells by 7%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1407, 38, 'This passive ability reduces the amount of hate generated by your melee damage mercenary''s attacks and spells by 8%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1408, 38, 'This passive ability reduces the amount of hate generated by your melee damage mercenary''s attacks and spells by 9%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1409, 38, 'This passive ability reduces the amount of hate generated by your melee damage mercenary''s attacks and spells by 10%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1410, 38, 'This passive ability reduces the amount of hate generated by your melee damage mercenary''s attacks and spells by 11%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1411, 38, 'This passive ability reduces the amount of hate generated by your melee damage mercenary''s attacks and spells by 12%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1412, 38, 'This passive ability reduces the amount of hate generated by your melee damage mercenary''s attacks and spells by 13%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1413, 38, 'This passive ability reduces the amount of hate generated by your melee damage mercenary''s attacks and spells by 14%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1414, 38, 'This passive ability reduces the amount of hate generated by your melee damage mercenary''s attacks and spells by 15%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1415, 38, 'This passive ability reduces the amount of hate generated by your melee damage mercenary''s attacks and spells by 16%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1416, 38, 'This passive ability reduces the amount of hate generated by your melee damage mercenary''s attacks and spells by 17%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1417, 38, 'This passive ability reduces the amount of hate generated by your melee damage mercenary''s attacks and spells by 18%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1418, 38, 'This passive ability reduces the amount of hate generated by your melee damage mercenary''s attacks and spells by 19%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1419, 38, 'This passive ability reduces the amount of hate generated by your melee damage mercenary''s attacks and spells by 20%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1420, 38, 'This passive ability reduces the amount of hate generated by your melee damage mercenary''s attacks and spells by 21%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1500, 38, 'This passive ability increases the damage your melee damage mercenary deals with all forms of melee combat by 1%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1501, 38, 'This passive ability increases the damage your melee damage mercenary deals with all forms of melee combat by 2%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1502, 38, 'This passive ability increases the damage your melee damage mercenary deals with all forms of melee combat by 3%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1503, 38, 'This passive ability increases the damage your melee damage mercenary deals with all forms of melee combat by 4%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1504, 38, 'This passive ability increases the damage your melee damage mercenary deals with all forms of melee combat by 5%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1505, 38, 'This passive ability increases the damage your melee damage mercenary deals with all forms of melee combat by 6%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1506, 38, 'This passive ability increases the damage your melee damage mercenary deals with all forms of melee combat by 7%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1507, 38, 'This passive ability increases the damage your melee damage mercenary deals with all forms of melee combat by 8%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1508, 38, 'This passive ability increases the damage your melee damage mercenary deals with all forms of melee combat by 9%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1509, 38, 'This passive ability increases the damage your melee damage mercenary deals with all forms of melee combat by 10%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1510, 38, 'This passive ability increases the damage your melee damage mercenary deals with all forms of melee combat by 11%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1511, 38, 'This passive ability increases the damage your melee damage mercenary deals with all forms of melee combat by 12%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1512, 38, 'This passive ability increases the damage your melee damage mercenary deals with all forms of melee combat by 13%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1513, 38, 'This passive ability increases the damage your melee damage mercenary deals with all forms of melee combat by 14%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1514, 38, 'This passive ability increases the damage your melee damage mercenary deals with all forms of melee combat by 15%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1515, 38, 'This passive ability increases the damage your melee damage mercenary deals with all forms of melee combat by 16%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1516, 38, 'This passive ability increases the damage your melee damage mercenary deals with all forms of melee combat by 17%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1517, 38, 'This passive ability increases the damage your melee damage mercenary deals with all forms of melee combat by 18%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1518, 38, 'This passive ability increases the damage your melee damage mercenary deals with all forms of melee combat by 19%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1519, 38, 'This passive ability increases the damage your melee damage mercenary deals with all forms of melee combat by 20%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1520, 38, 'This passive ability increases the damage your melee damage mercenary deals with all forms of melee combat by 21%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1600, 38, 'This passive ability reduces the amount of hate generated by your damage caster mercenary''s spells by 2%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1601, 38, 'This passive ability reduces the amount of hate generated by your damage caster mercenary''s spells by 4%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1602, 38, 'This passive ability reduces the amount of hate generated by your damage caster mercenary''s spells by 6%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1603, 38, 'This passive ability reduces the amount of hate generated by your damage caster mercenary''s spells by 8%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1604, 38, 'This passive ability reduces the amount of hate generated by your damage caster mercenary''s spells by 10%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1605, 38, 'This passive ability reduces the amount of hate generated by your damage caster mercenary''s spells by 12%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1606, 38, 'This passive ability reduces the amount of hate generated by your damage caster mercenary''s spells by 14%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1607, 38, 'This passive ability reduces the amount of hate generated by your damage caster mercenary''s spells by 16%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1608, 38, 'This passive ability reduces the amount of hate generated by your damage caster mercenary''s spells by 18%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1609, 38, 'This passive ability reduces the amount of hate generated by your damage caster mercenary''s spells by 20%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1610, 38, 'This passive ability reduces the amount of hate generated by your damage caster mercenary''s spells by 21%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1611, 38, 'This passive ability reduces the amount of hate generated by your damage caster mercenary''s spells by 22%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1612, 38, 'This passive ability reduces the amount of hate generated by your damage caster mercenary''s spells by 23%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1613, 38, 'This passive ability reduces the amount of hate generated by your damage caster mercenary''s spells by 24%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1614, 38, 'This passive ability reduces the amount of hate generated by your damage caster mercenary''s spells by 25%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1615, 38, 'This passive ability reduces the amount of hate generated by your damage caster mercenary''s spells by 26%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1616, 38, 'This passive ability reduces the amount of hate generated by your damage caster mercenary''s spells by 27%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1617, 38, 'This passive ability reduces the amount of hate generated by your damage caster mercenary''s spells by 28%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1618, 38, 'This passive ability reduces the amount of hate generated by your damage caster mercenary''s spells by 29%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1619, 38, 'This passive ability reduces the amount of hate generated by your damage caster mercenary''s spells by 30%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1620, 38, 'This passive ability reduces the amount of hate generated by your damage caster mercenary''s spells by 31%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1700, 38, 'This passive increases the damage dealt by your damage caster mercenary''s spells by 1%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1701, 38, 'This passive increases the damage dealt by your damage caster mercenary''s spells by 2%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1702, 38, 'This passive increases the damage dealt by your damage caster mercenary''s spells by 3%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1703, 38, 'This passive increases the damage dealt by your damage caster mercenary''s spells by 4%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1704, 38, 'This passive increases the damage dealt by your damage caster mercenary''s spells by 5%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1705, 38, 'This passive increases the damage dealt by your damage caster mercenary''s spells by 6%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1706, 38, 'This passive increases the damage dealt by your damage caster mercenary''s spells by 7%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1707, 38, 'This passive increases the damage dealt by your damage caster mercenary''s spells by 8%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1708, 38, 'This passive increases the damage dealt by your damage caster mercenary''s spells by 9%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1709, 38, 'This passive increases the damage dealt by your damage caster mercenary''s spells by 10%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1710, 38, 'This passive increases the damage dealt by your damage caster mercenary''s spells by 11%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1711, 38, 'This passive increases the damage dealt by your damage caster mercenary''s spells by 12%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1712, 38, 'This passive increases the damage dealt by your damage caster mercenary''s spells by 13%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1713, 38, 'This passive increases the damage dealt by your damage caster mercenary''s spells by 14%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1714, 38, 'This passive increases the damage dealt by your damage caster mercenary''s spells by 15%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1715, 38, 'This passive increases the damage dealt by your damage caster mercenary''s spells by 16%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1716, 38, 'This passive increases the damage dealt by your damage caster mercenary''s spells by 17%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1717, 38, 'This passive increases the damage dealt by your damage caster mercenary''s spells by 18%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1718, 38, 'This passive increases the damage dealt by your damage caster mercenary''s spells by 19%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1719, 38, 'This passive increases the damage dealt by your damage caster mercenary''s spells by 20%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (1720, 38, 'This passive increases the damage dealt by your damage caster mercenary''s spells by 21%.');
INSERT IGNORE INTO `db_str` (`id`, `type`, `value`) VALUES (5000, 38, 'This passive ability grants your mercenary immunity to Gust of Atrebe and Aura of the Kar`Zok.<br><br>This ability can be purchased once you have obtained the Progression ability Essence of the Dragon.');

-- Verification.
--
--   SELECT type, COUNT(*) FROM db_str WHERE type IN (37, 38) GROUP BY type;
--   -- expect 37 => 14, 38 => 279
--
--   -- every id the AA schema references must resolve:
--   SELECT a.ability_id, a.name_sid, a.desc_sid
--   FROM merc_aa_ability a
--   LEFT JOIN db_str sn ON sn.id = a.name_sid AND sn.type = 37
--   LEFT JOIN db_str sd ON sd.id = a.desc_sid AND sd.type = 38
--   WHERE sn.id IS NULL OR sd.id IS NULL;
--   -- expect no rows
