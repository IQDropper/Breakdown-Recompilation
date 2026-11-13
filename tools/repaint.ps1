# repaint.ps1 -Spec repaints.json -Src <AI png folder> -Out <touched folder> [-Only HASH]
# Hand repaints of upscaled textures (text the AI made up). For each entry in the spec: copy the AI image, apply its
# ops in order, save under the same name in -Out. Ops (coordinates in the upscaled image's pixels):
#   {"fill":  [x,y,w,h], "color":[r,g,b] | "sample":[sx,sy]}          solid fill (colour given, or taken from a pixel)
#   {"text":  "...", "box":[x,y,w,h], "font":"Yu Gothic UI", "size":px, "bold":true, "color":[r,g,b],
#    "align":"center|left|right", "valign":"center|top", "glow":[r,g,b,a], "spacing":0}   text fitted into the box
#   {"soft":  [x,y,w,h], "radius": n}                                  light blur over a box (blend new paint in)
# Each entry also records "wording": "known" (text certain) or "ours" (believable text we wrote) for the record.
#   {"orig":  [x,y,w,h]}   paste that box from the plain (Lanczos) enlargement of the ORIGINAL (-Orig folder): for
#                           text nobody can read, show the honest blur instead of letters the AI invented.
param([string]$Spec, [string]$Src, [string]$Out, [string]$Only = '', [string]$Orig = '')
Add-Type -AssemblyName System.Drawing
$entries = Get-Content $Spec -Raw -Encoding UTF8 | ConvertFrom-Json
New-Item -ItemType Directory -Force $Out | Out-Null
function Col($c, $a = 255) { [System.Drawing.Color]::FromArgb($a, [int]$c[0], [int]$c[1], [int]$c[2]) }
foreach ($e in $entries.textures) {
  if ($Only -and $e.hash -ne $Only) { continue }
  $srcFile = Get-ChildItem $Src -Filter "$($e.hash)_*" | Select-Object -First 1
  if (-not $srcFile) { Write-Output "MISSING $($e.hash)"; continue }
  $bmp = New-Object System.Drawing.Bitmap $srcFile.FullName
  $bmp2 = New-Object System.Drawing.Bitmap $bmp.Width, $bmp.Height, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $g = [System.Drawing.Graphics]::FromImage($bmp2); $g.DrawImage($bmp, 0, 0, $bmp.Width, $bmp.Height); $bmp.Dispose()
  $g.SmoothingMode = 'AntiAlias'; $g.TextRenderingHint = 'AntiAliasGridFit'; $g.InterpolationMode = 'HighQualityBicubic'
  foreach ($op in $e.ops) {
    if ($op.fill) {
      $f = $op.fill
      if ($op.sample) { $c = $bmp2.GetPixel([int]$op.sample[0], [int]$op.sample[1]); $brush = New-Object System.Drawing.SolidBrush $c }
      else { $brush = New-Object System.Drawing.SolidBrush (Col $op.color) }
      $g.FillRectangle($brush, [float]$f[0], [float]$f[1], [float]$f[2], [float]$f[3]); $brush.Dispose()
    } elseif ($op.text) {
      $b = $op.box; $style = if ($op.bold) { [System.Drawing.FontStyle]::Bold } else { [System.Drawing.FontStyle]::Regular }
      $font = New-Object System.Drawing.Font ([string]$op.font), ([float]$op.size), $style, ([System.Drawing.GraphicsUnit]::Pixel)
      $fmt = New-Object System.Drawing.StringFormat
      $fmt.Alignment = switch ($op.align) { 'left' { 'Near' } 'right' { 'Far' } default { 'Center' } }
      $fmt.LineAlignment = if ($op.valign -eq 'top') { 'Near' } else { 'Center' }
      $fmt.FormatFlags = 'NoClip, NoWrap'; $fmt.Trimming = 'None'
      $rect = New-Object System.Drawing.RectangleF ([float]$b[0]), ([float]$b[1]), ([float]$b[2]), ([float]$b[3])
      # shrink to fit the box width
      $sz = [float]$op.size
      $fitW = if ($op.rotate) { [float]$b[3] } else { [float]$b[2] }
      while ($sz -gt 4) { $m = $g.MeasureString([string]$op.text, $font); if ($m.Width -le $fitW + 2) { break }; $sz -= 0.5; $font.Dispose(); $font = New-Object System.Drawing.Font ([string]$op.font), $sz, $style, ([System.Drawing.GraphicsUnit]::Pixel) }
      if ($op.glow) { $gb = New-Object System.Drawing.SolidBrush (Col $op.glow ([int]$op.glow[3])); foreach ($d in @(@(-1,0),@(1,0),@(0,-1),@(0,1))) { $r2 = New-Object System.Drawing.RectangleF ($rect.X + $d[0]), ($rect.Y + $d[1]), $rect.Width, $rect.Height; $g.DrawString([string]$op.text, $font, $gb, $r2, $fmt) }; $gb.Dispose() }
      $tb = New-Object System.Drawing.SolidBrush (Col $op.color)
      if ($op.rotate) {
        # rotated text: the box is the area on the texture; text runs along its long side (90 = top to bottom)
        $cx = [float]$b[0] + [float]$b[2] / 2; $cy = [float]$b[1] + [float]$b[3] / 2
        $st = $g.Save(); $g.TranslateTransform($cx, $cy); $g.RotateTransform([float]$op.rotate)
        $rr = New-Object System.Drawing.RectangleF (-[float]$b[3] / 2), (-[float]$b[2] / 2), ([float]$b[3]), ([float]$b[2])
        $g.DrawString([string]$op.text, $font, $tb, $rr, $fmt); $g.Restore($st)
      } else {
        $g.DrawString([string]$op.text, $font, $tb, $rect, $fmt)
      }
      $tb.Dispose(); $font.Dispose()
    } elseif ($op.clean) {
      # remove dark marks (old letters) but keep the paper: pixels darker than the box's paper colour by more than
      # "tol" (default 18) become that colour. Paper colour = median of the box's brighter half.
      $c = $op.clean; $tol = if ($op.tol) { [int]$op.tol } else { 18 }
      $x0 = [int]$c[0]; $y0 = [int]$c[1]; $w = [int]$c[2]; $h = [int]$c[3]
      $rect = New-Object System.Drawing.Rectangle $x0, $y0, $w, $h
      $g.Flush()
      $data = $bmp2.LockBits($rect, 'ReadWrite', ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb))
      $n = $data.Stride * $h; $buf = New-Object byte[] $n
      [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $buf, 0, $n)
      $lums = New-Object System.Collections.Generic.List[int]
      for ($y = 0; $y -lt $h; $y += 2) { for ($x = 0; $x -lt $w; $x += 2) { $i = $y * $data.Stride + $x * 4; $lums.Add([int]$buf[$i] + [int]$buf[$i+1] + [int]$buf[$i+2]) } }
      # "light": true = light marks on a dark background (screens): background = a darker percentile
      $pct = if ($op.light) { 0.35 } else { 0.6 }
      $arr = $lums.ToArray(); [array]::Sort($arr); $paper = $arr[[int]($arr.Length * $pct)]
      # paper colour: average of pixels near that brightness
      $sb = 0; $sg = 0; $sr = 0; $cnt = 0
      for ($y = 0; $y -lt $h; $y += 2) { for ($x = 0; $x -lt $w; $x += 2) { $i = $y * $data.Stride + $x * 4; $l = [int]$buf[$i] + [int]$buf[$i+1] + [int]$buf[$i+2]; if ([math]::Abs($l - $paper) -le 9) { $sb += $buf[$i]; $sg += $buf[$i+1]; $sr += $buf[$i+2]; $cnt++ } } }
      if ($cnt -gt 0) { $pb = [byte]($sb / $cnt); $pg = [byte]($sg / $cnt); $pr = [byte]($sr / $cnt) } else { $pb = $pg = $pr = 220 }
      $lim = $paper - 3 * $tol; $limL = $paper + 3 * $tol
      for ($y = 0; $y -lt $h; $y++) { for ($x = 0; $x -lt $w; $x++) { $i = $y * $data.Stride + $x * 4; $l = [int]$buf[$i] + [int]$buf[$i+1] + [int]$buf[$i+2]; if ((-not $op.light -and $l -lt $lim) -or ($op.light -and $l -gt $limL)) { $buf[$i] = $pb; $buf[$i+1] = $pg; $buf[$i+2] = $pr } } }
      [System.Runtime.InteropServices.Marshal]::Copy($buf, 0, $data.Scan0, $n); $bmp2.UnlockBits($data)
    } elseif ($op.orig) {
      $of = Get-ChildItem $Orig -Filter "$($e.hash)_*" | Select-Object -First 1
      if (-not $of) { Write-Output "  no -Orig image for $($e.hash): orig op skipped"; continue }
      $ob = New-Object System.Drawing.Bitmap $of.FullName
      $r = New-Object System.Drawing.Rectangle ([int]$op.orig[0]), ([int]$op.orig[1]), ([int]$op.orig[2]), ([int]$op.orig[3])
      $g.SetClip($r); $g.CompositingMode = 'SourceCopy'
      $g.DrawImage($ob, 0, 0, $bmp2.Width, $bmp2.Height)
      $g.CompositingMode = 'SourceOver'; $g.ResetClip(); $ob.Dispose()
    } elseif ($op.soft) {
      $s = $op.soft; $rad = if ($op.radius) { [int]$op.radius } else { 1 }
      $crop = $bmp2.Clone((New-Object System.Drawing.Rectangle ([int]$s[0]), ([int]$s[1]), ([int]$s[2]), ([int]$s[3])), $bmp2.PixelFormat)
      $small = New-Object System.Drawing.Bitmap ([math]::Max(1, [int]($s[2] / (1 + $rad)))), ([math]::Max(1, [int]($s[3] / (1 + $rad))))
      $gs = [System.Drawing.Graphics]::FromImage($small); $gs.InterpolationMode = 'HighQualityBilinear'; $gs.DrawImage($crop, 0, 0, $small.Width, $small.Height); $gs.Dispose()
      $g.DrawImage($small, (New-Object System.Drawing.Rectangle ([int]$s[0]), ([int]$s[1]), ([int]$s[2]), ([int]$s[3]))); $small.Dispose(); $crop.Dispose()
    }
  }
  $g.Dispose()
  $dst = Join-Path $Out $srcFile.Name
  $bmp2.Save($dst, [System.Drawing.Imaging.ImageFormat]::Png); $bmp2.Dispose()
  Write-Output "repainted $($e.hash) ($($e.what)) wording=$($e.wording)"
}
