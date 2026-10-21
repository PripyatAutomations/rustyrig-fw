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

-- DEVELOPMENT-ONLY: Permit the authenticated admin account from any linked
-- station to operate every rig and join every room owned by this server.
-- subject_host='*' matches remote hosts; resource_uuid='*' matches all rigs
-- and rooms, including resources created after bootstrap. The startup schema
-- migration adds remote_allowed=1 to these fresh-database rows.
-- REMOVE before release: this grants broad cross-site control to admin@*.
INSERT INTO resource_privileges
   (subject_user,subject_host,resource_kind,resource_uuid,privilege,effect,granted_by)
VALUES
   ('admin','*','rig','*','rig.full','allow','seed'),
   ('admin','*','room','*','room.join','allow','seed');
