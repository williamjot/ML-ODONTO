# Teste de equivalência R x Python (fase 5). Usa radiografias de pacientes: roda só no computador local.
# Rscript tests/equivalencia.R  ->  imprime o resultado e grava equivalencia_resultado.csv
suppressMessages(library(odontolesao))
PROJ <- "D:/WILLIAM/ELDER/ML Odonto"
E <- file.path(PROJ, "software", "teste_dados", "equivalencia")
ler16 <- function(f) t(EBImage::imageData(EBImage::readImage(f)))
vizinho <- function(m, H, W) m[floor((seq_len(H) - 1) * nrow(m) / H) + 1, floor((seq_len(W) - 1) * ncol(m) / W) + 1]
ordem_raster <- function(rot) {       # rótulos na ordem do OpenCV (primeiro pixel na varredura por linhas)
  idx <- which(rot > 0, arr.ind = TRUE); lab <- rot[idx]
  o <- order(idx[, 1], idx[, 2]); unique(lab[o])
}
ref <- utils::read.csv(file.path(PROJ, "Resultados/figuras/dados/lesoes_validacao.csv"), colClasses = c(paciente = "character"))
modelo <- odontolesao:::carregar_unet(odontolesao:::caminho_pesos("unet_lesoes_v2.safetensors"))
linhas <- list(); t0 <- Sys.time()
for (nm in sort(unique(ref$paciente))) {
  original <- odontolesao:::ler_radiografia(file.path(PROJ, "Treinamento/extraido/imagens", paste0(nm, ".png")))
  H <- round(nrow(original) * 1400 / ncol(original))
  verdade <- vizinho(ler16(file.path(PROJ, "Treinamento/extraido/mascaras", paste0(nm, ".png"))) > .5, H, 1400)
  p_py <- ler16(file.path(E, paste0(nm, "_prob.png")))
  p_r <- odontolesao:::prever_radiografia(modelo, original, 2000)
  aval <- function(p) {
    m <- odontolesao:::mascara_lesoes(odontolesao:::detectar_lesoes(p), 0.5, dim(p))
    a <- odontolesao:::avaliar_lesoes(m, verdade)
    a$por_lesao <- a$achada_por_lesao[ordem_raster(a$rotulos_verdade)]; a$mascara <- m; a
  }
  A <- aval(p_py); B <- aval(p_r)
  esperado <- ref$achada_UNET_V2[ref$paciente == nm][order(ref$lesao[ref$paciente == nm])] == 1
  dice_ab <- if (!any(A$mascara) && !any(B$mascara)) 1 else 2 * sum(A$mascara & B$mascara) / (sum(A$mascara) + sum(B$mascara))  # ambas vazias = concordância
  linhas[[nm]] <- data.frame(paciente = nm, lesoes = length(esperado), esperado = sum(esperado),
    A_achadas = sum(A$por_lesao), A_igual = identical(A$por_lesao, esperado),
    B_achadas = sum(B$por_lesao), B_igual = identical(B$por_lesao, esperado),
    B_falsos = B$previstas - B$previstas_corretas, cor_prob = stats::cor(as.vector(p_r), as.vector(p_py)),
    dif_max_prob = max(abs(p_r - p_py)), dice_mascaras = dice_ab)
  cat(sprintf("%s  esperado %d/%d | A %d (%s) | B %d (%s) | cor %.5f | Dice máscaras %.3f  [%.0f s]\n", nm, sum(esperado), length(esperado),
              sum(A$por_lesao), if (linhas[[nm]]$A_igual) "igual" else "DIFERENTE", sum(B$por_lesao), if (linhas[[nm]]$B_igual) "igual" else "DIFERENTE",
              linhas[[nm]]$cor_prob, dice_ab, as.numeric(difftime(Sys.time(), t0, units = "secs"))), sep = "")
}
r <- do.call(rbind, linhas)
utils::write.csv(r, file.path(E, "equivalencia_resultado.csv"), row.names = FALSE)
cat("\nTOTAL esperado", sum(r$esperado), "/", sum(r$lesoes), "| A (prob. Python + detecção R):", sum(r$A_achadas), if (all(r$A_igual)) "IGUAL" else "DIFERENTE",
    "| B (tudo em R):", sum(r$B_achadas), if (all(r$B_igual)) "IGUAL" else "DIFERENTE",
    sprintf("| falsos B %d | cor mínima %.5f | Dice mínimo das máscaras %.3f\n", sum(r$B_falsos), min(r$cor_prob), min(r$dice_mascaras)))
cat("APROVADO:", all(r$B_igual) && min(r$dice_mascaras) >= 0.95, "\n")
