# U5 blackboard: block data, drawing, waxing, drops, placement, editor sessions (selftests), plus
# the console channel of the edit screen.
$r = Invoke-SelfTest 'blackboard.*'
if ($r.Pass -lt 10) { throw "expected at least 10 blackboard selftests to pass, got $($r.Pass)" }

# The block exists and the console can set a drawing (the command the client screen uses).
Send-Console 'tickingarea add 0 -64 0 15 0 15 cm_bb true'
Wait-Seconds 3
Send-Console 'setblock 0 -50 0 complementaries:blackboard'
Wait-Seconds 1
$hex = ('1' * 16) + ('0' * 224) + ('1' * 16)
$mark = Get-LogMark
Send-Console "supplementaries blackboard set 0 -50 0 `"$hex`""
Wait-LogLine 'blackboard: pixels set at 0:0:-50:0' 15 -Since $mark | Out-Null
$mark = Get-LogMark
Send-Console 'supplementaries blackboard set 0 -50 0 "xyz"'
Wait-LogLine '256 hex digits expected' 15 -Since $mark | Out-Null
Send-Console 'setblock 0 -50 0 air'
Send-Console 'tickingarea remove cm_bb'
Wait-Seconds 1
