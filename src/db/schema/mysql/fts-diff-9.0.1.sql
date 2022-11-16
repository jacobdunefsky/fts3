--
-- FTS3 Schema 9.0.1
-- Per physical link control variables
--

DROP TABLE IF EXISTS `t_plinks`; 
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `t_plinks` ( 
  `plink_id` varchar(36) NOT NULL,
  `ingress` varchar(150) NOT NULL,
  `egress`   varchar(150) NOT NULL,
  `cap`  int(11) DEFAULT '10240', 
  `ldelay` int(11) DEFAULT NULL,
  PRIMARY KEY (`plink_id`)
) ENGINE=InnoDB DEFAULT CHARSET=latin1;
`/*!40101 SET character_set_client = @saved_cs_client */;`
INSERT INTO t_link_config (plink_id, ingress, egress, cap, ldelay)
VALUES (UUID(), '*', '*', '10', '100');

DROP TABLE IF EXISTS `t_routing`; 
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `t_routing` (
  `source_se` varchar(150) NOT NULL, 
  `dest_se `  varchar(150) NOT NULL,
  `plink_id`  varchar(36) DEFAULT NULL, 
  PRIMARY KEY (`source_se`, `dest_se`, `plink_id`)
) ENGINE=InnoDB DEFAULT CHARSET=latin1;
`/*!40101 SET character_set_client = @saved_cs_client */;`
INSERT INTO t_link_config (source_se, dest_se, plink_id)
SELECT '*', '*', plink_id FROM `t_plinks`
LIMIT 1;
/*VALUES ('*', '*', '*', '*', NULL);*/


DROP TABLE IF EXISTS `t_bounds`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `t_bounds` (
  `vo_name`   varchar(150) NOT NULL, 
  `source_se` varchar(150) NOT NULL,
  `dest_se`   varchar(150) NOT NULL,
  `plink_id`  varchar(36) DEFAULT NULL, 
  `max_throughput` int(11) DEFAULT NULL, 
  `relative_share` int(11) DEFAULT '10',
  PRIMARY KEY ('vo_name', 'source_se', 'dest_se', `plink_id`)
) ENGINE=InnoDB DEFAULT CHARSET=latin1;
`/*!40101 SET character_set_client = @saved_cs_client */;`
INSERT INTO t_link_config (vo_name, source_se, dest_se, plink_id, max_throughput, relative_share)
VALUES ('*', '*', '*', '*', '5', '0');
