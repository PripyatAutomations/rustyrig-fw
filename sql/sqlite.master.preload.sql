-- sqlite.master.preload.sql: Seed data applied to the master
-- database after sql/sqlite.master.sql when a new database is created.
-- The disabled, empty admin placeholder is provisioned with a random password
-- on first database creation. Its credential file is beside the database with
-- suffix .bootstrap-password, mode 0600; change it at first login.
-- Guest remains disabled by default.
-- Everything else (schema, indexes) belongs in sql/sqlite.master.sql.

-- DEVELOPMENT-ONLY v1.0 BLOCKER: remove remote-admin from bootstrap admin
-- permissions before release; see doc/release-checklist.md.
-- Users (mirror of config/http.users)
INSERT INTO users (uid, name, enabled, password, email, maxsessions, permissions) VALUES
   (1, 'admin', 0, '', 'no@example.com',   3, 'admin,remote-admin,bridge.admin,bridge.use,chat,elmer,files.read,files.write,media.source,owner,rx,serial,serial.*,tx,video-src,websdr.admin,websdr.connect,websdr.listen'),
   (2, 'guest', 0, '-', 'no@example.com', 3, 'rx,chat');

-- TX credits for testing (seconds of TX; with quota.enforce=true users
-- cannot key up without a row here)
INSERT INTO tx_credits (name, credits) VALUES
   ('admin', 14400);

-- DEVELOPMENT ONLY: grant the @full privilege set on every rig owned by
-- this server to authenticated admin accounts from ANY linked site (admin@*).
-- The preload runs before database.c creates its standard privilege presets,
-- so establish @full and its rig.full entry here first. INSERT OR IGNORE in
-- database.c will reuse the same set. The migration adds remote_allowed=1 to
-- these rows on first startup, making linked-site access explicit in the DB.
-- This does NOT bypass independent PTT, TX, or radio hardware safety checks.
-- REMOVE these test grants and revoke previously seeded grants before release.
INSERT OR IGNORE INTO privilege_sets (name,description,created_by)
VALUES ('full','Full rig operation, subject to independent TX and safety checks','system');
INSERT OR IGNORE INTO privilege_set_entries (set_id,privilege,effect)
SELECT id,'rig.full','allow' FROM privilege_sets WHERE name='full';
INSERT OR IGNORE INTO privilege_set_assignments
   (set_id,subject_user,subject_host,resource_kind,resource_uuid,assigned_by)
SELECT id,'admin','*','rig','*','seed'
FROM privilege_sets WHERE name='full';

-- @full is rig-scoped. A separate room grant is still needed for normal
-- remote room joins (including site lobby rooms with no rig).
INSERT INTO resource_privileges
   (subject_user,subject_host,resource_kind,resource_uuid,privilege,effect,granted_by)
VALUES ('admin','*','room','*','room.join','allow','seed');
