#requires -Version 7.3
param([Parameter(Mandatory = $true)][string]$Executable)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$exe = (Resolve-Path -LiteralPath $Executable).Path
$dir = Join-Path ([IO.Path]::GetTempPath()) "mythroad-native-$([Guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $dir -ErrorAction Stop | Out-Null
$files = [System.Collections.Generic.List[string]]::new()

function New-Source([string]$Name, [string]$Source) {
    $path = Join-Path $dir $Name
    [IO.File]::WriteAllText($path, $Source, [Text.UTF8Encoding]::new($false))
    $files.Add($path)
    return $path
}

function Run-App([string[]]$Arguments) {
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $exe
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($arg in $Arguments) { $info.ArgumentList.Add($arg) }
    $process = [Diagnostics.Process]::Start($info)
    try {
        if (-not $process.WaitForExit(15000)) {
            Stop-Process -Id $process.Id -Force
            throw 'Native app timed out.'
        }
        return @{
            ExitCode = $process.ExitCode
            Stdout = $process.StandardOutput.ReadToEnd()
            Stderr = $process.StandardError.ReadToEnd()
        }
    } finally {
        $process.Dispose()
    }
}

function Assert-Result($Result, [int]$ExitCode, [string]$Output, [string]$ErrorText) {
    $unexpectedStderr = $Result.Stderr -replace '(?m)^libpng warning: iCCP: cHRM chunk does not match sRGB\r?\n?', ''
    if ($Result.ExitCode -ne $ExitCode -or $Result.Stdout -notlike "*$Output*" -or
        $Result.Stderr -notlike "*$ErrorText*" -or
        ($ErrorText -eq '' -and $unexpectedStderr -ne '')) {
        throw "Unexpected result: exit=$($Result.ExitCode); stdout=$($Result.Stdout); stderr=$($Result.Stderr)"
    }
}

try {
    $example = Join-Path $root 'tools\mrp_lua_guide_example.mr'
    $result = Run-App @('--check', '--event', '0', '18', '0', $example)
    Assert-Result $result 0 'GUIDE|press|18|1' ''
    if ($result.Stdout -notlike '*GUIDE|ready*') { throw 'Example startup did not run.' }
    Write-Host 'PASS: existing source example, drawing calls and key event.'

    $unicode = New-Source '中文.mr' '_drawText("中文", 0, 0, 255, 255, 255)'
    Assert-Result (Run-App @('--check', $unicode)) 0 '' ''
    $invalidUtf8 = Join-Path $dir 'invalid-utf8.mr'
    $bytes = [Text.Encoding]::ASCII.GetBytes('_drawText("') + [byte[]]@(255) +
        [Text.Encoding]::ASCII.GetBytes('", 0, 0, 255, 255, 255)')
    [IO.File]::WriteAllBytes($invalidUtf8, $bytes)
    $files.Add($invalidUtf8)
    Assert-Result (Run-App @('--check', $invalidUtf8)) 1 '' '_drawText received invalid UTF-8'
    Write-Host 'PASS: UTF-8 text accepted and malformed text rejected.'

    $timer = New-Source 'timer.mr' @'
def old()
  print("TIMER|wrong")
end
def tick()
  print("TIMER|ok")
  _exit()
end
_timerStart(0, 1000, "old")
_timerStart(0, 20, "tick")
'@
    $result = Run-App @($timer)
    Assert-Result $result 0 'TIMER|ok' ''
    if ($result.Stdout -like '*TIMER|wrong*') { throw 'Replaced timer fired.' }
    Write-Host 'PASS: Windows event loop and replaced timer.'

    $snakePath = Join-Path $root 'examples\mythroad-native\snake.mr'
    $snake = [IO.File]::ReadAllText($snakePath, [Text.Encoding]::UTF8)
    $snakeChecks = @'
def expect(ok, name)
  if !ok then
    print("SNAKE|FAIL|" .. name)
    missing()
  end
end
expect(state == 1 && count == 3 && score == 0, "initial")
dealevent(1, 20, 0)
expect(state == 1, "key release")
dealevent(0, 20, 0)
tick()
expect(snake_x[1] == 4 && snake_y[1] == 3, "first step")
tick()
expect(snake_x[1] == 5 && score == 1 && count == 4, "food growth")
expect(food_x == 5 && food_y == 8, "next food")
local i = 1
while i <= count do
  expect(food_x != snake_x[i] || food_y != snake_y[i], "food outside snake")
  i = i + 1
end
dealevent(0, 14, 0)
expect(next_dx == 1 && next_dy == 0, "reverse blocked")
dealevent(0, 12, 0)
dealevent(0, 14, 0)
tick()
expect(snake_x[1] == 5 && snake_y[1] == 2, "queued turn")
dealevent(0, 17, 0)
tick()
expect(state == 1 && snake_y[1] == 2, "pause")
dealevent(0, 17, 0)
dealevent(0, 13, 0)
tick()
expect(snake_y[1] == 1, "resume and reverse blocked")
tick()
expect(state == 2, "wall collision")
dealevent(0, 18, 0)
expect(state == 1 && score == 0 && best == 1 && count == 3, "restart")

score = 3
food_x = 4
food_y = 3
state = 0
tick()
expect(score == 4 && best == 4 && level == 2 && delay == 225, "level up")
score = 23
level = 6
delay = 125
next_level_score = 24
food_x = 5
food_y = 3
tick()
expect(score == 24 && level == 7 && delay == 100, "maximum speed")
score = 27
food_x = 6
food_y = 3
tick()
expect(score == 28 && level == 7 && delay == 100, "speed stays capped")
dealevent(0, 18, 0)
expect(score == 0 && best == 28 && level == 1 && delay == 250, "restart retains best")

food_cursor = 23
place_food()
expect(food_x == 4 && food_y == 3, "food skips occupied cells")
snake_x = {10}
snake_y = {12}
count = 1
food_cursor = 120
place_food()
expect(food_x == 1 && food_y == 1, "food scan wraps")

snake_x = {3, 2, 2, 3}
snake_y = {3, 3, 2, 2}
count = 4
dx = 1
dy = 0
next_dx = 0
next_dy = -1
food_x = 10
food_y = 10
state = 0
tick()
expect(state == 0 && snake_x[1] == 3 && snake_y[1] == 2, "vacated tail")
snake_x = {3, 2, 2, 3, 4}
snake_y = {3, 3, 2, 2, 2}
count = 5
dx = 1
dy = 0
next_dx = 0
next_dy = -1
state = 0
tick()
expect(state == 2, "body collision")

snake_x = {9}
snake_y = {12}
count = 1
local y = 1
while y <= 12 do
  local x = 1
  while x <= 10 do
    if !(x == 9 && y == 12) && !(x == 10 && y == 12) then
      count = count + 1
      snake_x[count] = x
      snake_y[count] = y
    end
    x = x + 1
  end
  y = y + 1
end
expect(count == 119, "full board setup")
dx = 1
dy = 0
next_dx = 1
next_dy = 0
food_x = 10
food_y = 12
state = 0
tick()
expect(state == 3 && count == 120 && food_x == 0, "full board win")
print("SNAKE|logic|ok")
'@
    $snakeTest = New-Source 'snake-test.mr' ($snake + "`n" + $snakeChecks)
    Assert-Result (Run-App @('--check', $snakeTest)) 0 'SNAKE|logic|ok' ''

    $snakeTimer = @'
real_tick = tick
def tick()
  real_tick()
  if snake_x[1] != 4 || snake_y[1] != 3 then
    missing()
  end
  print("SNAKE|timer|ok")
  _exit()
end
dealevent(0, 20, 0)
'@
    $snakeTimerTest = New-Source 'snake-timer.mr' ($snake + "`n" + $snakeTimer)
    Assert-Result (Run-App @($snakeTimerTest)) 0 'SNAKE|timer|ok' ''
    Write-Host 'PASS: snake movement, food, levels, collisions, restart, win and timer.'

    $badSyntax = New-Source 'syntax.mr' 'def broken('
    Assert-Result (Run-App @('--check', $badSyntax)) 1 '' 'compile failed'
    $badApi = New-Source 'api.mr' '_clearScr(256, 0, 0)'
    Assert-Result (Run-App @('--check', $badApi)) 1 '' 'color components must be between 0 and 255'
    $extraArgs = New-Source 'args.mr' '_drawText("x", 0, 0, 0, 0, 0, 1)'
    Assert-Result (Run-App @('--check', $extraArgs)) 1 '' '_drawText expects text'
    $badRect = New-Source 'bad-rect.mr' '_drawRect(32768, 0, 1, 1, 0, 0, 0)'
    Assert-Result (Run-App @('--check', $badRect)) 1 '' 'drawing coordinates and dimensions must fit in int16'
    $badLine = New-Source 'bad-line.mr' '_drawLine(0, 0, 1, 1, 0, 256, 0)'
    Assert-Result (Run-App @('--check', $badLine)) 1 '' 'color components must be between 0 and 255'
    $badPoint = New-Source 'bad-point.mr' '_drawPoint(0, 0, 0, 0)'
    Assert-Result (Run-App @('--check', $badPoint)) 1 '' '_drawPoint expects'
    $badEvent = New-Source 'event.mr' 'def dealevent(code, p0, p1) missing() end'
    Assert-Result (Run-App @('--check', '--event', '0', '18', '0', $badEvent)) 1 '' 'event failed'
    $badTimer = New-Source 'missing-timer.mr' '_timerStart(0, 20, "missing")'
    Assert-Result (Run-App @($badTimer)) 1 '' 'timer callback not found'
    $unsupported = New-Source 'unsupported.mr' '_strCom(800, "", 0)'
    Assert-Result (Run-App @('--check', $unsupported)) 1 '' 'startup failed'
    $ext = Join-Path $root 'docs\research\mrc\asm\cfunction.ext'
    Assert-Result (Run-App @('--check', $ext)) 1 '' 'only source text is supported'
    Write-Host 'PASS: compile, API, event, timer and EXT errors are surfaced.'
} finally {
    foreach ($file in $files) { Remove-Item -LiteralPath $file -Force -ErrorAction SilentlyContinue }
    Remove-Item -LiteralPath $dir -Force -ErrorAction SilentlyContinue
}
