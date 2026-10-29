# U11 rope: rope, rope arrow, pulley, flax. Logic runs in the selftests (including the wild flax
# world generation feature); the scenario also checks that the pack content loads without
# content-log errors.
$r = Invoke-SelfTest 'rope.*'
if ($r.Pass -lt 16) { throw "expected at least 16 rope selftests to pass, got $($r.Pass)" }


# Content log: nothing of ours may be rejected by the game (blocks, items, entity, recipes, features).
$bad = Get-ServerLog | Where-Object {
    ($_ -match 'complementaries' -and $_ -match '(?i)\b(error|invalid|failed|unknown|not valid|unexpected)\b' -and $_ -notmatch 'SELFTEST') -or
    ($_ -match ' ERR \[Server\] \[(Json|Blocks|Item|Recipes|Entity|Molang|FeatureRegistry|Texture|Geometry)\]')
}
if ($bad) {
    $bad | ForEach-Object { Write-Host "content: $_" }
    throw "the rope content produced $(@($bad).Count) content-log error line(s)"
}
