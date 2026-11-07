#pragma once

// Acesso ao banco SQLite do ML ODONTO (equivale a fct_banco.R). É o mesmo arquivo e o mesmo esquema
// do aplicativo R, para os dois programas poderem ser usados durante a migração.

#include <QDateTime>
#include <QMap>
#include <QPolygonF>
#include <QSqlDatabase>
#include <QString>
#include <QVariant>
#include <QVector>

#include <optional>

namespace odonto {

struct Model {
    int id{0};
    QString name;
    QString file;
    QString type;
    int width{2000};
    double threshold{0.5};
    QString description;
    QString createdAt;
};

// Uma linha da lista de revisão (lista_revisao)
struct ReviewItem {
    QString id;
    QString file;
    bool sample{false};
    int detections{0};
    int group{4};          // 1 amostra · 2 com 4+ detecções · 3 sem detecção · 4 demais
    QString status;        // pendente · parcial · concluida
};

struct Detection {
    int id{0};
    int index{0};
    QPolygonF contour;     // pixels da imagem original
    QPointF center;
    std::optional<double> confidence;
};

struct AddedLesion {
    int id{0};
    QPolygonF contour;
};

struct ImageData {
    QString id;
    QString file;
    QString path;
    int width{0};
    int height{0};
    std::optional<int> reviewId;
    QString observations;
    bool inadequate{false};
    QString updatedAt;
    QVector<Detection> detections;
    QMap<int, QString> decisions;  // índice da detecção -> confirmada/rejeitada/duvida
    QVector<AddedLesion> added;
};

struct Proportion {
    int x{0};
    int n{0};
    double p{qQNaN()};
    double lower{qQNaN()};
    double upper{qQNaN()};
    [[nodiscard]] bool valid() const { return n > 0; }
};

struct ReviewSummary {
    int images{0};
    int confirmed{0};
    int rejected{0};
    int doubts{0};
    int added{0};
    Proportion precision;
    Proportion precisionPessimistic;
    Proportion sensitivity;
    double fpPerImage{qQNaN()};
};

struct ReviewStats {
    ReviewSummary all;
    ReviewSummary sample;
    int inadequate{0};
};

struct Counts {
    int images{0};
    int sample{0};
    int detections{0};
    int withDetection{0};
    int completed{0};
    int sampleCompleted{0};
    int reviewers{0};
};

struct EventRow { QString at, reviewer, type, detail; };

struct AuditedSensitivity {
    Proportion original;
    Proportion corrected;
    int answered{0}, missed{0}, excluded{0};
};

// Lesão de referência que um modelo não encontrou (lesoes_perdidas)
struct MissedLesion {
    int id{0};
    QString patient;
    int lesion{0};
    QPolygonF contour;
    QPointF center;
    double area{0}, diameter{0};
    QString arch;
    QString answer, observation;
    double distanceToFound{qQNaN()};  // escala de 1.400 px
};

struct ReferenceImage { QString patient, image, mask, trace, probability; int width{0}, height{0}; };
struct FrocRow { QString model; double threshold{0}; int lesions{0}, found{0}, predicted{0}, correct{0}, images{0}; };
struct PatientRow { QString model; double threshold{0}; QString patient; double inter{0}, sum{0}; int lesions{0}, found{0}, predicted{0}, correct{0}; };
struct MapRow { int id{0}; double diameter{0}; bool upper{false}; QString model; bool found{false}; };
struct KappaRow { QString first, second; int detections{0}; double agreement{0}, kappa{qQNaN()}; };
struct ApplicationRow { QString id, file, at; int lesions{0}; double seconds{0}, threshold{0}; std::optional<double> maxConfidence; };

// Uma lesão detectada pela tela Aplicar, pronta para gravar
struct NewDetection { int index{0}; QPolygonF contour; QPointF center; double area{0}, confidence{0}; };

class Database {
public:
    // Pasta padrão dos dados (Documentos/OdontoLesao), como caminho_dados_padrao()
    static QString defaultDataDir();

    bool open(const QString& dataDir, QString* error = nullptr);
    [[nodiscard]] QString dataDir() const { return dataDir_; }
    [[nodiscard]] QString file() const;
    [[nodiscard]] QSqlDatabase db() const { return QSqlDatabase::database(connection_); }

    // configuração e registro de eventos
    QString config(const QString& key, const QString& fallback = {}) const;
    void setConfig(const QString& key, const QString& value);
    void logEvent(const QString& reviewer, const QString& type, const QString& detail = {});

    std::optional<Model> activeModel() const;

    // revisão
    QVector<ReviewItem> reviewList(const QString& reviewer, int modelId) const;
    int obtainReview(const QString& imageId, int modelId, const QString& reviewer);
    ImageData imageData(const QString& imageId, int modelId, const QString& reviewer) const;
    void setDecision(int reviewId, const QString& imageId, int modelId, int index, const QString& decision);
    int addLesion(int reviewId, const QPolygonF& contour);
    void removeLesion(int reviewId, int lesionId);
    void setInadequate(int reviewId, bool value);
    void setObservations(int reviewId, const QString& text);
    void setCompleted(int reviewId, bool completed);

    // estatísticas
    ReviewStats reviewStats(int modelId, const QString& reviewer = {}) const;
    Counts counts(int modelId) const;
    QVector<QPair<QDate, int>> dailyProgress(int modelId) const;
    QVector<int> detectionsPerImage(int modelId) const;

    QVector<EventRow> events(int limit = 60) const;

    // referência (20 panorâmicas marcadas) e auditoria
    QPair<int, int> referenceCounts() const;  // lesões, pacientes
    QStringList validationModels() const;
    std::optional<AuditedSensitivity> auditedSensitivity(const QString& model, const QString& reviewer,
                                                         const QStringList& excluded = {"erro_marcacao", "nao_visivel"}) const;
    QVector<MissedLesion> missedLesions(const QString& model, const QString& reviewer) const;
    std::optional<ReferenceImage> referenceImage(const QString& patient) const;
    void saveAudit(int referenceId, const QString& model, const QString& reviewer, const std::optional<QString>& answer,
                   const std::optional<QString>& observation);

    // resultados
    QVector<FrocRow> frocRows() const;
    QVector<PatientRow> patientRows() const;
    QVector<MapRow> lesionMap() const;
    QVector<KappaRow> kappa(int modelId) const;

    // aplicação a novas radiografias
    QVector<ApplicationRow> applications(int modelId) const;
    std::optional<QString> imageIdByPath(const QString& path) const;
    bool hasReview(const QString& imageId, int modelId) const;
    QString saveApplication(const std::optional<QString>& existingId, const QString& path, int width, int height, int modelId,
                            const QVector<NewDetection>& detections, double threshold, double seconds);
    QString detectionsCsv(int modelId) const;

    // backup: uma cópia por dia, guarda as 7 mais recentes; e cópia imediata (VACUUM INTO)
    void dailyBackup() const;
    QString backupNow() const;

    static QString now();
    static QPolygonF parseContour(const QString& json);
    static QString contourJson(const QPolygonF& contour);

private:
    bool migrate(QString* error);
    void seedModels();

    QString dataDir_;
    QString connection_{"odonto"};
};

}  // namespace odonto
