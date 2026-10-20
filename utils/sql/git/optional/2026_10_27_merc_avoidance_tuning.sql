-- 2026_10_27_merc_avoidance_tuning.sql
--
-- Seeds merc_stats.Avoidance (added by 2026_10_26_merc_avoidance.sql / migration 9333) with modest
-- per-archetype values instead of leaving every row at 0.
--
-- Read the axis correction first
-- ------------------------------
-- proficiency_id is NOT tank / healer / dps. It is the mercenary's TRAINING TIER:
--
--   proficiency_id 1 = Apprentice
--   proficiency_id 2 = Journeyman
--
-- The archetype is class_id. merc_npc_types.name spells this out for all 40 rows:
--
--   "Apprentice Tank - Tier I"        class_id  1, proficiency_id 1, tier_id 1
--   "Journeyman Tank - Tier II"       class_id  1, proficiency_id 2, tier_id 2
--   "Apprentice Healer - Tier V"      class_id  2, proficiency_id 1, tier_id 5
--   "Apprentice Melee DPS - Tier I"   class_id  9, proficiency_id 1, tier_id 1
--   "Apprentice Caster DPS - Tier I"  class_id 12, proficiency_id 1, tier_id 1
--
-- So this migration tunes on both axes: class_id for the archetype, proficiency_id for the training
-- tier. (The earlier draft of this file assumed proficiency_id was the archetype. It is not, and the
-- numbers below are keyed on class_id.)
--
-- What the shipped data actually says about the two axes
-- ------------------------------------------------------
-- Averaged over merc_stats joined to merc_npc_types, AC is the archetype discriminator, and the
-- ordering is identical at every level band:
--
--   archetype      avg AC L1-10   avg AC L51-70   avg ATK L1-10   avg HP L1-10
--   Tank      (1)        206.4          2268.9           159.6            289
--   Healer    (2)        159.5          1754.5            96.2            228
--   Melee DPS (9)        118.1          1300.6           162.1            244
--   Caster DPS(12)        76.7           846.7            47.8            184
--
-- Tank : Caster is 2.69 : 1 at L1-10 and 2.68 : 1 at L51-70, so the weighting is a fixed property of
-- the archetype, not something that drifts with level.
--
-- proficiency_id is the training tier, and Journeyman is uniformly better than Apprentice on the same
-- archetype: AC +15 to +20 percent, HP +60 percent, and strictly more stances in merc_stance_entries
-- (class 2 Apprentice {1,2} vs Journeyman {1,2,3,4}; class 1 Apprentice {1,5} vs Journeyman {1,5,6}).
-- Journeyman also carries higher AGI in merc_stats, which already raises compute_defense() on its own,
-- so the tier multiplier here is deliberately smaller than the AC gap.
--
-- Why the formula is a percentage of the mercenary's own defense
-- --------------------------------------------------------------
-- Mob::compute_defense() (zone/attack.cpp) is:
--
--     GetSkill(Defense) * 400 / 225
--   + 8000 * (AGI - 40) / 36000            (Combat:LegacyComputeDefense = false)
--   + itembonuses.AvoidMeleeChance         (0: mercenaries have no gear model)
--   + CastToNPC()->GetAvoidanceRating()    (IsNPC(); Merc is `class Merc : public NPC`)
--
-- The first two terms are already computed from the mercenary's own class and level, and they are
-- nearly identical across archetypes -- at level 10 every archetype lands at 93..98. A flat bonus or a
-- single percentage would therefore give a Tank and a Caster DPS the same number, which is not what
-- the archetype data or the live patch notes describe.
--
-- So: take the mercenary's own compute_defense() at its own class and level, then apply an archetype
-- percentage and a training-tier multiplier. The base term is read straight out of skill_caps and
-- merc_stats, so the seeded value tracks the same formula the server evaluates at runtime, and it
-- stays a constant *relative* bonus from level 1 to 85 rather than a flat number that dominates at
-- low levels and vanishes at high ones.
--
-- The archetype percentages are the one deliberate design input here, and they are ordered to match
-- the AC ordering above while being far flatter than it. AC already gives a Tank 2.69x a Caster's
-- mitigation; giving the Tank 2.0x a Caster's avoidance on top would compound. 20 / 15 / 12 / 10 keeps
-- the ordering the live patch notes describe -- "Increased the total HP and evasion of tank
-- mercenaries. Increased the evasion of healer mercenaries. Increased the evasion, offense, and HP of
-- melee damage mercenaries" (Allakhazam eq:mercenaries) -- without doubling down on it. Every archetype
-- gets a real number, because every archetype got evasion live.
--
-- Scale check against real EQ mobs: in a stock peq database the mobs that do carry a non-zero
-- npc_types.avoidance use 5 at level 55, 15 at 65, 25 at 66, 35 at 67, 45 at 68. The values this
-- migration writes land in the same band at the same levels (Tank 90 / Healer 62 / Melee 53 / Caster
-- 39 at level 50), so a mercenary ends up roughly as hard to avoid as a tuned named mob of its level,
-- and no more.
--
-- Resulting ladder
-- ----------------
--   archetype        Apprentice L1 / L10 / L50 / L85      Journeyman L1 / L10 / L50 / L85
--   Tank      (1)          3 /  19 /  90 / 148                4 /  25 / 114 / 188
--   Healer    (2)          2 /  14 /  62 /  99                2 /  18 /  78 / 125
--   Melee DPS (9)          2 /  12 /  53 /  87                2 /  15 /  67 / 110
--   Caster DPS(12)         1 /   9 /  39 /  63                2 /  12 /  50 /  80
--
-- For the two level-10 test mercenaries: Serkog (class 2, Journeyman) gets 18, so its Evasion row goes
-- 106 -> 124. Edecr (class 1, Journeyman) gets 25, so 108 -> 133.
--
-- Notes on the expression
-- -----------------------
-- * FLOOR() on the two terms reproduces C++ integer division in compute_defense().
-- * GREATEST(AGI - 40, 0) clamps the AGI term at zero. C++ lets it go negative for a sub-40 AGI mob
--   and then clamps the whole defense with std::max(1, defense); clamping the term instead keeps this
--   bonus from being computed off a near-zero base at level 1.
-- * GREATEST(1, ...) means no mercenary is left with a zero bonus once the column is in use.
-- * LEAST(250, ...) is a guard rail against a future merc_stats row with an absurd AGI.
-- * merc_stats.level always equals merc_stats.clientlevel, and clientlevel is the owner level the row
--   is selected by, so joining skill_caps on ms.level matches the level Merc::GetLevel() will have.
-- * merc_stats only covers clientlevel 1..85, and skill_caps covers 1..100, so every row joins.
--
-- To revert: UPDATE merc_stats SET Avoidance = 0;
-- To re-tune after editing the percentages: UPDATE merc_stats SET Avoidance = 0; then re-run this file.

UPDATE `merc_stats` ms
JOIN `merc_npc_types` n ON n.`merc_npc_type_id` = ms.`merc_npc_type_id`
JOIN `skill_caps` sc  ON sc.`class_id` = n.`class_id`
                      AND sc.`level`   = ms.`level`
                      AND sc.`skill_id` = 15                    -- EQ::skills::SkillDefense
SET ms.`Avoidance` = LEAST(250, GREATEST(1, ROUND(
    ( FLOOR(sc.`cap` * 400 / 225)
    + FLOOR(8000 * GREATEST(ms.`AGI` - 40, 0) / 36000) )
    * CASE n.`class_id`
        WHEN  1 THEN 0.20   -- Tank
        WHEN  2 THEN 0.15   -- Healer
        WHEN  9 THEN 0.12   -- Melee DPS
        WHEN 12 THEN 0.10   -- Caster DPS
        ELSE 0.10           -- any archetype added later
      END
    * CASE n.`proficiency_id`
        WHEN 2 THEN 1.25    -- Journeyman
        ELSE 1.00           -- Apprentice
      END
)))
WHERE ms.`Avoidance` = 0;
