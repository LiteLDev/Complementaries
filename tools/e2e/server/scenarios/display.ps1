# U12 display: pedestal, item shelf, statue and hat stand logic (selftests), plus the items and
# blocks being registered from the behavior pack.
$r = Invoke-SelfTest 'display.*'
if ($r.Pass -lt 10) { throw "expected at least 10 display selftests to pass, got $($r.Pass)" }

$mark = Get-LogMark
Send-Console 'give @a complementaries:pedestal 1'
Send-Console 'give @a complementaries:item_shelf 1'
Send-Console 'give @a complementaries:statue 1'
Send-Console 'give @a complementaries:hat_stand 1'
Send-Console 'summon complementaries:hat_stand 0 -60 0'
Wait-Seconds 2
$log = (Get-ServerLog -Since $mark) -join "`n"
foreach ($bad in 'Unknown item', 'Unknown block', 'is not a valid', 'Unknown entity', 'No targets matched selector: complementaries') {
    if ($log -match [regex]::Escape($bad) -and $log -notmatch 'No targets matched selector: @a') { throw "registration problem: $bad" }
}
