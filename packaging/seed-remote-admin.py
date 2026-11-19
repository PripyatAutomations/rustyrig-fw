#!/usr/bin/env python3
"""Seed explicit remote administrator grants for a NEW RustyRig database only.

Read a whitespace-separated user@site list from stdin, not argv or SQL text.
A grant is a policy for an authenticated origin identity, never certificate
enrollment, a locally created account, or an authority to claim a site name.
"""
import argparse
import re
import sqlite3
import sys

# The site portion is an advertised RustyRig station identity, not a DNS
# hostname, arbitrary certificate subject, SQL expression, or IP address.
USER = re.compile(r"[A-Za-z0-9_][A-Za-z0-9_.-]{0,62}\Z")
SITE = re.compile(r"(?:\*|[A-Za-z0-9][A-Za-z0-9_.-]{0,62})\Z")
MAX_SUBJECTS = 64
MAX_INPUT = 4096


def parse_subjects(raw):
    if len(raw) > MAX_INPUT:
        raise ValueError("remote administrator list exceeds 4096 characters")
    names = raw.split()
    if not names or len(names) > MAX_SUBJECTS:
        raise ValueError("specify 1 to 64 user@site identities")
    found = set()
    subjects = []
    for entry in names:
        if entry.count("@") != 1:
            raise ValueError("expected user@site: " + repr(entry))
        username, site = entry.split("@")
        if not USER.fullmatch(username) or not SITE.fullmatch(site) or site in (".", ".."):
            raise ValueError("invalid remote identity: " + repr(entry))
        # Reject lookalike case variants: site/user comparisons are case
        # insensitive in the authorization resolver.
        identity = (username.lower(), site.lower())
        if identity not in found:
            found.add(identity)
            subjects.append((username, site))
    return subjects


def seed(database, subjects):
    connection = sqlite3.connect(database, timeout=5)
    try:
        with connection:
            # Only invoke this for a freshly created database. Never overwrite
            # administrator-managed grants on an existing installation.
            for username, site in subjects:
                for privilege in ("remote-admin", "admin"):
                    connection.execute(
                        """INSERT OR IGNORE INTO resource_privileges
                           (subject_user,subject_host,resource_kind,resource_uuid,
                            privilege,effect,granted_by,remote_allowed)
                           VALUES (?,?,'site','*',?,'allow','debian-first-install',1)""",
                        (username, site, privilege),
                    )
    finally:
        connection.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="validate the subject list without writing")
    parser.add_argument("database", nargs="?", help="new master.db path")
    args = parser.parse_args()
    raw = sys.stdin.read(MAX_INPUT + 1)
    try:
        subjects = parse_subjects(raw)
        if not args.check:
            if not args.database:
                parser.error("database path required unless --check is used")
            seed(args.database, subjects)
    except (ValueError, sqlite3.Error) as error:
        print("rustyrig remote access: " + str(error), file=sys.stderr)
        return 1
    if not args.check:
        print("RustyRig: created remote-admin policies for "
              + str(len(subjects)) + " subject(s); mesh identities still require verification.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
