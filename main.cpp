#include "fileFunction/folder.h"
#include "mapExtensor/mapV2.h"
#include "minMysql/min_mysql.h"
#include "nanoSpammer/QCommandLineParserV2.h"
#include "nanoSpammer/QDebugHandler.h"
#include "nanoSpammer/config.h"
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

const QString        folderTimeFormat = "yyyy-MM-dd_HH-mm";
static const QString loginBlock       = " -u roy -proy ";
static const QString optionData       = " --single-transaction --no-create-info --extended-insert --quick --set-charset --skip-add-drop-table ";
static const QString optionSchema     = " --single-transaction --no-data --opt --skip-add-drop-table ";
static const QString optionView       = " --single-transaction  --opt --skip-add-drop-table ";
static const QString optionEvents     = " --no-create-db --no-create-info --no-data --events --routines --triggers --skip-opt ";

class Table {
      public:
	QString schema;
	QString name;
	QString compressionProg;
	QString compressionOpt;
	QString compressionSuffix;
	QString incremental;
	//InnoDB uses an unknown encoding scheme for non ASCII table name, so we force the path
	QString path;
	//In case there is an error we write in the db too in the result
	QString   error;
	QDateTime lastUpdateTime;
	QDateTime lastBackup;
	QDateTime started;
	QDateTime completed;
	uint      frequency = 0;
	uint      lastId    = 0;

	bool isView   = false;
	bool isInnoDb = false;

	//This is the folder where we ALWAYS store the current up to date data
	static QString completeFolder();

	bool isIncremental() const;

	//A folder used to store what we are doing today, and simlink stuff to current when needed
	static QString dailyBackupFolder();

	//If the table is NOT PROGRESSIVE, we archive the old data
	void archiveOldData();
	//For this table, when the last backup was performed, used to move the old data from completedFolder before updating it
	QString lastBackupFolder() const;

	//is just easier, as not all table have id -.- as the primary column...
	bool hasNewData() const;

	/**
	 * @brief moveOld will move an old backup in a folder named according with the backup date, and remove the symlink
	 * @return 
	 */
	bool moveOld();
	void annoyingJoin();
	void innoDbLastUpdate();
	void saveResult() const;
	Table(const sqlRow& row);
	Table(QString _schema, QString _name);

	QString getDbFolder() const;

	QString getFinalName() const;
	//TODO per dove mettere i file in caso vi siano nuovi dati, il vecchio lo mettiamo nella cartella di quando venne fatto, il nuovo nella complete

	static QString getFolder();

	uint64_t getCurrentId() const;

	void dump(QString option, QString saveBlock);
};

QStringList loadDB() {
	QStringList dbs;
	auto        res = db.query("SELECT * FROM dbBackupView WHERE frequency > 0");
	for (const auto& row : res) {
		dbs.append(row["SCHEMA_NAME"]);
	}
	return dbs;
}

QString Table::completeFolder() {
	return backupFolder + "/complete/";
}

bool Table::isIncremental() const {
	return !incremental.isEmpty();
}

QString Table::dailyBackupFolder() {
	return backupFolder + "/complete/";
}

void Table::archiveOldData() {
	if (!isIncremental()) {
		return;
	}
}

QString Table::lastBackupFolder() const {
	return backupFolder + "/" + lastBackup.toString(folderTimeFormat);
}

bool Table::hasNewData() const {
	return lastUpdateTime > lastBackup;
}

bool Table::moveOld() {
}

void Table::annoyingJoin() {
	QString sql = R"(SELECT started,lastId FROM backupResult WHERE TABLE_SCHEMA = %1 AND TABLE_NAME = %2 AND error IS NULL ORDER BY started DESC LIMIT 1)";
	sql         = sql.arg(base64this(schema), base64this(name));
	auto line   = db.queryLine(sql);
	if (line.empty()) {
		return;
	}
	line.rq("started", lastBackup);
	line.rq("lastId", lastId);
}

void Table::innoDbLastUpdate() {
	if (isInnoDb) {
		QString ibdPath;
		if (path.isEmpty()) {
			ibdPath = QSL("%1/%2/%3.ibd").arg(datadir, schema, name);
		} else {
			ibdPath = QSL("%1/%2/%3.ibd").arg(datadir, schema, path);
		}

		QFileInfo info(ibdPath);
		if (!info.exists()) {
			//TODO explain that this program has to run as a user in the mysql group
			throw ExceptionV2(QSL("Missing file (Or we do not have privileges to read) %1, for table `%2`.`%3`, are you sure you are pointing to the right mysql datadir folder ?").arg(ibdPath, schema, name));
		}
		lastUpdateTime = info.lastModified();
	}
}

Table::Table(const sqlRow& row) {
	row.rq("TABLE_SCHEMA", schema);
	row.rq("TABLE_NAME", name);
	row.rq("frequency", frequency);
	row.rq("lastUpdateTime", lastUpdateTime);
	row.rq("compressionProg", compressionProg);
	row.rq("compressionOpt", compressionOpt);
	row.rq("compressionSuffix", compressionSuffix);
	row.rq("incremental", incremental);
	if (row["TABLE_TYPE"] == "view") {
		isView = true;
	}
	if (row["ENGINE"] == "InnoDB") {
		isInnoDb = true;
	}
	annoyingJoin();
	innoDbLastUpdate();
}

Table::Table(QString _schema, QString _name) {
	schema = _schema;
	name   = _name;
}

QString Table::getDbFolder() const {
	return completeFolder() + "/" + schema;
}

QString Table::getFinalName() const {
	QString finalName;
	//2020-12-28_22-01/externalAgencies.rtyAssignmentStatusHistory.data.sql.gz
	return QString("%1/%2.%3").arg(getDbFolder(), schema, name);
}

QString Table::getFolder() {
	return QString("%1/%2/").arg(backupFolder, processStartTime.toString("yyyy-MM-dd_HH-mm"));
}

uint64_t Table::getCurrentId() const {
	if (!incremental.isEmpty()) {
		auto sql = QSL("SELECT COALESCE(MAX(%1),0) as maxId FROM `%2`.`%3`").arg(incremental, schema, name);
		return db.queryLine(sql).get2<uint64_t>("maxId");
	}
	return 0;
}

void Table::dump(QString option, QString saveBlock) {
	QProcess sh;
	QString  where;
	if (auto currentId = getCurrentId(); currentId) {
		where = QSL(R"(--where="%1 > %2")").arg(incremental).arg(lastId);
	}
	auto param = QString("mysqldump %1 %2 %3 %4 %5 %6").arg(loginBlock, option, schema, name, where, saveBlock);
	sh.start("sh", QStringList() << "-c" << param);
	started = QDateTime::currentDateTimeUtc();
	sh.waitForFinished(1E9);

	error.append(sh.readAllStandardError());
	error.append(sh.readAllStandardOutput());
	if (!error.isEmpty()) {
		qCritical().noquote() << error;
	}
	sh.close();
}

QString validSuffix(QString compress, QString suffix) {
	static const mapV2<QString, QString> mapping{{
	    {"xz", "xz"},
	    {"gzip", "gz"},
	    {"pigz", "gz"},
	}};

	if (auto v = mapping.get(compress); v) {
		if (suffix.isEmpty()) {
			return *v.val;
		} else if (*v.val != suffix) {
			throw ExceptionV2(QSL("Mismatched compression program and suffix, %1 vs %2").arg(compress, suffix));
		} else {
			return suffix;
		}
	} else {
		if (suffix.isEmpty()) {
			throw ExceptionV2(QSL("Unknown compression program, which suffix shall I use ?").arg(compress));
		} else {
			return suffix;
		}
	}
}

QVector<Table> loadTables() {
	QVector<Table> tables;
	db.state.get().NULL_as_EMPTY = true;
	//Just easier to load in two stages than doig a join on the last row of backupResult
	auto res = db.query("SELECT * FROM tableBackupView WHERE frequency > 0");
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
	QDir().mkpath(Table::dailyBackupFolder());

	for (auto& row : loadDB()) {
		Table temp(row, "");
		QDir().mkpath(temp.getDbFolder());
		temp.dump(optionEvents, QSL("> %5/%6.events.sql").arg(temp.getDbFolder(), temp.schema));
	}

	auto tables = loadTables();

	for (auto& table : tables) {
		if (table.isView) {
			//View take 0 space, so we store both in the complete folder and in the daily one
			table.dump(optionView, "> %5.view.sql");
		} else {
			if (table.hasNewData()) {
				QString compress;
				QString suffix;
				if (table.compressionProg.isEmpty()) {
					compress = QSL("| pigz -p %1").arg(parser.value("thread").toUInt());
					suffix   = "gz";
				} else {
					compress = QSL("| %1 %2").arg(table.compressionProg, table.compressionOpt);
					suffix   = validSuffix(table.compressionProg, table.compressionSuffix);
				}

				auto folder = table.getFinalName();
				//Move the old backup in a folder with the date of the old backup
				table.moveOld();
				table.dump(optionData, QSL("%1 > %2.data.sql.%3").arg(compress, folder, suffix));
				table.dump(optionSchema, QSL("> %5.schema.sql").arg(folder));
				table.saveResult();
			}
		}
	}
}

void Table::saveResult() const {
	QString sql = R"(
INSERT INTO backupResult
SET
	TABLE_SCHEMA = %1,
	TABLE_NAME = %2,
	started = '%3',
	ended = NOW(),
	error = %4,
	lastId = %5
)";

	sql = sql.arg(base64this(schema), base64this(name), started.toString(mysqlDateTimeFormat), mayBeBase64(error, true))
	          .arg(getCurrentId());
	db.query(sql);
}
