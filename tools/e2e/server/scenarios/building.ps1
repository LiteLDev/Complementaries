# U10 building: every building selftest, then a check that the data-driven content (blocks,
# items, recipes, loot tables, features) loaded without content-log errors.
$r = Invoke-SelfTest 'building.*' -TimeoutSeconds 900
if ($r.Pass -lt 13) { throw "expected at least 13 building selftests to pass, got $($r.Pass)" }

$bad = Get-ServerLog | Where-Object { $_ -match 'complementaries' -and $_ -match '(?i)\b(error|invalid|failed|unknown)\b' -and $_ -notmatch 'SELFTEST' }
if ($bad) { throw "content errors for complementaries:`n$($bad -join "`n")" }
