#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDockWidget>
#include <QSyntaxHighlighter>
#include <QTabWidget>
#include <QVector>
#include <QHash>
#include <QPointer>
#include <QTreeView>
#include <QFileSystemModel>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDir>
#include <QFileDialog>
#include <QStandardPaths>
#include <QDebug>
#include <QProcess>
#include <QPlainTextEdit>
#include <QFont>
#include <QScrollBar>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QSortFilterProxyModel>
#include <QHeaderView>
#include <QToolButton>
#include <QMenu>
#include <QAction>

#include "../plugins/project/newprojectdialog.h"
#include "GitClientPanel.hpp"
#include "widgetpalettedock.h"
#include "uicanvaswidget.h"
#include "componentseditordock.h"
#include "projectimport.h"
#include "cmakeautoadd.h"
#include "projecttree.h"
#include "propertyeditordock.h"
#include "buildconfig.h"
#include "navigationhistory.h"
#include "editortabwidget.h"


class LineNumberArea;
class EditorTabWidget;
class WelcomeTabWidget;
class BottomPanel;
class EditorStatusBar;
class ModeBar;
class InFormIntegration;
class BuildConfigurations;
class ToolchainSetupDialog;
class QLocalServer;
class LargeFileTab;
#ifdef KAYTE_HAVE_QTERMWIDGET
class QTermWidget;
#endif

#include "vbsyntaxhighlighter.h"
#include "cppsyntaxhighlighter.h"
#include "kaytesyntaxhighlighter.h"
#include "pascalsyntaxhighlighter.h"
#include "delphisyntaxhighlighter.h"
#include "keyboard.h"

// ── Version control panels ────────────────────────────────────────────────────
// Forward-declare to avoid pulling heavy headers into every translation unit
// that includes mainwindow.h.
namespace Kayte::Svn { class SvnPanel; }
namespace Kayte::Llm { class AssistantPanel; }

// ─── TerminalWidget ───────────────────────────────────────────────────────────
// Embedded bash terminal. QTermWidget provides a real PTY/VT emulator where
// available; older or unsupported builds retain the simple shell fallback.
class TerminalWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TerminalWidget(QWidget *parent = nullptr);
    ~TerminalWidget() override;

    // Send a command directly to the running shell (e.g. cd into a project).
    void runCommand(const QString &command);

    // Short name of the shell program (e.g. "zsh"), for the terminal picker.
    QString shellName() const;

signals:
    // The shell process ended (user typed `exit`, Ctrl+D, ...).
    void shellExited();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
#ifndef KAYTE_HAVE_QTERMWIDGET
    void onReadyReadStdOut();
    void onReadyReadStdErr();
    void onReturnPressed();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
#endif

private:
    void setupUi();
    QString m_shell;
#ifdef KAYTE_HAVE_QTERMWIDGET
    QTermWidget *m_terminal { nullptr };
#else
    void appendOutput(const QString &text, bool isError = false);

    QPlainTextEdit *m_output  { nullptr };
    QLineEdit      *m_input   { nullptr };
    QProcess       *m_process { nullptr };
#endif
};
// ─────────────────────────────────────────────────────────────────────────────

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    // Open a file or project folder given on the command line.
    void openPath(const QString &path);
    void openFileAtLine(const QString &file, int line, int column = 0);
    bool maybeSaveLargeTab(LargeFileTab *large);

    // Reopen a saved local-LLM assistant session (KayteIDE --resume <hash>).
    bool resumeAssistantSession(const QString &hash, QString *error = nullptr);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void updateLineNumberArea(const QRect &rect, int dy);
    void closeEvent(QCloseEvent *event) override;

private slots:
    // ── File ──────────────────────────────────────────────────────────────────
    void onNewFile();
    void handleOpenFileTriggered();
    void handleSaveFileTriggered();
    bool handleSaveFileAsTriggered();
    void onCloseTab();
    void saveProjectAs();

    // ── Tab management ────────────────────────────────────────────────────────
    void onTabCloseRequested(int index);
    void onCurrentTabChanged(int index);
    void onTabClosed(QObject *obj = nullptr);

    // ── Tab title helpers ─────────────────────────────────────────────────────
    void updateTabTitle(bool modified);
    void updateTabTitleOnRename(const QString &newTitle);
    void handleTabModificationChanged(bool modified);
    void handleTabTitleChanged(const QString &title);

    // ── Build / Run ───────────────────────────────────────────────────────────
    void buildProject();
    void runProject();
    void cleanProject();
    void debugProject();

    // ── Tools menu (SVN + Git) ────────────────────────────────────────────────
    void setupToolsMenu();
    void setCurrentProjectPath(const QString &path);
    void launchUpdater();

    // ── Terminal dock ─────────────────────────────────────────────────────────
    void onToggleTerminal();

    // ── .xproj scaffold ───────────────────────────────────────────────────────
    void onCreateXProj();

    // ── .xprj projects (with VS Code / Qt Creator / clang-tidy imports) ──────
    void onOpenXprj();
    void onSaveProject();
    void onProjectImports();
    void runClangTidyOnFile();
    void runClangTidyOnProject();

    // ── Widget palette / UI designer ──────────────────────────────────────────
    void onToggleWidgetPalette();
    void onExportUiFile();
    void onClearCanvas();
    void onNewUiFile();

    // ── Project panel ─────────────────────────────────────────────────────────
    void onOpenProjectFolder();
    void onProjectTreeDoubleClicked(const QModelIndex &index);
    void onProjectTreeContextMenu(const QPoint &pos);
    void onCollapseAll();
    void onRefreshProject();

    // ── File browser ──────────────────────────────────────────────────────────
    void handlePathLineEditReturnPressed();
    void handleListViewDoubleClicked(const QModelIndex &index);

    // ── Dialogs ───────────────────────────────────────────────────────────────
    void showAboutDialog();
    void onNewProject();

    // ── Line number area ──────────────────────────────────────────────────────
    void updateLineNumberAreaWidth(int newBlockCount);

private:
    // ── UI ────────────────────────────────────────────────────────────────────
    Ui::MainWindow *ui;

    // ── Editor tabs ───────────────────────────────────────────────────────────
    QVector<EditorTabWidget *> openEditorTabs;
    EditorTabWidget           *currentEditorTab() const;
    void                       createNewTab(const QString &filePath = QString());
    bool                       saveCurrentFile();

    // ── Welcome tab (native start page) ──────────────────────────────────
    WelcomeTabWidget *m_welcomeTab { nullptr };
    void               showWelcomeTab();

    // ── Project panel (left dock) ──────────────────────────────────────────────
    QDockWidget            *m_projectDock        { nullptr };
    QTreeView              *m_projectTree        { nullptr };
    QFileSystemModel       *m_projectModel       { nullptr };
    QSortFilterProxyModel  *m_projectProxy       { nullptr };
    QLabel                 *m_projectNameLabel   { nullptr };
    QAction                *m_actProjectPanel    { nullptr };
    void                    setupProjectPanel();
    void                    setProjectRoot(const QString &path);
    void                    showFileHistory(const QString &filePath);

    // ── File browser ──────────────────────────────────────────────────────────
    QTreeView        *fileListView    { nullptr };
    QFileSystemModel *fileSystemModel { nullptr };
    QLineEdit        *pathLineEdit    { nullptr };
    QPushButton      *browseButton    { nullptr };
    void              setupFileBrowser();
    void              setCurrentPath(const QString &path);

    // ── Version control panels ────────────────────────────────────────────────
    Kayte::Svn::SvnPanel  *m_svnPanel  { nullptr };
    Kayte::GitClientPanel *m_gitPanel  { nullptr };
    QDockWidget           *m_gitDock   { nullptr };
    QString                m_currentProjectPath;

    // ── Terminal dock ─────────────────────────────────────────────────────────
    TerminalWidget *m_terminalWidget { nullptr };  // first terminal tab
    BottomPanel    *m_bottomPanel    { nullptr };  // Problems/Output/Debug/Terminal/Ports
    QDockWidget    *m_terminalDock   { nullptr };
    QAction        *m_actTerminal    { nullptr };  // checkable - toggles dock

    // ── Local LLM assistant (plugins/llm) ─────────────────────────────────────
    Kayte::Llm::AssistantPanel *m_assistant     { nullptr };
    QDockWidget                *m_assistantDock { nullptr };
    void                        setupAssistantDock();

    // ── Qt Creator-style mode bar (far left) ─────────────────────────────────
    ModeBar *m_modeBar     { nullptr };
    QAction *m_actGit      { nullptr };
    EditorStatusBar *m_editorStatus { nullptr };
    // Go > Back / Forward
    NavigationHistory m_navHistory;
    bool     m_navigating   { false };
    QAction *m_actNavBack    { nullptr };
    QAction *m_actNavForward { nullptr };
    int      m_modeWelcome { -1 };
    int      m_modeEdit    { -1 };
    int      m_modeDesign  { -1 };
    int      m_modeDebug   { -1 };
    int      m_btnProjects { -1 };
    int      m_btnGit      { -1 };
    int      m_btnHelp     { -1 };
    void     setupModeBar();

    // New Project: writes a starter project; returns its main file or "".
    QString  createProject(int type, const QString &name, const QString &parentDir, QString *error);
    void     openCreatedProject(const QString &name, const QString &root, const QString &mainFile);
    void     onModeBarItem(int index);
    void     updateModeBarKit();

    // ── Font Awesome ──────────────────────────────────────────────────────────
    // Load fa-solid-900.ttf from Qt resources (:/fa-solid-900.ttf) once, then
    // use m_faFont to render any ICON_FA_* glyph string on a QLabel / QAction.
    QFont m_faFont;
    void  setupFontAwesome();
    QIcon faIcon(const char *glyph) const;   // FA glyph -> theme-coloured icon
    void  applyFontAwesomeIcons();           // main toolbar / menu icons
    void  setupTerminalDock();
    void  setupWidgetPalette();
    void  setDesignerTabVisible(bool show, bool focus = true);

    // ── Widget Palette Dock ───────────────────────────────────────────────────
    WidgetPaletteDock *m_paletteDock   { nullptr };
    QWidget           *m_designerPage  { nullptr }; // central tab, after Welcome
    UiCanvasWidget    *m_canvas        { nullptr };
    QAction           *m_actPalette    { nullptr };
    QAction           *m_actDesigner   { nullptr };

    // ── Components editor (left) + Property editor (right) ────────────────────
    ComponentsEditorDock *m_componentsDock { nullptr };
    InFormIntegration    *m_inform         { nullptr }; // InForm .frm import/export/preview
    PropertyEditorDock   *m_propertyDock   { nullptr };
    QAction              *m_actComponents  { nullptr };
    QAction              *m_actProperties  { nullptr };

    // ── .xprj project and what it imports ────────────────────────────────────
    bool openXprjProject(const QString &path);
    bool writeXprj(const QString &path);
    void applyProjectImports();
    void applyEditorSettings(EditorTabWidget *tab);
    void setupEditorStatusBar();
    void setupNavigation();
    void recordNavigation();
    void navigateTo(const NavigationHistory::Location &loc);
    void updateNavigationActions();
    void updateEditorStatusBar();
    void runClangTidy(bool wholeProject);
    XprjProject     m_xprj;
    ClangTidyConfig m_clangTidy;
    EditorSettings  m_editorSettings;
    QStringList     m_cxxFlags;          // from c_cpp_properties.json, for clang-tidy without compile_commands.json
    QObject        *m_indentFilter { nullptr };
    bool            m_openXprjAsText { false };  // "Open as Text" on a .xprj in the project tree

    // ── CMakeLists.txt kept in step with new files ───────────────────────────
    void setupCMakeAutoAdd();
    bool applyCMakeEdit(const CMakeAuto::Edit &edit);
    CMakeAutoAdder *m_cmakeAuto { nullptr };

    // ── The whole project folder, loaded and watched ─────────────────────────
    void syncOpenFileWatch();                       // after tabs open / close / save as
    void onOpenFileChangedOnDisk(const QString &file);
    void onOpenFileRemovedFromDisk(const QString &file);
    ProjectTreeFilter *m_projectFilter { nullptr };
    ProjectLoader     *m_projectLoader { nullptr };
    OpenFileWatcher   *m_openFiles     { nullptr };
    QSet<QString>      m_reloadAsking;              // files whose "reload?" question is open

    // ── Project persistence ───────────────────────────────────────────────────
    QString m_currentProjectFilePath;
    QString m_currentProjectName;

    // ── Build commands (defaults; overridden by loaded project settings) ──────
    // Build / Run / Clean / Debug commands come from the active build
    // configuration of the current project (see buildconfig.h).
    BuildConfigurations *m_buildConfigs { nullptr };

    // First-run / on-demand toolchain installer (requirements.sh in the bundle).
    QPointer<ToolchainSetupDialog> m_setupDialog;

    // Local control channel: KayteIDEUpdater asks the IDE to quit before it
    // installs a new build (the normal close, with its unsaved-file prompts).
    QLocalServer *m_controlServer { nullptr };
    void setupControlServer();
    void maybeRunFirstSetup();
    void showToolchainSetup(bool firstRun);

    // Build diagnostics by file (canonical path), shown in the editors.
    QHash<QString, QVector<EditorTabWidget::Diagnostic>> m_diagnostics;
    static QString diagnosticKey(const QString &file);
    void    applyDiagnostics(EditorTabWidget *tab);
    // Run / Debug after a successful build ("Build before running").
    enum class AfterBuild { Nothing, Run, Debug };
    AfterBuild m_afterBuild { AfterBuild::Nothing };
    QString    m_afterBuildCommand;
    bool    buildFirst(AfterBuild then, const QString &thenCommand);
    void    onTaskFinished(int exitCode, bool crashed);
    void    ensureBuildConfigs();
    QString configCommand(const QString BuildConfiguration::*field, const QString &what);
    void    populateBuildConfigMenu(QMenu *menu);
    void    editBuildConfigurations();
    void    showKitMenu();
    QString buildWorkingDir() const;
    bool    confirmTrustedRun(const QString &command, const QString &dir);

    // ── Development mode (Edit / Design in the mode bar), saved in .xprj ──────
    enum class DevelopmentMode { TextEditor, RAD };
    DevelopmentMode currentDevelopmentMode { DevelopmentMode::TextEditor };

    // ── Project list ──────────────────────────────────────────────────────────
    void populateProjectList();

    // ── Keyboard shortcuts ────────────────────────────────────────────────────
    KeyboardShortcutsManager *m_keyboardShortcutsManager { nullptr };
};

#endif // MAINWINDOW_H
