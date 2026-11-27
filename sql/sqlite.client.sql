-- RustyRig client persistent state, schema v4.
-- Ungrouped is virtual: a server without network_memberships rows.
PRAGMA foreign_keys = ON;
CREATE TABLE IF NOT EXISTS networks (
  id INTEGER PRIMARY KEY,
  name TEXT NOT NULL UNIQUE COLLATE NOCASE,
  network_key TEXT,
  enabled INTEGER NOT NULL DEFAULT 1 CHECK(enabled IN (0,1))
);
CREATE TABLE IF NOT EXISTS servers (
  id INTEGER PRIMARY KEY,
  name TEXT NOT NULL UNIQUE,
  url TEXT NOT NULL,
  username TEXT,
  proxy TEXT,
  enabled INTEGER NOT NULL DEFAULT 1 CHECK(enabled IN (0,1))
);
CREATE TABLE IF NOT EXISTS network_memberships (
  network_id INTEGER NOT NULL REFERENCES networks(id) ON DELETE CASCADE,
  server_id INTEGER NOT NULL REFERENCES servers(id) ON DELETE CASCADE,
  PRIMARY KEY(network_id, server_id)
);
CREATE INDEX IF NOT EXISTS idx_network_memberships_server
  ON network_memberships(server_id);
CREATE TABLE IF NOT EXISTS client_settings (
  key TEXT PRIMARY KEY,
  value TEXT
);
-- Saved effective rrclient configuration; loaded over rrclient.cfg at startup.
CREATE TABLE IF NOT EXISTS config_overrides (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS server_properties (
  server_id INTEGER NOT NULL REFERENCES servers(id) ON DELETE CASCADE,
  property TEXT NOT NULL,
  value TEXT NOT NULL,
  PRIMARY KEY(server_id, property)
);
CREATE TABLE IF NOT EXISTS proxies (name TEXT PRIMARY KEY COLLATE NOCASE, url TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS themes (name TEXT NOT NULL COLLATE NOCASE, property TEXT NOT NULL, value TEXT NOT NULL, PRIMARY KEY(name,property));
-- Persistent SDR user metadata; never purged with receiver catalog records.
CREATE TABLE IF NOT EXISTS receiver_favorites(url TEXT PRIMARY KEY);
CREATE TABLE IF NOT EXISTS receiver_state(url TEXT PRIMARY KEY,state TEXT NOT NULL CHECK(state IN ('unknown','unreachable','restricted','good')),reason TEXT NOT NULL,updated_at INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS receiver_connections(url TEXT PRIMARY KEY,last_connected INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS receiver_preference_migration(id INTEGER PRIMARY KEY CHECK(id=1));
PRAGMA user_version = 4;
