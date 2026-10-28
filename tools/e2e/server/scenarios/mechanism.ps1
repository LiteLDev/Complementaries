# U7 mechanism: spring launcher, bellows, turn table, cog block, crank.
# The selftests cover the mechanics; the console part checks that the blocks exist for commands and
# that a launcher placed by /setblock reacts to redstone placed by /setblock (tracking through the
# block-change and redstone-setup hooks, not through a selftest helper).
$r = Invoke-SelfTest 'mechanism.*'
if ($r.Pass -lt 15) { throw "expected at least 15 mechanism selftests to pass, got $($r.Pass)" }

foreach ($id in 'piston_launcher', 'bellows', 'turn_table', 'cog_block', 'crank') {
    $mark = Get-LogMark
    Send-Console "give @a complementaries:$id 1"
    Start-Sleep -Milliseconds 300
    $bad = Get-ServerLog -Since $mark | Where-Object { $_ -match 'Unknown|Syntax error|not found' }
    if ($bad) { throw "give complementaries:${id} failed: $bad" }
}

# a launcher pointing up at x=40 (well away from the selftest area), powered by a redstone block
Send-Console 'tickingarea add 30 -64 30 50 0 50 cm_mechanism_e2e'
Wait-Seconds 2
Send-Console 'setblock 40 -60 40 complementaries:piston_launcher ["minecraft:facing_direction"="down"]'
Wait-Seconds 1
$mark = Get-LogMark
Send-Console 'setblock 41 -60 40 redstone_block'
Wait-LogLine 'mechanism: launcher at 40 -60 40 launched' 10 -Since $mark | Out-Null
Send-Console 'setblock 41 -60 40 air'
Send-Console 'setblock 40 -60 40 air'
Send-Console 'tickingarea remove cm_mechanism_e2e'
