#include "const.h"
#include "fileFunction/filefunction.h"
#include "fileFunction/folder.h"
#include "mapExtensor/mapV2.h"
#include "minMysql/min_mysql.h"
#include "nanoSpammer/QCommandLineParserV2.h"
#include "nanoSpammer/QDebugHandler.h"
#include "nanoSpammer/config.h"
#include "table.h"
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <thread>

using namespace std;
/*
 * Si salva quando è stato fatto ultimo backup (ora di inizio) se è attivo e vi sono nuovi dati allora rifà un backup
 * Nel caso la frequenza è maggiore di 1, aspetta che almeno X tempo sia passato dall'ultimo backup ovvero
 * Ultimo backup 2020 01 01
 * Freq 2
 * Verrà fatto un backup il 2020 01 03, se ci sono nuovi dati dopo il 2020 01 01 : 00:00:00 (mezzanotte UTC)
 * 
 * Ovviamente per evitare approssimazioni od errori, viene presa in considerazione solo la DATA per ultimo backup e data attuale 
 * (altrimenti se backup è partito alle 2020 01 01 e 1 minuto e il check lo faccio poi a mezzanotte esatta fallirebbe)
 * 
 * 
 * Peccato che per InnoDB, sia un poco complesso (a dir poco)
 * https://stackoverflow.com/questions/2785429/how-can-i-determine-when-an-innodb-table-was-last-changed
 * 
 * in breve stat del file (ammesso sia attivo a livello dell'os la cosa che salva il last update)
 *  -.-
 * Perché altrimenti è error prone definire una colonna da monitorare, non tutte la tabelle lo hanno ecc ecc, non tutte le tabelle sono append only e mille edge case
 */

//TODO on the first day of the month, whatever is PRESENT, a full backup is performed ??? (if there are new data of course)
//So if last update < 1 month, do nothing and save time
DB        db;
QString   datadir;
QString   backupFolder;
QDateTime processStartTime   = QDateTime::currentDateTime();
uint      compressionThreads = thread::hardware_concurrency() / 2;

QStringList loadDB() {
	QStringList dbs;
	auto        res = db.query("SELECT * FROM dbBackupView WHERE frequency > 0");
	for (const auto& row : res) {
		dbs.append(row["SCHEMA_NAME"]);
	}
	return dbs;
}

QVector<Table> loadTables() {
	QVector<Table> tables;
	db.state.get().NULL_as_EMPTY = true;
	//Just easier to load in two stages than doig a join on the last row of backupResult
	auto res = db.query("SELECT * FROM tableBackupView WHERE frequency > 0 AND TABLE_NAME = 'general_log' ");
	for (const auto& row : res) {
		tables.push_back({row});
	}
	return tables;
}

int main(int argc, char* argv[]) {
	StackerMinLevel = "myBackupV2/";

	QCoreApplication application(argc, argv);
	QCoreApplication::setApplicationName("myBackupV2");
	QCoreApplication::setApplicationVersion("2.01");

	QCommandLineParserV2 parser;
	parser.addHelpOption();
	parser.addVersionOption();
	parser.addOption({{"d", "datadir"}, "Where the mysql datadir is, needed to know innodb last modification timestamp", "string"});
	parser.addOption({{"f", "folder"}, "Where to store the backup, please do not change folder whitout resetting the backupResult table first", "string"});
	parser.addOption({{"t", "thread"}, QSL("How many compression thread to spawn, default %1").arg(compressionThreads), "int", QString::number(compressionThreads)});
	parser.process(application);

	NanoSpammerConfig c2;
	c2.instanceName             = "s8";
	c2.BRUTAL_INHUMAN_REPORTING = false;
	c2.warningToSlack           = false;
	c2.warningToMail            = false;
	c2.warningMailRecipients    = {"admin@seisho.us"};

	commonInitialization(&c2);

	DBConf dbConf;
	dbConf.user = "roy";
	dbConf.pass = "roy";
	dbConf.setDefaultDB("backupV2");

	db.setConf(dbConf);

	//TODO verificare sia una cartella di mysql plausibile
	datadir = parser.require("datadir");

	backupFolder       = parser.require("folder");
	compressionThreads = parser.value("thread").toUInt();
	if (compressionThreads == 0) {
		throw ExceptionV2("no thread ? leave default or use 1");
	} else if (auto maxT = thread::hardware_concurrency(); compressionThreads > maxT) {
		throw ExceptionV2(
		    QSL("too many thread %1, you should use at most %2 to avoid overloading (and slowing down) the machine")
		        .arg(compressionThreads)
		        .arg(maxT));
	}

	QDir bf(backupFolder);
	if (bf.isRelative()) {
		qCritical() << "Backup folder" << backupFolder << "is using a relative path, plase use an absolute one";
		exit(1);
	}
	//Standard folder
	QDir().mkpath(backupFolder);
	QDir().mkpath(Table::completeFolder());
	QDir().mkpath(Table::currentFolder());

	for (auto& row : loadDB()) {
		Table temp(row, "");
		mkdir(temp.getDbFolder(Table::currentFolder()));
		mkdir(temp.getDbFolder(Table::completeFolder()));

		auto currentPath  = QSL("> %5/%6.events.sql").arg(Table::currentFolder(), temp.schema);
		auto completePath = QSL("> %5/%6.events.sql").arg(Table::completeFolder(), temp.schema);
		temp.dump(optionEvents, currentPath);
		temp.dump(optionEvents, completePath);
	}

	auto tables = loadTables();

	for (auto& table : tables) {
		if (table.isView) {
			//View take 0 space, so we store both in the complete folder and in the daily one
			table.dump(optionView, " > " + table.getPath(Table::currentFolder(), FType::view));
			table.dump(optionView, " > " + table.getPath(Table::completeFolder(), FType::view));
		} else {
			//schema takes 0 space so is useless to save time and space here, just make a copy on both folder
			table.dump(optionSchema, " > " + table.getPath(Table::currentFolder(), FType::schema));
			table.dump(optionSchema, " > " + table.getPath(Table::completeFolder(), FType::schema));

			if (table.hasNewData()) {
				qDebug() << table.schema << table.name << "has new data";
				//Move the old backup in a folder with the date of the old backup
				table.moveOld();
				{
					auto fileName = table.getPath(Table::completeFolder(), FType::data);
					auto command  = QSL(" %1 > %2").arg(table.compress(), fileName);
					table.dump(optionData, command, true);
					//this will create the symlink

					auto dest = table.getPath(Table::currentFolder(), FType::data);
					hardlink(fileName, dest);
				}
				table.saveResult();
			} else {
				//just create the symlink if the source exists (maybe there are not data at all ?
				auto source = table.getPath(Table::completeFolder(), FType::data);
				auto dest   = table.getPath(Table::currentFolder(), FType::data);
				hardlink(source, dest, false);
			}
		}
	}
}
