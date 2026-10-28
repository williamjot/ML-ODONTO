banco_teste <- function() {
  con <- abrir_banco(tempfile(fileext = ".sqlite"))
  DBI::dbExecute(con, "INSERT INTO modelos (nome, tipo, limiar, descricao, ativo, criado_em) VALUES ('m', 'unet_lesoes', 0.5, 'teste', 1, 'agora')")
  for (k in 1:5) DBI::dbExecute(con, "INSERT INTO imagens (id, arquivo, caminho, largura, altura, amostra, importada_em) VALUES (?, ?, '', 100, 50, ?, 'agora')",
                                params = list(sprintf("A%03d", k), paste0("img", k, ".jpg"), as.integer(k <= 2)))
  dets <- list(A001 = 2, A002 = 1, A003 = 5, A004 = 0, A005 = 1)
  for (id in names(dets)) for (i in seq_len(dets[[id]]))
    DBI::dbExecute(con, "INSERT INTO deteccoes (imagem_id, modelo_id, indice, contorno, cx, cy, area, confianca) VALUES (?, 1, ?, '[[0,0],[1,0],[1,1]]', 1, 1, 1, 0.9)",
                   params = list(id, i))
  con
}

test_that("Wilson reproduz o valor de referência", {
  w <- wilson(29, 30)
  expect_equal(round(100 * c(w$p, w$inf, w$sup)), c(97, 83, 99))
  expect_true(is.na(wilson(0, 0)$p))
})

test_that("grupos da lista de revisão: amostra, 4+, nenhuma, demais", {
  con <- banco_teste(); on.exit(DBI::dbDisconnect(con))
  l <- lista_revisao(con, "r", 1)
  expect_equal(l$id, c("A001", "A002", "A003", "A004", "A005"))
  expect_equal(l$grupo, c(1L, 1L, 2L, 3L, 4L))
  expect_true(all(l$situacao == "pendente"))
})

test_that("decisões, lesões adicionadas e estatísticas", {
  con <- banco_teste(); on.exit(DBI::dbDisconnect(con))
  r1 <- obter_revisao(con, "A001", 1, "r")
  gravar_decisao(con, r1, "A001", 1, 1, "confirmada")
  gravar_decisao(con, r1, "A001", 1, 2, "rejeitada")
  adicionar_lesao(con, r1, matrix(c(1, 2, 3, 4, 5, 6), ncol = 2))
  gravar_campos_revisao(con, r1, concluida = TRUE)
  r3 <- obter_revisao(con, "A003", 1, "r")
  for (i in 1:5) gravar_decisao(con, r3, "A003", 1, i, "confirmada")
  gravar_decisao(con, r3, "A003", 1, 5, NULL)                    # desfaz a última
  gravar_campos_revisao(con, r3, concluida = TRUE)
  e <- estatisticas_revisao(con, 1, "r")
  expect_equal(c(e$todas$confirmadas, e$todas$rejeitadas, e$todas$adicionadas, e$todas$imagens), c(5, 1, 1, 2))
  expect_equal(c(e$amostra$confirmadas, e$amostra$rejeitadas, e$amostra$imagens), c(1, 1, 1))
  expect_equal(e$todas$precisao$p, 5 / 6)
  expect_equal(lista_revisao(con, "r", 1)$situacao[1], "concluida")
  expect_equal(lista_revisao(con, "outro", 1)$situacao[1], "pendente")   # revisões são por revisor
})

test_that("componentes da máscara seguem a ordem do OpenCV", {
  m <- matrix(0, 40, 30)          # EBImage: [x, y]
  m[30:35, 3:6] <- 1              # mais acima (y menor), à direita -> 1ª no OpenCV
  m[5:9, 10:14] <- 1              # mais abaixo, à esquerda -> 2ª
  f <- tempfile(fileext = ".png"); EBImage::writeImage(EBImage::Image(m), f)
  cps <- componentes_mascara(f, passo = 1)
  expect_length(cps, 2)
  expect_equal(c(cps[[1]]$cx, cps[[1]]$cy), c(31.5, 3.5))
  expect_equal(attr(cps, "dims"), c(40, 30))
})
