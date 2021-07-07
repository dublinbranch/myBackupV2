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
 * Peccato che per InnoDB, sia un poco inaffibabile
 * https://stackoverflow.com/questions/2785429/how-can-i-determine-when-an-innodb-table-was-last-changed
 * Per adesso lavoriamo secondo la logica, se è NULL allora non ci son state modifiche (che è vero), molto più facile, il codice per gestire eventualmente da disco lo stat del file ci sta, ma non è usato
 */

//TODO on the first day of the month, whatever is PRESENT, a full backup is performed ??? (if there are new data of course)
//So if last update < 1 month, do nothing and save time
DB        db;
QString   backupFolder;
QDateTime processStartTime   = QDateTime::currentDateTime();
uint      compressionThreads = thread::hardware_concurrency() / 2;

QStringList loadDB(QString& schema) {
	QStringList dbs;
	auto        sql = QSL("SELECT * FROM dbBackupView WHERE frequency > 0 %1 ORDER BY SCHEMA_NAME ASC");

	QString where;
	if (schema.size()) {
		where = QSL(" AND SCHEMA_NAME = %1 ").arg(base64this(schema));
	}
	sql      = sql.arg(where);
	auto res = db.query(sql);
	if (res.isEmpty()) {
		qWarning() << QSL("no result for %1, are you sure is enabled something ?").arg(sql);
	}
	for (const auto& row : res) {
		dbs.append(row["SCHEMA_NAME"]);
	}
	return dbs;
}

QVector<Table> loadTables(QString& schema, QString& table) {
	QVector<Table> tables;
	db.state.get().NULL_as_EMPTY = true;
	//Just easier to load in two stages than doing a join on the last row of backupResult
	auto    sql = QSL("SELECT * FROM tableBackupView WHERE frequency > 0 %1 ORDER BY TABLE_SCHEMA ASC, TABLE_NAME ASC");
	QString where;
	if (schema.size() && table.size()) {
		where = QSL(" AND TABLE_SCHEMA = %1 AND TABLE_NAME = %2").arg(base64this(schema), base64this(table));
	}
	sql      = sql.arg(where);
	auto res = db.query(sql);
	if (res.isEmpty()) {
		qWarning() << QSL("no result for %1, are you sure is enabled something ?").arg(sql);
	}
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

	parser.addOption({{"f", "folder"}, "Where to store the backup, please do not change folder whitout resetting the backupResult table first", "string"});
	parser.addOption({{"t", "thread"}, QSL("How many compression thread to spawn, default %1").arg(compressionThreads), "int", QString::number(compressionThreads)});
	parser.addOption({{"u", "user"}, QSL("Mysql user"), "string"});
	parser.addOption({{"p", "password"}, QSL("Mysql password"), "string"});
	parser.addOption({{"P", "port"}, QSL("Mysql port"), "int", "3306"});
	parser.addOption({{"H", "host"}, QSL("Mysql host"), "string", "127.0.0.1"});
	parser.addOption({"schema", QSL("Which schema to backup"), "string"});
	parser.addOption({"table", QSL("which table to backup"), "string"});
	parser.addOption({"ssl", QSL("if we need to use ssl to connect to remote db"), "bool"});

	parser.process(application);

	auto schema = parser.value("schema");
	auto table  = parser.value("table");
	if ((bool)schema.size() ^ (bool)table.size()) {
		qCritical() << "You must set both schema and table!";
		exit(1);
	}

	NanoSpammerConfig c2;
	c2.instanceName             = "s8";
	c2.BRUTAL_INHUMAN_REPORTING = false;
	c2.warningToSlack           = false;
	c2.warningToMail            = false;
	c2.warningMailRecipients    = {"admin@seisho.us"};

	commonInitialization(&c2);

	DBConf dbConf;
	dbConf.user = parser.require("user").toUtf8();
	dbConf.pass = parser.require("password").toUtf8();
	dbConf.port = parser.require("port").toUInt();
	dbConf.host = parser.require("host").toUtf8();
	if (parser.value("ssl").size()) {
		dbConf.ssl = true;
	}
	dbConf.writeBinlog = false;

	dbConf.setDefaultDB("backupV2");

	db.setConf(dbConf);

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

	for (auto& row : loadDB(schema)) {
		Table temp(row, "");
		mkdir(temp.getDbFolder(Table::currentFolder()));
		mkdir(temp.getDbFolder(Table::completeFolder()));

		auto currentPath  = QSL("> %5/%6.events.sql").arg(temp.getDbFolder(Table::currentFolder()), temp.schema);
		auto completePath = QSL("> %5/%6.events.sql").arg(temp.getDbFolder(Table::completeFolder()), temp.schema);
		if (!SkipDumpDb) {
			temp.dump(optionEvents, currentPath);
			temp.dump(optionEvents, completePath);
		}
	}

	auto tables = loadTables(schema, table);

	for (auto& table : tables) {
		if (table.isView) {
			//View take 0 space, so we store both in the complete folder and in the daily one
			table.dump(optionView, " > " + table.getPath(Table::currentFolder(), FType::view));
			table.dump(optionView, " > " + table.getPath(Table::completeFolder(), FType::view));
		} else {
			//schema takes 0 space so is useless to save time and space here, just make a copy on both folder
			if (!SkipDumpTableSChema) {
				table.dump(optionSchema, " > " + table.getPath(Table::currentFolder(), FType::schema));
				table.dump(optionSchema, " > " + table.getPath(Table::completeFolder(), FType::schema));
			}

			if (table.hasNewData()) {
				qDebug() << table.schema << table.name << "has new data";
				{
					table.dumpData();

					auto source = table.getPath(Table::completeFolder(), FType::data);
					auto dest   = table.getPath(Table::currentFolder(), FType::data);
					if (table.hasMultipleFile()) {
						//you can not hardlink folder, so create a folder and iterate
						hardLinkFolder(source + "*", dest);
					} else {
						hardlink(source, dest);
					}
				}
				table.saveResult();
			} else {
				//just create the symlink if the source exists (maybe there are not data at all ?
				auto source = table.getPath(Table::completeFolder(), FType::data);
				auto dest   = table.getPath(Table::currentFolder(), FType::data);
				hardlink(source, dest);
			}
		}
	}
}
