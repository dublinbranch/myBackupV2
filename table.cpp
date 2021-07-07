#include "table.h"
#include "const.h"
#include "fileFunction/filefunction.h"
#include "fileFunction/folder.h"
#include "magicEnum/magic_from_string.hpp"
#include "mapExtensor/mapV2.h"
#include "minMysql/min_mysql.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QProcess>

extern DB              db;
extern const QString   backupFolder;
extern const QDateTime processStartTime;
extern const uint      compressionThreads;

QString Table::completeFolder() {
	return backupFolder + "/complete";
}

bool Table::isIncremental() const {
	return incrementalOn;
}

bool Table::isFractionated() const {
	return fractionSize;
}

QString Table::getLastBackupFolder() const {
	return lastBackupFolder;
}

bool Table::hasNewData() const {
	//we do not really need to check the id in case of progressive...
	if (lastUpdateTime.isValid()) {
		return lastUpdateTime > lastBackup;
	}
	return true;
}

void Table::annoyingJoin() {
	QString sql = R"(SELECT started,lastId,folder FROM backupResult WHERE TABLE_SCHEMA = %1 AND TABLE_NAME = %2 AND error IS NULL ORDER BY started DESC LIMIT 1)";
	sql         = sql.arg(base64this(schema), base64this(name));
	auto line   = db.queryLine(sql);
	if (line.empty()) {
		return;
	}
	line.rq("started", lastBackup);
	line.rq("lastId", lastId);
	line.rq("folder", lastBackupFolder);
}

Table::Table(const sqlRow& row) {
	row.rq("TABLE_SCHEMA", schema);
	row.rq("TABLE_NAME", name);
	row.rq("frequency", frequency);
	row.rq("fractionSize", fractionSize);
	row.rq("lastUpdateTime", lastUpdateTime);
	if (!lastUpdateTime.isValid()) {
		//see the note in innoDbLastUpdate for why we do thisb
		lastUpdateTime.setSecsSinceEpoch(0);
	}
	row.rq("compressionProg", compressionProg);
	row.rq("compressionOpt", compressionOpt);
	row.rq("compressionSuffix", compressionSuffix);
	row.rq("incremental", incremental);
	row.rq("incrementalOn", incrementalOn);

	row.rq("schemaOnly", schemaOnly);

	if (row["TABLE_TYPE"] == "view") {
		isView = true;
	}
	if (row["ENGINE"] == "InnoDB") {
		isInnoDb = true;
	}
	annoyingJoin();
	//innoDbLastUpdate();
}

Table::Table(QString _schema, QString _name) {
	schema = _schema;
	name   = _name;
}

QString Table::getDbFolder(QString folder) const {
	return folder + "/" + schema;
}

//TODO shall we place the schema for a multipart backup inside the table ?
QString Table::getPath(QString folder, FType type, bool noSuffix) const {
	auto base = QString("%1/%2/%3.%4").arg(folder, schema, schema, name);
	//2020-12-28_22-01/externalAgencies.rtyAssignmentStatusHistory  .data.sql.gz
	switch (type) {
	case FType::data: {
		if (isFractionated() || isIncremental()) {
			return base + "/";
		}
		base.append(".data.sql");
		if (noSuffix) {
			return base;
		}
		return base + "." + suffix();
	}

	case FType::schema: {
		return base + ".schema.sql";
	}

	case FType::view: {
		return base + ".view.sql";
	}
	default:
		qCritical() << "invalid type used: " << asString(type) << QStacker16();
		exit(1);
	}
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
	return isIncremental() || isFractionated();
}

void Table::dumpData() {
	quint64 inProgressIdStart = 0;
	quint64 inProgressIdEnd   = 0;
	quint64 currentMax        = getCurrentMax();
	QString where;
	QString completePath;
	QString saveBlock;
	if (isFractionated()) {
		completePath = getPath(Table::completeFolder(), FType::data, true);

		if (isIncremental()) {
			inProgressIdStart = lastId;
		} else {
			//Remove the current data, if any, to avoid remnant of previous partial backup
			QDir dir(completePath);
			dir.removeRecursively();
			inProgressIdStart = 0;
		}

		mkdir(completePath);
	} else if (isIncremental()) {
		completePath = getPath(Table::completeFolder(), FType::data, true);
		mkdir(completePath);
	} else {
		completePath = getPath(Table::completeFolder(), FType::data, false);
	}

	while (true) {
		if (isFractionated()) {
			inProgressIdEnd = std::min(inProgressIdStart + fractionSize, currentMax);
			where           = QSL(R"(--where="%1 > %2 AND %1 <= %3 ")").arg(incremental).arg(inProgressIdStart).arg(inProgressIdEnd);
			auto fileName   = QSL("%2/%3_%4_%5.data.sql.%6").arg(completePath, name).arg(inProgressIdStart).arg(inProgressIdEnd).arg(suffix());
			saveBlock       = QSL("%1 > %2").arg(compress(), fileName);
		} else {
			if (currentMax) { //we have an incremental
				where         = QSL(R"(--where="%1 > %2 AND %1 <= %3 ")").arg(incremental).arg(lastId).arg(currentMax);
				auto fileName = QSL("%2/%3_%4_%5.data.sql.%6").arg(completePath, name).arg(lastId).arg(currentMax).arg(suffix());
				saveBlock     = QSL("%1 > %2").arg(compress(), fileName);
			} else { //standard full table dump
				saveBlock = QSL("%1 > %2").arg(compress(), completePath);
			}
		}

		dump(optionData, saveBlock, where);
		if (isFractionated()) {
			if (inProgressIdEnd >= currentMax) {
				return;
			} else {
				inProgressIdStart = inProgressIdEnd;
			}
		} else {
			return;
		}
	}
}
void Table::dump(const QString& option, const QString& saveBlock, const QString& where) {
	QProcess sh;

	QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
	env.remove("LD_LIBRARY_PATH");
	sh.setProcessEnvironment(env);

	//TODO gestire se si usa il socket ?
	//We first evaluate the port, as the password can contain %1, use FMT to format the string
	auto const& conf = db.getConf();
	QString     ssl;
	if (conf.ssl) {
		ssl = " --ssl ";
	}
	QString loginBlock = QSL("-P %1 -u %2 -p%3 -h %4 %5").arg(conf.port).arg(QString(conf.user), QString(conf.pass), QString(conf.host), ssl);
	QString param      = QSL("mysqldump %1 %2 %3 %4 %5 %6").arg(loginBlock, option, schema, name, where, saveBlock);
	sh.start("sh", QStringList() << "-c" << param);
	started = QDateTime::currentDateTimeUtc();
	sh.waitForFinished(1E9);

	error.append(sh.readAllStandardError());
	error.append(sh.readAllStandardOutput());
	if (!error.isEmpty()) {
		qCritical().noquote() << error.leftRef(2048);
	}
	sh.close();
	completed = QDateTime::currentDateTimeUtc();
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
	ended = '%6',
	folder = %7
)";

	sql = sql.arg(base64this(schema), base64this(name), started.toString(mysqlDateTimeFormat), mayBeBase64(error, true))
	          .arg(getCurrentMax())
	          .arg(completed.toString(mysqlDateTimeFormat))
	          .arg(base64this(currentFolder()));
	db.query(sql);
}
