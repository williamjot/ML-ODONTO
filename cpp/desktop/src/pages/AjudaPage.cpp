// Ajuda: o manual interativo do dentista (inst/manuais/manual_dentista.html, o mesmo publicado como página),
// mostrado num WebView2 embutido. Os botões "Abrir a tela" do manual chamam window.irPara(), que troca a
// tela do programa. Sem o WebView2, cai para o manual em Markdown num QTextBrowser.

#include "Pane.h"
#include "pages/Pages.h"

#include <QFile>
#include <QFileDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QStackedLayout>
#include <QTextBrowser>
#include <QVBoxLayout>

#ifdef ML_ODONTO_HAS_WEBVIEW
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "webview/webview.h"
#endif

#include <memory>

namespace odonto {
namespace {

QString resourceText(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

// Documento completo a partir do fragmento publicado (o publicador da página acrescenta o mesmo esqueleto)
QString fullDocument(QString page, bool offline) {
    if (offline) {
        // dentro do programa: nenhuma chamada à internet; as fontes caem para a Segoe UI
        page.remove(QRegularExpression("<link[^>]*fonts\\.(googleapis|gstatic)\\.com[^>]*>\\s*"));
    }
    return "<!doctype html><html lang=\"pt-BR\"><head><meta charset=\"utf-8\">"
           "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1, viewport-fit=cover\">"
           "<style>:root{color-scheme:light}body{margin:0;font:14px system-ui,sans-serif}img{max-width:100%}"
           "[hidden]{display:none!important}</style></head><body>" +
           page + "</body></html>";
}

#ifdef ML_ODONTO_HAS_WEBVIEW
// Janela nativa que hospeda o WebView2 e acompanha o tamanho do widget
class WebHost : public QWidget {
public:
    WebHost(Context& ctx, QString html) : ctx_(ctx), html_(std::move(html)) {
        setAttribute(Qt::WA_NativeWindow);
        setAttribute(Qt::WA_PaintOnScreen);  // o conteúdo é do WebView2, não do Qt
        setAttribute(Qt::WA_NoSystemBackground);
    }
    QPaintEngine* paintEngine() const override { return nullptr; }
    bool start() {
        try {
            auto hwnd = reinterpret_cast<HWND>(winId());
            view_ = std::make_unique<webview::webview>(false, hwnd);
            view_->bind("irPara", [this](const std::string& request) -> std::string {
                static const QStringList names{"inicio", "revisao", "auditoria", "resultados", "aplicar", "ajuda", "config"};
                const QString name = QString::fromStdString(request).remove(QRegularExpression("[\\[\\]\" ]"));
                if (const int page = static_cast<int>(names.indexOf(name)); page >= 0) {
                    QMetaObject::invokeMethod(&ctx_, [this, page] { ctx_.goTo(static_cast<Page>(page)); }, Qt::QueuedConnection);
                }
                return "true";
            });
            view_->set_html(html_.toStdString());
            fit();
            return true;
        } catch (...) {
            view_.reset();
            return false;
        }
    }

protected:
    void resizeEvent(QResizeEvent*) override { fit(); }

private:
    void fit() {
        if (!view_) return;
        const auto parent = reinterpret_cast<HWND>(winId());
        if (HWND child = FindWindowExW(parent, nullptr, L"webview_widget", nullptr)) {
            const qreal ratio = devicePixelRatioF();
            MoveWindow(child, 0, 0, qRound(width() * ratio), qRound(height() * ratio), TRUE);
        }
    }
    Context& ctx_;
    QString html_;
    std::unique_ptr<webview::webview> view_;
};
#endif

}  // namespace

AjudaPage::AjudaPage(Context& ctx, QWidget* parent) : QWidget(parent), ctx_(ctx) {
    page_ = resourceText(":/manuais/manual_dentista.html");
    body_ = new QWidget;
    auto* stack = new QVBoxLayout(body_);
    stack->setContentsMargins(0, 0, 0, 0);

    auto* pane = new Pane("Manual do dentista", body_);
    auto* save = new QPushButton("Salvar como HTML");
    save->setToolTip("Salva o manual num arquivo que abre em qualquer navegador");
    pane->addTool(save);
    connect(save, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getSaveFileName(this, "Salvar manual", "Manual_do_dentista_ML_ODONTO.html", "HTML (*.html)");
        if (file.isEmpty()) return;
        QFile out(file);
        if (out.open(QIODevice::WriteOnly)) out.write(fullDocument(page_, false).toUtf8());
    });

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->addWidget(pane);
}

void AjudaPage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (started_) return;
    started_ = true;
#ifdef ML_ODONTO_HAS_WEBVIEW
    auto* host = new WebHost(ctx_, fullDocument(page_, true));
    body_->layout()->addWidget(host);
    if (host->start()) return;
    delete host;
#endif
    // sem WebView2: o mesmo conteúdo em Markdown
    auto* text = new QTextBrowser;
    text->setFrameShape(QFrame::NoFrame);
    text->setOpenExternalLinks(true);
    text->setMarkdown(resourceText(":/manuais/manual_dentista.md"));
    text->setStyleSheet("QTextBrowser{padding:12px 24px; font-size:10.5pt;}");
    body_->layout()->addWidget(text);
}

}  // namespace odonto
