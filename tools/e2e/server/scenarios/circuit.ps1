# U8 circuit: relayer, speaker block, crystal display, redstone illuminator, minecart with dispenser.
# The selftests cover the redstone logic and the cart; the log check makes sure the game accepted
# every pack file of the unit (block / item / entity / recipe definitions).
$r = Invoke-SelfTest 'circuit.*'
if ($r.Pass -lt 9) { throw "expected at least 9 circuit selftests to pass, got $($r.Pass)" }

$files = 'relayer', 'speaker_block', 'crystal_display', 'redstone_illuminator', 'dispenser_minecart'
$pattern = '(ERR|WRN).*(' + (($files | ForEach-Object { [regex]::Escape("$_.json") }) -join '|') + ')'
$bad = Get-ServerLog | Where-Object { $_ -match $pattern }
if ($bad) { throw "the game rejected circuit pack files:`n$($bad -join "`n")" }
