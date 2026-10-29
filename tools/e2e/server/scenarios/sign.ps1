# U4 sign: sign posts, notice boards, doormats and way signs (selftests: data, arms, posts,
# player interaction through a simulated player, notice board pages + redstone, doormat, way sign).
$r = Invoke-SelfTest 'sign.*'
if ($r.Pass -lt 7) { throw "expected 7 sign selftests to pass, got $($r.Pass)" }
