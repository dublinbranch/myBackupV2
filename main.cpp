#include "minMysql/min_mysql.h"
#include "nanoSpammer/QDebugHandler.h"
#include "nanoSpammer/config.h"
#include <QCommandLineParser>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QProcess>

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

//TODO on the first day of the month, whatever is PRESENT, a full backup is performed ???
DB        db;
QString   path;
QDateTime processStartTime = QDateTime::currentDateTime();

static const QString loginBlock   = " -u roy -proy ";
static const QString optionData   = " --single-transaction --no-create-info --extended-insert --quick --set-charset --skip-add-drop-table ";
static const QString optionSchema = " --single-transaction --no-data --opt --skip-add-drop-table ";
static const QString optionView   = " --single-transaction  --opt --skip-add-drop-table ";
static const QString optionEvents = " --no-create-db --no-create-info --no-data --events --routines --triggers --skip-opt ";

class Table {
      public:
	inline static QString basePath = ".";

	QString schema;
	QString name;
	QString compressionOpt;
	QString incremental;
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

	//is just easier, as not all table have id -.- as the primary column...
	bool hasNewData() const {
		return lastUpdateTime > lastBackup;
	}

	void annoyingJoin();
	void innoDbLastUpdate();
	void saveResult();
	Table(const sqlRow& row);
	Table(QString _schema, QString _name);

	QString getFinalName() const {
		QString finalName;
		//2020-12-28_22-01/externalAgencies.rtyAssignmentStatusHistory.data.sql.gz
		return QString("%1/%2.%3").arg(getFolder(), schema, name);
	}
	static QString getFolder() {
		return QString("%1/%2/").arg(basePath, processStartTime.toString("yyyy-MM-dd_HH-mm"));
	}

	uint64_t getCurrentId() const {
		if (!incremental.isEmpty()) {
			auto sql = QSL("SELECT COALESCE(MAX(%1),0) as maxId FROM `%2`.`%3`").arg(incremental, schema, name);
			return db.queryLine(sql).get2<uint64_t>("maxId");
		}
		return 0;
	}

	void dump(QString option, QString saveBlock) {
		saveBlock = saveBlock.arg(getFinalName());
		QProcess sh;
		QString  where;
		if (auto currentId = getCurrentId(); currentId) {
			where = QSL(R"(--where="%1 > %2")").arg(incremental).arg(lastId);
		}
		auto param = QString("mysqldump %1 %2 %3 %4 %5 %6").arg(loginBlock, option, schema, name, where, saveBlock);
		sh.start("sh", QStringList() << "-c" << param);
		started = QDateTime::currentDateTimeUtc();
		sh.waitForFinished(1E9);

		error.append(sh.readAll());
		if (!error.isEmpty()) {
			qCritical().noquote() << error;
		}
		sh.close();
	}
};

QStringList loadDB() {
	QStringList dbs;
	auto        res = db.query("SELECT * FROM dbBackupView WHERE frequency > 0");
	for (const auto& row : res) {
		dbs.append(row["SCHEMA_NAME"]);
	}
	return dbs;
}

//Is nonsensical complex and many times slow to join at take just one row
void Table::annoyingJoin() {
	QString sql = R"(SELECT started,lastId FROM backupResult WHERE TABLE_SCHEMA = %1 AND TABLE_NAME = %2 ORDER BY started DESC LIMIT 1)";
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
		QString   filePath = QSL("%1/%2/%3.ibd").arg(path, schema, name);
		QFileInfo info(filePath);
		lastUpdateTime = info.lastModified();
	}
}

Table::Table(const sqlRow& row) {
	row.rq("TABLE_SCHEMA", schema);
	row.rq("TABLE_NAME", name);
	row.rq("frequency", frequency);
	row.rq("lastUpdateTime", lastUpdateTime);
	row.rq("compressionOpt", compressionOpt);
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

QVector<Table> loadTables() {
	QVector<Table> tables;
	auto           res = db.query("SELECT * FROM tableBackupView WHERE frequency > 0");
	for (const auto& row : res) {
		tables.push_back({row});
	}
	return tables;
}

int main(int argc, char* argv[]) {
	QCoreApplication application(argc, argv);
	QCoreApplication::setApplicationName("myBackupV2");
	QCoreApplication::setApplicationVersion("2.01");

	QCommandLineParser parser;
	parser.addHelpOption();
	parser.addVersionOption();
	parser.addOption({{"p", "path"}, "Where the mysql datadir is, needed to know innodb last modification timestamp", "string"});
	parser.addOption({{"t", "thread"}, "How many compression thread to spawn", "int", "4"});
	parser.process(application);

	if (!parser.isSet("path")) {
		qWarning() << "Where is the path ?";
		return 1;
	} else {
		path = parser.value("path");
	}

	NanoSpammerConfig c2;
	c2.instanceName             = "s8";
	c2.BRUTAL_INHUMAN_REPORTING = false;
	c2.warningToSlack           = true;
	c2.warningToMail            = true;
	c2.warningMailRecipients    = {"admin@seisho.us"};

	commonInitialization(&c2);

	DBConf dbConf;
	dbConf.user = "roy";
	dbConf.pass = "roy";
	dbConf.setDefaultDB("backupV2");

	db.setConf(dbConf);

	QDir().mkpath(Table::getFolder());
	for (auto& row : loadDB()) {
		Table temp(row, "");
		temp.dump(optionEvents, "> %5events.sql");
	}

	auto tables = loadTables();

	for (auto& table : tables) {
		if (table.isView) {
			table.dump(optionView, "> %5.view.sql");
		} else {
			if (table.hasNewData()) {
				auto compress = QSL("| pigz -p %1").arg(parser.value("thread").toUInt());
				table.dump(optionData, QSL("%1 > %5.data.sql.gz").arg(compress));
				table.dump(optionSchema, "> %5.schema.sql");
				table.saveResult();
			}
		}
	}
}

void Table::saveResult() {
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

	sql = sql.arg(base64this(schema), base64this(name), started.toString(mysqlDateTimeFormat), mayBeBase64(error))
	          .arg(getCurrentId());
	db.query(sql);
}
