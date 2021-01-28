INSERT IGNORE, Download CSV INTO dbBackup
SELECT 
NULL,
SCHEMA_NAME,
1,
1,
NOW(),
NULL
FROM `information_schema`.`SCHEMATA` 
