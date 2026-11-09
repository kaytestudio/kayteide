#ifndef NAVIGATIONHISTORY_H
#define NAVIGATIONHISTORY_H

#include <QString>
#include <QVector>

#include <cstdlib>

// Back / Forward through editor locations, as in VS Code: a move to another
// file, or 10+ lines away in the same one, leaves a stop behind; smaller moves
// only update where you are.
class NavigationHistory
{
public:
    struct Location {
        QString path;
        int line = 1;     // 1-based
        int column = 1;   // 1-based
    };

    void visit(const Location &loc)
    {
        if (!m_hasCurrent) { m_current = loc; m_hasCurrent = true; return; }
        if (loc.path != m_current.path || std::abs(loc.line - m_current.line) >= kJump) {
            m_back << m_current;
            if (m_back.size() > kLimit) m_back.removeFirst();
            m_forward.clear();
        }
        m_current = loc;
    }

    bool canGoBack() const    { return !m_back.isEmpty(); }
    bool canGoForward() const { return !m_forward.isEmpty(); }

    Location back()
    {
        m_forward << m_current;
        m_current = m_back.takeLast();
        return m_current;
    }

    Location forward()
    {
        m_back << m_current;
        m_current = m_forward.takeLast();
        return m_current;
    }

private:
    static constexpr int kJump  = 10;
    static constexpr int kLimit = 50;
    QVector<Location> m_back;
    QVector<Location> m_forward;
    Location m_current;
    bool m_hasCurrent = false;
};

#endif // NAVIGATIONHISTORY_H
