# Acesso ao banco SQLite. Todo o SQL do programa fica neste arquivo.

agora <- function() format(Sys.time(), "%Y-%m-%d %H:%M:%S")

#' Abre (e cria, se preciso) o banco do OdontoLesão
#' @param caminho arquivo .sqlite
#' @export
abrir_banco <- function(caminho) {
  dir.create(dirname(caminho), showWarnings = FALSE, recursive = TRUE)
  con <- DBI::dbConnect(RSQLite::SQLite(), caminho)
  DBI::dbExecute(con, "PRAGMA foreign_keys = ON")
  DBI::dbExecute(con, "PRAGMA journal_mode = WAL")
  migrar(con)
  con
}

migrar <- function(con) {
  DBI::dbExecute(con, "CREATE TABLE IF NOT EXISTS versao_esquema (versao INTEGER NOT NULL)")
  atual <- DBI::dbGetQuery(con, "SELECT MAX(versao) AS v FROM versao_esquema")$v
  atual <- if (is.na(atual)) 0L else as.integer(atual)
  arqs <- sort(list.files(system.file("sql", package = "odontolesao"), "^\\d+_.*\\.sql$", full.names = TRUE))
  for (a in arqs) {
    v <- as.integer(sub("_.*", "", basename(a)))
    if (v <= atual) next
    texto <- paste(readLines(a, encoding = "UTF-8"), collapse = "\n")
    texto <- gsub("--[^\n]*", "", texto)
    comandos <- trimws(strsplit(texto, ";\\s*(\n|$)")[[1]])
    DBI::dbWithTransaction(con, {
      for (cmd in comandos[nzchar(comandos)]) DBI::dbExecute(con, cmd)
      DBI::dbExecute(con, "INSERT INTO versao_esquema (versao) VALUES (?)", params = list(v))
    })
  }
  invisible(con)
}

# ---------------------------------------------------------------- configuração

ler_config <- function(con, chave, padrao = NULL) {
  v <- DBI::dbGetQuery(con, "SELECT valor FROM config WHERE chave = ?", params = list(chave))$valor
  if (length(v)) v else padrao
}

gravar_config <- function(con, chave, valor) {
  DBI::dbExecute(con, "INSERT INTO config (chave, valor) VALUES (?, ?)
                       ON CONFLICT(chave) DO UPDATE SET valor = excluded.valor",
                 params = list(chave, as.character(valor)))
}

registrar_evento <- function(con, revisor, tipo, detalhe = "") {
  DBI::dbExecute(con, "INSERT INTO eventos (em, revisor, tipo, detalhe) VALUES (?, ?, ?, ?)",
                 params = list(agora(), revisor %||% "", tipo, detalhe))
}

`%||%` <- function(a, b) if (is.null(a) || length(a) == 0 || identical(a, "")) b else a

# ---------------------------------------------------------------- modelos

modelo_ativo <- function(con) {
  DBI::dbGetQuery(con, "SELECT * FROM modelos WHERE ativo = 1 ORDER BY id DESC LIMIT 1")
}

# ---------------------------------------------------------------- revisão

#' Lista de imagens para a revisão, com grupo, número de detecções e situação
lista_revisao <- function(con, revisor, modelo_id) {
  d <- DBI::dbGetQuery(con, "
    SELECT i.id, i.arquivo, i.amostra,
           (SELECT COUNT(*) FROM deteccoes d WHERE d.imagem_id = i.id AND d.modelo_id = ?) AS n,
           r.concluida_em, r.id AS revisao_id
    FROM imagens i
    LEFT JOIN revisoes r ON r.imagem_id = i.id AND r.modelo_id = ? AND r.revisor = ?
    ORDER BY i.id", params = list(modelo_id, modelo_id, revisor))
  d$grupo <- ifelse(d$amostra == 1, 1L, ifelse(d$n >= 4, 2L, ifelse(d$n == 0, 3L, 4L)))
  d$situacao <- ifelse(!is.na(d$concluida_em), "concluida", ifelse(!is.na(d$revisao_id), "parcial", "pendente"))
  d[order(d$grupo, -d$n, d$id), ]
}

NOMES_GRUPO <- c("1º Amostra aleatória", "4 ou mais detecções", "Nenhuma detecção", "Demais imagens")

#' Garante que existe uma revisão (imagem, modelo, revisor) e devolve seu id
obter_revisao <- function(con, imagem_id, modelo_id, revisor) {
  id <- DBI::dbGetQuery(con, "SELECT id FROM revisoes WHERE imagem_id = ? AND modelo_id = ? AND revisor = ?",
                        params = list(imagem_id, modelo_id, revisor))$id
  if (length(id)) return(id)
  DBI::dbExecute(con, "INSERT INTO revisoes (imagem_id, modelo_id, revisor, iniciada_em, atualizada_em)
                       VALUES (?, ?, ?, ?, ?)", params = list(imagem_id, modelo_id, revisor, agora(), agora()))
  DBI::dbGetQuery(con, "SELECT last_insert_rowid() AS id")$id
}

tocar_revisao <- function(con, revisao_id) {
  DBI::dbExecute(con, "UPDATE revisoes SET atualizada_em = ? WHERE id = ?", params = list(agora(), revisao_id))
}

#' Tudo o que o visor precisa para abrir uma imagem
dados_imagem <- function(con, imagem_id, modelo_id, revisor) {
  img <- DBI::dbGetQuery(con, "SELECT * FROM imagens WHERE id = ?", params = list(imagem_id))
  det <- DBI::dbGetQuery(con, "SELECT id, indice, contorno, cx, cy, confianca FROM deteccoes
                               WHERE imagem_id = ? AND modelo_id = ? ORDER BY indice",
                         params = list(imagem_id, modelo_id))
  rev <- DBI::dbGetQuery(con, "SELECT * FROM revisoes WHERE imagem_id = ? AND modelo_id = ? AND revisor = ?",
                         params = list(imagem_id, modelo_id, revisor))
  dec <- if (nrow(rev)) DBI::dbGetQuery(con, "SELECT d.indice, x.decisao FROM decisoes x
                                              JOIN deteccoes d ON d.id = x.deteccao_id WHERE x.revisao_id = ?",
                                        params = list(rev$id)) else data.frame(indice = integer(), decisao = character())
  add <- if (nrow(rev)) DBI::dbGetQuery(con, "SELECT id, contorno FROM lesoes_adicionadas WHERE revisao_id = ? ORDER BY id",
                                        params = list(rev$id)) else data.frame(id = integer(), contorno = character())
  list(imagem = img, deteccoes = det, revisao = rev, decisoes = dec, adicionadas = add)
}

gravar_decisao <- function(con, revisao_id, imagem_id, modelo_id, indice, decisao) {
  det_id <- DBI::dbGetQuery(con, "SELECT id FROM deteccoes WHERE imagem_id = ? AND modelo_id = ? AND indice = ?",
                            params = list(imagem_id, modelo_id, indice))$id
  if (!length(det_id)) return(invisible(FALSE))
  DBI::dbWithTransaction(con, {
    if (is.null(decisao) || !nzchar(decisao)) {
      DBI::dbExecute(con, "DELETE FROM decisoes WHERE revisao_id = ? AND deteccao_id = ?", params = list(revisao_id, det_id))
    } else {
      DBI::dbExecute(con, "INSERT INTO decisoes (revisao_id, deteccao_id, decisao, em) VALUES (?, ?, ?, ?)
                           ON CONFLICT(revisao_id, deteccao_id) DO UPDATE SET decisao = excluded.decisao, em = excluded.em",
                     params = list(revisao_id, det_id, decisao, agora()))
    }
    tocar_revisao(con, revisao_id)
  })
  invisible(TRUE)
}

adicionar_lesao <- function(con, revisao_id, pontos) {
  DBI::dbExecute(con, "INSERT INTO lesoes_adicionadas (revisao_id, contorno, em) VALUES (?, ?, ?)",
                 params = list(revisao_id, jsonlite::toJSON(pontos), agora()))
  tocar_revisao(con, revisao_id)
  DBI::dbGetQuery(con, "SELECT last_insert_rowid() AS id")$id
}

remover_lesao <- function(con, revisao_id, lesao_id) {
  DBI::dbExecute(con, "DELETE FROM lesoes_adicionadas WHERE id = ? AND revisao_id = ?", params = list(lesao_id, revisao_id))
  tocar_revisao(con, revisao_id)
}

gravar_campos_revisao <- function(con, revisao_id, inadequada = NULL, observacoes = NULL, concluida = NULL) {
  if (!is.null(inadequada)) DBI::dbExecute(con, "UPDATE revisoes SET inadequada = ? WHERE id = ?",
                                           params = list(as.integer(inadequada), revisao_id))
  if (!is.null(observacoes)) DBI::dbExecute(con, "UPDATE revisoes SET observacoes = ? WHERE id = ?",
                                            params = list(observacoes, revisao_id))
  if (!is.null(concluida)) DBI::dbExecute(con, "UPDATE revisoes SET concluida_em = ? WHERE id = ?",
                                          params = list(if (isTRUE(concluida)) agora() else NA, revisao_id))
  tocar_revisao(con, revisao_id)
}

# ---------------------------------------------------------------- estatísticas

#' Contagens das revisões concluídas (todas e só a amostra aleatória)
#' @export
estatisticas_revisao <- function(con, modelo_id, revisor = NULL) {
  filtro <- if (is.null(revisor)) "" else "AND r.revisor = ?"
  par <- if (is.null(revisor)) list(modelo_id) else list(modelo_id, revisor)
  por_img <- DBI::dbGetQuery(con, paste0("
    SELECT r.id, r.imagem_id, i.amostra, r.inadequada,
      (SELECT COUNT(*) FROM decisoes x WHERE x.revisao_id = r.id AND x.decisao = 'confirmada') AS confirmadas,
      (SELECT COUNT(*) FROM decisoes x WHERE x.revisao_id = r.id AND x.decisao = 'rejeitada') AS rejeitadas,
      (SELECT COUNT(*) FROM decisoes x WHERE x.revisao_id = r.id AND x.decisao = 'duvida') AS duvidas,
      (SELECT COUNT(*) FROM lesoes_adicionadas a WHERE a.revisao_id = r.id) AS adicionadas,
      (SELECT COUNT(*) FROM deteccoes d WHERE d.imagem_id = r.imagem_id AND d.modelo_id = r.modelo_id) AS deteccoes
    FROM revisoes r JOIN imagens i ON i.id = r.imagem_id
    WHERE r.concluida_em IS NOT NULL AND r.modelo_id = ? ", filtro), params = par)
  resumir <- function(d) {
    d <- d[d$inadequada == 0, ]
    C <- sum(d$confirmadas); R <- sum(d$rejeitadas); D <- sum(d$duvidas); A <- sum(d$adicionadas)
    list(imagens = nrow(d), confirmadas = C, rejeitadas = R, duvidas = D, adicionadas = A,
         precisao = wilson(C, C + R), precisao_pessimista = wilson(C, C + R + D),
         sensibilidade = wilson(C, C + A), fp_por_imagem = if (nrow(d)) R / nrow(d) else NA_real_)
  }
  list(todas = resumir(por_img), amostra = resumir(por_img[por_img$amostra == 1, ]),
       inadequadas = sum(por_img$inadequada), por_imagem = por_img)
}

progresso_diario <- function(con, modelo_id) {
  DBI::dbGetQuery(con, "SELECT substr(concluida_em, 1, 10) AS dia, COUNT(*) AS n FROM revisoes
                        WHERE concluida_em IS NOT NULL AND modelo_id = ? GROUP BY dia ORDER BY dia",
                  params = list(modelo_id))
}

contagens_gerais <- function(con, modelo_id) {
  q <- function(sql, ...) {
    par <- list(...)
    (if (length(par)) DBI::dbGetQuery(con, sql, params = par) else DBI::dbGetQuery(con, sql))[[1]]
  }
  list(imagens = q("SELECT COUNT(*) FROM imagens"),
       amostra = q("SELECT COUNT(*) FROM imagens WHERE amostra = 1"),
       deteccoes = q("SELECT COUNT(*) FROM deteccoes WHERE modelo_id = ?", modelo_id),
       com_deteccao = q("SELECT COUNT(DISTINCT imagem_id) FROM deteccoes WHERE modelo_id = ?", modelo_id),
       concluidas = q("SELECT COUNT(DISTINCT imagem_id) FROM revisoes WHERE concluida_em IS NOT NULL AND modelo_id = ?", modelo_id),
       amostra_concluidas = q("SELECT COUNT(DISTINCT r.imagem_id) FROM revisoes r JOIN imagens i ON i.id = r.imagem_id
                               WHERE r.concluida_em IS NOT NULL AND i.amostra = 1 AND r.modelo_id = ?", modelo_id),
       revisores = q("SELECT COUNT(DISTINCT revisor) FROM revisoes"))
}
