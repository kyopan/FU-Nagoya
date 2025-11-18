#ifndef OTAUPDATER_H
#define OTAUPDATER_H

#include "globals.h"

// HTTPヘッダ解析用のユーティリティ関数
String getHeaderValue(String header, String headerName);

// OTA実行関数
void execOTA();

#endif
