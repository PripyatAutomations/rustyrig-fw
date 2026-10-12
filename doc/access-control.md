# Named access policies

RustyRig listeners, server peers and RTP pools reference named ACLs. Rules are
ordered: the first matching rule decides, and no match (or no ACL) denies access.
A specific deny must precede a broader allow. These are server policies; native
and browser clients receive the same authentication decisions.

```ini
[acl:site]
deny=guest@10.200.0.0/16
allow=10.0.0.0/8
allow=@admin
deny=all

[listener:http]
acl=site
[listener:https]
acl=site
[listener:link]
acl=site
[listener:mqtt]
acl=site
[listener:media]
acl=site

[peer:other-site]
enabled=false
acl=site
```

Selectors accept `all`, a numeric IPv4/IPv6 address or CIDR, a username, an
application-supplied `@group`, `user@address`, `user@all`, and `@group@address`.
RustyRig groups match literal account permission tokens, case-insensitively;
`admin` does not implicitly belong to every group. No hostname lookup occurs.
`allow=all` explicitly opens a policy. The shipped example permits loopback and
private networks; existing deployments must add suitable ACL references when
upgrading. Empty or missing references deny. Invalid rules and unknown ACL
references reject startup; malformed policies deny during reload.

Before authentication, a potentially matching identity rule defers the decision
until the account is verified, preserving first-match ordering. The reusable
`librustyaxe/acl.h` matcher exposes `ACL_DEFER` for unknown identity or address;
this result is never final authorization. Applications supply group membership.
MQTT has no verified account identity here, so user/group rules cannot authorize
MQTT clients; use network rules for that listener.

Repeated allow/deny lines are retained in numbered dictionary entries by config
callbacks and saved in their original order. Endpoint ACL references are saved
in their corresponding sections.
