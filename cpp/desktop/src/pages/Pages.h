#pragma once

// As telas do ML ODONTO (uma por módulo mod_*.R do app R).

#include "Context.h"

#include <QWidget>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSlider;
class QTableWidget;
class QTextBrowser;
class QTimer;
class QVBoxLayout;

namespace odonto {

class CountChart;
class FlowDiagram;
class FrocChart;
class GroupedBarChart;
class ImageViewer;
class LesionMapChart;
class ProgressChart;
class Pane;

class InicioPage : public QWidget {
    Q_OBJECT
public:
    explicit InicioPage(Context& ctx, QWidget* parent = nullptr);
private:
    void refresh();
    Context& ctx_;
    FlowDiagram* flow_;
    QTextBrowser* environment_;
    QTextBrowser* model_;
    QPlainTextEdit* activity_;
    ProgressChart* progress_;
    CountChart* perImage_;
};

class RevisaoPage : public QWidget {
    Q_OBJECT
public:
    explicit RevisaoPage(Context& ctx, QWidget* parent = nullptr);
    ~RevisaoPage() override;
private:
    void flushObservations();
    void reloadList();
    void open(const QString& imageId);
    void navigate(int step);
    void complete(bool confirmed = false);
    int reviewId();
    void markSaved();
    QVector<ReviewItem> filtered() const;

    Context& ctx_;
    QVector<ReviewItem> items_;
    QString current_;
    std::optional<int> reviewId_;
    QLabel* sampleLabel_;
    QProgressBar* sampleBar_;
    QLabel* allLabel_;
    QButtonGroup* filter_;
    QListWidget* list_;
    ImageViewer* viewer_;
    QLabel* saved_;
    QLineEdit* observations_;
    QCheckBox* inadequate_;
    QTimer* observationsTimer_;
    bool loading_{false};
};

class AuditoriaPage : public QWidget {
    Q_OBJECT
public:
    explicit AuditoriaPage(Context& ctx, QWidget* parent = nullptr);
protected:
    void showEvent(QShowEvent* event) override;
private:
    bool loaded_{false};
    void reload();
    void rebuildCards();
    void updateSummary();
    Context& ctx_;
    QComboBox* model_;
    QButtonGroup* filter_;
    QLabel* summary_;
    QProgressBar* bar_;
    QWidget* cards_;
    QVBoxLayout* cardsLayout_;
    QVector<MissedLesion> missed_;
};

class ResultadosPage : public QWidget {
    Q_OBJECT
public:
    explicit ResultadosPage(Context& ctx, QWidget* parent = nullptr);
private:
    void refreshValidation();
    void refreshReview();
    void refreshAudited();
    Context& ctx_;
    FrocChart* froc_;
    GroupedBarChart* metrics_;
    LesionMapChart* map_;
    QTableWidget* table_;
    QComboBox* auditModel_;
    QCheckBox* excludeError_;
    QCheckBox* excludeInvisible_;
    QLabel* audited_;
    GroupedBarChart* precision_;
    CountChart* perImage_;
    QLabel* kappa_;
};

class AplicarPage : public QWidget {
    Q_OBJECT
public:
    explicit AplicarPage(Context& ctx, QWidget* parent = nullptr);
    ~AplicarPage() override;
private:
    void setQueue(const QStringList& files);
    void start();
    void reloadTable();
    Context& ctx_;
    QStringList queue_;
    QLineEdit* folder_;
    QLabel* found_;
    QSlider* threshold_;
    QLabel* thresholdLabel_;
    QCheckBox* skip_;
    QPushButton* apply_;
    QPushButton* cancel_;
    QLabel* status_;
    QProgressBar* progress_;
    QLabel* warnings_;
    QTableWidget* table_;
    class Worker* worker_{nullptr};
};

class AjudaPage : public QWidget {
    Q_OBJECT
public:
    explicit AjudaPage(Context& ctx, QWidget* parent = nullptr);
protected:
    void showEvent(QShowEvent* event) override;  // o WebView2 só nasce quando a tela é aberta
private:
    Context& ctx_;
    QString page_;
    QWidget* body_{nullptr};
    bool started_{false};
};

class ConfigPage : public QWidget {
    Q_OBJECT
public:
    explicit ConfigPage(Context& ctx, QWidget* parent = nullptr);
private:
    Context& ctx_;
    QLineEdit* reviewer_;
    QCheckBox* blind_;
};

// Botão pequeno para salvar um gráfico em PNG
QPushButton* pngButton(class ChartWidget* chart, const QString& name, QWidget* parent);

}  // namespace odonto
