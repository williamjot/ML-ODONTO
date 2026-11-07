#include "Pane.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabBar>
#include <QVBoxLayout>

namespace odonto {

Pane::Pane(const QString& title, QWidget* body, QWidget* parent) : QFrame(parent) {
    build();
    title_ = new QLabel(title);
    title_->setObjectName("paneTitle");
    header_->insertWidget(0, title_);
    stack_->addWidget(body);
}

Pane::Pane(const QStringList& tabs, const QList<QWidget*>& bodies, QWidget* parent) : QFrame(parent) {
    build();
    tabs_ = new QTabBar;
    tabs_->setObjectName("paneTabs");
    tabs_->setDrawBase(false);
    tabs_->setExpanding(false);
    for (const auto& tab : tabs) {
        tabs_->addTab(tab);
    }
    header_->insertWidget(0, tabs_, 0, Qt::AlignBottom);
    for (auto* body : bodies) {
        stack_->addWidget(body);
    }
    connect(tabs_, &QTabBar::currentChanged, stack_, &QStackedWidget::setCurrentIndex);
}

void Pane::build() {
    setObjectName("pane");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* head = new QFrame;
    head->setObjectName("paneHeader");
    header_ = new QHBoxLayout(head);
    header_->setContentsMargins(8, 0, 6, 0);
    header_->setSpacing(6);
    header_->addStretch(1);
    tools_ = new QHBoxLayout;
    tools_->setSpacing(4);
    header_->addLayout(tools_);
    stack_ = new QStackedWidget;
    stack_->setObjectName("paneBody");
    layout->addWidget(head);
    layout->addWidget(stack_, 1);
}

void Pane::addTool(QWidget* tool) { tools_->addWidget(tool); }

void Pane::setTitle(const QString& title) {
    if (title_ != nullptr) {
        title_->setText(title);
    }
}

QSplitter* splitter(Qt::Orientation orientation, const QList<QWidget*>& children, const QList<int>& stretch) {
    auto* s = new QSplitter(orientation);
    s->setHandleWidth(5);
    s->setChildrenCollapsible(false);
    for (int i = 0; i < children.size(); ++i) {
        s->addWidget(children[i]);
        s->setStretchFactor(i, i < stretch.size() ? stretch[i] : 1);
    }
    // tamanhos proporcionais iniciais (o QSplitter normaliza pela área disponível)
    QList<int> sizes;
    for (int i = 0; i < children.size(); ++i) {
        sizes << (i < stretch.size() ? stretch[i] : 1) * 100;
    }
    s->setSizes(sizes);
    return s;
}

QString styleSheet() {
    return R"(
* { font-family: "Segoe UI"; font-size: 9.5pt; color: #1f2a37; }
QMainWindow, QWidget#workArea { background: #dfe4ea; }
QToolBar { background: #eef1f5; border: 0; border-bottom: 1px solid #c9d1db; spacing: 2px; padding: 2px 6px; }
QToolBar QToolButton { padding: 2px 8px; border: 1px solid transparent; border-radius: 3px; color: #2d3a4a; }
QToolBar QToolButton:hover { border-color: #c9d1db; background: #ffffff; }
QToolBar QToolButton:checked { background: #d5e2f1; border-color: #9fb8d4; color: #1d4f86; }
QToolBar#screens { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #fbfcfd, stop:1 #eceff3); }
QToolBar#screens QToolButton { padding: 5px 10px; }
QLabel#brand { font-weight: 700; letter-spacing: 1px; padding: 0 6px 0 2px; }
QLabel#brandSub { color: #5f6b7a; padding-right: 12px; }
QLabel.pill { background: #ffffff; border: 1px solid #c9d1db; border-radius: 9px; padding: 1px 9px; color: #5f6b7a; }
QStatusBar { background: #2b6cb0; color: #ffffff; }
QStatusBar QLabel { color: #ffffff; padding: 0 8px; }
QSplitter::handle { background: transparent; }
QSplitter::handle:hover { background: #b9c7d8; }
QFrame#pane { background: #ffffff; border: 1px solid #c9d1db; }
QFrame#paneHeader { min-height: 26px; max-height: 26px; border-bottom: 1px solid #c9d1db;
  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #f7f9fb, stop:1 #e9edf2); }
QLabel#paneTitle { font-weight: 600; color: #2d3a4a; }
QTabBar#paneTabs::tab { padding: 4px 12px; border: 1px solid transparent; border-bottom: 0; color: #4a5768;
  margin-bottom: -1px; }
QTabBar#paneTabs::tab:selected { background: #ffffff; border-color: #c9d1db; color: #1f2a37; font-weight: 600; }
QStackedWidget#paneBody { background: #ffffff; }
QPushButton { padding: 3px 10px; border: 1px solid #b8c4d2; border-radius: 3px; background: #f7f9fb; }
QPushButton:hover { background: #eaf0f7; }
QPushButton:pressed, QPushButton:checked { background: #d5e2f1; }
QPushButton[primary="true"] { background: #2b6cb0; color: #ffffff; border-color: #1d4f86; }
QPushButton[primary="true"]:hover { background: #245c97; }
QPushButton[success="true"] { background: #2fb36b; color: #ffffff; border-color: #24935a; }
QPushButton[danger="true"] { color: #b42318; border-color: #e5a3a0; }
QTableWidget, QTableView, QListWidget, QTreeWidget, QTextBrowser, QPlainTextEdit, QLineEdit, QComboBox, QSpinBox {
  border: 1px solid #c9d1db; background: #ffffff; selection-background-color: #d5e2f1; selection-color: #1f2a37; }
QHeaderView::section { background: #eef1f5; border: 0; border-right: 1px solid #dde3ea; border-bottom: 1px solid #c9d1db;
  padding: 3px 6px; font-weight: 600; }
QPlainTextEdit#console { font-family: Consolas; font-size: 9pt; border: 0; }
QProgressBar { border: 1px solid #c9d1db; border-radius: 3px; background: #eef1f5; height: 8px; text-align: center; }
QProgressBar::chunk { background: #2fb36b; border-radius: 2px; }
QGroupBox { border: 1px solid #c9d1db; margin-top: 10px; padding-top: 6px; background: #ffffff; }
QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; font-weight: 600; }
)";
}

}  // namespace odonto
