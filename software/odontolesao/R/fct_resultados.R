# Cálculos da tela de Resultados (validação nas 20 panorâmicas e revisão do acervo).

#' Curvas FROC: maior sensibilidade alcançada para cada nº de falsos positivos por imagem
curvas_froc <- function(con) {
  f <- DBI::dbGetQuery(con, "SELECT * FROM validacao_froc")
  if (!nrow(f)) return(f)
  f$fp <- (f$previstas - f$corretas) / f$n_imagens
  f$sens <- f$achadas / f$lesoes
  saida <- do.call(rbind, lapply(split(f, f$modelo), function(d) {
    d <- d[order(d$fp, -d$sens), ]; d <- d[!duplicated(d$fp), ]
    d$sens <- cummax(d$sens); d
  }))
  saida$modelo <- factor(saida$modelo, intersect(names(CORES_MODELO), unique(saida$modelo)))
  saida
}

#' Métricas no limiar de operação por modelo, com IC 95% (Wilson; bootstrap por paciente para o Dice)
metricas_operacao <- function(con, B = 2000, semente = 1) {
  p <- DBI::dbGetQuery(con, "SELECT * FROM validacao_pacientes")
  if (!nrow(p)) return(data.frame())
  set.seed(semente)
  do.call(rbind, lapply(split(p, p$modelo), function(d) {
    sens <- wilson(sum(d$lesoes_achadas), sum(d$lesoes))
    prec <- wilson(sum(d$previstas_corretas), max(1, sum(d$previstas)))
    dice <- 2 * sum(d$inter) / sum(d$soma)
    bs <- replicate(B, { i <- sample(nrow(d), replace = TRUE); 2 * sum(d$inter[i]) / sum(d$soma[i]) })
    data.frame(modelo = d$modelo[1], limiar = d$limiar[1],
               metrica = c("Sensibilidade", "Precisão", "Dice"),
               p = c(sens$p, prec$p, dice), inf = c(sens$inf, prec$inf, stats::quantile(bs, .025)),
               sup = c(sens$sup, prec$sup, stats::quantile(bs, .975)),
               x = c(sens$x, prec$x, NA), n = c(sens$n, prec$n, NA),
               fp_img = sum(d$previstas - d$previstas_corretas) / nrow(d))
  }))
}

#' Mapa lesão x modelo (encontrada ou não), lesões ordenadas por arcada e diâmetro
mapa_lesoes <- function(con) {
  DBI::dbGetQuery(con, "SELECT r.id, r.paciente, r.lesao, r.diam_eq_px, r.arcada, f.modelo, f.achada
                        FROM referencia r JOIN achados_referencia f ON f.referencia_id = r.id")
}

#' Sensibilidade de um modelo com a referência corrigida pela auditoria do revisor
sensibilidade_auditada <- function(con, modelo, revisor, excluir = c("erro_marcacao", "nao_visivel")) {
  a <- DBI::dbGetQuery(con, "SELECT f.achada, a.resposta FROM achados_referencia f
                             LEFT JOIN auditoria_perdidas a ON a.referencia_id = f.referencia_id AND a.modelo = f.modelo AND a.revisor = ?
                             WHERE f.modelo = ?", params = list(revisor, modelo))
  if (!nrow(a)) return(NULL)
  perdidas <- a$achada == 0
  list(original = wilson(sum(a$achada), nrow(a)),
       corrigida = wilson(sum(a$achada), nrow(a) - sum(perdidas & a$resposta %in% excluir)),
       respondidas = sum(perdidas & !is.na(a$resposta)), perdidas = sum(perdidas),
       excluidas = sum(perdidas & a$resposta %in% excluir))
}

#' Concordância entre dois revisores nas detecções que ambos decidiram (confirmada x rejeitada)
kappa_revisores <- function(con, modelo_id) {
  d <- DBI::dbGetQuery(con, "SELECT r.revisor, x.deteccao_id, x.decisao FROM decisoes x JOIN revisoes r ON r.id = x.revisao_id
                             WHERE r.modelo_id = ? AND r.concluida_em IS NOT NULL AND x.decisao IN ('confirmada', 'rejeitada')",
                        params = list(modelo_id))
  revs <- unique(d$revisor)
  if (length(revs) < 2) return(NULL)
  pares <- utils::combn(revs, 2, simplify = FALSE)
  do.call(rbind, lapply(pares, function(pr) {
    a <- d[d$revisor == pr[1], c("deteccao_id", "decisao")]; b <- d[d$revisor == pr[2], c("deteccao_id", "decisao")]
    m <- merge(a, b, by = "deteccao_id")
    if (nrow(m) < 2) return(NULL)
    po <- mean(m$decisao.x == m$decisao.y)
    pa <- mean(m$decisao.x == "confirmada"); pb <- mean(m$decisao.y == "confirmada")
    pe <- pa * pb + (1 - pa) * (1 - pb)
    data.frame(revisor_1 = pr[1], revisor_2 = pr[2], deteccoes = nrow(m), concordancia = po,
               kappa = if (pe < 1) (po - pe) / (1 - pe) else NA_real_)
  }))
}

deteccoes_por_imagem <- function(con, modelo_id) {
  DBI::dbGetQuery(con, "SELECT i.id, (SELECT COUNT(*) FROM deteccoes d WHERE d.imagem_id = i.id AND d.modelo_id = ?) AS n
                        FROM imagens i", params = list(modelo_id))
}
