--
-- FTS3 Schema 8.0.0
-- [FTS-1744] Schema changes for the eviction feature
-- [FTS-1768] SRM TURL protocol configurable per link
-- [DMC-1302] Implement HTTP bringonline operation
--

ALTER TABLE `t_se`
    ADD COLUMN `eviction` char(1) DEFAULT NULL;

ALTER TABLE `t_link_config`
    ADD COLUMN `3rd_party_turl` varchar(150) DEFAULT NULL;

ALTER TABLE `t_file`
    ADD COLUMN `staging_metadata` text;

ALTER TABLE `t_file_backup`
    ADD COLUMN `staging_metadata` text;

INSERT INTO t_schema_vers (major, minor, patch, message)
VALUES (8, 0, 0, 'FTS-1702: Schema changes for the eviction feature');
