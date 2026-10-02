-- mod-playerbot-raid-finder: raid definitions.
-- One row = one queueable raid. Add a raid by inserting a row, then `.rf reload`.
--   difficulty: 0 = 10 normal, 1 = 25 normal, 2 = 10 heroic, 3 = 25 heroic
--   min_gear:   playerbots mixed gear score (quality-weighted average item level), 0 = disabled
--   experimental: 1 = no dedicated playerbot raid strategy (bots use generic AI)

CREATE TABLE IF NOT EXISTS `raid_finder_definition` (
  `id`            VARCHAR(16)      NOT NULL,
  `display_name`  VARCHAR(64)      NOT NULL,
  `map_id`        INT UNSIGNED     NOT NULL,
  `difficulty`    TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `min_level`     TINYINT UNSIGNED NOT NULL DEFAULT 80,
  `min_gear`      INT UNSIGNED     NOT NULL DEFAULT 0,
  `tank_count`    TINYINT UNSIGNED NOT NULL DEFAULT 2,
  `healer_count`  TINYINT UNSIGNED NOT NULL DEFAULT 2,
  `dps_count`     TINYINT UNSIGNED NOT NULL DEFAULT 6,
  `enabled`       TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `experimental`  TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `sort_order`    INT              NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-playerbot-raid-finder raid definitions';

INSERT IGNORE INTO `raid_finder_definition`
  (`id`, `display_name`, `map_id`, `difficulty`, `min_level`, `min_gear`, `tank_count`, `healer_count`, `dps_count`, `enabled`, `experimental`, `sort_order`)
VALUES
  ('NAXX10', 'Naxxramas 10',             533, 0, 80, 0, 2, 2,  6, 1, 0, 10),
  ('NAXX25', 'Naxxramas 25',             533, 1, 80, 0, 2, 5, 18, 1, 0, 11),
  ('ULD10',  'Ulduar 10',                603, 0, 80, 0, 2, 2,  6, 1, 0, 20),
  ('ULD25',  'Ulduar 25',                603, 1, 80, 0, 2, 5, 18, 1, 0, 21),
  ('TOC10',  'Trial of the Crusader 10', 649, 0, 80, 0, 2, 2,  6, 1, 1, 30),
  ('TOC25',  'Trial of the Crusader 25', 649, 1, 80, 0, 2, 5, 18, 1, 1, 31),
  ('ICC10',  'Icecrown Citadel 10',      631, 0, 80, 0, 2, 2,  6, 1, 0, 40),
  ('ICC25',  'Icecrown Citadel 25',      631, 1, 80, 0, 2, 5, 18, 1, 0, 41);
