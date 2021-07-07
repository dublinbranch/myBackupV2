#ifndef CONST_H
#define CONST_H

#include <QString>

//During development use yyyy-MM-dd_HH-mm so you can have a table for each minute and easily check the operation
//Under normal condition this program is supposed to run daily so will use yyyy-MM-dd
const QString folderTimeFormat = "yyyy-MM-dd_HH-mm";
/********/
const QString optionData   = " --single-transaction --no-create-info --extended-insert --quick --set-charset --skip-add-drop-table ";
const QString optionSchema = " --single-transaction --no-data --opt --skip-add-drop-table ";
const QString optionView   = " --single-transaction  --opt --skip-add-drop-table ";
const QString optionEvents = " --no-create-db --no-create-info --no-data --events --routines --triggers --skip-opt ";

const bool SkipDumpDb          = true;
const bool SkipDumpTableSChema = true;
#endif // CONST_H
