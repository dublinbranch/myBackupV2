#ifndef TABLE_H
#define TABLE_H

#include <QDateTime>
#include <QString>

class sqlRow;

enum class FType {
	view,
	schema,
	data
};

class Table {
      public:
	QString schema;
	QString name;
	QString incremental;
	bool    incrementalOn = false;
	//InnoDB uses an unknown encoding scheme for non ASCII table name, so we force the path
	QString path;
	//In case there is an error we write in the db too in the result
	QString   error;
	QString   lastBackupFolder;
	QDateTime lastUpdateTime;
	QDateTime lastBackup;
	QDateTime started;
	QDateTime completed;
	uint      fractionSize = 0;
	uint      frequency    = 0;
	uint      lastId       = 0;

	bool isView     = false;
	bool isInnoDb   = false;
	bool schemaOnly = false;

	bool isIncremental() const;
	bool isFractionated() const;
	//is just easier, as not all table have id -.- as the primary column...
	bool hasNewData() const;

	void annoyingJoin();
	void innoDbLastUpdate();
	void saveResult() const;
	Table(const sqlRow& row);
	Table(QString _schema, QString _name);

	QString getDbFolder(QString folder) const;

	QString getPath(QString folder, FType type, bool noSuffix = false) const;

	//This is the folder where we ALWAYS store the current up to date data
	static QString completeFolder();
	//The folder for storing symlink of the current execution
	static QString currentFolder();
	//For this table, when the last backup was performed, used to move the old data from completedFolder before updating it
	QString getLastBackupFolder() const;

	uint64_t getCurrentMax() const;
	bool     hasMultipleFile() const;
	void     dump(const QString& option, const QString& saveBlock, const QString& where = QString());
	void     dumpData();

	QString compress() const;
	QString suffix() const;

      private:
	QString compressionProg;
	QString compressionOpt;
	QString compressionSuffix;
};

#endif // TABLE_H
