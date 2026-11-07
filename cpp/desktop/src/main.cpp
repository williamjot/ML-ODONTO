// ML ODONTO nativo: abre o banco (o mesmo do app R), aplica o tema e mostra a janela principal.

#include "Context.h"
#include "MainWindow.h"
#include "Pane.h"

#include <QApplication>
#include <QLockFile>
#include <QMessageBox>
#include <QDir>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("ML ODONTO");
    QApplication::setApplicationVersion("0.3.0");
    QApplication::setOrganizationName("LEMAG-UnB");
    app.setStyle("Fusion");
    app.setStyleSheet(odonto::styleSheet());

    odonto::Context ctx;
    // pasta dos dados: a mesma do app R; pode ser trocada com --dados <pasta> (testes)
    QString dataDir = odonto::Database::defaultDataDir();
    const auto args = QApplication::arguments();
    if (const int i = static_cast<int>(args.indexOf("--dados")); i >= 0 && i + 1 < args.size()) {
        dataDir = args[i + 1];
    }
    // uma janela por pasta de dados
    QDir().mkpath(dataDir);
    QLockFile lock(QDir(dataDir).filePath("ml_odonto_nativo.lock"));
    if (!lock.tryLock(100)) {
        QMessageBox::information(nullptr, "ML ODONTO", "O ML ODONTO já está aberto.");
        return 0;
    }
    QString error;
    if (!ctx.db.open(dataDir, &error)) {
        QMessageBox::critical(nullptr, "ML ODONTO", "Não foi possível abrir o banco de dados:\n" + error);
        return 1;
    }
    ctx.model = ctx.db.activeModel();
    ctx.reviewer = ctx.db.config("revisor");

    odonto::MainWindow window(ctx);
    window.showMaximized();
    // --tela <nome>: abre direto numa tela (atalhos e testes)
    if (const int i = static_cast<int>(args.indexOf("--tela")); i >= 0 && i + 1 < args.size()) {
        const QStringList names{"inicio", "revisao", "auditoria", "resultados", "aplicar", "ajuda", "config"};
        if (const int page = static_cast<int>(names.indexOf(args[i + 1].toLower())); page >= 0) {
            ctx.goTo(static_cast<odonto::Page>(page));
        }
    }
    if (const int i = static_cast<int>(args.indexOf("--aplicar")); i >= 0 && i + 1 < args.size()) {
        ctx.applyFolder(args[i + 1]);
    }
    if (ctx.db.counts(ctx.modelId()).images == 0) {
        QMessageBox::information(&window, "ML ODONTO",
                                 "O banco ainda não tem radiografias. Use a tela Aplicar para processar novas radiografias.");
    }
    window.askReviewerIfNeeded();
    return app.exec();
}
