// 流式回答的解析（0.13.0，I-02 / I-07；Electron main.js ai:start 的 SSE 拆包 + assistant.js onEvent）。
// SseParser：字节流 → 一条条 data（两种接法都是 SSE）。StreamState：把每条 JSON 事件累加成文字、思考、用量、停止原因。
// Anthropic：message_start / content_block_start / content_block_delta（text / thinking / signature / input_json / citations）/ message_delta / error。
// OpenAI 兼容：choices[0].delta.content / reasoning_content / tool_calls、finish_reason、usage（最后一块，stream_options.include_usage）、[DONE]。
// 另有 mdLite：模型偶尔写的 Markdown 收成简单的富文本（Electron mdLite）。都是纯计算，tst_ai 测。
#pragma once

#include "aiproviders.h"

#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

namespace tp::ai {

class SseParser
{
public:
    // 喂一段字节，返回这段里完整的事件的 data（多行 data 连起来）。不完整的留到下次
    QStringList feed(const QByteArray &bytes);
    void reset() { m_buf.clear(); }

private:
    QByteArray m_buf;
};

struct StreamState {
    QString text, thinking, signature;
    QStringList citations;          // 来源网址（Anthropic citations_delta）
    QMap<int, QString> toolJson;    // 工具调用的参数（0.13.1 用；这里只累加）
    Usage usage;
    QString stop;                   // 停止原因（统一：end / max_tokens / tool_use / pause_turn / 其他原样）
    QString error;                  // 服务器在流里报的错
    bool done = false;              // 收到结束标记（message_stop / [DONE]）

    bool truncated() const { return stop == QLatin1String("max_tokens"); }
    // 喂一条 data；返回 false = 认不出（不是 JSON）
    bool feedAnthropic(const QString &data);
    bool feedOpenAi(const QString &data);
};

// HTTP 出错时的回复 → 给用户看的一句话（已翻译）
QString httpError(int status, const QByteArray &body);

// 模型写的文字 → Qt 富文本（StyledText 能显示的子集：<p> <b> <ul><li> <ol><li> <br>）
QString mdLite(const QString &s);

} // namespace tp::ai
