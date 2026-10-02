-- mod-playerbot-raid-finder: level band for bots + Classic / TBC / remaining WotLK raids.
--   max_level: highest level a BOT may have for this raid (0 = no cap). Real players only need min_level.
--   Menu tier is derived from min_level: <70 Classic, <80 The Burning Crusade, else Wrath.
--   experimental = 1: no dedicated playerbot raid strategy (ZG, AQ40, Sunwell, ToC, Ruby Sanctum).

ALTER TABLE `raid_finder_definition`
  ADD COLUMN `max_level` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `min_level`;

-- Re-number the original WotLK rows into the tiered menu order.
UPDATE `raid_finder_definition` SET `sort_order` = 310 WHERE `id` = 'NAXX10';
UPDATE `raid_finder_definition` SET `sort_order` = 311 WHERE `id` = 'NAXX25';
UPDATE `raid_finder_definition` SET `sort_order` = 350 WHERE `id` = 'ULD10';
UPDATE `raid_finder_definition` SET `sort_order` = 351 WHERE `id` = 'ULD25';
UPDATE `raid_finder_definition` SET `sort_order` = 360 WHERE `id` = 'TOC10';
UPDATE `raid_finder_definition` SET `sort_order` = 361 WHERE `id` = 'TOC25';
UPDATE `raid_finder_definition` SET `sort_order` = 370 WHERE `id` = 'ICC10';
UPDATE `raid_finder_definition` SET `sort_order` = 371 WHERE `id` = 'ICC25';

INSERT IGNORE INTO `raid_finder_definition`
  (`id`, `display_name`, `map_id`, `difficulty`, `min_level`, `max_level`, `min_gear`, `tank_count`, `healer_count`, `dps_count`, `enabled`, `experimental`, `sort_order`)
VALUES
  -- Classic (bots 60-69)
  ('MC',    'Molten Core',            409, 0, 60, 69, 0, 4, 10, 26, 1, 0, 100),
  ('BWL',   'Blackwing Lair',         469, 0, 60, 69, 0, 4, 10, 26, 1, 0, 110),
  ('ZG',    'Zul''Gurub',             309, 0, 60, 69, 0, 2,  5, 13, 1, 1, 120),
  ('AQ20',  'Ruins of Ahn''Qiraj',    509, 0, 60, 69, 0, 2,  5, 13, 1, 0, 130),
  ('ONY40', 'Onyxia''s Lair 40',      249, 2, 60, 69, 0, 4, 10, 26, 1, 0, 145),
  ('AQ40',  'Temple of Ahn''Qiraj',   531, 0, 60, 69, 0, 4, 10, 26, 1, 1, 140),
  -- The Burning Crusade (bots 70-79)
  ('KARA',  'Karazhan',               532, 0, 70, 79, 0, 2,  3,  5, 1, 0, 200),
  ('ZA',    'Zul''Aman',              568, 0, 70, 79, 0, 2,  3,  5, 1, 0, 210),
  ('GRUUL', 'Gruul''s Lair',          565, 0, 70, 79, 0, 3,  6, 16, 1, 0, 220),
  ('MAG',   'Magtheridon''s Lair',    544, 0, 70, 79, 0, 3,  6, 16, 1, 0, 230),
  ('SSC',   'Serpentshrine Cavern',   548, 0, 70, 79, 0, 3,  6, 16, 1, 0, 240),
  ('TK',    'Tempest Keep: The Eye',  550, 0, 70, 79, 0, 3,  6, 16, 1, 0, 250),
  ('HYJAL', 'Battle for Mount Hyjal', 534, 0, 70, 79, 0, 3,  6, 16, 1, 0, 260),
  ('BT',    'Black Temple',           564, 0, 70, 79, 0, 3,  6, 16, 1, 0, 270),
  ('SWP',   'Sunwell Plateau',        580, 0, 70, 79, 0, 3,  6, 16, 1, 1, 280),
  -- Wrath of the Lich King (bots 80)
  ('VOA10', 'Vault of Archavon 10',   624, 0, 80,  0, 0, 2,  2,  6, 1, 0, 300),
  ('VOA25', 'Vault of Archavon 25',   624, 1, 80,  0, 0, 2,  5, 18, 1, 0, 301),
  ('OS10',  'Obsidian Sanctum 10',    615, 0, 80,  0, 0, 2,  2,  6, 1, 0, 320),
  ('OS25',  'Obsidian Sanctum 25',    615, 1, 80,  0, 0, 2,  5, 18, 1, 0, 321),
  ('EOE10', 'Eye of Eternity 10',     616, 0, 80,  0, 0, 2,  2,  6, 1, 0, 330),
  ('EOE25', 'Eye of Eternity 25',     616, 1, 80,  0, 0, 2,  5, 18, 1, 0, 331),
  ('ONY10', 'Onyxia''s Lair 10',      249, 0, 80,  0, 0, 2,  2,  6, 1, 0, 340),
  ('ONY25', 'Onyxia''s Lair 25',      249, 1, 80,  0, 0, 2,  5, 18, 1, 0, 341),
  ('RS10',  'Ruby Sanctum 10',        724, 0, 80,  0, 0, 2,  2,  6, 1, 1, 380),
  ('RS25',  'Ruby Sanctum 25',        724, 1, 80,  0, 0, 2,  5, 18, 1, 1, 381);
