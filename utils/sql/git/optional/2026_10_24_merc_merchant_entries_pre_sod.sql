-- Mercenary merchant mapping for the pre-SoD liaison NPCs that actually spawn
-- ---------------------------------------------------------------------------
-- MANUAL IMPORT. Nothing in the server creates these rows.
--
-- Why this file exists
-- --------------------
-- Spawn filtering uses the rule `Expansion:CurrentExpansion` (an expansion NUMBER),
-- not `World:ExpansionSettings` (a bitmask). The filter is built in
-- common/repositories/criteria/content_filter_criteria.h and applied to `spawn2` by
-- ZoneDatabase::PopulateZoneSpawnList() (zone/spawn2.cpp).
--
-- With Expansion:CurrentExpansion = 9 (Dragons of Norrath) the Seeds of Destruction
-- liaison NPCs (spawn2.min_expansion = 15) never spawn. The pre-SoD variants
-- (spawn2.max_expansion = 14) spawn in their place. Those pre-SoD variants are still
-- class 71 (Class::MercenaryLiaison), so the Laurion client opens MMTW_MerchantWnd and
-- sends OP_MercenaryDataRequest (0x7397) - but they have no merc_merchant_entries row,
-- so Client::SendMercMerchantShopListLaurion() answers with 0 types and 0 mercenaries:
-- OP_MercenaryDataResponse with an 8-byte payload (u32 type_count = 0, u32 merc_count = 0).
-- The client then prints "You do not meet the requirements to purchase any mercenaries."
-- (hardcoded client string at eqgame.exe 0x1408f0360) and closes the window.
--
-- Verified on this database: exactly three class 71 liaisons pass the expansion filter
-- at CurrentExpansion = 9, and none of them has a merc_merchant_entries row.
--
--   npc_types  name              race  zone          spawn2      min/max expansion
--   202296     Guardian_Frendad  128   poknowledge   3264392     -1 / 14
--   383100     Jacob_Masterson   1     freeportwest  (SoD gate)  -1 / 14
--   383091     Laura_Masterson   1     freeportwest  (SoD gate)  -1 / 14
--
-- The alternative is to raise Expansion:CurrentExpansion to 15 (Seeds of Destruction)
-- or -1 (all). That makes the SoD liaisons spawn and filters the pre-SoD duplicates out,
-- but it also starts 77 additional spawn2 groups in poknowledge alone, so it is a much
-- larger change than this file.
--
-- Each row below mirrors the mapping the SoD-version of the same liaison already has:
--   202116 Guardian_Frendad -> template 16 (Default Iksar Mercenaries, 21 offers)
--   202115 Guardian_Drendle  -> template 1  (Default Human Mercenaries, 28 offers)

-- Preflight: what currently spawns as a liaison with nothing to sell?
SELECT
	nt.id   AS npc_types_id,
	nt.name AS npc_name,
	nt.race AS npc_race,
	s2.zone AS zone,
	s2.id   AS spawn2_id,
	s2.min_expansion,
	s2.max_expansion
FROM spawn2 s2
JOIN spawnentry se ON se.spawngroupID = s2.spawngroupID
JOIN npc_types nt ON nt.id = se.npcID
WHERE nt.class = 71
	AND (s2.min_expansion <= 9 OR s2.min_expansion = -1)
	AND (s2.max_expansion >= 9 OR s2.max_expansion = -1)
	AND (SELECT COUNT(*) FROM merc_merchant_entries mme WHERE mme.merchant_id = nt.id) = 0
ORDER BY nt.name;

-- Guardian Frendad, Plane of Knowledge (Iksar liaison, pre-SoD variant).
INSERT INTO merc_merchant_entries (merc_merchant_template_id, merchant_id)
SELECT 16, 202296
WHERE NOT EXISTS (
	SELECT 1 FROM merc_merchant_entries
	WHERE merchant_id = 202296 AND merc_merchant_template_id = 16
);

-- Jacob Masterson, Freeport West (Human liaison, pre-SoD variant).
INSERT INTO merc_merchant_entries (merc_merchant_template_id, merchant_id)
SELECT 1, 383100
WHERE NOT EXISTS (
	SELECT 1 FROM merc_merchant_entries
	WHERE merchant_id = 383100 AND merc_merchant_template_id = 1
);

-- Laura Masterson, Freeport West (Human liaison, pre-SoD variant).
INSERT INTO merc_merchant_entries (merc_merchant_template_id, merchant_id)
SELECT 1, 383091
WHERE NOT EXISTS (
	SELECT 1 FROM merc_merchant_entries
	WHERE merchant_id = 383091 AND merc_merchant_template_id = 1
);

-- Postcheck: every class 71 liaison that passes the CurrentExpansion = 9 filter should
-- now have at least one template with offers.
SELECT
	nt.id   AS npc_types_id,
	nt.name AS npc_name,
	nt.race AS npc_race,
	(SELECT COUNT(*) FROM merc_merchant_entries mme WHERE mme.merchant_id = nt.id) AS merchant_rows,
	(SELECT SUM((SELECT COUNT(*) FROM merc_merchant_template_entries mmte
				 WHERE mmte.merc_merchant_template_id = mme.merc_merchant_template_id))
		FROM merc_merchant_entries mme WHERE mme.merchant_id = nt.id) AS total_offers
FROM npc_types nt
WHERE nt.class = 71
	AND (SELECT COUNT(*) FROM spawn2 s2
		 JOIN spawnentry se ON se.spawngroupID = s2.spawngroupID
		 WHERE se.npcID = nt.id
			AND (s2.min_expansion <= 9 OR s2.min_expansion = -1)
			AND (s2.max_expansion >= 9 OR s2.max_expansion = -1)) > 0
ORDER BY nt.name;
