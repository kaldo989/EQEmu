-- 2026_10_27_merc_accuracy_tuning.sql  (migration 9335)
--
-- Seeds merc_stats.Accuracy with modest per-archetype values, using the same method as migration 9334
-- for merc_stats.Avoidance: take the mercenary's own combat term at its own class and level, then scale
-- it by archetype and training tier.
--
-- The term being scaled is Mob::compute_tohit() (zone/attack.cpp:159), which is what
-- merc_stats.Accuracy feeds into:
--
--     int tohit = GetSkill(SkillOffense) + 7;
--     tohit += GetSkill(skillinuse);
--     if (IsNPC()) { ... tohit += CastToNPC()->GetAccuracyRating(); }
--
-- Chain: merc_stats.Accuracy -> NPCType::accuracy_rating (ZoneDatabase::GetMercenaryType)
--        -> NPC::accuracy_rating (NPC::NPC) -> compute_tohit() (combat)
--        -> Merc::GetDisplayAccuracy() -> 0x1f38 keys 12 and 13 (the Merc tab's Accuracy row)
--
-- Combat:LegacyComputeDefense and Combat:UseMobStaticOffenseSkill are both false (common/ruletypes.h),
-- so the skill-driven branch is the live one and the static-offense override does not apply.
--
-- Which weapon skill the "+ GetSkill(skillinuse)" term actually uses
-- ------------------------------------------------------------------
-- Not Hand-to-Hand. Merc::Attack() (zone/merc.cpp:4047) forwards to NPC::Attack()
-- (zone/attack.cpp:2246), and NPC::Attack sets the attack skill from GetPrimSkill() --
-- NPCType::prim_melee_type, which ZoneDatabase::GetMercenaryType() fills from
-- merc_weaponinfo.prim_melee_type. It is only overridden by the equipped item's ItemType when
-- equipment[slotPrimary] is non-zero. In the stock data every archetype has a real one-handed primary:
--
--   archetype          class_id   prim_melee_type   skill
--   Tank                    1            1          1H Slashing
--   Healer                  2            0          1H Blunt
--   Melee DPS               9           36          1H Piercing
--   Caster DPS             12            0          1H Blunt
--
-- This matters a lot: Hand-to-Hand is capped far below the real primaries (30..40 vs 75 at level 10),
-- so keying the bonus off H2H would understate a mercenary's accuracy by ~35 points at level 10. The
-- display path in zone/merc.cpp was fixed to use GetMeleeSkill() = prim_melee_type for the same reason.
-- zone/mob_info.cpp passes SkillHandtoHand for generic mobs; that is not correct for mercenaries.
--
-- Base term
-- ---------
--     offense_cap + 7 + weapon_cap
--
-- read from skill_caps at the mercenary's own class and level: skill 33 (SkillOffense) and skill
-- prim_melee_type. merc_stats.level always equals merc_stats.clientlevel, and clientlevel is the owner
-- level the row is selected by, so this is the level Merc::GetLevel() will have. All 2380 merc_stats
-- rows join exactly one merc_weaponinfo row and one pair of skill_caps rows (verified: 0 unmatched,
-- 0 duplicate weapon rows).
--
-- Why a percentage of the mercenary's own to-hit
-- ---------------------------------------------
-- Same reason as 9334. The base term is already level- and class-correct, so a percentage stays a
-- constant relative bonus from level 1 to 85 instead of a flat number that dominates at low levels and
-- vanishes at high ones. It also means the seeded value tracks the exact expression the server
-- evaluates at runtime, so the Accuracy row and the combat roll cannot drift apart.
--
-- Archetype percentages
-- ---------------------
-- Ordered by the shipped ATK column in merc_stats, which is the data's own hit/damage axis:
--
--   archetype        avg ATK L1-10   avg AC L1-10   avg HP L1-10   pct used
--   Melee DPS (9)         162.1           118.1          244        0.20
--   Tank      (1)         159.6           206.4          289        0.18
--   Healer    (2)          96.2           159.5          228        0.13
--   Caster DPS(12)         47.8            76.7          184        0.08
--
-- Melee DPS and Tank are effectively tied on ATK (162.1 vs 159.6) and are the two archetypes whose
-- job is landing swings. Caster DPS has a third of a Tank's ATK because its damage comes from spells,
-- so its melee accuracy is the least consequential -- but it still gets a real number, because a
-- caster mercenary that never lands a hit when it does swing is not live-like.
--
-- The spread is deliberately flatter than the ATK spread (2.5x from Melee to Caster in the data, 2.5x
-- here is kept but applied to a term that is already archetype-correct through the Offense and weapon
-- caps: Tank 137 vs Caster 87 at level 10). Compounding the full ATK ratio on top would make caster
-- mercenaries nearly unable to hit.
--
-- Training tier multiplier
-- ------------------------
-- proficiency_id is the training tier, not the archetype: 1 Apprentice, 2 Journeyman. Journeyman is
-- uniformly better than Apprentice on the same archetype (+15..20% AC, +60% HP, strictly more stances
-- in merc_stance_entries), so it gets 1.25x. See 2026_10_27_merc_avoidance_tuning.sql for the full
-- axis correction.
--
-- Resulting ladder
-- ----------------
--   archetype        Apprentice L1 / L10 / L50 / L85      Journeyman L1 / L10 / L50 / L85
--   Melee DPS (9)          4 /  26 /  93 / 130                6 /  33 / 117 / 163
--   Tank      (1)          5 /  25 /  84 / 117                6 /  31 / 105 / 147
--   Healer    (2)          2 /  14 /  56 /  73                3 /  17 /  69 /  91
--   Caster DPS(12)         1 /   7 /  27 /  37                2 /   9 /  34 /  47
--
-- Scale check against real EQ mobs: npc_types.accuracy is non-zero for a minority of mobs and the
-- per-level averages run 0.1 at low levels to 65-100 at level 60-70, with named mobs reaching 150-1500.
-- The values written here (25-33 at level 10, 84-117 at level 50) sit in the ordinary band, well below
-- the named-mob outliers.
--
-- Effect on the two level-10 test mercenaries
-- -------------------------------------------
-- Serkog (Healer, Journeyman, npc_type 17): Accuracy 17. compute_tohit(1H Blunt) = 50 + 7 + 50 + 17 =
--   124, so GetTotalToHit = (124 + 10) * 121 / 100 = 162. The Accuracy row goes 141 -> 162.
-- Edecr (Tank, Journeyman, npc_type 7): Accuracy 31. compute_tohit(1H Slashing) = 55 + 7 + 75 + 31 =
--   168, so GetTotalToHit = (168 + 10) * 121 / 100 = 215. The Accuracy row goes 177 -> 215.
--
-- Notes on the expression
-- -----------------------
-- * GREATEST(1, ...) means no mercenary is left with a zero bonus once the column is in use.
-- * LEAST(250, ...) is a guard rail; 250 is already above what a level-85 Journeyman Melee DPS needs.
-- * merc_stats only covers clientlevel 1..85 and skill_caps covers 1..100, so every row joins.
-- * statscale is 100 in all 2380 rows and Mercs:ScaleRate is 100, so Merc::ScaleStats() does not
--   double-apply a level curve on top of the one already baked into the skill caps here. If a server
--   sets statscale away from 100, that is the intended knob, not this migration.
--
-- To revert: UPDATE merc_stats SET Accuracy = 0;
-- To re-tune after editing the percentages: UPDATE merc_stats SET Accuracy = 0; then re-run this file.

UPDATE `merc_stats` ms
JOIN `merc_npc_types` n  ON n.`merc_npc_type_id` = ms.`merc_npc_type_id`
JOIN `merc_weaponinfo` w ON w.`merc_npc_type_id` = ms.`merc_npc_type_id`
                        AND w.`minlevel` <= ms.`level`
                        AND w.`maxlevel` >= ms.`level`
JOIN `skill_caps` so     ON so.`class_id` = n.`class_id`
                        AND so.`level`   = ms.`level`
                        AND so.`skill_id` = 33                       -- EQ::skills::SkillOffense
JOIN `skill_caps` sw     ON sw.`class_id` = n.`class_id`
                        AND sw.`level`   = ms.`level`
                        AND sw.`skill_id` = w.`prim_melee_type`      -- the skill NPC::Attack() uses
SET ms.`Accuracy` = LEAST(250, GREATEST(1, ROUND(
    ( so.`cap` + 7 + sw.`cap` )
    * CASE n.`class_id`
        WHEN  9 THEN 0.20   -- Melee DPS
        WHEN  1 THEN 0.18   -- Tank
        WHEN  2 THEN 0.13   -- Healer
        WHEN 12 THEN 0.08   -- Caster DPS
        ELSE 0.12           -- any archetype added later
      END
    * CASE n.`proficiency_id`
        WHEN 2 THEN 1.25    -- Journeyman
        ELSE 1.00           -- Apprentice
      END
)))
WHERE ms.`Accuracy` = 0;
