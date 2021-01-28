#include "config.h"

Config* Config::getConfig() {
	static Config config;
	return &config;
}

const Config* conf() {
	return Config::getConfig();
}
