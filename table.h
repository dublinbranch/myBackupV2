#ifndef TABLE_H
#define TABLE_H

#include <QDateTime>
#include <QString>

class sqlRow;
class Table {
      public:
	QString schema;
	QString name;
	QString incremental;
	QString fractionated;
	//InnoDB uses an unknown encoding scheme for non ASCII table name, so we force the path
	QString path;
	//In case there is an error we write in the db too in the result
	QString   error;
	QDateTime lastUpdateTime;
	QDateTime lastBackup;
	QDateTime started;
	QDateTime completed;
	uint      fractionSize = 0;
	uint      frequency    = 0;
	uint      lastId       = 0;

	bool isView   = false;
	bool isInnoDb = false;

	bool isIncremental() const;

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

	QString getDbFolder(QString folder) const;

	QString getPath(QString folder) const;

	//This is the folder where we ALWAYS store the current up to date data
	static QString completeFolder();
	//The folder for storing symlink of the current execution
	static QString currentFolder();
	//For this table, when the last backup was performed, used to move the old data from completedFolder before updating it
	QString lastBackupFolder() const;

	uint64_t getCurrentMax() const;
	bool     hasMultipleFile() const;
	void     dump(const QString &option, QString saveBlock, bool dataDump = false);

	QString compress() const;
	QString suffix() const;

      private:
	QString compressionProg;
	QString compressionOpt;
	QString compressionSuffix;
};

#endif // TABLE_H