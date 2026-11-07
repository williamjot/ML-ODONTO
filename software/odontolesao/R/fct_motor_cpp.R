# Motor de inferência em C++ (ONNX Runtime): a mesma U-Net e o mesmo pré-processamento, validados
# contra o Python (ver cpp/README.md). Quando o executável e o modelo .onnx estão instalados, a tela
# Aplicar usa este motor; a detecção das lesões continua em R (fct_deteccao.R).

#' Caminho do executável ml-odonto-cli ("" se não estiver instalado)
caminho_cli <- function() {
  cands <- c(getOption("mlodonto.cli", ""), Sys.getenv("ML_ODONTO_CLI"),
             system.file("bin", "ml-odonto-cli.exe", package = "odontolesao"),
             system.file("bin", "ml-odonto-cli", package = "odontolesao"))
  cands <- cands[nzchar(cands) & file.exists(cands)]
  if (length(cands)) normalizePath(cands[1], winslash = "/") else ""
}

#' Modelo ONNX correspondente aos pesos de um modelo ("" se não houver)
caminho_onnx <- function(arquivo_pesos) {
  if (length(arquivo_pesos) != 1 || is.na(arquivo_pesos)) return("")
  f <- caminho_pesos(sub("[.]safetensors$", ".onnx", arquivo_pesos))
  if (nzchar(f) && file.exists(f)) f else ""
}

motor_cpp_disponivel <- function(arquivo_pesos) nzchar(caminho_cli()) && nzchar(caminho_onnx(arquivo_pesos))

#' Motor que será usado: "cpp" quando instalado (e não desligado em options(mlodonto.motor = "r")), senão "r"
motor_inferencia <- function(arquivo_pesos) {
  if (!identical(getOption("mlodonto.motor", "auto"), "r") && motor_cpp_disponivel(arquivo_pesos)) "cpp" else "r"
}

#' Lê um mapa de probabilidade PFM (cinza, float32) gravado pelo motor C++
ler_pfm <- function(arq) {
  con <- file(arq, "rb"); on.exit(close(con))
  linha <- function() {
    b <- raw()
    repeat { x <- readBin(con, "raw", 1); if (!length(x) || x == as.raw(10)) break; b <- c(b, x) }
    trimws(rawToChar(b))
  }
  if (linha() != "Pf") stop("arquivo PFM inválido: ", arq)
  dims <- as.integer(strsplit(linha(), " +")[[1]])
  escala <- as.numeric(linha())
  v <- readBin(con, "double", n = dims[1] * dims[2], size = 4, endian = if (escala < 0) "little" else "big")
  m <- matrix(v, nrow = dims[2], ncol = dims[1], byrow = TRUE)
  m[rev(seq_len(nrow(m))), , drop = FALSE] * abs(escala)          # o PFM guarda as linhas de baixo para cima
}

#' Entradas para o executável: TIFF vira PNG e nomes repetidos ganham cópia com nome único
preparar_entradas_cpp <- function(arquivos, pasta) {
  base <- tools::file_path_sans_ext(basename(arquivos))
  vapply(seq_along(arquivos), function(i) {
    tiff <- grepl("[.]tiff?$", arquivos[i], ignore.case = TRUE)
    if (!tiff && sum(base == base[i]) == 1) return(arquivos[i])
    destino <- file.path(pasta, sprintf("entrada_%04d.%s", i, if (tiff) "png" else tools::file_ext(arquivos[i])))
    if (tiff) magick::image_write(magick::image_convert(magick::image_read(arquivos[i]), colorspace = "gray"), destino, format = "png")
    else file.copy(arquivos[i], destino, overwrite = TRUE)
    destino
  }, "")
}

#' Roda o motor C++ num lote, carregando o modelo uma vez só
#'
#' Chama `a_cada(i, resultado)` assim que a radiografia i termina; `resultado` traz a probabilidade
#' (escala de trabalho), o tamanho original e o tempo, ou `erro` com a mensagem.
executar_cpp <- function(arquivos, arquivo_pesos, largura_modelo = 2000, a_cada = function(i, r) NULL) {
  pasta <- tempfile("mlodonto_cpp_"); dir.create(pasta)
  on.exit(unlink(pasta, recursive = TRUE), add = TRUE)
  entradas <- preparar_entradas_cpp(arquivos, pasta)
  p <- processx::process$new(caminho_cli(), c("run", caminho_onnx(arquivo_pesos), pasta, entradas,
                                              "--model-width", as.character(largura_modelo)),
                             stdout = "|", stderr = "|")
  i <- 0L
  repeat {
    p$poll_io(1000)
    for (l in p$read_output_lines()) {
      if (!grepl("^(ok|error) ", l)) next
      i <- i + 1L
      stem <- tools::file_path_sans_ext(basename(entradas[i]))
      r <- if (startsWith(l, "ok ")) {
        meta <- jsonlite::read_json(file.path(pasta, paste0(stem, "_deteccoes.json")))
        list(prob = ler_pfm(file.path(pasta, paste0(stem, "_probabilidade.pfm"))),
             altura = meta$original_height, largura = meta$original_width, segundos = meta$seconds)
      } else list(erro = sub("^error [^:]*: ", "", l))
      a_cada(i, r)
    }
    if (!p$is_alive() && !p$is_incomplete_output()) break
  }
  if (i < length(arquivos)) stop("o motor C++ parou antes do fim: ", trimws(p$read_all_error()))
  invisible(i)
}
