#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QProcess>

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

int main() {
	Writer1 w;
	QDir().mkpath(w.getFolder("amarokdb"));

	std::map<QString, std::vector<TableOpt>> dbPack;

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
