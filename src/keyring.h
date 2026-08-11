#pragma once
#include <QString>

namespace Keyring {
bool storePassword(const QString& sessionId, const QString& password);
QString lookupPassword(const QString& sessionId);
bool deletePassword(const QString& sessionId);
} // namespace Keyring
