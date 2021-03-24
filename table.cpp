#include "table.h"
#include "const.h"
#include "fileFunction/folder.h"
#include "mapExtensor/mapV2.h"
#include "minMysql/min_mysql.h"
#include <QDebug>
#include <QFileInfo>
#include <QProcess>

extern DB              db;
extern const QString   backupFolder;
extern const QString   datadir;
extern const QDateTime processStartTime;
extern const uint      compressionThreads;

QString Table::completeFolder() {
	return backupFolder + "/complete";
}

bool Table::isIncremental() const {
	return !incremental.isEmpty();
}

QString Table::lastBackupFolder() const {
	return backupFolder + "/" + lastBackup.toString(folderTimeFormat);
}

bool Table::hasNewData() const {
	//we do not really need to check the id in case of progressive...
	return lastUpdateTime > lastBackup;
}

bool Table::moveOld() {
	if (this->isIncremental()) {
		return true;
	}

	//we only move the data, schema is NOT a symlink

	//file that contain the old symlink, to be removed
	auto lastPrefix = getPath(lastBackupFolder()) + ".data.sql.";
	{
		auto lastPattern = lastPrefix + "*";
		auto last        = search(lastPattern);
		erase(last);
	}

	//now move the current "old" data to the old table (before overwriting it with the new one)
	auto completePrefix  = getPath(completeFolder()) + ".data.sql.";
	auto completePattern = completePrefix + "*";
	auto complete        = search(completePattern);
	switch (complete.size()) {
	case 1:
		break;
	case 0:
		//This should be an error ?
		return false;
	default:

		qCritical() << "multiple files found for " << completePattern << "they are" << complete << "please remove/fix them";
		return false;
	}

	auto  s = QFileInfo(complete[0]).suffix();
	QFile file(completePrefix + s);
	if (!file.rename(lastPrefix + s)) {
		//In case the old folder got already delete the rename will fail, so just remove the file
		file.remove();
	}
	return true;
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
	row.rq("fractionated", fractionated);
	row.rq("fractionSize", fractionSize);
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

QString Table::getDbFolder(QString folder) const {
	return folder + "/" + schema;
}

QString Table::getPath(QString folder) const {
	//2020-12-28_22-01/externalAgencies.rtyAssignmentStatusHistory.data.sql.gz
	return QString("%1/%2/%3.%4").arg(folder, schema, schema, name);
}

QString Table::currentFolder() {
	return QString("%1/%2/").arg(backupFolder, processStartTime.toString("yyyy-MM-dd_HH-mm"));
}

uint64_t Table::getCurrentMax() const {
	if (hasMultipleFile()) {
		auto sql = QSL("SELECT COALESCE(MAX(%1),0) as maxId FROM `%2`.`%3`").arg(incremental, schema, name);
		return db.queryLine(sql).get2<uint64_t>("maxId");
	}
	return 0;
}

bool Table::hasMultipleFile() const {
	return !incremental.isEmpty() || !fractionated.isEmpty();
}

void Table::dump(const QString& option, QString saveBlock, bool dataDump) {
	QProcess sh;
	QString  where;
	quint64  inProgressIdStart = 0;
	quint64  inProgressIdEnd   = 0;
	quint64  currentMax        = 0;
	if (dataDump) {
		currentMax = getCurrentMax();
	}

	if (!fractionated.isEmpty()) {
		inProgressIdStart = lastId;
	}

	auto completePath = getPath(Table::completeFolder());
	if (currentMax) {
		mkdir(completePath);
	}
	while (true) {
		if (!fractionated.isEmpty()) {
			inProgressIdEnd = std::max(inProgressIdStart + fractionSize, currentMax);
			where           = QSL(R"(--where="%1 > %2 AND %1 <= %3 ")").arg(incremental).arg(inProgressIdStart).arg(inProgressIdEnd);

			auto fileName = QSL("%2/data_%4_%5.sql.%3").arg(completePath, suffix()).arg(inProgressIdStart).arg(inProgressIdEnd);
			saveBlock     = QSL("%1 > %2").arg(compress(), fileName);
		} else {
			if (currentMax) {

				where         = QSL(R"(--where="%1 > %2 AND %1 <= %3 ")").arg(incremental).arg(lastId).arg(currentMax);
				auto fileName = QSL("%2/data_%4_%5.sql.%3").arg(completePath, suffix()).arg(lastId).arg(currentMax);
				saveBlock     = QSL("%1 > %2").arg(compress(), fileName);
			}
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
		completed = QDateTime::currentDateTimeUtc();
		if (!dataDump) {
			return;
		}
		if (fractionated.isEmpty()) {
			return;
		} else if (inProgressIdEnd >= currentMax) {
			return;
		} else {
			inProgressIdStart = inProgressIdEnd;
		}
	}
}

QString Table::compress() const {
	if (compressionProg.isEmpty()) {
		return QSL("| pigz -p %1").arg(compressionThreads);
	} else {
		return QSL("| %1 %2").arg(compressionProg, compressionOpt);
	}
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

QString Table::suffix() const {
	if (compressionProg.isEmpty()) {
		return "gz";
	} else {
		return validSuffix(compressionProg, compressionSuffix);
	}
}

void Table::saveResult() const {
	QString sql = R"(
INSERT INTO backupResult
SET
	TABLE_SCHEMA = %1,
	TABLE_NAME = %2,
	started = '%3',
	error = %4,
	lastId = %5,
	ended = '%6'
)";

	sql = sql.arg(base64this(schema), base64this(name), started.toString(mysqlDateTimeFormat), mayBeBase64(error, true))
	          .arg(getCurrentMax())
	          .arg(completed.toString(mysqlDateTimeFormat));
	db.query(sql);
}
