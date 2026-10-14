#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QList>

// Stream filter for application-owned logs and output exports. The live terminal
// still receives the original bytes. Partial sensitive values never reach disk.
class SensitiveDataFilter {
public:
    void addValue(const QByteArray& value) {
        if (!value.isEmpty() && !m_values.contains(value))
            m_values.append(value);
    }

    bool containsValue(const QByteArray& data) const {
        for (const QByteArray& value : m_values)
            if (data.contains(value))
                return true;
        return false;
    }

    QByteArray process(const QByteArray& data) {
        if (m_values.isEmpty())
            return data;
        m_pending.append(data);
        QByteArray result;
        qsizetype offset = 0;
        while (offset < m_pending.size()) {
            const auto remaining = QByteArrayView(m_pending).sliced(offset);
            qsizetype matched = 0;
            bool partial = false;
            for (const QByteArray& value : m_values) {
                if (remaining.startsWith(value))
                    matched = qMax(matched, value.size());
                else if (QByteArrayView(value).startsWith(remaining))
                    partial = true;
            }
            // Hold a possible prefix across packets. On close it is discarded,
            // as flushing it could expose part of a sensitive value.
            if (partial)
                break;
            if (matched > 0) {
                result.append("[redacted]");
                offset += matched;
            } else {
                result.append(m_pending.at(offset++));
            }
        }
        m_pending.remove(0, offset);
        return result;
    }

private:
    QList<QByteArray> m_values;
    QByteArray m_pending;
};
