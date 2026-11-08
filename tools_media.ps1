# Renders the Nexus media for RDR2 DSX Rumble into .\media
#   powershell -ExecutionPolicy Bypass -File tools_media.ps1 -version v1.5
param([string]$version = 'v1.5')
Add-Type -AssemblyName System.Drawing
$out = Join-Path $PSScriptRoot 'media'
New-Item -ItemType Directory -Force $out | Out-Null

function C($a, $r, $g, $b) { [System.Drawing.Color]::FromArgb($a, $r, $g, $b) }
$cream  = New-Object System.Drawing.SolidBrush (C 255 245 230 200)
$dim    = New-Object System.Drawing.SolidBrush (C 255 215 185 150)
$amber  = New-Object System.Drawing.SolidBrush (C 255 250 175 80)
$shadow = New-Object System.Drawing.SolidBrush (C 170 0 0 0)
$panel  = New-Object System.Drawing.SolidBrush (C 120 15 5 3)
$ground = New-Object System.Drawing.SolidBrush (C 255 18 7 5)

function New-Canvas($w, $h) {
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'; $g.TextRenderingHint = 'AntiAliasGridFit'
    return @($bmp, $g)
}

# dusk sky + sun + rumble rings + mesa horizon. $sx,$sy = sun centre, $sr = sun radius, $hz = horizon y
function Draw-Scene($g, $w, $h, $sx, $sy, $sr, $hz, [bool]$rings = $true) {
    $rect = New-Object System.Drawing.Rectangle 0, 0, $w, $h
    $sky = New-Object System.Drawing.Drawing2D.LinearGradientBrush $rect, (C 255 0 0 0), (C 255 0 0 0), 90
    $blend = New-Object System.Drawing.Drawing2D.ColorBlend 4
    $blend.Colors = @((C 255 30 6 6), (C 255 120 22 12), (C 255 215 95 25), (C 255 25 10 6))
    $blend.Positions = @(0.0, ([double]$hz / $h * 0.6), ([double]$hz / $h), 1.0)
    $sky.InterpolationColors = $blend
    $g.FillRectangle($sky, $rect)
    $glow = [int]($sr * 2.8)
    for ($r = $glow; $r -gt 0; $r -= 4) {
        $a = [int](60 * (1 - $r / [double]$glow))
        $g.FillEllipse((New-Object System.Drawing.SolidBrush (C $a 255 190 90)), $sx - $r, $sy - $r, 2 * $r, 2 * $r)
    }
    $g.FillEllipse((New-Object System.Drawing.SolidBrush (C 255 255 200 120)), $sx - $sr, $sy - $sr, 2 * $sr, 2 * $sr)
    if ($rings) {
        for ($i = 1; $i -le 6; $i++) {
            $r = $sr + $i * $sr * 0.47
            $pen = New-Object System.Drawing.Pen (C ([int](170 - $i * 24)) 255 220 160), ([single]([Math]::Max(1, (9 - $i) * $sr / 150)))
            $g.DrawArc($pen, [single]($sx - $r), [single]($sy - $r), [single](2 * $r), [single](2 * $r), 200, 140)
        }
    }
    $k = $w / 1920.0
    $pts = @(@(0, 70), @(160, 60), @(260, 0), @(420, -5), @(470, 40), @(640, 50), @(700, 20), @(780, 30), @(1150, 40),
             @(1220, -30), @(1420, -38), @(1470, 30), @(1600, 45), @(1760, 20), @(1920, 30))
    $poly = New-Object System.Collections.Generic.List[System.Drawing.PointF]
    foreach ($p in $pts) { $poly.Add((New-Object System.Drawing.PointF ([single]($p[0] * $k)), ([single]($hz + $p[1] * $k)))) }
    $poly.Add((New-Object System.Drawing.PointF $w, $h)); $poly.Add((New-Object System.Drawing.PointF 0, $h))
    $g.FillPolygon($ground, $poly.ToArray())
}

function Text($g, $s, $font, $brush, $x, $y, $w, $h, $align = 'Near', [bool]$shade = $false) {
    $sf = New-Object System.Drawing.StringFormat; $sf.Alignment = $align
    if ($shade) { $g.DrawString($s, $font, $shadow, (New-Object System.Drawing.RectangleF ($x + 4), ($y + 4), $w, $h), $sf) }
    $g.DrawString($s, $font, $brush, (New-Object System.Drawing.RectangleF $x, $y, $w, $h), $sf)
}
function F($size, $style = 'Regular', $fam = 'Georgia') { New-Object System.Drawing.Font $fam, $size, ([System.Drawing.FontStyle]$style) }
function Save($bmp, $g, $name) { $bmp.Save((Join-Path $out $name), [System.Drawing.Imaging.ImageFormat]::Png); $g.Dispose(); $bmp.Dispose(); "saved $name" }
function Panel($g, $x, $y, $w, $h) {
    $g.FillRectangle($panel, $x, $y, $w, $h)
    $g.DrawRectangle((New-Object System.Drawing.Pen (C 120 245 230 200), 2), $x, $y, $w, $h)
}

# ---------------------------------------------------------------------------- 1. header banner 1300x372
$c = New-Canvas 1300 372; $bmp = $c[0]; $g = $c[1]
Draw-Scene $g 1300 372 1010 300 70 300
Text $g 'DSX RUMBLE' (F 84 'Bold') $cream 60 70 900 120 'Near' $true
$g.DrawLine((New-Object System.Drawing.Pen (C 200 245 230 200), 2), 66, 190, 640, 190)
Text $g 'Feel every step, hoofbeat and shot on your DualSense' (F 24 'Italic') $cream 64 200 900 50
Text $g 'Rumble  -  Adaptive triggers  -  Lightbar' (F 17 'Bold' 'Segoe UI') $dim 66 250 900 40
Text $g $version (F 18 'Bold' 'Segoe UI') $cream 0 330 1280 40 'Far'
Save $bmp $g 'banner_1300x372.png'

# ---------------------------------------------------------------------------- 2. cover 1920x1080
$c = New-Canvas 1920 1080; $bmp = $c[0]; $g = $c[1]
Draw-Scene $g 1920 1080 960 760 150 800
Text $g 'DSX RUMBLE' (F 150 'Bold') $cream 0 120 1920 220 'Center' $true
$g.DrawLine((New-Object System.Drawing.Pen (C 200 245 230 200), 3), 560, 320, 1360, 320)
Text $g 'Feel every step, hoofbeat and shot on your DualSense' (F 46 'Italic') $cream 0 330 1920 80 'Center'
Text $g $version (F 30 'Bold' 'Segoe UI') $cream 0 1010 1880 60 'Far'
Save $bmp $g '1_cover.png'

# ---------------------------------------------------------------------------- 3. rumble feature grid
function Grid($title, $sub, $items, $name, $cols = 3, $maxCh = 170) {
    $c = New-Canvas 1920 1080; $bmp = $c[0]; $g = $c[1]
    Draw-Scene $g 1920 1080 1600 980 120 1000 $false
    Text $g $title (F 72 'Bold') $cream 0 40 1920 110 'Center' $true
    Text $g $sub (F 28 'Italic') $dim 0 145 1920 50 'Center'
    $rows = [Math]::Ceiling($items.Count / $cols)
    $cw = 560; $gap = 30; $x0 = (1920 - ($cols * $cw + ($cols - 1) * $gap)) / 2
    $ch = [Math]::Min($maxCh, (820 - 210 - ($rows - 1) * 22) / $rows); $y0 = 220
    for ($i = 0; $i -lt $items.Count; $i++) {
        $x = $x0 + ($i % $cols) * ($cw + $gap); $y = $y0 + [Math]::Floor($i / $cols) * ($ch + 22)
        Panel $g $x $y $cw $ch
        Text $g $items[$i][0] (F 30 'Bold') $amber ($x + 24) ($y + 14) ($cw - 40) 46
        Text $g $items[$i][1] (F ([Math]::Min(26, 19 * $maxCh / 170)) 'Regular' 'Segoe UI') $cream ($x + 24) ($y + 60) ($cw - 44) ($ch - 66)
    }
    Text $g $version (F 24 'Bold' 'Segoe UI') $cream 0 1020 1880 50 'Far'
    Save $bmp $g $name
}

Grid 'Rumble that makes sense' 'Every effect is tuned to how it would feel - snappy where it should be, soft where it should not' @(
    @('Footsteps', 'One tap per real step, read from the foot bones. Softer, longer splashes in water.'),
    @('Horse', 'Hoofbeats that follow the gait: walk, trot, canter, gallop. Spur kicks, ledge drops, crashes.'),
    @('Gunshots', 'A crack plus a recoil kick sized to the gun - from the .22 tap to the Elephant Rifle.'),
    @('Fistfights', 'A hard smack when you take a punch, a knuckle thud when yours lands.'),
    @('Animals', 'Dog and wolf bites that tug, bear mauls, bull rams - and the growl when a bear roars.'),
    @('Water and swimming', 'Rivers push against you, still water only stirs. Rhythmic swim strokes.'),
    @('World and cutscenes', 'Door slams, carrying bodies, sliding, falls, wagons. Cutscene gunfights too.'),
    @('F11 settings menu', 'Tune every effect in game - mouse, keyboard or controller.'),
    @('Health', 'A heartbeat at low health, a jolt for every wound. Light clicks in menus.')
) '2_rumble.png'

# ---------------------------------------------------------------------------- 4. adaptive triggers chart
$c = New-Canvas 1920 1080; $bmp = $c[0]; $g = $c[1]
Draw-Scene $g 1920 1080 260 980 120 1000 $false
Text $g 'Adaptive triggers' (F 72 'Bold') $cream 0 40 1920 110 'Center' $true
Text $g 'R2 breaks like a real trigger sear  -  L2 carries the weight of the gun' (F 28 'Italic') $dim 0 145 1920 50 'Center'
$guns = @(
    @('.22 Varmint Rifle', 2, 0), @('Volcanic / Navy', 3, 0), @('Semi-auto pistols', 4, 1), @('Cattleman (single action)', 5, 1),
    @('Double-Action (long pull)', 6, 1), @('Schofield / LeMat', 6, 1), @('Lever-action repeaters', 5, 1), @('Springfield / Bolt-Action', 6, 2),
    @('Rolling Block / Carcano', 7, 2), @('Pump and semi-auto shotguns', 6, 2), @('Double-barrel / Sawed-off', 8, 2), @('Elephant Rifle', 8, 3))
Panel $g 230 220 1460 750
Text $g 'R2 trigger pull' (F 22 'Bold' 'Segoe UI') $amber 900 236 400 36
Text $g 'L2 aim weight' (F 22 'Bold' 'Segoe UI') $amber 1370 236 300 36
for ($i = 0; $i -lt $guns.Count; $i++) {
    $y = 284 + $i * 52
    Text $g $guns[$i][0] (F 24 'Regular' 'Segoe UI') $cream 260 $y 620 40
    for ($s = 0; $s -lt 8; $s++) {
        $b = if ($s -lt $guns[$i][1]) { $amber } else { New-Object System.Drawing.SolidBrush (C 70 245 230 200) }
        $g.FillRectangle($b, 900 + $s * 52, $y + 8, 44, 24)
    }
    for ($s = 0; $s -lt 3; $s++) {
        $b = if ($s -lt $guns[$i][2]) { $cream } else { New-Object System.Drawing.SolidBrush (C 70 245 230 200) }
        $g.FillRectangle($b, 1370 + $s * 52, $y + 8, 44, 24)
    }
}
Text $g 'Plus: bow draw  -  lasso tension  -  galloping hooves  -  loose in menus and outside the game' (F 22 'Italic') $cream 0 925 1920 40 'Center'
Text $g $version (F 24 'Bold' 'Segoe UI') $cream 0 1030 1880 40 'Far'
Save $bmp $g '3_triggers.png'
