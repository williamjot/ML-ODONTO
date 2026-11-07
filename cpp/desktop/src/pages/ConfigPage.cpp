// Configurações: revisor, preferências, dados e backup (mod_config.R).

#include "Pane.h"
#include "pages/Pages.h"

#include <QApplication>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace odonto {

ConfigPage::ConfigPage(Context& ctx, QWidget* parent) : QWidget(parent), ctx_(ctx) {
    auto* prefs = new QWidget;
    auto* p = new QVBoxLayout(prefs);
    p->setContentsMargins(12, 10, 12, 10);
    reviewer_ = new QLineEdit(ctx_.reviewer);
    blind_ = new QCheckBox("Abrir imagens ainda não revisadas com as marcações ocultas (modo cego)");
    blind_->setChecked(ctx_.db.config("modo_cego_inicial", "FALSE") == "TRUE");
    auto* save = new QPushButton("Salvar");
    save->setProperty("primary", true);
    p->addWidget(new QLabel("Nome do revisor"));
    p->addWidget(reviewer_);
    p->addWidget(blind_);
    auto* row = new QHBoxLayout;
    row->addWidget(save);
    row->addStretch(1);
    p->addLayout(row);
    p->addStretch(1);

    auto* data = new QWidget;
    auto* d = new QVBoxLayout(data);
    d->setContentsMargins(12, 10, 12, 10);
    auto* folder = new QLabel("<b>Pasta dos dados:</b> " + QDir::toNativeSeparators(ctx_.db.dataDir()).toHtmlEscaped());
    folder->setWordWrap(true);
    folder->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* info = new QLabel("Banco, cópias reduzidas das imagens, relatórios e backups ficam nesta pasta. "
                            "Um backup do banco é feito automaticamente a cada dia (7 cópias).");
    info->setWordWrap(true);
    info->setStyleSheet("color:#5f6b7a;");
    auto* open = new QPushButton("Abrir a pasta");
    auto* backup = new QPushButton("Fazer backup agora");
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(open);
    buttons->addWidget(backup);
    buttons->addStretch(1);
    auto* about = new QLabel("<b>Programa:</b> ML ODONTO " + QApplication::applicationVersion() +
                             " (C++ nativo)<br><span style='color:#5f6b7a'>Uso acadêmico e não comercial. O programa apoia a "
                             "revisão por especialistas e não substitui o diagnóstico clínico.</span>");
    about->setWordWrap(true);
    d->addWidget(folder);
    d->addWidget(info);
    d->addLayout(buttons);
    d->addSpacing(16);
    d->addWidget(about);
    d->addStretch(1);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->addWidget(splitter(Qt::Horizontal, {new Pane("Revisor e preferências", prefs), new Pane("Dados", data)}, {1, 1}));

    connect(save, &QPushButton::clicked, this, [this] {
        const QString name = reviewer_->text().trimmed();
        if (name.isEmpty()) {
            QMessageBox::warning(this, "Configurações", "Informe o nome do revisor.");
            return;
        }
        ctx_.db.setConfig("revisor", name);
        ctx_.db.setConfig("modo_cego_inicial", blind_->isChecked() ? "TRUE" : "FALSE");
        ctx_.db.logEvent(name, "config_salva");
        if (name != ctx_.reviewer) ctx_.setReviewer(name);
        QMessageBox::information(this, "Configurações", "Configurações salvas.");
    });
    connect(open, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(ctx_.db.dataDir())); });
    connect(backup, &QPushButton::clicked, this, [this] {
        const QString file = ctx_.db.backupNow();
        QMessageBox::information(this, "Backup", file.isEmpty() ? QStringLiteral("Não foi possível fazer o backup.")
                                                                 : "Backup salvo em " + QDir::toNativeSeparators(file));
    });
    connect(&ctx_, &Context::reviewerChanged, this, [this] { reviewer_->setText(ctx_.reviewer); });
}

}  // namespace odonto
