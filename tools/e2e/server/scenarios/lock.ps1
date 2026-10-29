# U3 lock: keys, lock block, safe, gates, gold / netherite doors and trapdoors.
# The behaviour is covered by the `lock.*` selftests (SimulatedPlayers, redstone, item placement);
# the scenario also checks that the lock pack loads without content log errors.
Wait-LogLine 'lock: installed' 30 | Out-Null
$r = Invoke-SelfTest 'lock.*'
if ($r.Pass -lt 10) { throw "expected at least 10 lock selftests to pass, got $($r.Pass)" }
$bad = @(Get-ServerLog | Where-Object {
        $_ -match 'ERR \[Server\] \[(Blocks|Recipes|Items|Item|Entity|Geometry|Texture|Json)' -and
        $_ -match '(lock_block|safe|gate|_door|trapdoor|key\.json|safe_storage)'
    })
if ($bad.Count -gt 0) { throw "content log errors for the lock pack: $($bad -join ' | ')" }
