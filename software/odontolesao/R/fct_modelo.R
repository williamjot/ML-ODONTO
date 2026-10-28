# U-Net ResNet34 em R torch (mesma arquitetura e nomes de parâmetros de scripts/unet.py),
# pré-processamento equivalente ao do OpenCV e previsão.

bloco_unet <- function(entrada, saida) {
  torch::nn_sequential(
    torch::nn_conv2d(entrada, saida, 3, padding = 1, bias = FALSE), torch::nn_batch_norm2d(saida), torch::nn_relu(inplace = TRUE),
    torch::nn_conv2d(saida, saida, 3, padding = 1, bias = FALSE), torch::nn_batch_norm2d(saida), torch::nn_relu(inplace = TRUE))
}

unet_resnet34 <- torch::nn_module(
  "UNetResNet34",
  initialize = function() {
    r <- torchvision::model_resnet34(pretrained = FALSE)
    self$stem <- torch::nn_sequential(r$conv1, r$bn1, r$relu)
    self$pool <- r$maxpool
    self$c1 <- r$layer1; self$c2 <- r$layer2; self$c3 <- r$layer3; self$c4 <- r$layer4
    self$d4 <- bloco_unet(512 + 256, 256)
    self$d3 <- bloco_unet(256 + 128, 128)
    self$d2 <- bloco_unet(128 + 64, 64)
    self$d1 <- bloco_unet(64 + 64, 64)
    self$d0 <- bloco_unet(64, 32)
    self$saida <- torch::nn_conv2d(32, 1, 1)
  },
  forward = function(x) {
    media <- torch::torch_tensor(c(0.485, 0.456, 0.406))$view(c(1, 3, 1, 1))
    desvio <- torch::torch_tensor(c(0.229, 0.224, 0.225))$view(c(1, 3, 1, 1))
    x <- (x$`repeat`(c(1, 3, 1, 1)) - media) / desvio
    sobe <- function(t) torch::nnf_interpolate(t, scale_factor = 2, mode = "bilinear", align_corners = FALSE)
    # aplica um bloco camada a camada, liberando as ativações intermediárias (importante em alta resolução)
    em_partes <- function(seq, t) { for (m in seq$children) { t <- m(t); invisible(gc(verbose = FALSE)) }; t }
    s1 <- self$stem(x); rm(x)
    s2 <- self$c1(self$pool(s1))
    s3 <- self$c2(s2)
    s4 <- self$c3(s3)
    s5 <- self$c4(s4)
    d <- self$d4(torch::torch_cat(list(sobe(s5), s4), dim = 2)); rm(s5, s4)
    d <- self$d3(torch::torch_cat(list(sobe(d), s3), dim = 2)); rm(s3)
    d <- self$d2(torch::torch_cat(list(sobe(d), s2), dim = 2)); rm(s2); invisible(gc(verbose = FALSE))
    d <- em_partes(self$d1, torch::torch_cat(list(sobe(d), s1), dim = 2)); rm(s1)
    d <- em_partes(self$d0, sobe(d))
    self$saida(d)
  }
)

#' Carrega uma U-Net a partir de um arquivo .safetensors (convertido de scripts/unet.py)
carregar_unet <- function(arquivo) {
  torch::torch_set_num_threads(max(1L, parallel::detectCores()))
  # o coletor de lixo do R não "vê" a memória dos tensores: sem isto, as ativações se acumulam e o
  # Windows passa a usar o disco (a previsão fica até 10x mais lenta em PCs com 8 GB)
  options(torch.threshold_call_gc = 800)
  m <- unet_resnet34()
  pesos <- safetensors::safe_load_file(arquivo, framework = "torch")
  esperados <- names(m$state_dict())
  faltam <- setdiff(esperados, names(pesos)); sobram <- setdiff(names(pesos), esperados)
  if (length(faltam) || length(sobram))
    stop("Pesos incompatíveis com a arquitetura. Faltam: ", paste(head(faltam), collapse = ", "),
         " | sobram: ", paste(head(sobram), collapse = ", "))
  m$load_state_dict(pesos)
  m$eval()
  m
}

caminho_pesos <- function(arquivo) system.file("modelos", arquivo, package = "odontolesao")

# ---------------------------------------------------------------- pré-processamento

#' Lê uma radiografia como matriz de intensidades 0–255 (linhas = altura, colunas = largura)
ler_radiografia <- function(caminho) {
  im <- magick::image_read(caminho)
  im <- magick::image_convert(im, colorspace = "gray")
  a <- as.integer(magick::image_data(im, channels = "gray"))      # [largura, altura, 1] ou [altura, largura, 1]
  m <- if (length(dim(a)) == 3) a[, , 1] else a
  info <- magick::image_info(im)
  if (nrow(m) != info$height) m <- t(m)
  storage.mode(m) <- "double"
  m
}

#' Matriz (esparsa) de pesos da redução por área (equivalente a cv2.INTER_AREA ao reduzir)
pesos_area <- function(n_ent, n_sai) {
  s <- n_ent / n_sai
  lin <- integer(); col <- integer(); val <- numeric()
  for (i in seq_len(n_sai)) {
    a <- (i - 1) * s; b <- i * s
    j <- (floor(a) + 1):min(n_ent, ceiling(b))
    lin <- c(lin, rep(i, length(j))); col <- c(col, j); val <- c(val, (pmin(b, j) - pmax(a, j - 1)) / s)
  }
  Matrix::sparseMatrix(i = lin, j = col, x = val, dims = c(n_sai, n_ent))
}

#' Redução por área de uma matriz para (altura, largura)
reduzir_area <- function(m, altura, largura) as.matrix(pesos_area(nrow(m), altura) %*% m %*% Matrix::t(pesos_area(ncol(m), largura)))

#' Redimensiona para a largura dada (área ao reduzir, bicúbica ao ampliar) e arredonda para 0–255
redimensionar <- function(m, largura) {
  h <- nrow(m); w <- ncol(m)
  altura <- round(h * largura / w)
  if (largura <= w) {
    r <- reduzir_area(m, altura, largura)
  } else {
    im <- magick::image_read(as.raster(m / 255))
    im <- magick::image_resize(im, sprintf("%dx%d!", largura, altura), filter = "Catrom")
    r <- ler_radiografia(magick::image_write(im, tempfile(fileext = ".png"), format = "png"))
  }
  r[] <- pmin(255, pmax(0, round(r)))
  r
}

#' CLAHE como o do OpenCV (clipLimit 2,0, grade 8x8), implementado em R
clahe <- function(m, limite = 2, grade = 8) {
  h <- nrow(m); w <- ncol(m)
  # o OpenCV completa a imagem (reflexão sem repetir a borda) até múltiplo do tamanho do bloco
  th <- ceiling(h / grade); tw <- ceiling(w / grade)
  H <- th * grade; W <- tw * grade
  refl <- function(i, n) { i <- ifelse(i > n, 2 * n - i, i); i }
  p <- m[refl(seq_len(H), h), refl(seq_len(W), w)]
  area <- th * tw
  corte <- max(1, floor(limite * area / 256))
  luts <- array(0, c(grade, grade, 256))
  for (ty in seq_len(grade)) for (tx in seq_len(grade)) {
    bloco <- p[((ty - 1) * th + 1):(ty * th), ((tx - 1) * tw + 1):(tx * tw)]
    hist <- tabulate(as.integer(bloco) + 1L, 256)
    excesso <- sum(pmax(hist - corte, 0))
    hist <- pmin(hist, corte)
    redist <- excesso %/% 256; resto <- excesso - redist * 256
    hist <- hist + redist
    if (resto > 0) { passo <- max(256 %/% resto, 1); idx <- seq(1, 256, by = passo)[seq_len(resto)]; hist[idx] <- hist[idx] + 1 }
    luts[ty, tx, ] <- pmin(255, pmax(0, round(cumsum(hist) * 255 / area)))
  }
  # interpolação bilinear entre os centros dos blocos
  fy <- (seq_len(h) - 1) / th - .5; fx <- (seq_len(w) - 1) / tw - .5            # como no OpenCV (sem meio pixel)
  y1 <- floor(fy); x1 <- floor(fx); ay <- fy - y1; ax <- fx - x1
  y2 <- pmin(y1 + 1, grade - 1); x2 <- pmin(x1 + 1, grade - 1); y1 <- pmax(y1, 0); x1 <- pmax(x1, 0)
  v <- as.integer(m) + 1L
  Y1 <- rep(y1, times = w) + 1; Y2 <- rep(y2, times = w) + 1; AY <- rep(ay, times = w)
  X1 <- rep(x1, each = h) + 1; X2 <- rep(x2, each = h) + 1; AX <- rep(ax, each = h)
  L <- function(Y, X) luts[cbind(Y, X, v)]
  r <- (L(Y1, X1) * (1 - AX) + L(Y1, X2) * AX) * (1 - AY) + (L(Y2, X1) * (1 - AX) + L(Y2, X2) * AX) * AY
  matrix(pmin(255, pmax(0, round(r))), h, w)
}

#' Pré-processamento completo: largura de trabalho + CLAHE (como comum.normalizar)
normalizar_radiografia <- function(m, largura = 1400) clahe(redimensionar(m, largura))

# ---------------------------------------------------------------- previsão

#' Mapa de probabilidade (matriz) de uma imagem já normalizada, com média da versão espelhada
prever_unet <- function(modelo, img, espelhar = TRUE) {
  h <- nrow(img); w <- ncol(img)
  H <- ceiling(h / 32) * 32; W <- ceiling(w / 32) * 32
  refl <- function(i, n) { i <- ifelse(i > n, 2 * n - i, i); i }          # np.pad(mode = "reflect")
  x <- img[refl(seq_len(H), h), refl(seq_len(W), w)] / 255
  t <- torch::torch_tensor(array(x, c(1, 1, H, W)), dtype = torch::torch_float())
  passada <- function(entrada) {                      # mapa completo (H x W), já na escala de probabilidade
    r <- torch::with_no_grad(as.array(torch::torch_sigmoid(modelo(entrada))[1, 1, , ]))
    gc(verbose = FALSE)                                # libera as ativações antes da próxima passada
    r
  }
  p <- passada(t)
  if (espelhar) p <- (p + passada(torch::torch_flip(t, 4))[, W:1]) / 2   # desvira antes de recortar
  p[1:h, 1:w, drop = FALSE]
}

#' Probabilidade na escala de trabalho (1.400 px), prevendo na largura do modelo
prever_radiografia <- function(modelo, original, largura_modelo = 2000, largura_trabalho = 1400) {
  p <- prever_unet(modelo, normalizar_radiografia(original, largura_modelo))
  alvo_h <- round(nrow(original) * largura_trabalho / ncol(original))
  if (ncol(p) != largura_trabalho)
    p <- reduzir_area(p, alvo_h, largura_trabalho)
  p
}
