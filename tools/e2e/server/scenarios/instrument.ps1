# U6 instrument: globes, wind vane, clock block, hourglass, altimeter.
# Logic is covered by the `instrument.*` selftests (placement tracking, spinning, comparator output,
# special names, clock hands, hourglass flow and drops, wind vane weather and wind charge).
$r = Invoke-SelfTest 'instrument.*'
if ($r.Pass -lt 12) { throw "expected at least 12 instrument selftests to pass, got $($r.Pass)" }

# The globe seed commands.
$mark = Get-LogMark
Send-Console 'supplementaries globe newseed'
Wait-LogLine 'instrument: globe seed changed' 15 -Since $mark | Out-Null
Send-Console 'supplementaries globe resetseed'
Wait-LogLine 'instrument: globe seed reset' 15 -Since $mark | Out-Null

# The behavior pack definitions must load without content errors. (The sepia globe recipes need the
# antique ink and soap of other units; they resolve once those are present.)
$bad = Get-ServerLog | Where-Object {
    ($_ -match 'complementaries:(globe|globe_sepia|clock_block|hourglass|wind_vane|altimeter)\b' -or $_ -match '\[(Blocks|Components|Geometry|Item)\]') -and
    $_ -match '(?i)ERR|error|failed|invalid|unknown' -and $_ -notmatch 'recipes/globe_(sepia|from_sepia)\.json'
}
if ($bad) { throw "content errors for instrument blocks:`n$($bad -join "`n")" }
