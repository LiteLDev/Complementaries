# U2 storage: sack, presents, quiver, lunch basket (docs/spec/storage.md).
# The selftests cover drops with contents, falling sacks, the container view (write-through, refused
# items), overencumbered, presents (pack / recipient / unpack / dye / drops), trapped presents, the
# quiver (off-hand lending, cycling, screen) and the lunch basket.
$r = Invoke-SelfTest 'storage.*'
if ($r.Pass -lt 10) { throw "expected at least 10 storage selftests to pass, got $($r.Pass)" }

# The packs load without content errors for our blocks, items and the view entity.
$bad = Get-ServerLog | Where-Object { $_ -match '(?i)(error|warn).*complementaries:(sack|present|trapped_present|quiver|lunch_basket|storage_view)' }
if ($bad) { throw "content log errors for storage content:`n$($bad -join "`n")" }
