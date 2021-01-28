#ifndef CONFIG_H
#define CONFIG_H

#include <QString>

class Config {
      public:
	static Config* getConfig();
	QString        compression = "xz -6 -T 4";

      private:
	Config() = default;
};

const Config* conf();

#endif // CONFIG_H
