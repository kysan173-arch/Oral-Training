BEGIN;

-- Derived data only. Immutable source revisions and locked session manifests stay intact.
-- JSONB keeps installation compatible with PostgreSQL without requiring pgvector.
CREATE TABLE IF NOT EXISTS knowledge_chunk_embeddings (
  chunk_id TEXT NOT NULL REFERENCES knowledge_chunks(id) ON DELETE CASCADE,
  cache_key TEXT NOT NULL,
  input_sha256 TEXT NOT NULL,
  model TEXT NOT NULL,
  dimensions INTEGER NOT NULL CHECK (dimensions IN (128, 256, 512, 768)),
  embedding JSONB NOT NULL CHECK (jsonb_typeof(embedding) = 'array'),
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  PRIMARY KEY (chunk_id, cache_key),
  CHECK (jsonb_array_length(embedding) = dimensions)
);

COMMIT;
