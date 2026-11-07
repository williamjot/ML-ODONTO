// Ajuda: manual do dentista e atalhos da revisão (mod_ajuda.R). O manual é o mesmo Markdown do pacote R.

#include "Pane.h"
#include "pages/Pages.h"

#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QTextDocument>
#include <QVBoxLayout>

namespace odonto {

AjudaPage::AjudaPage(Context&, QWidget* parent) : QWidget(parent) {
    QFile source(":/manuais/manual_dentista.md");
    source.open(QIODevice::ReadOnly);
    const QString markdown = QString::fromUtf8(source.readAll());

    auto* manual = new QTextBrowser;
    manual->setOpenExternalLinks(true);
    manual->setFrameShape(QFrame::NoFrame);
    manual->document()->setDefaultStyleSheet(
        "h1{color:#1d4f86} h2{color:#1d4f86; margin-top:18px} table{border-collapse:collapse} "
        "td,th{border:1px solid #c9d1db; padding:4px 8px} th{background:#eef1f5} code{background:#eef1f5}");
    manual->setMarkdown(markdown);
    manual->setStyleSheet("QTextBrowser{padding:12px 24px; font-size:10.5pt;}");

    auto* side = new QWidget;
    auto* v = new QVBoxLayout(side);
    v->setContentsMargins(10, 8, 10, 8);
    auto* save = new QPushButton("Baixar manual (HTML)");
    connect(save, &QPushButton::clicked, this, [this, manual] {
        const QString file = QFileDialog::getSaveFileName(this, "Salvar manual", "Manual_do_dentista_ML_ODONTO.html", "HTML (*.html)");
        if (file.isEmpty()) return;
        QFile out(file);
        if (out.open(QIODevice::WriteOnly)) out.write(manual->document()->toHtml().toUtf8());
    });
    auto* keys = new QLabel(
        "<b>Atalhos da revisão</b><table cellpadding='3'>"
        "<tr><td><b>1 / 2 / 3</b></td><td>confirma / rejeita / dúvida</td></tr>"
        "<tr><td><b>Tab</b></td><td>próxima detecção</td></tr>"
        "<tr><td><b>M</b></td><td>desenhar lesão</td></tr>"
        "<tr><td><b>H</b></td><td>ocultar marcações</td></tr>"
        "<tr><td><b>0</b></td><td>ajustar à tela</td></tr>"
        "<tr><td><b>Enter</b></td><td>concluir imagem</td></tr>"
        "<tr><td><b>← / →</b></td><td>imagem anterior / próxima</td></tr></table>");
    v->addWidget(new QLabel("<b>Manual do dentista</b>"));
    v->addWidget(save);
    v->addSpacing(12);
    v->addWidget(keys);
    v->addStretch(1);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(5, 5, 5, 5);
    auto* split = splitter(Qt::Horizontal, {new Pane("Ajuda", side), new Pane("Manual do dentista", manual)}, {1, 4});
    split->setSizes({260, 1200});
    layout->addWidget(split);
}

}  // namespace odonto
