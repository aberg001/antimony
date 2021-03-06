#include <Python.h>

#include <QDebug>

#include <QPainter>
#include <QTextDocument>
#include <QTextCursor>
#include <QHelpEvent>

#include "graph/node.h"

#include "ui/script/editor.h"
#include "ui/script/syntax.h"
#include "ui/util/colors.h"
#include "ui_main_window.h"

#include "app/app.h"
#include "app/undo/undo_change_script.h"

ScriptEditor::ScriptEditor(Node* node, QWidget* parent)
    : QPlainTextEdit(parent), node(node)
{
    styleEditor(this);

    connect(document(), &QTextDocument::contentsChanged,
            this, &ScriptEditor::onTextChanged);

    connect(document(), &QTextDocument::undoCommandAdded,
            this, &ScriptEditor::onUndoCommandAdded);

    installEventFilter(this);

    trigger(node->getState());
}

void ScriptEditor::trigger(const NodeState& state)
{
    // Update the document text
    QString script = QString::fromStdString(state.script);
    if (script != document()->toPlainText())
    {
        // Keep the cursor at the same position in the document
        // (not 100% reliable)
        QTextCursor cursor = textCursor();
        int p = textCursor().position();
        document()->setPlainText(script);

        if (p < script.length())
        {
            cursor.setPosition(p);
            setTextCursor(cursor);
        }
    }

    // If the script is invalid, update the error line highlighting
    if (state.error_lineno != -1)
        highlightError(state.error_lineno);
    else
        setExtraSelections({});
}

void ScriptEditor::styleEditor(QPlainTextEdit* ed)
{
    QFont font;
    font.setFamily("Courier");
    QFontMetrics fm(font);
    ed->setTabStopWidth(fm.width("    "));
    ed->document()->setDefaultFont(font);

    new SyntaxHighlighter(ed->document());
    ed->setStyleSheet(QString(
        "QPlainTextEdit {"
        "    background-color: %1;"
        "    color: %2;"
        "}").arg(Colors::base00.name())
            .arg(Colors::base04.name()));

    ed->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}


void ScriptEditor::customizeUI(Ui::MainWindow* ui)
{
    ui->menuView->deleteLater();
    ui->menuAdd->deleteLater();

    connect(ui->actionCopy, &QAction::triggered,
            this, &QPlainTextEdit::copy);
    connect(ui->actionCut, &QAction::triggered,
            this, &QPlainTextEdit::cut);
    connect(ui->actionPaste, &QAction::triggered,
            this, &QPlainTextEdit::paste);
    connect(ui->actionShapes, &QAction::triggered,
            this, &ScriptEditor::openShapesLibrary);
}

void ScriptEditor::onTextChanged()
{
    node->setScript(document()->toPlainText().toStdString());
}

void ScriptEditor::onUndoCommandAdded()
{
    disconnect(document(), &QTextDocument::contentsChanged,
               this, &ScriptEditor::onTextChanged);

    document()->undo();
    QString before = document()->toPlainText();
    int cursor_before = textCursor().position();

    document()->redo();
    QString after = document()->toPlainText();
    int cursor_after = textCursor().position();

    App::instance()->pushStack(
            new UndoChangeScriptCommand(
                node, before, after,
                cursor_before, cursor_after, this));
    connect(document(), &QTextDocument::contentsChanged,
            this, &ScriptEditor::onTextChanged);
}

void ScriptEditor::openShapesLibrary()
{
    PyObject* fab_mod = PyImport_ImportModule("fab");
    PyObject* shapes_mod = PyObject_GetAttrString(fab_mod, "shapes");
    PyObject* shapes_path = PyObject_GetAttrString(shapes_mod, "__file__");

    Q_ASSERT(!PyErr_Occurred());

    auto filepath = QString::fromUtf8(PyUnicode_AsUTF8(shapes_path));

    QFile shapes(filepath);
    if (shapes.open(QIODevice::ReadOnly))
    {
        QTextStream in(&shapes);
        QString txt = in.readAll();

        auto ed = new QPlainTextEdit();
        styleEditor(ed);
        ed->document()->setPlainText(txt);
        ed->setReadOnly(true);
        ed->moveCursor(QTextCursor::Start);
        ed->resize(600, 600);
        ed->show();
    }

    Py_DECREF(fab_mod);
    Py_DECREF(shapes_mod);
    Py_DECREF(shapes_path);
}

bool ScriptEditor::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == this && event->type() == QEvent::KeyPress)
    {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->matches(QKeySequence::Undo))
            App::instance()->undo();
        else if (keyEvent->matches(QKeySequence::Redo))
            App::instance()->redo();
        else
            return false;
        return true;
    }
    return false;
}


void ScriptEditor::highlightError(int lineno)
{
    QTextEdit::ExtraSelection line;

    line.format.setBackground(Colors::dim(Colors::red));
    line.format.setProperty(QTextFormat::FullWidthSelection, true);

    line.cursor = textCursor();
    line.cursor.setPosition(
            document()->findBlockByLineNumber(lineno - 1).position() + 1);
    line.cursor.clearSelection();

    setExtraSelections({line});
}
