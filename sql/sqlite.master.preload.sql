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

-- Standard reusable privilege sets. These belong to initial seed data, not
-- migrations; a fresh database (including resetdb) loads this once.
-- Existing databases retain their administrator-edited group definitions.
INSERT INTO privilege_sets (name,description,created_by) VALUES
   ('rigctl','Rig tuning and control functions; TX requires separate authorization','system'),
   ('sysop','Reserved for future BBS system operator operations','system'),
   ('listener','Receive only: rig access, frequency and spectrum readback','system'),
   ('full','Full rig operation, subject to independent TX and safety checks','system'),
   ('transmit','Transmit privilege; safety and supervision remain mandatory','system'),
   ('supervised','New operator; Elmer checks remain mandatory','system');

INSERT INTO privilege_set_entries (set_id,privilege,effect)
SELECT s.id, e.privilege, 'allow'
FROM privilege_sets AS s
JOIN (
   SELECT 'rigctl' AS name, 'rig.use' AS privilege UNION ALL
   SELECT 'rigctl','rig.tune' UNION ALL
   SELECT 'rigctl','frequency.read' UNION ALL
   SELECT 'listener','rig.use' UNION ALL
   SELECT 'listener','rx' UNION ALL
   SELECT 'listener','frequency.read' UNION ALL
   SELECT 'listener','spectrum.read' UNION ALL
   SELECT 'full','rig.full' UNION ALL
   SELECT 'transmit','tx' UNION ALL
   SELECT 'supervised','noob'
) AS e ON e.name=s.name;

-- DEVELOPMENT ONLY: permit authenticated admin@* to control all rigs on
-- this server using @full. Delete this assignment and revoke existing
-- test grants before a production release. RF/PTT safety remains enforced.
INSERT OR IGNORE INTO privilege_set_assignments
   (set_id,subject_user,subject_host,resource_kind,resource_uuid,assigned_by)
SELECT id,'admin','*','rig','*','seed'
FROM privilege_sets WHERE name='full';

-- @full is rig-scoped. A separate room grant is still needed for normal
-- remote room joins (including site lobby rooms with no rig).
INSERT INTO resource_privileges
   (subject_user,subject_host,resource_kind,resource_uuid,privilege,effect,granted_by)
VALUES ('admin','*','room','*','room.join','allow','seed');

-- DEVELOPMENT ONLY: remote server administration for admin@*.
-- The destination independently checks site/* remote-admin plus admin/owner.
-- Add owner for full administrative parity during single-operator cluster tests.
-- These do not replace linked-node authentication or other safety controls.
-- REMOVE before production and revoke the already seeded rows.
INSERT INTO resource_privileges
   (subject_user,subject_host,resource_kind,resource_uuid,privilege,effect,granted_by)
VALUES
   ('admin','*','site','*','remote-admin','allow','seed'),
   ('admin','*','site','*','admin','allow','seed'),
   ('admin','*','site','*','owner','allow','seed');
