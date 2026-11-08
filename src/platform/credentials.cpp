#include "credentials.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#endif

namespace tp::platform::credentials {

#ifdef Q_OS_WIN
bool write(const QString &target, const QString &secret, QString *error)
{
    const QByteArray blob = secret.toUtf8();
    std::wstring t = target.toStdWString();
    CREDENTIALW c{};
    c.Type = CRED_TYPE_GENERIC;
    c.TargetName = t.data();
    c.CredentialBlobSize = DWORD(blob.size());
    c.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(blob.constData()));
    c.Persist = CRED_PERSIST_LOCAL_MACHINE;
    std::wstring user = L"TravelPlanner";
    c.UserName = user.data();
    if (CredWriteW(&c, 0)) return true;
    if (error) *error = QStringLiteral("CredWriteW %1").arg(GetLastError());
    return false;
}

QString read(const QString &target)
{
    PCREDENTIALW c = nullptr;
    if (!CredReadW(reinterpret_cast<LPCWSTR>(target.utf16()), CRED_TYPE_GENERIC, 0, &c)) return {};
    const QString s = QString::fromUtf8(reinterpret_cast<const char *>(c->CredentialBlob), int(c->CredentialBlobSize));
    CredFree(c);
    return s;
}

bool remove(const QString &target)
{
    return CredDeleteW(reinterpret_cast<LPCWSTR>(target.utf16()), CRED_TYPE_GENERIC, 0) || GetLastError() == ERROR_NOT_FOUND;
}
#else
bool write(const QString &, const QString &, QString *error) { if (error) *error = QStringLiteral("not windows"); return false; }
QString read(const QString &) { return {}; }
bool remove(const QString &) { return true; }
#endif

} // namespace tp::platform::credentials
