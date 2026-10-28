# U15 cannon: cannon block (load, aim, fire, redstone, barrel), cannonballs, bombs, red merchant.
$mark = Get-LogMark
$r = Invoke-SelfTest 'cannon.*'
if ($r.Pass -lt 10) { throw "expected at least 10 cannon selftests to pass, got $($r.Pass)" }
Wait-LogLine 'Cannon fired complementaries:cannonball' 5 -Since $mark | Out-Null
Wait-LogLine 'Bomb exploded: complementaries:bomb ' 5 -Since $mark | Out-Null
Wait-LogLine 'Red merchant spawned' 5 -Since $mark | Out-Null
