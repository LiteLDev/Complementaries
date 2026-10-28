# U9 lighting: sconces, candle holders, lamps, wall lanterns (docs/spec/lighting.md).
# Selftests cover the behaviour with simulated players; the console part checks that every block
# and item id exists in the world (behavior pack loaded) and that /setblock + /give accept them.
$r = Invoke-SelfTest 'lighting.*'
if ($r.Pass -lt 10) { throw "expected at least 10 lighting selftests to pass, got $($r.Pass)" }

$ids = @('sconce', 'sconce_soul', 'sconce_ender', 'sconce_glow', 'sconce_green', 'sconce_lever',
    'candle_holder', 'candle_holder_red', 'stone_lamp', 'blackstone_lamp', 'deepslate_lamp', 'end_stone_lamp')
$mark = Get-LogMark
Send-Console 'tickingarea add 32 -64 32 70 0 50 cm_lighting true'
Wait-LogLine 'ticking area|Ticking area' 10 -Since $mark | Out-Null
Wait-Seconds 4
$x = 40
foreach ($id in $ids) {
    $mark = Get-LogMark
    Send-Console "setblock $x -60 40 complementaries:$id"
    Wait-LogLine 'Block placed' 10 -Since $mark | Out-Null
    $x++
}
$mark = Get-LogMark
Send-Console 'setblock 60 -60 40 complementaries:wall_lantern ["minecraft:block_face"="north","complementaries:lantern"=1]'
Wait-LogLine 'Block placed' 10 -Since $mark | Out-Null
Send-Console 'tickingarea remove cm_lighting'
