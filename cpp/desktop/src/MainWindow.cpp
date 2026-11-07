#include "MainWindow.h"

#include "Icons.h"
#include "Metrics.h"
#include "Pane.h"
#include "pages/Pages.h"

#include <QActionGroup>
#include <QApplication>
#include <QDir>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>

namespace odonto {

MainWindow::MainWindow(Context& ctx, QWidget* parent) : QMainWindow(parent), ctx_(ctx) {
    setWindowTitle("ML ODONTO");
    setWindowIcon(QIcon(":/ml_odonto.ico"));
    resize(1500, 920);

    // barra de telas (como a barra de navegação do app R)
    auto* screens = addToolBar("Telas");
    screens->setObjectName("screens");
    screens->setMovable(false);
    screens->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    screens->setIconSize(QSize(15, 15));
    auto* logo = new QLabel;
    logo->setPixmap(QIcon(":/ml_odonto.ico").pixmap(20, 20));
    logo->setContentsMargins(6, 0, 4, 0);
    auto* brand = new QLabel("ML ODONTO");
    brand->setObjectName("brand");
    auto* sub = new QLabel("Lesões periapicais");
    sub->setObjectName("brandSub");
    screens->addWidget(logo);
    screens->addWidget(brand);
    screens->addWidget(sub);
    screens_ = new QActionGroup(this);
    struct Screen { Page page; const char* name; ushort glyph; };
    const Screen main[] = {{Page::Inicio, "Início", icons::home}, {Page::Revisao, "Revisão", icons::review},
                           {Page::Auditoria, "Auditoria", icons::search}, {Page::Resultados, "Resultados", icons::chart},
                           {Page::Aplicar, "Aplicar", icons::cpu}};
    const Screen right[] = {{Page::Ajuda, "Ajuda", icons::help}, {Page::Config, "Configurações", icons::settings}};
    auto addScreen = [&](const Screen& s) {
        auto* a = screens->addAction(icons::glyph(s.glyph), s.name);
        a->setCheckable(true);
        screens_->addAction(a);
        const Page page = s.page;
        connect(a, &QAction::triggered, this, [this, page] { showPage(page); });
        a->setData(static_cast<int>(page));
    };
    for (const auto& s : main) {
        addScreen(s);
    }
    auto* spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    screens->addWidget(spacer);
    for (const auto& s : right) {
        addScreen(s);
    }
    addToolBarBreak();

    // barra de ferramentas
    auto* tools = addToolBar("Ferramentas");
    tools->setMovable(false);
    tools->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    tools->setIconSize(QSize(14, 14));
    auto* review = tools->addAction(icons::glyph(icons::play), "Continuar revisão");
    review->setToolTip("Abrir a próxima imagem pendente");
    connect(review, &QAction::triggered, this, [this] { showPage(Page::Revisao); });
    connect(tools->addAction(icons::glyph(icons::search), "Auditoria"), &QAction::triggered, this,
            [this] { showPage(Page::Auditoria); });
    connect(tools->addAction(icons::glyph(icons::chart), "Resultados"), &QAction::triggered, this,
            [this] { showPage(Page::Resultados); });
    tools->addSeparator();
    auto* refresh = tools->addAction(icons::glyph(icons::refresh), {});
    refresh->setToolTip("Atualizar painéis e gráficos");
    connect(refresh, &QAction::triggered, &ctx_, &Context::refresh);
    auto* backup = tools->addAction(icons::glyph(icons::save), {});
    backup->setToolTip("Fazer backup do banco agora");
    connect(backup, &QAction::triggered, this, [this] {
        const QString file = ctx_.db.backupNow();
        if (file.isEmpty()) {
            QMessageBox::warning(this, "Backup", "Não foi possível fazer o backup.");
        } else {
            statusBar()->showMessage("Backup salvo em " + QDir::toNativeSeparators(file), 8000);
        }
    });
    tools->addSeparator();
    auto* help = tools->addAction(icons::glyph(icons::help), {});
    help->setToolTip("Abrir a ajuda");
    connect(help, &QAction::triggered, this, [this] { showPage(Page::Ajuda); });
    auto* spacer2 = new QWidget;
    spacer2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tools->addWidget(spacer2);
    reviewerPill_ = new QLabel;
    modelPill_ = new QLabel;
    for (auto* pill : {reviewerPill_, modelPill_}) {
        pill->setProperty("class", "pill");
        pill->setStyleSheet("background:#fff; border:1px solid #c9d1db; border-radius:9px; padding:1px 9px; color:#5f6b7a;");
        tools->addWidget(pill);
        auto* gap = new QWidget;
        gap->setFixedWidth(6);
        tools->addWidget(gap);
    }

    // telas
    pages_ = new QStackedWidget;
    pages_->setObjectName("workArea");
    pages_->addWidget(new InicioPage(ctx_));
    pages_->addWidget(new RevisaoPage(ctx_));
    pages_->addWidget(new AuditoriaPage(ctx_));
    pages_->addWidget(new ResultadosPage(ctx_));
    pages_->addWidget(new AplicarPage(ctx_));
    pages_->addWidget(new AjudaPage(ctx_));
    pages_->addWidget(new ConfigPage(ctx_));
    setCentralWidget(pages_);

    // barra de status
    auto* ready = new QLabel("● Pronto");
    statusSample_ = new QLabel;
    statusReviewed_ = new QLabel;
    statusSaved_ = new QLabel;
    statusDb_ = new QLabel;
    for (auto* l : {ready, statusSample_, statusReviewed_, statusSaved_}) {
        statusBar()->addWidget(l);
    }
    statusBar()->addPermanentWidget(statusDb_);
    statusBar()->setSizeGripEnabled(false);

    connect(&ctx_, &Context::pageRequested, this, &MainWindow::showPage);
    connect(&ctx_, &Context::changed, this, &MainWindow::updateStatus);
    showPage(Page::Inicio);
    updateStatus();
}

void MainWindow::showPage(Page page) {
    const int index = static_cast<int>(page);
    pages_->setCurrentIndex(index);
    for (auto* a : screens_->actions()) {
        if (a->data().toInt() == index) {
            a->setChecked(true);
        }
    }
}

void MainWindow::updateStatus() {
    const Counts c = ctx_.db.counts(ctx_.modelId());
    reviewerPill_->setText("👤 " + (ctx_.reviewer.isEmpty() ? QStringLiteral("revisor não definido") : ctx_.reviewer));
    QString model = ctx_.model ? ctx_.model->description : QStringLiteral("nenhum modelo");
    model = model.section(" (", 0, 0);
    modelPill_->setText("⚙ " + model);
    statusSample_->setText(QStringLiteral("Amostra %1/%2").arg(c.sampleCompleted).arg(c.sample));
    statusReviewed_->setText(QStringLiteral("%1 de %2 revisadas").arg(c.completed).arg(c.images));
    statusSaved_->setText(ctx_.lastSaved.isValid() ? "gravado às " + ctx_.lastSaved.toString("HH:mm:ss")
                                                   : QStringLiteral("nada gravado nesta sessão"));
    statusDb_->setText(QDir::toNativeSeparators(ctx_.db.file()) + "     ML ODONTO " + QApplication::applicationVersion());
}

void MainWindow::askReviewerIfNeeded() {
    if (!ctx_.reviewer.isEmpty()) {
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, "Bem-vindo ao ML ODONTO",
        "Informe seu nome. Ele identifica as suas revisões; cada revisor tem as próprias respostas.\n\nNome do revisor:",
        QLineEdit::Normal, {}, &ok).trimmed();
    if (ok && !name.isEmpty()) {
        ctx_.db.setConfig("revisor", name);
        ctx_.db.logEvent(name, "revisor_definido");
        ctx_.setReviewer(name);
    }
}

}  // namespace odonto
