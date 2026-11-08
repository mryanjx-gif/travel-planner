// Windows 凭据管理器（0.13.0，I-01；工作规则五：AI 钥匙存凭据管理器或 DPAPI，不放程序目录）。
// CredWriteW / CredReadW / CredDeleteW（advapi32，系统库）。普通凭据（CRED_TYPE_GENERIC），本机持久（CRED_PERSIST_LOCAL_MACHINE），
// 只有当前 Windows 用户能读。目标名由调用方给（AI 用 "TravelPlannerNative/ai/<服务商>"）。非 Windows 上什么都不做、读回空。
#pragma once

#include <QString>

namespace tp::platform::credentials {

bool write(const QString &target, const QString &secret, QString *error = nullptr);
QString read(const QString &target);   // 没有 = 空
bool remove(const QString &target);    // 本来就没有也算成功

} // namespace tp::platform::credentials
