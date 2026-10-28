-- Esquema inicial do banco do OdontoLesão.
CREATE TABLE IF NOT EXISTS config (
  chave TEXT PRIMARY KEY,
  valor TEXT
);

CREATE TABLE IF NOT EXISTS imagens (
  id TEXT PRIMARY KEY,              -- identificador anônimo A001...
  arquivo TEXT NOT NULL,            -- nome do arquivo original (só local)
  caminho TEXT NOT NULL,            -- caminho completo da radiografia
  largura INTEGER, altura INTEGER,
  amostra INTEGER NOT NULL DEFAULT 0,  -- 1 = amostra aleatória fixa
  importada_em TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS modelos (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  nome TEXT UNIQUE NOT NULL,
  arquivo TEXT,
  tipo TEXT NOT NULL,
  largura INTEGER,
  limiar REAL,
  descricao TEXT,
  metricas TEXT,                    -- JSON
  ativo INTEGER NOT NULL DEFAULT 0,
  criado_em TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS deteccoes (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  imagem_id TEXT NOT NULL REFERENCES imagens(id),
  modelo_id INTEGER NOT NULL REFERENCES modelos(id),
  indice INTEGER NOT NULL,          -- número mostrado ao revisor (1, 2, ...)
  contorno TEXT NOT NULL,           -- JSON [[x,y],...] em pixels da imagem original
  cx REAL, cy REAL, area REAL,
  confianca REAL,
  UNIQUE (imagem_id, modelo_id, indice)
);
CREATE INDEX IF NOT EXISTS ix_det_img ON deteccoes (imagem_id, modelo_id);

CREATE TABLE IF NOT EXISTS revisoes (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  imagem_id TEXT NOT NULL REFERENCES imagens(id),
  modelo_id INTEGER NOT NULL REFERENCES modelos(id),
  revisor TEXT NOT NULL,
  iniciada_em TEXT NOT NULL,
  atualizada_em TEXT NOT NULL,
  concluida_em TEXT,
  inadequada INTEGER NOT NULL DEFAULT 0,
  observacoes TEXT NOT NULL DEFAULT '',
  UNIQUE (imagem_id, modelo_id, revisor)
);

CREATE TABLE IF NOT EXISTS decisoes (
  revisao_id INTEGER NOT NULL REFERENCES revisoes(id) ON DELETE CASCADE,
  deteccao_id INTEGER NOT NULL REFERENCES deteccoes(id),
  decisao TEXT NOT NULL CHECK (decisao IN ('confirmada', 'rejeitada', 'duvida')),
  em TEXT NOT NULL,
  PRIMARY KEY (revisao_id, deteccao_id)
);

CREATE TABLE IF NOT EXISTS lesoes_adicionadas (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  revisao_id INTEGER NOT NULL REFERENCES revisoes(id) ON DELETE CASCADE,
  contorno TEXT NOT NULL,
  em TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS eventos (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  em TEXT NOT NULL,
  revisor TEXT,
  tipo TEXT NOT NULL,
  detalhe TEXT
);
