-- Lesões de referência (marcadas pelo especialista), achados por modelo, auditoria e validação.
CREATE TABLE IF NOT EXISTS referencia_imagens (
  paciente TEXT PRIMARY KEY,         -- 01..20 (número do PDF)
  caminho_imagem TEXT NOT NULL,
  caminho_mascara TEXT NOT NULL,
  caminho_traco TEXT NOT NULL,
  caminho_prob TEXT,                 -- mapa de probabilidade do modelo de referência (PNG 8 bits, escala 1.400 px)
  largura INTEGER, altura INTEGER
);

CREATE TABLE IF NOT EXISTS referencia (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  paciente TEXT NOT NULL REFERENCES referencia_imagens(paciente),
  lesao INTEGER NOT NULL,            -- número da lesão no paciente (ordem de lesoes_validacao.csv)
  contorno TEXT NOT NULL,            -- JSON, pixels da imagem original
  cx REAL, cy REAL, area REAL,
  diam_eq_px REAL,                   -- diâmetro equivalente na escala de trabalho (1.400 px)
  arcada TEXT,
  UNIQUE (paciente, lesao)
);

CREATE TABLE IF NOT EXISTS achados_referencia (
  referencia_id INTEGER NOT NULL REFERENCES referencia(id),
  modelo TEXT NOT NULL,              -- rótulo do modelo (ex.: "U-Net v2")
  achada INTEGER NOT NULL,
  PRIMARY KEY (referencia_id, modelo)
);

CREATE TABLE IF NOT EXISTS auditoria_perdidas (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  referencia_id INTEGER NOT NULL REFERENCES referencia(id),
  modelo TEXT NOT NULL,
  revisor TEXT NOT NULL,
  resposta TEXT CHECK (resposta IN ('visivel', 'duvidosa', 'nao_visivel', 'erro_marcacao')),
  observacao TEXT NOT NULL DEFAULT '',
  em TEXT NOT NULL,
  UNIQUE (referencia_id, modelo, revisor)
);

CREATE TABLE IF NOT EXISTS validacao_froc (
  modelo TEXT, limiar REAL, lesoes INTEGER, achadas INTEGER, previstas INTEGER, corretas INTEGER,
  inter_px REAL, soma_px REAL, n_imagens INTEGER
);

CREATE TABLE IF NOT EXISTS validacao_pacientes (
  modelo TEXT, limiar REAL, paciente TEXT, inter REAL, soma REAL, lesoes INTEGER,
  lesoes_achadas INTEGER, previstas INTEGER, previstas_corretas INTEGER
);
