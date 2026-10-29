# U16 tool: the tool.* selftests (slingshot, bubble blower and bubble block, soap, wrench, flute,
# confetti popper, antique ink, slice map) plus a console check that the behavior pack block loads.
$r = Invoke-SelfTest 'tool.*'
if ($r.Pass -lt 15) { throw "expected 15 tool selftests to pass, got $($r.Pass)" }

Send-Console 'tickingarea add 0 0 0 15 0 15 cm_tool_e2e'
Wait-Seconds 3
$mark = Get-LogMark
Send-Console 'setblock 8 0 8 complementaries:bubble_block'
Wait-LogLine 'Block placed' 15 -Since $mark | Out-Null
Send-Console 'setblock 8 0 8 air'
Send-Console 'tickingarea remove cm_tool_e2e'
