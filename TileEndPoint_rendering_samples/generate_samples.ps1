param(
    [double]$Latitude = 46.5405,
    [double]$Longitude = 12.1357,
    [int]$Zoom = 13,
    [int]$Radius = 1
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

$outDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$tileSize = 256
$userAgent = "GPXLister-doc-sample/1.0"

$providers = @(
    @{
        Name = "Esri_World_Street_Map_standard_street_map"
        Endpoint = "https://server.arcgisonline.com/ArcGIS/rest/services/World_Street_Map/MapServer/tile/{z}/{y}/{x}"
        Attribution = "Esri, OpenStreetMap contributors, and data providers"
    },
    @{
        Name = "OpenTopoMap_topographic_contours_hillshade"
        Endpoint = "https://a.tile.opentopomap.org/{z}/{x}/{y}.png"
        Attribution = "OpenTopoMap, OpenStreetMap contributors, SRTM"
    },
    @{
        Name = "CARTO_Voyager_clean_street_map"
        Endpoint = "https://a.basemaps.cartocdn.com/rastertiles/voyager/{z}/{x}/{y}.png"
        Attribution = "CARTO, OpenStreetMap contributors"
    },
    @{
        Name = "Esri_World_Imagery_satellite"
        Endpoint = "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}"
        Attribution = "Esri and imagery data providers"
    },
    @{
        Name = "Esri_World_Topographic_Map"
        Endpoint = "https://server.arcgisonline.com/ArcGIS/rest/services/World_Topo_Map/MapServer/tile/{z}/{y}/{x}"
        Attribution = "Esri, OpenStreetMap contributors, and data providers"
    }
)

function Get-TileX([double]$lon, [int]$z) {
    $n = [math]::Pow(2, $z)
    return [int][math]::Floor((($lon + 180.0) / 360.0) * $n)
}

function Get-TileY([double]$lat, [int]$z) {
    $latRad = $lat * [math]::PI / 180.0
    $n = [math]::Pow(2, $z)
    return [int][math]::Floor((1.0 - ([math]::Log([math]::Tan($latRad) + (1.0 / [math]::Cos($latRad))) / [math]::PI)) / 2.0 * $n)
}

function Expand-Endpoint([string]$endpoint, [int]$z, [int]$x, [int]$y) {
    return $endpoint.Replace("{z}", [string]$z).Replace("{x}", [string]$x).Replace("{y}", [string]$y)
}

function Add-Label([System.Drawing.Bitmap]$bitmap, [string]$title, [string]$subtitle) {
    $g = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $font = New-Object System.Drawing.Font("Segoe UI", 14, [System.Drawing.FontStyle]::Bold)
        $smallFont = New-Object System.Drawing.Font("Segoe UI", 9, [System.Drawing.FontStyle]::Regular)
        $brush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(220, 255, 255, 255))
        $textBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(235, 20, 24, 28))
        $rect = New-Object System.Drawing.RectangleF(10, 10, ($bitmap.Width - 20), 58)
        $g.FillRectangle($brush, $rect)
        $g.DrawString($title, $font, $textBrush, 18, 15)
        $g.DrawString($subtitle, $smallFont, $textBrush, 18, 40)
    }
    finally {
        $g.Dispose()
    }
}

$centerX = Get-TileX $Longitude $Zoom
$centerY = Get-TileY $Latitude $Zoom
$grid = ($Radius * 2) + 1
$width = $grid * $tileSize
$height = $grid * $tileSize

$web = New-Object System.Net.WebClient
$web.Headers.Add("User-Agent", $userAgent)

$summary = @()
$summary += "# Tile Endpoint Rendering Samples"
$summary += ""
$summary += "Generated for the same area and zoom so the visual style differences are easy to compare."
$summary += ""
$summary += "- Center: latitude $Latitude, longitude $Longitude"
$summary += "- Zoom: $Zoom"
$summary += "- Tile grid: ${grid}x${grid} tiles, ${width}x${height} px"
$summary += "- Center tile: z $Zoom, x $centerX, y $centerY"
$summary += "- User-Agent used by generator: $userAgent"
$summary += ""
$summary += "| Image | Endpoint | Notes |"
$summary += "| --- | --- | --- |"

foreach ($provider in $providers) {
    $name = $provider.Name
    $endpoint = $provider.Endpoint
    $bitmap = New-Object System.Drawing.Bitmap($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.Clear([System.Drawing.Color]::White)

    try {
        for ($dy = -$Radius; $dy -le $Radius; $dy++) {
            for ($dx = -$Radius; $dx -le $Radius; $dx++) {
                $x = $centerX + $dx
                $y = $centerY + $dy
                $url = Expand-Endpoint $endpoint $Zoom $x $y
                $tmp = Join-Path $outDir "_tile.tmp"
                $web.DownloadFile($url, $tmp)
                $tile = [System.Drawing.Image]::FromFile($tmp)
                try {
                    $graphics.DrawImage($tile, (($dx + $Radius) * $tileSize), (($dy + $Radius) * $tileSize), $tileSize, $tileSize)
                }
                finally {
                    $tile.Dispose()
                    Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
                }
            }
        }

        Add-Label $bitmap $name ("lat $Latitude lon $Longitude z$Zoom - " + $provider.Attribution)
        $pngPath = Join-Path $outDir ($name + ".png")
        $bitmap.Save($pngPath, [System.Drawing.Imaging.ImageFormat]::Png)
        $summary += "| `"$name.png`" | ``$endpoint`` | " + $provider.Attribution + " |"
        Write-Host "Generated $pngPath"
    }
    catch {
        $summary += "| `"$name.png`" | ``$endpoint`` | Failed to fetch: $($_.Exception.Message) |"
        Write-Warning "Failed $name`: $($_.Exception.Message)"
    }
    finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

$summary += ""
$summary += "Providers that require private keys or session creation, such as MapTiler, Thunderforest, Stadia Maps, and the official Google Map Tiles API, are documented in the project README/MANUAL but are not rendered here with placeholder credentials."
$summary += ""
$summary += "These samples are for visual comparison and documentation only. Follow each provider's attribution, caching, and usage policy before redistribution."

Set-Content -Path (Join-Path $outDir "README.md") -Value $summary -Encoding UTF8
