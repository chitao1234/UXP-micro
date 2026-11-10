# HSTS preload maintenance

`nsSTSPreloadList.inc` is a pinned copy of Mozilla's maintained list, including
its expiration timestamp. `nsSTSPreloadList.source.json` records the source
revision, URL, hash and entry count. Builds use these checked-in files without
network access.

Refresh before the expiration date with Python 3:

```
python security/manager/tools/update-hsts-preloads.py --revision FULL_FIREFOX_COMMIT_ID
```

Use a full commit ID from `mozilla-firefox/firefox`. Review and commit the list
and metadata together. `--input` optionally accepts that revision's previously
downloaded list for offline maintenance; it is not a build requirement.

The build generates a read-only DAFSA using Firefox's generator. Do not replace
it with a heap-resident string table. Both the generator and its incremental
DAFSA helper come from Firefox and work with the configured Python 3.7 host.

The list expires according to its upstream timestamp. The
`network.stricttransportsecurity.preloadlist` preference controls only static
preloads; dynamic HSTS remains independent. All memory tiers use the same
security policy. A site's `max-age=0` creates a local knockout for its own
preload, while an ancestor's `includeSubDomains` still applies. Private browsing
keeps overrides separate and clears them when its last context exits.
