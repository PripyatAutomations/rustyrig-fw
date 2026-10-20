-- sqlite.master.preload.sql: Seed data applied to the master
-- database after sql/sqlite.master.sql when a new database is created.
-- The disabled, empty admin placeholder is provisioned with a random password
-- on first database creation. Its credential file is beside the database with
-- suffix .bootstrap-password, mode 0600; change it at first login.
-- Guest remains disabled by default.
-- Everything else (schema, indexes) belongs in sql/sqlite.master.sql.

-- Users (mirror of config/http.users)
INSERT INTO users (uid, name, enabled, password, email, maxsessions, permissions) VALUES
   (1, 'admin', 0, '', 'no@example.com',   3, 'admin,bridge.admin,bridge.use,chat,elmer,files.read,files.write,media.source,owner,rx,serial,serial.*,tx,video-src,websdr.admin,websdr.connect,websdr.listen'),
   (2, 'guest', 0, '-', 'no@example.com', 3, 'rx,chat');

-- TX credits for testing (seconds of TX; with quota.enforce=true users
-- cannot key up without a row here)
INSERT INTO tx_credits (name, credits) VALUES
   ('admin', 14400);
