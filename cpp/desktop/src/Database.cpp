#include "Database.h"

#include "Metrics.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

namespace odonto {
namespace {

// Executa uma consulta com parâmetros posicionais; em caso de erro, devolve a consulta inválida.
QSqlQuery run(const QSqlDatabase& db, const QString& sql, const QVariantList& params = {}) {
    QSqlQuery query(db);
    query.prepare(sql);
    for (const auto& value : params) {
        query.addBindValue(value);
    }
    if (!query.exec()) {
        qWarning("SQL: %s\n%s", qPrintable(query.lastError().text()), qPrintable(sql));
    }
    return query;
}

QVariant scalar(const QSqlDatabase& db, const QString& sql, const QVariantList& params = {}) {
    auto query = run(db, sql, params);
    return query.next() ? query.value(0) : QVariant{};
}

}  // namespace

QString Database::defaultDataDir() {
    auto documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (documents.isEmpty()) {
        documents = QDir::homePath();
    }
    return QDir(documents).filePath("OdontoLesao");
}

QString Database::now() { return QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"); }

QString Database::file() const { return QDir(dataDir_).filePath("odontolesao.sqlite"); }

bool Database::open(const QString& dataDir, QString* error) {
    dataDir_ = dataDir;
    for (const auto* sub : {"", "cache", "backups", "relatorios", "logs"}) {
        QDir().mkpath(QDir(dataDir_).filePath(sub));
    }
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection_);
    db.setDatabaseName(file());
    if (!db.open()) {
        if (error != nullptr) {
            *error = db.lastError().text();
        }
        return false;
    }
    run(db, "PRAGMA foreign_keys = ON");
    run(db, "PRAGMA journal_mode = WAL");
    run(db, "PRAGMA busy_timeout = 5000");  // o app R pode estar gravando ao mesmo tempo
    if (!migrate(error)) {
        return false;
    }
    seedModels();
    dailyBackup();
    return true;
}

// Banco novo (sem o app R): registra os modelos de inst/modelos/modelos.yml, como importar_projeto()
void Database::seedModels() {
    if (scalar(db(), "SELECT COUNT(*) FROM modelos").toInt() > 0) {
        return;
    }
    QFile source(":/modelos/modelos.yml");
    if (!source.open(QIODevice::ReadOnly)) {
        return;
    }
    // YAML simples: "nome:" na coluna 0 e "  campo: valor" com dois espaços (o bloco meta é ignorado)
    QMap<QString, QMap<QString, QString>> models;
    QStringList order;
    QString current;
    for (const auto& raw : QString::fromUtf8(source.readAll()).split('\n')) {
        const QString line = QString(raw).remove('\r');
        if (line.trimmed().isEmpty() || line.trimmed().startsWith('#')) continue;
        if (!line.startsWith(' ') && line.endsWith(':')) {
            current = line.chopped(1).trimmed();
            order << current;
        } else if (!current.isEmpty() && line.startsWith("  ") && !line.startsWith("   ")) {
            const int colon = static_cast<int>(line.indexOf(':'));
            if (colon > 0) models[current][line.left(colon).trimmed()] = line.mid(colon + 1).trimmed();
        }
    }
    for (const auto& name : order) {
        const auto& m = models[name];
        run(db(), "INSERT OR IGNORE INTO modelos (nome, arquivo, tipo, largura, limiar, descricao, metricas, ativo, criado_em) "
                  "VALUES (?, ?, ?, ?, ?, ?, '{}', ?, ?)",
            {name, m.value("arquivo"), m.value("tipo"),
             m.contains("largura") ? QVariant(m.value("largura").toInt()) : QVariant(QMetaType(QMetaType::Int)),
             m.contains("limiar") ? QVariant(m.value("limiar").toDouble()) : QVariant(QMetaType(QMetaType::Double)),
             m.value("descricao"), m.value("ativo") == "true" ? 1 : 0, now()});
    }
}

// Mesmas migrações do pacote R (inst/sql/NNN_*.sql, embutidas como recurso)
bool Database::migrate(QString* error) {
    auto db = this->db();
    run(db, "CREATE TABLE IF NOT EXISTS versao_esquema (versao INTEGER NOT NULL)");
    const int current = scalar(db, "SELECT COALESCE(MAX(versao), 0) FROM versao_esquema").toInt();
    QStringList files = QDir(":/sql").entryList({"*.sql"}, QDir::Files, QDir::Name);
    static const QRegularExpression number("^(\\d+)_");
    static const QRegularExpression comment("--[^\\n]*");
    static const QRegularExpression separator(";\\s*(\\n|$)");
    for (const auto& name : files) {
        const auto match = number.match(name);
        const int version = match.hasMatch() ? match.captured(1).toInt() : 0;
        if (version <= current) {
            continue;
        }
        QFile source(":/sql/" + name);
        source.open(QIODevice::ReadOnly);
        QString text = QString::fromUtf8(source.readAll()).remove(comment);
        db.transaction();
        for (auto command : text.split(separator)) {
            command = command.trimmed();
            if (command.isEmpty()) {
                continue;
            }
            QSqlQuery query(db);
            if (!query.exec(command)) {
                db.rollback();
                if (error != nullptr) {
                    *error = name + ": " + query.lastError().text();
                }
                return false;
            }
        }
        run(db, "INSERT INTO versao_esquema (versao) VALUES (?)", {version});
        db.commit();
    }
    return true;
}

QString Database::config(const QString& key, const QString& fallback) const {
    const auto value = scalar(db(), "SELECT valor FROM config WHERE chave = ?", {key});
    return value.isValid() ? value.toString() : fallback;
}

void Database::setConfig(const QString& key, const QString& value) {
    run(db(), "INSERT INTO config (chave, valor) VALUES (?, ?) "
              "ON CONFLICT(chave) DO UPDATE SET valor = excluded.valor", {key, value});
}

void Database::logEvent(const QString& reviewer, const QString& type, const QString& detail) {
    run(db(), "INSERT INTO eventos (em, revisor, tipo, detalhe) VALUES (?, ?, ?, ?)",
        {now(), reviewer, type, detail});
}

std::optional<Model> Database::activeModel() const {
    auto q = run(db(), "SELECT id, nome, arquivo, tipo, largura, limiar, descricao, criado_em "
                       "FROM modelos WHERE ativo = 1 ORDER BY id DESC LIMIT 1");
    if (!q.next()) {
        return std::nullopt;
    }
    Model m;
    m.id = q.value(0).toInt();
    m.name = q.value(1).toString();
    m.file = q.value(2).toString();
    m.type = q.value(3).toString();
    m.width = q.value(4).isNull() ? 2000 : q.value(4).toInt();
    m.threshold = q.value(5).isNull() ? 0.5 : q.value(5).toDouble();
    m.description = q.value(6).toString();
    m.createdAt = q.value(7).toString();
    return m;
}

QVector<ReviewItem> Database::reviewList(const QString& reviewer, int modelId) const {
    auto q = run(db(), R"(
        SELECT i.id, i.arquivo, i.amostra,
               (SELECT COUNT(*) FROM deteccoes d WHERE d.imagem_id = i.id AND d.modelo_id = ?) AS n,
               r.concluida_em, r.id
        FROM imagens i
        LEFT JOIN revisoes r ON r.imagem_id = i.id AND r.modelo_id = ? AND r.revisor = ?
        ORDER BY i.id)", {modelId, modelId, reviewer});
    QVector<ReviewItem> items;
    while (q.next()) {
        ReviewItem item;
        item.id = q.value(0).toString();
        item.file = q.value(1).toString();
        item.sample = q.value(2).toInt() == 1;
        item.detections = q.value(3).toInt();
        item.group = item.sample ? 1 : item.detections >= 4 ? 2 : item.detections == 0 ? 3 : 4;
        item.status = !q.value(4).isNull() ? "concluida" : !q.value(5).isNull() ? "parcial" : "pendente";
        items.push_back(item);
    }
    std::stable_sort(items.begin(), items.end(), [](const ReviewItem& a, const ReviewItem& b) {
        if (a.group != b.group) {
            return a.group < b.group;
        }
        if (a.detections != b.detections) {
            return a.detections > b.detections;
        }
        return a.id < b.id;
    });
    return items;
}

int Database::obtainReview(const QString& imageId, int modelId, const QString& reviewer) {
    const auto existing = scalar(db(), "SELECT id FROM revisoes WHERE imagem_id = ? AND modelo_id = ? AND revisor = ?",
                                 {imageId, modelId, reviewer});
    if (existing.isValid()) {
        return existing.toInt();
    }
    auto q = run(db(), "INSERT INTO revisoes (imagem_id, modelo_id, revisor, iniciada_em, atualizada_em) "
                       "VALUES (?, ?, ?, ?, ?)", {imageId, modelId, reviewer, now(), now()});
    return q.lastInsertId().toInt();
}

QPolygonF Database::parseContour(const QString& json) {
    QPolygonF polygon;
    for (const auto& point : QJsonDocument::fromJson(json.toUtf8()).array()) {
        const auto xy = point.toArray();
        if (xy.size() >= 2) {
            polygon << QPointF(xy[0].toDouble(), xy[1].toDouble());
        }
    }
    return polygon;
}

QString Database::contourJson(const QPolygonF& contour) {
    QJsonArray points;
    for (const auto& p : contour) {
        points.append(QJsonArray{qRound(p.x()), qRound(p.y())});
    }
    return QString::fromUtf8(QJsonDocument(points).toJson(QJsonDocument::Compact));
}

ImageData Database::imageData(const QString& imageId, int modelId, const QString& reviewer) const {
    ImageData data;
    auto img = run(db(), "SELECT id, arquivo, caminho, largura, altura FROM imagens WHERE id = ?", {imageId});
    if (!img.next()) {
        return data;
    }
    data.id = img.value(0).toString();
    data.file = img.value(1).toString();
    data.path = img.value(2).toString();
    data.width = img.value(3).toInt();
    data.height = img.value(4).toInt();

    auto det = run(db(), "SELECT id, indice, contorno, cx, cy, confianca FROM deteccoes "
                         "WHERE imagem_id = ? AND modelo_id = ? ORDER BY indice", {imageId, modelId});
    while (det.next()) {
        Detection d;
        d.id = det.value(0).toInt();
        d.index = det.value(1).toInt();
        d.contour = parseContour(det.value(2).toString());
        d.center = {det.value(3).toDouble(), det.value(4).toDouble()};
        if (!det.value(5).isNull()) {
            d.confidence = det.value(5).toDouble();
        }
        data.detections.push_back(d);
    }

    auto rev = run(db(), "SELECT id, observacoes, inadequada, atualizada_em FROM revisoes "
                         "WHERE imagem_id = ? AND modelo_id = ? AND revisor = ?", {imageId, modelId, reviewer});
    if (rev.next()) {
        data.reviewId = rev.value(0).toInt();
        data.observations = rev.value(1).toString();
        data.inadequate = rev.value(2).toInt() == 1;
        data.updatedAt = rev.value(3).toString();
        auto dec = run(db(), "SELECT d.indice, x.decisao FROM decisoes x JOIN deteccoes d ON d.id = x.deteccao_id "
                             "WHERE x.revisao_id = ?", {*data.reviewId});
        while (dec.next()) {
            data.decisions.insert(dec.value(0).toInt(), dec.value(1).toString());
        }
        auto add = run(db(), "SELECT id, contorno FROM lesoes_adicionadas WHERE revisao_id = ? ORDER BY id",
                       {*data.reviewId});
        while (add.next()) {
            data.added.push_back({add.value(0).toInt(), parseContour(add.value(1).toString())});
        }
    }
    return data;
}

namespace {
void touch(const QSqlDatabase& db, int reviewId) {
    run(db, "UPDATE revisoes SET atualizada_em = ? WHERE id = ?", {Database::now(), reviewId});
}
}  // namespace

void Database::setDecision(int reviewId, const QString& imageId, int modelId, int index, const QString& decision) {
    const auto detection = scalar(db(), "SELECT id FROM deteccoes WHERE imagem_id = ? AND modelo_id = ? AND indice = ?",
                                  {imageId, modelId, index});
    if (!detection.isValid()) {
        return;
    }
    auto database = db();
    database.transaction();
    if (decision.isEmpty()) {
        run(database, "DELETE FROM decisoes WHERE revisao_id = ? AND deteccao_id = ?", {reviewId, detection});
    } else {
        run(database, "INSERT INTO decisoes (revisao_id, deteccao_id, decisao, em) VALUES (?, ?, ?, ?) "
                      "ON CONFLICT(revisao_id, deteccao_id) DO UPDATE SET decisao = excluded.decisao, em = excluded.em",
            {reviewId, detection, decision, now()});
    }
    touch(database, reviewId);
    database.commit();
}

int Database::addLesion(int reviewId, const QPolygonF& contour) {
    auto q = run(db(), "INSERT INTO lesoes_adicionadas (revisao_id, contorno, em) VALUES (?, ?, ?)",
                 {reviewId, contourJson(contour), now()});
    const int id = q.lastInsertId().toInt();
    touch(db(), reviewId);
    return id;
}

void Database::removeLesion(int reviewId, int lesionId) {
    run(db(), "DELETE FROM lesoes_adicionadas WHERE id = ? AND revisao_id = ?", {lesionId, reviewId});
    touch(db(), reviewId);
}

void Database::setInadequate(int reviewId, bool value) {
    run(db(), "UPDATE revisoes SET inadequada = ? WHERE id = ?", {value ? 1 : 0, reviewId});
    touch(db(), reviewId);
}

void Database::setObservations(int reviewId, const QString& text) {
    run(db(), "UPDATE revisoes SET observacoes = ? WHERE id = ?", {text, reviewId});
    touch(db(), reviewId);
}

void Database::setCompleted(int reviewId, bool completed) {
    run(db(), "UPDATE revisoes SET concluida_em = ? WHERE id = ?",
        {completed ? QVariant(now()) : QVariant(QMetaType(QMetaType::QString)), reviewId});
    touch(db(), reviewId);
}

ReviewStats Database::reviewStats(int modelId, const QString& reviewer) const {
    QString sql = R"(
        SELECT i.amostra, r.inadequada,
          (SELECT COUNT(*) FROM decisoes x WHERE x.revisao_id = r.id AND x.decisao = 'confirmada'),
          (SELECT COUNT(*) FROM decisoes x WHERE x.revisao_id = r.id AND x.decisao = 'rejeitada'),
          (SELECT COUNT(*) FROM decisoes x WHERE x.revisao_id = r.id AND x.decisao = 'duvida'),
          (SELECT COUNT(*) FROM lesoes_adicionadas a WHERE a.revisao_id = r.id)
        FROM revisoes r JOIN imagens i ON i.id = r.imagem_id
        WHERE r.concluida_em IS NOT NULL AND r.modelo_id = ? )";
    QVariantList params{modelId};
    if (!reviewer.isEmpty()) {
        sql += "AND r.revisor = ?";
        params << reviewer;
    }
    struct Row { bool sample; bool inadequate; int c, r, d, a; };
    QVector<Row> rows;
    auto q = run(db(), sql, params);
    while (q.next()) {
        rows.push_back({q.value(0).toInt() == 1, q.value(1).toInt() == 1, q.value(2).toInt(), q.value(3).toInt(),
                        q.value(4).toInt(), q.value(5).toInt()});
    }
    auto summarize = [&](bool onlySample) {
        ReviewSummary s;
        for (const auto& row : rows) {
            if (row.inadequate || (onlySample && !row.sample)) {
                continue;
            }
            ++s.images;
            s.confirmed += row.c;
            s.rejected += row.r;
            s.doubts += row.d;
            s.added += row.a;
        }
        s.precision = wilson(s.confirmed, s.confirmed + s.rejected);
        s.precisionPessimistic = wilson(s.confirmed, s.confirmed + s.rejected + s.doubts);
        s.sensitivity = wilson(s.confirmed, s.confirmed + s.added);
        s.fpPerImage = s.images > 0 ? static_cast<double>(s.rejected) / s.images : qQNaN();
        return s;
    };
    ReviewStats stats;
    stats.all = summarize(false);
    stats.sample = summarize(true);
    stats.inadequate = static_cast<int>(std::count_if(rows.begin(), rows.end(), [](const Row& r) { return r.inadequate; }));
    return stats;
}

Counts Database::counts(int modelId) const {
    const auto d = db();
    Counts c;
    c.images = scalar(d, "SELECT COUNT(*) FROM imagens").toInt();
    c.sample = scalar(d, "SELECT COUNT(*) FROM imagens WHERE amostra = 1").toInt();
    c.detections = scalar(d, "SELECT COUNT(*) FROM deteccoes WHERE modelo_id = ?", {modelId}).toInt();
    c.withDetection = scalar(d, "SELECT COUNT(DISTINCT imagem_id) FROM deteccoes WHERE modelo_id = ?", {modelId}).toInt();
    c.completed = scalar(d, "SELECT COUNT(DISTINCT imagem_id) FROM revisoes WHERE concluida_em IS NOT NULL AND modelo_id = ?",
                         {modelId}).toInt();
    c.sampleCompleted = scalar(d, "SELECT COUNT(DISTINCT r.imagem_id) FROM revisoes r JOIN imagens i ON i.id = r.imagem_id "
                                  "WHERE r.concluida_em IS NOT NULL AND i.amostra = 1 AND r.modelo_id = ?", {modelId}).toInt();
    c.reviewers = scalar(d, "SELECT COUNT(DISTINCT revisor) FROM revisoes").toInt();
    return c;
}

QVector<QPair<QDate, int>> Database::dailyProgress(int modelId) const {
    auto q = run(db(), "SELECT substr(concluida_em, 1, 10) AS dia, COUNT(*) FROM revisoes "
                       "WHERE concluida_em IS NOT NULL AND modelo_id = ? GROUP BY dia ORDER BY dia", {modelId});
    QVector<QPair<QDate, int>> result;
    while (q.next()) {
        result.push_back({QDate::fromString(q.value(0).toString(), "yyyy-MM-dd"), q.value(1).toInt()});
    }
    return result;
}

QVector<int> Database::detectionsPerImage(int modelId) const {
    auto q = run(db(), "SELECT (SELECT COUNT(*) FROM deteccoes d WHERE d.imagem_id = i.id AND d.modelo_id = ?) "
                       "FROM imagens i", {modelId});
    QVector<int> result;
    while (q.next()) {
        result.push_back(q.value(0).toInt());
    }
    return result;
}

QVector<EventRow> Database::events(int limit) const {
    auto q = run(db(), "SELECT em, revisor, tipo, detalhe FROM eventos ORDER BY id DESC LIMIT ?", {limit});
    QVector<EventRow> rows;
    while (q.next()) {
        rows.push_back({q.value(0).toString(), q.value(1).toString(), q.value(2).toString(), q.value(3).toString()});
    }
    return rows;
}

QPair<int, int> Database::referenceCounts() const {
    auto q = run(db(), "SELECT COUNT(*), COUNT(DISTINCT paciente) FROM referencia");
    return q.next() ? QPair<int, int>{q.value(0).toInt(), q.value(1).toInt()} : QPair<int, int>{0, 0};
}

QStringList Database::validationModels() const {
    auto q = run(db(), "SELECT DISTINCT modelo FROM achados_referencia");
    QStringList models;
    while (q.next()) {
        models << q.value(0).toString();
    }
    return models;
}

std::optional<AuditedSensitivity> Database::auditedSensitivity(const QString& model, const QString& reviewer,
                                                               const QStringList& excluded) const {
    auto q = run(db(), "SELECT f.achada, a.resposta FROM achados_referencia f "
                       "LEFT JOIN auditoria_perdidas a ON a.referencia_id = f.referencia_id AND a.modelo = f.modelo AND a.revisor = ? "
                       "WHERE f.modelo = ?", {reviewer, model});
    int total = 0, found = 0;
    AuditedSensitivity s;
    while (q.next()) {
        ++total;
        const bool hit = q.value(0).toInt() == 1;
        found += hit ? 1 : 0;
        if (!hit) {
            ++s.missed;
            if (!q.value(1).isNull()) {
                ++s.answered;
                s.excluded += excluded.contains(q.value(1).toString()) ? 1 : 0;
            }
        }
    }
    if (total == 0) {
        return std::nullopt;
    }
    s.original = wilson(found, total);
    s.corrected = wilson(found, total - s.excluded);
    return s;
}

QVector<MissedLesion> Database::missedLesions(const QString& model, const QString& reviewer) const {
    auto q = run(db(), R"(
        SELECT r.id, r.paciente, r.lesao, r.contorno, r.cx, r.cy, r.area, r.diam_eq_px, r.arcada, a.resposta, a.observacao
        FROM referencia r JOIN achados_referencia f ON f.referencia_id = r.id AND f.modelo = ?
        LEFT JOIN auditoria_perdidas a ON a.referencia_id = r.id AND a.modelo = ? AND a.revisor = ?
        WHERE f.achada = 0 ORDER BY r.paciente, r.lesao)", {model, model, reviewer});
    QVector<MissedLesion> lesions;
    while (q.next()) {
        MissedLesion l;
        l.id = q.value(0).toInt();
        l.patient = q.value(1).toString();
        l.lesion = q.value(2).toInt();
        l.contour = parseContour(q.value(3).toString());
        l.center = {q.value(4).toDouble(), q.value(5).toDouble()};
        l.area = q.value(6).toDouble();
        l.diameter = q.value(7).toDouble();
        l.arch = q.value(8).toString();
        l.answer = q.value(9).toString();
        l.observation = q.value(10).toString();
        lesions.push_back(l);
    }
    // distância (escala de 1.400 px) até a lesão encontrada mais próxima no mesmo paciente
    auto found = run(db(), "SELECT r.paciente, r.contorno, i.largura FROM referencia r "
                           "JOIN achados_referencia f ON f.referencia_id = r.id AND f.modelo = ? "
                           "JOIN referencia_imagens i ON i.paciente = r.paciente WHERE f.achada = 1", {model});
    QMultiMap<QString, QPair<QPolygonF, int>> byPatient;
    while (found.next()) {
        byPatient.insert(found.value(0).toString(), {parseContour(found.value(1).toString()), found.value(2).toInt()});
    }
    for (auto& l : lesions) {
        double best = qInf();
        int width = 0;
        for (auto it = byPatient.find(l.patient); it != byPatient.end() && it.key() == l.patient; ++it) {
            width = it->second;
            for (const auto& a : l.contour) {
                for (const auto& b : it->first) {
                    best = std::min(best, std::hypot(a.x() - b.x(), a.y() - b.y()));
                }
            }
        }
        if (width > 0 && std::isfinite(best)) {
            l.distanceToFound = best * 1400.0 / width;
        }
    }
    return lesions;
}

std::optional<ReferenceImage> Database::referenceImage(const QString& patient) const {
    auto q = run(db(), "SELECT paciente, caminho_imagem, caminho_mascara, caminho_traco, caminho_prob, largura, altura "
                       "FROM referencia_imagens WHERE paciente = ?", {patient});
    if (!q.next()) {
        return std::nullopt;
    }
    return ReferenceImage{q.value(0).toString(), q.value(1).toString(), q.value(2).toString(), q.value(3).toString(),
                          q.value(4).toString(), q.value(5).toInt(), q.value(6).toInt()};
}

void Database::saveAudit(int referenceId, const QString& model, const QString& reviewer,
                         const std::optional<QString>& answer, const std::optional<QString>& observation) {
    auto current = run(db(), "SELECT resposta, observacao FROM auditoria_perdidas WHERE referencia_id = ? AND modelo = ? AND revisor = ?",
                       {referenceId, model, reviewer});
    QVariant oldAnswer(QMetaType(QMetaType::QString));
    QString oldObservation;
    if (current.next()) {
        oldAnswer = current.value(0);
        oldObservation = current.value(1).toString();
    }
    run(db(), "INSERT INTO auditoria_perdidas (referencia_id, modelo, revisor, resposta, observacao, em) VALUES (?, ?, ?, ?, ?, ?) "
              "ON CONFLICT(referencia_id, modelo, revisor) DO UPDATE SET resposta = excluded.resposta, "
              "observacao = excluded.observacao, em = excluded.em",
        {referenceId, model, reviewer, answer ? QVariant(*answer) : oldAnswer, observation.value_or(oldObservation), now()});
}

QVector<FrocRow> Database::frocRows() const {
    auto q = run(db(), "SELECT modelo, limiar, lesoes, achadas, previstas, corretas, n_imagens FROM validacao_froc");
    QVector<FrocRow> rows;
    while (q.next()) {
        rows.push_back({q.value(0).toString(), q.value(1).toDouble(), q.value(2).toInt(), q.value(3).toInt(),
                        q.value(4).toInt(), q.value(5).toInt(), q.value(6).toInt()});
    }
    return rows;
}

QVector<PatientRow> Database::patientRows() const {
    auto q = run(db(), "SELECT modelo, limiar, paciente, inter, soma, lesoes, lesoes_achadas, previstas, previstas_corretas "
                       "FROM validacao_pacientes");
    QVector<PatientRow> rows;
    while (q.next()) {
        rows.push_back({q.value(0).toString(), q.value(1).toDouble(), q.value(2).toString(), q.value(3).toDouble(),
                        q.value(4).toDouble(), q.value(5).toInt(), q.value(6).toInt(), q.value(7).toInt(), q.value(8).toInt()});
    }
    return rows;
}

QVector<MapRow> Database::lesionMap() const {
    auto q = run(db(), "SELECT r.id, r.diam_eq_px, r.arcada, f.modelo, f.achada FROM referencia r "
                       "JOIN achados_referencia f ON f.referencia_id = r.id");
    QVector<MapRow> rows;
    while (q.next()) {
        rows.push_back({q.value(0).toInt(), q.value(1).toDouble(), q.value(2).toString() == "superior",
                        q.value(3).toString(), q.value(4).toInt() == 1});
    }
    return rows;
}

QVector<KappaRow> Database::kappa(int modelId) const {
    auto q = run(db(), "SELECT r.revisor, x.deteccao_id, x.decisao FROM decisoes x JOIN revisoes r ON r.id = x.revisao_id "
                       "WHERE r.modelo_id = ? AND r.concluida_em IS NOT NULL AND x.decisao IN ('confirmada', 'rejeitada')", {modelId});
    QMap<QString, QMap<int, bool>> byReviewer;  // revisor -> detecção -> confirmada?
    while (q.next()) {
        byReviewer[q.value(0).toString()][q.value(1).toInt()] = q.value(2).toString() == "confirmada";
    }
    QVector<KappaRow> rows;
    const auto names = byReviewer.keys();
    for (int i = 0; i < names.size(); ++i) {
        for (int j = i + 1; j < names.size(); ++j) {
            const auto& a = byReviewer[names[i]];
            const auto& b = byReviewer[names[j]];
            int n = 0, agree = 0, ca = 0, cb = 0;
            for (auto it = a.begin(); it != a.end(); ++it) {
                if (!b.contains(it.key())) {
                    continue;
                }
                ++n;
                agree += it.value() == b[it.key()] ? 1 : 0;
                ca += it.value() ? 1 : 0;
                cb += b[it.key()] ? 1 : 0;
            }
            if (n < 2) {
                continue;
            }
            const double po = double(agree) / n;
            const double pa = double(ca) / n, pb = double(cb) / n;
            const double pe = pa * pb + (1 - pa) * (1 - pb);
            rows.push_back({names[i], names[j], n, po, pe < 1 ? (po - pe) / (1 - pe) : qQNaN()});
        }
    }
    return rows;
}

QVector<ApplicationRow> Database::applications(int modelId) const {
    auto q = run(db(), R"(
        SELECT a.imagem_id, i.arquivo, a.n_lesoes, a.segundos, a.limiar, a.em,
               (SELECT MAX(confianca) FROM deteccoes d WHERE d.imagem_id = a.imagem_id AND d.modelo_id = a.modelo_id)
        FROM aplicacoes a JOIN imagens i ON i.id = a.imagem_id WHERE a.modelo_id = ? ORDER BY a.id DESC)", {modelId});
    QVector<ApplicationRow> rows;
    while (q.next()) {
        ApplicationRow r;
        r.id = q.value(0).toString();
        r.file = q.value(1).toString();
        r.lesions = q.value(2).toInt();
        r.seconds = q.value(3).toDouble();
        r.threshold = q.value(4).toDouble();
        r.at = q.value(5).toString();
        if (!q.value(6).isNull()) {
            r.maxConfidence = q.value(6).toDouble();
        }
        rows.push_back(r);
    }
    return rows;
}

std::optional<QString> Database::imageIdByPath(const QString& path) const {
    const auto id = scalar(db(), "SELECT id FROM imagens WHERE caminho = ?", {QDir::toNativeSeparators(path)});
    return id.isValid() ? std::optional<QString>(id.toString()) : std::nullopt;
}

bool Database::hasReview(const QString& imageId, int modelId) const {
    return scalar(db(), "SELECT COUNT(*) FROM revisoes WHERE imagem_id = ? AND modelo_id = ?", {imageId, modelId}).toInt() > 0;
}

QString Database::saveApplication(const std::optional<QString>& existingId, const QString& path, int width, int height,
                                  int modelId, const QVector<NewDetection>& detections, double threshold, double seconds) {
    auto database = db();
    database.transaction();
    QString id;
    if (existingId) {
        id = *existingId;
    } else {
        // próximo identificador anônimo A###, como proximo_id_imagem()
        int highest = 0;
        auto ids = run(database, "SELECT id FROM imagens");
        while (ids.next()) {
            bool ok = false;
            const int n = ids.value(0).toString().mid(1).toInt(&ok);
            if (ok) {
                highest = std::max(highest, n);
            }
        }
        id = QStringLiteral("A%1").arg(highest + 1, 3, 10, QChar('0'));
        run(database, "INSERT INTO imagens (id, arquivo, caminho, largura, altura, amostra, importada_em) VALUES (?, ?, ?, ?, ?, 0, ?)",
            {id, QFileInfo(path).fileName(), QDir::toNativeSeparators(path), width, height, now()});
    }
    run(database, "DELETE FROM deteccoes WHERE imagem_id = ? AND modelo_id = ?", {id, modelId});
    for (const auto& d : detections) {
        run(database, "INSERT INTO deteccoes (imagem_id, modelo_id, indice, contorno, cx, cy, area, confianca) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
            {id, modelId, d.index, contourJson(d.contour), d.center.x(), d.center.y(), d.area, d.confidence});
    }
    run(database, "INSERT INTO aplicacoes (imagem_id, modelo_id, limiar, n_lesoes, segundos, em) VALUES (?, ?, ?, ?, ?, ?)",
        {id, modelId, threshold, static_cast<int>(detections.size()), seconds, now()});
    database.commit();
    return id;
}

QString Database::detectionsCsv(int modelId) const {
    auto q = run(db(), "SELECT d.imagem_id, i.arquivo, d.indice, ROUND(d.cx), ROUND(d.cy), ROUND(d.area), ROUND(d.confianca, 3) "
                       "FROM deteccoes d JOIN imagens i ON i.id = d.imagem_id "
                       "WHERE d.modelo_id = ? AND d.imagem_id IN (SELECT imagem_id FROM aplicacoes) ORDER BY d.imagem_id, d.indice",
                 {modelId});
    QString csv = "imagem_id;arquivo;lesao;x;y;area_px;confianca\n";
    while (q.next()) {
        QStringList cells;
        for (int i = 0; i < 7; ++i) {
            cells << q.value(i).toString().replace('.', i == 6 ? ',' : '.');
        }
        csv += cells.join(';') + "\n";
    }
    return csv;
}

namespace {
// Cópia consistente do banco aberto. Copiar o arquivo .sqlite não basta: no modo WAL as gravações recentes
// ficam no arquivo -wal, e a cópia sairia vazia ou desatualizada.
bool vacuumInto(const QSqlDatabase& db, const QString& target) {
    QFile::remove(target);
    QString escaped = QDir::toNativeSeparators(target);
    escaped.replace("'", "''");
    QSqlQuery query(db);
    return query.exec("VACUUM INTO '" + escaped + "'");
}
}  // namespace

// Uma cópia por dia (odontolesao_AAAA-MM-DD.sqlite), as 7 mais recentes; as cópias feitas à mão ficam
void Database::dailyBackup() const {
    const QDir folder(QDir(dataDir_).filePath("backups"));
    const QString target = folder.filePath("odontolesao_" + QDate::currentDate().toString("yyyy-MM-dd") + ".sqlite");
    // refaz a de hoje se ela tiver saído vazia (cópias de versões anteriores, feitas sem o WAL)
    if (!QFileInfo::exists(target) || QFileInfo(target).size() < 16 * 1024) {
        vacuumInto(db(), target);
    }
    auto daily = folder.entryList({"odontolesao_\?\?\?\?-\?\?-\?\?.sqlite"}, QDir::Files, QDir::Name | QDir::Reversed);
    for (int i = 7; i < daily.size(); ++i) {
        QFile::remove(folder.filePath(daily[i]));
    }
}

QString Database::backupNow() const {
    const QString target = QDir(QDir(dataDir_).filePath("backups"))
                               .filePath("odontolesao_" + QDateTime::currentDateTime().toString("yyyy-MM-dd_HHmmss") + ".sqlite");
    return vacuumInto(db(), target) ? target : QString();
}

}  // namespace odonto
