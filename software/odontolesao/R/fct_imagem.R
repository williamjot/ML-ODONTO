# Operações de imagem: leitura, versão reduzida para o visor e contornos de máscaras.

LARGURA_VISOR <- 2000  # px; o visor recebe a imagem reduzida, as coordenadas continuam as originais

#' Caminho da cópia reduzida (JPEG) de uma radiografia, criada na primeira vez
imagem_visor <- function(imagem, pasta_cache) {
  dir.create(pasta_cache, showWarnings = FALSE, recursive = TRUE)
  destino <- file.path(pasta_cache, paste0(imagem$id, ".jpg"))
  if (!file.exists(destino)) {
    im <- magick::image_read(imagem$caminho)
    im <- magick::image_convert(im, colorspace = "gray")
    if (magick::image_info(im)$width > LARGURA_VISOR)
      im <- magick::image_resize(im, paste0(LARGURA_VISOR, "x"))
    magick::image_write(im, destino, format = "jpeg", quality = 92)
  }
  destino
}

#' Componentes conectados de uma máscara, na mesma ordem do OpenCV (varredura por linhas)
#'
#' Devolve uma lista com, para cada lesão: contorno (matriz n x 2, pixels 0-based da imagem
#' original), centroide e área. A ordem coincide com cv2.connectedComponents, usada pela ferramenta
#' de revisão anterior, para que os números das lesões sejam os mesmos.
componentes_mascara <- function(caminho_mascara, passo = 2) {
  m <- EBImage::readImage(caminho_mascara)
  if (length(dim(m)) > 2) m <- m[, , 1]
  rot <- EBImage::bwlabel(m > 0.5)
  n <- max(rot)
  if (n == 0) return(structure(list(), dims = dim(m)))
  # primeiro pixel de cada componente na varredura por linhas (menor y, depois menor x)
  idx <- which(rot > 0, arr.ind = TRUE)            # colunas: x (dim1), y (dim2)
  lab <- rot[idx]
  ordem_px <- order(idx[, 2], idx[, 1])
  primeiro <- !duplicated(lab[ordem_px])
  ordem_comp <- lab[ordem_px][primeiro]           # rótulos na ordem do OpenCV
  contornos <- EBImage::ocontour(rot)
  area <- tabulate(lab, nbins = n)
  cx <- tapply(idx[, 1], lab, mean); cy <- tapply(idx[, 2], lab, mean)   # centroides (1-based)
  saida <- lapply(seq_along(ordem_comp), function(k) {
    r <- ordem_comp[k]
    cont <- contornos[[as.character(r)]]
    if (nrow(cont) > 3 * passo) cont <- cont[seq(1, nrow(cont), by = passo), , drop = FALSE]
    list(indice = k, contorno = cont - 1L, cx = cx[[as.character(r)]] - 1, cy = cy[[as.character(r)]] - 1, area = area[r])
  })
  structure(saida, dims = dim(m))
}
