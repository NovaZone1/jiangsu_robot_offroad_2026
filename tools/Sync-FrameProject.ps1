param([string]$ProjectPath = (Join-Path $PSScriptRoot '..\MDK-ARM\jiangsu_robot_offroad_2026.uvprojx'))
$ErrorActionPreference = 'Stop'
$taskProjectPath = (Resolve-Path -LiteralPath $ProjectPath).Path
$taskMdk = Split-Path -Parent $taskProjectPath
$taskRoot = Split-Path -Parent $taskMdk
[xml]$taskXml = [System.IO.File]::ReadAllText($taskProjectPath)
foreach ($taskTarget in $taskXml.Project.Targets.Target) {
    $taskTarget.uAC6 = '1'
    $taskControl = $taskTarget.TargetOption.TargetArmAds.Cads.VariousControls
    $taskDefines = @($taskControl.Define -split '[,;]' | Where-Object { $_ })
    foreach ($taskDefine in @('HAL_ADC_MODULE_ENABLED','HAL_UART_MODULE_ENABLED','ARM_MATH_CM3')) {
        if ($taskDefines -notcontains $taskDefine) { $taskDefines += $taskDefine }
    }
    $taskControl.Define = $taskDefines -join ','
    $taskIncludes = @($taskControl.IncludePath.Replace('portable/RVDS/ARM_CM3','portable/GCC/ARM_CM3') -split ';' | Where-Object { $_ })
    foreach ($taskLayer in @('Bsps','Mods','Sys','Apps','Algorithm')) {
        $taskInclude = "../FrameComponets/$taskLayer/Inc"
        if ($taskIncludes -notcontains $taskInclude) { $taskIncludes += $taskInclude }
    }
    $taskControl.IncludePath = $taskIncludes -join ';'
    foreach ($taskFile in $taskTarget.Groups.Group.Files.File) {
        if ($taskFile.FileName -eq 'port.c') {
            $taskFile.FilePath = '../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM3/port.c'
        }
    }
    foreach ($taskLayer in @('Bsps','Mods','Sys','Apps','Algorithm')) {
        $taskGroupName = "Frame/$taskLayer"
        foreach ($taskOld in @($taskTarget.Groups.Group | Where-Object { $_.GroupName -eq $taskGroupName })) {
            [void]$taskTarget.Groups.RemoveChild($taskOld)
        }
        $taskGroup = $taskXml.CreateElement('Group')
        $taskNameNode = $taskXml.CreateElement('GroupName')
        $taskNameNode.InnerText = $taskGroupName
        [void]$taskGroup.AppendChild($taskNameNode)
        $taskFilesNode = $taskXml.CreateElement('Files')
        foreach ($taskSource in (Get-ChildItem -LiteralPath "$taskRoot\FrameComponets\$taskLayer\Src" -File | Sort-Object Name)) {
            if ($taskSource.Extension -notin @('.c','.cpp')) { continue }
            $taskFileNode = $taskXml.CreateElement('File')
            $taskType = if ($taskSource.Extension -eq '.cpp') { '8' } else { '1' }
            foreach ($taskField in @(@('FileName',$taskSource.Name),@('FileType',$taskType),@('FilePath',"../FrameComponets/$taskLayer/Src/$($taskSource.Name)"))) {
                $taskFieldNode = $taskXml.CreateElement($taskField[0])
                $taskFieldNode.InnerText = $taskField[1]
                [void]$taskFileNode.AppendChild($taskFieldNode)
            }
            [void]$taskFilesNode.AppendChild($taskFileNode)
        }
        [void]$taskGroup.AppendChild($taskFilesNode)
        [void]$taskTarget.Groups.AppendChild($taskGroup)
    }
    $taskHalGroup = $taskTarget.Groups.Group | Where-Object { $_.GroupName -eq 'Drivers/STM32F1xx_HAL_Driver' } | Select-Object -First 1
    if (!$taskHalGroup) {
        $taskHalGroup = $taskTarget.Groups.Group | Where-Object { $_.Files.File.FileName -contains 'stm32f1xx_hal.c' } | Select-Object -First 1
    }
    if (!$taskHalGroup) { throw 'Cannot find STM32F1 HAL source group.' }
    foreach ($taskDriver in @('stm32f1xx_hal_uart.c','stm32f1xx_hal_adc.c','stm32f1xx_hal_adc_ex.c')) {
        if ($taskHalGroup.Files.File.FileName -contains $taskDriver) { continue }
        $taskFileNode = $taskXml.CreateElement('File')
        foreach ($taskField in @(@('FileName',$taskDriver),@('FileType','1'),@('FilePath',"../Drivers/STM32F1xx_HAL_Driver/Src/$taskDriver"))) {
            $taskFieldNode = $taskXml.CreateElement($taskField[0])
            $taskFieldNode.InnerText = $taskField[1]
            [void]$taskFileNode.AppendChild($taskFieldNode)
        }
        [void]$taskHalGroup.Files.AppendChild($taskFileNode)
    }
}
$taskSettings = [System.Xml.XmlWriterSettings]::new()
$taskSettings.Indent = $true
$taskSettings.IndentChars = '  '
$taskSettings.Encoding = [System.Text.UTF8Encoding]::new($false)
$taskWriter = [System.Xml.XmlWriter]::Create($taskProjectPath,$taskSettings)
try { $taskXml.Save($taskWriter) } finally { $taskWriter.Dispose() }
Write-Output "Synced V6 framework sources and include paths: $taskProjectPath"
