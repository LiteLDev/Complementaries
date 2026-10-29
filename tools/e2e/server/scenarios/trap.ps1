# U17 trap: bamboo spikes, tipped spikes, cage and fodder selftests, plus the blocks and items
# being registered by the behavior pack (give / setblock through the console).
$r = Invoke-SelfTest 'trap.*'
if ($r.Pass -lt 9) { throw "expected at least 9 trap selftests to pass, got $($r.Pass)" }

foreach ($id in 'bamboo_spikes', 'bamboo_spikes_tipped', 'cage', 'fodder') {
    $mark = Get-LogMark
    Send-Console "give @a complementaries:$id 1"
    # no player is online: the command answers "No targets matched selector"; an unknown item
    # would be a syntax error instead
    $line = Wait-LogLine '(No targets matched|Gave |Syntax error|Unknown|not found)' 15 -Since $mark
    if ($line -match 'Syntax error|Unknown|not found') { throw "item complementaries:$id is not registered: $line" }
}

$mark = Get-LogMark
Send-Console 'setblock 0 -50 0 complementaries:fodder ["complementaries:eaten"=5]'
$line = Wait-LogLine '(Block placed|Syntax error|Unknown|Cannot place|out of the world|not loaded)' 15 -Since $mark
if ($line -match 'Syntax error|Unknown') { throw "fodder block state is not registered: $line" }
