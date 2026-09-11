-- sqlite.master.examples.sql: Test/example data applied to the master
-- database after sql/sqlite.master.sql when a new database is created.
-- Users here mirror config/http.users (passwords are sha1 and shown in that
-- file's format: name:enabled:sha1:email:maxsessions:privs):
--   admin / "admin"  (5baa61e4c9b93f3f0682250b6cf8331b7ee68fd8)
--   guest / "guest"  (35675e68f4b5af7b995d9205ad0fc43842f16450)
--   bob   / "bob"    (48181acd22b3edaebc8a447868a7df7ce629920a)
-- Everything else (schema, indexes) belongs in sql/sqlite.master.sql.

-- Users (mirror of config/http.users)
INSERT INTO users (uid, name, enabled, password, email, maxclones, permissions) VALUES
   (1, 'admin', 1, '5baa61e4c9b93f3f0682250b6cf8331b7ee68fd8', 'no@example.com',   3, 'admin,edit,view,radio,tx,elmer,syslog,chat'),
   (2, 'guest', 1, '35675e68f4b5af7b995d9205ad0fc43842f16450', 'no@example.com',   3, 'edit,view,radio,tx,noob,syslog,chat'),
   (3, 'bob',   1, '48181acd22b3edaebc8a447868a7df7ce629920a', 'none@example.com', 3, 'admin,edit,view,radio,tx,elmer,syslog,chat,restart');

-- TX credits for testing (seconds of TX; with quota.enforce=true users
-- cannot key up without a row here)
INSERT INTO tx_credits (username, credits) VALUES
   ('admin', 14400),
   ('guest', 3600),
   ('bob',   14400);
