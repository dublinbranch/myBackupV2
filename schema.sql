SET NAMES utf8;
SET time_zone = '+00:00';
SET foreign_key_checks = 0;

SET NAMES utf8mb4;

CREATE DATABASE `backupV2` /*!40100 DEFAULT CHARACTER SET utf8mb4 */;
USE `backupV2`;

CREATE TABLE `backupResult` (
  `id` int(10) unsigned NOT NULL AUTO_INCREMENT,
  `TABLE_SCHEMA` varchar(64) NOT NULL,
  `TABLE_NAME` varchar(64) NOT NULL,
  `started` datetime NOT NULL,
  `ended` datetime NOT NULL DEFAULT '0000-00-00 00:00:00',
  `folder` varchar(2048) CHARACTER SET ascii COLLATE ascii_bin NOT NULL COMMENT 'absolute path of where the file got stored in, used when we have to move around symlink and backup',
  `error` text CHARACTER SET utf8mb4 DEFAULT NULL,
  `lastId` bigint(20) unsigned DEFAULT NULL COMMENT 'used for incremental table to know where we arrived',
  PRIMARY KEY (`id`),
  KEY `TABLE_SCHEMA_TABLE_NAME` (`TABLE_SCHEMA`,`TABLE_NAME`),
  KEY `lastBackup` (`started`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;


CREATE TABLE `dbBackupOverride` (
  `id` int(11) NOT NULL AUTO_INCREMENT,
  `SCHEMA_NAME` varchar(64) NOT NULL,
  `frequency` tinyint(4) unsigned NOT NULL DEFAULT 1 COMMENT '1 = each day, 0 = disabled',
  `schemaOnly` tinyint(4) unsigned NOT NULL DEFAULT 0 COMMENT 'for temp db, where data is irrelevant',
  `addedOn` timestamp NOT NULL DEFAULT current_timestamp(),
  `note` text DEFAULT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `SCHEMA_NAME` (`SCHEMA_NAME`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;


CREATE TABLE `dbBackupView` (`SCHEMA_NAME` varchar(64), `frequency` decimal(4,0), `note` text);


CREATE TABLE `tableBackupOverride` (
  `id` int(11) NOT NULL AUTO_INCREMENT,
  `TABLE_SCHEMA` varchar(64) NOT NULL,
  `TABLE_NAME` varchar(64) NOT NULL,
  `note` text NOT NULL,
  `frequency` tinyint(4) unsigned DEFAULT NULL COMMENT 'this will pre-empt the db setting, 1 = each day',
  `where` text DEFAULT NULL COMMENT 'used to filter, like last day for local backup',
  `incremental` varchar(255) CHARACTER SET ascii COLLATE ascii_bin DEFAULT NULL COMMENT 'if set is the colum to use for ranged backup',
  `incrementalOn` tinyint(3) unsigned NOT NULL DEFAULT 0,
  `compressionProg` varchar(255) CHARACTER SET ascii COLLATE ascii_bin DEFAULT NULL COMMENT 'if you want to use something else than pigz',
  `compressionOpt` varchar(255) CHARACTER SET ascii COLLATE ascii_bin DEFAULT NULL,
  `compressionSuffix` varchar(255) CHARACTER SET ascii COLLATE ascii_bin DEFAULT NULL,
  `path` varchar(255) CHARACTER SET ascii COLLATE ascii_bin DEFAULT NULL COMMENT 'innodb use an unknown encoding scheme for non ascii char',
  `fractionSize` int(10) unsigned NOT NULL DEFAULT 0 COMMENT 'how big the chunk',
  PRIMARY KEY (`id`),
  UNIQUE KEY `TABLE_SCHEMA_TABLE_NAME` (`TABLE_SCHEMA`,`TABLE_NAME`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;


CREATE TABLE `tableBackupView` (`TABLE_SCHEMA` varchar(64), `TABLE_NAME` varchar(64), `TABLE_TYPE` varchar(64), `ENGINE` varchar(64), `TABLE_COMMENT` varchar(2048), `frequency` decimal(4,0), `lastUpdateTime` datetime, `schemaOnly` decimal(4,0), `incremental` varchar(255), `incrementalOn` decimal(3,0), `compressionProg` varchar(255), `compressionOpt` varchar(255), `compressionSuffix` varchar(255), `path` varchar(255), `fractionSize` decimal(10,0), `note` text);


DROP TABLE IF EXISTS `dbBackupView`;
CREATE ALGORITHM=MERGE DEFINER=`root`@`localhost` SQL SECURITY DEFINER VIEW `dbBackupView` AS select `s`.`SCHEMA_NAME` AS `SCHEMA_NAME`,coalesce(`d`.`frequency`,1) AS `frequency`,`d`.`note` AS `note` from (`information_schema`.`SCHEMATA` `s` left join `backupV2`.`dbBackupOverride` `d` on(`d`.`SCHEMA_NAME` = `s`.`SCHEMA_NAME`));

DROP TABLE IF EXISTS `tableBackupView`;
CREATE ALGORITHM=MERGE DEFINER=`root`@`localhost` SQL SECURITY DEFINER VIEW `tableBackupView` AS select `t`.`TABLE_SCHEMA` AS `TABLE_SCHEMA`,`t`.`TABLE_NAME` AS `TABLE_NAME`,`t`.`TABLE_TYPE` AS `TABLE_TYPE`,`t`.`ENGINE` AS `ENGINE`,`t`.`TABLE_COMMENT` AS `TABLE_COMMENT`,coalesce(`b`.`frequency`,`d`.`frequency`,1) AS `frequency`,`t`.`UPDATE_TIME` AS `lastUpdateTime`,coalesce(`d`.`schemaOnly`,0) AS `schemaOnly`,`b`.`incremental` AS `incremental`,coalesce(`b`.`incrementalOn`,0) AS `incrementalOn`,`b`.`compressionProg` AS `compressionProg`,`b`.`compressionOpt` AS `compressionOpt`,`b`.`compressionSuffix` AS `compressionSuffix`,`b`.`path` AS `path`,coalesce(`b`.`fractionSize`,0) AS `fractionSize`,`b`.`note` AS `note` from ((`information_schema`.`TABLES` `t` left join `backupV2`.`tableBackupOverride` `b` on(`t`.`TABLE_NAME` = `b`.`TABLE_NAME` and `t`.`TABLE_SCHEMA` = `b`.`TABLE_SCHEMA`)) left join `backupV2`.`dbBackupOverride` `d` on(`d`.`SCHEMA_NAME` = `t`.`TABLE_SCHEMA`)) where `t`.`TEMPORARY` = 'N' or `t`.`TEMPORARY` is null;

-- 2021-07-07 19:29:44

INSERT INTO `dbBackupOverride` (`id`, `SCHEMA_NAME`, `frequency`, `addedOn`, `note`) VALUES
(1,    'performance_schema',   0,      '2021-03-25 13:03:27',  'internal mysql stuff'),
(2,    'information_schema',   0,      '2021-03-25 13:03:27',  'internal mysql stuff');


INSERT INTO `tableBackupOverride` (`id`, `TABLE_SCHEMA`, `TABLE_NAME`, `note`, `frequency`, `where`, `incremental`, `incrementalOn`, `compressionProg`, `compressionOpt`, `compressionSuffix`, `path`, `fractionSize`) VALUES
(10,	'mysql',	'global_priv',	'',	1,	NULL,	NULL,	0,	NULL,	NULL,	NULL,	NULL,	0),
(11,	'mysql',	'tables_priv',	'',	1,	NULL,	NULL,	0,	NULL,	NULL,	NULL,	NULL,	0),
(12,	'mysql',	'servers',	'',	1,	NULL,	NULL,	0,	NULL,	NULL,	NULL,	NULL,	0),
(13,	'mysql',	'proc',	'',	1,	NULL,	NULL,	0,	NULL,	NULL,	NULL,	NULL,	0),
(14,	'mysql',	'event',	'',	1,	NULL,	NULL,	0,	NULL,	NULL,	NULL,	NULL,	0),
(15,	'mysql',	'db',	'',	1,	NULL,	NULL,	0,	NULL,	NULL,	NULL,	NULL,	0);

