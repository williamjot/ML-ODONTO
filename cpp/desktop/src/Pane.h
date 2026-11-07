#pragma once

// Painel do ambiente de trabalho (como painel()/painel_abas() em utils_layout.R): cabeçalho com título
// ou abas, ferramentas à direita e corpo. Os painéis são postos em QSplitter (divisórias arrastáveis).

#include <QFrame>
#include <QStringList>

class QHBoxLayout;
class QLabel;
class QStackedWidget;
class QTabBar;
#include <QSplitter>

namespace odonto {

class Pane : public QFrame {
    Q_OBJECT
public:
    Pane(const QString& title, QWidget* body, QWidget* parent = nullptr);
    // Painel com abas: um corpo por aba
    Pane(const QStringList& tabs, const QList<QWidget*>& bodies, QWidget* parent = nullptr);

    void addTool(QWidget* tool);
    void setTitle(const QString& title);

private:
    void build();
    QHBoxLayout* header_{nullptr};
    QLabel* title_{nullptr};
    QTabBar* tabs_{nullptr};
    QStackedWidget* stack_{nullptr};
    QHBoxLayout* tools_{nullptr};
};

// Divisória com tamanhos iniciais em proporção (ex.: {62, 38})
QSplitter* splitter(Qt::Orientation orientation, const QList<QWidget*>& children, const QList<int>& stretch);

// Folha de estilo do programa (equivale a estilo.css)
QString styleSheet();

// Cores do projeto
namespace color {
inline constexpr const char* blue = "#1F6FB4";
inline constexpr const char* darkBlue = "#0B2E59";
inline constexpr const char* lightBlue = "#7FB2DE";
inline constexpr const char* grey = "#6B7782";
inline constexpr const char* confirmed = "#2fb36b";
inline constexpr const char* rejected = "#e5484d";
inline constexpr const char* doubt = "#e8c33a";
inline constexpr const char* added = "#3fa9ff";
}  // namespace color

}  // namespace odonto
