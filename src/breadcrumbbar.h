#ifndef BREADCRUMBBAR_H
#define BREADCRUMBBAR_H

#include "codesymbols.h"

#include <QWidget>

class QHBoxLayout;
class QMenu;

// The strip above an editor, as in VS Code:
//
//   src › mainwindow.cpp › ƒ MainWindow::setupToolsMenu
//
// Folder and file parts open a menu of what is beside them (folders open as
// submenus); symbol parts open the file's outline and jump to the pick.
// The path is shown relative to the project root.
class BreadcrumbBar : public QWidget
{
    Q_OBJECT
public:
    explicit BreadcrumbBar(QWidget *parent = nullptr);

    // Shared by every bar: the folder paths are shown relative to.
    static void    setProjectRoot(const QString &dir);
    static QString projectRoot();

    void setFile(const QString &path);
    void setSymbols(const QVector<CodeSymbol> &symbols);
    void setCursorLine(int line);
    void refresh();

    const QVector<CodeSymbol> &symbols() const { return m_symbols; }
    // The file's outline as a menu at `pos` (global); picks emit symbolActivated.
    void showOutline(const QPoint &pos);

signals:
    void openFileRequested(const QString &path);
    void symbolActivated(int line);

private:
    void rebuild();
    void addSeparator();
    void fillFolderMenu(QMenu *menu, const QString &dir, const QString &current);

    QHBoxLayout *m_layout;
    QString m_file;
    QVector<CodeSymbol> m_symbols;
    QVector<int> m_path;
    int m_line { 1 };
};

#endif // BREADCRUMBBAR_H
