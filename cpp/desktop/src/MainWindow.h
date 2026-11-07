#pragma once

// Janela principal: barra de telas, barra de ferramentas, área de trabalho e barra de status
// (equivale a app_ui.R e app_server.R).

#include "Context.h"

#include <QMainWindow>

class QActionGroup;
class QLabel;
class QStackedWidget;

namespace odonto {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(Context& ctx, QWidget* parent = nullptr);
    void askReviewerIfNeeded();

private:
    void showPage(Page page);
    void updateStatus();

    Context& ctx_;
    QStackedWidget* pages_{nullptr};
    QActionGroup* screens_{nullptr};
    QLabel* reviewerPill_{nullptr};
    QLabel* modelPill_{nullptr};
    QLabel* statusSample_{nullptr};
    QLabel* statusReviewed_{nullptr};
    QLabel* statusSaved_{nullptr};
    QLabel* statusDb_{nullptr};
};

}  // namespace odonto
