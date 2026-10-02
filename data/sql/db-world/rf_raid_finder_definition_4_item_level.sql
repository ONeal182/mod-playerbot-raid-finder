-- mod-playerbot-raid-finder: minimum AVERAGE ITEM LEVEL per raid (players and bots).
-- `min_gear` holds Player::GetAverageItemLevel() - the same metric as
-- dungeon_access_template.min_avg_item_level (empty slots count as 0). 0 disables the check.

UPDATE `raid_finder_definition` SET `min_gear` = 55 WHERE `id` = 'MC';
UPDATE `raid_finder_definition` SET `min_gear` = 62 WHERE `id` = 'BWL';
UPDATE `raid_finder_definition` SET `min_gear` = 55 WHERE `id` = 'ZG';
UPDATE `raid_finder_definition` SET `min_gear` = 58 WHERE `id` = 'AQ20';
UPDATE `raid_finder_definition` SET `min_gear` = 58 WHERE `id` = 'ONY40';
UPDATE `raid_finder_definition` SET `min_gear` = 66 WHERE `id` = 'AQ40';
UPDATE `raid_finder_definition` SET `min_gear` = 105 WHERE `id` = 'KARA';
UPDATE `raid_finder_definition` SET `min_gear` = 115 WHERE `id` = 'ZA';
UPDATE `raid_finder_definition` SET `min_gear` = 112 WHERE `id` = 'GRUUL';
UPDATE `raid_finder_definition` SET `min_gear` = 112 WHERE `id` = 'MAG';
UPDATE `raid_finder_definition` SET `min_gear` = 120 WHERE `id` = 'SSC';
UPDATE `raid_finder_definition` SET `min_gear` = 120 WHERE `id` = 'TK';
UPDATE `raid_finder_definition` SET `min_gear` = 128 WHERE `id` = 'HYJAL';
UPDATE `raid_finder_definition` SET `min_gear` = 134 WHERE `id` = 'BT';
UPDATE `raid_finder_definition` SET `min_gear` = 141 WHERE `id` = 'SWP';
UPDATE `raid_finder_definition` SET `min_gear` = 180 WHERE `id` = 'VOA10';
UPDATE `raid_finder_definition` SET `min_gear` = 190 WHERE `id` = 'VOA25';
UPDATE `raid_finder_definition` SET `min_gear` = 180 WHERE `id` = 'NAXX10';
UPDATE `raid_finder_definition` SET `min_gear` = 190 WHERE `id` = 'NAXX25';
UPDATE `raid_finder_definition` SET `min_gear` = 180 WHERE `id` = 'OS10';
UPDATE `raid_finder_definition` SET `min_gear` = 190 WHERE `id` = 'OS25';
UPDATE `raid_finder_definition` SET `min_gear` = 185 WHERE `id` = 'EOE10';
UPDATE `raid_finder_definition` SET `min_gear` = 195 WHERE `id` = 'EOE25';
UPDATE `raid_finder_definition` SET `min_gear` = 190 WHERE `id` = 'ONY10';
UPDATE `raid_finder_definition` SET `min_gear` = 200 WHERE `id` = 'ONY25';
UPDATE `raid_finder_definition` SET `min_gear` = 200 WHERE `id` = 'ULD10';
UPDATE `raid_finder_definition` SET `min_gear` = 213 WHERE `id` = 'ULD25';
UPDATE `raid_finder_definition` SET `min_gear` = 213 WHERE `id` = 'TOC10';
UPDATE `raid_finder_definition` SET `min_gear` = 226 WHERE `id` = 'TOC25';
UPDATE `raid_finder_definition` SET `min_gear` = 232 WHERE `id` = 'ICC10';
UPDATE `raid_finder_definition` SET `min_gear` = 245 WHERE `id` = 'ICC25';
UPDATE `raid_finder_definition` SET `min_gear` = 245 WHERE `id` = 'RS10';
UPDATE `raid_finder_definition` SET `min_gear` = 258 WHERE `id` = 'RS25';
