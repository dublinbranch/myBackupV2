#include "minMysql/min_mysql.h"
#include "nanoSpammer/QDebugHandler.h"
#include "nanoSpammer/config.h"
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QProcess>


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
 */ 
DB db;

class Writer1 {
      public:
	QString               basePath     = ".";
	inline static QString loginBlock   = " -u roy -proy ";
	inline static QString optionData   = " --single-transaction --no-create-info --extended-insert --quick --set-charset --skip-add-drop-table ";
	inline static QString optionSchema = " --single-transaction --no-data --opt --skip-add-drop-table ";
	inline static QString optionView   = " --single-transaction  --opt --skip-add-drop-table ";
	inline static QString optionEvents = " --no-create-db --no-create-info --no-data --events --routines --triggers --skip-opt ";

	QDateTime dateTime = QDateTime::currentDateTime();

	QString getFinalName(QString db, QString table) {
		QString finalName;
		//2020-12-28_22-01/DB/table
		return QString("%1/%4").arg(getFolder(db), table);
	}

	QString getFolder(QString db) {
		return QString("%1/%2/%3").arg(basePath, dateTime.toString("yyyy-MM-dd_HH-mm"), db);
	}

	bool dump(QString db, QString table, QString option, QString saveBlock) {
		saveBlock = saveBlock.arg(getFinalName(db, table));
		QProcess sh;
		auto     param = QString("mysqldump %1 %2 %3 %4 %5").arg(loginBlock, option, db, table, saveBlock);
		sh.start("sh", QStringList() << "-c"
		                             << param);

		sh.waitForFinished(1E9);

		if (auto stderr = sh.readAllStandardError(); !stderr.isEmpty()) {
			qCritical().noquote() << stderr;
			return false;
		}

		if (auto stdout = sh.readAllStandardOutput(); !stdout.isEmpty()) {
			qCritical().noquote() << stderr;
			return false;
		}
		sh.close();
		return true;
	}
};

class TableOpt {
      public:
	QString name;
	bool    isView = false;
};

void checkForUnhandledDB() {
	/*
	A che serve forzare una cosa che è il default ?
	la join serve solo per override, di default è attivo e fine ogni DB per la daily.
	Opzione certamente più logica e a prova di errore...
	
	Quindi foreach di tutto quello che sta in tableBackupView,
	    fase dello                                    schema(tabelle e viste)
	        ci si diverte a farlo                     threaded,
	    nella fase dati, essi van compressi quindi non servono i thread.
*/
	    auto res = db.query("SELECT SCHEMA_NAME FROM `dbBackupView` WHERE `id` IS NULL");
	if (!res.empty()) {
		qCritical().noquote() << res << R"(Are not accounted for the backup, please execute
INSERT IGNORE, Download CSV INTO dbBackup
SELECT 
NULL,
SCHEMA_NAME,
1,
1,
NOW(),
NULL
FROM `information_schema`.`SCHEMATA` ;
And configure if needed, in any case they will backed up even if you forget to configure them
)";
	}
}

std::map<QString, std::vector<TableOpt>> loadWorkSet() {
	std::map<QString, std::vector<TableOpt>> dbPack;

	auto res = db.query("SELECT * FROM dbBackupView WHERE enabled = 1 OR enabled IS NULL");
}

int main() {

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

	checkForUnhandledDB();

	Writer1 w;
	QDir().mkpath(w.getFolder("amarokdb"));

	std::map<QString, std::vector<TableOpt>> dbPack = loadWorkSet();

	dbPack["amarokdb"] = {{"albums"}, {"artists"}, {"prova1", true}};

	for (auto& [db, tables] : dbPack) {
		w.dump(db, "", w.optionEvents, "> %5events.sql");
		for (auto& table : tables) {
			if (table.isView) {
				w.dump(db, table.name, w.optionView, "> %5.view.sql");
			} else {
				w.dump(db, table.name, w.optionData, "| gzip > %5.data.sql.gz");
				w.dump(db, table.name, w.optionSchema, "> %5.schema.sql");
			}
		}
	}
}
