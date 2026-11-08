test_that("o backup diário contém os dados gravados no WAL", {
  pasta <- tempfile("dados_"); dir.create(file.path(pasta, "backups"), recursive = TRUE)
  con <- odontolesao:::abrir_banco(file.path(pasta, "odontolesao.sqlite"))
  on.exit(DBI::dbDisconnect(con), add = TRUE)
  DBI::dbExecute(con, "INSERT INTO imagens (id, arquivo, caminho, largura, altura, amostra, importada_em)
                       VALUES ('A001', 'x.jpg', 'C:/x.jpg', 10, 10, 0, '2026-11-08')")
  # a gravação ainda está no arquivo -wal: uma cópia do .sqlite sairia sem ela
  destino <- odontolesao:::fazer_backup(con, file.path(pasta, "backups"))
  copia <- DBI::dbConnect(RSQLite::SQLite(), destino); on.exit(DBI::dbDisconnect(copia), add = TRUE)
  expect_equal(DBI::dbGetQuery(copia, "SELECT COUNT(*) AS n FROM imagens")$n, 1)
})

test_that("o backup de hoje vazio é refeito e só as 7 diárias mais recentes ficam", {
  pasta <- tempfile("dados_"); bk <- file.path(pasta, "backups"); dir.create(bk, recursive = TRUE)
  con <- odontolesao:::abrir_banco(file.path(pasta, "odontolesao.sqlite"))
  on.exit(DBI::dbDisconnect(con), add = TRUE)
  hoje <- file.path(bk, paste0("odontolesao_", format(Sys.Date()), ".sqlite"))
  writeBin(raw(4096), hoje)                                   # como os backups vazios de versões anteriores
  for (d in 1:9) file.create(file.path(bk, sprintf("odontolesao_2020-01-%02d.sqlite", d)))
  file.create(file.path(bk, "odontolesao_2020-01-01_120000.sqlite"))   # cópia feita à mão
  odontolesao:::fazer_backup(con, bk)
  expect_gt(file.size(hoje), 16 * 1024)
  diarias <- list.files(bk, "^odontolesao_[0-9]{4}-[0-9]{2}-[0-9]{2}[.]sqlite$")
  expect_length(diarias, 7)
  expect_true(file.exists(file.path(bk, "odontolesao_2020-01-01_120000.sqlite")))
})
