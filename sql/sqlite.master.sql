-- General audit log
CREATE TABLE audit_log (
   id INTEGER PRIMARY KEY AUTOINCREMENT,
   timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
   username TEXT NOT NULL,
   event_type TEXT NOT NULL CHECK(event_type IN (
      'login', 'logout', 'kicked', 'muted', 'unmuted', 'banned'
   )),
   details TEXT  -- optional, for additional info if needed
);

-- PTT-specific event log
CREATE TABLE ptt_log (
   id INTEGER PRIMARY KEY AUTOINCREMENT,
   username TEXT NOT NULL,
   frequency REAL NOT NULL,
   mode TEXT NOT NULL,
   bandwidth INTEGER NOT NULL,
   power REAL NOT NULL,
   start_time DATETIME DEFAULT CURRENT_TIMESTAMP,
   end_time DATETIME,
   record_file TEXT
);

CREATE TABLE chat_log (
   msg_id INTEGER PRIMARY KEY AUTOINCREMENT,
   msg_ts DATETIME DEFAULT CURRENT_TIMESTAMP,
   msg_src TEXT NOT NULL,
   msg_dest TEXT DEFAULT NULL,
   msg_type TEXT NOT NULL,
   msg_data TEXT NOT NULL
);

-- Users
CREATE TABLE users (
   uid INTEGER PRIMARY KEY,         -- unique user ID
   name TEXT NOT NULL UNIQUE,       -- username
   enabled BOOLEAN NOT NULL,        -- 0 = disabled, 1 = enabled
   password TEXT NOT NULL,          -- hashed password
   email TEXT,                      -- optional email
   maxclones INTEGER DEFAULT 1,     -- max allowed simultaneous sessions
   permissions TEXT                 -- comma-separated or JSON if complex
);
