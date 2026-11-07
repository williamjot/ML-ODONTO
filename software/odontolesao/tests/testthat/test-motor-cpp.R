test_that("ler_pfm lê o PFM do motor C++ (linhas de baixo para cima, little-endian)", {
  m <- matrix(c(0, .25, .5, .75, 1, .125), nrow = 2, byrow = TRUE)
  f <- tempfile(fileext = ".pfm")
  con <- file(f, "wb")
  writeBin(charToRaw("Pf\n3 2\n-1\n"), con)
  writeBin(as.vector(t(m[2:1, ])), con, size = 4, endian = "little")
  close(con)
  expect_equal(odontolesao:::ler_pfm(f), m, tolerance = 1e-7)
})

test_that("options(mlodonto.motor = 'r') desliga o motor C++", {
  old <- options(mlodonto.motor = "r"); on.exit(options(old))
  expect_identical(odontolesao:::motor_inferencia("unet_lesoes_v2.safetensors"), "r")
})

test_that("o motor C++, quando instalado, devolve a probabilidade na escala de trabalho", {
  skip_if_not(odontolesao:::motor_cpp_disponivel("unet_lesoes_v2.safetensors"), "motor C++ não instalado")
  f <- tempfile(fileext = ".png")
  im <- matrix(rep(seq(0, 1, length.out = 2400), each = 1100), 1100, 2400)
  EBImage::writeImage(EBImage::Image(t(im)), f)
  r <- NULL
  odontolesao:::executar_cpp(f, "unet_lesoes_v2.safetensors", 2000, function(i, x) r <<- x)
  expect_null(r$erro)
  expect_equal(dim(r$prob), c(round(1100 * 1400 / 2400), 1400))
  expect_equal(c(r$altura, r$largura), c(1100, 2400))
  expect_true(all(r$prob >= 0 & r$prob <= 1))
})
