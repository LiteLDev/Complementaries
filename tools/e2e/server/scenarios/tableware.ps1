# U13 tableware: goblet, flower box, book piles, pancake, planter, urn, jar boat.
# The logic is covered by the selftests (blocks and items registered by the behavior pack, every
# interaction through handleUse and once through the real interaction event of a simulated player).
$r = Invoke-SelfTest 'tableware.*'
if ($r.Pass -lt 8) { throw "expected 8 tableware selftests to pass, got $($r.Pass)" }
Wait-LogLine 'tableware: goblet filled with potion' 5 | Out-Null
Wait-LogLine 'tableware: placed a book pile' 5 | Out-Null

# The behavior pack (blocks, items, recipes, loot tables, features) must load without content errors.
$bad = @(Get-ServerLog | Where-Object {
        ($_ -match 'complementaries:|tableware|urn_cave' -and $_ -match '(?i)\b(error|failed|invalid|duplicate)\b' -and $_ -notmatch 'SELFTEST') -or
        $_ -match '(ERR|WRN) \[Server\] \[(Components|Blocks|Geometry|Item|Recipes|FeatureRegistry|Molang|Json)'
    })
if ($bad.Count) { throw "content errors for tableware:`n$($bad -join "`n")" }
