--
-- FTS3 Schema 8.1.0
-- Add "t_projects" table for project management
-- Add "t_routing" table for network resource management
-- Add "t_bound" table for resource control specification
--

DROP TABLE IF EXISTS `t_tcn_projects`;

CREATE TABLE `t_tcn_projects` (
    `source_se` varchar(150) NOT NULL,
    `dest_se`   varchar(150) NOT NULL,
    `vo_name`   varchar(50) NOT NULL,
    `proj_id`   varchar(50) NOT NULL,
    PRIMARY KEY (`source_se`, `dest_se`, `vo_name`)
);

DROP TABLE IF EXISTS `t_tcn_resource_use`;

CREATE TABLE `t_tcn_resource_use` (
    `source_se` varchar(50) NOT NULL,
    `dest_se`  varchar(150) NOT NULL,
    `resc_id`  varchar(64) NOT NULL,
    PRIMARY KEY (`source_se`, `dest_se`, `resc_id`)
);

DROP TABLE IF EXISTS `t_tcn_resource_ctrlspec`;

CREATE TABLE `t_tcn_resource_ctrlspec` (
    `proj_id`   varchar(50) NOT NULL,
    `resc_id`  varchar(64) NOT NULL,
    `max_usage` double DEFAULT NULL,
    `max_share` double DEFAULT NULL,
    PRIMARY KEY (`proj_id`, `resc_id`)
);

INSERT INTO t_schema_vers (major, minor, patch, message)
VALUES (8, 1, 0, 'Add new tables for TCN resource control');
