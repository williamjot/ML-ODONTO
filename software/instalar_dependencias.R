# Instala as dependências e o pacote do ML ODONTO.
# Rode uma vez, a partir da raiz do projeto, DEPOIS de colocar os pesos dos modelos em
# software/odontolesao/inst/modelos (veja o README):   Rscript software/instalar_dependencias.R
options(repos = c(CRAN = "https://cloud.r-project.org"))
cat("biblioteca:", .libPaths()[1], "\n")
pacotes <- c("shiny", "bslib", "bsicons", "DT", "RSQLite", "DiagrammeR", "magick", "htmltools",
             "mirai", "promises", "testthat", "pkgload", "roxygen2", "yaml", "svglite", "ragg",
             "shinytest2", "renv", "BiocManager", "safetensors", "torch", "torchvision", "callr", "commonmark", "Matrix")
falta <- setdiff(pacotes, rownames(installed.packages()))
cat("instalando:", falta, "\n")
if (length(falta)) install.packages(falta, Ncpus = 4)
if (!"EBImage" %in% rownames(installed.packages()))
  BiocManager::install("EBImage", update = FALSE, ask = FALSE)
cat("FALTANDO:", setdiff(c(pacotes, "EBImage"), rownames(installed.packages())), "\n")

# biblioteca do torch (libtorch, ~200 MB), baixada uma única vez
if (!torch::torch_is_installed()) torch::install_torch(timeout = 3600)
# o pacote do programa
if (!length(list.files("software/odontolesao/inst/modelos", "[.]safetensors$")))
  warning("Nenhum peso de modelo em software/odontolesao/inst/modelos: a tela Aplicar não vai funcionar.")
install.packages("software/odontolesao", repos = NULL, type = "source")
cat("Pronto. Abra o programa com Abrir_ML_ODONTO.bat", fill = TRUE)
