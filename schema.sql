CREATE DATABASE `backupV2` /*!40100 DEFAULT CHARACTER SET utf8 */;
USE `backupV2`;

CREATE TABLE `backupResult` (
  `id` int(10) unsigned NOT NULL AUTO_INCREMENT,
  `TABLE_SCHEMA` varchar(64) NOT NULL,
  `TABLE_NAME` varchar(64) NOT NULL,
  `lastBackup` timestamp NOT NULL DEFAULT current_timestamp() ON UPDATE current_timestamp(),
  `ok` tinyint(3) unsigned NOT NULL,
  PRIMARY KEY (`id`),
  KEY `TABLE_SCHEMA_TABLE_NAME` (`TABLE_SCHEMA`,`TABLE_NAME`),
  KEY `lastBackup` (`lastBackup`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;


CREATE TABLE `dbBackup` (
  `id` int(11) NOT NULL AUTO_INCREMENT,
  `SCHEMA_NAME` varchar(64) NOT NULL,
  `enabled` tinyint(4) unsigned NOT NULL DEFAULT 1,
  `frequency` tinyint(4) unsigned NOT NULL DEFAULT 1 COMMENT '1 = each day',
  `note` text NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `SCHEMA_NAME` (`SCHEMA_NAME`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;


CREATE TABLE `dbBackupView` (`s` varchar(64), `id` int(11), `enabled` tinyint(4) unsigned, `note` text);


CREATE TABLE `tableBackup` (
  `id` int(11) NOT NULL AUTO_INCREMENT,
  `TABLE_SCHEMA` varchar(64) NOT NULL,
  `TABLE_NAME` varchar(64) NOT NULL,
  `skip` tinyint(4) unsigned NOT NULL DEFAULT 0,
  `note` text NOT NULL,
  `frequency` tinyint(4) unsigned DEFAULT NULL COMMENT 'this will pre-empt the db setting, 1 = each day',
  PRIMARY KEY (`id`),
  UNIQUE KEY `TABLE_SCHEMA_TABLE_NAME` (`TABLE_SCHEMA`,`TABLE_NAME`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;


CREATE TABLE `tableBackupView` (`TABLE_SCHEMA` varchar(64), `TABLE_NAME` varchar(64), `TABLE_TYPE` varchar(64), `TABLE_COMMENT` varchar(2048), `enabled` tinyint(4) unsigned, `frequency` tinyint(4) unsigned, `skip` tinyint(4) unsigned, `note` text);


DROP TABLE IF EXISTS `dbBackupView`;
CREATE ALGORITHM=MERGE SQL SECURITY DEFINER VIEW `dbBackupView` AS select `s`.`SCHEMA_NAME` AS `s`,`d`.`id` AS `id`,`d`.`enabled` AS `enabled`,`d`.`note` AS `note` from (`information_schema`.`SCHEMATA` `s` left join `backupV2`.`dbBackup` `d` on(`d`.`SCHEMA_NAME` = `s`.`SCHEMA_NAME`));

DROP TABLE IF EXISTS `tableBackupView`;
CREATE ALGORITHM=UNDEFINED SQL SECURITY DEFINER VIEW `tableBackupView` AS select `t`.`TABLE_SCHEMA` AS `TABLE_SCHEMA`,`t`.`TABLE_NAME` AS `TABLE_NAME`,`t`.`TABLE_TYPE` AS `TABLE_TYPE`,`t`.`TABLE_COMMENT` AS `TABLE_COMMENT`,`d`.`enabled` AS `enabled`,coalesce(`b`.`frequency`,`d`.`frequency`) AS `frequency`,`b`.`skip` AS `skip`,`b`.`note` AS `note` from ((`information_schema`.`TABLES` `t` left join `backupV2`.`tableBackup` `b` on(`t`.`TABLE_NAME` = `b`.`TABLE_NAME` and `t`.`TABLE_SCHEMA` = `b`.`TABLE_SCHEMA`)) join `backupV2`.`dbBackup` `d` on(`d`.`SCHEMA_NAME` = `t`.`TABLE_SCHEMA`)) where `t`.`TEMPORARY` = 'N';
