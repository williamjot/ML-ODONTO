#pragma once

// Estado compartilhado entre as telas (equivale ao `ctx` de app_server.R).

#include "Database.h"

#include <QDateTime>
#include <QObject>

#include <optional>

namespace odonto {

enum class Page { Inicio, Revisao, Auditoria, Resultados, Aplicar, Ajuda, Config };

class Context : public QObject {
    Q_OBJECT
public:
    Database db;
    std::optional<Model> model;
    QString reviewer;
    QDateTime lastSaved;

    [[nodiscard]] int modelId() const { return model ? model->id : 0; }

    // Uma gravação aconteceu: atualiza painéis, gráficos e a barra de status
    void dataChanged() {
        lastSaved = QDateTime::currentDateTime();
        emit changed();
    }
    void refresh() { emit changed(); }
    void goTo(Page page) { emit pageRequested(page); }
    void openInReview(const QString& imageId) {
        emit reviewRequested(imageId);
        emit pageRequested(Page::Revisao);
    }
    // processa uma pasta na tela Aplicar (opção --aplicar <pasta>)
    void applyFolder(const QString& folder) {
        emit pageRequested(Page::Aplicar);
        emit applyRequested(folder);
    }
    void setReviewer(const QString& name) {
        reviewer = name;
        emit reviewerChanged();
        emit changed();
    }

signals:
    void changed();
    void pageRequested(Page page);
    void reviewRequested(const QString& imageId);
    void reviewerChanged();
    void applyRequested(const QString& folder);
};

}  // namespace odonto
