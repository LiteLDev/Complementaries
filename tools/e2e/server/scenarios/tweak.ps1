# U18 tweak: placeable sticks / gunpowder / hanging pots, throwables, bells, cauldron dyeing, bottling,
# zombie horses (selftests), plus a clean load of the unit's blocks and entities.
$packErrors = Get-ServerLog | Where-Object { $_ -match 'ERR' -and $_ -match 'Complementaries_(BP|RP)' -and $_ -match 'tweak|stick|blaze_rod|breeze_rod|gunpowder|hanging_flower_pot|throwable_' }
if ($packErrors) { throw "tweak pack definitions failed to load: $($packErrors -join ' | ')" }

$r = Invoke-SelfTest 'tweak.*' -TimeoutSeconds 600
if ($r.Pass -lt 12) { throw "expected at least 12 tweak selftests to pass, got $($r.Pass)" }
foreach ($line in 'tweak: .* bottled experience', 'tweak: .* dyed 4 minecraft:red_wool', 'tweak: rang the bell', 'skeleton horse turned into a zombie horse') {
    Wait-LogLine $line 5 | Out-Null
}
