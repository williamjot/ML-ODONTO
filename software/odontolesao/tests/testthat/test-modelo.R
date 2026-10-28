test_that("rotular8 une vizinhos na diagonal (como o OpenCV)", {
  m <- matrix(FALSE, 6, 6); m[2, 2] <- TRUE; m[3, 3] <- TRUE; m[5, 5] <- TRUE
  r <- rotular8(m)
  expect_equal(max(r), 2)
  expect_equal(r[2, 2], r[3, 3])
})

test_that("redução por área conserva a média e as dimensões", {
  W <- pesos_area(10, 4)
  expect_equal(Matrix::rowSums(W), rep(1, 4))
  m <- matrix(runif(2000) * 255, 40, 50)
  r <- redimensionar(m, 20)
  expect_equal(dim(r), c(16, 20))
  expect_lt(abs(mean(r) - mean(m)), 1)
})

test_that("suavização e máximo em janela", {
  expect_equal(suavizar(matrix(0.3, 30, 40)), matrix(0.3, 30, 40), tolerance = 1e-12)
  p <- matrix(0, 50, 50); p[25, 25] <- 1
  s <- suavizar(p)
  expect_equal(which(s == max(s), arr.ind = TRUE)[1, ], c(row = 25, col = 25))
  expect_equal(sum(s), 1, tolerance = 1e-9)
  mx <- maximo_janela(p, 3)
  expect_true(all(mx[22:28, 22:28] == 1)); expect_equal(mx[21, 25], 0)
})

test_that("CLAHE mantém a forma e o intervalo 0–255", {
  m <- matrix(round(runif(130 * 170) * 120 + 60), 130, 170)
  r <- clahe(m)
  expect_equal(dim(r), dim(m)); expect_true(all(r >= 0 & r <= 255))
  expect_gt(stats::sd(r), stats::sd(m))           # aumenta o contraste
})

test_that("detecção e avaliação por lesão", {
  p <- matrix(0, 80, 120); p[20:30, 20:30] <- 0.9; p[50:60, 80:94] <- 0.2
  d <- detectar_lesoes(p, raio_regiao = 40)
  expect_length(d, 2)
  expect_gt(d[[1]]$confianca, d[[2]]$confianca)
  m <- mascara_lesoes(d, 0.5, dim(p))
  verdade <- matrix(FALSE, 80, 120); verdade[18:32, 18:32] <- TRUE; verdade[52:58, 82:92] <- TRUE
  a <- avaliar_lesoes(m, verdade)
  expect_equal(c(a$lesoes, a$lesoes_achadas, a$previstas, a$previstas_corretas), c(2, 1, 1, 1))
})
