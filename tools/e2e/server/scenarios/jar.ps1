# U1 jar: jars (fluids, cookies, fish, mobs), soft fluids and faucets.
# The behaviour runs in the selftests; this also checks that the game loaded the unit's pack
# files (blocks, helper entity, recipes) without content errors.
$r = Invoke-SelfTest 'jar.*'
if ($r.Pass -lt 12) { throw "expected at least 12 jar selftests to pass, got $($r.Pass)" }

$packErrors = Get-ServerLog | Where-Object {
    $_ -match ' ERR ' -and $_ -match '(blocks/(jar|faucet)\.json|entities/jar_mob\.json|recipes/(jar|faucet)\.json|complementaries:(jar|faucet|jar_mob))'
}
if ($packErrors) { throw "content errors in the jar pack files:`n$($packErrors -join "`n")" }
