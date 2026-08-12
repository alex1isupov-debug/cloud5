// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_CLOUDGTAHOST_H
#define CHIAKI_CLOUDGTAHOST_H

#include <QString>

class QGuiApplication;
class Settings;

int RunCloudGTAHost(QGuiApplication &app, Settings *settings);
int CloudGTAHostSelfTest();
int CloudGTAHostFail(const QString &code);

#endif
