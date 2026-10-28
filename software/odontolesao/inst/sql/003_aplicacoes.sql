-- Registro de cada aplicação do modelo a uma radiografia (tela Aplicar).
CREATE TABLE IF NOT EXISTS aplicacoes (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  imagem_id TEXT NOT NULL REFERENCES imagens(id),
  modelo_id INTEGER NOT NULL REFERENCES modelos(id),
  limiar REAL,
  n_lesoes INTEGER,
  segundos REAL,
  em TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS ix_aplic_img ON aplicacoes (imagem_id, modelo_id);
