# U14 flag: flags (cloth entity, patterns, drops, pole), awnings (placement, slant, bounce) and
# buntings (ceiling / wall, support loss). Logic lives in the `flag.*` selftests.
$r = Invoke-SelfTest 'flag.*'
if ($r.Pass -lt 8) { throw "expected at least 8 flag selftests to pass, got $($r.Pass)" }

# The blocks, items and the helper entity are registered by the behavior pack: commands use them.
Send-Console 'give @a complementaries:flag_red 2'
Send-Console 'give @a complementaries:awning_white 4'
Send-Console 'give @a complementaries:bunting_blue 6'
Send-Console 'give @a complementaries:dragon_banner_pattern 1'
Send-Console 'tickingarea add circle -20 -55 25 1 cm_flag_e2e'
Wait-Seconds 3
$mark = Get-LogMark
Send-Console 'setblock -20 -55 25 complementaries:flag_yellow'
Wait-LogLine 'flag: flag placed at -20 -55 25 with 0 pattern' 20 -Since $mark | Out-Null
Send-Console 'setblock -20 -55 25 air'
Send-Console 'setblock -18 -55 25 complementaries:awning_red ["complementaries:facing"=2,"complementaries:bottom"=1,"complementaries:slanted"=1]'
Send-Console 'setblock -16 -55 25 complementaries:bunting_wall_green ["complementaries:facing"=1]'
Wait-Seconds 2
$bad = Get-ServerLog -Since $mark | Where-Object { $_ -match 'Unknown block|Syntax error|is not a valid|could not spawn the cloth' }
if ($bad) { throw "flag unit blocks failed: $($bad -join ' | ')" }
Send-Console 'tickingarea remove cm_flag_e2e'
