// Testes da lógica do ML ODONTO nativo: banco (o mesmo esquema do app R), revisão, estatísticas, métricas,
// backup e contornos. Os cenários e os valores esperados repetem os de software/odontolesao/tests/testthat,
// para que as duas versões do programa sejam conferidas com os mesmos números.

#include "Contours.h"
#include "Database.h"
#include "Metrics.h"

#include <QCoreApplication>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FALHOU: " << message << '\n';
    }
}

QSqlQuery exec(odonto::Database& db, const QString& sql, const QVariantList& params = {}) {
    QSqlQuery q(db.db());
    q.prepare(sql);
    for (const auto& p : params) q.addBindValue(p);
    if (!q.exec()) std::cerr << "SQL: " << sql.toStdString() << '\n';
    return q;
}

int scalar(odonto::Database& db, const QString& sql) {
    auto q = exec(db, sql);
    return q.next() ? q.value(0).toInt() : -1;
}

// Mesmo banco de teste de test-nucleo.R: 5 imagens (2 na amostra) com 2, 1, 5, 0 e 1 detecções
void fixture(odonto::Database& db) {
    exec(db, "DELETE FROM modelos");
    exec(db, "INSERT INTO modelos (id, nome, tipo, limiar, descricao, ativo, criado_em) VALUES (1, 'm', 'unet_lesoes', 0.5, 'teste', 1, 'agora')");
    const int dets[] = {2, 1, 5, 0, 1};
    for (int k = 1; k <= 5; ++k) {
        const QString id = QStringLiteral("A%1").arg(k, 3, 10, QChar('0'));
        exec(db, "INSERT INTO imagens (id, arquivo, caminho, largura, altura, amostra, importada_em) VALUES (?, ?, '', 100, 50, ?, 'agora')",
             {id, QStringLiteral("img%1.jpg").arg(k), k <= 2 ? 1 : 0});
        for (int i = 1; i <= dets[k - 1]; ++i) {
            exec(db, "INSERT INTO deteccoes (imagem_id, modelo_id, indice, contorno, cx, cy, area, confianca) "
                     "VALUES (?, 1, ?, '[[0,0],[1,0],[1,1]]', 1, 1, 1, 0.9)", {id, i});
        }
    }
}

void test_open_and_seed(const QString& dir) {
    odonto::Database db;
    QString error;
    check(db.open(dir, &error), "abre e migra um banco novo");
    check(scalar(db, "SELECT MAX(versao) FROM versao_esquema") >= 3, "aplica as migrações do pacote R");
    check(scalar(db, "SELECT COUNT(*) FROM modelos") == 3, "registra os modelos de modelos.yml");
    const auto model = db.activeModel();
    check(model && model->name == "unet_lesoes_v2" && model->width == 2000, "U-Net v2 é o modelo ativo");
    QSqlDatabase::database("odonto").close();
    QSqlDatabase::removeDatabase("odonto");
}

void test_review(odonto::Database& db) {
    fixture(db);
    auto list = db.reviewList("r", 1);
    QStringList ids;
    QList<int> groups;
    for (const auto& i : list) {
        ids << i.id;
        groups << i.group;
    }
    check(ids == QStringList({"A001", "A002", "A003", "A004", "A005"}), "ordem da lista de revisão");
    check(groups == QList<int>({1, 1, 2, 3, 4}), "grupos: amostra, 4+, nenhuma, demais");

    const int r1 = db.obtainReview("A001", 1, "r");
    check(db.obtainReview("A001", 1, "r") == r1, "obter a revisão de novo devolve a mesma");
    db.setDecision(r1, "A001", 1, 1, "confirmada");
    db.setDecision(r1, "A001", 1, 2, "rejeitada");
    db.addLesion(r1, QPolygonF({{1, 4}, {2, 5}, {3, 6}}));
    db.setCompleted(r1, true);
    const int r3 = db.obtainReview("A003", 1, "r");
    for (int i = 1; i <= 5; ++i) db.setDecision(r3, "A003", 1, i, "confirmada");
    db.setDecision(r3, "A003", 1, 5, "");  // desfaz a última
    db.setCompleted(r3, true);

    const auto e = db.reviewStats(1, "r");
    check(e.all.confirmed == 5 && e.all.rejected == 1 && e.all.added == 1 && e.all.images == 2, "estatísticas de todas as revisadas");
    check(e.sample.confirmed == 1 && e.sample.rejected == 1 && e.sample.images == 1, "estatísticas da amostra");
    check(std::abs(e.all.precision.p - 5.0 / 6.0) < 1e-12, "precisão = 5/6");
    check(db.reviewList("r", 1).front().status == "concluida", "imagem concluída");
    check(db.reviewList("outro", 1).front().status == "pendente", "revisões são por revisor");

    const auto data = db.imageData("A001", 1, "r");
    check(data.decisions.value(1) == "confirmada" && data.decisions.value(2) == "rejeitada", "decisões gravadas");
    check(data.added.size() == 1 && data.added[0].contour.size() == 3, "lesão adicionada gravada");
    db.removeLesion(r1, data.added[0].id);
    check(db.imageData("A001", 1, "r").added.isEmpty(), "lesão adicionada removida");

    db.setObservations(r1, "lesões 1 e 2 são a mesma");
    db.setInadequate(r1, true);
    const auto again = db.imageData("A001", 1, "r");
    check(again.observations == "lesões 1 e 2 são a mesma" && again.inadequate, "observação e imagem inadequada");
    check(db.reviewStats(1, "r").inadequate == 1, "inadequadas saem das estatísticas");

    const auto counts = db.counts(1);
    check(counts.images == 5 && counts.sample == 2 && counts.detections == 9 && counts.withDetection == 4 &&
              counts.completed == 2 && counts.sampleCompleted == 1 && counts.reviewers == 1,
          "contagens gerais");
}

void test_kappa(odonto::Database& db) {
    // dois revisores concordam em 3 de 4 detecções de A003
    const int a = db.obtainReview("A003", 1, "ana");
    const int b = db.obtainReview("A003", 1, "bia");
    const char* da[] = {"confirmada", "confirmada", "rejeitada", "rejeitada"};
    const char* dbv[] = {"confirmada", "rejeitada", "rejeitada", "rejeitada"};
    for (int i = 0; i < 4; ++i) {
        db.setDecision(a, "A003", 1, i + 1, da[i]);
        db.setDecision(b, "A003", 1, i + 1, dbv[i]);
    }
    db.setCompleted(a, true);
    db.setCompleted(b, true);
    const auto k = db.kappa(1);
    bool found = false;
    for (const auto& row : k) {
        if (row.first == "ana" && row.second == "bia") {
            found = true;
            // po = 3/4; pa = 1/2, pb = 1/4; pe = 1/8 + 3/8 = 1/2; kappa = (0,75 - 0,5) / 0,5 = 0,5
            check(row.detections == 4 && std::abs(row.agreement - 0.75) < 1e-12 && std::abs(row.kappa - 0.5) < 1e-12,
                  "kappa de Cohen");
        }
    }
    check(found, "par de revisores no kappa");
}

void test_backup_includes_wal(odonto::Database& db) {
    // a gravação ainda está no arquivo -wal: a cópia precisa trazê-la
    exec(db, "INSERT INTO config (chave, valor) VALUES ('teste_backup', 'sim')");
    const QString file = db.backupNow();
    check(!file.isEmpty(), "backup feito");
    {
        auto copy = QSqlDatabase::addDatabase("QSQLITE", "copia");
        copy.setDatabaseName(file);
        copy.open();
        QSqlQuery q(copy);
        q.exec("SELECT valor FROM config WHERE chave = 'teste_backup'");
        check(q.next() && q.value(0).toString() == "sim", "o backup contém o que estava no WAL");
        copy.close();
    }
    QSqlDatabase::removeDatabase("copia");
}

void test_application(odonto::Database& db) {
    const QString id = db.saveApplication(std::nullopt, "C:/radiografias/nova.jpg", 2800, 1400, 1,
                                           {{1, QPolygonF({{0, 0}, {4, 0}, {4, 4}}), {2, 2}, 16, 0.97}}, 0.5, 12.3);
    check(id == "A006", "nova radiografia recebe o próximo código");
    check(db.imageIdByPath("C:/radiografias/nova.jpg") == std::optional<QString>("A006"), "encontra a radiografia pelo caminho");
    db.saveApplication(id, "C:/radiografias/nova.jpg", 2800, 1400, 1, {}, 0.5, 11.0);
    check(db.imageData("A006", 1, "r").detections.isEmpty(), "reaplicar substitui as detecções");
    check(db.applications(1).size() == 2, "aplicações registradas");
}

void test_metrics() {
    const auto w = odonto::wilson(29, 30);
    check(qRound(100 * w.p) == 97 && qRound(100 * w.lower) == 83 && qRound(100 * w.upper) == 99, "Wilson (valor de referência do R)");
    check(!odonto::wilson(0, 0).valid(), "Wilson sem dados");
    // round() do R arredonda metade para o par
    check(odonto::percent(0.125) == "12%" && odonto::percent(0.135) == "14%", "porcentagem arredonda metade para o par");
    check(odonto::formatProportion(w) == "97% (IC 95% 83–99%; 29/30)", "texto da proporção");

    // FROC: mantém a maior sensibilidade por FP e acumula o máximo
    QVector<odonto::FrocRow> rows{{"U-Net v2", 0.9, 10, 5, 5, 5, 2}, {"U-Net v2", 0.5, 10, 7, 9, 7, 2},
                                  {"U-Net v2", 0.4, 10, 6, 9, 7, 2}, {"U-Net v2", 0.2, 10, 6, 12, 8, 2}};
    const auto curves = odonto::frocCurves(rows);
    check(curves.size() == 1 && curves[0].second.size() == 3, "pontos da curva FROC");
    check(curves[0].second[1] == QPointF(1.0, 0.7) && curves[0].second[2] == QPointF(2.0, 0.7), "FROC acumula o máximo");

    QVector<odonto::PatientRow> patients;
    for (int p = 0; p < 4; ++p) patients.push_back({"U-Net v2", 0.5, QString::number(p), 10, 40, 2, 1, 2, 1});
    const auto op = odonto::operatingMetrics(patients, 200);
    check(op.size() == 1 && op[0].sensitivity.x == 4 && op[0].sensitivity.n == 8 && std::abs(op[0].dice - 0.5) < 1e-12 &&
              std::abs(op[0].fpPerImage - 1.0) < 1e-12,
          "métricas no limiar de operação");
    check(std::abs(op[0].diceLower - 0.5) < 1e-12 && std::abs(op[0].diceUpper - 0.5) < 1e-12, "bootstrap de pacientes iguais");
}

void test_contours() {
    // dois componentes: o de cima à direita vem primeiro (varredura por linhas, como o OpenCV)
    ml_odonto::Mask mask(30, 40, 0);
    for (std::size_t y = 2; y <= 5; ++y)
        for (std::size_t x = 29; x <= 34; ++x) mask(y, x) = 1;
    for (std::size_t y = 9; y <= 13; ++y)
        for (std::size_t x = 4; x <= 8; ++x) mask(y, x) = 1;
    std::vector<ml_odonto::Detection> dets(2);
    dets[0].peak_y = 3; dets[0].peak_x = 30; dets[0].confidence = 0.8F;
    dets[1].peak_y = 11; dets[1].peak_x = 6; dets[1].confidence = 0.3F;  // abaixo do limiar
    const auto lesions = odonto::lesionsFromMask(mask, dets, 0.5F, 2.0);
    check(lesions.size() == 2, "duas lesões na máscara");
    check(lesions[0].center == QPointF(31.5 * 2, 3.5 * 2), "centro da 1ª lesão (como componentes_mascara)");
    check(lesions[0].area == 24 * 4 && std::abs(lesions[0].confidence - 0.8) < 1e-6, "área e confiança da 1ª");
    check(lesions[1].confidence == 0, "pico abaixo do limiar não conta");

    int count = 0;
    ml_odonto::Mask square(5, 5, 0);
    for (std::size_t y = 1; y <= 3; ++y)
        for (std::size_t x = 1; x <= 3; ++x) square(y, x) = 1;
    const auto squareLabels = ml_odonto::connected_components8(square, &count);  // count antes de usá-lo
    const auto contours = odonto::traceContours(squareLabels, count);
    check(count == 1 && contours[0].size() == 8, "contorno de um quadrado 3x3 tem 8 pontos");
    ml_odonto::Mask dot(3, 3, 0);
    dot(1, 1) = 1;
    const auto dotLabels = ml_odonto::connected_components8(dot, &count);
    check(odonto::traceContours(dotLabels, count)[0].size() == 1, "pixel isolado");

    const QPolygonF polygon({{1.2, 2.7}, {3, 4}});
    check(odonto::Database::parseContour(odonto::Database::contourJson(polygon)) == QPolygonF({{1, 3}, {3, 4}}),
          "contorno em JSON (pixels inteiros, como o R)");
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    test_open_and_seed(dir.filePath("novo"));
    {
        odonto::Database db;
        db.open(dir.filePath("dados"));
        test_review(db);
        test_kappa(db);
        test_backup_includes_wal(db);
        test_application(db);
    }
    QSqlDatabase::removeDatabase("odonto");
    test_metrics();
    test_contours();
    if (failures) {
        std::cerr << failures << " verificação(ões) falharam\n";
        return 1;
    }
    std::cout << "todos os testes do programa nativo passaram\n";
    return 0;
}
